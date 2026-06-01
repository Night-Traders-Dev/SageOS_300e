# SageOS Telemetry & Observability Subsystem

## 1. Overview
The Telemetry subsystem provides deep, real-time observability into the SageOS kernel and SGVM runtime. It is designed for minimal overhead, utilizing a high-performance circular buffer to capture system-wide events across multiple CPUs.

## 2. Event Model
Each trace entry captures a high-resolution timestamp, the event type, the executing CPU, the active task ID, and two 64-bit arguments for event-specific data.

| Event | Arg 1 | Arg 2 |
|-------|-------|-------|
| `TRACE_SCHED_SWITCH` | Previous Task ID | Next Task ID |
| `TRACE_IPC_SEND` | Capability Handle | Flags |
| `TRACE_IPC_RECV` | Capability Handle | Flags |
| `TRACE_VFS_READ` | File Offset | Bytes Requested |
| `TRACE_VFS_WRITE` | File Offset | Bytes Written |
| `TRACE_VM_EXEC` | Opcode | Instruction Pointer |
| `TRACE_ALLOC_MALLOC` | Requested Size | Allocated Pointer |
| `TRACE_ALLOC_FREE` | Freed Pointer | (Unused) |
| `TRACE_SYSCALL_ENTER` | Syscall Number | First Argument |

## 3. Storage Architecture
The telemetry buffer is stored in a fixed-size kernel-resident array (`TELEMETRY_BUFFER_SIZE = 1024` entries). Access is governed by a lightweight, architecture-optimized spinlock to ensure integrity in SMP environments.

## 4. Introspection Tools
System events can be visualized using the kernel's built-in diagnostic tools:
- **`trace_dump()`**: Serial-out dump of the entire trace buffer in a human-readable format.
- **`dmesg` Integration**: High-level events are occasionally mirrored to the kernel log for simplified debugging.
- **Trace Shell Command**: Fully implemented in the C kernel. Access via `trace` with subcommands:
  - `trace dump` — full trace buffer dump
  - `trace dump <event>` — filtered dump (e.g., `trace dump IPC_SEND`)
  - `trace stats` — event count summary
  - `trace clear` — reset trace buffer

## 5. Usage in Development
Telemetry is mandatory for all new core logic. Developers should inject `trace_log()` calls at critical state transition points to maintain the system's "observable-by-default" status.
