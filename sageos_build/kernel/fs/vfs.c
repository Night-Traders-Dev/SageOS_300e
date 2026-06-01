#include "vfs.h"
#include "telemetry.h"
#include "console.h"
#include "sage_libc_shim.h"
#include <stddef.h>
#include <string.h>
#include "metal_vm.h"
#include "vfs_bridge_bytecode.h"
#include "dmesg.h"
#include "commands_embed.h"

// Note: EmbeddedFile struct and g_embedded_files array are defined in commands_embed.h

#define EMBEDDED_FILES_COUNT (sizeof(g_embedded_files)/sizeof(g_embedded_files[0]))

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

static MetalValue vfs_mv_dbl(double d) {
    union { double d; uint64_t u; } v;
    v.d = d;
    MetalValue mv; mv.type = MV_NUM; mv.as.num_bits = v.u;
    return mv;
}

MetalValue n_len(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1) return mv_nil();
    if (args[0].type == MV_STR) {
        const char *s = metal_string_get(vm, args[0].as.str_idx);
        return vfs_mv_dbl((double)strlen(s));
    }
    if (args[0].type == MV_ARR) {
        return vfs_mv_dbl((double)metal_array_len(vm, args[0].as.arr_idx));
    }
    return mv_nil();
}

static MetalValue n_dict_keys(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_DICT) return mv_nil();
    int dict_idx = args[0].as.dict_idx;
    int max = (int)(sizeof(vm->dicts) / sizeof(vm->dicts[0]));
    if (dict_idx < 0 || dict_idx >= max) return mv_nil();
    
    MetalDict* d = &vm->dicts[dict_idx];
    int arr_idx = metal_array_new(vm);
    if (arr_idx < 0) return mv_nil();
    
    for (int i = 0; i < d->count; i++) {
        MetalValue key_val;
        key_val.type = MV_STR;
        key_val.as.str_idx = d->key_str_idx[i];
        metal_array_push(vm, arr_idx, key_val);
    }
    
    MetalValue res;
    res.type = MV_ARR;
    res.as.arr_idx = arr_idx;
    return res;
}

MetalValue n_os_strlen(MetalVM* vm, MetalValue* args, int argc) {
    return n_len(vm, args, argc);
}

MetalValue n_os_starts_with(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 2 || args[0].type != MV_STR || args[1].type != MV_STR) return vfs_mv_dbl(0.0);
    const char *s = metal_string_get(vm, args[0].as.str_idx);
    const char *pre = metal_string_get(vm, args[1].as.str_idx);
    while (*pre) {
        if (*s++ != *pre++) return vfs_mv_dbl(0.0);
    }
    return vfs_mv_dbl(1.0);
}

MetalValue n_os_array_len(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_ARR) return vfs_mv_dbl(0.0);
    return vfs_mv_dbl((double)metal_array_len(vm, args[0].as.arr_idx));
}

MetalValue n_os_array_push(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 2 || args[0].type != MV_ARR) return mv_nil();
    metal_array_push(vm, args[0].as.arr_idx, args[1]);
    return mv_nil();
}

MetalValue n_os_write_str(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_STR) return mv_nil();
    console_write(metal_string_get(vm, args[0].as.str_idx));
    return mv_nil();
}

MetalValue n_os_num_to_str(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_NUM) return mv_nil();
    char buf[32];
    union { double d; uint64_t u; } v; v.u = args[0].as.num_bits;
    int n = snprintf(buf, sizeof(buf), "%lld", (long long)v.d);
    return mv_str(vm, buf, n);
}

MetalValue n_os_stat(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_STR) return mv_nil();
    const char *path = metal_string_get(vm, args[0].as.str_idx);
    VfsStat st;
    if (vfs_stat(path, &st) != VFS_OK) return mv_nil();
    
    int d = metal_dict_new(vm);
    metal_dict_set(vm, d, metal_string_intern(vm, "name", 4), mv_str(vm, st.name, strlen(st.name)));
    metal_dict_set(vm, d, metal_string_intern(vm, "type", 4), vfs_mv_dbl((double)st.type));
    metal_dict_set(vm, d, metal_string_intern(vm, "size", 4), vfs_mv_dbl((double)st.size));
    
    MetalValue res; res.type = MV_DICT; res.as.dict_idx = d;
    return res;
}

