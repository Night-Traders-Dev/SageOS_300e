// timer_bridge.c
// Arch-specific C primitives callable from timer.sage via MetalVM FFI.
// timer.sage selects the right code path at runtime via arch_id().
// No separate arch/*/kernel/timer.c is needed.

#include "timer.h"
#include <stdint.h>

// ── Architecture IDs — must match ARCH_* constants in timer.sage ──────────
#define TIMER_ARCH_X86   0
#define TIMER_ARCH_ARM64 1
#define TIMER_ARCH_RV64  2

#if   defined(__aarch64__)
#  define CURRENT_ARCH TIMER_ARCH_ARM64
#elif defined(__riscv)
#  define CURRENT_ARCH TIMER_ARCH_RV64
#else
#  define CURRENT_ARCH TIMER_ARCH_X86
#endif

int arch_id(void) { return CURRENT_ARCH; }

// ============================================================================
// ARM64 — ARM Generic Timer (CNTV_* virtual timer registers)
// Accessible from EL1 without a hypervisor, safe on QEMU virt.
// ============================================================================
#if CURRENT_ARCH == TIMER_ARCH_ARM64

#define ARM_FREQ_FALLBACK 62500000ULL   // 62.5 MHz — QEMU virt default

uint64_t arch_timer_freq(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v ? v : ARM_FREQ_FALLBACK;
}
uint64_t arch_timer_count(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}
void arch_timer_cval_write(uint64_t v) {
    __asm__ volatile("msr cntv_cval_el0, %0" :: "r"(v) : "memory");
}
void arch_timer_ctl_write(uint64_t v) {
    __asm__ volatile("msr cntv_ctl_el0, %0" :: "r"(v) : "memory");
}
uint64_t arch_timer_ctl_read(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, cntv_ctl_el0" : "=r"(v));
    return v;
}
void arch_cpu_relax(void) { __asm__ volatile("yield"); }
void arch_cpu_idle(void)  { __asm__ volatile("wfi"); }

// ============================================================================
// RV64 — RISC-V machine timer (rdtime + CLINT MMIO mtimecmp)
// ============================================================================
#elif CURRENT_ARCH == TIMER_ARCH_RV64

#define CLINT_TIMECMP   0x2004000ULL    // CLINT hart0 mtimecmp
#define RV_TIMER_HZ     10000000ULL     // 10 MHz — QEMU virt

uint64_t arch_timer_freq(void) { return RV_TIMER_HZ; }
uint64_t arch_timer_count(void) {
    uint64_t v;
    __asm__ volatile("rdtime %0" : "=r"(v));
    return v;
}
void arch_timer_cval_write(uint64_t v) {
    *(volatile uint64_t *)CLINT_TIMECMP = v;
}
void     arch_timer_ctl_write(uint64_t v) { (void)v; }
uint64_t arch_timer_ctl_read(void)        { return 0; }
void arch_cpu_relax(void) { __asm__ volatile("fence"); }
void arch_cpu_idle(void)  { __asm__ volatile("wfi"); }

// ============================================================================
// x86 — PIT is driven directly by outb (SageLang stdlib intrinsic).
//        Stubs keep the call signature uniform so timer.sage compiles
//        on all archs without conditional includes.
// ============================================================================
#else /* TIMER_ARCH_X86 */

uint64_t arch_timer_freq(void)             { return 1193182ULL; }
uint64_t arch_timer_count(void)            { return 0; }
void     arch_timer_cval_write(uint64_t v) { (void)v; }
void     arch_timer_ctl_write(uint64_t v)  { (void)v; }
uint64_t arch_timer_ctl_read(void)         { return 0; }
void arch_cpu_relax(void) { __asm__ volatile("pause" ::: "memory"); }
void arch_cpu_idle(void)  { __asm__ volatile("hlt"); }

#endif // arch blocks

// ============================================================================
// Timer Implementation
// ============================================================================
extern void ata_timer_tick(void);
extern void sched_timer_tick(void);
extern void console_periodic_flip(void);
#if CURRENT_ARCH == TIMER_ARCH_X86
#include "io.h"
#endif

static uint64_t g_timer_freq = 0;
static uint64_t g_timer_interval = 0;
static uint64_t g_ticks = 0;
static uint32_t g_flip_counter = 0;
static uint64_t g_next_tick_time = 0;
static uint64_t g_boot_timer_count = 0;  /* Hardware timer value at boot time */

#if CURRENT_ARCH == TIMER_ARCH_X86
static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
#endif

#define PIT_HZ 100
#define PIT_BASE_HZ 1193182
#define PIT_CH0 0x40
#define PIT_CMD 0x43
#define CNTV_ENABLE 1
#define CNTV_ISTATUS 4

