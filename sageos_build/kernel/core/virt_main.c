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
#include "scheduler.h"

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

#include "boot_stages.h"
#include "phys_alloc.h"
#include "vmm.h"

extern void sage_runtime_init(void);

void kmain(SageOSBootInfo *info) {
    sched_init();
    static SageOSBootInfo dummy_info;
    if (!info || info->magic != SAGEOS_BOOT_MAGIC) {
        // Construct dummy info for bare-metal boot
        __builtin_memset(&dummy_info, 0, sizeof(dummy_info));
        dummy_info.magic = SAGEOS_BOOT_MAGIC;
        
        // Architecture-specific defaults (must match linker.ld base addresses)
#if defined(__x86_64__)
        dummy_info.kernel_base = 0x100000; // 1MB
#elif defined(__aarch64__)
        dummy_info.kernel_base = 0x40000000; // 1GB
#elif defined(__riscv)
        dummy_info.kernel_base = 0x80000000; // 2GB
#endif
        
        dummy_info.kernel_size = 0x1000000; 
        dummy_info.memory_total = 1024 * 1024 * 1024; // 1GB
        dummy_info.memory_usable = 1024 * 1024 * 1024;
        info = &dummy_info;
    }

    // --- STAGE 1: Early Memory Management ---
    sageos_set_boot_stage(STAGE_1_EARLY_MM);
    
    serial_init();
    
    extern void telemetry_init(void);
    telemetry_init();

    // Initialize console, keyboard, VFS, RamFS
    console_init(info);
    console_clear();
    console_write("SageOS Virt Kernel Booting...\n");
    
    phys_init(info);
    vmm_init();

    // --- STAGE 2: IRQ & System Init ---
    sageos_set_boot_stage(STAGE_2_IRQ_INIT);

#if defined(__x86_64__)
    x86_64_syscall_init();
#elif defined(__aarch64__)
    setup_vectors();
#endif

    // --- STAGE 3: Device Discovery & IPC ---
    sageos_set_boot_stage(STAGE_3_DEVICE_DISCOVERY);
    
    extern void ipc_subsystem_init(void);
    ipc_subsystem_init();
    
    keyboard_init();
    extern void sage_timer_init(void);
    sage_timer_init();
    ata_init();

    // --- STAGE 4: Storage & VFS Mounting ---
    sageos_set_boot_stage(STAGE_4_STORAGE_VFS);
    vfs_init();

    // Initialize and mount filesystems
    if (fat32_init()) {
        vfs_mount("/", fat32_get_backend());
        vfs_mount("/mnt/fat32", fat32_get_backend());
        vfs_mount("/usr", fat32_get_backend());
        dmesg_log("VFS: Mounted FAT32 at / (root), /mnt/fat32 and /usr");
    } else {
        dmesg_log("VFS: FAT32 initialization FAILED");
    }
    
    if (btrfs_init()) {
        vfs_mount("/mnt/btrfs", btrfs_get_backend());
        dmesg_log("VFS: Mounted BTRFS at /mnt/btrfs");
    } else {
        dmesg_log("VFS: BTRFS initialization FAILED");
    }

    swap_init();
    console_write("\n[TRACE] After swap_init");
    bootlog_init(info);
    console_write("\n[TRACE] After bootlog_init");
    
    // --- STAGE 5: Runtime Bring-up ---
    sageos_set_boot_stage(STAGE_5_RUNTIME_BRINGUP);
    console_write("\n[TRACE] After stage 5 set");
    sage_runtime_init();
    console_write("\n[TRACE] After sage_runtime_init");

    dmesg_log("SageOS Virt Kernel initialization complete.");
    
    // --- STAGE 6: System Service Activation ---
    sageos_set_boot_stage(STAGE_6_SERVICE_ACTIVATION);
    console_write("\n[TRACE] After stage 6 set");
    extern void sage_execute_init(void);
    sage_execute_init();
    console_write("\n[TRACE] After sage_execute_init");
    
    // allow PID 1 to run asynchronously
    console_write("\n[TRACE] Entering multitasking idle loop...");
    while (1) {
        extern void timer_idle_poll(void);
        extern void sched_watchdog_update(void);
        sched_watchdog_update();
        timer_idle_poll();
        sched_schedule();
    }

    // --- STAGE 7: Userspace Session ---
    // (This stage is now handled by runtime_manager spawning the shell)
    sageos_set_boot_stage(STAGE_7_USERSPACE_SESSION);

    // Launch interactive C shell (Disabled in favor of Sage supervisor)
    // shell_run();
}
