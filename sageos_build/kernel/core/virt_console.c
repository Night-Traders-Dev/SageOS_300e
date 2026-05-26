#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "serial.h"

#if defined(__x86_64__)
static inline void outb(uint16_t port, uint8_t data) {
    __asm__ volatile ("outb %0, %1" :: "a"(data), "Nd"(port));
}
#endif

static int serial_echo = 1;

void __attribute__((weak)) serial_init(void) {
    // UART already initialized by boot.S
}

void console_init(SageOSBootInfo *info) {
    (void)info;
    serial_init();
}

void console_clear(void) {
    console_write("\033[2J\033[H");
}

void console_putc(char c) {
    if (c == '\n') serial_putc('\r');
    serial_putc(c);
}

void serial_putc(char c) {
#if defined(__x86_64__)
    outb(0x3F8, (uint8_t)c);
#elif defined(__aarch64__)
    volatile uint8_t *uart = (volatile uint8_t *)0x09000000;
    uart[0] = (uint8_t)c;
#else // __riscv
    volatile uint8_t *uart = (volatile uint8_t *)0x10000000;
    uart[0] = (uint8_t)c;
#endif
}

void console_write(const char *s) {
    while (*s) console_putc(*s++);
}

void console_write_n(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) console_putc(s[i]);
}

void console_hex64(uint64_t v) {
    static const char *hex = "0123456789ABCDEF";
    console_write("0x");
    for (int i = 0; i < 16; i++) {
        serial_putc(hex[(v >> ((15 - i) * 4)) & 0xF]);
    }
}

void console_u32(uint32_t v) {
    if (v == 0) {
        serial_putc('0');
        return;
    }
    char buf[10];
    int i = 0;
    while (v) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0) serial_putc(buf[--i]);
}

void     console_set_fg(uint32_t rgb) { (void)rgb; }
uint32_t console_get_fg(void) { return 0xFFFFFF; }
void     console_set_bg(uint32_t rgb) { (void)rgb; }
uint32_t console_get_bg(void) { return 0x000000; }
void     console_set_inverted(int inverted) { (void)inverted; }
void     console_set_cursor(uint32_t row, uint32_t col) { (void)row; (void)col; }
void     console_get_cursor(uint32_t *row, uint32_t *col) { if (row) *row = 0; if (col) *col = 0; }
void     console_set_serial_echo(int enabled) { serial_echo = enabled; }
int      console_get_serial_echo(void) { return serial_echo; }
void     console_draw_status_bar(const char *right_text) { (void)right_text; }
void     console_serial_redraw_line(const char *line, uint32_t pos) {
    serial_putc('\r');
    serial_putc('>'); serial_putc(' ');
    console_write(line);
}
void     console_periodic_flip(void) {}
int      console_has_fb(void) { return 0; }
uint32_t console_cols(void) { return 80; }
uint32_t console_rows(void) { return 24; }
SageOSBootInfo *console_boot_info(void) { return NULL; }
