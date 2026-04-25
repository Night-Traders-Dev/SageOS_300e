# SageOS Lenovo 300e Build

SageOS is a small x86_64 UEFI operating system bring-up project targeting the **Lenovo 300e Chromebook 2nd Gen AST**.

The kernel boots through UEFI, loads a freestanding kernel, initializes a GOP framebuffer console, runs a kernel-resident shell with fish-style line editing, discovers platform hardware through ACPI, and provides early diagnostics for keyboard, framebuffer, SMP, ACPI, timer, memory, and battery/EC support.

Recent updates:
- **Centralized Versioning**: Versioning is now managed through a single `VERSION` file in the root directory, with automatic header generation during the build process.
- **Status Bar**: Fixed missing `%` glyph in the framebuffer console; battery, CPU, and RAM metrics now display correctly.
- **ELF & SageLang**: Added foundational ELF loading/execution support and integrated SageLang as a git submodule for future modular development.
- **Battery:** Correct CrOS EC identity check (`'E','C'` at `EC_MEMMAP_ID + 0x20`), `BATT_FLAG` validity gate before reading capacity, removed false 50% fallback.
- **Shell line editing (QEMU):** Fixed backspace ghost character, history Up/Down screen update, and fish-style dim-grey tab completion hint. Multi-match Tab now correctly updates the prompt anchor row so subsequent edits land in the right place.
- **Keyboard (UEFI path):** Arrow/special keys no longer silently dropped — UEFI scan codes are now mapped to PS/2-style extended scancodes unified across both input backends.

## Current Version

```text
SageOS v0.1.2
```

## Target Hardware

```text
Device: Lenovo 300e Chromebook 2nd Gen AST
Boot:   x86_64 UEFI
Display: UEFI GOP framebuffer
Input:  UEFI ConIn firmware fallback + native i8042/PS2 driver
CPU:    AMD x86_64, multi-core SMP enabled
```

## Current Feature Status

| Area | Status |
|---|---|
| UEFI boot | Working |
| GPT + EFI System Partition image | Working |
| PE/COFF BOOTX64.EFI | Working |
| Kernel loading | Working |
| GOP framebuffer console | Working |
| Kernel shell | Working |
| Shell line editing | Working — insert, delete, cursor move, history, tab complete with hint |
| Unified build/flash tool | Working |
| Modular kernel tree | Working |
| IDT installation | Working |
| PIT timer (IRQ0) | Working |
| CPU% accounting | Working — real-time 1 s sliding window |
| Status bar | Working — persistent top-bar, non-blocking refresh, preserved during scroll |
| Keyboard | Working — UEFI ConIn + native i8042; arrow/special keys unified across both paths |
| RAM status | Working — real-time used RAM tracking; may read high while firmware boot services are active |
| SMP | Working — INIT/SIPI sequence, per-CPU stacks, AP idle loop |
| ACPI | Working — minimal AML parser, Battery (_BST) & Lid detection |
| Battery | Working — CrOS EC LPC probed at 0x900/0x880/0x800; `BATT_FLAG` validity gate; `--` shown when EC or data not confirmed |
| VFS / FAT32 | Working — ATA PIO block driver, VFS layer, FAT32 mount |
| SageLang Backend | Working — bare-metal stabilized, runtime-free modules |
| ELF Execution | Working — segment mapping, BSS, entry jump |
| SageLang Toolchain | Working — compiler/runtime hooks |

## Important Design Note

The current hardware bring-up path intentionally uses a freestanding C/ASM kernel instead of the Sage AOT kernel path. The older Sage AOT path hit backend/runtime issues such as unsupported codegen statements and missing Sage runtime symbols during kernel linking.

Once the Sage compiler backend can reliably emit freestanding procedures/imports/runtime-free code, parts of the kernel can migrate back into Sage modules.

## Directory Layout

```text
SageOS_300e/
├── lenovo_300e.sh
├── sageos.img
├── README.md
├── VERSION
└── sageos_build/
    ├── BOOTX64.EFI
    ├── KERNEL.BIN
    ├── kernel.elf
    ├── esp.img
    ├── boot/
    │   └── uefi_loader.c
    └── kernel/
        ├── entry.S
        ├── linker.ld
        ├── include/
        │   ├── acpi.h
        │   ├── battery.h
        │   ├── bootinfo.h
        │   ├── console.h
        │   ├── idt.h
        │   ├── io.h
        │   ├── keyboard.h
        │   ├── power.h
        │   ├── ramfs.h
        │   ├── serial.h
        │   ├── shell.h
        │   ├── smp.h
        │   ├── status.h
        │   ├── timer.h
        │   └── version.h
        ├── core/
        │   └── kernel.c
        ├── drivers/
        │   ├── acpi.c
        │   ├── battery.c
        │   ├── framebuffer.c
        │   ├── idt.c
        │   ├── keyboard.c
        │   ├── power.c
        │   ├── serial.c
        │   ├── smp.c
        │   ├── status.c
        │   └── timer.c
        ├── fs/
        │   └── ramfs.c
        └── shell/
            └── shell.c
```

