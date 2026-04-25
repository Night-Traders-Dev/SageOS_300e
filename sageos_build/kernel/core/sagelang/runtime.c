#include "runtime.h"
#include "compiler/lexer.h"
#include "compiler/parser.h"
#include "compiler/bytecode.h"
#include "metal_vm.h"
#include <stdio.h>
#include <string.h>

// Persistent REPL VM
static MetalVM g_repl_vm;
static int g_repl_vm_inited = 0;

static void metal_vm_write_char_bridge(char c) {
    console_putc(c);
}

void sage_repl_init() {
    if (!g_repl_vm_inited) {
        metal_vm_init(&g_repl_vm);
        g_repl_vm.write_char = metal_vm_write_char_bridge;
        g_repl_vm_inited = 1;
    }
}

void sage_repl_step(const char* line) {
    sage_repl_init();
    
    // 1. Lexer & Parser
    init_lexer(line, "<repl>");
    parser_init();
    
    Stmt* stmt = parse();
    if (!stmt) return;
    
    // 2. Bytecode Compiler
    BytecodeChunk chunk;
    bytecode_chunk_init(&chunk);
    
    char error[256];
    if (!bytecode_compile_statement(&chunk, stmt, error, sizeof(error))) {
        printf("Compile error: %s\n", error);
        bytecode_chunk_free(&chunk);
        return;
    }
    
    // 3. Load constants into VM
    int const_offset = g_repl_vm.const_count;
    for (int i = 0; i < chunk.constant_count; i++) {
        Value v = chunk.constants[i];
        MetalValue mv;
        
        switch (v.type) {
            case VAL_NUMBER: mv = mv_num(v.as.number); break;
            case VAL_BOOL:   mv = mv_bool(v.as.boolean); break;
            case VAL_NIL:    mv = mv_nil(); break;
            case VAL_STRING: mv = mv_str(&g_repl_vm, v.as.string, (int)strlen(v.as.string)); break;
            default:         mv = mv_nil(); break;
        }
        
        metal_vm_add_constant(&g_repl_vm, mv);
    }
    
    // 4. Patch constant/name indices in bytecode
    for (int i = 0; i < chunk.code_count; i++) {
        uint8_t op = chunk.code[i];
        int operands = 0;
        
        if (op == BC_OP_CONSTANT || 
            op == BC_OP_GET_GLOBAL || op == BC_OP_DEFINE_GLOBAL || op == BC_OP_SET_GLOBAL ||
            op == BC_OP_GET_PROPERTY || op == BC_OP_SET_PROPERTY ||
            op == BC_OP_CALL_METHOD || op == BC_OP_ARRAY || op == BC_OP_TUPLE || op == BC_OP_DICT) {
            
            uint16_t idx = (uint16_t)((chunk.code[i+1] << 8) | chunk.code[i+2]);
            idx += (uint16_t)const_offset;
            chunk.code[i+1] = (uint8_t)((idx >> 8) & 0xff);
            chunk.code[i+2] = (uint8_t)(idx & 0xff);
            operands = 2;
        } else if (op == BC_OP_DEFINE_FUNCTION) {
            uint16_t name_idx = (uint16_t)((chunk.code[i+1] << 8) | chunk.code[i+2]);
            name_idx += (uint16_t)const_offset;
            chunk.code[i+1] = (uint8_t)((name_idx >> 8) & 0xff);
            chunk.code[i+2] = (uint8_t)(name_idx & 0xff);
            operands = 4;
        } else if (op == BC_OP_JUMP || op == BC_OP_JUMP_IF_FALSE || op == BC_OP_LOOP_BACK) {
            operands = 2;
        } else if (op == BC_OP_CALL || op == BC_OP_DUP) {
            operands = 1;
        }
        
        i += operands;
    }
    
    // 5. Run in MetalVM
    metal_vm_load(&g_repl_vm, chunk.code, chunk.code_count);
    metal_vm_run(&g_repl_vm);
    
    if (g_repl_vm.error) {
        printf("Runtime error: %s\n", g_repl_vm.error_msg);
        g_repl_vm.error = 0;
    }
    
    bytecode_chunk_free(&chunk);
}

void sage_execute(const char* line) {
    sage_repl_step(line);
}

void sage_run_file(const char* path) {
    (void)path;
    printf("File execution not yet implemented in kernel REPL.\n");
}
