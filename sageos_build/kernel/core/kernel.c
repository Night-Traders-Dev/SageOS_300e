#include "bootinfo.h"
#include "serial.h"
#include "console.h"
#include "keyboard.h"
#include "shell.h"
#include "status.h"
#include "timer.h"
#include "acpi.h"
#include "smp.h"
#include "battery.h"
#include "idt.h"
#include "vfs.h"
#include "fat32.h"

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

    acpi_init(info);

    if (!firmware_input) {
        smp_init();
    } else {
        smp_init_firmware_bsp();
    }

    /* Timer-driven status updates and CPU accounting must work even when
       firmware console input is active. */
    timer_init();
    idt_init();
    irq_enable();

    battery_init();

    vfs_init();
    fat32_init();

    keyboard_init();
    status_init();

    banner();

    console_write("SageOS modular kernel v0.1.1 entered.\n");
    console_write("Framebuffer console online.\n");
    console_write("Keyboard backend: ");
    console_write(keyboard_backend());
    console_write("\n");
    console_write("Type help to list commands.\n");

    shell_run();
}
