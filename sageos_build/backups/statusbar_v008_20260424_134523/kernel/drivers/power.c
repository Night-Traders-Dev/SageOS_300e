#include "io.h"
#include "console.h"
#include "power.h"

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
    console_write("\nACPI shutdown driver is not active in modular v0.0.7 yet.");
    console_write("\nUse halt for now. ACPI S5 comes next.");
}

void power_suspend_stub(void) {
    console_write("\nACPI suspend driver is not active in modular v0.0.7 yet.");
    console_write("\nNext step: ACPI table inspector, then S3/GPE/lid support.");
}
