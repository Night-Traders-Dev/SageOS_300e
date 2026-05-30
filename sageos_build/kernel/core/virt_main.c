#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "keyboard.h"
#include "shell.h"
#include "bootinfo.h"
#include "version.h"

#include "vfs.h"
#include "ata.h"
#include "fat32.h"
#include "btrfs.h"
#include "swap.h"
#include "bootlog.h"
#include "dmesg.h"

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

extern void serial_init(void);

#include "syscall_numbers.h"
long syscall_dispatch(long num, long a1, long a2, long a3, long a4, long a5);

#if defined(__x86_64__)
static uint64_t g_gdt64[] = {
    0,                          // Null descriptor
    0x00209A0000000000ULL,      // Kernel Code (0x08)
    0x0000920000000000ULL,      // Kernel Data (0x10)
    0x0000F20000000000ULL,      // User Data (0x18)
    0x0020FA0000000000ULL,      // User Code (0x20)
};

struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) g_gdtr64 = {
    .limit = sizeof(g_gdt64) - 1,
    .base = (uint64_t)g_gdt64
};

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t low = (uint32_t)val;
    uint32_t high = (uint32_t)(val >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high) : "memory");
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t low, high;
    __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

extern void syscall_entry(void);

static void x86_64_syscall_init(void) {
    // 1. Load new GDT
    __asm__ volatile ("lgdt %0" : : "m"(g_gdtr64));

    // 2. Enable SCE (System Call Enable) in EFER MSR (0xC0000080)
    uint64_t efer = rdmsr(0xC0000080);
    wrmsr(0xC0000080, efer | 1);

    // 3. Configure STAR MSR (0xC0000081)
    // STAR = (User_Base_Selector << 48) | (Kernel_CS_Selector << 32)
    uint64_t star = ((uint64_t)0x13 << 48) | ((uint64_t)0x08 << 32);
    wrmsr(0xC0000081, star);

    // 4. Configure LSTAR MSR (0xC0000082) with the address of syscall_entry
    wrmsr(0xC0000082, (uint64_t)syscall_entry);

    // 5. Configure FMASK MSR (0xC0000084) to mask flags (disable interrupts and clear DF, TF)
    wrmsr(0xC0000084, 0x3F7FD5);
}
#endif

#if defined(__aarch64__)
extern void* exception_vectors;
static void setup_vectors(void) {
    __asm__ volatile ("msr vbar_el1, %0" : : "r"(&exception_vectors));
}
#endif

void kmain(SageOSBootInfo *info) {
    serial_init();
    
    // Initialize console, keyboard, VFS, RamFS
    console_init(info);
    console_clear();
    console_write("SageOS Virt Kernel Booting...\n");

#if defined(__x86_64__)
    x86_64_syscall_init();
#elif defined(__aarch64__)
    setup_vectors();
#endif
    
    keyboard_init();
    vfs_init();


    // Initialize block device subsystem (ATA/Virtio)
    ata_init();

    // Initialize and mount filesystems
    if (fat32_init()) {
        vfs_mount("/fat32", fat32_get_backend());
        vfs_mount("/usr", fat32_get_backend());
        dmesg_log("VFS: Mounted FAT32 at /fat32 and /usr");
    } else {
        dmesg_log("VFS: FAT32 initialization FAILED");
    }
    
    if (btrfs_init()) {
        vfs_mount("/", btrfs_get_backend());
        dmesg_log("VFS: Mounted BTRFS at /");
    } else {
        dmesg_log("VFS: BTRFS initialization FAILED");
    }

    swap_init();
    bootlog_init(info);
    dmesg_log("SageOS Virt Kernel initialization complete.");
    
    /* GCC Port Phase 0: Syscall Smoke Test */
    dmesg_log("Syscall Smoke Test: Calling SYS_write to stdout...");
    syscall_dispatch(SYS_write, 1, (long)"[SYSCALL TEST] Hello via syscall_dispatch\n", 42, 0, 0);

    /* Milestone 2: Execute userspace Hello World */
    console_write("Milestone 2: Attempting to exec /fat32/hello with args...\n");
    char *argv[] = {"/fat32/hello", "arg1", "arg2", NULL};
    
    long pid = syscall_dispatch(SYS_vfork, 0, 0, 0, 0, 0);
    if (pid == 0) {
        /* Child */
        syscall_dispatch(SYS_execve, (long)"/fat32/hello", (long)argv, 0, 0, 0);
        syscall_dispatch(SYS_exit, 1, 0, 0, 0, 0);
    } else {
        /* Parent */
        int status;
        syscall_dispatch(SYS_waitpid, pid, (long)&status, 0, 0, 0);
        console_write("Child exited. Parent resuming.\n");
    }

    console_write("\n[DEBUG] Before shell_run, swap is: ");
    console_u32(swap_is_available());
    
    // Launch interactive C shell
    shell_run();
}
