#include "value.h"
#include "module.h"
#include "env.h"
#include "io.h"
#include "parser.h"
#include "lexer.h"
#include "interpreter.h"
#include "repl.h"
#include "sage_libc_shim.h"
#include "console.h"
#include "scheduler.h"
#include "scheduler_ipc_ext.h"
#include "ipc.h"
#include "serial.h"
#include "keyboard.h"
#include "ata.h"
#include "sdhci.h"
#include "net.h"
#include "acpi.h"
#include "wifi_qca6174.h"
#include "pci.h"
#include "smp.h"
#include "swap.h"
#include "sysinfo.h"
#include "idt.h"
#include "bootinfo.h"
#include "vfs.h"
#include "shell.h"
#include "version.h"
#include "dmesg.h"
#include "metal_vm.h"
#include <string.h>
#include <stdio.h>

// External kernel functions
extern void ata_timer_tick(void);
extern void console_periodic_flip(void);
extern void timer_irq(void);
extern SageOSBootInfo* console_boot_info(void);
extern uint32_t console_get_fg(void);

// --- Sage Interpreter Integration ---

static Env* g_sage_env = NULL;
static ModuleCache* g_sage_cache = NULL;

void register_sageos_natives(ModuleCache* cache);

/* --- IPC Native Wrappers (AST Interpreter) --- */

static Value n_ipc_endpoint_create(int argCount, Value* args) {
    (void)argCount; (void)args;
    uint32_t send_cap, recv_cap;
    extern long sys_ipc_endpoint_create(uintptr_t out_send, uintptr_t out_recv);
    if (sys_ipc_endpoint_create((uintptr_t)&send_cap, (uintptr_t)&recv_cap) < 0) return val_nil();
    
    Value res = val_array();
    array_push(&res, val_number((double)send_cap));
    array_push(&res, val_number((double)recv_cap));
    return res;
}

static Value n_ipc_service_register(int argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[0]) || !IS_NUMBER(args[1])) return val_number(-1.0);
    const char* name = AS_STRING(args[0]);
    uint32_t cap_handle = (uint32_t)AS_NUMBER(args[1]);
    extern long sys_ipc_ns_register(const char *name, uint32_t cap_handle);
    return val_number((double)sys_ipc_ns_register(name, cap_handle));
}

static Value n_ipc_service_lookup(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) return val_nil();
    const char* name = AS_STRING(args[0]);
    uint32_t cap_handle;
    extern long sys_ipc_ns_lookup(const char *name, uint32_t *out_cap_handle);
    if (sys_ipc_ns_lookup(name, (uint32_t *)&cap_handle) < 0) return val_nil();
    return val_number((double)cap_handle);
}

// Stubs for missing interpreter dependencies
void bytecode_program_free(void* p) { (void)p; }
void vm_mark_roots(void* vm) { (void)vm; }
void* parse_program(const char* s) { (void)s; return NULL; }
void bytecode_program_init(void* p) { (void)p; }
void bytecode_compile_program(void* p, void* ast) { (void)p; (void)ast; }
void bytecode_program_read_file(void* p, const char* path) { (void)p; (void)path; }
void bytecode_program_write_file(void* p, const char* path) { (void)p; (void)path; }
void vm_execute_program(void* vm, void* p) { (void)vm; (void)p; }
void aot_init(void* a) { (void)a; }
void aot_free(void* a) { (void)a; }
void aot_set_var_type(void* a, const char* n, int t) { (void)a; (void)n; (void)t; }
void aot_compile_program(void* a, void* p) { (void)a; (void)p; }
void aot_compile_to_binary(void* a, const char* path) { (void)a; (void)path; }
void run_passes(void* p) { (void)p; }

// Atomic stubs for ARM64
#ifdef __aarch64__
void __aarch64_swp8_acq_rel(void) {}
void __aarch64_cas8_acq_rel(void) {}
void __aarch64_ldadd8_acq_rel(void) {}
#endif

#include "version.h"

