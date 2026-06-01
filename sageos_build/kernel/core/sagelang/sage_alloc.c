#include <stdint.h>
#include <stddef.h>
#include "sage_alloc.h"
#include "console.h"
#include "sage_libc_shim.h"
#include "telemetry.h"

/* 
 * sage_alloc.c — Tagged bump allocator for SageOS SageLang
 *
 * Implements a robust bump allocator with per-subsystem tagging.
 * Each allocation header stores the size and tag, enabling
 * per-subsystem memory accounting without overhead on the hot path.
 */

static uint8_t sage_heap[SAGE_ARENA_SIZE] __attribute__((aligned(16)));
static size_t sage_bump = 0;
static int g_alloc_lock = 0;

void *sage_malloc_tagged(size_t size, alloc_tag_t tag) {
    if (size == 0) return NULL;
    
    while (__sync_lock_test_and_set(&g_alloc_lock, 1)) {
        /* Spin or yield. In a cooperative kernel, we must yield. */
        extern void sched_yield(void);
        sched_yield();
    }

    size_t raw_size = size;
    /* Align to 16 bytes */
    size = (size + 15) & ~(size_t)15;
    
    if (sage_bump + size + ALLOC_HEADER_SIZE > SAGE_ARENA_SIZE) {
        console_write("\nsage: out of memory (requested: ");
        console_u32((uint32_t)raw_size);
        console_write(" bytes, tag: ");
        if ((unsigned)tag < ALLOC_TAG_MAX) {
            console_write(g_tag_names[tag]);
        }
        console_write(")\n");
        __sync_lock_release(&g_alloc_lock);
        return NULL;
    }
    
    /* Write header */
    uint8_t *header = &sage_heap[sage_bump];
    *(size_t *)header = size;
    *(uint32_t *)(header + 8) = (uint32_t)tag;
    sage_bump += size + ALLOC_HEADER_SIZE;
    
    void *ptr = (void *)(header + ALLOC_HEADER_SIZE);
    sage_memset(ptr, 0, size);
    
    /* Update per-tag statistics */
    if ((unsigned)tag < ALLOC_TAG_MAX) {
        g_alloc_stats[tag].alloc_count++;
        g_alloc_stats[tag].bytes_total += raw_size;
    }
    
    trace_log(TRACE_ALLOC_MALLOC, (uint64_t)raw_size, (uint64_t)ptr);
    
    __sync_lock_release(&g_alloc_lock);
    return ptr;
}

void *sage_calloc_tagged(size_t count, size_t size, alloc_tag_t tag) {
    return sage_malloc_tagged(count * size, tag);
}

void *sage_realloc(void *ptr, size_t new_size) {
    if (!ptr) return sage_malloc(new_size);
    if (new_size == 0) return NULL;
    
    uint8_t *header = (uint8_t *)ptr - ALLOC_HEADER_SIZE;
    size_t old_size = *(size_t *)header;
    alloc_tag_t tag = (alloc_tag_t)(*(uint32_t *)(header + 8));
    
    if (new_size <= old_size) return ptr;

    /* Last allocation optimization */
    if ((uint8_t *)ptr + old_size == &sage_heap[sage_bump]) {
        size_t needed = (new_size + 15) & ~(size_t)15;
        size_t extra = needed - old_size;
        if (sage_bump + extra <= SAGE_ARENA_SIZE) {
            *(size_t *)header = needed;
            sage_bump += extra;
            return ptr;
        }
    }
    
    void *np = sage_malloc_tagged(new_size, tag);
    if (!np) return NULL;
    
    sage_memcpy(np, ptr, old_size);
    return np;
}

void sage_free(void *ptr) {
    trace_log(TRACE_ALLOC_FREE, (uint64_t)ptr, 0);
    (void)ptr;
}

char *sage_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = sage_strlen(s);
    char *d = (char *)sage_malloc(len + 1);
    if (!d) return NULL;
    sage_memcpy(d, s, len + 1);
    return d;
}

void sage_arena_reset(void) {
    sage_bump = 0;
    /* Reset per-tag stats on arena reset */
    for (int i = 0; i < ALLOC_TAG_MAX; i++) {
        g_alloc_stats[i].alloc_count = 0;
        g_alloc_stats[i].bytes_total = 0;
    }
}

size_t sage_arena_used(void) {
    return sage_bump;
}

alloc_stats_t sage_alloc_stats(alloc_tag_t tag) {
    if ((unsigned)tag >= ALLOC_TAG_MAX) {
        alloc_stats_t empty = {0, 0};
        return empty;
    }
    return g_alloc_stats[tag];
}

const char *sage_alloc_tag_name(alloc_tag_t tag) {
    if ((unsigned)tag >= ALLOC_TAG_MAX) return "unknown";
    return g_tag_names[tag];
}

void *sage_malloc(size_t size) {
    return sage_malloc_tagged(size, ALLOC_TAG_KERNEL);
}

void *sage_calloc(size_t count, size_t size) {
    return sage_calloc_tagged(count, size, ALLOC_TAG_KERNEL);
}
