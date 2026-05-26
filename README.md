# SageOS - The Multi-Architecture Operating System

SageOS is a lightweight, modular operating system project. This repository is the central hub containing the **architecture-agnostic core** and linking to architecture-specific ports via submodules.

## Project Structure

SageOS uses a modular structure to maximize code reuse across architectures.

- **`sageos_build/`**: The **Source of Truth** for architecture-agnostic core components.
  - `kernel/`: VFS, Shell, and common kernel logic.
  - `sage_lang/`: The SageLang compiler and VM.
- **`docs/`**: [Comprehensive Documentation](docs/README.md) for all architectures and devices.
- **`arch/`**: Architecture-specific ports (Git Submodules).
  - Each port (e.g., `arch/arm64`) includes this main repository as a submodule named `core` to access the agnostic components.

### Architecture Ports
- **[x64](https://github.com/Night-Traders-Dev/SageOS_x64)**: Intel/AMD 64-bit.
- **[arm64](https://github.com/Night-Traders-Dev/SageOS_arm64)**: ARM 64-bit (RPi4).
- **[rv64](https://github.com/Night-Traders-Dev/SageOS_rv64)**: RISC-V 64-bit (Orange Pi RV 2).

## Quick Start: Raspberry Pi 4 (QEMU)

SageOS includes a master management script for easy building and testing.

1.  **Build and Run**:
    ```bash
    ./sageos.sh arm64 rpi4 run
    ```

For detailed instructions on other targets, see the [Management Script Guide](docs/guides/management_script.md).

## Getting Started

To clone SageOS with all architecture ports:
```bash
git clone --recursive https://github.com/Night-Traders-Dev/SageOS.git
```

### Developing for a Specific Architecture
The architecture ports are located in the `arch/` directory. Each port is an independent repository designed to link against the `core/` submodule.

To build a specific architecture (e.g., ARM64):
1.  Navigate to the port directory: `cd arch/arm64`
2.  The agnostic code is available in `./core/sageos_build/`
3.  Follow the port-specific instructions in `arch/*/README.md`.

## License
This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
