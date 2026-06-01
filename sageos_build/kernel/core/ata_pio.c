#if defined(__x86_64__)
#include <stdint.h>
#include "io.h"
#include "console.h"
#include "ata.h"
#include "dmesg.h"

/* Simple Synchronous ATA PIO driver for the primary master */
#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERR          0x1F1
#define ATA_PRIMARY_SECCOUNT     0x1F2
#define ATA_PRIMARY_LBA_LOW      0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HIGH     0x1F5
#define ATA_PRIMARY_DRIVE        0x1F6
#define ATA_PRIMARY_STATUS       0x1F7
#define ATA_PRIMARY_COMMAND     0x1F7
#define ATA_PRIMARY_ALT_STATUS   0x3F6

#define ATA_STATUS_ERR          0x01
#define ATA_STATUS_DRQ          0x08
#define ATA_STATUS_DF           0x20
#define ATA_STATUS_DRDY         0x40
#define ATA_STATUS_BSY          0x80

static int ata_present = 0;

static void ata_io_delay(void) {
    inb(ATA_PRIMARY_ALT_STATUS);
    inb(ATA_PRIMARY_ALT_STATUS);
    inb(ATA_PRIMARY_ALT_STATUS);
    inb(ATA_PRIMARY_ALT_STATUS);
}

static int ata_wait_not_busy(void) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (!(status & ATA_STATUS_BSY)) return 1;
    }
    return 0;
}

static int ata_wait_drq(void) {
    for (int i = 0; i < 1000000; i++) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        if (status & ATA_STATUS_DRQ) return 1;
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) return 0;
    }
    return 0;
}

void ata_init(void) {
    outb(ATA_PRIMARY_DRIVE, 0xA0);
    ata_io_delay();
    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0xFF) {
        ata_present = 0;
        console_write("\nata: No Primary Master detected (floating bus)");
        dmesg_log("ata: No Primary Master detected (floating bus)");
        return;
    }
    
    outb(ATA_PRIMARY_SECCOUNT, 0);
    outb(ATA_PRIMARY_LBA_LOW, 0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HIGH, 0);
    
    outb(ATA_PRIMARY_COMMAND, 0xEC); // IDENTIFY
    ata_io_delay();
    
    status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        ata_present = 0;
        console_write("\nata: Primary Master not ready");
        dmesg_log("ata: Primary Master not ready");
        return;
    }

    /* Wait for BSY to clear after IDENTIFY */
    if (!ata_wait_not_busy()) {
        ata_present = 0;
        console_write("\nata: IDENTIFY timed out");
        dmesg_log("ata: IDENTIFY timed out");
        return;
    }

    status = inb(ATA_PRIMARY_STATUS);
    if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) {
        ata_present = 0;
        console_write("\nata: Primary Master IDENTIFY failed");
        dmesg_log("ata: Primary Master IDENTIFY failed");
        return;
    }

    /* Drain the 256 words of IDENTIFY data so DRQ is cleared
     * and subsequent READ SECTORS commands work correctly.     */
    if (status & ATA_STATUS_DRQ) {
        for (int i = 0; i < 256; i++) {
            (void)inw(ATA_PRIMARY_DATA);
        }
    }

    ata_present = 1;
    console_write("\nata: Primary Master detected (PIO)");
    dmesg_log("ata: Primary Master detected (PIO)");
}

int ata_is_available(void) {
    return ata_present;
}

int ata_read_sector(uint32_t lba, uint16_t *buffer) {
    if (!ata_present) return 0;
    
    // Use low-level console helper
    extern void console_write(const char *str);
    extern void console_u32(uint32_t val);
    console_write("[ATA_READ] LBA: ");
    console_u32(lba);
    console_write("\n");

    if (!ata_wait_not_busy()) {
        console_write("[ATA_READ] Timed out waiting not busy\n");
        return 0;
    }

    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECCOUNT, 1);
    outb(ATA_PRIMARY_LBA_LOW, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_COMMAND, 0x20); // READ SECTORS

    if (!ata_wait_drq()) {
        console_write("[ATA_READ] Timed out waiting DRQ\n");
        return 0;
    }

    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(ATA_PRIMARY_DATA);
    }
    console_write("[ATA_READ] Sector read done\n");
    return 1;
}

int ata_write_sector(uint32_t lba, const uint16_t *buffer) {
    if (!ata_present) return 0;
    if (!ata_wait_not_busy()) return 0;

    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECCOUNT, 1);
    outb(ATA_PRIMARY_LBA_LOW, (uint8_t)lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_LBA_HIGH, (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_COMMAND, 0x30); // WRITE SECTORS

    if (!ata_wait_drq()) return 0;

    for (int i = 0; i < 256; i++) {
        outw(ATA_PRIMARY_DATA, buffer[i]);
    }
    
    outb(ATA_PRIMARY_COMMAND, 0xE7); // CACHE FLUSH
    ata_wait_not_busy();
    
    return 1;
}

int ata_read_sector_async(uint32_t lba, uint16_t *buffer) {
    return ata_read_sector(lba, buffer);
}

int ata_write_sector_async(uint32_t lba, const uint16_t *buffer) {
    return ata_write_sector(lba, buffer);
}

int ata_wait_completion(void) {
    return 1;
}

void ata_timer_tick(void) {}
#endif
