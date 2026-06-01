# =============================================================================
# SageOS Shell - Input Handling
# input.sage
#
# Fish-style line editing for SageShell. The C bridge normalizes keyboard input
# across UEFI ConIn, serial/QEMU escape sequences, and native i8042.
# =============================================================================

let KEY_UP = 1001
let KEY_DOWN = 1002
let KEY_RIGHT = 1003
let KEY_LEFT = 1004
let KEY_HOME = 1005
let KEY_END = 1006
let KEY_DELETE = 1008

let g_history = []
let g_history_max = 16

proc history_add(line):
    if len(line) == 0:
        return nil
    let hlen = len(g_history)
    if hlen > 0:
        if g_history[hlen - 1] == line:
            return nil
    if hlen >= g_history_max:
        let new_h = []
        let i = 1
        while i < hlen:
            os_array_push(new_h, g_history[i])
            i = i + 1
        g_history = new_h
    os_array_push(g_history, line)

proc history_get(n):
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

proc str_insert(s, pos, ch):
    let slen = os_strlen(s)
    let left = os_substr(s, 0, pos)
    let right = os_substr(s, pos, slen)
    return left + os_chr(ch) + right

proc str_delete_before(s, pos):
    if pos <= 0:
        return s
    let slen = os_strlen(s)
    let left = os_substr(s, 0, pos - 1)
    let right = os_substr(s, pos, slen)
    return left + right

proc str_delete_at(s, pos):
    let slen = os_strlen(s)
    if pos < 0:
        return s
    if pos >= slen:
        return s
    let left = os_substr(s, 0, pos)
    let right = os_substr(s, pos + 1, slen)
    return left + right

proc token_start_at(line, pos):
    let i = pos
    while i > 0:
        let c = os_char_at(line, i - 1)
        if c == 32:
            return i
        if c == 9:
            return i
        i = i - 1
    return 0

proc history_suggestion(line):
    let hlen = history_len()
    let i = 0
    while i < hlen:
        let item = history_get(i)
        if os_strlen(item) > os_strlen(line):
            if starts_with(item, line):
                return item
        i = i + 1
    return ""

proc current_suggestion(line, pos):
    let llen = os_strlen(line)
    if pos != llen:
        return ""
    if llen == 0:
        return ""
    let h = history_suggestion(line)
    if os_strlen(h) > llen:
        return h
    let ts = token_start_at(line, pos)
    if ts == 0:
        let c = os_shell_suggestion(line)
        if os_strlen(c) > llen:
            return c
    return ""

proc display_len(line, suggestion):
    let llen = os_strlen(line)
    let slen = os_strlen(suggestion)
    if slen > llen:
        return slen
    return llen

proc redraw_line(line, pos, old_display_len):
    let suggestion = current_suggestion(line, pos)
    os_line_redraw(line, pos, old_display_len, suggestion)
    return display_len(line, suggestion)

proc accept_completion(line, pos):
    let llen = os_strlen(line)
    let ts = token_start_at(line, pos)
    if ts != 0:
        return line
    let prefix = os_substr(line, 0, pos)
    let common = os_shell_completion_common(prefix)
    if os_strlen(common) > os_strlen(prefix):
        return common + os_substr(line, pos, llen)
    let suggestion = current_suggestion(line, pos)
    if os_strlen(suggestion) > llen:
        return suggestion
    return line

proc show_completions(line, pos):
    let ts = token_start_at(line, pos)
    if ts == 0:
        let prefix = os_substr(line, 0, pos)
        os_shell_print_completions(prefix)
        shell_prompt()
        os_input_begin()
        os_line_redraw(line, pos, 0, current_suggestion(line, pos))

