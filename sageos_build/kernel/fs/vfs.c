#include "vfs.h"
#include "console.h"
#include <stddef.h>

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
    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->stat) return VFS_ENOENT;

    return m->backend->stat(m->backend, rel, out);
}

int vfs_readdir(const char *path, VfsDirEntry *entries, int max_entries) {
    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->readdir) return VFS_ENOENT;

    return m->backend->readdir(m->backend, rel, entries, max_entries);
}

int vfs_read(const char *path, uint64_t offset, void *buffer, size_t size) {
    char norm[VFS_MAX_PATH];
    vfs_normalize_path(path, norm, VFS_MAX_PATH);

    const char *rel;
    VfsMount *m = resolve_mount(norm, &rel);
    if (!m || !m->backend) return VFS_ENOENT;
    if (!m->backend->read) return VFS_ENOENT;

    return m->backend->read(m->backend, rel, offset, buffer, size);
}

int vfs_write(const char *path, uint64_t offset, const void *data, size_t size) {
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
}
