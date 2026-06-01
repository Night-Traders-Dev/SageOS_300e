#ifndef SAGE_PORT_H
#define SAGE_PORT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

// Missing macros/types
#ifndef PATH_MAX
#define PATH_MAX 256
#endif
#define INT_MAX  2147483647

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#define EEXIST 17

#ifndef PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_MUTEX_INITIALIZER 0
#endif

// Types for threads (if not defined by shim)
#ifndef _PTHREAD_H
typedef int pthread_t;
typedef int pthread_mutex_t;
typedef int pthread_cond_t;
typedef int pthread_rwlock_t;
typedef int pthread_attr_t;
#endif

#ifndef _TIMESPEC_DEFINED
#define _TIMESPEC_DEFINED
struct timespec {
    long tv_sec;
    long tv_nsec;
};
#endif

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

// Safe stubs to avoid unused parameter warnings
static inline char* getcwd_stub(char* b, size_t s) { (void)b; (void)s; return NULL; }
static inline int chdir_stub(const char* p) { (void)p; return -1; }
static inline int mkdir_stub(const char* p, mode_t m) { (void)p; (void)m; return -1; }
static inline int unlink_stub(const char* p) { (void)p; return -1; }
static inline int access_stub(const char* p, int m) { (void)p; (void)m; return -1; }
static inline int fork_stub(void) { return -1; }
static inline int waitpid_stub(int p, int* s, int o) { (void)p; if(s) *s = 0; (void)o; return -1; }
static inline int execlp_stub(const char* f, ...) { (void)f; return -1; }
static inline int execvp_stub(const char* f, char* const a[]) { (void)f; (void)a; return -1; }
static inline void _exit_stub(int s) { (void)s; for(;;); }
static inline int mkstemps_stub(char* p, int s) { (void)p; (void)s; return -1; }
static inline int mkstemp_stub(char* p) { (void)p; return -1; }
static inline int clock_gettime_stub(int c, struct timespec* t) { (void)c; if(t) { t->tv_sec = 0; t->tv_nsec = 0; } return 0; }
static inline int setenv_stub(const char* n, const char* v, int o) { (void)n; (void)v; (void)o; return -1; }
static inline char* realpath_stub(const char* p, char* r) {
    extern void* sage_malloc(size_t);
    extern char* sage_strncpy(char*, const char*, size_t);
    if (!r) r = sage_malloc(256);
    sage_strncpy(r, p, 255);
    r[255] = '\0';
    return r;
}

#ifndef __sageos__
#define getcwd getcwd_stub
#define chdir chdir_stub
#define mkdir mkdir_stub
#define unlink unlink_stub
#define access access_stub
#define fork fork_stub
#define waitpid waitpid_stub
#define execlp execlp_stub
#define execvp execvp_stub
#define _exit _exit_stub
#define mkstemps mkstemps_stub
#define mkstemp mkstemp_stub
#define clock_gettime clock_gettime_stub
#define setenv setenv_stub
#define realpath realpath_stub
#endif

#ifdef __sageos__
#include "sage_alloc.h"
#undef malloc
#undef calloc
#define malloc        sage_malloc_parser
#define calloc        sage_calloc_parser
#endif

#endif
