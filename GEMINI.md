# SageOS Development Guidelines

- **Platform Compliance**: All new subsystems and architectural changes MUST comply with the [Platform Specification](docs/architecture/platform_spec.md), adhering to the defined boot stages and execution contracts.
- **Sage-First Principle**: Wherever possible, new code, drivers, and system services MUST be implemented in `SageLang` (`.sage` files). C should be reserved strictly for low-level kernel primitives, memory management shims, and performance-critical hardware drivers that cannot yet be expressed in `SageLang`.
- **Architectural Integrity**: All new drivers and system components should strive to use the `virt` kernel architecture. If a feature requires platform-specific functionality, use the defined `MetalVM` native interfaces rather than implementing new, complex C-based subsystems.
- **Security & Isolation**: SageOS follows a **Capability-First** security model. All new interfaces MUST use capabilities for resource access. Tasks are limited by explicit permissions; sensitive operations (reboot, raw IO, VFS access) must be gated.
- **IPC-Centric Design**: Communication between subsystems MUST use the formalized IPC subsystem with strict lifecycle and capability semantics. Avoid global shared state. VFS operations are gated by `IPC_OBJ_FILE` and `IPC_OBJ_DIR` capabilities.
- **Runtime Supervision**: All system services must be registered with and managed by the `runtime_manager` (PID 1), which runs as an asynchronous background kernel task. Services must define their dependencies and support self-healing/restart semantics. Use `os_spawn_task` to launch real scheduler threads.
- **Instrumentation**: New core logic MUST include tracepoints using the `telemetry` subsystem to maintain system observability.
- **Preemption**: MetalVM execution loops MUST include `timer_poll()` calls to ensure cooperative multitasking and clock progression.
- **Rootfs Build System**: All system files, SGVM bytecode, and scripts MUST be organized within the `rootfs/` directory before being merged into the virtual disk image. Use `scripts/populate_rootfs.sh` to update the rootfs and `scripts/merge_rootfs.sh` to sync it with `virt.img`.
- **Documentation**: All new `SageLang` modules must be documented within the source. Refer to `docs/architecture/` for subsystem-specific contracts.
- **Code Style**: Prefer idiomatic `SageLang` patterns over complex C boilerplate.
