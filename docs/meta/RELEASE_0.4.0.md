# SageOS v0.4.0 - Multi-Tasking & POSIX Readiness

SageOS 0.4.0 is a major milestone that transforms the kernel from a single-tasking bare-metal loop into a multi-tasking execution host capable of running standard ELF binaries.

## New in v0.4.0

### 1. Multi-Tasking Scheduler
- **Round-Robin Scheduler**: Supports up to 64 concurrent threads.
- **Task Isolation**: Each task has its own 64KB kernel stack and 64MB userspace heap.
- **Process Model**: Implemented `vfork()`, `execve()`, `waitpid()`, and `exit()`.
- **Context Switching**: Architecture-specific assembly handlers for x64, ARM64, and RISC-V.

### 2. POSIX Syscall Suite (20+ Syscalls)
- **File I/O**: `open`, `close`, `read`, `write`, `lseek`, `fstat`, `unlink`, `getdents64`, `dup2`.
- **Process Control**: `vfork`, `execve`, `waitpid`, `exit`, `getpid`.
- **Memory**: `brk` (heap management).
- **Time/Sleep**: `gettimeofday`, `nanosleep`, `times`.
- **VFS Bridge**: Fully integrated with FAT32, BTRFS, and internal RamFS.

### 3. ELF Execution Environment
- **Standard Stack Layout**: Complies with System V / AAPCS64 ABIs for `argc`, `argv`, and `envp`.
- **Safe Memory Mapping**: Support for loading binaries into safe physical RAM regions.
- **Multi-Arch Loader**: Unified loader for all three target architectures.

### 4. GCC Port Infrastructure
- **Cross-Compiler**: Automated build scripts for `aarch64-unknown-sageos-gcc`.
- **C Runtime (CRT0)**: Unified `crt0.S` handles BSS and stack initialization.
- **Newlib Support**: Syscall stubs for Newlib integration.

## Updated Partition Layout
To support the full GCC toolchain, the virtual disk image (`virt.img`) now uses an expanded layout:
- **FAT32**: 512MB (Toolchain and user binaries)
- **BTRFS**: 512MB (System headers and source)
- **SWAP**: 125MB

## Road to Native GCC
The kernel now possesses the necessary "Foundations" to host a compiler. Current efforts are focused on:
1. Finishing the host-side cross-compiler build.
2. Compiling Binutils and GCC for the `sageos` host.
3. Stabilizing the VFS under the heavy load of compiler header lookups.
