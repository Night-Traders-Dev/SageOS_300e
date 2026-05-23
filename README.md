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
   ./lenovo_300e.sh build   # firmware is downloaded fresh on every build
   ```
3. **Test in QEMU:**
   ```bash
   ./lenovo_300e.sh qemu
   ```
4. **Flash to USB (Hardware Deployment):**
   ```bash
   ./lenovo_300e.sh flash live /dev/sdX  # Replace /dev/sdX with your USB device
   ```

## Key Features

- **UEFI-Native Boot**: Freestanding kernel execution with GOP framebuffer console.
- **SageShell & MetalVM**: A kernel-resident, SageLang-driven shell with fish-style line editing and high-performance bytecode execution.
- **QCA6174A Wi-Fi Driver**: Full PCI enumeration, firmware staging (firmware-6.bin / board-2.bin downloaded fresh each build), WMI/HTT ring initialization, WPA2-PSK handshake, and DHCP. Credentials are saved to `/fat32/WIFI.CFG` and auto-reconnect on boot.
- **Hardware Abstraction**: Early diagnostics for SMP, ACPI, battery/EC, and PCI bus.
- **Programmable Init**: System initialization orchestrated via `init.sage`.
- **Advanced Storage**: Full read-write case-insensitive FAT32 filesystem support, and BTRFS superblock reader with copy-on-write (COW) write stubs.
- **Memory Management**: Formal physical memory allocator and virtual memory management (paging).
- **Native Drivers**: Decoupled hardware (keyboard, boot logging) from UEFI runtime services using interrupt-driven I/O.
- **Real-Time Resource Monitor (`btop`)**: Live CPU%, RAM, battery, storage, and scheduler stats. Runs on both the GOP framebuffer and serial terminal. Hardware-safe (no PIT-dependent sleep loops).
- **Persistent Boot Log (`bmesg`)**: Full kernel boot sequence written to `/fat32/BOOTLOG.TXT`, readable via the `bmesg` shell command.
- **Live dmesg**: Kernel messages logged throughout boot and runtime, persisted to ATA sectors, viewable with `dmesg`.
- **SagePkg Package Manager**: Integrated package management. Automatically pulls, builds, and installs modular packages from the official repository directly within the OS.

## Shell Commands

| Category | Commands |
|---|---|
| System Info | `neofetch`, `sysinfo`, `uname`, `version`, `status`, `timer`, `sched`, `smp`, `acpi`, `battery`, `pci`, `fb`, `input` |
| Filesystem | `ls`, `cat`, `cp`, `rm`, `mkdir`, `touch`, `stat`, `hexdump`, `nano`, `write`, `pwd` |
| Networking | `curl`, `net`, `net selftest`, `ipconfig`, `wifi`, `wifi reset`, `wifi upload`, `wifi init-rings`, `wifi scan`, `wifi connect <ssid> <pass>` |
| Diagnostics | `dmesg`, `bmesg`, `btop`, `keydebug` |
| Power | `reboot`, `shutdown`, `poweroff`, `halt`, `suspend` |
| SageLang | `sage <module>`, `sageshell`, `source <script>`, `sagepkg` |
| Storage | `swap`, `sdhci`, `install` |
| Scripting | `sh`, `echo`, `color`, `history`, `execelf` |

## Wi-Fi Usage

```
wifi                        # Show hardware info / connected SSID
wifi reset                  # Cold-reset target chipset
wifi upload                 # Stage firmware-6.bin and board-2.bin from /fat32
wifi init-rings             # Initialize WMI + HTT host rings (makes wlan0 READY)
wifi scan                   # Active RF scan (reads live RTC state from MMIO)
wifi connect <SSID> <pass>  # WPA2-PSK handshake + DHCP, saves to /fat32/WIFI.CFG
```

Credentials saved to `/fat32/WIFI.CFG` are automatically loaded on boot and a reconnect is attempted.

## Documentation

- **[Boot Log](docs/boot_log.md)**: Persistent USB boot logging — use `bmesg` to read in-shell.
- **[Init System](docs/init_system.md)**: Deep dive into the programmable initialization process.
- **[Hardware Support](docs/lenovo_300e_ast_hardware.md)**: Hardware-specific architectural details for the Lenovo 300e AST.
- **[VFS Architecture](docs/vfs_architecture.md)**: The Virtual Filesystem layer, SageLang bridge, mount table, and case-insensitive resolution.
- **[Wi-Fi Driver Architecture](docs/wifi_driver.md)**: Qualcomm QCA6174A PCI hardware support, staging sequence, and WPA2-PSK association.

## Directory Structure

```text
SageOS_300e/
├── lenovo_300e.sh           # Unified build/flash/qemu script
├── VERSION                  # Current operating system version (0.1.95)
├── sageos_build/
│   ├── kernel/              # Core kernel, drivers, VFS, shell
│   │   ├── core/            # Kernel entry, scheduler, VMM, SageLang VM
│   │   ├── drivers/         # PCI, Wi-Fi, keyboard, timer, battery, etc.
│   │   ├── fs/              # FAT32, BTRFS, ramfs, VFS
│   │   └── shell/           # SageShell, btop, nano, extra commands
│   ├── sage_lang/           # SageLang toolchain
│   └── scripts/             # Compilation and build helpers
├── firmware/                # QCA6174A firmware blobs (downloaded at build time)
└── docs/                    # Technical documentation
```

## Contributing

Contributions are highly encouraged. Please fork the repository, create a feature branch for your improvements, test thoroughly in QEMU, and submit a pull request.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Repository

- **GitHub**: [https://github.com/Night-Traders-Dev/SageOS_300e](https://github.com/Night-Traders-Dev/SageOS_300e)
- **Issues**: [https://github.com/Night-Traders-Dev/SageOS_300e/issues](https://github.com/Night-Traders-Dev/SageOS_300e/issues)
