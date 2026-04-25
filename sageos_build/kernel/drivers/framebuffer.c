#include <stdint.h>
#include <stddef.h>
#include "bootinfo.h"
#include "console.h"
#include "serial.h"
#include "status.h"
#include "timer.h"
#include "version.h"

#define VGA_W 80
#define VGA_H 25
#define VGA_MEM ((volatile uint16_t*)0xB8000)

static SageOSBootInfo *g_info;
static int g_have_fb;

static uint32_t row;
static uint32_t col;
static uint32_t cols = VGA_W;
static uint32_t rows = VGA_H;

static uint32_t char_w = 12;
static uint32_t char_h = 16;
static uint32_t scale = 2;

static const uint32_t status_rows = 2;

static uint32_t fg = 0xE8E8E8;
static uint32_t bg = 0x05070A;

/*
 * serial_echo: mirrors console output to serial.
 * Shell redraws disable this to avoid confusing the terminal when
 * doing absolute cursor movement on the framebuffer; instead they
 * emit explicit VT100 sequences directly.
 */
static int serial_echo = 1;

/* ------------------------------------------------------------------ */
/* Low-level serial helpers for VT100 sequences                       */
/* ------------------------------------------------------------------ */

/* Write a raw string to serial unconditionally (bypasses echo flag). */
static void serial_raw(const char *s) {
    while (*s) serial_putc(*s++);
}

/*
 * serial_erase_in_line(n)
 *   n=0  Erase from cursor to end of line   ESC[0K
 *   n=1  Erase from start of line to cursor  ESC[1K
 *   n=2  Erase entire current line           ESC[2K
 */
static void serial_erase_in_line(int n) {
    char seq[8];
    seq[0] = '\033'; seq[1] = '['; seq[2] = (char)('0' + n); seq[3] = 'K'; seq[4] = 0;
    serial_raw(seq);
}

/*
 * serial_move_col(c)  Move cursor to column c (1-based) on serial.
 */
static void serial_move_col(uint32_t c) {
    /* ESC[{c}G */
    char seq[16];
    uint32_t c1 = c + 1; /* 1-based */
    int i = 0;
    seq[i++] = '\033'; seq[i++] = '[';
    /* print c1 in decimal */
    char tmp[8]; int ti = 0;
    uint32_t v = c1;
    if (v == 0) { tmp[ti++] = '0'; } else { while (v) { tmp[ti++] = (char)('0' + v % 10); v /= 10; } }
    /* reverse */
    for (int j = ti - 1; j >= 0; j--) seq[i++] = tmp[j];
    seq[i++] = 'G'; seq[i] = 0;
    serial_raw(seq);
}

SageOSBootInfo *console_boot_info(void) {
    return g_info;
}

int console_has_fb(void) {
    return g_have_fb;
}

void console_get_cursor(uint32_t *out_row, uint32_t *out_col) {
    if (out_row) *out_row = row;
    if (out_col) *out_col = col;
}

void console_set_cursor(uint32_t new_row, uint32_t new_col) {
    if (new_row >= rows) new_row = rows - 1;
    if (new_col >= cols) new_col = cols - 1;
    row = new_row;
    col = new_col;
}

uint32_t console_get_fg(void) { return fg; }
void console_set_fg(uint32_t rgb) { fg = rgb; }
int console_get_serial_echo(void) { return serial_echo; }
void console_set_serial_echo(int enabled) { serial_echo = enabled ? 1 : 0; }

static uint32_t pack_rgb(uint32_t rgb) {
    uint8_t r = (rgb >> 16) & 0xFF;
    uint8_t g_c = (rgb >> 8) & 0xFF;
    uint8_t b = rgb & 0xFF;
    if (g_info && g_info->pixel_format == 0)
        return ((uint32_t)r) | ((uint32_t)g_c << 8) | ((uint32_t)b << 16);
    return ((uint32_t)b) | ((uint32_t)g_c << 8) | ((uint32_t)r << 16);
}

static void my_memset32(void *dest, uint32_t val, size_t count) {
    uint32_t *p = (uint32_t *)dest;
    while (count--) *p++ = val;
}

static void my_memcpy32(void *dest, const void *src, size_t count) {
    uint32_t *d = (uint32_t *)dest;
    const uint32_t *s = (const uint32_t *)src;
    while (count--) *d++ = *s++;
}

