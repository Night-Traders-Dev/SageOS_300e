#ifndef SAGEOS_SWAP_H
#define SAGEOS_SWAP_H

#include <stdint.h>

/*
 * Minimal SWAP support for SageOS
 * 
 * Identifies swap partitions and provides an interface for future
 * paging operations.
 */

typedef struct {
    uint32_t partition_lba;
    uint64_t size_bytes;
    int active;
} SwapDevice;

int swap_init(void);
int swap_is_available(void);
void swap_info(void);

#endif
