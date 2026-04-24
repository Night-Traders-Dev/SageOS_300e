#ifndef SAGEOS_CONSOLE_H
#define SAGEOS_CONSOLE_H

#include <stdint.h>
#include <stddef.h>
#include "bootinfo.h"

void console_init(SageOSBootInfo *info);
void console_clear(void);
void console_putc(char c);
void console_write(const char *s);
void console_write_n(const char *s, size_t n);
void console_hex64(uint64_t v);
void console_u32(uint32_t v);
void console_set_fg(uint32_t rgb);
uint32_t console_get_fg(void);
int console_has_fb(void);
SageOSBootInfo *console_boot_info(void);

#endif
