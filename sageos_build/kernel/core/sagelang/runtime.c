/*
 * runtime.c — SageLang kernel REPL driver for SageOS
 *
 * Integer-only evaluator (no SSE/float — freestanding kernel constraint).
 * Provides:
 *   sage_execute(name) — run a .sage file from RAMFS/FAT32
 *   sage_repl()        — interactive REPL loop
 */

#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "keyboard.h"
#include "ramfs.h"
#include "io.h"

/* -----------------------------------------------------------------------
 * REPL line reader
 * ----------------------------------------------------------------------- */

#define SAGE_LINE_MAX 256

static int sage_read_line(char *buf, size_t max) {
    size_t pos = 0;
    buf[0] = 0;

    for (;;) {
        KeyEvent ev;
        if (!keyboard_wait_event(&ev)) continue;
        if (!ev.pressed) continue;
        if (ev.extended) continue;

        char c = ev.ascii;

        if (c == '\r' || c == '\n') {
            buf[pos] = 0;
            console_write("\n");
            return (int)pos;
        }
        if (c == 4 && pos == 0) return -1;  /* Ctrl-D */
        if (c == 3) { console_write("^C\n"); buf[0] = 0; return 0; }

        if (c == 8 || c == 127) {
            if (pos > 0) { pos--; buf[pos] = 0; console_write("\b \b"); }
            continue;
        }

        if ((uint8_t)c >= 32 && (uint8_t)c <= 126 && pos + 1 < max) {
            buf[pos++] = c;
            buf[pos] = 0;
            console_putc(c);
        }
    }
}

/* -----------------------------------------------------------------------
 * Integer-only recursive-descent evaluator
 *
 * Supports: arithmetic (+, -, *, /, %), variables (let), print(),
 * string literals, string concatenation, peek/poke, inb/outb.
 * ----------------------------------------------------------------------- */

#define SAGE_MAX_VARS 64

typedef struct {
    char name[32];
    int64_t num_val;
    char str_val[128];
    int is_string;
} SageVar;

static SageVar sage_vars[SAGE_MAX_VARS];
static int sage_var_count = 0;

static SageVar *sage_find_var(const char *name) {
    for (int i = 0; i < sage_var_count; i++) {
        const char *a = sage_vars[i].name, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) return &sage_vars[i];
    }
    return (SageVar *)0;
}

static SageVar *sage_define_var(const char *name) {
    SageVar *v = sage_find_var(name);
    if (v) return v;
    if (sage_var_count >= SAGE_MAX_VARS) {
        console_write("\nsage: too many variables");
        return (SageVar *)0;
    }
    v = &sage_vars[sage_var_count++];
    int i = 0;
    while (name[i] && i < 31) { v->name[i] = name[i]; i++; }
    v->name[i] = 0;
    v->num_val = 0; v->str_val[0] = 0; v->is_string = 0;
    return v;
}

static const char *sage_src;

static void sage_skip_ws(void) {
    while (*sage_src == ' ' || *sage_src == '\t') sage_src++;
}

static int sage_is_digit(char c) { return c >= '0' && c <= '9'; }
static int sage_is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int64_t sage_parse_expr(void);

static int64_t sage_parse_number(void) {
    int64_t result = 0;
    int base = 10;

    /* Handle 0x prefix for hex */
    if (*sage_src == '0' && (sage_src[1] == 'x' || sage_src[1] == 'X')) {
        sage_src += 2;
        base = 16;
        while (1) {
            char c = *sage_src;
            int digit = -1;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = 10 + c - 'a';
            else if (c >= 'A' && c <= 'F') digit = 10 + c - 'A';
            if (digit < 0) break;
            result = result * 16 + digit;
            sage_src++;
        }
        return result;
    }

    while (sage_is_digit(*sage_src)) {
        result = result * base + (*sage_src - '0');
        sage_src++;
    }
    return result;
}

static int64_t sage_parse_primary(void) {
    sage_skip_ws();

    if (*sage_src == '(') {
        sage_src++;
        int64_t v = sage_parse_expr();
        sage_skip_ws();
        if (*sage_src == ')') sage_src++;
        return v;
    }

    if (*sage_src == '-') {
        sage_src++;
        return -sage_parse_primary();
    }

    if (sage_is_digit(*sage_src)) {
        return sage_parse_number();
    }

    if (sage_is_alpha(*sage_src)) {
        char name[32];
        int ni = 0;
        while ((sage_is_alpha(*sage_src) || sage_is_digit(*sage_src)) && ni < 31)
            name[ni++] = *sage_src++;
        name[ni] = 0;

        SageVar *v = sage_find_var(name);
        if (v && !v->is_string) return v->num_val;
        return 0;
    }

    return 0;
}

