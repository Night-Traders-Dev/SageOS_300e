// ============================================================================
// Timer Driver (SageLang) — multi-architecture
// ============================================================================
// Architecture is selected at runtime via arch_id() from timer_bridge.c.
// Each arch section is a self-contained block; only one executes per boot.
// ============================================================================

// ── Architecture IDs ──────────────────────────────────────────────────────
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

proc timer_init_x86():
    dmesg_log("x64 Timer init...\n")
    pit_reload = PIT_BASE_HZ / PIT_HZ
    if pit_reload == 0:
        pit_reload = 11932
    end
    outb(PIT_CMD, 0x34)
    outb(PIT_CH0, pit_reload & 0xFF)
    outb(PIT_CH0, pit_reload >> 8)
    g_timer_freq     = PIT_BASE_HZ
    g_timer_interval = 0
end

proc timer_irq_x86():
    g_ticks = g_ticks + 1
    ata_timer_tick()
    sched_timer_tick()
    g_flip_counter = g_flip_counter + 1
    if g_flip_counter >= 5:
        console_periodic_flip()
        g_flip_counter = 0
    end
end

proc timer_poll_x86():
    // PIT is purely interrupt-driven
end

proc timer_idle_poll_x86():
    arch_cpu_idle()
end

proc timer_delay_ms_x86(ms):
    var target = g_ticks + (ms / (1000 / PIT_HZ)) + 1
    while g_ticks < target:
        arch_cpu_relax()
    end
end

// ============================================================================
// ARM64 — ARM Generic Timer
// ============================================================================

proc timer_init_arm64():
    dmesg_log("arm64 Timer init...\n")
    g_timer_freq = arch_timer_freq()
    if g_timer_freq == 0:
        g_timer_freq = 62500000
    end
    g_timer_interval = g_timer_freq / PIT_HZ
    arch_timer_cval_write(arch_timer_count() + g_timer_interval)
    arch_timer_ctl_write(CNTV_ENABLE)
end

proc timer_irq_arm64():
    g_ticks = g_ticks + 1
    arch_timer_cval_write(arch_timer_count() + g_timer_interval)
    arch_timer_ctl_write(CNTV_ENABLE)
    ata_timer_tick()
    sched_timer_tick()
    g_flip_counter = g_flip_counter + 1
    if g_flip_counter >= 5:
        console_periodic_flip()
        g_flip_counter = 0
    end
end

proc timer_poll_arm64():
    if arch_timer_ctl_read() & CNTV_ISTATUS:
        timer_irq_arm64()
    end
end

proc timer_idle_poll_arm64():
    arch_cpu_idle()
    timer_poll_arm64()
end

proc timer_delay_ms_arm64(ms):
    var target = arch_timer_count() + (g_timer_freq / 1000) * ms
    while arch_timer_count() < target:
        arch_cpu_relax()
    end
end

// ============================================================================
// RV64 — RISC-V machine timer
// ============================================================================

proc timer_init_rv64():
    dmesg_log("rv64 Timer init...\n")
    g_timer_freq = arch_timer_freq()
    if g_timer_freq == 0:
        g_timer_freq = 10000000
    end
    g_timer_interval = g_timer_freq / PIT_HZ
    arch_timer_cval_write(arch_timer_count() + g_timer_interval)
end

proc timer_irq_rv64():
    g_ticks = g_ticks + 1
    arch_timer_cval_write(arch_timer_count() + g_timer_interval)
    ata_timer_tick()
    sched_timer_tick()
    g_flip_counter = g_flip_counter + 1
    if g_flip_counter >= 5:
        console_periodic_flip()
        g_flip_counter = 0
    end
end

proc timer_poll_rv64():
    // RV64 timer fires via IRQ
end

proc timer_idle_poll_rv64():
    arch_cpu_idle()
    timer_poll_rv64()
end

proc timer_delay_ms_rv64(ms):
    var target = arch_timer_count() + (g_timer_freq / 1000) * ms
    while arch_timer_count() < target:
        arch_cpu_relax()
    end
end

// ============================================================================
// Unified entry points
// ============================================================================

proc timer_init():
    g_ticks        = 0
    g_flip_counter = 0
    var arch = arch_id()
    if arch == ARCH_X86:
        timer_init_x86()
    elif arch == ARCH_ARM64:
        timer_init_arm64()
    elif arch == ARCH_RV64:
        timer_init_rv64()
    end
end

proc timer_irq():
    var arch = arch_id()
    if arch == ARCH_X86:
        timer_irq_x86()
    elif arch == ARCH_ARM64:
        timer_irq_arm64()
    elif arch == ARCH_RV64:
        timer_irq_rv64()
    end
end

proc timer_poll():
    var arch = arch_id()
    if arch == ARCH_X86:
        timer_poll_x86()
    elif arch == ARCH_ARM64:
        timer_poll_arm64()
    elif arch == ARCH_RV64:
        timer_poll_rv64()
    end
end

proc timer_idle_poll():
    var arch = arch_id()
    if arch == ARCH_X86:
        timer_idle_poll_x86()
    elif arch == ARCH_ARM64:
        timer_idle_poll_arm64()
    elif arch == ARCH_RV64:
        timer_idle_poll_rv64()
    end
end

proc timer_delay_ms(ms):
    var arch = arch_id()
    if arch == ARCH_X86:
        timer_delay_ms_x86(ms)
    elif arch == ARCH_ARM64:
        timer_delay_ms_arm64(ms)
    elif arch == ARCH_RV64:
        timer_delay_ms_rv64(ms)
    end
end

proc timer_ticks():
    return g_ticks
end

proc timer_seconds():
    return g_ticks / PIT_HZ
end

proc timer_cmd_info():
    // Hooked by shell's `timer info` command
end
