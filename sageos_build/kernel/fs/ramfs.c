#include <stddef.h>
#include <stdint.h>
#include "console.h"
#include "ramfs.h"
#include "vfs.h"
#include "version.h"

/* Embedded binary files */
#include "bin_hello.h"
#include "bin_hello_json.h"
#include "bin_hello_kernel.h"
#include "commands_embed.h"

/* -----------------------------------------------------------------------
 * Freestanding string helpers
 * ----------------------------------------------------------------------- */

static int r_strlen(const char *s) {
    int n = 0; while (s[n]) n++; return n;
}

static int r_streq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static void r_strncpy(char *d, const char *s, int n) {
    int i = 0; while (i < n - 1 && s[i]) { d[i] = s[i]; i++; } d[i] = 0;
}

static void r_memcpy(void *d, const void *s, size_t n) {
    const uint8_t *src = (const uint8_t *)s;
    uint8_t *dst = (uint8_t *)d;
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
}

/* -----------------------------------------------------------------------
 * Inode table
 * ----------------------------------------------------------------------- */

typedef struct {
    char         name[RAMFS_NAME_MAX];  /* entry name (not full path) */
    VfsNodeType  type;
    uint64_t     size;

    /* For files: pointer to data (either in pool or external const) */
    const void  *data;
    int          data_in_pool;  /* 1 = data lives in g_data_pool, writable */

    /* For directories: child inode indices */
    int          children[RAMFS_MAX_CHILDREN];
    int          child_count;

    /* Parent inode index (-1 for root) */
    int          parent;

    int          active;        /* 1 = in use */
} RamfsInode;

static RamfsInode  g_inodes[RAMFS_MAX_INODES];
static int         g_inode_count = 0;

/* Data pool for writable file contents */
static uint8_t     g_data_pool[RAMFS_DATA_POOL];
static int         g_pool_used = 0;

/* -----------------------------------------------------------------------
 * Inode allocation
 * ----------------------------------------------------------------------- */

static int alloc_inode(void) {
    if (g_inode_count >= RAMFS_MAX_INODES) return -1;
    int idx = g_inode_count++;
    RamfsInode *n = &g_inodes[idx];
    n->name[0] = 0;
    n->type = VFS_FILE;
    n->size = 0;
    n->data = NULL;
    n->data_in_pool = 0;
    n->child_count = 0;
    n->parent = -1;
    n->active = 1;
    return idx;
}

static void *pool_alloc(size_t size) {
    if (g_pool_used + (int)size > RAMFS_DATA_POOL) return NULL;
    void *p = &g_data_pool[g_pool_used];
    g_pool_used += (int)size;
    return p;
}

/* -----------------------------------------------------------------------
 * Path resolution
 *
 * Walks the inode tree from root following path components.
 * Algorithm from SageLang lib/os/tmpfs.sage _resolve()
 * ----------------------------------------------------------------------- */

static int find_root(void) {
    /* Root is always inode 0 */
    return (g_inode_count > 0 && g_inodes[0].active) ? 0 : -1;
}

/* Find a child by name within a directory inode */
static int find_child(int dir_idx, const char *name) {
    RamfsInode *dir = &g_inodes[dir_idx];
    if (dir->type != VFS_DIRECTORY) return -1;

    for (int i = 0; i < dir->child_count; i++) {
        int ci = dir->children[i];
        if (ci >= 0 && ci < g_inode_count && g_inodes[ci].active) {
            if (r_streq(g_inodes[ci].name, name)) return ci;
        }
    }
    return -1;
}

/* Resolve a full path to an inode index. Returns -1 if not found. */
static int resolve_path(const char *path) {
    int cur = find_root();
    if (cur < 0) return -1;

    /* Skip leading slash */
    const char *p = path;
    if (*p == '/') p++;
    if (*p == 0) return cur;  /* root itself */

    while (*p) {
        /* Extract next component */
        char comp[RAMFS_NAME_MAX];
        int len = 0;
        while (*p && *p != '/' && len < RAMFS_NAME_MAX - 1) {
            comp[len++] = *p++;
        }
        comp[len] = 0;
        if (*p == '/') p++;
        if (len == 0) continue;

        /* Look up in current directory */
        cur = find_child(cur, comp);
        if (cur < 0) return -1;
    }

    return cur;
}

