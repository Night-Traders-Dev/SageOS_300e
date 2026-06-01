/*
 * sage_libc_shim.c — Freestanding libc for SageLang in kernel
 *
 * Provides string, memory, I/O and math functions backed by the kernel
 * console and bump allocator.
 */

#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "sage_alloc.h"
#include "sage_libc_shim.h"

void *sage_memset(void *s, int c, size_t n);
void *sage_memcpy(void *dest, const void *src, size_t n);

void* kernel_malloc(size_t size) { return sage_malloc(size); }
void* kernel_realloc(void* ptr, size_t size) { return sage_realloc(ptr, size); }
void* kernel_calloc(size_t n, size_t size) { return sage_calloc(n, size); }
void  kernel_free(void* ptr) { sage_free(ptr); }
char* kernel_strdup(const char* str) { return sage_strdup(str); }

void sage_arena_reset_shim(void) { sage_arena_reset(); }
size_t sage_arena_used_shim(void) { return sage_arena_used(); }

/* --- String functions --- */

size_t sage_strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

int sage_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

int sage_strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(uint8_t)a[i] - (int)(uint8_t)b[i];
        if (a[i] == 0) return 0;
    }
    return 0;
}

char *sage_strcpy(char *dest, const char *src) {
    char *d = dest; while (*src) *d++ = *src++; *d = '\0'; return dest;
}

char *sage_strcat(char *dest, const char *src) {
    char *d = dest; while (*d) d++; while (*src) *d++ = *src++; *d = '\0'; return dest;
}

char *sage_strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

char *sage_strchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char *)s; s++; }
    return c == 0 ? (char *)s : (char *)0;
}

char *sage_strstr(const char *h, const char *n) {
    if (!*n) return (char *)h;
    for (; *h; h++) {
        const char *a = h, *b = n;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*b) return (char *)h;
    }
    return (char *)0;
}

/* --- Memory functions --- */

void *sage_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s; for (size_t i = 0; i < n; i++) p[i] = (uint8_t)c; return s;
}

void *sage_memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest; const uint8_t *sr = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = sr[i]; return dest;
}

void *sage_memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest; const uint8_t *sr = (const uint8_t *)src;
    if (d < sr) for (size_t i = 0; i < n; i++) d[i] = sr[i];
    else if (d > sr) for (size_t i = n; i > 0; i--) d[i-1] = sr[i-1];
    return dest;
}

int sage_memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *a = (const uint8_t *)s1, *b = (const uint8_t *)s2;
    for (size_t i = 0; i < n; i++) { if (a[i] != b[i]) return (int)a[i] - (int)b[i]; }
    return 0;
}

/* --- Minimal printf --- */

static void put_uint(uint64_t v) {
    char buf[21]; int pos = 20; buf[pos] = 0;
    if (v == 0) { console_putc('0'); return; }
    while (v > 0) { buf[--pos] = '0' + (char)(v % 10); v /= 10; }
    console_write(&buf[pos]);
}

int sage_printf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    char buf[512];
    int n = sage_vsnprintf(buf, sizeof(buf), fmt, ap);
    console_write(buf);
    __builtin_va_end(ap);
    return n;
}

