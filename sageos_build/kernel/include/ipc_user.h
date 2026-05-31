/* ============================================================================
 * SageOS IPC — User-Space C Library Header
 * 
 * Provides a POSIX-like wrapper around the raw IPC syscalls for C programs
 * running on SageOS.  Also serves as the ABI contract for SageLang FFI.
 * ============================================================================ */

#ifndef SAGEOS_IPC_USER_H
#define SAGEOS_IPC_USER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * Opaque Handles (user-space view of capabilities)
 * ============================================================================ */

typedef uint32_t ipc_handle_t;
#define IPC_INVALID_HANDLE  ((ipc_handle_t)0xFFFFFFFFU)

/* ============================================================================
 * High-Level Object Types
 * ============================================================================ */

typedef struct ipc_channel ipc_channel_t;
typedef struct ipc_port   ipc_port_t;
typedef struct ipc_shm    ipc_shm_t;

/* ============================================================================
 * Message API (Typed, user-friendly)
 * ============================================================================ */

typedef struct {
    uint32_t type;
    uint32_t flags;
    const void *data;
    size_t    len;
    ipc_handle_t *caps;      /* inline capability attachments */
    size_t    cap_count;
} ipc_message_t;

#define IPC_FLAG_BLOCKING     0x0000
#define IPC_FLAG_NONBLOCK     0x0001
#define IPC_FLAG_TRANSFER     0x0002   /* move caps instead of copy */
#define IPC_FLAG_NO_REPLY     0x0004   /* one-way fire-and-forget */

/* ============================================================================
 * Channel Lifecycle
 * ============================================================================ */

/* Create a bidirectional channel.  Returns 0 on success, -errno on failure.
 * On success, *local receives the send/recv capability for this end,
 * and *peer receives the capability suitable for passing to another task. */
int ipc_channel_create(ipc_handle_t *local, ipc_handle_t *peer);

/* Destroy a channel (or any IPC object) by capability handle. */
int ipc_channel_destroy(ipc_handle_t handle);

/* ============================================================================
 * Message Send / Receive
 * ============================================================================ */

int ipc_send(ipc_handle_t handle, const ipc_message_t *msg);
int ipc_recv(ipc_handle_t handle, ipc_message_t *msg, size_t max_len);

/* Synchronous RPC: sends request, blocks for response.
 * The response is written into *resp.  Caller must provide max_resp_len. */
int ipc_call(ipc_handle_t handle, const ipc_message_t *req,
             ipc_message_t *resp, size_t max_resp_len, uint32_t timeout_ms);

/* ============================================================================
 * Port (Rendezvous) API
 * ============================================================================ */

/* Create a listening port with the specified connection backlog. */
int ipc_port_open(ipc_handle_t *port, uint32_t backlog);

/* Accept a connection on a port.  Blocks until a client connects.
 * Returns a channel handle for the new connection. */
int ipc_port_accept(ipc_handle_t port, ipc_handle_t *channel, uint32_t flags);

/* Connect to a named service.  Returns a channel handle. */
int ipc_connect(const char *service_name, ipc_handle_t *channel, uint32_t flags);

/* ============================================================================
 * Service Namespace
 * ============================================================================ */

/* Register the current task under a hierarchical service name.
 * The provided handle must be a PORT or CHANNEL capability. */
int ipc_service_register(const char *name, ipc_handle_t handle);

/* Lookup a service by name.  Returns a capability handle with narrowed rights
 * (no REVOKE, no DUP). */
int ipc_service_lookup(const char *name, ipc_handle_t *handle);

/* Unbind a service name.  Only the original registrant may unbind. */
int ipc_service_unregister(const char *name);

/* ============================================================================
 * Shared Memory
 * ============================================================================ */

/* Create a shared memory region of `size` bytes.
 * `flags` may include IPC_SHM_PHYS_CONTIG or IPC_SHM_DMA_CAPABLE. */
int ipc_shm_create(size_t size, uint32_t flags, ipc_handle_t *handle);

/* Map a shared memory region into the caller's address space.
 * `prot` uses PROT_READ / PROT_WRITE / PROT_EXEC bits. */
int ipc_shm_map(ipc_handle_t handle, void **vaddr, uint32_t prot);

/* Unmap a shared memory region. */
int ipc_shm_unmap(ipc_handle_t handle);

/* Grant a subset of rights on a shared memory region to another task.
 * The derived capability is returned in *derived. */
int ipc_shm_grant(ipc_handle_t handle, uint32_t target_task,
                  uint32_t rights, ipc_handle_t *derived);

/* ============================================================================
 * Capability Operations
 * ============================================================================ */

/* Duplicate a capability, creating a new handle with the same or fewer rights. */
int ipc_cap_dup(ipc_handle_t handle, uint32_t rights, ipc_handle_t *new_handle);

/* Narrow the rights on an existing capability in-place.  Irreversible. */
int ipc_cap_narrow(ipc_handle_t handle, uint32_t new_rights);

/* Revoke a capability, destroying the underlying object if the caller holds
 * the REVOKE right. */
int ipc_cap_revoke(ipc_handle_t handle);

/* ============================================================================
 * Lifecycle Control
 * ============================================================================ */

int ipc_pause(ipc_handle_t handle);    /* pause an object */
int ipc_resume(ipc_handle_t handle);   /* resume a paused object */
int ipc_drain(ipc_handle_t handle, uint32_t timeout_ms); /* drain then destroy */

/* ============================================================================
 * Introspection
 * ============================================================================ */

int ipc_info(ipc_handle_t handle, char *buf, size_t buf_len);
int ipc_stats(ipc_handle_t handle, uint64_t *stats, size_t count);

/* ============================================================================
 * Convenience Macros for Message Construction
 * ============================================================================ */

#define IPC_MSG_INIT(type_, data_, len_)     ((ipc_message_t){ .type = (type_), .flags = 0,                       .data = (data_), .len = (len_),                       .caps = NULL, .cap_count = 0 })

#define IPC_MSG_INIT_CAPS(type_, data_, len_, caps_, n_)     ((ipc_message_t){ .type = (type_), .flags = 0,                       .data = (data_), .len = (len_),                       .caps = (caps_), .cap_count = (n_) })

#endif /* SAGEOS_IPC_USER_H */
