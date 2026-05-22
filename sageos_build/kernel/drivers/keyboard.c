#include "keyboard.h"
#include "console.h"
#include "io.h"
#include "serial.h"
#include "status.h"
#include "timer.h"
#include "scheduler.h"
#include "bootinfo.h"
#include <stdint.h>

extern SageOSBootInfo *kernel_get_boot_info(void);

typedef struct {
    uint16_t ScanCode;
    uint16_t UnicodeChar;
} EfiKey;

struct EfiSimpleTextInputProtocol;

typedef uint64_t (__attribute__((ms_abi)) *EfiInputReadKey)(
    struct EfiSimpleTextInputProtocol *This,
    EfiKey *Key
);

typedef struct EfiSimpleTextInputProtocol {
    void *Reset;
    EfiInputReadKey ReadKeyStroke;
    void *WaitForKey;
} EfiSimpleTextInputProtocol;

#define I8042_DATA    0x60
#define I8042_STATUS  0x64
#define I8042_COMMAND 0x64

#define I8042_OBF    0x01
#define I8042_IBF    0x02
#define I8042_AUX_DATA 0x20

#define SCANCODE_QUEUE_SIZE 64

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

static int shift_down;
static int caps_lock;
static int ctrl_down;
static int alt_down;
static int extended_prefix;

static int queue_next(int idx) {
    return (idx + 1) % SCANCODE_QUEUE_SIZE;
}

