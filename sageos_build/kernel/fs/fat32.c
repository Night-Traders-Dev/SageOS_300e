#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "ata.h"
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

typedef struct {
    uint32_t lead_sig;          /* 0x41615252 */
    uint8_t  reserved1[480];
    uint32_t struc_sig;         /* 0x61417272 */
    uint32_t free_count;        /* 0xFFFFFFFF = unknown */
    uint32_t next_free;
    uint8_t  reserved2[12];
    uint32_t trail_sig;         /* 0xAA550000 */
} __attribute__((packed)) FAT32_FSInfo;

static int fat32_is_end_of_chain(uint32_t entry) {
    return entry >= 0x0FFFFFF8;
}

#define fat32_next_cluster fat32_read_fat_entry

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
    return ata_read_sector(lba, (uint16_t *)buffer);
}

static int streq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
        a++;
        b++;
    }
    char ca = *a;
    char cb = *b;
    if (ca >= 'a' && ca <= 'z') ca -= 32;
    if (cb >= 'a' && cb <= 'z') cb -= 32;
    return ca == cb;
}

static uint32_t fat32_read_fat_entry(uint32_t cluster) {
    uint8_t sector[512];
    uint32_t fat_offset = cluster * 4;
    uint32_t lba = FAT32_PARTITION_START_LBA + fat32_reserved_sectors + (fat_offset / 512);
    uint32_t index = fat_offset % 512;

    fat32_read_sector(lba, sector);
    uint32_t entry = *(uint32_t *)&sector[index];
    return entry & 0x0FFFFFFF;
}

static int fat32_write_fat_entry(uint32_t cluster, uint32_t value) {
    uint8_t sector[512];
    uint32_t fat_offset = cluster * 4;
    uint32_t lba = FAT32_PARTITION_START_LBA + fat32_reserved_sectors + (fat_offset / 512);
    uint32_t index = fat_offset % 512;

    if (!fat32_read_sector(lba, sector)) return 0;
    uint32_t entry = *(uint32_t *)&sector[index];
    entry = (entry & 0xF0000000) | (value & 0x0FFFFFFF);
    *(uint32_t *)&sector[index] = entry;

    return ata_write_sector(lba, (uint16_t *)sector);
}

static uint32_t fat32_find_free_cluster(void) {
    /* Simple linear scan from cluster 2 */
    for (uint32_t cluster = 2; cluster < (fat32_fat_size * 512 / 4); cluster++) {
        if (fat32_read_fat_entry(cluster) == 0) {
            return cluster;
        }
    }
    return 0;
}

