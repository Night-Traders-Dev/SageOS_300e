#ifndef SAGEOS_SAGE_LIBC_SHIM_H
#define SAGEOS_SAGE_LIBC_SHIM_H

/*
 * sage_libc_shim.h — Macro overrides redirecting libc to kernel functions
 *
 * Include this BEFORE any SageLang source that uses libc.
 * All malloc/free/string/stdio calls are redirected to the kernel
 * bump allocator and console_write.
 */

#include <stdint.h>
#include <stddef.h>

#include "sage_alloc.h"

/* Memory allocation */
#define malloc(s)        sage_malloc(s)
#define free(p)          sage_free(p)
#define realloc(p,s)     sage_realloc(p, 0, s)
#define calloc(n,s)      sage_calloc(n, s)
#define strdup(s)        sage_strdup(s)

/* String functions */
size_t sage_strlen(const char *s);
int    sage_strcmp(const char *a, const char *b);
int    sage_strncmp(const char *a, const char *b, size_t n);
char  *sage_strcpy(char *dest, const char *src);
char  *sage_strncpy(char *dest, const char *src, size_t n);
char  *sage_strchr(const char *s, int c);
char  *sage_strstr(const char *h, const char *n);

#define strlen(s)        sage_strlen(s)
#define strcmp(a,b)       sage_strcmp(a,b)
#define strncmp(a,b,n)   sage_strncmp(a,b,n)
#define strcpy(d,s)      sage_strcpy(d,s)
#define strncpy(d,s,n)   sage_strncpy(d,s,n)
#define strchr(s,c)      sage_strchr(s,c)
#define strstr(h,n)      sage_strstr(h,n)

/* Memory ops */
void *sage_memset(void *s, int c, size_t n);
void *sage_memcpy(void *d, const void *s, size_t n);
void *sage_memmove(void *d, const void *s, size_t n);
int   sage_memcmp(const void *a, const void *b, size_t n);

#define memset(d,c,n)    sage_memset(d,c,n)
#define memcpy(d,s,n)    sage_memcpy(d,s,n)
#define memmove(d,s,n)   sage_memmove(d,s,n)
#define memcmp(a,b,n)    sage_memcmp(a,b,n)

/* I/O — printf family */
void sage_printf(const char *fmt, ...);
int  sage_snprintf(char *buf, size_t n, const char *fmt, ...);

#define printf(...)       sage_printf(__VA_ARGS__)
#define fprintf(f,...)    sage_printf(__VA_ARGS__)
#define sprintf(b,...)    sage_printf(__VA_ARGS__)
#define snprintf(b,n,...) sage_snprintf(b,n,__VA_ARGS__)

/* Control flow */
extern volatile int sage_exit_flag;
extern int sage_exit_code;
void sage_exit(int code);

#define exit(c)          sage_exit(c)
#define abort()          sage_exit(1)

/* Math */
double sage_fmod(double x, double y);
double sage_fabs(double x);
double sage_floor(double x);
double sage_ceil(double x);
double sage_pow(double b, double e);
double sage_sqrt(double x);
double sage_strtod(const char *s, char **end);
int    sage_atoi(const char *s);
int    sage_isdigit(int c);
int    sage_isalpha(int c);
int    sage_isalnum(int c);
int    sage_isspace(int c);

#define fmod(x,y)        sage_fmod(x,y)
#define fabs(x)          sage_fabs(x)
#define floor(x)         sage_floor(x)
#define ceil(x)          sage_ceil(x)
#define pow(b,e)         sage_pow(b,e)
#define sqrt(x)          sage_sqrt(x)
#define strtod(s,e)      sage_strtod(s,e)
#define atoi(s)          sage_atoi(s)
#define isdigit(c)       sage_isdigit(c)
#define isalpha(c)       sage_isalpha(c)
#define isalnum(c)       sage_isalnum(c)
#define isspace(c)       sage_isspace(c)

/* Suppress headers that would conflict */
#define _STDIO_H 1
#define _STDLIB_H 1
#define _STRING_H 1
#define _MATH_H 1
#define _CTYPE_H 1
#define _SETJMP_H 1

/* Stub jmp_buf for repl.h — no real longjmp in kernel */
typedef int jmp_buf[1];
#define setjmp(b) 0
#define longjmp(b,v) sage_exit(v)

/* Stub FILE for any fprintf references */
typedef void FILE;
#define stderr ((FILE*)0)
#define stdout ((FILE*)0)
#define stdin  ((FILE*)0)
#define fflush(f) ((void)0)

/* NULL */
#ifndef NULL
#define NULL ((void*)0)
#endif

#endif /* SAGEOS_SAGE_LIBC_SHIM_H */
