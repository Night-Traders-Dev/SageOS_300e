#include "bootinfo.h"
#include "bootlog.h"
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
#include "wifi_qca6174.h"
#include "metal_vm.h"
#include "vmm.h"
#include "phys_alloc.h"

extern void register_timer_native_bindings(MetalVM* vm);
extern void register_bootlog_native_bindings(MetalVM* vm);
extern void register_power_native_bindings(MetalVM* vm);
extern void register_status_native_bindings(MetalVM* vm);
extern void register_battery_native_bindings(MetalVM* vm);
extern void sage_kernel_early_init(void);

extern MetalVM g_repl_vm;

extern int fat32_init(void);

static void shell_main_thread(void *arg) {
    (void)arg;

    dmesg_log("shell thread started");
    bootlog("[KRN] shell thread running\r\n");
    /* Force a framebuffer flush immediately so the boot banner is visible
     * before we block on the first keyboard_wait_event() call. */
    console_periodic_flip();
    for (;;) {
        timer_poll();
        sage_shell_run();
        sched_yield();  /* Yield to allow other threads to run */
    }
}


static SageOSBootInfo *g_boot_info = NULL;

SageOSBootInfo *kernel_get_boot_info(void) {
    return g_boot_info;
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
    g_boot_info = info;
    int firmware_input = firmware_input_mode(info);

    sage_kernel_early_init();

    phys_init(info);
    if (!firmware_input) {
        vmm_init();
    }

    serial_init();
    console_init(info);
    bootlog_init(info);
    bootlog("[KRN] kmain entered\r\n");
    bootlog("[KRN] serial_init OK\r\n");
    bootlog("[KRN] console_init OK\r\n");

    dmesg_log("SageOS modular kernel starting...");
    dmesg_log("serial and console initialized");

    bootlog(firmware_input
        ? "[KRN] mode: firmware-input (boot services active)\r\n"
        : "[KRN] mode: native (boot services exited)\r\n");

    bootlog("[KRN] acpi_init: start\r\n");
    acpi_init(info);
    dmesg_log("ACPI initialized");
    bootlog("[KRN] acpi_init: OK\r\n");

    if (!firmware_input) {
        bootlog("[KRN] smp_init: start\r\n");
        smp_init();
        dmesg_log("SMP initialized");
        bootlog("[KRN] smp_init: OK\r\n");
    } else {
        bootlog("[KRN] smp_init_firmware_bsp: start\r\n");
        smp_init_firmware_bsp();
        dmesg_log("SMP initialized (firmware input mode)");
        bootlog("[KRN] smp_init_firmware_bsp: OK\r\n");
    }

    bootlog("[KRN] timer_init: start\r\n");
    extern void timer_init(void);
    timer_init();
    dmesg_log("timer initialized");
    bootlog("[KRN] timer_init: OK\r\n");

    if (!firmware_input) {
        bootlog("[KRN] idt_init: start\r\n");
        idt_init();
        dmesg_log("IDT initialized");
        bootlog("[KRN] idt_init: OK\r\n");

        bootlog("[KRN] ata_init: start\r\n");
        ata_init();
        dmesg_log("ATA initialized");
        bootlog("[KRN] ata_init: OK\r\n");

        bootlog("[KRN] irq_enable\r\n");
        irq_enable();
    } else {
        dmesg_log("skipping IDT/IRQ initialization (firmware input mode)");
        bootlog("[KRN] skipping IDT/IRQ (firmware input mode)\r\n");

        bootlog("[KRN] ata_init (polling): start\r\n");
        ata_init();
        dmesg_log("ATA initialized (polling mode)");
        bootlog("[KRN] ata_init (polling): OK\r\n");
    }

    bootlog("[KRN] battery_init: start\r\n");
    battery_init();
    dmesg_log("battery subsystem initialized");
    bootlog("[KRN] battery_init: OK\r\n");

    bootlog("[KRN] ramfs_init: start\r\n");
    ramfs_init();
    dmesg_log("RamFS initialized");
    bootlog("[KRN] ramfs_init: OK\r\n");

    bootlog("[KRN] vfs_init: start\r\n");
    vfs_init();
    vfs_mount("/", ramfs_get_backend());
    dmesg_log("VFS initialized - ramfs mounted at /");
    bootlog("[KRN] vfs_init: OK\r\n");

    bootlog("[KRN] fat32_init: start\r\n");
    fat32_init();
    if (fat32_is_available()) {
        vfs_mount("/fat32", fat32_get_backend());
        dmesg_log("FAT32 mounted at /fat32");
        bootlog("[KRN] fat32_init: mounted at /fat32\r\n");
    } else {
        dmesg_log("FAT32 not available");
        bootlog("[KRN] fat32_init: not available\r\n");
    }

    bootlog("[KRN] btrfs_init: start\r\n");
    btrfs_init();
    if (btrfs_is_available()) {
        vfs_mount("/btrfs", btrfs_get_backend());
        dmesg_log("BTRFS mounted at /btrfs");
        bootlog("[KRN] btrfs_init: mounted at /btrfs\r\n");
    } else {
        bootlog("[KRN] btrfs_init: not available\r\n");
    }

    bootlog("[KRN] swap_init: start\r\n");
    swap_init();
    dmesg_log("SWAP initialized");
    bootlog("[KRN] swap_init: OK\r\n");

    /* PCI bus enumeration - discovers AMD SoC, QCA6174A Wi-Fi, eMMC */
    bootlog("[KRN] pci_enumerate: start\r\n");
    pci_enumerate();
    dmesg_log("PCI bus enumerated");
    bootlog("[KRN] pci_enumerate: OK\r\n");

    bootlog("[KRN] sdhci_init: start\r\n");
    sdhci_init();
    dmesg_log("SDHCI initialized");
    bootlog("[KRN] sdhci_init: OK\r\n");

    bootlog("[KRN] net_init: start\r\n");
    net_init();
    extern void lwip_port_init(void);
    lwip_port_init();
    dmesg_log("network subsystem initialized (lwIP active)");
    bootlog("[KRN] net_init: OK\r\n");

    /* Attempt auto-connect from saved /fat32/WIFI.CFG */
    bootlog("[KRN] wifi_auto_connect: start\r\n");
    qca6174_auto_connect();
    bootlog("[KRN] wifi_auto_connect: done\r\n");

    bootlog("[KRN] keyboard_init: start\r\n");
    keyboard_init();
    dmesg_log("keyboard initialized");
    bootlog("[KRN] keyboard_init: OK\r\n");

    bootlog("[KRN] status_init: start\r\n");
    status_init();
    dmesg_log("status bar initialized");
    bootlog("[KRN] status_init: OK\r\n");

    bootlog("[KRN] dmesg_load_persistent: start\r\n");
    dmesg_load_persistent();
    dmesg_log("persistent dmesg loaded");
    bootlog("[KRN] dmesg_load_persistent: OK\r\n");

    bootlog("[KRN] sched_init: start\r\n");
    sched_init();
    dmesg_log("scheduler initialized");
    bootlog("[KRN] sched_init: OK\r\n");

    dmesg_log("init system starting");
    bootlog("[KRN] sage_init_run: start\r\n");
    sage_init_run();
    bootlog("[KRN] sage_init_run: OK\r\n");

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

    /*
     * Flush the back buffer to the physical framebuffer now.
     * In firmware-input mode the PIT timer is not initialized so
     * timer_irq() / console_periodic_flip() never fires automatically.
     */
    console_periodic_flip();
    bootlog("[KRN] framebuffer flushed\r\n");

    dmesg_log("creating main shell thread");
    bootlog("[KRN] creating shell thread\r\n");

    sched_create_thread("shell-main", shell_main_thread, NULL, THREAD_PRIORITY_NORMAL);

    bootlog("[KRN] sched_start - log ends here\r\n");
    bootlog_close();

    /* Start scheduling - this will not return */
    sched_start();

    /* Should never reach here */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
