# SageOS Release Notes: v0.7.9 (Active Multitasking)
*Date: June 1, 2026*

## 1. Overview
The **v0.7.9** release marks a transition from a simulated boot sequence to a physically integrated, capability-gated multitasking environment. It resolves critical architectural gaps in process supervision, file security, and system stability.

## 2. Key Features

### Asynchronous PID 1 Supervision
The System Supervisor (`runtime_manager.sage`) has been transitioned from a synchronous "Stage 6" boot step to a real, persistent background kernel thread. It now actively orchestrates the system lifecycle and monitors service health.

### Multi-Arch Multitasking
A unified kernel scheduler is now active across all three major architectures (x86_64, ARM64, and RISC-V 64).
- **Thread Trampolines**: Standardized thread entry logic ensures robust task spawning on all CPU variants.
- **Multitasking FFI**: The `os_spawn_task` bridge allows system services implemented in SageLang to launch real scheduler threads.

### Capability-First VFS
The Virtual Filesystem is now physically integrated into the capability security model.
- **Object Types**: `IPC_OBJ_FILE` and `IPC_OBJ_DIR`.
- **Enforcement**: Mandatory capability gating for all VFS syscalls (`read`, `write`, `open`, etc.).
- **Authorization**: Access is mediated by unforgeable tokens rather than global paths.

### System Stability Hardening
- **Global Interpreter Lock (GIL)**: Thread-safe AST interpreter orchestration, preventing memory corruption during concurrent script execution.
- **MetalVM Preemption**: Cooperative timer polling within the VM loop eliminates clock freezing and ensures responsive task switching.
- **Thread-Safe Allocator**: Standardized bump allocator with synchronization guards for concurrent kernel tasks.
- **FAT32 Robustness**: Segmented path resolution and improved LFN reconstruction logic.

## 3. Architecture Status

| Architecture | Status | Multitasking | VFS Security |
|--------------|--------|--------------|--------------|
| **x86_64** | Stable | Active | Capability-Gated |
| **ARM64** | Stable | Active | Capability-Gated |
| **RISC-V 64** | Stable | Active | Capability-Gated |

## 4. Documentation Updates
- Updated **Security Model** with VFS capability details.
- Formalized **Syscall ABI v0.4** with mandatory gating.
- Revised **Gap Analysis** (Gaps 1, 2, and 4 RESOLVED).
- Updated **SageOS Book** and **Architecture Guides**.

---
*Night-Traders-Dev*
