# SageOS Lenovo 300e Build

SageOS is a small x86_64 UEFI operating system bring-up project targeting the **Lenovo 300e Chromebook 2nd Gen AST**.

The current build boots through UEFI, loads a freestanding kernel, initializes a GOP framebuffer console, runs a kernel-resident shell, discovers platform hardware through ACPI, and provides early diagnostics for keyboard, framebuffer, SMP, ACPI, timer, memory, and battery/EC support.

## Current Version

```text
SageOS v0.0.10
```

## Target Hardware

```text
Device: Lenovo 300e Chromebook 2nd Gen AST
Boot:   x86_64 UEFI
Display: UEFI GOP framebuffer
Input:  native i8042/PS2 early keyboard path
CPU:    AMD x86_64, ACPI MADT-discovered CPUs
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
| Unified build/flash tool | Working |
| Modular kernel tree | Working |
| Keyboard diagnostics | Working via `keydebug` |
| RAM status | Early UEFI memory-map summary |
| CPU status | PIT IRQ + idle-loop estimate |
| SMP | ACPI MADT CPU discovery; AP startup not enabled yet |
| ACPI | RSDP/XSDT/FADT/MADT inspection |
| Battery | ACPI battery and Chromebook/EC hints detected; percentage not implemented yet |
| Shutdown | ACPI S5 attempt |
| Suspend | ACPI S3 attempt; lid close/wake not implemented yet |

## Important Design Note

The current hardware bring-up path intentionally uses a freestanding C/ASM kernel instead of the Sage AOT kernel path. The older Sage AOT path hit backend/runtime issues such as unsupported codegen statements and missing Sage runtime symbols during kernel linking. See the saved build log for that failure context. :contentReference[oaicite:0]{index=0}

Once the Sage compiler backend can reliably emit freestanding procedures/imports/runtime-free code, parts of the kernel can migrate back into Sage modules.

## Directory Layout

```text
SageOS_300e/
├── lenovo_300e.sh
├── sageos.img
├── README.md
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
        │   └── timer.h
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

## Boot Flow

```text
UEFI firmware
  ↓
/EFI/BOOT/BOOTX64.EFI
  ↓
UEFI loader initializes GOP and reads KERNEL.BIN
  ↓
Boot info is passed to kernel
  ↓
Kernel entry.S bridges ABI and stack
  ↓
kmain()
  ↓
serial + framebuffer console + ACPI + SMP discovery + timer + keyboard + shell
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

Current shell commands include:

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
acpi
acpi tables
acpi fadt
acpi madt
acpi battery
battery
ls
cat <path>
echo <text>
color <white|green|amber|blue|red>
dmesg
shutdown
poweroff
suspend
halt
reboot
```

## Diagnostic Commands

### Status Bar / Metrics

```text
status
timer
```

The top-right status bar currently shows:

```text
BAT --%  CPU NN%  RAM NN%
```

Battery remains `--%` until an ACPI battery AML evaluator or Chromebook EC battery query path is implemented.

### SMP / CPU Discovery

```text
smp
```

Current Lenovo output shows two discovered CPUs through ACPI MADT:

```text
discovered CPUs: 2
AP startup: not enabled yet
next: IDT + LAPIC + INIT/SIPI trampoline
```

### ACPI

```text
acpi
acpi tables
acpi fadt
acpi madt
acpi battery
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

### Keyboard

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

Battery device hints are detected, but real percentage is not implemented yet.

Next required work:

```text
Option A: AML interpreter enough for _BST / _BIF / _BIX
Option B: Verified Chromebook EC host-command battery query
```

### SMP

SMP currently discovers CPUs but does not start application processors.

Next required work:

```text
IDT stability
LAPIC MMIO helpers
AP trampoline below 1 MiB
INIT/SIPI/SIPI sequence
per-CPU stacks
AP idle loop
smp start command
```

### Suspend / Lid Close Wake

The `suspend` command attempts ACPI S3, but automatic lid-close/lid-open behavior is not implemented.

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

The current filesystem is a tiny built-in RAMFS. There is no persistent filesystem driver yet.

Next required work:

```text
FAT32 read support from ESP
initrd support
VFS expansion
file-backed shell commands
```

## Roadmap

### v0.0.11 — AP Startup Foundation

```text
- LAPIC MMIO helpers
- local APIC enable
- trampoline allocation below 1 MiB
- per-CPU stacks
- INIT/SIPI/SIPI sequence
- AP enters idle loop
- smp start command
```

### v0.0.12 — ACPI Battery

```text
- ACPI namespace scan improvements
- minimal AML Name/Package/Method support
- _BIF / _BIX / _BST support
- status bar battery percentage
```

### v0.0.13 — Lid Suspend/Wake

```text
- ACPI SCI
- GPE status/enable
- lid device detection
- lid close suspend trigger
- lid open wake/resume handling
```

### v0.0.14 — Persistent Storage

```text
- FAT32 reader
- load files from ESP
- initrd support
- RAMFS to VFS bridge
```

### v0.0.15 — Sage Integration

```text
- stabilize Sage bare-metal backend
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
```

## Current Best Next Step

Implement **v0.0.11 AP startup foundation**:

```text
1. Map/confirm LAPIC at 0xFEE00000
2. Add LAPIC read/write helpers
3. Add AP trampoline below 1 MiB
4. Add per-CPU stacks
5. Send INIT/SIPI/SIPI to discovered APIC IDs
6. Confirm AP enters idle loop
7. Add smp start command
```
