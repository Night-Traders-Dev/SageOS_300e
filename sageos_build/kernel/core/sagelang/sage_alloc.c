#include <stdint.h>
#include <stddef.h>
#include "sage_alloc.h"
#include "console.h"
#include "sage_libc_shim.h"
#include "telemetry.h"
#include "scheduler.h"

/* 
 * sage_alloc.c — Explicit Free List Allocator with Coalescing
 */

static uint8_t sage_heap[SAGE_ARENA_SIZE] __attribute__((aligned(16)));
static size_t sage_bump = 0;
static int g_alloc_lock = 0;

typedef struct FreeBlock {
    size_t size;            /* Total size including this header */
    struct FreeBlock* next;
} FreeBlock;

static FreeBlock* g_free_list = NULL;
static alloc_stats_t g_alloc_stats[ALLOC_TAG_MAX];

#define ALLOC_HEADER_SIZE 16
#define ALIGNMENT 16
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(size_t)(ALIGNMENT - 1))

static const char *g_tag_names[] = {
    "kernel", "vm", "vfs", "ipc", "parser", "shell", "other"
};

static void lock_alloc() {
    while (__sync_lock_test_and_set(&g_alloc_lock, 1)) {
        extern void sched_yield(void);
        sched_yield();
    }
}

static void unlock_alloc() {
    __sync_lock_release(&g_alloc_lock);
}

void *sage_malloc_tagged(size_t size, alloc_tag_t tag) {
    if (size == 0) return NULL;
    lock_alloc();

    size_t raw_size = size;
    size_t needed = ALIGN(size + ALLOC_HEADER_SIZE);
    
    FreeBlock** prev = &g_free_list;
    FreeBlock* curr = g_free_list;
    
    while (curr) {
        if (curr->size >= needed) {
            if (curr->size >= needed + sizeof(FreeBlock) + ALIGNMENT) {
                FreeBlock* next_block = (FreeBlock*)((uint8_t*)curr + needed);
                next_block->size = curr->size - needed;
                next_block->next = curr->next;
                *prev = next_block;
            } else {
                *prev = curr->next;
                needed = curr->size;
            }
            
            uint8_t* header = (uint8_t*)curr;
            *(size_t*)header = needed - ALLOC_HEADER_SIZE;
            *(uint32_t*)(header + 8) = (uint32_t)tag;
            
            void* user_ptr = (void*)(header + ALLOC_HEADER_SIZE);
            sage_memset(user_ptr, 0, needed - ALLOC_HEADER_SIZE);
            
            if ((unsigned)tag < ALLOC_TAG_MAX) {
                g_alloc_stats[tag].alloc_count++;
                g_alloc_stats[tag].bytes_total += raw_size;
            }
            unlock_alloc();
            return user_ptr;
        }
        prev = &curr->next;
        curr = curr->next;
    }

    if (sage_bump + needed > SAGE_ARENA_SIZE) {
        console_write("\nsage: out of memory (requested: ");
        console_u32((uint32_t)raw_size);
        console_write(" bytes, tag: ");
        if ((unsigned)tag < ALLOC_TAG_MAX) console_write(g_tag_names[tag]);
        console_write(")\n");
        unlock_alloc();
        return NULL;
    }
    
    uint8_t *header = &sage_heap[sage_bump];
    *(size_t *)header = needed - ALLOC_HEADER_SIZE;
    *(uint32_t *)(header + 8) = (uint32_t)tag;
    sage_bump += needed;
    
    void *ptr = (void *)(header + ALLOC_HEADER_SIZE);
    sage_memset(ptr, 0, needed - ALLOC_HEADER_SIZE);
    
    if ((unsigned)tag < ALLOC_TAG_MAX) {
        g_alloc_stats[tag].alloc_count++;
        g_alloc_stats[tag].bytes_total += raw_size;
    }

    if (tag == ALLOC_TAG_PARSER) {
        thread_t *curr_t = sched_current_thread();
        console_write("[DEBUG_ALLOC_PARSER] thread=");
        if (curr_t) {
            console_write(curr_t->name);
        } else {
            console_write("none");
        }
        console_write(" ptr=");
        console_hex64((uint64_t)ptr);
        console_write(" size=");
        console_u64((uint64_t)needed - ALLOC_HEADER_SIZE);
        console_write("\n");
    }

    trace_log(TRACE_ALLOC_MALLOC, (uint64_t)raw_size, (uint64_t)ptr);
    unlock_alloc();
    return ptr;
}