proc read_line():
    os_input_begin()
    let line = ""
    let pos = 0
    let displayed_len = 0
    let history_nav = -1
    let saved_line = ""
    let done = 0

    displayed_len = redraw_line(line, pos, displayed_len)

    while done == 0:
        let key = os_read_key()
        let llen = os_strlen(line)

        if key == 10:
            os_write_char(10)
            done = 1
        elif key == 13:
            os_write_char(10)
            done = 1
        elif key == 3:
            os_write_str("^C")
            os_write_char(10)
            return ""
        elif key == 1:
            pos = 0
            displayed_len = redraw_line(line, pos, displayed_len)
        elif key == 5:
            pos = os_strlen(line)
            displayed_len = redraw_line(line, pos, displayed_len)
        elif key == 11:
            line = os_substr(line, 0, pos)
            displayed_len = redraw_line(line, pos, displayed_len)
        elif key == 12:
            os_console_clear()
            shell_prompt()
            os_input_begin()
            displayed_len = redraw_line(line, pos, displayed_len)
        elif key == 21:
            line = ""
            pos = 0
            displayed_len = redraw_line(line, pos, displayed_len)
        elif key == KEY_HOME:
            pos = 0
            displayed_len = redraw_line(line, pos, displayed_len)
        elif key == KEY_END:
            pos = os_strlen(line)
            displayed_len = redraw_line(line, pos, displayed_len)
        elif key == KEY_LEFT:
            if pos > 0:
                pos = pos - 1
                displayed_len = redraw_line(line, pos, displayed_len)
        elif key == KEY_RIGHT:
            let suggestion = current_suggestion(line, pos)
            if os_strlen(suggestion) > os_strlen(line):
                line = suggestion
                pos = os_strlen(line)
                displayed_len = redraw_line(line, pos, displayed_len)
            elif pos < os_strlen(line):
                pos = pos + 1
                displayed_len = redraw_line(line, pos, displayed_len)
        elif key == KEY_UP:
            let hlen = history_len()
            if hlen > 0:
                if history_nav < 0:
                    saved_line = line
                    history_nav = 0
                elif history_nav < hlen - 1:
                    history_nav = history_nav + 1
                line = history_get(history_nav)
                pos = os_strlen(line)
                displayed_len = redraw_line(line, pos, displayed_len)
        elif key == KEY_DOWN:
            if history_nav >= 0:
                if history_nav > 0:
                    history_nav = history_nav - 1
                    line = history_get(history_nav)
                else:
                    history_nav = -1
                    line = saved_line
                pos = os_strlen(line)
                displayed_len = redraw_line(line, pos, displayed_len)
        elif key == KEY_DELETE:
            line = str_delete_at(line, pos)
            displayed_len = redraw_line(line, pos, displayed_len)
        elif key == 8:
            if pos > 0:
                line = str_delete_before(line, pos)
                pos = pos - 1
                displayed_len = redraw_line(line, pos, displayed_len)
        elif key == 127:
            if pos > 0:
                line = str_delete_before(line, pos)
                pos = pos - 1
                displayed_len = redraw_line(line, pos, displayed_len)
        elif key == 9:
            let completed = accept_completion(line, pos)
            if completed != line:
                line = completed
                pos = os_strlen(line)
                displayed_len = redraw_line(line, pos, displayed_len)
            else:
                show_completions(line, pos)
                displayed_len = display_len(line, current_suggestion(line, pos))
        elif key >= 32:
            if key <= 126:
                line = str_insert(line, pos, key)
                pos = pos + 1
                history_nav = -1
                displayed_len = redraw_line(line, pos, displayed_len)
    return line
# =============================================================================
# SageOS Shell - Ported Command Implementations
# commands.sage
# =============================================================================

