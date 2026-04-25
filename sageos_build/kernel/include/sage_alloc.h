#ifndef SAGEOS_SAGE_ALLOC_H
#define SAGEOS_SAGE_ALLOC_H

/*
 * sage_alloc.h — Bump allocator for kernel-resident SageLang
 *
 * Provides a simple arena allocator that backs malloc/free/realloc
 * for the SageLang lexer, parser, and interpreter running inside
 * the SageOS kernel.
 *
 * The arena is reset between REPL lines so memory does not leak
 * across evaluations.
 */

#include <stddef.h>
#include <stdint.h>

/* Arena size: 1 MB */
#define SAGE_ARENA_SIZE  (1024 * 1024)

/* Bump allocator */
void *sage_malloc(size_t size);
void *sage_calloc(size_t count, size_t size);
void *sage_realloc(void *ptr, size_t old_size, size_t new_size);
void  sage_free(void *ptr);
char *sage_strdup(const char *s);

/* Reset the arena — call between REPL iterations */
void sage_arena_reset(void);

/* Query arena usage */
size_t sage_arena_used(void);

#endif /* SAGEOS_SAGE_ALLOC_H */
