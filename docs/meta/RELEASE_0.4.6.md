# Release v0.4.6

This release focuses on architectural alignment and runtime stability improvements, specifically implementing the foundations defined in the new SageOS Core Systems Architecture Specification.

## Architectural Changes

- **Boot Sequence Formalization**: The kernel boot process (`virt_main.c`) has been strictly refactored into the four distinct stages (Stage 1 to Stage 3 + Userspace Session) as mandated by the architecture specification.
- **Physical Memory Manager (PMM)**: Implemented a new bitmap-backed Physical Memory Manager (`phys_alloc`) capable of parsing the system memory map and tracking page frames correctly during early kernel initialization.
- **Virtual Memory Manager (VMM) Foundation**: Added architectural stubs for the Virtual Memory Manager to support future address space isolation and identity paging.
- **Stage 3 Service Activation**: The system service activation script (`init.sage`) is now properly embedded and executed during the Stage 3 boot sequence, allowing for runtime orchestration.

## Bug Fixes and Stability Improvements

- **Init Script Parsing Error Fixed**: Resolved a critical bug where `/etc/init.sage` was incorrectly treated as raw string input instead of being executed from the embedded filesystem. 
- **Recursion Depth Exceeded Fixed**: Fixed a runtime error caused by the syntax parser attempting to deeply recurse on malformed file input by ensuring `init.sage` is properly located, loaded, and interpreted as an embedded file.
- **Compilation and Linker Errors**: Fixed missing includes, implicit declarations (`dmesg_log` to `dmesg_printf`), and missing linker references (`boot.c`) that broke the `virt_x86_64` kernel build.
- **Disk Image Generator**: Updated `scripts/gen_virt_disk.sh` to dynamically calculate partition offsets based on input size, while increasing the default swap space to 512MB to mitigate `Disk full` errors during toolchain installation.

## Documentation

- **Core Systems Architecture Specification**: Created the foundational `docs/core_systems_architecture.md` outlining system philosophy, memory models, IPC, security models, and the SGVM runtime lifecycle.