static int64_t sage_parse_factor(void) {
    int64_t left = sage_parse_primary();
    sage_skip_ws();
    while (*sage_src == '*' || *sage_src == '/' || *sage_src == '%') {
        char op = *sage_src++;
        int64_t right = sage_parse_primary();
        if (op == '*') left *= right;
        else if (op == '/') left = right != 0 ? left / right : 0;
        else left = right != 0 ? left % right : 0;
        sage_skip_ws();
    }
    return left;
}

static int64_t sage_parse_expr(void) {
    int64_t left = sage_parse_factor();
    sage_skip_ws();
    while (*sage_src == '+' || *sage_src == '-') {
        char op = *sage_src++;
        int64_t right = sage_parse_factor();
        if (op == '+') left += right;
        else left -= right;
        sage_skip_ws();
    }
    return left;
}

/* Parse a string expression — returns 1 if it was a string */
static int sage_parse_str_expr(char *out, size_t max) {
    sage_skip_ws();
    if (*sage_src == '"') {
        sage_src++;
        size_t i = 0;
        while (*sage_src && *sage_src != '"' && i + 1 < max) {
            if (*sage_src == '\\' && sage_src[1]) {
                sage_src++;
                if (*sage_src == 'n') out[i++] = '\n';
                else if (*sage_src == 't') out[i++] = '\t';
                else out[i++] = *sage_src;
            } else {
                out[i++] = *sage_src;
            }
            sage_src++;
        }
        if (*sage_src == '"') sage_src++;
        out[i] = 0;

        /* String concatenation with + */
        sage_skip_ws();
        while (*sage_src == '+') {
            sage_src++;
            sage_skip_ws();
            char rhs[128];
            if (sage_parse_str_expr(rhs, sizeof(rhs))) {
                size_t ri = 0;
                while (rhs[ri] && i + 1 < max) out[i++] = rhs[ri++];
                out[i] = 0;
            }
            sage_skip_ws();
        }
        return 1;
    }

    /* String variable */
    if (sage_is_alpha(*sage_src)) {
        const char *save = sage_src;
        char name[32];
        int ni = 0;
        while ((sage_is_alpha(*sage_src) || sage_is_digit(*sage_src)) && ni < 31)
            name[ni++] = *sage_src++;
        name[ni] = 0;
        SageVar *v = sage_find_var(name);
        if (v && v->is_string) {
            size_t i = 0;
            const char *s = v->str_val;
            while (*s && i + 1 < max) out[i++] = *s++;
            out[i] = 0;
            sage_skip_ws();
            while (*sage_src == '+') {
                sage_src++;
                sage_skip_ws();
                char rhs[128];
                if (sage_parse_str_expr(rhs, sizeof(rhs))) {
                    size_t ri = 0;
                    while (rhs[ri] && i + 1 < max) out[i++] = rhs[ri++];
                    out[i] = 0;
                }
                sage_skip_ws();
            }
            return 1;
        }
        sage_src = save;
    }
    return 0;
}

static void sage_print_int(int64_t v) {
    if (v < 0) { console_putc('-'); v = -v; }
    char buf[21];
    int pos = 20;
    buf[pos] = 0;
    if (v == 0) { console_putc('0'); return; }
    while (v > 0) { buf[--pos] = '0' + (char)(v % 10); v /= 10; }
    console_write(&buf[pos]);
}

/* -----------------------------------------------------------------------
 * Statement execution
 * ----------------------------------------------------------------------- */

static int starts_kw(const char *s, const char *kw) {
    while (*kw) { if (*s != *kw) return 0; s++; kw++; }
    return *s == ' ' || *s == '(' || *s == 0;
}

