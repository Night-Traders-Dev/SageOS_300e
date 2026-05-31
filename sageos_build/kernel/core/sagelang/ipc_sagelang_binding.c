/* ============================================================================
 * SageOS IPC — SageLang FFI Binding (MetalVM / SGVM Integration)
 * 
 * This file provides the bridge between SageLang's MetalVM and the C IPC
 * subsystem.  It exports IPC primitives as SGVM native functions that can be
 * called from SageLang bytecode.
 * ============================================================================ */

#include "metal_vm.h"
#include "ipc.h"
#include <string.h>

/* ============================================================================
 * MetalVM Native Function Wrappers
 * 
 * Each wrapper converts MetalVM stack values to C types, calls the IPC
 * subsystem directly, and returns the result as a MetalValue.
 * ============================================================================ */

/* Helper: read a string from a MetalVM value */
static const char *metal_str(MetalVM *vm, MetalValue v) {
    if (v.type == MV_STR) {
        return metal_string_get(vm, v.as.str_idx);
    }
    return NULL;
}

/* Helper: read a uint32 from a MetalVM value */
static uint32_t metal_u32(MetalValue v) {
    if (v.type == MV_NUM) {
        union { double d; uint64_t u; } val;
        val.u = v.as.num_bits;
        return (uint32_t)val.d;
    }
    return 0;
}

/* Helper: read a pointer from a MetalVM value */
static void *metal_ptr(MetalValue v) {
    if (v.type == MV_PTR) return v.as.ptr;
    return NULL;
}

static MetalValue make_int_val(int i) {
    union { double d; uint64_t u; } v;
    v.d = (double)i;
    return mv_num(v.u);
}

static MetalValue make_uint_val(uint64_t u) {
    union { double d; uint64_t u; } v;
    v.d = (double)u;
    return mv_num(v.u);
}

/* --- ipc_channel_create() → [result, local_handle, peer_handle] --- */
static MetalValue sage_ipc_channel_create(MetalVM *vm, MetalValue *args, int argc) {
    (void)args; (void)argc;
    uint32_t local, peer;
    extern long sys_ipc_endpoint_create(uintptr_t out_send, uintptr_t out_recv);
    int r = (int)sys_ipc_endpoint_create((uintptr_t)&local, (uintptr_t)&peer);
    
    int arr = metal_array_new(vm);
    if (arr >= 0) {
        metal_array_push(vm, arr, make_int_val(r));
        metal_array_push(vm, arr, make_uint_val(local));
        metal_array_push(vm, arr, make_uint_val(peer));
        MetalValue v; v.type = MV_ARR; v.as.arr_idx = arr; return v;
    }
    return mv_nil();
}

/* --- ipc_send(handle, msg_type, data_ptr, len, flags) → result --- */
static MetalValue sage_ipc_send(MetalVM *vm, MetalValue *args, int argc) {
    if (argc < 5) return make_int_val(-1);
    uint32_t handle = metal_u32(args[0]);
    uint32_t type   = metal_u32(args[1]);
    void *data      = metal_ptr(args[2]);
    uint32_t len    = metal_u32(args[3]);
    uint32_t flags  = metal_u32(args[4]);

    ipc_msg_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.hdr.type = type;
    msg.hdr.payload_len = len;
    if (data && len > 0) {
        if (len > IPC_MSG_MAX_BYTES) len = IPC_MSG_MAX_BYTES;
        memcpy(msg.payload, data, len);
    }

    extern long sys_ipc_send(uint32_t cap_handle, const ipc_msg_t *user_msg, uint32_t flags);
    int r = (int)sys_ipc_send(handle, &msg, flags);
    return make_int_val(r);
}

