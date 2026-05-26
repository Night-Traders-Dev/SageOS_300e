#include "vfs.h"
#include "console.h"
#include <stddef.h>
#include <string.h>
#include "metal_vm.h"
#include "vfs_bridge_bytecode.h"

/* -----------------------------------------------------------------------
 * Mount table — static array, no dynamic allocation
 * ----------------------------------------------------------------------- */

typedef struct {
    const char  *path;      /* mount point, e.g. "/" or "/fat32" */
    int          path_len;
    VfsBackend  *backend;
    int          active;
} VfsMount;

static VfsMount g_mounts[VFS_MAX_MOUNTS];
static int g_mount_count = 0;

static MetalVM g_vfs_vm;
static int g_vfs_vm_inited = 0;

/* External natives from runtime.c (or we can re-register them) */
extern int metal_vm_register_native(MetalVM* vm, const char* name, MetalNativeFn fn);
extern MetalValue n_len(MetalVM* vm, MetalValue* args, int argc);
extern MetalValue n_os_strlen(MetalVM* vm, MetalValue* args, int argc);
extern MetalValue n_os_starts_with(MetalVM* vm, MetalValue* args, int argc);
extern MetalValue n_os_array_len(MetalVM* vm, MetalValue* args, int argc);
extern MetalValue n_os_array_push(MetalVM* vm, MetalValue* args, int argc);
extern MetalValue n_os_write_str(MetalVM* vm, MetalValue* args, int argc);
extern MetalValue n_os_num_to_str(MetalVM* vm, MetalValue* args, int argc);
extern MetalValue n_os_stat(MetalVM* vm, MetalValue* args, int argc);

static void vfs_bridge_write_char(char c) { console_putc(c); }

static MetalValue vfs_mv_dbl(double d) {
    union { double d; uint64_t u; } v;
    v.d = d;
    MetalValue mv; mv.type = MV_NUM; mv.as.num_bits = v.u;
    return mv;
}

static MetalValue n_os_backend_stat(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 2 || args[0].type != MV_PTR || args[1].type != MV_STR) return mv_nil();
    VfsBackend* b = (VfsBackend*)args[0].as.ptr;
    const char* rel = metal_string_get(vm, args[1].as.str_idx);
    VfsStat st;
    if (b->stat(b, rel, &st) == VFS_OK) {
        int d = metal_dict_new(vm);
        metal_dict_set(vm, d, metal_string_intern(vm, "name", 4), mv_str(vm, st.name, strlen(st.name)));
        metal_dict_set(vm, d, metal_string_intern(vm, "type", 4), vfs_mv_dbl((double)st.type));
        metal_dict_set(vm, d, metal_string_intern(vm, "size", 4), vfs_mv_dbl((double)st.size));
        MetalValue res; res.type = MV_DICT; res.as.dict_idx = d;
        return res;
    }
    return mv_nil();
}

static MetalValue n_os_substr(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 3 || args[0].type != MV_STR) return mv_nil();
    const char* s = metal_string_get(vm, args[0].as.str_idx);
    
    union { double d; uint64_t u; } v_start, v_len;
    v_start.u = args[1].as.num_bits;
    v_len.u = args[2].as.num_bits;
    int start = (int)v_start.d;
    int len = (int)v_len.d;

    int slen = strlen(s);
    if (start < 0) start = 0;
    if (start > slen) start = slen;
    if (start + len > slen) len = slen - start;
    return mv_str(vm, s + start, len);
}

static MetalValue n_os_char_at(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 2 || args[0].type != MV_STR) return vfs_mv_dbl(0.0);
    const char* s = metal_string_get(vm, args[0].as.str_idx);
    
    union { double d; uint64_t u; } v_idx;
    v_idx.u = args[1].as.num_bits;
    int i = (int)v_idx.d;

    if (i < 0 || i >= (int)strlen(s)) return vfs_mv_dbl(0.0);
    return vfs_mv_dbl((double)(uint8_t)s[i]);
}