void sage_repl_init(void) {
    if (!g_sage_env) {
        g_sage_cache = create_module_cache();
        extern ModuleCache* global_module_cache;
        global_module_cache = g_sage_cache;

        g_sage_env = env_create(NULL);
        g_global_env = g_sage_env;
        extern void init_stdlib(Env* env);
        init_stdlib(g_sage_env);
        register_sageos_natives(g_sage_cache);

        // Define ABI version constants for runtime/kernel handshake
        env_define_const(g_sage_env, "SAGE_ABI_MAJOR", 14, val_number(SAGE_ABI_MAJOR));
        env_define_const(g_sage_env, "SAGE_ABI_MINOR", 14, val_number(SAGE_ABI_MINOR));
    }
}

void sage_runtime_init(void) {
    dmesg_log("RUNTIME: Initializing SGVM Core...");
    sage_repl_init();
    
    // In the future, this will also:
    // - initialize runtime object allocator
    // - initialize IPC namespace
    // - initialize service registry
    // - initialize capability manager
    
    dmesg_log("RUNTIME: SGVM Runtime Bring-up complete.");
}

extern void sage_execute(const char* mod);

static void sage_supervisor_thread(void *arg) {
    (void)arg;
    dmesg_log("RUNTIME: Launching System Supervisor (/etc/sagelang/runtime_manager.sage)...");
    sage_execute("/etc/sagelang/runtime_manager.sage");
    extern void sys_exit(int code);
    sys_exit(0);
}

void sage_execute_init(void) {
    thread_t *t = sched_create_thread("supervisor", sage_supervisor_thread, NULL, THREAD_PRIORITY_NORMAL);
    if (t) {
        dmesg_log("RUNTIME: Successfully spawned System Supervisor (PID 1) in background.");
    } else {
        dmesg_log("RUNTIME: FAILED to spawn System Supervisor!");
    }
}

void sage_import_module(void* vm, const char* name) {
    (void)vm;
    sage_repl_init();
}

static Stmt* sage_parse_string(const char* source) {
    init_lexer(source, "/system/init.sage");
    parser_init();
    return parse();
}

void sage_execute_source(const char* source, const char* name) {
    if (!source) return;
    init_lexer(source, name);
    parser_init();
    
    // Check if we are in a REPL-like context or if we should just execute the whole block
    // For now, we just execute the statements found.
    if (setjmp(g_repl_error_jmp) == 0) {
        while (1) {
            Stmt* program = parse();
            if (program == NULL) break;
            interpret(program, g_sage_env);
        }
    } else {
        console_write("\n[RUNTIME MANAGER EXCEPTION CAUGHT!]\n");
    }
}

int sage_execute_file(const char* path) {
    VfsStat st;
    int err = vfs_stat(path, &st);
    if (err != VFS_OK) {
        char err_buf[64];
        extern int sprintf(char* str, const char* format, ...);
        sprintf(err_buf, " (vfs_stat failed: %d)\n", err);
        console_write(err_buf);
        return -1;
    }
    if (st.type != VFS_FILE) {
        console_write(" (not a file)\n");
        return -1;
    }
    char sz_buf[64];
    extern int sprintf(char* str, const char* format, ...);
    sprintf(sz_buf, " (size: %d bytes)\n", (int)st.size);
    console_write(sz_buf);

    char* source = (char*)malloc((size_t)st.size + 1);
    if (!source) {
        console_write("sage: out of memory reading file\n");
        return -1;
    }
    
    int read_bytes = vfs_read(path, 0, source, (size_t)st.size);
    source[st.size] = 0;
    
    sprintf(sz_buf, " (read: %d bytes)\n", read_bytes);
    console_write(sz_buf);
    
    sage_execute_source(source, path);
    
    free(source);
    return 0;
}

