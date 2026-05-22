#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "keyboard.h"
#include "ramfs.h"
#include "fat32.h"
#include "vfs.h"
#include "power.h"
#include "bootinfo.h"
#include "shell.h"
#include "status.h"
#include "timer.h"
#include "acpi.h"
#include "smp.h"
#include "battery.h"
#include "sysinfo.h"
#include "serial.h"
#include "pci.h"
#include "sdhci.h"
#include "elf.h"
#include "version.h"
#include "dmesg.h"
#include "scheduler.h"
#include "net.h"
#include "wifi_qca6174.h"
#include "sage_shell_entry.h"
#include "sage_libc_shim.h"

static int streq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == 0 && *b == 0;
}

static int starts_word(const char *line, const char *word) {
    while (*word) { if (*line != *word) return 0; line++; word++; }
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
static int  shell_history_nav;

static const char *const shell_commands[] = {
    "about", "acpi", "acpi battery", "acpi fadt", "acpi lid", "acpi madt",
    "acpi tables", "battery", "bmesg", "btop", "cat", "clear", "color", "cp", "dmesg", "echo",
    "execelf", "exit", "fb", "halt", "help", "hexdump", "history", "input", "install",
    "keydebug", "ls", "mkdir", "nano", "neofetch", "net", "net selftest",
    "pci", "poweroff", "pwd", "q", "reboot", "rm", "sage", "sagepkg",
    "sched", "sageshell", "sdhci", "sh", "shutdown", "smp", "smp start", "source", "stat",
    "status", "stop", "suspend", "swap", "sysinfo", "timer", "touch", "uname", "version",
    "wifi", "write",
};

#define SHELL_CMD_COUNT (sizeof(shell_commands) / sizeof(shell_commands[0]))

static int starts_with(const char *text, const char *prefix) {
    while (*prefix) { if (*text != *prefix) return 0; text++; prefix++; }
    return 1;
}

int shell_completion_count(const char *prefix) {
    int count = 0;
    if (!prefix) prefix = "";
    for (size_t i = 0; i < SHELL_CMD_COUNT; i++) {
        if (starts_with(shell_commands[i], prefix)) count++;
    }
    return count;
}

const char *shell_completion_at(const char *prefix, int index) {
    if (!prefix) prefix = "";
    for (size_t i = 0; i < SHELL_CMD_COUNT; i++) {
        if (!starts_with(shell_commands[i], prefix)) continue;
        if (index == 0) return shell_commands[i];
        index--;
    }
    return "";
}

const char *shell_completion_common_prefix(const char *prefix) {
    static char common[SHELL_LINE_MAX];
    const char *first = shell_completion_at(prefix, 0);
    if (!first || !*first) return "";

    size_t len = 0;
    while (first[len] && len + 1 < sizeof(common)) {
        common[len] = first[len];
        len++;
    }
    common[len] = 0;

    for (size_t i = 0; i < SHELL_CMD_COUNT; i++) {
        const char *cand = shell_commands[i];
        if (!starts_with(cand, prefix)) continue;
        size_t j = 0;
        while (j < len && common[j] && cand[j] == common[j]) j++;
        len = j;
        common[len] = 0;
    }

    return common;
}

const char *shell_suggestion(const char *line) {
    if (!line) line = "";
    if (!*line) return "";
    if (shell_completion_count(line) <= 0) return "";
    const char *match = shell_completion_at(line, 0);
    if (!match || !*match || streq(match, line)) return "";
    return match;
}

void shell_print_completions(const char *prefix) {
    int count = 0;
    if (!prefix) prefix = "";
    console_write("\n");
    for (size_t i = 0; i < SHELL_CMD_COUNT; i++) {
        if (!starts_with(shell_commands[i], prefix)) continue;
        console_write(shell_commands[i]);
        console_write("  ");
        count++;
    }
    if (count == 0) console_write("(no completions)");
}


static int history_physical_index(int logical) {
    return (shell_history_head - 1 - logical + SHELL_HISTORY_SIZE * 2) % SHELL_HISTORY_SIZE;
}

static void shell_save_history(const char *line) {
    size_t len = 0;
    while (line[len]) len++;
    if (!len) return;
    if (shell_history_count > 0) {
        int newest = history_physical_index(0);
        size_t i = 0;
        while (i <= len && i < SHELL_LINE_MAX) {
            if (shell_history[newest][i] != line[i]) break;
            i++;
        }
        if (i > len && shell_history[newest][i] == 0) { shell_history_nav = -1; return; }
    }
    int store = shell_history_head;
    for (size_t i = 0; i <= len && i < SHELL_LINE_MAX; i++) shell_history[store][i] = line[i];
    if (shell_history_count < SHELL_HISTORY_SIZE) shell_history_count++;
    shell_history_head = (shell_history_head + 1) % SHELL_HISTORY_SIZE;
    shell_history_nav  = -1;
}

static void shell_load_history(int nav, char *out_line, size_t *out_len) {
    if (nav < 0 || nav >= shell_history_count) { *out_len = 0; out_line[0] = 0; return; }
    int idx = history_physical_index(nav);
    size_t len = 0;
    while (len + 1 < SHELL_LINE_MAX && shell_history[idx][len]) {
        out_line[len] = shell_history[idx][len];
        len++;
    }
    out_line[len] = 0;
    *out_len = len;
}

/*
 * shell_redraw_line
 *
 * Redraws the input line on the framebuffer by setting the framebuffer
 * cursor and repainting from start_col.  Serial echo is suppressed
 * during the FB paint and then console_serial_redraw_line() is called
 * to emit the VT100 equivalent so the serial terminal stays in sync.
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

    uint32_t avail = console_cols();
    /* How many character cells remain on the prompt row */
    uint32_t max_chars = (avail > start_col) ? (avail - start_col) : 0;
    if (max_chars == 0) { if (console_has_fb()) console_set_serial_echo(saved_serial_echo); return; }

    size_t new_len = 0;
    while (line[new_len]) new_len++;
    /* Clamp both lengths to the available width */
    if (new_len  > max_chars) new_len  = max_chars;
    if (erase_len > max_chars) erase_len = max_chars;

    console_set_cursor(start_row, start_col);
    /* Write only as many characters as fit on the row */
    for (size_t i = 0; i < new_len; i++) console_putc(line[i]);

    /* Erase leftover tail if old content was longer */
    if (erase_len > new_len) {
        for (size_t i = 0; i < erase_len - new_len; i++) console_putc(' ');
    }

    /* Clamp cursor position to the row as well */
    uint32_t clamped_pos = (uint32_t)pos;
    if (clamped_pos > max_chars) clamped_pos = max_chars;
    uint32_t cursor_offset = start_col + clamped_pos;
    console_set_cursor(start_row, cursor_offset);

    if (console_has_fb()) {
        console_set_serial_echo(saved_serial_echo);
        console_serial_redraw_line(line, clamped_pos);
    }
}

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

    uint32_t end_off = start_col + (uint32_t)line_len;
    console_set_cursor(start_row + end_off / console_cols(), end_off % console_cols());

    uint32_t old_fg = console_get_fg();
    console_set_fg(0x606060);
    const char *suffix = match + typed_len;
    while (*suffix) console_putc(*suffix++);
    console_set_fg(old_fg);

    uint32_t cur_off = start_col + (uint32_t)pos;
    console_set_cursor(start_row + cur_off / console_cols(), cur_off % console_cols());

    if (console_has_fb()) console_set_serial_echo(saved_serial_echo);
}

