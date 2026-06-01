#ifndef SAGEOS_TELEMETRY_H
#define SAGEOS_TELEMETRY_H

#include <stdint.h>
#include <stddef.h>

/* 
 * SageOS Telemetry Subsystem
 * High-performance circular buffer for system-wide tracing.
 */

typedef enum {
    TRACE_NONE = 0,
    TRACE_SCHED_SWITCH,
    TRACE_SCHED_PRIORITY,
    TRACE_IPC_SEND,
    TRACE_IPC_RECV,
    TRACE_VM_EXEC,
    TRACE_VM_CALL,
    TRACE_ALLOC_MALLOC,
    TRACE_ALLOC_FREE,
    TRACE_SYSCALL_ENTER,
    TRACE_VFS_READ,
    TRACE_VFS_WRITE,
    TRACE_VFS_MOUNT,
    TRACE_TIMER_TICK,
    TRACE_BOOT_STAGE,
    TRACE_ALLOC_STATS,
    TRACE_MAX
} trace_event_t;

typedef struct {
    uint64_t        timestamp;
    trace_event_t   event;
    uint32_t        cpu_id;
    uint32_t        task_id;
    uint64_t        arg1;
    uint64_t        arg2;
} trace_entry_t;

#define TELEMETRY_BUFFER_SIZE  1024

void telemetry_init(void);
void trace_log(trace_event_t event, uint64_t arg1, uint64_t arg2);
void trace_dump(void);
void trace_dump_filtered(trace_event_t filter);
int  trace_count(void);
void trace_clear(void);

#endif
