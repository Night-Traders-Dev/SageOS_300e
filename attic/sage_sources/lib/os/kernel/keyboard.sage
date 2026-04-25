gc_disable()
import console
import core

let KEY_ESC = 1
let KEY_ENTER = 28
let KEY_BACKSPACE = 14
let KEY_LSHIFT = 42

let KBD_DATA_PORT = 96
let KBD_STATUS_PORT = 100

let hardware_mode = "hardware"

proc init():
    if hardware_mode == "hardware":
        core.outb(KBD_STATUS_PORT, 174)
    end
end

proc read_scancode():
    if hardware_mode == "hardware":
        if (core.inb(KBD_STATUS_PORT) & 1) != 0:
            return core.inb(KBD_DATA_PORT)
        end
    end
    return nil
end
