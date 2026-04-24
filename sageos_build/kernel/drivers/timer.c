#include <stdint.h>
#include "io.h"
#include "timer.h"
#include "console.h"

#define PIT_HZ 100
#define PIT_BASE_HZ 1193182
#define PIT_CH0 0x40
#define PIT_CMD 0x43

static uint16_t pit_reload;
static volatile uint64_t ticks;

static volatile uint64_t idle_loops;
static volatile uint64_t total_loops;
static uint64_t last_idle_loops;
static uint64_t last_total_loops;
static volatile uint32_t cached_cpu_percent;

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
    idle_loops = 0;
    total_loops = 0;
    last_idle_loops = 0;
    last_total_loops = 0;
    cached_cpu_percent = 0;
}

void timer_irq(void) {
    ticks++;

    uint64_t idle_delta = idle_loops - last_idle_loops;
    uint64_t total_delta = total_loops - last_total_loops;

    if (total_delta > 0) {
        uint64_t idle_pct = (idle_delta * 100ULL) / total_delta;

        if (idle_pct > 100) {
            idle_pct = 100;
        }

        cached_cpu_percent = (uint32_t)(100 - (uint32_t)idle_pct);
    } else {
        /*
         * If total_delta is 0, it means no poll/idle loops were performed
         * since the last tick, meaning the CPU was fully occupied by
         * kernel or command execution.
         */
        cached_cpu_percent = 100;
    }

    last_idle_loops = idle_loops;
    last_total_loops = total_loops;
}

void timer_poll(void) {
    total_loops++;
}

void timer_idle_poll(void) {
    idle_loops++;
    total_loops++;
    cpu_pause();
}

uint64_t timer_ticks(void) {
    return ticks;
}

uint64_t timer_seconds(void) {
    return ticks / PIT_HZ;
}

uint32_t timer_cpu_percent(void) {
    return cached_cpu_percent;
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
    console_u32(cached_cpu_percent);
    console_write("%");
}
