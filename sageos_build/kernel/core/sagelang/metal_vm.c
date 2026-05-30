// ============================================================================
// SageMetal VM — Freestanding Bytecode Virtual Machine
// ============================================================================
// No malloc, no libc, no OS. Pure static pools and bump allocators.
// Compiles with: -ffreestanding -nostdlib -DSAGE_BARE_METAL -DSAGE_METAL_VM
// ============================================================================

#include "metal_vm.h"
#include <stdint.h>
#include <string.h>
#include "sage_libc_shim.h"
#include "console.h"
#include "vm_core_shared.h"
#include "vm_hal.h"

#if OP_CALL == 8
#error OP_CALL IS 8
#endif

// Bare-metal HAL implementations
void* vm_memset(void* s, int c, size_t n) { return memset(s, c, n); }
void* vm_memcpy(void* dest, const void* src, size_t n) { return memcpy(dest, src, n); }
size_t vm_strlen(const char* s) { return strlen(s); }
void vm_console_write(const char* s) { console_write(s); }
void vm_console_u32(uint32_t v) { console_u32(v); }
extern void* sage_malloc(size_t size);
void* vm_alloc(size_t size) { return sage_malloc(size); }
void vm_free(void* ptr) { (void)ptr; }

// ============================================================================
// Helpers
// ============================================================================

unsigned int metal_fnv1a(const char* s) {
    unsigned int hash = 2166136261u;
    while (*s) { hash ^= (unsigned char)(*s++); hash *= 16777619u; }
    return hash;
}

static int read_u8(MetalVM* vm) {
    if (vm->ip >= vm->code_length) {
        vm->error = 1;
        vm->error_msg = "Metal VM: code read out of bounds";
        return 0;
    }
    return vm->code[vm->ip++];
}

static int read_u16(MetalVM* vm) {
    if (vm->ip + 1 >= vm->code_length) {
        vm->error = 1;
        vm->error_msg = "Metal VM: code read out of bounds";
        return 0;
    }
    int hi = vm->code[vm->ip++];
    int lo = vm->code[vm->ip++];
    return (hi << 8) | lo;
}

static void metal_print_str(MetalVM* vm, const char* s) {
    if (!vm->write_char) return;
    while (*s) vm->write_char(*s++);
}

static void metal_print_int(MetalVM* vm, long long n) {
    if (n < 0) { if (vm->write_char) vm->write_char('-'); n = -n; }
    char buf[24];
    int i = 0;
    if (n == 0) { buf[i++] = '0'; }
    else { while (n > 0) { buf[i++] = '0' + (int)(n % 10); n /= 10; } }
    while (--i >= 0) if (vm->write_char) vm->write_char(buf[i]);
}

static void metal_print_double(MetalVM* vm, double d) {
    if (d == (double)(long long)d && d >= -1e15 && d <= 1e15) {
        metal_print_int(vm, (long long)d);
    } else {
        // Simplified float printing for bare-metal
        if (d < 0) { if (vm->write_char) vm->write_char('-'); d = -d; }
        long long integer = (long long)d;
        metal_print_int(vm, integer);
        if (vm->write_char) vm->write_char('.');
        double frac = d - (double)integer;
        for (int i = 0; i < 6; i++) {
            frac *= 10.0;
            int digit = (int)frac;
            if (vm->write_char) vm->write_char('0' + digit);
            frac -= digit;
        }
    }
}

// ============================================================================
// Value Constructors
// ============================================================================

MetalValue mv_nil(void) {
    MetalValue v; v.type = MV_NIL; v.as.num_bits = 0; return v;
}

MetalValue mv_num(uint64_t val) {
    MetalValue v; v.type = MV_NUM; v.as.num_bits = val; return v;
}

MetalValue mv_bool(int val) {
    MetalValue v; v.type = MV_BOOL; v.as.boolean = val ? 1 : 0; return v;
}

MetalValue mv_str(MetalVM* vm, const char* s, int len) {
    MetalValue v; v.type = MV_STR;
    v.as.str_idx = metal_string_intern(vm, s, len);
    return v;
}

MetalValue mv_ptr(void* p) {
    MetalValue v; v.type = MV_PTR; v.as.ptr = p; return v;
}

MetalValue metal_vm_string_new(MetalVM* vm, const char* s) {
    return mv_str(vm, s, (int)strlen(s));
}

// ============================================================================
// VM Init & Load
// ============================================================================

void metal_vm_init(MetalVM* vm) {
    memset(vm, 0, sizeof(MetalVM));
    vm->constants = vm->main_constants;
}

void metal_vm_load(MetalVM* vm, const unsigned char* code, int length) {
    vm->code = code;
    vm->code_length = length;
    vm->ip = 0;
}

static int read_u32_le(const unsigned char* p, int* pos) {
    int v = (p[*pos]) | (p[*pos + 1] << 8) | (p[*pos + 2] << 16) | (p[*pos + 3] << 24);
    *pos += 4;
    return v;
}

static int read_u16_le(const unsigned char* p, int* pos) {
    int v = (p[*pos]) | (p[*pos + 1] << 8);
    *pos += 2;
    return v;
}

static int read_u16_be(MetalVM* vm) {
    if (vm->ip + 1 >= vm->code_length) {
        vm->error = 1;
        vm->error_msg = "Metal VM: code read out of bounds";
        return 0;
    }
    int hi = vm->code[vm->ip++];
    int lo = vm->code[vm->ip++];
    return (hi << 8) | lo;
}


