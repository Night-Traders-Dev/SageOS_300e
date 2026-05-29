/* Full Argument and Syscall Test for SageOS */
#include <stdint.h>
#include <stddef.h>

#define SYS_write      1
#define SYS_exit       60
#define SYS_getpid     39
#define SYS_nanosleep  35

long syscall3(long num, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile (
        "mov x8, %1\n"
        "mov x0, %2\n"
        "mov x1, %3\n"
        "mov x2, %4\n"
        "svc #0\n"
        "mov %0, x0"
        : "=r"(ret)
        : "r"(num), "r"(a1), "r"(a2), "r"(a3)
        : "x0", "x1", "x2", "x8", "memory"
    );
    return ret;
}

long syscall1(long num, long a1) {
    long ret;
    __asm__ volatile (
        "mov x8, %1\n"
        "mov x0, %2\n"
        "svc #0\n"
        "mov %0, x0"
        : "=r"(ret)
        : "r"(num), "r"(a1)
        : "x0", "x8", "memory"
    );
    return ret;
}

static void print(const char *s) {
    size_t l = 0;
    while (s[l]) l++;
    syscall3(SYS_write, 1, (long)s, l);
}

void main_c(int argc, char **argv) {
    print("\n--- Userspace Started ---\n");
    print("argc is: ");
    char c = '0' + (argc % 10);
    syscall3(SYS_write, 1, (long)&c, 1);
    print("\n");

    for (int i = 0; i < argc; i++) {
        print("argv[");
        char idx = '0' + (i % 10);
        syscall3(SYS_write, 1, (long)&idx, 1);
        print("]: ");
        if (argv[i]) print(argv[i]);
        else print("(null)");
        print("\n");
    }

    long pid = syscall1(SYS_getpid, 0);
    print("My PID is: ");
    char p = '0' + (pid % 10);
    syscall3(SYS_write, 1, (long)&p, 1);
    print("\n");

    print("Sleeping for 1 second...\n");
    struct { long tv_sec; long tv_nsec; } req = { 1, 0 };
    /* nanosleep(req, rem) */
    register long x8 __asm__("x8") = SYS_nanosleep;
    register long x0 __asm__("x0") = (long)&req;
    register long x1 __asm__("x1") = 0;
    __asm__ volatile ("svc #0" : : "r"(x8), "r"(x0), "r"(x1) : "memory");

    print("Execution complete. Exiting.\n");
    syscall3(SYS_exit, 0, 0, 0);
}

__asm__ (
    ".section .text\n"
    ".global _start\n"
    "_start:\n"
    "    mov x29, #0\n"
    "    mov x30, #0\n"
    "    ldr x0, [sp]\n"
    "    add x1, sp, #8\n"
    "    bl main_c\n"
);
