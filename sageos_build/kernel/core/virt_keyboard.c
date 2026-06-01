#include <stdint.h>
#include <stddef.h>
#include "keyboard.h"
#include "serial.h"

#if defined(__x86_64__)
#define UART_BASE UART_BASE_X64
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
int serial_avail(void) { return inb(UART_BASE + 5) & 1; }
char serial_getc(void) {
    extern void timer_poll(void);
    extern void sched_yield(void);
    while (!(inb(UART_BASE + 5) & 1)) { 
        timer_poll(); 
        sched_yield(); 
        __asm__ volatile("pause"); 
    }
    return (char)inb(UART_BASE);
}
#elif defined(__aarch64__)
#define UART_BASE UART_BASE_ARM64
int serial_avail(void) { return !(*(volatile uint32_t *)(UART_BASE + 0x18) & 0x10); }
char serial_getc(void) {
    extern void timer_poll(void);
    extern void sched_yield(void);
    while (*(volatile uint32_t *)(UART_BASE + 0x18) & 0x10) { 
        timer_poll(); 
        sched_yield(); 
    }
    return (char)(*(volatile uint32_t *)(UART_BASE) & 0xFF);
}
#elif defined(__riscv)
#define UART_BASE UART_BASE_RV64
int serial_avail(void) { return *(volatile uint8_t *)(UART_BASE + 5) & 1; }
char serial_getc(void) {
    extern void timer_poll(void);
    extern void sched_yield(void);
    while (!(*(volatile uint8_t *)(UART_BASE + 5) & 1)) { 
        timer_poll(); 
        sched_yield(); 
    }
    return (char)*(volatile uint8_t *)(UART_BASE);
}
#endif

static int g_esc_state = 0;

int keyboard_wait_event(KeyEvent *ev) {
    if (!ev) return 0;
    
    for (;;) {
        char c = serial_getc();
        
        if (g_esc_state == 0) {
            if (c == 27) { // ESC
                g_esc_state = 1;
                continue;
            }
            ev->pressed = 1;
            ev->extended = 0;
            ev->scancode = 0;
            ev->ascii = c;
            return 1;
        } else if (g_esc_state == 1) {
            if (c == '[') {
                g_esc_state = 2;
                continue;
            }
            g_esc_state = 0;
            // Treat as raw ESC followed by this char? 
            // For now just return the char
            ev->pressed = 1;
            ev->extended = 0;
            ev->ascii = c;
            return 1;
        } else if (g_esc_state == 2) {
            g_esc_state = 0;
            ev->pressed = 1;
            ev->extended = 1;
            ev->ascii = 0;
            switch (c) {
                case 'A': ev->scancode = 0x48; return 1; // Up
                case 'B': ev->scancode = 0x50; return 1; // Down
                case 'C': ev->scancode = 0x4D; return 1; // Right
                case 'D': ev->scancode = 0x4B; return 1; // Left
                case 'H': ev->scancode = 0x47; return 1; // Home
                case 'F': ev->scancode = 0x4F; return 1; // End
                case '3': g_esc_state = 3; continue;    // Possible Delete [3~
                default: break;
            }
            ev->extended = 0;
            ev->ascii = c;
            return 1;
        } else if (g_esc_state == 3) {
            g_esc_state = 0;
            if (c == '~') {
                ev->pressed = 1;
                ev->extended = 1;
                ev->scancode = 0x53; // Delete
                ev->ascii = 0;
                return 1;
            }
            ev->pressed = 1;
            ev->extended = 0;
            ev->ascii = c;
            return 1;
        }
    }
}

int keyboard_poll_any_event(KeyEvent *ev) {
    if (!ev) return 0;
    if (serial_avail() || g_esc_state > 0) {
        return keyboard_wait_event(ev);
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