static MetalValue n_os_backend_readdir(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 2 || args[0].type != MV_PTR || args[1].type != MV_STR) return mv_nil();
    VfsBackend* b = (VfsBackend*)args[0].as.ptr;
    const char* rel = metal_string_get(vm, args[1].as.str_idx);
    VfsDirEntry entries[VFS_DIRENT_MAX];
    int count = b->readdir(b, rel, entries, VFS_DIRENT_MAX);
    if (count >= 0) {
        int arr = metal_array_new(vm);
        for (int i = 0; i < count; i++) {
            int d = metal_dict_new(vm);
            metal_dict_set(vm, d, metal_string_intern(vm, "name", 4), mv_str(vm, entries[i].name, strlen(entries[i].name)));
            metal_dict_set(vm, d, metal_string_intern(vm, "type", 4), mv_num((uint64_t)entries[i].type));
            metal_dict_set(vm, d, metal_string_intern(vm, "size", 4), mv_num(entries[i].size));
            MetalValue item; item.type = MV_DICT; item.as.dict_idx = d;
            metal_array_push(vm, arr, item);
        }
        MetalValue res; res.type = MV_ARR; res.as.arr_idx = arr;
        return res;
    }
    return mv_nil();
}

static MetalValue n_os_backend_read(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 4 || args[0].type != MV_PTR || args[1].type != MV_STR) return mv_nil();
    VfsBackend* b = (VfsBackend*)args[0].as.ptr;
    const char* rel = metal_string_get(vm, args[1].as.str_idx);
    uint64_t offset = args[2].as.num_bits;
    size_t size = (size_t)args[3].as.num_bits;
    
    /* 
     * We use the VM heap for temporary read buffer.
     * Ensure we don't overflow.
     */
    if (size > 32768) size = 32768; 
    uint8_t* tmp = (uint8_t*)&vm->heap[vm->heap_used];
    if (vm->heap_used + size > METAL_HEAP_SIZE) return mv_nil();

    int n = b->read(b, rel, offset, tmp, size);
    if (n >= 0) {
        return mv_str(vm, (const char*)tmp, n);
    }
    return mv_nil();
}

static MetalValue n_os_backend_write(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 5 || args[0].type != MV_PTR || args[1].type != MV_STR || args[3].type != MV_STR || args[4].type != MV_NUM) return mv_nil();
    VfsBackend* b = (VfsBackend*)args[0].as.ptr;
    const char* rel = metal_string_get(vm, args[1].as.str_idx);
    uint64_t offset = args[2].as.num_bits;
    const char* data = metal_string_get(vm, args[3].as.str_idx);
    union { double d; uint64_t u; } v; v.u = args[4].as.num_bits;
    int size = (int)v.d;
    int n = b->write(b, rel, offset, data, (size_t)size);
    return mv_num(n >= 0 ? (uint64_t)n : 0);
}

void vfs_bridge_init(void) {
    if (g_vfs_vm_inited) return;
    metal_vm_init(&g_vfs_vm);
    g_vfs_vm.write_char = vfs_bridge_write_char;
    
    metal_vm_register_native(&g_vfs_vm, "len", n_len);
    metal_vm_register_native(&g_vfs_vm, "os_strlen", n_os_strlen);
    metal_vm_register_native(&g_vfs_vm, "os_starts_with", n_os_starts_with);
    metal_vm_register_native(&g_vfs_vm, "os_array_len", n_os_array_len);
    metal_vm_register_native(&g_vfs_vm, "os_array_push", n_os_array_push);
    metal_vm_register_native(&g_vfs_vm, "os_write_str", n_os_write_str);
    metal_vm_register_native(&g_vfs_vm, "os_num_to_str", n_os_num_to_str);
    metal_vm_register_native(&g_vfs_vm, "os_stat", n_os_stat);
    metal_vm_register_native(&g_vfs_vm, "os_backend_stat", n_os_backend_stat);
    metal_vm_register_native(&g_vfs_vm, "os_backend_readdir", n_os_backend_readdir);
    metal_vm_register_native(&g_vfs_vm, "os_backend_read", n_os_backend_read);
    metal_vm_register_native(&g_vfs_vm, "os_backend_write", n_os_backend_write);
    metal_vm_register_native(&g_vfs_vm, "os_substr", n_os_substr);
    metal_vm_register_native(&g_vfs_vm, "os_char_at", n_os_char_at);

    if (!metal_vm_load_binary(&g_vfs_vm, vfs_bridge_bytecode, vfs_bridge_bytecode_len)) {
        console_write("\n[VFS] Bridge load FAILED");
    } else if (metal_vm_run(&g_vfs_vm) != 0) {
        console_write("\n[VFS] Bridge init FAILED: ");
        console_write(g_vfs_vm.error_msg ? g_vfs_vm.error_msg : "unknown error");
    }
    g_vfs_vm_inited = 1;
}

