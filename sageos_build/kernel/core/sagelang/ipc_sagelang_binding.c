/* ============================================================================
 * SageOS IPC — SageLang FFI Binding (MetalVM / SGVM Integration)
 * 
 * This file provides the bridge between SageLang's MetalVM and the C IPC
 * subsystem.  It exports IPC primitives as SGVM native functions that can be
 * called from SageLang bytecode.
 * ============================================================================ */

#include "metal_vm.h"
#include "ipc_user.h"
#include <string.h>

/* ============================================================================
 * MetalVM Native Function Wrappers
 * 
 * Each wrapper converts MetalVM stack values to C types, calls the IPC
 * library, and pushes the result back onto the MetalVM stack.
 * ============================================================================ */

/* Helper: read a string from a MetalVM value (string or symbol) */
static const char *metal_str(MetalValue v) {
    if (v.type == METAL_STRING || v.type == METAL_SYMBOL) {
        return v.as.string;
    }
    return NULL;
}

/* Helper: read a uint32 from a MetalVM value */
static uint32_t metal_u32(MetalValue v) {
    if (v.type == METAL_INT) return (uint32_t)v.as.integer;
    if (v.type == METAL_UINT) return (uint32_t)v.as.uinteger;
    return 0;
}

/* Helper: read a pointer from a MetalVM value */
static void *metal_ptr(MetalValue v) {
    if (v.type == METAL_POINTER) return v.as.pointer;
    return NULL;
}

/* --- ipc_channel_create() → (local_handle, peer_handle) --- */
static void sage_ipc_channel_create(MetalVM *vm) {
    ipc_handle_t local, peer;
    int r = ipc_channel_create(&local, &peer);
    metal_push(vm, metal_make_int(r));
    metal_push(vm, metal_make_uint((uint64_t)local));
    metal_push(vm, metal_make_uint((uint64_t)peer));
}

/* --- ipc_send(handle, msg_type, data_ptr, len, flags) → result --- */
static void sage_ipc_send(MetalVM *vm) {
    ipc_handle_t handle = metal_u32(metal_pop(vm));
    uint32_t flags      = metal_u32(metal_pop(vm));
    size_t len          = (size_t)metal_u32(metal_pop(vm));
    const void *data    = metal_ptr(metal_pop(vm));
    uint32_t type       = metal_u32(metal_pop(vm));

    ipc_message_t msg = {
        .type = type,
        .flags = flags,
        .data = data,
        .len = len,
        .caps = NULL,
        .cap_count = 0,
    };

    int r = ipc_send(handle, &msg);
    metal_push(vm, metal_make_int(r));
}

/* --- ipc_recv(handle, buf_ptr, max_len, flags) → (result, actual_len) --- */
static void sage_ipc_recv(MetalVM *vm) {
    uint32_t flags      = metal_u32(metal_pop(vm));
    size_t max_len      = (size_t)metal_u32(metal_pop(vm));
    void *buf           = metal_ptr(metal_pop(vm));
    ipc_handle_t handle = metal_u32(metal_pop(vm));

    ipc_message_t msg = { .data = buf, .len = max_len };
    int r = ipc_recv(handle, &msg, max_len);
    metal_push(vm, metal_make_int(r));
    if (r == 0) {
        metal_push(vm, metal_make_uint((uint64_t)msg.len));
    } else {
        metal_push(vm, metal_make_uint(0));
    }
}

/* --- ipc_call(handle, req_type, req_data, req_len, resp_buf, resp_max, timeout)
 *     → (result, resp_type, resp_len) --- */
static void sage_ipc_call(MetalVM *vm) {
    uint32_t timeout_ms = metal_u32(metal_pop(vm));
    size_t resp_max     = (size_t)metal_u32(metal_pop(vm));
    void *resp_buf      = metal_ptr(metal_pop(vm));
    size_t req_len      = (size_t)metal_u32(metal_pop(vm));
    const void *req_data= metal_ptr(metal_pop(vm));
    uint32_t req_type   = metal_u32(metal_pop(vm));
    ipc_handle_t handle = metal_u32(metal_pop(vm));

    ipc_message_t req = {
        .type = req_type,
        .flags = 0,
        .data = req_data,
        .len = req_len,
    };
    ipc_message_t resp = { .data = resp_buf, .len = resp_max };

    int r = ipc_call(handle, &req, &resp, resp_max, timeout_ms);
    metal_push(vm, metal_make_int(r));
    if (r == 0) {
        metal_push(vm, metal_make_uint((uint64_t)resp.type));
        metal_push(vm, metal_make_uint((uint64_t)resp.len));
    } else {
        metal_push(vm, metal_make_uint(0));
        metal_push(vm, metal_make_uint(0));
    }
}

/* --- ipc_port_open(backlog) → (result, port_handle) --- */
static void sage_ipc_port_open(MetalVM *vm) {
    uint32_t backlog = metal_u32(metal_pop(vm));
    ipc_handle_t port;
    int r = ipc_port_open(&port, backlog);
    metal_push(vm, metal_make_int(r));
    metal_push(vm, metal_make_uint((uint64_t)port));
}

