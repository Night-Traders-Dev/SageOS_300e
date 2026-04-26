#!/bin/bash
# scripts/compile_sage_shell.sh
#
# Compiles the SageLang shell sources into a single bytecode blob and
# emits it as a C header: kernel/shell/sage_shell_bytecode.h
#
# Usage: bash scripts/compile_sage_shell.sh [/path/to/sage] [output_dir]
#
# The `sage` interpreter must be on PATH or passed as first argument.
# The three .sage sources are concatenated (input.sage + commands.sage +
# shell.sage) so they share a single bytecode chunk, which is what
# MetalVM expects via metal_vm_load().

set -euo pipefail

SAGE="${1:-sage}"
OUT_DIR="${2:-kernel/shell}"

SAGE_SHELL_DIR="sageos_build/kernel/shell"
INPUT="${SAGE_SHELL_DIR}/sage_shell/shell.sage"
COMMANDS="${SAGE_SHELL_DIR}/sage_shell/commands.sage"
INPUT_HELPER="${SAGE_SHELL_DIR}/sage_shell/input.sage"
BYTECODE="${SAGE_SHELL_DIR}/sage_shell.bc"
OUT_H="${SAGE_SHELL_DIR}/sage_shell_bytecode.h"
COMBINED="${SAGE_SHELL_DIR}/sage_shell_combined.sage"

echo "[sage-shell] Combining .sage sources..."
cat "${INPUT_HELPER}" "${COMMANDS}" "${INPUT}" > "${COMBINED}"

echo "[sage-shell] Compiling to bytecode..."
# sage --compile-bytecode outputs a raw binary bytecode file
"${SAGE}" --emit-bytecode "${COMBINED}" -o "${BYTECODE}"

echo "[sage-shell] Generating C header: ${OUT_H}..."
python3 - "${BYTECODE}" "${OUT_H}" <<'PYEOF'
import sys, struct

src  = sys.argv[1]
dest = sys.argv[2]

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
                    i += 2 # skip param names
                
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
                functions.append({'params': params_count, 'consts': consts, 'code': code_data})
        
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

try:
    functions, chunks = parse_sagebc(src)
except Exception as e:
    print(f"Error parsing bytecode: {e}")
    sys.exit(1)

blob = bytearray(b"SGVM")

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

# Main chunk (concatenate all chunks and merge constants)
main_consts = []
main_code = bytearray()

for chunk in chunks:
    base = len(main_consts)
    main_consts.extend(chunk['consts'])
    
    code = bytearray(chunk['code'])
    if code and code[-1] == 43: # BC_OP_RETURN
        code = code[:-1]
    
    pc = 0
    while pc < len(code):
        op = code[pc]
        # Opcodes with u16 constant/name indices
        if op in [0, 5, 6, 7, 9, 10, 42, 61, 63]:
            if pc + 2 >= len(code): break
            idx = (code[pc+1] << 8) | code[pc+2]
            new_idx = idx + base
            code[pc+1] = (new_idx >> 8) & 0xff
            code[pc+2] = new_idx & 0xff
            pc += 3
        elif op == 8: # DEFINE_FN
            if pc + 4 >= len(code): break
            idx = (code[pc+1] << 8) | code[pc+2]
            new_idx = idx + base
            code[pc+1] = (new_idx >> 8) & 0xff
            code[pc+2] = new_idx & 0xff
            pc += 5
        elif op == 37: # CALL_METHOD
            if pc + 3 >= len(code): break
            idx = (code[pc+1] << 8) | code[pc+2]
            new_idx = idx + base
            code[pc+1] = (new_idx >> 8) & 0xff
            code[pc+2] = new_idx & 0xff
            pc += 4
        elif op == 62: # CLASS
            if pc + 5 >= len(code): break
            idx = (code[pc+1] << 8) | code[pc+2]
            new_idx = idx + base
            code[pc+1] = (new_idx >> 8) & 0xff
            code[pc+2] = new_idx & 0xff
            pc += 6
        elif op in [34, 35, 38, 39, 40, 60, 65]:
            pc += 3
        elif op in [36, 46, 48, 49, 50, 56]:
            pc += 2
        else:
            pc += 1
    main_code += code

main_code.append(43) # End with RETURN

blob += pack_consts(main_consts)
blob += struct.pack("<I", len(main_code))
blob += main_code

blob += struct.pack("<H", len(functions))
for fn in functions:
    blob += pack_consts(fn['consts'])
    blob += struct.pack("<I", len(fn['code']))
    blob += fn['code']

size = len(blob)
with open(dest, "w") as f:
    f.write("/* Auto-generated binary SGVM artifact — DO NOT EDIT */\n")
    f.write("#pragma once\n")
    f.write("#include <stdint.h>\n")
    f.write(f"static const uint8_t sage_shell_bytecode[] = {{\n")
    for i in range(0, size, 16):
        row = blob[i:i+16]
        f.write("    " + ", ".join(f"0x{b:02x}" for b in row) + ",\n")
    f.write("};\n")
    f.write(f"static const int sage_shell_bytecode_len = {size};\n")

print(f"[sage-shell] Wrote {size} bytes (binary SGVM) -> {dest}")
PYEOF

echo "[sage-shell] Done. Bytecode header ready at ${OUT_H}"
