/* ============================================================================
 * SageOS IPC — User-Space C Library Implementation
 * ============================================================================ */

#include "ipc_user.h"
#include "syscall_numbers.h"
#include <string.h>

/* ============================================================================
 * Syscall wrappers (architecture-specific inline asm would go here;
 * we use a generic extern declaration for portability).
 * ============================================================================ */

extern long syscall6(long num, long a1, long a2, long a3,
                     long a4, long a5, long a6);

static inline long sys_ipc(long num, long a1, long a2, long a3,
                           long a4, long a5) {
    return syscall6(num, a1, a2, a3, a4, a5, 0);
}

/* ============================================================================
 * Channel API
 * ============================================================================ */

int ipc_channel_create(ipc_handle_t *local, ipc_handle_t *peer) {
    long r = sys_ipc(SYS_ipc_endpoint_create, (long)local, (long)peer, 0, 0, 0);
    return (int)r;
}

int ipc_channel_destroy(ipc_handle_t handle) {
    long r = sys_ipc(SYS_ipc_object_destroy, (long)handle, 0, 0, 0, 0);
    return (int)r;
}

/* ============================================================================
 * Message API
 * ============================================================================ */

int ipc_send(ipc_handle_t handle, const ipc_message_t *msg) {
    /* Build kernel ipc_msg_t from user ipc_message_t */
    struct {
        uint32_t magic;
        uint32_t type;
        uint32_t sender_task;
        uint32_t sender_cap;
        uint32_t flags;
        uint32_t payload_len;
        uint32_t cap_count;
    } khdr;

    khdr.magic = 0x49504321;  /* IPC_MSG_MAGIC */
    khdr.type = msg->type;
    khdr.sender_task = 0;      /* kernel fills */
    khdr.sender_cap = handle;
    khdr.flags = msg->flags;
    khdr.payload_len = (uint32_t)msg->len;
    khdr.cap_count = (uint32_t)msg->cap_count;

    /* For small messages, inline the payload directly in the syscall args.
     * For larger messages, we would use a shared buffer or copy-in. */
    long r = sys_ipc(SYS_ipc_send, (long)handle, (long)msg->data,
                     (long)msg->len, (long)msg->flags, 0);
    return (int)r;
}

int ipc_recv(ipc_handle_t handle, ipc_message_t *msg, size_t max_len) {
    (void)max_len;
    long r = sys_ipc(SYS_ipc_recv, (long)handle, (long)msg, 0, 0, 0);
    return (int)r;
}

int ipc_call(ipc_handle_t handle, const ipc_message_t *req,
             ipc_message_t *resp, size_t max_resp_len, uint32_t timeout_ms) {
    long r = sys_ipc(SYS_ipc_call, (long)handle, (long)req,
                     (long)resp, (long)timeout_ms, 0);
    (void)max_resp_len;
    return (int)r;
}

/* ============================================================================
 * Port API
 * ============================================================================ */

int ipc_port_open(ipc_handle_t *port, uint32_t backlog) {
    long r = sys_ipc(SYS_ipc_port_create, (long)backlog, (long)port, 0, 0, 0);
    return (int)r;
}

int ipc_port_accept(ipc_handle_t port, ipc_handle_t *channel, uint32_t flags) {
    long r = sys_ipc(SYS_ipc_port_accept, (long)port, (long)channel,
                     (long)flags, 0, 0);
    return (int)r;
}

int ipc_connect(const char *service_name, ipc_handle_t *channel, uint32_t flags) {
    long r = sys_ipc(SYS_ipc_port_connect, (long)service_name,
                     (long)channel, (long)flags, 0, 0);
    return (int)r;
}

/* ============================================================================
 * Service Namespace
 * ============================================================================ */

int ipc_service_register(const char *name, ipc_handle_t handle) {
    long r = sys_ipc(SYS_ipc_ns_register, (long)name, (long)handle, 0, 0, 0);
    return (int)r;
}

int ipc_service_lookup(const char *name, ipc_handle_t *handle) {
    long r = sys_ipc(SYS_ipc_ns_lookup, (long)name, (long)handle, 0, 0, 0);
    return (int)r;
}

int ipc_service_unregister(const char *name) {
    long r = sys_ipc(SYS_ipc_ns_unbind, (long)name, 0, 0, 0, 0);
    return (int)r;
}

/* ============================================================================
 * Shared Memory
 * ============================================================================ */

int ipc_shm_create(size_t size, uint32_t flags, ipc_handle_t *handle) {
    long r = sys_ipc(SYS_ipc_shm_create, (long)size, (long)flags,
                     (long)handle, 0, 0);
    return (int)r;
}

int ipc_shm_map(ipc_handle_t handle, void **vaddr, uint32_t prot) {
    long r = sys_ipc(SYS_ipc_shm_map, (long)handle, (long)vaddr,
                     (long)prot, 0, 0);
    return (int)r;
}

int ipc_shm_unmap(ipc_handle_t handle) {
    long r = sys_ipc(SYS_ipc_shm_unmap, (long)handle, 0, 0, 0, 0);
    return (int)r;
}

int ipc_shm_grant(ipc_handle_t handle, uint32_t target_task,
                  uint32_t rights, ipc_handle_t *derived) {
    long r = sys_ipc(SYS_ipc_shm_grant, (long)handle, (long)target_task,
                     (long)rights, (long)derived, 0);
    return (int)r;
}

/* ============================================================================
 * Capability Operations
 * ============================================================================ */

int ipc_cap_dup(ipc_handle_t handle, uint32_t rights, ipc_handle_t *new_handle) {
    long r = sys_ipc(SYS_ipc_cap_dup, (long)handle, (long)rights,
                     (long)new_handle, 0, 0);
    return (int)r;
}

int ipc_cap_narrow(ipc_handle_t handle, uint32_t new_rights) {
    long r = sys_ipc(SYS_ipc_cap_narrow, (long)handle, (long)new_rights, 0, 0, 0);
    return (int)r;
}

int ipc_cap_revoke(ipc_handle_t handle) {
    long r = sys_ipc(SYS_ipc_cap_revoke, (long)handle, 0, 0, 0, 0);
    return (int)r;
}

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

int ipc_pause(ipc_handle_t handle) {
    long r = sys_ipc(SYS_ipc_object_pause, (long)handle, 0, 0, 0, 0);
    return (int)r;
}

int ipc_resume(ipc_handle_t handle) {
    long r = sys_ipc(SYS_ipc_object_resume, (long)handle, 0, 0, 0, 0);
    return (int)r;
}

int ipc_drain(ipc_handle_t handle, uint32_t timeout_ms) {
    long r = sys_ipc(SYS_ipc_object_drain, (long)handle, (long)timeout_ms,
                     0, 0, 0);
    return (int)r;
}

/* ============================================================================
 * Introspection
 * ============================================================================ */

int ipc_info(ipc_handle_t handle, char *buf, size_t buf_len) {
    long r = sys_ipc(SYS_ipc_object_info, (long)handle, (long)buf,
                     (long)buf_len, 0, 0);
    return (int)r;
}

int ipc_stats(ipc_handle_t handle, uint64_t *stats, size_t count) {
    long r = sys_ipc(SYS_ipc_object_stats, (long)handle, (long)stats,
                     (long)count, 0, 0);
    return (int)r;
}
