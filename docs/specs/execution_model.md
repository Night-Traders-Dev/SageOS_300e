# Execution Model Specification

## 1. Hybrid Execution Architecture
SageOS supports three primary execution modes, allowing for a flexible balance between performance and safety:

- **Native**: ELF64 machine code running directly on the hardware (reserved for kernel and performance-critical drivers).
- **SGVM**: Managed bytecode execution within the Sage Virtual Machine (default for system services and applications).
- **Hybrid**: Native processes hosting their own SGVM runtime instances for internal scripting or extension.

## 2. Boot Model & Lifecycle
The system transitions through 8 granular stages, each with specific architectural guarantees:

| Stage | Name | Responsibilities |
|-------|------|------------------|
| **0** | FIRMWARE | Memory map delivery, framebuffer handoff, boot device identification. |
| **1** | EARLY_MM | Identity paging, PMM and VMM initialization. |
| **2** | IRQ_INIT | Interrupt controller setup, exception vectors, syscall entry points. |
| **3** | DISCOVERY | Bus scanning (PCI, etc.), console and timer initialization, IPC boot. |
| **4** | STORAGE_VFS | Storage drivers loaded, rootfs mounted (FAT32/BTRFS), `/etc` and `/dev` active. |
| **5** | RUNTIME | SGVM core initialized, object allocator active, IPC namespace binding. |
| **6** | SERVICE | `runtime_manager.sage` (PID 1) launched, system services bootstrap. |
| **7** | USERSPACE | Transition to interactive shell, multiuser support enabled. |

## 3. Process & Thread Model
A **Process** in SageOS is a resource container comprising:
- A private virtual address space.
- A set of execution threads.
- A capability set for resource access.
- An IPC namespace.
- A runtime context (for SGVM-enabled processes).

**Thread Classes**:
- **Kernel Thread**: Runs in supervisor mode, typically for system tasks.
- **Native User Thread**: Standard userspace thread executing machine code.
- **SGVM Managed Thread**: A thread executing bytecode, managed by the MetalVM scheduler.
- **Async Task**: A lightweight cooperative entity integrated with the event loop.

## 4. Asynchronous Runtime
SageOS treats asynchrony as a first-class citizen. The execution model is built around:
- **Futures & Promises**: For non-blocking operation tracking.
- **Event Dispatch**: Centralized kernel-to-runtime event routing.
- **Async Syscalls**: Non-blocking interfaces for I/O and IPC.

## 5. MetalVM Integration
The execution of SageLang code is integrated into the system scheduler. The `timer_poll()` primitive is injected into the MetalVM execution loop to ensure that heavy VM compute does not starve the rest of the system, maintaining fluid multitasking even during CPU-intensive script execution.
