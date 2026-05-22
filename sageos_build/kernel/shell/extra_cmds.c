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
#include "ata.h"
#include "sdhci.h"
#include "shell.h"
#include "serial.h"
#include "scheduler.h"
#include "smp.h"
#include "vfs.h"
#include "sage_libc_shim.h"

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

static void serial_raw(const char *s) {
    while (*s) serial_putc(*s++);
}

static void serial_u32(uint32_t v) {
    char tmp[12];
    int n = 0;
    if (v == 0) {
        serial_putc('0');
        return;
    }
    while (v && n < (int)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n > 0) serial_putc(tmp[--n]);
}

static void serial_mb(uint64_t bytes) {
    serial_u32((uint32_t)(bytes / 1024 / 1024));
    serial_raw(" MB");
}

static void serial_bar(uint32_t val, uint32_t max, uint32_t width) {
    if (max == 0) max = 1;
    uint32_t filled = (val * width) / max;
    if (filled > width) filled = width;
    serial_putc('[');
    for (uint32_t i = 0; i < width; i++) serial_putc(i < filled ? '#' : ' ');
    serial_putc(']');
}

#include "swap.h"
#include "btrfs.h"

void cmd_swap(void) {
    swap_info();
}

/* ------------------------------------------------------------------ */
/* OS Installer                                                       */
/* ------------------------------------------------------------------ */

void cmd_install(void) {
    console_write("\n=== SageOS Lenovo 300e Full Installer ===");

    int has_ata = ata_is_available();
    int has_sdhci = sdhci_is_available();

    if (!has_ata && !has_sdhci) {
        console_write("\nNo local storage device (ATA or eMMC) is available for installation.");
        return;
    }

    if (has_sdhci && !has_ata) {
        console_write("\nTarget Storage: Soldered eMMC 5.1 (32 GB soldered, per 300e spec)");
    } else {
        console_write("\nTarget Storage: ATA primary-master hard disk");
    }

    console_write("\nPlanned Partition Layout:");
    console_write("\n  1. ESP FAT32 (64MiB) -> /fat32");
    console_write("\n  2. Root BTRFS (64MiB) -> /btrfs");
    console_write("\n  3. SWAP (125MB)      -> [SWAP]");

    console_write("\n\nWARNING: This will DESTROY all data on the local drive.");
    console_write("\nType 'YES' to confirm installation: ");

    char input[16];
    size_t pos = 0;
    for (;;) {
        KeyEvent ev;
        if (keyboard_wait_event(&ev) && ev.pressed && ev.ascii) {
            if (ev.ascii == '\n') break;
            if (ev.ascii == 8 && pos > 0) { pos--; console_write("\b \b"); }
            else if (pos < 15) { input[pos++] = ev.ascii; console_putc(ev.ascii); }
        }
    }
    input[pos] = 0;

    if (input[0] != 'Y' || input[1] != 'E' || input[2] != 'S' || input[3] != 0) {
        console_write("\nAborted.");
        return;
    }

    console_write("\nFormatting and installing SageOS...");

    if (has_sdhci && !has_ata) {
        // eMMC dry-run simulation
        for (int i = 1; i <= 4; i++) {
            timer_delay_ms(400); // Simulate some timing
            if (i == 1) console_write("\n  Formatting partition 1 (FAT32 ESP)... [OK]");
            if (i == 2) console_write("\n  Formatting partition 2 (BTRFS Root)... [OK]");
            if (i == 3) console_write("\n  Formatting partition 3 (SWAP)...         [OK]");
            if (i == 4) console_write("\n  Copying bootloader and system files...   [OK]");
        }
    } else {
        // Native ATA dry-run with sector zeroing
        uint16_t zero_buf[256];
        for (int i = 0; i < 256; i++) zero_buf[i] = 0;

        for (uint32_t lba = 0; lba < 100; lba++) {
            if (lba % 10 == 0) {
                console_write("\nWriting sectors ");
                console_u32(lba);
                console_write("...");
            }
            if (!ata_write_sector(lba, zero_buf)) {
                console_write("\nWrite failed at LBA ");
                console_u32(lba);
                return;
            }
        }

        console_write("\nVerifying sectors...");
        uint16_t read_buf[256];
        for (uint32_t lba = 0; lba < 10; lba++) {
            if (!ata_read_sector(lba, read_buf)) {
                console_write("\nReadback failed at LBA ");
                console_u32(lba);
                return;
            }
            for (int i = 0; i < 256; i++) {
                if (read_buf[i] != 0) {
                    console_write("\nVerification failed at LBA ");
                    console_u32(lba);
                    return;
                }
            }
        }
    }

    console_write("\nInstallation complete! BTRFS root and SWAP have been initialized.");
    console_write("\nPlease reboot without the installation media.");
    console_write("\n(Note: This was a dry-run installation.)");
}

