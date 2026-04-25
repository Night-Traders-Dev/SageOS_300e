#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "bootinfo.h"
#include "timer.h"
#include "fat32.h"
#include "sysinfo.h"

/* ── helpers ────────────────────────────────────────────────────────────── */

static void print_u32(uint32_t v) {
    console_u32(v);
}

static void print_u64_mib(uint64_t bytes) {
    uint32_t mib = (uint32_t)(bytes >> 20);
    print_u32(mib);
    console_write(" MiB");
}

/* ── CPU frequency ──────────────────────────────────────────────────────── */

/*
 * cpuid_leaf16: CPUID leaf 0x16 returns base/max/bus frequency in MHz in
 * EAX/EBX/ECX.  Supported on Intel Broadwell+ and AMD Zen+; Stoney Ridge
 * (the 300e APU) is pre-Zen (Excavator) and may not support it, so we
 * fall back to TSC calibration in that case.
 */
static uint32_t cpuid_leaf16_base_mhz(void) {
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    uint32_t max_leaf = 0;

    /* First check how many standard leaves the CPU supports */
    __asm__ volatile (
        "cpuid"
        : "=a"(max_leaf), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x00000000)
    );

    if (max_leaf < 0x16) {
        return 0;
    }

    __asm__ volatile (
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x00000016), "c"(0)
    );

    /* EAX[15:0] = base frequency in MHz (0 means unsupported) */
    return (eax & 0xFFFF);
}

/*
 * tsc_calibrate_mhz: spin for ~10 ms (1 PIT tick at 100 Hz) and measure
 * the number of TSC counts.  Multiply by 100 to get Hz then divide by
 * 1 000 000 to get MHz.  Returns 0 if RDTSC is unavailable.
 */
static uint32_t tsc_calibrate_mhz(void) {
    /* Check RDTSC support via CPUID */
    uint32_t edx = 0;
    __asm__ volatile (
        "cpuid"
        : "=d"(edx)
        : "a"(0x1)
        : "eax", "ebx", "ecx"
    );
    if (!(edx & (1u << 4))) {
        return 0;  /* no RDTSC */
    }

    uint64_t start_tick = timer_ticks();
    /* Wait for the next PIT tick boundary */
    while (timer_ticks() == start_tick) {
        __asm__ volatile ("pause");
    }
    /* Now at the start of a fresh tick — measure over exactly 1 tick */
    uint64_t tsc_start;
    __asm__ volatile ("rdtsc" : "=A"(tsc_start));
    uint64_t meas_tick = timer_ticks();
    while (timer_ticks() == meas_tick) {
        __asm__ volatile ("pause");
    }
    uint64_t tsc_end;
    __asm__ volatile ("rdtsc" : "=A"(tsc_end));

    /* 1 tick = 1/100 s at PIT_HZ 100.  tsc_delta * 100 = tsc/s */
    uint64_t delta = tsc_end - tsc_start;
    uint32_t mhz   = (uint32_t)((delta * 100ULL) / 1000000ULL);
    return mhz;
}

static void print_cpu_freq(void) {
    uint32_t mhz = cpuid_leaf16_base_mhz();
    if (!mhz) {
        mhz = tsc_calibrate_mhz();
    }

    console_write("\n  CPU Frequency : ");
    if (!mhz) {
        console_write("unknown");
        return;
    }
    if (mhz >= 1000) {
        /* Print as X.YY GHz */
        uint32_t ghz_whole = mhz / 1000;
        uint32_t ghz_frac  = (mhz % 1000) / 10;  /* 2 decimal places */
        print_u32(ghz_whole);
        console_write(".");
        if (ghz_frac < 10) console_write("0");
        print_u32(ghz_frac);
        console_write(" GHz");
    } else {
        print_u32(mhz);
        console_write(" MHz");
    }
    console_write("  (source: ");
    console_write(cpuid_leaf16_base_mhz() ? "CPUID leaf 0x16" : "TSC calibration");
    console_write(")");
}

/* ── RAM ────────────────────────────────────────────────────────────────── */

static void print_ram(void) {
    SageOSBootInfo *b = console_boot_info();

    console_write("\n  Memory        : ");

    if (!b || !b->memory_total ||
        b->memory_total > 0x20000000000ULL ||
        b->memory_total < 0x100000ULL) {
        console_write("unknown");
        return;
    }

    uint64_t total = b->memory_total;
    uint64_t free_ = (b->memory_usable <= total) ? b->memory_usable : 0;
    uint64_t used  = total - free_;

    print_u64_mib(used);
    console_write(" used / ");
    print_u64_mib(total);
    console_write(" total");

    uint32_t pct = (uint32_t)((used * 100ULL) / total);
    console_write("  (");
    print_u32(pct);
    console_write("%)");
}

/* ── Storage ────────────────────────────────────────────────────────────── */

static void print_storage(void) {
    console_write("\n  Storage (ESP) : ");

    if (!fat32_is_available()) {
        console_write("FAT32 not mounted");
        return;
    }

    uint32_t total_kb = 0;
    uint32_t free_kb  = 0;
    int      free_valid = fat32_storage_info(&total_kb, &free_kb);

    if (!total_kb) {
        console_write("size unknown");
        return;
    }

    uint32_t total_mib = total_kb / 1024;
    print_u32(total_mib ? total_mib : total_kb);
    console_write(total_mib ? " MiB total" : " KiB total");

    if (free_valid && total_kb > 0) {
        uint32_t used_kb  = (free_kb <= total_kb) ? (total_kb - free_kb) : 0;
        uint32_t used_mib = used_kb / 1024;
        console_write("  /  ");
        print_u32(used_mib ? used_mib : used_kb);
        console_write(used_mib ? " MiB used" : " KiB used");
        uint32_t pct = (uint32_t)((uint64_t)used_kb * 100ULL / total_kb);
        console_write("  (");
        print_u32(pct);
        console_write("%)");
    } else {
        console_write("  (free count unavailable)");
    }
}

/* ── public entry ───────────────────────────────────────────────────────── */

void sysinfo_cmd(void) {
    console_write("\nSystem Info:");
    print_cpu_freq();
    print_ram();
    print_storage();
    console_write("\n");
}
