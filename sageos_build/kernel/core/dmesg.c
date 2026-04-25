#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "timer.h"
#include "dmesg.h"

#define DMESG_SIZE 16384

static char dmesg_buf[DMESG_SIZE];
static uint32_t dmesg_head = 0;
static uint32_t dmesg_total = 0;

static void append_char(char c) {
    dmesg_buf[dmesg_head] = c;
    dmesg_head = (dmesg_head + 1) % DMESG_SIZE;
    if (dmesg_total < DMESG_SIZE) dmesg_total++;
}

void dmesg_log(const char *msg) {
    // Add timestamp [ seconds.microseconds ]
    uint64_t ticks = timer_ticks();
    uint64_t sec = ticks / 100;

    append_char('[');
    
    char buf[20];
    int i = 0;
    uint64_t v = sec;
    if (v == 0) buf[i++] = '0';
    while (v > 0) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) append_char(buf[--i]);

    append_char('.');
    
    // Fixed width 6-digit microsecond approximation (actually centiseconds * 1000)
    uint32_t csec = (uint32_t)(ticks % 100);
    append_char((char)('0' + (csec / 10)));
    append_char((char)('0' + (csec % 10)));
    append_char('0');
    append_char('0');
    append_char('0');
    append_char('0');

    append_char(']');
    append_char(' ');

    while (*msg) {
        append_char(*msg++);
    }
    append_char('\n');
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
