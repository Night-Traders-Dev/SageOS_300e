#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "keyboard.h"
#include "ramfs.h"
#include "power.h"
#include "bootinfo.h"
#include "shell.h"
#include "status.h"
#include "timer.h"
#include "acpi.h"
#include "smp.h"
#include "battery.h"

static int streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++;
        b++;
    }

    return *a == 0 && *b == 0;
}

static int starts_word(const char *line, const char *word) {
    while (*word) {
        if (*line != *word) return 0;
        line++;
        word++;
    }

    return *line == 0 || *line == ' ' || *line == '\t';
}

static const char *skip_spaces(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static const char *arg_after(const char *line, const char *cmd) {
    while (*cmd && *line == *cmd) {
        line++;
        cmd++;
    }

    return skip_spaces(line);
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
    console_write("\n  ls                list RAMFS");
    console_write("\n  cat <path>        print RAMFS file");
    console_write("\n  echo <text>       print text");
    console_write("\n  color <name>      white green amber blue red");
    console_write("\n  dmesg             early log");
    console_write("\n  shutdown          placeholder");
    console_write("\n  poweroff          alias for shutdown");
    console_write("\n  suspend           placeholder");
    console_write("\n  halt              halt CPU");
    console_write("\n  reboot            reboot via i8042");
}

static void cmd_fb(void) {
    SageOSBootInfo *b = console_boot_info();

    console_write("\nFramebuffer: ");

    if (!console_has_fb() || !b) {
        console_write("not available");
        return;
    }

    console_write("enabled");
    console_write("\n  base: ");
    console_hex64(b->framebuffer_base);
    console_write("\n  size: ");
    console_hex64(b->framebuffer_size);
    console_write("\n  resolution: ");
    console_u32(b->width);
    console_write("x");
    console_u32(b->height);
    console_write("\n  pixels_per_scanline: ");
    console_u32(b->pixels_per_scanline);
    console_write("\n  pixel_format: ");
    console_u32(b->pixel_format);
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
    if (streq(name, "green")) {
        console_set_fg(0x79FFB0);
        console_write("\ncolor set to green");
        return;
    }

    if (streq(name, "white")) {
        console_set_fg(0xE8E8E8);
        console_write("\ncolor set to white");
        return;
    }

    if (streq(name, "amber")) {
        console_set_fg(0xFFBF40);
        console_write("\ncolor set to amber");
        return;
    }

    if (streq(name, "blue")) {
        console_set_fg(0x80C8FF);
        console_write("\ncolor set to blue");
        return;
    }

    if (streq(name, "red")) {
        console_set_fg(0xFF7070);
        console_write("\ncolor set to red");
        return;
    }

    console_write("\nusage: color <white|green|amber|blue|red>");
}

static void exec(const char *cmd) {
    cmd = skip_spaces(cmd);

    if (streq(cmd, "")) return;

    if (starts_word(cmd, "help")) {
        help();
        return;
    }

    if (starts_word(cmd, "clear")) {
        console_clear();
        return;
    }

    if (starts_word(cmd, "version")) {
        console_write("\nSageOS kernel 0.1.1 modular x86_64");
        return;
    }

    if (starts_word(cmd, "uname")) {
        console_write("\nSageOS sageos 0.1.1 x86_64 lenovo_300e");
        return;
    }

    if (starts_word(cmd, "about")) {
        console_write("\nSageOS is a small POSIX-inspired OS target.");
        console_write("\nCurrent phase: modular kernel and hardware diagnostics.");
        return;
    }

    if (starts_word(cmd, "fb")) {
        cmd_fb();
        return;
    }

    if (starts_word(cmd, "input")) {
        console_write("\nInput backend: ");
        console_write(keyboard_backend());
        console_write("\nUse keydebug to inspect raw scancodes.");
        return;
    }

    if (starts_word(cmd, "status")) {
        status_print();
        return;
    }

    if (starts_word(cmd, "timer")) {
        timer_cmd_info();
        return;
    }

    if (starts_word(cmd, "smp start")) {
        smp_boot_aps();
        return;
    }

    if (starts_word(cmd, "smp")) {
        smp_cmd_info();
        return;
    }

    if (starts_word(cmd, "battery")) {
        battery_cmd_info();
        return;
    }

    if (starts_word(cmd, "acpi tables")) {
        acpi_cmd_tables();
        return;
    }

    if (starts_word(cmd, "acpi fadt")) {
        acpi_cmd_fadt();
        return;
    }

    if (starts_word(cmd, "acpi madt")) {
        acpi_cmd_madt();
        return;
    }

    if (starts_word(cmd, "acpi lid")) {
        acpi_cmd_lid();
        return;
    }

    if (starts_word(cmd, "acpi battery")) {
        acpi_cmd_battery();
        return;
    }

    if (starts_word(cmd, "acpi")) {
        acpi_cmd_summary();
        return;
    }

    if (starts_word(cmd, "keydebug")) {
        keyboard_keydebug();
        return;
    }

    if (starts_word(cmd, "ls")) {
        ramfs_ls();
        return;
    }

    if (starts_word(cmd, "cat")) {
        const char *path = arg_after(cmd, "cat");

        if (!*path) {
            console_write("\nusage: cat <path>");
            return;
        }

        const char *data = ramfs_find(path);

        if (!data) {
            console_write("\ncat: no such file: ");
            console_write(path);
            return;
        }

        console_write("\n");
        console_write(data);
        return;
    }

    if (starts_word(cmd, "echo")) {
        console_write("\n");
        console_write(arg_after(cmd, "echo"));
        return;
    }

    if (starts_word(cmd, "color")) {
        cmd_color(arg_after(cmd, "color"));
        return;
    }

    if (starts_word(cmd, "dmesg")) {
        cmd_dmesg();
        return;
    }

    if (starts_word(cmd, "shutdown") || starts_word(cmd, "poweroff")) {
        power_shutdown_stub();
        return;
    }

    if (starts_word(cmd, "suspend")) {
        power_suspend_stub();
        return;
    }

    if (starts_word(cmd, "halt")) {
        power_halt();
        return;
    }

    if (starts_word(cmd, "reboot")) {
        console_write("\nRebooting.");
        power_reboot();
        return;
    }

    console_write("\nUnknown command: ");
    console_write(cmd);
}

void shell_run(void) {
    char line[160];
    size_t len = 0;

    prompt();

    for (;;) {
        char c = keyboard_getchar();

        if (c == 10 || c == 13) {
            line[len] = 0;
            exec(line);
            len = 0;
            prompt();
            continue;
        }

        if (c == 8 || c == 127) {
            if (len > 0) {
                len--;
                console_putc(8);
            }

            continue;
        }

        if (c >= 32 && c <= 126 && len + 1 < sizeof(line)) {
            line[len++] = c;
            console_putc(c);
        }
    }
}
