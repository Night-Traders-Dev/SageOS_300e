# SageOS Documentation

Welcome to the SageOS documentation hub. This directory contains architecture overviews, hardware guides, and developer-focused manuals for the SageOS project.

## What You Will Find Here
- Architecture deep dives explaining how each supported port integrates with the shared SageOS core.
- Device-level setup instructions for supported hardware.
- Developer guides for the build pipeline and management scripts.
- Troubleshooting notes for the repository’s nested submodule structure.

## Architecture Overviews
- [ARM 64-bit (ARM64)](arch/arm64.md)
- [Intel/AMD 64-bit (x86_64)](arch/x64.md)
- [RISC-V 64-bit (RV64)](arch/rv64.md)

## Device Guides
- [Raspberry Pi 4](devices/rpi4.md)
- [Orange Pi RV 2](devices/orangepi_rv2.md)
- [Lenovo 300e Chromebook](devices/lenovo_300e.md)
- [QEMU Virt (ARM64)](devices/virt_arm64.md)

## Developer Guides
- [Master Management Script (`sageos.sh`)](guides/management_script.md)
- [SageLang OS Build Pipeline](guides/build_pipeline.md)

## Getting Started with the Docs
1. Start with the root [README.md](../README.md) for setup and quick-start instructions.
2. Read [SageOS_Book.md](../SageOS_Book.md) for architecture, design principles, and project philosophy.
3. Use the architecture and device guides below for platform-specific details.

## Submodules and Setup
SageOS requires a special initialization process because it uses nested architecture repositories and local core reuse.
- Always run `./setup_submodules.sh` from the repository root.
- Do not rely on `git submodule update --init --recursive` by itself.
- If submodule errors occur, inspect the failing path and rerun the setup script after repair.

## Notes
This documentation is intended for developers and contributors. If you are new to SageOS, begin with the root README and SageOS Book before working in the `arch/` ports.
