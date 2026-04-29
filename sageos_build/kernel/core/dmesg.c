#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "timer.h"
#include "dmesg.h"
#include "ata.h"

#define DMESG_SIZE 16384
#define PERSISTENT_LBA 100
#define PERSISTENT_SECTORS 32

static char dmesg_buf[DMESG_SIZE];
static uint32_t dmesg_head = 0;
static uint32_t dmesg_total = 0;

static void append_char(char c) {
    dmesg_buf[dmesg_head] = c;
    dmesg_head = (dmesg_head + 1) % DMESG_SIZE;
    if (dmesg_total < DMESG_SIZE) dmesg_total++;
}

void dmesg_save_persistent(void) {
    if (!ata_is_available()) return;

    uint16_t sector[256];
    uint32_t bytes_saved = 0;

    for (uint32_t s = 0; s < PERSISTENT_SECTORS && bytes_saved < DMESG_SIZE; s++) {
        for (int i = 0; i < 256; i++) {
            sector[i] = (uint16_t)dmesg_buf[bytes_saved++];
            sector[i] |= (uint16_t)dmesg_buf[bytes_saved++] << 8;
        }
        ata_write_sector(PERSISTENT_LBA + s, sector);
    }
}

void dmesg_load_persistent(void) {
    if (!ata_is_available()) return;

    uint16_t sector[256];
    uint32_t bytes_loaded = 0;

    for (uint32_t s = 0; s < PERSISTENT_SECTORS && bytes_loaded < DMESG_SIZE; s++) {
        if (!ata_read_sector(PERSISTENT_LBA + s, sector)) break;
        for (int i = 0; i < 256; i++) {
            dmesg_buf[bytes_loaded++] = (char)(sector[i] & 0xFF);
            dmesg_buf[bytes_loaded++] = (char)((sector[i] >> 8) & 0xFF);
        }
    }
    dmesg_total = bytes_loaded;
    dmesg_head = bytes_loaded % DMESG_SIZE;
}

void dmesg_log(const char *msg) {
    uint64_t ticks = timer_ticks();
    uint64_t sec = ticks / 100;

    append_char('[');
    
    char buf[20];
    int i = 0;
    uint64_t v = sec;
    if (v == 0) buf[i++] = '0';
    while (v > 0) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) append_char(buf[--i]);

    append_char('.');
    
    uint32_t csec = (uint32_t)(ticks % 100);
    append_char((char)('0' + (csec / 10)));
    append_char((char)('0' + (csec % 10)));
    append_char('0');
    append_char('0');
    append_char('0');
    append_char('0');

    append_char(']');
    append_char(' ');

    while (*msg) {
        append_char(*msg++);
    }
    append_char('\n');
    
    dmesg_save_persistent();
}

void dmesg_dump(void) {
    if (dmesg_total == 0) return;

    uint32_t start;
    uint32_t count;

    if (dmesg_total < DMESG_SIZE) {
        start = 0;
        count = dmesg_head;
    } else {
        start = dmesg_head;
        count = DMESG_SIZE;
    }

    for (uint32_t i = 0; i < count; i++) {
        char c = dmesg_buf[(start + i) % DMESG_SIZE];
        if (c) console_putc(c);
    }
}

uint32_t dmesg_get_total(void) { return dmesg_total; }
uint32_t dmesg_get_head(void) { return dmesg_head; }
uint32_t dmesg_get_size(void) { return DMESG_SIZE; }
char dmesg_get_char(uint32_t index) {
    if (index >= DMESG_SIZE) return 0;
    return dmesg_buf[index];
}
