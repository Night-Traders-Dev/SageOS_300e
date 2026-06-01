# SageOS Internal API Specifications — Subsystem Contracts

## 1. Overview
This document formalizes the stable internal APIs for SageOS core subsystems. These interfaces are considered "locked" and should only be changed with careful architectural review.

## 2. Scheduler API (`scheduler.h`)
The scheduler manages thread execution, context switching, and SMP load balancing.

- `sched_create_thread()`: Instantiate a new schedulable entity.
- `sched_yield()`: Voluntarily relinquish the CPU.
- `sched_block()` / `sched_unblock()`: Manage thread wait states.
- `sched_current_thread()`: Retrieve the active thread context.

## 3. Memory Management API (`mm.h`, `phys_alloc.h`)
SageOS uses a multi-layered memory architecture.

- `phys_alloc_frame()`: Allocate a physical memory frame (PMM).
- `vmm_map()`: Map a physical range into a virtual address space.
- `sage_malloc()` / `sage_free()`: Kernel-level heap management.

## 4. VFS API (`vfs.h`)
The Virtual Filesystem layer provides a uniform interface for all storage backends.

- `vfs_mount()`: Bind a filesystem backend to a namespace path.
- `vfs_open()` / `vfs_read()` / `vfs_write()`: Canonical file operations.
- `vfs_stat()`: Retrieve metadata about a file or directory.

## 5. IPC API (`ipc.h`)
The Inter-Process Communication backbone and capability manager.

- `ipc_send()` / `ipc_recv()`: Message passing primitives.
- `ipc_cap_narrow()`: Irreversibly attenuate rights.
- `ipc_ns_lookup()`: Resolve a named service to a capability.

## 6. Managed Execution substrate (`metal_vm.h`)
The SGVM / MetalVM interface for running portable bytecode.

- `metal_vm_load_binary()`: Load a compiled SGVM artifact.
- `metal_vm_run()` / `metal_vm_step()`: Execute bytecode.
- `metal_vm_register_native()`: Bind kernel C functions to SageLang.
- `sage_gil_acquire()` / `sage_gil_release()`: Serialize access to the AST interpreter state.

## 7. FFI & Bridge Extensions (`sageos_bridge.c`)
- `os_spawn_task()`: Launch a real scheduler thread to run a SageLang script.
- `os_vfs_stat()`: Capability-aware file metadata retrieval.

## 8. Driver Interface (`driver.h` — Prototypical)
- `driver_register()`: Announce a new device driver to the system.
- `device_event_notify()`: Push hardware events into the system event queue.
- `dma_map_buffer()`: Securely map buffers for hardware DMA.