static int load_const_pool(MetalVM* vm, const unsigned char* data, int length, int* pos, MetalValue* pool, int max) {
    if (*pos + 2 > length) return 0;
    int count = read_u16_le(data, pos);
    for (int i = 0; i < count; i++) {
        if (*pos >= length) break;
        int type = data[(*pos)++];
        MetalValue v;
        if (type == 1) { // Num
            if (*pos + 8 > length) break;
            memcpy(&v.as.num_bits, &data[*pos], 8);
            v.type = MV_NUM;
            *pos += 8;
        } else if (type == 2) { // Str
            if (*pos + 2 > length) break;
            int slen = read_u16_le(data, pos);
            if (*pos + slen > length) break;
            v = mv_str(vm, (const char*)&data[*pos], slen);
            if (v.as.str_idx < 0) {
                console_write("Metal VM: FAILED TO INTERN STRING\n");
            }
            *pos += slen;
        } else {
            v = mv_nil();
        }
        if (i < max) pool[i] = v;
    }
    return count < max ? count : max;
}

int metal_vm_load_binary(MetalVM* vm, const unsigned char* data, int length) {
    int pos = 0;
    if (length < 8) {
        console_write("load_binary: length < 8\n");
        return 0;
    }
    
    // Magic "SGVM"
    if (data[0] != 'S' || data[1] != 'G' || data[2] != 'V' || data[3] != 'M') {
        console_write("load_binary: magic mismatch\n");
        return 0;
    }
    pos = 4;
    
    // Main Constants
    vm->main_const_count = load_const_pool(vm, data, length, &pos, vm->main_constants, METAL_CONST_POOL);
    vm->constants = vm->main_constants;
    vm->const_count = vm->main_const_count;
    
    // Main Code
    if (pos + 4 > length) return 0;
    int main_len = read_u32_le(data, &pos);
    if (pos + main_len > length) return 0;
    vm->code = &data[pos];
    vm->code_length = main_len;
    vm->ip = 0;
    pos += main_len;
    
    // Functions
    if (pos + 2 > length) return 1; // No functions? That's okay.
    int fn_count = read_u16_le(data, &pos);
    if (fn_count < 0 || fn_count > (int)(sizeof(vm->functions)/sizeof(vm->functions[0]))) {
        console_write("load_binary: too many functions\n");
        return 0;
    }
    vm->fn_count = fn_count;
    for (int i = 0; i < fn_count; i++) {
        if (pos + 2 > length) return 0;
        int param_count = read_u16_le(data, &pos);
        if (param_count < 0 || param_count > 8) {
            console_write("load_binary: too many params\n");
            return 0;
        }
        vm->functions[i].param_count = param_count;
        for (int p = 0; p < param_count; p++) {
            if (pos + 4 > length) return 0;
            vm->functions[i].param_name_hashes[p] = (unsigned int)read_u32_le(data, &pos);
        }

        // Load function's constants onto heap
        if (pos + 2 > length) return 0;
        int c_count_peek = (data[pos] | (data[pos+1] << 8));
        if (vm->heap_used + c_count_peek * sizeof(MetalValue) > METAL_HEAP_SIZE) {
            console_write("load_binary: heap overflow at fn constants\n");
            return 0;
        }
        MetalValue* pool = (MetalValue*)&vm->heap[vm->heap_used];
        int actual_count = load_const_pool(vm, data, length, &pos, pool, c_count_peek);
        vm->functions[i].constants = pool;
        vm->functions[i].const_count = actual_count;
        vm->heap_used += actual_count * sizeof(MetalValue);
        
        // Function Code
        if (pos + 4 > length) return 0;
        int flen = read_u32_le(data, &pos);
        if (pos + flen > length) return 0;
        vm->functions[i].code = &data[pos];
        vm->functions[i].code_length = flen;
        pos += flen;
    }
    
    return 1;
}

int metal_vm_add_constant(MetalVM* vm, MetalValue value) {
    if (vm->main_const_count >= METAL_CONST_POOL) return -1;
    vm->main_constants[vm->main_const_count] = value;
    vm->main_const_count++;
    vm->constants = vm->main_constants;
    vm->const_count = vm->main_const_count;
    return vm->main_const_count - 1;
}

// ============================================================================
// Stack Operations
// ============================================================================

int metal_vm_push(MetalVM* vm, MetalValue value) {
    if (vm->sp >= METAL_STACK_SIZE) {
        vm->error = 1;
        vm->error_msg = "Metal VM: stack overflow";
        return 0;
    }
    vm->stack[vm->sp++] = value;
    return 1;
}

MetalValue metal_vm_pop(MetalVM* vm) {
    if (vm->sp <= 0) return mv_nil();
    return vm->stack[--vm->sp];
}

MetalValue metal_vm_peek(MetalVM* vm, int distance) {
    int idx = vm->sp - 1 - distance;
    if (idx < 0 || idx >= vm->sp) return mv_nil();
    return vm->stack[idx];
}

// ============================================================================
// String Pool (bump allocator)
// ============================================================================

int metal_string_intern(MetalVM* vm, const char* s, int len) {
    if (len < 0) return -1;
    
    // Check if already interned
    int search = 0;
    while (search < vm->string_used) {
        const char* existing = &vm->strings[search];
        int existing_len = (int)strlen(existing);
        if (existing_len == len) {
            int match = 1;
            for (int i = 0; i < len; i++) {
                if (existing[i] != s[i]) { match = 0; break; }
            }
            if (match) return search;
        }
        search += existing_len + 1;
    }

    // Allocate new
    if (vm->string_used + len + 1 > METAL_STRING_POOL) {
        console_write("Metal VM: STRING POOL OVERFLOW\n");
        return -1;
    }
    
    int idx = vm->string_used;
    memcpy(&vm->strings[idx], s, (unsigned long)len);
    vm->strings[idx + len] = '\0';
    vm->string_used += len + 1;
    return idx;
}

const char* metal_string_get(MetalVM* vm, int idx) {
    if (idx < 0 || idx >= vm->string_used) return "";
    return &vm->strings[idx];
}

// ============================================================================
// String Utilities for Native Callbacks
// ============================================================================

