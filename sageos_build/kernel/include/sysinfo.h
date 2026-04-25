#ifndef SAGEOS_SYSINFO_H
#define SAGEOS_SYSINFO_H

#include <stdint.h>

/*
 * sysinfo_cmd - print a formatted system information block to the console.
 *
 * CPU frequency: CPUID leaf 0x16 (AMD Stoney/Excavator and later, Intel
 * Broadwell and later).  Falls back to a TSC-calibration estimate using
 * the PIT if the leaf is absent or returns zero.
 *
 * RAM: derived from SageOSBootInfo memory_total / memory_usable.
 *
 * Storage: FAT32 BPB total_sectors_32 for total; FSInfo free-cluster
 * count (if valid) for free, otherwise "unknown" for used percentage.
 */
void sysinfo_cmd(void);

#endif
