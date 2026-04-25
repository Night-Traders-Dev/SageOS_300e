static void exec(const char *cmd) {
    cmd = skip_spaces(cmd);
    if (streq(cmd, "")) return;

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        /* Patch 1: word boundary required — name must be followed by NUL or space. */
        { size_t _n = 0; while (commands[i].name[_n]) _n++;
        if (starts_with(cmd, commands[i].name) &&
            (cmd[_n] == '\0' || cmd[_n] == ' ')) {
            commands[i].func(arg_after(cmd, commands[i].name));
            return;
        }
    }
    console_write("\nUnknown command: ");
    console_write(cmd);
}
