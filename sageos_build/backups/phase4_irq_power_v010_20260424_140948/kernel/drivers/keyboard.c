#include <stdint.h>
#include "io.h"
#include "keyboard.h"
#include "console.h"
#include "timer.h"

static char keymap[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', 8,
    9, 'q','w','e','r','t','y','u','i','o','p','[',']', 10, 0,
    'a','s','d','f','g','h','j','k','l',';',39,'`', 0, 92,
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' ',
};

static char shiftmap[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', 8,
    9, 'Q','W','E','R','T','Y','U','I','O','P','{','}', 10, 0,
    'A','S','D','F','G','H','J','K','L',':',34,'~', 0, 124,
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' ',
};

static int shift_down;
static int extended;

const char *keyboard_backend(void) {
    return "native-i8042-ps2";
}

static int wait_read(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if (inb(0x64) & 1) return 1;
        timer_idle_poll();
    }

    return 0;
}

static int wait_write(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inb(0x64) & 2) == 0) return 1;
        timer_idle_poll();
    }

    return 0;
}

static void flush(void) {
    for (int i = 0; i < 32; i++) {
        if (!(inb(0x64) & 1)) break;
        (void)inb(0x60);
    }
}

static void cmd(uint8_t v) {
    if (wait_write()) outb(0x64, v);
}

static void data(uint8_t v) {
    if (wait_write()) outb(0x60, v);
}

static uint8_t read_timeout(uint8_t fallback) {
    if (wait_read()) return inb(0x60);
    return fallback;
}

void keyboard_init(void) {
    flush();

    cmd(0xAD);
    cmd(0xA7);
    flush();

    uint8_t cfg = 0;
    cmd(0x20);
    cfg = read_timeout(0);

    cfg |= 0x01;
    cfg |= 0x40;
    cfg &= (uint8_t)~0x02;

    cmd(0x60);
    data(cfg);

    cmd(0xAE);
    flush();

    data(0xF4);
    (void)read_timeout(0);

    shift_down = 0;
    extended = 0;
}

int keyboard_poll_event(KeyEvent *ev) {
    if (!(inb(0x64) & 1)) return 0;

    uint8_t sc = inb(0x60);

    ev->scancode = sc;
    ev->pressed = (sc & 0x80) ? 0 : 1;
    ev->extended = extended ? 1 : 0;
    ev->ascii = 0;

    if (sc == 0xE0 || sc == 0xE1) {
        extended = 1;
        return 1;
    }

    if (extended) {
        extended = 0;
        return 1;
    }

    if (sc & 0x80) {
        uint8_t base = sc & 0x7F;
        if (base == 0x2A || base == 0x36) shift_down = 0;
        return 1;
    }

    if (sc == 0x2A || sc == 0x36) {
        shift_down = 1;
        return 1;
    }

    if (sc == 0x3A) {
        return 1;
    }

    if (sc < sizeof(keymap)) {
        ev->ascii = shift_down ? shiftmap[sc] : keymap[sc];
    }

    return 1;
}

char keyboard_getchar(void) {
    for (;;) {
        KeyEvent ev;

        if (keyboard_poll_event(&ev)) {
            if (ev.pressed && ev.ascii) return ev.ascii;
        }

        timer_idle_poll();
    }
}

void keyboard_keydebug(void) {
    console_write("\nKEYDEBUG MODE");
    console_write("\nPress ESC to exit.");
    console_write("\n");

    for (;;) {
        KeyEvent ev;

        if (!keyboard_poll_event(&ev)) {
            timer_idle_poll();
            continue;
        }

        console_write("sc=");
        console_hex64(ev.scancode);
        console_write(ev.pressed ? " make" : " break");

        if (ev.extended) console_write(" ext");

        if (ev.ascii) {
            console_write(" ascii='");
            console_putc(ev.ascii);
            console_write("'");
        }

        console_write("\n");

        if (ev.pressed && ev.ascii == 27) {
            console_write("Leaving keydebug.\n");
            return;
        }
    }
}
