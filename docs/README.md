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
SageOS provides unified virtual target support for `virt` environments across `x86_64` (x64), `aarch64` (ARM64), and `riscv64` (RV64). These virtual kernels are fully functional, interactive, and automatically execute the pure-SageLang **SageShell** via our MetalVM bytecode interpreter, offering a highly responsive developer playground across all three major system architectures.
