#include "runtime.h"

#include "compiler/bytecode.h"
#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "console.h"
#include "keyboard.h"
#include "metal_vm.h"
#include "sage_alloc.h"
#include "version.h"
#include "vfs.h"

extern void register_timer_native_bindings(MetalVM* vm);
extern void register_bootlog_native_bindings(MetalVM* vm);
extern void register_power_native_bindings(MetalVM* vm);
extern void register_status_native_bindings(MetalVM* vm);
extern void register_battery_native_bindings(MetalVM* vm);
extern void register_core_drivers_native_bindings(MetalVM* vm);

#include <stdio.h>
#include <string.h>

#define SAGE_REPL_LINE_MAX   256
#define SAGE_REPL_BLOCK_MAX  2048
#define SAGE_REPL_PARAM_MAX  8

#define REPL_HISTORY_MAX 32
static char g_repl_history[REPL_HISTORY_MAX][SAGE_REPL_LINE_MAX];
static int g_repl_history_count = 0;

static void repl_history_add(const char* line) {
    if (g_repl_history_count > 0 && strcmp(g_repl_history[g_repl_history_count - 1], line) == 0) {
        return; // Don't duplicate last
    }
    if (g_repl_history_count >= REPL_HISTORY_MAX) {
        for (int i = 0; i < REPL_HISTORY_MAX - 1; i++) {
            extern char *sage_strncpy(char *dest, const char *src, size_t n);
            sage_strncpy(g_repl_history[i], g_repl_history[i + 1], SAGE_REPL_LINE_MAX - 1);
        }
        g_repl_history_count = REPL_HISTORY_MAX - 1;
    }
    extern char *sage_strncpy(char *dest, const char *src, size_t n);
    sage_strncpy(g_repl_history[g_repl_history_count], line, SAGE_REPL_LINE_MAX - 1);
    g_repl_history[g_repl_history_count][SAGE_REPL_LINE_MAX - 1] = 0;
    g_repl_history_count++;
}

// Persistent REPL VM
MetalVM g_repl_vm;
static int g_repl_vm_inited = 0;

static void metal_vm_write_char_bridge(char c) {
    console_putc(c);
}

// ============================================================================
// AST Debug Print
// ============================================================================

static void repl_print_ast_expr(Expr* expr, int depth) {
    if (expr == NULL) { console_write("nil"); return; }
    for (int i = 0; i < depth; i++) console_write("  ");
    switch (expr->type) {
        case EXPR_NUMBER: {
            union { double d; uint64_t u; } v;
            v.u = expr->as.number.value;
            printf("(number %g)", v.d); 
            break;
        }
        case EXPR_STRING: printf("(string \"%s\")", expr->as.string.value); break;
        case EXPR_BOOL: printf("(bool %s)", expr->as.boolean.value ? "true" : "false"); break;
        case EXPR_NIL: console_write("(nil)"); break;
        case EXPR_VARIABLE: printf("(var %.*s)", expr->as.variable.name.length, expr->as.variable.name.start); break;
        case EXPR_BINARY:
            printf("(binary %.*s\n", expr->as.binary.op.length, expr->as.binary.op.start);
            repl_print_ast_expr(expr->as.binary.left, depth + 1); console_write("\n");
            repl_print_ast_expr(expr->as.binary.right, depth + 1); console_write(")");
            break;
        case EXPR_CALL: {
            console_write("(call\n");
            repl_print_ast_expr(expr->as.call.callee, depth + 1);
            for (int i = 0; i < expr->as.call.arg_count; i++) {
                console_write("\n");
                repl_print_ast_expr(expr->as.call.args[i], depth + 1);
            }
            console_write(")");
            break;
        }
        case EXPR_ARRAY: printf("(array count=%d)", expr->as.array.count); break;
        case EXPR_DICT: printf("(dict count=%d)", expr->as.dict.count); break;
        case EXPR_TUPLE: printf("(tuple count=%d)", expr->as.tuple.count); break;
        case EXPR_INDEX:
            console_write("(index\n");
            repl_print_ast_expr(expr->as.index.array, depth + 1); console_write("\n");
            repl_print_ast_expr(expr->as.index.index, depth + 1); console_write(")");
            break;
        case EXPR_GET:
            printf("(get .%.*s\n", expr->as.get.property.length, expr->as.get.property.start);
            repl_print_ast_expr(expr->as.get.object, depth + 1); console_write(")");
            break;
        case EXPR_SET:
            if (expr->as.set.object == NULL) {
                printf("(assign %.*s\n", expr->as.set.property.length, expr->as.set.property.start);
                repl_print_ast_expr(expr->as.set.value, depth + 1); console_write(")");
            } else {
                printf("(set .%.*s\n", expr->as.set.property.length, expr->as.set.property.start);
                repl_print_ast_expr(expr->as.set.object, depth + 1); console_write("\n");
                repl_print_ast_expr(expr->as.set.value, depth + 1); console_write(")");
            }
            break;
        default: printf("(expr type=%d)", expr->type); break;
    }
}

