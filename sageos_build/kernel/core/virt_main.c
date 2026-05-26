#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "keyboard.h"
#include "shell.h"
#include "bootinfo.h"
#include "version.h"

// External SageLang runtime init (dummy for now)
void sage_kernel_early_init(void) {}
void sage_shell_run(void) {
    console_write("SageLang VM not available in this virt build.\n");
}

void power_reboot(void) {
    console_write("Rebooting...\n");
    while (1) {
#if defined(__x86_64__)
        __asm__ volatile ("hlt");
#elif defined(__aarch64__)
        __asm__ volatile ("wfe");
#elif defined(__riscv)
        __asm__ volatile ("wfi");
#endif
    }
}

void power_shutdown(void) {
    console_write("Shutting down...\n");
    while (1) {
#if defined(__x86_64__)
        __asm__ volatile ("hlt");
#elif defined(__aarch64__)
        __asm__ volatile ("wfe");
#elif defined(__riscv)
        __asm__ volatile ("wfi");
#endif
    }
}

void kmain(SageOSBootInfo *info) {
    // 1. Initialize hardware (Serial & Console)
    console_init(info);
    
    console_write("\n\033[1;36mSageOS Kernel (Virt) starting...\033[0m\n");
    console_write("Version: "); console_write(SAGEOS_VERSION); console_write("\n");
    
    // 3. Launch Shell
    console_write("Launching C Shell...\n");
    shell_run();
    
    // If shell exits, halt
    console_write("\nSystem halted.\n");
    while (1) {
#if defined(__x86_64__)
        __asm__ volatile ("hlt");
#elif defined(__aarch64__)
        __asm__ volatile ("wfe");
#elif defined(__riscv)
        __asm__ volatile ("wfi");
#endif
    }
}