static void vfs_bridge_write_char(char c) { console_putc(c); }

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
    
    union { double d; uint64_t u; } v_start, v_end;
    v_start.u = args[1].as.num_bits;
    v_end.u = args[2].as.num_bits;
    int start = (int)v_start.d;
    int end = (int)v_end.d;

    int slen = strlen(s);
    if (start < 0) start = 0;
    if (end > slen) end = slen;
    if (start >= end) return mv_str(vm, "", 0);
    return mv_str(vm, s + start, end - start);
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
            metal_dict_set(vm, d, metal_string_intern(vm, "type", 4), vfs_mv_dbl((double)entries[i].type));
            metal_dict_set(vm, d, metal_string_intern(vm, "size", 4), vfs_mv_dbl((double)entries[i].size));
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
    
    union { double d; uint64_t u; } v_offset, v_size;
    v_offset.u = args[2].as.num_bits;
    v_size.u = args[3].as.num_bits;
    uint64_t offset = (uint64_t)v_offset.d;
    size_t size = (size_t)v_size.d;
    
    /* 
     * We use the VM heap for temporary read buffer.
     * Ensure we don't overflow.
     */
    if (size > 32768) size = 32768; 
    uint8_t* tmp = (uint8_t*)&vm->heap[vm->heap_used];
    if (vm->heap_used + size > METAL_HEAP_SIZE) return mv_nil();

    int n = b->read(b, rel, offset, tmp, size);
    if (n >= 0) {
        int arr = metal_array_new(vm);
        metal_array_push(vm, arr, mv_str(vm, (const char*)tmp, n));
        metal_array_push(vm, arr, vfs_mv_dbl((double)n));
        MetalValue res; res.type = MV_ARR; res.as.arr_idx = arr;
        return res;
    }
    return mv_nil();
}

static MetalValue n_os_backend_write(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 5 || args[0].type != MV_PTR || args[1].type != MV_STR || args[3].type != MV_STR || args[4].type != MV_NUM) return mv_nil();
    VfsBackend* b = (VfsBackend*)args[0].as.ptr;
    const char* rel = metal_string_get(vm, args[1].as.str_idx);
    
    union { double d; uint64_t u; } v_offset, v_size;
    v_offset.u = args[2].as.num_bits;
    v_size.u = args[4].as.num_bits;
    uint64_t offset = (uint64_t)v_offset.d;
    int size = (int)v_size.d;
    
    const char* data = metal_string_get(vm, args[3].as.str_idx);
    int n = b->write(b, rel, offset, data, (size_t)size);
    return vfs_mv_dbl((double)(n >= 0 ? n : 0));
}

static MetalValue n_os_get_embedded_count(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    return vfs_mv_dbl((double)EMBEDDED_FILES_COUNT);
}

static MetalValue n_os_get_embedded_file(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_NUM) return mv_nil();
    union { double d; uint64_t u; } v; v.u = args[0].as.num_bits;
    int idx = (int)v.d;
    if (idx < 0 || idx >= (int)EMBEDDED_FILES_COUNT) return mv_nil();
    
    int d = metal_dict_new(vm);
    const EmbeddedFile* f = &g_embedded_files[idx];
    metal_dict_set(vm, d, metal_string_intern(vm, "path", 4), mv_str(vm, f->path, strlen(f->path)));
    metal_dict_set(vm, d, metal_string_intern(vm, "data", 4), mv_str(vm, (const char*)f->data, (int)f->size));
    
    MetalValue res; res.type = MV_DICT; res.as.dict_idx = d;
    return res;
}