static void repl_print_ast_stmt(Stmt* stmt, int depth) {
    if (stmt == NULL) return;
    for (int i = 0; i < depth; i++) console_write("  ");
    switch (stmt->type) {
        case STMT_PRINT:
            console_write("(print\n");
            repl_print_ast_expr(stmt->as.print.expression, depth + 1);
            console_write(")\n");
            break;
        case STMT_EXPRESSION:
            console_write("(expr-stmt\n");
            repl_print_ast_expr(stmt->as.expression, depth + 1);
            console_write(")\n");
            break;
        case STMT_LET:
            printf("(let %.*s\n", stmt->as.let.name.length, stmt->as.let.name.start);
            repl_print_ast_expr(stmt->as.let.initializer, depth + 1);
            console_write(")\n");
            break;
        case STMT_PROC:
            printf("(proc %.*s param_count=%d)\n", stmt->as.proc.name.length, stmt->as.proc.name.start, stmt->as.proc.param_count);
            break;
        case STMT_IF:
            console_write("(if\n");
            repl_print_ast_expr(stmt->as.if_stmt.condition, depth + 1);
            console_write("\n");
            repl_print_ast_stmt(stmt->as.if_stmt.then_branch, depth + 1);
            if (stmt->as.if_stmt.else_branch) {
                for (int i = 0; i < depth; i++) console_write("  ");
                console_write("else\n");
                repl_print_ast_stmt(stmt->as.if_stmt.else_branch, depth + 1);
            }
            console_write(")\n");
            break;
        case STMT_BLOCK:
            console_write("(block\n");
            for (Stmt* s = stmt->as.block.statements; s != NULL; s = s->next) {
                repl_print_ast_stmt(s, depth + 1);
            }
            console_write(")\n");
            break;
        default: printf("(stmt type=%d)\n", stmt->type); break;
    }
}

static void sage_clear_exit_state(void) {
    sage_exit_flag = 0;
    sage_exit_code = 0;
}

static MetalValue mv_dbl(double d) {
    union { double d; uint64_t u; } v;
    v.d = d;
    return mv_num(v.u);
}

static MetalValue metal_value_from_host(MetalVM* vm, Value value) {
    switch (value.type) {
        case VAL_NUMBER:
            return mv_num(value.as.number);
        case VAL_BOOL:
            return mv_bool(value.as.boolean);
        case VAL_NIL:
            return mv_nil();
        case VAL_STRING:
            return mv_str(vm, value.as.string, (int)strlen(value.as.string));
        default:
            return mv_nil();
    }
}

static unsigned int fnv1a_bytes(const char* s, int len) {
    unsigned int hash = 2166136261u;
    for (int i = 0; i < len; i++) {
        hash ^= (unsigned char)s[i];
        hash *= 16777619u;
    }
    return hash;
}

MetalValue n_os_strlen(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_STR) return mv_dbl(0.0);
    const char* s = metal_string_get(vm, args[0].as.str_idx);
    if (!s) return mv_dbl(0.0);
    return mv_dbl((double)strlen(s));
}

MetalValue n_os_starts_with(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 2 || args[0].type != MV_STR || args[1].type != MV_STR) return mv_bool(0);
    const char* s = metal_string_get(vm, args[0].as.str_idx);
    const char* prefix = metal_string_get(vm, args[1].as.str_idx);
    return mv_bool(strncmp(s, prefix, strlen(prefix)) == 0);
}

MetalValue n_os_array_len(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_ARR) return mv_dbl(0.0);
    return mv_dbl((double)metal_array_len(vm, args[0].as.arr_idx));
}

MetalValue n_os_array_push(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 2 || args[0].type != MV_ARR) return mv_nil();
    metal_array_push(vm, args[0].as.arr_idx, args[1]);
    return mv_nil();
}

MetalValue n_os_num_to_str(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_NUM) return mv_nil();
    union { double d; uint64_t u; } v;
    v.u = args[0].as.num_bits;
    int idx;
    metal_num_to_str(vm, (long long)v.d, &idx);
    MetalValue res; res.type = MV_STR; res.as.str_idx = idx;
    return res;
}

MetalValue n_os_stat(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_STR) return mv_nil();
    const char* path = metal_string_get(vm, args[0].as.str_idx);
    if (!path || strlen(path) > 255) return mv_nil(); // Basic path length limit
    
    VfsStat st;
    if (vfs_stat(path, &st) == 0) {
        int d_idx = metal_dict_new(vm);
        if (d_idx < 0) return mv_nil();
        
        int k_name = metal_string_intern(vm, "name", 4);
        int k_size = metal_string_intern(vm, "size", 4);
        int k_type = metal_string_intern(vm, "type", 4);
        
        if (k_name < 0 || k_size < 0 || k_type < 0) return mv_nil();

        metal_dict_set(vm, d_idx, k_name, mv_str(vm, st.name, (int)strlen(st.name)));
        metal_dict_set(vm, d_idx, k_size, mv_dbl((double)st.size));
        metal_dict_set(vm, d_idx, k_type, mv_dbl((double)st.type));
        
        MetalValue v; v.type = MV_DICT; v.as.dict_idx = d_idx;
        return v;
    }
    return mv_nil();
}

MetalValue n_len(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1) return mv_dbl(0.0);

    if (args[0].type == MV_STR) {
        const char* s = metal_string_get(vm, args[0].as.str_idx);
        return mv_dbl((double)strlen(s));
    }

    if (args[0].type == MV_ARR) {
        return mv_dbl((double)metal_array_len(vm, args[0].as.arr_idx));
    }

    if (args[0].type == MV_DICT) {
        int idx = args[0].as.dict_idx;
        int max = (int)(sizeof(vm->dicts) / sizeof(vm->dicts[0]));
        if (idx >= 0 && idx < max) {
            return mv_dbl((double)vm->dicts[idx].count);
        }
    }

    return mv_dbl(0.0);
}

MetalValue n_os_version_string(MetalVM* vm, MetalValue* args, int argc) {
    (void)args;(void)argc;
    return mv_str(vm, SAGEOS_VERSION, (int)strlen(SAGEOS_VERSION));
}

MetalValue n_os_write_char(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm;
    if (argc >= 1 && args[0].type == MV_NUM) {
        union { double d; uint64_t u; } v;
        v.u = args[0].as.num_bits;
        console_putc((char)v.d);
    }
    return mv_nil();
}

