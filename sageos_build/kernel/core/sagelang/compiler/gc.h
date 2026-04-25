#ifndef SAGELANG_GC_SHIM_H
#define SAGELANG_GC_SHIM_H

#include "value.h"

// Minimal GC shim for kernel compiler
#define SAGE_ALLOC(s) sage_malloc(s)
#define SAGE_FREE(p)  sage_free(p)
#define SAGE_REALLOC(p, s) sage_realloc(p, s)
#define SAGE_STRDUP(s) sage_strdup(s)

static inline void* gc_alloc(int type, size_t size) {
    (void)type;
    return sage_malloc(size);
}

static inline void gc_free(void* p) { (void)p; }
static inline void gc_pin() {}
static inline void gc_unpin() {}
static inline void gc_track_external_allocation(size_t s) { (void)s; }
static inline void gc_track_external_resize(size_t o, size_t n) { (void)o; (void)n; }
static inline void gc_track_external_free(size_t s) { (void)s; }
static inline void GC_WRITE_BARRIER(Value v) { (void)v; }
static inline void GC_WRITE_BARRIER_ENV(void* e) { (void)e; }

#endif
