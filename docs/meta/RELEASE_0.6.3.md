# SageOS v0.6.3 Release Notes

## Overview
SageOS v0.6.3 is a stability and hardening release focusing on the build pipeline, architecture isolation, and runtime integrity.

## Key Changes

### 1. Build Pipeline Hardening
- **Architecture-Specific Disk Images**: Resolved the "Shared `virt.img`" risk. Each architecture now generates a dedicated image (e.g., `virt-x64.img`, `virt-arm64.img`), preventing cross-contamination of binaries and rootfs state.
- **Decoupled Build & Run**: Optimized the developer iteration loop. The `run` command now only triggers a rebuild if artifacts are missing or an explicit `build` is requested.
- **Safe RootFS Population**: Implemented architecture-specific temporary rootfs directories and added `trap`-based cleanup to purge stale state.

### 2. SGVM ABI v2.0
- **Version Handshake**: Implemented an ABI version byte in the SGVM header.
- **Runtime Validation**: The MetalVM runtime now validates the bytecode version upon loading, preventing crashes caused by ABI desynchronization between the compiler and kernel.

### 3. Filesystem & RootFS Robustness
- **Explicit Directory Creation**: The merge process now explicitly recreates the FHS directory structure on the FAT32 boot partition before copying files.
- **Robust Path Handling**: Improved `mtools` integration to ensure reliable file placement and avoid directory nesting bugs.

### 4. Kernel Initialization Fixes
- **Architecture-Specific Defaults**: Fixed a bug where the kernel used incorrect default memory base addresses for x64 and ARM64.
- **QEMU Machine Consistency**: Standardized on the `pc` machine type for x64 `virt` to support legacy IDE/PIO drivers, while maintaining modern `q35` support for specific targets.

### 5. Automated Validation
- **ELF Integrity**: Added a post-link validation phase using `readelf` to verify entry point presence and linker success.

## Boot Sequence Improvements
The 4-stage boot process is now more observable, with improved dmesg logging during filesystem mounting and SGVM bring-up.
