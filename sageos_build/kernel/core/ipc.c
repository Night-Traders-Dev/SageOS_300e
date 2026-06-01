/* ============================================================================
 * SageOS IPC Subsystem — Kernel Implementation
 * Canonical primitives, lifecycle semantics, service addressing,
 * and capability routing.
 * ============================================================================ */

#include "ipc.h"
#include "process.h"
#include "scheduler.h"
#include "scheduler_ipc_ext.h"
#include "sage_alloc.h"
#include "console.h"
#include <string.h>

/* Simple strtok implementation for kernel */
static char *g_strtok_ptr = NULL;
static char *strtok(char *str, const char *delim) {
    if (str) g_strtok_ptr = str;
    if (!g_strtok_ptr) return NULL;
    char *start = g_strtok_ptr;
    while (*start && strchr(delim, *start)) start++;
    if (!*start) { g_strtok_ptr = NULL; return NULL; }
    char *end = start;
    while (*end && !strchr(delim, *end)) end++;
    if (*end) { *end = '\0'; g_strtok_ptr = end + 1; }
    else { g_strtok_ptr = NULL; }
    return start;
}

/* ============================================================================
 * Internal Object Pools
 * ============================================================================ */

ipc_endpoint_t    g_endpoints[IPC_MAX_ENDPOINTS];
ipc_shared_mem_t  g_shm[IPC_MAX_SHM];
ipc_port_t        g_ports[IPC_MAX_PORTS];
ipc_ns_entry_t    g_ns_entries[IPC_MAX_NS_ENTRIES];
ipc_namespace_t   g_namespace;

static uint32_t g_ipc_lock = 0;   /* global coarse lock for pool allocators */

/* ============================================================================
 * Spinlock Helpers
 * ============================================================================ */

static void ipc_spin_lock(uint32_t *lock) {
    while (__atomic_test_and_set(lock, __ATOMIC_SEQ_CST)) {
#if defined(__aarch64__)
        __asm__ volatile ("yield");
#elif defined(__riscv)
        __asm__ volatile ("fence");
#else
        __asm__ volatile ("pause");
#endif
    }
}

static void ipc_spin_unlock(uint32_t *lock) {
    __atomic_clear(lock, __ATOMIC_SEQ_CST);
}

/* ============================================================================
 * Capability Table Management (per-task)
 * ============================================================================ */

static ipc_cap_table_t *ipc_get_cap_table(task_t *t) {
    return task_ipc_cap_table(t);
}

static int ipc_cap_alloc_slot(ipc_cap_table_t *tab, uint32_t *out_handle) {
    ipc_spin_lock(&tab->lock);
    for (int i = 0; i < IPC_CAP_MAX_PER_TASK; i++) {
        if (tab->caps[i].object_type == IPC_OBJ_NONE) {
            *out_handle = (uint32_t)i;
            ipc_spin_unlock(&tab->lock);
            return 0;
        }
    }
    ipc_spin_unlock(&tab->lock);
    return -1;  /* ENOSPC */
}

static void ipc_cap_free_slot(ipc_cap_table_t *tab, uint32_t handle) {
    if (handle >= IPC_CAP_MAX_PER_TASK) return;
    ipc_spin_lock(&tab->lock);
    memset(&tab->caps[handle], 0, sizeof(ipc_capability_t));
    ipc_spin_unlock(&tab->lock);
}

static ipc_capability_t *ipc_cap_resolve(task_t *t, uint32_t handle) {
    ipc_cap_table_t *tab = ipc_get_cap_table(t);
    if (handle >= IPC_CAP_MAX_PER_TASK) return NULL;
    ipc_capability_t *cap = &tab->caps[handle];
    if (!ipc_cap_is_valid(cap)) return NULL;
    return cap;
}

/* ============================================================================
 * Object Pool Allocators
 * ============================================================================ */

