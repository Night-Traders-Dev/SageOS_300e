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
