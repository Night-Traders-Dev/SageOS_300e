ENTRY(_start)

SECTIONS
{
    . = 1M;
    __kernel_start = .;

    .text ALIGN(4K) :
    {
        *(.text*)
    }

    .rodata ALIGN(4K) :
    {
        *(.rodata*)
    }

    .data ALIGN(4K) :
    {
        *(.data*)
    }

    .bss ALIGN(4K) :
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        __bss_end = .;
    }

    __kernel_end = .;
}

/* Patch 4: Fail at link time if extent symbols used by lenovo_300e.sh
 * to size KERNEL.BIN are absent.  Never let a silent truncate() silently
 * produce a zero-length or wrongly-sized binary again.                  */
ASSERT(DEFINED(__kernel_start),
    "__kernel_start undefined — add PROVIDE(__kernel_start = .) at section start")
ASSERT(DEFINED(__kernel_end),
    "__kernel_end undefined   — add PROVIDE(__kernel_end   = .) at section end")