proc cmd_help():
    os_write_char(10)
    os_write_str("Commands:")
    os_write_char(10)
    os_write_str("  help              show this help")
    os_write_char(10)
    os_write_str("  clear             clear console")
    os_write_char(10)
    os_write_str("  neofetch          system information fetch")
    os_write_char(10)
    os_write_str("  btop              resource monitor")
    os_write_char(10)
    os_write_str("  version           show version")
    os_write_char(10)
    os_write_str("  uname             show system id")
    os_write_char(10)
    os_write_str("  about             project summary")
    os_write_char(10)
    os_write_str("  sysinfo           CPU frequency, RAM, and storage usage")
    os_write_char(10)
    os_write_str("  exit              exit QEMU (no-op on real hardware)")
    os_write_char(10)
    os_write_char(10)
    os_write_str("Filesystem:")
    os_write_char(10)
    os_write_str("  pwd               print working directory")
    os_write_char(10)
    os_write_str("  ls [path]         list directory (default: /)")
    os_write_char(10)
    os_write_str("  cat <path>        print file contents")
    os_write_char(10)
    os_write_str("  cp <src> <dst>    copy file")
    os_write_char(10)
    os_write_str("  mkdir <path>      create a directory")
    os_write_char(10)
    os_write_str("  touch <path>      create an empty file")
    os_write_char(10)
    os_write_str("  rm <path>         remove a file or empty directory")
    os_write_char(10)
    os_write_str("  stat <path>       show file/directory info")
    os_write_char(10)
    os_write_str("  write <path> <s>  write text to a file")
    os_write_char(10)
    os_write_str("  hexdump <path>    hex dump file (first 4KB)")
    os_write_char(10)
    os_write_str("  nano <path>       edit a text file")
    os_write_char(10)
    os_write_str("  sh <path>         run a shell script")
    os_write_char(10)
    os_write_str("  source <path>     run a shell script")
    os_write_char(10)
    os_write_str("  execelf <path>    execute ELF binary")
    os_write_char(10)
    os_write_char(10)
    os_write_str("Shell editing:")
    os_write_char(10)
    os_write_str("  Up/Down           history navigation (newest first)")
    os_write_char(10)
    os_write_str("  Left/Right        cursor move")
    os_write_char(10)
    os_write_str("  Home/End          jump to start/end of line")
    os_write_char(10)
    os_write_str("  Tab               autocomplete / show completions")
    os_write_char(10)
    os_write_str("  Ctrl-A / Ctrl-E   jump to start / end of line")
    os_write_char(10)
    os_write_str("  Ctrl-K            kill to end of line")
    os_write_char(10)
    os_write_str("  Ctrl-U            clear entire line")
    os_write_char(10)
    os_write_str("  Ctrl-W            delete word backwards")
    os_write_char(10)
    os_write_str("  Ctrl-C            cancel current line")
    os_write_char(10)
    os_write_char(10)
    os_write_str("History:")
    os_write_char(10)
    os_write_str("  history           show command history list")
    os_write_char(10)
    os_write_char(10)
    os_write_str("Display:")
    os_write_char(10)
    os_write_str("  echo <text>       print text")
    os_write_char(10)
    os_write_str("  color <name>      white green amber blue red cyan purple reset")
    os_write_char(10)
    os_write_str("  dmesg             early kernel log")
    os_write_char(10)
    os_write_str("  fb                framebuffer info")
    os_write_char(10)
    os_write_str("  input             input backend info")
    os_write_char(10)
    os_write_char(10)
    os_write_str("Hardware & Platform:")
    os_write_char(10)
    os_write_str("  status            show top-bar metrics")
    os_write_char(10)
    os_write_str("  timer             show PIT timer info")
    os_write_char(10)
    os_write_str("  sched             show scheduler queues and threads")
    os_write_char(10)
    os_write_str("  smp               show CPU/APIC discovery")
    os_write_char(10)
    os_write_str("  acpi              show ACPI summary")
    os_write_char(10)
    os_write_str("  acpi tables       list ACPI tables")
    os_write_char(10)
    os_write_str("  acpi fadt         show FADT power fields")
    os_write_char(10)
    os_write_str("  acpi madt         show MADT/APIC fields")
    os_write_char(10)
    os_write_str("  battery           show battery/EC detector")
    os_write_char(10)
    os_write_str("  keydebug          raw keyboard scancode monitor")
    os_write_char(10)
    os_write_str("  pci               list PCI devices")
    os_write_char(10)
    os_write_str("  net               network stack and interface status")
    os_write_char(10)
    os_write_str("  net selftest      build sample ARP and DHCP frames")
    os_write_char(10)
    os_write_str("  wifi              QCA6174A Wi-Fi probe details")
    os_write_char(10)
    os_write_str("  sdhci             eMMC/SD controller info")
    os_write_char(10)
    os_write_str("  install           install to local drive")
    os_write_char(10)
    os_write_char(10)
    os_write_str("SageLang:")
    os_write_char(10)
    os_write_str("  sage              interactive SageLang REPL")
    os_write_char(10)
    os_write_str("  sage run <path>   execute .sage or .sgvm file")
    os_write_char(10)
    os_write_str("  sageshell         launch SageShell")
    os_write_char(10)
    os_write_char(10)
    os_write_str("Power:")
    os_write_char(10)
    os_write_str("  shutdown          ACPI S5 shutdown")
    os_write_char(10)
    os_write_str("  poweroff          alias for shutdown")
    os_write_char(10)
    os_write_str("  suspend           ACPI S3 suspend")
    os_write_char(10)
    os_write_str("  halt              halt CPU")
    os_write_char(10)
    os_write_str("  reboot            reboot via i8042")
    os_write_char(10)