MetalValue n_os_write_str(MetalVM* vm, MetalValue* args, int argc) {
    if (argc >= 1 && args[0].type == MV_STR) {
        const char* s = metal_string_get(vm, args[0].as.str_idx);
        if (s) console_write(s);
    }
    return mv_nil();
}

MetalValue n_os_set_color_hex(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm;
    if (argc >= 1 && args[0].type == MV_NUM) {
        union { double d; uint64_t u; } v;
        v.u = args[0].as.num_bits;
        console_set_fg((uint32_t)v.d);
    }
    return mv_nil();
}

MetalValue n_os_path_exists(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_STR) return mv_bool(0);
    const char* path = metal_string_get(vm, args[0].as.str_idx);
    VfsStat st;
    return mv_bool(vfs_stat(path, &st) == 0);
}

MetalValue n_os_cat(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_STR) return mv_nil();
    const char* path = metal_string_get(vm, args[0].as.str_idx);
    char buf[513];
    uint64_t off = 0;
    while (1) {
        int n = vfs_read(path, off, buf, 512);
        if (n <= 0) break;
        buf[n] = 0;
        console_write(buf);
        off += (uint64_t)n;
    }
    return mv_nil();
}

MetalValue n_os_get_mount_count(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    return mv_dbl((double)vfs_get_mount_count());
}

MetalValue n_os_get_mount_info(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_NUM) return mv_nil();
    union { double d; uint64_t u; } v;
    v.u = args[0].as.num_bits;
    int index = (int)v.d;

    VfsMountInfo mi;
    if (vfs_get_mount_info(index, &mi) == 0) {
        int d_idx = metal_dict_new(vm);
        if (d_idx < 0) return mv_nil();

        int k_path = metal_string_intern(vm, "path", 4);
        int k_type = metal_string_intern(vm, "type", 4);

        metal_dict_set(vm, d_idx, k_path, mv_str(vm, mi.path, (int)strlen(mi.path)));
        metal_dict_set(vm, d_idx, k_type, mv_str(vm, mi.type, (int)strlen(mi.type)));

        MetalValue res; res.type = MV_DICT; res.as.dict_idx = d_idx;
        return res;
    }
    return mv_nil();
}

#include "swap.h"
#include "shell.h"
MetalValue n_os_swap_is_available(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    return mv_bool(swap_is_available());
}

MetalValue n_os_shell_exec(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_STR) return mv_nil();
    const char* cmd = metal_string_get(vm, args[0].as.str_idx);
    shell_exec_command(cmd);
    return mv_nil();
}

MetalValue n_os_get_c0(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    return mv_nil();
}

MetalValue n_os_get_color(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    extern uint32_t console_get_fg(void);
    return mv_dbl((double)console_get_fg());
}

static MetalValue n_sys_exec(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_STR) return mv_nil();
    const char* cmd = metal_string_get(vm, args[0].as.str_idx);
    extern void shell_exec_command(const char *cmd);
    shell_exec_command(cmd);
    return mv_nil();
}

static MetalValue n_sys_getenv(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_STR) return mv_nil();
    const char* var = metal_string_get(vm, args[0].as.str_idx);
    if (strcmp(var, "HOME") == 0) return metal_vm_string_new(vm, "/");
    return mv_nil();
}

static MetalValue n_io_readfile(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_STR) return mv_nil();
    const char* path = metal_string_get(vm, args[0].as.str_idx);
    extern int vfs_read(const char *path, uint64_t offset, void *buffer, size_t size);
    static char buf[4096];
    int n = vfs_read(path, 0, buf, sizeof(buf)-1);
    if (n < 0) return mv_nil();
    buf[n] = '\0';
    return metal_vm_string_new(vm, buf);
}

static MetalValue n_json_parse(MetalVM* vm, MetalValue* args, int argc) {
    if (argc < 1 || args[0].type != MV_STR) return mv_nil();
    // Simplified JSON parser placeholder
    return metal_vm_lookup(vm, "os_get_mount_info"); // Dummy
}

// Forward declare scope_define (defined in metal_vm.c as static? No, I should make it public or export a helper)
// Actually, I'll just use metal_vm_register_native and then manually fix up the scope in metal_vm.c if needed.
// Wait, I already added scope_define to metal_vm.c but it was static.

static void sage_register_builtin_modules(MetalVM* vm) {
    // 1. Register the underlying natives globally
    metal_vm_register_native(vm, "_sys_exec", n_sys_exec);
    metal_vm_register_native(vm, "_sys_getenv", n_sys_getenv);
    metal_vm_register_native(vm, "_io_readfile", n_io_readfile);
    metal_vm_register_native(vm, "_json_parse", n_json_parse);
    metal_vm_register_native(vm, "len", n_len);
    metal_vm_register_native(vm, "chr", n_os_get_c0); // Dummy chr for now

    // 2. Create the 'sys' module dictionary
    int sys_idx = metal_dict_new(vm);
    if (sys_idx >= 0) {
        metal_dict_set(vm, sys_idx, metal_string_intern(vm, "exec", 4), mv_str(vm, "_sys_exec", 9));
        metal_dict_set(vm, sys_idx, metal_string_intern(vm, "getenv", 6), mv_str(vm, "_sys_getenv", 11));
        
        // sys.argv
        int argv_idx = metal_array_new(vm);
        metal_array_push(vm, argv_idx, metal_vm_string_new(vm, "sagepkg"));
        MetalValue argv_val; argv_val.type = MV_ARR; argv_val.as.arr_idx = argv_idx;
        metal_dict_set(vm, sys_idx, metal_string_intern(vm, "argv", 4), argv_val);

        MetalValue sys_val; sys_val.type = MV_DICT; sys_val.as.dict_idx = sys_idx;
        scope_define(vm, metal_fnv1a("sys"), -1, sys_val);
    }

    // 3. Create 'io' module
    int io_idx = metal_dict_new(vm);
    if (io_idx >= 0) {
        metal_dict_set(vm, io_idx, metal_string_intern(vm, "readfile", 8), mv_str(vm, "_io_readfile", 12));
        MetalValue io_val; io_val.type = MV_DICT; io_val.as.dict_idx = io_idx;
        scope_define(vm, metal_fnv1a("io"), -1, io_val);
    }

    // 4. Create 'json' module
    int json_idx = metal_dict_new(vm);
    if (json_idx >= 0) {
        metal_dict_set(vm, json_idx, metal_string_intern(vm, "parse", 5), mv_str(vm, "_json_parse", 11));
        MetalValue json_val; json_val.type = MV_DICT; json_val.as.dict_idx = json_idx;
        scope_define(vm, metal_fnv1a("json"), -1, json_val);
    }

    // 5. Create 'string' module (placeholder)
    int str_idx = metal_dict_new(vm);
    if (str_idx >= 0) {
        MetalValue str_val; str_val.type = MV_DICT; str_val.as.dict_idx = str_idx;
        scope_define(vm, metal_fnv1a("string"), -1, str_val);
    }
}

