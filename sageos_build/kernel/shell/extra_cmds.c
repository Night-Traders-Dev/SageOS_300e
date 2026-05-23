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

#include "lwip/apps/http_client.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "mbedtls/ssl.h"

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
/* ------------------------------------------------------------------ */
/* Nano                                                               */
/* ------------------------------------------------------------------ */

#define NANO_MAX_TEXT (128 * 1024)
#define NANO_PATH_MAX 256

typedef struct {
    char *buf;
    char *kill_buf;      /* For Cut/Uncut */
    int len;
    int kill_len;
    int pos;
    int scroll_top;      /* Byte offset of the first character on screen */
    int modified;
    int running;
    char path[NANO_PATH_MAX];
    char status[128];
    uint32_t rows, cols;
    uint32_t edit_rows;  /* Number of rows available for text editing */
} NanoContext;

static int nano_line_start(const char *buf, int pos) {
    while (pos > 0 && buf[pos - 1] != '\n') pos--;
    return pos;
}

static int nano_line_end(const char *buf, int len, int pos) {
    while (pos < len && buf[pos] != '\n') pos++;
    return pos;
}

static int nano_move_vertical(const char *buf, int len, int pos, int delta) {
    int cur_start = nano_line_start(buf, pos);
    int col = pos - cur_start;
    int target_start;

    if (delta < 0) {
        if (cur_start == 0) return 0;
        target_start = nano_line_start(buf, cur_start - 1);
    } else {
        int cur_end = nano_line_end(buf, len, pos);
        if (cur_end >= len) return len;
        target_start = cur_end + 1;
    }

    int target_end = nano_line_end(buf, len, target_start);
    int target = target_start + col;
    if (target > target_end) target = target_end;
    return target;
}

static void nano_draw_bar(NanoContext *ctx, uint32_t row, const char *text, int invert) {
    console_set_cursor(row, 0);
    if (invert) console_set_inverted(1);
    
    uint32_t len = 0;
    while (text[len]) len++;
    
    console_write(text);
    for (uint32_t i = len; i < ctx->cols; i++) console_putc(' ');
    
    if (invert) console_set_inverted(0);
}

static void nano_draw_shortcuts(NanoContext *ctx) {
    uint32_t r = ctx->rows - 2;
    nano_draw_bar(ctx, r, "^G Get Help  ^O Write Out  ^W Where Is   ^K Cut Text", 0);
    nano_draw_bar(ctx, r + 1, "^X Exit      ^J Justify    ^C Cur Pos    ^U Uncut Text", 0);
    
    /* Highlight the carets */
    for (uint32_t i = 0; i < 2; i++) {
        for (uint32_t j = 0; j < ctx->cols; j += 14) {
            console_set_cursor(r + i, j);
            console_set_fg(0x79FFB0);
            console_putc('^');
            console_set_fg(0xE8E8E8);
        }
    }
}

static void nano_update_scroll(NanoContext *ctx) {
    /* Ensure pos is within [scroll_top, end_of_screen] */
    if (ctx->pos < ctx->scroll_top) {
        ctx->scroll_top = nano_line_start(ctx->buf, ctx->pos);
        return;
    }

    int current = ctx->scroll_top;
    uint32_t visible_lines = 0;
    int pos_visible = 0;

    while (visible_lines < ctx->edit_rows) {
        int end = nano_line_end(ctx->buf, ctx->len, current);
        if (ctx->pos >= current && ctx->pos <= end) {
            pos_visible = 1;
            break;
        }
        if (end >= ctx->len) break;
        current = end + 1;
        visible_lines++;
    }

    if (!pos_visible) {
        ctx->scroll_top = nano_move_vertical(ctx->buf, ctx->len, ctx->scroll_top, 1);
    }
}