void metal_string_concat(MetalVM* vm, int idx_a, int idx_b, int* out_idx) {
    const char* a = metal_string_get(vm, idx_a);
    const char* b = metal_string_get(vm, idx_b);
    int la = (int)strlen(a);
    int lb = (int)strlen(b);
    
    if (la < 0 || lb < 0 || la > 0x3FFFFFFF || lb > 0x3FFFFFFF) { *out_idx = -1; return; }
    int total = la + lb;
    
    // Check if combined length is reasonable
    if (total > 4096) { *out_idx = -1; return; }
    
    char tmp[4097];
    memcpy(tmp, a, (unsigned long)la);
    memcpy(tmp + la, b, (unsigned long)lb);
    tmp[total] = '\0';
    
    *out_idx = metal_string_intern(vm, tmp, total);
}

void metal_num_to_str(MetalVM* vm, long long n, int* out_idx) {
    char buf[24];
    int i = 0;
    int neg = (n < 0);
    if (neg) { buf[i++] = '-'; n = -n; }
    if (n == 0) { buf[i++] = '0'; }
    else {
        char tmp[20]; int j = 0;
        while (n > 0) { tmp[j++] = '0' + (int)(n % 10); n /= 10; }
        while (j > 0) buf[i++] = tmp[--j];
    }
    buf[i] = '\0';
    *out_idx = metal_string_intern(vm, buf, i);
}

// ============================================================================
// Native Function Dispatch Table
// ============================================================================

void scope_define(MetalVM* vm, unsigned int hash, int name_const_idx, MetalValue value);

int metal_vm_register_native(MetalVM* vm, const char* name, MetalNativeFn fn) {
    if (vm->native_count >= METAL_NATIVE_MAX) {
        extern void console_write(const char* s);
        console_write("Metal VM: native table full: ");
        console_write(name);
        console_write("\n");
        return 0;
    }
    vm->natives[vm->native_count].name_hash = metal_fnv1a(name);
    vm->natives[vm->native_count].fn = fn;
    vm->native_count++;
    scope_define(vm, metal_fnv1a(name), -1, mv_str(vm, name, (int)strlen(name)));
    return 1;
}

MetalNativeFn metal_vm_find_native(MetalVM* vm, unsigned int hash) {
    for (int i = 0; i < vm->native_count; i++) {
        if (vm->natives[i].name_hash == hash)
            return vm->natives[i].fn;
    }
    return (MetalNativeFn)0;
}

// ============================================================================
// Array Pool
// ============================================================================

int metal_array_new(MetalVM* vm) {
    int max = (int)(sizeof(vm->arrays) / sizeof(vm->arrays[0]));
    if (vm->array_count >= max) return -1;
    int idx = vm->array_count++;
    vm->arrays[idx].count = 0;
    vm->arrays[idx].in_use = 1;
    return idx;
}

void metal_array_push(MetalVM* vm, int arr_idx, MetalValue val) {
    int max = (int)(sizeof(vm->arrays) / sizeof(vm->arrays[0]));
    if (arr_idx < 0 || arr_idx >= max) return;
    MetalArray* a = &vm->arrays[arr_idx];
    if (a->count >= METAL_ARRAY_MAX_ELEMS) return;
    a->elems[a->count++] = val;
}

MetalValue metal_array_get(MetalVM* vm, int arr_idx, int index) {
    int max = (int)(sizeof(vm->arrays) / sizeof(vm->arrays[0]));
    if (arr_idx < 0 || arr_idx >= max) return mv_nil();
    MetalArray* a = &vm->arrays[arr_idx];
    if (index < 0 || index >= a->count) return mv_nil();
    return a->elems[index];
}

int metal_array_len(MetalVM* vm, int arr_idx) {
    int max = (int)(sizeof(vm->arrays) / sizeof(vm->arrays[0]));
    if (arr_idx < 0 || arr_idx >= max) return 0;
    return vm->arrays[arr_idx].count;
}

// ============================================================================
// Dictionary Pool
// ============================================================================

int metal_dict_new(MetalVM* vm) {
    int max = (int)(sizeof(vm->dicts) / sizeof(vm->dicts[0]));
    if (vm->dict_count >= max) return -1;
    int idx = vm->dict_count++;
    vm->dicts[idx].count = 0;
    vm->dicts[idx].in_use = 1;
    return idx;
}

void metal_dict_set(MetalVM* vm, int dict_idx, int key_idx, MetalValue val) {
    int max = (int)(sizeof(vm->dicts) / sizeof(vm->dicts[0]));
    if (dict_idx < 0 || dict_idx >= max) return;
    MetalDict* d = &vm->dicts[dict_idx];
    for (int i = 0; i < d->count; i++) {
        if (d->key_str_idx[i] == key_idx) {
            d->values[i] = val;
            return;
        }
    }
    if (d->count < METAL_DICT_MAX_ENTRIES) {
        d->key_str_idx[d->count] = key_idx;
        d->values[d->count] = val;
        d->count++;
    }
}

MetalValue metal_dict_get(MetalVM* vm, int dict_idx, int key_idx) {
    int max = (int)(sizeof(vm->dicts) / sizeof(vm->dicts[0]));
    if (dict_idx < 0 || dict_idx >= max) return mv_nil();
    MetalDict* d = &vm->dicts[dict_idx];
    for (int i = 0; i < d->count; i++) {
        if (d->key_str_idx[i] == key_idx) {
            return d->values[i];
        }
    }
    return mv_nil();
}

int metal_dict_has(MetalVM* vm, int dict_idx, int key_idx) {
    int max = (int)(sizeof(vm->dicts) / sizeof(vm->dicts[0]));
    if (dict_idx < 0 || dict_idx >= max) return 0;
    MetalDict* d = &vm->dicts[dict_idx];
    for (int i = 0; i < d->count; i++) {
        if (d->key_str_idx[i] == key_idx) {
            return 1;
        }
    }
    return 0;
}

