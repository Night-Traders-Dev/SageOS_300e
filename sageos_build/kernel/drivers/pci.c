/*
 * pci.c — PCI bus enumeration for SageOS
 *
 * Uses the x86 PCI Type 1 configuration mechanism (ports 0xCF8/0xCFC)
 * to enumerate all devices on the PCI bus.  Reports vendor/device IDs,
 * class codes, and BARs.  Identifies known AMD Stoney Ridge SoC
 * components and the QCA6174A Wi-Fi card.
 */

#include <stdint.h>
#include <stddef.h>
#include "io.h"
#include "console.h"
#include "pci.h"
#include "sdhci.h"
#include "dmesg.h"

/* -----------------------------------------------------------------------
 * Device table
 * ----------------------------------------------------------------------- */

static PciDevice pci_devices[PCI_MAX_DEVICES];
static int       pci_dev_count = 0;

/* -----------------------------------------------------------------------
 * Config space access
 * ----------------------------------------------------------------------- */

uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t func,
                         uint8_t offset) {
    uint32_t addr = (1U << 31)
                  | ((uint32_t)bus    << 16)
                  | ((uint32_t)device << 11)
                  | ((uint32_t)func   <<  8)
                  | ((uint32_t)offset & 0xFC);

    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write(uint8_t bus, uint8_t device, uint8_t func,
                      uint8_t offset, uint32_t val) {
    uint32_t addr = (1U << 31)
                  | ((uint32_t)bus    << 16)
                  | ((uint32_t)device << 11)
                  | ((uint32_t)func   <<  8)
                  | ((uint32_t)offset & 0xFC);

    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}


/* -----------------------------------------------------------------------
 * Enumeration
 * ----------------------------------------------------------------------- */

static void pci_probe(uint8_t bus, uint8_t device, uint8_t func) {
    if (pci_dev_count >= PCI_MAX_DEVICES) return;

    uint32_t reg0 = pci_config_read(bus, device, func, 0x00);
    uint16_t vendor = reg0 & 0xFFFF;
    uint16_t dev_id = (reg0 >> 16) & 0xFFFF;

    if (vendor == 0xFFFF || vendor == 0x0000) return;

    {
        char msg[32];
        static const char hex[] = "0123456789abcdef";
        msg[0] = 'p'; msg[1] = 'c'; msg[2] = 'i'; msg[3] = ' ';
        msg[4] = hex[(bus >> 4) & 0xF]; msg[5] = hex[bus & 0xF]; msg[6] = ':';
        msg[7] = hex[(device >> 4) & 0xF]; msg[8] = hex[device & 0xF]; msg[9] = '.';
        msg[10] = (char)('0' + (func & 7)); msg[11] = ' ';
        msg[12] = hex[(vendor >> 12) & 0xF]; msg[13] = hex[(vendor >> 8) & 0xF];
        msg[14] = hex[(vendor >> 4) & 0xF]; msg[15] = hex[vendor & 0xF]; msg[16] = ':';
        msg[17] = hex[(dev_id >> 12) & 0xF]; msg[18] = hex[(dev_id >> 8) & 0xF];
        msg[19] = hex[(dev_id >> 4) & 0xF]; msg[20] = hex[dev_id & 0xF];
        msg[21] = 0;
        dmesg_log(msg);
    }

    uint32_t reg2 = pci_config_read(bus, device, func, 0x08);
    uint32_t reg3 = pci_config_read(bus, device, func, 0x0C);
    uint32_t reg_irq = pci_config_read(bus, device, func, 0x3C);

    PciDevice *d = &pci_devices[pci_dev_count++];
    d->bus        = bus;
    d->device     = device;
    d->func       = func;
    d->vendor_id  = vendor;
    d->device_id  = dev_id;
    d->class_code = (reg2 >> 24) & 0xFF;
    d->subclass   = (reg2 >> 16) & 0xFF;
    d->prog_if    = (reg2 >>  8) & 0xFF;
    d->header_type = (reg3 >> 16) & 0xFF;
    d->irq_line   = reg_irq & 0xFF;

    /* Read BARs (only for type 0 headers) */
    int bar_count = (d->header_type & 0x7F) == 0 ? 6 : 2;
    for (int b = 0; b < bar_count; b++) {
        d->bar[b] = pci_config_read(bus, device, func,
                                    0x10 + (uint8_t)(b * 4));
    }
    for (int b = bar_count; b < 6; b++) {
        d->bar[b] = 0;
    }
}

void pci_enumerate(void) {
    pci_dev_count = 0;

    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            uint32_t reg0 = pci_config_read((uint8_t)bus, (uint8_t)dev, 0, 0x00);
            uint16_t vendor = reg0 & 0xFFFF;
            if (vendor == 0xFFFF) continue;

            pci_probe((uint8_t)bus, (uint8_t)dev, 0);

            /* Check multi-function bit */
            uint32_t reg3 = pci_config_read((uint8_t)bus, (uint8_t)dev, 0, 0x0C);
            uint8_t header = (reg3 >> 16) & 0xFF;
            if (header & 0x80) {
                for (int func = 1; func < 8; func++) {
                    pci_probe((uint8_t)bus, (uint8_t)dev, (uint8_t)func);
                }
            }
        }
    }
}

