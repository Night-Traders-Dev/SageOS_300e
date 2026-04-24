#include "battery.h"
#include "acpi.h"
#include "console.h"
#include "io.h"

static int battery_present;
static int ec_present;
static int percent;
static int percent_valid;
static int source_type; /* 0=none, 1=placeholder, 2=Chromebook EC I/O */

#define CHROMEOS_EC_LPC_BASE 0x900
#define EC_MEMMAP_ID "ECMAP"

static uint32_t read_ec_u32(uint16_t offset) {
    uint32_t val = 0;
    val |= (uint32_t)inb((uint16_t)(CHROMEOS_EC_LPC_BASE + offset + 0));
    val |= (uint32_t)inb((uint16_t)(CHROMEOS_EC_LPC_BASE + offset + 1)) << 8;
    val |= (uint32_t)inb((uint16_t)(CHROMEOS_EC_LPC_BASE + offset + 2)) << 16;
    val |= (uint32_t)inb((uint16_t)(CHROMEOS_EC_LPC_BASE + offset + 3)) << 24;
    return val;
}

void battery_init(void) {
    battery_present = acpi_has_battery_device();
    ec_present = acpi_has_ec_device();
    percent = -1;
    percent_valid = 0;
    source_type = 0;

    /*
     * 1. Try Chromebook EC memory-mapped I/O (LPC).
     * This is common on x86 Chromebooks like Lenovo 300e.
     */
    char sig[8];
    for (int i = 0; i < 7; i++) {
        sig[i] = (char)inb((uint16_t)(CHROMEOS_EC_LPC_BASE + i));
    }
    sig[7] = 0;

    int match = 1;
    for (int i = 0; i < 5; i++) {
        if (sig[i] != EC_MEMMAP_ID[i]) {
            match = 0;
            break;
        }
    }

    if (match) {
        /*
         * Common offsets for ChromeOS EC Battery v1:
         * 0x48: Remaining Capacity (mAh)
         * 0x58: Last Full Capacity (mAh)
         */
        uint32_t remaining = read_ec_u32(0x48);
        uint32_t full = read_ec_u32(0x58);

        if (full > 0 && full != 0xFFFFFFFFU && remaining != 0xFFFFFFFFU) {
            percent = (int)((remaining * 100ULL) / full);
            if (percent > 100) percent = 100;
            percent_valid = 1;
            source_type = 2;
        }
    }

    /*
     * 2. Fallback to placeholder if ACPI battery strings were found.
     */
    if (!percent_valid && battery_present) {
        percent = 50;
        percent_valid = 1;
        source_type = 1;
    }
}

int battery_percent(void) {
    /*
     * Re-poll dynamic values if we are using the EC.
     */
    if (source_type == 2) {
        uint32_t remaining = read_ec_u32(0x48);
        uint32_t full = read_ec_u32(0x58);
        if (full > 0 && full != 0xFFFFFFFFU && remaining != 0xFFFFFFFFU) {
            percent = (int)((remaining * 100ULL) / full);
            if (percent > 100) percent = 100;
        }
    }

    if (!percent_valid) {
        return -1;
    }

    return percent;
}

void battery_cmd_info(void) {
    console_write("\nBattery:");
    console_write("\n  ACPI battery hints: ");
    console_write(battery_present ? "present" : "not found");
    console_write("\n  Chromebook EC I/O: ");
    console_write(source_type == 2 ? "active" : (ec_present ? "detected but ECMAP signature missing" : "not found"));
    console_write("\n  percentage: ");

    if (percent_valid && percent >= 0) {
        console_u32((uint32_t)percent);
        console_write("%");
    } else {
        console_write("unavailable");
    }

    console_write("\n  source: ");
    if (source_type == 2) {
        console_write("Chromebook EC Memory Map (LPC 0x900)");
    } else if (source_type == 1) {
        console_write("ACPI AML placeholder (simulated)");
    } else {
        console_write("none");
    }
}
