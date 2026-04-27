#include "runtime.h"

#include "compiler/bytecode.h"
#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "console.h"
#include "keyboard.h"
#include "metal_vm.h"
#include "sage_alloc.h"

#include <stdio.h>
#include <string.h>

#define SAGE_REPL_LINE_MAX   256
#define SAGE_REPL_BLOCK_MAX  2048
#define SAGE_REPL_PARAM_MAX  8

// Persistent REPL VM
static MetalVM g_repl_vm;
static int g_repl_vm_inited = 0;

static void metal_vm_write_char_bridge(char c) {
    console_putc(c);
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

#include "vfs.h"
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

static void sage_register_repl_natives(MetalVM* vm) {
    metal_vm_register_native(vm, "len", n_len);
    metal_vm_register_native(vm, "os_strlen", n_os_strlen);
    metal_vm_register_native(vm, "os_starts_with", n_os_starts_with);
    metal_vm_register_native(vm, "os_array_len", n_os_array_len);
    metal_vm_register_native(vm, "os_stat", n_os_stat);
}

static void sage_repl_reset_vm(void) {
    metal_vm_init(&g_repl_vm);
    g_repl_vm.write_char = metal_vm_write_char_bridge;
    sage_register_repl_natives(&g_repl_vm);
    g_repl_vm_inited = 1;
}

void sage_repl_init(void) {
    if (!g_repl_vm_inited) {
        sage_repl_reset_vm();
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
    console_write("  :help   show this help\n");
    console_write("  :quit   exit the Sage REPL\n");
    console_write("  :exit   exit the Sage REPL\n");
    console_write("  :reset  reset the Sage REPL session\n");
    console_write("  :clear  clear the console\n");
    console_write("\n");
    console_write("Blocks ending with ':' continue on the next prompt.\n");
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
    for (int i = 0; i < chunk->code_count; i++) {
        uint8_t op = chunk->code[i];
        int operands = 0;

        if (op == BC_OP_CONSTANT ||
            op == BC_OP_GET_GLOBAL || op == BC_OP_DEFINE_GLOBAL || op == BC_OP_SET_GLOBAL ||
            op == BC_OP_GET_PROPERTY || op == BC_OP_SET_PROPERTY ||
            op == BC_OP_CALL_METHOD) {

            uint16_t idx = (uint16_t)((chunk->code[i + 1] << 8) | chunk->code[i + 2]);
            idx += (uint16_t)const_offset;
            chunk->code[i + 1] = (uint8_t)((idx >> 8) & 0xff);
            chunk->code[i + 2] = (uint8_t)(idx & 0xff);
            operands = 2;
        } else if (op == BC_OP_DEFINE_FUNCTION) {
            uint16_t name_idx = (uint16_t)((chunk->code[i + 1] << 8) | chunk->code[i + 2]);
            name_idx += (uint16_t)const_offset;
            chunk->code[i + 1] = (uint8_t)((name_idx >> 8) & 0xff);
            chunk->code[i + 2] = (uint8_t)(name_idx & 0xff);
            operands = 4;
        } else if (op == BC_OP_JUMP || op == BC_OP_JUMP_IF_FALSE || op == BC_OP_LOOP_BACK) {
            operands = 2;
        } else if (op == BC_OP_CALL || op == BC_OP_DUP) {
            operands = 1;
        }

        i += operands;
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

static void sage_repl_run(void) {
    char line[SAGE_REPL_LINE_MAX];
    char block[SAGE_REPL_BLOCK_MAX];

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

void sage_execute(const char* line) {
    if (line == NULL || line[0] == '\0' || strcmp(line, "--repl") == 0) {
        sage_repl_run();
        return;
    }

    console_putc('\n');
    sage_repl_step(line);
}

void sage_run_file(const char* path) {
    if (!path || !*path) {
        printf("sage: no path specified\n");
        return;
    }

    VfsStat st;
    int r = vfs_stat(path, &st);
    if (r < 0) {
        printf("sage: cannot stat %s: %s\n", path, vfs_strerror(r));
        return;
    }

    if (st.type != VFS_FILE) {
        printf("sage: %s is not a file\n", path);
        return;
    }

    /* Limit script size to 1MB for now (we have plenty of RAM) */
    if (st.size > 1024 * 1024) {
        printf("sage: script too large (max 1MB)\n");
        return;
    }

    /* Allocation from kernel space is tricky without a true malloc.
       We'll use a temporary buffer from the sage_heap if it's large enough,
       or just reuse a static buffer. */
    static uint8_t script_buffer[65536]; /* 64KB static buffer for scripts */
    size_t to_read = (size_t)st.size;
    if (to_read > sizeof(script_buffer)) to_read = sizeof(script_buffer);

    int n = vfs_read(path, 0, script_buffer, to_read);
    if (n < 0) {
        printf("sage: error reading %s: %s\n", path, vfs_strerror(n));
        return;
    }

    /* Check for SGVM magic if it's bytecode */
    if (n >= 4 && script_buffer[0] == 'S' && script_buffer[1] == 'G' &&
        script_buffer[2] == 'V' && script_buffer[3] == 'M') {
        
        MetalVM vm;
        metal_vm_init(&vm);
        vm.write_char = metal_vm_write_char_bridge;
        sage_register_repl_natives(&vm);
        
        if (metal_vm_load_binary(&vm, script_buffer, n)) {
            metal_vm_run(&vm);
            if (vm.error) {
                printf("Runtime error: %s\n", vm.error_msg ? vm.error_msg : "unknown");
            }
        } else {
            printf("sage: invalid bytecode file %s\n", path);
        }
    } else {
        /* Fallback to interpreting as source code */
        script_buffer[n] = '\0';
        sage_execute((const char*)script_buffer);
    }
}
