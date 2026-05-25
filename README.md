# SageOS - The Multi-Architecture Operating System

SageOS is a lightweight, modular operating system project designed to run across multiple hardware architectures. This repository serves as the central hub and index for the various architecture-specific ports and components of the SageOS ecosystem.

## Project Structure

The SageOS project is divided into specialized repositories based on target CPU architectures. Each repository contains specific branches for supported hardware platforms.

### [SageOS_x64](https://github.com/Night-Traders-Dev/SageOS_x64)
Primary port for 64-bit Intel and AMD processors.
- **[300e](https://github.com/Night-Traders-Dev/SageOS_x64/tree/300e)**: Target branch for the Lenovo 300e Chromebook (2nd Gen AST). *This is currently the most mature port.*

### [SageOS_arm64](https://github.com/Night-Traders-Dev/SageOS_arm64)
Port for 64-bit ARM (AArch64) architectures.
- **[RPi4](https://github.com/Night-Traders-Dev/SageOS_arm64/tree/RPi4)**: Target branch for the Raspberry Pi 4 Model B.

### [SageOS_rv64](https://github.com/Night-Traders-Dev/SageOS_rv64)
Port for 64-bit RISC-V architectures.
- **[OrangePi_RV_2](https://github.com/Night-Traders-Dev/SageOS_rv64/tree/OrangePi_RV_2)**: Target branch for the Orange Pi RV 2.

---

## Shared Components

- **[SageLang](https://github.com/Night-Traders-Dev/SageOS_x64/tree/300e/sageos_build/sage_lang)**: The core system language and VM used across all ports.
- **[SagePkg](https://github.com/Night-Traders-Dev/SageOS_x64/tree/300e/sageos_build/sage_pkg)**: The unified package manager.

## Getting Started

For the most complete experience, we recommend starting with the **[SageOS x64 (300e)](https://github.com/Night-Traders-Dev/SageOS_x64/tree/300e)** port, which includes full Wi-Fi support, a kernel-resident shell, and programmable initialization.

## License

This project is licensed under the MIT License - see the individual repositories for details.

---
*Maintained by the Night-Traders-Dev team.*
