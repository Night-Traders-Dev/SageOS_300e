# SageOS - The Multi-Architecture Operating System

SageOS is a lightweight, modular operating system project. This repository is the central hub containing the **architecture-agnostic core** and linking to architecture-specific ports via submodules.

## Project Structure

- **`sageos_build/kernel/`**: The shared core components (SageLang, VFS, Shell, etc.).
- **`arch/`**: Architecture-specific ports (Git Submodules).
  - **[x64](https://github.com/Night-Traders-Dev/SageOS_x64)**: Intel/AMD 64-bit.
    - *Targets*: QEMU (q35), Physical PC.
    - *Status*: Stable bootstrap, basic drivers.
  - **[arm64](https://github.com/Night-Traders-Dev/SageOS_arm64)**: ARM 64-bit.
    - *Targets*: Raspberry Pi 4 (RPi4), QEMU (virt).
    - *Status*: RPi4 boot support (experimental), UART PL011.
    - *Branch*: Use the `RPi4` branch for Raspberry Pi specific code.
  - **[rv64](https://github.com/Night-Traders-Dev/SageOS_rv64)**: RISC-V 64-bit.
    - *Targets*: QEMU (virt).
    - *Status*: Initial port.

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

### Building for Raspberry Pi 4

SageOS now includes a boot build pipeline for RPi4.

1.  **Generate the RPi4 Build Environment**:
    ```bash
    ./sageos_build/sage_lang/core/sage rpi4_demo.sage
    ```
2.  **Compile the Kernel**:
    ```bash
    chmod +x build_rpi4.sh
    ./build_rpi4.sh
    ```
3.  **Run in QEMU**:
    ```bash
    qemu-system-aarch64 -machine raspi4b -cpu cortex-a72 -m 1G -display none -serial stdio -kernel rpi4_boot_demo/kernel.elf
    ```

### Developing for a Specific Architecture
The architecture ports are located in the `arch/` directory. Each submodule points to its respective repository's `main` branch, designed to link against the agnostic core.

- **x64**: `arch/x64`
- **ARM64**: `arch/arm64` (Switch to `RPi4` branch for RPi hardware support).
- **RISC-V**: `arch/rv64`

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
