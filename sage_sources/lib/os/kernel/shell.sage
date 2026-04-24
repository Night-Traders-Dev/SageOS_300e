gc_disable()

# shell.sage - SageOS kernel shell
# Pure SageLang line editor with fish-style history and autosuggestions.

import console
import keyboard
import syscall

let SHELL_PROMPT_USER = "sage@os"
let SHELL_PROMPT_PATH = "~"
let HISTORY_MAX = 16

let shell_history = []
let shell_commands = [
    "help",
    "clear",
    "version",
    "uname",
    "about",
    "mem",
    "fb",
    "ls",
    "cat",
    "echo",
    "color",
    "input",
    "dmesg",
    "history",
    "shutdown",
    "poweroff",
    "halt",
    "reboot",
    "exit",
]

let shell_paths = [
    "/",
    "/etc/motd",
    "/etc/version",
    "/bin/sh",
    "/dev/fb0",
    "/proc/fb",
    "/proc/meminfo",
]

let shell_colors = [
    "white",
    "green",
    "amber",
    "blue",
    "red",
]

proc print_prompt():
    console.set_color(console.LIGHT_CYAN, console.BLACK)
    console.print_str(SHELL_PROMPT_USER)
    console.set_color(console.WHITE, console.BLACK)
    console.print_str(":")
    console.set_color(console.LIGHT_BLUE, console.BLACK)
    console.print_str(SHELL_PROMPT_PATH)
    console.set_color(console.WHITE, console.BLACK)
    console.print_str("$ ")
end

proc prompt_len():
    return len(SHELL_PROMPT_USER) + len(SHELL_PROMPT_PATH) + 4
end

proc starts_with(text, prefix):
    if len(prefix) > len(text):
        return false
    end
    for i in range(len(prefix)):
        if text[i] != prefix[i]:
            return false
        end
    end
    return true
end

proc starts_word(text, word):
    if starts_with(text, word) == false:
        return false
    end
    if len(text) == len(word):
        return true
    end
    let sep = text[len(word)]
    return sep == " " or sep == chr(9)
end

proc has_space(text):
    for i in range(len(text)):
        if text[i] == " " or text[i] == chr(9):
            return true
        end
    end
    return false
end

proc is_blank(text):
    for i in range(len(text)):
        if text[i] != " " and text[i] != chr(9):
            return false
        end
    end
    return true
end

proc arg_after(text, word):
    let i = len(word)
    while i < len(text):
        if text[i] != " " and text[i] != chr(9):
            break
        end
        i = i + 1
    end

    let out = ""
    while i < len(text):
        out = out + text[i]
        i = i + 1
    end
    return out
end

proc append_history(line):
    if is_blank(line):
        return
    end
    if len(shell_history) > 0:
        if shell_history[len(shell_history) - 1] == line:
            return
        end
    end

    if len(shell_history) >= HISTORY_MAX:
        let next_history = []
        let i = 1
        while i < len(shell_history):
            append(next_history, shell_history[i])
            i = i + 1
        end
        shell_history = next_history
    end

    append(shell_history, line)
end

proc suggestion_from_list(prefix, items, allow_empty):
    if len(prefix) == 0 and allow_empty == false:
        return ""
    end
    for i in range(len(items)):
        let item = items[i]
        if starts_with(item, prefix):
            if item != prefix:
                let suffix = ""
                let j = len(prefix)
                while j < len(item):
                    suffix = suffix + item[j]
                    j = j + 1
                end
                return suffix
            end
        end
    end
    return ""
end

proc argument_suggestion(line, cmd, items):
    if starts_word(line, cmd) == false:
        return ""
    end
    if len(line) <= len(cmd):
        return ""
    end
    if line[len(cmd)] != " " and line[len(cmd)] != chr(9):
        return ""
    end
    return suggestion_from_list(arg_after(line, cmd), items, true)
end

proc find_suggestion(line):
    if len(line) == 0:
        return ""
    end

    let i = len(shell_history) - 1
    while i >= 0:
        let candidate = shell_history[i]
        if starts_with(candidate, line):
            if candidate != line:
                let suffix = ""
                let j = len(line)
                while j < len(candidate):
                    suffix = suffix + candidate[j]
                    j = j + 1
                end
                return suffix
            end
        end
        i = i - 1
    end

    let path_suggestion = argument_suggestion(line, "cat", shell_paths)
    if path_suggestion != "":
        return path_suggestion
    end

    let color_suggestion = argument_suggestion(line, "color", shell_colors)
    if color_suggestion != "":
        return color_suggestion
    end

    if has_space(line) == false:
        return suggestion_from_list(line, shell_commands, false)
    end

    return ""