static size_t shell_token_start(const char *line, size_t pos) {
    size_t start = pos;
    while (start > 0 && line[start-1] != ' ' && line[start-1] != '\t') start--;
    return start;
}

static void shell_tab_complete(
    char *line, size_t *len, size_t *pos,
    uint32_t *start_row, uint32_t *start_col,
    size_t displayed_len
) {
    size_t token_start = shell_token_start(line, *pos);
    size_t token_len   = *pos - token_start;
    char token[SHELL_LINE_MAX];
    for (size_t i = 0; i < token_len; i++) token[i] = line[token_start + i];
    token[token_len] = 0;

    int         match_count = 0;
    const char *first_match = NULL;
    for (size_t i = 0; i < SHELL_CMD_COUNT; i++) {
        if (!starts_with(shell_commands[i], token)) continue;
        if (!first_match) first_match = shell_commands[i];
        match_count++;
    }
    if (!first_match) return;

    if (match_count == 1) {
        size_t fill_start = token_len;
        size_t fill_len   = 0;
        while (first_match[fill_start + fill_len] && fill_start + fill_len < SHELL_LINE_MAX - 1) fill_len++;
        if (fill_len > 0) {
            shell_draw_hint(first_match, token_start + token_len, *len, *pos, *start_row, *start_col);
            if (*len + fill_len >= SHELL_LINE_MAX - 1) fill_len = SHELL_LINE_MAX - 1 - *len;
            memmove(line + token_start + token_len + fill_len,
                          line + token_start + token_len,
                          *len - token_start - token_len + 1);
            for (size_t i = 0; i < fill_len; i++)
                line[token_start + token_len + i] = first_match[token_len + i];
            *len += fill_len;
            *pos  = token_start + token_len + fill_len;
            size_t erase = displayed_len + fill_len + 2;
            shell_redraw_line(line, *pos, *start_row, *start_col, erase);
        }
        return;
    }

    size_t lcp = 0;
    { while (first_match[lcp]) lcp++;
      for (size_t i = 0; i < SHELL_CMD_COUNT; i++) {
          const char *cand = shell_commands[i];
          if (!starts_with(cand, token) || cand == first_match) continue;
          size_t j = 0;
          while (j < lcp && first_match[j] && cand[j] == first_match[j]) j++;
          lcp = j;
      }
    }
    if (lcp > token_len) {
        size_t fill_len = lcp - token_len;
        if (*len + fill_len >= SHELL_LINE_MAX - 1) fill_len = SHELL_LINE_MAX - 1 - *len;
        if (fill_len > 0) {
            memmove(line + token_start + token_len + fill_len,
                          line + token_start + token_len,
                          *len - token_start - token_len + 1);
            for (size_t i = 0; i < fill_len; i++)
                line[token_start + token_len + i] = first_match[token_len + i];
            *len += fill_len; *pos = token_start + token_len + fill_len;
        }
    }
    console_write("\n");
    for (size_t i = 0; i < SHELL_CMD_COUNT; i++) {
        if (!starts_with(shell_commands[i], token)) continue;
        console_write(shell_commands[i]); console_write("  ");
    }
    prompt();
    console_get_cursor(start_row, start_col);
    console_write(line);
    uint32_t off = *start_col + (uint32_t)(*pos);
    console_set_cursor(*start_row + off / console_cols(), off % console_cols());
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
    console_write("\n  neofetch          system information fetch");
    console_write("\n  btop              resource monitor");
    console_write("\n  version           show version");
    console_write("\n  uname             show system id");
    console_write("\n  about             project summary");
    console_write("\n  sysinfo           CPU frequency, RAM, and storage usage");
    console_write("\n  exit              exit QEMU (no-op on real hardware)");
    console_write("\n\nFilesystem:");
    console_write("\n  pwd               print working directory");
    console_write("\n  ls [path]         list directory (default: /)");
    console_write("\n  cat <path>        print file contents");
    console_write("\n  cp <src> <dst>    copy file");
    console_write("\n  mkdir <path>      create a directory");
    console_write("\n  touch <path>      create an empty file");
    console_write("\n  rm <path>         remove a file or empty directory");
    console_write("\n  stat <path>       show file/directory info");
    console_write("\n  write <path> <s>  write text to a file");
    console_write("\n  hexdump <path>    hex dump file (first 4KB)");
    console_write("\n  nano <path>       edit a text file");
    console_write("\n  sh <path>         run a shell script");
    console_write("\n  source <path>     run a shell script");
    console_write("\n  execelf <path>    execute ELF binary");
    console_write("\n\nShell editing:");
    console_write("\n  Up/Down           history navigation (newest first)");
    console_write("\n  Left/Right        cursor move");
    console_write("\n  Home/End          jump to start/end of line");
    console_write("\n  Tab               autocomplete / show completions");
    console_write("\n  Ctrl-A / Ctrl-E   jump to start / end of line");
    console_write("\n  Ctrl-K            kill to end of line");
    console_write("\n  Ctrl-U            clear entire line");
    console_write("\n  Ctrl-W            delete word backwards");
    console_write("\n  Ctrl-C            cancel current line");
    console_write("\n\nHistory:");
    console_write("\n  history           show command history list");
    console_write("\n\nDisplay:");
    console_write("\n  echo <text>       print text");
    console_write("\n  color <name>      white green amber blue red cyan purple reset");
    console_write("\n  dmesg             early kernel log");
    console_write("\n  bmesg             boot log from /fat32/BOOTLOG.TXT");
    console_write("\n  fb                framebuffer info");
    console_write("\n  input             input backend info");
    console_write("\n\nHardware & Platform:");
    console_write("\n  status            show top-bar metrics");
    console_write("\n  timer             show PIT timer info");
    console_write("\n  sched             show scheduler queues and threads");
    console_write("\n  smp               show CPU/APIC discovery");
    console_write("\n  acpi              show ACPI summary");
    console_write("\n  acpi tables       list ACPI tables");
    console_write("\n  acpi fadt         show FADT power fields");
    console_write("\n  acpi madt         show MADT/APIC fields");
    console_write("\n  battery           show battery/EC detector");
    console_write("\n  keydebug          raw keyboard scancode monitor");
    console_write("\n  pci               list PCI devices");
    console_write("\n  net               network stack and interface status");
    console_write("\n  net selftest      build sample ARP and DHCP frames");
    console_write("\n  wifi              QCA6174A Wi-Fi probe details");
    console_write("\n  sdhci             eMMC/SD controller info");
    console_write("\n  install           install to local drive");
    console_write("\n\nSageLang:");
    console_write("\n  sage              interactive SageLang REPL");
    console_write("\n  sage run <path>   execute .sage or .sgvm file");
    console_write("\n  sage reset        reset SageLang VM and heap");
    console_write("\n  sagepkg           SageOS package manager");
    console_write("\n  sageshell         launch SageShell");
    console_write("\n\nPower:");
    console_write("\n  shutdown          ACPI S5 shutdown");
    console_write("\n  poweroff          alias for shutdown");
    console_write("\n  suspend           ACPI S3 suspend");
    console_write("\n  halt              halt CPU");
    console_write("\n  reboot            reboot via i8042");
}

