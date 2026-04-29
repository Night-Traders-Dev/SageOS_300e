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
# Command implementations live in the kernel C dispatcher.
# This file remains in the SageShell compile bundle to keep the build script
# layout stable while the shell is reduced to prompt, history, and line editing.
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
        "                  "
    ]

    os_write_char(10)
    let i = 0
    while i < 12:
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
            os_write_str(" x86_64")
        elif i == 3:
            neofetch_label("Host:     ")
            os_write_str("Lenovo 300e target")
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
            os_write_str("x86_64, ")
            os_write_str(os_num_to_str(os_smp_cpu_count()))
            os_write_str(" logical CPU(s)")
        elif i == 10:
            neofetch_label("Memory:   ")
            os_write_str(os_num_to_str(os_ram_used_mb()))
            os_write_str(" MB / ")
            os_write_str(os_num_to_str(os_ram_total_mb()))
            os_write_str(" MB")
        elif i == 11:
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
