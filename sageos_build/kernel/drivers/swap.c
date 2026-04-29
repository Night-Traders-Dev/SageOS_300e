#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "ata.h"
#include "swap.h"
#include "dmesg.h"

/* 
 * SWAP support for SageOS
 * 
 * Partition Layout (assumed):
 * 1: ESP (FAT32)
 * 2: Root (BTRFS)
 * 3: SWAP (125MB)
 */

#define SWAP_PARTITION_START_LBA (2048 + (64 * 1024 * 1024 / 512) + (128 * 1024 * 1024 / 512))

static SwapDevice g_swap;

int swap_init(void) {
    if (!ata_is_available()) {
        g_swap.active = 0;
        return 0;
    }

    /* Minimal detection: check if partition exists? 
       For now, we just hardcode the location and assume it's there.
    */
    g_swap.partition_lba = SWAP_PARTITION_START_LBA;
    g_swap.size_bytes = 125 * 1024 * 1024;
    g_swap.active = 1;

    console_write("\nSWAP: Registered swap device on partition 3 (125MB)");
    dmesg_log("SWAP: Registered swap device on partition 3 (125MB)");
    return 1;
}

int swap_is_available(void) {
    return g_swap.active;
}

void swap_info(void) {
    if (!g_swap.active) {
        console_write("\nSWAP: No swap device active");
        return;
    }

    console_write("\nSWAP: Partition start LBA: ");
    console_u32(g_swap.partition_lba);
    console_write("\n  Size: ");
    console_u32((uint32_t)(g_swap.size_bytes / 1024 / 1024));
    console_write(" MiB");
}