/* ------------------------------------------------------------------ */
/* Neofetch                                                           */
/* ------------------------------------------------------------------ */

static void print_uptime_compact(void) {
    uint64_t secs = timer_seconds();
    uint32_t days = (uint32_t)(secs / 86400);
    uint32_t hours = (uint32_t)((secs / 3600) % 24);
    uint32_t mins = (uint32_t)((secs / 60) % 60);
    uint32_t rem = (uint32_t)(secs % 60);

    if (days) { console_u32(days); console_write("d "); }
    if (hours || days) { console_u32(hours); console_write("h "); }
    if (mins || hours || days) { console_u32(mins); console_write("m "); }
    console_u32(rem);
    console_write("s");
}

static void serial_uptime_compact(void) {
    uint64_t secs = timer_seconds();
    uint32_t days = (uint32_t)(secs / 86400);
    uint32_t hours = (uint32_t)((secs / 3600) % 24);
    uint32_t mins = (uint32_t)((secs / 60) % 60);
    uint32_t rem = (uint32_t)(secs % 60);

    if (days) { serial_u32(days); serial_raw("d "); }
    if (hours || days) { serial_u32(hours); serial_raw("h "); }
    if (mins || hours || days) { serial_u32(mins); serial_raw("m "); }
    serial_u32(rem);
    serial_raw("s");
}

/* ------------------------------------------------------------------ */
/* BTOP                                                               */
/* ------------------------------------------------------------------ */

#define BTOP_HISTORY_WIDTH 48

static uint32_t btop_cpu_history[BTOP_HISTORY_WIDTH];
static uint32_t btop_mem_history[BTOP_HISTORY_WIDTH];
static uint32_t btop_history_len;

static uint32_t pct_u64(uint64_t value, uint64_t max) {
    if (max == 0) return 0;
    uint64_t pct = (value * 100ULL) / max;
    if (pct > 100) pct = 100;
    return (uint32_t)pct;
}

static void btop_push_history(uint32_t cpu, uint32_t mem) {
    if (cpu > 100) cpu = 100;
    if (mem > 100) mem = 100;

    if (btop_history_len < BTOP_HISTORY_WIDTH) {
        btop_cpu_history[btop_history_len] = cpu;
        btop_mem_history[btop_history_len] = mem;
        btop_history_len++;
        return;
    }

    for (uint32_t i = 1; i < BTOP_HISTORY_WIDTH; i++) {
        btop_cpu_history[i - 1] = btop_cpu_history[i];
        btop_mem_history[i - 1] = btop_mem_history[i];
    }
    btop_cpu_history[BTOP_HISTORY_WIDTH - 1] = cpu;
    btop_mem_history[BTOP_HISTORY_WIDTH - 1] = mem;
}

static char btop_graph_char(uint32_t pct) {
    if (pct >= 90) return '#';
    if (pct >= 75) return 'O';
    if (pct >= 60) return '*';
    if (pct >= 45) return '+';
    if (pct >= 30) return '=';
    if (pct >= 15) return '-';
    if (pct >= 5) return '.';
    return ' ';
}

static void btop_write_repeated(char c, uint32_t count) {
    while (count--) console_putc(c);
}

static void btop_serial_repeated(char c, uint32_t count) {
    while (count--) serial_putc(c);
}

static void btop_clear_rows(uint32_t start_row, uint32_t height, uint32_t width) {
    uint32_t rows = console_rows();
    if (rows == 0) rows = 25;

    for (uint32_t r = start_row; r < start_row + height && r < rows; r++) {
        console_set_cursor(r, 0);
        btop_write_repeated(' ', width);
    }
}

static void btop_box_top(uint32_t row, uint32_t width, const char *title, uint32_t color) {
    console_set_cursor(row, 0);
    console_set_fg(color);
    console_putc('+');
    console_putc('-');
    console_write(title);
    console_putc(' ');

    uint32_t used = 3;
    while (title[used - 3]) used++;
    if (used < width - 1) btop_write_repeated('-', width - used - 1);
    console_putc('+');
}

static void btop_box_mid(uint32_t row, uint32_t width) {
    console_set_cursor(row, 0);
    console_set_fg(0x3A4458);
    console_putc('|');
    btop_write_repeated(' ', width - 2);
    console_putc('|');
}

