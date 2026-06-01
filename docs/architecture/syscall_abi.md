# SageOS Syscall ABI Reference

## 1. Overview
SageOS implements a hybrid syscall ABI that combines Linux-compatible POSIX syscalls with SageOS-specific IPC extensions. Syscall numbers follow Linux x86_64/AArch64/RISC-V 64 conventions where possible.

## 2. ABI Versioning
All syscall interfaces are versioned via `version.h`:
- `SAGE_ABI_MAJOR` — Incremented on breaking changes to syscalls or IPC.
- `SAGE_ABI_MINOR` — Incremented on backwards-compatible additions.

Current: **ABI v0.4**

## 3. Calling Convention

### x86_64
Syscalls are invoked via `syscall`. Arguments in: `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9`. Syscall number in `rax`. Return value in `rax`.

### AArch64
Syscalls are invoked via `svc #0`. Arguments in: `x0`–`x5`. Syscall number in `x8`. Return value in `x0`.

### RISC-V 64
Syscalls are invoked via `ecall`. Arguments in: `a0`–`a5`. Syscall number in `a7`. Return value in `a0`.

## 4. POSIX-Compatible Syscalls

All VFS-related syscalls are **capability-gated** if the task has the `PERM_VFS_CAP_ONLY` bit set.

| Number | Name | Signature | Notes |
|--------|------|-----------|-------|
| 0 | `read` | `read(fd, buf, count)` | Returns bytes read. Gated by `VFS_READ`. |
| 1 | `write` | `write(fd, buf, count)` | Gated by `VFS_WRITE`. fd 1/2 go to console. |
| 2 | `open` | `open(path, flags, mode)` | Gated by `VFS_READ`/`VFS_WRITE`. |
| 3 | `close` | `close(fd)` | Invalidates FD entry. |
| 5 | `fstat` | `fstat(fd, stat*)` | Returns `st_size` and `st_mode`. |
| 8 | `lseek` | `lseek(fd, offset, whence)` | `SEEK_SET=0`, `SEEK_CUR=1`, `SEEK_END=2`. |
| 12 | `brk` | `brk(addr)` | Adjusts process heap limit. |
| 33 | `dup2` | `dup2(oldfd, newfd)` | Duplicates file descriptor. |
| 35 | `nanosleep` | `nanosleep(req, rem)` | Blocks task via scheduler. |
| 39 | `getpid` | `getpid()` | Returns current task ID. |
| 58 | `vfork` | `vfork()` | Clones kernel stack, parent blocks until child exits/execs. |
| 59 | `execve` | `execve(path, argv, envp)` | Loads and executes ELF64 binary. |
| 60 | `exit` | `exit(code)` | Terminates current task. |
| 61 | `waitpid` | `waitpid(pid, status, options)` | Blocks until child terminates. |
| 62 | `kill` | `kill(pid, sig)` | **Stub**: Returns `-EINVAL`. |
| 79 | `getcwd` | `getcwd(buf, size)` | Returns current working directory string. |
| 80 | `chdir` | `chdir(path)` | Validates path is a directory via VFS. |
| 83 | `mkdir` | `mkdir(path, mode)` | Gated by `VFS_WRITE`. |
| 87 | `unlink` | `unlink(path)` | Gated by `VFS_WRITE`. |
| 96 | `gettimeofday` | `gettimeofday(tv, tz)` | Timezone ignored. Uses `timer_seconds()`. |
| 100 | `isatty` | `isatty(fd)` | Returns 1 for fd 0–2, 0 otherwise. **SageOS custom number.** |
| 101 | `times` | `times(tms*)` | Returns timer ticks. Only `tms_utime` populated. |
| 217 | `getdents64` | `getdents64(fd, dirp, count)` | Gated by `VFS_READ`. |

## 5. System Control Syscalls

| Number | Name | Signature | Permission Required |
|--------|------|-----------|-------------------|
| 169 | `reboot` | `reboot()` | `PERM_SYS_REBOOT` |
| 170 | `shutdown` | `shutdown()` | `PERM_SYS_REBOOT` |

> [!IMPORTANT]
> `reboot` and `shutdown` are **capability-gated**. Tasks without `PERM_SYS_REBOOT` receive `-EACCES` and a security audit log entry via `dmesg`.

## 6. IPC Syscalls (200–244)

IPC syscalls are dispatched through `ipc_syscall_dispatch()`. See [IPC Specification](ipc.md) for full semantics.

### Object Creation

| Number | Name | Description |
|--------|------|-------------|
| 200 | `ipc_endpoint_create` | Create unidirectional message pipe |
| 201 | `ipc_channel_create` | Create bidirectional message stream |
| 202 | `ipc_port_create` | Create connection-oriented rendezvous |
| 203 | `ipc_shm_create` | Create shared memory region |

### Messaging

| Number | Name | Description |
|--------|------|-------------|
| 204 | `ipc_send` | Send message to capability |
| 205 | `ipc_recv` | Receive message from capability |
| 206 | `ipc_call` | Synchronous RPC (send + wait for reply) |

### Connection Management

| Number | Name | Description |
|--------|------|-------------|
| 207 | `ipc_port_listen` | Begin accepting connections on port |
| 208 | `ipc_port_accept` | Accept incoming connection |
| 209 | `ipc_port_connect` | Connect to a listening port |

### Shared Memory

| Number | Name | Description |
|--------|------|-------------|
| 210 | `ipc_shm_map` | Map shared memory into address space |
| 211 | `ipc_shm_unmap` | Unmap shared memory |
| 212 | `ipc_shm_grant` | Grant shared memory access to another task |

### Namespace

| Number | Name | Description |
|--------|------|-------------|
| 213 | `ipc_ns_register` | Register a named service |
| 214 | `ipc_ns_lookup` | Resolve a service name to capability |
| 215 | `ipc_ns_unbind` | Remove a service registration |

### Lifecycle

| Number | Name | Description |
|--------|------|-------------|
| 216 | `ipc_object_destroy` | Destroy an IPC object |
| 217 | `ipc_object_pause` | Pause an IPC object (no new ops) |
| 218 | `ipc_object_resume` | Resume a paused object |
| 219 | `ipc_object_drain` | Drain queued messages, then destroy |
| 220 | `ipc_object_info` | Query object metadata |
| 221 | `ipc_object_stats` | Query object statistics |

### Capability Management

| Number | Name | Description |
|--------|------|-------------|
| 222 | `ipc_cap_insert` | Insert capability into task's table |
| 223 | `ipc_cap_narrow` | Irreversibly attenuate capability rights |
| 224 | `ipc_cap_revoke` | Revoke a capability (cascading) |
| 225 | `ipc_cap_dup` | Duplicate a capability |

## 7. Error Codes

All syscalls return negative values on error, using VFS error codes:

| Code | Name | Meaning |
|------|------|---------|
| -1 | `EINVAL` | Invalid argument |
| -2 | `ENOENT` | File or object not found |
| -3 | `EIO` | I/O error |
| -4 | `ENOSPC` | No space left (FDs, memory, etc.) |
| -5 | `EEXIST` | Object already exists |
| -6 | `ENOTDIR` | Not a directory |
| -13 | `EACCES` | Permission denied (capability check failed) |

## 8. Telemetry Integration

All syscall entries are traced via:
```c
trace_log(TRACE_SYSCALL_ENTER, (uint64_t)num, (uint64_t)a1);
```

Non-write syscalls also produce console debug output with syscall number, first argument, and return value.