const char* vfs_get_embedded_data(const char* path, uint64_t* out_size) {
    for (int i = 0; i < (int)EMBEDDED_FILES_COUNT; i++) {
        if (strcmp(g_embedded_files[i].path, path) == 0) {
            if (out_size) *out_size = (uint64_t)g_embedded_files[i].size;
            return (const char*)g_embedded_files[i].data;
        }
    }
    if (out_size) *out_size = 0;
    return NULL;
}

static MetalValue n_os_debug_val(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    return mv_nil();
}

static MetalValue n_os_chr(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_NUM) return mv_str(vm, "", 0);
    union { double d; uint64_t u; } v; v.u = args[0].as.num_bits;
    char c = (char)v.d;
    return mv_str(vm, &c, 1);
}

void vfs_bridge_init(void) {
    if (g_vfs_vm_inited) return;
    metal_vm_init(&g_vfs_vm);
    g_vfs_vm.write_char = vfs_bridge_write_char;
    
    metal_vm_register_native(&g_vfs_vm, "len", n_len);
    metal_vm_register_native(&g_vfs_vm, "dict_keys", n_dict_keys);
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
    metal_vm_register_native(&g_vfs_vm, "os_chr", n_os_chr);
    metal_vm_register_native(&g_vfs_vm, "os_get_embedded_count", n_os_get_embedded_count);
    metal_vm_register_native(&g_vfs_vm, "os_get_embedded_file", n_os_get_embedded_file);
    metal_vm_register_native(&g_vfs_vm, "os_debug_val", n_os_debug_val);

    if (!metal_vm_load_binary(&g_vfs_vm, vfs_bridge_bytecode, vfs_bridge_bytecode_len)) {
        console_write("\n[VFS] Bridge load FAILED");
    } else {
        int res = metal_vm_run(&g_vfs_vm);
        if (res != 0) {
            console_write("\n[VFS] Bridge init FAILED (");
            console_u32((uint32_t)-res);
            console_write("): ");
            console_write(g_vfs_vm.error_msg ? g_vfs_vm.error_msg : "unknown error");
        } else {
            g_vfs_vm_inited = 1;
            console_write("\n[VFS] Bridge initialized successfully.");
        }
    }
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

    trace_log(TRACE_VFS_MOUNT, (uint64_t)mount_path, (uint64_t)backend);

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
    extern void console_write(const char *str);
    console_write("[VFS_STAT] ");
    console_write(path);
    console_write("\n");
    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    console_write("[VFS_STAT] Normalize done\n");
    const char *rel = NULL;
    console_write("[VFS_STAT] Resolving mount...\n");
    VfsMount *m = resolve_mount(norm, &rel);
    console_write("[VFS_STAT] Resolving mount done\n");
    if (m && m->backend && m->backend->stat) {
        console_write("[VFS_STAT] Backend stat call...\n");
        int res = m->backend->stat(m->backend, rel, out);
        console_write("[VFS_STAT] Backend stat call done\n");
        return res;
    }

    if (g_vfs_vm_inited) {
        int saved_string = g_vfs_vm.string_used;
        int saved_heap = g_vfs_vm.heap_used;
        int saved_arrays = g_vfs_vm.array_count;
        int saved_dicts = g_vfs_vm.dict_count;

        MetalValue arg = mv_str(&g_vfs_vm, path, strlen(path));
        MetalValue res = metal_vm_call(&g_vfs_vm, "vfs_stat", &arg, 1);
        int ret_val = -1;
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
                if (v_size.type == MV_NUM) {
                    union { double d; uint64_t u; } v;
                    v.u = v_size.as.num_bits;
                    out->size = (uint64_t)v.d;
                }
                ret_val = VFS_OK;
            }
        } g_vfs_vm.string_used = saved_string;
        g_vfs_vm.heap_used = saved_heap;
        g_vfs_vm.array_count = saved_arrays;
        g_vfs_vm.dict_count = saved_dicts;

        if (ret_val == VFS_OK) return VFS_OK;
    }

    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->stat) return VFS_ENOENT;

    return m->backend->stat(m->backend, rel, out);
}