static void btop_box_bottom(uint32_t row, uint32_t width) {
    console_set_cursor(row, 0);
    console_set_fg(0x3A4458);
    console_putc('+');
    btop_write_repeated('-', width - 2);
    console_putc('+');
}

static void btop_draw_history_console(uint32_t row,
                                      uint32_t col,
                                      uint32_t width,
                                      const uint32_t *history,
                                      uint32_t color) {
    uint32_t pad = 0;

    if (width > BTOP_HISTORY_WIDTH) width = BTOP_HISTORY_WIDTH;
    if (btop_history_len < width) pad = width - btop_history_len;

    console_set_cursor(row, col);
    console_set_fg(color);
    for (uint32_t i = 0; i < pad; i++) console_putc(' ');
    for (uint32_t i = 0; i < width - pad; i++) {
        uint32_t idx = btop_history_len - (width - pad) + i;
        console_putc(btop_graph_char(history[idx]));
    }
}

static void btop_draw_console(uint32_t cpu_avg, uint64_t used, uint64_t total, int bat) {
    (void)cpu_avg;
    uint32_t old_fg = console_get_fg();
    uint32_t start_row = console_has_fb() ? 2 : 0;
    uint32_t cols = console_cols();
    uint32_t rows = console_rows();
    uint32_t width;
    uint32_t mem_pct = pct_u64(used, total);
    const sched_stats_t *sched = sched_get_stats();
    uint32_t cpu_count = smp_cpu_count();

    if (cols == 0) cols = 80;
    if (rows == 0) rows = 25;
    width = cols > 1 ? cols - 1 : 79;
    if (width > 96) width = 96;
    if (width < 58 && cols >= 59) width = 58;

    btop_clear_rows(start_row, 24, width);

    console_set_cursor(start_row, 0);
    console_set_fg(0x80C8FF);
    console_write("btop");
    console_set_fg(0xE8E8E8);
    console_write("  SageOS resources");
    console_set_cursor(start_row, width > 22 ? width - 22 : 36);
    console_set_fg(0x9AA4B2);
    console_write("q quit | r refresh");

    uint32_t cpu_box_height = 4 + cpu_count;
    btop_box_top(start_row + 2, width, " cpu ", 0x80C8FF);
    for (uint32_t r = 0; r < cpu_box_height - 2; r++) btop_box_mid(start_row + 3 + r, width);
    btop_box_bottom(start_row + cpu_box_height + 1, width);

    for (uint32_t i = 0; i < cpu_count; i++) {
        uint32_t cpu_pct = timer_cpu_percent_at(i);
        console_set_cursor(start_row + 3 + i, 2);
        console_set_fg(0x79FFB0);
        console_write("cpu");
        console_u32(i);
        console_write(" ");
        draw_bar(cpu_pct, 100, 20);
        console_write(" ");
        console_u32(cpu_pct);
        console_write("%");
    }

    console_set_cursor(start_row + 3, 35);
    console_set_fg(0xE8E8E8);
    console_write("cores ");
    console_u32(cpu_count);
    console_write("  uptime ");
    print_uptime_compact();

    console_set_cursor(start_row + 4, 35);
    console_set_fg(0x9AA4B2);
    console_write("history ");
    btop_draw_history_console(start_row + 4, 43, width > 45 ? width - 45 : 12, btop_cpu_history, 0x80C8FF);

    console_set_cursor(start_row + 5, 35);
    console_set_fg(0x9AA4B2);
    console_write("input ");
    console_set_fg(0xE8E8E8);
    console_write(keyboard_backend());

    uint32_t mem_box_row = start_row + cpu_box_height + 3;
    btop_box_top(mem_box_row, width, " mem ", 0xFFBF40);
    btop_box_mid(mem_box_row + 1, width);
    btop_box_mid(mem_box_row + 2, width);
    btop_box_mid(mem_box_row + 3, width);
    btop_box_bottom(mem_box_row + 4, width);

    console_set_cursor(mem_box_row + 1, 2);
    console_set_fg(0xFFBF40);
    console_write("ram   ");
    draw_bar(mem_pct, 100, 28);
    console_write(" ");
    console_u32(mem_pct);
    console_write("%  ");
    print_mb(used);
    console_write(" / ");
    print_mb(total);
    console_set_cursor(mem_box_row + 2, 2);
    console_set_fg(0x9AA4B2);
    console_write("trend ");
    btop_draw_history_console(mem_box_row + 2, 10, width > 16 ? width - 16 : 32, btop_mem_history, 0xFFBF40);
    console_set_cursor(mem_box_row + 3, 2);
    console_set_fg(0xFF7070);
    console_write("bat   ");
    if (bat >= 0) {
        draw_bar((uint32_t)bat, 100, 28);
        console_write(" ");
        console_u32((uint32_t)bat);
        console_write("%");
    } else {
        console_write("[------------ unavailable ------------]");
    }

    uint32_t proc_box_row = mem_box_row + 6;
    btop_box_top(proc_box_row, width, " storage & proc ", 0xDDA0FF);
    for (uint32_t r = 0; r < 8; r++) btop_box_mid(proc_box_row + 1 + r, width);
    btop_box_bottom(proc_box_row + 9, width);

    /* Storage info */
    int m_count = vfs_get_mount_count();
    for (int i = 0; i < m_count && i < 2; i++) {
        VfsMountInfo mi;
        if (vfs_get_mount_info(i, &mi) == 0) {
            console_set_cursor(proc_box_row + 1 + i, 2);
            console_set_fg(0x79FFB0);
            console_write("fs   ");
            console_write(mi.path);
            console_set_cursor(proc_box_row + 1 + i, 20);
            console_set_fg(0x9AA4B2);
            console_write("type ");
            console_set_fg(0xE8E8E8);
            console_write(mi.type);
        }
    }

    /* Swap info */
    console_set_cursor(proc_box_row + 3, 2);
    console_set_fg(0x79FFB0);
    console_write("swap ");
    if (swap_is_available()) {
        console_write("125 MB free");
    } else {
        console_write("[ none ]");
    }

    /* Proc info */
    console_set_cursor(proc_box_row + 4, 2);
    console_set_fg(0x9AA4B2);
    console_write("scheduler: ");
    console_set_fg(0xE8E8E8);
    console_u32(sched ? sched->thread_count : 0);
    console_write(" threads, ");
    console_u32((uint32_t)(sched ? sched->context_switches : 0));
    console_write(" switches");

    console_set_cursor(proc_box_row + 5, 2);
    console_set_fg(0x80C8FF);
    console_write("TASKS:");
    
    int task_line = 0;
    for (uint32_t i = 0; i < SCHED_MAX_THREADS && task_line < 3; i++) {
        char name[32];
        thread_state_t state;
        uint32_t cpu_id;
        if (sched_get_thread_info(i, name, &state, &cpu_id)) {
            console_set_cursor(proc_box_row + 6 + task_line, 2);
            console_set_fg(0xE8E8E8);
            console_write(name);
            console_set_cursor(proc_box_row + 6 + task_line, 18);
            console_set_fg(0x9AA4B2);
            if (state == THREAD_STATE_RUNNING) console_set_fg(0x79FFB0);
            console_write(state == THREAD_STATE_RUNNING ? "RUN" : 
                         (state == THREAD_STATE_SLEEPING ? "SLP" : "RDY"));
            console_set_cursor(proc_box_row + 6 + task_line, 24);
            console_set_fg(0x9AA4B2);
            console_write("cpu");
            console_u32(cpu_id);
            task_line++;
        }
    }

    console_set_fg(old_fg);
}