static void sage_register_repl_natives(MetalVM* vm) {
    sage_register_builtin_modules(vm);
    metal_vm_register_native(vm, "os_strlen", n_os_strlen);
    metal_vm_register_native(vm, "os_starts_with", n_os_starts_with);
    metal_vm_register_native(vm, "os_array_len", n_os_array_len);
    metal_vm_register_native(vm, "os_stat", n_os_stat);
    metal_vm_register_native(vm, "os_version_string", n_os_version_string);
    metal_vm_register_native(vm, "os_write_char", n_os_write_char);
    metal_vm_register_native(vm, "os_write_str", n_os_write_str);
    metal_vm_register_native(vm, "os_set_color_hex", n_os_set_color_hex);
    metal_vm_register_native(vm, "os_path_exists", n_os_path_exists);
    metal_vm_register_native(vm, "os_cat", n_os_cat);
    metal_vm_register_native(vm, "os_get_mount_count", n_os_get_mount_count);
    metal_vm_register_native(vm, "os_get_mount_info", n_os_get_mount_info);
    metal_vm_register_native(vm, "os_swap_is_available", n_os_swap_is_available);
    metal_vm_register_native(vm, "os_shell_exec", n_os_shell_exec);
    metal_vm_register_native(vm, "os_get_c0", n_os_get_c0);
    metal_vm_register_native(vm, "os_get_color", n_os_get_color);
    register_timer_native_bindings(vm);
    register_bootlog_native_bindings(vm);
    register_power_native_bindings(vm);
    register_status_native_bindings(vm);
    register_battery_native_bindings(vm);
    register_core_drivers_native_bindings(vm);
}
static void sage_repl_reset_vm(void) {
    sage_arena_reset();
    metal_vm_init(&g_repl_vm);
    g_repl_vm.write_char = metal_vm_write_char_bridge;
    sage_register_repl_natives(&g_repl_vm);
}

void sage_kernel_early_init(void) {
    if (!g_repl_vm_inited) {
        sage_repl_reset_vm();
        g_repl_vm_inited = 1;
    }
}

void sage_repl_init(void) {
    if (!g_repl_vm_inited) {
        sage_repl_reset_vm();
        g_repl_vm_inited = 1;
    }
}

static int line_starts_block(const char* line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t' ||
                       line[len - 1] == '\r' || line[len - 1] == '\n')) {
        len--;
    }
    return len > 0 && line[len - 1] == ':';
}

static int sage_repl_readline(const char* prompt, char* out, size_t cap) {
    size_t len = 0;

    console_write(prompt);
    out[0] = '\0';

    for (;;) {
        KeyEvent ev;
        if (!keyboard_wait_event(&ev) || !ev.pressed) continue;
        if (ev.extended) continue;

        char c = ev.ascii;

        if (c == '\r' || c == '\n') {
            console_putc('\n');
            out[len] = '\0';
            return 1;
        }

        if (c == 4) {
            if (len == 0) {
                console_putc('\n');
                out[0] = '\0';
                return 0;
            }
            continue;
        }

        if (c == 3) {
            console_write("^C\n");
            out[0] = '\0';
            return 1;
        }

        if (c == 21) {
            while (len > 0) {
                console_putc('\b');
                console_putc(' ');
                console_putc('\b');
                len--;
            }
            out[0] = '\0';
            continue;
        }

        if (c == 8 || c == 127) {
            if (len > 0) {
                len--;
                console_putc('\b');
                console_putc(' ');
                console_putc('\b');
                out[len] = '\0';
            }
            continue;
        }

        if (c >= 32 && len + 1 < cap) {
            out[len++] = c;
            out[len] = '\0';
            console_putc(c);
        }
    }
}

static void sage_repl_print_help(void) {
    console_write("Sage REPL Commands:\n");
    console_write("\nSession:\n");
    console_write("  :help           Show this help message\n");
    console_write("  :quit / :exit   Exit the Sage REPL\n");
    console_write("  :reset          Reset the REPL session\n");
    console_write("  :clear          Clear the console\n");
    console_write("  :history [n]    Show recent history\n");
    console_write("  :search <pat>   Search history for pattern\n");
    console_write("  :clear-history  Clear history\n");
    
    console_write("\nInspection:\n");
    console_write("  :vars [prefix]  List global bindings\n");
    console_write("  :type <expr>    Evaluate expression and show its type\n");
    console_write("  :ast <code>     Show parsed AST for code\n");
    console_write("  :env            Show the full scope chain\n");
    console_write("  :doc <name>     Show documentation for a function\n");
    
    console_write("\nPerformance:\n");
    console_write("  :time <expr>    Time a single expression evaluation\n");
    console_write("  :bench <n> <code> Run code n times and show stats\n");
    
    console_write("\nSystem:\n");
    console_write("  :pwd            Print current working directory\n");
    console_write("  :cd <dir>       Change working directory\n");
    console_write("  :ls [dir]       List files in a directory\n");
    console_write("  :cat <file>     Print file contents\n");
    console_write("  :sh <cmd>       Execute a kernel shell command\n");
    console_write("  :gc             Show arena allocator usage\n");

    console_write("\nBlocks ending with ':' continue on the next prompt.\n");
    console_write("Submit an empty continuation line to run the block.\n");
}

