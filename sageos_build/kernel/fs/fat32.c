#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "fat32.h"

#define FAT32_PARTITION_START_LBA 2048
#define FAT32_ENTRY_SIZE 32
#define FAT32_ATTR_LONG_NAME 0x0F
#define FAT32_ATTR_DIRECTORY 0x10

typedef struct {
    uint8_t jump[3];
    uint8_t oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t fat_count;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t media_type;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t version;
    uint32_t root_cluster;
    uint16_t fs_info;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved0;
    uint8_t boot_signature;
    uint32_t volume_id;
    char volume_label[11];
    char fs_type[8];
} __attribute__((packed)) FAT32_BPB;

/* FAT32 FSInfo sector layout (offset within the sector) */
typedef struct {
    uint32_t lead_sig;          /* 0x41615252 */
    uint8_t  reserved1[480];
    uint32_t struc_sig;         /* 0x61417272 */
    uint32_t free_count;        /* 0xFFFFFFFF = unknown */
    uint32_t next_free;
    uint8_t  reserved2[12];
    uint32_t trail_sig;         /* 0xAA550000 */
} __attribute__((packed)) FAT32_FSInfo;

typedef struct {
    char name[8];
    char ext[3];
    uint8_t attr;
    uint8_t reserved;
    uint8_t create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t first_cluster_hi;
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} __attribute__((packed)) FAT32_DirEntry;

static int fat32_available;
static uint32_t fat32_root_cluster;
static uint16_t fat32_sectors_per_cluster;
static uint16_t fat32_reserved_sectors;
static uint8_t fat32_fat_count;
static uint32_t fat32_fat_size;
static uint32_t fat32_total_sectors;   /* from BPB total_sectors_32 */
static uint16_t fat32_fsinfo_sector;   /* BPB fs_info field */
static uint16_t fat32_bytes_per_sector;

extern void ata_read_sector(uint32_t lba, uint16_t *buffer);

static void fat32_print_name(const FAT32_DirEntry *entry) {
    char name[13];
    size_t len = 0;

    for (size_t i = 0; i < 8 && entry->name[i] != ' '; i++) {
        name[len++] = entry->name[i];
    }

    if (entry->ext[0] != ' ') {
        name[len++] = '.';
        for (size_t i = 0; i < 3 && entry->ext[i] != ' '; i++) {
            name[len++] = entry->ext[i];
        }
    }

    name[len] = 0;
    console_write(name);
}

static uint32_t fat32_cluster_to_lba(uint32_t cluster) {
    uint32_t data_start = FAT32_PARTITION_START_LBA +
        fat32_reserved_sectors +
        fat32_fat_count * fat32_fat_size;

    return data_start + (cluster - 2) * fat32_sectors_per_cluster;
}

static int fat32_read_sector(uint32_t lba, uint8_t *buffer) {
    ata_read_sector(lba, (uint16_t *)buffer);
    return 1;
}

static int streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static uint32_t fat32_next_cluster(uint32_t cluster) {
    uint8_t sector[512];
    uint32_t fat_offset = cluster * 4;
    uint32_t lba = FAT32_PARTITION_START_LBA + fat32_reserved_sectors + (fat_offset / 512);
    uint32_t index = fat_offset % 512;

    fat32_read_sector(lba, sector);
    uint32_t entry = *(uint32_t *)&sector[index];
    return entry & 0x0FFFFFFF;
}

static int fat32_is_end_of_chain(uint32_t entry) {
    return entry >= 0x0FFFFFF8;
}

