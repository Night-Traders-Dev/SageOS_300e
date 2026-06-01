# SageOS v0.7.9 Release Notes

## Overview
SageOS v0.7.9 introduces the **Platform Specification**, a formal architectural contract between the kernel, firmware, and language runtime. This release also features a more granular, documented bootstrap sequence and a stabilized runtime supervisor.

## Key Changes

### 1. Platform Specification
A new document, `docs/architecture/platform_spec.md`, defines the canonical guarantees for SageOS layers. It establishes the bootstrap state machine, runtime execution modes, and ABI versioning requirements.

### 2. Granular Bootstrap Sequence
The boot process has been expanded from 5 to 8 explicit stages:
- **STAGE 0: Firmware** (UEFI, OpenSBI, etc.)
- **STAGE 1: Early Memory Management** (PMM/VMM)
- **STAGE 2: IRQ & System Init** (Exceptions/Syscalls)
- **STAGE 3: Device Discovery & IPC** (Bus scanning, IPC init)
- **STAGE 4: Storage & VFS Mounting** (FAT32/BTRFS)
- **STAGE 5: Runtime Bring-up** (SGVM core initialization)
- **STAGE 6: Service Activation** (Supervisor launch)
- **STAGE 7: Userspace Session** (Interactive shell)

### 3. Runtime execution stability
- **Refactored `sage_execute`**: Fixed a critical ambiguity where missing file paths would fall through to the parser. The runtime now uses heuristics to distinguish paths from code and hard-aborts on resolution failure.
- **Stabilized Supervisor**: `runtime_manager.sage` has been rewritten with idiomatic SageLang syntax and dependency management.
- **Improved Logging**: `dmesg` now ensures each log entry ends with a newline, preventing jumbled output.

### 4. ABI Versioning
Introduced `SAGE_ABI_MAJOR` (0) and `SAGE_ABI_MINOR` (4) to manage compatibility between the kernel and SageLang modules.

### 5. RootFS Cleanup
Removed redundant/duplicate `.sage` source files from the kernel core directory to reduce confusion and favor the `etc/system/sagelang` distribution location.

## Build System Improvements
- Fixed a bug in `scripts/populate_rootfs.sh` where the SGVM compiler was incorrectly invoked.
- Corrected executable permissions for maintenance scripts.
- Version 0.7.9 is now the standard for all `virt` builds.
