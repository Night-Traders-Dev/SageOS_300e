gc_disable()
# SageOS Interactive Shell (Multi-Architecture)
# Usage: sage shell.sage [x86_64|aarch64|riscv64]

import sys
import io
import os.examples.common as common

let NL = chr(10)
let arch = common.arch_from_args("x86_64")
if not common.is_valid_arch(arch):
    print "Error: unsupported arch: " + arch
    sys.exit(1)
end

print "=== SageOS Shell ==="
print "Arch: " + arch

let out_dir = "/tmp/sageos_shell_" + arch
sys.exec("mkdir -p " + out_dir)

let features = {}
features["entry"] = "shell_main"
features["has_shell"] = true
features["has_vga"] = false

let result = common.build_kernel(arch, out_dir, features)

# Append shell_main to kernel C
let shell_c = ""
shell_c = shell_c + "#define CMD_MAX 128" + NL
shell_c = shell_c + "#define HISTORY_MAX 16" + NL
shell_c = shell_c + "#define CMD_LIST_COUNT 9" + NL
shell_c = shell_c + "static const char* commands[CMD_LIST_COUNT] = {" + NL
shell_c = shell_c + "    \"help\", \"about\", \"uptime\", \"regs\", \"mem\", \"clear\", \"halt\", \"neofetch\", \"btop\"" + NL
shell_c = shell_c + "};" + NL
shell_c = shell_c + "static char history[HISTORY_MAX][CMD_MAX];" + NL
shell_c = shell_c + "static int history_count = 0;" + NL
shell_c = shell_c + "static int history_idx = -1;" + NL
shell_c = shell_c + "static int cursor_pos = 0;" + NL

if arch == "x86_64":
    shell_c = shell_c + "static inline int serial_avail() { return inb(COM1+5) & 1; }" + NL
elif arch == "aarch64":
    shell_c = shell_c + "static inline int serial_avail() { return !(UART_FR & 0x10); }" + NL
elif arch == "riscv64":
    shell_c = shell_c + "static inline int serial_avail() { return uart_read(5) & 1; }" + NL
end

shell_c = shell_c + "static int strncmp(const char *s1, const char *s2, int n) {" + NL
shell_c = shell_c + "    for (int i = 0; i < n; i++) {" + NL
shell_c = shell_c + "        if (s1[i] != s2[i]) return s1[i] - s2[i];" + NL
shell_c = shell_c + "        if (s1[i] == '\\0') return 0;" + NL
shell_c = shell_c + "    }" + NL
shell_c = shell_c + "    return 0; }" + NL

shell_c = shell_c + "static void strcpy(char *dest, const char *src) {" + NL
shell_c = shell_c + "    while (*src) *dest++ = *src++;" + NL
shell_c = shell_c + "    *dest = '\\0'; }" + NL

shell_c = shell_c + "static void serial_raw(const char *s) {" + NL
shell_c = shell_c + "    while (*s) serial_putc(*s++); }" + NL

shell_c = shell_c + "static const char* find_suggestion(const char* cmd, int cmd_len) {" + NL
shell_c = shell_c + "    if (cmd_len == 0) return \"\";" + NL
shell_c = shell_c + "    for (int i = history_count - 1; i >= 0; i--) {" + NL
shell_c = shell_c + "        if (strncmp(history[i % HISTORY_MAX], cmd, cmd_len) == 0) {" + NL
shell_c = shell_c + "            return history[i % HISTORY_MAX];" + NL
shell_c = shell_c + "        }" + NL
shell_c = shell_c + "    }" + NL
shell_c = shell_c + "    for (int i = 0; i < CMD_LIST_COUNT; i++) {" + NL
shell_c = shell_c + "        if (strncmp(commands[i], cmd, cmd_len) == 0) {" + NL
shell_c = shell_c + "            return commands[i];" + NL
shell_c = shell_c + "        }" + NL
shell_c = shell_c + "    }" + NL
shell_c = shell_c + "    return \"\"; }" + NL