int sage_vsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap) {
    size_t pos = 0;
    while (*fmt) {
        if (*fmt != '%') {
            if (buf && pos + 1 < n) {
                buf[pos] = *fmt;
            }
            pos++;
            fmt++;
            continue;
        }
        fmt++;
        
        int width = 0;
        int precision = -1;
        int zero_pad = 0;
        
        while (*fmt == '0') { zero_pad = 1; fmt++; }
        if (*fmt == '*') {
            width = __builtin_va_arg(ap, int);
            if (width < 0) { width = -width; } // Left-justify not supported yet
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        }
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            if (*fmt == '*') {
                precision = __builtin_va_arg(ap, int);
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9') { precision = precision * 10 + (*fmt - '0'); fmt++; }
            }
        }
        while (*fmt == 'h' || *fmt == 'l' || *fmt == 'L' || *fmt == 'z' || *fmt == 'j' || *fmt == 't') fmt++;

        if (*fmt == 's') {
            const char *s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = 0;
            while (s[len] && (precision < 0 || len < precision)) len++;
            int pad = width > len ? width - len : 0;
            while (pad > 0) {
                if (buf && pos + 1 < n) buf[pos] = ' ';
                pos++; pad--;
            }
            for (int i = 0; i < len; i++) {
                if (buf && pos + 1 < n) buf[pos] = s[i];
                pos++;
            }
        } else if (*fmt == 'd' || *fmt == 'u' || *fmt == 'x' || *fmt == 'X' || *fmt == 'p') {
            uint64_t uv;
            int neg = 0;
            if (*fmt == 'd') {
                int64_t v = __builtin_va_arg(ap, int);
                if (v < 0) { neg = 1; uv = (uint64_t)(-v); } else { uv = (uint64_t)v; }
            } else if (*fmt == 'p') {
                uv = (uintptr_t)__builtin_va_arg(ap, void *);
                zero_pad = 1; width = 16;
            } else {
                uv = __builtin_va_arg(ap, unsigned int);
            }
            
            char tmp[32];
            int tp = 0;
            int base = (*fmt == 'x' || *fmt == 'X' || *fmt == 'p') ? 16 : 10;
            const char *digits = (*fmt == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
            
            if (uv == 0) tmp[tp++] = '0';
            while (uv > 0) { tmp[tp++] = digits[uv % base]; uv /= base; }
            
            int total_len = tp + neg;
            if (width > total_len) {
                int pad = width - total_len;
                if (neg && zero_pad) {
                    if (buf && pos + 1 < n) {
                        buf[pos] = '-';
                    }
                    pos++;
                    neg = 0;
                }
                while (pad-- > 0) {
                    if (buf && pos + 1 < n) {
                        buf[pos] = (char)(zero_pad ? '0' : ' ');
                    }
                    pos++;
                }
            }
            if (neg) {
                if (buf && pos + 1 < n) {
                    buf[pos] = '-';
                }
                pos++;
            }
            while (tp > 0) {
                if (buf && pos + 1 < n) {
                    buf[pos] = tmp[tp - 1];
                }
                pos++;
                tp--;
            }
        } else if (*fmt == 'c') {
            char c = (char)__builtin_va_arg(ap, int);
            if (buf && pos + 1 < n) {
                buf[pos] = c;
            }
            pos++;
        } else if (*fmt == '%') {
            if (buf && pos + 1 < n) {
                buf[pos] = '%';
            }
            pos++;
        }
        if (*fmt) fmt++;
    }
    if (buf && n > 0) {
        if (pos < n) {
            buf[pos] = '\0';
        } else {
            buf[n - 1] = '\0';
        }
    }
    return (int)pos;
}

int sage_snprintf(char *buf, size_t n, const char *fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    int res = sage_vsnprintf(buf, n, fmt, ap);
    __builtin_va_end(ap);
    return res;
}

int sage_sprintf(char *buf, const char *fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    int res = sage_vsnprintf(buf, 1024, fmt, ap);
    __builtin_va_end(ap);
    return res;
}

/* --- Control flow --- */
volatile int sage_exit_flag = 0;
int sage_exit_code = 0;
void sage_exit(int code) { sage_exit_flag = 1; sage_exit_code = code; }
void exit(int code) { sage_exit(code); }
void abort(void) { sage_exit(1); }

/* --- Integer math / char classification --- */

int sage_atoi(const char *s) { int r = 0, sg = 1; while (*s == ' ') s++; if (*s == '-') { sg = -1; s++; } while (*s >= '0' && *s <= '9') { r = r * 10 + (*s - '0'); s++; } return r * sg; }

int sage_isdigit(int c) { return c >= '0' && c <= '9'; }
int sage_isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int sage_isalnum(int c) { return sage_isdigit(c) || sage_isalpha(c); }
int sage_isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

