# fix_table.pl — Regenerates the ShellCommand dispatch table in shell.c
#
# PURPOSE
#   Replaces the static ShellCommand commands[] array inside shell.c with the
#   canonical table defined in $table below.  Run this whenever you add or
#   remove a cmd_* handler so the dispatch table stays in sync without hand-
#   editing the C source.
#
# USAGE
#   perl fix_table.pl < sageos_build/kernel/shell/shell.c > shell.c.new
#   mv shell.c.new sageos_build/kernel/shell/shell.c
#
# NOTES
#   - Entries must be sorted longest-prefix-first for two-word commands
#     ("acpi battery" before "acpi") so the dispatcher matches correctly.
#   - After running, rebuild with: ./lenovo_300e.sh build-kernel
#
# GUARD: fix_table.pl-header-v1
undef $/;
my $file = <STDIN>;
my $table = q{    {"about", "project summary", cmd_about},
    {"acpi", "show ACPI summary", cmd_acpi},
    {"acpi battery", "show ACPI battery info", cmd_acpi_battery},
    {"acpi fadt", "show FADT power fields", cmd_acpi_fadt},
    {"acpi lid", "show ACPI lid info", cmd_acpi_lid},
    {"acpi madt", "show MADT/APIC fields", cmd_acpi_madt},
    {"acpi tables", "list ACPI tables", cmd_acpi_tables},
    {"battery", "show battery/EC detector", cmd_battery},
    {"cat", "print file content", cmd_cat},
    {"clear", "clear console", cmd_clear},
    {"color", "set color", cmd_color},
    {"dmesg", "show early log", cmd_dmesg},
    {"echo", "print text", cmd_echo},
    {"execelf", "execute ELF binary", cmd_execelf},
    {"exit", "exit QEMU", cmd_exit},
    {"fb", "framebuffer info", cmd_fb},
    {"halt", "halt CPU", cmd_halt},
    {"help", "show help", cmd_help},
    {"input", "input backend info", cmd_input},
    {"keydebug", "raw keyboard scancode monitor", cmd_keydebug},
    {"ls", "list root files", cmd_ls},
    {"poweroff", "ACPI shutdown", cmd_poweroff},
    {"reboot", "reboot", cmd_reboot},
    {"sage", "execute SageLang module", cmd_sage},
    {"shutdown", "ACPI shutdown", cmd_shutdown},
    {"smp", "show CPU/APIC discovery", cmd_smp},
    {"smp start", "start APs", cmd_smp_start},
    {"status", "show top-bar metrics", cmd_status},
    {"suspend", "ACPI suspend", cmd_suspend},
    {"sysinfo", "show system info", cmd_sysinfo},
    {"timer", "show timer info", cmd_timer},
    {"uname", "show system id", cmd_uname},
    {"version", "show version", cmd_version},};
$file =~ s/static const ShellCommand commands\[\] = \{.*?\};/$table/s;
print $file;
