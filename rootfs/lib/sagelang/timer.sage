// ============================================================================
// Timer Driver (SageLang) — multi-architecture
// ============================================================================
// Architecture is selected at runtime via arch_id() from timer_bridge.c.
// Each arch section is a self-contained block; only one executes per boot.
//
// Bridge primitives (all provided by timer_bridge.c, callable via MetalVM FFI):
//
//   arch_id()                   → int     0=x86, 1=arm64, 2=rv64
//   arch_timer_freq()           → uint64  counter Hz
//   arch_timer_count()          → uint64  current hardware counter value
//   arch_timer_cval_write(val)  → void    write compare / alarm register
//   arch_timer_ctl_write(val)   → void    write control register (arm64)
//   arch_timer_ctl_read()       → uint64  read  control register (arm64)
//   arch_cpu_relax()            → void    yield / pause hint
//   arch_cpu_idle()             → void    wfi / hlt
// ============================================================================

// ── Architecture IDs — must match TIMER_ARCH_* in timer_bridge.c ─────────
const ARCH_X86   = 0
const ARCH_ARM64 = 1
const ARCH_RV64  = 2

// ── ARM Generic Timer control register bits (CNTV_CTL_EL0) ───────────────
const CNTV_ENABLE  = 1
const CNTV_IMASK   = 2
const CNTV_ISTATUS = 4

// ── x86 PIT constants ─────────────────────────────────────────────────────
const PIT_HZ      = 100
const PIT_BASE_HZ = 1193182
const PIT_CH0     = 0x40
const PIT_CMD     = 0x43

// ── Shared state ──────────────────────────────────────────────────────────
var g_timer_freq     = 0
var g_timer_interval = 0
var g_ticks          = 0
var g_flip_counter   = 0
var pit_reload       = 0

// ============================================================================
// x86 — 8254 PIT
// ============================================================================

fn timer_init_x86() {
    dmesg_log("x64 Timer init...\n")
    pit_reload = PIT_BASE_HZ / PIT_HZ
    if pit_reload == 0 {
        pit_reload = 11932
    }
    outb(PIT_CMD, 0x34)
    outb(PIT_CH0, pit_reload & 0xFF)
    outb(PIT_CH0, pit_reload >> 8)
    g_timer_freq     = PIT_BASE_HZ
    g_timer_interval = 0
}

fn timer_irq_x86() {
    g_ticks = g_ticks + 1
    ata_timer_tick()
    sched_timer_tick()
    g_flip_counter = g_flip_counter + 1
    if g_flip_counter >= 5 {
        console_periodic_flip()
        g_flip_counter = 0
    }
}

fn timer_poll_x86() {
    // PIT is purely interrupt-driven; nothing to poll
}

fn timer_idle_poll_x86() {
    arch_cpu_idle()
}

proc timer_delay_ms_x86(ms) {
    // Tick-based: PIT fires at PIT_HZ so 1 tick ≈ 10 ms
    var target = g_ticks + (ms / (1000 / PIT_HZ)) + 1
    while g_ticks < target {
        arch_cpu_relax()
    }
}

// ============================================================================
// ARM64 — ARM Generic Timer (virtual timer, CNTV_* registers)
//
// QEMU virt exposes GIC PPI #27 for the virtual timer interrupt.
// timer_irq_arm64() is called from the IRQ handler registered for PPI 27.
// timer_poll_arm64() provides a software fallback using ISTATUS for
// polling mode (used before GIC is wired up, or in timer_idle_poll).
// ============================================================================

proc timer_init_arm64() {
    dmesg_log("arm64 Timer init...\n")
    g_timer_freq = arch_timer_freq()
    if g_timer_freq == 0 {
        g_timer_freq = 62500000      // 62.5 MHz fallback
    }
    g_timer_interval = g_timer_freq / PIT_HZ
    arch_timer_cval_write(arch_timer_count() + g_timer_interval)
    arch_timer_ctl_write(CNTV_ENABLE)
}

proc timer_irq_arm64() {
    g_ticks = g_ticks + 1
    // Re-arm: advance compare by one fixed interval to avoid drift
    arch_timer_cval_write(arch_timer_count() + g_timer_interval)
    arch_timer_ctl_write(CNTV_ENABLE)
    ata_timer_tick()
    sched_timer_tick()
    g_flip_counter = g_flip_counter + 1
    if g_flip_counter >= 5 {
        console_periodic_flip()
        g_flip_counter = 0
    }
}

