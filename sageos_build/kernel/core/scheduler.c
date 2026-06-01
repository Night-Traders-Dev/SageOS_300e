#include "scheduler.h"
#include "scheduler_ipc_ext.h"
#include "sage_alloc.h"
#include "console.h"
#include <string.h>

#define MAX_TASKS 128
#define WATCHDOG_TIMEOUT_TICKS 500 /* 5 seconds at 100Hz */

static thread_t g_tasks[MAX_TASKS];
thread_t *g_current_task = NULL;
static int g_sched_inited = 0;
static uint64_t g_watchdog_counter = 0;
static uint64_t g_last_watchdog_update = 0;

void sched_watchdog_update(void) {
    g_watchdog_counter++;
}

static void kernel_panic(const char* msg) {
    console_write("\n\033[1;31m!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\033[0m\n");
    console_write("\033[1;31m!!                      KERNEL PANIC                          !!\033[0m\n");
    console_write("\033[1;31m!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\033[0m\n");
    console_write("Reason: ");
    console_write(msg);
    console_write("\n\nHalting CPU.\n");

#if defined(__x86_64__) || defined(__i386__)

    while (1) {
        __asm__ volatile("cli; hlt");
    }

#elif defined(__aarch64__)

    while (1) {
        __asm__ volatile(
            "msr daifset, #0xf\n" // mask interrupts
            "wfi\n"               // wait for interrupt
        );
    }

#elif defined(__riscv)

    while (1) {
        __asm__ volatile(
            "csrci mstatus, 8\n" // clear MIE bit (disable interrupts)
            "wfi\n"              // wait for interrupt
        );
    }

#else

    while (1) {
        __asm__ volatile("" ::: "memory");
    }

#endif
}

static uint32_t g_next_pid = 1;

extern void thread_switch(uint64_t *prev_rsp_ptr, uint64_t *next_rsp_ptr);

static void idle_task(void *arg) {
    (void)arg;
    extern void timer_idle_poll(void);
    while (1) {
        sched_watchdog_update();
        timer_idle_poll();
        sched_schedule();
    }
}

extern uint8_t stack_bottom[];
extern uint8_t stack_top_sym[]; // to avoid conflict if any

static void sched_trampoline(void) {
    /* The newly switched-to task starts here. 
     * Registers x19/rbx/s0 were used to store entry and arg. */
    thread_t *t = g_current_task;
    void (*entry)(void *) = t->entry;
    void *arg = t->arg;
    
    /* Release scheduler lock if we had one (we don't yet) */
    
    if (entry) {
        entry(arg);
    }
    
    /* Thread finished */
    extern void sys_exit(int code);
    sys_exit(0);
}

void sched_init(void) {
    memset(g_tasks, 0, sizeof(g_tasks));
    
    /* Initialize task 0 as boot/idle task */
    g_tasks[0].id = 0;
    g_tasks[0].state = THREAD_STATE_RUNNING;
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
    t->entry = entry;
    t->arg = arg;
    strncpy(t->name, name, 31);
    strcpy(t->cwd, "/");
    
    sched_ipc_init_thread(t);

    t->stack_base = (uint64_t)sage_malloc(SCHED_STACK_SIZE);
    t->stack_top = t->stack_base + SCHED_STACK_SIZE;
    
    /* Setup initial stack for thread_switch to start at trampoline */
    uint64_t *sp = (uint64_t *)t->stack_top;
    
#if defined(__aarch64__)
    sp -= 12; /* x19..x30 */
    sp[11] = (uint64_t)sched_trampoline; /* x30 (LR) */
#elif defined(__x86_64__)
    sp -= 8; /* r15..r12, rbx, rbp, rip + dummy */
    sp[6] = (uint64_t)sched_trampoline; /* rip */
    sp[7] = 0;
#elif defined(__riscv)
    sp -= 14; /* s0..s11, ra */
    sp[13] = (uint64_t)sched_trampoline; /* ra */
#endif

    t->rsp = (uint64_t)sp;
    
    return t;
}

#include "telemetry.h"

void sched_schedule(void) {
    if (!g_sched_inited) return;

    static int in_sched = 0;
    if (in_sched) return;
    in_sched = 1;

    thread_t *prev = g_current_task;
    thread_t *next = NULL;

    while (1) {
        /* Simple round robin */
        int current_idx = (prev && prev >= g_tasks && prev < g_tasks + MAX_TASKS) ? (prev - g_tasks) : 0;
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
        sched_watchdog_update();
        timer_idle_poll();
    }

    if (next == prev) {
        in_sched = 0;
        return;
    }

    trace_log(TRACE_SCHED_SWITCH, (uint64_t)(prev ? prev->id : 0), (uint64_t)next->id);

    if (prev->state == THREAD_STATE_RUNNING) {
        prev->state = THREAD_STATE_READY;
    }
    next->state = THREAD_STATE_RUNNING;
    g_current_task = next;

    /* Quiet debug: console_write("[sched] Switching to task..."); */

    in_sched = 0;
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
    /* Watchdog check */
    static uint64_t last_counter = 0;
    static uint64_t ticks_since_update = 0;

    if (g_watchdog_counter == last_counter) {
        ticks_since_update++;
        if (ticks_since_update > WATCHDOG_TIMEOUT_TICKS) {
            kernel_panic("WATCHDOG TIMEOUT - Kernel hung or deadlock detected");
        }
    } else {
        last_counter = g_watchdog_counter;
        ticks_since_update = 0;
    }

    /* Called by timer IRQ. Preemption logic via yield. */
    sched_yield();
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
