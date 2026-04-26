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

/* --- Bump allocator implementation --- */

static uint8_t sage_heap[SAGE_ARENA_SIZE] __attribute__((aligned(16)));
static size_t sage_bump = 0;

void *sage_malloc(size_t size) {
    size = (size + 15) & ~(size_t)15;
    if (sage_bump + size + 16 > SAGE_ARENA_SIZE) {
        console_write("\nsage: out of memory");
        return (void *)0;
    }
    size_t *header = (size_t *)&sage_heap[sage_bump];
    *header = size;
    sage_bump += size + 16;
    void *ptr = (void *)(header + 2);
    sage_memset(ptr, 0, size); // Compiler likes zeroed memory
    return ptr;
}

void *sage_calloc(size_t count, size_t size) {
    return sage_malloc(count * size);
}

void *sage_realloc(void *ptr, size_t new_size) {
    if (!ptr) return sage_malloc(new_size);
    if (new_size == 0) return (void *)0;
    
    size_t *header = (size_t *)((uint8_t *)ptr - 16);
    size_t old_size = *header;
    
    if (new_size <= old_size) return ptr; // Already big enough
    
    void *np = sage_malloc(new_size);
    if (!np) return (void *)0;
    
    sage_memcpy(np, ptr, old_size);
    return np;
}

void sage_free(void *ptr) { (void)ptr; }

char *sage_strdup(const char *s) {
    if (!s) return (char *)0;
    size_t len = 0; while (s[len]) len++;
    char *d = (char *)sage_malloc(len + 1);
    if (!d) return (char *)0;
    for (size_t i = 0; i <= len; i++) d[i] = s[i];
    return d;
}

void sage_arena_reset(void) { sage_bump = 0; }
size_t sage_arena_used(void) { return sage_bump; }

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

void sage_printf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    while (*fmt) {
        if (*fmt != '%') { console_putc(*fmt++); continue; }
        fmt++;
        switch (*fmt) {
        case 's': { const char *s = __builtin_va_arg(ap, const char *); console_write(s ? s : "(null)"); break; }
        case 'd': { int v = __builtin_va_arg(ap, int); if (v < 0) { console_putc('-'); put_uint((uint64_t)(-(int64_t)v)); } else put_uint((uint64_t)v); break; }
        case 'u': { unsigned v = __builtin_va_arg(ap, unsigned); put_uint(v); break; }
        case 'c': { int c = __builtin_va_arg(ap, int); console_putc((char)c); break; }
        case '%': console_putc('%'); break;
        case 0: goto done;
        default: console_putc('%'); console_putc(*fmt); break;
        }
        fmt++;
    }
done:
    __builtin_va_end(ap);
}

int sage_snprintf(char *buf, size_t n, const char *fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap, fmt);
    size_t pos = 0;
    while (*fmt && pos + 1 < n) {
        if (*fmt != '%') { buf[pos++] = *fmt++; continue; }
        fmt++;
        if (*fmt == 's') { const char *s = __builtin_va_arg(ap, const char *); if (!s) s = "(null)"; while (*s && pos + 1 < n) buf[pos++] = *s++; }
        else if (*fmt == 'd') { int v = __builtin_va_arg(ap, int); char tmp[21]; int tp = 20; tmp[tp] = 0; int neg = v < 0; uint64_t uv = neg ? (uint64_t)(-(int64_t)v) : (uint64_t)v; if (uv == 0) tmp[--tp] = '0'; while (uv > 0) { tmp[--tp] = '0' + (char)(uv % 10); uv /= 10; } if (neg) tmp[--tp] = '-'; const char *r = &tmp[tp]; while (*r && pos + 1 < n) buf[pos++] = *r++; }
        else if (*fmt == '%') { buf[pos++] = '%'; }
        else { (void)__builtin_va_arg(ap, int); }
        fmt++;
    }
    if (n > 0) buf[pos] = '\0';
    __builtin_va_end(ap);
    return (int)pos;
}

/* --- Control flow --- */
volatile int sage_exit_flag = 0;
int sage_exit_code = 0;
void sage_exit(int code) { sage_exit_flag = 1; sage_exit_code = code; }

/* --- Integer math / char classification --- */

int sage_atoi(const char *s) { int r = 0, sg = 1; while (*s == ' ') s++; if (*s == '-') { sg = -1; s++; } while (*s >= '0' && *s <= '9') { r = r * 10 + (*s - '0'); s++; } return r * sg; }

int sage_isdigit(int c) { return c >= '0' && c <= '9'; }
int sage_isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int sage_isalnum(int c) { return sage_isdigit(c) || sage_isalpha(c); }
int sage_isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

uint64_t sage_strtod(const char *s, char **end) {
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
    
    // We still need to do the division and convert to double bits.
    // If I use double here, will it fail?
    // Let's try using a pointer.
    double d_res = (double)res / (double)fact;
    if (neg) d_res = -d_res;
    
    union { double d; uint64_t u; } conv;
    conv.d = d_res;
    return conv.u;
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

void vfprintf(void* stream, const char* fmt, __builtin_va_list ap) {
    (void)stream;
    while (*fmt) {
        if (*fmt != '%') { console_putc(*fmt++); continue; }
        fmt++;
        if (*fmt == 's') { const char *s = __builtin_va_arg(ap, const char *); console_write(s ? s : "(null)"); }
        else if (*fmt == 'd') { int v = __builtin_va_arg(ap, int); if (v < 0) { console_putc('-'); put_uint((uint64_t)(-(int64_t)v)); } else put_uint((uint64_t)v); }
        else if (*fmt == 'u') { unsigned v = __builtin_va_arg(ap, unsigned); put_uint(v); }
        else if (*fmt == 'c') { int c = __builtin_va_arg(ap, int); console_putc((char)c); }
        else if (*fmt == '.') { /* skip precision for now */ while (*fmt && (*fmt < 'a' || *fmt > 'z')) fmt++; continue; }
        else if (*fmt == '%') console_putc('%');
        fmt++;
    }
}

void fputc(int c, void* stream) { (void)stream; console_putc((char)c); }

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
void *memset(void *s, int c, size_t n) __attribute__((alias("sage_memset")));
void *memcpy(void *dest, const void *src, size_t n) __attribute__((alias("sage_memcpy")));
size_t strlen(const char *s) __attribute__((alias("sage_strlen")));
int strcmp(const char *a, const char *b) __attribute__((alias("sage_strcmp")));

