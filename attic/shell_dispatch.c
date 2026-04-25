int cmd_matches_word(const char *line, const char *word);

static void exec(const char *cmd) {
    cmd = skip_spaces(cmd);
    if (streq(cmd, "")) return;

    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        if (cmd_matches_word(cmd, commands[i].name)) {
            commands[i].func(arg_after(cmd, commands[i].name));
            return;
        }
    }
    console_write("\nUnknown command: ");
    console_write(cmd);
}
