#pragma once
#include <stdint.h>
#include <stddef.h>
#include "bootinfo.h"

void     console_init(SageOSBootInfo *info);
void     console_clear(void);
void     console_putc(char c);
void     console_write(const char *s);
void     console_write_n(const char *s, size_t n);
void     console_hex64(uint64_t v);
void     console_u32(uint32_t v);
void     console_set_fg(uint32_t rgb);
uint32_t console_get_fg(void);
void     console_set_cursor(uint32_t row, uint32_t col);
void     console_get_cursor(uint32_t *row, uint32_t *col);
void     console_set_serial_echo(int enabled);
int      console_get_serial_echo(void);
void     console_draw_status_bar(const char *right_text);
void     console_serial_redraw_line(const char *line, uint32_t pos);
int      console_has_fb(void);
uint32_t console_cols(void);
uint32_t console_rows(void);
SageOSBootInfo *console_boot_info(void);