/* -----------------------------------------------------------------------
 * String helpers (freestanding — no libc)
 * ----------------------------------------------------------------------- */

static int vfs_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int vfs_strncmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

static int vfs_strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}



/* -----------------------------------------------------------------------
 * Path normalization — strip double slashes, resolve . and ..
 *
 * Algorithm from SageLang lib/os/vfs.sage normalize_path()
 * ----------------------------------------------------------------------- */

int vfs_normalize_path(const char *input, char *output, int output_size) {
    /* Split by '/' into components, skip empty and ".", handle ".." */
    const char *parts[32];
    int part_lens[32];
    int nparts = 0;

    const char *p = input;
    while (*p) {
        /* Skip slashes */
        while (*p == '/') p++;
        if (*p == 0) break;

        /* Find end of component */
        const char *start = p;
        while (*p && *p != '/') p++;
        int len = (int)(p - start);

        /* Handle . and .. */
        if (len == 1 && start[0] == '.') {
            continue; /* skip */
        }
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (nparts > 0) nparts--;
            continue;
        }

        if (nparts < 32) {
            parts[nparts] = start;
            part_lens[nparts] = len;
            nparts++;
        }
    }

    /* Rebuild path */
    if (nparts == 0) {
        if (output_size >= 2) { output[0] = '/'; output[1] = 0; }
        return 1;
    }

    int pos = 0;
    for (int i = 0; i < nparts; i++) {
        if (pos < output_size - 1) output[pos++] = '/';
        for (int j = 0; j < part_lens[i] && pos < output_size - 1; j++) {
            output[pos++] = parts[i][j];
        }
    }
    output[pos] = 0;
    return pos;
}

/* -----------------------------------------------------------------------
 * Mount / unmount
 * ----------------------------------------------------------------------- */

int vfs_mount(const char *mount_path, VfsBackend *backend) {
    if (g_mount_count >= VFS_MAX_MOUNTS) return VFS_ENOSPC;
    if (!mount_path || !backend) return VFS_EINVAL;

    VfsMount *m = &g_mounts[g_mount_count];
    m->path = mount_path;
    m->path_len = vfs_strlen(mount_path);
    m->backend = backend;
    m->active = 1;
    g_mount_count++;

    /* Notify Sage bridge */
    if (g_vfs_vm_inited) {
        MetalValue args[2];
        args[0] = mv_str(&g_vfs_vm, mount_path, strlen(mount_path));
        args[1] = mv_ptr(backend);
        metal_vm_call(&g_vfs_vm, "vfs_mount", args, 2);
    }

    return VFS_OK;
}

int vfs_get_mount_count(void) {
    return g_mount_count;
}

