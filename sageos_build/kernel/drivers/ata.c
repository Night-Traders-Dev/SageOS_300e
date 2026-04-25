#include <stdint.h>
#include "io.h"
#include "console.h"

/* Simple ATA PIO driver for the primary master */
#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERR          0x1F1
#define ATA_PRIMARY_SECCOUNT     0x1F2
#define ATA_PRIMARY_LBA_LOW      0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HIGH     0x1F5
#define ATA_PRIMARY_DRIVE        0x1F6
#define ATA_PRIMARY_STATUS       0x1F7
#define ATA_PRIMARY_COMMAND     0x1F7

void ata_read_sector(uint32_t lba, uint16_t *buffer) {
    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECCOUNT, 1);
    outb(ATA_PRIMARY_LBA_LOW, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_COMMAND, 0x20); /* Read with retry */

    while (!(inb(ATA_PRIMARY_STATUS) & 0x08)) {
        /* Wait for BSY to clear and DRQ to set */
        cpu_pause();
    }

    for (int i = 0; i < 256; i++) {
        /* Read 512 bytes (256 words) */
        buffer[i] = inw(ATA_PRIMARY_DATA);
    }
}

void ata_write_sector(uint32_t lba, const uint16_t *buffer) {
    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECCOUNT, 1);
    outb(ATA_PRIMARY_LBA_LOW, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_COMMAND, 0x30); /* Write sectors */

    while (!(inb(ATA_PRIMARY_STATUS) & 0x08)) {
        cpu_pause();
    }

    for (int i = 0; i < 256; i++) {
        outw(ATA_PRIMARY_DATA, buffer[i]);
    }

    /* Wait for disk to finish writing */
    while (inb(ATA_PRIMARY_STATUS) & 0x80) {
        cpu_pause();
    }
}
