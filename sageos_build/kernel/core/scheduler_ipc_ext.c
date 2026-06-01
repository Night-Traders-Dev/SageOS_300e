/* ============================================================================
 * SageOS Scheduler — IPC Extension Implementation
 * ============================================================================ */

#include "scheduler_ipc_ext.h"
#include "scheduler.h"
#include "ipc.h"
#include "sage_alloc.h"
#include "console.h"
#include <string.h>

/* Forward declaration of kernel IPC function */
extern long sys_ipc_object_destroy(uint32_t cap_handle);

/* Global parallel array for IPC extensions */
thread_ipc_ext_t g_thread_ipc_ext[SCHED_MAX_THREADS];

/* ============================================================================
 * Thread Lifecycle Hooks
 * ============================================================================ */

void sched_ipc_init_thread(thread_t *t) {
    thread_ipc_ext_t *ext = thread_ipc_ext(t);
    memset(ext, 0, sizeof(thread_ipc_ext_t));

    /* Initialize capability table */
    ext->cap_table.next_free = 0;
    ext->cap_table.lock = 0;
    for (int i = 0; i < IPC_CAP_MAX_PER_TASK; i++) {
        ext->cap_table.caps[i].object_type = IPC_OBJ_NONE;
        ext->cap_table.caps[i].ref_count = 0;
        ext->cap_table.caps[i].path[0] = '\0';
    }

    /* Pre-populate slot 0 with VFS Root Directory capability */
    ext->cap_table.caps[0].object_type = IPC_OBJ_DIR;
    ext->cap_table.caps[0].rights = IPC_CAP_RIGHT_VFS_READ | IPC_CAP_RIGHT_VFS_WRITE;
    ext->cap_table.caps[0].ref_count = 1;
    strcpy(ext->cap_table.caps[0].path, "/");
    ext->cap_table.next_free = 1;

    ext->rpc_reply_cap = (uint32_t)-1;
    ext->rpc_timeout_tick = 0;
    ext->signal_mask = ~0ULL;  /* All signals blocked by default */
}

void sched_ipc_cleanup_thread(thread_t *t) {
    thread_ipc_ext_t *ext = thread_ipc_ext(t);

    /* Release all capabilities held by this thread */
    for (int i = 0; i < IPC_CAP_MAX_PER_TASK; i++) {
        ipc_capability_t *cap = &ext->cap_table.caps[i];
        if (ipc_cap_is_valid(cap)) {
            /* Decrement ref count; if zero, object may be destroyed */
            __atomic_sub_fetch(&cap->ref_count, 1, __ATOMIC_SEQ_CST);
            if (cap->ref_count == 0) {
                /* If we hold REVOKE right, destroy the object */
                if (cap->rights & IPC_CAP_RIGHT_REVOKE) {
                    sys_ipc_object_destroy((uint32_t)i);
                }
            }
            cap->object_type = IPC_OBJ_NONE;
        }
    }

    memset(ext, 0, sizeof(thread_ipc_ext_t));
}

/* ============================================================================
 * Wait Queue Integration
 * ============================================================================ */

void sched_ipc_on_block(thread_t *t, void *wait_queue_head) {
    /* The thread is already unlinked from the run queue by sched_block().
     * We now link it into the IPC wait queue. */
    thread_ipc_ext_t *ext = thread_ipc_ext(t);
    ext->ipc_wait_next = NULL;
    ext->ipc_wait_prev = NULL;

    /* The wait_queue_head is actually an ipc_endpoint_t* cast to void*.
     * We use the endpoint's wait_head/wait_tail fields. */
    ipc_endpoint_t *ep = (ipc_endpoint_t *)wait_queue_head;

    if (!ep->wait_head) {
        ep->wait_head = t;
        ep->wait_tail = t;
    } else {
        ep->wait_tail->next = t;  /* overlay: scheduler's next field */
        ep->wait_tail = t;
    }
}

void sched_ipc_on_unblock(thread_t *t) {
    /* Thread is being woken.  It is already unlinked from the IPC wait queue
     * by the sender (see ipc_msg_enqueue).  We just ensure it is placed back
     * on the scheduler's run queue by sched_unblock(). */
    (void)t;
}

/* ============================================================================
 * Boot-time Initialization
 * ============================================================================ */

void ipc_subsystem_init(void) {
    memset(g_thread_ipc_ext, 0, sizeof(g_thread_ipc_ext));

    /* Initialize the global namespace root */
    extern ipc_namespace_t g_namespace;
    memset(&g_namespace, 0, sizeof(g_namespace));

    /* Initialize global object pools */
    extern ipc_endpoint_t g_endpoints[IPC_MAX_ENDPOINTS];
    extern ipc_port_t     g_ports[IPC_MAX_PORTS];
    extern ipc_shared_mem_t g_shm[IPC_MAX_SHM];
    extern ipc_ns_entry_t g_ns_entries[IPC_MAX_NS_ENTRIES];

    memset(g_endpoints, 0, sizeof(g_endpoints));
    memset(g_ports, 0, sizeof(g_ports));
    memset(g_shm, 0, sizeof(g_shm));
    memset(g_ns_entries, 0, sizeof(g_ns_entries));

    for (int i = 0; i < IPC_MAX_ENDPOINTS; i++) {
        g_endpoints[i].state = IPC_STATE_DEAD;
        g_endpoints[i].id = (uint32_t)i;
    }
    for (int i = 0; i < IPC_MAX_PORTS; i++) {
        g_ports[i].state = IPC_STATE_DEAD;
        g_ports[i].id = (uint32_t)i;
    }
    for (int i = 0; i < IPC_MAX_SHM; i++) {
        g_shm[i].state = IPC_STATE_DEAD;
        g_shm[i].id = (uint32_t)i;
    }

    console_write("[IPC] Subsystem initialized\n");
}
