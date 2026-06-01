#include "telemetry.h"
#include "timer.h"
#include "console.h"
#include "scheduler.h"

static trace_entry_t g_trace_buffer[TELEMETRY_BUFFER_SIZE];
static uint32_t       g_trace_head = 0;
static uint32_t       g_trace_count = 0;
static uint32_t       g_trace_lock = 0;

static void trace_spin_lock(void) {
    while (__atomic_test_and_set(&g_trace_lock, __ATOMIC_SEQ_CST)) {
#if defined(__aarch64__)
        __asm__ volatile ("yield");
#elif defined(__riscv)
        __asm__ volatile ("fence");
#else
        __asm__ volatile ("pause");
#endif
    }
}

static void trace_spin_unlock(void) {
    __atomic_clear(&g_trace_lock, __ATOMIC_SEQ_CST);
}

void telemetry_init(void) {
    g_trace_head = 0;
    g_trace_count = 0;
    g_trace_lock = 0;
}

void trace_log(trace_event_t event, uint64_t arg1, uint64_t arg2) {
    trace_spin_lock();

    uint32_t idx = g_trace_head % TELEMETRY_BUFFER_SIZE;
    g_trace_buffer[idx].timestamp = timer_ticks();
    g_trace_buffer[idx].event = event;
    g_trace_buffer[idx].cpu_id = sched_cpu_id();
    
    thread_t *curr = sched_current_thread();
    g_trace_buffer[idx].task_id = curr ? curr->id : 0;
    
    g_trace_buffer[idx].arg1 = arg1;
    g_trace_buffer[idx].arg2 = arg2;

    g_trace_head = (g_trace_head + 1) % TELEMETRY_BUFFER_SIZE;
    if (g_trace_count < TELEMETRY_BUFFER_SIZE) g_trace_count++;

    trace_spin_unlock();
}

static const char* event_names[] = {
    "NONE",
    "SCHED_SWITCH",
    "SCHED_PRIO",
    "IPC_SEND",
    "IPC_RECV",
    "VM_EXEC",
    "VM_CALL",
    "ALLOC_MALLOC",
    "ALLOC_FREE",
    "SYSCALL_ENTER",
    "VFS_READ",
    "VFS_WRITE",
    "VFS_MOUNT",
    "TIMER_TICK",
    "BOOT_STAGE",
    "ALLOC_STATS"
};

void trace_dump(void) {
    trace_dump_filtered(TRACE_NONE);
}

void trace_dump_filtered(trace_event_t filter) {
    console_write("\n--- SageOS System Trace Dump ---\n");
    console_write("TIME | EVENT | CPU | TASK | ARG1 | ARG2\n");
    
    trace_spin_lock();
    uint32_t start = (g_trace_count < TELEMETRY_BUFFER_SIZE) ? 0 : g_trace_head;
    
    for (uint32_t i = 0; i < g_trace_count; i++) {
        uint32_t idx = (start + i) % TELEMETRY_BUFFER_SIZE;
        trace_entry_t *e = &g_trace_buffer[idx];
        
        if (filter != TRACE_NONE && e->event != filter) continue;
        
        console_u32((uint32_t)e->timestamp);
        console_write(" | ");
        if (e->event < TRACE_MAX) {
            console_write(event_names[e->event]);
        } else {
            console_write("UNKNOWN");
        }
        console_write(" | ");
        console_u32(e->cpu_id);
        console_write(" | ");
        console_u32(e->task_id);
        console_write(" | ");
        console_hex64(e->arg1);
        console_write(" | ");
        console_hex64(e->arg2);
        console_write("\n");
    }
    trace_spin_unlock();
}

int trace_count(void) {
    return (int)g_trace_count;
}

void trace_clear(void) {
    trace_spin_lock();
    g_trace_head = 0;
    g_trace_count = 0;
    trace_spin_unlock();
    console_write("[TRACE] Buffer cleared.\n");
}
