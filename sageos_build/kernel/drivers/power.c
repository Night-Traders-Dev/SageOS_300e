#include "io.h"
#include "console.h"
#include "power.h"
#include "acpi.h"

void power_reboot(void) {
    uint8_t good = 0x02;

    while (good & 0x02) {
        good = inb(0x64);
    }

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
