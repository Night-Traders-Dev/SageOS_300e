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

void kmain(SageOSBootInfo *info) {
    serial_init();
    
    // Initialize console, keyboard, VFS, RamFS
    console_init(info);
    console_clear();
    console_write("SageOS Virt Kernel Booting...\n");
    
    keyboard_init();
    vfs_init();

    // Initialize block device subsystem (ATA/Virtio)
    ata_init();

    // Initialize and mount filesystems
    if (fat32_init()) {
        vfs_mount("/fat32", fat32_get_backend());
        dmesg_log("VFS: Mounted FAT32 at /fat32");
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
    
    console_write("\n[DEBUG] Before shell_run, swap is: ");
    console_u32(swap_is_available());
    
    // Launch interactive C shell
    shell_run();
}
