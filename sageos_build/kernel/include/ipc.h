#ifndef SAGEOS_IPC_H
#define SAGEOS_IPC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ============================================================================
 * SageOS IPC Subsystem — Canonical Primitives, Lifecycle Semantics,
 * Service Addressing, and Capability Routing
 * Version: 0.1.0 (Formalization Draft)
 * ============================================================================ */

/* ----------------------------------------------------------------------------
 * SECTION 1: CAPABILITY MODEL
 * ----------------------------------------------------------------------------
 * Every IPC right is represented by a capability: a kernel-verified token
 * that grants a specific set of permissions over a specific IPC object.
 * Capabilities are reference-counted, non-forgeable, and revocable.
 */

#define IPC_CAP_MAX_PER_TASK      64
#define IPC_CAP_RIGHT_SEND        (1U << 0)
#define IPC_CAP_RIGHT_RECV        (1U << 1)
#define IPC_CAP_RIGHT_CALL        (1U << 2)   /* synchronous RPC */
#define IPC_CAP_RIGHT_MAP         (1U << 3)   /* shared memory mapping */
#define IPC_CAP_RIGHT_DUP         (1U << 4)   /* right to duplicate cap */
#define IPC_CAP_RIGHT_REVOKE      (1U << 5)   /* right to revoke */
#define IPC_CAP_RIGHTS_ALL        0x3FU

typedef uint64_t ipc_cap_handle_t;

typedef struct {
    uint32_t        rights;         /* bitmask of IPC_CAP_RIGHT_* */
    uint32_t        object_type;    /* IPC_OBJ_* */
    uint32_t        object_id;      /* kernel object index */
    uint32_t        ref_count;      /* reference count for GC */
    uint32_t        owner_task;     /* task that holds this cap */
    uint32_t        flags;          /* IPC_CAPF_* */
} ipc_capability_t;

#define IPC_CAPF_KERNEL_OWNED     (1U << 0)   /* kernel retains final ref */
#define IPC_CAPF_IMMUTABLE        (1U << 1)   /* rights cannot be narrowed */
#define IPC_CAPF_PERSISTENT       (1U << 2)   /* survives task exit */

/* Capability slot table embedded in thread_t (see scheduler.h extension) */
typedef struct {
    ipc_capability_t caps[IPC_CAP_MAX_PER_TASK];
    uint32_t         next_free;
    uint32_t         lock;          /* spinlock for concurrent mutation */
} ipc_cap_table_t;

/* ----------------------------------------------------------------------------
 * SECTION 2: SERVICE ADDRESSING (Name → Capability Resolution)
 * ----------------------------------------------------------------------------
 * Services register under hierarchical names (e.g. "vfs.root", "net.tcp").
 * The namespace is a kernel-managed trie. Lookups return a capability
 * handle, never a raw pointer.
 */

#define IPC_NS_MAX_DEPTH          8
#define IPC_NS_MAX_NAME_LEN       64
#define IPC_NS_MAX_ENTRIES        256

typedef struct ipc_ns_entry {
    char                name[IPC_NS_MAX_NAME_LEN];
    uint32_t            cap_handle;     /* capability index in caller's table */
    uint32_t            owner_task_id;
    struct ipc_ns_entry *child;
    struct ipc_ns_entry *sibling;
} ipc_ns_entry_t;

typedef struct {
    ipc_ns_entry_t *root;
    uint32_t        count;
    uint32_t        lock;
} ipc_namespace_t;

/* Well-known service names (resolved at boot) */
#define IPC_SVC_VFS_ROOT          "vfs.root"
#define IPC_SVC_NET_STACK         "net.stack"
#define IPC_SVC_DEV_MGR           "dev.manager"
#define IPC_SVC_SCHED_CTL         "sched.control"
#define IPC_SVC_LOG_SINK          "log.sink"

/* ----------------------------------------------------------------------------
 * SECTION 3: IPC OBJECT TYPES & LIFECYCLE
 * ----------------------------------------------------------------------------
 */

typedef enum {
    IPC_OBJ_NONE = 0,
    IPC_OBJ_ENDPOINT,       /* unidirectional message pipe */
    IPC_OBJ_CHANNEL,        /* bidirectional message stream */
    IPC_OBJ_PORT,           /* rendezvous / acceptor */
    IPC_OBJ_SHARED_MEM,     /* memory-mapped buffer */
    IPC_OBJ_SIGNAL_SET,     /* event/signal broadcasting */
    IPC_OBJ_MONITOR,        /* condition variable + mutex hybrid */
} ipc_obj_type_t;

/* Lifecycle states */
typedef enum {
    IPC_STATE_CONSTRUCTED = 0,
    IPC_STATE_ACTIVE,
    IPC_STATE_PAUSED,       /* no new operations accepted */
    IPC_STATE_DRAINING,     /* finish in-flight, reject new */
    IPC_STATE_DEAD
} ipc_lifecycle_state_t;

