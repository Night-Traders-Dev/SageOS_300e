#include "io.h"
#include "serial.h"

#define COM1 0x3F8
#define SERIAL_BUFFER_SIZE 1024

static char serial_tx_buffer[SERIAL_BUFFER_SIZE];
static volatile int serial_tx_head = 0;
static volatile int serial_tx_tail = 0;
static volatile int serial_tx_full = 0;

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);

    serial_tx_head = 0;
    serial_tx_tail = 0;
    serial_tx_full = 0;
}

static int serial_ready(void) {
    return inb(COM1 + 5) & 0x20;
}

static int serial_rx_ready(void) {
    return inb(COM1 + 5) & 0x01;
}

void serial_process_tx_buffer(void) {
    while (!serial_tx_full && serial_tx_head != serial_tx_tail) {
        if (!serial_ready()) break;

        outb(COM1, (uint8_t)serial_tx_buffer[serial_tx_tail]);
        serial_tx_tail = (serial_tx_tail + 1) % SERIAL_BUFFER_SIZE;

        if (serial_tx_full) serial_tx_full = 0;
    }
}

void serial_putc(char c) {
    /* Try to send immediately if buffer is empty and UART is ready */
    if (serial_tx_head == serial_tx_tail && !serial_tx_full && serial_ready()) {
        outb(COM1, (uint8_t)c);
        return;
    }

    /* Buffer the character */
    int next_head = (serial_tx_head + 1) % SERIAL_BUFFER_SIZE;
    if (next_head == serial_tx_tail) {
        /* Buffer full, wait for space */
        while (next_head == serial_tx_tail) {
            serial_process_tx_buffer();
            cpu_pause();
        }
    }

    serial_tx_buffer[serial_tx_head] = c;
    serial_tx_head = next_head;
    if (serial_tx_head == serial_tx_tail) serial_tx_full = 1;

    /* Try to send buffered data */
    serial_process_tx_buffer();
}

void serial_write(const char *s) {
    while (*s) {
        serial_putc(*s++);
    }
    /* Flush buffer */
    while (serial_tx_head != serial_tx_tail || serial_tx_full) {
        serial_process_tx_buffer();
    }
}

int serial_poll_char(char *out) {
    if (!serial_rx_ready()) {
        return 0;
    }

    *out = (char)inb(COM1);
    return 1;
}
