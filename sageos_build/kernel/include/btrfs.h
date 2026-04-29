#ifndef SAGEOS_BTRFS_H
#define SAGEOS_BTRFS_H

#include "vfs.h"

/* 
 * Minimal BTRFS Reader for SageOS
 * 
 * BTRFS Superblock is at 64KiB (0x10000).
 * Magic: "_BHRfS_M" (0x4D5F53665248425F)
 */

#define BTRFS_SUPER_INFO_OFFSET 0x10000
#define BTRFS_MAGIC 0x4D5F53665248425FULL

typedef struct {
    uint8_t csum[32];
    uint8_t fsid[16];
    uint64_t bytenr;
    uint64_t flags;
    uint64_t magic;
    uint64_t generation;
    uint64_t root;
    uint64_t chunk_root;
    uint64_t log_root;
    uint64_t log_root_transid;
    uint64_t total_bytes;
    uint64_t bytes_used;
    uint64_t root_dir_objectid;
    uint64_t num_devices;
    uint32_t sectorsize;
    uint32_t nodesize;
    uint32_t leafsize;
    uint32_t stripesize;
    uint32_t sys_chunk_array_size;
    uint64_t chunk_root_generation;
    uint64_t compat_flags;
    uint64_t compat_ro_flags;
    uint64_t incompat_flags;
    uint16_t csum_type;
    uint8_t root_level;
    uint8_t chunk_root_level;
    uint8_t log_root_level;
    struct {
        uint8_t uuid[16];
        uint64_t devid;
        uint64_t total_bytes;
        uint64_t bytes_used;
        uint32_t io_align;
        uint32_t io_width;
        uint32_t sector_size;
        uint64_t type;
        uint64_t generation;
        uint64_t start_offset;
        uint32_t dev_group;
        uint8_t seek_speed;
        uint8_t bandwidth;
        uint8_t uuid_inner[16];
    } __attribute__((packed)) dev_item;
    char label[256];
    uint64_t cache_generation;
    uint64_t uuid_tree_generation;
    uint8_t reserved[240];
    uint8_t sys_chunk_array[2048];
    uint8_t super_roots[512];
} __attribute__((packed)) btrfs_super_block;

int btrfs_init(void);
int btrfs_is_available(void);
VfsBackend *btrfs_get_backend(void);

#endif
