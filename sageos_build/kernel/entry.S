.global _start
.extern kmain

.section .rodata
.align 16
kernel_gdt:
    .quad 0x0000000000000000   /* 0x00: null */
    .quad 0x00AF9A000000FFFF   /* 0x08: 64-bit code, DPL0, present */
    .quad 0x00AF92000000FFFF   /* 0x10: 64-bit data, DPL0, present */
kernel_gdt_end:

.section .text
.code64

_start:
    cli
    cld
    mov %rcx, %rbx

    /*
     * KERNEL.BIN is loaded as a flat image, so the loader does not apply ELF
     * NOBITS semantics. Clear BSS before touching C globals or the stack.
     */
    lea __bss_start(%rip), %rdi
    lea __bss_end(%rip), %rcx
    sub %rdi, %rcx
    xor %eax, %eax
    rep stosb

    /*
     * Install our own GDT so we are not relying on OVMF's GDT layout.
     * OVMF's selector 0x38 is not guaranteed to be a 64-bit code segment
     * across all firmware versions — a wrong selector in an IDT gate causes
     * an immediate triple fault on the first exception.
     *
     * Build the GDTR on the stack at runtime to avoid any linker absolute
     * address embedding issue in a flat binary.
     *
     * Our GDT layout:
     *   0x00 null
     *   0x08 64-bit code (DPL 0)
     *   0x10 64-bit data (DPL 0)
     *
     * GDTR format (10 bytes): [limit:16][base:64]
     * We build it on the stack: push base (8 bytes), then push limit word.
     * The sub/movw approach ensures the structure is correctly laid out.
     */
    lea kernel_gdt(%rip), %rax      /* base of GDT */
    subq $16, %rsp                  /* make room on stack for GDTR */
    movq %rax, 2(%rsp)              /* store 64-bit base at offset +2 */
    movw $(kernel_gdt_end - kernel_gdt - 1), %cx
    movw %cx, 0(%rsp)               /* store 16-bit limit at offset +0 */
    lgdt (%rsp)
    addq $16, %rsp

    /* Reload data segment registers with the new data selector. */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    /*
     * Far-return to flush CS with our 64-bit code selector (0x08).
     * We push the new CS:RIP pair and lretq to atomically reload CS.
     */
    lea .Lafter_lgdt(%rip), %rax
    pushq $0x08       /* new CS */
    pushq %rax        /* return RIP */
    lretq
.Lafter_lgdt:

    lea stack_top(%rip), %rsp
    andq $-16, %rsp

    /*
     * UEFI loader is built for Microsoft x64 ABI.
     * It passes the first argument in RCX.
     *
     * Kernel C is built for SysV x86_64 ABI.
     * SysV expects the first argument in RDI.
     */
    mov %rbx, %rdi

    call kmain

.hang:
    hlt
    jmp .hang

/*
 * AP Trampoline
 * This code will be copied to 0x8000.
 */
.align 4096
.global ap_trampoline
.global ap_trampoline_end
ap_trampoline:
.code16
    cli
    cld
    xor %ax, %ax
    mov %ax, %ds

    /* Load temporary GDT */
    lgdtl (ap_gdt_ptr - ap_trampoline + 0x8000)

    /* Enable Protected Mode */
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0

    /* Long jump to 32-bit code segment */
    ljmpl $0x08, $(ap_trampoline_32 - ap_trampoline + 0x8000)

.code32
ap_trampoline_32:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    /* 
     * Transition to 64-bit mode.
     */
    
    /* 1. Enable PAE */
    mov %cr4, %eax
    or $0x20, %eax
    mov %eax, %cr4

    /* 2. Load CR3 with BSP's page table root */
    /* Use absolute address for 32-bit access: ap_cr3_ptr is at a fixed offset from 0x8000 */
    mov (ap_cr3_ptr - ap_trampoline + 0x8000), %eax
    mov %eax, %cr3

    /* 3. Enable Long Mode in EFER MSR */
    mov $0xC0000080, %ecx
    rdmsr
    or $0x100, %eax
    wrmsr

    /* 4. Enable Paging */
    mov %cr0, %eax
    or $0x80000000, %eax
    mov %eax, %cr0

    /* 5. Load 64-bit GDT and jump */
    lgdt (ap_gdt_ptr_64 - ap_trampoline + 0x8000)
    ljmp $0x18, $(ap_trampoline_64 - ap_trampoline + 0x8000)

.code64
ap_trampoline_64:
    /* Load stack pointer */
    movq ap_stack_ptr(%rip), %rsp
    
    /* Load AP entry point */
    movq ap_entry_ptr(%rip), %rax
    
    /* Read LAPIC ID (at 0xFEE00020) and pass as first argument */
    mov $0xFEE00020, %rbx
    mov (%rbx), %edi
    shr $24, %edi
    
    /* Call entry point */
    call *%rax

.ap_hang:
    hlt
    jmp .ap_hang

.align 16
ap_gdt_32:
    .quad 0x0000000000000000 /* Null */
    .quad 0x00CF9A000000FFFF /* 32-bit Code (0x08) */
    .quad 0x00CF92000000FFFF /* 32-bit Data (0x10) */
ap_gdt_ptr:
    .word . - ap_gdt_32 - 1
    .long ap_gdt_32 - ap_trampoline + 0x8000

.align 16
ap_gdt_64:
    .quad 0x0000000000000000 /* Null */
    .quad 0x00CF9A000000FFFF /* 32-bit Code (0x08) - still needed for transition */
    .quad 0x00CF92000000FFFF /* 32-bit Data (0x10) */
    .quad 0x00AF9A000000FFFF /* 64-bit Code (0x18) */
ap_gdt_ptr_64:
    .word . - ap_gdt_64 - 1
    .quad ap_gdt_64 - ap_trampoline + 0x8000

.align 8
.global ap_stack_ptr
.global ap_entry_ptr
.global ap_cr3_ptr
ap_stack_ptr: .quad 0
ap_entry_ptr: .quad 0
ap_cr3_ptr:   .quad 0

ap_trampoline_end:

.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:
