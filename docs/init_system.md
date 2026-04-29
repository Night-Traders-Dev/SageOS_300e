# SageOS Init System (SageInit)

SageOS uses a programmable init system written in **SageLang**. This allows for flexible, scriptable system bring-up without modifying the kernel's C code for every initialization change.

## Overview

The init system is located at `/etc/init.sage` in the root filesystem. It is the first "userspace" (SageLang) code executed by the kernel after the core subsystems (VFS, Scheduler, PCI, etc.) are initialized.

## Boot Process

1.  **Kernel Initialization**: The C kernel initializes hardware and core subsystems.
2.  **Init Launch**: The kernel creates the main shell thread, which immediately executes `/etc/init.sage`.
3.  **Service Bring-up**: `init.sage` performs system-level tasks:
    *   Displays the system banner and MOTD.
    *   Checks for hardware presence (e.g., local storage).
    *   Configures system settings.
    *   (Future) Starts background services and daemons.
4.  **Shell Hand-off**: Once `init.sage` completes, the kernel enters the interactive `sage_shell` loop.

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

## Internal Native APIs

The init system has access to a variety of native APIs exposed by the kernel:

*   `os_write_str(string)`: Print to console.
*   `os_set_color_hex(int)`: Change console text color.
*   `os_path_exists(string)`: Check if a file exists in VFS.
*   `os_cat(string)`: Print file contents to console.
*   `os_shell_exec(string)`: Execute a kernel shell command.

## Advantages

*   **Programmable**: Logic can be as complex as needed using SageLang.
*   **Decoupled**: Kernel bring-up and system configuration are separated.
*   **Flexible**: Easy to add "Live" vs "Installer" detection logic.
