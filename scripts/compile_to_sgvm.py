
import sys, struct

def parse_sagebc(path):
    with open(path, 'r') as f:
        lines = [l.strip() for l in f.readlines()]
    
    if not lines or lines[0] != "SAGEBC1":
        raise ValueError("Invalid SAGEBC1 header")
    
    functions = []
    chunks = []
    
    i = 1
    while i < len(lines):
        line = lines[i]
        if line.startswith("functions "):
            count = int(line.split()[1])
            i += 1
            for _ in range(count):
                if lines[i] != "function": raise ValueError("Expected function")
                i += 1
                params_count = int(lines[i].split()[1])
                i += 1
                params = []
                for _ in range(params_count):
                    if not lines[i].startswith("param "):
                        raise ValueError("Expected param")
                    plen = int(lines[i].split()[1])
                    i += 1
                    pdata = bytes.fromhex(lines[i])
                    if len(pdata) != plen:
                        raise ValueError("Param length mismatch")
                    params.append(pdata)
                    i += 1
                
                # constants
                consts = []
                if not lines[i].startswith("constants "): raise ValueError("Expected constants")
                c_count = int(lines[i].split()[1])
                i += 1
                for _ in range(c_count):
                    if lines[i].startswith("number "):
                        consts.append(('num', float(lines[i].split()[1])))
                        i += 1
                    elif lines[i].startswith("string "):
                        slen = int(lines[i].split()[1])
                        i += 1
                        sdata = bytes.fromhex(lines[i])
                        consts.append(('str', sdata))
                        i += 1
                
                # code
                if not lines[i].startswith("code "): raise ValueError("Expected code")
                code_len = int(lines[i].split()[1])
                i += 1
                code_data = bytes.fromhex(lines[i])
                i += 1
                if lines[i] != "endfunction": raise ValueError("Expected endfunction")
                i += 1
                functions.append({'params': params, 'consts': consts, 'code': code_data})
        
        elif line.startswith("chunks "):
            count = int(line.split()[1])
            i += 1
            for _ in range(count):
                if lines[i] != "chunk": raise ValueError("Expected chunk")
                i += 1
                consts = []
                if not lines[i].startswith("constants "): raise ValueError("Expected constants")
                c_count = int(lines[i].split()[1])
                i += 1
                for _ in range(c_count):
                    if lines[i].startswith("number "):
                        consts.append(('num', float(lines[i].split()[1])))
                        i += 1
                    elif lines[i].startswith("string "):
                        slen = int(lines[i].split()[1])
                        i += 1
                        sdata = bytes.fromhex(lines[i])
                        consts.append(('str', sdata))
                        i += 1
                if not lines[i].startswith("code "): raise ValueError("Expected code")
                code_len = int(lines[i].split()[1])
                i += 1
                code_data = bytes.fromhex(lines[i])
                i += 1
                if lines[i] != "endchunk": raise ValueError("Expected endchunk")
                i += 1
                chunks.append({'consts': consts, 'code': code_data})
        else:
            i += 1
    return functions, chunks

def fnv1a(data):
    h = 2166136261
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xffffffff
    return h

def pack_consts(consts):
    res = struct.pack("<H", len(consts))
    for ct, cv in consts:
        if ct == 'num':
            res += b'\x01'
            res += struct.pack("<d", cv)
        else:
            res += b'\x02'
            res += struct.pack("<H", len(cv))
            res += cv
    return res

import sys, struct, os

