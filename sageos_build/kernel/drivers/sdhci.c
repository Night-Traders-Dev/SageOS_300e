/*
 * sdhci.c — SDHCI (SD Host Controller) discovery for SageOS
 *
 * Finds the eMMC/SD controller via PCI bus scan.  On the Lenovo 300e
 * (AMD Stoney Ridge), the eMMC controller may also appear through ACPI
 * rather than standard PCI enumeration.
 *
 * This is a discovery-only driver — no data transfer is implemented yet.
 */

#include <stdint.h>
#include "console.h"
#include "pci.h"
#include "acpi.h"
#include "sdhci.h"
#include "dmesg.h"

/* -----------------------------------------------------------------------
 * State
 * ----------------------------------------------------------------------- */

static int       sdhci_found = 0;
static int       sdhci_is_acpi = 0;
static uint32_t  sdhci_bar0  = 0;
static uint16_t  sdhci_vendor = 0;
static uint16_t  sdhci_device = 0;
static uint8_t   sdhci_bus   = 0;
static uint8_t   sdhci_dev   = 0;
static uint8_t   sdhci_func  = 0;

/* -----------------------------------------------------------------------
 * Initialization
 * ----------------------------------------------------------------------- */

int sdhci_init(void) {
    sdhci_found = 0;
    sdhci_is_acpi = 0;

    /* Search PCI for class 08:05 (SD Host Controller) */
    const PciDevice *d = pci_find_class(SDHCI_PCI_CLASS, SDHCI_PCI_SUBCLASS);
    if (d) {
        sdhci_found  = 1;
        sdhci_bar0   = d->bar[0] & ~0xFU; /* Mask lower bits for MMIO */
        sdhci_vendor = d->vendor_id;
        sdhci_device = d->device_id;
        sdhci_bus    = d->bus;
        sdhci_dev    = d->device;
        sdhci_func   = d->func;
        dmesg_log("sdhci: controller found via PCI");
        return 1;
    }

    /* The AMD Stoney Ridge eMMC uses ACPI-based discovery (sdhci-acpi AMDI0040)
       instead of standard PCI. Probe ACPI DSDT/SSDT. */
    uint32_t acpi_base = acpi_get_emmc_base();
    if (acpi_base) {
        sdhci_found = 1;
        sdhci_is_acpi = 1;
        sdhci_bar0 = acpi_base;
        dmesg_log("sdhci: controller found via ACPI (AMDI0040)");
        return 1;
    }

    dmesg_log("sdhci: controller not found");
    return 0;
}

/* -----------------------------------------------------------------------
 * Shell diagnostic
 * ----------------------------------------------------------------------- */

static void print_hex16(uint16_t v) {
    static const char hex[] = "0123456789abcdef";
    char buf[5];
    buf[0] = hex[(v >> 12) & 0xF];
    buf[1] = hex[(v >>  8) & 0xF];
    buf[2] = hex[(v >>  4) & 0xF];
    buf[3] = hex[ v        & 0xF];
    buf[4] = 0;
    console_write(buf);
}

static void print_hex8_s(uint8_t v) {
    static const char hex[] = "0123456789abcdef";
    char buf[3];
    buf[0] = hex[(v >> 4) & 0xF];
    buf[1] = hex[ v       & 0xF];
    buf[2] = 0;
    console_write(buf);
}

void sdhci_cmd_info(void) {
    console_write("\nSDHCI / eMMC Controller Status:");

    if (!sdhci_found) {
        console_write("\n  Not found via PCI enumeration or ACPI AMDI0040.");
        return;
    }

    if (sdhci_is_acpi) {
        console_write("\n  Found via ACPI AML (AMDI0040)");
    } else {
        console_write("\n  Found via PCI: ");
        print_hex16(sdhci_vendor);
        console_write(":");
        print_hex16(sdhci_device);

        console_write("\n  PCI location: ");
        print_hex8_s(sdhci_bus);
        console_write(":");
        print_hex8_s(sdhci_dev);
        console_write(".");
        char fc = '0' + sdhci_func;
        console_putc(fc);
    }

    console_write("\n  BAR0 (MMIO base): ");
    console_hex64(sdhci_bar0);

    if (sdhci_bar0 != 0) {
        console_write("\n  (MMIO registers not read — paging/mapping TBD)");
    }

    console_write("\n  Storage type: eMMC 5.1 (32 GB soldered, per 300e spec)");
}