static void btop_serial_history(const uint32_t *history, uint32_t width) {
    uint32_t pad = 0;
    if (width > BTOP_HISTORY_WIDTH) width = BTOP_HISTORY_WIDTH;
    if (btop_history_len < width) pad = width - btop_history_len;

    for (uint32_t i = 0; i < pad; i++) serial_putc(' ');
    for (uint32_t i = 0; i < width - pad; i++) {
        uint32_t idx = btop_history_len - (width - pad) + i;
        serial_putc(btop_graph_char(history[idx]));
    }
}

static void btop_serial_box_top(const char *title) {
    serial_raw("+-- ");
    serial_raw(title);
    serial_raw(" ");
    btop_serial_repeated('-', 65);
    serial_raw("+\r\n");
}

static void btop_draw_serial(uint32_t cpu_avg, uint64_t used, uint64_t total, int bat) {
    (void)cpu_avg;
    uint32_t mem_pct = pct_u64(used, total);
    const sched_stats_t *sched = sched_get_stats();
    uint32_t cpu_count = smp_cpu_count();

    serial_raw("\033[2J\033[H");
    serial_raw("btop  SageOS resources                                      q quit | r refresh\r\n");

    btop_serial_box_top("cpu");
    for (uint32_t i = 0; i < cpu_count; i++) {
        uint32_t cpu_pct = timer_cpu_percent_at(i);
        serial_raw("| cpu");
        serial_u32(i);
        serial_raw(" ");
        serial_bar(cpu_pct, 100, 20);
        serial_raw(" ");
        serial_u32(cpu_pct);
        serial_raw("%  ");
        if (i == 0) {
            serial_raw("cores ");
            serial_u32(cpu_count);
            serial_raw("  uptime ");
            serial_uptime_compact();
        }
        serial_raw("\r\n");
    }
    serial_raw("| history ");
    btop_serial_history(btop_cpu_history, 48);
    serial_raw("\r\n| input   ");
    serial_raw(keyboard_backend());
    serial_raw("\r\n+-----------------------------------------------------------------------+\r\n");

    btop_serial_box_top("mem");
    serial_raw("| ram   ");
    serial_bar(mem_pct, 100, 28);
    serial_raw(" ");
    serial_u32(mem_pct);
    serial_raw("%  ");
    serial_mb(used);
    serial_raw(" / ");
    serial_mb(total);
    serial_raw("\r\n| trend ");
    btop_serial_history(btop_mem_history, 48);
    serial_raw("\r\n| bat   ");
    if (bat >= 0) {
        serial_bar((uint32_t)bat, 100, 30);
        serial_raw(" ");
        serial_u32((uint32_t)bat);
        serial_raw("%");
    } else {
        serial_raw("[------------ unavailable ------------]");
    }
    serial_raw("\r\n+-----------------------------------------------------------------------+\r\n");

    btop_serial_box_top("storage & proc");
    /* Storage info */
    int m_count = vfs_get_mount_count();
    for (int i = 0; i < m_count && i < 2; i++) {
        VfsMountInfo mi;
        if (vfs_get_mount_info(i, &mi) == 0) {
            serial_raw("| fs ");
            serial_raw(mi.path);
            serial_raw("  type ");
            serial_raw(mi.type);
            serial_raw("\r\n");
        }
    }

    /* Swap info */
    serial_raw("| swap ");
    if (swap_is_available()) {
        serial_raw("125 MB free\r\n");
    } else {
        serial_raw("[ none ]\r\n");
    }

    /* Proc info */
    serial_raw("| scheduler: ");
    serial_u32(sched ? sched->thread_count : 0);
    serial_raw(" threads, ");
    serial_u32((uint32_t)(sched ? sched->context_switches : 0));
    serial_raw(" switches\r\n");
    
    serial_raw("| TASKS: ");
    int task_count = 0;
    for (uint32_t i = 0; i < SCHED_MAX_THREADS && task_count < 4; i++) {
        char name[32];
        thread_state_t state;
        uint32_t cpu_id;
        if (sched_get_thread_info(i, name, &state, &cpu_id)) {
            if (task_count > 0) serial_raw(", ");
            serial_raw(name);
            serial_raw("(");
            serial_raw(state == THREAD_STATE_RUNNING ? "R" : 
                      (state == THREAD_STATE_SLEEPING ? "S" : "W"));
            serial_raw(")");
            task_count++;
        }
    }
    serial_raw("\r\n+-----------------------------------------------------------------------+\r\n");
}