static void fb_putpixel(uint32_t x, uint32_t y, uint32_t rgb) {
    if (!g_have_fb || !g_info) return;
    if (x >= g_info->width || y >= g_info->height) return;
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)g_info->framebuffer_base;
    fb[(uint64_t)y * g_info->pixels_per_scanline + x] = pack_rgb(rgb);
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb) {
    if (!g_have_fb || !g_info) return;
    uint32_t packed = pack_rgb(rgb);
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)g_info->framebuffer_base;
    uint32_t pitch = g_info->pixels_per_scanline;

    for (uint32_t yy = 0; yy < h; yy++) {
        my_memset32((void *)(fb + (uint64_t)(y + yy) * pitch + x), packed, w);
    }
}

static const uint8_t *glyph(char ch) {
    static const uint8_t SPACE[7] = {0,0,0,0,0,0,0};
    static const uint8_t UNKNOWN[7] = {14,17,1,2,4,0,4};

    switch (ch) {
        case ' ': return SPACE;
        case 'A': { static const uint8_t g[7]={14,17,17,31,17,17,17}; return g; }
        case 'B': { static const uint8_t g[7]={30,17,17,30,17,17,30}; return g; }
        case 'C': { static const uint8_t g[7]={14,17,16,16,16,17,14}; return g; }
        case 'D': { static const uint8_t g[7]={30,17,17,17,17,17,30}; return g; }
        case 'E': { static const uint8_t g[7]={31,16,16,30,16,16,31}; return g; }
        case 'F': { static const uint8_t g[7]={31,16,16,30,16,16,16}; return g; }
        case 'G': { static const uint8_t g[7]={14,17,16,23,17,17,15}; return g; }
        case 'H': { static const uint8_t g[7]={17,17,17,31,17,17,17}; return g; }
        case 'I': { static const uint8_t g[7]={14,4,4,4,4,4,14}; return g; }
        case 'J': { static const uint8_t g[7]={7,2,2,2,18,18,12}; return g; }
        case 'K': { static const uint8_t g[7]={17,18,20,24,20,18,17}; return g; }
        case 'L': { static const uint8_t g[7]={16,16,16,16,16,16,31}; return g; }
        case 'M': { static const uint8_t g[7]={17,27,21,21,17,17,17}; return g; }
        case 'N': { static const uint8_t g[7]={17,25,21,19,17,17,17}; return g; }
        case 'O': { static const uint8_t g[7]={14,17,17,17,17,17,14}; return g; }
        case 'P': { static const uint8_t g[7]={30,17,17,30,16,16,16}; return g; }
        case 'Q': { static const uint8_t g[7]={14,17,17,17,21,18,13}; return g; }
        case 'R': { static const uint8_t g[7]={30,17,17,30,20,18,17}; return g; }
        case 'S': { static const uint8_t g[7]={15,16,16,14,1,1,30}; return g; }
        case 'T': { static const uint8_t g[7]={31,4,4,4,4,4,4}; return g; }
        case 'U': { static const uint8_t g[7]={17,17,17,17,17,17,14}; return g; }
        case 'V': { static const uint8_t g[7]={17,17,17,17,17,10,4}; return g; }
        case 'W': { static const uint8_t g[7]={17,17,17,21,21,21,10}; return g; }
        case 'X': { static const uint8_t g[7]={17,17,10,4,10,17,17}; return g; }
        case 'Y': { static const uint8_t g[7]={17,17,10,4,4,4,4}; return g; }
        case 'Z': { static const uint8_t g[7]={31,1,2,4,8,16,31}; return g; }
        case 'a': { static const uint8_t g[7]={0,0,14,1,15,17,15}; return g; }
        case 'b': { static const uint8_t g[7]={16,16,22,25,17,17,30}; return g; }
        case 'c': { static const uint8_t g[7]={0,0,14,16,16,17,14}; return g; }
        case 'd': { static const uint8_t g[7]={1,1,13,19,17,17,15}; return g; }
        case 'e': { static const uint8_t g[7]={0,0,14,17,31,16,14}; return g; }
        case 'f': { static const uint8_t g[7]={6,9,8,28,8,8,8}; return g; }
        case 'g': { static const uint8_t g[7]={0,0,15,17,15,1,14}; return g; }
        case 'h': { static const uint8_t g[7]={16,16,22,25,17,17,17}; return g; }
        case 'i': { static const uint8_t g[7]={4,0,12,4,4,4,14}; return g; }
        case 'j': { static const uint8_t g[7]={2,0,6,2,2,18,12}; return g; }
        case 'k': { static const uint8_t g[7]={16,16,18,20,24,20,18}; return g; }
        case 'l': { static const uint8_t g[7]={12,4,4,4,4,4,14}; return g; }
        case 'm': { static const uint8_t g[7]={0,0,26,21,21,21,21}; return g; }
        case 'n': { static const uint8_t g[7]={0,0,22,25,17,17,17}; return g; }
        case 'o': { static const uint8_t g[7]={0,0,14,17,17,17,14}; return g; }
        case 'p': { static const uint8_t g[7]={0,0,30,17,30,16,16}; return g; }
        case 'q': { static const uint8_t g[7]={0,0,13,19,15,1,1}; return g; }
        case 'r': { static const uint8_t g[7]={0,0,22,25,16,16,16}; return g; }
        case 's': { static const uint8_t g[7]={0,0,15,16,14,1,30}; return g; }
        case 't': { static const uint8_t g[7]={8,8,28,8,8,9,6}; return g; }
        case 'u': { static const uint8_t g[7]={0,0,17,17,17,19,13}; return g; }
        case 'v': { static const uint8_t g[7]={0,0,17,17,17,10,4}; return g; }
        case 'w': { static const uint8_t g[7]={0,0,17,17,21,21,10}; return g; }
        case 'x': { static const uint8_t g[7]={0,0,17,10,4,10,17}; return g; }
        case 'y': { static const uint8_t g[7]={0,0,17,17,15,1,14}; return g; }
        case 'z': { static const uint8_t g[7]={0,0,31,2,4,8,31}; return g; }
        case '0': { static const uint8_t g[7]={14,17,19,21,25,17,14}; return g; }
        case '1': { static const uint8_t g[7]={4,12,4,4,4,4,14}; return g; }
        case '2': { static const uint8_t g[7]={14,17,1,2,4,8,31}; return g; }
        case '3': { static const uint8_t g[7]={30,1,1,14,1,1,30}; return g; }
        case '4': { static const uint8_t g[7]={2,6,10,18,31,2,2}; return g; }
        case '5': { static const uint8_t g[7]={31,16,16,30,1,1,30}; return g; }
        case '6': { static const uint8_t g[7]={14,16,16,30,17,17,14}; return g; }
        case '7': { static const uint8_t g[7]={31,1,2,4,8,8,8}; return g; }
        case '8': { static const uint8_t g[7]={14,17,17,14,17,17,14}; return g; }
        case '9': { static const uint8_t g[7]={14,17,17,15,1,1,14}; return g; }
        case '.': { static const uint8_t g[7]={0,0,0,0,0,12,12}; return g; }
        case ',': { static const uint8_t g[7]={0,0,0,0,0,12,8}; return g; }
        case ':': { static const uint8_t g[7]={0,12,12,0,12,12,0}; return g; }
        case ';': { static const uint8_t g[7]={0,12,12,0,12,8,16}; return g; }
        case '-': { static const uint8_t g[7]={0,0,0,31,0,0,0}; return g; }
        case '_': { static const uint8_t g[7]={0,0,0,0,0,0,31}; return g; }
        case '/': { static const uint8_t g[7]={1,2,2,4,8,8,16}; return g; }
        case '\\': { static const uint8_t g[7]={16,8,8,4,2,2,1}; return g; }
        case '#': { static const uint8_t g[7]={10,10,31,10,31,10,10}; return g; }
        case '@': { static const uint8_t g[7]={14,17,23,21,23,16,14}; return g; }
        case '=': { static const uint8_t g[7]={0,0,31,0,31,0,0}; return g; }
        case '+': { static const uint8_t g[7]={0,4,4,31,4,4,0}; return g; }
        case '*': { static const uint8_t g[7]={0,21,14,31,14,21,0}; return g; }
        case '\'': { static const uint8_t g[7]={4,4,8,0,0,0,0}; return g; }
        case '"': { static const uint8_t g[7]={10,10,0,0,0,0,0}; return g; }
        case '!': { static const uint8_t g[7]={4,4,4,4,4,0,4}; return g; }
        case '%': { static const uint8_t g[7]={24,25,2,4,8,19,3}; return g; }
        case '?': return UNKNOWN;
        case '[': { static const uint8_t g[7]={14,8,8,8,8,8,14}; return g; }
        case ']': { static const uint8_t g[7]={14,2,2,2,2,2,14}; return g; }
        case '(': { static const uint8_t g[7]={2,4,8,8,8,4,2}; return g; }
        case ')': { static const uint8_t g[7]={8,4,2,2,2,4,8}; return g; }
        case '<': { static const uint8_t g[7]={2,4,8,16,8,4,2}; return g; }
        case '>': { static const uint8_t g[7]={8,4,2,1,2,4,8}; return g; }
        default: return UNKNOWN;
    }
}