/* Resolve parent directory and extract the last component name */
static int resolve_parent(const char *path, char *name_out) {
    /* Find last slash to split parent/name */
    int path_len = r_strlen(path);
    int last_slash = -1;
    for (int i = path_len - 1; i >= 0; i--) {
        if (path[i] == '/') { last_slash = i; break; }
    }

    char parent_path[VFS_MAX_PATH];
    if (last_slash <= 0) {
        parent_path[0] = '/';
        parent_path[1] = 0;
    } else {
        int i = 0;
        for (; i < last_slash && i < VFS_MAX_PATH - 1; i++) {
            parent_path[i] = path[i];
        }
        parent_path[i] = 0;
    }

    /* Extract name */
    const char *name_start = path + last_slash + 1;
    r_strncpy(name_out, name_start, RAMFS_NAME_MAX);

    return resolve_path(parent_path);
}

/* -----------------------------------------------------------------------
 * VFS Backend callbacks
 * ----------------------------------------------------------------------- */

static int ramfs_be_stat(VfsBackend *self, const char *rel_path, VfsStat *out) {
    (void)self;
    int idx = resolve_path(rel_path);
    if (idx < 0) return VFS_ENOENT;

    RamfsInode *n = &g_inodes[idx];
    r_strncpy(out->name, n->name, VFS_NAME_MAX);
    out->type = n->type;
    out->size = n->size;
    out->mode = (n->type == VFS_DIRECTORY) ? 0755 : 0644;
    return VFS_OK;
}

static int ramfs_be_readdir(VfsBackend *self, const char *rel_path,
                            VfsDirEntry *entries, int max_entries) {
    (void)self;
    int idx = resolve_path(rel_path);
    if (idx < 0) return VFS_ENOENT;

    RamfsInode *dir = &g_inodes[idx];
    if (dir->type != VFS_DIRECTORY) return VFS_ENOTDIR;

    int count = 0;
    for (int i = 0; i < dir->child_count && count < max_entries; i++) {
        int ci = dir->children[i];
        if (ci >= 0 && ci < g_inode_count && g_inodes[ci].active) {
            r_strncpy(entries[count].name, g_inodes[ci].name, VFS_NAME_MAX);
            entries[count].type = g_inodes[ci].type;
            entries[count].size = g_inodes[ci].size;
            count++;
        }
    }
    return count;
}

static int ramfs_be_read(VfsBackend *self, const char *rel_path,
                         uint64_t offset, void *buffer, size_t size) {
    (void)self;
    int idx = resolve_path(rel_path);
    if (idx < 0) return VFS_ENOENT;

    RamfsInode *n = &g_inodes[idx];
    if (n->type != VFS_FILE) return VFS_EISDIR;
    if (!n->data) return 0;

    if (offset >= n->size) return 0;
    size_t avail = n->size - (size_t)offset;
    if (size > avail) size = avail;

    r_memcpy(buffer, (const uint8_t *)n->data + offset, size);
    return (int)size;
}

static int ramfs_be_write(VfsBackend *self, const char *rel_path,
                          uint64_t offset, const void *data, size_t size) {
    (void)self;
    int idx = resolve_path(rel_path);
    if (idx < 0) return VFS_ENOENT;

    RamfsInode *n = &g_inodes[idx];
    if (n->type != VFS_FILE) return VFS_EISDIR;

    /* If the file has external (const) data, we can't write to it */
    if (n->data && !n->data_in_pool) return VFS_EROFS;

    /* For simplicity: replace entire file content on write */
    size_t total = (size_t)offset + size;
    void *new_data = pool_alloc(total);
    if (!new_data) return VFS_ENOSPC;

    /* Copy existing data if offset > 0 */
    if (offset > 0 && n->data) {
        size_t copy_len = (size_t)offset;
        if (copy_len > n->size) copy_len = n->size;
        r_memcpy(new_data, n->data, copy_len);
    }

    /* Write new data */
    r_memcpy((uint8_t *)new_data + offset, data, size);
    n->data = new_data;
    n->data_in_pool = 1;
    n->size = total;
    return (int)size;
}

static int ramfs_be_mkdir(VfsBackend *self, const char *rel_path) {
    (void)self;
    return ramfs_create_dir(rel_path);
}

static int ramfs_be_create(VfsBackend *self, const char *rel_path) {
    (void)self;
    return ramfs_create_file(rel_path, "", 0);
}