## Unified Build Tool

Use `lenovo_300e.sh` for all normal operations.

### Build Image

```bash
./lenovo_300e.sh build
```

### Build Kernel Only

```bash
./lenovo_300e.sh build-kernel
```

### Run in QEMU

```bash
./lenovo_300e.sh qemu
```

> **QEMU notes:**
> - Battery reads `--` — QEMU exposes no real ACPI battery by default.
> - CPU% may read `0%` at an idle shell prompt — expected for a truly idle VM.
> - All shell line editing features (backspace, history, tab hint) are fully functional in QEMU as of v0.1.2.

### Flash to USB

Default target is usually `/dev/sdb`:

```bash
./lenovo_300e.sh flash /dev/sdb
```

### Build and Flash

```bash
./lenovo_300e.sh all /dev/sdb
```

### Clean Build Outputs

```bash
./lenovo_300e.sh clean
```

### Show Build Status

```bash
./lenovo_300e.sh status
```

## Required Host Tools

Install these on the Linux host:

```bash
sudo apt update
sudo apt install -y \
  clang \
  lld \
  llvm \
  qemu-system-x86 \
  ovmf \
  dosfstools \
  mtools \
  gdisk \
  util-linux
```

The build expects these commands to exist:

```text
clang
lld-link
ld.lld
llvm-objcopy
mkfs.fat
mcopy
mmd
sgdisk
truncate
dd
qemu-system-x86_64
```

## Prerequisites

- Linux host system
- Required tools: clang, lld, llvm, qemu-system-x86, ovmf, dosfstools, mtools, gdisk, util-linux

Install on Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y clang lld llvm qemu-system-x86 ovmf dosfstools mtools gdisk util-linux
```

## Building

1. Clone the repository:
   ```bash
   git clone https://github.com/Night-Traders-Dev/SageOS_300e.git
   cd SageOS_300e
   ```

2. Build the OS image:
   ```bash
   ./lenovo_300e.sh build
   ```

## Running

### In QEMU (recommended for testing)
```bash
./lenovo_300e.sh qemu
```

### On Hardware (Lenovo 300e)
1. Flash to USB drive:
   ```bash
   ./lenovo_300e.sh flash /dev/sdX  # Replace /dev/sdX with your USB device
   ```
2. Boot the Lenovo 300e from USB in developer mode.

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly in QEMU
5. Submit a pull request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Repository

- **GitHub**: https://github.com/Night-Traders-Dev/SageOS_300e
- **Issues**: https://github.com/Night-Traders-Dev/SageOS_300e/issues

## Boot Flow

```text
UEFI firmware
  ↓
/EFI/BOOT/BOOTX64.EFI
  ↓
UEFI loader initializes GOP and reads KERNEL.BIN
  ↓
Boot info passed to kernel
  ↓
Kernel entry.S bridges ABI and stack
  ↓
kmain()
  ↓
