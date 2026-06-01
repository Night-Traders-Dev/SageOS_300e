# SageOS Documentation Index

## Architectural Specifications
- [**Platform Specification**](architecture/platform_spec.md): Canonical contract defining boot stages, runtime ownership, and ABI guarantees.
- [**Core Systems Architecture**](core_systems_architecture.md): The high-level philosophy and design of the SageOS hybrid kernel.
- [**IPC Subsystem**](architecture/ipc.md): Formal specification of the communication backbone and capability manager.
- [**Security Model**](architecture/security.md): Overview of the Capability-First authority gating and system permissions.
- [**Internal API Contracts**](architecture/internal_apis.md): Documentation of the stable interfaces between kernel subsystems.
- [**Telemetry & Observability**](architecture/telemetry.md): Deep dive into the system-wide tracing and event logging infrastructure.

## Architecture Guides
- [**x86_64 (x64)**](arch/x64.md)
- [**ARM64 (AArch64)**](arch/arm64.md)
- [**RISC-V 64 (RV64)**](arch/rv64.md)

## Developer Guides
- [**Build Pipeline**](guides/build_pipeline.md): Comprehensive guide to the SageOS build system and cross-compilation.
- [**Management Script**](guides/management_script.md): Usage details for the `sageos.sh` master control script.
- [**Native Toolchain**](toolchain.md): Details on the integrated GCC/Binutils environment for on-device development.

## Hardware & Devices
- [**Raspberry Pi 4**](devices/rpi4.md)
- [**Lenovo 300e (Gemini Lake)**](devices/lenovo_300e.md)
- [**LicheeRV Nano**](devices/licheeRV_nano.md)
- [**OrangePi RV2**](devices/orangepi_rv2.md)
- [**Virtual Targets (QEMU)**](devices/virt_arm64.md)