shell_c = shell_c + "static void redraw_line(const char* prompt, const char* cmd, int len, int pos, const char* suggestion) {" + NL
shell_c = shell_c + "    serial_raw(\"\\r\\033[K\");" + NL
shell_c = shell_c + "    serial_raw(prompt);" + NL
shell_c = shell_c + "    serial_raw(\"\\033[1;32m\");" + NL
shell_c = shell_c + "    serial_raw(cmd);" + NL
shell_c = shell_c + "    serial_raw(\"\\033[0m\");" + NL
shell_c = shell_c + "    int sug_len = _strlen(suggestion);" + NL
shell_c = shell_c + "    if (sug_len > len && strncmp(suggestion, cmd, len) == 0) {" + NL
shell_c = shell_c + "        serial_raw(\"\\033[90m\");" + NL
shell_c = shell_c + "        serial_raw(suggestion + len);" + NL
shell_c = shell_c + "        serial_raw(\"\\033[0m\");" + NL
shell_c = shell_c + "    }" + NL
shell_c = shell_c + "    serial_putc('\\r');" + NL
shell_c = shell_c + "    for (int i = 0; i < _strlen(prompt) + pos; i++) serial_raw(\"\\033[C\");" + NL
shell_c = shell_c + "}" + NL

shell_c = shell_c + "#define ARCH_STRING \"" + arch + "\"" + NL

shell_c = shell_c + "static void cmd_help(void) {" + NL
shell_c = shell_c + "    serial_puts(\"Available commands:\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"  help       Show this help message\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"  about      Show information about SageOS\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"  uptime     Show system uptime metrics\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"  regs       Dump CPU registers\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"  mem        Display memory statistics\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"  clear      Clear the screen\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"  neofetch   Show system specs and beautiful logo\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"  btop       Interactive-style resource monitor\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"  halt       Shut down the virtual machine\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"  echo <msg> Echo the message\\n\"); }" + NL

shell_c = shell_c + "static void cmd_about(void) {" + NL
shell_c = shell_c + "    serial_puts(\"SageOS v0.3.0 — Premium SageLang Bare-Metal Operating System\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"Built using SageLang cross-compilation pipeline on freestanding C.\\n\"); }" + NL

shell_c = shell_c + "static void cmd_uptime(void) {" + NL
shell_c = shell_c + "    serial_puts(\"Uptime info:\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"  Commands executed: \"); serial_putdec(history_count); serial_puts(\"\\n\"); }" + NL

shell_c = shell_c + "static void cmd_mem(void) {" + NL
if arch == "x86_64":
    shell_c = shell_c + "    serial_puts(\"VGA Text Memory: 0xB8000 (VGA active)\\n\");" + NL
    shell_c = shell_c + "    serial_puts(\"Base Memory: \"); serial_putdec(mem_lower_kb); serial_puts(\" KB, Extended Memory: \");" + NL
    shell_c = shell_c + "    serial_putdec(mem_upper_kb); serial_puts(\" KB (4 GB total physical)\\n\");" + NL
else:
    shell_c = shell_c + "    serial_puts(\"Physical RAM: 4096 MB (QEMU Virt Memory Space)\\n\");" + NL
    shell_c = shell_c + "    serial_puts(\"Kernel Segments loaded successfully.\\n\");" + NL
end
shell_c = shell_c + "}" + NL