end

proc redraw_input(start, line, suggestion, last_len):
    console.set_cursor(start["x"], start["y"])
    console.set_color(console.WHITE, console.BLACK)
    console.print_str(line)
    console.set_color(console.DARK_GRAY, console.BLACK)
    console.print_str(suggestion)
    console.set_color(console.WHITE, console.BLACK)

    let visible = len(line) + len(suggestion)
    let i = visible
    while i < last_len:
        console.print_str(" ")
        i = i + 1
    end

    console.set_cursor(start["x"] + len(line), start["y"])
    return visible
end

proc finish_input(start, line, last_len):
    console.set_cursor(start["x"], start["y"])
    console.set_color(console.WHITE, console.BLACK)
    console.print_str(line)

    let i = len(line)
    while i < last_len:
        console.print_str(" ")
        i = i + 1
    end

    console.set_cursor(start["x"] + len(line), start["y"])
end

proc read_command_line():
    let line = ""
    let draft = ""
    let history_view = -1
    let last_len = 0

    print_prompt()
    let start = console.get_cursor()

    while true:
        let suggestion = find_suggestion(line)
        let key = keyboard.wait_key()
        let scancode = key["scancode"]
        let ch = key["char"]

        if ch == chr(10):
            finish_input(start, line, last_len)
            console.newline()
            return line
        end

        if scancode == keyboard.KEY_BACKSPACE:
            if len(line) > 0:
                line = line[0:len(line) - 1]
                history_view = -1
                suggestion = find_suggestion(line)
                last_len = redraw_input(start, line, suggestion, last_len)
            end
            syscall.builtin_yield(nil)
            continue
        end

        if scancode == keyboard.KEY_UP:
            if len(shell_history) > 0:
                if history_view < 0:
                    draft = line
                    history_view = len(shell_history) - 1
                elif history_view > 0:
                    history_view = history_view - 1
                end
                line = shell_history[history_view]
                suggestion = find_suggestion(line)
                last_len = redraw_input(start, line, suggestion, last_len)
            end
            syscall.builtin_yield(nil)
            continue
        end

        if scancode == keyboard.KEY_DOWN:
            if history_view >= 0:
                if history_view + 1 < len(shell_history):
                    history_view = history_view + 1
                    line = shell_history[history_view]
                else:
                    history_view = -1
                    line = draft
                end
                suggestion = find_suggestion(line)
                last_len = redraw_input(start, line, suggestion, last_len)
            end
            syscall.builtin_yield(nil)
            continue
        end

        if ch == chr(9) or scancode == keyboard.KEY_RIGHT:
            if suggestion != "":
                line = line + suggestion
                history_view = -1
            end
            suggestion = find_suggestion(line)
            last_len = redraw_input(start, line, suggestion, last_len)
            syscall.builtin_yield(nil)
            continue
        end

        if ch != nil:
            if ch != chr(9):
                let max_len = console.VGA_WIDTH - prompt_len() - 1
                if len(line) < max_len:
                    line = line + ch
                    history_view = -1
                    suggestion = find_suggestion(line)
                    last_len = redraw_input(start, line, suggestion, last_len)
                end
            end
        end

        syscall.builtin_yield(nil)
    end
end

proc print_history():
    if len(shell_history) == 0:
        console.print_line("History is empty.")
        return
    end
    for i in range(len(shell_history)):
        console.print_line("  " + str(i + 1) + "  " + shell_history[i])
    end
end

proc print_help():
    console.print_line("Available commands:")
    console.print_line("  help      - Show this help message")
    console.print_line("  ls        - List files")
    console.print_line("  cat       - Show RAMFS or proc file")
    console.print_line("  clear     - Clear the screen")
    console.print_line("  version   - Show SageOS version")
    console.print_line("  uname     - Show kernel/system id")
    console.print_line("  about     - Show project summary")
    console.print_line("  mem       - Show memory status")
    console.print_line("  fb        - Show framebuffer status")
    console.print_line("  input     - Show keyboard backend")
    console.print_line("  dmesg     - Show early kernel log")
    console.print_line("  color     - Set console color")
    console.print_line("  history   - Show command history")
    console.print_line("  exit      - Exit the shell")
    console.print_line("")
    console.print_line("Line editing:")
    console.print_line("  Up/Down   - Navigate history")
    console.print_line("  Tab/Right - Accept autosuggestion")
