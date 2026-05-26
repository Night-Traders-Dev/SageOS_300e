#include <stdint.h>
#include <stddef.h>
#include "keyboard.h"
#include "serial.h"

#if defined(__x86_64__)
#define UART_BASE 0x3F8
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
int serial_avail(void) { return inb(UART_BASE + 5) & 1; }
char serial_getc(void) {
    while (!(inb(UART_BASE + 5) & 1));
    return (char)inb(UART_BASE);
}
#elif defined(__aarch64__)
#define UART_BASE 0x09000000
int serial_avail(void) { return !(*(volatile uint32_t *)(UART_BASE + 0x18) & 0x10); }
char serial_getc(void) {
    while (*(volatile uint32_t *)(UART_BASE + 0x18) & 0x10);
    return (char)(*(volatile uint32_t *)(UART_BASE) & 0xFF);
}
#elif defined(__riscv)
#define UART_BASE 0x10000000
int serial_avail(void) { return *(volatile uint8_t *)(UART_BASE + 5) & 1; }
char serial_getc(void) {
    while (!(*(volatile uint8_t *)(UART_BASE + 5) & 1));
    return (char)*(volatile uint8_t *)(UART_BASE);
}
#endif

int keyboard_wait_event(KeyEvent *ev) {
    if (!ev) return 0;
    ev->pressed = 1;
    ev->extended = 0;
    ev->scancode = 0;
    ev->ascii = serial_getc();
    return 1;
}

int keyboard_poll_any_event(KeyEvent *ev) {
    if (!ev) return 0;
    if (serial_avail()) {
        ev->pressed = 1;
        ev->extended = 0;
        ev->scancode = 0;
        ev->ascii = serial_getc();
        return 1;
    }
    return 0;
}

const char* keyboard_backend(void) {
    return "Virt Serial Keyboard";
}

void keyboard_init(void) {
    // Serial already initialized by console_init
}

void keyboard_keydebug(void) {
    // Not implemented for serial
}