shell_c = shell_c + "static void cmd_neofetch(void) {" + NL
shell_c = shell_c + "    serial_puts(\"\\033[1;36m   _____             __  ____  _____\\n\\033[0m\");" + NL
shell_c = shell_c + "    serial_puts(\"\\033[1;36m  / ___/____ _____ _/ /_/ __ \\\\/ ___/\\n\\033[0m\");" + NL
shell_c = shell_c + "    serial_puts(\"\\033[1;36m  \\\\__ \\\\/ __ `/ __ `/ __/ / / /\\\\__ \\\\ \\n\\033[0m\");" + NL
shell_c = shell_c + "    serial_puts(\"\\033[1;36m ___/ / /_/ / /_/ / /_/ /_/ /___/ / \\n\\033[0m\");" + NL
shell_c = shell_c + "    serial_puts(\"\\033[1;36m/____/\\\\__,_/\\\\__, /\\\\__/\\\\____//____/  \\n\\033[0m\");" + NL
shell_c = shell_c + "    serial_puts(\"\\033[1;36m           /____/                   \\n\\033[0m\");" + NL
shell_c = shell_c + "    serial_puts(\"\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"\\033[1;33mOS\\033[0m:        SageOS v0.3.0 (Bare-Metal)\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"\\033[1;33mKernel\\033[0m:    AOT-Compiled Freestanding SageLang\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"\\033[1;33mArch\\033[0m:      \" ARCH_STRING \"\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"\\033[1;33mCPU\\033[0m:       QEMU Virt System\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"\\033[1;33mMemory\\033[0m:    4096 MB RAM (virt default)\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"\\033[1;33mUptime\\033[0m:    Active and running\\n\"); }" + NL

shell_c = shell_c + "static void cmd_btop(void) {" + NL
shell_c = shell_c + "    serial_puts(\"\\033[2J\\033[H\");" + NL
shell_c = shell_c + "    int frame = 0;" + NL
shell_c = shell_c + "    while (1) {" + NL
shell_c = shell_c + "        serial_puts(\"\\033[H\");" + NL
shell_c = shell_c + "        serial_puts(\"\\033[1;35m┌────────────────────────────────────────────────────────┐\\033[0m\\n\");" + NL
shell_c = shell_c + "        serial_puts(\"\\033[1;35m│\\033[0m \\033[1;32mSageOS System Monitor (btop-interactive)\\033[0m           \\033[1;35m│\\033[0m\\n\");" + NL
shell_c = shell_c + "        serial_puts(\"\\033[1;35m├────────────────────────────────────────────────────────┤\\033[0m\\n\");" + NL
shell_c = shell_c + "        int cpu = 40 + (frame % 30);" + NL
shell_c = shell_c + "        serial_puts(\"\\033[1;35m│\\033[0m \\033[1;33mCPU Utilization\\033[0m: [\");" + NL
shell_c = shell_c + "        for(int i=0; i<25; i++) if(i < cpu/4) serial_raw(\"█\"); else serial_raw(\"░\");" + NL
shell_c = shell_c + "        serial_puts(\"] \"); serial_putdec(cpu); serial_puts(\"%        \\033[1;35m│\\033[0m\\n\");" + NL
shell_c = shell_c + "        int mem = 20 + (frame % 10);" + NL
shell_c = shell_c + "        serial_puts(\"\\033[1;35m│\\033[0m \\033[1;33mMEM Utilization\\033[0m: [\");" + NL
shell_c = shell_c + "        for(int i=0; i<25; i++) if(i < mem/4) serial_raw(\"█\"); else serial_raw(\"░\");" + NL
shell_c = shell_c + "        serial_puts(\"] \"); serial_putdec(mem); serial_puts(\"%        \\033[1;35m│\\033[0m\\n\");" + NL
shell_c = shell_c + "        serial_puts(\"\\033[1;35m├────────────────────────────────────────────────────────┤\\033[0m\\n\");" + NL
shell_c = shell_c + "        serial_puts(\"\\033[1;35m│\\033[0m \\033[1;34mSystem Information\\033[0m                                     \\033[1;35m│\\033[0m\\n\");" + NL
shell_c = shell_c + "        serial_puts(\"\\033[1;35m│\\033[0m   Arch:     \" ARCH_STRING \"                                     \\033[1;35m│\\033[0m\\n\");" + NL
shell_c = shell_c + "        serial_puts(\"\\033[1;35m│\\033[0m   Frame:    \"); serial_putdec(frame); serial_puts(\"                                    \\033[1;35m│\\033[0m\\n\");" + NL
shell_c = shell_c + "        serial_puts(\"\\033[1;35m│\\033[0m   Press 'q' to exit.                                   \\033[1;35m│\\033[0m\\n\");" + NL
shell_c = shell_c + "        serial_puts(\"\\033[1;35m└────────────────────────────────────────────────────────┘\\033[0m\\n\");" + NL
shell_c = shell_c + "        for (int d=0; d<1000000; d++) {" + NL
shell_c = shell_c + "            if (serial_avail()) if (serial_getc() == 'q') { serial_puts(\"\\033[2J\\033[H\"); return; }" + NL
shell_c = shell_c + "        }" + NL
shell_c = shell_c + "        frame++;" + NL
shell_c = shell_c + "    }" + NL
shell_c = shell_c + "}" + NL

if arch == "x86_64":
    shell_c = shell_c + "void shell_main(uint32_t magic, mb_t *mbi) {" + NL
    shell_c = shell_c + "    parse_multiboot(magic, mbi);" + NL
else:
    shell_c = shell_c + "void shell_main(void) {" + NL
end

shell_c = shell_c + "    serial_init();" + NL
shell_c = shell_c + "    serial_puts(\"\\033[1;36mSageOS Premium Shell v0.3.0 \" ARCH_STRING \"\\033[0m\\n\");" + NL
shell_c = shell_c + "    serial_puts(\"Type \\033[1;33mhelp\\033[0m for commands. Autocomplete with \\033[1;33mTab\\033[0m or \\033[1;33mRight Arrow\\033[0m.\\n\\n\");" + NL
shell_c = shell_c + "    char cmd[CMD_MAX];" + NL
shell_c = shell_c + "    int len = 0;" + NL
shell_c = shell_c + "    cmd[0] = '\\0';" + NL
shell_c = shell_c + "    cursor_pos = 0;" + NL
shell_c = shell_c + "    const char* prompt = \"sage@os:~$ \";" + NL
shell_c = shell_c + "    serial_raw(prompt);" + NL
shell_c = shell_c + "    while (1) {" + NL
shell_c = shell_c + "        char c = serial_getc();" + NL
shell_c = shell_c + "        if (c == 27) { // ESC sequence" + NL
shell_c = shell_c + "            char c2 = serial_getc();" + NL
shell_c = shell_c + "            if (c2 == 91) {" + NL
shell_c = shell_c + "                char c3 = serial_getc();" + NL
shell_c = shell_c + "                if (c3 == 65) { // Up Arrow" + NL
shell_c = shell_c + "                    if (history_count > 0 && history_idx < history_count - 1 && history_idx < HISTORY_MAX - 1) {" + NL
shell_c = shell_c + "                        history_idx++;" + NL
shell_c = shell_c + "                        strcpy(cmd, history[(history_count - 1 - history_idx) % HISTORY_MAX]);" + NL
shell_c = shell_c + "                        len = _strlen(cmd); cursor_pos = len;" + NL
shell_c = shell_c + "                        redraw_line(prompt, cmd, len, cursor_pos, \"\");" + NL
shell_c = shell_c + "                    }" + NL
shell_c = shell_c + "                }" + NL
shell_c = shell_c + "                else if (c3 == 66) { // Down Arrow" + NL
shell_c = shell_c + "                    if (history_idx > 0) {" + NL
shell_c = shell_c + "                        history_idx--;" + NL
shell_c = shell_c + "                        strcpy(cmd, history[(history_count - 1 - history_idx) % HISTORY_MAX]);" + NL
shell_c = shell_c + "                        len = _strlen(cmd); cursor_pos = len;" + NL
shell_c = shell_c + "                        redraw_line(prompt, cmd, len, cursor_pos, \"\");" + NL
shell_c = shell_c + "                    } else if (history_idx == 0) {" + NL
shell_c = shell_c + "                        history_idx = -1;" + NL
shell_c = shell_c + "                        cmd[0] = '\\0'; len = 0; cursor_pos = 0;" + NL
shell_c = shell_c + "                        redraw_line(prompt, cmd, len, cursor_pos, \"\");" + NL
shell_c = shell_c + "                    }" + NL
shell_c = shell_c + "                }" + NL
shell_c = shell_c + "                else if (c3 == 67) { // Right Arrow" + NL
shell_c = shell_c + "                    if (cursor_pos < len) {" + NL
shell_c = shell_c + "                        cursor_pos++;" + NL
shell_c = shell_c + "                        redraw_line(prompt, cmd, len, cursor_pos, find_suggestion(cmd, len));" + NL
shell_c = shell_c + "                    } else {" + NL
shell_c = shell_c + "                        const char* sug = find_suggestion(cmd, len);" + NL
shell_c = shell_c + "                        if (_strlen(sug) > len) {" + NL
shell_c = shell_c + "                            strcpy(cmd, sug); len = _strlen(cmd); cursor_pos = len;" + NL
shell_c = shell_c + "                            redraw_line(prompt, cmd, len, cursor_pos, \"\");" + NL
shell_c = shell_c + "                        }" + NL
shell_c = shell_c + "                    }" + NL
shell_c = shell_c + "                }" + NL
shell_c = shell_c + "                else if (c3 == 68) { // Left Arrow" + NL
shell_c = shell_c + "                    if (cursor_pos > 0) {" + NL
shell_c = shell_c + "                        cursor_pos--;" + NL
shell_c = shell_c + "                        redraw_line(prompt, cmd, len, cursor_pos, find_suggestion(cmd, len));" + NL
shell_c = shell_c + "                    }" + NL
shell_c = shell_c + "                }" + NL
shell_c = shell_c + "            }" + NL
shell_c = shell_c + "            continue;" + NL
shell_c = shell_c + "        }" + NL
shell_c = shell_c + "        if (c == '\\r' || c == '\\n') {" + NL
shell_c = shell_c + "            serial_puts(\"\\n\");" + NL
shell_c = shell_c + "            cmd[len] = '\\0';" + NL
shell_c = shell_c + "            if (len > 0) {" + NL
shell_c = shell_c + "                strcpy(history[history_count % HISTORY_MAX], cmd);" + NL
shell_c = shell_c + "                history_count++; history_idx = -1;" + NL
shell_c = shell_c + "                if (streq(cmd, \"help\")) cmd_help();" + NL
shell_c = shell_c + "                else if (streq(cmd, \"about\")) cmd_about();" + NL
shell_c = shell_c + "                else if (streq(cmd, \"uptime\")) cmd_uptime();" + NL
shell_c = shell_c + "                else if (streq(cmd, \"regs\")) dump_regs();" + NL
shell_c = shell_c + "                else if (streq(cmd, \"mem\")) cmd_mem();" + NL
shell_c = shell_c + "                else if (streq(cmd, \"neofetch\")) cmd_neofetch();" + NL
shell_c = shell_c + "                else if (streq(cmd, \"btop\")) cmd_btop();" + NL
shell_c = shell_c + "                else if (streq(cmd, \"clear\")) serial_puts(\"\\033[2J\\033[H\");" + NL
shell_c = shell_c + "                else if (streq(cmd, \"halt\")) {" + NL
shell_c = shell_c + "                    serial_puts(\"Halting system...\\n\");" + NL
if arch == "x86_64":
    shell_c = shell_c + "                    __asm__ volatile(\"cli; hlt\");" + NL
elif arch == "aarch64":
    shell_c = shell_c + "                    while(1) __asm__ volatile(\"wfe\");" + NL
elif arch == "riscv64":
    shell_c = shell_c + "                    while(1) __asm__ volatile(\"wfi\");" + NL
end
shell_c = shell_c + "                }" + NL
shell_c = shell_c + "                else if (startswith(cmd, \"echo \")) {" + NL
shell_c = shell_c + "                    serial_puts(cmd + 5); serial_puts(\"\\n\");" + NL
shell_c = shell_c + "                } else {" + NL
shell_c = shell_c + "                    serial_puts(\"Unknown command: \"); serial_puts(cmd); serial_puts(\"\\n\");" + NL
shell_c = shell_c + "                }" + NL
shell_c = shell_c + "            }" + NL
shell_c = shell_c + "            len = 0; cursor_pos = 0; cmd[0] = '\\0';" + NL
shell_c = shell_c + "            serial_raw(prompt);" + NL
shell_c = shell_c + "            continue;" + NL
shell_c = shell_c + "        }" + NL
shell_c = shell_c + "        if (c == 9) { // Tab autocomplete" + NL
shell_c = shell_c + "            const char* sug = find_suggestion(cmd, len);" + NL
shell_c = shell_c + "            if (_strlen(sug) > len) {" + NL
shell_c = shell_c + "                strcpy(cmd, sug); len = _strlen(cmd); cursor_pos = len;" + NL
shell_c = shell_c + "                redraw_line(prompt, cmd, len, cursor_pos, \"\");" + NL
shell_c = shell_c + "            }" + NL
shell_c = shell_c + "            continue;" + NL
shell_c = shell_c + "        }" + NL
shell_c = shell_c + "        if (c == 127 || c == 8) { // Backspace" + NL
shell_c = shell_c + "            if (cursor_pos > 0) {" + NL
shell_c = shell_c + "                for (int i = cursor_pos - 1; i < len; i++) cmd[i] = cmd[i+1];" + NL
shell_c = shell_c + "                len--; cursor_pos--;" + NL
shell_c = shell_c + "                redraw_line(prompt, cmd, len, cursor_pos, find_suggestion(cmd, len));" + NL
shell_c = shell_c + "            }" + NL
shell_c = shell_c + "            continue;" + NL
shell_c = shell_c + "        }" + NL
shell_c = shell_c + "        if (c >= 32 && c <= 126) {" + NL
shell_c = shell_c + "            if (len < CMD_MAX - 1) {" + NL
shell_c = shell_c + "                for (int i = len; i > cursor_pos; i--) cmd[i] = cmd[i-1];" + NL
shell_c = shell_c + "                cmd[cursor_pos++] = c; len++; cmd[len] = '\\0';" + NL
shell_c = shell_c + "                redraw_line(prompt, cmd, len, cursor_pos, find_suggestion(cmd, len));" + NL
shell_c = shell_c + "            }" + NL
shell_c = shell_c + "        }" + NL
shell_c = shell_c + "    }" + NL
shell_c = shell_c + "}" + NL

let kernel_c_path = result["kernel_c"]
let existing = io.readfile(kernel_c_path)
io.writefile(kernel_c_path, existing + NL + shell_c)

print "Building..."
let rc = common.run_commands(result["commands"])
if rc != 0:
    print "Build FAILED: " + str(rc)
    sys.exit(1)
end

print "Build OK: " + result["elf"]
print ""
print "Run: " + result["qemu"]
print "Commands: help, echo <text>, mem, regs, uptime, clear, halt"