def load_opcodes():
    # Try to find bytecode.h in known locations
    paths = [
        "sageos_build/sage_lang/core/include/bytecode.h",
        "../sageos_build/sage_lang/core/include/bytecode.h",
        "sage_lang/core/include/bytecode.h"
    ]
    
    bytecode_h = None
    for p in paths:
        if os.path.exists(p):
            bytecode_h = p
            break
            
    if not bytecode_h:
        # Fallback to hardcoded if not found, but warn
        print("Warning: bytecode.h not found, using hardcoded fallback")
        return {
            'BC_OP_CONSTANT': 0, 'BC_OP_NIL': 1, 'BC_OP_TRUE': 2, 'BC_OP_FALSE': 3,
            'BC_OP_POP': 4, 'BC_OP_GET_GLOBAL': 5, 'BC_OP_DEFINE_GLOBAL': 6,
            'BC_OP_SET_GLOBAL': 7, 'BC_OP_DEFINE_FUNCTION': 8, 'BC_OP_GET_PROPERTY': 9,
            'BC_OP_SET_PROPERTY': 10, 'BC_OP_GET_INDEX': 11, 'BC_OP_SET_INDEX': 12,
            'BC_OP_SLICE': 13, 'BC_OP_ADD': 14, 'BC_OP_SUB': 15, 'BC_OP_MUL': 16,
            'BC_OP_DIV': 17, 'BC_OP_MOD': 18, 'BC_OP_NEGATE': 19, 'BC_OP_EQUAL': 20,
            'BC_OP_NOT_EQUAL': 21, 'BC_OP_GREATER': 22, 'BC_OP_GREATER_EQUAL': 23,
            'BC_OP_LESS': 24, 'BC_OP_LESS_EQUAL': 25, 'BC_OP_BIT_AND': 26,
            'BC_OP_BIT_OR': 27, 'BC_OP_BIT_XOR': 28, 'BC_OP_BIT_NOT': 29,
            'BC_OP_SHIFT_LEFT': 30, 'BC_OP_SHIFT_RIGHT': 31, 'BC_OP_NOT': 32,
            'BC_OP_TRUTHY': 33, 'BC_OP_JUMP': 34, 'BC_OP_JUMP_IF_FALSE': 35,
            'BC_OP_CALL': 36, 'BC_OP_CALL_METHOD': 37, 'BC_OP_ARRAY': 38,
            'BC_OP_TUPLE': 39, 'BC_OP_DICT': 40, 'BC_OP_PRINT': 41,
            'BC_OP_EXEC_AST_STMT': 42, 'BC_OP_RETURN': 43, 'BC_OP_PUSH_ENV': 44,
            'BC_OP_POP_ENV': 45, 'BC_OP_DUP': 46, 'BC_OP_ARRAY_LEN': 47,
            'BC_OP_BREAK': 48, 'BC_OP_CONTINUE': 49, 'BC_OP_LOOP_BACK': 50,
            'BC_OP_IMPORT': 51, 'BC_OP_CLASS': 52, 'BC_OP_METHOD': 53,
            'BC_OP_INHERIT': 54, 'BC_OP_SETUP_TRY': 55, 'BC_OP_END_TRY': 56,
            'BC_OP_RAISE': 57
        }

    ops = {}
    with open(bytecode_h, 'r') as f:
        in_enum = False
        val = 0
        for line in f:
            line = line.strip()
            if 'typedef enum {' in line:
                in_enum = True
                continue
            if in_enum and '}' in line and 'BytecodeOp;' in line:
                break
            if in_enum:
                parts = line.split(',')
                for p in parts:
                    p = p.strip()
                    if not p or p.startswith('//'): continue
                    name = p.split('=')[0].strip()
                    if name.startswith('BC_OP_'):
                        if '=' in p:
                            val = int(p.split('=')[1].strip().split()[0], 0)
                        ops[name] = val
                        val += 1
    return ops

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 compile_to_sgvm.py <input.bc> <output.sgvm>")
        sys.exit(1)
    
    ops = load_opcodes()
    # Reverse lookup for convenience
    op_names = {v: k for k, v in ops.items()}
    
    src = sys.argv[1]
    dest = sys.argv[2]
    
    functions, chunks = parse_sagebc(src)
    
    blob = bytearray(b"SGVM")
    blob.append(2) # Version 2.0
    
    # Main chunk
    main_consts = []
    main_code = bytearray()
    
    op_const   = ops.get('BC_OP_CONSTANT', 0)
    op_get_g   = ops.get('BC_OP_GET_GLOBAL', 5)
    op_def_g   = ops.get('BC_OP_DEFINE_GLOBAL', 6)
    op_set_g   = ops.get('BC_OP_SET_GLOBAL', 7)
    op_get_p   = ops.get('BC_OP_GET_PROPERTY', 9)
    op_set_p   = ops.get('BC_OP_SET_PROPERTY', 10)
    op_exec_ast = ops.get('BC_OP_EXEC_AST_STMT', 42)
    op_def_fn  = ops.get('BC_OP_DEFINE_FUNCTION', 8)
    op_call_m  = ops.get('BC_OP_CALL_METHOD', 37)
    op_class   = ops.get('BC_OP_CLASS', 52)
    op_method  = ops.get('BC_OP_METHOD', 53)
    op_return  = ops.get('BC_OP_RETURN', 43)

    for chunk in chunks:
        base = len(main_consts)
        main_consts.extend(chunk['consts'])
        code = bytearray(chunk['code'])
        if code and code[-1] == op_return:
            code = code[:-1]
        
        pc = 0
        while pc < len(code):
            op = code[pc]
            # Ops with 2-byte constant index
            if op in [op_const, op_get_g, op_def_g, op_set_g, op_get_p, op_set_p, op_exec_ast, op_method]:
                if pc + 2 >= len(code): break
                idx = (code[pc+1] << 8) | code[pc+2]
                new_idx = idx + base
                code[pc+1] = (new_idx >> 8) & 0xff
                code[pc+2] = new_idx & 0xff
                pc += 3
            elif op == op_def_fn: # DEFINE_FN: name_idx(2), fn_idx(2)
                if pc + 4 >= len(code): break
                idx = (code[pc+1] << 8) | code[pc+2]
                new_idx = idx + base
                code[pc+1] = (new_idx >> 8) & 0xff
                code[pc+2] = new_idx & 0xff
                pc += 5
            elif op == op_call_m: # CALL_METHOD: name_idx(2), arg_count(1)
                if pc + 3 >= len(code): break
                idx = (code[pc+1] << 8) | code[pc+2]
                new_idx = idx + base
                code[pc+1] = (new_idx >> 8) & 0xff
                code[pc+2] = new_idx & 0xff
                pc += 4
            elif op == op_class: # CLASS: name_idx(2), method_count(2), parent_idx(2?)
                # CLASS name_idx(2) method_count(2) has_parent(1) [parent_idx(2)]
                if pc + 5 >= len(code): break
                idx = (code[pc+1] << 8) | code[pc+2]
                new_idx = idx + base
                code[pc+1] = (new_idx >> 8) & 0xff
                code[pc+2] = new_idx & 0xff
                # Check for parent
                has_parent = code[pc+5]
                pc += 6
                if has_parent:
                    if pc + 1 >= len(code): break
                    pidx = (code[pc] << 8) | code[pc+1]
                    new_pidx = pidx + base
                    code[pc] = (new_pidx >> 8) & 0xff
                    code[pc+1] = new_pidx & 0xff
                    pc += 2
            elif op in [ops.get('BC_OP_JUMP', 34), ops.get('BC_OP_JUMP_IF_FALSE', 35), 
                        ops.get('BC_OP_ARRAY', 38), ops.get('BC_OP_TUPLE', 39), ops.get('BC_OP_DICT', 40),
                        ops.get('BC_OP_SETUP_TRY', 55)]:
                pc += 3
            elif op in [ops.get('BC_OP_CALL', 36), ops.get('BC_OP_DUP', 46), 
                        ops.get('BC_OP_BREAK', 48), ops.get('BC_OP_CONTINUE', 49), ops.get('BC_OP_LOOP_BACK', 50)]:
                pc += 2
            else:
                pc += 1
        main_code += code

    main_code.append(op_return)

    blob += pack_consts(main_consts)
    blob += struct.pack("<I", len(main_code))
    blob += main_code
    blob += struct.pack("<H", len(functions))
    for fn in functions:
        blob += struct.pack("<H", len(fn['params']))
        for param in fn['params']:
            blob += struct.pack("<I", fnv1a(param))
        blob += pack_consts(fn['consts'])
        blob += struct.pack("<I", len(fn['code']))
        blob += fn['code']

    with open(dest, "wb") as f:
        f.write(blob)
    print(f"Wrote {len(blob)} bytes to {dest}")

if __name__ == "__main__":
    main()