static void enqueue_scancode(uint8_t sc) {
    uint8_t next = (uint8_t)queue_next(scancode_head);
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

static int g_i8042_present = 1;

static void flush_output(void) {
    if (!g_i8042_present) return;
    for (int i = 0; i < 32; i++) {
        if (!(inb(I8042_STATUS) & I8042_OBF)) break;
        (void)inb(I8042_DATA);
    }
}

static void drain_controller(void) {
    if (!g_i8042_present) return;
    for (int i = 0; i < 32; i++) {
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
    shift_down = 0;
    caps_lock = 0;
    ctrl_down = 0;
    alt_down = 0;
    extended_prefix = 0;

    // Detect if i8042 exists by reading status port.
    // If it returns 0xFF, the port is floating (controller absent).
    if (inb(I8042_STATUS) == 0xFF) {
        g_i8042_present = 0;
    } else {
        g_i8042_present = 1;
        flush_output();
    }
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

static int parse_serial_escape(KeyEvent *ev) {
    char next = 0;
    int wait = 0;
    while (wait++ < 2000) {
        if (serial_poll_char(&next)) break;
        status_tick_poll();
        cpu_pause();
    }
    if (next != '[') {
        ev->scancode = 0; ev->pressed = 1; ev->extended = 0; ev->ascii = 27; return 1;
    }
    wait = 0;
    while (wait++ < 2000) {
        if (serial_poll_char(&next)) break;
        status_tick_poll();
        cpu_pause();
    }
    if (next == 'A') { ev->scancode = 0x48; ev->pressed = 1; ev->extended = 1; ev->ascii = 0; return 1; }
    if (next == 'B') { ev->scancode = 0x50; ev->pressed = 1; ev->extended = 1; ev->ascii = 0; return 1; }
    if (next == 'C') { ev->scancode = 0x4D; ev->pressed = 1; ev->extended = 1; ev->ascii = 0; return 1; }
    if (next == 'D') { ev->scancode = 0x4B; ev->pressed = 1; ev->extended = 1; ev->ascii = 0; return 1; }
    ev->scancode = 0; ev->pressed = 1; ev->extended = 0; ev->ascii = next;
    return 1;
}

int keyboard_poll_event(KeyEvent *ev) {
    uint8_t sc;
    if (!dequeue_scancode(&sc)) {
        drain_controller();
        if (!dequeue_scancode(&sc)) return 0;
    }
    ev->scancode = sc & 0x7Fu;
    ev->pressed  = (sc & 0x80) ? 0 : 1;
    ev->ascii    = 0;
    if (sc == 0xE0 || sc == 0xE1) {
        extended_prefix = 1;
        ev->extended = 0;
        return 0;
    }
    ev->extended = extended_prefix;
    extended_prefix = 0;
    if (!ev->pressed) {
        uint8_t base = sc & 0x7F;
        if (base == 0x2A || base == 0x36) shift_down = 0;
        if (base == 0x1D) ctrl_down = 0;
        if (base == 0x38) alt_down = 0;
        return 1;
    }
    if (sc == 0x2A || sc == 0x36) { shift_down = 1; return 1; }
    if (sc == 0x3A) { caps_lock = !caps_lock; return 1; }
    if (sc == 0x1D) { ctrl_down = 1; return 1; }
    if (sc == 0x38) { alt_down  = 1; return 1; }
    ev->ascii = translate_ascii(sc);
    if (ctrl_down && ev->ascii >= 'A' && ev->ascii <= 'Z')
        ev->ascii = (char)(ev->ascii - 'A' + 1);
    else if (ctrl_down && ev->ascii >= 'a' && ev->ascii <= 'z')
        ev->ascii = (char)(ev->ascii - 'a' + 1);
    return 1;
}

int keyboard_poll_any_event(KeyEvent *ev) {
    // 1. Check if firmware input mode is active (UEFI boot services active)
    SageOSBootInfo *info = kernel_get_boot_info();
    if (info && info->boot_services_active && info->con_in) {
        EfiSimpleTextInputProtocol *con_in = (EfiSimpleTextInputProtocol *)info->con_in;
        EfiKey key;
        // Call ReadKeyStroke via MS ABI (returns 0 on success, 6/EFI_NOT_READY if no key)
        uint64_t status = con_in->ReadKeyStroke(con_in, &key);
        if (status == 0) { // EFI_SUCCESS
            ev->pressed = 1;
            ev->extended = 0;
            if (key.UnicodeChar != 0) {
                ev->ascii = (char)key.UnicodeChar;
                if (ev->ascii == '\r') ev->ascii = '\n';
                ev->scancode = 0;
            } else {
                ev->ascii = 0;
                // Map UEFI arrow keys to standard scancodes
                if (key.ScanCode == 1) ev->scancode = 0x48;      // UP
                else if (key.ScanCode == 2) ev->scancode = 0x50; // DOWN
                else if (key.ScanCode == 3) ev->scancode = 0x4D; // RIGHT
                else if (key.ScanCode == 4) ev->scancode = 0x4B; // LEFT
                else ev->scancode = 0;
            }
            return 1;
        }
        return 0;
    }

    char serial_c;
    if (serial_poll_char(&serial_c)) {
        if (serial_c == 27) return parse_serial_escape(ev);
        ev->scancode = 0; ev->pressed = 1; ev->extended = 0;
        ev->ascii = (serial_c == '\r') ? '\n' : serial_c;
        return 1;
    }
    return keyboard_poll_event(ev);
}

static int keyboard_poll_visible_event(KeyEvent *ev) {
    for (int i = 0; i < 16; i++) {
        if (!keyboard_poll_any_event(ev)) return 0;
        if (!ev->pressed) continue;
        if (ev->ascii || ev->extended) return 1;
    }
    return 0;
}

const char *keyboard_backend(void) {
    SageOSBootInfo *info = kernel_get_boot_info();
    if (info && info->boot_services_active && info->con_in) {
        return "UEFI-ConIn (firmware mode)";
    }
    if (!g_i8042_present) {
        return "serial-only (i8042 absent)";
    }
    return "i8042-irq+poll+serial";
}

int keyboard_wait_event(KeyEvent *ev) {
    for (;;) {
        if (keyboard_poll_visible_event(ev)) return 1;
        status_tick_poll();
        timer_poll();
        cpu_pause();
        sched_yield();
    }
}

void keyboard_keydebug(void) {
    console_write("\nKEYDEBUG NOT SUPPORTED IN NATIVE MODE\n");
}
