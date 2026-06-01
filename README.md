# SageOS v0.6.3 - Formalized Hybrid Operating System

SageOS is a hybrid operating system that combines a low-level C kernel with a high-level, SageLang-driven runtime. It is designed to be modular, secure, and fully observable across multiple architectures.

## What SageOS Provides
- **Hybrid kernel architecture**: C for performance, SageLang for system services, shell logic, and runtime extensions.
- **Multi-architecture support**: Native ports for x86_64, ARM64 (AArch64), and RISC-V 64.
- **Self-Healing Runtime Supervision**: Managed by `runtime_manager.sage` (PID 1) with auto-restart and dependency orchestration.
- **Capability-First Security**: Strict authority gating via unforgeable tokens and task-level permissions.
- **Deep Instrumentation**: System-wide telemetry for real-time observability of scheduler, IPC, and VM events.

## Core Features (v0.6.3)
- **Hardened Build Pipeline**: Isolated, architecture-specific disk images (`virt-x64.img`, `virt-arm64.img`) to prevent binary and state cross-contamination.
- **SGVM ABI v2.0**: Robust versioning handshake between the Sage compiler and MetalVM runtime, preventing incompatible bytecode execution.
- **Optimized Iteration**: Decoupled build and run actions for faster development cycles.
- **Robust RootFS System**: Explicit directory structure preservation and improved `mtools`-based merging.
- **Formalized IPC**: Robust communication backbone with strict object lifecycle and capability routing.
- **Capability Security**: Permission-gated syscalls (reboot, raw IO) and isolated resource access.
- **System Supervision**: SageLang-native PID 1 supervisor managing the system bootstrap and service health.
- **Unified Telemetry**: High-performance circular trace buffer providing deep insight into kernel and VM behavior.
- **Multi-Arch Binary Execution**: Load and execute static ELF64 binaries on x64, ARM64, and RV64.
- **Native Toolchain Integration**: GCC 14.1.0 pre-installed in the disk image for on-device C development.

## Documentation
SageOS documentation is organized into focused architectural specifications:
- [Core Systems Architecture](docs/core_systems_architecture.md)
- [IPC Subsystem Spec](docs/architecture/ipc.md)
- [Security Model](docs/architecture/security.md)
- [Internal API Contracts](docs/architecture/internal_apis.md)
- [Telemetry & Observability](docs/architecture/telemetry.md)

## Why This Repository Exists
This repository is the central coordination point for SageOS development. It contains:
- `sageos_build/`: the shared system core and SageLang runtime.
- `arch/`: architecture-specific ports that reuse the core via nested submodules.
- `docs/`: project documentation and developer guides.
- `examples/`: sample SageLang programs and boot scripts.
- `setup_submodules.sh`: a trusted initializer that prevents infinite submodule recursion.

## Quick Start
```bash
git clone https://github.com/Night-Traders-Dev/SageOS.git
cd SageOS
./setup_submodules.sh
```

### Important
Do not use `git submodule update --init --recursive` directly on the root repository. SageOS relies on `./setup_submodules.sh` to properly configure local `core` references and disable redundant nested submodule clones.

## Building and Running
Build and run the OS with the management script.

### Virtualized targets
```bash
./sageos.sh x64 virt build
./sageos.sh x64 virt run

./sageos.sh arm64 virt build
./sageos.sh arm64 virt run

./sageos.sh rv64 virt build
./sageos.sh rv64 virt run
```

### Virtual Disk for Virt builds
The `virt` builds now use architecture-specific virtual disk images (e.g., `virt-x64.img`).
- **Partition 1**: FAT32 (4GB) - Mounted at `/`. Contains the system root, toolchain, and scripts.
- **Partition 2**: BTRFS (512MB) - Secondary filesystem support.
- **Partition 3**: SWAP (512MB) - Registered as a swap device.

## License
MIT License. See [LICENSE](LICENSE).
