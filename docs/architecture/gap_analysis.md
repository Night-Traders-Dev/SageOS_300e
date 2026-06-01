# SageOS Architectural Gap Analysis (v0.7.2)

This document provides a highly objective, technical assessment of the deviations, limitations, and simulated behaviors present in the current SageOS implementation compared to the canonical designs described in the **Platform Specification** and **Core Systems Architecture**.

---

## 1. POSIX File Operations vs. Capability-First VFS

### The Design Ambition
The [Security Model Specification](security.md) states that SageOS adheres to a **Capability-First** model where all resources—including files and directories—can *only* be accessed via explicitly delegated, unforgeable tokens (Capabilities) held in a task's private capability table. There are no global mutable namespaces.

### The Actual Implementation (`sageos_build/kernel/core/syscall.c`)
VFS file operations in the system call layer (`sys_open`, `sys_read`, `sys_write`) still follow legacy **POSIX-like string-path lookup**:
* **Direct Path Mapping**: `sys_open(const char *path, int flags, int mode)` takes a standard absolute string path and operates directly on a global VFS structure.
* **No Directory Capabilities**: Tasks can open files using arbitrary string paths (e.g. `/etc/init.sage`) without needing directory handle capabilities.
* **Implicit File Descriptors**: File descriptor tables inside tasks map standard integer indices to path strings instead of mapping to secure kernel-mediated capabilities.
* **Global Access**: Any task can request access to any path, guarded only by standard directory string checks rather than explicit token checks.

**Status**: **Structural Gap**. POSIX-compatible string path resolution is active, and file capability routing remains unimplemented.

---

## 2. Simulated Process Supervision (PID 1 `runtime_manager.sage`)

### The Design Ambition
The [Core Systems Architecture](../core_systems_architecture.md) and platform specifications describe `runtime_manager.sage` as a fully operational system supervisor running as PID 1. It is designed to launch critical system processes, monitor resource bounds, manage inter-service dependencies, and perform dynamic self-healing/auto-restarts of crashed components.

### The Actual Implementation (`sageos_build/kernel/etc/system/sagelang/runtime_manager.sage`)
The supervisor behaves as a **mock state generator**:
* **Simulated Execution**: `start_service(name)` does not spawn a new kernel task or allocate a fresh address space. It simply creates a dictionary entry simulating state:
  ```python
  services[name] = {"status": "active", "pid": 100 + len(services)}
  ```
* **No Real Supervision**: The monitoring loop does not track live kernel process IDs, signal masks, or task failures. It executes a local loop yielding a mock "Pulse..." trace.
* **Static Sandbox**: The kernel AST interpreter (`sage_execute`) executes `runtime_manager.sage` to completion, and then directly proceeds to launch the main shell on the serial terminal rather than keeping the supervisor active as an active process orchestrator.

**Status**: **Simulated Subsystem**. Service bootstrapping and process supervision are logically mocked rather than physically integrated into the kernel scheduler.

---

## 3. Hardware-Enforced Address Space Isolation

### The Design Ambition
The system specifications describe separate execution domains—kernel space, process spaces, and user-space SGVM sandboxes—enforcing strong isolation layers to prevent side-channel leaks and arbitrary physical memory access.

### The Actual Implementation
Memory isolation is heavily dependent on **interpreter bounds checking** rather than hardware-enforced MMU paging:
* **Single Shared Address Space**: Standard kernel execution is identity-paged. Userspace tasks executed via the AST interpreter run within the same page tables as the core kernel.
* **Vulnerabilities**: While path limits are now sanitized via `strnlen` checks in `syscall.c` (mitigating buffer overflows), untrusted tasks sharing the higher-half memory map lack hardware-level page protection (e.g. User/Supervisor bit enforcement on a per-task PML4/Sv39 table basis) during VM FFI/bridge execution.
* **MetalVM Trust**: System integrity is entirely reliant on the correctness of `MetalVM`'s stack and instruction pointer checks.

**Status**: **Incomplete Isolation**. Hardware page-table isolation per user-space task is in an early stub phase; protection relies on runtime execution boundaries.

---

## 4. Cooperative Timer Starvation & Tick Progression

### The Design Ambition
The scheduler runs a preemptive, priority-based thread dispatcher capable of multiplexing native tasks and virtual machine instances seamlessly.

### The Actual Implementation
Tick tracking is cooperative under execution loops:
* **Busy-Wait Clock Starvation**: The PIT/timer subsystem on x86_64 relies on cooperative polling. If a task spins in a delay loop or a long bytecode execution path without calling `timer_poll()`, system ticks (`g_ticks`) halt completely, freezing scheduler intervals.
* **Mitigation**: While we successfully patched `timer_delay_ms` to perform cooperative polling (`timer_poll()`) in the delay loop, complex userspace loops that block hardware interrupts can still starve system tick progression.

**Status**: **Partially Mitigated**. Cooperative polling preserves the clock, but true asynchronous preemptive tick delivery requires deeper APIC/PLIC hardware integration.
