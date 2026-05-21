#include "phys_alloc.h"
#include "dmesg.h"

#define MAX_PHYS_PAGES 1048576 /* 4GB support */
static uint8_t bitmap[MAX_PHYS_PAGES / 8];

static void mark_used(uint64_t page) {
    bitmap[page / 8] |= (1 << (page % 8));
}

static void mark_free(uint64_t page) {
    bitmap[page / 8] &= ~(1 << (page % 8));
}

static int is_used(uint64_t page) {
    return bitmap[page / 8] & (1 << (page % 8));
}

void phys_init(void *memory_map, uint64_t map_size) {
    /* For now, just mark the first 1MB as used (BIOS/UEFI legacy) */
    for (uint64_t i = 0; i < 256; i++) {
        mark_used(i);
    }
    dmesg_log("phys_alloc: initialized");
}

void* phys_alloc(void) {
    for (uint64_t i = 0; i < MAX_PHYS_PAGES; i++) {
        if (!is_used(i)) {
            mark_used(i);
            return (void*)(i * PAGE_SIZE);
        }
    }
    return NULL;
}

void phys_free(void *addr) {
    uint64_t page = (uint64_t)addr / PAGE_SIZE;
    mark_free(page);
}
