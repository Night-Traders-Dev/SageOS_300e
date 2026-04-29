#ifndef SAGEOS_SHELL_H
#define SAGEOS_SHELL_H

void shell_run(void);
void shell_exec_command(const char *cmd);
int shell_completion_count(const char *prefix);
const char *shell_completion_at(const char *prefix, int index);
const char *shell_completion_common_prefix(const char *prefix);
const char *shell_suggestion(const char *line);
void shell_print_completions(const char *prefix);
void cmd_btop(void);
void cmd_install(void);
void cmd_nano(const char *path);
void cmd_source(const char *path);

#endif
