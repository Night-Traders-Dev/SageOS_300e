#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "scheduler.h"
#include "console.h"
#include "dmesg.h"
#include "io.h"
#include "smp.h"
#include "timer.h"

static uint8_t thread_stacks[SCHED_MAX_THREADS][SCHED_STACK_SIZE] __attribute__((aligned(16)));
static thread_t threads[SCHED_MAX_THREADS];
static uint32_t next_thread_id;
static uint32_t live_thread_count;

static run_queue_t run_queues[SAGEOS_MAX_CPUS];
static thread_t *current_threads[SAGEOS_MAX_CPUS];
static thread_t *idle_threads[SAGEOS_MAX_CPUS];
static volatile uint8_t need_resched[SAGEOS_MAX_CPUS];

static sched_spinlock_t thread_table_lock;
static sched_stats_t sched_stats;
static uint32_t managed_cpu_count;
static volatile uint32_t scheduler_ready;
static uint64_t last_balance_tick;

static void idle_thread_entry(void *arg) __attribute__((noreturn));
static void sched_thread_bootstrap(void) __attribute__((noreturn));

__attribute__((naked)) static void sched_context_switch(
    uint64_t *old_rsp __attribute__((unused)),
    uint64_t new_rsp __attribute__((unused))) {
    __asm__ volatile (
        "pushq %rbp\n"
        "pushq %rbx\n"
        "pushq %r12\n"
        "pushq %r13\n"
        "pushq %r14\n"
        "pushq %r15\n"
        "movq %rsp, (%rdi)\n"
        "movq %rsi, %rsp\n"
        "popq %r15\n"
        "popq %r14\n"
        "popq %r13\n"
        "popq %r12\n"
        "popq %rbx\n"
        "popq %rbp\n"
        "retq\n"
    );
}

__attribute__((naked, noreturn)) static void sched_context_start(
    uint64_t new_rsp __attribute__((unused))) {
    __asm__ volatile (
        "movq %rdi, %rsp\n"
        "popq %r15\n"
        "popq %r14\n"
        "popq %r13\n"
        "popq %r12\n"
        "popq %rbx\n"
        "popq %rbp\n"
        "retq\n"
    );
}

static uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void irq_restore(uint64_t flags) {
    if (flags & (1ULL << 9)) {
        __asm__ volatile ("sti" : : : "memory");
    } else {
        __asm__ volatile ("cli" : : : "memory");
    }
}

static void spin_lock(sched_spinlock_t *lock) {
    while (__sync_lock_test_and_set(&lock->locked, 1)) {
        while (lock->locked) cpu_pause();
    }
}

static void spin_unlock(sched_spinlock_t *lock) {
    __sync_lock_release(&lock->locked);
}

static uint32_t priority_weight(thread_priority_t priority) {
    switch (priority) {
    case THREAD_PRIORITY_LOW:      return 512;
    case THREAD_PRIORITY_NORMAL:   return 1024;
    case THREAD_PRIORITY_HIGH:     return 2048;
    case THREAD_PRIORITY_CRITICAL: return 4096;
    default:                       return 1024;
    }
}

static uint32_t priority_load(thread_priority_t priority) {
    return priority_weight(priority) / 512;
}

static uint32_t priority_slice(thread_priority_t priority) {
    return SCHED_BASE_SLICE_TICKS + (uint32_t)priority;
}

static int cpu_started(uint32_t cpu) {
    const CpuInfo *info;

    if (cpu == 0) return 1;
    info = smp_cpu(cpu);
    return info && info->started;
}

static int cpu_can_run_thread(uint32_t cpu, const thread_t *thread) {
    if (cpu >= managed_cpu_count) return 0;
    if (!cpu_started(cpu)) return 0;
    return thread->cpu_affinity < 0 || (uint32_t)thread->cpu_affinity == cpu;
}