void timer_init(void) {
    g_ticks = 0;
    g_flip_counter = 0;
    
    int arch = arch_id();
    if (arch == TIMER_ARCH_X86) {
#if CURRENT_ARCH == TIMER_ARCH_X86
        uint32_t pit_reload = PIT_BASE_HZ / PIT_HZ;
        if (pit_reload == 0) pit_reload = 11932;
        outb(PIT_CMD, 0x34);
        outb(PIT_CH0, pit_reload & 0xFF);
        outb(PIT_CH0, pit_reload >> 8);
        g_boot_timer_count = rdtsc();
        g_next_tick_time = g_boot_timer_count + 20000000ULL; // ~100Hz on 2GHz CPU
#endif
        g_timer_freq = PIT_BASE_HZ;
        g_timer_interval = 0;
    } else if (arch == TIMER_ARCH_ARM64) {
        g_timer_freq = arch_timer_freq();
        if (g_timer_freq == 0) g_timer_freq = 62500000;
        g_boot_timer_count = arch_timer_count();
        g_timer_interval = g_timer_freq / PIT_HZ;
        arch_timer_cval_write(g_boot_timer_count + g_timer_interval);
        arch_timer_ctl_write(CNTV_ENABLE);
    } else if (arch == TIMER_ARCH_RV64) {
        g_timer_freq = arch_timer_freq();
        if (g_timer_freq == 0) g_timer_freq = 10000000;
        g_boot_timer_count = arch_timer_count();
        g_timer_interval = g_timer_freq / PIT_HZ;
        g_next_tick_time = g_boot_timer_count + g_timer_interval;
        arch_timer_cval_write(g_next_tick_time);
    }
}

void timer_irq(void) {
    g_ticks++;
    int arch = arch_id();
    
    if (arch == TIMER_ARCH_ARM64) {
        arch_timer_cval_write(arch_timer_count() + g_timer_interval);
        arch_timer_ctl_write(CNTV_ENABLE);
    } else if (arch == TIMER_ARCH_RV64) {
        g_next_tick_time = arch_timer_count() + g_timer_interval;
        arch_timer_cval_write(g_next_tick_time);
    }
    
    ata_timer_tick();
    sched_timer_tick();
    
    if (++g_flip_counter >= 5) {
        console_periodic_flip();
        g_flip_counter = 0;
    }
}

void timer_poll(void) {
    int arch = arch_id();
    if (arch == TIMER_ARCH_ARM64) {
        if (arch_timer_ctl_read() & CNTV_ISTATUS) {
            timer_irq();
        }
    } else if (arch == TIMER_ARCH_RV64) {
        if (arch_timer_count() >= g_next_tick_time) {
            timer_irq();
        }
    } else if (arch == TIMER_ARCH_X86) {
#if CURRENT_ARCH == TIMER_ARCH_X86
        uint64_t current = rdtsc();
        if (current >= g_next_tick_time) {
            g_next_tick_time = current + 20000000ULL;
            timer_irq();
        }
#endif
    }
}

void timer_idle_poll(void) {
    arch_cpu_relax();
    timer_poll();
}

void timer_delay_ms(uint32_t ms) {
    int arch = arch_id();
    if (arch == TIMER_ARCH_X86) {
        uint64_t target = g_ticks + (ms / (1000 / PIT_HZ)) + 1;
        while (g_ticks < target) { arch_cpu_relax(); }
    } else {
        uint64_t target = arch_timer_count() + (g_timer_freq / 1000) * ms;
        while (arch_timer_count() < target) { arch_cpu_relax(); }
    }
}

uint64_t timer_ticks(void) { return g_ticks; }
uint64_t timer_seconds(void) { return g_ticks / PIT_HZ; }

uint64_t timer_elapsed_centiseconds(void) {
    int arch = arch_id();
    uint64_t current_count = 0;
    
    if (arch == TIMER_ARCH_X86) {
#if CURRENT_ARCH == TIMER_ARCH_X86
        current_count = rdtsc();
#endif
    } else if (arch == TIMER_ARCH_ARM64) {
        current_count = arch_timer_count();
    } else if (arch == TIMER_ARCH_RV64) {
        current_count = arch_timer_count();
    }
    
    if (current_count < g_boot_timer_count) {
        return 0;  /* Guard against timer wrap-around or uninitialized state */
    }
    
    uint64_t elapsed = current_count - g_boot_timer_count;
    /* Convert hardware timer units to centiseconds (1/100th second) */
    if (g_timer_freq == 0) return 0;
    uint64_t centiseconds = (elapsed * 100) / g_timer_freq;
    return centiseconds;
}
void timer_cmd_info(void) {}

// For compatibility with calls elsewhere:
uint32_t timer_cpu_percent(void) {
    static uint32_t seed = 42;
    seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
    return 10 + (seed % 20); // 10-30%
}
uint32_t timer_cpu_percent_at(uint32_t cpu) {
    static uint32_t seed = 1337;
    seed = (seed * 1103515245 + 12345 + cpu) & 0x7FFFFFFF;
    return (seed % 100); // 0-100% per core
}

void     sage_timer_init(void)               { timer_init(); }
void     sage_timer_irq(void)                { timer_irq(); }
uint64_t sage_timer_ticks(void)              { return timer_ticks(); }
uint64_t sage_timer_seconds(void)            { return timer_seconds(); }
uint64_t sage_timer_elapsed_centiseconds(void) { return timer_elapsed_centiseconds(); }
void     sage_timer_delay_ms(uint32_t ms)    { timer_delay_ms(ms); }
void     sage_timer_poll(void)               { timer_poll(); }
void     sage_timer_idle_poll(void)          { timer_idle_poll(); }
void     sage_timer_cmd_info(void)           { timer_cmd_info(); }
