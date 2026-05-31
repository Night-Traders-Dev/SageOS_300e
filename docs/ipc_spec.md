# SageOS IPC Subsystem — Formal Specification v0.1.0

## 1. Overview

This document formalizes the Inter-Process Communication (IPC) subsystem for
SageOS.  It covers four pillars:

1. **Canonical Primitives** — the minimal, unambiguous operations that all IPC
   is built from.
2. **Lifecycle Semantics** — how IPC objects are created, transitioned, and
   destroyed.
3. **Service Addressing** — how tasks discover and bind to named services.
4. **Capability Routing** — how rights are represented, transferred, and
   attenuated.

## 2. Canonical Primitives

### 2.1 Object Types

| Type | Description | Directionality |
|------|-------------|----------------|
| `ENDPOINT` | Unidirectional message pipe | One-way |
| `CHANNEL` | Bidirectional message stream | Two-way |
| `PORT` | Rendezvous / acceptor | Connection-oriented |
| `SHARED_MEM` | Memory-mapped buffer | Zero-copy |
| `SIGNAL_SET` | Event broadcasting | Many-to-many |
| `MONITOR` | Condition + mutex hybrid | Synchronization |

### 2.2 Message Format

All messages share a fixed header followed by a bounded payload:

```
+--------+--------+--------+--------+--------+--------+--------+--------+
| magic  | type   | sender | cap-hint | flags | len    | cap-cnt| pad    |
| 32-bit | 32-bit | 32-bit | 32-bit | 32-bit| 32-bit | 32-bit | 32-bit |
+--------+--------+--------+--------+--------+--------+--------+--------+
| payload[0 .. IPC_MSG_MAX_BYTES-1]                                    |
+-----------------------------------------------------------------------+
| caps[0 .. IPC_MSG_MAX_CAPS-1] (32-bit handles)                       |
+-----------------------------------------------------------------------+
```

* `magic` = `0x49504321` ("IPC!")
* `type` = user-defined discriminant
* `sender` = kernel-populated task ID
* `cap-hint` = reply-capability handle (for RPC routing)
* `flags` = `NOBLOCK`, `KERNEL_ORIGIN`, `CAP_TRANSFER`
* `len` ≤ 4096 bytes
* `cap-cnt` ≤ 8

### 2.3 Core Operations

| Primitive | Input | Output | Blocking? |
|-----------|-------|--------|-----------|
| `ipc_send` | cap, msg | status | Optional |
| `ipc_recv` | cap, buf | msg | Optional |
| `ipc_call` | cap, req, resp | status | Yes |
| `ipc_create` | type, params | cap-pair | No |
| `ipc_destroy` | cap | status | No |
| `ipc_map` | shm-cap | vaddr | No |
| `ipc_grant` | cap, target, rights | derived-cap | No |

## 3. Lifecycle Semantics

### 3.1 State Machine

```
CONSTRUCTED ──→ ACTIVE ──→ PAUSED ──→ DEAD
                  │           │
                  └─→ DRAINING ─┘
```

* **CONSTRUCTED** — object exists in kernel memory but accepts no operations.
* **ACTIVE** — full send/recv/map operations permitted.
* **PAUSED** — no new operations accepted; in-flight operations complete.
* **DRAINING** — finish queued messages, reject new ones; auto-transition to DEAD
  when queue empty.
* **DEAD** — object memory may be reclaimed; all caps become invalid.

### 3.2 Transition Rules

| From → To | Required Right | Precondition |
|-----------|----------------|--------------|
| CONSTRUCTED → ACTIVE | `REVOKE` | Object fully initialized |
| ACTIVE → PAUSED | `REVOKE` | No sender holds `IMMUTABLE` flag |
| PAUSED → ACTIVE | `REVOKE` | — |
| ACTIVE → DRAINING | `REVOKE` | — |
| DRAINING → DEAD | (implicit) | `msg_count == 0` |
| PAUSED → DEAD | `REVOKE` | — |

### 3.3 Reference Counting

Each capability holds a `ref_count`.  When it reaches zero:

1. If `KERNEL_OWNED` flag is set, the kernel retains a ghost reference.
2. Otherwise, the object transitions to DEAD and its memory is reclaimed.

## 4. Service Addressing

### 4.1 Namespace Hierarchy

Services are registered under dot-separated names:

```
vfs.root
net.tcp
net.udp
dev.manager
sched.control
log.sink
```

The namespace is a kernel-managed trie.  Each node stores:

* `name` — path segment
* `cap_handle` — capability index in the registrant's table
* `owner_task_id` — the task that registered this name

### 4.2 Lookup Semantics

`ipc_ns_lookup(name)` performs a trie walk and returns a **derived capability**
into the caller's table.  The derived capability has:

* `REVOKE` and `DUP` rights **stripped**
* `ref_count` incremented on the underlying object
* `owner_task` set to the caller

This prevents callers from revoking or duplicating a service they do not own.

### 4.3 Well-Known Services (Boot-Time)

The kernel pre-registers these names during `ipc_subsystem_init()`:

| Name | Object Type | Initial Owner |
|------|-------------|---------------|
| `vfs.root` | PORT | init task |
| `net.stack` | PORT | network daemon |
| `dev.manager` | PORT | device manager |
| `sched.control` | CHANNEL | scheduler |
| `log.sink` | ENDPOINT | logger task |