// ============================================================================
// Environment (scope chain — flat array)
// ============================================================================

int scope_lookup(MetalVM* vm, unsigned int hash, MetalValue* out) {
    for (int d = vm->scope_depth; d >= 0; d--) {
        MetalScope* s = &vm->scopes[d];
        for (int i = 0; i < s->count; i++) {
            if (s->name_hash[i] == (int)hash) {
                *out = s->values[i];
                return 1;
            }
        }
    }
    return 0;
}

void scope_define(MetalVM* vm, unsigned int hash, int name_const_idx, MetalValue value) {
    MetalScope* s = &vm->scopes[vm->scope_depth];
    for (int i = 0; i < s->count; i++) {
        if (s->name_hash[i] == (int)hash) {
            s->values[i] = value;
            return;
        }
    }
    if (s->count >= METAL_VARS_PER_SCOPE) {
        extern void console_write(const char* s);
        console_write("Metal VM: scope full\n");
        return;
    }
    s->name_hash[s->count] = (int)hash;
    s->name_const_idx[s->count] = name_const_idx;
    s->values[s->count] = value;
    s->count++;
}

static void scope_assign(MetalVM* vm, unsigned int hash, MetalValue value) {
    for (int d = vm->scope_depth; d >= 0; d--) {
        MetalScope* s = &vm->scopes[d];
        for (int i = 0; i < s->count; i++) {
            if (s->name_hash[i] == (int)hash) {
                s->values[i] = value;
                return;
            }
        }
    }
    scope_define(vm, hash, -1, value);
}

// ============================================================================
// Print
// ============================================================================

void metal_print_value(MetalVM* vm, MetalValue value) {
    switch (value.type) {
        case MV_NUM: {
            union { double d; uint64_t u; } val;
            val.u = value.as.num_bits;
            metal_print_double(vm, val.d);
            break;
        }
        case MV_BOOL:
            metal_print_str(vm, value.as.boolean ? "true" : "false");
            break;
        case MV_STR:
            metal_print_str(vm, metal_string_get(vm, value.as.str_idx));
            break;
        case MV_ARR: {
            metal_print_str(vm, "[");
            int len = metal_array_len(vm, value.as.arr_idx);
            for (int i = 0; i < len; i++) {
                if (i > 0) metal_print_str(vm, ", ");
                metal_print_value(vm, metal_array_get(vm, value.as.arr_idx, i));
            }
            metal_print_str(vm, "]");
            break;
        }
        case MV_DICT:
            metal_print_str(vm, "{...}");
            break;
        case MV_PTR:
            metal_print_str(vm, "<ptr>");
            break;
        case MV_FN:
            metal_print_str(vm, "<proc>");
            break;
        case MV_NIL:
        default:
            metal_print_str(vm, "nil");
            break;
    }
}

// ============================================================================
// Truthiness
// ============================================================================

static int metal_truthy(MetalValue v) {
    switch (v.type) {
        case MV_NIL:  return 0;
        case MV_BOOL: return v.as.boolean;
        case MV_NUM: {
            union { double d; uint64_t u; } val;
            val.u = v.as.num_bits;
            return val.d != 0.0;
        }
        default:      return 1;
    }
}

// ============================================================================
// Main Dispatch Loop
// ============================================================================