static void sage_prepare_vm_for_step(MetalVM* vm) {
    vm->sp = 0;
    vm->frame_count = 0;
    vm->constants = vm->main_constants;
    vm->const_count = vm->main_const_count;
    vm->scope_depth = 0;
    vm->halted = 0;
    vm->error = 0;
    vm->error_msg = 0;
}

static int sage_import_chunk_constants(MetalVM* vm, BytecodeChunk* chunk,
                                       char* error, size_t error_size,
                                       int* const_offset_out) {
    int const_offset = vm->const_count;
    for (int i = 0; i < chunk->constant_count; i++) {
        MetalValue mv = metal_value_from_host(vm, chunk->constants[i]);
        if (metal_vm_add_constant(vm, mv) < 0) {
            snprintf(error, error_size, "REPL constant pool exhausted");
            return 0;
        }
    }
    *const_offset_out = const_offset;
    return 1;
}

static void sage_patch_main_chunk_indices(BytecodeChunk* chunk, int const_offset) {
    if (const_offset == 0) return;
    
    for (int i = 0; i < chunk->code_count; ) {
        uint8_t op = chunk->code[i];
        int operands = 0;

        switch (op) {
            case BC_OP_CONSTANT:
            case BC_OP_GET_GLOBAL:
            case BC_OP_DEFINE_GLOBAL:
            case BC_OP_SET_GLOBAL:
            case BC_OP_GET_PROPERTY:
            case BC_OP_SET_PROPERTY:
            case BC_OP_ARRAY:
            case BC_OP_TUPLE:
            case BC_OP_DICT:
            case BC_OP_IMPORT:
            case BC_OP_CLASS:
            case BC_OP_METHOD:
            {
                uint16_t idx = (uint16_t)((chunk->code[i + 1] << 8) | chunk->code[i + 2]);
                idx += (uint16_t)const_offset;
                chunk->code[i + 1] = (uint8_t)((idx >> 8) & 0xff);
                chunk->code[i + 2] = (uint8_t)(idx & 0xff);
                operands = 2;
                break;
            }

            case BC_OP_DEFINE_FUNCTION:
            {
                uint16_t name_idx = (uint16_t)((chunk->code[i + 1] << 8) | chunk->code[i + 2]);
                name_idx += (uint16_t)const_offset;
                chunk->code[i + 1] = (uint8_t)((name_idx >> 8) & 0xff);
                chunk->code[i + 2] = (uint8_t)(name_idx & 0xff);
                // Note: fn_idx (bytes 3,4) does not need patching
                operands = 4;
                break;
            }

            case BC_OP_CALL_METHOD:
            {
                // Format: OP (1), ARGC (1), NAME_IDX (2)
                uint16_t idx = (uint16_t)((chunk->code[i + 2] << 8) | chunk->code[i + 3]);
                idx += (uint16_t)const_offset;
                chunk->code[i + 2] = (uint8_t)((idx >> 8) & 0xff);
                chunk->code[i + 3] = (uint8_t)(idx & 0xff);
                operands = 3;
                break;
            }

            case BC_OP_JUMP:
            case BC_OP_JUMP_IF_FALSE:
            case BC_OP_LOOP_BACK:
            case BC_OP_BREAK:
            case BC_OP_CONTINUE:
            case BC_OP_SETUP_TRY:
                operands = 2;
                break;

            case BC_OP_CALL:
                operands = 1;
                break;

            default:
                operands = 0;
                break;
        }

        i += (1 + operands);
    }
}

static int sage_wrap_expression_for_print(BytecodeChunk* chunk,
                                          char* error, size_t error_size) {
    if (chunk->code_count <= 0 || chunk->code[chunk->code_count - 1] != BC_OP_RETURN) {
        return 1;
    }

    uint8_t* code = realloc(chunk->code, (size_t)chunk->code_count + 2u);
    if (!code) {
        snprintf(error, error_size, "out of memory while expanding REPL expression");
        return 0;
    }

    chunk->code = code;
    chunk->code[chunk->code_count - 1] = BC_OP_PRINT;
    chunk->code[chunk->code_count] = BC_OP_NIL;
    chunk->code[chunk->code_count + 1] = BC_OP_RETURN;
    chunk->code_count += 2;
    return 1;
}

