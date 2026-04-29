# SageOS Lenovo 300e Build

SageOS is a small x86_64 UEFI operating system bring-up project targeting the **Lenovo 300e Chromebook 2nd Gen AST**.

The kernel boots through UEFI, loads a freestanding kernel, initializes a GOP framebuffer console with graphics acceleration, runs a kernel-resident SageShell with fish-style line editing, discovers platform hardware through ACPI, and provides early diagnostics for keyboard, framebuffer, SMP, ACPI, timer, memory, and battery/EC support.

Recent updates:
- **Phase 5: Networking Groundwork** ✓:
  - **Network subsystem**: Added a kernel `net` registry for future interfaces, packet-layer structures, and IPv4 checksum helpers
  - **QCA6174A Wi-Fi probe**: Detects the Lenovo 300e Qualcomm `168c:003e` device, reads PCI revision/IRQ/capabilities, and enables MMIO + bus mastering
  - **Firmware staging checks**: Looks for ath10k assets in VFS and reports whether `firmware-6.bin` and `board-2.bin` are present
  - **New diagnostics**: `net` reports stack/interface state and `wifi` reports detailed QCA6174A bring-up status
- **Phase 4: Graphics Acceleration & SageLang Scripting** ✓:
  - **Double Buffering**: 16MB back buffer allocated by UEFI bootloader for flicker-free rendering (supports up to 2560x1600)
  - **Fast Scrolling**: Optimized `memmove` on back buffer followed by targeted `console_flip` updates
  - **Instant Clears**: `memset32` bulk operations for rapid buffer clearing
  - **SageLang Script Execution**: `sage run <path>` command executes `.sgvm bytecode` or `.sage` source files from VFS
  - **Efficient Display Updates**: `console_flip(y_start, y_end)` copies dirty regions only
  - **Performance Boost**: Scrolling is now instantaneous and flicker-free on both QEMU and hardware
- **SageShell & MetalVM**: The kernel shell has been fully ported to SageLang. It runs on the **MetalVM** bytecode interpreter, which features a 32-level call stack, per-function constant pools, and a custom binary loading format (**SGVM**) for efficient execution.
- **Unified Command Dispatch**: SageShell owns prompt, history, and line editing while delegating command execution to the kernel C dispatcher for centralized control.
- **Unified VFS & Shell Integration**: Shell communicates directly with the kernel's C-based VFS layer with support for recursive directory removal (`rm -rf`), directory creation (`mkdir`), and file operations.
- **SGVM Binary Format**: Packed binary format with function metadata, constant pools, and remapped branch offsets enabling complex multi-file SageLang applications on bare metal.
- **Battery & EC**: Stabilized CrOS EC identity checks and `BATT_FLAG` validation with real-time battery percentage display.
- **Line Editing**: Fish-style suggestions, Tab completion, history navigation (16-entry ring buffer), and full Ctrl-combo support (Ctrl-A/E/K/L/U/C).
- **Rich Shell Features**: `btop` resource monitor, `nano` text editor, shell script execution via `sh <path>` / `source <path>`, persistent shell history.
- **Keyboard & Input**: Unified UEFI ConIn, serial, and native i8042 scancode mapping with consistent special-key handling.

## Current Version

```text
SageOS v0.1.2
```

## Target Hardware

