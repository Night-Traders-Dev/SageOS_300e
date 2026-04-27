#include "keyboard.h"
#include "console.h"
#include "io.h"
#include "serial.h"
#include "status.h"
#include "timer.h"
#include <stdint.h>

#if defined(__clang__) || defined(__GNUC__)
#define EFIAPI __attribute__((ms_abi))
#else
#define EFIAPI
#endif

#define EFI_SUCCESS 0

typedef uint16_t CHAR16;
typedef uint64_t EFI_STATUS;

typedef struct {
    uint16_t ScanCode;
    CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_READ_KEY_STROKE)(
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *self,
    EFI_INPUT_KEY *key
);

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    void *Reset;
    EFI_READ_KEY_STROKE ReadKeyStroke;
    void *WaitForKey;
};

/* UEFI Simple Text Input scan codes for special keys */
#define UEFI_SCAN_UP       0x01
#define UEFI_SCAN_DOWN     0x02
#define UEFI_SCAN_RIGHT    0x03
#define UEFI_SCAN_LEFT     0x04
#define UEFI_SCAN_HOME     0x05
#define UEFI_SCAN_END      0x06
#define UEFI_SCAN_DELETE   0x08
#define UEFI_SCAN_ESC      0x17

#define I8042_DATA    0x60
#define I8042_STATUS  0x64
#define I8042_COMMAND 0x64

#define I8042_OBF    0x01
#define I8042_IBF    0x02
#define I8042_AUX_DATA 0x20

#define SCANCODE_QUEUE_SIZE 64

#ifndef SAGEOS_FIRMWARE_I8042_FALLBACK
#define SAGEOS_FIRMWARE_I8042_FALLBACK 1
#endif

