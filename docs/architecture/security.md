# SageOS Security Model — Capability-First Authority

## 1. Overview
SageOS follows a **Capability-First** security model. In this model, authority is not derived from a global identity (like a UID) but from the possession of unforgeable tokens called **Capabilities**.

## 2. Core Principles
- **Principle of Least Privilege**: Every task starts with zero authority and must be explicitly granted the capabilities it needs to function.
- **No Global Namespaces**: Tasks cannot access system objects (files, endpoints, devices) by global ID. They only see what is in their private capability table.
- **Authority Transfer**: Authority can be delegated by passing a capability over IPC. This delegation can be attenuated (narrowed) to provide a subset of rights.
- **Revocability**: The grantor of a capability (or the kernel) can revoke it at any time, immediately invalidating all derived authorities.

## 3. Kernel Permissions
While capabilities govern object-specific access, certain global system operations are governed by a task-level `permissions` bitmask.

| Permission | Description |
|------------|-------------|
| `SYS_REBOOT` | Ability to reboot or shut down the system. |
| `RAW_IO` | Ability to perform direct port I/O or MMIO. |
| `SCHED_CONTROL` | Ability to modify thread priorities or affinity. |
| `DRIVER_LOAD` | Ability to register new device drivers. |
| `DEBUG_TRACE` | Ability to access system telemetry and trace logs. |
| `VFS_CAP_ONLY` | Forces mandatory capability gating for all VFS operations. |

## 4. Capability-First VFS
As of **v0.7.9**, the Virtual Filesystem is integrated into the capability model.

- **Object Types**: `IPC_OBJ_FILE` and `IPC_OBJ_DIR`.
- **Rights**: `VFS_READ` and `VFS_WRITE`.
- **Enforcement**: Syscalls like `open`, `read`, and `write` verify that the task holds a capability covering the target path.
- **Root Delegation**: The system supervisor (PID 1) holds a root directory capability (`/`) and delegates sub-tree capabilities to specific services.

## 5. Managed Execution (SGVM)
The SGVM execution substrate enforces security at the instruction level:
- **Memory Safety**: Bytecode validation prevents out-of-bounds access.
- **Syscall Mediation**: All syscalls from SGVM are checked against the task's capability table and permission mask.
- **Type Safety**: The runtime ensures that capability handles are not treated as integers and vice versa.

## 5. Future Hardening
- **Address Space Isolation**: Transitioning to per-process MMU/Paging to prevent side-channel attacks.
- **Signed Artifacts**: Only executing SGVM bytecode signed by a trusted authority.
- **Measured Boot**: Using TPM/Secure Enclave to verify the integrity of the boot chain.
