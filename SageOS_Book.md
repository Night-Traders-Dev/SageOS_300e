# The SageOS Book
*A Comprehensive Guide to the Modular Hybrid Operating System*

## Introduction
SageOS is a modern, modular, and multi-architecture operating system designed with a unique hybrid architecture. It leverages a low-level C kernel for performance-critical tasks and a high-level scripting language, **SageLang**, for complex system logic, driver management, and user interaction.

The core philosophy of SageOS is "Modular Agnosticism": the system's core logic is kept entirely independent of hardware, while architecture-specific ports link against this core as submodules.

---

## 1. System Architecture

### 1.1 The Hybrid Core (`sageos_build`)
The heart of SageOS resides in `sageos_build/kernel`. It consists of several key layers:

#### SageLang VM (MetalVM)
MetalVM is a custom-built, freestanding bytecode interpreter. Unlike standard VMs, MetalVM is designed to run directly in the kernel's privileged mode without an underlying OS. It allows SageOS to:
*   Execute OS logic written in SageLang.
*   Manage dynamic system services.
*   Provide a safe execution environment for system scripts.

#### Virtual Filesystem (VFS)
The VFS in SageOS is a hybrid implementation:
*   **C Backend**: Provides the raw interface for hardware-linked filesystem drivers (FAT32, BTRFS).
*   **Sage-Native Core**: The default system filesystem (**RamFS**) is implemented entirely in SageLang (`vfs_bridge.sage`). This ensures that the majority of filesystem logic (path resolution, directory management, and file access) is memory-safe and easily extensible.
*   **Unified Router**: A SageLang-based router handles mount points and dispatches requests between Sage-native and C-native backends seamlessly.

#### SageShell & Telemetry
The SageShell is the primary diagnostic and interactive environment. Following the **Sage-First Principle**, all major telemetry and diagnostic tools are implemented as pure Sage scripts:
*   `sched`: Interactive process schedule table.
*   `swap`: Visual memory utilization metrics.
*   `dmesg`: Kernel log ring-buffer viewer.
These tools consume lightweight OS natives to fetch raw data, while all formatting and styling logic resides in SageLang.

### 1.2 Architecture Ports (`arch/`)
SageOS supports multiple architectures, each maintained in its own repository and linked as a submodule in the `arch/` directory.

*   **x64 (`arch/x64`)**: The most mature port, supporting standard PC hardware (QEMU `q35`) and specifically optimized for the **Lenovo 300e Chromebook**. It includes drivers for ACPI, PCI, SATA, E1000 networking, and initial WiFi support (QCA6174).
*   **ARM64 (`arch/arm64`)**: Targets the **Raspberry Pi 4** and QEMU `virt` machine. It features PL011 UART support and a specialized boot pipeline for RPi4 hardware.
*   **RISC-V (`arch/rv64`)**: An emerging port targeting the **Orange Pi RV 2** and RISC-V QEMU `virt` machine.

### 1.3 Submodule Strategy and Initialization
SageOS uses a nested submodule graph to share the same core implementation across multiple architecture ports without duplicating work.

* `setup_submodules.sh` is the supported initialization path.
* The script initializes root submodules with `--remote` to resolve forked libraries and remote branches.
* It then configures each `arch/*/core` submodule to use the local root repository as the core source.
* Nested `lwip` and `mbedtls` paths are disabled inside the architecture core to prevent redundant cloning.
* This avoids the common infinite recursion failure mode of repeated nested architecture cores.

### 1.4 Recommended Workflow
1. Clone the repository:
   ```bash
git clone https://github.com/Night-Traders-Dev/SageOS.git
cd SageOS
```
2. Initialize the workspace:
   ```bash
./setup_submodules.sh
```
3. After pulling new changes:
   ```bash
git pull
./setup_submodules.sh
```
4. If a submodule commit is missing, inspect the affected path and repair the gitlink before rerunning the setup script.

### 1.5 Recent Porting Work

The project follows the "Sage-First Principle" and has migrated many non-critical kernel services into pure SageLang implementations. Notable areas migrated:
- Virtual Filesystem high-level logic (`vfs_bridge.sage`), RamFS, and mount routing.
- Shell command implementations and diagnostics (`sage_shell/*` and `/etc/commands/*.sage`).
- Init scripts and service orchestration (`/etc/init.sage`).

Low-level boot, scheduler, drivers, and the MetalVM interpreter remain in C.

---

## 2. SageLang Integration
SageLang is not just an application language in SageOS; it is a fundamental part of the kernel. Key system modules include:
*   `os.boot`: Manages the kernel bootstrap process.
*   `os.vfs`: Provides the high-level API for filesystem operations.
*   `os.serial`: Handles serial communication for debugging and shell access.

The build system itself is written in SageLang, using the `os.boot.build` module to orchestrate cross-compilation and image generation.

---

## 3. Networking & Security
SageOS integrates two major third-party libraries, maintained as forks in the `Night-Traders-Dev` organization:
*   **lwIP**: A lightweight TCP/IP stack, ported to the SageOS kernel through `lwip_port.c`.
*   **mbedtls**: Provides cryptographic primitives and TLS support, integrated via `mbedtls_port.c`.

These libraries allow SageOS to support networking features like DHCP, HTTP, and secure communication even in its early development stages.

---

## 4. Development & Build System

### 4.1 Environment Setup
SageOS uses a specialized setup script to manage its complex submodule tree and prevent infinite recursion:
```bash
./setup_submodules.sh
```
This script parallelizes downloads and ensures that forked repositories are correctly linked.

### 4.2 The Management Script
The `sageos.sh` script is the entry point for building and running the OS:
```bash
./sageos.sh [arch] [target] [action]
```
Example: Building and running for ARM64 on QEMU:
```bash
./sageos.sh arm64 rpi4 run
```

---

## 5. Current Status & Roadmap
SageOS is currently at **v0.2.0 (Alpha)**.

### Achievements:
*   Functional MetalVM integrated into the kernel core.
*   Hybrid VFS supporting multiple mounts.
*   Multi-arch boot support (x64, ARM64, RV64).
*   Integrated networking via lwIP and mbedtls.
*   Unified multi-architecture **SageShell** port, running interactively across x86_64, aarch64, and riscv64 virtual targets.

### Upcoming Milestones:
*   Expansion of the SageLang standard library for kernel development.
*   Robust WiFi driver support for the Lenovo 300e.
*   User-mode process isolation and multitasking.
*   Graphical subsystem (Lumo) integration.

---
*Generated by Gemini CLI for Night-Traders-Dev*
