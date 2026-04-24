# SageOS Lenovo 300e Build

SageOS is a small POSIX-inspired educational OS target for the Lenovo 300e
Chromebook 2nd Gen AST. This bundle currently boots through a UEFI loader into
a freestanding x86_64 framebuffer kernel.

## Current Status

- Target: Lenovo 300e Chromebook 2nd Gen AST
- Boot path: x86_64 UEFI loader, GOP framebuffer handoff
- Active kernel build: `sageos_build/kernel/kernel.c` plus `entry.S`
- Pure SageLang port: started under `sage_sources/lib/os/kernel/`
- Shell: kernel-resident, command based, with fish-style line editing

## Build

SageLang is expected to be available as `sage` on `PATH`.

Build the active kernel only:

```sh
./sageos_build/build_kernel.sh
```

Build the full bootable disk image:

```sh
./scripts/build_os_unified.sh
```

The full image is written to:

```text
sageos.img
```

## Run In QEMU

```sh
./run_qemu_lenovo_300e.sh
```

Alternative QEMU helpers live in `qemu/`.

## Flash To USB

Pass the target block device explicitly:

```sh
./flash_lenovo_300e_usb.sh sageos.img /dev/sdX
```

Replace `/dev/sdX` with the real USB device. This overwrites the target device.

## Shell Controls

The active kernel shell supports:

- `Up` / `Down`: navigate command history
- `Tab` / `Right`: accept the inline autosuggestion
- `Backspace`: edit the current command
- `history`: print the in-memory command history

Autosuggestions are sourced from recent history first, then built-in command,
path, and color completions.

On hardware, keyboard input starts in auto mode. The kernel resets firmware
`ConIn` once, then accepts the first working source from UEFI firmware input or
legacy PS/2 polling. This avoids getting stuck when firmware exposes `ConIn` but
does not deliver keystrokes after handoff.

## Built-In Commands

```text
help
clear
version
uname
about
mem
fb
ls
cat <path>
echo <text>
color <white|green|amber|blue|red>
input
dmesg
history
shutdown
poweroff
suspend
fwshutdown
halt
reboot
```

## SageLang Kernel Port

The pure SageLang kernel port has begun in `sage_sources/lib/os/kernel/`.
The first active porting slice is the shell and line editor:

- `sage_sources/lib/os/kernel/shell.sage` implements the SageLang shell,
  history, and autosuggestions.
- `sage_sources/src/user/sh.sage` delegates to the shared SageLang shell entry.
- `sage_sources/lib/os/kernel/kmain.sage` remains the SageLang kernel entry
  scaffold for console, memory, interrupt, keyboard, timer, and shell startup.

The current bootable image still uses the C/ASM kernel path while the SageLang
kernel reaches hardware parity.
