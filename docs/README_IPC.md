# SageOS IPC Subsystem

## Overview

This directory contains the formalized IPC (Inter-Process Communication)
subsystem for SageOS, implementing four core pillars:

1. **Canonical Primitives** — typed messages, endpoints, channels, ports, shared memory
2. **Lifecycle Semantics** — state machine (Constructed → Active → Paused/Draining → Dead)
3. **Service Addressing** — hierarchical namespace (`vfs.root`, `net.tcp`, etc.)
4. **Capability Routing** — non-forgeable, attenuatable, revocable rights tokens

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                        User Space                                    │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐               │
│  │ C Program   │  │ SageLang    │  │ ELF Binary  │               │
│  │ (ipc_user.h)│  │ (MetalVM)   │  │ (libc shim) │               │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘               │
│         │                │                │                       │
│  ┌──────┴────────────────┴────────────────┴──────┐               │
│  │         Syscall Interface (200-244)            │               │
│  └──────────────────┬───────────────────────────┘               │
└───────────────────────┼───────────────────────────────────────────┘
                        │
┌───────────────────────┼───────────────────────────────────────────┐
│                       ▼                                             │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │              IPC Subsystem (kernel/ipc.c)                    │   │
│  │  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐        │   │
│  │  │Endpoint │  │ Channel │  │  Port   │  │  SHM    │        │   │
│  │  │  Pool   │  │  Pool   │  │  Pool   │  │  Pool   │        │   │
│  │  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘        │   │
│  │       └─────────────┴─────────────┴─────────────┘              │   │
│  │  ┌─────────────────────────────────────────────────────────┐ │   │
│  │  │        Capability Routing & Namespace Trie               │ │   │
│  │  └─────────────────────────────────────────────────────────┘ │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                         │                                         │
│  ┌──────────────────────┴─────────────────────────────────────┐   │
│  │              Scheduler Extension (scheduler_ipc_ext.c)      │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐       │   │
│  │  │ Per-Task    │  │ IPC Wait    │  │ Signal      │       │   │
│  │  │ Cap Table   │  │ Queues      │  │ Sets        │       │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘       │   │
│  └─────────────────────────────────────────────────────────┘   │
└───────────────────────────────────────────────────────────────────┘
```

## File Guide

| File | Purpose | Where to Place |
|------|---------|----------------|
| `ipc.h` | Kernel types, constants, syscall numbers | `sageos_build/kernel/include/` |
| `ipc.c` | Kernel implementation | `sageos_build/kernel/core/` |
| `scheduler_ipc_ext.h` | Scheduler extension header | `sageos_build/kernel/include/` |
| `scheduler_ipc_ext.c` | Scheduler extension impl | `sageos_build/kernel/core/` |
| `syscall_ipc_patch.c` | Patch for `core/syscall.c` | Apply manually to existing file |
| `ipc_user.h` | User-space API header | `sageos_build/kernel/include/` (or libc) |
| `ipc_user.c` | User-space library | `sageos_build/kernel/core/` (or libc) |
| `ipc_sagelang_binding.c` | MetalVM native functions | `sageos_build/kernel/core/sagelang/` |
| `ipc_spec.md` | Design specification | `docs/` |
| `Makefile.ipc` | Build integration | Merge into existing Makefile |
| `ipc_test.c` | Example/test program | `examples/` or `tests/` |

## Integration Steps

### 1. Copy headers and sources

```bash
cp ipc.h scheduler_ipc_ext.h ipc_user.h sageos_build/kernel/include/
cp ipc.c scheduler_ipc_ext.c ipc_sagelang_binding.c sageos_build/kernel/core/
cp ipc_user.c sageos_build/kernel/core/   # or into libc
```

### 2. Patch syscall dispatch

Open `sageos_build/kernel/core/syscall.c` and:

1. Add `#include "ipc.h"` near the top.
2. Add `extern long ipc_syscall_dispatch(...);` declaration.
3. Insert the IPC cases from `syscall_ipc_patch.c` into the `switch` in
   `syscall_dispatch()`, before the `default:` case.

### 3. Extend scheduler lifecycle

In `sageos_build/kernel/core/scheduler.c`:

1. Add `#include "scheduler_ipc_ext.h"`.
2. Call `sched_ipc_init_thread(t)` at the end of `sched_create_thread()`.
3. Call `sched_ipc_cleanup_thread(t)` at the start of `sched_destroy_thread()`.
4. Call `ipc_subsystem_init()` from the kernel boot sequence (e.g.,
   `virt_main.c` after `sched_init()`).

### 4. Update syscall numbers header

Merge the `SYS_ipc_*` defines from `ipc.h` into `syscall_numbers.h`.

### 5. Build and test

```bash
./sageos.sh x64 virt build
./sageos.sh x64 virt run
# Inside the VM, compile and run the test:
#   gcc /examples/ipc_test.c -lipc -o /bin/ipc_test
#   ipc_test
```

## Key Design Decisions

### Capability Model

- **No global object IDs**: tasks access IPC only through private capability
  handles.  This prevents information leaks and enables fine-grained revocation.
- **Rights attenuation**: `ipc_cap_narrow()` removes rights irreversibly,
  allowing a task to create a "weaker" capability to hand to an untrusted peer.
- **Transfer vs. Copy**: the `CAP_TRANSFER` flag lets a task move (not copy)
  a capability, useful for passing ownership of a resource.

### Lifecycle State Machine

- **PAUSED** lets a service temporarily stop accepting new work without
  dropping in-flight messages.
- **DRAINING** is used for graceful shutdown: finish queued work, then die.
- **DEAD** objects are immediately reclaimable; any subsequent cap lookup fails.

### Service Namespace

- Hierarchical names (`vfs.root`, `net.tcp.listen`) map to a kernel trie.
- Lookups return **derived** capabilities with `REVOKE` and `DUP` stripped,
  preventing privilege escalation.
- Well-known services are pre-registered at boot by the init task.

### Memory Safety

- Message payloads are bounded (4096 bytes max).
- Capability attachments per message are bounded (8 max).
- All ring buffers are pre-allocated; no dynamic allocation on the hot path.
- Spinlocks protect concurrent access to endpoint queues and capability tables.

## Security Properties

| Property | Mechanism |
|----------|-----------|
| Non-forgeability | Cap handles are kernel-managed table indices |
| Non-escalation | Derived caps strip `REVOKE` and `DUP` |
| Revocation | `REVOKE` right → object transitions to DEAD |
| Isolation | Tasks have private cap tables; no shared state |
| Auditability | `sender_task` and `sender_cap` in every message header |

## Performance Notes

- **Send/recv hot path**: ~20 instructions (spinlock acquire, ring buffer
  index update, memcpy, spinlock release).
- **No kernel heap allocation** on send/recv — ring buffers are pre-allocated.
- **Capability lookup**: O(1) array index.
- **Namespace lookup**: O(depth) trie walk.
- **State transitions**: atomic CAS — no locks required.

## Future Extensions

1. **Async multiplexer** (`ipc_poll()`) for waiting on multiple handles.
2. **Cross-CPU wakeups** via IPI instead of spin-polling.
3. **Zero-copy large messages** using shared-memory page flipping.
4. **Capability garbage collection** to reclaim dead slots.
5. **Audit log** for security-critical cap transfers.

## License

MIT — same as SageOS.