static void draw_cell(uint32_t cx, uint32_t cy, char ch) {
    uint32_t px = cx * char_w;
    uint32_t py = cy * char_h;
    fill_rect(px, py, char_w, char_h, bg);
    const uint8_t *g = glyph(ch);
    for (uint32_t gy = 0; gy < 7; gy++)
        for (uint32_t gx = 0; gx < 5; gx++)
            if (g[gy] & (1U << (4 - gx)))
                fill_rect(px + gx * scale + scale, py + gy * scale + scale, scale, scale, fg);
}

static void scroll(void) {
    if (!g_have_fb || !g_info) return;
    volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)g_info->framebuffer_base;
    uint32_t pitch = g_info->pixels_per_scanline;
    uint32_t h = g_info->height;
    uint32_t w = g_info->width;
    uint32_t status_height = status_rows * char_h;

    for (uint32_t y = status_height + char_h; y < h; y++) {
        my_memcpy32((void *)(fb + (uint64_t)(y - char_h) * pitch),
                    (void *)(fb + (uint64_t)y * pitch),
                    w);
    }

    uint32_t bg_packed = pack_rgb(bg);
    for (uint32_t y = h - char_h; y < h; y++) {
        my_memset32((void *)(fb + (uint64_t)y * pitch), bg_packed, w);
    }

    if (row > status_rows) row--;
}

