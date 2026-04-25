#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "timer.h"
#include "status.h"
#include "battery.h"
#include "sysinfo.h"
#include "bootinfo.h"
#include "version.h"
#include "keyboard.h"
#include "shell.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void draw_bar(uint32_t val, uint32_t max, uint32_t width) {
    if (max == 0) max = 1;
    uint32_t filled = (val * width) / max;
    if (filled > width) filled = width;

    console_write("[");
    for (uint32_t i = 0; i < width; i++) {
        if (i < filled) console_write("#");
        else console_write(" ");
    }
    console_write("]");
}

static void print_mb(uint64_t bytes) {
    console_u32((uint32_t)(bytes / 1024 / 1024));
    console_write(" MB");
}

/* ------------------------------------------------------------------ */
/* Neofetch                                                           */
/* ------------------------------------------------------------------ */

void cmd_neofetch(void) {
    uint32_t old_fg = console_get_fg();
    SageOSBootInfo *b = console_boot_info();
    
    const char *logo[] = {
        "       _       ",
        "     _/ \\_     ",
        "    /     \\    ",
        "   |  (S)  |   ",
        "    \\_   _/    ",
        "      \\ /      ",
        "       |       ",
        "               "
    };

    console_write("\n");
    for (int i = 0; i < 8; i++) {
        console_set_fg(0x79FFB0);
        console_write(logo[i]);
        console_write("  ");
        console_set_fg(0xE8E8E8);

        switch (i) {
            case 0: 
                console_set_fg(0x80C8FF); console_write("root");
                console_set_fg(0xE8E8E8); console_write("@");
                console_set_fg(0x80C8FF); console_write("sageos");
                break;
            case 1:
                console_write("-------------");
                break;
            case 2:
                console_set_fg(0x79FFB0); console_write("OS: "); 
                console_set_fg(0xE8E8E8); console_write("SageOS");
                break;
            case 3:
                console_set_fg(0x79FFB0); console_write("Kernel: "); 
                console_set_fg(0xE8E8E8); console_write("SageOS v" SAGEOS_VERSION);
                break;
            case 4:
                console_set_fg(0x79FFB0); console_write("Uptime: ");
                console_set_fg(0xE8E8E8);
                uint64_t secs = timer_seconds();
                if (secs / 3600) { console_u32((uint32_t)(secs / 3600)); console_write("h "); }
                if ((secs / 60) % 60) { console_u32((uint32_t)((secs / 60) % 60)); console_write("m "); }
                console_u32((uint32_t)(secs % 60)); console_write("s");
                break;
            case 5:
                console_set_fg(0x79FFB0); console_write("Shell: "); 
                console_set_fg(0xE8E8E8); console_write("sage-sh");
                break;
            case 6:
                if (b) {
                    console_set_fg(0x79FFB0); console_write("Res: "); 
                    console_set_fg(0xE8E8E8); console_u32(b->width); console_write("x"); console_u32(b->height);
                }
                break;
            case 7:
                console_set_fg(0x79FFB0); console_write("Mem: ");
                console_set_fg(0xE8E8E8);
                print_mb(ram_used_bytes());
                console_write(" / ");
                print_mb(ram_total_bytes());
                break;
        }
        console_write("\n");
    }
    console_set_fg(old_fg);
}

/* ------------------------------------------------------------------ */
/* BTOP                                                               */
/* ------------------------------------------------------------------ */

void cmd_btop(void) {
    console_clear();
    uint32_t old_fg = console_get_fg();

    for (;;) {
        console_set_cursor(0, 0);
        console_set_fg(0x80C8FF);
        console_write("=== SageOS BTOP - Press 'q' to exit ===\n");
        console_set_fg(0xE8E8E8);

        // System Header
        console_write("Uptime: ");
        uint64_t secs = timer_seconds();
        console_u32((uint32_t)secs); console_write("s    ");
        console_write("Kernel: v" SAGEOS_VERSION "\n\n");

        // CPU Section
        uint32_t cpu = timer_cpu_percent();
        console_set_fg(0x79FFB0);
        console_write("CPU Usage: ");
        draw_bar(cpu, 100, 30);
        console_write(" "); console_u32(cpu); console_write("%\n");

        // Memory Section
        uint64_t used = ram_used_bytes();
        uint64_t total = ram_total_bytes();
        console_set_fg(0xFFBF40);
        console_write("Memory:    ");
        draw_bar((uint32_t)(used / 1024), (uint32_t)(total / 1024), 30);
        console_write(" "); print_mb(used); console_write(" / "); print_mb(total);
        console_write("\n");

        // Battery Section
        int bat = battery_percent();
        console_set_fg(0xFF7070);
        console_write("Battery:   ");
        if (bat >= 0) {
            draw_bar((uint32_t)bat, 100, 30);
            console_write(" "); console_u32((uint32_t)bat); console_write("%\n");
        } else {
            console_write("[      N/A      ] --%\n");
        }

        console_set_fg(0xE8E8E8);
        console_write("\nTasks:\n");
        console_write("  PID  NAME          STATUS\n");
        console_write("  0    kernel        running\n");
        console_write("  1    idle          waiting\n");
        console_write("  2    shell         active\n");
        console_write("  3    timer         active\n");
        console_write("  4    status_bar    active\n");

        for (int i = 0; i < 50; i++) timer_poll();
        timer_delay_ms(200);

        KeyEvent ev;
        if (keyboard_poll_event(&ev)) {
            if (ev.pressed && ev.ascii == 'q') break;
        }
    }

    console_set_fg(old_fg);
    console_clear();
}
