## metal.core — Bare-metal core primitives for Sage (Stabilized x86_64)
##
## Provides real hardware interop using asm_exec for port I/O,
## MMIO, and CPU control instructions.

## ============================================================
## Port I/O (x86_64)
## ============================================================

## Write a byte to an I/O port
proc outb(port, val):
    unsafe:
        asm_exec("outb %al, %dx", "void", port, val)
    end
end

## Read a byte from an I/O port
proc inb(port):
    unsafe:
        return asm_exec("inb %dx, %al", "byte", port)
    end
end

## Write a 16-bit word to an I/O port
proc outw(port, val):
    unsafe:
        asm_exec("outw %ax, %dx", "void", port, val)
    end
end

## Read a 16-bit word from an I/O port
proc inw(port):
    unsafe:
        return asm_exec("inw %dx, %ax", "int", port)
    end
end

## Write a 32-bit dword to an I/O port
proc outl(port, val):
    unsafe:
        asm_exec("outl %eax, %dx", "void", port, val)
    end
end

## Read a 32-bit dword from an I/O port
proc inl(port):
    unsafe:
        return asm_exec("inl %dx, %eax", "int", port)
    end
end

## ============================================================
## CPU Control
## ============================================================

## Disable interrupts
proc cli():
    unsafe:
        asm_exec("cli", "void")
    end
end

## Enable interrupts
proc sti():
    unsafe:
        asm_exec("sti", "void")
    end
end

## Halt CPU (wait for interrupt)
proc hlt():
    unsafe:
        asm_exec("hlt", "void")
    end
end

## I/O wait (delay one I/O cycle)
proc io_wait():
    outb(0x80, 0)
end

## ============================================================
## Memory-Mapped I/O (MMIO)
## ============================================================

## Read a 16-bit value from a physical address
proc mmio_read16(addr):
    unsafe:
        return mem_read(addr, 0, "short")
    end
end

## Write a 16-bit value to a physical address
proc mmio_write16(addr, val):
    unsafe:
        mem_write(addr, 0, "short", val)
    end
end

## Read a 32-bit dword from a physical address
proc mmio_read32(addr):

    unsafe:
        return mem_read(addr, 0, "int")
    end
end

## Write a 32-bit value to a physical address
proc mmio_write32(addr, val):
    unsafe:
        mem_write(addr, 0, "int", val)
    end
end

## Read a byte from a physical address
proc mmio_read8(addr):
    unsafe:
        return mem_read(addr, 0, "byte")
    end
end

## Write a byte to a physical address
proc mmio_write8(addr, val):
    unsafe:
        mem_write(addr, 0, "byte", val)
    end
end

## ============================================================
## Timing
## ============================================================

proc delay_us(us):
    # For now, approximate busy-loop
    # A real implementation would use TSC or PIT
    let cycles = us * 10
    for i in range(cycles):
        unsafe: asm_exec("pause", "void") end
    end
end

proc delay_ms(ms):
    for i in range(ms):
        delay_us(1000)
    end
end

## ============================================================
## Panic / Halt
## ============================================================

proc panic(msg):
    print "PANIC: " + msg
    cli()
    while true:
        hlt()
    end
end
