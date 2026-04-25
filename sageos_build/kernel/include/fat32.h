#ifndef SAGEOS_FAT32_H
#define SAGEOS_FAT32_H

#include <stdint.h>

int fat32_init(void);
int fat32_is_available(void);
void fat32_ls(void);
int fat32_cat(const char *path);

#endif