static int sage_exec_line(const char *line) {
    sage_src = line;
    sage_skip_ws();

    if (*sage_src == 0 || *sage_src == '#') return 0;

    /* let name = expr */
    if (starts_kw(sage_src, "let")) {
        sage_src += 3;
        sage_skip_ws();
        char name[32];
        int ni = 0;
        while ((sage_is_alpha(*sage_src) || sage_is_digit(*sage_src)) && ni < 31)
            name[ni++] = *sage_src++;
        name[ni] = 0;
        sage_skip_ws();
        if (*sage_src == '=') sage_src++;
        sage_skip_ws();

        char str_buf[128];
        const char *save = sage_src;
        if (sage_parse_str_expr(str_buf, sizeof(str_buf))) {
            SageVar *v = sage_define_var(name);
            if (v) {
                v->is_string = 1;
                int i = 0;
                while (str_buf[i] && i < 127) { v->str_val[i] = str_buf[i]; i++; }
                v->str_val[i] = 0;
            }
        } else {
            sage_src = save;
            int64_t val = sage_parse_expr();
            SageVar *v = sage_define_var(name);
            if (v) { v->num_val = val; v->is_string = 0; }
        }
        return 0;
    }

    /* print(...) */
    if (starts_kw(sage_src, "print")) {
        sage_src += 5;
        sage_skip_ws();
        if (*sage_src == '(') sage_src++;
        sage_skip_ws();

        char str_buf[128];
        const char *save = sage_src;
        if (sage_parse_str_expr(str_buf, sizeof(str_buf))) {
            console_write(str_buf);
        } else {
            sage_src = save;
            int64_t val = sage_parse_expr();
            sage_print_int(val);
        }
        sage_skip_ws();
        if (*sage_src == ')') sage_src++;
        console_write("\n");
        return 0;
    }

    /* peek(addr) */
    if (starts_kw(sage_src, "peek")) {
        sage_src += 4;
        if (*sage_src == '(') sage_src++;
        int64_t addr = sage_parse_expr();
        sage_skip_ws();
        if (*sage_src == ')') sage_src++;
        volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)addr;
        console_write("0x");
        console_hex64(*p);
        console_write("\n");
        return 0;
    }

    /* poke(addr, val) */
    if (starts_kw(sage_src, "poke")) {
        sage_src += 4;
        if (*sage_src == '(') sage_src++;
        int64_t addr = sage_parse_expr();
        sage_skip_ws();
        if (*sage_src == ',') sage_src++;
        int64_t val = sage_parse_expr();
        sage_skip_ws();
        if (*sage_src == ')') sage_src++;
        volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)addr;
        *p = (uint8_t)val;
        console_write("ok\n");
        return 0;
    }

    /* inb(port) */
    if (starts_kw(sage_src, "inb")) {
        sage_src += 3;
        if (*sage_src == '(') sage_src++;
        uint16_t port = (uint16_t)sage_parse_expr();
        sage_skip_ws();
        if (*sage_src == ')') sage_src++;
        uint8_t val = inb(port);
        console_write("0x");
        console_hex64(val);
        console_write("\n");
        return 0;
    }

    /* outb(port, val) */
    if (starts_kw(sage_src, "outb")) {
        sage_src += 4;
        if (*sage_src == '(') sage_src++;
        uint16_t port = (uint16_t)sage_parse_expr();
        sage_skip_ws();
        if (*sage_src == ',') sage_src++;
        uint8_t val = (uint8_t)sage_parse_expr();
        sage_skip_ws();
        if (*sage_src == ')') sage_src++;
        outb(port, val);
        console_write("ok\n");
        return 0;
    }

    /* exit */
    if (starts_kw(sage_src, "exit")) return -1;

    /* Bare expression — evaluate and show result */
    char str_buf[128];
    const char *save = sage_src;
    if (sage_parse_str_expr(str_buf, sizeof(str_buf))) {
        console_write(str_buf);
        console_write("\n");
    } else {
        sage_src = save;
        int64_t val = sage_parse_expr();
        sage_print_int(val);
        console_write("\n");
    }
    return 0;
}

/* -----------------------------------------------------------------------
 * Multi-line file execution
 * ----------------------------------------------------------------------- */

static void sage_exec_source(const char *source) {
    const char *p = source;
    while (*p) {
        char line[SAGE_LINE_MAX];
        int li = 0;
        while (*p && *p != '\n' && li + 1 < SAGE_LINE_MAX)
            line[li++] = *p++;
        line[li] = 0;
        if (*p == '\n') p++;
        if (sage_exec_line(line) < 0) return;
    }
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */

void sage_runtime_init(void) {
    /* Future: register native function table */
}

void sage_execute(const char *module_name) {
    if (!module_name || !*module_name) {
        /* Interactive REPL mode */
        console_write("\nSageLang REPL v0.1.2 (integer mode)");
        console_write("\nBuiltins: print() peek() poke() inb() outb()");
        console_write("\nType exit or Ctrl-D to return to shell.\n");

        char line[SAGE_LINE_MAX];
        for (;;) {
            uint32_t old_fg = console_get_fg();
            console_set_fg(0xFFBF40);
            console_write("sage> ");
            console_set_fg(old_fg);

            int len = sage_read_line(line, sizeof(line));
            if (len < 0) { console_write("(exit)\n"); return; }
            if (len == 0) continue;
            if (sage_exec_line(line) < 0) return;
        }
    }

    /* File execution mode */
    const char *data = ramfs_find(module_name);
    if (!data) {
        char path[128];
        int pi = 0;
        const char *prefix = "/etc/";
        while (*prefix && pi < 120) path[pi++] = *prefix++;
        const char *m = module_name;
        while (*m && pi < 127) path[pi++] = *m++;
        path[pi] = 0;
        data = ramfs_find(path);
        if (!data) {
            console_write("\nsage: module not found: ");
            console_write(module_name);
            return;
        }
    }

    console_write("\nsage: executing ");
    console_write(module_name);
    console_write("\n");
    sage_exec_source(data);
}

int sage_compile(const char *source_path, const char *output_path) {
    console_write("\nsage: compile not yet available in kernel mode");
    (void)source_path;
    (void)output_path;
    return 0;
}
