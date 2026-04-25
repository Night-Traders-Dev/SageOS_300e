#ifndef SAGEOS_KEYBOARD_H
#define SAGEOS_KEYBOARD_H

#include <stdint.h>

typedef struct {
    uint8_t scancode;
    uint8_t pressed;
    uint8_t extended;
    char ascii;
} KeyEvent;

void keyboard_init(void);
char keyboard_getchar(void);
int keyboard_poll_event(KeyEvent *ev);
int keyboard_wait_event(KeyEvent *ev);
const char *keyboard_backend(void);
void keyboard_keydebug(void);
void keyboard_irq(void);

#endif
