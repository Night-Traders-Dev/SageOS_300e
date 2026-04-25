#include <stdint.h>
#include "console.h"
#include "fat32.h"
#include "sysinfo.h"

/* ------------------------------------------------------------------ */
/* CPU frequency via CPUID + RDTSC                                     */
/* ------------------------------------------------------------------ */

/*
 * cpuid_max_leaf: returns the highest basic CPUID leaf supported.
 *
 * All four registers are explicit output/input operands so no clobbers
 * are needed (a register cannot appear in both the operand list and the
 * clobber list simultaneously).
 */
static uint32_t cpuid_max_leaf(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "0"(0)          /* input: EAX = 0, tied to output "=a" */
    );
    return eax;
}

/*
 * cpuid_freq_leaf16
 *
 * CPUID leaf 0x16 (Broadwell+ / Skylake+, incl. Celeron N4020) returns:
 *   EAX[15:0] = base frequency in MHz
 *   EBX[15:0] = maximum frequency in MHz
 *   ECX[15:0] = bus (reference) frequency in MHz
 *
 * Returns 1 on success with *base_mhz and *max_mhz set.
 * Returns 0 if leaf 0x16 is not available.
 */
static int cpuid_freq_leaf16(uint32_t *base_mhz, uint32_t *max_mhz) {
    if (cpuid_max_leaf() < 0x16) return 0;

    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "0"(0x16)       /* input: EAX = 0x16, tied to output "=a" */
    );

    *base_mhz = eax & 0xFFFF;
    *max_mhz  = ebx & 0xFFFF;
    return (*base_mhz > 0) ? 1 : 0;
}

/*
 * rdtsc_mhz
 *
 * Fallback: measure TSC ticks during a busy-loop calibration window.
 * Intentionally rough (+/-15%) but always non-zero when leaf 0x16 is absent.
 */
static uint32_t rdtsc_mhz(void) {
    uint32_t lo1, hi1, lo2, hi2;
    __asm__ volatile("rdtsc" : "=a"(lo1), "=d"(hi1));

    volatile uint32_t n = 10000000UL;
    while (n--) __asm__ volatile("");

    __asm__ volatile("rdtsc" : "=a"(lo2), "=d"(hi2));

    uint64_t t1 = ((uint64_t)hi1 << 32) | lo1;
    uint64_t t2 = ((uint64_t)hi2 << 32) | lo2;
    uint64_t delta = t2 - t1;

    /* ~10 ms loop => freq_mhz ~= delta / 10000 */
    return (uint32_t)(delta / 10000ULL);
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

static void print_u32(uint32_t v)        { console_u32(v); }
static void print_kb_as_mb(uint32_t kb)  { print_u32(kb / 1024); console_write(" MB"); }

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
        uint32_t mhz = rdtsc_mhz();
        console_write("\n  est  : "); print_u32(mhz); console_write(" MHz");
        console_write("  [RDTSC calibration, ~15% margin]");
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