uint32_t sched_cpu_id(void) {
    uint32_t cpu = smp_current_cpu_index();
    if (cpu >= managed_cpu_count) return 0;
    return cpu;
}

thread_t *sched_current_thread(void) {
    uint32_t cpu = sched_cpu_id();
    if (cpu >= managed_cpu_count) return NULL;
    return current_threads[cpu];
}

static void run_queue_init(run_queue_t *queue) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->load = 0;
    queue->min_vruntime = 0;
    queue->lock.locked = 0;
}

static void rq_remove_locked(run_queue_t *queue, thread_t *thread) {
    if (!thread || !thread->on_run_queue) return;

    if (thread->prev) thread->prev->next = thread->next;
    else queue->head = thread->next;

    if (thread->next) thread->next->prev = thread->prev;
    else queue->tail = thread->prev;

    if (queue->count > 0) queue->count--;
    if (queue->load >= priority_load(thread->priority)) {
        queue->load -= priority_load(thread->priority);
    } else {
        queue->load = 0;
    }

    thread->next = NULL;
    thread->prev = NULL;
    thread->on_run_queue = 0;

    if (queue->head) {
        queue->min_vruntime = queue->head->vruntime;
    } else if (thread->vruntime > queue->min_vruntime) {
        queue->min_vruntime = thread->vruntime;
    }
}

static void rq_enqueue_locked(run_queue_t *queue, thread_t *thread) {
    thread_t *pos;

    if (!thread || thread->is_idle || thread->on_run_queue) return;

    if (queue->min_vruntime > thread->vruntime &&
        queue->min_vruntime - thread->vruntime > SCHED_VRUNTIME_SCALE) {
        thread->vruntime = queue->min_vruntime;
    }

    thread->next = NULL;
    thread->prev = NULL;

    pos = queue->head;
    while (pos) {
        if (thread->vruntime < pos->vruntime) break;
        if (thread->vruntime == pos->vruntime && thread->priority > pos->priority) break;
        pos = pos->next;
    }

    if (!pos) {
        thread->prev = queue->tail;
        if (queue->tail) queue->tail->next = thread;
        else queue->head = thread;
        queue->tail = thread;
    } else {
        thread->next = pos;
        thread->prev = pos->prev;
        if (pos->prev) pos->prev->next = thread;
        else queue->head = thread;
        pos->prev = thread;
    }

    thread->on_run_queue = 1;
    queue->count++;
    queue->load += priority_load(thread->priority);
    if (queue->head) queue->min_vruntime = queue->head->vruntime;
}

static thread_t *rq_pop_locked(run_queue_t *queue) {
    thread_t *thread = queue->head;
    if (!thread) return NULL;
    rq_remove_locked(queue, thread);
    return thread;
}

static thread_t *rq_steal_candidate_locked(run_queue_t *queue, uint32_t dst_cpu) {
    thread_t *thread = queue->tail;

    while (thread) {
        if (!thread->is_idle &&
            thread->state == THREAD_STATE_READY &&
            thread->cpu_affinity < 0 &&
            dst_cpu < managed_cpu_count) {
            return thread;
        }
        thread = thread->prev;
    }

    return NULL;
}

static uint32_t choose_cpu_for_thread(thread_t *thread) {
    uint32_t best_cpu = 0;
    uint32_t best_load = UINT32_MAX;

    if (thread->cpu_affinity >= 0 &&
        (uint32_t)thread->cpu_affinity < managed_cpu_count) {
        return (uint32_t)thread->cpu_affinity;
    }

    for (uint32_t cpu = 0; cpu < managed_cpu_count; cpu++) {
        uint32_t load;
        if (!cpu_can_run_thread(cpu, thread)) continue;
        load = run_queues[cpu].load + run_queues[cpu].count;
        if (load < best_load) {
            best_load = load;
            best_cpu = cpu;
        }
    }

    return best_cpu;
}

