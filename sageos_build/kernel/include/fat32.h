#ifndef SAGEOS_FAT32_H
#define SAGEOS_FAT32_H

#include <stdint.h>
#include <stddef.h>
#include "vfs.h"

/* Initialize FAT32 from the primary ATA disk. Returns 1 on success. */
int fat32_init(void);

/* Check if FAT32 was successfully mounted. */
int fat32_is_available(void);

/*
 * fat32_storage_info - return total and free space from the BPB / FSInfo.
 *
 * total_kb and free_kb are set in kibibytes.  free_kb is only valid when
 * the return value is non-zero (FSInfo sector free-cluster count was valid).
 * Returns 0 if the free count could not be read or was flagged invalid.
 */
int fat32_storage_info(uint32_t *total_kb, uint32_t *free_kb);

/* Get the VFS backend for FAT32 (read-only) */
VfsBackend *fat32_get_backend(void);

/* Legacy API */
void fat32_ls(void);
int  fat32_cat(const char *path);

/* New VFS-compatible API */
int fat32_stat(const char *path, VfsStat *out);
int fat32_readdir(const char *path, VfsDirEntry *entries, int max_entries);
int fat32_read(const char *path, uint64_t offset, void *buffer, size_t size);

#endif