int vfs_get_mount_info(int index, VfsMountInfo *out) {
    if (index < 0 || index >= g_mount_count || !g_mounts[index].active) {
        return VFS_ENOENT;
    }
    
    /* Copy path */
    int i = 0;
    const char *p = g_mounts[index].path;
    while (p[i] && i < VFS_MAX_PATH - 1) {
        out->path[i] = p[i];
        i++;
    }
    out->path[i] = 0;

    /* Copy type (backend name) */
    i = 0;
    const char *t = g_mounts[index].backend->name;
    while (t[i] && i < 31) {
        out->type[i] = t[i];
        i++;
    }
    out->type[i] = 0;

    return VFS_OK;
}

int vfs_umount(const char *mount_path) {
    for (int i = 0; i < g_mount_count; i++) {
        if (g_mounts[i].active &&
            vfs_strlen(mount_path) == g_mounts[i].path_len &&
            vfs_strncmp(mount_path, g_mounts[i].path, g_mounts[i].path_len) == 0) {
            g_mounts[i].active = 0;
            return VFS_OK;
        }
    }
    return VFS_ENOENT;
}

/* -----------------------------------------------------------------------
 * Mount resolution — longest prefix match
 *
 * Algorithm from SageLang lib/os/vfs.sage resolve_mount()
 * ----------------------------------------------------------------------- */

static VfsMount *resolve_mount(const char *norm_path, const char **rel_out) {
    VfsMount *best = NULL;
    int best_len = -1;
    int path_len = vfs_strlen(norm_path);

    for (int i = 0; i < g_mount_count; i++) {
        VfsMount *m = &g_mounts[i];
        if (!m->active) continue;

        int mp_len = m->path_len;

        /* Root mount "/" matches everything */
        if (mp_len == 1 && m->path[0] == '/') {
            if (mp_len > best_len) {
                best = m;
                best_len = mp_len;
            }
            continue;
        }

        /* Check prefix match */
        if (path_len >= mp_len &&
            vfs_strncmp(norm_path, m->path, mp_len) == 0) {
            /* Must be exact match or followed by '/' */
            if (path_len == mp_len || norm_path[mp_len] == '/') {
                if (mp_len > best_len) {
                    best = m;
                    best_len = mp_len;
                }
            }
        }
    }

    if (best && rel_out) {
        if (best->path_len == 1 && best->path[0] == '/') {
            /* Root mount: relative path is the full path */
            *rel_out = norm_path;
        } else if (path_len == best->path_len) {
            /* Exact mount point match */
            *rel_out = "/";
        } else {
            /* Strip mount prefix */
            *rel_out = norm_path + best->path_len;
            if (**rel_out != '/') {
                /* Shouldn't happen after normalize, but safety */
                *rel_out = norm_path + best->path_len;
            }
        }
    }

    return best;
}

/* -----------------------------------------------------------------------
 * Core VFS operations
 * ----------------------------------------------------------------------- */

int vfs_stat(const char *path, VfsStat *out) {
    if (g_vfs_vm_inited) {
        MetalValue arg = mv_str(&g_vfs_vm, path, strlen(path));
        MetalValue res = metal_vm_call(&g_vfs_vm, "vfs_stat", &arg, 1);
        if (res.type == MV_DICT) {
            MetalValue v_name = metal_dict_get(&g_vfs_vm, res.as.dict_idx, metal_string_intern(&g_vfs_vm, "name", 4));
            MetalValue v_type = metal_dict_get(&g_vfs_vm, res.as.dict_idx, metal_string_intern(&g_vfs_vm, "type", 4));
            MetalValue v_size = metal_dict_get(&g_vfs_vm, res.as.dict_idx, metal_string_intern(&g_vfs_vm, "size", 4));
            
            if (v_name.type == MV_STR) {
                const char* name = metal_string_get(&g_vfs_vm, v_name.as.str_idx);
                strncpy(out->name, name, VFS_NAME_MAX - 1);
                out->name[VFS_NAME_MAX-1] = 0;
            }
            if (v_type.type == MV_NUM) {
                union { double d; uint64_t u; } v;
                v.u = v_type.as.num_bits;
                out->type = (VfsNodeType)v.d;
            }
            if (v_size.type == MV_NUM) {
                union { double d; uint64_t u; } v;
                v.u = v_size.as.num_bits;
                out->size = (uint64_t)v.d;
            }
            return VFS_OK;
        }
    }

    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->stat) return VFS_ENOENT;

    return m->backend->stat(m->backend, rel, out);
}