static void cmd_fb(void) {
    SageOSBootInfo *b = console_boot_info();
    console_write("\nFramebuffer:");
    if (!console_has_fb() || !b) { console_write("not available"); return; }
    console_write("enabled");
    console_write("\n  base: ");               console_hex64(b->framebuffer_base);
    console_write("\n  size: ");               console_hex64(b->framebuffer_size);
    console_write("\n  resolution: ");         console_u32(b->width);
    console_write("x");                        console_u32(b->height);
    console_write("\n  pixels_per_scanline: "); console_u32(b->pixels_per_scanline);
    console_write("\n  pixel_format: ");        console_u32(b->pixel_format);
}

static void cmd_color(const char *name) {
    if (streq(name, "green"))  { console_set_fg(0x79FFB0); console_write("\ncolor set to green");  return; }
    if (streq(name, "white"))  { console_set_fg(0xE8E8E8); console_write("\ncolor set to white");  return; }
    if (streq(name, "amber"))  { console_set_fg(0xFFBF40); console_write("\ncolor set to amber");  return; }
    if (streq(name, "blue"))   { console_set_fg(0x80C8FF); console_write("\ncolor set to blue");   return; }
    if (streq(name, "red"))    { console_set_fg(0xFF7070); console_write("\ncolor set to red");    return; }
    if (streq(name, "cyan"))   { console_set_fg(0x40E8E8); console_write("\ncolor set to cyan");   return; }
    if (streq(name, "purple")) { console_set_fg(0xDDA0FF); console_write("\ncolor set to purple"); return; }
    if (streq(name, "reset"))  { console_set_fg(0xE8E8E8); console_write("\ncolor reset");         return; }
    console_write("\nusage: color <white|green|amber|blue|red|cyan|purple|reset>");
}