void sage_execute(const char* mod) {
    sage_repl_init();
    
    if (mod == NULL || *mod == 0) {
        // REPL mode (not fully implemented here as a block, usually handled via sage_repl_step)
        return;
    }
    
    // Heuristic to decide if this is a file path or direct code
    bool is_path = (mod[0] == '/' || mod[0] == '.' || strstr(mod, ".sage") != NULL);
    
    console_write("\nsage: executing ");
    if (mod) console_write(mod);

    if (is_path) {
        if (sage_execute_file(mod) == 0) {
            console_write(" (ok)\n");
            return;
        }
        console_write(" (file not found or error)\n");
        return; // DO NOT fall through if it was clearly meant to be a path
    }

    console_write(" (direct code)\n");
    sage_execute_source(mod, "direct_code");
}

// --- Input Helpers ---

#define SAGE_KEY_UP      1001
#define SAGE_KEY_DOWN    1002
#define SAGE_KEY_RIGHT   1003
#define SAGE_KEY_LEFT    1004
#define SAGE_KEY_HOME    1005
#define SAGE_KEY_END     1006
#define SAGE_KEY_DELETE  1008

static int key_event_code(const KeyEvent *ev) {
    if (!ev || !ev->pressed) return -1;
    if (ev->extended) {
        switch (ev->scancode) {
        case 0x48: return SAGE_KEY_UP;
        case 0x50: return SAGE_KEY_DOWN;
        case 0x4D: return SAGE_KEY_RIGHT;
        case 0x4B: return SAGE_KEY_LEFT;
        case 0x47: return SAGE_KEY_HOME;
        case 0x4F: return SAGE_KEY_END;
        case 0x53: return SAGE_KEY_DELETE;
        default: return -1;
        }
    }
    if (ev->ascii) return (int)(unsigned char)ev->ascii;
    return -1;
}

// --- OS Natives ---

static Value n_os_version(int argCount, Value* args) {
    (void)argCount; (void)args;
    return val_string(SAGEOS_VERSION);
}

static Value n_os_write_char(int argCount, Value* args) {
    if (argCount >= 1 && IS_NUMBER(args[0])) {
        console_putc((char)AS_NUMBER(args[0]));
    }
    return val_nil();
}

static Value n_os_write_str(int argCount, Value* args) {
    if (argCount >= 1 && IS_STRING(args[0])) {
        console_write(AS_STRING(args[0]));
    }
    return val_nil();
}

static Value n_os_read_char(int argCount, Value* args) {
    (void)argCount; (void)args;
    KeyEvent ev;
    for (;;) {
        if (!keyboard_wait_event(&ev)) return val_number(-1.0);
        int code = key_event_code(&ev);
        if (code >= 0) return val_number((double)code);
    }
}

static Value n_os_read_key(int argCount, Value* args) {
    return n_os_read_char(argCount, args);
}

static Value n_os_poll_char(int argCount, Value* args) {
    (void)argCount; (void)args;
    KeyEvent ev;
    if (keyboard_poll_any_event(&ev) && ev.pressed && ev.ascii) {
        return val_number((double)(unsigned char)ev.ascii);
    }
    return val_number(-1.0);
}

static Value n_os_poll_key(int argCount, Value* args) {
    (void)argCount; (void)args;
    KeyEvent ev;
    if (!keyboard_poll_any_event(&ev)) return val_number(-1.0);
    return val_number((double)key_event_code(&ev));
}

static Value n_os_set_color_hex(int argCount, Value* args) {
    if (argCount >= 1 && IS_NUMBER(args[0])) {
        console_set_fg((uint32_t)AS_NUMBER(args[0]));
    }
    return val_nil();
}

static Value n_os_get_color(int argCount, Value* args) {
    (void)argCount; (void)args;
    return val_number((double)console_get_fg());
}

static uint32_t g_input_start_row = 0;
static uint32_t g_input_start_col = 0;

static Value n_os_input_begin(int argCount, Value* args) {
    (void)argCount; (void)args;
    console_get_cursor(&g_input_start_row, &g_input_start_col);
    return val_nil();
}

static void serial_raw(const char *s) {
    while (*s) serial_putc(*s++);
}
static void serial_dec(uint32_t v) {
    char tmp[12]; int n = 0;
    if (v == 0) { serial_putc('0'); return; }
    while (v && n < (int)sizeof(tmp)) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    while (n > 0) serial_putc(tmp[--n]);
}