end

proc cmd_about():
    os_write_char(10)
    os_write_str("SageOS is a small POSIX-inspired OS target.")
    os_write_char(10)
    os_write_str("Current phase: modular kernel and hardware diagnostics.")
    os_write_char(10)
end

proc cmd_version():
    os_write_char(10)
    os_write_str("SageOS version: ")
    os_write_str(os_version_string())
    os_write_char(10)
end

proc cmd_uname():
    os_write_char(10)
    os_write_str("SageOS ")
    os_write_str(os_version_string())
    os_write_str(" x86_64")
    os_write_char(10)
end

proc cmd_fb():
    os_write_char(10)
    os_write_str("Framebuffer: ")
    if os_fb_available() == 0.0:
        os_write_str("not available")
        os_write_char(10)
        return nil
    end
    os_write_str("enabled")
    os_write_char(10)
    os_write_str("  base: ")
    os_write_str(os_fb_base_str())
    os_write_char(10)
    os_write_str("  resolution: ")
    os_write_str(os_fb_width_str())
    os_write_str("x")
    os_write_str(os_fb_height_str())
    os_write_char(10)
    os_write_str("  pixels_per_scanline: ")
    os_write_str(os_fb_pps_str())
    os_write_char(10)
end
# =============================================================================
# SageOS dmesg implementation
# dmesg.sage
# =============================================================================

proc dmesg_dump_sage():
    let total = os_dmesg_get_total()
    let size = os_dmesg_get_size()
    let head = os_dmesg_get_head()
    
    let start = 0
    let count = 0
    
    if total < size:
        start = 0
        count = total
    else:
        start = head
        count = size
        
    let i = 0
    os_write_char(10) # Newline
    while i < count:
        let idx = (start + i) % size
        let c = os_dmesg_get_char(idx)
        if c != 0:
            os_write_char(c)
        i = i + 1

# Export for shell dispatch if needed
proc cmd_dmesg():
    dmesg_dump_sage()
    dmesg_log("Test")

# =============================================================================
# SageOS neofetch implementation
# neofetch.sage
# =============================================================================

proc neofetch_label(label):
    os_set_color_hex(0x79FFB0) # Greenish
    os_write_str(label)
    os_set_color_hex(0xE8E8E8) # White

proc neofetch_colors():
    let old = os_get_color()
    # 0x79FFB0, 0x80C8FF, 0xFFBF40, 0xFF7070, 0xDDA0FF, 0xE8E8E8
    os_set_color_hex(0x79FFB0)
    os_write_str("### ")
    os_set_color_hex(0x80C8FF)
    os_write_str("### ")
    os_set_color_hex(0xFFBF40)
    os_write_str("### ")
    os_set_color_hex(0xFF7070)
    os_write_str("### ")
    os_set_color_hex(0xDDA0FF)
    os_write_str("### ")
    os_set_color_hex(0xE8E8E8)
    os_write_str("###")
    os_set_color(old)

