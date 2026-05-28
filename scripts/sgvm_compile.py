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
                    if not lines[i].startswith("param "):
                        raise ValueError("Expected param")
                    plen = int(lines[i].split()[1])
                    i += 1
                    pdata = bytes.fromhex(lines[i])
                    if len(pdata) != plen:
                        raise ValueError("Param length mismatch")
                    params.append(pdata)
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

try:
    functions, chunks = parse_sagebc(src)
except Exception as e:
    print(f"Error parsing bytecode: {e}")
    sys.exit(1)

blob = bytearray(b"SGVM")

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
        if op in [0, 5, 6, 7, 9, 10, 42, 61, 63]:
            if pc + 2 >= len(code): break
            idx = (code[pc+1] << 8) | code[pc+2]
            new_idx = idx + base
            code[pc+1] = (new_idx >> 8) & 0xff
            code[pc+2] = new_idx & 0xff
            pc += 3
        elif op == 8:
            if pc + 4 >= len(code): break
            idx = (code[pc+1] << 8) | code[pc+2]
            new_idx = idx + base
            code[pc+1] = (new_idx >> 8) & 0xff
            code[pc+2] = new_idx & 0xff
            pc += 5
        elif op == 37:
            if pc + 3 >= len(code): break
            idx = (code[pc+1] << 8) | code[pc+2]
            new_idx = idx + base
            code[pc+1] = (new_idx >> 8) & 0xff
            code[pc+2] = new_idx & 0xff
            pc += 4
        elif op == 62:
            if pc + 5 >= len(code): break
            idx = (code[pc+1] << 8) | code[pc+2]
            new_idx = idx + base
            code[pc+1] = (new_idx >> 8) & 0xff
            code[pc+2] = new_idx & 0xff
            pc += 6
        elif op in [34, 35, 38, 39, 40, 48, 49, 50, 51, 60, 65]:
            pc += 3
        elif op in [36]:
            pc += 2
        else:
            pc += 1
    main_code += code

main_code.append(43)

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
