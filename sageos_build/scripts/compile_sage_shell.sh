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
                    # param <len> \n <hex>
                    i += 2 # skip param names for now
                
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

# Construct Binary Blob (SGVM)
# Format:
# MAGIC "SGVM" (4)
# MAIN_CONST_COUNT (u16)
# MAIN_CONSTS... [Type(u8), Data]
# MAIN_CODE_LEN (u32)
# MAIN_CODE_BYTES...
# FUNCTION_COUNT (u16)
# FUNCTIONS... [const_count(u16), consts, code_len(u32), code_bytes]

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

# Main chunk
main_chunk = chunks[-1] if chunks else {'consts': [], 'code': b''}
blob += pack_consts(main_chunk['consts'])
blob += struct.pack("<I", len(main_chunk['code']))
blob += main_chunk['code']

# Functions
blob += struct.pack("<H", len(functions))
for fn in functions:
    blob += pack_consts(fn['consts'])
    blob += struct.pack("<I", len(fn['code']))
    blob += fn['code']

# Write C header
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