proc cmd_neofetch():
    let old_fg = os_get_color()
    
    let logo = [
        "      .::::.      ",
        "   .:++++++++:.   ",
        "  :+++:.  .:+++:  ",
        " /++:   SG   :++\\ ",
        "|++:  SageOS  :++|",
        " \\++:.      .:++/ ",
        "  `:++++++++++:`  ",
        "     `-::::-`     ",
        "                  ",
        "                  ",
        "                  ",
        "                  ",
        "                  "
    ]

    os_write_char(10)
    let i = 0
    while i < 13:
        os_set_color_hex(0x79FFB0)
        os_write_str(logo[i])
        os_write_str("  ")
        os_set_color_hex(0xE8E8E8)

        if i == 0:
            os_set_color_hex(0x80C8FF)
            os_write_str("root")
            os_set_color_hex(0xE8E8E8)
            os_write_str("@")
            os_set_color_hex(0x80C8FF)
            os_write_str("sageos")
        elif i == 1:
            os_write_str("-----------")
        elif i == 2:
            neofetch_label("OS:       ")
            os_write_str("SageOS ")
            os_write_str(os_version_string())
            os_write_str(" ")
            os_write_str(os_arch())
        elif i == 3:
            neofetch_label("Host:     ")
            os_write_str(os_host())
        elif i == 4:
            neofetch_label("Kernel:   ")
            os_write_str("SageOS modular kernel")
        elif i == 5:
            neofetch_label("Uptime:   ")
            os_write_str(os_uptime_str())
        elif i == 6:
            neofetch_label("Packages: ")
            os_write_str("builtins (kernel shell)")
        elif i == 7:
            neofetch_label("Shell:    ")
            os_write_str("sage-sh")
        elif i == 8:
            neofetch_label("Terminal: ")
            if os_fb_available():
                os_write_str("framebuffer + serial")
            else:
                os_write_str("serial")
        elif i == 9:
            neofetch_label("CPU:      ")
            os_write_str(os_cpu_name())
            os_write_str(", ")
            os_write_str(os_num_to_str(os_smp_cpu_count()))
            os_write_str(" logical CPU(s)")
        elif i == 10:
            neofetch_label("Memory:   ")
            os_write_str(os_num_to_str(os_ram_used_mb()))
            os_write_str(" MB / ")
            os_write_str(os_num_to_str(os_ram_total_mb()))
            os_write_str(" MB")
        elif i == 11:
            if os_swap_total_mb() > 0:
                neofetch_label("Swap:     ")
                os_write_str(os_num_to_str(os_swap_used_mb()))
                os_write_str(" MB / ")
                os_write_str(os_num_to_str(os_swap_total_mb()))
                os_write_str(" MB")
            else:
                neofetch_label("Colors:   ")
                neofetch_colors()
        elif i == 12:
            neofetch_label("Colors:   ")
            neofetch_colors()
        
        os_write_char(10)
        i = i + 1

    if os_fb_available():
        os_set_color_hex(0x79FFB0)
        os_write_str("                    Resolution: ")
        os_set_color_hex(0xE8E8E8)
        os_write_str(os_fb_width_str())
        os_write_str("x")
        os_write_str(os_fb_height_str())
        os_write_char(10)

    os_set_color(old_fg)
# SageOS Status Bar and System Metrics
# status.sage

proc status_refresh():
    let bat = os_battery_percent()
    let cpu = os_cpu_percent()
    let ram = 0

    let used_mb = os_ram_used_mb()
    let total_mb = os_ram_total_mb()
    if total_mb > 0:
        ram = (used_mb * 100) / total_mb
    end

    let bat_str = "--"
    if bat >= 0:
        bat_str = os_num_to_str(bat)
    end

    let cpu_str = "--"
    if cpu >= 0:
        cpu_str = os_num_to_str(cpu)
    end

    let ram_str = "--"
    if ram >= 0:
        ram_str = os_num_to_str(ram)
    end

    let text = "BAT " + bat_str + "%  CPU " + cpu_str + "%  RAM " + ram_str + "%"
end

