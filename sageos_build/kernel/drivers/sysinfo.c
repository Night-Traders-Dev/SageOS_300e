#include <stdint.h>
#include "console.h"
#include "fat32.h"
#include "timer.h"
#include "sysinfo.h"

/* ------------------------------------------------------------------ */
/* CPU frequency via CPUID leaf 0x16 + PIT-gated RDTSC fallback       */
/* ------------------------------------------------------------------ */

static uint32_t cpuid_max_leaf(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "0"(0));
    return eax;
}

static int cpuid_freq_leaf16(uint32_t *base_mhz, uint32_t *max_mhz) {
    if (cpuid_max_leaf() < 0x16) return 0;
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "0"(0x16));
    *base_mhz = eax & 0xFFFF;
    *max_mhz  = ebx & 0xFFFF;
    return (*base_mhz > 0) ? 1 : 0;
}

static uint64_t rdtsc_now(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#define CALIB_MS 50

static uint32_t rdtsc_mhz_pit(void) {
    uint64_t t0 = rdtsc_now();
    timer_delay_ms(CALIB_MS);
    uint64_t t1 = rdtsc_now();
    uint64_t delta = t1 - t0;
    return (uint32_t)(delta / ((uint64_t)CALIB_MS * 1000ULL));
}

/* ------------------------------------------------------------------ */
/* Memory via CMOS extended-memory registers                          */
/* ------------------------------------------------------------------ */

static uint8_t cmos_read(uint8_t reg) {
    __asm__ volatile("outb %0, $0x70" :: "a"(reg));
    uint8_t val;
    __asm__ volatile("inb $0x71, %0" : "=a"(val));
    return val;
}

static uint32_t cmos_total_ram_kb(void) {
    uint16_t ext1 = ((uint16_t)cmos_read(0x18) << 8) | cmos_read(0x17);
    uint16_t ext2 = ((uint16_t)cmos_read(0x31) << 8) | cmos_read(0x30);
    return 1024UL + (uint32_t)ext1 + (uint32_t)ext2 * 64UL;
}

/* ------------------------------------------------------------------ */
/* Print helpers                                                       */
/* ------------------------------------------------------------------ */

/*
 * print_mem: display a KB value as "NNN MB" or "NNN KB" depending
 * on magnitude.  Prevents showing "0 MB" for sub-1024-KB values.
 */
static void print_mem(uint32_t kb) {
    if (kb >= 1024) {
        console_u32(kb / 1024);
        console_write(" MB");
    } else {
        console_u32(kb);
        console_write(" KB");
    }
}

/* ------------------------------------------------------------------ */
/* Kernel memory usage estimate                                       */
/* ------------------------------------------------------------------ */

/*
 * kernel_used_kb: estimate actual kernel footprint from well-known
 * symbols placed by the linker.  __kernel_start and __kernel_end are
 * declared in linker.ld so the measurement survives future size changes.
 *
 * We add a fixed 256 KB overhead for stack, heap bump allocator, and
 * runtime BSS that may not be captured between the two symbols.
 */
extern char __kernel_start[];
extern char __kernel_end[];

static uint32_t kernel_used_kb(void) {
    uint32_t image_bytes = (uint32_t)((uintptr_t)__kernel_end -
                                       (uintptr_t)__kernel_start);
    uint32_t image_kb    = (image_bytes + 1023) / 1024;  /* round up */
    return image_kb + 256;  /* + 256 KB stack/BSS/heap overhead */
}

/* ------------------------------------------------------------------ */
/* sysinfo_cmd                                                         */
/* ------------------------------------------------------------------ */

void sysinfo_cmd(void) {
    console_write("\n=== System Info ===");

    /* --- CPU Frequency --- */
    console_write("\n\nCPU frequency:");
    uint32_t base_mhz = 0, max_mhz = 0;
    if (cpuid_freq_leaf16(&base_mhz, &max_mhz)) {
        console_write("\n  base : "); console_u32(base_mhz); console_write(" MHz");
        if (max_mhz && max_mhz != base_mhz) {
            console_write("\n  max  : "); console_u32(max_mhz); console_write(" MHz");
        }
        console_write("  [CPUID leaf 0x16]");
    } else {
        console_write("\n  measuring...");
        uint32_t mhz = rdtsc_mhz_pit();
        console_write("\r  est  : "); console_u32(mhz); console_write(" MHz");
        console_write("  [RDTSC/PIT, ~2% margin]    ");
    }

    /* --- Memory --- */
    uint32_t total_ram_kb = cmos_total_ram_kb();
    uint32_t used_kb      = kernel_used_kb();
    if (used_kb > total_ram_kb) used_kb = total_ram_kb;
    uint32_t free_kb      = total_ram_kb - used_kb;

    console_write("\n\nMemory (CMOS):");
    console_write("\n  total: "); print_mem(total_ram_kb);
    console_write("\n  used : "); print_mem(used_kb); console_write("  (kernel)");
    console_write("\n  free : "); print_mem(free_kb);

    /* --- Storage --- */
    console_write("\n\nStorage (FAT32):");
    if (!fat32_is_available()) {
        console_write("  not mounted");
    } else {
        uint32_t total_kb = 0, free_kb2 = 0;
        int free_valid = fat32_storage_info(&total_kb, &free_kb2);
        console_write("\n  total: "); print_mem(total_kb);
        if (free_valid) {
            uint32_t used_kb2 = (free_kb2 <= total_kb) ? (total_kb - free_kb2) : 0;
            console_write("\n  used : "); print_mem(used_kb2);
            console_write("\n  free : "); print_mem(free_kb2);
        } else {
            console_write("\n  free : unknown (FSInfo unavailable)");
        }
    }

    console_write("\n");
}
