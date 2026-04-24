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

extern void fat32_init(void);

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

void kmain(SageOSBootInfo *info) {
    serial_init();
    console_init(info);

    acpi_init(info);
    smp_init();

    idt_init();
    timer_init();
    irq_enable();

    battery_init();

    vfs_init();
    fat32_init();

    keyboard_init();
    status_init();

    banner();

    console_write("SageOS modular kernel v0.0.10 entered.\n");
    console_write("Framebuffer console online.\n");
    console_write("Keyboard backend: ");
    console_write(keyboard_backend());
    console_write("\n");
    console_write("Type help to list commands.\n");

    shell_run();
}
