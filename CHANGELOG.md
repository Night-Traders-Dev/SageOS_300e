# Changelog

All notable changes to SageOS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [0.7.0] — 2026-05-30

### Added
- **Platform Specification** (`docs/architecture/platform_spec.md`): Formal architectural contract defining boot stages, runtime execution modes, and ABI versioning.
- **Security Model** (`docs/architecture/security.md`): Capability-first authority model with kernel permission bitmasks and SGVM mediation.
- **Telemetry Subsystem** (`docs/architecture/telemetry.md`): High-performance circular trace buffer with system-wide event capture.
- **Internal API Contracts** (`docs/architecture/internal_apis.md`): Locked interfaces for scheduler, memory, VFS, IPC, and MetalVM subsystems.
- 8-stage deterministic bootstrap sequence with per-stage guarantees (`boot_stages.h`).
- Architecture-isolated disk images (`virt-x64.img`, `virt-arm64.img`, `virt-rv64.img`) to prevent cross-contamination.
- SGVM ABI v2.0 versioning handshake (`SAGE_ABI_MAJOR`/`SAGE_ABI_MINOR`).
- Decoupled build/run actions in `sageos.sh` for faster iteration.
- FHS-compliant rootfs structure with `mtools`-based merging.
- GitHub Actions CI pipeline for multi-architecture build validation.

### Changed
- Core Systems Architecture document revised to v0.7.0 (Formalized).
- Bumped from v0.6.3 to v0.7.0.
- Rootfs population script now compiles `.sage` to `.sgvm` bytecode during build.
- Cleaned up redundant scripts and build artifacts.

### Fixed
- `dmesg`: Ensure each log entry ends with a newline.
- SGVM compiler invocation and script permissions in rootfs build.

---

## [0.6.3] — 2026-05-28

### Added
- Hardened build pipeline with isolated architecture builds.

### Changed
- Updated core systems architecture revision.

---

## [0.6.2] — 2026-05-26

### Added
- Rootfs-centric VFS with increased interpreter depth.

### Changed
- Removed SagePkg from core, refactored build system.

---

## [0.6.0] — 2026-05-25

### Added
- **Core systems formalization**: Initial subsystem contracts and documentation.
- Formalized boot stage definitions.

---

## [0.5.0] — 2026-05-23

### Added
- **IPC Subsystem** (`docs/architecture/ipc.md`): Full formal specification with message format, state machine, capability routing, lifecycle semantics, and 25 syscalls (200–244).
- IPC kernel implementation (`ipc.c`, `ipc.h`).
- Scheduler IPC extensions (`scheduler_ipc_ext.c`).
- SageLang IPC bindings (`ipc_sagelang_binding.c`).
- User-space IPC library (`ipc_user.c`, `ipc_user.h`).

---

## [0.4.8] — 2026-05-22

### Changed
- Standardized timer and memory reporting across architectures.

---

## [0.4.0] — 2026-05-15

### Added
- Multi-tasking kernel with `vfork`, `execve`, `waitpid`.
- ELF64 execution from disk.
- POSIX-compatible syscall layer (20+ syscalls).

---

## [0.3.0] — Early Development

### Added
- Initial multi-architecture support (x86_64, ARM64, RISC-V 64).
- MetalVM bytecode interpreter integrated into kernel.
- VFS with FAT32, BTRFS (read-only), and SWAP support.
- SageShell interactive shell.
- Networking via lwIP and mbedtls integration.
