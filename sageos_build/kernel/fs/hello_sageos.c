/*
 * hello_sageos.c — Minimal test ELF for SageOS
 *
 * This is a freestanding ELF that demonstrates execution within the
 * SageOS kernel environment. It uses direct hardware I/O to print
 * to the serial port (COM1: 0x3F8) because there are no syscalls yet.
 */

#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void serial_write(const char *s) {
    while (*s) {
        outb(0x3F8, *s++);
    }
}

int _start(void) {
    serial_write("\r\n[ELF] Hello from a freestanding executable running in SageOS!\r\n");
    return 42; /* Return code checked by the shell */
}
