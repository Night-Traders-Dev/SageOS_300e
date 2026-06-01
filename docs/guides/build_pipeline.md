# SageOS Build Pipeline

SageOS leverages the SageLang compiler to build its core components and architecture-specific kernels.

## Components
1.  **SageLang Compiler**: Located in `sageos_build/sage_lang/`. Compiles `.sage` scripts to native code or VM bytecode.
2.  **OS Build Module**: `os.boot.build` provides the logic for generating boot assembly, linker scripts, and kernel C stubs.

## Pipeline Flow
1.  **Script Selection**: A Sage script (e.g., `examples/boot/rpi4_demo.sage`) defines the target architecture and device.
2.  **File Generation**: The compiler generates `boot.S`, `kernel.c`, and `linker.ld`.
3.  **Compilation**: `aarch64-linux-gnu-gcc` (or relevant cross-compiler) compiles the C code and assembly.
4.  **Linking**: The linker produces a final ELF binary.
5.  **Artifact Management**: The `sageos.sh` script moves artifacts to the `build/` directory.

### Hardened Virt Pipeline (v0.7.0)
For cross-platform development, SageOS uses a hardened `virt` target managed by `scripts/build_virt.sage` and `sageos.sh`.
- **Platform Spec Handshake**: The pipeline now generates `version.h` with `SAGE_ABI_MAJOR/MINOR` constants, enforcing the [Platform Specification](../architecture/platform_spec.md).
- **Formalized Bootstrap**: Build stages are explicitly linked to kernel initialization paths, ensuring 8-stage granular boot compliance.
- **Architecture Isolation**: Each target architecture uses a dedicated disk image (e.g., `virt-x64.img`) to prevent ABI and rootfs cross-contamination.
