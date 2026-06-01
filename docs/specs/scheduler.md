# Scheduler Specification

## 1. Design Philosophy
The SageOS scheduler is a preemptive, SMP-aware, and priority-based system designed to manage a mix of native and managed execution workloads. It is "runtime-cooperative," meaning it has direct visibility into and control over SGVM execution states.

## 2. Core API (`scheduler.h`)
The kernel exports the following stable interfaces for thread management:

- `sched_create_thread()`: Allocates and initializes a new schedulable entity (Native or SGVM).
- `sched_yield()`: Voluntarily relinquishes the current CPU time slice.
- `sched_block()` / `sched_unblock()`: Transitions threads between running and waiting states based on resource availability or IPC events.
- `sched_current_thread()`: Returns the context of the thread currently executing on the local core.

## 3. Multitasking & Preemption
- **Preemptive Multitasking**: Hardware timers (APIC, Generic Timer, etc.) trigger periodic interrupts to force context switches.
- **Cooperative Integration**: For SGVM execution, the interpreter includes `timer_poll()` calls. This allows the scheduler to perform context switches even when the processor is executing high-level bytecode, preventing "busy-wait" starvation in the VM.

## 4. SMP Load Balancing
On multi-core systems, the scheduler maintains per-core runqueues and implements periodic load balancing to ensure efficient distribution of tasks. Affinity masks are supported for pinning critical system services to specific cores.

## 5. Global Interpreter Lock (GIL)
To ensure the integrity of the SageLang AST-level execution and object system, SageOS employs a **Global Interpreter Lock (GIL)**.
- `sage_gil_acquire()`: Must be called before any thread enters the AST interpreter.
- `sage_gil_release()`: Must be called when the interpreter loop yields or finishes.
The scheduler coordinates GIL acquisition to prevent deadlocks and ensure that managed execution remains thread-safe across SMP cores.
