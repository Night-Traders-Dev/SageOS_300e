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
    size = (size + 7) & ~(size_t)7;
    if (sage_bump + size > SAGE_ARENA_SIZE) {
        console_write("\nsage: out of memory");
        return (void *)0;
    }
    void *ptr = &sage_heap[sage_bump];
    sage_bump += size;
    return ptr;
}

void *sage_calloc(size_t count, size_t size) {
    size_t total = count * size;
    void *p = sage_malloc(total);
    if (p) { uint8_t *d = (uint8_t *)p; for (size_t i = 0; i < total; i++) d[i] = 0; }
    return p;
}

void *sage_realloc(void *ptr, size_t old_size, size_t new_size) {
    if (!ptr) return sage_malloc(new_size);
    if (new_size == 0) return (void *)0;
    void *np = sage_malloc(new_size);
    if (!np) return (void *)0;
    size_t cp = old_size < new_size ? old_size : new_size;
    uint8_t *d = (uint8_t *)np; const uint8_t *s = (const uint8_t *)ptr;
    for (size_t i = 0; i < cp; i++) d[i] = s[i];
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
        case 'f': { double v = __builtin_va_arg(ap, double); if (v < 0) { console_putc('-'); v = -v; } put_uint((uint64_t)v); console_putc('.'); double f = v - (double)((uint64_t)v); for (int i = 0; i < 6; i++) { f *= 10.0; console_putc('0' + (char)((int)f)); f -= (int)f; } break; }
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

/* --- Math --- */
double sage_fmod(double x, double y) { if (y == 0.0) return 0.0; return x - (double)((int64_t)(x / y)) * y; }
double sage_fabs(double x) { return x < 0 ? -x : x; }
double sage_floor(double x) { int64_t i = (int64_t)x; return (double)(x < (double)i ? i - 1 : i); }
double sage_ceil(double x) { int64_t i = (int64_t)x; return (double)(x > (double)i ? i + 1 : i); }
double sage_pow(double b, double e) {
    if (e == (double)(int64_t)e) { int64_t n = (int64_t)e; double r = 1.0; int neg = 0; if (n < 0) { neg = 1; n = -n; } while (n > 0) { if (n & 1) r *= b; b *= b; n >>= 1; } return neg ? 1.0 / r : r; }
    return 0.0;
}
double sage_sqrt(double x) {
    if (x <= 0.0) return 0.0;
    double g = x / 2.0; for (int i = 0; i < 50; i++) g = (g + x / g) / 2.0; return g;
}

double sage_strtod(const char *s, char **end) {
    double r = 0.0; int sg = 1;
    while (*s == ' ') s++;
    if (*s == '-') { sg = -1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { r = r * 10.0 + (*s - '0'); s++; }
    if (*s == '.') { s++; double f = 0.1; while (*s >= '0' && *s <= '9') { r += (*s - '0') * f; f *= 0.1; s++; } }
    if (end) *end = (char *)s;
    return r * sg;
}

int sage_atoi(const char *s) { int r = 0, sg = 1; while (*s == ' ') s++; if (*s == '-') { sg = -1; s++; } while (*s >= '0' && *s <= '9') { r = r * 10 + (*s - '0'); s++; } return r * sg; }

int sage_isdigit(int c) { return c >= '0' && c <= '9'; }
int sage_isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int sage_isalnum(int c) { return sage_isdigit(c) || sage_isalpha(c); }
int sage_isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
