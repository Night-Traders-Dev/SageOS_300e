unsafe:
    asm_exec("mov $0x3F8, %dx; mov $0x41, %al; out %al, %dx")
end