double sage_strtod(const char *s, char **end) {
    long long res = 0, fact = 1; int neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') { res = res * 10 + (*s - '0'); s++; }
    if (*s == '.') { 
        s++; 
        while (*s >= '0' && *s <= '9') { 
            res = res * 10 + (*s - '0'); 
            fact *= 10; 
            s++; 
        } 
    }
    if (end) *end = (char *)s;
    
    double d_res = (double)res / (double)fact;
    if (neg) d_res = -d_res;
    
    return d_res;
}

long sage_strtol(const char *s, char **end, int base) {
    long res = 0; int neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    if (base == 0) {
        if (*s == '0') {
            if (*(s+1) == 'x' || *(s+1) == 'X') { base = 16; s += 2; }
            else if (*(s+1) == 'b' || *(s+1) == 'B') { base = 2; s += 2; }
            else base = 8;
        } else base = 10;
    } else if (base == 16 && *s == '0' && (*(s+1) == 'x' || *(s+1) == 'X')) s += 2;
    for (;; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'f') d = 10 + (*s - 'a');
        else if (*s >= 'A' && *s <= 'F') d = 10 + (*s - 'A');
        else break;
        if (d >= base) break;
        res = res * base + d;
    }
    if (end) *end = (char *)s;
    return neg ? -res : res;
}

int vfprintf(void* stream, const char* fmt, __builtin_va_list ap) {
    (void)stream;
    char buf[1024];
    int n = sage_vsnprintf(buf, sizeof(buf), fmt, ap);
    console_write(buf);
    return n;
}

int fputc(int c, void* stream) {
    (void)stream;
    console_putc((char)c);
    return c;
}

int putchar(int c) {
    console_putc((char)c);
    return c;
}

int clock_gettime(int clk_id, struct timespec *tp) {
    (void)clk_id;
    if (!tp) return -1;
    extern uint64_t timer_ticks(void);
    uint64_t ticks = timer_ticks();
    tp->tv_sec = (long)(ticks / 100);
    tp->tv_nsec = (long)((ticks % 100) * 10000000);
    return 0;
}

int readlink(const char* path, char* buf, size_t bufsiz) {
    (void)path; (void)buf; (void)bufsiz;
    return -1;
}

int mkdir(const char* path, uint32_t mode) {
    (void)mode;
    extern int vfs_mkdir(const char*);
    return vfs_mkdir(path);
}

int close(int fd) { (void)fd; return -1; }
int write(int fd, const void* buf, size_t count) { (void)fd; (void)buf; (void)count; return -1; }
int access(const char* path, int mode) { (void)path; (void)mode; return -1; }
int unlink(const char* path) { (void)path; return -1; }
int mkstemp(char* template) { (void)template; return -1; }
int mkstemps(char* template, int suffixlen) { (void)template; (void)suffixlen; return -1; }
void* fdopen(int fd, const char* mode) { (void)fd; (void)mode; return NULL; }

/* Dummy math functions for compiler linking */
uint64_t sage_fmod(uint64_t x, uint64_t y) { (void)x; (void)y; return 0; }
uint64_t sage_fabs(uint64_t x) { 
    return x & 0x7FFFFFFFFFFFFFFFULL;
}
uint64_t sage_floor(uint64_t x) { return x; }
uint64_t sage_ceil(uint64_t x) { return x; }
uint64_t sage_pow(uint64_t b, uint64_t e) { (void)b; (void)e; return 0; }
uint64_t sage_sqrt(uint64_t x) { (void)x; return 0; }

/* Linker aliases for standard symbols required by Metal VM */
#undef memset
#undef memcpy
#undef strlen
#undef strcmp
#undef memcmp
void *memset(void *s, int c, size_t n) __attribute__((alias("sage_memset")));
void *memcpy(void *dest, const void *src, size_t n) __attribute__((alias("sage_memcpy")));
size_t strlen(const char *s) __attribute__((alias("sage_strlen")));
int strcmp(const char *a, const char *b) __attribute__((alias("sage_strcmp")));
int memcmp(const void *s1, const void *s2, size_t n) __attribute__((alias("sage_memcmp")));
