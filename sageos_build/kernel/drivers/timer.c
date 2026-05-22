#include <stdint.h>
#include "io.h"
#include "timer.h"
#include "console.h"
#include "scheduler.h"
#include "metal_vm.h"
#include "smp.h"
#include "../core/sagelang/runtime.h"

#define PIT_HZ 100
#define PIT_BASE_HZ 1193182
#define PIT_CH0 0x40
#define PIT_CMD 0x43

static uint16_t pit_reload;
static volatile uint64_t ticks;

static volatile uint64_t idle_loops[SAGEOS_MAX_CPUS];
static volatile uint64_t total_loops[SAGEOS_MAX_CPUS];
static uint64_t last_idle_loops[SAGEOS_MAX_CPUS];
static uint64_t last_total_loops[SAGEOS_MAX_CPUS];
static volatile uint32_t cached_cpu_percent[SAGEOS_MAX_CPUS];

#define CPU_WINDOW_SIZE 100

static uint32_t cpu_history[CPU_WINDOW_SIZE];
static uint32_t cpu_history_idx;
static uint32_t cpu_history_sum;
static uint32_t cpu_history_count;

/* Forward declaration for framebuffer flip */
void console_periodic_flip(void);

void timer_init(void) {
    pit_reload = (uint16_t)(PIT_BASE_HZ / PIT_HZ);

    if (pit_reload == 0) {
        pit_reload = 11932;
    }

    /*
     * Channel 0, lo/hi byte, mode 2 rate generator.
     */
    outb(PIT_CMD, 0x34);
    outb(PIT_CH0, (uint8_t)(pit_reload & 0xFF));
    outb(PIT_CH0, (uint8_t)(pit_reload >> 8));

    ticks = 0;
    for (int i = 0; i < SAGEOS_MAX_CPUS; i++) {
        idle_loops[i] = 0;
        total_loops[i] = 0;
        last_idle_loops[i] = 0;
        last_total_loops[i] = 0;
        cached_cpu_percent[i] = 0;
    }

    for (int i = 0; i < CPU_WINDOW_SIZE; i++) cpu_history[i] = 0;
    cpu_history_idx = 0;
    cpu_history_sum = 0;
    cpu_history_count = 0;
}

extern MetalVM g_repl_vm;

static void timer_update_cpu_percent(void) {
    static uint64_t last_update_ticks = 0;
    if (ticks - last_update_ticks >= 10) {
        for (uint32_t i = 0; i < smp_cpu_count(); i++) {
            uint64_t diff_idle = idle_loops[i] - last_idle_loops[i];
            uint64_t diff_total = total_loops[i] - last_total_loops[i];
            if (diff_total > 0) {
                uint32_t idle_pct = (uint32_t)((diff_idle * 100ULL) / diff_total);
                if (idle_pct > 100) idle_pct = 100;
                cached_cpu_percent[i] = 100 - idle_pct;
            } else {
                cached_cpu_percent[i] = 0;
            }
            last_idle_loops[i] = idle_loops[i];
            last_total_loops[i] = total_loops[i];
        }
        last_update_ticks = ticks;
    }
}

void timer_irq(void) {
    ticks++;
    timer_update_cpu_percent();
    sched_timer_tick();
    // Direct call into SageLang timer_irq
    metal_vm_call(&g_repl_vm, "timer_irq", NULL, 0);
}

void timer_poll(void) {
    uint32_t cpu = smp_current_cpu_index();
    total_loops[cpu]++;

    /* Update ticks in firmware mode */
    if (cpu == 0) {
        ticks++;
        timer_update_cpu_percent();
    }

    /*
     * Drive the framebuffer periodic flip in firmware-input mode.
     */
    static volatile uint32_t fb_poll_counter = 0;
    if (++fb_poll_counter >= 500) {
        fb_poll_counter = 0;
        console_periodic_flip();
    }
}

void timer_idle_poll(void) {
    uint32_t cpu = smp_current_cpu_index();
    idle_loops[cpu]++;
    total_loops[cpu]++;
    cpu_pause();
}

uint64_t timer_ticks(void) {
    return ticks;
}

uint64_t timer_seconds(void) {
    return ticks / PIT_HZ;
}

uint32_t timer_cpu_percent(void) {
    return cached_cpu_percent[0];
}

uint32_t timer_cpu_percent_at(uint32_t cpu) {
    if (cpu >= SAGEOS_MAX_CPUS) return 0;
    return cached_cpu_percent[cpu];
}

void timer_delay_ms(uint32_t ms) {
    if (pit_reload == 0) {
        /* Firmware mode: PIT not initialized, use a rough busy loop */
        for (uint32_t i = 0; i < ms; i++) {
            for (uint32_t j = 0; j < 10000; j++) {
                cpu_pause();
            }
        }
        return;
    }

    uint64_t start = ticks;
    uint64_t end = start + (ms * PIT_HZ) / 1000;
    if (end == start && ms > 0) end++;
    while (ticks < end) {
        timer_poll();
        sched_yield();
    }
}

void timer_cmd_info(void) {
    console_write("\nTimer:");
    console_write("\n  backend: PIT IRQ0 through PIC");
    console_write("\n  hz: ");
    console_u32(PIT_HZ);
    console_write("\n  reload: ");
    console_u32(pit_reload);
    console_write("\n  ticks: ");
    console_hex64(ticks);
    console_write("\n  seconds: ");
    console_hex64(timer_seconds());
    console_write("\n  cpu: ");
    console_u32(cached_cpu_percent[0]);
    console_write("%");
}