static void cmd_history(void) {
    if (shell_history_count == 0) { console_write("\n(no history)"); return; }
    for (int i = shell_history_count - 1; i >= 0; i--) {
        int idx = history_physical_index(i);
        console_write("\n  ");
        console_u32((uint32_t)(shell_history_count - i));
        console_write("  ");
        console_write(shell_history[idx]);
    }
}

static void cmd_cp(const char *args) {
    /* cp <src> <dst> */
    const char *s = args;
    while (*s && *s != ' ') s++;
    if (!*s) { console_write("\nusage: cp <src> <dst>"); return; }
    char src[256];
    int slen = (int)(s - args);
    if (slen >= 256) slen = 255;
    for (int i = 0; i < slen; i++) src[i] = args[i];
    src[slen] = 0;
    const char *dst = skip_spaces(s);
    if (!*dst) { console_write("\nusage: cp <src> <dst>"); return; }

    /* Read source in 512-byte chunks, write to dst */
    char buf[512];
    uint64_t off = 0;
    int first = 1;
    vfs_create(dst);
    while (1) {
        int n = vfs_read(src, off, buf, 512);
        if (n <= 0) {
            if (n < 0 && first) {
                console_write("\ncp: "); console_write(src);
                console_write(": "); console_write(vfs_strerror(n));
            }
            break;
        }
        int r = vfs_write(dst, off, buf, (size_t)n);
        if (r < 0) { console_write("\ncp: write error: "); console_write(vfs_strerror(r)); break; }
        off += (uint64_t)n;
        first = 0;
    }
}