static int ramfs_be_unlink(VfsBackend *self, const char *rel_path) {
    (void)self;
    char name[RAMFS_NAME_MAX];
    int parent_idx = resolve_parent(rel_path, name);
    if (parent_idx < 0) return VFS_ENOENT;

    int child_idx = find_child(parent_idx, name);
    if (child_idx < 0) return VFS_ENOENT;

    RamfsInode *child = &g_inodes[child_idx];

    /* Can't delete non-empty directories */
    if (child->type == VFS_DIRECTORY && child->child_count > 0) {
        return VFS_ENOENT;
    }

    /* Remove from parent's child list */
    RamfsInode *parent = &g_inodes[parent_idx];
    for (int i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child_idx) {
            /* Shift remaining children down */
            for (int j = i; j < parent->child_count - 1; j++) {
                parent->children[j] = parent->children[j + 1];
            }
            parent->child_count--;
            break;
        }
    }

    child->active = 0;
    return VFS_OK;
}

/* -----------------------------------------------------------------------
 * VfsBackend instance
 * ----------------------------------------------------------------------- */

static VfsBackend g_ramfs_backend = {
    .name    = "ramfs",
    .stat    = ramfs_be_stat,
    .readdir = ramfs_be_readdir,
    .read    = ramfs_be_read,
    .write   = ramfs_be_write,
    .mkdir   = ramfs_be_mkdir,
    .create  = ramfs_be_create,
    .unlink  = ramfs_be_unlink,
    .priv    = NULL
};

VfsBackend *ramfs_get_backend(void) {
    return &g_ramfs_backend;
}

/* -----------------------------------------------------------------------
 * Direct creation API
 * ----------------------------------------------------------------------- */

int ramfs_create_dir(const char *path) {
    char name[RAMFS_NAME_MAX];
    int parent_idx = resolve_parent(path, name);
    if (parent_idx < 0) return VFS_ENOENT;

    /* Check if already exists */
    if (find_child(parent_idx, name) >= 0) return VFS_EEXIST;

    /* Check parent has room */
    RamfsInode *parent = &g_inodes[parent_idx];
    if (parent->child_count >= RAMFS_MAX_CHILDREN) return VFS_ENOSPC;

    int idx = alloc_inode();
    if (idx < 0) return VFS_ENOSPC;

    RamfsInode *n = &g_inodes[idx];
    r_strncpy(n->name, name, RAMFS_NAME_MAX);
    n->type = VFS_DIRECTORY;
    n->parent = parent_idx;

    parent->children[parent->child_count++] = idx;
    return VFS_OK;
}

int ramfs_create_file(const char *path, const void *data, size_t size) {
    char name[RAMFS_NAME_MAX];
    int parent_idx = resolve_parent(path, name);
    if (parent_idx < 0) return VFS_ENOENT;

    /* Check if already exists */
    if (find_child(parent_idx, name) >= 0) return VFS_EEXIST;

    RamfsInode *parent = &g_inodes[parent_idx];
    if (parent->child_count >= RAMFS_MAX_CHILDREN) return VFS_ENOSPC;

    int idx = alloc_inode();
    if (idx < 0) return VFS_ENOSPC;

    RamfsInode *n = &g_inodes[idx];
    r_strncpy(n->name, name, RAMFS_NAME_MAX);
    n->type = VFS_FILE;
    n->parent = parent_idx;

    if (size > 0 && data) {
        void *pool_data = pool_alloc(size);
        if (!pool_data) return VFS_ENOSPC;
        r_memcpy(pool_data, data, size);
        n->data = pool_data;
        n->data_in_pool = 1;
    }
    n->size = size;

    parent->children[parent->child_count++] = idx;
    return VFS_OK;
}

int ramfs_create_file_ref(const char *path, const void *data, size_t size) {
    char name[RAMFS_NAME_MAX];
    int parent_idx = resolve_parent(path, name);
    if (parent_idx < 0) return VFS_ENOENT;

    if (find_child(parent_idx, name) >= 0) return VFS_EEXIST;

    RamfsInode *parent = &g_inodes[parent_idx];
    if (parent->child_count >= RAMFS_MAX_CHILDREN) return VFS_ENOSPC;

    int idx = alloc_inode();
    if (idx < 0) return VFS_ENOSPC;

    RamfsInode *n = &g_inodes[idx];
    r_strncpy(n->name, name, RAMFS_NAME_MAX);
    n->type = VFS_FILE;
    n->parent = parent_idx;
    n->data = data;
    n->data_in_pool = 0;  /* external const data */
    n->size = size;

    parent->children[parent->child_count++] = idx;
    return VFS_OK;
}

/* -----------------------------------------------------------------------
 * Initialization — create directory structure and pre-populate files
 * ----------------------------------------------------------------------- */

