/* ============================================================================
 * SageOS Scheduler — IPC Extension Header
 * To be included after scheduler.h or merged into it.
 * Adds capability table and IPC-related fields to thread_t.
 * ============================================================================ */

#ifndef SAGEOS_SCHEDULER_IPC_EXT_H
#define SAGEOS_SCHEDULER_IPC_EXT_H

#include "ipc.h"

/* ============================================================================
 * Thread Extension — IPC Fields
 * ============================================================================
 * These fields extend thread_t (defined in scheduler.h).  In a real build,
 * they are either appended to the struct definition or accessed via a
 * parallel array.  We declare a parallel array here to avoid modifying
 * scheduler.h in place (which would require rebuilding all arch ports).
 */

typedef struct {
    ipc_cap_table_t cap_table;

    /* IPC wait linkage (overlays the scheduler's next/prev when blocked) */
    struct thread  *ipc_wait_next;
    struct thread  *ipc_wait_prev;

    /* Reply capability for in-flight RPC */
    uint32_t        rpc_reply_cap;
    uint32_t        rpc_timeout_tick;

    /* Signal set (bitmask of pending async events) */
    uint64_t        signal_pending;
    uint64_t        signal_mask;

    /* IPC statistics */
    uint64_t        ipc_msgs_sent;
    uint64_t        ipc_msgs_recv;
    uint64_t        ipc_bytes_tx;
    uint64_t        ipc_bytes_rx;
    uint64_t        ipc_rpc_count;
    uint64_t        ipc_rpc_latency_sum;
} thread_ipc_ext_t;

/* Global parallel array — indexed by thread id */
extern thread_ipc_ext_t g_thread_ipc_ext[SCHED_MAX_THREADS];

/* Convenience accessor */
static inline thread_ipc_ext_t *thread_ipc_ext(thread_t *t) {
    return &g_thread_ipc_ext[t->id % SCHED_MAX_THREADS];
}

/* Helper to get the IPC-cap table for a task (used by ipc.c) */
static inline ipc_cap_table_t *task_ipc_cap_table(task_t *t) {
    return &thread_ipc_ext((thread_t *)t)->cap_table;
}

/* ============================================================================
 * Scheduler Integration Hooks
 * ============================================================================ */

/* Called by sched_create_thread() after the thread struct is zeroed */
void sched_ipc_init_thread(thread_t *t);

/* Called by sched_destroy_thread() before freeing the thread */
void sched_ipc_cleanup_thread(thread_t *t);

/* Called by sched_block() / sched_unblock() to manage IPC wait queues */
void sched_ipc_on_block(thread_t *t, void *wait_queue_head);
void sched_ipc_on_unblock(thread_t *t);

/* ============================================================================
 * Boot-time IPC subsystem initialization
 * ============================================================================ */

void ipc_subsystem_init(void);

#endif /* SAGEOS_SCHEDULER_IPC_EXT_H */
