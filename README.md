# SageOS - The Multi-Architecture Operating System

SageOS is a lightweight, modular operating system project. This repository is the central hub containing the **architecture-agnostic core** and linking to architecture-specific ports via submodules.

## Project Structure

SageOS uses a modular structure to maximize code reuse across architectures.

- **`sageos_build/`**: The **Source of Truth** for architecture-agnostic core components.
  - `kernel/`: VFS, Shell, and common kernel logic.
  - `sage_lang/`: The SageLang compiler and VM.
- **`arch/`**: Architecture-specific ports (Git Submodules).
  - Each port (e.g., `arch/arm64`) includes this main repository as a submodule named `core` to access the agnostic components.

### Architecture Ports
- **[x64](https://github.com/Night-Traders-Dev/SageOS_x64)**: Intel/AMD 64-bit.
- **[arm64](https://github.com/Night-Traders-Dev/SageOS_arm64)**: ARM 64-bit (RPi4).
- **[rv64](https://github.com/Night-Traders-Dev/SageOS_rv64)**: RISC-V 64-bit.

## Core Components (Agnostic)

These components are shared across all architectures:
- **SageLang VM**: High-performance bytecode execution engine for OS logic.
- **VFS Layer**: Virtual Filesystem with FAT32 and BTRFS support.
- **SageShell**: Kernel-resident shell and diagnostic environment.
- **System Libraries**: Standard SageLang scripts (`os.boot`, `os.serial`, etc.) for kernel bootstrapping.

## Getting Started

To clone SageOS with all architecture ports:
```bash
git clone --recursive https://github.com/Night-Traders-Dev/SageOS.git
```

## Quick Start: Raspberry Pi 4 (QEMU)

SageOS includes a demo pipeline for RPi4.

1.  **Build the Kernel**:
    ```bash
    ./sageos_build/sage_lang/core/sage examples/boot/rpi4_demo.sage
    chmod +x build_rpi4.sh && ./build_rpi4.sh
    ```
2.  **Run in QEMU**:
    ```bash
    qemu-system-aarch64 -machine raspi4b -cpu cortex-a72 -m 1G -nographic -kernel rpi4_boot_demo/kernel.elf
    ```

### Developing for a Specific Architecture
The architecture ports are located in the `arch/` directory. Each port is an independent repository designed to link against the `core/` submodule (which points back to this repository).

To build a specific architecture (e.g., ARM64):
1.  Navigate to the port directory: `cd arch/arm64`
2.  The agnostic code is available in `./core/sageos_build/`
3.  Follow the port-specific instructions in `arch/*/README.md`.

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