proc cmd_status():
    let bat = os_battery_percent()
    let cpu = os_cpu_percent()
    let total_mb = os_ram_total_mb()
    let used_mb = os_ram_used_mb()

    os_write_str("\nStatus:")

    os_write_str("\n  battery: ")
    if bat >= 0:
        os_write_str(os_num_to_str(bat))
        os_write_str("%")
    else:
        os_write_str("unavailable until ACPI battery AML/EC query")
    end

    os_write_str("\n  cpu: ")
    if cpu >= 0:
        os_write_str(os_num_to_str(cpu))
        os_write_str("%")
    else:
        os_write_str("unavailable")
    end

    os_write_str("\n  ram: ")
    if total_mb > 0:
        let ram_pct = (used_mb * 100) / total_mb
        os_write_str(os_num_to_str(ram_pct))
        os_write_str("% reserved/used by early firmware/kernel view")
    else:
        os_write_str("unavailable, no UEFI memory summary")
    end
    os_write_str("\n")
end

# =============================================================================
# SageOS Power Management - Pure SageLang Port
# power.sage
#
# C source kept: sageos_build/kernel/drivers/power.c (unchanged, regression baseline)
#
# All hardware I/O (outb 0x501, outb 0x64, HLT loop, ACPI S3/S5) is delegated
# to the C driver via VM bindings. This module handles the interactive shell
# layer: confirmation messages, dmesg logging, and dispatch.
# =============================================================================

proc cmd_shutdown():
    os_write_str("\nRequesting ACPI S5 poweroff...")
    os_write_str("\n")
    os_shutdown()
end

proc cmd_poweroff():
    cmd_shutdown()
end

proc cmd_suspend():
    os_write_str("\nRequesting ACPI S3 suspend...")
    os_write_str("\n")
    os_suspend()
end

proc cmd_halt():
    os_write_str("\nHalting.")
    os_write_str("\n")
    os_halt()
end

proc cmd_reboot():
    os_write_str("\nRebooting via i8042...")
    os_write_str("\n")
    os_reboot()
end

proc cmd_exit():
    if os_is_qemu() != 0:
        os_write_str("\nExiting QEMU...")
        os_write_str("\n")
        os_qemu_exit()
    else:
        os_write_str("\nExit: calling ACPI S5 shutdown...")
        os_write_str("\n")
        os_shutdown()
    end
end
# =============================================================================
# SageOS Kernel Info Display — Pure SageLang Port
# kernel_info.sage
#
# C source kept: sageos_build/kernel/core/kernel.c (UNCHANGED — C bootstrap)
#
# kmain() is called from entry.S before the SageLang VM is initialised.
# It creates the VM itself via sage_init_run(). It cannot move to Sage.
#
# This module provides a Sage-native reproduction of the kernel banner and
# subsystem state, readable at any time from the interactive shell.
# It also demonstrates os_dmesg_log() — Sage writing to the kernel ring buffer.
# =============================================================================

proc cmd_kernel():
    os_dmesg_log("kernel_info: display requested from SageLang")

    os_write_str("\nSageOS Kernel Information:")
    os_write_str("\n  version:   ")
    os_write_str(os_version_string())

    os_write_str("\n  arch:      x86_64 freestanding ELF")
    os_write_str("\n  entry:     kmain() via entry.S [C bootstrap]")

    os_write_str("\n  keyboard:  ")
    os_write_str(os_input_backend())

    os_write_str("\n  cpus:      ")
    os_write_str(os_num_to_str(os_sched_cpu_count()))

    os_write_str("\n  threads:   ")
    os_write_str(os_num_to_str(os_sched_thread_count()))

    os_write_str("\n  uptime:    ")
    os_write_str(os_uptime_str())

    os_write_str("\n")
    os_write_str("\nMemory:")
    os_write_str("\n  ram used:  ")
    os_write_str(os_ram_used_str())
    os_write_str("\n  ram total: ")
    os_write_str(os_ram_total_str())

    os_write_str("\n")
    os_write_str("\nSubsystems (all initialised in kmain before Sage VM):")
    os_write_str("\n  [x] serial    [x] console   [x] acpi")
    os_write_str("\n  [x] smp       [x] battery   [x] ramfs+vfs")
    os_write_str("\n  [x] ata       [x] pci       [x] sdhci")
    os_write_str("\n  [x] net       [x] keyboard  [x] status")
    os_write_str("\n  [x] dmesg     [x] scheduler [x] sage-vm")

    os_write_str("\n")
    os_write_str("\nFramebuffer:")
    if os_fb_available() != 0:
        os_write_str("\n  base:       ")
        os_write_str(os_fb_base_str())
        os_write_str("\n  resolution: ")
        os_write_str(os_fb_width_str())
        os_write_str("x")
        os_write_str(os_fb_height_str())
    else:
        os_write_str("\n  not available (serial-only mode)")
    end

    os_write_str("\n")
    os_write_str("\nSwap:")
    if os_swap_available() != 0:
        os_write_str("\n  partition 3, 125 MiB — active")
    else:
        os_write_str("\n  not available (no ATA device)")
    end

    os_write_str("\n")
