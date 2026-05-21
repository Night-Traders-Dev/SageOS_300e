#ifndef _SAGEOS_VMM_H
#define _SAGEOS_VMM_H

#include <stdint.h>

typedef uint64_t pte_t;

#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_USER (1ULL << 2)

void vmm_init(void);
void vmm_map(uint64_t vaddr, uint64_t paddr, uint64_t flags);

#endif