static int ipc_alloc_endpoint(ipc_endpoint_t **out) {
    ipc_spin_lock(&g_ipc_lock);
    for (int i = 0; i < IPC_MAX_ENDPOINTS; i++) {
        if (g_endpoints[i].state == IPC_STATE_DEAD ||
            g_endpoints[i].state == IPC_STATE_CONSTRUCTED) {
            memset(&g_endpoints[i], 0, sizeof(ipc_endpoint_t));
            g_endpoints[i].id = (uint32_t)i;
            g_endpoints[i].type = IPC_OBJ_ENDPOINT;
            g_endpoints[i].state = IPC_STATE_CONSTRUCTED;
            *out = &g_endpoints[i];
            ipc_spin_unlock(&g_ipc_lock);
            return 0;
        }
    }
    ipc_spin_unlock(&g_ipc_lock);
    return -1;
}

static int ipc_alloc_port(ipc_port_t **out) {
    ipc_spin_lock(&g_ipc_lock);
    for (int i = 0; i < IPC_MAX_PORTS; i++) {
        if (g_ports[i].state == IPC_STATE_DEAD ||
            g_ports[i].state == IPC_STATE_CONSTRUCTED) {
            memset(&g_ports[i], 0, sizeof(ipc_port_t));
            g_ports[i].id = (uint32_t)i;
            g_ports[i].type = IPC_OBJ_PORT;
            g_ports[i].state = IPC_STATE_CONSTRUCTED;
            *out = &g_ports[i];
            ipc_spin_unlock(&g_ipc_lock);
            return 0;
        }
    }
    ipc_spin_unlock(&g_ipc_lock);
    return -1;
}

static int ipc_alloc_shm(ipc_shared_mem_t **out) {
    ipc_spin_lock(&g_ipc_lock);
    for (int i = 0; i < IPC_MAX_SHM; i++) {
        if (g_shm[i].state == IPC_STATE_DEAD ||
            g_shm[i].state == IPC_STATE_CONSTRUCTED) {
            memset(&g_shm[i], 0, sizeof(ipc_shared_mem_t));
            g_shm[i].id = (uint32_t)i;
            g_shm[i].type = IPC_OBJ_SHARED_MEM;
            g_shm[i].state = IPC_STATE_CONSTRUCTED;
            *out = &g_shm[i];
            ipc_spin_unlock(&g_ipc_lock);
            return 0;
        }
    }
    ipc_spin_unlock(&g_ipc_lock);
    return -1;
}

/* ============================================================================
 * Lifecycle State Machine
 * ============================================================================
 *
 * CONSTRUCTED → ACTIVE  : on first successful cap insert
 * ACTIVE    → PAUSED    : ipc_object_pause()
 * PAUSED    → ACTIVE    : ipc_object_resume()
 * ACTIVE    → DRAINING : ipc_object_drain()
 * DRAINING  → DEAD      : when queue empty and no refs remain
 * PAUSED    → DEAD      : ipc_object_destroy() or revoke
 *
 * A state transition is only valid if the caller holds a REVOKE-capable
 * capability on the object.
 */