```text
Device: Lenovo 300e Chromebook 2nd Gen AST
Boot:   x86_64 UEFI
Display: UEFI GOP framebuffer
Input:  UEFI ConIn + serial by default; native i8042/PS2 in strict/fallback builds
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
| **Graphics Acceleration** (Phase 4) | **Working** — Double buffering, fast scrolling, instant clears |
| Framebuffer back buffer | Working — 16MB allocated by UEFI loader, used for all rendering |
| console_flip (dirty region sync) | Working — Copies specified scanline ranges to hardware framebuffer |
| Kernel shell | Working — **SageShell** (SageLang-based) is the default REPL |
| Shell line editing | Working — fish-style suggestions, Tab completion, cursor movement, history, and Ctrl combos |
| Shell scripting | Working — `sh <path>` / `source <path>` run line-oriented shell scripts from VFS |
| **SageLang Script Execution** (Phase 4) | **Working** — `sage run <path>` executes .sgvm bytecode or .sage source files |
| Unified build/flash tool | Working |
| Modular kernel tree | Working |
| IDT installation | Working |
| PIT timer (IRQ0) | Working |
| CPU% accounting | Working — real-time 1 s sliding window |
| Status bar | Working — persistent top-bar with battery/CPU/RAM metrics, non-blocking refresh |
| Keyboard | Working — UEFI ConIn, serial/QEMU, and native i8042; arrow/special keys unified |
| RAM status | Working — real-time used RAM tracking; includes 16MB backbuffer allocation |
| SMP | Working — INIT/SIPI sequence, per-CPU stacks, AP idle loop |
| ACPI | Working — minimal AML parser, Battery (_BST) & Lid detection |
| Battery | Working — CrOS EC LPC probed at 0x900/0x880/0x800; `BATT_FLAG` validity gate |
| VFS / FAT32 | Working — Unified VFS layer, dynamic RamFS backend (writable), read-only FAT32 boot partition |
| Networking core | Partial — interface registry, Ethernet/ARP/IPv4/UDP packet structures, checksum helpers, `net` diagnostics |
| Wi-Fi (QCA6174A) | Partial — PCI discovery, BAR/IRQ/capability probing, firmware asset detection, `wifi` diagnostics |
| Text editor | Working — `nano <path>` edits small text files in RamFS |
| Resource monitor | Working — `btop` provides real-time CPU, memory, and system metrics |
| SageLang Backend | Working — bare-metal stabilized, runtime-free modules |
| ELF Execution | Working — segment mapping, BSS, entry jump |
| MetalVM Bytecode | Working — 32-level call stack, per-function constant pools, SGVM binary format |
| SageLang Toolchain | Working — compiler/runtime hooks, bytecode emission

## Important Design Note

The current hardware bring-up path intentionally uses a freestanding C/ASM kernel instead of the Sage AOT kernel path. The older Sage AOT path hit backend/runtime issues such as unsupported codegen statements and missing Sage runtime symbols during kernel linking.

However, we are actively migrating high-level logic (like the **SageShell**) to run on top of our bare-metal **MetalVM** interpreter, bridging SageLang to C kernel APIs via a native dispatch table. Once the Sage compiler backend can reliably emit freestanding procedures/imports/runtime-free AOT code, lower-level parts of the kernel can migrate back into Sage modules.

## Directory Layout

```text
SageOS_300e/
├── lenovo_300e.sh           # Unified build/flash/qemu script
├── README.md
├── VERSION
└── sageos_build/
    ├── sage_lang/           # SageLang toolchain (submodule)
    ├── scripts/
    │   └── compile_sage_shell.sh # Bytecode compiler & SGVM packer
    ├── kernel/
    │   ├── core/
    │   │   ├── kernel.c
    │   │   └── sagelang/    # MetalVM interpreter & Sage runtime
    │   ├── drivers/         # ACPI, Battery, Framebuffer, Network, PCI, etc.
    │   ├── fs/              # VFS, FAT32, JSON, RamFS
    │   ├── include/
    │   │   ├── metal_vm.h   # VM architecture definitions
    │   │   ├── net.h        # Network device model and packet-layer types
    │   │   └── version.h    # Auto-generated from VERSION
    │   └── shell/
    │       ├── shell.c      # Legacy C shell & command dispatcher
    │       ├── sage_shell/  # SageLang shell sources (.sage)
    │       ├── sage_shell_entry.c # MetalVM bridge for SageShell
    │       └── sage_shell_bytecode.h # Embedded SGVM binary artifact
    ├── BOOTX64.EFI          # UEFI loader binary
    └── KERNEL.BIN           # Kernel binary (merged with loader in img)