static const char keymap[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 8,
    9,   'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 10,  0,
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', 39,  '`', 0,   92,  'z',
    'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,   '*', 0,   ' ',
};

static const char shiftmap[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 8,
    9,   'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 10,  0,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', 34,  '~', 0,   124, 'Z',
    'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,   '*', 0,   ' ',
};

static volatile uint8_t scancode_queue[SCANCODE_QUEUE_SIZE];
static volatile uint8_t scancode_head;
static volatile uint8_t scancode_tail;

volatile uint64_t scancode_buffer = 0;

static int shift_down;
static int caps_lock;
static int ctrl_down;
static int alt_down;
static int extended_prefix;

static int firmware_input_available(void) {
    SageOSBootInfo *b = console_boot_info();
    return b &&
           b->magic == SAGEOS_BOOT_MAGIC &&
           b->boot_services_active &&
           b->input_mode == 1 &&
           b->con_in;
}

static int firmware_i8042_fallback_enabled(void) {
    return SAGEOS_FIRMWARE_I8042_FALLBACK != 0;
}

const char *keyboard_backend(void) {
    if (!firmware_input_available()) return "i8042-irq+poll+serial";
    return firmware_i8042_fallback_enabled()
        ? "uefi-conin+i8042-poll+serial"
        : "uefi-conin+serial";
}

static int queue_next(int idx) {
    return (idx + 1) % SCANCODE_QUEUE_SIZE;
}

static void enqueue_scancode(uint8_t sc) {
    uint8_t next = (uint8_t)queue_next(scancode_head);
    scancode_buffer = sc;
    if (next == scancode_tail) return;
    scancode_queue[scancode_head] = sc;
    scancode_head = next;
}

static int dequeue_scancode(uint8_t *sc) {
    if (scancode_head == scancode_tail) return 0;
    *sc = scancode_queue[scancode_tail];
    scancode_tail = (uint8_t)queue_next(scancode_tail);
    return 1;
}

static int wait_read(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if (inb(I8042_STATUS) & I8042_OBF) return 1;
        cpu_pause();
    }
    return 0;
}

static int wait_write(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inb(I8042_STATUS) & I8042_IBF) == 0) return 1;
        cpu_pause();
    }
    return 0;
}

static void flush_output(void) {
    for (int i = 0; i < 32; i++) {
        if (!(inb(I8042_STATUS) & I8042_OBF)) break;
        (void)inb(I8042_DATA);
    }
}

static void command(uint8_t v) {
    if (wait_write()) outb(I8042_COMMAND, v);
}

static void data(uint8_t v) {
    if (wait_write()) outb(I8042_DATA, v);
}

static uint8_t read_timeout(uint8_t fallback) {
    if (wait_read()) return inb(I8042_DATA);
    return fallback;
}

static void drain_controller(void) {
    for (int i = 0; i < 32; i++) {  /* Increased from 16 to 32 */
        uint8_t status = inb(I8042_STATUS);
        if (!(status & I8042_OBF)) return;
        uint8_t sc = inb(I8042_DATA);
        if (status & I8042_AUX_DATA) continue;
        enqueue_scancode(sc);
    }
}

void keyboard_init(void) {
    scancode_head = 0;
    scancode_tail = 0;
    scancode_buffer = 0;
    shift_down = 0;
    caps_lock = 0;
    extended_prefix = 0;

    if (firmware_input_available() && !firmware_i8042_fallback_enabled()) return;

    command(0xAD);
    command(0xA7);
    flush_output();

    command(0x20);
    uint8_t cfg = read_timeout(0);
    cfg &= (uint8_t)~0x03;  /* Disable keyboard and mouse interrupts */
    cfg |= 0x10;           /* Disable mouse interface */
    cfg |= 0x20;           /* Enable scancode translation */

    command(0x60);
    data(cfg);

    command(0xAE);
    flush_output();

    /* Enable keyboard scanning */
    data(0xF4);
    if (wait_read()) {
        uint8_t ack = inb(I8042_DATA);
        if (ack != 0xFA) {
            /* Keyboard didn't acknowledge, try again */
            data(0xF4);
            (void)read_timeout(0);
        }
    }

    command(0x20);
    cfg = read_timeout(cfg);
    cfg |= 0x01;           /* Enable keyboard interrupts */
    cfg |= 0x10;           /* Disable mouse interface */
    cfg |= 0x20;           /* Enable scancode translation */
    cfg &= (uint8_t)~0x02; /* Disable mouse interrupts */

    command(0x60);
    data(cfg);
    flush_output();
}

void keyboard_irq(void) {
    drain_controller();
}

static char translate_ascii(uint8_t sc) {
    char c;
    if (sc >= sizeof(keymap)) return 0;
    c = shift_down ? shiftmap[sc] : keymap[sc];
    if (c >= 'a' && c <= 'z' && caps_lock)
        c = (char)(c - ('a' - 'A'));
    else if (c >= 'A' && c <= 'Z' && caps_lock)
        c = (char)(c + ('a' - 'A'));
    return c;
}

/*
 * firmware_poll_key — poll UEFI ConIn for a full KeyEvent.
 *
 * UEFI EFI_INPUT_KEY carries two fields:
 *   ScanCode    — non-zero for special/function keys (arrows, Home, End…)
 *   UnicodeChar — non-zero for printable + control characters
 *
 * The old firmware_poll_char() discarded any key where UnicodeChar==0,
 * which silently dropped every arrow key press on the UEFI-ConIn path.
 *
 * This function maps UEFI scan codes to the same PS/2-style extended
 * scancodes that the i8042 path emits so the shell dispatch is unified.
 */
static int firmware_poll_key(KeyEvent *ev) {
    SageOSBootInfo *b = console_boot_info();
    if (!firmware_input_available()) return 0;

    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *con_in =
        (EFI_SIMPLE_TEXT_INPUT_PROTOCOL *)(uintptr_t)b->con_in;
    if (!con_in || !con_in->ReadKeyStroke) return 0;

    EFI_INPUT_KEY key;
    key.ScanCode   = 0;
    key.UnicodeChar = 0;
    if (con_in->ReadKeyStroke(con_in, &key) != EFI_SUCCESS) return 0;

    ev->pressed  = 1;
    ev->extended = 0;
    ev->ascii    = 0;
    ev->scancode = 0;

    /* Special/extended keys — map UEFI scan to PS/2 extended scancode */
    if (key.ScanCode) {
        ev->extended = 1;
        switch (key.ScanCode) {
        case UEFI_SCAN_UP:     ev->scancode = 0x48; return 1;  /* Up    */
        case UEFI_SCAN_DOWN:   ev->scancode = 0x50; return 1;  /* Down  */
        case UEFI_SCAN_RIGHT:  ev->scancode = 0x4D; return 1;  /* Right */
        case UEFI_SCAN_LEFT:   ev->scancode = 0x4B; return 1;  /* Left  */
        case UEFI_SCAN_HOME:   ev->scancode = 0x47; return 1;  /* Home  */
        case UEFI_SCAN_END:    ev->scancode = 0x4F; return 1;  /* End   */
        case UEFI_SCAN_DELETE: ev->scancode = 0x53; return 1;  /* Del   */
        case UEFI_SCAN_ESC:
            ev->extended = 0;
            ev->ascii    = 27;
            return 1;
        default:
            /* Unknown scan code — ignore */
            return 0;
        }
    }

    /* Printable / control character */
    if (key.UnicodeChar && key.UnicodeChar <= 0x7F) {
        char c = (char)key.UnicodeChar;
        ev->ascii = (c == '\r') ? '\n' : c;
        return 1;
    }

    return 0;
}

static int parse_serial_escape(KeyEvent *ev) {
    char next;
    int wait = 0;

    while (wait++ < 2000) {
        if (serial_poll_char(&next)) break;
        status_tick_poll();
        cpu_hlt();
    }

    if (next != '[') {
        ev->scancode = 0;
        ev->pressed  = 1;
        ev->extended = 0;
        ev->ascii    = 27;
        return 1;
    }

    wait = 0;
    while (wait++ < 2000) {
        if (serial_poll_char(&next)) break;
        status_tick_poll();
        cpu_hlt();
    }

    if (next == 'A') { ev->scancode = 0x48; ev->pressed = 1; ev->extended = 1; ev->ascii = 0; return 1; }
    if (next == 'B') { ev->scancode = 0x50; ev->pressed = 1; ev->extended = 1; ev->ascii = 0; return 1; }
    if (next == 'C') { ev->scancode = 0x4D; ev->pressed = 1; ev->extended = 1; ev->ascii = 0; return 1; }
    if (next == 'D') { ev->scancode = 0x4B; ev->pressed = 1; ev->extended = 1; ev->ascii = 0; return 1; }

    if (next == '3') {
        wait = 0;
        while (wait++ < 2000) {
            if (serial_poll_char(&next)) break;
            status_tick_poll();
            cpu_hlt();
        }
        if (next == '~') { ev->scancode = 0x53; ev->pressed = 1; ev->extended = 1; ev->ascii = 0; return 1; }
    }

    if (next == '1') {
        wait = 0;
        while (wait++ < 2000) {
            if (serial_poll_char(&next)) break;
            status_tick_poll();
            cpu_hlt();
        }
        if (next == '~') { ev->scancode = 0x47; ev->pressed = 1; ev->extended = 1; ev->ascii = 0; return 1; } /* Home */
    }

    if (next == '4') {
        wait = 0;
        while (wait++ < 2000) {
            if (serial_poll_char(&next)) break;
            status_tick_poll();
            cpu_hlt();
        }
        if (next == '~') { ev->scancode = 0x4F; ev->pressed = 1; ev->extended = 1; ev->ascii = 0; return 1; } /* End */
    }

    ev->scancode = 0;
    ev->pressed  = 1;
    ev->extended = 0;
    ev->ascii    = 27;
    return 1;
}

int keyboard_poll_event(KeyEvent *ev) {
    uint8_t sc;

    if (!dequeue_scancode(&sc)) {
        drain_controller();
        if (!dequeue_scancode(&sc)) return 0;
    }

    ev->scancode = sc & 0x7Fu;  /* strip break bit for scancode field */
    ev->pressed  = (sc & 0x80) ? 0 : 1;
    ev->ascii    = 0;

    /*
     * 0xE0 / 0xE1 — extended prefix byte.  Record the flag and return
     * without emitting a visible event; the *next* scancode will carry
     * ev->extended = 1.
     */
    if (sc == 0xE0 || sc == 0xE1) {
        extended_prefix = 1;
        ev->extended = 0;   /* The prefix itself is not an extended key event */
        return 0;           /* Consume silently; wait for the real scancode    */
    }

    /*
     * Latch and clear the extended flag onto this (the actual) key event.
     */
    ev->extended = extended_prefix;
    extended_prefix = 0;

    if (!ev->pressed) {
        uint8_t base = sc & 0x7F;
        if (base == 0x2A || base == 0x36) shift_down = 0;
        if (base == 0x1D)                 ctrl_down  = 0;
        if (base == 0x38)                 alt_down   = 0;
        return 1;
    }

    if (sc == 0x2A || sc == 0x36) { shift_down = 1; return 1; }
    if (sc == 0x3A)               { caps_lock = !caps_lock; return 1; }
    if (sc == 0x1D)               { ctrl_down = 1; return 1; }
    if (sc == 0x38)               { alt_down  = 1; return 1; }

    ev->ascii = translate_ascii(sc);
    if (ctrl_down && ev->ascii >= 'A' && ev->ascii <= 'Z')
        ev->ascii = (char)(ev->ascii - 'A' + 1);
    else if (ctrl_down && ev->ascii >= 'a' && ev->ascii <= 'z')
        ev->ascii = (char)(ev->ascii - 'a' + 1);

    return 1;
}

int keyboard_poll_any_event(KeyEvent *ev) {
    char serial_c;
    int firmware_mode = firmware_input_available();

    if (firmware_poll_key(ev)) return 1;

    if (serial_poll_char(&serial_c)) {
        if (serial_c == 27) return parse_serial_escape(ev);
        ev->scancode = 0;
        ev->pressed  = 1;
        ev->extended = 0;
        ev->ascii    = (serial_c == '\r') ? '\n' : serial_c;
        return 1;
    }

    if (!firmware_mode || firmware_i8042_fallback_enabled()) {
        return keyboard_poll_event(ev);
    }

    return 0;
}

void keyboard_keydebug(void) {
    console_write("\nKEYDEBUG MODE");
    console_write("\nPress ESC to exit.");
    console_write("\n");

    for (;;) {
        KeyEvent ev;
        timer_poll();
        if (!keyboard_poll_any_event(&ev)) {
            status_tick_poll();
            cpu_hlt();
            continue;
        }
        console_write("sc=");
        console_hex64(ev.scancode);
        console_write(ev.pressed ? " make" : " break");
        if (ev.extended) console_write(" ext");
        if (ev.ascii) { console_write(" ascii='"); console_putc(ev.ascii); console_write("'"); }
        console_write("\n");

        if (ev.pressed && ev.ascii == 27) {
            console_write("Leaving keydebug.\n");
            return;
        }
    }
}

/*
 * keyboard_wait_event — blocking event read.
 *
 * Priority order:
 *   1. UEFI ConIn (firmware_poll_key) — handles both printable chars AND
 *      special keys (arrows etc.) via the new unified firmware_poll_key().
 *   2. Serial terminal escape sequences.
 *   3. Native i8042 PS/2 polling.
 */
int keyboard_wait_event(KeyEvent *ev) {
    for (;;) {
        int firmware_mode = firmware_input_available();

        if (keyboard_poll_any_event(ev) && ev->pressed) return 1;

        status_tick_poll();
        timer_poll(); /* Ensure we account for this loop in CPU% */

        if (firmware_mode) {
            /* 
             * No artificial delay here; timer_idle_poll() provides cpu_pause().
             * Reducing latency for firmware-based input.
             */
            timer_idle_poll();
        } else {
            cpu_pause();
        }
    }
}

char keyboard_getchar(void) {
    KeyEvent ev;
    for (;;) {
        if (keyboard_wait_event(&ev)) {
            if (ev.pressed && ev.ascii) return ev.ascii;
        }
    }
}
