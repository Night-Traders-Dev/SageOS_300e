# =============================================================================
# SageOS Shell - Command Implementations
# commands.sage
#
# Each proc here implements one shell command.
# All OS interaction goes through os_* native callbacks.
# Newlines: use os_nl() or os_write_char(10) — no \n escape in SageLang.
# =============================================================================

# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

proc _print(s):
    os_write_str(s)

proc _println(s):
    os_write_str(s)
    os_write_char(10)

proc _nl():
    os_write_char(10)

# ---------------------------------------------------------------------------
# cmd_help — list all commands
# ---------------------------------------------------------------------------
proc cmd_help():
    _nl()
    _println("Commands:")
    _println("  help              show this help")
    _println("  clear             clear console")
    _println("  neofetch          system information fetch")
    _println("  btop              resource monitor (press q to quit)")
    _println("  version           show version")
    _println("  uname             show system id")
    _println("  about             project summary")
    _println("  sysinfo           CPU freq, RAM, and storage usage")
    _println("  fb                framebuffer info")
    _println("  input             input backend info")
    _println("  status            show top-bar metrics")
    _println("  timer             show PIT timer info")
    _println("  smp               show CPU/APIC discovery")
    _println("  smp start         start application processors")
    _println("  battery           show battery/EC detector")
    _println("  acpi              show ACPI summary")
    _println("  acpi tables       list ACPI tables")
    _println("  acpi fadt         show FADT power fields")
    _println("  acpi madt         show MADT/APIC fields")
    _println("  acpi lid          show ACPI lid status")
    _println("  acpi battery      show ACPI battery info")
    _println("  pci               list PCI devices")
    _println("  sdhci             eMMC/SD controller info")
    _println("  ls                list RAMFS and FAT32 root")
    _println("  cat <path>        print RAMFS or FAT32 file")
    _println("  execelf <path>    execute ELF binary")
    _println("  sage              interactive SageLang REPL")
    _println("  sage <code>       execute one Sage statement")
    _println("  echo <text>       print text")
    _println("  color <name>      white green amber blue red")
    _println("  dmesg             early kernel log")
    _println("  shutdown          ACPI S5 shutdown")
    _println("  poweroff          alias for shutdown")
    _println("  suspend           ACPI S3 suspend")
    _println("  halt              halt CPU")
    _println("  reboot            reboot via i8042")
    _println("  exit              exit QEMU (no-op on real hardware)")
    _nl()
    _println("Shell editing:")
    _println("  Backspace         delete character")
    _println("  Ctrl-U            clear input line")
    _println("  Ctrl-C            cancel current line")

# ---------------------------------------------------------------------------
# cmd_clear
# ---------------------------------------------------------------------------
proc cmd_clear():
    os_console_clear()

# ---------------------------------------------------------------------------
# cmd_version
# ---------------------------------------------------------------------------
proc cmd_version():
    _nl()
    _print("SageOS kernel ")
    _print(os_version_string())
    _println(" modular x86_64")

# ---------------------------------------------------------------------------
# cmd_uname
# ---------------------------------------------------------------------------
proc cmd_uname():
    _nl()
    _print("SageOS sageos ")
    _print(os_version_string())
    _println(" x86_64 lenovo_300e")

# ---------------------------------------------------------------------------
# cmd_about
# ---------------------------------------------------------------------------
proc cmd_about():
    _nl()
    _println("SageOS is a small POSIX-inspired OS target.")
    _println("Current phase: modular kernel and hardware diagnostics.")

# ---------------------------------------------------------------------------
# cmd_fb
# ---------------------------------------------------------------------------
proc cmd_fb():
    _nl()
    _print("Framebuffer: ")
    let fb_avail = os_fb_available()
    if fb_avail == 0:
        _println("not available")
        return nil
    _println("enabled")
    _print("  base: ")
    _println(os_fb_base_str())
    _print("  resolution: ")
    _print(os_fb_width_str())
    _print("x")
    _println(os_fb_height_str())
    _print("  pixels_per_scanline: ")
    _println(os_fb_pps_str())

# ---------------------------------------------------------------------------
# cmd_input
# ---------------------------------------------------------------------------
proc cmd_input():
    _nl()
    _print("Input backend: ")
    _println(os_input_backend())
    _println("Use keydebug to inspect raw scancodes.")

# ---------------------------------------------------------------------------
# cmd_echo
# ---------------------------------------------------------------------------
proc cmd_echo(args):
    _nl()
    _println(args)