/* -----------------------------------------------------------------------
 * Lookup
 * ----------------------------------------------------------------------- */

int pci_device_count(void) {
    return pci_dev_count;
}

const PciDevice *pci_get_devices(void) {
    return pci_devices;
}

const PciDevice *pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (int i = 0; i < pci_dev_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id &&
            pci_devices[i].device_id == device_id) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

const PciDevice *pci_find_class(uint8_t class_code, uint8_t subclass) {
    for (int i = 0; i < pci_dev_count; i++) {
        if (pci_devices[i].class_code == class_code &&
            pci_devices[i].subclass == subclass) {
            return &pci_devices[i];
        }
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * Helpers for hex printing
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

static void print_hex8(uint8_t v) {
    static const char hex[] = "0123456789abcdef";
    char buf[3];
    buf[0] = hex[(v >> 4) & 0xF];
    buf[1] = hex[ v       & 0xF];
    buf[2] = 0;
    console_write(buf);
}

static const char *pci_class_name(uint8_t class_code, uint8_t subclass) {
    switch (class_code) {
    case 0x00: return "Legacy";
    case 0x01:
        switch (subclass) {
        case 0x01: return "IDE";
        case 0x06: return "SATA";
        case 0x08: return "NVMe";
        default:   return "Storage";
        }
    case 0x02:
        switch (subclass) {
        case 0x00: return "Ethernet";
        case 0x80: return "Network";
        default:   return "Network";
        }
    case 0x03:
        switch (subclass) {
        case 0x00: return "VGA";
        case 0x80: return "Display";
        default:   return "Display";
        }
    case 0x04: return "Multimedia";
    case 0x05: return "Memory";
    case 0x06:
        switch (subclass) {
        case 0x00: return "Host Bridge";
        case 0x01: return "ISA Bridge";
        case 0x04: return "PCI-PCI Bridge";
        default:   return "Bridge";
        }
    case 0x07: return "Serial";
    case 0x08:
        switch (subclass) {
        case 0x00: return "PIC";
        case 0x01: return "DMA";
        case 0x02: return "Timer";
        case 0x03: return "RTC";
        case 0x05: return "SD Host";
        default:   return "System";
        }
    case 0x0C:
        switch (subclass) {
        case 0x03: return "USB";
        case 0x05: return "SMBus";
        default:   return "Serial Bus";
        }
    case 0x0D:
        switch (subclass) {
        case 0x80: return "Wireless";
        default:   return "Wireless";
        }
    default: return "Other";
    }
}

/* -----------------------------------------------------------------------
 * Shell diagnostic
 * ----------------------------------------------------------------------- */

void pci_cmd_info(void) {
    console_write("\nPCI devices discovered: ");
    console_u32((uint32_t)pci_dev_count);

    if (pci_dev_count == 0) {
        console_write("\n  (none — PCI bus scan found no devices)");
        return;
    }

    for (int i = 0; i < pci_dev_count; i++) {
        const PciDevice *d = &pci_devices[i];
        console_write("\n  ");

        /* Bus:Device.Function */
        print_hex8(d->bus);
        console_write(":");
        print_hex8(d->device);
        console_write(".");
        char fc = '0' + d->func;
        console_putc(fc);

        /* Vendor:Device */
        console_write("  ");
        print_hex16(d->vendor_id);
        console_write(":");
        print_hex16(d->device_id);

        /* Class description */
        console_write("  ");
        console_write(pci_class_name(d->class_code, d->subclass));

        console_write("  [");
        print_hex8(d->class_code);
        console_write(":");
        print_hex8(d->subclass);
        console_write("]");

        /* Identify known devices */
        if (d->vendor_id == PCI_VENDOR_QUALCOMM_ATH &&
            d->device_id == PCI_DEVICE_QCA6174A) {
            console_write("  << QCA6174A Wi-Fi >>");
        }
        if (d->vendor_id == PCI_VENDOR_AMD) {
            console_write("  [AMD]");
        }
    }

    /* Summary: look for key 300e devices */
    console_write("\n");
    const PciDevice *wifi = pci_find_device(PCI_VENDOR_QUALCOMM_ATH,
                                            PCI_DEVICE_QCA6174A);
    if (wifi) {
        console_write("\n  QCA6174A Wi-Fi found at ");
        print_hex8(wifi->bus);
        console_write(":");
        print_hex8(wifi->device);
        console_write(".");
        char fc2 = '0' + wifi->func;
        console_putc(fc2);
        console_write("  BAR0=");
        console_hex64(wifi->bar[0]);
    } else {
        console_write("\n  QCA6174A Wi-Fi: not found");
    }

    const PciDevice *emmc = pci_find_class(SDHCI_PCI_CLASS, SDHCI_PCI_SUBCLASS);
    if (emmc) {
        console_write("\n  SD/eMMC controller found at ");
        print_hex8(emmc->bus);
        console_write(":");
        print_hex8(emmc->device);
        console_write(".");
        char fc3 = '0' + emmc->func;
        console_putc(fc3);
    } else {
        console_write("\n  SD/eMMC controller: not found via PCI");
    }
}
