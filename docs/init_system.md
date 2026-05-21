# SageOS Init System (SageInit)

SageOS uses a programmable init system written in **SageLang**. This allows for flexible, scriptable system bring-up without modifying the kernel's C code for every initialization change.

## Overview

The init system is located at `/etc/init.sage` in the root filesystem. It is the first "userspace" (SageLang) code executed by the kernel after the core subsystems (VFS, Scheduler, PCI, etc.) are initialized.

## Boot Process

1. **Kernel Initialization** — The C kernel initializes hardware and core subsystems in `kmain()`:
   - Serial, console, ACPI, SMP, ATA, battery, RamFS, VFS, FAT32, BTRFS, SWAP
   - PCI bus enumeration (discovers QCA6174A Wi-Fi)
   - SDHCI, network stack (`net_init` → `qca6174_init`)
   - **Wi-Fi Auto-Connect** — kernel reads `/fat32/WIFI.CFG` and attempts to reconnect to saved credentials
   - Keyboard, status bar, dmesg (persistent load), scheduler
2. **Boot Log Ends** — `bootlog_close()` is called; `/fat32/BOOTLOG.TXT` is finalized before the scheduler starts.
3. **Init Launch** — The kernel creates the main shell thread, which immediately executes `/etc/init.sage`.
4. **Service Bring-up** — `init.sage` performs system-level tasks:
   - Displays the system banner and MOTD.
   - Checks for hardware presence (e.g., local storage).
   - Configures system settings.
   - (Future) Starts background services and daemons.
5. **Shell Hand-off** — Once `init.sage` completes, the kernel enters the interactive `sage_shell` loop.

## Configuration

You can customize the boot process by editing `sageos_build/kernel/etc/init.sage`.

### Example `init.sage`

```python
# SageOS Init System
os_set_color_hex(7995312) # Green
os_write_str("--- SageOS Booting ---\n")
os_set_color_hex(15263976) # Default

if os_path_exists("/etc/motd"):
    os_cat("/etc/motd")

os_write_str("System ready.\n")
```

## Wi-Fi Auto-Connect

At boot, after `net_init()` completes, the kernel calls `qca6174_auto_connect()`. This reads `/fat32/WIFI.CFG` (if present) and attempts a full WPA2-PSK association + DHCP cycle.

**Format of `/fat32/WIFI.CFG`:**
```
ssid=MyNetwork
pass=MyPassword
```

This file is written automatically when `wifi connect <SSID> <pass>` is run. It is stored on the FAT32 ESP partition and survives reboots.

## Internal Native APIs

The init system has access to a variety of native APIs exposed by the kernel:

| API | Description |
|---|---|
| `os_write_str(string)` | Print to console |
| `os_set_color_hex(int)` | Change console text color |
| `os_path_exists(string)` | Check if a file exists in VFS |
| `os_cat(string)` | Print file contents to console |
| `os_shell_exec(string)` | Execute a kernel shell command |

## Advantages

- **Programmable**: Logic can be as complex as needed using SageLang.
- **Decoupled**: Kernel bring-up and system configuration are separated.
- **Flexible**: Easy to add "Live" vs "Installer" detection logic.
- **Resilient**: Benefits from formal memory management and a read-write capable VFS layer.
- **Persistent Config**: Wi-Fi credentials and system settings can be stored on FAT32 and loaded at boot.
