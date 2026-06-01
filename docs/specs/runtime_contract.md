# Runtime Contract Specification

## 1. Introduction
The Runtime Contract defines the formal relationship and stable interface between the SageOS kernel and the high-level language runtime (SGVM/SageLang).

## 2. The Runtime Manager (PID 1)
The `runtime_manager.sage` script is the first managed entity launched by the kernel (Stage 6).
- **Responsibilities**: Service discovery, dependency management, service health monitoring, and system-wide asynchronous orchestration.
- **Privileges**: Runs with elevated capabilities to manage other tasks and IPC namespaces.

## 3. ABI Versioning & Handshake
To ensure compatibility, the kernel and runtime perform a versioning handshake during initialization:
- **SAGE_ABI_MAJOR**: Incremented for breaking changes to syscalls, IPC protocols, or bytecode formats.
- **SAGE_ABI_MINOR**: Incremented for backwards-compatible additions to the runtime library or kernel APIs.
Mismatch in `SAGE_ABI_MAJOR` MUST prevent system bring-up.

## 4. Execution Guarantees & Failure Semantics
The runtime MUST adhere to strict execution rules to maintain system stability:
- **Path Resolution**: If a requested script file is not found, the runtime MUST abort with `ENOENT`. It MUST NOT fallback to interpreting the file path as source code.
- **Integrity**: Failed imports or syntax errors in critical system scripts MUST be reported to the `runtime_manager` for self-healing or degraded-mode transition.
- **Self-Healing**: Services managed by the `runtime_manager` should define restart policies for automatic recovery from runtime crashes.

## 5. Cooperative Multitasking Requirement
Managed code execution is subject to kernel scheduling. 
- **Timer Polling**: All MetalVM execution loops MUST include `timer_poll()` calls. This ensures that the kernel can preempt the VM and perform context switches, even during long-running or computationally expensive script segments.

## 6. Resource Ownership
Objects created within the SGVM are owned by the respective process's runtime context.
- **Memory**: Managed by the Mark-and-Sweep GC.
- **Handles**: Files and IPC endpoints are managed via capabilities; the runtime must ensure that handles are closed or transferred correctly upon task termination.
