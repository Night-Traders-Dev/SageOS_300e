gc_disable()
import console
import keyboard
import syscall

let SHELL_PROMPT_USER = "sage@os"
let shell_history = []

proc handle_command(cmd):
    if cmd == "help":
        console.print_line("Available: help, clear, version")
    elif cmd == "clear":
        console.clear_screen(0)
    end
end

proc sh_main():
    console.print_line("SageOS Shell")
    while true:
        console.print_str(SHELL_PROMPT_USER + "> ")
        let cmd = keyboard.read_line()
        handle_command(cmd)
    end
end