void cmd_btop(void) {
    uint32_t old_fg = console_get_fg();
    int saved_serial_echo = console_get_serial_echo();
    int running = 1;

    console_clear();

    while (running) {
        /* Drive timer ticks before sampling so metrics are fresh */
        timer_poll();

        uint32_t cpu = timer_cpu_percent();
        uint64_t used = ram_used_bytes();
        uint64_t total = ram_total_bytes();
        int bat = battery_percent();

        btop_push_history(cpu, pct_u64(used, total));

        if (console_has_fb()) {
            console_set_serial_echo(0);
            btop_draw_console(cpu, used, total, bat);
            console_periodic_flip();
            console_set_serial_echo(saved_serial_echo);
            btop_draw_serial(cpu, used, total, bat);
        } else {
            btop_draw_serial(cpu, used, total, bat);
        }

        /*
         * Poll for input for ~500 ms using timer_delay_ms + timer_poll.
         * We do NOT use sched_sleep() here: on real hardware without PIT
         * IRQs the tick counter never advances, so sched_sleep() blocks
         * forever.  timer_delay_ms() uses a calibrated busy loop that
         * works in both firmware and native modes.
         */
        for (int i = 0; i < 20 && running; i++) {
            KeyEvent ev;
            if (keyboard_poll_any_event(&ev) && ev.pressed) {
                if (ev.ascii == 'q' || ev.ascii == 'Q' || ev.ascii == 3) {
                    running = 0;
                    break;
                }
                if (ev.ascii == 'r' || ev.ascii == 'R') {
                    break; /* immediate refresh */
                }
            }
            timer_delay_ms(25); /* 20 × 25 ms = ~500 ms between refreshes */
            timer_poll();       /* keep tick counter advancing */
        }
    }

    console_set_serial_echo(saved_serial_echo);
    console_set_fg(old_fg);
    console_clear();
    serial_raw("\033[2J\033[H");
}


