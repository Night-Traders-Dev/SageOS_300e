# SageOS Release 0.4.3

## Overview
SageOS 0.4.3 focuses on standardizing the execution environment across all supported architectures (x64, arm64, rv64) and improving the performance of system services by pre-compiling more high-level components to SGVM bytecode.

## Key Changes
- **SGVM Standardization**: All core shell commands and utilities in `/etc/commands` are now pre-compiled to SGVM bytecode, ensuring consistent behavior and improved startup time across all architectures.
- **Native Toolchain Integration**: Automated download and installation of the native C toolchain (GCC 14.1.0) from GitHub releases into the virtual disk image.
- **Improved VFS Embedding**: The kernel's VFS now prioritizes `.sgvm` artifacts over source `.sage` files for system commands.
- **Cross-Platform Parity**: Ensured that `arm64`, `x64`, and `rv64` virt targets share the same set of compiled system services.

## Architecture Status
- **x64**: Fully functional virt target with ATA PIO and native toolchain support. (Kernel size: 275K)
- **arm64**: Fully functional virt target with VirtIO-MMIO and native toolchain support. (Kernel size: 358K)
- **rv64**: Fully functional virt target with VirtIO-MMIO and native toolchain support. (Kernel size: 361K)

## Known Issues
- `curl.sage` and `install.sage` remain as source files due to complex imports requiring the full AST interpreter.
- Multi-core SMP support remains experimental on ARM64 and RV64.