# ---------------------------------------------------------------------------
# cmd_color
# ---------------------------------------------------------------------------
proc cmd_color(name):
    if name == "green":
        os_set_color_hex(0x79FFB0)
        _nl()
        _println("color set to green")
        return nil
    if name == "white":
        os_set_color_hex(0xE8E8E8)
        _nl()
        _println("color set to white")
        return nil
    if name == "amber":
        os_set_color_hex(0xFFBF40)
        _nl()
        _println("color set to amber")
        return nil
    if name == "blue":
        os_set_color_hex(0x80C8FF)
        _nl()
        _println("color set to blue")
        return nil
    if name == "red":
        os_set_color_hex(0xFF7070)
        _nl()
        _println("color set to red")
        return nil
    _nl()
    _println("usage: color <white|green|amber|blue|red>")

# ---------------------------------------------------------------------------
# cmd_dmesg
# ---------------------------------------------------------------------------
proc cmd_dmesg():
    os_dmesg_dump()

# ---------------------------------------------------------------------------
# cmd_status
# ---------------------------------------------------------------------------
proc cmd_status():
    os_status_print()

# ---------------------------------------------------------------------------
# cmd_sysinfo
# ---------------------------------------------------------------------------
proc cmd_sysinfo():
    os_sysinfo()

# ---------------------------------------------------------------------------
# cmd_timer
# ---------------------------------------------------------------------------
proc cmd_timer():
    os_timer_info()

# ---------------------------------------------------------------------------
# cmd_smp
# ---------------------------------------------------------------------------
proc cmd_smp():
    os_smp_info()

# ---------------------------------------------------------------------------
# cmd_smp_start
# ---------------------------------------------------------------------------
proc cmd_smp_start():
    os_smp_boot_aps()

# ---------------------------------------------------------------------------
# cmd_battery
# ---------------------------------------------------------------------------
proc cmd_battery():
    os_battery_info()

# ---------------------------------------------------------------------------
# cmd_acpi
# ---------------------------------------------------------------------------
proc cmd_acpi(sub):
    if sub == "tables":
        os_acpi_tables()
        return nil
    if sub == "fadt":
        os_acpi_fadt()
        return nil
    if sub == "madt":
        os_acpi_madt()
        return nil
    if sub == "lid":
        os_acpi_lid()
        return nil
    if sub == "battery":
        os_acpi_battery()
        return nil
    os_acpi_summary()

# ---------------------------------------------------------------------------
# cmd_pci
# ---------------------------------------------------------------------------
proc cmd_pci():
    os_pci_info()

# ---------------------------------------------------------------------------
# cmd_sdhci
# ---------------------------------------------------------------------------
proc cmd_sdhci():
    os_sdhci_info()

# ---------------------------------------------------------------------------
# cmd_ls
# ---------------------------------------------------------------------------
proc cmd_ls(path):
    os_ls(path)

# ---------------------------------------------------------------------------
# cmd_cat
# ---------------------------------------------------------------------------
proc cmd_cat(path):
    os_cat(path)

# ---------------------------------------------------------------------------
# cmd_mkdir
# ---------------------------------------------------------------------------
proc cmd_mkdir(path):
    os_mkdir(path)

# ---------------------------------------------------------------------------
# cmd_touch
# ---------------------------------------------------------------------------
proc cmd_touch(path):
    os_touch(path)

# ---------------------------------------------------------------------------
# cmd_rm
# ---------------------------------------------------------------------------
proc cmd_rm(path):
    os_rm(path)

# ---------------------------------------------------------------------------
# cmd_stat
# ---------------------------------------------------------------------------
proc cmd_stat(path):
    os_stat(path)

# ---------------------------------------------------------------------------
# cmd_write
# ---------------------------------------------------------------------------
proc cmd_write(path, content):
    os_write(path, content)

# ---------------------------------------------------------------------------
# cmd_keydebug
# ---------------------------------------------------------------------------
proc cmd_keydebug():
    os_keydebug()

# ---------------------------------------------------------------------------
# cmd_execelf
# ---------------------------------------------------------------------------
proc cmd_execelf(path):
    os_execelf(path)

proc cmd_exit():
    os_halt()

proc cmd_shutdown():
    os_halt()