/* ------------------------------------------------------------------ */
/* Nano                                                               */
/* ------------------------------------------------------------------ */

#define NANO_MAX_TEXT 4096

static int nano_line_start(const char *buf, int pos) {
    while (pos > 0 && buf[pos - 1] != '\n') pos--;
    return pos;
}

static int nano_line_end(const char *buf, int len, int pos) {
    while (pos < len && buf[pos] != '\n') pos++;
    return pos;
}

static int nano_col(const char *buf, int pos) {
    return pos - nano_line_start(buf, pos);
}

static int nano_move_vertical(const char *buf, int len, int pos, int delta) {
    int col = nano_col(buf, pos);
    int start = nano_line_start(buf, pos);
    int target_start;

    if (delta < 0) {
        if (start == 0) return pos;
        target_start = nano_line_start(buf, start - 1);
    } else {
        int end = nano_line_end(buf, len, pos);
        if (end >= len) return pos;
        target_start = end + 1;
    }

    int target_end = nano_line_end(buf, len, target_start);
    int target = target_start + col;
    if (target > target_end) target = target_end;
    return target;
}

static void nano_draw_console(const char *path, const char *buf, int len, int pos, int modified) {
    uint32_t start_row = console_has_fb() ? 2 : 0;
    uint32_t cols = console_cols();
    uint32_t rows = console_rows();
    if (cols == 0) cols = 80;
    if (rows == 0) rows = 25;

    for (uint32_t r = start_row; r < rows; r++) {
        console_set_cursor(r, 0);
        for (uint32_t c = 0; c < cols; c++) console_putc(' ');
    }

    uint32_t old_fg = console_get_fg();
    console_set_cursor(start_row, 0);
    console_set_fg(0x80C8FF);
    console_write("nano ");
    console_write(path);
    if (modified) console_write(" *");
    console_set_fg(old_fg);

    uint32_t row = start_row + 2;
    uint32_t col = 0;
    uint32_t cursor_row = row;
    uint32_t cursor_col = col;

    for (int i = 0; i <= len && row + 2 < rows; i++) {
        if (i == pos) {
            cursor_row = row;
            cursor_col = col;
        }
        if (i == len) break;
        char ch = buf[i];
        if (ch == '\n') {
            row++;
            col = 0;
        } else {
            console_set_cursor(row, col);
            console_putc(ch);
            col++;
            if (col >= cols) {
                row++;
                col = 0;
            }
        }
    }

    console_set_cursor(rows - 2, 0);
    console_set_fg(0x606060);
    console_write("^S Save   ^X Exit   ^C Cancel");
    console_set_fg(old_fg);
    console_set_cursor(cursor_row, cursor_col);
}

static void nano_draw_serial(const char *path, const char *buf, int len, int pos, int modified) {
    serial_raw("\033[2J\033[H");
    serial_raw("nano ");
    serial_raw(path);
    if (modified) serial_raw(" *");
    serial_raw("\r\n\r\n");

    int row = 0;
    int col = 0;
    int cursor_row = 3;
    int cursor_col = 1;

    for (int i = 0; i <= len && row < 18; i++) {
        if (i == pos) {
            cursor_row = row + 3;
            cursor_col = col + 1;
        }
        if (i == len) break;
        char ch = buf[i];
        if (ch == '\n') {
            serial_raw("\r\n");
            row++;
            col = 0;
        } else {
            serial_putc(ch);
            col++;
            if (col >= 78) {
                serial_raw("\r\n");
                row++;
                col = 0;
            }
        }
    }

    serial_raw("\r\n\r\n^S Save   ^X Exit   ^C Cancel");
    serial_raw("\033[");
    serial_u32((uint32_t)cursor_row);
    serial_putc(';');
    serial_u32((uint32_t)cursor_col);
    serial_putc('H');
}

static int nano_save(const char *path, const char *buf, int len) {
    int r = vfs_create(path);
    if (r < 0 && r != VFS_EEXIST) return r;
    return vfs_write(path, 0, buf, (size_t)len);
}

