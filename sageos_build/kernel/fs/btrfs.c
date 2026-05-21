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

static int btrfs_read_node(uint64_t logical_addr, void *buffer) {
    /* For now, assume logical address is directly mapped to sector offset 
       ignoring complex chunk mapping. */
    uint32_t lba = BTRFS_PARTITION_START_LBA + (uint32_t)(logical_addr / 512);
    for (int i = 0; i < (int)(g_super.nodesize / 512); i++) {
        if (!btrfs_read_sector(lba + i, (uint8_t*)buffer + (i * 512))) return 0;
    }
    return 1;
}

int btrfs_init(void) {
    uint8_t buffer[sizeof(btrfs_super_block)];
    
    if (!ata_is_available()) {
        btrfs_available = 0;
        return 0;
    }

    uint32_t super_lba = BTRFS_PARTITION_START_LBA + (BTRFS_SUPER_INFO_OFFSET / 512);
    
    /* BTRFS superblock spans 4 sectors (2048 bytes) */
    for (int i = 0; i < (int)(sizeof(btrfs_super_block) / 512); i++) {
        if (!btrfs_read_sector(super_lba + i, buffer + (i * 512))) {
            btrfs_available = 0;
            return 0;
        }
    }

    btrfs_super_block *sb = (btrfs_super_block *)buffer;
    
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

void btrfs_ls(void) {
    if (!btrfs_available) return;
    uint8_t buffer[16384]; /* Assume max node size */
    if (!btrfs_read_node(g_super.root, buffer)) return;
    
    btrfs_leaf *leaf = (btrfs_leaf *)buffer;
    console_write("\n/BTRFS (Root Tree):");
    for (uint32_t i = 0; i < leaf->header.nritems; i++) {
        console_write("\n  ObjectID: ");
        console_hex64(leaf->items[i].key.objectid);
        console_write(" Type: ");
        console_u32(leaf->items[i].key.type);
    }
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