/* ----------------------------------------------------------------------------
 * SECTION 4: MESSAGE PRIMITIVES
 * ----------------------------------------------------------------------------
 * Messages are typed, bounded, and carry capabilities as inline attachments.
 * The kernel copies data (up to IPC_MSG_MAX_BYTES) and translates caps.
 */

#define IPC_MSG_MAX_BYTES         4096
#define IPC_MSG_MAX_CAPS          8
#define IPC_MSG_MAGIC             0x49504321   /* "IPC!" */

typedef struct {
    uint32_t magic;
    uint32_t type;              /* user-defined message type */
    uint32_t sender_task;
    uint32_t sender_cap;        /* reply-cap hint */
    uint32_t flags;
    uint32_t payload_len;
    uint32_t cap_count;
    /* Inline payload follows header in contiguous buffer */
} ipc_msg_header_t;

#define IPC_MSGF_NOBLOCK          (1U << 0)
#define IPC_MSGF_KERNEL_ORIGIN    (1U << 1)
#define IPC_MSGF_CAP_TRANSFER     (1U << 2)   /* caps in payload are moved, not copied */

typedef struct {
    ipc_msg_header_t hdr;
    uint8_t          payload[IPC_MSG_MAX_BYTES];
    uint32_t         caps[IPC_MSG_MAX_CAPS];    /* capability handles */
} ipc_msg_t;

/* Message type categories */
#define IPC_MSG_TYPE_PING         0x00000000
#define IPC_MSG_TYPE_RPC_REQ      0x00000001
#define IPC_MSG_TYPE_RPC_RESP     0x00000002
#define IPC_MSG_TYPE_SIGNAL       0x00000003
#define IPC_MSG_TYPE_MEM_REQ      0x00000010
#define IPC_MSG_TYPE_MEM_GRANT    0x00000011
#define IPC_MSG_TYPE_MEM_REVOKE   0x00000012
#define IPC_MSG_TYPE_SVC_REGISTER 0x00000100
#define IPC_MSG_TYPE_SVC_LOOKUP   0x00000101
#define IPC_MSG_TYPE_SVC_UNBIND   0x00000102

/* ----------------------------------------------------------------------------
 * SECTION 5: ENDPOINT & CHANNEL DESCRIPTORS
 * ---------------------------------------------------------------------------- */

typedef struct {
    uint32_t            id;
    ipc_obj_type_t      type;
    ipc_lifecycle_state_t state;

    /* Message queue (bounded buffer ring) */
    ipc_msg_t          *msg_ring;
    uint32_t            ring_size;
    uint32_t            head;        /* producer */
    uint32_t            tail;        /* consumer */
    uint32_t            msg_count;
    uint32_t            queue_lock;

    /* Linked capabilities (send/recv halves) */
    uint32_t            peer_cap;    /* for CHANNEL: linked endpoint */
    uint32_t            bound_task;  /* task that owns the recv side */

    /* Wait queue for blocking receivers */
    struct thread      *wait_head;
    struct thread      *wait_tail;

    /* Statistics */
    uint64_t            msgs_sent;
    uint64_t            msgs_recv;
    uint64_t            bytes_moved;
    uint64_t            drops;
} ipc_endpoint_t;

/* ----------------------------------------------------------------------------
 * SECTION 6: SHARED MEMORY DESCRIPTOR
 * ---------------------------------------------------------------------------- */

typedef struct {
    uint32_t            id;
    ipc_obj_type_t      type;
    ipc_lifecycle_state_t state;

    uintptr_t           kernel_vaddr;
    uintptr_t           user_vaddr;      /* mapped address in owner task */
    size_t              size;
    uint32_t            owner_task;
    uint32_t            map_count;       /* number of tasks mapped */
    uint32_t            prot;            /* PROT_READ / PROT_WRITE etc. */
    uint32_t            flags;
} ipc_shared_mem_t;

#define IPC_SHM_FLAG_PHYS_CONTIG  (1U << 0)
#define IPC_SHM_FLAG_DMA_CAPABLE  (1U << 1)

/* ----------------------------------------------------------------------------
 * SECTION 7: PORT (RENDEZVOUS) DESCRIPTOR
 * ----------------------------------------------------------------------------
 * A PORT is an acceptor: clients send connection requests; the owner
 * receives them and may spawn a dedicated CHANNEL per connection.
 */

typedef struct {
    uint32_t            id;
    ipc_obj_type_t      type;
    ipc_lifecycle_state_t state;

    uint32_t            owner_task;
    uint32_t            backlog;
    uint32_t            pending_count;

    /* Pending connection requests */
    struct {
        uint32_t        client_task;
        uint32_t        client_cap;
        ipc_msg_t       connect_msg;
    } pending[16];

    struct thread      *wait_head;
    struct thread      *wait_tail;
} ipc_port_t;

