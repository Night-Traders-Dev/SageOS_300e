#include <stdint.h>
#include <stddef.h>
#include "console.h"

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
    /* ... more fields ... */
} __attribute__((packed)) FAT32_BPB;

extern void ata_read_sector(uint32_t lba, uint16_t *buffer);

void fat32_init(void) {
    uint16_t buffer[256];
    ata_read_sector(0, buffer);
    FAT32_BPB *bpb = (FAT32_BPB *)buffer;

    if (bpb->bytes_per_sector != 512) {
        return;
    }

    console_write("\nFAT32: Mounted on primary master");
    console_write("\n  sectors per cluster: ");
    console_u32(bpb->sectors_per_cluster);
}
