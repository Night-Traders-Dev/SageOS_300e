#include "io.h"
#include "serial.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

static int serial_ready(void) {
    return inb(COM1 + 5) & 0x20;
}

static int serial_rx_ready(void) {
    return inb(COM1 + 5) & 0x01;
}

void serial_putc(char c) {
    while (!serial_ready()) {}
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *s) {
    while (*s) {
        serial_putc(*s++);
    }
}

int serial_poll_char(char *out) {
    if (!serial_rx_ready()) {
        return 0;
    }

    *out = (char)inb(COM1);
    return 1;
}