static int sage_repl_build_function(void* data, ProcStmt* proc,
                                    char* error, size_t error_size,
                                    int* function_index_out) {
    MetalVM* vm = (MetalVM*)data;
    BytecodeChunk chunk;
    MetalFunction* fn;
    unsigned char* heap_ptr;
    int fn_idx;
    int const_bytes;
    int code_bytes;

    if (proc->param_count > SAGE_REPL_PARAM_MAX) {
        snprintf(error, error_size, "REPL functions support at most %d parameters",
                 SAGE_REPL_PARAM_MAX);
        return 0;
    }

    if (vm->fn_count >= (int)(sizeof(vm->functions) / sizeof(vm->functions[0]))) {
        snprintf(error, error_size, "REPL function table exhausted");
        return 0;
    }

    bytecode_chunk_init(&chunk);
    if (!bytecode_compile_function_body(&chunk, proc->body,
                                        sage_repl_build_function, data,
                                        error, error_size)) {
        bytecode_chunk_free(&chunk);
        return 0;
    }

    fn_idx = vm->fn_count++;
    fn = &vm->functions[fn_idx];
    memset(fn, 0, sizeof(*fn));
    fn->param_count = proc->param_count;
    for (int i = 0; i < proc->param_count; i++) {
        fn->param_name_hashes[i] = fnv1a_bytes(proc->params[i].start, proc->params[i].length);
    }

    const_bytes = chunk.constant_count * (int)sizeof(MetalValue);
    // Heap threshold warning (90%)
    if (vm->heap_used > (METAL_HEAP_SIZE * 9 / 10)) {
        console_write("WARNING: REPL VM heap usage is high (");
        console_u32(vm->heap_used);
        console_write("/");
        console_u32(METAL_HEAP_SIZE);
        console_write("). Consider calling :reset\n");
    }

    if (vm->heap_used + const_bytes > METAL_HEAP_SIZE) {
        snprintf(error, error_size, "REPL VM heap exhausted by function constants");
        vm->fn_count--;
        bytecode_chunk_free(&chunk);
        return 0;
    }
    if (const_bytes > 0) {
        MetalValue* pool = (MetalValue*)&vm->heap[vm->heap_used];
        for (int i = 0; i < chunk.constant_count; i++) {
            pool[i] = metal_value_from_host(vm, chunk.constants[i]);
        }
        fn->constants = pool;
        fn->const_count = chunk.constant_count;
        vm->heap_used += const_bytes;
    }

    code_bytes = chunk.code_count;
    if (vm->heap_used + code_bytes > METAL_HEAP_SIZE) {
        snprintf(error, error_size, "REPL VM heap exhausted by function bytecode");
        vm->fn_count--;
        bytecode_chunk_free(&chunk);
        return 0;
    }
    heap_ptr = &vm->heap[vm->heap_used];
    memcpy(heap_ptr, chunk.code, (size_t)code_bytes);
    fn->code = heap_ptr;
    fn->code_length = code_bytes;
    vm->heap_used += code_bytes;

    *function_index_out = fn_idx;
    bytecode_chunk_free(&chunk);
    return 1;
}

void sage_repl_step(const char* line) {
    char error[256];
    
    if (!line || !*line) return;

    sage_repl_init();
    sage_clear_exit_state();
    sage_arena_reset();

    init_lexer(line, "<repl>");
    parser_init();

    while (!parser_is_at_end()) {
        Stmt* stmt = parse();
        if (sage_exit_flag || stmt == NULL) {
            sage_clear_exit_state();
            break;
        }

        BytecodeChunk chunk;
        int const_offset = 0;
        int is_expression = (stmt->type == STMT_EXPRESSION);

        bytecode_chunk_init(&chunk);
        if (!bytecode_compile_statement_with_functions(&chunk, stmt, BYTECODE_COMPILE_STRICT,
                                                       sage_repl_build_function, &g_repl_vm,
                                                       error, sizeof(error))) {
            if (!sage_exit_flag) {
                printf("Compile error: %s\n", error);
            }
            bytecode_chunk_free(&chunk);
            sage_clear_exit_state();
            break;
        }

        if (is_expression &&
            !sage_wrap_expression_for_print(&chunk, error, sizeof(error))) {
            printf("Compile error: %s\n", error);
            bytecode_chunk_free(&chunk);
            sage_clear_exit_state();
            break;
        }

        if (!sage_import_chunk_constants(&g_repl_vm, &chunk, error, sizeof(error), &const_offset)) {
            printf("Compile error: %s\n", error);
            bytecode_chunk_free(&chunk);
            sage_clear_exit_state();
            break;
        }
        sage_patch_main_chunk_indices(&chunk, const_offset);

        sage_prepare_vm_for_step(&g_repl_vm);
        metal_vm_load(&g_repl_vm, chunk.code, chunk.code_count);
        metal_vm_run(&g_repl_vm);

        if (g_repl_vm.error) {
            printf("Runtime error: %s\n", g_repl_vm.error_msg ? g_repl_vm.error_msg : "unknown");
            g_repl_vm.error = 0;
            g_repl_vm.error_msg = 0;
        }

        bytecode_chunk_free(&chunk);
    }
    sage_clear_exit_state();
}

static int command_matches(const char* line, const char* cmd, const char** arg_out) {
    size_t cmd_len = strlen(cmd);
    if (strncmp(line, cmd, cmd_len) == 0) {
        if (line[cmd_len] == '\0') {
            if (arg_out) *arg_out = "";
            return 1;
        }
        if (line[cmd_len] == ' ') {
            if (arg_out) {
                const char* p = line + cmd_len;
                while (*p == ' ') p++;
                *arg_out = p;
            }
            return 1;
        }
    }
    return 0;
}