int vfs_readdir(const char *path, VfsDirEntry *entries, int max_entries) {
    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (m && m->backend && m->backend->readdir) {
        return m->backend->readdir(m->backend, rel, entries, max_entries);
    }

    if (g_vfs_vm_inited) {
        int saved_string = g_vfs_vm.string_used;
        int saved_heap = g_vfs_vm.heap_used;
        int saved_arrays = g_vfs_vm.array_count;
        int saved_dicts = g_vfs_vm.dict_count;

        MetalValue arg = mv_str(&g_vfs_vm, path, strlen(path));
        MetalValue res = metal_vm_call(&g_vfs_vm, "vfs_readdir", &arg, 1);
        int ret_val = -1;
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
            ret_val = count;
        } else {
            /* If VM readdir fails or returns nil, fall back to C mounts */
        }

        g_vfs_vm.string_used = saved_string;
        g_vfs_vm.heap_used = saved_heap;
        g_vfs_vm.array_count = saved_arrays;
        g_vfs_vm.dict_count = saved_dicts;

        if (ret_val >= 0) return ret_val;
    }

    /* 
     * Special case: Root directory listing 
     * Always show virtual directories and active mounts.
     */
    if (strcmp(norm, "/") == 0) {
        int count = 0;
        
        /* 1. Add essential virtual directories */
        if (count < max_entries) { strcpy(entries[count].name, "dev"); entries[count].type = VFS_DIRECTORY; entries[count].size = 0; count++; }
        if (count < max_entries) { strcpy(entries[count].name, "tmp"); entries[count].type = VFS_DIRECTORY; entries[count].size = 0; count++; }
        if (count < max_entries) { strcpy(entries[count].name, "etc"); entries[count].type = VFS_DIRECTORY; entries[count].size = 0; count++; }
        if (count < max_entries) { strcpy(entries[count].name, "bin"); entries[count].type = VFS_DIRECTORY; entries[count].size = 0; count++; }

        /* 2. Add mount points */
        for (int i = 0; i < g_mount_count && count < max_entries; i++) {
            if (g_mounts[i].active) {
                const char *p = g_mounts[i].path;
                if (p[0] == '/') p++; /* skip leading slash */
                if (p[0] == 0) continue; /* skip root mount itself */
                
                /* Just show top-level component of mount path */
                const char *slash = p;
                while (*slash && *slash != '/') slash++;
                int len = slash - p;
                if (len <= 0) continue;
                
                /* Check if already added */
                int found = 0;
                for (int j = 0; j < count; j++) {
                    if (vfs_strncmp(entries[j].name, p, len) == 0 && entries[j].name[len] == 0) {
                        found = 1; break;
                    }
                }
                
                if (!found) {
                    strncpy(entries[count].name, p, len);
                    entries[count].name[len] = 0;
                    entries[count].type = VFS_DIRECTORY;
                    entries[count].size = 0;
                    count++;
                }
            }
        }
        return count;
    }
    m = resolve_mount(norm, &rel);
    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->readdir) return VFS_ENOENT;

    return m->backend->readdir(m->backend, rel, entries, max_entries);
}