/* --- ipc_recv(handle, buf_ptr, max_len, flags) → [result, actual_len, type] --- */
static MetalValue sage_ipc_recv(MetalVM *vm, MetalValue *args, int argc) {
    if (argc < 4) return mv_nil();
    uint32_t handle = metal_u32(args[0]);
    void *buf       = metal_ptr(args[1]);
    uint32_t max_len = metal_u32(args[2]);
    uint32_t flags  = metal_u32(args[3]);

    ipc_msg_t msg;
    extern long sys_ipc_recv(uint32_t cap_handle, ipc_msg_t *user_msg, uint32_t flags);
    int r = (int)sys_ipc_recv(handle, &msg, flags);
    
    int arr = metal_array_new(vm);
    if (arr >= 0) {
        metal_array_push(vm, arr, make_int_val(r));
        if (r == 0) {
            uint32_t actual = msg.hdr.payload_len;
            if (actual > max_len) actual = max_len;
            if (buf && actual > 0) memcpy(buf, msg.payload, actual);
            metal_array_push(vm, arr, make_uint_val(actual));
            metal_array_push(vm, arr, make_uint_val(msg.hdr.type));
        } else {
            metal_array_push(vm, arr, make_uint_val(0));
            metal_array_push(vm, arr, make_uint_val(0));
        }
        MetalValue v; v.type = MV_ARR; v.as.arr_idx = arr; return v;
    }
    return mv_nil();
}

/* --- ipc_port_open(backlog) → [result, port_handle] --- */
static MetalValue sage_ipc_port_open(MetalVM *vm, MetalValue *args, int argc) {
    if (argc < 1) return mv_nil();
    uint32_t backlog = metal_u32(args[0]);
    uint32_t port;
    extern long sys_ipc_port_create(uint32_t backlog, uint32_t *out_cap);
    int r = (int)sys_ipc_port_create(backlog, &port);
    
    int arr = metal_array_new(vm);
    if (arr >= 0) {
        metal_array_push(vm, arr, make_int_val(r));
        metal_array_push(vm, arr, make_uint_val(port));
        MetalValue v; v.type = MV_ARR; v.as.arr_idx = arr; return v;
    }
    return mv_nil();
}

/* --- ipc_service_register(name, handle) → result --- */
static MetalValue sage_ipc_service_register(MetalVM *vm, MetalValue *args, int argc) {
    if (argc < 2) return make_int_val(-1);
    const char *name = metal_str(vm, args[0]);
    uint32_t handle = metal_u32(args[1]);
    if (!name) return make_int_val(-1);
    
    extern long sys_ipc_ns_register(const char *name, uint32_t cap_handle);
    int r = (int)sys_ipc_ns_register(name, handle);
    return make_int_val(r);
}

/* --- ipc_service_lookup(name) → [result, handle] --- */
static MetalValue sage_ipc_service_lookup(MetalVM *vm, MetalValue *args, int argc) {
    if (argc < 1) return mv_nil();
    const char *name = metal_str(vm, args[0]);
    if (!name) return mv_nil();
    
    uint32_t handle;
    extern long sys_ipc_ns_lookup(const char *name, uint32_t *out_cap_handle);
    int r = (int)sys_ipc_ns_lookup(name, &handle);
    
    int arr = metal_array_new(vm);
    if (arr >= 0) {
        metal_array_push(vm, arr, make_int_val(r));
        metal_array_push(vm, arr, make_uint_val(handle));
        MetalValue v; v.type = MV_ARR; v.as.arr_idx = arr; return v;
    }
    return mv_nil();
}

/* ============================================================================
 * Registration Function
 * ============================================================================ */

void ipc_register_metal_natives(MetalVM *vm) {
    metal_vm_register_native(vm, "ipc:channel_create", sage_ipc_channel_create);
    metal_vm_register_native(vm, "ipc:send",           sage_ipc_send);
    metal_vm_register_native(vm, "ipc:recv",           sage_ipc_recv);
    metal_vm_register_native(vm, "ipc:port_open",      sage_ipc_port_open);
    metal_vm_register_native(vm, "ipc:service_register", sage_ipc_service_register);
    metal_vm_register_native(vm, "ipc:service_lookup",   sage_ipc_service_lookup);
}
