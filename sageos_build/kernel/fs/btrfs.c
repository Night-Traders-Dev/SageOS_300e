#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "console.h"
#include "ata.h"
#include "btrfs.h"
#include "vfs.h"
#include "dmesg.h"

/* 
 * BTRFS support for SageOS
 * 
 * Partition Layout (assumed):
 * 1: ESP (FAT32) - LBA 2048
 * 2: Root (BTRFS) - LBA 133120 (approx, depending on ESP size)
 * 3: SWAP
 */

#define BTRFS_PARTITION_START_LBA (2048 + (64 * 1024 * 1024 / 512)) /* ESP + 64MiB */

static int btrfs_available = 0;
static btrfs_super_block g_super;

static int btrfs_read_sector(uint32_t lba, uint8_t *buffer) {
    return ata_read_sector(lba, (uint16_t *)buffer);
}

int btrfs_init(void) {
    uint8_t buffer[512];
    
    if (!ata_is_available()) {
        btrfs_available = 0;
        return 0;
    }

    /* BTRFS Superblock is at 64KiB (offset 128 sectors) */
    uint32_t super_lba = BTRFS_PARTITION_START_LBA + (BTRFS_SUPER_INFO_OFFSET / 512);
    
    if (!btrfs_read_sector(super_lba, buffer)) {
        btrfs_available = 0;
        return 0;
    }

    /* We need to read more than one sector for the full superblock (2048 bytes + reserved) 
       But the magic is in the first 1KB. */
    btrfs_super_block *sb = (btrfs_super_block *)buffer;
    
    /* Re-read to get the full SB if needed, but let's check magic first.
       Actually btrfs_super_block magic is at offset 0x40 from start of SB.
       SB starts at 0x10000. Magic at 0x10040.
    */
    
    if (sb->magic == BTRFS_MAGIC) {
        btrfs_available = 1;
        memcpy(&g_super, sb, sizeof(btrfs_super_block));
        console_write("\nBTRFS: Superblock detected on partition 2");
        dmesg_log("BTRFS: Superblock detected on partition 2");
        return 1;
    }

    btrfs_available = 0;
    return 0;
}

int btrfs_is_available(void) {
    return btrfs_available;
}

static int btrfs_be_stat(VfsBackend *self, const char *rel_path, VfsStat *out) {
    (void)self;
    (void)rel_path;
    (void)out;
    return VFS_ENOENT; /* Placeholder */
}

static int btrfs_be_readdir(VfsBackend *self, const char *rel_path,
                            VfsDirEntry *entries, int max_entries) {
    (void)self;
    (void)rel_path;
    (void)entries;
    (void)max_entries;
    return 0; /* Placeholder */
}

static int btrfs_be_read(VfsBackend *self, const char *rel_path,
                         uint64_t offset, void *buffer, size_t size) {
    (void)self;
    (void)rel_path;
    (void)offset;
    (void)buffer;
    (void)size;
    return VFS_EIO; /* Placeholder */
}

static VfsBackend g_btrfs_backend = {
    .name    = "btrfs",
    .stat    = btrfs_be_stat,
    .readdir = btrfs_be_readdir,
    .read    = btrfs_be_read,
    .write   = NULL,
    .mkdir   = NULL,
    .create  = NULL,
    .unlink  = NULL,
    .priv    = NULL
};

VfsBackend *btrfs_get_backend(void) {
    return &g_btrfs_backend;
}