static void cmd_hexdump(const char *path) {
    if (!*path) { console_write("\nusage: hexdump <path>"); return; }
    char buf[256];
    uint64_t off = 0;
    int first = 1;
    static const char hex[] = "0123456789abcdef";
    while (1) {
        int n = vfs_read(path, off, buf, 16);
        if (n <= 0) {
            if (n < 0 && first) {
                console_write("\nhexdump: "); console_write(path);
                console_write(": "); console_write(vfs_strerror(n));
            }
            break;
        }
        first = 0;
        /* offset */
        char tmp[10];
        uint64_t o = off;
        for (int i = 7; i >= 0; i--) { tmp[i] = hex[o & 0xF]; o >>= 4; }
        tmp[8] = 0;
        console_write("\n"); console_write(tmp); console_write("  ");
        /* hex bytes */
        for (int i = 0; i < 16; i++) {
            if (i < n) {
                char hx[3];
                hx[0] = hex[((uint8_t)buf[i]) >> 4];
                hx[1] = hex[((uint8_t)buf[i]) & 0xF];
                hx[2] = 0;
                console_write(hx);
                console_putc(' ');
            } else {
                console_write("   ");
            }
            if (i == 7) console_putc(' ');
        }
        console_write(" |");
        /* ASCII */
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            console_putc((c >= 32 && c < 127) ? c : '.');
        }
        console_putc('|');
        off += (uint64_t)n;
        if (off >= 4096) { console_write("\n(output truncated at 4KB)"); break; }
    }
}