/* --- ipc_port_accept(port_handle, flags) → (result, channel_handle) --- */
static void sage_ipc_port_accept(MetalVM *vm) {
    uint32_t flags      = metal_u32(metal_pop(vm));
    ipc_handle_t port   = metal_u32(metal_pop(vm));
    ipc_handle_t channel;
    int r = ipc_port_accept(port, &channel, flags);
    metal_push(vm, metal_make_int(r));
    metal_push(vm, metal_make_uint((uint64_t)channel));
}

/* --- ipc_connect(service_name, flags) → (result, channel_handle) --- */
static void sage_ipc_connect(MetalVM *vm) {
    uint32_t flags = metal_u32(metal_pop(vm));
    const char *name = metal_str(metal_pop(vm));
    ipc_handle_t channel;
    int r = ipc_connect(name, &channel, flags);
    metal_push(vm, metal_make_int(r));
    metal_push(vm, metal_make_uint((uint64_t)channel));
}

/* --- ipc_service_register(name, handle) → result --- */
static void sage_ipc_service_register(MetalVM *vm) {
    ipc_handle_t handle = metal_u32(metal_pop(vm));
    const char *name = metal_str(metal_pop(vm));
    int r = ipc_service_register(name, handle);
    metal_push(vm, metal_make_int(r));
}

/* --- ipc_service_lookup(name) → (result, handle) --- */
static void sage_ipc_service_lookup(MetalVM *vm) {
    const char *name = metal_str(metal_pop(vm));
    ipc_handle_t handle;
    int r = ipc_service_lookup(name, &handle);
    metal_push(vm, metal_make_int(r));
    metal_push(vm, metal_make_uint((uint64_t)handle));
}

/* --- ipc_shm_create(size, flags) → (result, handle) --- */
static void sage_ipc_shm_create(MetalVM *vm) {
    uint32_t flags = metal_u32(metal_pop(vm));
    size_t size    = (size_t)metal_u32(metal_pop(vm));
    ipc_handle_t handle;
    int r = ipc_shm_create(size, flags, &handle);
    metal_push(vm, metal_make_int(r));
    metal_push(vm, metal_make_uint((uint64_t)handle));
}

/* --- ipc_shm_map(handle, prot) → (result, vaddr) --- */
static void sage_ipc_shm_map(MetalVM *vm) {
    uint32_t prot       = metal_u32(metal_pop(vm));
    ipc_handle_t handle = metal_u32(metal_pop(vm));
    void *vaddr;
    int r = ipc_shm_map(handle, &vaddr, prot);
    metal_push(vm, metal_make_int(r));
    metal_push(vm, metal_make_pointer(vaddr));
}

/* --- ipc_cap_narrow(handle, new_rights) → result --- */
static void sage_ipc_cap_narrow(MetalVM *vm) {
    uint32_t new_rights = metal_u32(metal_pop(vm));
    ipc_handle_t handle = metal_u32(metal_pop(vm));
    int r = ipc_cap_narrow(handle, new_rights);
    metal_push(vm, metal_make_int(r));
}

/* --- ipc_pause(handle) → result --- */
static void sage_ipc_pause(MetalVM *vm) {
    ipc_handle_t handle = metal_u32(metal_pop(vm));
    int r = ipc_pause(handle);
    metal_push(vm, metal_make_int(r));
}

/* --- ipc_resume(handle) → result --- */
static void sage_ipc_resume(MetalVM *vm) {
    ipc_handle_t handle = metal_u32(metal_pop(vm));
    int r = ipc_resume(handle);
    metal_push(vm, metal_make_int(r));
}

/* --- ipc_drain(handle, timeout_ms) → result --- */
static void sage_ipc_drain(MetalVM *vm) {
    uint32_t timeout_ms = metal_u32(metal_pop(vm));
    ipc_handle_t handle = metal_u32(metal_pop(vm));
    int r = ipc_drain(handle, timeout_ms);
    metal_push(vm, metal_make_int(r));
}

/* ============================================================================
 * Registration Table
 * ============================================================================ */

typedef struct {
    const char *name;
    void (*fn)(MetalVM *);
} MetalNativeFn;

static MetalNativeFn g_ipc_natives[] = {
    { "ipc:channel-create",  sage_ipc_channel_create },
    { "ipc:send",            sage_ipc_send },
    { "ipc:recv",            sage_ipc_recv },
    { "ipc:call",            sage_ipc_call },
    { "ipc:port-open",       sage_ipc_port_open },
    { "ipc:port-accept",     sage_ipc_port_accept },
    { "ipc:connect",         sage_ipc_connect },
    { "ipc:service-register", sage_ipc_service_register },
    { "ipc:service-lookup",   sage_ipc_service_lookup },
    { "ipc:shm-create",      sage_ipc_shm_create },
    { "ipc:shm-map",         sage_ipc_shm_map },
    { "ipc:cap-narrow",      sage_ipc_cap_narrow },
    { "ipc:pause",           sage_ipc_pause },
    { "ipc:resume",          sage_ipc_resume },
    { "ipc:drain",           sage_ipc_drain },
    { NULL, NULL }
};

/* ============================================================================
 * Public Registration Function
 * ============================================================================ */

void ipc_register_metal_natives(MetalVM *vm) {
    for (int i = 0; g_ipc_natives[i].name != NULL; i++) {
        metal_register_native(vm, g_ipc_natives[i].name, g_ipc_natives[i].fn);
    }
}