serial → console → ACPI → SMP → IDT → PIT/IRQ → battery → keyboard → shell
```

## Boot Info Handoff

The UEFI loader passes a `SageOSBootInfo` structure into the kernel.

Current fields include:

```text
magic
framebuffer_base
framebuffer_size
width
height
pixels_per_scanline
pixel_format
system_table
boot_services
runtime_services
con_in
con_out
boot_services_active
input_mode
acpi_rsdp
memory_map
memory_map_size
memory_desc_size
memory_total
memory_usable
kernel_base
kernel_size
```

## Shell Commands

```text
help
clear
version
uname
about
fb
input
keydebug
status
timer
smp
smp start
acpi
acpi tables
acpi fadt
acpi madt
acpi lid
acpi battery
battery
ls
cat <path>
execelf <path>
sage <module>
echo <text>
color <white|green|amber|blue|red>dmesg
shutdown
poweroff
suspend
halt
reboot
```

### Shell Line Editing

| Key | Action |
|---|---|
| `Up` / `Down` | History navigation — newest entry first |
| `Left` / `Right` | Move cursor within line |
| `Home` / `End` | Jump to start / end of line |
| `Tab` | Autocomplete — dim grey hint on unique match; candidate list + LCP fill on multiple matches |
| `Backspace` / `Delete` | Delete character before / at cursor |
| `Ctrl-A` / `Ctrl-E` | Jump to beginning / end of line |
| `Ctrl-U` | Clear entire line |
| `Ctrl-C` | Cancel current line |

History stores up to 16 entries in a ring buffer. Duplicate consecutive commands are suppressed.

## Diagnostic Commands

### Status Bar / Metrics

```text
status
timer
```

The top-right status bar shows:

```text
BAT --%  CPU NN%  RAM NN%
```

The status bar uses a dirty-cell shadow buffer — only cells that change are redrawn. It refreshes at 10 Hz from the keyboard idle path without touching the framebuffer from interrupt context.

CPU% is computed from IRQ0-driven idle accounting using a 100-tick sliding window (1-second average at 100 Hz).

### Battery

```text
battery
```

The `battery` command probes the CrOS EC LPC memory-mapped region at candidate bases `0x900`, `0x880`, and `0x800`. It confirms the two-byte EC identity (`'E','C'` at `EC_MEMMAP_ID` offset `0x20`), checks `BATT_FLAG` for `BATT_PRESENT` and `INVALID_DATA` bits, then reads the raw `BATT_CAP` and `BATT_CAP_FULL` registers to compute percentage.

Sample output on hardware with a confirmed EC:

```text
EC ID bytes: 'E','C' confirmed
BATT_FLAG: 0xNN  [PRESENT] [VALID] [DISCHARGING]
percentage: 73%
```

If the EC is not found at any candidate base, `battery` prints `ID not confirmed` and the status bar shows `--`.

### SMP / CPU Discovery

```text
smp
```

Current Lenovo output shows two discovered CPUs through ACPI MADT:

```text
discovered CPUs: 2
AP startup: INIT/SIPI sequence enabled
per-CPU stacks: allocated
```

### ACPI

```text
acpi
acpi tables
acpi fadt
acpi madt
acpi battery
acpi lid
```

The Lenovo 300e has working ACPI discovery with:

```text
RSDP detected
XSDT detected
FADT/FACP detected
DSDT detected
MADT/APIC detected
Battery hints present
Chromebook/ACPI EC hints present
```

S5 (shutdown) and S3 (suspend) sleep packages are parsed from the DSDT at boot and wired to the `shutdown` and `suspend` commands.

### Keyboard

Default builds keep UEFI boot services active so the kernel can read `ConIn` events on the internal keyboard. Set `SAGEOS_EXIT_BOOT_SERVICES=1` when building to test the strict native i8042 path.

Both input backends now correctly deliver arrow keys and other special keys (Home, End, Delete, Page Up/Down) by mapping UEFI scan codes to PS/2-style extended scancodes in a unified dispatch path.

```text
keydebug
```

Use this to inspect raw scancodes. Press `ESC` to leave keydebug mode.

## Known Lenovo 300e ACPI Values

Observed on hardware:

```text
Local APIC: 0xFEE00000
SMI_CMD:    0xB2
ACPI_ENABLE: 225
PM1a_CNT:   0x404
PM1b_CNT:   0x0
CPUs:       2 discovered through MADT
Battery:    ACPI battery hints present
EC:         Chromebook/ACPI EC hints present
```

## Current Limitations

### Battery Percentage

The CrOS EC LPC identity check and `BATT_FLAG` validity gate are implemented. Whether the EC base address is `0x900`, `0x880`, or `0x800` on a given 300e firmware build depends on the BIOS variant. If `battery` prints `ID not confirmed` on hardware, the next step is to verify the EC base via `keydebug` or a targeted port scan.

If the EC is confirmed but data still reads invalid, the fallback path is:

```text
Option A: AML interpreter for _BST / _BIF / _BIX
Option B: CrOS EC host command 0x10 (EC_CMD_CHARGE_STATE) via LPC host command port
```

### Suspend / Lid Close Wake

The `suspend` command attempts ACPI S3. The S3 sleep package is parsed and the PM1a_CNT write is issued, but automatic lid-close/lid-open behavior is not implemented.

Next required work:

```text
ACPI SCI routing
GPE status/enable registers
LID device detection
_LID method evaluation or targeted EC query
Chromebook EC event handling
resume path cleanup
```

### Filesystem

A read-only FAT32 root filesystem is mounted from the EFI System Partition and accessible from the shell alongside the built-in RAMFS.

Next required work:

```text
initrd support
VFS expansion
file-backed shell commands
```

## Roadmap

### v0.1.2 — Battery Confirmation

```text
- Verify EC base address on all 300e firmware variants
- Confirm BATT_CAP / BATT_CAP_FULL register reads against hardware
- Fall back to CrOS EC host command path if ECMAP not confirmed
- Live battery percentage in status bar
```

### v0.1.3 — Lid Suspend/Wake

```text
- ACPI SCI routing
- GPE status/enable registers
- lid device detection
- lid close suspend trigger
- lid open wake/resume handling
```

### v0.1.4 — Persistent Storage Expansion

```text
- initrd support
- VFS expansion
- file-backed shell commands
```

### v0.1.5 — Sage Integration

```text
- Stabilize Sage bare-metal backend
- procedure/import support
- runtime-free Sage kernel modules
- start migrating shell/ramfs/console logic to Sage
```

## Development Rules

1. Keep `lenovo_300e.sh` as the only normal build/flash entry point.
2. Keep old monolithic kernel backups out of the active compile path.
3. Add new kernel features as modules under `sageos_build/kernel/`.
4. Do not reintroduce the Sage AOT kernel path until backend support is ready.
5. Validate in QEMU before flashing.
6. Validate on the Lenovo 300e after every hardware-facing change.

## Quick Start

```bash
./lenovo_300e.sh build
./lenovo_300e.sh qemu
./lenovo_300e.sh flash /dev/sdb
```

After booting on hardware:

```text
status
timer
smp
acpi
acpi fadt
acpi madt
battery
keydebug
shutdown
```
