#include "bootinfo.h"
#include "serial.h"
#include "console.h"
#include "keyboard.h"
#include "shell.h"
#include "../shell/sage_shell_entry.h"
#include "status.h"
#include "timer.h"
#include "acpi.h"
#include "smp.h"
#include "battery.h"
#include "idt.h"
#include "vfs.h"
#include "fat32.h"
#include "btrfs.h"
#include "ramfs.h"
#include "swap.h"
#include "pci.h"
#include "sdhci.h"
#include "version.h"
#include "dmesg.h"
#include "ata.h"
#include "scheduler.h"
#include "net.h"

extern int fat32_init(void);

static void shell_main_thread(void *arg) {
    (void)arg;

    dmesg_log("shell thread started");
    for (;;) {
        timer_poll();
        sage_shell_run();
        sched_yield();  /* Yield to allow other threads to run */
    }
}


static int firmware_input_mode(SageOSBootInfo *info) {
    return
        info &&
        info->magic == SAGEOS_BOOT_MAGIC &&
        info->boot_services_active &&
        info->input_mode == 1 &&
        info->con_in;
}

void kmain(SageOSBootInfo *info) {
    int firmware_input = firmware_input_mode(info);

    serial_init();
    console_init(info);

    dmesg_log("SageOS modular kernel starting...");
    dmesg_log("serial and console initialized");

    acpi_init(info);
    dmesg_log("ACPI initialized");

    if (!firmware_input) {
        smp_init();
        dmesg_log("SMP initialized");
    } else {
        smp_init_firmware_bsp();
        dmesg_log("SMP initialized (firmware input mode)");
    }

    if (!firmware_input) {
        /* Timer-driven status updates and CPU accounting */
        timer_init();
        dmesg_log("timer initialized");
        idt_init();
        dmesg_log("IDT initialized");
        ata_init();
        dmesg_log("ATA initialized");
        irq_enable();
    } else {
        dmesg_log("skipping IDT/timer initialization (firmware input mode)");
        /*
         * We still need ATA initialized even if we don't use interrupts for it
         * (ATA polling mode will be used if interrupts aren't available).
         */
        ata_init();
        dmesg_log("ATA initialized (polling mode)");
    }

    battery_init();
    dmesg_log("battery subsystem initialized");

    ramfs_init();
    dmesg_log("RamFS initialized");
    vfs_init();
    vfs_mount("/", ramfs_get_backend());
    dmesg_log("VFS initialized — ramfs mounted at /");
    fat32_init();
    if (fat32_is_available()) {
        vfs_mount("/fat32", fat32_get_backend());
        dmesg_log("FAT32 mounted at /fat32");
    } else {
        dmesg_log("FAT32 not available");
    }

    btrfs_init();
    if (btrfs_is_available()) {
        vfs_mount("/btrfs", btrfs_get_backend());
        dmesg_log("BTRFS mounted at /btrfs");
    }

    swap_init();
    dmesg_log("SWAP initialized");

    /* PCI bus enumeration — discovers AMD SoC, QCA6174A Wi-Fi, eMMC */
    pci_enumerate();
    dmesg_log("PCI bus enumerated");
    sdhci_init();
    dmesg_log("SDHCI initialized");
    net_init();
    dmesg_log("network subsystem initialized");

    keyboard_init();
    dmesg_log("keyboard initialized");
    status_init();
    dmesg_log("status bar initialized");

    dmesg_load_persistent();
    dmesg_log("persistent dmesg loaded");

    sched_init();
    dmesg_log("scheduler initialized");

    /* Initialize system services */
    dmesg_log("init system starting");
    sage_init_run();

    console_write("SageOS modular kernel v" SAGEOS_VERSION " entered.\n");
    console_write("Framebuffer console online.\n");
    console_write("Keyboard backend: ");
    console_write(keyboard_backend());
    console_write("\n");
    console_write("PCI devices: ");
    console_u32((uint32_t)pci_device_count());
    console_write("\nNetwork devices: ");
    console_u32((uint32_t)net_device_count());
    console_write("\n");
    console_write("Type help to list commands.\n");

    dmesg_log("creating main shell thread");

    /* Create main shell thread */
    sched_create_thread("shell-main", shell_main_thread, NULL, THREAD_PRIORITY_NORMAL);

    /* Start scheduling - this will not return */
    sched_start();

    /* Should never reach here */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
