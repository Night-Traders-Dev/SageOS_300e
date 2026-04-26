#ifndef SAGEOS_FAT32_H
#define SAGEOS_FAT32_H

#include <stdint.h>
#include <stddef.h>
#include "vfs.h"

/* Initialize FAT32 from the primary ATA disk. Returns 1 on success. */
int fat32_init(void);

/* Check if FAT32 was successfully mounted. */
int fat32_is_available(void);

/* Storage info: total/free KB. Returns 1 if free_kb is valid. */
int fat32_storage_info(uint32_t *total_kb, uint32_t *free_kb);

/* Get the VFS backend for FAT32 (read-only) */
VfsBackend *fat32_get_backend(void);

/* -----------------------------------------------------------------------
 * Legacy API — kept for backward compatibility
 * ----------------------------------------------------------------------- */

void fat32_ls(void);
int  fat32_cat(const char *path);

/* -----------------------------------------------------------------------
 * New API — used by VFS backend
 * ----------------------------------------------------------------------- */

/* Stat a file/directory by path. Returns 0 on success. */
int fat32_stat(const char *path, VfsStat *out);

/* List entries in a directory. Returns entry count or negative error. */
int fat32_readdir(const char *path, VfsDirEntry *entries, int max_entries);

/* Read bytes from a file. Returns bytes read or negative error. */
int fat32_read(const char *path, uint64_t offset, void *buffer, size_t size);

#endif
