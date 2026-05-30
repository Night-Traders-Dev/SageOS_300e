void _start() {
    // Write "Hello Syscall!\n" to fd 1 (stdout)
    const char *msg = "Hello Syscall!\n";
    long len = 15;
    long ret;
    __asm__ volatile (
        "mov $1, %%rax\n" // SYS_write (on x86_64, SYS_write is 1)
        "mov $1, %%rdi\n" // fd = 1
        "mov %1, %%rsi\n" // buf
        "mov %2, %%rdx\n" // count
        "syscall\n"
        "mov %%rax, %0\n"
        : "=r"(ret)
        : "r"(msg), "r"(len)
        : "rax", "rdi", "rsi", "rdx", "rcx", "r11", "memory"
    );

    // Exit
    __asm__ volatile (
        "mov $60, %%rax\n" // SYS_exit (on x86_64, SYS_exit is 60)
        "mov $0, %%rdi\n"  // status = 0
        "syscall\n"
        :::"rax", "rdi"
    );
}