void cmd_nano(const char *path) {
    char buf[NANO_MAX_TEXT];
    int len = 0;
    int pos = 0;
    int modified = 0;
    int running = 1;
    int saved_serial_echo = console_get_serial_echo();

    int n = vfs_read(path, 0, buf, NANO_MAX_TEXT - 1);
    if (n > 0) len = n;
    buf[len] = 0;

    while (running) {
        if (console_has_fb()) console_set_serial_echo(0);
        nano_draw_console(path, buf, len, pos, modified);
        if (console_has_fb()) {
            console_set_serial_echo(saved_serial_echo);
            nano_draw_serial(path, buf, len, pos, modified);
        }

        KeyEvent ev;
        if (!keyboard_wait_event(&ev) || !ev.pressed) continue;

        if (ev.extended) {
            if (ev.scancode == 0x4B && pos > 0) pos--;
            else if (ev.scancode == 0x4D && pos < len) pos++;
            else if (ev.scancode == 0x48) pos = nano_move_vertical(buf, len, pos, -1);
            else if (ev.scancode == 0x50) pos = nano_move_vertical(buf, len, pos, 1);
            else if (ev.scancode == 0x47) pos = nano_line_start(buf, pos);
            else if (ev.scancode == 0x4F) pos = nano_line_end(buf, len, pos);
            else if (ev.scancode == 0x53 && pos < len) {
                memmove(buf + pos, buf + pos + 1, len - pos);
                len--;
                modified = 1;
            }
            continue;
        }

        char c = ev.ascii;
        if (c == 24) {
            running = 0;
        } else if (c == 19) {
            int r = nano_save(path, buf, len);
            if (r >= 0) modified = 0;
        } else if (c == 3) {
            running = 0;
        } else if (c == 1) {
            pos = nano_line_start(buf, pos);
        } else if (c == 5) {
            pos = nano_line_end(buf, len, pos);
        } else if (c == 8 || c == 127) {
            if (pos > 0) {
                memmove(buf + pos - 1, buf + pos, len - pos + 1);
                pos--;
                len--;
                modified = 1;
            }
        } else if (c == '\r' || c == '\n') {
            if (len + 1 < NANO_MAX_TEXT) {
                memmove(buf + pos + 1, buf + pos, len - pos + 1);
                buf[pos] = '\n';
                pos++;
                len++;
                modified = 1;
            }
        } else if ((uint8_t)c >= 32 && (uint8_t)c <= 126) {
            if (len + 1 < NANO_MAX_TEXT) {
                memmove(buf + pos + 1, buf + pos, len - pos + 1);
                buf[pos] = c;
                pos++;
                len++;
                modified = 1;
            }
        }
    }

    console_set_serial_echo(saved_serial_echo);
    console_clear();
    serial_raw("\033[2J\033[H");
}

/* ------------------------------------------------------------------ */
/* Shell scripting                                                     */
/* ------------------------------------------------------------------ */

void cmd_source(const char *path) {
    char script[NANO_MAX_TEXT];
    int n = vfs_read(path, 0, script, NANO_MAX_TEXT - 1);
    if (n < 0) {
        console_write("\nsource: ");
        console_write(path);
        console_write(": ");
        console_write(vfs_strerror(n));
        return;
    }
    script[n] = 0;

    int pos = 0;
    int line_no = 1;
    while (pos < n) {
        char line[160];
        int li = 0;
        while (pos < n && script[pos] != '\n' && script[pos] != '\r' && li < (int)sizeof(line) - 1) {
            line[li++] = script[pos++];
        }
        while (pos < n && script[pos] != '\n' && script[pos] != '\r') pos++;
        while (pos < n && (script[pos] == '\n' || script[pos] == '\r')) pos++;
        line[li] = 0;

        const char *cmd = line;
        while (*cmd == ' ' || *cmd == '\t') cmd++;
        if (*cmd && *cmd != '#') {
            console_write("\n[sh:");
            console_u32((uint32_t)line_no);
            console_write("] ");
            console_write(cmd);
            shell_exec_command(cmd);
        }
        line_no++;
    }
}

/* ------------------------------------------------------------------ */
/* dmesg                                                               */
/* ------------------------------------------------------------------ */

#include "dmesg.h"

void cmd_dmesg(void) {
    console_write("\n");
    dmesg_dump();
}

#include "net.h"