void console_clear(void) {
    row = 0;
    col = 0;

    if (g_have_fb && g_info) {
        uint32_t status_height = status_rows * char_h;
        uint32_t bg_packed = pack_rgb(bg);
        volatile uint32_t *fb = (volatile uint32_t *)(uintptr_t)g_info->framebuffer_base;
        uint32_t pitch = g_info->pixels_per_scanline;

        for (uint32_t y = status_height; y < g_info->height; y++) {
            my_memset32((void *)(fb + (uint64_t)y * pitch), bg_packed, g_info->width);
        }
    } else {
        // For VGA, clear from below status_rows
        for (size_t y = status_rows; y < VGA_H; y++)
            for (size_t x = 0; x < VGA_W; x++)
                VGA_MEM[y * VGA_W + x] = ((uint16_t)0x0F << 8) | ' ';
    }

    if (g_have_fb) {
        row = status_rows;
        col = 0;
    }

    /*
     * On serial (QEMU -nographic), the serial port IS the display.
     * Emit ANSI erase-screen + cursor-home so the terminal clears.
     * This is unconditional: harmless on real hardware (serial
     * console logs it), and essential in QEMU.
     */
    serial_raw("\033[2J\033[H");
}

void console_init(SageOSBootInfo *info) {
    g_info = info;
    g_have_fb =
        info &&
        info->magic == SAGEOS_BOOT_MAGIC &&
        info->framebuffer_base &&
        info->width >= 320 &&
        info->height >= 200 &&
        info->pixels_per_scanline >= info->width;

    if (g_have_fb) {
        scale = 2;
        char_w = 6 * scale;
        char_h = 8 * scale;
        cols = info->width / char_w;
        rows = info->height / char_h;
        if (!cols) cols = 1;
        if (!rows) rows = 1;
    } else {
        cols = VGA_W;
        rows = VGA_H;
    }

    console_clear();
}

