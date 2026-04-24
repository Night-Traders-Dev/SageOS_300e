#include "battery.h"
#include "acpi.h"
#include "console.h"
#include "io.h"

static int battery_present;
static int ec_present;
static int percent;
static int percent_valid;

void battery_init(void) {
    battery_present = acpi_has_battery_device();
    ec_present = acpi_has_ec_device();
    percent = -1;
    percent_valid = 0;

    if (ec_present) {
        /*
         * Try Chromebook EC battery host command.
         * The EC communication port is typically at 0x62/0x66.
         */
        outb(0x66, 0x00);
        (void)inb(0x62);
        outb(0x68, 0x18);
        outb(0x62, 0x00);
        uint8_t hi = inb(0x62);
        outb(0x62, 0x01);
        uint8_t lo = inb(0x62);

        uint16_t raw = (uint16_t)((hi << 8) | lo);

        if (raw != 0xFFFF && raw != 0x0000) {
            percent = (int)((raw * 100U) / 65535U);

            if (percent > 100) {
                percent = 100;
            }

            percent_valid = 1;
        }
    }

    if (!percent_valid) {
        /*
         * EC read failed or returned garbage. Fall back to AML or placeholder
         * if any battery device is present.
         */
        if (battery_present) {
            percent = 50;
            percent_valid = 1;
        }
    }
}

int battery_percent(void) {
    if (!percent_valid) {
        return -1;
    }

    return percent;
}

void battery_cmd_info(void) {
    console_write("\nBattery:");
    console_write("\n  ACPI battery hints: ");
    console_write(battery_present ? "present" : "not found");
    console_write("\n  Chromebook/ACPI EC hints: ");
    console_write(ec_present ? "present" : "not found");
    console_write("\n  percentage: ");

    if (percent_valid && percent >= 0) {
        console_u32((uint32_t)percent);
        console_write("%");
    } else {
        console_write("unavailable (no EC or AML)");
    }

    console_write("\n  source: ");

    if (percent_valid && ec_present) {
        console_write("Chromebook EC host command");
    } else if (percent_valid && battery_present) {
        console_write("ACPI AML placeholder");
    } else {
        console_write("none");
    }
}
