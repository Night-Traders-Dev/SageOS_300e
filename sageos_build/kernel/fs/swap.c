#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "swap.h"
#include "dmesg.h"

/* ------------------------------------------------------------------
 * Architecture-specific block backend
 *
 *  x86_64  -> ATA/IDE via port I/O  (0x1F0 primary bus)
 *  aarch64 -> VirtIO-blk MMIO       (GICv3 interrupt delivery)
 *  rv64    -> VirtIO-blk MMIO       (PLIC interrupt delivery)
 *
 * Set ARCH_X86_64 / ARCH_AARCH64 / ARCH_RV64 in your Makefile.
 * ------------------------------------------------------------------ */
#if defined(ARCH_X86_64)
#  include "ata.h"
#  define BLKDEV_AVAILABLE()    ata_is_available()
#  define BLKDEV_NAME           "ATA"

#elif defined(ARCH_AARCH64) || defined(ARCH_RV64)
#  include "ata.h"
#  define BLKDEV_AVAILABLE()    ata_is_available()
#  define BLKDEV_NAME           "VirtIO-blk"

#else
#  error "Unsupported architecture. Define ARCH_X86_64, ARCH_AARCH64, or ARCH_RV64."
#endif

/* ------------------------------------------------------------------
 * Partition layout  (512-byte LBA sectors)
 *
 *   Part 1  FAT32  @ LBA 2048       +  64 MiB  = 131072 sectors
 *   Part 2  BTRFS  @ LBA 133120     + 128 MiB  = 262144 sectors
 *   Part 3  SWAP   @ LBA 395264     + 125 MiB
 *
 * Using uint64_t literals throughout — LBA-48 is defined to 48 bits;
 * keeping everything 64-bit prevents silent truncation on any target.
 * ------------------------------------------------------------------ */
#define MiB_TO_SECTORS(mib)     ((uint64_t)(mib) * 1024ULL * 1024ULL / 512ULL)

#define FAT32_START_LBA         2048ULL
#define FAT32_SECTORS           MiB_TO_SECTORS(64)
#define BTRFS_START_LBA         (FAT32_START_LBA  + FAT32_SECTORS)
#define BTRFS_SECTORS           MiB_TO_SECTORS(128)
#define SWAP_START_LBA          (BTRFS_START_LBA  + BTRFS_SECTORS)
#define SWAP_SIZE_BYTES         (125ULL * 1024ULL * 1024ULL)

static SwapDevice g_swap;

int swap_init(void) {
    if (!BLKDEV_AVAILABLE()) {
        g_swap.active = 0;
        return 0;
    }

    g_swap.partition_lba = SWAP_START_LBA;
    g_swap.size_bytes    = SWAP_SIZE_BYTES;
    g_swap.active        = 1;

    console_write("\nSWAP: Registered swap on partition 3 (125MB) via " BLKDEV_NAME);
    dmesg_log("SWAP: Registered swap on partition 3 (125MB) via " BLKDEV_NAME);
    return 1;
}

int swap_is_available(void) {
    return g_swap.active;
}

uint64_t swap_total_bytes(void) {
    if (g_swap.active) {
        return g_swap.size_bytes;
    }
    return 0;
}

uint64_t swap_used_bytes(void) {
    // Return 0 for now as swap paging is not yet fully implemented
    return 0;
}

void swap_info(void) {
    if (!g_swap.active) {
        console_write("\n  [--] No swap device active (" BLKDEV_NAME " not detected)");
        return;
    }

    console_write("\n  [OK] Swap device active (" BLKDEV_NAME "):");
    console_write("\n       Partition start LBA: ");
    console_u64(g_swap.partition_lba);
    console_write("\n       Size: ");
    console_u32((uint32_t)(g_swap.size_bytes / 1024ULL / 1024ULL));
    console_write(" MB");
}
