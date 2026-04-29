# =============================================================================
# SageOS Shell - Main Shell Loop
# shell.sage
#
# SageShell owns prompt rendering, history, and line editing. Command execution
# is delegated to the kernel C dispatcher so command names, help text, and
# behavior have one source of truth.
# =============================================================================

proc shell_prompt():
    os_status_refresh()
    let old_color = os_get_color()
    os_set_color_hex(0x80C8FF)
    os_write_char(10)
    os_write_str("root@sageos:/# ")
    os_set_color(old_color)

proc shell_dispatch(line):
    if os_strlen(line) == 0:
        return nil
    if os_streq(line, "dmesg"):
        cmd_dmesg()
        return nil
    if os_streq(line, "neofetch"):
        cmd_neofetch()
        return nil
    os_shell_exec(line)
    return nil

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