static void enqueue_thread_on_cpu(thread_t *thread, uint32_t cpu) {
    run_queue_t *queue;
    uint64_t flags;

    if (!thread || thread->is_idle) return;
    if (cpu >= managed_cpu_count) cpu = 0;

    flags = irq_save();
    thread->state = THREAD_STATE_READY;
    thread->cpu_id = cpu;

    queue = &run_queues[cpu];
    spin_lock(&queue->lock);
    rq_enqueue_locked(queue, thread);
    spin_unlock(&queue->lock);
    irq_restore(flags);
}

static void enqueue_thread_balanced(thread_t *thread) {
    enqueue_thread_on_cpu(thread, choose_cpu_for_thread(thread));
}

static void setup_initial_stack(thread_t *thread) {
    uint64_t top = thread->stack_top & ~0xFULL;
    uint64_t *stack;

    top -= 8;
    stack = (uint64_t *)(uintptr_t)top;

    *(--stack) = (uint64_t)(uintptr_t)sched_thread_bootstrap;
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;
    *(--stack) = 0;

    thread->rsp = (uint64_t)(uintptr_t)stack;
}

static void copy_thread_name(thread_t *thread, const char *name, uint32_t id) {
    if (name && *name) {
        strncpy(thread->name, name, sizeof(thread->name) - 1);
        thread->name[sizeof(thread->name) - 1] = 0;
        return;
    }

    thread->name[0] = 't';
    thread->name[1] = 'h';
    thread->name[2] = 'r';
    thread->name[3] = 'e';
    thread->name[4] = 'a';
    thread->name[5] = 'd';
    thread->name[6] = '-';
    thread->name[7] = (char)('0' + (id % 10));
    thread->name[8] = 0;
}

static void make_cpu_name(char *dst, size_t dst_len, const char *prefix, uint32_t cpu) {
    size_t pos = 0;
    char digits[10];
    size_t digit_count = 0;

    while (*prefix && pos + 1 < dst_len) dst[pos++] = *prefix++;
    do {
        digits[digit_count++] = (char)('0' + (cpu % 10));
        cpu /= 10;
    } while (cpu && digit_count < sizeof(digits));

    while (digit_count > 0 && pos + 1 < dst_len) {
        dst[pos++] = digits[--digit_count];
    }
    dst[pos] = 0;
}

static thread_t *alloc_thread_slot(uint32_t *slot_out) {
    for (uint32_t i = 0; i < SCHED_MAX_THREADS; i++) {
        if (threads[i].id == 0 || threads[i].state == THREAD_STATE_TERMINATED) {
            *slot_out = i;
            return &threads[i];
        }
    }
    return NULL;
}

static void init_thread(thread_t *thread,
                        uint32_t slot,
                        const char *name,
                        void (*entry)(void *),
                        void *arg,
                        thread_priority_t priority,
                        uint8_t is_idle,
                        int affinity) {
    uint32_t id = next_thread_id++;

    memset(thread, 0, sizeof(*thread));
    thread->id = id;
    thread->state = THREAD_STATE_READY;
    thread->priority = priority;
    thread->runtime_ticks = 0;
    thread->vruntime = 0;
    thread->wake_tick = 0;
    thread->stack_base = (uint64_t)(uintptr_t)&thread_stacks[slot][0];
    thread->stack_top = thread->stack_base + SCHED_STACK_SIZE;
    thread->cpu_affinity = affinity;
    thread->cpu_id = affinity >= 0 ? (uint32_t)affinity : 0;
    thread->time_slice_ticks = priority_slice(priority);
    thread->is_idle = is_idle;
    thread->entry = entry;
    thread->arg = arg;
    copy_thread_name(thread, name, id);
    setup_initial_stack(thread);
}

