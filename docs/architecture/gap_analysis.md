# SageOS Architectural Gap Analysis (v0.7.9)

This document provides a technical assessment of the deviations and limitations present in the current SageOS implementation compared to the canonical designs.

---

## 1. POSIX File Operations vs. Capability-First VFS
**Status**: **RESOLVED (v0.7.9)**
- **Implementation**: Introduced `IPC_OBJ_FILE` and `IPC_OBJ_DIR` capability types.
- **Enforcement**: Syscalls `read`, `write`, `open`, `mkdir`, `unlink`, and `getdents64` are now gated by capability rights (`VFS_READ`, `VFS_WRITE`).
- **Authorization**: Tasks must hold a valid capability for the requested path or a parent directory.

---

## 2. Simulated Process Supervision (PID 1 `runtime_manager.sage`)
**Status**: **RESOLVED (v0.7.9)**
- **Implementation**: The supervisor now runs as a persistent, asynchronous background kernel task.
- **Dynamic Spawning**: Real multitasking is enabled via `os_spawn_task` FFI, allowing the supervisor to launch system services as separate scheduler-managed threads.
- **Active Monitoring**: The supervisor remains active after boot, orchestrating service lifecycles.

---

## 3. Hardware-Enforced Address Space Isolation
**Status**: **STRATEGIC MILESTONE (OPEN)**
- **The Design Ambition**: Separate execution domains—kernel space and process spaces—enforced by the hardware MMU.
- **Current State**: The system relies on SGVM-level isolation and kernel-mediated syscall boundaries. Physical MMU-based process isolation (paging/segmentation) for native tasks is not yet fully activated in the common scheduler.

---

## 4. VM Clock Freezing and Timer Cooperative Yield
**Status**: **RESOLVED (v0.7.9)**
- **Implementation**: Injected `timer_poll()` calls into the MetalVM execution loop.
- **Preemption**: The scheduler now correctly yields CPU time during long-running bytecode tasks, ensuring system-wide timer progression and responsiveness.
