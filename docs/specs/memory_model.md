# Memory Model Specification

## 1. Layered Architecture
SageOS employs a multi-layered memory architecture to support both low-level native execution and high-level managed objects:

1. **Physical Memory Layer**: Managed by the PMM.
2. **Kernel Virtual Memory**: Privileged address space for the kernel core.
3. **Process Address Spaces**: Isolated virtual regions for each task.
4. **SGVM Managed Regions**: Sub-segments within process space managed by the runtime.

## 2. Physical Memory Manager (PMM)
The PMM is responsible for tracking and allocating raw hardware memory frames.
- **Allocator**: Uses a bitmap-backed frame allocator for deterministic performance.
- **Capabilities**: Huge page support (2M, 1G) and DMA-safe allocation regions.
- **API**: `phys_alloc_frame()` for obtaining new frames.

## 3. Virtual Memory Manager (VMM)
The VMM handles mapping physical frames into virtual address spaces.
- **Features**: Lazy allocation, Copy-on-Write (CoW), demand paging, and memory-mapped files.
- **Portability**: Assumes a **Weakly Ordered** memory model; atomic operations require explicit memory barriers (`smp_mb`).
- **Isolation**: Enforces strict kernel/userspace separation.

## 4. SGVM Memory Regions
SGVM memory is organized into functional segments within the process's address space:
- **Code Segment**: Immutable region for bytecode instructions.
- **Heap Segment**: Dynamic allocation for runtime structures.
- **Object Arena**: Dedicated space for reference-tracked objects (Arrays, Dictionaries).
- **Message Arena**: Temporary buffers for IPC message assembly.
- **Shared Runtime Region**: Read-only global state and shared modules.

## 5. Garbage Collection
SageOS implements a **Mark-and-Sweep Garbage Collector** within the SGVM.
- **Trigger**: Automatic invocation when object pool limits are reached.
- **Operation**: Traces roots from the active stack and environment variables to reclaim unused object slots.
- **Scope**: Primarily manages dynamic SageLang types (Arrays/Dicts) to prevent memory leaks in long-running services.

## 6. Security & Protection
The memory model enforces several modern security primitives:
- **NX (No-Execute)**: Data regions cannot be executed as code.
- **W^X (Write XOR Execute)**: Memory cannot be simultaneously writable and executable.
- **ASLR**: Randomization of stack and heap locations to mitigate exploitation.
- **Stack Guards**: Canary-based detection of stack overflows.
