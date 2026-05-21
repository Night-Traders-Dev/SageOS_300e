#ifndef _SAGEOS_PHYS_ALLOC_H
#define _SAGEOS_PHYS_ALLOC_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096

void phys_init(void *memory_map, uint64_t map_size);
void* phys_alloc(void);
void phys_free(void *addr);

#endif
