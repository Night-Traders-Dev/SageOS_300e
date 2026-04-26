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
