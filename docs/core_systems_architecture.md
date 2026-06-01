# SageOS Core Systems Architecture Specification

Revision 0.6.3 (Formalized)

Project: "github.com/Night-Traders-Dev/SageOS"

---

1. System Philosophy

SageOS is a runtime-centric operating environment built around the SGVM execution substrate. 

Version 0.6 formalizes the core communication, security, and supervision layers, establishing a stable foundation for a truly managed operating environment.

Detailed specifications for core subsystems can be found in:
- [**IPC Subsystem**](architecture/ipc.md)
- [**Security Model**](architecture/security.md)
- [**Internal API Contracts**](architecture/internal_apis.md)
- [**Telemetry & Observability**](architecture/telemetry.md)

The operating system is not designed as a traditional POSIX-first UNIX clone. Instead, SageOS treats:

- the kernel,
- SGVM,
- SageLang,
- system services,
- and userspace

as components of a unified execution architecture.

The kernel provides:

- hardware abstraction,
- scheduling,
- memory management,
- security enforcement,
- IPC primitives,
- and runtime orchestration.

SGVM provides:

- portable execution,
- application ABI stability,
- runtime isolation,
- service interoperability,
- and future JIT/AOT execution capability.

The system is designed for:

- portability,
- self-hosting,
- runtime introspection,
- deterministic service architecture,
- and future distributed/runtime-native computing models.

---

2. Boot Model

2.1 Boot Philosophy

Boot is divided into three stages:

Firmware
    ↓
Bootstrap Loader
    ↓
Microkernel Runtime Initialization
    ↓
SGVM Runtime Bring-up
    ↓
System Service Activation
    ↓
Userspace Session

SageOS boot prioritizes:

- deterministic initialization,
- architecture abstraction,
- and runtime readiness.

The SGVM runtime is considered a first-class boot target.

---

2.2 Boot Stages

Stage 0 — Firmware

Supported:

- UEFI
- Limine
- future embedded loaders

Responsibilities:

- memory map delivery
- framebuffer handoff
- SMP metadata
- ACPI/DTB exposure
- initrd loading

---

Stage 1 — Early Kernel Initialization

Responsibilities:

- establish identity paging
- initialize early allocator
- initialize interrupt handling
- initialize serial/debug console
- initialize architecture HAL
- bring up BSP core

Subsystems initialized:

- PMM
- VMM
- scheduler core
- timer subsystem
- interrupt routing

---

Stage 2 — Runtime Bring-up

Responsibilities:

- initialize SGVM core
- initialize runtime object allocator
- initialize IPC namespace
- initialize VFS root
- initialize service registry
- initialize capability manager

The SGVM runtime becomes available before userspace launch.

---

Stage 3 — System Service Activation

Core services launched:

- VFS service
- device manager
- process manager
- runtime manager
- security manager
- shell/session service

Services may execute:

- natively
- inside SGVM
- or hybridized

---

3. Memory Model

3.1 Philosophy

SageOS uses a hybrid memory architecture:

Physical Memory Layer
    ↓
Kernel Virtual Memory
    ↓
Process Address Spaces
    ↓
SGVM Managed Regions

The model is designed to support:

- low-level native execution,
- managed runtime execution,
- zero-copy IPC,
- and future capability-secured memory regions.

---

3.2 Physical Memory Manager (PMM)

Responsibilities:

- frame allocation
- NUMA awareness (future)
- huge page support
- DMA-safe allocation
- memory pressure accounting

Allocator:

- bitmap-backed frame allocator
- optional buddy-layer optimization

---

3.3 Virtual Memory Manager (VMM)

Features:

- per-process address spaces
- lazy allocation
- copy-on-write
- shared memory regions
- demand paging
- swap integration
- memory-mapped files

Supported page sizes:

- 4K
- 2M
- 1G (where supported)

---

3.4 SGVM Memory Regions

SGVM memory is segmented into:

Code Segment
Heap Segment
Object Arena
Message Arena
Shared Runtime Region

Runtime-managed objects are:

- reference tracked
- optionally garbage collected
- capability tagged

Kernel memory is never directly writable from SGVM contexts.

---

3.5 Memory Security

Enforced:

- NX
- W^X
- ASLR
- stack guards
- kernel/userspace isolation

Future:

- memory capabilities
- pointer authentication
- tagged memory

---

4. Execution Model

4.1 Hybrid Execution Architecture

SageOS supports:

Mode| Description
Native| ELF64 machine code
SGVM| Managed bytecode execution
Hybrid| Native process hosting SGVM runtime

---

4.2 Scheduler Design

Scheduler:

- preemptive
- SMP-aware
- priority-based
- runtime-cooperative aware

Thread classes:

- kernel thread
- native user thread
- SGVM managed thread
- async task

---

4.3 Process Model

Processes contain:

- address space
- thread set
- capability set
- IPC namespace
- runtime context

Processes may host:

- native binaries
- SGVM programs
- mixed execution workloads

---

4.4 Async Runtime

SageOS standardizes async execution.

Core primitives:

- futures
- promises
- event dispatch
- message queues
- async syscalls

The SGVM scheduler integrates directly with kernel event queues.

---

5. IPC Model

5.1 IPC Philosophy

