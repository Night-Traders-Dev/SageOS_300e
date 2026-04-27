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
#include "ramfs.h"
#include "pci.h"
#include "sdhci.h"
#include "version.h"
#include "dmesg.h"
#include "ata.h"

extern int fat32_init(void);

static void banner(void) {
    uint32_t old = console_get_fg();

    console_set_fg(0x79FFB0);
    console_write("  ____    _    ____ _____ ___  ____  \n");
    console_write(" / ___|  / \\  / ___| ____/ _ \\/ ___| \n");
    console_write(" \\___ \\ / _ \\| |  _|  _|| | | \\___ \\ \n");
    console_write("  ___) / ___ \\ |_| | |__| |_| |___) |\n");
    console_write(" |____/_/   \\_\\____|_____\\___/|____/ \n");
    console_set_fg(old);
    console_write("\n");
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

    /* Timer-driven status updates and CPU accounting must work even when
       firmware console input is active. */
    timer_init();
    dmesg_log("timer initialized");
    idt_init();
    dmesg_log("IDT initialized");
    ata_init();
    dmesg_log("ATA initialized");
    irq_enable();

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

    /* PCI bus enumeration — discovers AMD SoC, QCA6174A Wi-Fi, eMMC */
    pci_enumerate();
    dmesg_log("PCI bus enumerated");
    sdhci_init();
    dmesg_log("SDHCI initialized");

    keyboard_init();
    dmesg_log("keyboard initialized");
    status_init();
    dmesg_log("status bar initialized");

    banner();

    console_write("SageOS modular kernel v" SAGEOS_VERSION " entered.\n");
    console_write("Framebuffer console online.\n");
    console_write("Keyboard backend: ");
    console_write(keyboard_backend());
    console_write("\n");
    console_write("PCI devices: ");
    console_u32((uint32_t)pci_device_count());
    console_write("\n");
    console_write("Type help to list commands.\n");

    dmesg_log("shell starting");
    for (;;) {
        timer_poll();
        sage_shell_run();
    }
}
