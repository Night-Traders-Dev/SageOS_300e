#include "io.h"
#include "console.h"
#include "power.h"
#include "acpi.h"
#include "sysinfo.h"

/*
 * power_qemu_exit
 *
 * Exits QEMU cleanly via the ISA debug-exit device (iobase 0x501, iosize 2).
 * Writing any value to port 0x501 causes QEMU to exit with code
 * ((value << 1) | 1).  We write 0 so the exit code is 1.
 *
 * If not running in QEMU, it calls acpi_poweroff() to shut down.
 */
void power_qemu_exit(void) {
    if (sysinfo_is_qemu()) {
        console_write("\nExiting QEMU...");
        outb(0x501, 0x00);
        /* Fallback: halt */
        for (;;) cpu_hlt();
    } else {
        console_write("\nExiting (Hardware Shutdown)...");
        if (!acpi_poweroff()) {
            console_write("\nACPI S5 failed. Halting.");
            for (;;) cpu_hlt();
        }
    }
}

void power_reboot(void) {
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);
}

void power_halt(void) {
    console_write("\nHalting.");
    for (;;) cpu_hlt();
}

void power_shutdown_stub(void) {
    console_write("\nRequesting ACPI S5 poweroff...");
    if (!acpi_poweroff()) {
        console_write("\nACPI S5 failed or unsupported.");
        console_write("\nSystem is still running.");
    }
}

void power_suspend_stub(void) {
    console_write("\nRequesting ACPI S3 suspend...");
    if (!acpi_suspend()) {
        console_write("\nACPI S3 failed or unsupported.");
        console_write("\nLid-close wake still needs SCI/GPE/EC event handling.");
    }
}