end

proc ramfs_read(path):
    if path == "/etc/motd":
        return "Welcome to SageOS."
    end
    if path == "/etc/version":
        return "SageOS 0.1.0"
    end
    if path == "/bin/sh":
        return "Pure SageLang kernel shell."
    end
    if path == "/dev/fb0":
        return "Framebuffer console device."
    end
    if path == "/proc/fb":
        return "Framebuffer: initialized by kernel console"
    end
    if path == "/proc/meminfo":
        return "Memory: managed by SageLang PMM"
    end
    return nil
end

proc handle_command(raw_cmd):
    let cmd = arg_after(raw_cmd, "")
    if cmd == "":
        return
    end

    if starts_word(cmd, "help"):
        print_help()
        return
    end

    if starts_word(cmd, "ls"):
        console.print_line("/")
        for i in range(len(shell_paths)):
            console.print_line(shell_paths[i])
        end
        return
    end

    if starts_word(cmd, "cat"):
        let path = arg_after(cmd, "cat")
        let content = ramfs_read(path)
        if content == nil:
            console.print_line("cat: no such file: " + path)
        else:
            console.print_line(content)
        end
        return
    end

    if starts_word(cmd, "clear"):
        console.clear_screen(console.BLACK)
        return
    end

    if starts_word(cmd, "version"):
        console.print_line("SageOS 0.1.0 x86_64")
        return
    end

    if starts_word(cmd, "uname"):
        console.print_line("SageOS sageos 0.1.0 x86_64 sagelang")
        return
    end

    if starts_word(cmd, "about"):
        console.print_line("SageOS is a small POSIX-inspired educational OS target.")
        console.print_line("Current porting slice: SageLang kernel shell and line editor.")
        return
    end

    if starts_word(cmd, "mem"):
        console.print_line("Memory manager: SageLang PMM/VMM modules loaded")
        return
    end

    if starts_word(cmd, "fb"):
        console.print_line("Framebuffer console: available through console.sage")
        return
    end

    if starts_word(cmd, "input"):
        console.print_line("Keyboard backend: SageLang PS/2 keyboard module")
        return
    end

    if starts_word(cmd, "dmesg"):
        console.print_line("[0.000000] SageOS SageLang kernel entered")
        console.print_line("[0.000001] console initialized")
        console.print_line("[0.000002] keyboard initialized")
        console.print_line("[0.000003] shell started")
        return
    end

    if starts_word(cmd, "echo"):
        console.print_line(arg_after(cmd, "echo"))
        return
    end

    if starts_word(cmd, "color"):
        let color = arg_after(cmd, "color")
        if color == "green":
            console.set_color(console.LIGHT_GREEN, console.BLACK)
            return
        end
        if color == "white":
            console.set_color(console.WHITE, console.BLACK)
            return
        end
        if color == "blue":
            console.set_color(console.LIGHT_BLUE, console.BLACK)
            return
        end
        if color == "red":
            console.set_color(console.LIGHT_RED, console.BLACK)
            return
        end
        if color == "amber":
            console.set_color(console.YELLOW, console.BLACK)
            return
        end
        console.print_line("usage: color <white|green|amber|blue|red>")
        return
    end

    if starts_word(cmd, "history"):
        print_history()
        return
    end

    if starts_word(cmd, "shutdown") or starts_word(cmd, "poweroff") or starts_word(cmd, "halt") or starts_word(cmd, "exit"):
        console.print_line("Shutting down...")
        syscall.sys_exit(0)
        return
    end

    if starts_word(cmd, "reboot"):
        console.print_line("reboot: firmware reset handoff is still in the C bootstrap kernel")
        return
    end

    console.print_line("sh: command not found: " + cmd)
end

proc sh_main():
    console.print_line("SageOS Shell v0.1.0")
    console.print_line("Type 'help' for available commands.")
    console.print_line("")

    while true:
        let cmd = read_command_line()
        append_history(cmd)
        handle_command(cmd)
    end
end