static void account_current(uint32_t cpu, uint64_t now) {
    thread_t *current;
    uint64_t delta;
    uint32_t weight;

    if (cpu >= managed_cpu_count) return;
    current = current_threads[cpu];
    if (!current || current->state != THREAD_STATE_RUNNING) return;

    if (current->last_started_tick == 0) {
        current->last_started_tick = now;
        current->slice_started_tick = now;
        return;
    }

    if (now < current->last_started_tick) return;
    delta = now - current->last_started_tick;
    if (delta == 0) return;

    current->last_started_tick = now;
    if (current->is_idle) {
        sched_stats.idle_ticks += delta;
        return;
    }

    weight = priority_weight(current->priority);
    current->runtime_ticks += delta;
    current->vruntime += (delta * SCHED_VRUNTIME_SCALE) / weight;
    sched_stats.total_runtime_ticks += delta;

    if (now - current->slice_started_tick >= current->time_slice_ticks) {
        need_resched[cpu] = 1;
    }
}

static void wake_sleepers(uint64_t now) {
    uint64_t flags = irq_save();

    spin_lock(&thread_table_lock);
    for (uint32_t i = 0; i < SCHED_MAX_THREADS; i++) {
        thread_t *thread = &threads[i];
        if (thread->id == 0 || thread->is_idle) continue;
        if (thread->state != THREAD_STATE_SLEEPING) continue;
        if (thread->wake_tick > now) continue;

        thread->wake_tick = 0;
        thread->state = THREAD_STATE_READY;
        enqueue_thread_balanced(thread);
    }
    spin_unlock(&thread_table_lock);

    irq_restore(flags);
}

static void load_balance(void) {
    uint32_t max_cpu = 0;
    uint32_t min_cpu = 0;
    uint32_t max_score = 0;
    uint32_t min_score = UINT32_MAX;
    run_queue_t *src;
    run_queue_t *dst;
    thread_t *thread;

    if (managed_cpu_count <= 1) return;

    for (uint32_t cpu = 0; cpu < managed_cpu_count; cpu++) {
        uint32_t score;
        if (!cpu_started(cpu)) continue;
        score = run_queues[cpu].load + run_queues[cpu].count;
        if (score > max_score) {
            max_score = score;
            max_cpu = cpu;
        }
        if (score < min_score) {
            min_score = score;
            min_cpu = cpu;
        }
    }

    if (max_cpu == min_cpu) return;
    if (max_score <= min_score + 2) return;

    src = &run_queues[max_cpu];
    dst = &run_queues[min_cpu];

    if (max_cpu < min_cpu) {
        spin_lock(&src->lock);
        spin_lock(&dst->lock);
    } else {
        spin_lock(&dst->lock);
        spin_lock(&src->lock);
    }

    thread = rq_steal_candidate_locked(src, min_cpu);
    if (thread) {
        rq_remove_locked(src, thread);
        thread->cpu_id = min_cpu;
        rq_enqueue_locked(dst, thread);
        sched_stats.migrations++;
    }

    if (max_cpu < min_cpu) {
        spin_unlock(&dst->lock);
        spin_unlock(&src->lock);
    } else {
        spin_unlock(&src->lock);
        spin_unlock(&dst->lock);
    }

    sched_stats.balance_passes++;
}

static thread_t *dequeue_next(uint32_t cpu) {
    run_queue_t *queue = &run_queues[cpu];
    thread_t *next;

    spin_lock(&queue->lock);
    next = rq_pop_locked(queue);
    spin_unlock(&queue->lock);

    return next;
}

static void sched_exit_current(void) __attribute__((noreturn));
static void sched_exit_current(void) {
    thread_t *current = sched_current_thread();
    uint64_t flags = irq_save();

    if (current && !current->is_idle && current->state != THREAD_STATE_TERMINATED) {
        current->state = THREAD_STATE_TERMINATED;
        if (live_thread_count > 0) live_thread_count--;
        sched_stats.thread_count = live_thread_count;
    }

    irq_restore(flags);
    sched_schedule();

    for (;;) cpu_pause();
}

