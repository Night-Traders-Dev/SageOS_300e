/*
 * bootlog.c — kernel-side persistent boot log
 *
 * Appends ASCII text to /fat32/BOOTLOG.TXT using the VFS layer.
 */

#include <stdint.h>
#include <stddef.h>
#include "bootlog.h"
#include "vfs.h"

struct EfiFileProtocol;

typedef uint64_t (__attribute__((ms_abi)) *EfiFileWrite)(
    struct EfiFileProtocol *This,
    uint64_t *BufferSize,
    void *Buffer
);

typedef uint64_t (__attribute__((ms_abi)) *EfiFileSetPosition)(
    struct EfiFileProtocol *This,
    uint64_t Position
);

typedef uint64_t (__attribute__((ms_abi)) *EfiFileFlush)(
    struct EfiFileProtocol *This
);

typedef struct EfiFileProtocol {
    uint64_t Revision;
    void *Open;
    void *Close;
    void *Delete;
    void *Read;
    EfiFileWrite Write;
    void *GetPosition;
    EfiFileSetPosition SetPosition;
    void *Flush;
} EfiFileProtocol;

static SageOSBootInfo *g_boot_info = NULL;
static uint64_t g_log_offset = 0;
static int g_enabled = 0;

void bl_write_raw(const char *buf, uint64_t len) {
    if (!g_enabled) return;
    
    // 1. If boot services are active and we have an open EFI log file, write via UEFI MS ABI!
    if (g_boot_info && g_boot_info->boot_services_active && g_boot_info->log_file) {
        EfiFileProtocol *log_file = (EfiFileProtocol *)g_boot_info->log_file;
        if (log_file->SetPosition) {
            log_file->SetPosition(log_file, g_log_offset);
        }
        uint64_t written = len;
        log_file->Write(log_file, &written, (void *)buf);
        g_log_offset += written;
        g_boot_info->log_offset = g_log_offset;
        
        if (log_file->Flush) {
            // Call Flush
            typedef uint64_t (__attribute__((ms_abi)) *EfiFlushFn)(EfiFileProtocol *);
            ((EfiFlushFn)log_file->Flush)(log_file);
        }
        return;
    }

    // 2. Otherwise fallback to VFS write
    int written = vfs_write("/fat32/BOOTLOG.TXT", g_log_offset, buf, len);
    if (written > 0) {
        g_log_offset += (uint64_t)written;
    }
}

static uint64_t bl_strlen(const char *s) {
    uint64_t n = 0;
    while (s && s[n]) n++;
    return n;
}

void bootlog_init(void *info) {
    g_boot_info = (SageOSBootInfo *)info;
    g_enabled = 1;
    if (g_boot_info && g_boot_info->boot_services_active) {
        g_log_offset = g_boot_info->log_offset;
    } else {
        g_log_offset = 0;
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
