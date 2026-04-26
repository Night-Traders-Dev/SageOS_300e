# =============================================================================
# SageOS Shell - Input Handling
# input.sage
#
# Provides line reading with backspace support and command history.
# Calls native os_* functions registered by sage_shell_entry.c.
# =============================================================================

# History storage (up to 16 entries)
let g_history = []
let g_history_max = 16

proc history_add(line):
    if len(line) == 0:
        return nil
    # Don't add duplicate of last entry
    let hlen = len(g_history)
    if hlen > 0:
        if g_history[hlen - 1] == line:
            return nil
    if hlen >= g_history_max:
        # Shift: remove first entry
        let new_h = []
        let i = 1
        while i < hlen:
            os_array_push(new_h, g_history[i])
            i = i + 1
        g_history = new_h
    os_array_push(g_history, line)

proc history_get(n):
    # n=0 is most recent
    let hlen = len(g_history)
    if hlen == 0:
        return ""
    if n < 0:
        return ""
    if n >= hlen:
        return ""
    return g_history[hlen - 1 - n]

proc history_len():
    return len(g_history)

# ---------------------------------------------------------------------------
# read_line: Read characters from keyboard until newline.
# Supports: printable chars, backspace (8/127), Ctrl-C (3), Ctrl-U (21).
# Returns the completed line string (without newline).
# ---------------------------------------------------------------------------
proc read_line():
    let line = ""
    let done = 0
    while done == 0:
        let c = os_read_char()
        if c < 0:
            # No char available — tight poll (bare-metal)
            continue
        if c == 10:
            # Newline / Enter
            os_write_char(10)
            done = 1
        elif c == 13:
            # Carriage return (some keyboards)
            os_write_char(10)
            done = 1
        elif c == 3:
            # Ctrl-C — cancel line
            os_write_str("^C")
            os_write_char(10)
            return ""
        elif c == 21:
            # Ctrl-U — clear line (erase displayed chars)
            let llen = os_strlen(line)
            let i = 0
            while i < llen:
                os_write_char(8)
                os_write_char(32)
                os_write_char(8)
                i = i + 1
            line = ""
        elif c == 8:
            # Backspace
            let llen = os_strlen(line)
            if llen > 0:
                line = os_str_chop(line)
                os_write_char(8)
                os_write_char(32)
                os_write_char(8)
        elif c == 127:
            # Delete (some terminals send this for backspace)
            let llen = os_strlen(line)
            if llen > 0:
                line = os_str_chop(line)
                os_write_char(8)
                os_write_char(32)
                os_write_char(8)
        elif c >= 32:
            # Printable character
            line = line + os_chr(c)
            os_write_char(c)
    return line
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
# =============================================================================
# SageOS Shell - Main Shell Loop
# shell.sage
#
# Implements the main REPL: prompt, line read, command dispatch.
# Imports input.sage and commands.sage procs (linked at bytecode level).
# =============================================================================

# ---------------------------------------------------------------------------
# skip_spaces: return substring after leading spaces
# ---------------------------------------------------------------------------
proc skip_spaces(s):
    let i = 0
    let slen = os_strlen(s)
    while i < slen:
        let c = os_char_at(s, i)
        if c == 32:
            i = i + 1
        elif c == 9:
            i = i + 1
        else:
            return os_substr(s, i, slen)
    return ""

# ---------------------------------------------------------------------------
# starts_with: does `s` start with `prefix`?
# ---------------------------------------------------------------------------
proc starts_with(s, prefix):
    let slen = os_strlen(s)
    let plen = os_strlen(prefix)
    if plen > slen:
        return 0
    let i = 0
    while i < plen:
        if os_char_at(s, i) != os_char_at(prefix, i):
            return 0
        i = i + 1
    return 1

# ---------------------------------------------------------------------------
# streq: are two strings equal?
# ---------------------------------------------------------------------------
proc streq(a, b):
    return os_streq(a, b)

# ---------------------------------------------------------------------------
# arg_after: return the part of `line` after `cmd` token, with spaces stripped
# ---------------------------------------------------------------------------
proc arg_after(line, cmd):
    let clen = os_strlen(cmd)
    let rest = os_substr(line, clen, os_strlen(line))
    return skip_spaces(rest)

# ---------------------------------------------------------------------------
# prompt: print the shell prompt in blue
# ---------------------------------------------------------------------------
proc shell_prompt():
    os_status_refresh()
    let old_color = os_get_color()
    os_set_color_hex(0x80C8FF)
    os_write_char(10)
    os_write_str("root@sageos:/# ")
    os_set_color(old_color)

