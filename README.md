# SageOS (Lenovo 300e)

SageOS is a lightweight, x86_64 UEFI-based operating system project primarily targeting the **Lenovo 300e Chromebook (2nd Gen AST)**. The system features a freestanding kernel, a programmable init system using [SageLang](https://github.com/Night-Traders-Dev/SageOS_300e/tree/main/sage_lang), and a kernel-resident shell (**SageShell**) with advanced diagnostics.

## Quick Start

### Prerequisites
- Linux host system
- Required tools: `clang`, `lld`, `llvm`, `qemu-system-x86`, `ovmf-generic`, `dosfstools`, `mtools`, `gdisk`, `util-linux`

### Build & Run
1. **Clone the repository:**
   ```bash
   git clone https://github.com/Night-Traders-Dev/SageOS_300e.git
   cd SageOS_300e
   ```
2. **Build the OS image:**
   ```bash
   ./lenovo_300e.sh download-firmware # Required for Wi-Fi
   ./lenovo_300e.sh build
   ```
3. **Test in QEMU:**
   ```bash
   ./lenovo_300e.sh qemu
   ```
4. **Flash to USB (Hardware Deployment):**
   ```bash
   ./lenovo_300e.sh flash /dev/sdX  # Replace /dev/sdX with your USB device
   ```

## Key Features

- **UEFI-Native Boot**: Freestanding kernel execution with GOP framebuffer.
- **SageShell & MetalVM**: A kernel-resident, SageLang-driven shell with fish-style line editing and high-performance bytecode execution.
- **Hardware Abstraction**: Early diagnostics for SMP, ACPI, battery/EC, and PCI bus.
- **Programmable Init**: System initialization orchestrated via `init.sage`.
- **Advanced Storage**: Full read-write FAT32 filesystem support and BTRFS superblock reader.
- **Memory Management**: Formal physical memory allocator and virtual memory management (paging).
- **Native Drivers**: Decoupled hardware (keyboard, boot logging) from UEFI runtime services using interrupt-driven I/O.

## Documentation

- **[Boot Log](docs/boot_log.md)**: Persistent USB boot logging details.
- **[Init System](docs/init_system.md)**: Deep dive into the programmable initialization process.
- **[Hardware Support](docs/lenovo_300e_ast_hardware.md)**: Hardware-specific architectural details for the Lenovo 300e.

## Directory Structure

```text
SageOS_300e/
├── lenovo_300e.sh           # Unified build/flash/qemu script
├── sageos_build/
│   ├── kernel/              # Core kernel, drivers, VFS, shell
│   ├── sage_lang/           # SageLang toolchain
│   └── scripts/             # Compilation and build helpers
└── docs/                    # Technical documentation
```

## Contributing

Contributions are highly encouraged. Please fork the repository, create a feature branch for your improvements, test thoroughly in QEMU, and submit a pull request.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Repository

- **GitHub**: [https://github.com/Night-Traders-Dev/SageOS_300e](https://github.com/Night-Traders-Dev/SageOS_300e)
- **Issues**: [https://github.com/Night-Traders-Dev/SageOS_300e/issues](https://github.com/Night-Traders-Dev/SageOS_300e/issues)
