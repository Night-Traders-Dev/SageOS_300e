# SageOS Persistent USB Boot Log

> Added in **v0.1.83** to diagnose hangs on real hardware (Lenovo 300e).  
> `bmesg` shell command added to read the log in-session without removing the USB.

## Overview

SageOS writes a persistent log file — `BOOTLOG.TXT` — to the root of the EFI System
Partition (the same FAT32 volume the USB drive boots from). Every major boot stage,
from the UEFI loader through the kernel's full `kmain()` sequence, is written and
immediately flushed to disk. If the system hangs, remove the USB, mount it on any
Linux/Windows/macOS machine, and open `BOOTLOG.TXT`. The last line shows exactly
where boot stopped.

## Reading the Log In-Shell

After boot, the log can be read directly from the SageShell without removing the USB:

```
bmesg
```

This reads `/fat32/BOOTLOG.TXT` via VFS and prints it to the console, stripping `\r` for
clean display.

## How It Works

### Bootloader side (`boot/uefi_loader.c`)

1. `open_log_file()` is called before anything else in `EfiMain`.
2. It opens (or creates) `\BOOTLOG.TXT` on the ESP root using the same
   `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` handle already used to load `KERNEL.BIN`.
3. A `log_write()` helper appends ASCII text via `EFI_FILE_PROTOCOL::Write` and
   immediately calls `EFI_FILE_PROTOCOL::Flush`, ensuring FAT32 directory entries
   are committed before the next instruction.
4. The open `EFI_FILE_PROTOCOL*` and the current byte offset are stored in two new
   fields of `SageOSBootInfo`:

   ```c
   uint64_t log_file;    // EFI_FILE_PROTOCOL*, 0 if ExitBootServices was called
   uint64_t log_offset;  // next write position
   ```

### Kernel side (`kernel/drivers/bootlog.c`)

1. `bootlog_init(info)` — called first in `kmain()` — picks up `log_file` and
   `log_offset` from the boot info struct.
2. `bootlog(msg)` appends an ASCII string. Internally it calls `SetPosition` to
   the stored offset then `Write`, then `Flush` — all via the MS-ABI vtable
   pointer array of `EFI_FILE_PROTOCOL`.
3. `bootlog_hex(label, value)` appends a label and a 64-bit hex value.
4. All functions are **safe no-ops** when `log_file == 0`, which happens when
   `SAGEOS_EXIT_BOOT_SERVICES=1` (boot services were exited before the handoff).
5. `bootlog_close()` is called just before `sched_start()` — the log ends cleanly
   when the scheduler takes over.

> **Why use the vtable directly?**  
> The kernel is compiled as a freestanding ELF; it cannot include EFI headers.
> `EFI_FILE_PROTOCOL` is re-declared as a forward struct and the known UEFI spec
> slot indices (Write = 5, SetPosition = 7, Flush = 10) are accessed via
> `void **vtable = (void **)file_ptr`.

## Log Format

Each entry is a plain ASCII line terminated with `\r\n` (CRLF for FAT32/Windows
compatibility):

```
=== SageOS Boot Log ===
[BL] UEFI loader entered
[BL] GOP framebuffer base: 0x00000000C0000000
[BL] GOP width:  0x0000000000000556
[BL] GOP height: 0x0000000000000300
[BL] GOP: OK
[BL] ACPI RSDP: 0x000000007FE14000
[BL] system_table: 0x000000007FA48018
[BL] boot_services: 0x000000007FA42398
[BL] loading KERNEL.BIN
[BL] KERNEL.BIN loaded, size: 0x00000000004E2A60
[BL] backbuffer allocated at: 0x0000000070000000
[BL] memory_total:  0x000000007FFFFFFF
[BL] memory_usable: 0x0000000070000000
[BL] handing off to kernel — log continues from kernel
[KRN] kmain entered
[KRN] serial_init OK
[KRN] console_init OK
[KRN] mode: firmware-input (boot services active)
[KRN] acpi_init: start
[KRN] acpi_init: OK
[KRN] smp_init_firmware_bsp: start
[KRN] smp_init_firmware_bsp: OK
[KRN] skipping IDT/timer/IRQ (firmware input mode)
[KRN] ata_init (polling): start
[KRN] ata_init (polling): OK
[KRN] battery_init: start
[KRN] battery_init: OK
...
[KRN] net_init: start
[KRN] net_init: OK
[KRN] wifi_auto_connect: start
[KRN] wifi_auto_connect: done
[KRN] keyboard_init: start
[KRN] keyboard_init: OK
...
[KRN] sched_start - log ends here
```

**The last line written is where boot stopped.** If `[KRN] smp_init_firmware_bsp: start`
is the last entry, SMP init is the hang. If `[KRN] pci_enumerate: start` is last,
the PCI scan hung.

## Reading the Log (offline)

### Linux

```bash
# Find the USB ESP partition (usually partition 1)
lsblk /dev/sdb
sudo mount /dev/sdb1 /mnt
cat /mnt/BOOTLOG.TXT
sudo umount /mnt
```

### Windows

Open File Explorer, navigate to the USB drive, open `BOOTLOG.TXT` with Notepad.

### macOS

```bash
diskutil list          # find the disk, e.g. disk2s1
sudo mkdir /mnt/esp
sudo mount -t msdos /dev/disk2s1 /mnt/esp
cat /mnt/esp/BOOTLOG.TXT
```

## Limitations

| Condition | Behaviour |
|---|---|
| `SAGEOS_EXIT_BOOT_SERVICES=1` | Log is written up to `ExitBootServices`, then `log_file` is cleared. Kernel-side log is silent. |
| File open fails (no FAT32 ESP found) | All `bootlog()` calls are no-ops; boot continues normally. |
| System triple-faults before `Flush` returns | Last entry may be truncated. Use the previous complete line. |
| FAT32 write-protect | File open fails silently; no log is written. |

## Files

| File | Role |
|---|---|
| `boot/uefi_loader.c` | `open_log_file()`, `log_write()`, `log_hex64()`, `log_line()` helpers; full stage logging in `EfiMain` |
| `kernel/include/bootinfo.h` | `log_file`, `log_offset` fields in `SageOSBootInfo` |
| `kernel/include/bootlog.h` | `bootlog_init`, `bootlog`, `bootlog_hex`, `bootlog_close` |
| `kernel/drivers/bootlog.c` | EFI vtable access, MS-ABI, flush-on-write |
| `kernel/core/kernel.c` | `bootlog()` calls at every `init: start` / `OK` pair in `kmain`, including `wifi_auto_connect` |
| `kernel/shell/extra_cmds.c` | `cmd_bmesg()` — reads `/fat32/BOOTLOG.TXT` via VFS |
| `kernel/shell/shell.c` | `bmesg` registered as shell command |