static void sched_thread_bootstrap(void) {
    thread_t *current = sched_current_thread();

    if (current && current->entry) {
        current->entry(current->arg);
    }

    sched_exit_current();
}

static void idle_thread_entry(void *arg) {
    uint32_t cpu = (uint32_t)(uintptr_t)arg;

    for (;;) {
        timer_idle_poll();
        if (cpu < managed_cpu_count && run_queues[cpu].count > 0) {
            sched_yield();
        } else {
            cpu_pause();
        }
    }
}

void sched_init(void) {
    uint64_t flags = irq_save();

    memset(threads, 0, sizeof(threads));
    memset(run_queues, 0, sizeof(run_queues));
    memset(current_threads, 0, sizeof(current_threads));
    memset(idle_threads, 0, sizeof(idle_threads));
    memset((void *)need_resched, 0, sizeof(need_resched));
    memset(&sched_stats, 0, sizeof(sched_stats));

    thread_table_lock.locked = 0;
    next_thread_id = 1;
    live_thread_count = 0;
    last_balance_tick = 0;

    managed_cpu_count = smp_cpu_count();
    if (managed_cpu_count == 0) managed_cpu_count = 1;
    if (managed_cpu_count > SAGEOS_MAX_CPUS) managed_cpu_count = SAGEOS_MAX_CPUS;
    sched_stats.cpu_count = managed_cpu_count;

    for (uint32_t cpu = 0; cpu < managed_cpu_count; cpu++) {
        run_queue_init(&run_queues[cpu]);
    }

    spin_lock(&thread_table_lock);
    for (uint32_t cpu = 0; cpu < managed_cpu_count; cpu++) {
        uint32_t slot = 0;
        char name[16];
        thread_t *idle = alloc_thread_slot(&slot);

        if (!idle) break;
        make_cpu_name(name, sizeof(name), "idle-", cpu);
        init_thread(idle,
                    slot,
                    name,
                    idle_thread_entry,
                    (void *)(uintptr_t)cpu,
                    THREAD_PRIORITY_LOW,
                    1,
                    (int)cpu);
        idle->cpu_id = cpu;
        idle_threads[cpu] = idle;
    }
    spin_unlock(&thread_table_lock);

    scheduler_ready = 1;
    irq_restore(flags);

    dmesg_log("Scheduler: fair per-CPU queues initialized");
}

thread_t *sched_create_thread(const char *name,
                              void (*entry)(void *),
                              void *arg,
                              thread_priority_t priority) {
    thread_t *thread;
    uint32_t slot = 0;
    uint64_t flags;

    if (!scheduler_ready || !entry) return NULL;
    if (priority >= THREAD_PRIORITY_MAX) priority = THREAD_PRIORITY_NORMAL;

    flags = irq_save();
    spin_lock(&thread_table_lock);

    if (live_thread_count >= SCHED_MAX_THREADS - managed_cpu_count) {
        spin_unlock(&thread_table_lock);
        irq_restore(flags);
        return NULL;
    }

    thread = alloc_thread_slot(&slot);
    if (!thread) {
        spin_unlock(&thread_table_lock);
        irq_restore(flags);
        return NULL;
    }

    init_thread(thread, slot, name, entry, arg, priority, 0, -1);
    live_thread_count++;
    sched_stats.thread_count = live_thread_count;

    spin_unlock(&thread_table_lock);
    irq_restore(flags);

    enqueue_thread_balanced(thread);
    dmesg_log("Scheduler: thread created");
    dmesg_log(thread->name);
    return thread;
}

void sched_destroy_thread(thread_t *thread) {
    uint64_t flags;

    if (!thread || thread->is_idle || thread->state == THREAD_STATE_TERMINATED) return;
    if (thread == sched_current_thread()) sched_exit_current();

    flags = irq_save();

    spin_lock(&thread_table_lock);
    if (thread->state != THREAD_STATE_TERMINATED) {
        thread->state = THREAD_STATE_TERMINATED;
        if (live_thread_count > 0) live_thread_count--;
        sched_stats.thread_count = live_thread_count;
    }
    spin_unlock(&thread_table_lock);

    if (thread->on_run_queue && thread->cpu_id < managed_cpu_count) {
        run_queue_t *queue = &run_queues[thread->cpu_id];
        spin_lock(&queue->lock);
        rq_remove_locked(queue, thread);
        spin_unlock(&queue->lock);
    }

    irq_restore(flags);
}