## 5. Capability Routing

### 5.1 Capability Structure

```c
typedef struct {
    uint32_t rights;        // bitmask
    uint32_t object_type;   // IPC_OBJ_*
    uint32_t object_id;     // index into global pool
    uint32_t ref_count;
    uint32_t owner_task;
    uint32_t flags;
} ipc_capability_t;
```

### 5.2 Rights

| Right | Bit | Meaning |
|-------|-----|---------|
| `SEND` | 0 | Transmit messages |
| `RECV` | 1 | Receive messages |
| `CALL` | 2 | Synchronous RPC |
| `MAP` | 3 | Map shared memory |
| `DUP` | 4 | Duplicate this cap |
| `REVOKE` | 5 | Destroy / transition object |

### 5.3 Attenuation (Narrowing)

`ipc_cap_narrow(handle, new_rights)` removes rights from a capability.
It is **irreversible** (unless `IMMUTABLE` flag is set, in which case it fails).

Example:
```c
ipc_handle_t h = ...;           // has SEND | RECV | DUP | REVOKE
ipc_cap_narrow(h, IPC_RIGHT_SEND);   // now only SEND
// DUP and REVOKE are gone — this cap cannot be further propagated
```

### 5.4 Transfer vs. Copy

When `IPC_MSGF_CAP_TRANSFER` is set in a message:

1. Source capability is **invalidated** in the sender's table.
2. Receiver receives a new capability with the **same rights**.
3. `ref_count` on the underlying object is unchanged (moved, not duplicated).

When the flag is **not** set:

1. Source capability remains valid.
2. Receiver receives a **derived** capability with `REVOKE` and `DUP` stripped.
3. `ref_count` is incremented.

## 6. Integration with Existing SageOS Subsystems

### 6.1 Scheduler

The scheduler's `thread_t` is extended via a parallel array
`g_thread_ipc_ext[]` containing:

* `cap_table` — 64 capability slots
* `ipc_wait_next/prev` — wait-queue linkage when blocked on IPC
* `rpc_reply_cap` — in-flight RPC tracking
* `signal_pending/mask` — async signal delivery

`sched_ipc_init_thread()` is called from `sched_create_thread()`.
`sched_ipc_cleanup_thread()` is called from `sched_destroy_thread()`.

### 6.2 Syscall Layer

IPC syscalls occupy numbers **200–244** (after the existing POSIX-compatible
set).  The `syscall_dispatch()` switch gains a new block that delegates to
`ipc_syscall_dispatch()`.

### 6.3 VFS Bridge

The VFS can be exposed as a service (`vfs.root`) listening on a PORT.
File descriptors are translated into IPC messages:

```
VFS_READ  → msg.type = 0x10, payload = { fd, offset, len }
VFS_WRITE → msg.type = 0x11, payload = { fd, offset, data... }
```

This allows user-space file systems and network-backed storage.

### 6.4 SageLang / MetalVM

IPC primitives are registered as MetalVM native functions:

```sage
let (local, peer) = ipc:channel-create()
ipc:send(local, 0x01, "hello", 5, 0)
let (result, len) = ipc:recv(local, buf, 4096, 0)
```

## 7. Security Model

### 7.1 No Global Namespaces

Tasks cannot access IPC objects by raw pointer or global ID.  All access
is mediated by capabilities held in the task's private table.

### 7.2 Non-Forgeable Tokens

Capability handles are indices into a kernel-managed table.  User space cannot
construct valid handles without kernel cooperation.

### 7.3 Revocation Cascade

Revoking a capability immediately invalidates all derived capabilities
(because the underlying object transitions to DEAD).  This is O(1) — no
iteration over derived caps is required.

### 7.4 Resource Limits

* Max 64 capabilities per task
* Max 128 endpoints system-wide
* Max 32 ports system-wide
* Max 32 shared-memory regions system-wide
* Max 256 namespace entries system-wide

## 8. Future Work

1. **Async I/O** — integrate with `epoll`/`kqueue`-style multiplexer.
2. **Cross-CPU Message Passing** — use IPIs for SMP wakeups instead of
   spin-polling.
3. **Zero-Copy Shared Memory** — page-flipping for large payloads.
4. **Capability Compaction** — garbage-collect dead slots to reduce
   table fragmentation.
5. **Audit Logging** — log all capability transfers for security analysis.

## 9. File Inventory

| File | Role | Lines (approx) |
|------|------|----------------|
| `ipc.h` | Kernel header — all types and syscall prototypes | 380 |
| `ipc.c` | Kernel implementation — dispatch, queues, lifecycle | 520 |
| `scheduler_ipc_ext.h` | Scheduler extension header | 90 |
| `scheduler_ipc_ext.c` | Scheduler extension implementation | 120 |
| `syscall_ipc_patch.c` | Syscall dispatch integration | 80 |
| `ipc_user.h` | User-space C library header | 220 |
| `ipc_user.c` | User-space C library implementation | 200 |
| `ipc_sagelang_binding.c` | SageLang/MetalVM FFI bridge | 230 |
| `ipc_spec.md` | This document | 350 |
