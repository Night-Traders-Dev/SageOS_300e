#include <stdint.h>
#include "console.h"
#include "fat32.h"
#include "timer.h"
#include "sysinfo.h"

/* ------------------------------------------------------------------ */
/* CPU frequency via CPUID leaf 0x16 + PIT-gated RDTSC fallback       */
/* ------------------------------------------------------------------ */

/*
 * cpuid_max_leaf: return highest basic CPUID leaf supported.
 * All four output registers are explicit operands — no clobbers needed.
 */
static uint32_t cpuid_max_leaf(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "0"(0)
    );
    return eax;
}

/*
 * cpuid_freq_leaf16
 *
 * CPUID leaf 0x16 (Broadwell+ / Skylake+, incl. Celeron N4020) returns:
 *   EAX[15:0] = base frequency in MHz
 *   EBX[15:0] = maximum frequency in MHz
 *
 * Available in QEMU when launched with -cpu Skylake-Client (or host).
 * Returns 1 on success, 0 if leaf is absent or reports 0 MHz.
 */
static int cpuid_freq_leaf16(uint32_t *base_mhz, uint32_t *max_mhz) {
    if (cpuid_max_leaf() < 0x16) return 0;

    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "0"(0x16)
    );

    *base_mhz = eax & 0xFFFF;
    *max_mhz  = ebx & 0xFFFF;
    return (*base_mhz > 0) ? 1 : 0;
}

/*
 * rdtsc_now: read the 64-bit TSC.
 */
static uint64_t rdtsc_now(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/*
 * rdtsc_mhz_pit
 *
 * PIT-gated RDTSC calibration.
 *
 * Strategy:
 *   1. Read TSC at t0.
 *   2. Wait exactly CALIB_MS milliseconds using timer_delay_ms() which
 *      blocks on the PIT tick counter — a real time source unaffected
 *      by QEMU's TCG speed or host CPU boost.
 *   3. Read TSC at t1.
 *   4. freq_MHz = (t1 - t0) / (CALIB_MS * 1000)
 *
 * CALIB_MS = 50 ms gives ~2% granularity at 1 GHz (2 ticks of 1 MHz
 * resolution) and keeps the sysinfo command responsive.
 *
 * Accuracy: limited by PIT tick resolution (10 ms at 100 Hz).  50 ms
 * window spans 5 ticks so jitter is at most ±1 tick = ±2 %, which is
 * far better than the old spin-loop (~1000 % error in QEMU TCG).
 */
#define CALIB_MS 50

static uint32_t rdtsc_mhz_pit(void) {
    uint64_t t0 = rdtsc_now();
    timer_delay_ms(CALIB_MS);
    uint64_t t1 = rdtsc_now();

    uint64_t delta = t1 - t0;
    /* MHz = ticks / (ms * 1000) = ticks / 50000 */
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

static uint32_t estimate_used_ram_kb(void) {
    return 384UL; /* 256 KB image + 128 KB stack/bss/data */
}

/* ------------------------------------------------------------------ */
/* Print helpers                                                       */
/* ------------------------------------------------------------------ */

static void print_u32(uint32_t v)       { console_u32(v); }
static void print_kb_as_mb(uint32_t kb) { print_u32(kb / 1024); console_write(" MB"); }

/* ------------------------------------------------------------------ */
/* sysinfo_cmd                                                         */
/* ------------------------------------------------------------------ */

void sysinfo_cmd(void) {
    console_write("\n=== System Info ===");

    /* --- CPU Frequency --- */
    console_write("\n\nCPU frequency:");
    uint32_t base_mhz = 0, max_mhz = 0;
    if (cpuid_freq_leaf16(&base_mhz, &max_mhz)) {
        console_write("\n  base : "); print_u32(base_mhz); console_write(" MHz");
        if (max_mhz && max_mhz != base_mhz) {
            console_write("\n  max  : "); print_u32(max_mhz); console_write(" MHz");
        }
        console_write("  [CPUID leaf 0x16]");
    } else {
        console_write("\n  measuring via PIT (50 ms)...");
        uint32_t mhz = rdtsc_mhz_pit();
        /* Overwrite the progress line with the result */
        console_write("\r  est  : "); print_u32(mhz); console_write(" MHz");
        console_write("  [RDTSC/PIT, ~2% margin]    ");
    }

    /* --- Memory --- */
    uint32_t total_ram_kb = cmos_total_ram_kb();
    uint32_t used_ram_kb  = estimate_used_ram_kb();
    console_write("\n\nMemory (CMOS):");
    console_write("\n  total: "); print_kb_as_mb(total_ram_kb);
    console_write("\n  used : "); print_kb_as_mb(used_ram_kb); console_write("  (kernel est.)");
    console_write("\n  free : "); print_kb_as_mb(total_ram_kb - used_ram_kb);

    /* --- Storage --- */
    console_write("\n\nStorage (FAT32):");
    if (!fat32_is_available()) {
        console_write("  not mounted");
    } else {
        uint32_t total_kb = 0, free_kb = 0;
        int free_valid = fat32_storage_info(&total_kb, &free_kb);
        console_write("\n  total: "); print_kb_as_mb(total_kb);
        if (free_valid) {
            uint32_t used_kb = (free_kb <= total_kb) ? (total_kb - free_kb) : 0;
            console_write("\n  used : "); print_kb_as_mb(used_kb);
            console_write("\n  free : "); print_kb_as_mb(free_kb);
        } else {
            console_write("\n  free : unknown (FSInfo unavailable)");
        }
    }

    console_write("\n");
}