static Value n_os_line_redraw(int argCount, Value* args) {
    if (argCount < 4) return val_nil();
    const char *line = AS_STRING(args[0]);
    int pos = (int)AS_NUMBER(args[1]);
    int erase_len = (int)AS_NUMBER(args[2]);
    const char *hint = AS_STRING(args[3]);
    
    int line_len = strlen(line);
    int hint_len = (hint && *hint && strncmp(hint, line, line_len) == 0) ? strlen(hint) : 0;
    int visible_len = hint_len > line_len ? hint_len : line_len;
    
    int saved_echo = console_get_serial_echo();
    console_set_serial_echo(0);
    console_set_cursor(g_input_start_row, g_input_start_col);
    console_write(line);
    if (hint_len > line_len) {
        uint32_t old = console_get_fg();
        console_set_fg(0x606060);
        console_write(hint + line_len);
        console_set_fg(old);
    }
    if (erase_len > visible_len) {
        for (int i = 0; i < erase_len - visible_len; i++) console_putc(' ');
    }
    uint32_t off = g_input_start_col + (uint32_t)pos;
    console_set_cursor(g_input_start_row + off / console_cols(), off % console_cols());
    console_set_serial_echo(saved_echo);
    
    // Serial redraw
    serial_putc('\r');
    serial_raw("\033[0K");
    serial_raw("root@sageos:/# ");
    serial_raw(line);
    if (hint_len > line_len) { serial_raw("\033[90m"); serial_raw(hint + line_len); serial_raw("\033[0m"); }
    serial_putc('\r');
    serial_raw("\033[");
    serial_dec((uint32_t)(15 + pos + 1)); // 15 is len of prompt
    serial_putc('G');
    
    return val_nil();
}

static Value n_os_strlen(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) return val_number(0);
    return val_number((double)strlen(AS_STRING(args[0])));
}

static Value n_os_char_at(int argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[0])) return val_number(-1.0);
    const char* s = AS_STRING(args[0]);
    int idx = (int)AS_NUMBER(args[1]);
    if (idx < 0 || idx >= (int)strlen(s)) return val_number(-1.0);
    return val_number((double)(unsigned char)s[idx]);
}

static Value n_os_substr(int argCount, Value* args) {
    if (argCount < 3 || !IS_STRING(args[0])) return val_string("");
    const char* s = AS_STRING(args[0]);
    int from = (int)AS_NUMBER(args[1]);
    int to = (int)AS_NUMBER(args[2]);
    int len = strlen(s);
    if (from < 0) from = 0; if (to > len) to = len;
    if (from >= to) return val_string("");
    return val_string_len(s + from, to - from);
}

static Value n_os_chr(int argCount, Value* args) {
    if (argCount < 1) return val_string("");
    char buf[2]; buf[0] = (char)AS_NUMBER(args[0]); buf[1] = 0;
    return val_string(buf);
}

static Value n_os_array_push(int argCount, Value* args) {
    if (argCount < 2 || !IS_ARRAY(args[0])) return val_nil();
    array_push(&args[0], args[1]);
    return val_nil();
}

static Value n_os_shell_suggestion(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) return val_string("");
    return val_string(shell_suggestion(AS_STRING(args[0])));
}

static Value n_os_shell_completion_common(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) return val_string("");
    return val_string(shell_completion_common_prefix(AS_STRING(args[0])));
}

static Value n_os_shell_print_completions(int argCount, Value* args) {
    if (argCount < 1 || !IS_STRING(args[0])) return val_nil();
    shell_print_completions(AS_STRING(args[0]));
    return val_nil();
}

static Value n_os_console_clear(int argCount, Value* args) {
    (void)argCount; (void)args;
    console_clear(); return val_nil();
}

static Value n_os_dmesg_log(int argCount, Value* args) {
    if (argCount >= 1 && IS_STRING(args[0])) {
        dmesg_log(AS_STRING(args[0]));
    }
    return val_nil();
}