static int fat32_find_entry_in_cluster(uint32_t cluster, const char *name, FAT32_DirEntry *out_entry) {
    uint8_t sector[512];

    while (!fat32_is_end_of_chain(cluster)) {
        for (uint32_t sector_idx = 0; sector_idx < fat32_sectors_per_cluster; sector_idx++) {
            uint32_t lba = fat32_cluster_to_lba(cluster) + sector_idx;
            fat32_read_sector(lba, sector);

            for (uint32_t offset = 0; offset < 512; offset += FAT32_ENTRY_SIZE) {
                FAT32_DirEntry *entry = (FAT32_DirEntry *)(sector + offset);

                if ((uint8_t)entry->name[0] == 0x00) {
                    return 0;
                }

                if ((uint8_t)entry->name[0] == 0xE5) {
                    continue;
                }

                if (entry->attr == FAT32_ATTR_LONG_NAME) {
                    continue;
                }

                char entry_name[13];
                size_t len = 0;
                for (size_t i = 0; i < 8 && entry->name[i] != ' '; i++) {
                    entry_name[len++] = entry->name[i];
                }
                if (entry->ext[0] != ' ') {
                    entry_name[len++] = '.';
                    for (size_t i = 0; i < 3 && entry->ext[i] != ' '; i++) {
                        entry_name[len++] = entry->ext[i];
                    }
                }
                entry_name[len] = 0;

                if (len > 0 && entry_name[0] != '\0' && streq(entry_name, name)) {
                    for (size_t i = 0; i < sizeof(FAT32_DirEntry); i++) {
                        ((uint8_t *)out_entry)[i] = ((uint8_t *)entry)[i];
                    }
                    return 1;
                }
            }
        }

        cluster = fat32_next_cluster(cluster);
    }

    return 0;
}

static int fat32_find_root_entry(const char *path, FAT32_DirEntry *out_entry) {
    if (*path == '/') {
        path++;
    }

    if (*path == 0) {
        return 0;
    }

    uint32_t current_cluster = fat32_root_cluster;
    char segment[13];
    size_t i = 0;

    while (1) {
        if (*path == '/' || *path == '\0') {
            segment[i] = '\0';
            if (!fat32_find_entry_in_cluster(current_cluster, segment, out_entry)) {
                return 0;
            }

            if (*path == '\0') {
                return 1;
            }

            if (!(out_entry->attr & FAT32_ATTR_DIRECTORY)) {
                return 0;
            }

            uint32_t next_cluster = ((uint32_t)out_entry->first_cluster_hi << 16) | out_entry->first_cluster_lo;
            if (next_cluster < 2) {
                return 0;
            }

            current_cluster = next_cluster;
            path++;
            i = 0;
            continue;
        }

        if (i < sizeof(segment) - 1) {
            segment[i++] = *path;
        }
        path++;
    }
}

int fat32_init(void) {
    uint8_t buffer[512];
    fat32_read_sector(FAT32_PARTITION_START_LBA, buffer);
    FAT32_BPB *bpb = (FAT32_BPB *)buffer;

    if (bpb->bytes_per_sector != 512 || bpb->fat_count == 0 || bpb->fat_size_32 == 0) {
        fat32_available = 0;
        return 0;
    }

    fat32_available        = 1;
    fat32_sectors_per_cluster = bpb->sectors_per_cluster;
    fat32_reserved_sectors = bpb->reserved_sectors;
    fat32_fat_count        = bpb->fat_count;
    fat32_fat_size         = bpb->fat_size_32;
    fat32_root_cluster     = bpb->root_cluster;
    fat32_total_sectors    = bpb->total_sectors_32;
    fat32_fsinfo_sector    = bpb->fs_info;
    fat32_bytes_per_sector = bpb->bytes_per_sector;

    console_write("\nFAT32: Mounted on primary master");
    console_write("\n  partition start LBA: ");
    console_u32(FAT32_PARTITION_START_LBA);
    console_write("\n  root cluster: ");
    console_u32(fat32_root_cluster);
    console_write("\n  sectors per cluster: ");
    console_u32(fat32_sectors_per_cluster);
    console_write("\n  fat size sectors: ");
    console_u32(fat32_fat_size);
    return 1;
}

int fat32_is_available(void) {
    return fat32_available;
}