IPC is message-oriented.

SageOS avoids heavy dependence on:

- UNIX pipes
- signal-driven control flow
- global shared state

Core IPC is capability-routed message passing.

---

5.2 IPC Primitives

Supported IPC:

- channels
- ports
- shared memory
- async event streams
- service endpoints

All IPC objects are kernel-managed capabilities.

---

5.3 Service Architecture

System services are discoverable through:

/service/<name>

Examples:

- /service/vfs
- /service/net
- /service/audio
- /service/runtime

IPC transport is abstracted from service identity.

---

5.4 Zero-Copy Messaging

Large payloads use:

- shared region handles
- reference-counted transfer pages
- immutable message buffers

---

6. Security Model

6.1 Security Philosophy

Security is capability-first.

Processes receive explicit authority instead of implicit global access.

---

6.2 Capability System

Capabilities govern:

- files
- devices
- services
- memory regions
- IPC endpoints
- runtime permissions

Capabilities are:

- transferable
- revocable
- namespace-scoped

---

6.3 Isolation Domains

Isolation layers:

- kernel
- service
- process
- SGVM runtime
- thread sandbox

Future:

- microVM execution
- hardware-enforced compartments

---

6.4 Runtime Security

SGVM execution enforces:

- bytecode validation
- runtime permission checks
- object boundary enforcement
- syscall mediation

---

6.5 Secure Boot Path

Future support:

- signed kernel images
- signed SGVM packages
- measured boot
- runtime integrity validation

---

7. Runtime Lifecycle

7.1 Runtime Philosophy

SGVM is a system runtime, not merely an application VM.

The runtime participates directly in:

- scheduling
- IPC
- service activation
- resource accounting
- and userspace orchestration.

---

7.2 Runtime Initialization

Runtime startup stages:

SGVM Core Init
    ↓
Object System Init
    ↓
IPC Binding
    ↓
Service Registry Bind
    ↓
Userspace Runtime Activation

---

7.3 Runtime Services

Core runtime services:

- object allocator
- async executor
- bytecode verifier
- module loader
- profiler
- tracing hooks

---

7.4 Runtime States

Runtime states:

- initializing
- active
- suspended
- degraded
- terminating

Kernel may restart runtime services independently.

---

8. Driver Model

8.1 Driver Philosophy

Drivers are modular services.

The kernel contains only:

- essential hardware interfaces
- scheduler integration
- interrupt routing
- DMA mediation

Everything else is externalizable.

---

8.2 Driver Classes

Supported driver types:

- kernel-native
- userspace driver
- SGVM-managed driver
- hybrid driver

---

8.3 Device Abstraction

Devices exposed as:

/device/<class>/<instance>

Examples:

- /device/block/nvme0
- /device/net/eth0
- /device/input/kbd0

---

8.4 Driver IPC

Drivers communicate through:

- event queues
- DMA channels
- capability handles
- async requests

---

8.5 Driver Safety

Future driver protections:

- isolated driver address spaces
- fault containment
- hot-reloadable drivers
- runtime crash recovery

---

9. Userspace Model

9.1 Userspace Philosophy

Userspace is service-oriented and runtime-aware.

Traditional UNIX semantics are supported where practical but are not the architectural center.

---

9.2 Application Types

Supported:

- native ELF64 applications
- SGVM bytecode applications
- hybrid applications
- runtime services

---

9.3 Filesystem Layout

Proposed layout:

/system
/runtime
/apps
/services
/users
/device
/tmp

---

9.4 Shell Architecture

The Sage shell is runtime-native.

Shell capabilities:

- async pipelines
- object-aware scripting
- IPC-native commands
- SGVM execution
- structured data streams

---

9.5 Package Model

Packages may contain:

- native binaries
- SGVM modules
- runtime libraries
- service manifests
- capability manifests

---

10. SGVM Lifecycle

10.1 SGVM Philosophy

SGVM is the portable execution substrate of SageOS.

It functions as:

- runtime ABI
- application execution layer
- service execution layer
- future distributed execution substrate

---

10.2 SGVM Pipeline

SageLang Source
    ↓
Compiler Frontend
    ↓
SGIR
    ↓
SGVM Bytecode
    ↓
Verifier
    ↓
Runtime Execution

---

10.3 Bytecode Verification

Verification checks:

- control flow validity
- type safety
- memory region access
- capability access
- syscall permissions

Invalid bytecode cannot execute.

---

10.4 Execution Modes

SGVM supports:

- interpreted execution
- threaded interpreter
- JIT compilation (future)
- AOT caching (future)

---

10.5 SGVM Integration

SGVM integrates with:

- scheduler
- VFS
- IPC
- tracing
- profiler
- security manager

SGVM tasks are first-class schedulable entities.

---

10.6 Distributed Future

Long-term SGVM goals:

- distributed actor execution
- remote runtime messaging
- network-transparent services
- runtime migration
- clustered SGVM execution

---

11. Long-Term Direction

SageOS is designed to evolve toward:

- self-hosting
- runtime-native services
- distributed execution
- capability-secured computing
- portable managed systems architecture

The project intentionally prioritizes:

- architectural coherence,
- runtime integration,
- and execution portability

over strict UNIX compatibility.
