.section .text
.global _start
_start:
    # Print something via a BIOS call if available?
    # No, let's just make it a loop.
    jmp _start
