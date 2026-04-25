#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "keyboard.h"
#include "ramfs.h"
#include "fat32.h"
#include "power.h"
#include "bootinfo.h"
#include "shell.h"
#include "status.h"
#include "timer.h"
#include "acpi.h"
#include "smp.h"
#include "battery.h"
#include "sysinfo.h"

static int streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

static int starts_word(const char *line, const char *word) {
    while (*word) {
        if (*line != *word) return 0;
        line++; word++;
    }
    return *line == 0 || *line == ' ' || *line == '\t';
}

static const char *skip_spaces(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static const char *arg_after(const char *line, const char *cmd) {
    while (*cmd && *line == *cmd) { line++; cmd++; }
    return skip_spaces(line);
}

static void prompt(void);

#define SHELL_LINE_MAX      160
#define SHELL_HISTORY_SIZE   16

static char shell_history[SHELL_HISTORY_SIZE][SHELL_LINE_MAX];
static int  shell_history_count;
static int  shell_history_head;
static int  shell_history_nav;   /* -1 = not navigating; 0..count-1 logical newest→oldest */

static const char *const shell_commands[] = {
    "about",
    "acpi",
    "acpi battery",
    "acpi fadt",
    "acpi lid",
    "acpi madt",
    "acpi tables",
    "battery",
    "cat",
    "clear",
    "color",
    "dmesg",
    "echo",
    "fb",
    "halt",
    "help",
    "input",
    "keydebug",
    "ls",
    "poweroff",
    "reboot",
    "shutdown",
    "smp",
    "smp start",
    "status",
    "stop",
    "suspend",
    "sysinfo",
    "timer",
    "uname",
    "version",
};

#define SHELL_CMD_COUNT (sizeof(shell_commands) / sizeof(shell_commands[0]))

static int starts_with(const char *text, const char *prefix) {
    while (*prefix) {
        if (*text != *prefix) return 0;
        text++; prefix++;
    }
    return 1;
}

static void *shell_memmove(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    if (d < s)      for (size_t i = 0;   i < n; i++)   d[i]   = s[i];
    else if (d > s) for (size_t i = n; i > 0; i--)     d[i-1] = s[i-1];
    return dest;
}

/* ── History ring-buffer helpers ──────────────────────────────────────────────
 *
 * Layout: shell_history[0..SHELL_HISTORY_SIZE-1] is a circular buffer.
 * shell_history_head  = next *write* slot.
 * shell_history_count = number of valid entries (capped at SHELL_HISTORY_SIZE).
 *
 * Logical index mapping (newest = 0, oldest = count-1):
 *   physical = (head - 1 - logical + SHELL_HISTORY_SIZE * 2) % SHELL_HISTORY_SIZE
 *
 * This means shell_history_nav = 0 → most recent entry (Up pressed once).
 */
static int history_physical_index(int logical) {
    return (shell_history_head - 1 - logical + SHELL_HISTORY_SIZE * 2)
           % SHELL_HISTORY_SIZE;
}

static void shell_save_history(const char *line) {
    size_t len = 0;
    while (line[len]) len++;
    if (!len) return;

    /* Skip duplicate of the most recent entry */
    if (shell_history_count > 0) {
        int newest = history_physical_index(0);
        size_t i = 0;
        while (i <= len && i < SHELL_LINE_MAX) {
            if (shell_history[newest][i] != line[i]) break;
            i++;
        }
        if (i > len && shell_history[newest][i] == 0) {
            shell_history_nav = -1;
            return;
        }
    }

    int store = shell_history_head;
    for (size_t i = 0; i <= len && i < SHELL_LINE_MAX; i++)
        shell_history[store][i] = line[i];

    if (shell_history_count < SHELL_HISTORY_SIZE)
        shell_history_count++;

    shell_history_head = (shell_history_head + 1) % SHELL_HISTORY_SIZE;
    shell_history_nav  = -1;
}

static void shell_load_history(int nav, char *out_line, size_t *out_len) {
    if (nav < 0 || nav >= shell_history_count) {
        *out_len  = 0;
        out_line[0] = 0;
        return;
    }
    int idx = history_physical_index(nav);
    size_t len = 0;
    while (len + 1 < SHELL_LINE_MAX && shell_history[idx][len])
        out_line[len] = shell_history[idx][len++];
    out_line[len] = 0;
    *out_len = len;
}

/*
 * shell_redraw_line
 *
 * Erase [start_col .. start_col + erase_len) on start_row (wrapping
 * across rows as needed), then rewrite `line`, then place the cursor
 * at `pos` characters past start_col.
 *
 * erase_len must be >= the number of characters currently visible on
 * screen (i.e. the *previously* displayed line length, not the new one).
 * Pass max(old_displayed_len, new_len) + 1 to be safe.
 */
static void shell_redraw_line(
    const char *line,
    size_t pos,
    uint32_t start_row,
    uint32_t start_col,
    size_t erase_len
) {
    int saved_serial_echo = console_get_serial_echo();
    if (console_has_fb()) console_set_serial_echo(0);

    /* Erase the old content — go one beyond to wipe any ghost char */
    console_set_cursor(start_row, start_col);
    for (size_t i = 0; i <= erase_len; i++) console_putc(' ');

    /* Rewrite the new content */
    console_set_cursor(start_row, start_col);
    console_write(line);

    /* Reposition cursor */
    uint32_t cursor_offset = start_col + (uint32_t)pos;
    console_set_cursor(
        start_row + cursor_offset / console_cols(),
        cursor_offset % console_cols()
    );

    if (console_has_fb()) console_set_serial_echo(saved_serial_echo);
}

/*
 * shell_draw_hint
 *
 * Draw a fish-style grey ghost hint for a unique tab-completion match.
 * Shows the remaining suffix of `match` after the `typed_len` chars
 * the user already typed, in a dim colour, then restores the cursor
 * to `pos` characters past start_col.
 *
 * The hint is drawn after the line content already on screen so it
 * never disturbs the actual line buffer.
 */
static void shell_draw_hint(
    const char *match,
    size_t typed_len,
    size_t line_len,
    size_t pos,
    uint32_t start_row,
    uint32_t start_col
) {
    int saved_serial_echo = console_get_serial_echo();
    if (console_has_fb()) console_set_serial_echo(0);

    /* Position right after the current line content */
    uint32_t end_off = start_col + (uint32_t)line_len;
    console_set_cursor(
        start_row + end_off / console_cols(),
        end_off % console_cols()
    );

    /* Draw hint in dim grey */
    uint32_t old_fg = console_get_fg();
    console_set_fg(0x606060);
    const char *suffix = match + typed_len;
    while (*suffix) console_putc(*suffix++);
    console_set_fg(old_fg);

    /* Restore cursor to the actual edit position */
    uint32_t cur_off = start_col + (uint32_t)pos;
    console_set_cursor(
        start_row + cur_off / console_cols(),
        cur_off % console_cols()
    );

    if (console_has_fb()) console_set_serial_echo(saved_serial_echo);
}

static size_t shell_token_start(const char *line, size_t pos) {
    size_t start = pos;
    while (start > 0 && line[start-1] != ' ' && line[start-1] != '\t')
        start--;
    return start;
}

/*
 * shell_tab_complete
 *
 * start_row / start_col are passed as pointers so that after printing
 * candidates below the prompt and re-issuing a new prompt, the caller's
 * anchor coordinates are updated to the new prompt position.  Without
 * this, all subsequent shell_redraw_line calls use stale coordinates
 * and overdraw the wrong row.
 */
static void shell_tab_complete(
    char *line,
    size_t *len,
    size_t *pos,
    uint32_t *start_row,
    uint32_t *start_col,
    size_t displayed_len
) {
    size_t token_start = shell_token_start(line, *pos);
    size_t token_len   = *pos - token_start;
    char token[SHELL_LINE_MAX];

    for (size_t i = 0; i < token_len; i++)
        token[i] = line[token_start + i];
    token[token_len] = 0;

    int         match_count = 0;
    const char *first_match = NULL;

    /* Pass 1: count matches and record the first */
    for (size_t i = 0; i < SHELL_CMD_COUNT; i++) {
        if (!starts_with(shell_commands[i], token)) continue;
        if (!first_match) first_match = shell_commands[i];
        match_count++;
    }

    if (!first_match) return;

    if (match_count == 1) {
        /*
         * Unique match.
         *
         * 1. Draw a fish-style grey ghost hint showing the suffix that
         *    would be completed (visible before the user presses Tab
         *    again — though here we show it momentarily and then
         *    complete inline on the same Tab press, matching fish
         *    behaviour when there is only one candidate).
         * 2. Complete the remainder into the line buffer.
         * 3. Redraw with the full completed text.
         */
        size_t fill_start = token_len;
        size_t fill_len   = 0;
        while (first_match[fill_start + fill_len] &&
               fill_start + fill_len < SHELL_LINE_MAX - 1)
            fill_len++;

        if (fill_len > 0) {
            /*
             * Show the grey hint first so the user can see what will
             * be inserted.  We redraw the hint immediately then
             * complete — on a fast display this is effectively
             * instant; on a slow framebuffer it gives a brief flash.
             */
            shell_draw_hint(first_match, token_start + token_len,
                            *len, *pos, *start_row, *start_col);

            if (*len + fill_len >= SHELL_LINE_MAX - 1)
                fill_len = SHELL_LINE_MAX - 1 - *len;

            shell_memmove(
                line + token_start + token_len + fill_len,
                line + token_start + token_len,
                *len - token_start - token_len + 1
            );
            for (size_t i = 0; i < fill_len; i++)
                line[token_start + token_len + i] = first_match[token_len + i];

            *len += fill_len;
            *pos  = token_start + token_len + fill_len;

            /* erase_len must cover the hint glyphs we just drew */
            size_t erase = displayed_len + fill_len + 2;
            shell_redraw_line(line, *pos, *start_row, *start_col, erase);
        }
        return;
    }

    /*
     * Multiple matches — compute the longest common prefix across ALL
     * matching candidates, fill that much into the line, then print
     * the candidates on the next line and re-issue the prompt.
     */
    size_t lcp = 0;
    {
        while (first_match[lcp]) lcp++;

        for (size_t i = 0; i < SHELL_CMD_COUNT; i++) {
            const char *cand = shell_commands[i];
            if (!starts_with(cand, token)) continue;
            if (cand == first_match)       continue;

            size_t j = 0;
            while (j < lcp && first_match[j] && cand[j] == first_match[j])
                j++;
            lcp = j;
        }
    }

    if (lcp > token_len) {
        size_t fill_len = lcp - token_len;
        if (*len + fill_len >= SHELL_LINE_MAX - 1)
            fill_len = SHELL_LINE_MAX - 1 - *len;

        if (fill_len > 0) {
            shell_memmove(
                line + token_start + token_len + fill_len,
                line + token_start + token_len,
                *len - token_start - token_len + 1
            );
            for (size_t i = 0; i < fill_len; i++)
                line[token_start + token_len + i] = first_match[token_len + i];

            *len += fill_len;
            *pos  = token_start + token_len + fill_len;
        }
    }

    /* Print candidates below the current line */
    console_write("\n");
    for (size_t i = 0; i < SHELL_CMD_COUNT; i++) {
        if (!starts_with(shell_commands[i], token)) continue;
        console_write(shell_commands[i]);
        console_write("  ");
    }

    /*
     * Re-issue the prompt and rewrite the (possibly extended) line.
     * After prompt() the cursor is at the start of the new input area;
     * we must update start_row/start_col so the caller's subsequent
     * redraws land in the right place.
     */
    prompt();
    console_get_cursor(start_row, start_col);
    console_write(line);
    /* Reposition cursor to *pos characters into the new prompt line */
    uint32_t off = *start_col + (uint32_t)(*pos);
    console_set_cursor(
        *start_row + off / console_cols(),
        off % console_cols()
    );
}

static void prompt(void) {
    status_refresh();
    uint32_t old = console_get_fg();
    console_set_fg(0x80C8FF);
    console_write("\nroot@sageos:/# ");
    console_set_fg(old);
}

static void help(void) {
    console_write("\nCommands:");
    console_write("\n  help              show this help");
    console_write("\n  clear             clear console");
    console_write("\n  version           show version");
    console_write("\n  uname             show system id");
    console_write("\n  about             project summary");
    console_write("\n  sysinfo           CPU frequency, RAM, and storage usage");
    console_write("\n\nShell editing:");
    console_write("\n  Up/Down arrows    history navigation (newest first)");
    console_write("\n  Left/Right arrows cursor move");
    console_write("\n  Home/End          jump to start/end of line");
    console_write("\n  Tab               autocomplete / show completions");
    console_write("\n  Ctrl-A/Ctrl-E     jump begin/end");
    console_write("\n  Ctrl-U            clear input line");
    console_write("\n  Ctrl-C            cancel current line");
    console_write("\n  fb                framebuffer info");
    console_write("\n  input             input backend info");
    console_write("\n  status            show top-bar metrics");
    console_write("\n  timer             show PIT timer info");
    console_write("\n  smp               show CPU/APIC discovery");
    console_write("\n  acpi              show ACPI summary");
    console_write("\n  acpi tables       list ACPI tables");
    console_write("\n  acpi fadt         show FADT power fields");
    console_write("\n  acpi madt         show MADT/APIC fields");
    console_write("\n  battery           show battery/EC detector");
    console_write("\n  keydebug          raw keyboard scancode monitor");
    console_write("\n  ls                list RAMFS and FAT32 root");
    console_write("\n  cat <path>        print RAMFS or FAT32 file");
    console_write("\n  echo <text>       print text");
    console_write("\n  color <name>      white green amber blue red");
    console_write("\n  dmesg             early log");
    console_write("\n  shutdown          ACPI S5 shutdown");
    console_write("\n  poweroff          alias for shutdown");
    console_write("\n  suspend           ACPI S3 suspend");
    console_write("\n  halt              halt CPU");
    console_write("\n  reboot            reboot via i8042");
}

static void cmd_fb(void) {
    SageOSBootInfo *b = console_boot_info();
    console_write("\nFramebuffer: ");
    if (!console_has_fb() || !b) { console_write("not available"); return; }
    console_write("enabled");
    console_write("\n  base: ");               console_hex64(b->framebuffer_base);
    console_write("\n  size: ");               console_hex64(b->framebuffer_size);
    console_write("\n  resolution: ");         console_u32(b->width);
    console_write("x");                        console_u32(b->height);
    console_write("\n  pixels_per_scanline: "); console_u32(b->pixels_per_scanline);
    console_write("\n  pixel_format: ");        console_u32(b->pixel_format);
}

static void cmd_dmesg(void) {
    console_write("\n[0.000000] SageOS modular kernel entered");
    console_write("\n[0.000001] serial initialized");
    console_write("\n[0.000002] framebuffer console initialized");
    console_write("\n[0.000003] keyboard backend: ");
    console_write(keyboard_backend());
    console_write("\n[0.000004] RAMFS mounted");
    console_write("\n[0.000005] shell started");
}

static void cmd_color(const char *name) {
    if (streq(name, "green"))  { console_set_fg(0x79FFB0); console_write("\ncolor set to green");  return; }
    if (streq(name, "white"))  { console_set_fg(0xE8E8E8); console_write("\ncolor set to white");  return; }
    if (streq(name, "amber"))  { console_set_fg(0xFFBF40); console_write("\ncolor set to amber");  return; }
    if (streq(name, "blue"))   { console_set_fg(0x80C8FF); console_write("\ncolor set to blue");   return; }
    if (streq(name, "red"))    { console_set_fg(0xFF7070); console_write("\ncolor set to red");    return; }
    console_write("\nusage: color <white|green|amber|blue|red>");
}

static void exec(const char *cmd) {
    cmd = skip_spaces(cmd);
    if (streq(cmd, "")) return;

    if (starts_word(cmd, "help"))         { help(); return; }
    if (starts_word(cmd, "clear"))        { console_clear(); return; }
    if (starts_word(cmd, "version"))      { console_write("\nSageOS kernel 0.1.1 modular x86_64"); return; }
    if (starts_word(cmd, "uname"))        { console_write("\nSageOS sageos 0.1.1 x86_64 lenovo_300e"); return; }
    if (starts_word(cmd, "about"))        { console_write("\nSageOS is a small POSIX-inspired OS target."); console_write("\nCurrent phase: modular kernel and hardware diagnostics."); return; }
    if (starts_word(cmd, "fb"))           { cmd_fb(); return; }
    if (starts_word(cmd, "input"))        { console_write("\nInput backend: "); console_write(keyboard_backend()); console_write("\nUse keydebug to inspect raw scancodes."); return; }
    if (starts_word(cmd, "status"))       { status_print(); return; }
    if (starts_word(cmd, "sysinfo"))      { sysinfo_cmd(); return; }
    if (starts_word(cmd, "timer"))        { timer_cmd_info(); return; }
    if (starts_word(cmd, "smp start"))    { smp_boot_aps(); return; }
    if (starts_word(cmd, "smp"))          { smp_cmd_info(); return; }
    if (starts_word(cmd, "battery"))      { battery_cmd_info(); return; }
    if (starts_word(cmd, "acpi tables"))  { acpi_cmd_tables(); return; }
    if (starts_word(cmd, "acpi fadt"))    { acpi_cmd_fadt(); return; }
    if (starts_word(cmd, "acpi madt"))    { acpi_cmd_madt(); return; }
    if (starts_word(cmd, "acpi lid"))     { acpi_cmd_lid(); return; }
    if (starts_word(cmd, "acpi battery")) { acpi_cmd_battery(); return; }
    if (starts_word(cmd, "acpi"))         { acpi_cmd_summary(); return; }
    if (starts_word(cmd, "keydebug"))     { keyboard_keydebug(); return; }

    if (starts_word(cmd, "ls")) {
        const char *path = arg_after(cmd, "ls");
        if (*path && !streq(path, "/")) { console_write("\nusage: ls [/path]"); return; }
        if (fat32_is_available()) fat32_ls();
        ramfs_ls();
        return;
    }

    if (starts_word(cmd, "cat")) {
        const char *path = arg_after(cmd, "cat");
        if (!*path) { console_write("\nusage: cat <path>"); return; }
        if (fat32_is_available() && fat32_cat(path)) return;
        const char *data = ramfs_find(path);
        if (!data) { console_write("\ncat: no such file: "); console_write(path); return; }
        console_write("\n"); console_write(data);
        return;
    }

    if (starts_word(cmd, "echo"))  { console_write("\n"); console_write(arg_after(cmd, "echo")); return; }
    if (starts_word(cmd, "color")) { cmd_color(arg_after(cmd, "color")); return; }
    if (starts_word(cmd, "dmesg")) { cmd_dmesg(); return; }

    if (starts_word(cmd, "shutdown") || starts_word(cmd, "poweroff")) { power_shutdown_stub(); return; }
    if (starts_word(cmd, "suspend")) { power_suspend_stub(); return; }
    if (starts_word(cmd, "halt"))    { power_halt(); return; }
    if (starts_word(cmd, "reboot"))  { console_write("\nRebooting."); power_reboot(); return; }

    console_write("\nUnknown command: ");
    console_write(cmd);
}

void shell_run(void) {
    char   line[SHELL_LINE_MAX];
    size_t len = 0;
    size_t pos = 0;
    uint32_t start_row = 0;
    uint32_t start_col = 0;

    /*
     * displayed_len tracks how many characters are *currently visible*
     * on screen past start_col.  This is NOT always equal to len:
     * after drawing a ghost hint the visible span is len + hint_chars,
     * and after a history load the visible span is the previous entry's
     * length until the next redraw completes.  We use this value as
     * erase_len in shell_redraw_line so the clear-pass always wipes
     * exactly what was last drawn — no ghost characters left behind.
     */
    size_t displayed_len = 0;

    shell_history_count = 0;
    shell_history_head  = 0;
    shell_history_nav   = -1;

    prompt();
    console_get_cursor(&start_row, &start_col);
    line[0] = 0;

    for (;;) {
        KeyEvent ev;

        if (!keyboard_wait_event(&ev)) continue;
        if (!ev.pressed) continue;

        /* ── Extended / special keys ────────────────────────────────────── */
        if (ev.extended) {
            switch (ev.scancode) {

            case 0x48: /* Up — older entry */
                if (shell_history_count == 0) break;
                if (shell_history_nav < 0)
                    shell_history_nav = 0;
                else if (shell_history_nav < shell_history_count - 1)
                    shell_history_nav++;
                shell_load_history(shell_history_nav, line, &len);
                pos = len;
                shell_redraw_line(line, pos, start_row, start_col, displayed_len);
                displayed_len = len;
                break;

            case 0x50: /* Down — newer entry or clear */
                if (shell_history_nav < 0) break;
                if (shell_history_nav > 0) {
                    shell_history_nav--;
                    shell_load_history(shell_history_nav, line, &len);
                    pos = len;
                } else {
                    shell_history_nav = -1;
                    len = 0; pos = 0; line[0] = 0;
                }
                shell_redraw_line(line, pos, start_row, start_col, displayed_len);
                displayed_len = len;
                break;

            case 0x4B: /* Left */
                if (pos > 0) {
                    pos--;
                    uint32_t off = start_col + (uint32_t)pos;
                    console_set_cursor(start_row + off / console_cols(), off % console_cols());
                }
                break;

            case 0x4D: /* Right */
                if (pos < len) {
                    pos++;
                    uint32_t off = start_col + (uint32_t)pos;
                    console_set_cursor(start_row + off / console_cols(), off % console_cols());
                }
                break;

            case 0x47: /* Home */
                pos = 0;
                shell_redraw_line(line, pos, start_row, start_col, displayed_len);
                displayed_len = len;
                break;

            case 0x4F: /* End */
                pos = len;
                shell_redraw_line(line, pos, start_row, start_col, displayed_len);
                displayed_len = len;
                break;

            case 0x53: /* Delete */
                if (pos < len) {
                    shell_memmove(line + pos, line + pos + 1, len - pos);
                    len--;
                    line[len] = 0;
                    shell_redraw_line(line, pos, start_row, start_col, displayed_len);
                    displayed_len = len;
                }
                break;

            default: break;
            }
            continue;
        }

        /* ── ASCII / control characters ──────────────────────────────────── */
        char c = ev.ascii;

        if (c == '\r' || c == '\n') {
            line[len] = 0;
            console_write("\n");
            if (len > 0) shell_save_history(line);
            exec(line);
            len = 0; pos = 0; line[0] = 0;
            displayed_len = 0;
            shell_history_nav = -1;
            prompt();
            console_get_cursor(&start_row, &start_col);
            continue;
        }

        if (c == 3) { /* Ctrl-C */
            console_write("^C\n");
            len = 0; pos = 0; line[0] = 0;
            displayed_len = 0;
            shell_history_nav = -1;
            prompt();
            console_get_cursor(&start_row, &start_col);
            continue;
        }

        if (c == 1) { /* Ctrl-A */
            pos = 0;
            shell_redraw_line(line, pos, start_row, start_col, displayed_len);
            displayed_len = len;
            continue;
        }
        if (c == 5) { /* Ctrl-E */
            pos = len;
            shell_redraw_line(line, pos, start_row, start_col, displayed_len);
            displayed_len = len;
            continue;
        }
        if (c == 21) { /* Ctrl-U */
            len = 0; pos = 0; line[0] = 0;
            shell_redraw_line(line, pos, start_row, start_col, displayed_len);
            displayed_len = 0;
            continue;
        }
        if (c == 9) { /* Tab */
            shell_tab_complete(line, &len, &pos, &start_row, &start_col, displayed_len);
            displayed_len = len;
            continue;
        }

        if (c == 8 || c == 127) { /* Backspace */
            if (pos > 0) {
                /*
                 * erase_len must be the OLD displayed width so the
                 * clear-pass wipes the character that just disappeared.
                 * Pass displayed_len (which includes any ghost hint
                 * from a previous Tab) to be thorough.
                 */
                size_t erase = displayed_len > len ? displayed_len : len;
                shell_memmove(line + pos - 1, line + pos, len - pos + 1);
                pos--; len--;
                shell_redraw_line(line, pos, start_row, start_col, erase);
                displayed_len = len;
            }
            continue;
        }

        if ((uint8_t)c >= 32 && (uint8_t)c <= 126 && len + 1 < sizeof(line)) {
            int append_at_end = (pos == len) && (displayed_len == len);
            shell_memmove(line + pos + 1, line + pos, len - pos + 1);
            line[pos] = c;
            len++; pos++;
            line[len] = 0;
            if (append_at_end) {
                console_putc(c);
                displayed_len = len;
            } else {
                shell_redraw_line(line, pos, start_row, start_col, displayed_len);
                displayed_len = len;
            }
        }
    }
}