static void sage_repl_run(void) {
    char line[SAGE_REPL_LINE_MAX];
    char block[SAGE_REPL_BLOCK_MAX];
    const char* arg = NULL;

    sage_repl_init();

    console_write("\nSage REPL\n");
    console_write("Type :help for help, :quit to exit.\n");

    for (;;) {
        size_t block_len = 0;

        if (!sage_repl_readline("sage> ", line, sizeof(line))) {
            break;
        }

        if (line[0] == '\0') {
            continue;
        }

        repl_history_add(line);

        if (strcmp(line, ":quit") == 0 || strcmp(line, ":exit") == 0) {
            break;
        }

        if (strcmp(line, ":help") == 0) {
            sage_repl_print_help();
            continue;
        }

        if (strcmp(line, ":reset") == 0) {
            sage_repl_reset_vm();
            sage_clear_exit_state();
            console_write("Sage REPL session reset.\n");
            continue;
        }

        if (strcmp(line, ":clear") == 0) {
            console_clear();
            continue;
        }

        if (strcmp(line, ":clear-history") == 0) {
            g_repl_history_count = 0;
            console_write("History cleared.\n");
            continue;
        }

        if (command_matches(line, ":history", &arg)) {
            int n = 20;
            if (*arg != '\0') n = atoi(arg);
            if (n <= 0) n = 20;
            int start = g_repl_history_count - n;
            if (start < 0) start = 0;
            for (int i = start; i < g_repl_history_count; i++) {
                printf("  %3d  %s\n", i + 1, g_repl_history[i]);
            }
            if (g_repl_history_count == 0) console_write("No history.\n");
            continue;
        }

        if (command_matches(line, ":search", &arg)) {
            if (*arg == '\0') {
                console_write("Usage: :search <pattern>\n");
            } else {
                int found = 0;
                for (int i = 0; i < g_repl_history_count; i++) {
                    if (strstr(g_repl_history[i], arg) != NULL) {
                        printf("  %3d  %s\n", i + 1, g_repl_history[i]);
                        found++;
                    }
                }
                if (found == 0) printf("No matches found for \"%s\".\n", arg);
                else printf("%d match%s found.\n", found, found == 1 ? "" : "es");
            }
            continue;
        }

        if (command_matches(line, ":vars", &arg)) {
            MetalScope* s = &g_repl_vm.scopes[0];
            const char* prefix = (*arg != '\0') ? arg : NULL;
            int found = 0;
            for (int i = 0; i < s->count; i++) {
                const char* name = NULL;
                if (s->name_const_idx[i] >= 0) {
                    name = metal_string_get(&g_repl_vm, g_repl_vm.constants[s->name_const_idx[i]].as.str_idx);
                } else if (s->values[i].type == MV_STR) {
                    name = metal_string_get(&g_repl_vm, s->values[i].as.str_idx);
                }
                
                if (name) {
                    if (prefix && strncmp(name, prefix, strlen(prefix)) != 0) continue;
                    console_write("  ");
                    console_write(name);
                    console_write(" = ");
                    metal_print_value(&g_repl_vm, s->values[i]);
                    console_write("\n");
                    found++;
                }
            }
            if (found == 0) {
                if (prefix) printf("No variables starting with '%s'\n", prefix);
                else console_write("No variables defined.\n");
            }
            continue;
        }

        if (command_matches(line, ":ast", &arg)) {
            if (*arg == '\0') {
                console_write("Usage: :ast <code>\n");
            } else {
                init_lexer(arg, "<repl-ast>");
                parser_init();
                while (!parser_is_at_end()) {
                    Stmt* stmt = parse();
                    if (stmt) {
                        repl_print_ast_stmt(stmt, 0);
                    } else {
                        break;
                    }
                }
            }
            continue;
        }

        if (command_matches(line, ":type", &arg)) {
            if (*arg == '\0') {
                console_write("Usage: :type <expr>\n");
            } else {
                init_lexer(arg, "<repl-type>");
                parser_init();
                Stmt* stmt = parse();
                if (stmt && stmt->type == STMT_EXPRESSION) {
                    BytecodeChunk chunk;
                    bytecode_chunk_init(&chunk);
                    char error[128];
                    if (bytecode_compile_statement_with_functions(&chunk, stmt, BYTECODE_COMPILE_STRICT,
                                                                 sage_repl_build_function, &g_repl_vm,
                                                                 error, sizeof(error))) {
                        int const_offset = 0;
                        sage_import_chunk_constants(&g_repl_vm, &chunk, error, sizeof(error), &const_offset);
                        sage_patch_main_chunk_indices(&chunk, const_offset);
                        sage_prepare_vm_for_step(&g_repl_vm);
                        metal_vm_load(&g_repl_vm, chunk.code, chunk.code_count);
                        metal_vm_run(&g_repl_vm);
                        bytecode_chunk_free(&chunk);

                        bytecode_chunk_free(&chunk);
                    }
                } else {
                    console_write("sage: :type requires an expression\n");
                }
            }
            continue;
        }

        if (command_matches(line, ":time", &arg)) {
            if (*arg == '\0') {
                console_write("Usage: :time <expr>\n");
            } else {
                extern uint64_t timer_ticks(void);
                uint64_t start = timer_ticks() * 10;
                sage_repl_step(arg);
                uint64_t end = timer_ticks() * 10;
                printf("Time: %u ms\n", (uint32_t)(end - start));
            }
            continue;
        }

        if (command_matches(line, ":env", NULL)) {
            for (int d = g_repl_vm.scope_depth; d >= 0; d--) {
                MetalScope* s = &g_repl_vm.scopes[d];
                printf("Scope %d (%d bindings):\n", d, s->count);
                for (int i = 0; i < s->count; i++) {
                    const char* name = NULL;
                    if (s->name_const_idx[i] >= 0) {
                        name = metal_string_get(&g_repl_vm, g_repl_vm.constants[s->name_const_idx[i]].as.str_idx);
                    } else if (s->values[i].type == MV_STR) {
                        name = metal_string_get(&g_repl_vm, s->values[i].as.str_idx);
                    }
                    if (name) {
                        console_write("  ");
                        console_write(name);
                        console_write(" = ");
                        metal_print_value(&g_repl_vm, s->values[i]);
                        console_write("\n");
                    }
                }
            }
            continue;
        }

        if (command_matches(line, ":bench", &arg)) {
            if (*arg == '\0') {
                console_write("Usage: :bench <n> <expr>\n");
            } else {
                int n = atoi(arg);
                if (n <= 0) n = 1;
                const char* expr = arg;
                while (*expr != ' ' && *expr != '\0') expr++;
                while (*expr == ' ') expr++;
                
                if (*expr == '\0') {
                    console_write("Usage: :bench <n> <expr>\n");
                    continue;
                }

                extern uint64_t timer_ticks(void);
                uint64_t start = timer_ticks() * 10;
                for (int i = 0; i < n; i++) {
                    sage_repl_step(expr);
                }
                uint64_t end = timer_ticks() * 10;
                uint64_t total = end - start;
                printf("Bench %d iterations: total %u ms, avg %u.%03u ms\n", 
                       n, (uint32_t)total, (uint32_t)(total / n), (uint32_t)(((total % n) * 1000) / n));
            }
            continue;
        }

        if (command_matches(line, ":pwd", NULL)) {
            console_write("/\n");
            continue;
        }

        if (command_matches(line, ":cd", NULL)) {
            // Not implemented yet
            continue;
        }

        if (command_matches(line, ":doc", &arg)) {
            if (*arg == '\0') {
                console_write("Usage: :doc <name>\n");
            } else {
                MetalValue val = metal_vm_lookup(&g_repl_vm, arg);
                if (val.type == MV_FN) {
                    console_write("Function ");
                    console_write(arg);
                    console_write("\n(docstrings not yet compiled into bytecode)\n");
                } else if (val.type == MV_STR) {
                    // Check if it's a native function
                    extern MetalNativeFn metal_vm_find_native(MetalVM* vm, unsigned int hash);
                    if (metal_vm_find_native(&g_repl_vm, metal_fnv1a(arg))) {
                        console_write("Native Function ");
                        console_write(arg);
                        console_write("\n");
                    } else {
                        printf("No documentation found for \"%s\".\n", arg);
                    }
                } else {
                    printf("No documentation found for \"%s\".\n", arg);
                }
            }
            continue;
        }

        if (command_matches(line, ":ls", &arg)) {
            const char* path = (*arg != '\0') ? arg : "/";
            VfsDirEntry entries[32];
            int count = vfs_readdir(path, entries, 32);
            if (count >= 0) {
                for (int i = 0; i < count; i++) {
                    console_write(entries[i].name);
                    if (entries[i].type == VFS_DIRECTORY) console_putc('/');
                    console_write("  ");
                }
                console_putc('\n');
            } else {
                printf("ls: cannot access %s\n", path);
            }
            continue;
        }

        if (command_matches(line, ":cat", &arg)) {
            if (*arg == '\0') {
                console_write("Usage: :cat <file>\n");
            } else {
                VfsStat st;
                if (vfs_stat(arg, &st) == 0) {
                    char buf[1024];
                    int n = vfs_read(arg, 0, buf, sizeof(buf) - 1);
                    if (n >= 0) {
                        buf[n] = '\0';
                        console_write(buf);
                        if (n == sizeof(buf) - 1) console_write("\n(output truncated)");
                        console_putc('\n');
                    }
                } else {
                    printf("cat: %s: no such file\n", arg);
                }
            }
            continue;
        }

        if (command_matches(line, ":sh", &arg)) {
            if (*arg == '\0') {
                console_write("Usage: :sh <cmd>\n");
            } else {
                extern void shell_exec_command(const char* cmd);
                shell_exec_command(arg);
                console_putc('\n');
            }
            continue;
        }

        if (command_matches(line, ":gc", NULL)) {
            extern size_t sage_arena_used(void);
            printf("Arena used: %u bytes\n", (uint32_t)sage_arena_used());
            continue;
        }

        block[0] = '\0';
        block_len = strlen(line);
        if (block_len + 1 >= sizeof(block)) {
            console_write("sage: input line too long\n");
            continue;
        }
        memcpy(block, line, block_len);
        block[block_len++] = '\n';
        block[block_len] = '\0';

        if (line_starts_block(line)) {
            for (;;) {
                char cont[SAGE_REPL_LINE_MAX];
                size_t cont_len;

                if (!sage_repl_readline("...   ", cont, sizeof(cont))) {
                    break;
                }
                if (cont[0] == '\0') {
                    break;
                }

                cont_len = strlen(cont);
                if (block_len + cont_len + 1 >= sizeof(block)) {
                    console_write("sage: block too large\n");
                    block[0] = '\0';
                    break;
                }
                memcpy(block + block_len, cont, cont_len);
                block_len += cont_len;
                block[block_len++] = '\n';
                block[block_len] = '\0';
            }
            if (block[0] == '\0') {
                continue;
            }
        }

        sage_repl_step(block);
    }

    sage_clear_exit_state();
}