int vfs_readdir(const char *path, VfsDirEntry *entries, int max_entries) {
    if (g_vfs_vm_inited) {
        MetalValue arg = mv_str(&g_vfs_vm, path, strlen(path));
        MetalValue res = metal_vm_call(&g_vfs_vm, "vfs_readdir", &arg, 1);
        if (res.type == MV_ARR) {
            int count = metal_array_len(&g_vfs_vm, res.as.arr_idx);
            if (count > max_entries) count = max_entries;
            for (int i = 0; i < count; i++) {
                MetalValue item = metal_array_get(&g_vfs_vm, res.as.arr_idx, i);
                if (item.type == MV_DICT) {
                    MetalValue v_name = metal_dict_get(&g_vfs_vm, item.as.dict_idx, metal_string_intern(&g_vfs_vm, "name", 4));
                    MetalValue v_type = metal_dict_get(&g_vfs_vm, item.as.dict_idx, metal_string_intern(&g_vfs_vm, "type", 4));
                    MetalValue v_size = metal_dict_get(&g_vfs_vm, item.as.dict_idx, metal_string_intern(&g_vfs_vm, "size", 4));
                    if (v_name.type == MV_STR) {
                        const char* name = metal_string_get(&g_vfs_vm, v_name.as.str_idx);
                        strncpy(entries[i].name, name, VFS_NAME_MAX - 1);
                        entries[i].name[VFS_NAME_MAX-1] = 0;
                    }
                    if (v_type.type == MV_NUM) {
                        union { double d; uint64_t u; } v;
                        v.u = v_type.as.num_bits;
                        entries[i].type = (VfsNodeType)v.d;
                    }
                    if (v_size.type == MV_NUM) {
                        union { double d; uint64_t u; } v;
                        v.u = v_size.as.num_bits;
                        entries[i].size = (uint64_t)v.d;
                    }
                }
            }
            return count;
        }
    }

    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->readdir) return VFS_ENOENT;

    return m->backend->readdir(m->backend, rel, entries, max_entries);
}

int vfs_read(const char *path, uint64_t offset, void *buffer, size_t size) {
    if (g_vfs_vm_inited) {
        MetalValue args[3];
        args[0] = mv_str(&g_vfs_vm, path, strlen(path));
        args[1] = mv_num(offset);
        args[2] = mv_num((uint64_t)size);
        MetalValue res = metal_vm_call(&g_vfs_vm, "vfs_read", args, 3);
        if (res.type == MV_ARR) {
            MetalValue mv_data = metal_array_get(&g_vfs_vm, res.as.arr_idx, 0);
            MetalValue mv_len = metal_array_get(&g_vfs_vm, res.as.arr_idx, 1);
            if (mv_data.type == MV_STR && mv_len.type == MV_NUM) {
                const char* data = metal_string_get(&g_vfs_vm, mv_data.as.str_idx);
                union { double d; uint64_t u; } v; v.u = mv_len.as.num_bits;
                int n = (int)v.d;
                if (n > (int)size) n = (int)size;
                extern void *sage_memcpy(void *dest, const void *src, size_t n);
                sage_memcpy(buffer, data, n);
                return n;
            }
        }
    }

    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->read) return VFS_ENOENT;

    return m->backend->read(m->backend, rel, offset, buffer, size);
}
int vfs_write(const char *path, uint64_t offset, const void *data, size_t size) {
    if (g_vfs_vm_inited) {
        MetalValue args[4];
        args[0] = mv_str(&g_vfs_vm, path, strlen(path));
        args[1] = mv_num(offset);
        args[2] = mv_str(&g_vfs_vm, (const char*)data, (int)size);
        args[3] = mv_num((uint64_t)size);
        MetalValue res = metal_vm_call(&g_vfs_vm, "vfs_write", args, 4);
        if (res.type == MV_NUM) {
            union { double d; uint64_t u; } v;
            v.u = res.as.num_bits;
            return (int)v.d;
        }
    }
    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->write) return VFS_EROFS;

    return m->backend->write(m->backend, rel, offset, data, size);
}