void *sage_calloc_tagged(size_t count, size_t size, alloc_tag_t tag) {
    return sage_malloc_tagged(count * size, tag);
}

void sage_free(void *ptr) {
    if (!ptr) return;
    trace_log(TRACE_ALLOC_FREE, (uint64_t)ptr, 0);

    uint8_t *header = (uint8_t *)ptr - ALLOC_HEADER_SIZE;
    size_t user_size = *(size_t *)header;
    alloc_tag_t tag = (alloc_tag_t)(*(uint32_t *)(header + 8));

    if (tag == ALLOC_TAG_PARSER) {
        thread_t *curr_t = sched_current_thread();
        console_write("[DEBUG_FREE_PARSER] thread=");
        if (curr_t) {
            console_write(curr_t->name);
        } else {
            console_write("none");
        }
        console_write(" ptr=");
        console_hex64((uint64_t)ptr);
        console_write(" size=");
        console_u64((uint64_t)user_size);
        console_write(" caller=");
        console_hex64((uint64_t)__builtin_return_address(0));
        console_write("\n");
    }

    size_t total_size = ALIGN(user_size + ALLOC_HEADER_SIZE);

    lock_alloc();
    FreeBlock* new_block = (FreeBlock*)header;
    new_block->size = total_size;

    /* Insert into free list maintained in address order for coalescing */
    FreeBlock** prev = &g_free_list;
    FreeBlock* curr = g_free_list;
    while (curr && curr < new_block) {
        prev = &curr->next;
        curr = curr->next;
    }

    new_block->next = curr;
    *prev = new_block;

    /* Coalesce with next */
    if (new_block->next && (uint8_t*)new_block + new_block->size == (uint8_t*)new_block->next) {
        new_block->size += new_block->next->size;
        new_block->next = new_block->next->next;
    }

    /* Coalesce with previous */
    if (prev != &g_free_list) {
        /* This is tricky without a pointer to the previous block's structure start. 
           But since we inserted in order, we can find the previous block. */
        /* Actually, 'prev' is a pointer to the 'next' field of the previous block. */
        /* To find the previous block start, we need to know its header address. */
        /* Let's re-scan from start to find the block that points to new_block. */
        FreeBlock* p = g_free_list;
        while (p && p->next != new_block) p = p->next;
        if (p && (uint8_t*)p + p->size == (uint8_t*)new_block) {
            p->size += new_block->size;
            p->next = new_block->next;
        }
    }

    unlock_alloc();
}

void *sage_realloc(void *ptr, size_t new_size) {
    if (!ptr) return sage_malloc(new_size);
    if (new_size == 0) { sage_free(ptr); return NULL; }
    
    uint8_t *header = (uint8_t *)ptr - ALLOC_HEADER_SIZE;
    size_t old_user_size = *(size_t *)header;
    alloc_tag_t tag = (alloc_tag_t)(*(uint32_t *)(header + 8));
    if (new_size <= old_user_size) return ptr;

    void *np = sage_malloc_tagged(new_size, tag);
    if (!np) return NULL;
    sage_memcpy(np, ptr, old_user_size);
    sage_free(ptr);
    return np;
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
    lock_alloc();
    sage_bump = 0;
    g_free_list = NULL;
    for (int i = 0; i < ALLOC_TAG_MAX; i++) {
        g_alloc_stats[i].alloc_count = 0;
        g_alloc_stats[i].bytes_total = 0;
    }
    unlock_alloc();
}

size_t sage_arena_used(void) { return sage_bump; }

alloc_stats_t sage_alloc_stats(alloc_tag_t tag) {
    if ((unsigned)tag >= ALLOC_TAG_MAX) { alloc_stats_t e = {0, 0}; return e; }
    return g_alloc_stats[tag];
}

const char *sage_alloc_tag_name(alloc_tag_t tag) {
    if ((unsigned)tag >= ALLOC_TAG_MAX) return "unknown";
    return g_tag_names[tag];
}

void *sage_malloc(size_t size) { return sage_malloc_tagged(size, ALLOC_TAG_KERNEL); }
void *sage_calloc(size_t count, size_t size) { return sage_calloc_tagged(count, size, ALLOC_TAG_KERNEL); }