```

## Code Quality Audit

A comprehensive audit was performed to identify and resolve code quality issues:

### Issues Fixed

- **Dead Code Removal**: Removed the `attic/` directory containing outdated and unused code files.
- **Duplicate Code Consolidation**: Consolidated duplicate string and memory manipulation functions across the codebase:
  - Replaced custom `vfs_strcpy` and `vfs_strcat` with standardized `sage_strcpy` and `sage_strcat` from `sage_libc_shim`.
  - Removed duplicate `shell_memmove` and `s_memmove` implementations, using the standard `memmove` from `sage_libc_shim`.
  - Added missing `sage_strcat` implementation to ensure complete libc shim coverage.
- **Security**: No buffer overflow vulnerabilities found; all string operations use bounds-checked implementations.
- **Performance**: No significant bottlenecks identified; code is optimized for kernel environment.
- **Build Validation**: All changes compile successfully and maintain functionality.

### Audit Scope

- Kernel core, drivers, filesystem, and shell components
- String handling, memory operations, and path normalization
- UEFI boot process and hardware initialization
- SageLang integration and MetalVM bytecode execution

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
> - **Headless Mode**: QEMU runs headlessly; interaction is through the serial console.
> - Battery reads `--` — QEMU exposes no real ACPI battery by default.
> - CPU% may read `0%` at an idle shell prompt — expected for a truly idle VM.
> - `Network devices: 0` is expected in the default QEMU profile; the Lenovo 300e QCA6174A Wi-Fi card is only visible on real hardware.
> - **Serial Interaction**: Shell line editing, Ctrl combinations, `btop`, and full-screen `nano` are handled directly over the serial interface.

## Graphics Performance

### Phase 4: Graphics Acceleration (v0.1.2+)

**Double Buffering**: A 16MB back buffer is allocated by the UEFI loader and used for all rendering operations. This eliminates flicker during screen updates and enables efficient partial updates at resolutions up to 2560x1600.

**Fast Scrolling**: Console scrolling now uses optimized `memmove` operations on the back buffer followed by targeted `console_flip` updates, making scrolling instantaneous even on hardware.

**Efficient Clears**: The `console_clear` operation uses `memset32` for rapid bulk buffer operations instead of cell-by-cell writes.

**Dirty Region Updates**: The `console_flip(y_start, y_end)` function copies only the specified scanline range from the back buffer to the hardware framebuffer, minimizing memory traffic.

**Shadow Buffer**: A shadow character/color buffer tracks console content for status bar rendering and efficient selective updates.

**Result**: Smooth, flicker-free console interaction on both QEMU and real hardware with no perceptible lag.

## SageShell Features

SageShell is the default interactive REPL. Its command execution is centralized in the C kernel dispatcher while the interactive line editor runs in SageLang for maintainability.

### Line Editing

```text
Up/Down          history navigation (16-entry ring buffer, newest first)
Left/Right       cursor movement; Right accepts fish-style suggestion at line end
Home/End         jump to start/end of line
Tab              autocomplete commands; show candidates on multiple matches
Backspace        delete character before cursor
Delete           delete character at cursor
Ctrl-A / Ctrl-E  jump to start/end of line
Ctrl-K           delete to end of line
Ctrl-L           clear screen and redraw prompt
Ctrl-U           clear entire line
Ctrl-C           cancel current line and return to prompt
```

History stores up to 16 entries in a ring buffer. Duplicate consecutive commands are suppressed.

### Shell Scripts

Shell scripts are plain text files with one command per line:

```text
# Comments start with #
# Empty lines are skipped

ls /
cat /note.txt
echo "Script finished"
```

Each line is sent through the same command dispatcher as interactive input, so scripts can mix file commands, diagnostics, and `sage` statements.

Execute with: `sh <path>` or `source <path>`

### SageLang Integration

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
serial → console → ACPI → SMP → IDT → PIT/IRQ → VFS → PCI → storage → net → keyboard → shell
```

## Boot Info Handoff

The UEFI loader passes a `SageOSBootInfo` structure into the kernel.

Current fields include:

```text
magic
framebuffer_base
framebuffer_size
backbuffer_address         (Phase 4: 16MB buffer for double-buffered rendering)
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

The SageShell provides a rich set of commands for system diagnostics, file operations, and scripting:

### System Information & Diagnostics
```text
help              show available commands and usage
version           print SageOS kernel version
uname             print system information (name, version, architecture)
about             print project summary
neofetch          display system information and branding
sysinfo           detailed system information and metrics
fb                framebuffer configuration details
input             display current input backend (UEFI ConIn, serial, or i8042)
status            show real-time status bar: battery, CPU%, RAM%
timer             display timer and idle accounting information
```

### Resource Monitoring
```text
btop              interactive resource monitor (press q to exit)
```

### Filesystem Operations
```text
ls [path]         list directory contents; defaults to /
cat <path>        print file contents
mkdir <path>      create a directory
touch <path>      create an empty file
rm [-rf] <path>   delete file or directory; -r for recursive, -f for force
stat <path>       show file type and size information
write <path> <s>  write text string to file
```

### Text Editing & Scripts
```text
nano <path>       edit small text files (^S to save, ^X to exit)
sh <path>         execute a shell script from file
source <path>     alias for sh <path>
```

### SageLang Execution
```text
sage <module>     execute inline SageLang code (requires sageshell)
sage run <path>   execute compiled .sgvm bytecode or .sage source file from VFS
sageshell         enter the full SageLang REPL interactive shell
```

### Hardware & Platform Diagnostics
```text
smp               show SMP status and CPU discovery
smp start         start additional CPUs (AP startup)
battery           show battery status and percentage (CrOS EC probing)
acpi              show ACPI summary
acpi tables       list detected ACPI tables (RSDP, XSDT, FADT, DSDT, MADT)
acpi fadt         show FADT (Fixed ACPI Description Table) details
acpi madt         show MADT (Multiple APIC Description Table) and CPU list
acpi lid          show Lid status
acpi battery      show ACPI battery object information
pci               enumerate PCI devices
net               show networking stack and interface status
net selftest      build sample ARP and DHCP frames for packet-layer validation
wifi              show detailed QCA6174A Wi-Fi probe state
sdhci             show SD/SDHCI controller information
keydebug          enter raw scancode inspection mode (press ESC to exit)
```

### Text & Display
```text
echo <text>       print text
color <name>      set terminal foreground color
dmesg             show kernel diagnostic messages
clear             clear the screen
```

### Power Management
```text
shutdown          initiate ACPI shutdown (S5)
poweroff          alias for shutdown
suspend           attempt ACPI suspend (S3)
halt              halt the CPU
reboot            reboot via i8042 or ACPI
exit / q          exit the shell (QEMU only)
```

### File Execution
```text
execelf <path>    load and execute an ELF binary from RamFS
install           show installation/development information
```

### SageShell Line Editing

| Key | Action |
|---|---|
| `Up` / `Down` | History navigation — newest entry first |
| `Left` / `Right` | Move cursor within line |
| `Home` / `End` | Jump to start / end of line |
| `Tab` | Autocomplete — dim grey hint on unique match; candidate list + LCP fill on multiple matches |
| `Backspace` / `Delete` | Delete character before / at cursor |
History stores up to 16 entries in a ring buffer. Duplicate consecutive commands are suppressed.

## Implementation Details & Diagnostics

### Status Bar & Metrics

The top-right status bar displays: `BAT --%  CPU NN%  RAM NN%`

Implementation features:
- **Dirty-cell shadow buffer**: Only changed cells are redrawn
- **Non-blocking refresh**: Runs at 10 Hz from keyboard idle path without interrupt-context framebuffer access
- **CPU% calculation**: 100-tick sliding window (1-second average at 100 Hz) via IRQ0 idle accounting
- **Battery display**: Real-time CrOS EC battery percentage or `--` if EC not confirmed

### Networking Bring-up

The current networking slice is device-first, not user-facing networking yet:

- `net` owns a fixed interface registry and packet-layer scaffolding
- `net selftest` builds sample ARP and DHCP discover frames in-kernel so packet construction can be verified without live Wi-Fi association
- `wifi` probes the Lenovo 300e Qualcomm QCA6174A (`168c:003e`) over PCI
- The driver reads BAR0, IRQ routing, PCI capabilities, and enables memory/bus-master access
- ARP, IPv4, UDP, and DHCP discover frame builders are implemented; ICMP, DNS, and TCP are still pending
- Firmware assets are searched in VFS at:
  - `/ath10k/QCA6174/hw3.0/firmware-6.bin`
  - `/ath10k/QCA6174/hw3.0/board-2.bin`
  - `/fat32/ath10k/QCA6174/hw3.0/firmware-6.bin`
  - `/fat32/ath10k/QCA6174/hw3.0/board-2.bin`
  - `/firmware/ath10k/QCA6174/hw3.0/firmware-6.bin`
  - `/firmware/ath10k/QCA6174/hw3.0/board-2.bin`
  - `/fat32/firmware/ath10k/QCA6174/hw3.0/firmware-6.bin`
  - `/fat32/firmware/ath10k/QCA6174/hw3.0/board-2.bin`

Current limitation: there is no firmware upload, transmit/receive ring setup, scan, authentication, DHCP, or TCP path yet. This milestone establishes the interface model and real hardware probe path for later ath10k-style bring-up.

### Battery & EC Probing

The `battery` command probes CrOS EC via LPC memory-mapped region at candidate bases `0x900`, `0x880`, and `0x800`:

1. Confirms EC identity: Two-byte signature (`'E','C'`) at offset `0x20` (EC_MEMMAP_ID)
2. Validates data with `BATT_FLAG` bit checks: `BATT_PRESENT`, `INVALID_DATA`
3. Reads raw registers: `BATT_CAP` and `BATT_CAP_FULL` to compute percentage

Sample output (hardware with confirmed EC):
```text
EC ID bytes: 'E','C' confirmed
BATT_FLAG: 0xNN  [PRESENT] [VALID] [DISCHARGING]
percentage: 73%
```

If EC is not found at any candidate base, output shows `ID not confirmed` and the status bar displays `--`.

### SMP & CPU Discovery

The `smp` command shows:

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

`SAGEOS_FIRMWARE_I8042_FALLBACK` defaults to `0` on the Lenovo hardware build. Set it to `1` only for input diagnostics if you explicitly want the hybrid firmware+i8042 path.

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

## Current Limitations & Known Issues

### Graphics Acceleration
- ✓ **COMPLETED (Phase 4)**: Double buffering, fast scrolling, and efficient clears
- Status bar now uses optimized dirty-cell rendering with no framebuffer interference from interrupts

### SageLang Script Execution
- ✓ **COMPLETED (Phase 4)**: `sage run <path>` executes .sgvm bytecode and .sage source files
- Scripts can be embedded in RamFS, stored on FAT32, or embedded as binary artifacts

### Battery & EC
The CrOS EC LPC identity check and `BATT_FLAG` validity gate are implemented. EC base address (`0x900`, `0x880`, or `0x800`) depends on BIOS variant. 

If `battery` prints `ID not confirmed` on hardware:
- Verify EC base via `keydebug` mode or targeted port scan
- Or implement fallback paths:
  - Option A: AML interpreter for `_BST` / `_BIF` / `_BIX` ACPI methods
  - Option B: CrOS EC host command `0x10` (EC_CMD_CHARGE_STATE) via LPC host command port

### Suspend / Lid Close Wake
The `suspend` command attempts ACPI S3. S3 sleep package is parsed and PM1a_CNT write is issued, but automatic lid-close/lid-open behavior is not implemented.

To complete:
```text
ACPI SCI routing
GPE status/enable registers
LID device detection
_LID method evaluation or targeted EC query
Chromebook EC event handling
resume path cleanup
```

### Filesystem
A read-only FAT32 root filesystem is mounted from the EFI System Partition and accessible alongside the built-in RamFS.

To expand:
```text
initrd support
VFS extension to additional mount points
file-backed shell commands and modules
```

## Roadmap

### v0.1.2 — **CURRENT** (Graphics Acceleration & SageLang Scripting)

✓ Phase 4 complete:
```text
✓ Double buffering with 16MB back buffer
✓ Fast scrolling via memmove + console_flip
✓ Instant clears with memset32 optimization
✓ sage run <path> for bytecode/source execution
✓ Optimized status bar rendering
✓ Production-ready graphics performance
```

### v0.1.3 — Lid Suspend/Wake

```text
- ACPI SCI routing and GPE management
- LID device detection and _LID method evaluation
- Lid close suspend trigger and auto wake
- EC event handling for Chromebook integration
- Resume path cleanup and validation
```

### v0.1.4 — Persistent Storage Expansion

```text
- initrd support for loaded modules
- VFS expansion to additional partitions
- File-backed shell commands
- Modular filesystem driver framework
```

### v0.1.5 — SageLang Evolution (In Progress)

```text
✓ Port core shell logic to SageLang
✓ Implement MetalVM bytecode interpreter in the kernel
✓ Custom binary artifact format (SGVM) with function metadata
✓ Call stack support for non-native Sage functions
- Stabilize MetalVM heap and constant pool management
- Migrate RamFS and VFS logic to SageLang modules
- Implement SageLang-based driver framework
```

### v0.1.6 — Performance & AOT Optimization

```text
- Optimize MetalVM execution (JIT-lite or threaded interpretation)
- Restore Sage AOT path for performance-critical kernel paths
- Dynamic linking of SageLang bytecode "executables" from FAT32
- Sage-native ACPI AML parser extension
```

### Later — System Expansion

```text
- Userspace task execution with protected memory
- IPC and synchronization primitives
- Dynamic module loading and unloading
- Advanced power management and thermal control
- Multi-filesystem support and advanced VFS features
```

## Development Rules

1. Keep `lenovo_300e.sh` as the only normal build/flash entry point.
2. Keep old phase backups in `sageos_build/backups/` only; never in active compile path.
3. Add new kernel features as modules under `sageos_build/kernel/`.
4. Do not reintroduce the Sage AOT kernel path until backend support is stable.
5. Validate in QEMU before flashing to hardware.
6. Validate on the Lenovo 300e after every hardware-facing change.
7. Graphics operations use the back buffer; performance tests should verify `console_flip` behavior.
8. All shell commands must be registered in the kernel C dispatcher for consistency.

## Quick Start

### Build and test in QEMU:
```bash
./lenovo_300e.sh build
./lenovo_300e.sh qemu
```

### Flash to USB and boot on hardware:
```bash
./lenovo_300e.sh flash /dev/sdbX  # Replace sdbX with your USB device
```

### Initial hardware diagnostics:
```text
# Graphics & Performance
clear              # Verify flicker-free clearing
btop               # Check performance (CPU, RAM)

# Platform Discovery
sysinfo
smp
acpi tables
acpi fadt
acpi madt
battery
status

# Filesystem
ls /
cat /note.txt

# Input
input
keydebug
```

### Test SageLang scripting:
```bash
# In the shell, compile a test script on the host and copy to RamFS, then:
sage run /script.sgvm    # Run compiled bytecode
sage run /script.sage    # Run source (interpreted)
```

### Test shell scripting:
```bash
# Create a test script:
write /test.sh "echo test
ls /
status"

# Run it:
sh /test.sh
```

## Performance Validation

### Graphics Acceleration (Phase 4)
- **Scrolling**: Should be instantaneous; no lag or flicker
- **Status bar**: Updates smoothly at 10 Hz without affecting other operations
- **Clear performance**: `clear` command should execute in < 100ms

### QEMU Notes
```text
- Battery reads "--" because QEMU has no real ACPI battery
- CPU% may read "0%" at idle (expected for a truly idle VM)
- Shell line editing, btop, and nano work with ANSI sequences in serial
```
