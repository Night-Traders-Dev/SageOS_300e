#include "battery.h"
#include "acpi.h"
#include "console.h"

static int battery_present;
static int ec_present;
static int percent;

void battery_init(void) {
    battery_present = acpi_has_battery_device();
    ec_present = acpi_has_ec_device();

    /*
     * Real percentage needs one of:
     * 1. AML interpreter for ACPI _BST / _BIF / _BIX
     * 2. verified Chromebook EC host-command battery query
     */
    percent = -1;
}

int battery_percent(void) {
    return percent;
}

void battery_cmd_info(void) {
    console_write("\nBattery:");
    console_write("\n  ACPI battery hints: ");
    console_write(battery_present ? "present" : "not found");
    console_write("\n  Chromebook/ACPI EC hints: ");
    console_write(ec_present ? "present" : "not found");
    console_write("\n  percentage: ");

    if (percent >= 0) {
        console_u32((uint32_t)percent);
        console_write("%");
    } else {
        console_write("unavailable");
    }

    console_write("\n  next driver step:");
    console_write("\n    implement AML evaluation for _BST/_BIF, or");
    console_write("\n    implement verified Chromebook EC battery host command.");
}
