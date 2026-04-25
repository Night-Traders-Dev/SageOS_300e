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

#define I8042_DATA 0x60
#define I8042_STATUS 0x64
#define I8042_COMMAND 0x64

#define I8042_OBF 0x01
#define I8042_IBF 0x02
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

volatile uint64_t scancode_buffer = 0;

static int shift_down;
static int caps_lock;
static int extended_prefix;

static int firmware_input_available(void) {
    SageOSBootInfo *b = console_boot_info();

    return
        b &&
        b->magic == SAGEOS_BOOT_MAGIC &&
        b->boot_services_active &&
        b->input_mode == 1 &&
        b->con_in;
}

const char *keyboard_backend(void) {
    return firmware_input_available()
        ? "uefi-conin+i8042-poll+serial"
        : "i8042-irq+poll+serial";
}

static int queue_next(int idx) {
    return (idx + 1) % SCANCODE_QUEUE_SIZE;
}

static void enqueue_scancode(uint8_t sc) {
    uint8_t next = (uint8_t)queue_next(scancode_head);

    scancode_buffer = sc;

    if (next == scancode_tail) {
        return;
    }

    scancode_queue[scancode_head] = sc;
    scancode_head = next;
}

static int dequeue_scancode(uint8_t *sc) {
    if (scancode_head == scancode_tail) {
        return 0;
    }

    *sc = scancode_queue[scancode_tail];
    scancode_tail = (uint8_t)queue_next(scancode_tail);
    return 1;
}

static int wait_read(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if (inb(I8042_STATUS) & I8042_OBF) {
            return 1;
        }

        cpu_pause();
    }

    return 0;
}

static int wait_write(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inb(I8042_STATUS) & I8042_IBF) == 0) {
            return 1;
        }

        cpu_pause();
    }

    return 0;
}

static void flush_output(void) {
    for (int i = 0; i < 32; i++) {
        if (!(inb(I8042_STATUS) & I8042_OBF)) {
            break;
        }

        (void)inb(I8042_DATA);
    }
}

static void command(uint8_t v) {
    if (wait_write()) {
        outb(I8042_COMMAND, v);
    }
}

static void data(uint8_t v) {
    if (wait_write()) {
        outb(I8042_DATA, v);
    }
}

static uint8_t read_timeout(uint8_t fallback) {
    if (wait_read()) {
        return inb(I8042_DATA);
    }

    return fallback;
}

static void drain_controller(void) {
    for (int i = 0; i < 16; i++) {
        uint8_t status = inb(I8042_STATUS);

        if (!(status & I8042_OBF)) {
            return;
        }

        uint8_t sc = inb(I8042_DATA);

        if (status & I8042_AUX_DATA) {
            continue;
        }

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

    if (firmware_input_available()) {
        return;
    }

    command(0xAD);
    command(0xA7);
    flush_output();

    command(0x20);
    uint8_t cfg = read_timeout(0);
    cfg &= (uint8_t)~0x03;
    cfg |= 0x40;

    command(0x60);
    data(cfg);

    command(0xAE);
    flush_output();

    data(0xF4);
    (void)read_timeout(0);

    command(0x20);
    cfg = read_timeout(cfg);
    cfg |= 0x01;
    cfg |= 0x40;
    cfg &= (uint8_t)~0x02;

    command(0x60);
    data(cfg);
    flush_output();
}

void keyboard_irq(void) {
    drain_controller();
}

static char translate_ascii(uint8_t sc) {
    char c;

    if (sc >= sizeof(keymap)) {
        return 0;
    }

    c = shift_down ? shiftmap[sc] : keymap[sc];

    if (c >= 'a' && c <= 'z' && caps_lock) {
        c = (char)(c - ('a' - 'A'));
    } else if (c >= 'A' && c <= 'Z' && caps_lock) {
        c = (char)(c + ('a' - 'A'));
    }

    return c;
}

static int firmware_poll_char(char *out) {
    SageOSBootInfo *b = console_boot_info();

    if (!firmware_input_available()) {
        return 0;
    }

    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *con_in =
        (EFI_SIMPLE_TEXT_INPUT_PROTOCOL *)(uintptr_t)b->con_in;

    if (!con_in || !con_in->ReadKeyStroke) {
        return 0;
    }

    EFI_INPUT_KEY key;
    key.ScanCode = 0;
    key.UnicodeChar = 0;

    if (con_in->ReadKeyStroke(con_in, &key) != EFI_SUCCESS) {
        return 0;
    }

    if (!key.UnicodeChar || key.UnicodeChar > 0x7F) {
        return 0;
    }

    *out = (key.UnicodeChar == '\r') ? '\n' : (char)key.UnicodeChar;
    return 1;
}

int keyboard_poll_event(KeyEvent *ev) {
    uint8_t sc;

    if (!dequeue_scancode(&sc)) {
        drain_controller();

        if (!dequeue_scancode(&sc)) {
            return 0;
        }
    }

    ev->scancode = sc;
    ev->pressed = (sc & 0x80) ? 0 : 1;
    ev->extended = extended_prefix ? 1 : 0;
    ev->ascii = 0;

    if (sc == 0xE0 || sc == 0xE1) {
        extended_prefix = 1;
        ev->extended = 1;
        return 1;
    }

    if (extended_prefix) {
        extended_prefix = 0;
        return 1;
    }

    if (!ev->pressed) {
        uint8_t base = sc & 0x7F;

        if (base == 0x2A || base == 0x36) {
            shift_down = 0;
        }

        return 1;
    }

    if (sc == 0x2A || sc == 0x36) {
        shift_down = 1;
        return 1;
    }

    if (sc == 0x3A) {
        caps_lock = !caps_lock;
        return 1;
    }

    ev->ascii = translate_ascii(sc);
    return 1;
}

void keyboard_keydebug(void) {
    console_write("\nKEYDEBUG MODE");
    console_write("\nPress ESC to exit.");
    console_write("\n");

    for (;;) {
        KeyEvent ev;

        if (!keyboard_poll_event(&ev)) {
            status_tick_poll();
            cpu_hlt();
            continue;
        }

        console_write("sc=");
        console_hex64(ev.scancode);
        console_write(ev.pressed ? " make" : " break");

        if (ev.extended) {
            console_write(" ext");
        }

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

char keyboard_getchar(void) {
    for (;;) {
        char serial_c;
        char firmware_c;
        KeyEvent ev;

        if (firmware_poll_char(&firmware_c)) {
            return firmware_c;
        }

        if (serial_poll_char(&serial_c)) {
            return serial_c == '\r' ? '\n' : serial_c;
        }

        while (keyboard_poll_event(&ev)) {
            if (ev.pressed && ev.ascii) {
                return ev.ascii;
            }
        }

        status_tick_poll();

        if (firmware_input_available()) {
            timer_idle_poll();
            continue;
        }

        if (firmware_poll_char(&firmware_c)) {
            return firmware_c;
        }

        if (serial_poll_char(&serial_c)) {
            return serial_c == '\r' ? '\n' : serial_c;
        }

        cpu_hlt();
    }
}
