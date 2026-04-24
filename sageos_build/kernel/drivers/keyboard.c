#include "keyboard.h"
#include "console.h"
#include "io.h"
#include "status.h"
#include "timer.h"
#include <stdint.h>

extern volatile uint64_t scancode_buffer;

const char *keyboard_backend(void) { return "native-ps2-irq"; }

void keyboard_init(void) {
    /* Assembly implementation initializes IRQ1 */
}

int keyboard_poll_event(KeyEvent *ev) {
    uint64_t sc = scancode_buffer;
    if (sc == 0) return 0;
    
    /* Clear buffer after reading */
    scancode_buffer = 0;

    ev->scancode = (uint8_t)sc;
    ev->pressed = (sc & 0x80) ? 0 : 1;
    ev->extended = 0;
    ev->ascii = 0;

    /* Simple mapping for demonstration */
    if (ev->pressed && ev->scancode < 128) {
        /* Add basic ASCII mapping logic if needed */
    }

    return 1;
}

char keyboard_getchar(void) {
  for (;;) {
    KeyEvent ev;
    if (keyboard_poll_event(&ev)) {
      if (ev.pressed && ev.ascii) return ev.ascii;
      if (ev.pressed && ev.scancode == 0x1C) return '\n';
    }
    status_tick_poll();
    cpu_hlt();
  }
}