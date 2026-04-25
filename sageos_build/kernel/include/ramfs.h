#ifndef SAGEOS_RAMFS_H
#define SAGEOS_RAMFS_H

#include <stdint.h>

const char *ramfs_find(const char *path);
uint64_t ramfs_find_size(const char *path, const char **out_data);
void ramfs_ls(void);

#endif