static bool ipc_lifecycle_transition(ipc_endpoint_t *ep,
                                     ipc_lifecycle_state_t new_state) {
    /* Simple atomic CAS on state */
    ipc_lifecycle_state_t expected;
    switch (new_state) {
        case IPC_STATE_ACTIVE:
            expected = IPC_STATE_CONSTRUCTED;
            break;
        case IPC_STATE_PAUSED:
            expected = IPC_STATE_ACTIVE;
            break;
        case IPC_STATE_DRAINING:
            expected = IPC_STATE_ACTIVE;
            break;
        case IPC_STATE_DEAD:
            expected = IPC_STATE_PAUSED;
            break;
        default:
            return false;
    }

    if (__atomic_compare_exchange_n(&ep->state, &expected, new_state,
                                    false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        return true;
    }
    return false;
}

/* ============================================================================
 * Message Queue (Ring Buffer)
 * ============================================================================ */

static int ipc_msg_enqueue(ipc_endpoint_t *ep, const ipc_msg_t *msg) {
    ipc_spin_lock(&ep->queue_lock);

    if (ep->state != IPC_STATE_ACTIVE && ep->state != IPC_STATE_DRAINING) {
        ipc_spin_unlock(&ep->queue_lock);
        return -1;  /* EPIPE */
    }

    if (ep->msg_count >= ep->ring_size) {
        ep->drops++;
        ipc_spin_unlock(&ep->queue_lock);
        return -1;  /* EAGAIN / EWOULDBLOCK */
    }

    uint32_t slot = ep->head % ep->ring_size;
    memcpy(&ep->msg_ring[slot], msg, sizeof(ipc_msg_t));
    ep->head = (ep->head + 1) % ep->ring_size;
    ep->msg_count++;
    ep->msgs_sent++;
    ep->bytes_moved += msg->hdr.payload_len;

    /* Wake a single waiter */
    if (ep->wait_head) {
        thread_t *w = ep->wait_head;
        ep->wait_head = w->next;
        if (!ep->wait_head) ep->wait_tail = NULL;
        sched_unblock(w);
    }

    ipc_spin_unlock(&ep->queue_lock);
    return 0;
}

static int ipc_msg_dequeue(ipc_endpoint_t *ep, ipc_msg_t *msg) {
    ipc_spin_lock(&ep->queue_lock);

    if (ep->msg_count == 0) {
        if (ep->state == IPC_STATE_DRAINING || ep->state == IPC_STATE_DEAD) {
            ipc_spin_unlock(&ep->queue_lock);
            return -1;  /* EOF / EPIPE */
        }
        ipc_spin_unlock(&ep->queue_lock);
        return -2;  /* Would block */
    }

    uint32_t slot = ep->tail % ep->ring_size;
    memcpy(msg, &ep->msg_ring[slot], sizeof(ipc_msg_t));
    ep->tail = (ep->tail + 1) % ep->ring_size;
    ep->msg_count--;
    ep->msgs_recv++;

    ipc_spin_unlock(&ep->queue_lock);
    return 0;
}

static void ipc_wait_enqueue(ipc_endpoint_t *ep, thread_t *t) {
    t->next = NULL;
    if (!ep->wait_head) {
        ep->wait_head = t;
        ep->wait_tail = t;
    } else {
        ep->wait_tail->next = t;
        ep->wait_tail = t;
    }
    t->state = THREAD_STATE_BLOCKED;
}

/* ============================================================================
 * Capability Routing (Namespace)
 * ============================================================================ */

static ipc_ns_entry_t *ipc_ns_find(const char *name) {
    ipc_ns_entry_t *cur = g_namespace.root;
    char buf[IPC_NS_MAX_NAME_LEN];
    strncpy(buf, name, sizeof(buf));
    buf[sizeof(buf)-1] = '\0';

    char *token = strtok(buf, ".");
    while (token && cur) {
        bool found = false;
        ipc_ns_entry_t *child = cur->child;
        while (child) {
            if (strcmp(child->name, token) == 0) {
                cur = child;
                found = true;
                break;
            }
            child = child->sibling;
        }
        if (!found) return NULL;
        token = strtok(NULL, ".");
    }
    return cur;
}

static int ipc_ns_insert(const char *name, uint32_t cap_handle,
                         uint32_t owner_task_id) {
    ipc_spin_lock(&g_namespace.lock);

    if (!g_namespace.root) {
        g_namespace.root = &g_ns_entries[0];
        memset(g_namespace.root, 0, sizeof(ipc_ns_entry_t));
        strncpy(g_namespace.root->name, "", sizeof(g_namespace.root->name));
        g_namespace.root->name[sizeof(g_namespace.root->name)-1] = '\0';
    }

    ipc_ns_entry_t *cur = g_namespace.root;
    char buf[IPC_NS_MAX_NAME_LEN];
    strncpy(buf, name, sizeof(buf));
    buf[sizeof(buf)-1] = '\0';

    char *token = strtok(buf, ".");
    while (token) {
        ipc_ns_entry_t *child = cur->child;
        ipc_ns_entry_t *prev = NULL;
        bool found = false;

        while (child) {
            if (strcmp(child->name, token) == 0) {
                cur = child;
                found = true;
                break;
            }
            prev = child;
            child = child->sibling;
        }

        if (!found) {
            /* Allocate new entry */
            if (g_namespace.count >= IPC_MAX_NS_ENTRIES) {
                ipc_spin_unlock(&g_namespace.lock);
                return -1;  /* ENOSPC */
            }
            ipc_ns_entry_t *new_entry = &g_ns_entries[g_namespace.count++];
            memset(new_entry, 0, sizeof(ipc_ns_entry_t));
            strncpy(new_entry->name, token, sizeof(new_entry->name));
            new_entry->name[sizeof(new_entry->name)-1] = '\0';
            new_entry->sibling = cur->child;
            cur->child = new_entry;
            cur = new_entry;
        }

        token = strtok(NULL, ".");
    }

    cur->cap_handle = cap_handle;
    cur->owner_task_id = owner_task_id;
    ipc_spin_unlock(&g_namespace.lock);
    return 0;
}

/* ============================================================================
 * Syscall Implementations
 * ============================================================================ */

long sys_ipc_endpoint_create(uintptr_t out_send, uintptr_t out_recv) {
    task_t *t = current_task();
    if (!t) return -1;

    ipc_endpoint_t *ep_send = NULL, *ep_recv = NULL;
    if (ipc_alloc_endpoint(&ep_send) < 0) return -1;
    if (ipc_alloc_endpoint(&ep_recv) < 0) {
        ep_send->state = IPC_STATE_DEAD;
        return -1;
    }

    /* Allocate ring buffers */
    ep_send->ring_size = 16;
    ep_recv->ring_size = 16;
    ep_send->msg_ring = (ipc_msg_t *)sage_malloc_tagged(sizeof(ipc_msg_t) * 16, ALLOC_TAG_IPC);
    ep_recv->msg_ring = (ipc_msg_t *)sage_malloc_tagged(sizeof(ipc_msg_t) * 16, ALLOC_TAG_IPC);
    if (!ep_send->msg_ring || !ep_recv->msg_ring) {
        if (ep_send->msg_ring) sage_free(ep_send->msg_ring);
        if (ep_recv->msg_ring) sage_free(ep_recv->msg_ring);
        ep_send->state = IPC_STATE_DEAD;
        ep_recv->state = IPC_STATE_DEAD;
        return -1;
    }

    /* Link them as a channel pair */
    ep_send->peer_cap = ep_recv->id;
    ep_recv->peer_cap = ep_send->id;
    ep_send->bound_task = t->id;
    ep_recv->bound_task = t->id;

    ep_send->state = IPC_STATE_ACTIVE;
    ep_recv->state = IPC_STATE_ACTIVE;

    /* Create capabilities in caller's table */
    ipc_cap_table_t *tab = ipc_get_cap_table(t);
    uint32_t send_handle, recv_handle;

    if (ipc_cap_alloc_slot(tab, &send_handle) < 0 ||
        ipc_cap_alloc_slot(tab, &recv_handle) < 0) {
        ep_send->state = IPC_STATE_DEAD;
        ep_recv->state = IPC_STATE_DEAD;
        return -1;
    }

    tab->caps[send_handle] = (ipc_capability_t){
        .rights = IPC_CAP_RIGHT_SEND | IPC_CAP_RIGHT_CALL | IPC_CAP_RIGHT_DUP,
        .object_type = IPC_OBJ_ENDPOINT,
        .object_id = ep_send->id,
        .ref_count = 1,
        .owner_task = t->id,
    };
    tab->caps[recv_handle] = (ipc_capability_t){
        .rights = IPC_CAP_RIGHT_RECV | IPC_CAP_RIGHT_DUP,
        .object_type = IPC_OBJ_ENDPOINT,
        .object_id = ep_recv->id,
        .ref_count = 1,
        .owner_task = t->id,
    };

    *(uint32_t *)out_send = send_handle;
    *(uint32_t *)out_recv = recv_handle;
    return 0;
}

#include "telemetry.h"

long sys_ipc_send(uint32_t cap_handle, const ipc_msg_t *user_msg,
                  uint32_t flags) {
    task_t *t = current_task();
    if (!t) return -1;

    trace_log(TRACE_IPC_SEND, (uint64_t)cap_handle, (uint64_t)flags);

    ipc_capability_t *cap = ipc_cap_resolve(t, cap_handle);
    if (!cap || !ipc_cap_has_right(cap, IPC_CAP_RIGHT_SEND)) {
        return -1;  /* EPERM */
    }

    if (cap->object_type != IPC_OBJ_ENDPOINT) return -1;  /* EINVAL */

    ipc_endpoint_t *ep = &g_endpoints[cap->object_id];
    if (ep->state != IPC_STATE_ACTIVE && ep->state != IPC_STATE_DRAINING) {
        return -1;  /* EPIPE */
    }

    /* Validate and copy user message */
    ipc_msg_t msg;
    if (user_msg->hdr.magic != IPC_MSG_MAGIC) return -1;  /* EINVAL */
    if (user_msg->hdr.payload_len > IPC_MSG_MAX_BYTES) return -1;
    if (user_msg->hdr.cap_count > IPC_MSG_MAX_CAPS) return -1;

    memcpy(&msg, user_msg, sizeof(ipc_msg_t));
    msg.hdr.sender_task = t->id;
    msg.hdr.sender_cap = cap_handle;

    /* Translate embedded capabilities */
    for (uint32_t i = 0; i < msg.hdr.cap_count; i++) {
        uint32_t src_cap = msg.caps[i];
        ipc_capability_t *src = ipc_cap_resolve(t, src_cap);
        if (!src) return -1;  /* EBADFD */
        if (flags & IPC_MSGF_CAP_TRANSFER) {
            /* Move: invalidate source, create in destination */
            ipc_cap_free_slot(ipc_get_cap_table(t), src_cap);
        }
        /* For now, we keep the same handle value; a real implementation
         * would allocate a new slot in the receiver's table. */
    }

    int ret = ipc_msg_enqueue(ep, &msg);
    if (ret < 0 && (flags & IPC_MSGF_NOBLOCK)) {
        return -1;  /* EAGAIN */
    }
    return ret;
}

long sys_ipc_recv(uint32_t cap_handle, ipc_msg_t *user_msg, uint32_t flags) {
    task_t *t = current_task();
    if (!t) return -1;

    ipc_capability_t *cap = ipc_cap_resolve(t, cap_handle);
    if (!cap || !ipc_cap_has_right(cap, IPC_CAP_RIGHT_RECV)) {
        return -1;  /* EPERM */
    }

    if (cap->object_type != IPC_OBJ_ENDPOINT) return -1;

    ipc_endpoint_t *ep = &g_endpoints[cap->object_id];

    ipc_msg_t msg;
    int ret = ipc_msg_dequeue(ep, &msg);
    if (ret == -2) {
        /* Would block */
        if (flags & IPC_MSGF_NOBLOCK) return -1;  /* EAGAIN */

        /* Block until message arrives */
        ipc_spin_lock(&ep->queue_lock);
        ipc_wait_enqueue(ep, (thread_t *)t);
        ipc_spin_unlock(&ep->queue_lock);
        sched_block();
        sched_schedule();  /* yields CPU */

        /* Resumed — retry dequeue */
        ret = ipc_msg_dequeue(ep, &msg);
    }

    if (ret < 0) return ret;

    memcpy(user_msg, &msg, sizeof(ipc_msg_t));
    return 0;
}

long sys_ipc_call(uint32_t cap_handle, const ipc_msg_t *req,
                  ipc_msg_t *resp, uint32_t timeout_ms) {
    /* Synchronous RPC: send request, block for response on peer endpoint.
     * This requires a channel (bidirectional) or a reply-cap mechanism.
     * For simplicity, we assume the cap points to a CHANNEL where the
     * peer endpoint is the response path. */
    task_t *t = current_task();
    if (!t) return -1;

    ipc_capability_t *cap = ipc_cap_resolve(t, cap_handle);
    if (!cap || !ipc_cap_has_right(cap, IPC_CAP_RIGHT_CALL)) return -1;

    if (cap->object_type != IPC_OBJ_ENDPOINT) return -1;
    ipc_endpoint_t *ep = &g_endpoints[cap->object_id];
    if (ep->peer_cap >= IPC_MAX_ENDPOINTS) return -1;

    ipc_endpoint_t *reply_ep = &g_endpoints[ep->peer_cap];

    /* Send request */
    int ret = sys_ipc_send(cap_handle, req, 0);
    if (ret < 0) return ret;

    /* Block for response on reply endpoint */
    /* Find the recv cap for reply_ep in our table */
    ipc_cap_table_t *tab = ipc_get_cap_table(t);
    uint32_t reply_cap = (uint32_t)-1;
    for (int i = 0; i < IPC_CAP_MAX_PER_TASK; i++) {
        if (tab->caps[i].object_id == reply_ep->id &&
            tab->caps[i].object_type == IPC_OBJ_ENDPOINT &&
            (tab->caps[i].rights & IPC_CAP_RIGHT_RECV)) {
            reply_cap = (uint32_t)i;
            break;
        }
    }
    if (reply_cap == (uint32_t)-1) return -1;

    return sys_ipc_recv(reply_cap, resp, 0);
}

long sys_ipc_port_create(uint32_t backlog, uint32_t *out_cap) {
    task_t *t = current_task();
    if (!t) return -1;

    ipc_port_t *port = NULL;
    if (ipc_alloc_port(&port) < 0) return -1;

    port->backlog = backlog > 16 ? 16 : backlog;
    port->owner_task = t->id;
    port->state = IPC_STATE_ACTIVE;

    ipc_cap_table_t *tab = ipc_get_cap_table(t);
    uint32_t handle;
    if (ipc_cap_alloc_slot(tab, &handle) < 0) {
        port->state = IPC_STATE_DEAD;
        return -1;
    }

    tab->caps[handle] = (ipc_capability_t){
        .rights = IPC_CAP_RIGHT_SEND | IPC_CAP_RIGHT_RECV | IPC_CAP_RIGHT_DUP,
        .object_type = IPC_OBJ_PORT,
        .object_id = port->id,
        .ref_count = 1,
        .owner_task = t->id,
    };

    *out_cap = handle;
    return 0;
}

long sys_ipc_port_accept(uint32_t port_cap, uint32_t *out_channel_cap,
                         uint32_t flags) {
    (void)flags;
    task_t *t = current_task();
    if (!t) return -1;

    ipc_capability_t *cap = ipc_cap_resolve(t, port_cap);
    if (!cap || cap->object_type != IPC_OBJ_PORT) return -1;

    ipc_port_t *port = &g_ports[cap->object_id];
    if (port->state != IPC_STATE_ACTIVE) return -1;

    /* Wait for pending connection */
    while (port->pending_count == 0) {
        if (port->state != IPC_STATE_ACTIVE) return -1;
        ipc_spin_lock(&g_ipc_lock);  /* reuse coarse lock for wait queue */
        ipc_wait_enqueue((ipc_endpoint_t *)port, (thread_t *)t);  /* cast: same layout */
        ipc_spin_unlock(&g_ipc_lock);
        sched_block();
        sched_schedule();
    }

    /* Pop first pending connection */
    /* ... simplified: create a new channel pair, return one side ... */
    uint32_t local, peer;
    if (sys_ipc_endpoint_create((uintptr_t)&local, (uintptr_t)&peer) < 0)
        return -1;

    *out_channel_cap = local;
    return 0;
}

long sys_ipc_ns_register(const char *name, uint32_t cap_handle) {
    task_t *t = current_task();
    if (!t) return -1;

    ipc_capability_t *cap = ipc_cap_resolve(t, cap_handle);
    if (!cap) return -1;

    return ipc_ns_insert(name, cap_handle, t->id);
}

long sys_ipc_ns_lookup(const char *name, uint32_t *out_cap_handle) {
    task_t *t = current_task();
    if (!t) return -1;

    ipc_ns_entry_t *entry = ipc_ns_find(name);
    if (!entry) return -1;  /* ENOENT */

    /* Return a copy of the capability into caller's table */
    ipc_cap_table_t *tab = ipc_get_cap_table(t);
    uint32_t handle;
    if (ipc_cap_alloc_slot(tab, &handle) < 0) return -1;

    /* Copy with narrowed rights (no REVOKE, no DUP by default) */
    ipc_capability_t *src = ipc_get_cap_table(
        sched_get_thread_by_id(entry->owner_task_id))->caps + entry->cap_handle;

    tab->caps[handle] = *src;
    tab->caps[handle].rights &= ~(IPC_CAP_RIGHT_REVOKE | IPC_CAP_RIGHT_DUP);
    tab->caps[handle].ref_count++;
    tab->caps[handle].owner_task = t->id;

    *out_cap_handle = handle;
    return 0;
}

long sys_ipc_object_destroy(uint32_t cap_handle) {
    task_t *t = current_task();
    if (!t) return -1;

    ipc_capability_t *cap = ipc_cap_resolve(t, cap_handle);
    if (!cap) return -1;
    if (!ipc_cap_has_right(cap, IPC_CAP_RIGHT_REVOKE)) return -1;

    switch (cap->object_type) {
        case IPC_OBJ_ENDPOINT:
        case IPC_OBJ_CHANNEL: {
            ipc_endpoint_t *ep = &g_endpoints[cap->object_id];
            ep->state = IPC_STATE_DEAD;
            if (ep->msg_ring) {
                sage_free(ep->msg_ring);
                ep->msg_ring = NULL;
            }
            break;
        }
        case IPC_OBJ_PORT: {
            ipc_port_t *port = &g_ports[cap->object_id];
            port->state = IPC_STATE_DEAD;
            break;
        }
        case IPC_OBJ_SHARED_MEM: {
            ipc_shared_mem_t *shm = &g_shm[cap->object_id];
            shm->state = IPC_STATE_DEAD;
            /* TODO: unmap from all tasks */
            break;
        }
        default:
            break;
    }

    ipc_cap_free_slot(ipc_get_cap_table(t), cap_handle);
    return 0;
}

long sys_ipc_object_pause(uint32_t cap_handle) {
    task_t *t = current_task();
    if (!t) return -1;

    ipc_capability_t *cap = ipc_cap_resolve(t, cap_handle);
    if (!cap || !ipc_cap_has_right(cap, IPC_CAP_RIGHT_REVOKE)) return -1;

    if (cap->object_type == IPC_OBJ_ENDPOINT || cap->object_type == IPC_OBJ_CHANNEL) {
        ipc_endpoint_t *ep = &g_endpoints[cap->object_id];
        return ipc_lifecycle_transition(ep, IPC_STATE_PAUSED) ? 0 : -1;
    }
    return -1;
}

long sys_ipc_object_resume(uint32_t cap_handle) {
    task_t *t = current_task();
    if (!t) return -1;

    ipc_capability_t *cap = ipc_cap_resolve(t, cap_handle);
    if (!cap) return -1;

    if (cap->object_type == IPC_OBJ_ENDPOINT || cap->object_type == IPC_OBJ_CHANNEL) {
        ipc_endpoint_t *ep = &g_endpoints[cap->object_id];
        ipc_lifecycle_state_t expected = IPC_STATE_PAUSED;
        if (__atomic_compare_exchange_n(&ep->state, &expected, IPC_STATE_ACTIVE,
                                         false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            return 0;
        }
        return -1;
    }
    return -1;
}

long sys_ipc_object_drain(uint32_t cap_handle, uint32_t timeout_ms) {
    (void)timeout_ms;
    task_t *t = current_task();
    if (!t) return -1;

    ipc_capability_t *cap = ipc_cap_resolve(t, cap_handle);
    if (!cap || !ipc_cap_has_right(cap, IPC_CAP_RIGHT_REVOKE)) return -1;

    if (cap->object_type == IPC_OBJ_ENDPOINT || cap->object_type == IPC_OBJ_CHANNEL) {
        ipc_endpoint_t *ep = &g_endpoints[cap->object_id];
        if (!ipc_lifecycle_transition(ep, IPC_STATE_DRAINING)) return -1;

        /* Wait until queue is empty */
        while (1) {
            ipc_spin_lock(&ep->queue_lock);
            if (ep->msg_count == 0) {
                ipc_spin_unlock(&ep->queue_lock);
                break;
            }
            ipc_spin_unlock(&ep->queue_lock);
            sched_yield();
        }

        ipc_lifecycle_state_t expected = IPC_STATE_DRAINING;
        __atomic_compare_exchange_n(&ep->state, &expected, IPC_STATE_DEAD,
                                    false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        return 0;
    }
    return -1;
}

long sys_ipc_cap_narrow(uint32_t handle, uint32_t new_rights) {
    task_t *t = current_task();
    if (!t) return -1;

    ipc_cap_table_t *tab = ipc_get_cap_table(t);
    if (handle >= IPC_CAP_MAX_PER_TASK) return -1;

    ipc_spin_lock(&tab->lock);
    ipc_capability_t *cap = &tab->caps[handle];
    if (!ipc_cap_is_valid(cap)) {
        ipc_spin_unlock(&tab->lock);
        return -1;
    }

    if (cap->flags & IPC_CAPF_IMMUTABLE) {
        ipc_spin_unlock(&tab->lock);
        return -1; /* EPERM */
    }

    /* Narrowing can only remove rights, never add them */
    cap->rights &= (new_rights & IPC_CAP_RIGHTS_ALL);
    
    ipc_spin_unlock(&tab->lock);
    return 0;
}

long sys_ipc_cap_revoke(uint32_t handle) {
    /* Revoke is effectively destruction/invalidation */
    return sys_ipc_object_destroy(handle);
}

long sys_ipc_cap_dup(uint32_t handle, uint32_t *out_new_handle) {
    task_t *t = current_task();
    if (!t) return -1;

    ipc_capability_t *src = ipc_cap_resolve(t, handle);
    if (!src || !ipc_cap_has_right(src, IPC_CAP_RIGHT_DUP)) return -1;

    ipc_cap_table_t *tab = ipc_get_cap_table(t);
    uint32_t new_h;
    if (ipc_cap_alloc_slot(tab, &new_h) < 0) return -1;

    ipc_spin_lock(&tab->lock);
    tab->caps[new_h] = *src;
    tab->caps[new_h].ref_count++;
    ipc_spin_unlock(&tab->lock);

    *out_new_handle = new_h;
    return 0;
}

/* ============================================================================
 * Dispatch Hook (to be wired into syscall.c)
 * ============================================================================ */

long ipc_syscall_dispatch(long num, long a1, long a2, long a3,
                          long a4, long a5) {
    switch (num) {
        case SYS_ipc_cap_narrow:
            return sys_ipc_cap_narrow((uint32_t)a1, (uint32_t)a2);
        case SYS_ipc_cap_revoke:
            return sys_ipc_cap_revoke((uint32_t)a1);
        case SYS_ipc_cap_dup:
            return sys_ipc_cap_dup((uint32_t)a1, (uint32_t *)a2);
        case SYS_ipc_endpoint_create:
            return sys_ipc_endpoint_create((uintptr_t)a1, (uintptr_t)a2);
        case SYS_ipc_send:
            return sys_ipc_send((uint32_t)a1, (const ipc_msg_t *)a2, (uint32_t)a3);
        case SYS_ipc_recv:
            return sys_ipc_recv((uint32_t)a1, (ipc_msg_t *)a2, (uint32_t)a3);
        case SYS_ipc_call:
            return sys_ipc_call((uint32_t)a1, (const ipc_msg_t *)a2,
                                (ipc_msg_t *)a3, (uint32_t)a4);
        case SYS_ipc_port_create:
            return sys_ipc_port_create((uint32_t)a1, (uint32_t *)a2);
        case SYS_ipc_port_accept:
            return sys_ipc_port_accept((uint32_t)a1, (uint32_t *)a2, (uint32_t)a3);
        case SYS_ipc_ns_register:
            return sys_ipc_ns_register((const char *)a1, (uint32_t)a2);
        case SYS_ipc_ns_lookup:
            return sys_ipc_ns_lookup((const char *)a1, (uint32_t *)a2);
        case SYS_ipc_object_destroy:
            return sys_ipc_object_destroy((uint32_t)a1);
        case SYS_ipc_object_pause:
            return sys_ipc_object_pause((uint32_t)a1);
        case SYS_ipc_object_resume:
            return sys_ipc_object_resume((uint32_t)a1);
        case SYS_ipc_object_drain:
            return sys_ipc_object_drain((uint32_t)a1, (uint32_t)a2);
        default:
            return -1;  /* ENOSYS */
    }
}
