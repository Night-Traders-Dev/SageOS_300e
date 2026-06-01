#ifndef SAGEOS_SAGE_ALLOC_H
#define SAGEOS_SAGE_ALLOC_H

/*
 * sage_alloc.h — Tagged bump allocator for kernel-resident SageLang
 *
 * Provides a simple arena allocator that backs malloc/free/realloc
 * for the SageLang lexer, parser, and interpreter running inside
 * the SageOS kernel.
 *
 * Each allocation is tagged with a subsystem identifier, enabling
 * per-subsystem memory accounting via sage_alloc_stats().
 *
 * The arena is reset between REPL lines so memory does not leak
 * across evaluations.
 */

#include <stddef.h>
#include <stdint.h>

/* Arena size: 8 MB */
#define SAGE_ARENA_SIZE  (32 * 1024 * 1024)

/* Subsystem allocation tags */
typedef enum {
    ALLOC_TAG_KERNEL = 0,   /* Default / general kernel allocations */
    ALLOC_TAG_VM,           /* MetalVM / SGVM runtime */
    ALLOC_TAG_VFS,          /* VFS and filesystem operations */
    ALLOC_TAG_IPC,          /* IPC subsystem (endpoints, messages) */
    ALLOC_TAG_PARSER,       /* SageLang parser, AST, interpreter */
    ALLOC_TAG_SHELL,        /* Shell entry and command processing */
    ALLOC_TAG_OTHER,        /* Uncategorized */
    ALLOC_TAG_MAX
} alloc_tag_t;

/* Per-tag statistics */
typedef struct {
    uint64_t alloc_count;   /* Number of allocations */
    uint64_t bytes_total;   /* Total bytes allocated */
} alloc_stats_t;

/* Tagged bump allocator */
void *sage_malloc(size_t size);
void *sage_calloc(size_t count, size_t size);
void *sage_malloc_tagged(size_t size, alloc_tag_t tag);
void *sage_calloc_tagged(size_t count, size_t size, alloc_tag_t tag);
void *sage_realloc(void *ptr, size_t new_size);
void  sage_free(void *ptr);
char *sage_strdup(const char *s);

/* Query per-subsystem allocation statistics */
alloc_stats_t sage_alloc_stats(alloc_tag_t tag);

/* Total arena usage */
size_t sage_arena_used(void);

/* Reset the arena — call between REPL iterations */
void sage_arena_reset(void);

/* Get tag name for display */
const char *sage_alloc_tag_name(alloc_tag_t tag);

static inline void *sage_malloc_parser(size_t size) {
    return sage_malloc_tagged(size, ALLOC_TAG_PARSER);
}

static inline void *sage_calloc_parser(size_t count, size_t size) {
    return sage_calloc_tagged(count, size, ALLOC_TAG_PARSER);
}

#endif /* SAGEOS_SAGE_ALLOC_H */
