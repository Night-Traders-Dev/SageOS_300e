#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "ata.h"
#include  "swap.h"
#include "dmesg.h"

#define SWAP_PARTITION_START_LBA (2048 + (64 * 1024 * 1024 / 512) + (128 * 1024 * 1024 / 512))

static SwapDevice g_swap;

int swap_init(void) {
    if (!ata_is_available()) {
        g_swap.active = 0;
        return 0;
    }

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
        console_write("\n  [--] No swap device active (disk not detected?)");
        return;
    }

    console_write("\n  [OK] Swap device active:");
    console_write("\n       Partition start LBA: ");
    console_u32(g_swap.partition_lba);
    console_write("\n       Size: ");
    console_u32((uint32_t)(g_swap.size_bytes / 1024 / 1024));
    console_write(" MB");
}
