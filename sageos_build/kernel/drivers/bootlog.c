/*
 * bootlog.c — kernel-side persistent USB boot log
 *
 * Appends ASCII text to BOOTLOG.TXT on the ESP via the EFI_FILE_PROTOCOL*
 * handed off from the UEFI loader in SageOSBootInfo.log_file.
 *
 * Boot services remain active in firmware-input mode (the default for the
 * Lenovo 300e), so the EFI file handle is valid throughout kernel init and
 * into the shell.  Every bootlog() call flushes immediately so data is
 * preserved even if the kernel hangs a millisecond later.
 *
 * If log_file == 0 (ExitBootServices was called, or file open failed) all
 * calls are silent no-ops — the code path is always safe.
 */

#include <stdint.h>
#include <stddef.h>
#include "bootlog.h"
#include "bootinfo.h"

#if defined(__clang__) || defined(__GNUC__)
#define EFIAPI __attribute__((ms_abi))
#else
#define EFIAPI
#endif

/* ---------------------------------------------------------------------- */
/* Minimal EFI file protocol re-declaration (MS ABI, no EFI headers)      */
/* ---------------------------------------------------------------------- */

typedef uint64_t EFI_STATUS;
typedef uint64_t UINTN;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_FILE_WRITE_FN)(
    EFI_FILE_PROTOCOL *self,
    UINTN             *buffer_size,
    void              *buffer
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_POSITION_FN)(
    EFI_FILE_PROTOCOL *self,
    uint64_t           position
);

typedef EFI_STATUS (EFIAPI *EFI_FILE_FLUSH_FN)(
    EFI_FILE_PROTOCOL *self
);

/*
 * We only need the vtable slots we actually call.
 * The EFI spec layout (zero-indexed from slot 0):
 *   0  Revision (UINT64, not a fn ptr)
 *   1  Open
 *   2  Close
 *   3  Delete
 *   4  Read
 *   5  Write       ← slot 5
 *   6  GetPosition
 *   7  SetPosition  ← slot 7
 *   8  GetInfo
 *   9  SetInfo
 *  10  Flush        ← slot 10
 *
 * We access them via a raw pointer array to avoid including the full struct
 * definition (which is already in uefi_loader.c under the MS ABI target).
 */
#define EFI_FILE_VTABLE_WRITE        5
#define EFI_FILE_VTABLE_SET_POSITION 7
#define EFI_FILE_VTABLE_FLUSH        10

/* ---------------------------------------------------------------------- */
/* Module state                                                            */
/* ---------------------------------------------------------------------- */

static EFI_FILE_PROTOCOL *g_log_file   = 0;
static uint64_t           g_log_offset = 0;

/* ---------------------------------------------------------------------- */
/* Helpers                                                                 */
/* ---------------------------------------------------------------------- */

static void bl_write_raw(const char *buf, uint64_t len) {
    if (!g_log_file || len == 0) return;

    /* Access vtable entries via raw pointer array.
     * The first entry in EFI_FILE_PROTOCOL is Revision (a UINT64, not a
     * function pointer), so vtable function pointers start at byte offset 8.
     * We cast to an array of void* starting at the struct base. */
    void **vtable = (void **)g_log_file;

    /* SetPosition to our current offset */
    EFI_FILE_SET_POSITION_FN set_pos =
        (EFI_FILE_SET_POSITION_FN)vtable[EFI_FILE_VTABLE_SET_POSITION];
    if (set_pos) {
        set_pos(g_log_file, g_log_offset);
    }

    /* Write */
    EFI_FILE_WRITE_FN write_fn =
        (EFI_FILE_WRITE_FN)vtable[EFI_FILE_VTABLE_WRITE];
    if (!write_fn) return;

    UINTN written = (UINTN)len;
    write_fn(g_log_file, &written, (void *)buf);
    g_log_offset += written;

    /* Flush so the FAT directory entry is updated before we crash */
    EFI_FILE_FLUSH_FN flush_fn =
        (EFI_FILE_FLUSH_FN)vtable[EFI_FILE_VTABLE_FLUSH];
    if (flush_fn) {
        flush_fn(g_log_file);
    }
}

static uint64_t bl_strlen(const char *s) {
    uint64_t n = 0;
    while (s && s[n]) n++;
    return n;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */
/* ---------------------------------------------------------------------- */

void bootlog_init(SageOSBootInfo *info) {
    if (!info || !info->log_file) {
        g_log_file   = 0;
        g_log_offset = 0;
        return;
    }
    g_log_file   = (EFI_FILE_PROTOCOL *)(uintptr_t)info->log_file;
    g_log_offset = info->log_offset;
}

void bootlog(const char *msg) {
    if (!g_log_file || !msg) return;
    bl_write_raw(msg, bl_strlen(msg));
    /* Sync offset back for next call */
    /* (already updated inside bl_write_raw) */
}

void bootlog_hex(const char *label, uint64_t value) {
    if (!g_log_file) return;
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
    /* Nothing to close — the EFI handle stays open for the life of the
     * kernel since we need boot services active to flush it.  The OS
     * reboot/shutdown path will reset the firmware anyway. */
    g_log_file   = 0;
    g_log_offset = 0;
}