void sched_schedule(void) {
    uint32_t cpu;
    uint64_t flags;
    uint64_t now;
    thread_t *prev;
    thread_t *next;
    uint64_t next_rsp;

    if (!scheduler_ready) return;

    cpu = sched_cpu_id();
    if (cpu >= managed_cpu_count) cpu = 0;

    flags = irq_save();
    now = timer_ticks();
    account_current(cpu, now);

    prev = current_threads[cpu];
    if (prev && prev->state == THREAD_STATE_RUNNING && !prev->is_idle) {
        enqueue_thread_on_cpu(prev, cpu);
    }

    next = dequeue_next(cpu);
    if (!next) next = idle_threads[cpu];
    if (!next) {
        irq_restore(flags);
        return;
    }

    next->state = THREAD_STATE_RUNNING;
    next->cpu_id = cpu;
    next->last_started_tick = now;
    next->slice_started_tick = now;
    current_threads[cpu] = next;
    need_resched[cpu] = 0;

    if (prev == next) {
        irq_restore(flags);
        return;
    }

    sched_stats.context_switches++;
    next_rsp = next->rsp;

    if (!prev) {
        irq_restore(flags);
        sched_context_start(next_rsp);
    }

    irq_restore(flags);
    sched_context_switch(&prev->rsp, next_rsp);
}

void sched_start_on_cpu(uint32_t cpu_id) {
    uint64_t flags;

    while (!scheduler_ready) cpu_pause();
    if (cpu_id >= managed_cpu_count) cpu_id = sched_cpu_id();
    if (cpu_id >= managed_cpu_count) cpu_id = 0;

    flags = irq_save();
    current_threads[cpu_id] = NULL;
    need_resched[cpu_id] = 1;
    irq_restore(flags);

    sched_schedule();
    for (;;) cpu_pause();
}

void sched_start(void) {
    sched_start_on_cpu(sched_cpu_id());
}

void sched_yield(void) {
    sched_schedule();
}

void sched_sleep(uint32_t ms) {
    thread_t *current = sched_current_thread();
    uint64_t ticks;
    uint64_t flags;

    if (!current || current->is_idle) {
        timer_delay_ms(ms);
        return;
    }

    ticks = (ms + 9) / 10;
    if (ticks == 0) ticks = 1;

    flags = irq_save();
    current->wake_tick = timer_ticks() + ticks;
    current->state = THREAD_STATE_SLEEPING;
    irq_restore(flags);

    sched_schedule();
}

void sched_block(void) {
    thread_t *current = sched_current_thread();
    uint64_t flags;

    if (!current || current->is_idle) return;

    flags = irq_save();
    current->state = THREAD_STATE_BLOCKED;
    irq_restore(flags);

    sched_schedule();
}

void sched_unblock(thread_t *thread) {
    uint64_t flags;

    if (!thread || thread->is_idle) return;

    flags = irq_save();
    if (thread->state == THREAD_STATE_BLOCKED || thread->state == THREAD_STATE_SLEEPING) {
        thread->wake_tick = 0;
        thread->state = THREAD_STATE_READY;
        irq_restore(flags);
        enqueue_thread_balanced(thread);
        return;
    }
    irq_restore(flags);
}