void shell_exec_command(const char *cmd) {
    cmd = skip_spaces(cmd);
    if (streq(cmd, "")) return;
    if (starts_word(cmd, "pwd"))     { console_write("\n/"); return; }
    if (starts_word(cmd, "history")) { cmd_history(); return; }
    if (starts_word(cmd, "help"))         { help(); return; }
    if (starts_word(cmd, "clear"))        { console_clear(); return; }
    if (starts_word(cmd, "btop"))         { cmd_btop(); return; }
    if (starts_word(cmd, "install"))      { cmd_install(); return; }
    if (starts_word(cmd, "fb"))           { cmd_fb(); return; }
    if (starts_word(cmd, "about"))        { console_write("\nSageOS is a small POSIX-inspired OS target."); console_write("\nCurrent phase: modular kernel and hardware diagnostics."); return; }
    if (starts_word(cmd, "input"))        { console_write("\nInput backend: "); console_write(keyboard_backend()); console_write("\nUse keydebug to inspect raw scancodes."); return; }
    if (starts_word(cmd, "status"))       { status_print(); return; }
    if (starts_word(cmd, "sysinfo"))      { sysinfo_cmd(); return; }
    if (starts_word(cmd, "timer"))        { timer_cmd_info(); return; }
    if (starts_word(cmd, "sched"))        { sched_cmd_info(); return; }
    if (starts_word(cmd, "smp start"))    { smp_boot_aps(); return; }
    if (starts_word(cmd, "smp"))          { smp_cmd_info(); return; }
    if (starts_word(cmd, "battery"))      { battery_cmd_info(); return; }
    if (starts_word(cmd, "acpi tables"))  { acpi_cmd_tables(); return; }
    if (starts_word(cmd, "acpi fadt"))    { acpi_cmd_fadt(); return; }
    if (starts_word(cmd, "acpi madt"))    { acpi_cmd_madt(); return; }
    if (starts_word(cmd, "acpi lid"))     { acpi_cmd_lid(); return; }
    if (starts_word(cmd, "acpi battery")) { acpi_cmd_battery(); return; }
    if (starts_word(cmd, "acpi"))         { acpi_cmd_summary(); return; }
    if (starts_word(cmd, "swap"))         { extern void cmd_swap(void); cmd_swap(); return; }
    if (starts_word(cmd, "keydebug"))     { keyboard_keydebug(); return; }
    if (starts_word(cmd, "pci"))          { pci_cmd_info(); return; }
    if (starts_word(cmd, "net selftest")) { net_cmd_selftest(); return; }
    if (starts_word(cmd, "net"))          { net_cmd_info(); return; }
    if (starts_word(cmd, "wifi"))         {
        const char *sub = arg_after(cmd, "wifi");
        if (starts_word(sub, "reset"))       { qca6174_cmd_reset(); return; }
        if (starts_word(sub, "upload"))      { qca6174_cmd_upload(); return; }
        if (starts_word(sub, "init-rings"))  { qca6174_cmd_init_rings(); return; }
        if (starts_word(sub, "scan"))        { qca6174_cmd_scan(); return; }
        if (starts_word(sub, "connect"))     {
            const char *args = arg_after(sub, "connect");
            char ssid[32];
            char pass[32];
            int i = 0, j = 0;
            while (args[i] && args[i] == ' ') i++;
            while (args[i] && args[i] != ' ' && j < 31) ssid[j++] = args[i++];
            ssid[j] = 0;
            while (args[i] && args[i] == ' ') i++;
            j = 0;
            while (args[i] && args[i] != ' ' && j < 31) pass[j++] = args[i++];
            pass[j] = 0;
            qca6174_cmd_connect(ssid, pass);
            return;
        }
        qca6174_cmd_info();
        return;
    }
    if (starts_word(cmd, "sdhci"))        { sdhci_cmd_info(); return; }
    if (starts_word(cmd, "exit") || streq(cmd, "q")) { power_qemu_exit(); return; }
    if (starts_with(cmd, "ls")) {
        const char *path = arg_after(cmd, "ls");
        if (!*path) path = "/";
        vfs_ls(path);
        return;
    }
    if (starts_with(cmd, "cat")) {
        const char *path = arg_after(cmd, "cat");
        if (!*path) { console_write("\nusage: cat <path>"); return; }
        /* Read in 512-byte chunks and print */
        char buf[513];
        uint64_t off = 0;
        int first = 1;
        while (1) {
            int n = vfs_read(path, off, buf, 512);
            if (n < 0) {
                if (first) { console_write("\ncat: "); console_write(path); console_write(": "); console_write(vfs_strerror(n)); }
                break;
            }
            if (n == 0) break;
            if (first) { console_write("\n"); first = 0; }
            buf[n] = 0;
            console_write(buf);
            off += (uint64_t)n;
        }
        return;
    }
    if (starts_with(cmd, "mkdir")) {
        const char *path = arg_after(cmd, "mkdir");
        if (!*path) { console_write("\nusage: mkdir <path>"); return; }
        int r = vfs_mkdir(path);
        if (r < 0) { console_write("\nmkdir: "); console_write(vfs_strerror(r)); }
        return;
    }
    if (starts_with(cmd, "touch")) {
        const char *path = arg_after(cmd, "touch");
        if (!*path) { console_write("\nusage: touch <path>"); return; }
        int r = vfs_create(path);
        if (r < 0 && r != VFS_EEXIST) { console_write("\ntouch: "); console_write(vfs_strerror(r)); }
        return;
    }
    if (starts_with(cmd, "rm")) {
        const char *path = arg_after(cmd, "rm");
        int recursive = 0;
        if (starts_word(path, "-rf")) {
            path = arg_after(path, "-rf");
            recursive = 1;
        } else if (starts_word(path, "-r")) {
            path = arg_after(path, "-r");
            recursive = 1;
        } else if (starts_word(path, "-f")) {
            path = arg_after(path, "-f");
        }
        if (!*path) { console_write("\nusage: rm <path>"); return; }
        int r = recursive ? vfs_rm_rf(path) : vfs_unlink(path);
        if (r < 0) { console_write("\nrm: "); console_write(vfs_strerror(r)); }
        return;
    }
    if (starts_word(cmd, "stat")) {
        const char *path = arg_after(cmd, "stat");
        if (!*path) { console_write("\nusage: stat <path>"); return; }
        VfsStat st;
        int r = vfs_stat(path, &st);
        if (r < 0) { console_write("\nstat: "); console_write(path); console_write(": "); console_write(vfs_strerror(r)); return; }
        console_write("\n  File: "); console_write(st.name);
        console_write("\n  Type: "); console_write(st.type == VFS_DIRECTORY ? "directory" : (st.type == VFS_SYMLINK ? "symlink" : "file"));
        console_write("\n  Size: "); console_u32((uint32_t)st.size); console_write(" B");
        return;
    }
    if (starts_with(cmd, "write")) {
        /* write <path> <content> */
        const char *rest = arg_after(cmd, "write");
        if (!*rest) { console_write("\nusage: write <path> <content>"); return; }
        /* Split path and content */
        const char *p = rest;
        while (*p && *p != ' ') p++;
        char wpath[256];
        int plen = (int)(p - rest);
        if (plen >= 256) plen = 255;
        for (int i = 0; i < plen; i++) wpath[i] = rest[i];
        wpath[plen] = 0;
        const char *content = (*p == ' ') ? p + 1 : "";
        int clen = 0; const char *c = content; while (*c) { clen++; c++; }
        /* Create if needed, then write */
        vfs_create(wpath); /* ignore EEXIST */
        int r = vfs_write(wpath, 0, content, (size_t)clen);
        if (r < 0) { console_write("\nwrite: "); console_write(vfs_strerror(r)); }
        return;
    }
    if (starts_with(cmd, "execelf")) {
        const char *path = arg_after(cmd, "execelf");
        if (!*path) { console_write("\nusage: execelf <path>"); return; }
        const char *file_data;
        uint64_t file_size = ramfs_find_size(path, &file_data);
        if (!file_data) { console_write("\nexecelf: no such file: "); console_write(path); return; }
        elf_exec(file_data, file_size);
        return;
    }
    if (starts_word(cmd, "nano")) {
        const char *path = arg_after(cmd, "nano");
        if (!*path) { console_write("\nusage: nano <path>"); return; }
        cmd_nano(path);
        return;
    }
    if (starts_word(cmd, "source")) {
        const char *path = arg_after(cmd, "source");
        if (!*path) { console_write("\nusage: source <path>"); return; }
        cmd_source(path);
        return;
    }
    if (starts_word(cmd, "sh")) {
        const char *path = arg_after(cmd, "sh");
        if (!*path) { console_write("\nusage: sh <path>"); return; }
        cmd_source(path);
        return;
    }
    if (starts_word(cmd, "sagepkg")) {
        extern void sage_run_file(const char *path);
        sage_run_file("/bin/sagepkg.sage");
        return;
    }
    if (starts_with(cmd, "sageshell")) { sage_shell_run(); return; }
    if (starts_with(cmd, "sage")) {
        const char *mod = arg_after(cmd, "sage");
        if (starts_word(mod, "run")) {
            const char *path = arg_after(mod, "run");
            if (!*path) { console_write("\nusage: sage run <path>"); return; }
            extern void sage_run_file(const char *path);
            sage_run_file(path);
            return;
        }
        if (starts_word(mod, "reset")) {
            extern void sage_repl_init(void);
            extern void sage_arena_reset(void);
            sage_repl_init();
            sage_arena_reset();
            console_write("\nSageLang VM and heap reset.");
            return;
        }
        extern void sage_execute(const char *module_name);
        sage_execute(mod);
        return;
    }
    if (starts_with(cmd, "echo"))    { console_write("\n"); console_write(arg_after(cmd, "echo")); return; }
    if (starts_word(cmd, "color"))   { cmd_color(arg_after(cmd, "color")); return; }
    if (starts_with(cmd, "cp"))      { cmd_cp(arg_after(cmd, "cp")); return; }
    if (starts_with(cmd, "hexdump")) { cmd_hexdump(arg_after(cmd, "hexdump")); return; }
    if (starts_word(cmd, "bmesg"))   { extern void cmd_bmesg(void); cmd_bmesg(); return; }
    if (starts_word(cmd, "dmesg"))   { extern void cmd_dmesg(void); cmd_dmesg(); return; }
    if (starts_word(cmd, "shutdown") || starts_word(cmd, "poweroff")) { power_shutdown(); return; }
    if (starts_word(cmd, "suspend")) { power_suspend(); return; }
    if (starts_word(cmd, "halt"))    { power_halt(); return; }
    if (starts_word(cmd, "reboot"))  { console_write("\nRebooting."); power_reboot(); return; }
    console_write("\nUnknown command: "); console_write(cmd);
}