/* Test SageLang source — embedded as a string constant */
static const char test_sage_source[] =
    "# SageLang test for SageOS REPL\n"
    "let x = 42\n"
    "let y = x * 2\n"
    "print(y)\n"
    "let name = \"SageOS\"\n"
    "print(\"Hello, \" + name + \"!\")\n"
    "print(x + y)\n";

static const char test_error_sage_source[] =
    "print(\"Testing division by zero...\")\n"
    "try let a = 10 / 0 catch (e) print(e)\n"
    "print(\"Testing undefined var...\")\n"
    "try print(undefined_var) catch (err) print(err)\n"
    "print(\"Done.\")\n";

void ramfs_init(void) {
    g_inode_count = 0;
    g_pool_used = 0;

    /* Create root directory (inode 0) */
    int root = alloc_inode();
    (void)root; /* always 0 */
    g_inodes[0].type = VFS_DIRECTORY;
    r_strncpy(g_inodes[0].name, "/", RAMFS_NAME_MAX);

    /* Create directory structure */
    ramfs_create_dir("/etc");
    ramfs_create_dir("/etc/commands");
    ramfs_create_dir("/bin");
    ramfs_create_dir("/dev");
    ramfs_create_dir("/proc");
    ramfs_create_dir("/tmp");

    /* Pre-populate with static files (use _ref for const data) */
    static const char motd[] = "Welcome to SageOS v" SAGEOS_VERSION ".\nType help for commands.\n";
    static const char ver[]  = "SageOS " SAGEOS_VERSION " modular kernel\n";
    static const char sh[]   = "Kernel-resident shell\n";
    static const char fb[]   = "UEFI GOP framebuffer\n";
    static const char inp[]  = "native-i8042-ps2\n";

    ramfs_create_file_ref("/etc/motd",    motd, r_strlen(motd));
    ramfs_create_file_ref("/etc/version", ver,  r_strlen(ver));
    ramfs_create_file_ref("/bin/sh",      sh,   r_strlen(sh));
    ramfs_create_file_ref("/dev/fb0",     fb,   r_strlen(fb));
    ramfs_create_file_ref("/proc/input",  inp,  r_strlen(inp));

    /* ELF binary */
    ramfs_create_file_ref("/bin/hello", (const char *)bin_hello, sizeof(bin_hello));

    /* Sage test files */
    ramfs_create_file_ref("/etc/test.sage",     test_sage_source, r_strlen(test_sage_source));
    ramfs_create_file_ref("/etc/test_err.sage", test_error_sage_source, r_strlen(test_error_sage_source));
    ramfs_create_file_ref("/etc/hello_native.sagec", (const char *)hello_kernel_sagec, sizeof(hello_kernel_sagec));

    /* Embed external SageLang commands */
    ramfs_embed_commands();
}

/* -----------------------------------------------------------------------
 * Legacy API — backward compatibility
 * ----------------------------------------------------------------------- */

const char *ramfs_find(const char *path) {
    int idx = resolve_path(path);
    if (idx < 0) return NULL;
    RamfsInode *n = &g_inodes[idx];
    if (n->type != VFS_FILE) return NULL;
    return (const char *)n->data;
}

uint64_t ramfs_find_size(const char *path, const char **out_data) {
    int idx = resolve_path(path);
    if (idx < 0) { *out_data = NULL; return 0; }
    RamfsInode *n = &g_inodes[idx];
    if (n->type != VFS_FILE) { *out_data = NULL; return 0; }
    *out_data = (const char *)n->data;
    return n->size;
}

void ramfs_ls(void) {
    int root = find_root();
    if (root < 0) return;

    /* Recursively print all files */
    for (int i = 0; i < g_inode_count; i++) {
        if (!g_inodes[i].active) continue;
        if (g_inodes[i].type == VFS_FILE) {
            /* Reconstruct path */
            char path[VFS_MAX_PATH];
            int parts[16];
            int depth = 0;

            int cur = i;
            while (cur > 0 && depth < 16) {
                parts[depth++] = cur;
                cur = g_inodes[cur].parent;
            }

            int pos = 0;
            for (int d = depth - 1; d >= 0; d--) {
                if (pos < VFS_MAX_PATH - 1) path[pos++] = '/';
                const char *n = g_inodes[parts[d]].name;
                while (*n && pos < VFS_MAX_PATH - 1) path[pos++] = *n++;
            }
            path[pos] = 0;

            console_write("\n");
            console_write(path);
        }
    }
}