# ---------------------------------------------------------------------------
# dispatch: parse a completed line and call the right command proc
# ---------------------------------------------------------------------------
proc shell_dispatch(line):
    let cmd = skip_spaces(line)
    if os_strlen(cmd) == 0:
        return nil

    if streq(cmd, "help"):
        cmd_help()
        return nil
    if streq(cmd, "clear"):
        cmd_clear()
        return nil
    if streq(cmd, "version"):
        cmd_version()
        return nil
    if streq(cmd, "uname"):
        cmd_uname()
        return nil
    if streq(cmd, "about"):
        cmd_about()
        return nil
    if streq(cmd, "neofetch"):
        cmd_neofetch()
        return nil
    if streq(cmd, "btop"):
        cmd_btop()
        return nil
    if streq(cmd, "fb"):
        cmd_fb()
        return nil
    if streq(cmd, "input"):
        cmd_input()
        return nil
    if streq(cmd, "status"):
        cmd_status()
        return nil
    if streq(cmd, "sysinfo"):
        cmd_sysinfo()
        return nil
    if streq(cmd, "timer"):
        cmd_timer()
        return nil
    if streq(cmd, "smp start"):
        cmd_smp_start()
        return nil
    if streq(cmd, "smp"):
        cmd_smp()
        return nil
    if streq(cmd, "battery"):
        cmd_battery()
        return nil
    if streq(cmd, "acpi tables"):
        cmd_acpi("tables")
        return nil
    if streq(cmd, "acpi fadt"):
        cmd_acpi("fadt")
        return nil
    if streq(cmd, "acpi madt"):
        cmd_acpi("madt")
        return nil
    if streq(cmd, "acpi lid"):
        cmd_acpi("lid")
        return nil
    if streq(cmd, "acpi battery"):
        cmd_acpi("battery")
        return nil
    if streq(cmd, "acpi"):
        cmd_acpi("")
        return nil
    if streq(cmd, "keydebug"):
        cmd_keydebug()
        return nil
    if streq(cmd, "pci"):
        cmd_pci()
        return nil
    if streq(cmd, "sdhci"):
        cmd_sdhci()
        return nil
    # Commands with arguments
    if starts_with(cmd, "echo "):
        let arg = arg_after(cmd, "echo")
        cmd_echo(arg)
        return nil
    if starts_with(cmd, "echo"):
        cmd_echo("")
        return nil
    if starts_with(cmd, "color "):
        let arg = arg_after(cmd, "color")
        cmd_color(arg)
        return nil
    if starts_with(cmd, "cat "):
        let arg = arg_after(cmd, "cat")
        cmd_cat(arg)
        return nil
    if starts_with(cmd, "mkdir "):
        let arg = arg_after(cmd, "mkdir")
        cmd_mkdir(arg)
        return nil
    if starts_with(cmd, "touch "):
        let arg = arg_after(cmd, "touch")
        cmd_touch(arg)
        return nil
    if starts_with(cmd, "rm "):
        let arg = arg_after(cmd, "rm")
        if starts_with(arg, "-rf "):
            arg = arg_after(arg, "-rf")
        if starts_with(arg, "-r "):
            arg = arg_after(arg, "-r")
        if starts_with(arg, "-f "):
            arg = arg_after(arg, "-f")
        cmd_rm(arg)
        return nil
    if starts_with(cmd, "stat "):
        let arg = arg_after(cmd, "stat")
        cmd_stat(arg)
        return nil
    if starts_with(cmd, "ls "):
        let arg = arg_after(cmd, "ls")
        cmd_ls(arg)
        return nil
    if streq(cmd, "ls"):
        cmd_ls("")
        return nil
    if starts_with(cmd, "write "):
        let rest = arg_after(cmd, "write")
        # Split path and content at the first space
        let i = 0
        let rlen = os_strlen(rest)
        while i < rlen:
            if os_char_at(rest, i) == 32:
                let path = os_substr(rest, 0, i)
                let content = os_substr(rest, i + 1, rlen)
                cmd_write(path, content)
                return nil
            i = i + 1
        # No space found, treat as path with empty content
        cmd_write(rest, "")
        return nil
    if starts_with(cmd, "execelf "):
        let arg = arg_after(cmd, "execelf")
        cmd_execelf(arg)
        return nil
    if streq(cmd, "exit") or streq(cmd, "shutdown"):
        cmd_exit()
        return nil
    if starts_with(cmd, "sage "):
        let arg = arg_after(cmd, "sage")
        os_sage_exec(arg)
        return nil
    if streq(cmd, "sage"):
        os_sage_exec("")
        return nil

    # Unknown command
    os_write_char(10)
    os_write_str("Unknown command: ")
    os_write_str(cmd)

# ---------------------------------------------------------------------------
# shell_run: main shell REPL — runs forever
# ---------------------------------------------------------------------------
proc shell_run():
    shell_prompt()
    let running = 1
    while running == 1:
        let line = read_line()
        if os_strlen(line) > 0:
            history_add(line)
            shell_dispatch(line)
        shell_prompt()

# Entry point called by sage_shell_entry.c
shell_run()
