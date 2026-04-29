#pragma once
#include <stddef.h>

void dmesg_log(const char *msg);
void dmesg_dump(void);
void dmesg_save_persistent(void);
void dmesg_load_persistent(void);
uint32_t dmesg_get_total(void);
uint32_t dmesg_get_head(void);
uint32_t dmesg_get_size(void);
char dmesg_get_char(uint32_t index);