int metal_vm_step(MetalVM* vm) {
    if (vm->halted || vm->error || vm->ip >= vm->code_length) return 0;
    
    int op = read_u8(vm);
    if (vm->error) return 0;

    switch (op) {
        case OP_HALT: vm->halted = 1; return 0;

        case OP_CONSTANT: {
            int idx = read_u16(vm);
            if (vm->error) return 0;
            if (idx >= 0 && idx < vm->const_count)
                metal_vm_push(vm, vm->constants[idx]);
            else { vm->error = 1; vm->error_msg = "OP_CONSTANT index error"; return 0; }
            break;
        }

        case OP_NIL:   metal_vm_push(vm, mv_nil()); break;
        case OP_TRUE:  metal_vm_push(vm, mv_bool(1)); break;
        case OP_FALSE: metal_vm_push(vm, mv_bool(0)); break;
        case OP_POP:   metal_vm_pop(vm); break;
        
        case OP_DUP: {
            if (vm->sp < 1) { vm->error = 1; vm->error_msg = "OP_DUP: stack underflow"; return 0; }
            metal_vm_push(vm, vm->stack[vm->sp - 1]);
            break;
        }

        case OP_DEFINE_GLOBAL: {
            int idx = read_u16(vm);
            if (vm->error) return 0;
            MetalValue val = metal_vm_pop(vm);
            const char* name = metal_string_get(vm, vm->constants[idx].as.str_idx);
            scope_define(vm, metal_fnv1a(name), idx, val);
            break;
        }

        case OP_GET_GLOBAL: {
            int idx = read_u16(vm);
            if (vm->error) return 0;
            const char* name = metal_string_get(vm, vm->constants[idx].as.str_idx);
            MetalValue val;
            if (scope_lookup(vm, metal_fnv1a(name), &val)) metal_vm_push(vm, val);
            else metal_vm_push(vm, mv_nil());
            break;
        }

        case OP_SET_GLOBAL: {
            int idx = read_u16(vm);
            if (vm->error) return 0;
            MetalValue val = metal_vm_pop(vm);
            const char* name = metal_string_get(vm, vm->constants[idx].as.str_idx);
            scope_assign(vm, metal_fnv1a(name), val);
            break;
        }

        case OP_DEFINE_FN: {
            int name_idx = read_u16_be(vm);
            int fn_idx = read_u16_be(vm);
            if (vm->error) return 0;
            const char* name = metal_string_get(vm, vm->constants[name_idx].as.str_idx);
            MetalValue val; val.type = MV_FN; val.as.fn_idx = fn_idx;
            scope_define(vm, metal_fnv1a(name), name_idx, val);
            break;
        }

        case OP_ADD: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            if (a.type == MV_STR && b.type == MV_STR) {
                int out; metal_string_concat(vm, a.as.str_idx, b.as.str_idx, &out);
                MetalValue v; v.type = MV_STR; v.as.str_idx = out; metal_vm_push(vm, v);
            } else {
                union { double d; uint64_t u; } va, vb, vr;
                va.u = a.as.num_bits; vb.u = b.as.num_bits; vr.d = va.d + vb.d;
                metal_vm_push(vm, mv_num(vr.u));
            }
            break;
        }
        case OP_SUB: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            union { double d; uint64_t u; } va, vb, vr;
            va.u = a.as.num_bits; vb.u = b.as.num_bits; vr.d = va.d - vb.d;
            metal_vm_push(vm, mv_num(vr.u));
            break;
        }
        case OP_MUL: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            union { double d; uint64_t u; } va, vb, vr;
            va.u = a.as.num_bits; vb.u = b.as.num_bits; vr.d = va.d * vb.d;
            metal_vm_push(vm, mv_num(vr.u));
            break;
        }
        case OP_DIV: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            union { double d; uint64_t u; } va, vb, vr;
            va.u = a.as.num_bits; vb.u = b.as.num_bits;
            if (vb.d == 0.0) { vm->error = 1; vm->error_msg = "div zero"; return 0; }
            vr.d = va.d / vb.d; metal_vm_push(vm, mv_num(vr.u));
            break;
        }
        case OP_MOD: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            union { double d; uint64_t u; } va, vb, vr;
            va.u = a.as.num_bits; vb.u = b.as.num_bits;
            if (vb.d == 0.0) { vm->error = 1; vm->error_msg = "mod zero"; return 0; }
            vr.d = (double)((long long)va.d % (long long)vb.d);
            metal_vm_push(vm, mv_num(vr.u));
            break;
        }
        case OP_NEGATE: {
            MetalValue a = metal_vm_pop(vm);
            union { double d; uint64_t u; } va, vr;
            va.u = a.as.num_bits; vr.d = -va.d; metal_vm_push(vm, mv_num(vr.u));
            break;
        }

        case OP_EQUAL: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            int eq = 0;
            if (a.type == b.type) {
                switch (a.type) {
                    case MV_NIL:  eq = 1; break;
                    case MV_NUM:  eq = (a.as.num_bits == b.as.num_bits); break;
                    case MV_BOOL: eq = (!a.as.boolean == !b.as.boolean); break;
                    case MV_STR:  eq = (a.as.str_idx == b.as.str_idx); break;
                    case MV_ARR:  eq = (a.as.arr_idx == b.as.arr_idx); break;
                    case MV_DICT: eq = (a.as.dict_idx == b.as.dict_idx); break;
                    case MV_FN:   eq = (a.as.fn_idx == b.as.fn_idx); break;
                    case MV_PTR:  eq = (a.as.ptr == b.as.ptr); break;
                    default:      eq = 0; break;
                }
            }
            metal_vm_push(vm, mv_bool(eq));
            break;
        }
        case OP_NOT_EQUAL: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            int eq = 0;
            if (a.type == b.type) {
                switch (a.type) {
                    case MV_NIL:  eq = 1; break;
                    case MV_NUM:  eq = (a.as.num_bits == b.as.num_bits); break;
                    case MV_BOOL: eq = (!a.as.boolean == !b.as.boolean); break;
                    case MV_STR:  eq = (a.as.str_idx == b.as.str_idx); break;
                    case MV_ARR:  eq = (a.as.arr_idx == b.as.arr_idx); break;
                    case MV_DICT: eq = (a.as.dict_idx == b.as.dict_idx); break;
                    case MV_FN:   eq = (a.as.fn_idx == b.as.fn_idx); break;
                    case MV_PTR:  eq = (a.as.ptr == b.as.ptr); break;
                    default:      eq = 0; break;
                }
            }
            metal_vm_push(vm, mv_bool(!eq));
            break;
        }
        case OP_GREATER: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            union { double d; uint64_t u; } va, vb;
            va.u = a.as.num_bits; vb.u = b.as.num_bits; metal_vm_push(vm, mv_bool(va.d > vb.d));
            break;
        }
        case OP_GREATER_EQ: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            union { double d; uint64_t u; } va, vb;
            va.u = a.as.num_bits; vb.u = b.as.num_bits; metal_vm_push(vm, mv_bool(va.d >= vb.d));
            break;
        }
        case OP_LESS: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            union { double d; uint64_t u; } va, vb;
            va.u = a.as.num_bits; vb.u = b.as.num_bits; metal_vm_push(vm, mv_bool(va.d < vb.d));
            break;
        }
        case OP_LESS_EQ: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            union { double d; uint64_t u; } va, vb;
            va.u = a.as.num_bits; vb.u = b.as.num_bits; metal_vm_push(vm, mv_bool(va.d <= vb.d));
            break;
        }
        case OP_NOT:    metal_vm_push(vm, mv_bool(!metal_truthy(metal_vm_pop(vm)))); break;
        case OP_TRUTHY: metal_vm_push(vm, mv_bool(metal_truthy(metal_vm_pop(vm)))); break;

        case OP_BIT_AND: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            metal_vm_push(vm, mv_num(a.as.num_bits & b.as.num_bits));
            break;
        }
        case OP_BIT_OR: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            metal_vm_push(vm, mv_num(a.as.num_bits | b.as.num_bits));
            break;
        }
        case OP_BIT_XOR: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            metal_vm_push(vm, mv_num(a.as.num_bits ^ b.as.num_bits));
            break;
        }
        case OP_BIT_NOT: {
            MetalValue a = metal_vm_pop(vm);
            metal_vm_push(vm, mv_num(~a.as.num_bits));
            break;
        }
        case OP_SHIFT_LEFT: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            union { double d; uint64_t u; } vb; vb.u = b.as.num_bits;
            metal_vm_push(vm, mv_num(a.as.num_bits << (int)vb.d));
            break;
        }
        case OP_SHIFT_RIGHT: {
            MetalValue b = metal_vm_pop(vm), a = metal_vm_pop(vm);
            union { double d; uint64_t u; } vb; vb.u = b.as.num_bits;
            metal_vm_push(vm, mv_num(a.as.num_bits >> (int)vb.d));
            break;
        }

        case OP_JUMP: vm->ip = read_u16(vm); break;
        case OP_JUMP_IF_FALSE: {
            int off = read_u16(vm);
            if (!metal_truthy(metal_vm_pop(vm))) vm->ip = off;
            break;
        }
        case OP_LOOP_BACK: vm->ip -= read_u16(vm); break;

        case OP_IMPORT: {
            int idx = read_u16(vm);
            if (vm->error) return 0;
            const char* name = metal_string_get(vm, vm->constants[idx].as.str_idx);
            extern void sage_import_module(MetalVM* vm, const char* name);
            sage_import_module(vm, name);
            break;
        }
        case OP_EXEC_AST_STMT: {
            read_u16(vm); vm->error = 1; vm->error_msg = "AST execution not supported"; return 0;
        }

        case OP_PUSH_ENV:
            if (vm->scope_depth < METAL_ENV_DEPTH - 1) {
                vm->scope_depth++; vm->scopes[vm->scope_depth].count = 0;
            }
            break;
        case OP_POP_ENV: if (vm->scope_depth > 0) vm->scope_depth--; break;

        case OP_PRINT: {
            MetalValue v = metal_vm_pop(vm);
            metal_print_value(vm, v);
            if (vm->write_char) vm->write_char('\n');
            break;
        }

        case OP_BREAK:
        case OP_CONTINUE: vm->ip = read_u16(vm); break;

        case OP_SLICE: {
            MetalValue end = metal_vm_pop(vm), start = metal_vm_pop(vm), obj = metal_vm_pop(vm);
            if (obj.type == MV_ARR) {
                int arr = metal_array_new(vm);
                if (arr >= 0) {
                    union { double d; uint64_t u; } vs, ve;
                    vs.u = start.as.num_bits; ve.u = end.as.num_bits;
                    int s = (int)vs.d, e = (int)ve.d, len = metal_array_len(vm, obj.as.arr_idx);
                    if (s < 0) s = 0; if (e > len) e = len;
                    for (int i = s; i < e; i++) metal_array_push(vm, arr, metal_array_get(vm, obj.as.arr_idx, i));
                    MetalValue r; r.type = MV_ARR; r.as.arr_idx = arr; metal_vm_push(vm, r);
                } else { vm->error = 1; vm->error_msg = "array pool full"; return 0; }
            } else if (obj.type == MV_STR) {
                const char* s_ptr = metal_string_get(vm, obj.as.str_idx);
                if (s_ptr) {
                    union { double d; uint64_t u; } vs, ve;
                    vs.u = start.as.num_bits; ve.u = end.as.num_bits;
                    int s = (int)vs.d, e = (int)ve.d, len = (int)strlen(s_ptr);
                    if (s < 0) s = 0; if (e > len) e = len; if (e < s) e = s;
                    metal_vm_push(vm, mv_str(vm, s_ptr + s, e - s));
                } else metal_vm_push(vm, mv_nil());
            } else metal_vm_push(vm, mv_nil());
            break;
        }

        case OP_GET_PROPERTY: {
            int idx = read_u16(vm);
            if (vm->error) return 0;
            MetalValue obj = metal_vm_pop(vm);
            if (obj.type == MV_DICT) {
                metal_vm_push(vm, metal_dict_get(vm, obj.as.dict_idx, idx));
            } else {
                metal_vm_push(vm, mv_nil());
            }
            break;
        }

        case OP_SET_PROPERTY: {
            int idx = read_u16(vm);
            if (vm->error) return 0;
            MetalValue val = metal_vm_pop(vm), obj = metal_vm_pop(vm);
            if (obj.type == MV_DICT) {
                metal_dict_set(vm, obj.as.dict_idx, idx, val);
            }
            metal_vm_push(vm, val);
            break;
        }

        case OP_CALL_METHOD: {
            int argc = read_u8(vm), name_idx = read_u16(vm);
            const char* method_name = metal_string_get(vm, name_idx);
            MetalValue receiver = vm->stack[vm->sp - argc - 1];
            if (receiver.type == MV_ARR) {
                if (method_name && strcmp(method_name, "push") == 0) {
                    metal_array_push(vm, receiver.as.arr_idx, vm->stack[vm->sp - 1]);
                    vm->sp -= (argc + 1); metal_vm_push(vm, mv_nil()); break;
                }
                if (method_name && strcmp(method_name, "len") == 0) {
                    double l = (double)metal_array_len(vm, receiver.as.arr_idx);
                    union { double d; uint64_t u; } v; v.d = l;
                    vm->sp -= (argc + 1); metal_vm_push(vm, mv_num(v.u)); break;
                }
            } else if (receiver.type == MV_STR) {
                const char* s = metal_string_get(vm, receiver.as.str_idx);
                if (method_name && strcmp(method_name, "split") == 0) {
                    console_write("[VM] string.split\n");
                    int arr = metal_array_new(vm);
                    if (arr >= 0) {
                        const char* cur = s;
                        while (*cur) {
                            while (*cur && isspace((unsigned char)*cur)) cur++;
                            if (!*cur) break;
                            const char* token_start = cur;
                            while (*cur && !isspace((unsigned char)*cur)) cur++;
                            int tlen = (int)(cur - token_start);
                            char tbuf[64]; if (tlen > 63) tlen = 63;
                            memcpy(tbuf, token_start, (size_t)tlen); tbuf[tlen] = '\0';
                            metal_array_push(vm, arr, metal_vm_string_new(vm, tbuf));
                        }
                        MetalValue res; res.type = MV_ARR; res.as.arr_idx = arr;
                        vm->sp -= (argc + 1); metal_vm_push(vm, res); break;
                    }
                }
                if (method_name && strcmp(method_name, "strip") == 0) {
                    const char* st = s ? s : "";
                    while (*st && isspace((unsigned char)*st)) st++;
                    const char* en = st + strlen(st);
                    if (en > st) en--;
                    while (en > st && isspace((unsigned char)*en)) en--;
                    int res_len = (en >= st && !isspace((unsigned char)*st)) ? (int)(en - st + 1) : 0;
                    vm->sp -= (argc + 1); metal_vm_push(vm, mv_str(vm, st, res_len)); break;
                }
                if (method_name && (strcmp(method_name, "startswith") == 0 || strcmp(method_name, "endswith") == 0)) {
                    console_write("[VM] string."); console_write(method_name); console_write("\n");
                    MetalValue sub = vm->stack[vm->sp - 1];
                    const char* prefix = (sub.type == MV_STR) ? metal_string_get(vm, sub.as.str_idx) : "";
                    int res = 0;
                    if (s && prefix) {
                        if (method_name[0] == 's') res = (strncmp(s, prefix, strlen(prefix)) == 0);
                        else { int slen = (int)strlen(s), plen = (int)strlen(prefix); if (slen >= plen) res = (strcmp(s + slen - plen, prefix) == 0); }
                    }
                    vm->sp -= (argc + 1); metal_vm_push(vm, mv_bool(res)); break;
                }
            }
            vm->error = 1; vm->error_msg = "method not found"; return 0;
        }

        case OP_CALL: {
            int argc = read_u8(vm);
            MetalValue callee = vm->stack[vm->sp - argc - 1];
            if (callee.type == MV_STR) {
                const char* name = metal_string_get(vm, callee.as.str_idx);
                MetalNativeFn nfn = metal_vm_find_native(vm, metal_fnv1a(name));
                if (nfn) {
                    MetalValue args[16]; int nargs = argc < 16 ? argc : 16;
                    for (int i = 0; i < nargs; i++) args[i] = vm->stack[vm->sp - argc + i];
                    vm->sp -= argc + 1; metal_vm_push(vm, nfn(vm, args, nargs)); break;
                }
            } else if (callee.type == MV_FN) {
                int fn_idx = callee.as.fn_idx;
                if (fn_idx >= 0 && fn_idx < vm->fn_count) {
                    if (vm->frame_count >= METAL_CALL_STACK) { vm->error = 1; vm->error_msg = "stack overflow"; return 0; }
                    MetalFunction* fn = &vm->functions[fn_idx];
                    MetalFrame* frame = &vm->call_stack[vm->frame_count++];
                    frame->ip = vm->ip; frame->code = vm->code; frame->code_length = vm->code_length;
                    frame->sp_base = vm->sp - argc - 1; frame->constants = vm->constants; frame->const_count = vm->const_count;
                    vm->code = fn->code; vm->code_length = fn->code_length; vm->ip = 0;
                    vm->constants = fn->constants; vm->const_count = fn->const_count;
                    if (vm->scope_depth < METAL_ENV_DEPTH - 1) { vm->scope_depth++; vm->scopes[vm->scope_depth].count = 0; }
                    for (int p = 0; p < fn->param_count; p++) {
                        MetalValue arg = mv_nil(); if (p < argc) arg = vm->stack[vm->sp - argc + p];
                        scope_define(vm, fn->param_name_hashes[p], -1, arg);
                    }
                    vm->sp = frame->sp_base; return 1;
                }
            }
            if (callee.type == MV_STR) {
                console_write("\nOP_CALL failed: native function '");
                console_write(metal_string_get(vm, callee.as.str_idx));
                console_write("' not found.\n");
            } else {
                console_write("\nOP_CALL: unsupported target type "); console_u32(callee.type);
                console_write(" at IP: "); console_hex64((uint64_t)(vm->ip - 1));
            }
            vm->error = 1; vm->error_msg = "unsupported call target"; return 0;
        }

        case OP_ARRAY:
        case OP_TUPLE: {
            int count = read_u16(vm);
            int arr = metal_array_new(vm);
            if (arr < 0) { vm->error = 1; vm->error_msg = "array pool full"; return 0; }
            for (int i = 0; i < count; i++) metal_array_push(vm, arr, vm->stack[vm->sp - count + i]);
            vm->sp -= count; MetalValue v; v.type = MV_ARR; v.as.arr_idx = arr; metal_vm_push(vm, v);
            break;
        }
        case OP_DICT: {
            int count = read_u16(vm);
            int dict = metal_dict_new(vm);
            if (dict < 0) { vm->error = 1; vm->error_msg = "dict pool full"; return 0; }
            for (int i = 0; i < count; i++) {
                MetalValue val = metal_vm_pop(vm), key = metal_vm_pop(vm);
                if (key.type == MV_STR) metal_dict_set(vm, dict, key.as.str_idx, val);
            }
            MetalValue v; v.type = MV_DICT; v.as.dict_idx = dict; metal_vm_push(vm, v);
            break;
        }
        case OP_ARRAY_LEN: {
            MetalValue a = metal_vm_pop(vm);
            double l = (a.type == MV_ARR) ? (double)metal_array_len(vm, a.as.arr_idx) : 0.0;
            union { double d; uint64_t u; } vr; vr.d = l; metal_vm_push(vm, mv_num(vr.u));
            break;
        }
        case OP_GET_INDEX: {
            MetalValue idx = metal_vm_pop(vm), obj = metal_vm_pop(vm);
            if (obj.type == MV_ARR) {
                union { double d; uint64_t u; } vi; vi.u = idx.as.num_bits;
                metal_vm_push(vm, metal_array_get(vm, obj.as.arr_idx, (int)vi.d));
            } else if (obj.type == MV_DICT) {
                if (idx.type == MV_STR) metal_vm_push(vm, metal_dict_get(vm, obj.as.dict_idx, idx.as.str_idx));
                else metal_vm_push(vm, mv_nil());
            } else metal_vm_push(vm, mv_nil());
            break;
        }
        case OP_SET_INDEX: {
            MetalValue val = metal_vm_pop(vm), idx = metal_vm_pop(vm), obj = metal_vm_pop(vm);
            if (obj.type == MV_ARR) {
                union { double d; uint64_t u; } vi; vi.u = idx.as.num_bits;
                int ai = obj.as.arr_idx, ii = (int)vi.d, max = (int)(sizeof(vm->arrays)/sizeof(vm->arrays[0]));
                if (ai >= 0 && ai < max && ii >= 0 && ii < vm->arrays[ai].count) vm->arrays[ai].elems[ii] = val;
            } else if (obj.type == MV_DICT) {
                if (idx.type == MV_STR) metal_dict_set(vm, obj.as.dict_idx, idx.as.str_idx, val);
            }
            metal_vm_push(vm, val); break;
        }

        case OP_RETURN: {
            MetalValue res = metal_vm_pop(vm);
            if (vm->frame_count > 0) {
                if (vm->scope_depth > 0) vm->scope_depth--;
                MetalFrame* f = &vm->call_stack[--vm->frame_count];
                vm->ip = f->ip; vm->code = f->code; vm->code_length = f->code_length;
                vm->constants = f->constants; vm->const_count = f->const_count; vm->sp = f->sp_base;
                metal_vm_push(vm, res); break;
            } else { vm->halted = 1; return 0; }
        }

        case 52: case 53: case 54: // OP_CLASS, OP_METHOD, OP_INHERIT
            if (op == 52) { read_u16(vm); read_u16(vm); read_u16(vm); }
            else if (op == 53) read_u16(vm);
            else if (op == 54) metal_vm_pop(vm);
            break;
        case 55: read_u16(vm); read_u16(vm); break; // OP_SETUP_TRY
        case 56: break; // OP_END_TRY
        case 57: metal_vm_pop(vm); vm->error = 1; vm->error_msg = "exception raised"; return 0;

        default:
            vm->error = 1;
            {
                static char err[64]; int val = op;
                char num[4]; num[0] = '0' + (val/100)%10; num[1] = '0' + (val/10)%10; num[2] = '0' + (val%10); num[3] = '\0';
                const char* p = "unknown opcode: "; int i = 0; while (*p) err[i++] = *p++;
                for (int j = 0; j < 3; j++) err[i++] = num[j]; err[i] = '\0';
                vm->error_msg = err;
                console_write("\n[VM ERROR] Unknown Opcode: "); console_u32((uint32_t)op);
                console_write(" at IP: "); console_hex64((uint64_t)(vm->ip - 1)); console_write("\n");
            }
            return 0;
    }
    return 1;
}

