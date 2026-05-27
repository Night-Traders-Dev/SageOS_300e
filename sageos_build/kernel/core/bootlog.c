#include <stdint.h>
#include <stddef.h>
#include "bootlog.h"
#include "vfs.h"
#include "bootinfo.h"

static SageOSBootInfo *g_boot_info = NULL;
static uint64_t g_log_offset = 0;
static int g_enabled = 0;

static uint64_t bl_strlen(const char *s) {
    uint64_t n = 0;
    while (s && s[n]) n++;
    return n;
}

void bootlog_init(void *info) {
    g_boot_info = (SageOSBootInfo *)info;
    g_enabled = 1;
    if (g_boot_info) {
        g_log_offset = g_boot_info->log_offset;
    } else {
        g_log_offset = 0;
    }
}

void bl_write_raw(const char *buf, uint64_t len) {
    if (!g_enabled) return;
    
    int written = vfs_write("/fat32/BOOTLOG.TXT", g_log_offset, buf, (size_t)len);
    if (written > 0) {
        g_log_offset += (uint64_t)written;
    }
}

void bootlog(const char *msg) {
    if (!msg) return;
    bl_write_raw(msg, bl_strlen(msg));
}

void bootlog_hex(const char *label, uint64_t value) {
    if (label) bl_write_raw(label, bl_strlen(label));
    static const char hex[] = "0123456789ABCDEF";
    char out[19];
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 16; i++) {
        out[2 + i] = hex[(value >> ((15 - i) * 4)) & 0xF];
    }
    out[18] = 0;
    bl_write_raw(out, 18);
    bl_write_raw("\r\n", 2);
}

void bootlog_close(void) {
    g_enabled = 0;
}