/*
 * fat32_storage_info
 *
 * total_kb = (total_sectors_32 * bytes_per_sector) / 1024
 * free_kb  = (free_cluster_count * sectors_per_cluster * bytes_per_sector) / 1024
 *
 * Returns 1 if free_kb is valid (FSInfo lead/struc signatures present and
 * free_count != 0xFFFFFFFF), 0 otherwise (total_kb is still set).
 */
int fat32_storage_info(uint32_t *total_kb, uint32_t *free_kb) {
    if (!fat32_available) {
        *total_kb = 0;
        *free_kb  = 0;
        return 0;
    }

    /* Total size from BPB */
    uint64_t total_bytes = (uint64_t)fat32_total_sectors * fat32_bytes_per_sector;
    *total_kb = (uint32_t)(total_bytes / 1024ULL);

    /* Free count from FSInfo sector */
    *free_kb = 0;
    if (!fat32_fsinfo_sector) {
        return 0;
    }

    uint8_t buf[512];
    fat32_read_sector(FAT32_PARTITION_START_LBA + fat32_fsinfo_sector, buf);
    FAT32_FSInfo *fi = (FAT32_FSInfo *)buf;

    if (fi->lead_sig  != 0x41615252U ||
        fi->struc_sig != 0x61417272U ||
        fi->free_count == 0xFFFFFFFFU) {
        return 0;
    }

    uint64_t free_bytes = (uint64_t)fi->free_count *
                          fat32_sectors_per_cluster *
                          fat32_bytes_per_sector;
    *free_kb = (uint32_t)(free_bytes / 1024ULL);
    return 1;
}

void fat32_ls(void) {
    if (!fat32_available) {
        console_write("\nFAT32 filesystem unavailable");
        return;
    }

    uint32_t cluster = fat32_root_cluster;
    uint8_t sector[512];

    console_write("\n/FAT32:");
    while (!fat32_is_end_of_chain(cluster)) {
        for (uint32_t sector_idx = 0; sector_idx < fat32_sectors_per_cluster; sector_idx++) {
            uint32_t lba = fat32_cluster_to_lba(cluster) + sector_idx;
            fat32_read_sector(lba, sector);

            for (uint32_t offset = 0; offset < 512; offset += FAT32_ENTRY_SIZE) {
                FAT32_DirEntry *entry = (FAT32_DirEntry *)(sector + offset);

                if ((uint8_t)entry->name[0] == 0x00) {
                    return;
                }

                if ((uint8_t)entry->name[0] == 0xE5) {
                    continue;
                }

                if (entry->attr == FAT32_ATTR_LONG_NAME) {
                    continue;
                }

                console_write("\n");
                fat32_print_name(entry);

                if (entry->attr & FAT32_ATTR_DIRECTORY) {
                    console_write("/");
                }
            }
        }

        cluster = fat32_next_cluster(cluster);
    }
}

int fat32_cat(const char *path) {
    if (!fat32_available) {
        return 0;
    }

    FAT32_DirEntry entry;
    if (!fat32_find_root_entry(path, &entry)) {
        return 0;
    }

    if (entry.attr & FAT32_ATTR_DIRECTORY) {
        console_write("\ncat: cannot print directory: ");
        console_write(path);
        return 1;
    }

    uint32_t cluster = ((uint32_t)entry.first_cluster_hi << 16) | entry.first_cluster_lo;
    uint32_t remaining = entry.file_size;
    uint8_t sector[512];

    console_write("\n");
    while (remaining > 0 && !fat32_is_end_of_chain(cluster)) {
        for (uint32_t sector_idx = 0; sector_idx < fat32_sectors_per_cluster && remaining > 0; sector_idx++) {
            uint32_t lba = fat32_cluster_to_lba(cluster) + sector_idx;
            fat32_read_sector(lba, sector);

            uint32_t to_print = remaining < 512 ? remaining : 512;
            for (uint32_t i = 0; i < to_print; i++) {
                console_putc((char)sector[i]);
            }

            remaining -= to_print;
        }

        cluster = fat32_next_cluster(cluster);
    }

    return 1;
}
