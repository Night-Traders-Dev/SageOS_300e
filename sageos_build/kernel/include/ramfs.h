#ifndef SAGEOS_RAMFS_H
#define SAGEOS_RAMFS_H

#include <stdint.h>
#include <stddef.h>
#include "vfs.h"

/* -----------------------------------------------------------------------
 * Dynamic RamFS — inode-based in-memory filesystem
 *
 * Design inspired by SageLang lib/os/tmpfs.sage, implemented in C
 * with fixed-size structures and no dynamic allocation.
 * ----------------------------------------------------------------------- */

#define RAMFS_MAX_INODES   128
#define RAMFS_MAX_CHILDREN  32    /* max entries per directory */
#define RAMFS_DATA_POOL  65536    /* 64 KB data pool for file contents */
#define RAMFS_NAME_MAX      64

/* -----------------------------------------------------------------------
 * Public API — backend-facing (used by VFS)
 * ----------------------------------------------------------------------- */

/* Initialize the ramfs and create the root directory */
void ramfs_init(void);

/* Get the VFS backend for ramfs */
VfsBackend *ramfs_get_backend(void);

/* -----------------------------------------------------------------------
 * Legacy API — kept for backward compatibility
 * ----------------------------------------------------------------------- */

const char *ramfs_find(const char *path);
uint64_t ramfs_find_size(const char *path, const char **out_data);
void ramfs_ls(void);

/* -----------------------------------------------------------------------
 * Direct API — for kernel-internal use (e.g., pre-populating files)
 * ----------------------------------------------------------------------- */

/* Create a file with initial content (copies data into pool) */
int ramfs_create_file(const char *path, const void *data, size_t size);

/* Create a file that references external (const) data — no copy */
int ramfs_create_file_ref(const char *path, const void *data, size_t size);

/* Create a directory */
int ramfs_create_dir(const char *path);

#endif