/* ----------------------------------------------------------------------------
 * SECTION 8: KERNEL API (Syscall Interface)
 * ----------------------------------------------------------------------------
 * These are the canonical primitives exposed via syscall numbers.
 */

/* --- Capability operations --- */
int  ipc_cap_insert(ipc_capability_t *cap, uint32_t *out_handle);
int  ipc_cap_narrow(uint32_t handle, uint32_t new_rights);
int  ipc_cap_revoke(uint32_t handle);
int  ipc_cap_dup(uint32_t handle, uint32_t *out_new_handle);
void ipc_cap_release(uint32_t handle);

/* --- Object creation / destruction --- */
int  ipc_endpoint_create(uint32_t *out_send_cap, uint32_t *out_recv_cap);
int  ipc_channel_create(uint32_t *out_local_cap, uint32_t *out_peer_cap);
int  ipc_port_create(uint32_t backlog, uint32_t *out_cap);
int  ipc_shared_mem_create(size_t size, uint32_t flags, uint32_t *out_cap);
void ipc_object_destroy(uint32_t cap_handle);

/* --- Message operations --- */
int  ipc_send(uint32_t cap_handle, const ipc_msg_t *msg, uint32_t flags);
int  ipc_recv(uint32_t cap_handle, ipc_msg_t *msg, uint32_t flags);
int  ipc_call(uint32_t cap_handle, const ipc_msg_t *req,
              ipc_msg_t *resp, uint32_t timeout_ms);

/* --- Port operations --- */
int  ipc_port_listen(uint32_t port_cap);
int  ipc_port_accept(uint32_t port_cap, uint32_t *out_channel_cap,
                     uint32_t flags);
int  ipc_port_connect(const char *svc_name, uint32_t *out_channel_cap,
                      uint32_t flags);

/* --- Shared memory operations --- */
int  ipc_shm_map(uint32_t cap_handle, uintptr_t *out_vaddr, uint32_t prot);
int  ipc_shm_unmap(uint32_t cap_handle);
int  ipc_shm_grant(uint32_t cap_handle, uint32_t target_task,
                   uint32_t rights, uint32_t *out_derived_cap);

/* --- Namespace operations --- */
int  ipc_ns_register(const char *name, uint32_t cap_handle);
int  ipc_ns_lookup(const char *name, uint32_t *out_cap_handle);
int  ipc_ns_unbind(const char *name);

/* --- Lifecycle operations --- */
int  ipc_object_pause(uint32_t cap_handle);
int  ipc_object_resume(uint32_t cap_handle);
int  ipc_object_drain(uint32_t cap_handle, uint32_t timeout_ms);

/* --- Introspection --- */
int  ipc_object_info(uint32_t cap_handle, char *buf, size_t buf_len);
int  ipc_object_stats(uint32_t cap_handle, uint64_t *out_stats, size_t count);

/* ----------------------------------------------------------------------------
 * SECTION 9: SYSCALL NUMBERS (to be merged into syscall_numbers.h)
 * ----------------------------------------------------------------------------
 */

#define SYS_ipc_cap_insert        200
#define SYS_ipc_cap_narrow        201
#define SYS_ipc_cap_revoke        202
#define SYS_ipc_cap_dup           203
#define SYS_ipc_endpoint_create   210
#define SYS_ipc_channel_create    211
#define SYS_ipc_port_create       212
#define SYS_ipc_shm_create        213
#define SYS_ipc_object_destroy    214
#define SYS_ipc_send              220
#define SYS_ipc_recv              221
#define SYS_ipc_call              222
#define SYS_ipc_port_listen       223
#define SYS_ipc_port_accept       224
#define SYS_ipc_port_connect      225
#define SYS_ipc_shm_map           226
#define SYS_ipc_shm_unmap         227
#define SYS_ipc_shm_grant         228
#define SYS_ipc_ns_register       230
#define SYS_ipc_ns_lookup         231
#define SYS_ipc_ns_unbind         232
#define SYS_ipc_object_pause      240
#define SYS_ipc_object_resume     241
#define SYS_ipc_object_drain      242
#define SYS_ipc_object_info       243
#define SYS_ipc_object_stats      244

/* ============================================================================
 * Inline helpers
 * ============================================================================ */

static inline bool ipc_cap_has_right(const ipc_capability_t *cap, uint32_t right) {
    return (cap->rights & right) != 0;
}

static inline bool ipc_cap_is_valid(const ipc_capability_t *cap) {
    return cap->object_type != IPC_OBJ_NONE && cap->ref_count > 0;
}

#endif /* SAGEOS_IPC_H */