int metal_vm_run(MetalVM* vm) {
    while (metal_vm_step(vm));
    return vm->error ? -1 : 0;
}

const char* metal_value_type_name(MetalValueType type) {
    switch (type) {
        case MV_NIL:  return "Nil";
        case MV_NUM:  return "Number";
        case MV_BOOL: return "Bool";
        case MV_STR:  return "String";
        case MV_ARR:  return "Array";
        case MV_DICT: return "Dict";
        case MV_FN:   return "Function";
        case MV_PTR:  return "Ptr";
        default:      return "Unknown";
    }
}

MetalValue metal_vm_lookup(MetalVM* vm, const char* name) {
    MetalValue val;
    if (scope_lookup(vm, metal_fnv1a(name), &val)) return val;
    return mv_nil();
}

MetalValue metal_vm_call(MetalVM* vm, const char* fn_name, MetalValue* args, int argc) {
    MetalValue callee = metal_vm_lookup(vm, fn_name);
    if (callee.type != MV_FN) return mv_nil();
    int start_frame = vm->frame_count, was_halted = vm->halted; vm->halted = 0;
    metal_vm_push(vm, callee); for (int i = 0; i < argc; i++) metal_vm_push(vm, args[i]);
    int fn_idx = callee.as.fn_idx; MetalFunction* fn = &vm->functions[fn_idx];
    if (vm->frame_count >= METAL_CALL_STACK) { vm->error = 1; vm->error_msg = "stack overflow"; vm->halted = was_halted; return mv_nil(); }
    MetalFrame* frame = &vm->call_stack[vm->frame_count++];
    frame->ip = vm->ip; frame->code = vm->code; frame->code_length = vm->code_length;
    frame->sp_base = vm->sp - argc - 1; frame->constants = vm->constants; frame->const_count = vm->const_count;
    vm->code = fn->code; vm->code_length = fn->code_length; vm->ip = 0;
    vm->constants = fn->constants; vm->const_count = fn->const_count;
    if (vm->scope_depth < METAL_ENV_DEPTH - 1) { vm->scope_depth++; vm->scopes[vm->scope_depth].count = 0; }
    for (int p = 0; p < fn->param_count; p++) {
        MetalValue arg = mv_nil(); if (p < argc) arg = vm->stack[vm->sp - argc + p];
        scope_define(vm, fn->param_name_hashes[p], -1, arg);
    }
    vm->sp = frame->sp_base;
    while (vm->frame_count > start_frame && !vm->error && !vm->halted) metal_vm_step(vm);
    vm->halted = was_halted; if (vm->error) return mv_nil(); return metal_vm_pop(vm);
}