static void sage_task_entry(void *arg) {
    char *script_path = (char *)arg;
    extern void sage_execute(const char *path);
    sage_execute(script_path);
    free(script_path);
    extern void sys_exit(int code);
    sys_exit(0);
}

static Value n_os_spawn_task(int argCount, Value* args) {
    if (argCount < 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])) {
        return val_number(-1);
    }
    const char *name = AS_STRING(args[0]);
    const char *script_path = AS_STRING(args[1]);

    char *path_copy = malloc(strlen(script_path) + 1);
    if (!path_copy) return val_number(-2);
    strcpy(path_copy, script_path);

    thread_t *t = sched_create_thread(name, sage_task_entry, path_copy, THREAD_PRIORITY_NORMAL);
    if (!t) {
        free(path_copy);
        return val_number(-3);
    }

    t->permissions |= PERM_VFS_CAP_ONLY;

    thread_t *parent = sched_current_thread();
    if (parent) {
        thread_ipc_ext_t *parent_ext = thread_ipc_ext(parent);
        thread_ipc_ext_t *child_ext = thread_ipc_ext(t);
        for (int i = 0; i < IPC_CAP_MAX_PER_TASK; i++) {
            child_ext->cap_table.caps[i] = parent_ext->cap_table.caps[i];
        }
        child_ext->cap_table.next_free = parent_ext->cap_table.next_free;
    }

    return val_number(t->id);
}

// --- Module Registration ---

void register_sageos_natives(ModuleCache* cache) {
    // We register these globally as well for the shell script compatibility
    Environment* env = g_global_env;

    env_define(env, "os_write_char", 13, val_native(n_os_write_char));
    env_define(env, "os_write_str", 12, val_native(n_os_write_str));
    env_define(env, "os_read_char", 12, val_native(n_os_read_char));
    env_define(env, "os_read_key", 11, val_native(n_os_read_key));
    env_define(env, "os_poll_char", 12, val_native(n_os_poll_char));
    env_define(env, "os_poll_key", 11, val_native(n_os_poll_key));
    env_define(env, "os_set_color_hex", 16, val_native(n_os_set_color_hex));
    env_define(env, "os_get_color", 12, val_native(n_os_get_color));
    env_define(env, "os_input_begin", 14, val_native(n_os_input_begin));
    env_define(env, "os_line_redraw", 14, val_native(n_os_line_redraw));
    env_define(env, "os_strlen", 9, val_native(n_os_strlen));
    env_define(env, "os_char_at", 10, val_native(n_os_char_at));
    env_define(env, "os_substr", 9, val_native(n_os_substr));
    env_define(env, "os_chr", 6, val_native(n_os_chr));
    env_define(env, "os_array_push", 13, val_native(n_os_array_push));
    env_define(env, "os_shell_suggestion", 18, val_native(n_os_shell_suggestion));
    env_define(env, "os_shell_completion_common", 25, val_native(n_os_shell_completion_common));
    env_define(env, "os_shell_print_completions", 25, val_native(n_os_shell_print_completions));
    env_define(env, "os_console_clear", 16, val_native(n_os_console_clear));
    env_define(env, "os_dmesg_log", 12, val_native(n_os_dmesg_log));
    env_define(env, "dmesg_log", 9, val_native(n_os_dmesg_log));
    env_define(env, "os_version_string", 17, val_native(n_os_version));
    env_define(env, "os_spawn_task", 13, val_native(n_os_spawn_task));

    // Register 'os' module
    Module* os = create_native_module(cache, "os");
    env_define(os->env, "write_str", 9, val_native(n_os_write_str));
    env_define(os->env, "dmesg_log", 9, val_native(n_os_dmesg_log));
    env_define(os->env, "spawn_task", 10, val_native(n_os_spawn_task));

    // Register 'ipc' module
    Module* ipc = create_native_module(cache, "ipc");
    env_define(ipc->env, "endpoint_create", 15, val_native(n_ipc_endpoint_create));
    env_define(ipc->env, "service_register", 16, val_native(n_ipc_service_register));
    env_define(ipc->env, "service_lookup", 14, val_native(n_ipc_service_lookup));
}
