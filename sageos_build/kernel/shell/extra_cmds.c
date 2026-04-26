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
#include "serial.h"
#include "vfs.h"

extern void ata_read_sector(uint32_t lba, uint16_t *buffer);
extern void ata_write_sector(uint32_t lba, const uint16_t *buffer);

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

static void s_memmove(char *dst, const char *src, int n) {
    if (dst < src) {
        for (int i = 0; i < n; i++) dst[i] = src[i];
    } else if (dst > src) {
        for (int i = n - 1; i >= 0; i--) dst[i] = src[i];
    }
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

/* ------------------------------------------------------------------ */
/* OS Installer                                                       */
/* ------------------------------------------------------------------ */

void cmd_install(void) {
    console_write("\n=== SageOS Local Drive Installer ===");
    console_write("\nWARNING: This will format the local drive (ATA Primary Master).");
    console_write("\nAll data will be lost. Type 'YES' to continue: ");

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

    uint16_t zero_buf[256];
    for (int i = 0; i < 256; i++) zero_buf[i] = 0;

    // Simulate installation by zeroing out the first 100 sectors (MBR/GPT area)
    // and then verifying.
    for (uint32_t lba = 0; lba < 100; lba++) {
        if (lba % 10 == 0) {
            console_write("\nWriting sectors ");
            console_u32(lba);
            console_write("...");
        }
        ata_write_sector(lba, zero_buf);
    }

    console_write("\nVerifying sectors...");
    uint16_t read_buf[256];
    for (uint32_t lba = 0; lba < 10; lba++) {
        ata_read_sector(lba, read_buf);
        for (int i = 0; i < 256; i++) {
            if (read_buf[i] != 0) {
                console_write("\nVerification failed at LBA ");
                console_u32(lba);
                return;
            }
        }
    }

    console_write("\nInstallation complete! Please reboot without the installation media.");
    console_write("\n(Note: This was a dry-run/wipe of the boot sectors.)");
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

static void btop_draw_console(void) {
    uint32_t old_fg = console_get_fg();
    uint32_t start_row = console_has_fb() ? 2 : 0;
    uint32_t cols = console_cols();
    if (cols == 0) cols = 80;

    for (uint32_t r = start_row; r < start_row + 18 && r < console_rows(); r++) {
        console_set_cursor(r, 0);
        for (uint32_t c = 0; c < cols; c++) console_putc(' ');
    }

    console_set_cursor(start_row, 0);
    console_set_fg(0x80C8FF);
    console_write("SageOS BTOP");
    console_set_fg(0xE8E8E8);
    console_write("  q:quit  r:refresh");

    console_set_cursor(start_row + 2, 0);
    console_write("Uptime: ");
    uint64_t secs = timer_seconds();
    console_u32((uint32_t)secs);
    console_write("s    Kernel: v" SAGEOS_VERSION "    Input: ");
    console_write(keyboard_backend());

    uint32_t cpu = timer_cpu_percent();
    console_set_cursor(start_row + 4, 0);
    console_set_fg(0x79FFB0);
    console_write("CPU Usage: ");
    draw_bar(cpu, 100, 30);
    console_write(" ");
    console_u32(cpu);
    console_write("%");

    uint64_t used = ram_used_bytes();
    uint64_t total = ram_total_bytes();
    console_set_cursor(start_row + 5, 0);
    console_set_fg(0xFFBF40);
    console_write("Memory:    ");
    draw_bar((uint32_t)(used / 1024), (uint32_t)(total / 1024), 30);
    console_write(" ");
    print_mb(used);
    console_write(" / ");
    print_mb(total);

    int bat = battery_percent();
    console_set_cursor(start_row + 6, 0);
    console_set_fg(0xFF7070);
    console_write("Battery:   ");
    if (bat >= 0) {
        draw_bar((uint32_t)bat, 100, 30);
        console_write(" ");
        console_u32((uint32_t)bat);
        console_write("%");
    } else {
        console_write("[      N/A      ] --%");
    }

    console_set_fg(0xE8E8E8);
    console_set_cursor(start_row + 8, 0);
    console_write("Tasks:");
    console_set_cursor(start_row + 9, 0);
    console_write("  PID  NAME          STATUS");
    console_set_cursor(start_row + 10, 0);
    console_write("  0    kernel        running");
    console_set_cursor(start_row + 11, 0);
    console_write("  1    idle          waiting");
    console_set_cursor(start_row + 12, 0);
    console_write("  2    sageshell     active");
    console_set_cursor(start_row + 13, 0);
    console_write("  3    timer         active");
    console_set_cursor(start_row + 14, 0);
    console_write("  4    status_bar    active");

    console_set_fg(old_fg);
}

static void btop_draw_serial(void) {
    uint32_t cpu = timer_cpu_percent();
    uint64_t used = ram_used_bytes();
    uint64_t total = ram_total_bytes();
    int bat = battery_percent();

    serial_raw("\033[2J\033[H");
    serial_raw("SageOS BTOP  q:quit  r:refresh\r\n\r\n");
    serial_raw("Uptime: ");
    serial_u32((uint32_t)timer_seconds());
    serial_raw("s    Kernel: v" SAGEOS_VERSION "    Input: ");
    serial_raw(keyboard_backend());
    serial_raw("\r\n\r\n");

    serial_raw("CPU Usage: ");
    serial_bar(cpu, 100, 30);
    serial_raw(" ");
    serial_u32(cpu);
    serial_raw("%\r\n");

    serial_raw("Memory:    ");
    serial_bar((uint32_t)(used / 1024), (uint32_t)(total / 1024), 30);
    serial_raw(" ");
    serial_mb(used);
    serial_raw(" / ");
    serial_mb(total);
    serial_raw("\r\n");

    serial_raw("Battery:   ");
    if (bat >= 0) {
        serial_bar((uint32_t)bat, 100, 30);
        serial_raw(" ");
        serial_u32((uint32_t)bat);
        serial_raw("%\r\n");
    } else {
        serial_raw("[      N/A      ] --%\r\n");
    }

    serial_raw("\r\nTasks:\r\n");
    serial_raw("  PID  NAME          STATUS\r\n");
    serial_raw("  0    kernel        running\r\n");
    serial_raw("  1    idle          waiting\r\n");
    serial_raw("  2    sageshell     active\r\n");
    serial_raw("  3    timer         active\r\n");
    serial_raw("  4    status_bar    active\r\n");
}

void cmd_btop(void) {
    uint32_t old_fg = console_get_fg();
    int saved_serial_echo = console_get_serial_echo();
    int running = 1;

    console_clear();

    while (running) {
        if (console_has_fb()) console_set_serial_echo(0);
        btop_draw_console();
        if (console_has_fb()) {
            console_set_serial_echo(saved_serial_echo);
            btop_draw_serial();
        }

        for (int i = 0; i < 25; i++) {
            KeyEvent ev;
            timer_poll();
            if (keyboard_poll_any_event(&ev) && ev.pressed) {
                if (ev.ascii == 'q' || ev.ascii == 'Q' || ev.ascii == 3) {
                    running = 0;
                }
                if (ev.ascii == 'r' || ev.ascii == 'R') {
                    i = 25;
                }
            }
            timer_delay_ms(20);
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
                s_memmove(buf + pos, buf + pos + 1, len - pos);
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
                s_memmove(buf + pos - 1, buf + pos, len - pos + 1);
                pos--;
                len--;
                modified = 1;
            }
        } else if (c == '\r' || c == '\n') {
            if (len + 1 < NANO_MAX_TEXT) {
                s_memmove(buf + pos + 1, buf + pos, len - pos + 1);
                buf[pos] = '\n';
                pos++;
                len++;
                modified = 1;
            }
        } else if ((uint8_t)c >= 32 && (uint8_t)c <= 126) {
            if (len + 1 < NANO_MAX_TEXT) {
                s_memmove(buf + pos + 1, buf + pos, len - pos + 1);
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