int vfs_read(const char *path, uint64_t offset, void *buffer, size_t size) {
    trace_log(TRACE_VFS_READ, offset, (uint64_t)size);
    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (m && m->backend && m->backend->read) {
        return m->backend->read(m->backend, rel, offset, buffer, size);
    }

    if (g_vfs_vm_inited) {
        int saved_string = g_vfs_vm.string_used;
        int saved_heap = g_vfs_vm.heap_used;
        int saved_arrays = g_vfs_vm.array_count;
        int saved_dicts = g_vfs_vm.dict_count;

        MetalValue args[3];
        args[0] = mv_str(&g_vfs_vm, path, strlen(path));
        args[1] = vfs_mv_dbl((double)offset);
        args[2] = vfs_mv_dbl((double)size);
        MetalValue res = metal_vm_call(&g_vfs_vm, "vfs_read", args, 3);
        int ret_val = -1;
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
                ret_val = n;
            }
        }

        g_vfs_vm.string_used = saved_string;
        g_vfs_vm.heap_used = saved_heap;
        g_vfs_vm.array_count = saved_arrays;
        g_vfs_vm.dict_count = saved_dicts;

        if (ret_val >= 0) return ret_val;
    }

    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->read) return VFS_ENOENT;

    return m->backend->read(m->backend, rel, offset, buffer, size);
}
int vfs_write(const char *path, uint64_t offset, const void *data, size_t size) {
    trace_log(TRACE_VFS_WRITE, offset, (uint64_t)size);
    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (m && m->backend && m->backend->write) {
        return m->backend->write(m->backend, rel, offset, data, size);
    }

    if (g_vfs_vm_inited) {
        MetalValue args[4];
        args[0] = mv_str(&g_vfs_vm, path, strlen(path));
        args[1] = vfs_mv_dbl((double)offset);
        args[2] = mv_str(&g_vfs_vm, (const char*)data, (int)size);
        args[3] = vfs_mv_dbl((double)size);
        MetalValue res = metal_vm_call(&g_vfs_vm, "vfs_write", args, 4);
        if (res.type == MV_NUM) {
            union { double d; uint64_t u; } v;
            v.u = res.as.num_bits;
            return (int)v.d;
        }
    }

    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->write) return VFS_EROFS;

    return m->backend->write(m->backend, rel, offset, data, size);
}

int vfs_mkdir(const char *path) {
    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (m && m->backend && m->backend->mkdir) {
        return m->backend->mkdir(m->backend, rel);
    }

    if (g_vfs_vm_inited) {
        MetalValue arg = mv_str(&g_vfs_vm, path, strlen(path));
        MetalValue res = metal_vm_call(&g_vfs_vm, "vfs_mkdir", &arg, 1);
        if (res.type == MV_NUM) {
            union { double d; uint64_t u; } v; v.u = res.as.num_bits;
            return (int)v.d;
        }
    }

    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->mkdir) return VFS_EROFS;

    return m->backend->mkdir(m->backend, rel);
}

int vfs_create(const char *path) {
    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (m && m->backend && m->backend->create) {
        return m->backend->create(m->backend, rel);
    }

    if (g_vfs_vm_inited) {
        MetalValue arg = mv_str(&g_vfs_vm, path, strlen(path));
        MetalValue res = metal_vm_call(&g_vfs_vm, "vfs_create", &arg, 1);
        if (res.type == MV_NUM) {
            union { double d; uint64_t u; } v; v.u = res.as.num_bits;
            return (int)v.d;
        }
    }

    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->create) return VFS_EROFS;

    return m->backend->create(m->backend, rel);
}

int vfs_unlink(const char *path) {
    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (m && m->backend && m->backend->unlink) {
        return m->backend->unlink(m->backend, rel);
    }

    if (g_vfs_vm_inited) {
        MetalValue arg = mv_str(&g_vfs_vm, path, strlen(path));
        MetalValue res = metal_vm_call(&g_vfs_vm, "vfs_unlink", &arg, 1);
        if (res.type == MV_NUM) {
            union { double d; uint64_t u; } v; v.u = res.as.num_bits;
            return (int)v.d;
        }
    }

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
            int len = vfs_strlen(path);
            int name_len = vfs_strlen(entries[i].name);

            /* Safe path concatenation: path + "/" + name */
            if (len + 1 + name_len >= VFS_MAX_PATH) {
                /* Path too long, skip or return error? RM -RF should probably try to continue 
                 * but here we must prevent overflow. */
                continue;
            }

            if (len > 0 && path[len-1] == '/') {
                snprintf(child_path, VFS_MAX_PATH, "%s%s", path, entries[i].name);
            } else {
                snprintf(child_path, VFS_MAX_PATH, "%s/%s", path, entries[i].name);
            }

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
