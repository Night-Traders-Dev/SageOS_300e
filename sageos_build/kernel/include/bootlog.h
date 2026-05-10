#ifndef SAGEOS_BOOTLOG_H
#define SAGEOS_BOOTLOG_H

#include "bootinfo.h"

/*
 * bootlog — persistent USB boot log
 *
 * The UEFI loader opens BOOTLOG.TXT on the ESP (the boot USB) and passes
 * the EFI_FILE_PROTOCOL* in SageOSBootInfo.log_file.  Because boot services
 * remain active in firmware-input mode, the kernel can keep appending to the
 * same file handle throughout the entire init pipeline.
 *
 * Usage:
 *   bootlog_init(info);           // call first in kmain
 *   bootlog("[KRN] step done\n"); // append a message
 *   bootlog_hex("[KRN] val: ", 0xDEADBEEF); // append label + hex
 *   bootlog_close();              // optional: flush before scheduler
 */

void bootlog_init(SageOSBootInfo *info);
void bootlog(const char *msg);
void bootlog_hex(const char *label, uint64_t value);
void bootlog_close(void);

#endif