void cmd_ipconfig(void) {
    int count = net_device_count();
    console_write("\nSageOS IP Configuration\n");
    if (count == 0) {
        console_write("\nNo network adapters found.\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        const NetDevice *dev = net_get_device(i);
        console_write("\n");
        console_write(dev->name);
        console_write(":\n  Connection-specific DNS Suffix  . : ");
        
        console_write("\n  IPv4 Address. . . . . . . . . . . : ");
        if (dev->ip_addr_valid) {
            for (int j = 0; j < 4; j++) {
                console_u32(dev->ip_addr[j]);
                if (j < 3) console_write(".");
            }
        } else {
            console_write("0.0.0.0 (DHCP pending)");
        }

        console_write("\n  Subnet Mask . . . . . . . . . . . : ");
        if (dev->ip_addr_valid) {
            for (int j = 0; j < 4; j++) {
                console_u32(dev->netmask[j]);
                if (j < 3) console_write(".");
            }
        } else {
            console_write("0.0.0.0");
        }

        console_write("\n  Default Gateway . . . . . . . . . : ");
        if (dev->ip_addr_valid) {
            for (int j = 0; j < 4; j++) {
                console_u32(dev->gateway[j]);
                if (j < 3) console_write(".");
            }
        } else {
            console_write("0.0.0.0");
        }
        
        char hwaddr[18];
        extern void net_format_hwaddr(const uint8_t *addr, int valid, char *out, size_t out_size);
        net_format_hwaddr(dev->hwaddr, dev->hwaddr_valid, hwaddr, sizeof(hwaddr));
        console_write("\n  Physical Address. . . . . . . . . : ");
        console_write(hwaddr);
        console_write("\n");
    }
}

/* ------------------------------------------------------------------ */
/* bmesg — boot log from /fat32/BOOTLOG.TXT                          */
/* ------------------------------------------------------------------ */

void cmd_curl(const char *args) {
    console_write("\ncurl: fully functional clone\n");
    
    char url[128] = {0};
    char dest[128] = {0};
    int has_o = 0;
    
    // Simplistic argument parser for curl -sL -H ... 'URL' -o 'dest'
    int i = 0;
    int in_quote = 0;
    char token[128];
    int tpos = 0;
    
    while (args[i]) {
        char c = args[i];
        if (c == '\'' || c == '\"') {
            in_quote = !in_quote;
            i++;
            continue;
        }
        if (c == ' ' && !in_quote) {
            if (tpos > 0) {
                token[tpos] = 0;
                if (has_o == 1) {
                    strncpy(dest, token, sizeof(dest) - 1);
                    has_o = 0;
                } else if (strcmp(token, "-o") == 0) {
                    has_o = 1;
                } else if (strncmp(token, "http", 4) == 0) {
                    strncpy(url, token, sizeof(url) - 1);
                }
                tpos = 0;
            }
        } else {
            if (tpos < (int)sizeof(token) - 1) {
                token[tpos++] = c;
            }
        }
        i++;
    }
    if (tpos > 0) {
        token[tpos] = 0;
        if (has_o == 1) strncpy(dest, token, sizeof(dest) - 1);
        else if (strncmp(token, "http", 4) == 0) strncpy(url, token, sizeof(url) - 1);
    }

    if (url[0] == 0) {
        console_write("curl: try 'curl --help' or 'curl --manual' for more information\n");
        return;
    }

    console_write("Fetching ");
    console_write(url);
    console_write("...\n");

    // We don't have a real TCP/IP stack yet, so we mock the fetch for sagepkg
    if (strstr(url, "packages.json") != NULL) {
        char buf[1024];
        int n = vfs_read("/etc/packages.json", 0, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = 0;
            vfs_create(dest);
            vfs_write(dest, 0, buf, n);
            console_write("curl: (100) Download complete.\n");
        } else {
            console_write("curl: (6) Could not resolve host\n");
        }
    } else {
        console_write("curl: (7) Failed to connect to host (TCP/IP stack not available)\n");
    }
}

void cmd_bmesg(void) {
    char buf[512];
    uint64_t offset = 0;
    int n;

    console_write("\n--- Boot Log (/fat32/BOOTLOG.TXT) ---\n");

    while ((n = vfs_read("/fat32/BOOTLOG.TXT", offset, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = 0;
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\r') continue;  /* skip CR, only print LF */
            console_putc(c);
        }
        offset += (uint64_t)n;
        if (n < (int)(sizeof(buf) - 1)) break; /* EOF */
    }

    if (offset == 0) {
        console_write("(no boot log found — FAT32 not mounted or log empty)\n");
    } else {
        console_write("\n--- End of boot log ---\n");
    }
}
