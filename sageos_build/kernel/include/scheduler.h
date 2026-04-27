#ifndef SAGEOS_SCHEDULER_H
#define SAGEOS_SCHEDULER_H

#include <stdint.h>
#include <stddef.h>

#define SCHED_MAX_THREADS       64
#define SCHED_STACK_SIZE        16384
#define SCHED_BASE_SLICE_TICKS  2
#define SCHED_BALANCE_TICKS     25
#define SCHED_VRUNTIME_SCALE    1024ULL

typedef enum {
    THREAD_STATE_UNUSED = 0,
    THREAD_STATE_READY,
    THREAD_STATE_RUNNING,
    THREAD_STATE_SLEEPING,
    THREAD_STATE_BLOCKED,
    THREAD_STATE_TERMINATED
} thread_state_t;

typedef enum {
    THREAD_PRIORITY_LOW = 0,
    THREAD_PRIORITY_NORMAL = 1,
    THREAD_PRIORITY_HIGH = 2,
    THREAD_PRIORITY_CRITICAL = 3,
    THREAD_PRIORITY_MAX
} thread_priority_t;

typedef struct thread {
    uint32_t id;
    thread_state_t state;
    thread_priority_t priority;

    uint64_t runtime_ticks;
    uint64_t vruntime;
    uint64_t last_started_tick;
    uint64_t slice_started_tick;
    uint64_t wake_tick;

    uint64_t stack_base;
    uint64_t stack_top;
    uint64_t rsp;

    int cpu_affinity;
    uint32_t cpu_id;
    uint32_t time_slice_ticks;
    uint8_t on_run_queue;
    uint8_t is_idle;

    struct thread *next;
    struct thread *prev;

    void (*entry)(void *arg);
    void *arg;

    char name[32];
} thread_t;

typedef struct {
    volatile uint32_t locked;
} sched_spinlock_t;

typedef struct {
    thread_t *head;
    thread_t *tail;
    uint32_t count;
    uint32_t load;
    uint64_t min_vruntime;
    sched_spinlock_t lock;
} run_queue_t;

typedef struct {
    uint64_t context_switches;
    uint64_t balance_passes;
    uint64_t migrations;
    uint64_t total_runtime_ticks;
    uint64_t idle_ticks;
    uint32_t thread_count;
    uint32_t cpu_count;
} sched_stats_t;

void sched_init(void);
void sched_start(void) __attribute__((noreturn));
void sched_start_on_cpu(uint32_t cpu_id) __attribute__((noreturn));

thread_t *sched_create_thread(const char *name, void (*entry)(void *), void *arg, thread_priority_t priority);
void sched_destroy_thread(thread_t *thread);
void sched_yield(void);
void sched_sleep(uint32_t ms);
void sched_block(void);
void sched_unblock(thread_t *thread);
void sched_set_priority(thread_t *thread, thread_priority_t priority);
void sched_set_affinity(thread_t *thread, int cpu_id);

void sched_timer_tick(void);
void sched_schedule(void);
thread_t *sched_current_thread(void);
uint32_t sched_cpu_id(void);

const sched_stats_t *sched_get_stats(void);
void sched_cmd_info(void);

#endif