static int fat32_update_directory_entry(uint32_t cluster, const char *name, FAT32_DirEntry *new_entry) {
    uint8_t sector[512];

    while (!fat32_is_end_of_chain(cluster)) {
        for (uint32_t sector_idx = 0; sector_idx < fat32_sectors_per_cluster; sector_idx++) {
            uint32_t lba = fat32_cluster_to_lba(cluster) + sector_idx;
            fat32_read_sector(lba, sector);

            for (uint32_t offset = 0; offset < 512; offset += FAT32_ENTRY_SIZE) {
                FAT32_DirEntry *entry = (FAT32_DirEntry *)(sector + offset);
                
                char entry_name[13];
                size_t len = 0;
                for (size_t i = 0; i < 8 && entry->name[i] != ' '; i++) entry_name[len++] = entry->name[i];
                if (entry->ext[0] != ' ') {
                    entry_name[len++] = '.';
                    for (size_t i = 0; i < 3 && entry->ext[i] != ' '; i++) entry_name[len++] = entry->ext[i];
                }
                entry_name[len] = 0;

                if (len > 0 && streq(entry_name, name)) {
                    *entry = *new_entry;
                    return ata_write_sector(lba, (uint16_t *)sector);
                }
            }
        }
        cluster = fat32_next_cluster(cluster);
    }
    return 0;
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

    if (!ata_is_available()) {
        fat32_available = 0;
        return 0;
    }

    if (!fat32_read_sector(FAT32_PARTITION_START_LBA, buffer)) {
        fat32_available = 0;
        return 0;
    }
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
    if (fat32_available) return 1;
    extern SageOSBootInfo *kernel_get_boot_info(void);
    SageOSBootInfo *info = kernel_get_boot_info();
    if (info && info->boot_services_active && info->root_dir) {
        return 1;
    }
    return 0;
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
    if (!fat32_read_sector(FAT32_PARTITION_START_LBA + fat32_fsinfo_sector, buf)) {
        return 0;
    }
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

/* -----------------------------------------------------------------------
 * New VFS-compatible API
 * ----------------------------------------------------------------------- */

static void fat32_entry_to_name(const FAT32_DirEntry *entry, char *out, size_t out_size) {
    size_t len = 0;
    for (size_t i = 0; i < 8 && entry->name[i] != ' ' && len < out_size - 1; i++) {
        out[len++] = entry->name[i];
    }
    if (entry->ext[0] != ' ') {
        if (len < out_size - 1) out[len++] = '.';
        for (size_t i = 0; i < 3 && entry->ext[i] != ' ' && len < out_size - 1; i++) {
            out[len++] = entry->ext[i];
        }
    }
    out[len] = 0;
}

int fat32_stat(const char *path, VfsStat *out) {
    if (!fat32_available) return VFS_EIO;

    /* Root directory */
    if (path[0] == '/' && path[1] == 0) {
        out->name[0] = '/'; out->name[1] = 0;
        out->type = VFS_DIRECTORY;
        out->size = 0;
        out->mode = 0755;
        return VFS_OK;
    }

    FAT32_DirEntry entry;
    if (!fat32_find_root_entry(path, &entry)) {
        return VFS_ENOENT;
    }

    fat32_entry_to_name(&entry, out->name, VFS_NAME_MAX);
    out->type = (entry.attr & FAT32_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
    out->size = entry.file_size;
    out->mode = (entry.attr & FAT32_ATTR_DIRECTORY) ? 0755 : 0644;
    return VFS_OK;
}

int fat32_readdir(const char *path, VfsDirEntry *entries, int max_entries) {
    if (!fat32_available) return VFS_EIO;

    uint32_t cluster;

    if (path[0] == '/' && path[1] == 0) {
        /* Root directory */
        cluster = fat32_root_cluster;
    } else {
        FAT32_DirEntry dir_entry;
        if (!fat32_find_root_entry(path, &dir_entry)) {
            return VFS_ENOENT;
        }
        if (!(dir_entry.attr & FAT32_ATTR_DIRECTORY)) {
            return VFS_ENOTDIR;
        }
        cluster = ((uint32_t)dir_entry.first_cluster_hi << 16) | dir_entry.first_cluster_lo;
    }

    uint8_t sector[512];
    int count = 0;

    while (!fat32_is_end_of_chain(cluster) && count < max_entries) {
        for (uint32_t sector_idx = 0; sector_idx < fat32_sectors_per_cluster; sector_idx++) {
            uint32_t lba = fat32_cluster_to_lba(cluster) + sector_idx;
            fat32_read_sector(lba, sector);

            for (uint32_t offset = 0; offset < 512 && count < max_entries; offset += FAT32_ENTRY_SIZE) {
                FAT32_DirEntry *e = (FAT32_DirEntry *)(sector + offset);

                if ((uint8_t)e->name[0] == 0x00) goto done;
                if ((uint8_t)e->name[0] == 0xE5) continue;
                if (e->attr == FAT32_ATTR_LONG_NAME) continue;

                fat32_entry_to_name(e, entries[count].name, VFS_NAME_MAX);
                entries[count].type = (e->attr & FAT32_ATTR_DIRECTORY) ? VFS_DIRECTORY : VFS_FILE;
                entries[count].size = e->file_size;
                count++;
            }
        }
        cluster = fat32_next_cluster(cluster);
    }

done:
    return count;
}

int fat32_read(const char *path, uint64_t offset, void *buffer, size_t size) {
    if (!fat32_available) return VFS_EIO;

    FAT32_DirEntry entry;
    if (!fat32_find_root_entry(path, &entry)) {
        return VFS_ENOENT;
    }

    if (entry.attr & FAT32_ATTR_DIRECTORY) {
        return VFS_EISDIR;
    }

    if (offset >= entry.file_size) return 0;

    size_t avail = entry.file_size - (size_t)offset;
    if (size > avail) size = avail;

    uint32_t cluster = ((uint32_t)entry.first_cluster_hi << 16) | entry.first_cluster_lo;
    uint32_t cluster_bytes = (uint32_t)fat32_sectors_per_cluster * 512;

    /* Skip clusters until we reach the offset */
    uint64_t skip = offset;
    while (skip >= cluster_bytes && !fat32_is_end_of_chain(cluster)) {
        cluster = fat32_next_cluster(cluster);
        skip -= cluster_bytes;
    }

    uint8_t sector[512];
    uint8_t *out = (uint8_t *)buffer;
    size_t bytes_read = 0;

    while (bytes_read < size && !fat32_is_end_of_chain(cluster)) {
        for (uint32_t sector_idx = 0;
             sector_idx < fat32_sectors_per_cluster && bytes_read < size;
             sector_idx++) {
            uint32_t lba = fat32_cluster_to_lba(cluster) + sector_idx;
            uint32_t sector_offset = 0;

            /* Handle initial offset within the first sector */
            if (skip > 0) {
                if (skip >= 512) {
                    skip -= 512;
                    continue;
                }
                sector_offset = (uint32_t)skip;
                skip = 0;
            }

            fat32_read_sector(lba, sector);

            uint32_t to_copy = 512 - sector_offset;
            if (to_copy > size - bytes_read) to_copy = (uint32_t)(size - bytes_read);

            for (uint32_t i = 0; i < to_copy; i++) {
                out[bytes_read++] = sector[sector_offset + i];
            }
        }
        cluster = fat32_next_cluster(cluster);
    }

    return (int)bytes_read;
}

int fat32_write(const char *path, uint64_t offset, const void *buffer, size_t size) {
    if (!fat32_available) return VFS_EIO;

    FAT32_DirEntry entry;
    if (!fat32_find_root_entry(path, &entry)) return VFS_ENOENT;
    if (entry.attr & FAT32_ATTR_DIRECTORY) return VFS_EISDIR;

    uint32_t cluster = ((uint32_t)entry.first_cluster_hi << 16) | entry.first_cluster_lo;
    uint32_t cluster_bytes = (uint32_t)fat32_sectors_per_cluster * 512;
    uint64_t write_end = offset + size;

    /* Expand clusters if necessary */
    uint32_t current_cluster = cluster;
    while (write_end > (uint64_t)cluster_bytes) {
        uint32_t next = fat32_next_cluster(current_cluster);
        if (fat32_is_end_of_chain(next)) {
            uint32_t new_cluster = fat32_find_free_cluster();
            if (!new_cluster) return VFS_ENOSPC;
            fat32_write_fat_entry(current_cluster, new_cluster);
            fat32_write_fat_entry(new_cluster, 0x0FFFFFFF);
            next = new_cluster;
        }
        current_cluster = next;
        write_end -= cluster_bytes;
    }

    /* Perform write */
    uint8_t sector[512];
    const uint8_t *in = (const uint8_t *)buffer;
    size_t bytes_written = 0;
    current_cluster = cluster;
    uint64_t skip = offset;

    while (bytes_written < size && !fat32_is_end_of_chain(current_cluster)) {
        for (uint32_t sector_idx = 0;
             sector_idx < fat32_sectors_per_cluster && bytes_written < size;
             sector_idx++) {
            uint32_t lba = fat32_cluster_to_lba(current_cluster) + sector_idx;
            uint32_t sector_offset = 0;

            if (skip >= 512) {
                skip -= 512;
                continue;
            }
            sector_offset = (uint32_t)skip;
            skip = 0;

            if (sector_offset != 0 || (size - bytes_written) < 512) {
                fat32_read_sector(lba, sector);
            }

            uint32_t to_copy = 512 - sector_offset;
            if (to_copy > size - bytes_written) to_copy = (uint32_t)(size - bytes_written);

            for (uint32_t i = 0; i < to_copy; i++) sector[sector_offset + i] = in[bytes_written++];
            ata_write_sector(lba, (uint16_t *)sector);
        }
        current_cluster = fat32_next_cluster(current_cluster);
    }

    if (offset + size > entry.file_size) {
        entry.file_size = (uint32_t)(offset + size);
        fat32_update_directory_entry(fat32_root_cluster, path, &entry);
    }

    return (int)bytes_written;
}

#include "bootinfo.h"

// Forward declaration of EFI protocols
struct EfiFileProtocol;

typedef uint64_t (__attribute__((ms_abi)) *EfiFileOpen)(
    struct EfiFileProtocol *This,
    struct EfiFileProtocol **NewHandle,
    const uint16_t *FileName,
    uint64_t OpenMode,
    uint64_t Attributes
);

typedef uint64_t (__attribute__((ms_abi)) *EfiFileClose)(
    struct EfiFileProtocol *This
);

typedef uint64_t (__attribute__((ms_abi)) *EfiFileRead)(
    struct EfiFileProtocol *This,
    uint64_t *BufferSize,
    void *Buffer
);

typedef uint64_t (__attribute__((ms_abi)) *EfiFileWrite)(
    struct EfiFileProtocol *This,
    uint64_t *BufferSize,
    const void *Buffer
);

typedef uint64_t (__attribute__((ms_abi)) *EfiFileSetPosition)(
    struct EfiFileProtocol *This,
    uint64_t Position
);

typedef uint64_t (__attribute__((ms_abi)) *EfiFileGetPosition)(
    struct EfiFileProtocol *This,
    uint64_t *Position
);

typedef struct EfiFileProtocol {
    uint64_t Revision;
    EfiFileOpen Open;
    EfiFileClose Close;
    void *Delete;
    EfiFileRead Read;
    EfiFileWrite Write;
    EfiFileGetPosition GetPosition;
    EfiFileSetPosition SetPosition;
    void *GetInfo;
    void *SetInfo;
    void *Flush;
} EfiFileProtocol;

extern SageOSBootInfo *kernel_get_boot_info(void);

static void ascii_to_utf16_path(const char *src, uint16_t *dst, int max_len) {
    int i = 0;
    // Always start with backslash for root volume path
    dst[i++] = '\\';
    while (*src && i < max_len - 2) {
        char c = *src++;
        if (c == '/') c = '\\';
        dst[i++] = c;
    }
    dst[i] = 0;
}

static int fat32_be_stat(VfsBackend *self, const char *rel_path, VfsStat *out) {
    (void)self;
    SageOSBootInfo *info = kernel_get_boot_info();
    if (info && info->boot_services_active && info->root_dir) {
        EfiFileProtocol *root = (EfiFileProtocol *)info->root_dir;
        uint16_t wpath[256];
        ascii_to_utf16_path(rel_path, wpath, 256);

        EfiFileProtocol *file = NULL;
        uint64_t status = root->Open(root, &file, wpath, 1 /* READ */, 0);
        if (status != 0) {
            return -2; // ENOENT
        }

        uint64_t file_size = 0;
        if (file->SetPosition) {
            file->SetPosition(file, 0xFFFFFFFFFFFFFFFFULL);
        }
        if (file->GetPosition) {
            file->GetPosition(file, &file_size);
        }

        // Restore position
        if (file->SetPosition) {
            file->SetPosition(file, 0);
        }

        int i = 0;
        const char *p = rel_path;
        while (*p) p++;
        while (p > rel_path && *(p - 1) != '/') p--;
        while (*p && i < 63) out->name[i++] = *p++;
        out->name[i] = 0;

        out->type = VFS_FILE;
        out->size = file_size;

        file->Close(file);
        return 0;
    }
    return fat32_stat(rel_path, out);
}

static int fat32_be_readdir(VfsBackend *self, const char *rel_path,
                            VfsDirEntry *entries, int max_entries) {
    (void)self;
    SageOSBootInfo *info = kernel_get_boot_info();
    if (info && info->boot_services_active && info->root_dir) {
        EfiFileProtocol *root = (EfiFileProtocol *)info->root_dir;
        uint16_t wpath[256];
        ascii_to_utf16_path(rel_path, wpath, 256);

        EfiFileProtocol *dir = NULL;
        uint64_t status = root->Open(root, &dir, wpath, 1 /* READ */, 0);
        if (status != 0) return -2;

        int count = 0;
        uint8_t info_buf[512];
        while (count < max_entries) {
            uint64_t buf_size = sizeof(info_buf);
            status = dir->Read(dir, &buf_size, info_buf);
            if (status != 0 || buf_size == 0) break;

            /* EFI_FILE_INFO: Size(0), FileSize(8), PhysicalSize(16), CreateTime(24), 
               LastAccessTime(40), ModificationTime(56), Attribute(72), FileName(80) */
            uint64_t attr = *(uint64_t *)(info_buf + 72);
            uint16_t *wname = (uint16_t *)(info_buf + 80);

            /* Convert UTF-16 to ASCII */
            int i = 0;
            while (wname[i] && i < VFS_NAME_MAX - 1) {
                entries[count].name[i] = (char)(wname[i] & 0x7F);
                i++;
            }
            entries[count].name[i] = 0;

            if (entries[count].name[0] == '.' && (entries[count].name[1] == 0 || (entries[count].name[1] == '.' && entries[count].name[2] == 0))) {
                continue; /* Skip . and .. */
            }

            entries[count].type = (attr & 0x10) ? VFS_DIRECTORY : VFS_FILE;
            entries[count].size = *(uint64_t *)(info_buf + 8);
            count++;
        }

        dir->Close(dir);
        return count;
    }
    return fat32_readdir(rel_path, entries, max_entries);
}

static int fat32_be_read(VfsBackend *self, const char *rel_path,
                         uint64_t offset, void *buffer, size_t size) {
    (void)self;
    SageOSBootInfo *info = kernel_get_boot_info();
    if (info && info->boot_services_active && info->root_dir) {
        EfiFileProtocol *root = (EfiFileProtocol *)info->root_dir;
        uint16_t wpath[256];
        ascii_to_utf16_path(rel_path, wpath, 256);

        EfiFileProtocol *file = NULL;
        uint64_t status = root->Open(root, &file, wpath, 1 /* READ */, 0);
        if (status != 0) {
            return -2; // ENOENT
        }

        if (file->SetPosition) {
            file->SetPosition(file, offset);
        }

        uint64_t read_len = size;
        status = file->Read(file, &read_len, buffer);

        file->Close(file);

        if (status != 0) return -5; // EIO
        return (int)read_len;
    }
    return fat32_read(rel_path, offset, buffer, size);
}

static int fat32_be_write(VfsBackend *self, const char *rel_path,
                          uint64_t offset, const void *buffer, size_t size) {
    (void)self;
    SageOSBootInfo *info = kernel_get_boot_info();
    if (info && info->boot_services_active && info->root_dir) {
        EfiFileProtocol *root = (EfiFileProtocol *)info->root_dir;
        uint16_t wpath[256];
        ascii_to_utf16_path(rel_path, wpath, 256);

        EfiFileProtocol *file = NULL;
        uint64_t status = root->Open(root, &file, wpath, 3 /* READ | WRITE */, 0);
        if (status != 0) {
            // Try to create it
            status = root->Open(root, &file, wpath, 0x8000000000000003ULL /* CREATE | READ | WRITE */, 0);
            if (status != 0) return -5; // EIO
        }

        if (file->SetPosition) {
            file->SetPosition(file, offset);
        }

        uint64_t write_len = size;
        status = file->Write(file, &write_len, buffer);

        file->Close(file);

        if (status != 0) return -5;
        return (int)write_len;
    }
    return fat32_write(rel_path, offset, buffer, size);
}

static VfsBackend g_fat32_backend = {
    .name    = "fat32",
    .stat    = fat32_be_stat,
    .readdir = fat32_be_readdir,
    .read    = fat32_be_read,
    .write   = fat32_be_write,
    .mkdir   = NULL,
    .create  = NULL,
    .unlink  = NULL,
    .priv    = NULL
};

VfsBackend *fat32_get_backend(void) {
    return &g_fat32_backend;
}

/*
 * fat32_uefi_write — write directly via UEFI EFI_FILE_PROTOCOL,
 * bypassing the VFS/SageLang bridge.  Used by subsystems (e.g. wifi
 * credential persistence) that need to write to the FAT32 ESP while
 * the SageLang VFS VM is active and might intercept vfs_write().
 *
 * rel_path: path relative to the ESP root (e.g. "WIFI.CFG")
 * Returns bytes written, or < 0 on error.
 */
int fat32_uefi_write(const char *rel_path, const void *buffer, size_t size) {
    SageOSBootInfo *info = kernel_get_boot_info();
    if (!info || !info->boot_services_active || !info->root_dir) {
        /* Fallback: native ATA-based write */
        return fat32_write(rel_path, 0, buffer, size);
    }

    EfiFileProtocol *root = (EfiFileProtocol *)info->root_dir;
    uint16_t wpath[256];
    ascii_to_utf16_path(rel_path, wpath, 256);

    EfiFileProtocol *file = NULL;

    /* Try open for write (file must exist) */
    uint64_t status = root->Open(root, &file, wpath,
                                 3ULL /* EFI_FILE_MODE_READ | WRITE */, 0);
    if (status != 0) {
        /* Create the file */
        status = root->Open(root, &file, wpath,
                            0x8000000000000003ULL /* CREATE | READ | WRITE */,
                            0 /* normal file */);
        if (status != 0) return -5; /* EIO */
    }

    /* Seek to beginning (overwrite) */
    if (file->SetPosition) file->SetPosition(file, 0);

    uint64_t write_len = size;
    status = file->Write(file, &write_len, buffer);

    /* Flush to ensure FAT32 directory entries are committed */
    if (file->Flush) {
        typedef uint64_t (__attribute__((ms_abi)) *EfiFlushFn)(EfiFileProtocol *);
        ((EfiFlushFn)file->Flush)(file);
    }

    file->Close(file);

    if (status != 0) return -5;
    return (int)write_len;
}