proc timer_poll_arm64() {
    // Check ISTATUS — set by hardware when the timer has fired
    if arch_timer_ctl_read() & CNTV_ISTATUS {
        timer_irq_arm64()
    }
}

proc timer_idle_poll_arm64() {
    arch_cpu_idle()          // wfi — wakes on any pending interrupt
    timer_poll_arm64()       // drain any fired timer before returning
}

proc timer_delay_ms_arm64(ms) {
    // Counter-based: precise regardless of tick rate
    var target = arch_timer_count() + (g_timer_freq / 1000) * ms
    while arch_timer_count() < target {
        arch_cpu_relax()
    }
}

// ============================================================================
// RV64 — RISC-V machine timer (rdtime / mtimecmp via CLINT)
//
// mtimecmp is written by arch_timer_cval_write (CLINT MMIO in bridge).
// The machine-timer interrupt fires when mtime >= mtimecmp.
// ============================================================================

proc timer_init_rv64() {
    dmesg_log("rv64 Timer init...\n")
    g_timer_freq = arch_timer_freq()
    if g_timer_freq == 0 {
        g_timer_freq = 10000000      // 10 MHz fallback
    }
    g_timer_interval = g_timer_freq / PIT_HZ
    arch_timer_cval_write(arch_timer_count() + g_timer_interval)
}

proc timer_irq_rv64() {
    g_ticks = g_ticks + 1
    arch_timer_cval_write(arch_timer_count() + g_timer_interval)
    ata_timer_tick()
    sched_timer_tick()
    g_flip_counter = g_flip_counter + 1
    if g_flip_counter >= 5 {
        console_periodic_flip()
        g_flip_counter = 0
    }
}

proc timer_poll_rv64() {
    // RV64 timer fires via machine-timer interrupt; no ISTATUS equivalent
}

proc timer_idle_poll_rv64() {
    arch_cpu_idle()
    timer_poll_rv64()
}

proc timer_delay_ms_rv64(ms) {
    var target = arch_timer_count() + (g_timer_freq / 1000) * ms
    while arch_timer_count() < target {
        arch_cpu_relax()
    }
}

// ============================================================================
// Unified entry points — MetalVM exports each of these as a C symbol.
// timer_bridge.c forward-declares them and wraps them for the kernel.
// ============================================================================

proc timer_init() {
    g_ticks        = 0
    g_flip_counter = 0
    var arch = arch_id()
    if arch == ARCH_X86 {
        timer_init_x86()
    } else if arch == ARCH_ARM64 {
        timer_init_arm64()
    } else if arch == ARCH_RV64 {
        timer_init_rv64()
    }
}

proc timer_irq() {
    var arch = arch_id()
    if arch == ARCH_X86 {
        timer_irq_x86()
    } else if arch == ARCH_ARM64 {
        timer_irq_arm64()
    } else if arch == ARCH_RV64 {
        timer_irq_rv64()
    }
}

proc timer_poll() {
    var arch = arch_id()
    if arch == ARCH_X86 {
        timer_poll_x86()
    } else if arch == ARCH_ARM64 {
        timer_poll_arm64()
    } else if arch == ARCH_RV64 {
        timer_poll_rv64()
    }
}

proc timer_idle_poll() {
    var arch = arch_id()
    if arch == ARCH_X86 {
        timer_idle_poll_x86()
    } else if arch == ARCH_ARM64 {
        timer_idle_poll_arm64()
    } else if arch == ARCH_RV64 {
        timer_idle_poll_rv64()
    }
}

proc timer_delay_ms(ms) {
    var arch = arch_id()
    if arch == ARCH_X86 {
        timer_delay_ms_x86(ms)
    } else if arch == ARCH_ARM64 {
        timer_delay_ms_arm64(ms)
    } else if arch == ARCH_RV64 {
        timer_delay_ms_rv64(ms)
    }
}

proc timer_ticks() {
    return g_ticks
}

proc timer_seconds() {
    return g_ticks / PIT_HZ
}

proc timer_cmd_info() {
    // Hooked by shell's `timer info` command
    // print "freq=" + str(g_timer_freq) + " interval=" + str(g_timer_interval)
    // print "ticks=" + str(g_ticks) + " arch=" + str(arch_id())
}
