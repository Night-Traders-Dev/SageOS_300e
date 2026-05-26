# SageOS - The Modular Hybrid Operating System

SageOS is a modern, modular, and multi-architecture operating system project. It leverages a low-level C kernel for performance-critical tasks and a high-level scripting language, **SageLang**, for system logic and the interactive shell.

This repository is the central hub. It contains the **architecture-agnostic core** (`sageos_build/`) and links to architecture-specific hardware ports via Git submodules (`arch/`).

## Comprehensive Documentation
For a deep dive into the architecture, subsystems, and current status, please read **[The SageOS Book](SageOS_Book.md)**.

## Environment Setup
SageOS utilizes a complex submodule tree. **Do not use a standard recursive clone.** Instead, clone the repository and run the optimized setup script to initialize the environment safely, avoid recursion loops, and link our custom-forked libraries (`lwip` and `mbedtls`):

```bash
git clone https://github.com/Night-Traders-Dev/SageOS.git
cd SageOS
./setup_submodules.sh
```

## Project Structure
- **`sageos_build/`**: The **Source of Truth** for shared core components.
  - `kernel/`: The C kernel, Virtual Filesystem (VFS), and Shell logic.
  - `sage_lang/`: The SageLang compiler and MetalVM bytecode interpreter.
- **`docs/`**: Supplemental documentation and guides.
- **`arch/`**: Architecture-specific hardware ports.
  - **[x64](https://github.com/Night-Traders-Dev/SageOS_x64)**: PC/Q35 and Lenovo 300e Chromebook support.
  - **[arm64](https://github.com/Night-Traders-Dev/SageOS_arm64)**: Raspberry Pi 4 support (use the `RPi4` branch).
  - **[rv64](https://github.com/Night-Traders-Dev/SageOS_rv64)**: RISC-V Orange Pi RV 2 support.

## Building and Running
SageOS includes a master management script for building the kernel and running it in QEMU.

### Virtual Environments (QEMU `virt`/`q35`)
You can build and run generic virtualized targets for any supported architecture. These targets compile the full C kernel and SageShell:
```bash
# x86_64
./sageos.sh x64 virt build
./sageos.sh x64 virt run

# ARM64
./sageos.sh arm64 virt build
./sageos.sh arm64 virt run

# RISC-V 64
./sageos.sh rv64 virt build
./sageos.sh rv64 virt run
```

### Hardware-Specific Targets
To build for specific hardware, use the management script with the appropriate device identifier:
```bash
# Build and run the Lenovo 300e Chromebook target
./sageos.sh x64 lenovo_300e build
./sageos.sh x64 lenovo_300e run

# Build and run the Raspberry Pi 4 target
./sageos.sh arm64 rpi4 run
```

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