void sage_run_file(const char* path) {
    if (path == NULL) return;
    
    static char script_buffer[65536];
    uint32_t to_read = 65535;

    int n = vfs_read(path, 0, script_buffer, to_read);
    if (n <= 0) return;

    script_buffer[n] = '\0';

    /* Check for bytecode magic */

    if (n >= 4 && script_buffer[0] == 'S' && script_buffer[1] == 'G' &&
        script_buffer[2] == 'V' && script_buffer[3] == 'M') {
        
        static MetalVM vm;
        metal_vm_init(&vm);
        vm.write_char = metal_vm_write_char_bridge;
        sage_register_repl_natives(&vm);
        
        if (metal_vm_load_binary(&vm, (const unsigned char*)script_buffer, n)) {
            metal_vm_run(&vm);
            if (vm.error) {
                printf("Runtime error: %s\n", vm.error_msg ? vm.error_msg : "unknown");
            }
        } else {
            printf("sage: invalid bytecode file\n");
        }
    } else {
        sage_execute((const char*)script_buffer);
    }
}

void sage_execute(const char* line) {
    if (line == NULL || line[0] == '\0' || strcmp(line, "--repl") == 0) {
        sage_repl_run();
        return;
    }

    console_putc('\n');
    sage_repl_step(line);
}
