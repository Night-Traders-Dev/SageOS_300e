#include <stdint.h>
#include <stddef.h>
#include "bootinfo.h"
#include "console.h"
#include "status.h"
#include "timer.h"
#include "battery.h"

static uint32_t tick_counter;

static void append_char(char *buf, size_t cap, size_t *pos, char c) {
    if (*pos + 1 < cap) {
        buf[*pos] = c;
        (*pos)++;
        buf[*pos] = 0;
    }
}

static void append_str(char *buf, size_t cap, size_t *pos, const char *s) {
    while (*s) {
        append_char(buf, cap, pos, *s++);
    }
}

static void append_u32(char *buf, size_t cap, size_t *pos, uint32_t v) {
    char tmp[16];
    int n = 0;

    if (v == 0) {
        append_char(buf, cap, pos, '0');
        return;
    }

    while (v && n < 15) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }

    while (n > 0) {
        append_char(buf, cap, pos, tmp[--n]);
    }
}

static uint32_t ram_percent(void) {
    SageOSBootInfo *b = console_boot_info();

    if (!b || !b->memory_total) {
        return 0xFFFFFFFFU;
    }

    uint64_t used = 0;

    if (b->memory_total > b->memory_usable) {
        used = b->memory_total - b->memory_usable;
    }

    return (uint32_t)((used * 100ULL) / b->memory_total);
}

static uint32_t cpu_percent(void) {
    return timer_cpu_percent();
}

static uint32_t battery_percent_status(void) {
    int pct = battery_percent();

    if (pct < 0) {
        return 0xFFFFFFFFU;
    }

    return (uint32_t)pct;
}

void status_refresh(void) {
    char text[96];
    size_t pos = 0;

    text[0] = 0;

    tick_counter++;

    append_str(text, sizeof(text), &pos, "BAT ");
    uint32_t bat = battery_percent_status();

    if (bat == 0xFFFFFFFFU) {
        append_str(text, sizeof(text), &pos, "--");
    } else {
        append_u32(text, sizeof(text), &pos, bat);
    }

    append_str(text, sizeof(text), &pos, "%  CPU ");
    uint32_t cpu = cpu_percent();

    if (cpu == 0xFFFFFFFFU) {
        append_str(text, sizeof(text), &pos, "--");
    } else {
        append_u32(text, sizeof(text), &pos, cpu);
    }

    append_str(text, sizeof(text), &pos, "%  RAM ");
    uint32_t ram = ram_percent();

    if (ram == 0xFFFFFFFFU) {
        append_str(text, sizeof(text), &pos, "--");
    } else {
        append_u32(text, sizeof(text), &pos, ram);
    }

    append_char(text, sizeof(text), &pos, '%');

    console_draw_status_bar(text);
}

void status_print(void) {
    SageOSBootInfo *b = console_boot_info();

    console_write("\nStatus:");
    console_write("\n  battery: ACPI/EC detector active; percentage pending AML/EC query");
    console_write("\n  cpu: PIT polling + idle-loop accounting");
    console_write("\n  ram: ");

    uint32_t ram = ram_percent();

    if (ram == 0xFFFFFFFFU) {
        console_write("unavailable, no UEFI memory summary");
    } else {
        console_u32(ram);
        console_write("% reserved/used by early firmware/kernel view");
    }

    if (b) {
        console_write("\n  memory_total: ");
        console_hex64(b->memory_total);
        console_write("\n  memory_usable: ");
        console_hex64(b->memory_usable);
        console_write("\n  kernel_base: ");
        console_hex64(b->kernel_base);
        console_write("\n  kernel_size: ");
        console_hex64(b->kernel_size);
    }
}

void status_init(void) {
    tick_counter = 0;
    status_refresh();
}
