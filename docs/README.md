# SageOS Documentation

Welcome to the comprehensive SageOS documentation.

## Architecture Overviews
- [ARM 64-bit (ARM64)](arch/arm64.md)
- [Intel/AMD 64-bit (x86_64)](arch/x64.md)
- [RISC-V 64-bit (RV64)](arch/rv64.md)

## Device Guides
- [Raspberry Pi 4](devices/rpi4.md)
- [Orange Pi RV 2](devices/orangepi_rv2.md)
- [Lenovo 300e Chromebook](devices/lenovo_300e.md)
- [QEMU Virt (ARM64)](devices/virt_arm64.md)

## Development Guides
- [Master Management Script (`sageos.sh`)](guides/management_script.md)
- [SageLang OS Build Pipeline](guides/build_pipeline.md)

### Virt Kernel Architecture
SageOS now supports a minimalist kernel prototype for `virt` targets across x64, ARM64, and RISC-V. These builds use a shared `kernel_stubs.c` and `runtime_stub.c` to provide a base kernel environment for cross-platform driver and system development.
