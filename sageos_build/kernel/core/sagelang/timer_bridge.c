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
// C ↔ Sage dispatch
//
// MetalVM compiles timer.sage and exports each `fn foo()` as C symbol `foo`.
// These forward declarations satisfy the linker; the wrappers below satisfy
// the kernel's timer.h call sites.
//
// timer_cpu_percent / timer_cpu_percent_at are stubbed here — they have no
// Sage implementation and no arch equivalent yet.
// ============================================================================
void     timer_init(void);
void     timer_irq(void);
uint64_t timer_ticks(void);
uint64_t timer_seconds(void);
void     timer_delay_ms(uint32_t ms);
void     timer_poll(void);
void     timer_idle_poll(void);
void     timer_cmd_info(void);

void     sage_timer_init(void)               { timer_init(); }
void     sage_timer_irq(void)                { timer_irq(); }
uint64_t sage_timer_ticks(void)              { return timer_ticks(); }
uint64_t sage_timer_seconds(void)            { return timer_seconds(); }
void     sage_timer_delay_ms(uint32_t ms)    { timer_delay_ms(ms); }
void     sage_timer_poll(void)               { timer_poll(); }
void     sage_timer_idle_poll(void)          { timer_idle_poll(); }
void     sage_timer_cmd_info(void)           { timer_cmd_info(); }
uint32_t timer_cpu_percent(void)             { return 0; }
uint32_t timer_cpu_percent_at(uint32_t cpu)  { (void)cpu; return 0; }
