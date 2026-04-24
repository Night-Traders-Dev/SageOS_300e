.global _start
.extern kmain

.section .text
.code64

_start:
    cld

    lea stack_top(%rip), %rsp
    andq $-16, %rsp

    /*
     * UEFI loader is built for Microsoft x64 ABI.
     * It passes the first argument in RCX.
     *
     * Kernel C is built for SysV x86_64 ABI.
     * SysV expects the first argument in RDI.
     */
    mov %rcx, %rdi

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
    
    /* Call entry point */
    movq ap_entry_ptr(%rip), %rax
    
    /* Pass APIC ID or other info if needed, but for now just call */
    /* We can read LAPIC ID to identify ourselves in C */
    push %rax
    mov $0x0020, %edi /* LAPIC_ID offset */
    /* We need lapic_base, but we can't easily get it here without a global. 
       Actually, the C code can handle identification via LAPIC_ID. */
    pop %rax
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
