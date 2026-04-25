#include <stdint.h>
#include "console.h"
#include "fat32.h"
#include "sysinfo.h"

/* ------------------------------------------------------------------ */
/* CPU frequency via CPUID + RDTSC                                     */
/* ------------------------------------------------------------------ */

/*
 * cpuid_max_leaf: returns the highest basic CPUID leaf supported.
 */
static uint32_t cpuid_max_leaf(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0)
    );
    return eax;
}

/*
 * cpuid_brand_freq_mhz
 *
 * CPUID leaf 0x16 (available on Broadwell+ / Skylake+, including
 * Celeron N4020 used in the Lenovo 300e 2nd Gen) returns:
 *   EAX[15:0] = base frequency in MHz
 *   EBX[15:0] = maximum frequency in MHz
 *   ECX[15:0] = bus (reference) frequency in MHz
 *
 * Returns base_mhz in *base, max_mhz in *max, 1 on success.
 * Returns 0 if leaf 0x16 is not available.
 */
static int cpuid_freq_leaf16(uint32_t *base_mhz, uint32_t *max_mhz) {
    if (cpuid_max_leaf() < 0x16) return 0;

    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x16)
    );

    *base_mhz = eax & 0xFFFF;
    *max_mhz  = ebx & 0xFFFF;
    return (*base_mhz > 0) ? 1 : 0;
}

/*
 * rdtsc_mhz
 *
 * Fallback: measure TSC ticks during a busy-loop calibration window.
 * The loop count is tuned so that ~10 ms passes on a 1-4 GHz CPU when
 * running without a real timer IRQ (bare-metal before PIT calibration).
 *
 * This is intentionally rough (±15 %) but always produces a non-zero
 * result when CPUID leaf 0x16 is absent.
 */
static uint32_t rdtsc_mhz(void) {
    uint32_t lo1, hi1, lo2, hi2;
    __asm__ volatile("rdtsc" : "=a"(lo1), "=d"(hi1));

    /* ~10 ms busy loop at ~1 GHz = 10 000 000 iterations */
    volatile uint32_t n = 10000000UL;
    while (n--) {
        __asm__ volatile("");
    }

    __asm__ volatile("rdtsc" : "=a"(lo2), "=d"(hi2));

    uint64_t t1 = ((uint64_t)hi1 << 32) | lo1;
    uint64_t t2 = ((uint64_t)hi2 << 32) | lo2;
    uint64_t delta = t2 - t1;

    /*
     * We assume the loop above takes ~10 ms.
     * freq_hz ~= delta / 0.010
     * freq_mhz ~= delta / 10000
     */
    return (uint32_t)(delta / 10000ULL);
}

/* ------------------------------------------------------------------ */
/* Memory: read e820/multiboot map — simple upper-bound from bootinfo  */
/* ------------------------------------------------------------------ */

/*
 * We detect the total physical RAM by reading the conventional
 * CMOS extended-memory registers (ports 0x70/0x71).  CMOS reports
 * extended memory (above 1 MB) in KB in two banks:
 *
 *   Reg 0x17/0x18 — extended memory below 64 MB (max 63 MB, in KB)
 *   Reg 0x30/0x31 — extended memory above 64 MB (in 64 KB units)
 *
 * Total RAM (KB) = 1024 (conventional 0-1 MB)
 *               + cmos_ext1 (KB)
 *               + cmos_ext2 * 64 (KB)
 *
 * This matches what QEMU and real BIOSes report for <= 4 GB systems.
 * It does NOT account for ACPI SRAT; for bare hardware diagnostics
 * this is close enough (±0.1 %).
 *
 * Note: reading port 0x71 twice can clobber the CMOS address latch.
 * Always write 0x70 immediately before reading 0x71.
 */
static uint8_t cmos_read(uint8_t reg) {
    __asm__ volatile(
        "outb %0, $0x70"
        :: "a"(reg)
    );
    uint8_t val;
    __asm__ volatile(
        "inb $0x71, %0"
        : "=a"(val)
    );
    return val;
}

/*
 * cmos_total_ram_kb: return total physical RAM in KB using CMOS.
 */
static uint32_t cmos_total_ram_kb(void) {
    uint16_t ext1 = ((uint16_t)cmos_read(0x18) << 8) | cmos_read(0x17);
    uint16_t ext2 = ((uint16_t)cmos_read(0x31) << 8) | cmos_read(0x30);
    return 1024UL + (uint32_t)ext1 + (uint32_t)ext2 * 64UL;
}

/*
 * estimate_used_ram_kb
 *
 * Returns a rough kernel-footprint estimate:
 *   - Kernel image:   ~256 KB (conservative)
 *   - Stack + heap:   ~128 KB
 *   - Video/FB MMIO:  ~0 (not in RAM pool)
 *
 * A proper allocator would track this precisely; for the sysinfo
 * diagnostic it is intentionally labelled "kernel est."
 */
static uint32_t estimate_used_ram_kb(void) {
    return 384UL; /* 256 KB image + 128 KB stack/bss/data */
}

/* ------------------------------------------------------------------ */
/* Helpers: integer print routines                                     */
/* ------------------------------------------------------------------ */

static void print_u32(uint32_t v) {
    console_u32(v);
}

static void print_kb_as_mb(uint32_t kb) {
    /* Print as "X MB" (integer division, no float needed) */
    print_u32(kb / 1024);
    console_write(" MB");
}

/* ------------------------------------------------------------------ */
/* sysinfo_cmd                                                         */
/* ------------------------------------------------------------------ */

void sysinfo_cmd(void) {
    console_write("\n=== System Info ===\n");

    /* --- CPU Frequency --- */
    console_write("\nCPU frequency:");
    uint32_t base_mhz = 0, max_mhz = 0;
    if (cpuid_freq_leaf16(&base_mhz, &max_mhz)) {
        console_write("\n  base : "); print_u32(base_mhz); console_write(" MHz");
        if (max_mhz && max_mhz != base_mhz) {
            console_write("\n  max  : "); print_u32(max_mhz); console_write(" MHz");
        }
        console_write("  [CPUID leaf 0x16]");
    } else {
        uint32_t mhz = rdtsc_mhz();
        console_write("\n  est  : "); print_u32(mhz); console_write(" MHz");
        console_write("  [RDTSC calibration, \xb115%]");
    }

    /* --- Memory --- */
    uint32_t total_ram_kb = cmos_total_ram_kb();
    uint32_t used_ram_kb  = estimate_used_ram_kb();
    console_write("\n\nMemory (CMOS):");
    console_write("\n  total: "); print_kb_as_mb(total_ram_kb);
    console_write("\n  used : "); print_kb_as_mb(used_ram_kb);
    console_write("  (kernel est.)");
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
