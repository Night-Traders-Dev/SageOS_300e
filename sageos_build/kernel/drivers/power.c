#include "io.h"
#include "console.h"
#include "power.h"
#include "acpi.h"
#include "sysinfo.h"
#include "dmesg.h"

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
    dmesg_log("Power: Reboot requested.");
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);
}

void power_halt(void) {
    dmesg_log("Power: System halt.");
    console_write("\nHalting.");
    for (;;) cpu_hlt();
}

void power_shutdown(void) {
    dmesg_log("Power: Requesting ACPI S5 poweroff...");
    console_write("\nRequesting ACPI S5 poweroff...");
    if (!acpi_poweroff()) {
        dmesg_log("Power: ACPI S5 failed or unsupported.");
        console_write("\nACPI S5 failed or unsupported.");
    }
}

void power_suspend(void) {
    dmesg_log("Power: Requesting ACPI S3 suspend...");
    console_write("\nRequesting ACPI S3 suspend...");
    if (!acpi_suspend()) {
        dmesg_log("Power: ACPI S3 failed or unsupported.");
        console_write("\nACPI S3 failed or unsupported.");
    } else {
        /* Resume path */
        dmesg_log("Power: Resumed from S3.");
        console_write("\nResumed from S3.");
    }
}
