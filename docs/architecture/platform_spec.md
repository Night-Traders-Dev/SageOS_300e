# SageOS Platform Specification

## 1. Introduction
This document defines the formal architectural contracts between the various layers of SageOS. It establishes the guarantees provided by the platform firmware, kernel, VFS, and language runtime.

## 2. Bootstrap Sequence & State Machine
SageOS initialization follows a strictly ordered sequence of stages. Each stage MUST satisfy its preconditions before transitioning to the next.

| Stage | Name | Guarantees |
|-------|------|------------|
| STAGE_0 | FIRMWARE | Memory map available, boot device identified. |
| STAGE_1 | EARLY_MM | Physical allocator and page tables initialized. |
| STAGE_2 | IRQ_INIT | Exceptions and hardware interrupts enabled. |
| STAGE_3 | DEVICE_DISCOVERY | Console active, timers running, bus scanning complete. |
| STAGE_4 | STORAGE_VFS | FAT32/BTRFS mounted, `/etc`, `/boot`, `/dev` available. |
| STAGE_5 | RUNTIME_BRINGUP | SGVM core initialized, IPC namespace active. |
| STAGE_6 | SERVICE_ACTIVATION | `runtime_manager.sage` active (asynchronous), system services starting. |
| STAGE_7 | USERSPACE_SESSION | Shell prompt available, multitasking enabled. |

## 3. Runtime Execution Contract
The Sage runtime (`SGVM`) distinguishes between three execution modes:

1. **Source Execution**: Parsing and executing literal SageLang code.
2. **File Execution**: Loading and executing a `.sage` source file from the VFS.
3. **Module Import**: Resolving and loading a module through the `ModuleCache`.

### 3.1 Failure Semantics
- If a requested **File** is not found, the runtime MUST abort execution with `ENOENT`.
- If a **Source** block contains syntax errors, the parser MUST report them and abort.
- Under NO circumstances should a failed file path resolution fall back to interpreting the path string as source code.

## 4. Firmware Abstraction Layer (FAL)
SageOS abstracts firmware differences (UEFI, OpenSBI, U-Boot) through a standardized interface:

- `fal_get_memory_map()`
- `fal_get_framebuffer()`
- `fal_reboot()` / `fal_poweroff()`

## 5. ABI & Versioning
All system interfaces MUST include versioning headers to ensure compatibility between the kernel and the language runtime.

- `SAGE_ABI_MAJOR`: Breaking changes to syscalls or IPC.
- `SAGE_ABI_MINOR`: Backwards-compatible additions.

## 6. Memory Model
SageOS assumes a **Weakly Ordered** memory model to ensure portability across x86_64, ARM64, and RISC-V.
- Atomic operations MUST use explicit memory barriers (`smp_mb`, `smp_rmb`, `smp_wmb`).
- SMP systems MUST ensure cache coherency across cores before STAGE_5.
