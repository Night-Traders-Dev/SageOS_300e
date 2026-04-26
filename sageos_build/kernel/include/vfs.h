#ifndef SAGEOS_VFS_H
#define SAGEOS_VFS_H

#include <stdint.h>
#include <stddef.h>

/* -----------------------------------------------------------------------
 * VFS — Virtual Filesystem Layer
 *
 * Provides a uniform file/directory API that filesystem backends plug into.
 * Design inspired by SageLang lib/os/vfs.sage, implemented in C for
 * bare-metal determinism and zero dynamic allocation.
 * ----------------------------------------------------------------------- */

/* Node types */
typedef enum {
    VFS_FILE,
    VFS_DIRECTORY,
    VFS_SYMLINK
} VfsNodeType;

/* File open mode flags (bitmask) */
#define VFS_O_READ    0x01
#define VFS_O_WRITE   0x02
#define VFS_O_APPEND  0x04
#define VFS_O_CREATE  0x08

/* Error codes */
#define VFS_OK        0
#define VFS_ENOENT   -2
#define VFS_EIO      -5
#define VFS_EACCES  -13
#define VFS_EEXIST  -17
#define VFS_ENOTDIR -20
#define VFS_EISDIR  -21
#define VFS_ENOSPC  -28
#define VFS_EROFS   -30
#define VFS_EINVAL  -22

/* Limits */
#define VFS_MAX_MOUNTS   8
#define VFS_MAX_PATH   256
#define VFS_NAME_MAX    64
#define VFS_DIRENT_MAX  64   /* max entries returned by readdir */

/* -----------------------------------------------------------------------
 * Stat result
 * ----------------------------------------------------------------------- */
typedef struct {
    char         name[VFS_NAME_MAX];
    VfsNodeType  type;
    uint64_t     size;
    uint32_t     mode;       /* permissions (future) */
} VfsStat;

/* -----------------------------------------------------------------------
 * Directory entry (for readdir)
 * ----------------------------------------------------------------------- */
typedef struct {
    char         name[VFS_NAME_MAX];
    VfsNodeType  type;
    uint64_t     size;
} VfsDirEntry;

/* -----------------------------------------------------------------------
 * Backend interface
 *
 * Each filesystem implements some or all of these callbacks.
 * NULL means "not supported" for that operation.
 * ----------------------------------------------------------------------- */
typedef struct VfsBackend {
    const char *name;  /* e.g. "ramfs", "fat32" */

    /* stat: fill VfsStat for a path relative to mount point.
     * Returns 0 on success, negative error code on failure. */
    int (*stat)(struct VfsBackend *self, const char *rel_path, VfsStat *out);

    /* readdir: fill array of VfsDirEntry, return count (or negative error).
     * rel_path is relative to the mount point. */
    int (*readdir)(struct VfsBackend *self, const char *rel_path,
                   VfsDirEntry *entries, int max_entries);

    /* read: read up to `size` bytes from file at `rel_path` starting at
     * `offset`. Returns bytes read (>= 0) or negative error. */
    int (*read)(struct VfsBackend *self, const char *rel_path,
                uint64_t offset, void *buffer, size_t size);

    /* write: write `size` bytes to file at `rel_path` starting at `offset`.
     * Returns bytes written (>= 0) or negative error. */
    int (*write)(struct VfsBackend *self, const char *rel_path,
                 uint64_t offset, const void *data, size_t size);

    /* mkdir: create a directory. Returns 0 or negative error. */
    int (*mkdir)(struct VfsBackend *self, const char *rel_path);

    /* create: create an empty file. Returns 0 or negative error. */
    int (*create)(struct VfsBackend *self, const char *rel_path);

    /* unlink: remove a file or empty directory. Returns 0 or negative. */
    int (*unlink)(struct VfsBackend *self, const char *rel_path);

    /* Private data for the backend */
    void *priv;
} VfsBackend;

/* -----------------------------------------------------------------------
 * VFS Node (legacy compat — still used by some callers)
 * ----------------------------------------------------------------------- */
typedef struct VfsNode {
    char name[VFS_NAME_MAX];
    VfsNodeType type;
    uint64_t size;
    struct VfsNode *next;
    int (*read)(struct VfsNode *node, uint64_t offset, size_t size, void *buffer);
    void *priv;
} VfsNode;

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

/* Initialize the VFS and auto-mount default backends */
void vfs_init(void);

/* Mount a backend at a path (e.g. "/" or "/fat32") */
int vfs_mount(const char *mount_path, VfsBackend *backend);

/* Unmount a path */
int vfs_umount(const char *mount_path);

/* Stat a file or directory */
int vfs_stat(const char *path, VfsStat *out);

/* List directory contents */
int vfs_readdir(const char *path, VfsDirEntry *entries, int max_entries);

/* Read from a file */
int vfs_read(const char *path, uint64_t offset, void *buffer, size_t size);

/* Write to a file */
int vfs_write(const char *path, uint64_t offset, const void *data, size_t size);

/* Create a directory */
int vfs_mkdir(const char *path);

/* Create an empty file */
int vfs_create(const char *path);

/* Remove a file or empty directory */
int vfs_unlink(const char *path);

/* List a directory (console output — convenience) */
void vfs_ls(const char *path);

/* Legacy compat */
VfsNode *vfs_find(const char *path);

/* Path utilities */
int vfs_normalize_path(const char *input, char *output, int output_size);

/* Error name string */
const char *vfs_strerror(int err);

#endif
