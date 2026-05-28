/* toolchain/newlib/libc/sys/sageos/crt0.S — Unified SageOS startup */

#if defined(__x86_64__)

    .section .text
    .global _start
    .type   _start, @function
_start:
    andq    $-16, %rsp
    leaq    __bss_start(%rip), %rdi
    leaq    _end(%rip), %rcx
    subq    %rdi, %rcx
    xorl    %eax, %eax
    rep stosb
    movq    (%rsp), %rdi
    leaq    8(%rsp), %rsi
    leaq    8(%rsi,%rdi,8), %rdx
    call    main
    movl    %eax, %edi
    call    _exit
    ud2
    .size _start, .-_start

#elif defined(__aarch64__)

    .section .text
    .global _start
    .type   _start, %function
_start:
    and     sp, sp, #~15
    adrp    x0, __bss_start
    add     x0, x0, :lo12:__bss_start
    adrp    x1, _end
    add     x1, x1, :lo12:_end
    sub     x2, x1, x0
    mov     x1, #0
    bl      memset
    ldr     x0, [sp]
    add     x1, sp, #8
    add     x2, x1, x0, lsl #3
    add     x2, x2, #8
    bl      main
    mov     w1, w0
    mov     x8, #60
    svc     #0
    brk     #0
    .size _start, .-_start

#elif defined(__riscv)

    .section .text
    .global _start
    .type   _start, @function
_start:
    andi    sp, sp, -16
    la      a0, __bss_start
    la      a1, _end
    sub     a2, a1, a0
    li      a1, 0
    call    memset
    ld      a0, 0(sp)
    addi    a1, sp, 8
    slli    t0, a0, 3
    add     a2, a1, t0
    addi    a2, a2, 8
    call    main
    mv      a0, a0
    li      a7, 93
    ecall
    unimp
    .size _start, .-_start

#endif