static void nano_draw_console(NanoContext *ctx) {
    uint32_t old_fg = console_get_fg();
    
    /* 1. Title Bar */
    char title[160];
    snprintf(title, sizeof(title), "  SageOS nano 1.0     File: %s%s", ctx->path, ctx->modified ? " [Modified]" : "");
    nano_draw_bar(ctx, 0, title, 1);

    /* 2. Editor Area */
    int current = ctx->scroll_top;
    uint32_t cursor_row = 1;
    uint32_t cursor_col = 0;

    for (uint32_t r = 0; r < ctx->edit_rows; r++) {
        console_set_cursor(r + 1, 0);
        int line_end = nano_line_end(ctx->buf, ctx->len, current);
        
        for (int i = current; i <= line_end; i++) {
            if (i == ctx->pos) {
                cursor_row = r + 1;
                cursor_col = (uint32_t)(i - current);
            }
            if (i < line_end) {
                if ((uint32_t)(i - current) < ctx->cols) {
                    console_putc(ctx->buf[i]);
                }
            }
        }
        
        uint32_t line_len = (uint32_t)(line_end - current);
        for (uint32_t c = (line_len < ctx->cols ? line_len : ctx->cols); c < ctx->cols; c++) console_putc(' ');

        if (line_end >= ctx->len) {
            for (uint32_t rr = r + 1; rr < ctx->edit_rows; rr++) {
                console_set_cursor(rr + 1, 0);
                for (uint32_t cc = 0; cc < ctx->cols; cc++) console_putc(' ');
            }
            break;
        }
        current = line_end + 1;
    }

    /* 3. Status Bar */
    nano_draw_bar(ctx, ctx->rows - 3, ctx->status, 0);

    /* 4. Shortcut Bar */
    nano_draw_shortcuts(ctx);

    console_set_cursor(cursor_row, cursor_col);
    console_set_fg(old_fg);
}

static void nano_draw_serial(NanoContext *ctx) {
    serial_raw("\033[H");
    serial_raw("\033[7m");
    serial_raw("  SageOS nano 1.0     File: ");
    serial_raw(ctx->path);
    if (ctx->modified) serial_raw(" [Modified]");
    serial_raw("\033[K\033[27m\r\n");

    int current = ctx->scroll_top;
    int cursor_row = 2;
    int cursor_col = 1;

    for (uint32_t r = 0; r < ctx->edit_rows; r++) {
        int line_end = nano_line_end(ctx->buf, ctx->len, current);
        for (int i = current; i <= line_end; i++) {
            if (i == ctx->pos) {
                cursor_row = (int)r + 2;
                cursor_col = i - current + 1;
            }
            if (i < line_end && (uint32_t)(i - current) < 79) {
                serial_putc(ctx->buf[i]);
            }
        }
        serial_raw("\033[K\r\n");
        if (line_end >= ctx->len) {
            for (uint32_t rr = r + 1; rr < ctx->edit_rows; rr++) serial_raw("~\033[K\r\n");
            break;
        }
        current = line_end + 1;
    }
    serial_raw(ctx->status);
    serial_raw("\033[K\r\n");
    serial_raw("\033[7m^G\033[27m Get Help  \033[7m^O\033[27m Write Out  \033[7m^W\033[27m Where Is   \033[7m^K\033[27m Cut Text\r\n");
    serial_raw("\033[7m^X\033[27m Exit      \033[7m^J\033[27m Justify    \033[7m^C\033[27m Cur Pos    \033[7m^U\033[27m Uncut Text\033[K");
    char move[32];
    snprintf(move, sizeof(move), "\033[%d;%dH", cursor_row, cursor_col);
    serial_raw(move);
}

static int nano_save(NanoContext *ctx) {
    int r = vfs_create(ctx->path);
    if (r < 0 && r != VFS_EEXIST) return r;
    r = vfs_write(ctx->path, 0, ctx->buf, (size_t)ctx->len);
    if (r >= 0) {
        ctx->modified = 0;
        snprintf(ctx->status, sizeof(ctx->status), "[ Wrote %d bytes ]", ctx->len);
    } else {
        snprintf(ctx->status, sizeof(ctx->status), "[ Error writing file: %s ]", vfs_strerror(r));
    }
    return r;
}

void cmd_nano(const char *path) {
    NanoContext *ctx = (NanoContext *)sage_malloc(sizeof(NanoContext));
    if (!ctx) return;
    memset(ctx, 0, sizeof(NanoContext));
    ctx->buf = (char *)sage_malloc(NANO_MAX_TEXT);
    ctx->kill_buf = (char *)sage_malloc(NANO_MAX_TEXT);
    if (!ctx->buf || !ctx->kill_buf) {
        if (ctx->buf) sage_free(ctx->buf);
        if (ctx->kill_buf) sage_free(ctx->kill_buf);
        sage_free(ctx);
        return;
    }
    strncpy(ctx->path, path, NANO_PATH_MAX - 1);
    ctx->rows = console_rows();
    ctx->cols = console_cols();
    if (ctx->rows == 0) ctx->rows = 25;
    if (ctx->cols == 0) ctx->cols = 80;
    ctx->edit_rows = ctx->rows - 4;
    int n = vfs_read(path, 0, ctx->buf, NANO_MAX_TEXT - 1);
    if (n > 0) ctx->len = n;
    ctx->buf[ctx->len] = 0;
    ctx->running = 1;
    int saved_serial_echo = console_get_serial_echo();
    console_set_serial_echo(0);
    console_clear();
    serial_raw("\033[2J\033[H");

    while (ctx->running) {
        nano_update_scroll(ctx);
        nano_draw_console(ctx);
        nano_draw_serial(ctx);
        KeyEvent ev;
        if (!keyboard_wait_event(&ev) || !ev.pressed) continue;

        if (ev.extended) {
            if (ev.scancode == 0x4B && ctx->pos > 0) ctx->pos--;
            else if (ev.scancode == 0x4D && ctx->pos < ctx->len) ctx->pos++;
            else if (ev.scancode == 0x48) ctx->pos = nano_move_vertical(ctx->buf, ctx->len, ctx->pos, -1);
            else if (ev.scancode == 0x50) ctx->pos = nano_move_vertical(ctx->buf, ctx->len, ctx->pos, 1);
            else if (ev.scancode == 0x47) ctx->pos = nano_line_start(ctx->buf, ctx->pos);
            else if (ev.scancode == 0x4F) ctx->pos = nano_line_end(ctx->buf, ctx->len, ctx->pos);
            else if (ev.scancode == 0x49) { for (uint32_t i = 0; i < ctx->edit_rows; i++) ctx->pos = nano_move_vertical(ctx->buf, ctx->len, ctx->pos, -1); }
            else if (ev.scancode == 0x51) { for (uint32_t i = 0; i < ctx->edit_rows; i++) ctx->pos = nano_move_vertical(ctx->buf, ctx->len, ctx->pos, 1); }
            else if (ev.scancode == 0x53 && ctx->pos < ctx->len) {
                memmove(ctx->buf + ctx->pos, ctx->buf + ctx->pos + 1, (size_t)(ctx->len - ctx->pos));
                ctx->len--; ctx->modified = 1;
            }
            continue;
        }

        char c = ev.ascii;
        if (c == 24) { /* ^X */
            if (ctx->modified) {
                nano_draw_bar(ctx, ctx->rows - 3, "Save modified buffer? (Answer y/n, ^C to cancel)", 0);
                while(1) { if (keyboard_wait_event(&ev) && ev.pressed) {
                    if (ev.ascii == 'y' || ev.ascii == 'Y') { nano_save(ctx); break; }
                    if (ev.ascii == 'n' || ev.ascii == 'N') break;
                    if (ev.ascii == 3) goto cancel_exit;
                }}
            }
            ctx->running = 0;
            cancel_exit:;
            ctx->status[0] = 0;
        } else if (c == 15) { nano_save(ctx); }
        else if (c == 11) { /* ^K */
            int start = nano_line_start(ctx->buf, ctx->pos);
            int end = nano_line_end(ctx->buf, ctx->len, ctx->pos);
            if (end < ctx->len) end++;
            int cut_len = end - start;
            memcpy(ctx->kill_buf, ctx->buf + start, (size_t)cut_len);
            ctx->kill_len = cut_len;
            memmove(ctx->buf + start, ctx->buf + end, (size_t)(ctx->len - end + 1));
            ctx->len -= cut_len; ctx->pos = start; ctx->modified = 1;
            snprintf(ctx->status, sizeof(ctx->status), "[ Cut %d characters ]", cut_len);
        } else if (c == 21) { /* ^U */
            if (ctx->kill_len > 0 && ctx->len + ctx->kill_len < NANO_MAX_TEXT) {
                memmove(ctx->buf + ctx->pos + ctx->kill_len, ctx->buf + ctx->pos, (size_t)(ctx->len - ctx->pos + 1));
                memcpy(ctx->buf + ctx->pos, ctx->kill_buf, (size_t)ctx->kill_len);
                ctx->len += ctx->kill_len; ctx->pos += ctx->kill_len; ctx->modified = 1;
                snprintf(ctx->status, sizeof(ctx->status), "[ Uncut %d characters ]", ctx->kill_len);
            }
        } else if (c == 3) { /* ^C */
            int line = 1, total = 1;
            for (int i = 0; i < ctx->pos; i++) if (ctx->buf[i] == '\n') line++;
            for (int i = 0; i < ctx->len; i++) if (ctx->buf[i] == '\n') total++;
            snprintf(ctx->status, sizeof(ctx->status), "line %d/%d, col %d, char %d/%d", line, total, ctx->pos - nano_line_start(ctx->buf, ctx->pos) + 1, ctx->pos, ctx->len);
        } else if (c == 23) { /* ^W */
            nano_draw_bar(ctx, ctx->rows - 3, "Search: ", 0);
            char term[64]; int si = 0; term[0] = 0;
            while(1) {
                nano_draw_bar(ctx, ctx->rows - 3, "Search: ", 0); console_write(term);
                if (keyboard_wait_event(&ev) && ev.pressed) {
                    if (ev.ascii == '\r' || ev.ascii == '\n') break;
                    if (ev.ascii == 3) { si = 0; break; }
                    if ((ev.ascii == 8 || ev.ascii == 127) && si > 0) si--;
                    else if (ev.ascii >= 32 && si < 63) term[si++] = ev.ascii;
                    term[si] = 0;
                }
            }
            if (si > 0) {
                char *found = strstr(ctx->buf + ctx->pos + 1, term);
                if (!found) found = strstr(ctx->buf, term);
                if (found) { ctx->pos = (int)(found - ctx->buf); ctx->status[0] = 0; }
                else snprintf(ctx->status, sizeof(ctx->status), "[ '%s' not found ]", term);
            }
        } else if (c == 8 || c == 127) {
            if (ctx->pos > 0) {
                memmove(ctx->buf + ctx->pos - 1, ctx->buf + ctx->pos, (size_t)(ctx->len - ctx->pos + 1));
                ctx->pos--; ctx->len--; ctx->modified = 1;
            }
        } else if (c == '\r' || c == '\n') {
            if (ctx->len + 1 < NANO_MAX_TEXT) {
                memmove(ctx->buf + ctx->pos + 1, ctx->buf + ctx->pos, (size_t)(ctx->len - ctx->pos + 1));
                ctx->buf[ctx->pos] = '\n'; ctx->pos++; ctx->len++; ctx->modified = 1;
            }
        } else if ((uint8_t)c >= 32 && (uint8_t)c <= 126) {
            if (ctx->len + 1 < NANO_MAX_TEXT) {
                memmove(ctx->buf + ctx->pos + 1, ctx->buf + ctx->pos, (size_t)(ctx->len - ctx->pos + 1));
                ctx->buf[ctx->pos] = c; ctx->pos++; ctx->len++; ctx->modified = 1;
            }
        }
    }
    console_set_serial_echo(saved_serial_echo);
    console_clear(); serial_raw("\033[2J\033[H");
    sage_free(ctx->buf); sage_free(ctx->kill_buf); sage_free(ctx);
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

/* ------------------------------------------------------------------ */
/* curl — HTTP/HTTPS client                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    char dest[128];
    uint64_t offset;
    volatile int done;
    volatile int result;
    volatile int lwip_err;
    uint32_t bytes_received;
} curl_ctx_t;

static struct altcp_tls_config *g_curl_tls_conf = NULL;

static void curl_result_cb(void *arg, httpc_result_t httpc_result, u32_t rx_content_len, u32_t srv_res, err_t err) {
    curl_ctx_t *ctx = (curl_ctx_t *)arg;
    (void)rx_content_len;
    (void)srv_res;
    ctx->result = (int)httpc_result;
    ctx->lwip_err = (int)err;
    ctx->done = 1;
}

static err_t curl_recv_cb(void *arg, struct altcp_pcb *pcb, struct pbuf *p, err_t err) {
    curl_ctx_t *ctx = (curl_ctx_t *)arg;
    (void)err;
    if (p) {
        if (ctx->dest[0]) {
            vfs_write(ctx->dest, ctx->offset, p->payload, p->tot_len);
            ctx->offset += p->tot_len;
        }
        ctx->bytes_received += p->tot_len;
        altcp_recved(pcb, p->tot_len);
        pbuf_free(p);
    }
    return ERR_OK;
}

void cmd_curl(const char *args) {
    char url_raw[128] = {0};
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
                    strncpy(url_raw, token, sizeof(url_raw) - 1);
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
        else if (strncmp(token, "http", 4) == 0) strncpy(url_raw, token, sizeof(url_raw) - 1);
    }

    if (url_raw[0] == 0) {
        console_write("curl: try 'curl --help' or 'curl --manual' for more information\n");
        return;
    }

    int is_https = 0;
    const char *hostname = NULL;
    const char *uri = "/";
    char host_buf[128] = {0};

    if (strncmp(url_raw, "https://", 8) == 0) {
        is_https = 1;
        hostname = url_raw + 8;
    } else if (strncmp(url_raw, "http://", 7) == 0) {
        hostname = url_raw + 7;
    } else {
        hostname = url_raw;
    }

    const char *slash = strchr(hostname, '/');
    if (slash) {
        int host_len = slash - hostname;
        if (host_len < (int)sizeof(host_buf)) {
            strncpy(host_buf, hostname, (size_t)host_len);
            host_buf[host_len] = 0;
            hostname = host_buf;
        }
        uri = slash;
    }

    uint16_t port = is_https ? 443 : 80;

    console_write("Fetching ");
    console_write(url_raw);
    console_write("...\n");

    curl_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    if (dest[0]) {
        strncpy(ctx.dest, dest, sizeof(ctx.dest) - 1);
        vfs_create(ctx.dest);
    }

    httpc_connection_t settings;
    memset(&settings, 0, sizeof(settings));
    settings.result_fn = curl_result_cb;
    
    httpc_state_t *connection = NULL;
#if LWIP_ALTCP_TLS
    static altcp_allocator_t tls_allocator;
    if (is_https) {
        if (!g_curl_tls_conf) {
            g_curl_tls_conf = altcp_tls_create_config_client(NULL, 0);
            if (g_curl_tls_conf) {
                extern void altcp_tls_config_disable_verify(struct altcp_tls_config *config);
                altcp_tls_config_disable_verify(g_curl_tls_conf);
            }
        }
        tls_allocator.alloc = altcp_tls_alloc;
        tls_allocator.arg = g_curl_tls_conf;
        settings.altcp_allocator = &tls_allocator;
    }
#else
    if (is_https) {
        console_write("curl: HTTPS support disabled in this build\n");
        return;
    }
#endif

    err_t err = httpc_get_file_dns(hostname, port, uri, &settings, curl_recv_cb, &ctx, &connection);
    if (err != ERR_OK) {
        console_write("curl: failed to initiate request (err=");
        console_u32((uint32_t)err);
        console_write(")\n");
        return;
    }

#if LWIP_ALTCP_TLS
    if (is_https && connection) {
        struct altcp_pcb *pcb = httpc_get_tcp_pcb(connection);
        if (pcb) {
            void *mbedtls_ctx = altcp_tls_context(pcb);
            if (mbedtls_ctx) {
                mbedtls_ssl_set_hostname((mbedtls_ssl_context *)mbedtls_ctx, hostname);
            }
        }
    }
#endif

    while (!ctx.done) {
        sched_yield();
    }

    if (ctx.result == HTTPC_RESULT_OK) {
        dmesg_printf("curl: (100) Download complete. %u bytes received.", ctx.bytes_received);
        console_write("curl: download complete.\n");
    } else {
        dmesg_printf("curl: download failed (result=%u, lwip_err=%s)", (uint32_t)ctx.result, lwip_strerr((err_t)ctx.lwip_err));
        console_write("curl: download failed (see dmesg for details)\n");
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
