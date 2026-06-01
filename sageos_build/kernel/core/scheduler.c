#include "scheduler.h"
#include "scheduler_ipc_ext.h"
#include "sage_alloc.h"
#include "console.h"
#include <string.h>

#define MAX_TASKS 64

static thread_t g_tasks[MAX_TASKS];
thread_t *g_current_task = NULL;
static int g_sched_inited = 0;
static uint32_t g_next_pid = 1;

extern void thread_switch(uint64_t *prev_rsp_ptr, uint64_t *next_rsp_ptr);

static void idle_task(void *arg) {
    (void)arg;
    extern void timer_idle_poll(void);
    while (1) {
        timer_idle_poll();
        sched_schedule();
    }
}

extern uint8_t stack_bottom[];
extern uint8_t stack_top_sym[]; // to avoid conflict if any

void sched_init(void) {
    memset(g_tasks, 0, sizeof(g_tasks));
    
    /* Initialize task 0 as idle task */
    g_tasks[0].id = 0;
    g_tasks[0].state = THREAD_STATE_READY;
    g_tasks[0].priority = THREAD_PRIORITY_LOW;
    strncpy(g_tasks[0].name, "idle", 31);
    strcpy(g_tasks[0].cwd, "/");
    
    g_tasks[0].stack_base = (uint64_t)stack_bottom;
    g_tasks[0].stack_top = (uint64_t)stack_bottom + 16384; // 16KB boot stack
    
    sched_ipc_init_thread(&g_tasks[0]);
    
    g_current_task = &g_tasks[0];
    g_sched_inited = 1;
}

thread_t *sched_current_thread(void) {
    if (!g_sched_inited) {
        sched_init();
    }
    return g_current_task;
}

thread_t *sched_create_thread(const char *name, void (*entry)(void *), void *arg, thread_priority_t priority) {
    thread_t *t = NULL;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].state == THREAD_STATE_UNUSED) {
            t = &g_tasks[i];
            break;
        }
    }
    if (!t) return NULL;

    memset(t, 0, sizeof(thread_t));
    t->id = g_next_pid++;
    t->state = THREAD_STATE_READY;
    t->priority = priority;
    strncpy(t->name, name, 31);
    strcpy(t->cwd, "/");
    
    sched_ipc_init_thread(t);

    t->stack_base = (uint64_t)sage_malloc(SCHED_STACK_SIZE);
    t->stack_top = t->stack_base + SCHED_STACK_SIZE;
    
    /* Setup initial stack for thread_switch */
    uint64_t *sp = (uint64_t *)t->stack_top;
    
#if defined(__aarch64__)
    sp -= 12; /* 12 registers: x19..x30 */
    sp[11] = (uint64_t)entry; /* x30 (LR) */
    sp[0] = (uint64_t)arg;    /* We actually need a wrapper to pass arg to entry, 
                                 but for simple fork we copy stack directly. */
#elif defined(__x86_64__)
    sp -= 8; /* r15..r12, rbx, rbp, rip + dummy return address to align stack to 16n + 8 */
    sp[6] = (uint64_t)entry; /* rip (popped by ret) */
    sp[7] = 0;               /* dummy return address (staying on stack) */
#elif defined(__riscv)
    sp -= 14; /* s0..s11, ra */
    sp[13] = (uint64_t)entry; /* ra */
#endif

    t->rsp = (uint64_t)sp;
    
    return t;
}

#include "telemetry.h"

void sched_schedule(void) {
    if (!g_sched_inited) return;

    thread_t *prev = g_current_task;
    thread_t *next = NULL;

    while (1) {
        /* Simple round robin */
        int current_idx = prev - g_tasks;
        for (int i = 1; i <= MAX_TASKS; i++) {
            int idx = (current_idx + i) % MAX_TASKS;
            if (g_tasks[idx].state == THREAD_STATE_READY || g_tasks[idx].state == THREAD_STATE_RUNNING) {
                next = &g_tasks[idx];
                break;
            }
        }

        if (next) break;

        /* If no tasks are ready, we must wait for an interrupt (like a timer or IO).
         * But wait, in a cooperative system without interrupts, this is a deadlock.
         * For SageOS virt, we yield via timer_idle_poll(). */
        extern void timer_idle_poll(void);
        timer_idle_poll();
    }

    if (next == prev) return;

    trace_log(TRACE_SCHED_SWITCH, (uint64_t)(prev ? prev->id : 0), (uint64_t)next->id);

    if (prev->state == THREAD_STATE_RUNNING) {
        prev->state = THREAD_STATE_READY;
    }
    next->state = THREAD_STATE_RUNNING;
    g_current_task = next;

    console_write("[sched] Switching to task ");
    console_u32(next->id);
    console_write(" at rsp=");
    console_hex64(next->rsp);
    console_write("\n");

    thread_switch(&prev->rsp, &next->rsp);
}

void sched_exit(void) {
    if (g_current_task) {
        g_current_task->state = THREAD_STATE_TERMINATED;
        /* Wake up parent if waiting */
        for (int i = 0; i < MAX_TASKS; i++) {
            if (g_tasks[i].state == THREAD_STATE_BLOCKED) {
                g_tasks[i].state = THREAD_STATE_READY;
            }
        }
        sched_schedule();
    }
    while(1);
}

/* Stubs for other scheduler functions */
const sched_stats_t *sched_get_stats(void) {
    static sched_stats_t dummy_stats = {0};
    return &dummy_stats;
}

void sched_cmd_info(void) {
    console_write("\nScheduler: Multitasking Enabled");
}

thread_t *sched_get_thread_by_id(uint32_t id) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (g_tasks[i].id == id && g_tasks[i].state != THREAD_STATE_UNUSED) {
            return &g_tasks[i];
        }
    }
    return NULL;
}

int sched_get_thread_info(uint32_t index, char *name, thread_state_t *state, uint32_t *cpu) {
    if (index < MAX_TASKS && g_tasks[index].state != THREAD_STATE_UNUSED) {
        strncpy(name, g_tasks[index].name, 31);
        *state = g_tasks[index].state;
        *cpu = 0;
        return 1;
    }
    return 0;
}

uint32_t sched_cpu_id(void) {
    return 0;
}

void sched_timer_tick(void) {
    /* Called by timer IRQ. Preemption logic goes here. */
}

void sched_yield(void) {
    sched_schedule();
}

void sched_block(void) {
    if (g_current_task) {
        g_current_task->state = THREAD_STATE_BLOCKED;
        sched_schedule();
    }
}

void sched_unblock(thread_t *thread) {
    if (thread && thread->state == THREAD_STATE_BLOCKED) {
        thread->state = THREAD_STATE_READY;
    }
}