int vfs_mkdir(const char *path) {
    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->mkdir) return VFS_EROFS;

    return m->backend->mkdir(m->backend, rel);
}

int vfs_create(const char *path) {
    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->create) return VFS_EROFS;

    return m->backend->create(m->backend, rel);
}

int vfs_unlink(const char *path) {
    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->unlink) return VFS_EROFS;

    return m->backend->unlink(m->backend, rel);
}

int vfs_rm_rf(const char *path) {
    VfsStat st;
    int res = vfs_stat(path, &st);
    if (res != VFS_OK) return res;

    if (st.type == VFS_DIRECTORY) {
        VfsDirEntry entries[VFS_DIRENT_MAX];
        int count = vfs_readdir(path, entries, VFS_DIRENT_MAX);
        if (count < 0) return count;

        for (int i = 0; i < count; i++) {
            if (vfs_strcmp(entries[i].name, ".") == 0 || vfs_strcmp(entries[i].name, "..") == 0) continue;

            char child_path[VFS_MAX_PATH];
            strcpy(child_path, path);
            int len = vfs_strlen(child_path);
            if (len > 0 && child_path[len-1] != '/') {
                child_path[len] = '/';
                child_path[len+1] = 0;
                len++;
            }
            strcat(child_path, entries[i].name);

            res = vfs_rm_rf(child_path);
            if (res != VFS_OK) return res;
        }
    }

    return vfs_unlink(path);
}

/* -----------------------------------------------------------------------
 * Convenience: vfs_ls — print directory listing to console
 * ----------------------------------------------------------------------- */

void vfs_ls(const char *path) {
    VfsDirEntry entries[VFS_DIRENT_MAX];
    int count = vfs_readdir(path, entries, VFS_DIRENT_MAX);

    if (count < 0) {
        console_write("\nls: ");
        console_write(path);
        console_write(": ");
        console_write(vfs_strerror(count));
        return;
    }

    if (count == 0) {
        console_write("\n(empty)");
        return;
    }

    for (int i = 0; i < count; i++) {
        console_write("\n");
        console_write(entries[i].name);
        if (entries[i].type == VFS_DIRECTORY) {
            console_write("/");
        } else {
            /* Print file size */
            console_write("  ");
            console_u32((uint32_t)entries[i].size);
            console_write(" B");
        }
    }
}

/* -----------------------------------------------------------------------
 * Legacy compat
 * ----------------------------------------------------------------------- */

VfsNode *vfs_find(const char *path) {
    (void)path;
    return NULL; /* deprecated — use vfs_stat + vfs_read */
}

/* -----------------------------------------------------------------------
 * Error strings
 * ----------------------------------------------------------------------- */

const char *vfs_strerror(int err) {
    switch (err) {
        case VFS_OK:      return "OK";
        case VFS_ENOENT:  return "No such file or directory";
        case VFS_EIO:     return "I/O error";
        case VFS_EACCES:  return "Permission denied";
        case VFS_EEXIST:  return "File exists";
        case VFS_ENOTDIR: return "Not a directory";
        case VFS_EISDIR:  return "Is a directory";
        case VFS_ENOSPC:  return "No space left on device";
        case VFS_EROFS:   return "Read-only file system";
        case VFS_EINVAL:  return "Invalid argument";
        default:          return "Unknown error";
    }
}

/* -----------------------------------------------------------------------
 * Initialization — mounts are added by kernel.c after backends are ready
 * ----------------------------------------------------------------------- */

void vfs_init(void) {
    g_mount_count = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        g_mounts[i].active = 0;
    }
    vfs_bridge_init();
}