# ---------------------------------------------------------------------------
# cmd_neofetch — Sage-native implementation
# ---------------------------------------------------------------------------
proc cmd_neofetch():
    _nl()
    # Logo lines (printed in green)
    let logo0 = "       _       "
    let logo1 = "     _/ \\_     "
    let logo2 = "    /     \\    "
    let logo3 = "   |  (S)  |   "
    let logo4 = "    \\_   _/    "
    let logo5 = "      \\ /      "
    let logo6 = "       |       "
    let logo7 = "               "

    os_set_color_hex(0x79FFB0)
    _print(logo0)
    _print("  ")
    os_set_color_hex(0x80C8FF)
    _print("root")
    os_set_color_hex(0xE8E8E8)
    _print("@")
    os_set_color_hex(0x80C8FF)
    _println("sageos")

    os_set_color_hex(0x79FFB0)
    _print(logo1)
    _print("  ")
    os_set_color_hex(0xE8E8E8)
    _println("-------------")

    os_set_color_hex(0x79FFB0)
    _print(logo2)
    _print("  ")
    os_set_color_hex(0x79FFB0)
    _print("OS: ")
    os_set_color_hex(0xE8E8E8)
    _println("SageOS")

    os_set_color_hex(0x79FFB0)
    _print(logo3)
    _print("  ")
    os_set_color_hex(0x79FFB0)
    _print("Kernel: ")
    os_set_color_hex(0xE8E8E8)
    _print("SageOS v")
    _println(os_version_string())

    os_set_color_hex(0x79FFB0)
    _print(logo4)
    _print("  ")
    os_set_color_hex(0x79FFB0)
    _print("Uptime: ")
    os_set_color_hex(0xE8E8E8)
    _println(os_uptime_str())

    os_set_color_hex(0x79FFB0)
    _print(logo5)
    _print("  ")
    os_set_color_hex(0x79FFB0)
    _print("Shell: ")
    os_set_color_hex(0xE8E8E8)
    _println("SageShell")

    os_set_color_hex(0x79FFB0)
    _print(logo6)
    _print("  ")
    os_set_color_hex(0x79FFB0)
    _print("Res: ")
    os_set_color_hex(0xE8E8E8)
    _print(os_fb_width_str())
    _print("x")
    _println(os_fb_height_str())

    os_set_color_hex(0x79FFB0)
    _print(logo7)
    _print("  ")
    os_set_color_hex(0x79FFB0)
    _print("Mem: ")
    os_set_color_hex(0xE8E8E8)
    _print(os_ram_used_str())
    _print(" / ")
    _println(os_ram_total_str())

    os_set_color_hex(0xE8E8E8)

# ---------------------------------------------------------------------------
# cmd_btop — resource monitor, runs until 'q' pressed
# ---------------------------------------------------------------------------
proc cmd_btop():
    os_console_clear()
    let running = 1
    while running == 1:
        os_cursor_home()
        os_set_color_hex(0x80C8FF)
        _println("=== SageOS BTOP (SageShell) - Press q to exit ===")
        os_set_color_hex(0xE8E8E8)

        _print("Uptime: ")
        _print(os_uptime_str())
        _print("    Kernel: v")
        _println(os_version_string())
        _nl()

        # CPU bar
        let cpu = os_cpu_percent()
        os_set_color_hex(0x79FFB0)
        _print("CPU Usage: [")
        os_draw_bar(cpu, 100, 30)
        _print("] ")
        _print(os_num_to_str(cpu))
        _println("%")

        # Memory bar
        let used_mb = os_ram_used_mb()
        let total_mb = os_ram_total_mb()
        os_set_color_hex(0xFFBF40)
        _print("Memory:    [")
        os_draw_bar(used_mb, total_mb, 30)
        _print("] ")
        _print(os_ram_used_str())
        _print(" / ")
        _println(os_ram_total_str())

        # Battery bar
        let bat = os_battery_percent()
        os_set_color_hex(0xFF7070)
        _print("Battery:   [")
        if bat >= 0:
            os_draw_bar(bat, 100, 30)
            _print("] ")
            _print(os_num_to_str(bat))
            _println("%")
        else:
            _println("      N/A      ] --%")

        os_set_color_hex(0xE8E8E8)
        _nl()
        _println("Tasks:")
        _println("  PID  NAME          STATUS")
        _println("  0    kernel        running")
        _println("  1    idle          waiting")
        _println("  2    sageshell     active")
        _println("  3    timer         active")
        _println("  4    status_bar    active")

        os_delay_ms(200)

        let c = os_poll_char()
        if c == 113:
            running = 0

    os_set_color_hex(0xE8E8E8)
    os_console_clear()

# ---------------------------------------------------------------------------
# cmd_shutdown / poweroff
# ---------------------------------------------------------------------------
proc cmd_shutdown():
    os_shutdown()

# ---------------------------------------------------------------------------
# cmd_suspend
# ---------------------------------------------------------------------------
proc cmd_suspend():
    os_suspend()

# ---------------------------------------------------------------------------
# cmd_halt
# ---------------------------------------------------------------------------
proc cmd_halt():
    os_halt()

# ---------------------------------------------------------------------------
# cmd_reboot
# ---------------------------------------------------------------------------
proc cmd_reboot():
    _nl()
    _println("Rebooting.")
    os_reboot()

# ---------------------------------------------------------------------------
# cmd_exit
# ---------------------------------------------------------------------------
proc cmd_exit():
    os_qemu_exit()
