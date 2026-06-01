#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "timer.h"
#include "dmesg.h"
#include "ata.h"

#define DMESG_SIZE (128 * 1024)

static char dmesg_buf[DMESG_SIZE];
static uint32_t dmesg_head = 0;
static uint32_t dmesg_total = 0;

static void append_char(char c) {
    dmesg_buf[dmesg_head] = c;
    dmesg_head = (dmesg_head + 1) % DMESG_SIZE;
    if (dmesg_total < DMESG_SIZE) dmesg_total++;
}

void dmesg_printf(const char *fmt, ...) {
    char buf[256];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    extern int sage_vsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap);
    sage_vsnprintf(buf, sizeof(buf), fmt, ap);
    __builtin_va_end(ap);
    dmesg_log(buf);
}

static int dmesg_at_bol = 1;

static void append_timestamp(void) {
    uint64_t centiseconds = timer_elapsed_centiseconds();
    uint64_t sec = centiseconds / 100;
    uint32_t csec = (uint32_t)(centiseconds % 100);

    append_char('[');
    char buf[20];
    int i = 0;
    uint64_t v = sec;
    if (v == 0) buf[i++] = '0';
    while (v > 0) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) append_char(buf[--i]);
    append_char('.');
    append_char((char)('0' + (csec / 10)));
    append_char((char)('0' + (csec % 10)));
    append_char('0'); append_char('0'); append_char('0'); append_char('0');
    append_char(']'); append_char(' ');
}

void dmesg_log(const char *msg) {
    if (!msg || !*msg) return;

    if (!dmesg_at_bol) {
        append_char('\n');
        dmesg_at_bol = 1;
    }

    while (*msg) {
        if (dmesg_at_bol) {
            append_timestamp();
            dmesg_at_bol = 0;
        }

        char c = *msg++;
        append_char(c);
        if (c == '\n') dmesg_at_bol = 1;
    }

    // Ensure we end at BOL for the next entry
    if (!dmesg_at_bol) {
        append_char('\n');
        dmesg_at_bol = 1;
    }
}

void dmesg_dump(void) {
    if (dmesg_total == 0) return;

    uint32_t start;
    uint32_t count;

    if (dmesg_total < DMESG_SIZE) {
        start = 0;
        count = dmesg_head;
    } else {
        start = dmesg_head;
        count = DMESG_SIZE;
    }

    for (uint32_t i = 0; i < count; i++) {
        char c = dmesg_buf[(start + i) % DMESG_SIZE];
        if (c) console_putc(c);
    }
}

uint32_t dmesg_get_total(void) { return dmesg_total; }
uint32_t dmesg_get_head(void) { return dmesg_head; }
uint32_t dmesg_get_size(void) { return DMESG_SIZE; }
char dmesg_get_char(uint32_t index) {
    if (index >= DMESG_SIZE) return 0;
    return dmesg_buf[index];
}

void dmesg_load_persistent(void) {}
void dmesg_save_persistent(void) {}