end
# =============================================================================
# SageOS Shell - Main Shell Loop
# shell.sage
#
# SageShell owns prompt rendering, history, and line editing. Command execution
# is delegated to the kernel C dispatcher so command names, help text, and
# behavior have one source of truth.
# =============================================================================

proc shell_prompt():
    status_refresh()
    let old_color = os_get_color()
    os_set_color_hex(0x80C8FF)
    os_write_char(10)
    os_write_str("root@sageos:/# ")
    os_set_color(old_color)

proc shell_dispatch(line):
    if os_strlen(line) == 0:
        return nil
    end
    if os_streq(line, "status"):
        cmd_status()
        return nil
    end
    if os_streq(line, "swap"):
        cmd_swap()
        return nil
    end
    if os_streq(line, "shutdown"):
        cmd_shutdown()
        return nil
    end
    if os_streq(line, "poweroff"):
        cmd_poweroff()
        return nil
    end
    if os_streq(line, "halt"):
        cmd_halt()
        return nil
    end
    if os_streq(line, "reboot"):
        cmd_reboot()
        return nil
    end
    if os_streq(line, "suspend"):
        cmd_suspend()
        return nil
    end
    if os_streq(line, "exit"):
        cmd_exit()
        return nil
    end
    if os_streq(line, "sched"):
        cmd_sched()
        return nil
    end
    if os_streq(line, "kernel"):
        cmd_kernel()
        return nil
    end
    if os_streq(line, "help"):
        cmd_help()
        return nil
    end
    if os_streq(line, "about"):
        cmd_about()
        return nil
    end
    if os_streq(line, "version"):
        cmd_version()
        return nil
    end
    if os_streq(line, "uname"):
        cmd_uname()
        return nil
    end
    if os_streq(line, "fb"):
        cmd_fb()
        return nil
    end
    if os_streq(line, "dmesg"):
        cmd_dmesg()
        return nil
    end
    if os_streq(line, "neofetch"):
        cmd_neofetch()
        return nil
    end
    if os_streq(line, "sysinfo"):
        os_sysinfo()
        return nil
    end
    if os_streq(line, "timer"):
        os_timer_info()
        return nil
    end
    if os_streq(line, "smp"):
        os_smp_info()
        return nil
    end
    if os_streq(line, "smp start"):
        os_smp_boot_aps()
        return nil
    end
    if os_streq(line, "battery"):
        os_battery_info()
        return nil
    end
    if os_streq(line, "pci"):
        os_pci_info()
        return nil
    end
    if os_streq(line, "sdhci"):
        os_sdhci_info()
        return nil
    end
    if os_streq(line, "acpi"):
        os_acpi_summary()
        return nil
    end
    if os_streq(line, "acpi tables"):
        os_acpi_tables()
        return nil
    end
    if os_streq(line, "acpi fadt"):
        os_acpi_fadt()
        return nil
    end
    if os_streq(line, "acpi madt"):
        os_acpi_madt()
        return nil
    end
    if os_streq(line, "acpi lid"):
        os_acpi_lid()
        return nil
    end
    if os_streq(line, "acpi battery"):
        os_acpi_battery()
        return nil
    end

    # Hot Dispatch: Check for command script in /etc/commands/
    let cmd_path = "/etc/commands/" + line + ".sage"
    if os_path_exists(cmd_path):
        os_sage_exec(cmd_path)
        return nil
    end

    let res = os_shell_exec(line)
    return res

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
