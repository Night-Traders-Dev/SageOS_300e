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
