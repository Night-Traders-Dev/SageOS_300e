#include "console.h"
#include "ramfs.h"
#include "metal_vm.h"
#include "sage_libc_shim.h"

// Define a pool for the VM to live in
static MetalVM g_vm;

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return 0;
}

static void decode_hex(const char *src, uint8_t *dest, int len) {
    for (int i = 0; i < len; i++) {
        dest[i] = (uint8_t)((hex_val(src[i*2]) << 4) | hex_val(src[i*2+1]));
    }
}

static const char* find_line(const char* start, const char* end, const char* prefix) {
    const char* p = start;
    int plen = (int)strlen(prefix);
    while (p < end) {
        if (end - p >= plen && memcmp(p, prefix, (size_t)plen) == 0) return p;
        // Move to next line
        while (p < end && *p != '\n') p++;
        if (p < end) p++;
    }
    return NULL;
}

static int parse_int(const char* p) {
    int val = 0;
    while (*p >= '0' && *p <= '9') {
        val = val * 10 + (*p - '0');
        p++;
    }
    return val;
}

static uint64_t parse_double(const char* p) {
    // Simplified double parser for kernel
    union { double d; uint64_t u; } val;
    val.d = 0;
    int neg = 0;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') {
        val.d = val.d * 10 + (*p - '0');
        p++;
    }
    if (*p == '.') {
        p++;
        double div = 10;
        while (*p >= '0' && *p <= '9') {
            val.d += (*p - '0') / div;
            div *= 10;
            p++;
        }
    }
    if (neg) val.d = -val.d;
    return val.u;
}

void sage_run_artifact(const char *data, uint64_t size) {
    MetalVM* vm = &g_vm;
    metal_vm_init(vm);
    vm->write_char = console_putc;

    const char* end = data + size;

    if (size < 8 || memcmp(data, "SAGEBC1", 7) != 0) {
        console_write("sage: error: not a SAGEBC1 artifact\n");
        return;
    }

    // Find the first chunk
    const char* chunk_p = find_line(data, end, "chunk\n");
    if (!chunk_p) {
        console_write("sage: error: no chunk found in artifact\n");
        return;
    }
    chunk_p += 6; // skip "chunk\n"

    // Parse constants
    const char* consts_p = find_line(chunk_p, end, "constants ");
    if (consts_p) {
        int count = parse_int(consts_p + 10);
        const char* p = consts_p;
        // Move to next line after "constants N"
        while (p < end && *p != '\n') p++; if (p < end) p++;

        for (int i = 0; i < count; i++) {
            if (p >= end) break;
            if (memcmp(p, "number ", 7) == 0) {
                double val = parse_double(p + 7);
                metal_vm_add_constant(vm, mv_num(val));
            } else if (memcmp(p, "string ", 7) == 0) {
                int len = parse_int(p + 7);
                // Move to next line (hex payload)
                while (p < end && *p != '\n') p++; if (p < end) p++;
                if (p + len * 2 <= end) {
                    // Intern string (we need a temp buffer or just use the pool)
                    // metal_string_intern will copy it.
                    // We need to decode hex first.
                    char temp[256];
                    if (len < 255) {
                        decode_hex(p, (uint8_t*)temp, len);
                        temp[len] = '\0';
                        metal_vm_add_constant(vm, mv_str(vm, temp, len));
                    }
                }
            }
            // Move to next line
            while (p < end && *p != '\n') p++; if (p < end) p++;
        }
    }

    // Parse code
    const char* code_p = find_line(chunk_p, end, "code ");
    if (code_p) {
        int code_len = parse_int(code_p + 5);
        while (code_p < end && *code_p != '\n') code_p++; if (code_p < end) code_p++;
        
        // We need a buffer for the decoded code.
        // We can use the sage_arena or a static buffer.
        static uint8_t decoded_code[4096];
        if (code_len < 4096) {
            decode_hex(code_p, decoded_code, code_len);
            metal_vm_load(vm, decoded_code, code_len);
            
            console_write("sage: running native bytecode...\n\n");
            int status = metal_vm_run(vm);
            
            if (status != 0) {
                console_write("\nsage: VM error: ");
                console_write(vm->error_msg ? vm->error_msg : "unknown");
                console_write("\n");
            } else {
                console_write("\nsage: execution finished successfully.\n");
            }
        } else {
            console_write("sage: error: bytecode too large\n");
        }
    } else {
        console_write("sage: error: no code section found\n");
    }
}
void sage_execute(const char *module_name) {
    if (!module_name || !*module_name) {
        console_write("sage: REPL not supported in this build\n");
        return;
    }
    const char *data;
    uint64_t size = ramfs_find_size(module_name, &data);
    if (!data) {
        console_write("sage: error: file not found: ");
        console_write(module_name);
        console_write("\n");
        return;
    }
    sage_run_artifact(data, size);
}
