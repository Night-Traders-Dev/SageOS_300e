#ifndef SAGEOS_FAT32_H
#define SAGEOS_FAT32_H

#include <stdint.h>

int  fat32_init(void);
int  fat32_is_available(void);
void fat32_ls(void);
int  fat32_cat(const char *path);

/*
 * fat32_storage_info - return total and free space from the BPB / FSInfo.
 *
 * total_kb and free_kb are set in kibibytes.  free_kb is only valid when
 * the return value is non-zero (FSInfo sector free-cluster count was valid).
 * Returns 0 if the free count could not be read or was flagged invalid.
 */
int  fat32_storage_info(uint32_t *total_kb, uint32_t *free_kb);

#endif