void shell_run(void) {
    char   line[SHELL_LINE_MAX];
    size_t len = 0;
    size_t pos = 0;
    uint32_t start_row = 0;
    uint32_t start_col = 0;
    size_t displayed_len = 0;
    size_t line_max = SHELL_LINE_MAX - 1;  /* effective cap, recomputed per-prompt */

    shell_history_count = 0;
    shell_history_head  = 0;
    shell_history_nav   = -1;

    prompt();
    console_get_cursor(&start_row, &start_col);
    /* Clamp input length to the remaining columns on this row so the line
     * never wraps — the redraw logic assumes single-row input. */
    {
        uint32_t cols = console_cols();
        if (cols > start_col + 2)
            line_max = cols - start_col - 2;
        if (line_max >= SHELL_LINE_MAX) line_max = SHELL_LINE_MAX - 1;
    }
    line[0] = 0;

    for (;;) {
        KeyEvent ev;
        if (!keyboard_wait_event(&ev)) continue;
        if (!ev.pressed) continue;

        if (ev.extended) {
            switch (ev.scancode) {
            case 0x48: /* Up */
                if (shell_history_count == 0) break;
                if (shell_history_nav < 0) shell_history_nav = 0;
                else if (shell_history_nav < shell_history_count - 1) shell_history_nav++;
                shell_load_history(shell_history_nav, line, &len);
                if (len > line_max) { len = line_max; line[len] = 0; }
                pos = len;
                shell_redraw_line(line, pos, start_row, start_col, displayed_len);
                displayed_len = len;
                break;
            case 0x50: /* Down */
                if (shell_history_nav < 0) break;
                if (shell_history_nav > 0) {
                    shell_history_nav--;
                    shell_load_history(shell_history_nav, line, &len);
                    if (len > line_max) { len = line_max; line[len] = 0; }
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
                    if (console_has_fb()) {
                        serial_putc('\033'); serial_putc('['); serial_putc('D');
                    }
                }
                break;
            case 0x4D: /* Right */
                if (pos < len) {
                    pos++;
                    uint32_t off = start_col + (uint32_t)pos;
                    console_set_cursor(start_row + off / console_cols(), off % console_cols());
                    if (console_has_fb()) {
                        serial_putc('\033'); serial_putc('['); serial_putc('C');
                    }
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
                    memmove(line + pos, line + pos + 1, len - pos);
                    len--; line[len] = 0;
                    shell_redraw_line(line, pos, start_row, start_col, displayed_len);
                    displayed_len = len;
                }
                break;
            default: break;
            }
            continue;
        }

        char c = ev.ascii;

        if (c == '\r' || c == '\n') {
            line[len] = 0;
            console_write("\n");
            if (len > 0) shell_save_history(line);
            shell_exec_command(line);
            len = 0; pos = 0; line[0] = 0;
            displayed_len = 0;
            shell_history_nav = -1;
            prompt();
            console_get_cursor(&start_row, &start_col);
            /* Recompute line_max for the new prompt position */
            {
                uint32_t cols = console_cols();
                if (cols > start_col + 2)
                    line_max = cols - start_col - 2;
                if (line_max >= SHELL_LINE_MAX) line_max = SHELL_LINE_MAX - 1;
            }
            continue;
        }
        if (c == 3) { /* Ctrl-C */
            console_write("^C\n");
            len = 0; pos = 0; line[0] = 0; displayed_len = 0;
            shell_history_nav = -1;
            prompt();
            console_get_cursor(&start_row, &start_col);
            continue;
        }
        if (c == 1)  { pos = 0;   shell_redraw_line(line, pos, start_row, start_col, displayed_len); displayed_len = len; continue; } /* Ctrl-A */
        if (c == 5)  { pos = len; shell_redraw_line(line, pos, start_row, start_col, displayed_len); displayed_len = len; continue; } /* Ctrl-E */
        if (c == 11) { /* Ctrl-K: kill to end of line */
            line[pos] = 0;
            shell_redraw_line(line, pos, start_row, start_col, displayed_len);
            displayed_len = pos; len = pos;
            continue;
        }
        if (c == 21) { len = 0; pos = 0; line[0] = 0; shell_redraw_line(line, pos, start_row, start_col, displayed_len); displayed_len = 0; continue; } /* Ctrl-U */
        if (c == 23) { /* Ctrl-W: delete word backwards */
            if (pos > 0) {
                size_t new_pos = pos;
                while (new_pos > 0 && line[new_pos - 1] == ' ') new_pos--;
                while (new_pos > 0 && line[new_pos - 1] != ' ') new_pos--;
                size_t deleted = pos - new_pos;
                memmove(line + new_pos, line + pos, len - pos + 1);
                len -= deleted; pos = new_pos;
                shell_redraw_line(line, pos, start_row, start_col, displayed_len);
                displayed_len = len;
            }
            continue;
        }
        if (c == 9)  { shell_tab_complete(line, &len, &pos, &start_row, &start_col, displayed_len); displayed_len = len; continue; } /* Tab */

        if (c == 8 || c == 127) {
            if (pos > 0) {
                size_t erase = displayed_len > len ? displayed_len : len;
                memmove(line + pos - 1, line + pos, len - pos + 1);
                pos--; len--;
                shell_redraw_line(line, pos, start_row, start_col, erase);
                displayed_len = len;
            }
            continue;
        }

        if ((uint8_t)c >= 32 && (uint8_t)c <= 126 && len < line_max) {
            int append_at_end = (pos == len) && (displayed_len == len);
            memmove(line + pos + 1, line + pos, len - pos + 1);
            line[pos] = c;
            len++; pos++; line[len] = 0;
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