void console_putc(char c) {
    if (g_have_fb) status_tick_poll();

    if (c == '\r') {
        if (serial_echo) serial_putc('\r');
        col = 0;
        return;
    }

    if (c == '\n' || c == 10) {
        if (serial_echo) { serial_putc('\r'); serial_putc('\n'); }
        col = 0;
        row++;
        if (row >= rows) {
            if (g_have_fb) scroll();
            else row = rows - 1;
        }
        return;
    }

    /*
     * Backspace handling.
     *
     * Framebuffer: move col left, erase the cell by drawing a space.
     * Serial:      emit "\b \b" (backspace, space, backspace) which
     *              is the standard VT100 destructive backspace sequence.
     *              This erases the character visually on any terminal.
     *
     * Note: shell_redraw_line disables serial_echo while doing
     * full-line redraws, so this path is only hit for single
     * character backspaces typed directly (not mid-line edits).
     */
    if (c == 8 || c == 127) {
        if (col > 0) {
            col--;
            if (g_have_fb)
                draw_cell(col, row, ' ');
            if (serial_echo)
                serial_raw("\b \b");
        }
        return;
    }

    if (serial_echo) serial_putc(c);

    if (g_have_fb)
        draw_cell(col, row, c);
    else
        VGA_MEM[row * VGA_W + col] = ((uint16_t)0x0F << 8) | (uint8_t)c;

    col++;
    if (col >= cols) {
        col = 0;
        row++;
        if (row >= rows) {
            if (g_have_fb) scroll();
            else row = rows - 1;
        }
    }
}

/*
 * console_serial_redraw_line
 *
 * Called by shell_redraw_line when the framebuffer is active and
 * serial_echo has been suppressed during the FB redraw.  This
 * function emits the equivalent VT100 sequence to keep the serial
 * terminal in sync:
 *
 *   ESC[G        move to column 1 (start of line)
 *   ESC[0K       erase from cursor to end of line
 *   <line text>  re-print the current line content
 *   ESC[{c+1}G   move cursor to the correct position
 *
 * Only called when g_have_fb is true (serial mirrors the FB).
 * When running serial-only (no FB), serial_echo stays on and the
 * normal putc path handles everything.
 */
void console_serial_redraw_line(const char *line, uint32_t pos) {
    serial_putc('\r');
    serial_erase_in_line(0);   /* erase to end of line */
    /* reprint the prompt prefix: shell already printed it, we just
       reprint from column 0 which on serial means re-emit the full
       visible line (prompt is gone after \r, so we only have 'line'). */
    const char *p = line;
    while (*p) serial_putc(*p++);
    /* position the cursor */
    serial_move_col(pos);
}

void console_write(const char *s) {
    while (*s) console_putc(*s++);
}

void console_write_n(const char *s, size_t n) {
    for (size_t i = 0; i < n && s[i]; i++) console_putc(s[i]);
}

void console_hex64(uint64_t v) {
    static const char *hex = "0123456789ABCDEF";
    char out[19];
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 16; i++)
        out[2 + i] = hex[(v >> ((15 - i) * 4)) & 0xF];
    out[18] = 0;
    console_write(out);
}

void console_u32(uint32_t v) {
    char buf[16];
    int i = 0;
    if (v == 0) { console_putc('0'); return; }
    while (v > 0 && i < 15) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) console_putc(buf[--i]);
}

uint32_t console_cols(void) { return cols; }
uint32_t console_rows(void) { return rows; }

static uint32_t str_len32(const char *s) {
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

static char status_shadow[256];

void console_draw_status_bar(const char *right_text) {
    if (!g_have_fb || !g_info || cols == 0) return;

    uint32_t saved_row = row;
    uint32_t saved_col = col;
    uint32_t saved_fg = fg;
    uint32_t saved_bg = bg;

    fg = 0x05070A;
    bg = 0x79FFB0;

    char current[256];
    for (uint32_t i = 0; i < 255; i++) current[i] = ' ';
    current[255] = 0;
    if (cols < 255) current[cols] = 0;

    const char *left = " SageOS v" SAGEOS_VERSION " ";
    for (uint32_t i = 0; left[i] && i < 255 && i < cols; i++)
        current[i] = left[i];

    uint32_t len = str_len32(right_text);
    uint32_t start = 0;
    if (len + 1 < cols && len < 255) {
        start = cols - len - 1;
        for (uint32_t i = 0; right_text[i] && start + i < 255 && start + i < cols; i++)
            current[start + i] = right_text[i];
    }

    for (uint32_t i = 0; i < cols && i < 255; i++) {
        if (current[i] != status_shadow[i]) {
            draw_cell(i, 0, current[i]);
            status_shadow[i] = current[i];
        }
    }

    fg = saved_fg;
    bg = saved_bg;
    row = saved_row;
    col = saved_col;
    if (row < status_rows) row = status_rows;
}