void sched_set_priority(thread_t *thread, thread_priority_t priority) {
    uint64_t flags;

    if (!thread || priority >= THREAD_PRIORITY_MAX) return;

    flags = irq_save();
    if (thread->on_run_queue && thread->cpu_id < managed_cpu_count) {
        run_queue_t *queue = &run_queues[thread->cpu_id];
        spin_lock(&queue->lock);
        rq_remove_locked(queue, thread);
        thread->priority = priority;
        thread->time_slice_ticks = priority_slice(priority);
        rq_enqueue_locked(queue, thread);
        spin_unlock(&queue->lock);
    } else {
        thread->priority = priority;
        thread->time_slice_ticks = priority_slice(priority);
    }
    irq_restore(flags);
}

void sched_set_affinity(thread_t *thread, int cpu_id) {
    uint64_t flags;

    if (!thread || thread->is_idle) return;
    if (cpu_id >= (int)managed_cpu_count) return;

    flags = irq_save();
    if (thread->on_run_queue && thread->cpu_id < managed_cpu_count) {
        run_queue_t *queue = &run_queues[thread->cpu_id];
        spin_lock(&queue->lock);
        rq_remove_locked(queue, thread);
        spin_unlock(&queue->lock);
        thread->cpu_affinity = cpu_id;
        irq_restore(flags);
        enqueue_thread_balanced(thread);
        return;
    }

    thread->cpu_affinity = cpu_id;
    irq_restore(flags);
}

void sched_timer_tick(void) {
    uint32_t cpu;
    uint64_t now;

    if (!scheduler_ready) return;

    cpu = sched_cpu_id();
    if (cpu >= managed_cpu_count) return;

    now = timer_ticks();
    account_current(cpu, now);
    wake_sleepers(now);

    if (cpu == 0 && now - last_balance_tick >= SCHED_BALANCE_TICKS) {
        last_balance_tick = now;
        load_balance();
    }
}

const sched_stats_t *sched_get_stats(void) {
    return &sched_stats;
}

static const char *state_name(thread_state_t state) {
    switch (state) {
    case THREAD_STATE_READY:      return "ready";
    case THREAD_STATE_RUNNING:    return "running";
    case THREAD_STATE_SLEEPING:   return "sleeping";
    case THREAD_STATE_BLOCKED:    return "blocked";
    case THREAD_STATE_TERMINATED: return "terminated";
    default:                      return "unused";
    }
}

void sched_cmd_info(void) {
    console_write("\nScheduler:");
    console_write("\n  policy: weighted fair queues, cooperative switch, timer accounting");
    console_write("\n  cpus: ");
    console_u32(sched_stats.cpu_count);
    console_write("\n  threads: ");
    console_u32(sched_stats.thread_count);
    console_write("\n  switches: ");
    console_hex64(sched_stats.context_switches);
    console_write("\n  balance passes: ");
    console_hex64(sched_stats.balance_passes);
    console_write("\n  migrations: ");
    console_hex64(sched_stats.migrations);

    for (uint32_t cpu = 0; cpu < managed_cpu_count; cpu++) {
        thread_t *current = current_threads[cpu];
        console_write("\n  cpu");
        console_u32(cpu);
        console_write(cpu_started(cpu) ? " online" : " offline");
        console_write(" rq=");
        console_u32(run_queues[cpu].count);
        console_write(" load=");
        console_u32(run_queues[cpu].load);
        console_write(" current=");
        console_write(current ? current->name : "none");
    }

    console_write("\n  active threads:");
    for (uint32_t i = 0; i < SCHED_MAX_THREADS; i++) {
        thread_t *thread = &threads[i];
        if (thread->id == 0 || thread->is_idle || thread->state == THREAD_STATE_TERMINATED) continue;
        console_write("\n    #");
        console_u32(thread->id);
        console_write(" ");
        console_write(thread->name);
        console_write(" ");
        console_write(state_name(thread->state));
        console_write(" cpu=");
        console_u32(thread->cpu_id);
        console_write(" prio=");
        console_u32((uint32_t)thread->priority);
        console_write(" run=");
        console_hex64(thread->runtime_ticks);
    }
}
