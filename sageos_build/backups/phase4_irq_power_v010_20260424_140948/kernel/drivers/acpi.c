#include <stdint.h>
#include <stddef.h>
#include "bootinfo.h"
#include "acpi.h"
#include "console.h"

static AcpiInfo g_acpi;

static uint8_t mem8(uint64_t addr) {
    return *(volatile uint8_t *)(uintptr_t)addr;
}

static uint16_t mem16(uint64_t addr) {
    return *(volatile uint16_t *)(uintptr_t)addr;
}

static uint32_t mem32(uint64_t addr) {
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static uint64_t mem64(uint64_t addr) {
    return *(volatile uint64_t *)(uintptr_t)addr;
}

static int sig4(uint64_t addr, const char sig[4]) {
    return
        mem8(addr + 0) == (uint8_t)sig[0] &&
        mem8(addr + 1) == (uint8_t)sig[1] &&
        mem8(addr + 2) == (uint8_t)sig[2] &&
        mem8(addr + 3) == (uint8_t)sig[3];
}

static void print_sig(uint64_t addr) {
    console_putc((char)mem8(addr + 0));
    console_putc((char)mem8(addr + 1));
    console_putc((char)mem8(addr + 2));
    console_putc((char)mem8(addr + 3));
}

static int acpi_checksum(uint64_t addr, uint32_t len) {
    uint8_t sum = 0;

    for (uint32_t i = 0; i < len; i++) {
        sum = (uint8_t)(sum + mem8(addr + i));
    }

    return sum == 0;
}

static int table_contains_ascii(uint64_t table, const char *needle) {
    if (!table) return 0;

    uint32_t len = mem32(table + 4);
    if (len < 36) return 0;

    for (uint32_t i = 36; i + 8 < len; i++) {
        uint32_t j = 0;

        while (needle[j] && i + j < len && mem8(table + i + j) == (uint8_t)needle[j]) {
            j++;
        }

        if (!needle[j]) {
            return 1;
        }
    }

    return 0;
}

const AcpiInfo *acpi_info(void) {
    return &g_acpi;
}

uint64_t acpi_find_table(const char sig[4]) {
    if (!g_acpi.root) {
        return 0;
    }

    uint32_t root_len = mem32(g_acpi.root + 4);

    if (root_len < 36) {
        return 0;
    }

    uint32_t entry_size = g_acpi.xsdt ? 8 : 4;
    uint32_t entries = (root_len - 36) / entry_size;

    for (uint32_t i = 0; i < entries; i++) {
        uint64_t table = g_acpi.xsdt
            ? mem64(g_acpi.root + 36 + i * 8)
            : mem32(g_acpi.root + 36 + i * 4);

        if (!table) continue;

        if (sig4(table, sig)) {
            return table;
        }
    }

    return 0;
}

static void acpi_parse_root(void) {
    uint64_t rsdp = g_acpi.rsdp;

    if (!rsdp) {
        return;
    }

    if (
        mem8(rsdp + 0) != 'R' ||
        mem8(rsdp + 1) != 'S' ||
        mem8(rsdp + 2) != 'D' ||
        mem8(rsdp + 3) != ' ' ||
        mem8(rsdp + 4) != 'P' ||
        mem8(rsdp + 5) != 'T' ||
        mem8(rsdp + 6) != 'R' ||
        mem8(rsdp + 7) != ' '
    ) {
        return;
    }

    uint8_t revision = mem8(rsdp + 15);

    if (revision >= 2) {
        uint64_t xsdt = mem64(rsdp + 24);

        if (xsdt && sig4(xsdt, "XSDT")) {
            g_acpi.root = xsdt;
            g_acpi.xsdt = 1;
        }
    }

    if (!g_acpi.root) {
        uint32_t rsdt = mem32(rsdp + 16);

        if (rsdt && sig4(rsdt, "RSDT")) {
            g_acpi.root = rsdt;
            g_acpi.xsdt = 0;
        }
    }

    if (g_acpi.root) {
        uint32_t len = mem32(g_acpi.root + 4);
        uint32_t entry_size = g_acpi.xsdt ? 8 : 4;

        if (len >= 36) {
            g_acpi.table_count = (len - 36) / entry_size;
        }
    }
}

static void acpi_parse_fadt(void) {
    g_acpi.fadt = acpi_find_table("FACP");

    if (!g_acpi.fadt) {
        return;
    }

    uint32_t fadt_len = mem32(g_acpi.fadt + 4);

    uint32_t dsdt32 = mem32(g_acpi.fadt + 40);
    uint64_t x_dsdt = 0;

    if (fadt_len >= 148) {
        x_dsdt = mem64(g_acpi.fadt + 140);
    }

    g_acpi.dsdt = x_dsdt ? x_dsdt : dsdt32;

    g_acpi.smi_cmd = mem32(g_acpi.fadt + 48);
    g_acpi.acpi_enable = mem8(g_acpi.fadt + 52);
    g_acpi.pm1a_cnt = mem32(g_acpi.fadt + 64);
    g_acpi.pm1b_cnt = mem32(g_acpi.fadt + 68);
}

static void acpi_detect_devices(void) {
    /*
     * This is not AML evaluation. It is string detection in DSDT/SSDTs.
     * It tells us whether battery / EC devices are present before we add
     * a real AML method evaluator or Chromebook EC driver.
     */
    if (g_acpi.dsdt) {
        if (table_contains_ascii(g_acpi.dsdt, "PNP0C0A") || table_contains_ascii(g_acpi.dsdt, "_BIF") || table_contains_ascii(g_acpi.dsdt, "_BST")) {
            g_acpi.has_battery_device = 1;
        }

        if (table_contains_ascii(g_acpi.dsdt, "PNP0C09") || table_contains_ascii(g_acpi.dsdt, "GOOG0004") || table_contains_ascii(g_acpi.dsdt, "GOOG000C")) {
            g_acpi.has_ec_device = 1;
        }
    }

    for (uint32_t i = 0; i < g_acpi.table_count; i++) {
        uint64_t table = g_acpi.xsdt
            ? mem64(g_acpi.root + 36 + i * 8)
            : mem32(g_acpi.root + 36 + i * 4);

        if (!table) continue;

        if (sig4(table, "SSDT")) {
            if (table_contains_ascii(table, "PNP0C0A") || table_contains_ascii(table, "_BIF") || table_contains_ascii(table, "_BST")) {
                g_acpi.has_battery_device = 1;
            }

            if (table_contains_ascii(table, "PNP0C09") || table_contains_ascii(table, "GOOG0004") || table_contains_ascii(table, "GOOG000C")) {
                g_acpi.has_ec_device = 1;
            }
        }
    }
}

void acpi_init(SageOSBootInfo *boot) {
    g_acpi.rsdp = boot ? boot->acpi_rsdp : 0;
    g_acpi.root = 0;
    g_acpi.xsdt = 0;
    g_acpi.table_count = 0;
    g_acpi.fadt = 0;
    g_acpi.dsdt = 0;
    g_acpi.madt = 0;
    g_acpi.pm1a_cnt = 0;
    g_acpi.pm1b_cnt = 0;
    g_acpi.smi_cmd = 0;
    g_acpi.acpi_enable = 0;
    g_acpi.has_battery_device = 0;
    g_acpi.has_ec_device = 0;

    acpi_parse_root();
    acpi_parse_fadt();

    g_acpi.madt = acpi_find_table("APIC");

    acpi_detect_devices();
}

int acpi_has_battery_device(void) {
    return g_acpi.has_battery_device;
}

int acpi_has_ec_device(void) {
    return g_acpi.has_ec_device;
}

void acpi_cmd_summary(void) {
    console_write("\nACPI:");
    console_write("\n  RSDP: ");
    console_hex64(g_acpi.rsdp);
    console_write("\n  root: ");
    console_hex64(g_acpi.root);
    console_write(g_acpi.xsdt ? " XSDT" : " RSDT");
    console_write("\n  tables: ");
    console_u32(g_acpi.table_count);
    console_write("\n  FADT/FACP: ");
    console_hex64(g_acpi.fadt);
    console_write("\n  DSDT: ");
    console_hex64(g_acpi.dsdt);
    console_write("\n  MADT/APIC: ");
    console_hex64(g_acpi.madt);
    console_write("\n  battery device detected: ");
    console_write(g_acpi.has_battery_device ? "yes" : "no");
    console_write("\n  EC/Chromebook EC hints: ");
    console_write(g_acpi.has_ec_device ? "yes" : "no");
}

void acpi_cmd_tables(void) {
    console_write("\nACPI tables:");

    if (!g_acpi.root) {
        console_write("\n  unavailable");
        return;
    }

    for (uint32_t i = 0; i < g_acpi.table_count; i++) {
        uint64_t table = g_acpi.xsdt
            ? mem64(g_acpi.root + 36 + i * 8)
            : mem32(g_acpi.root + 36 + i * 4);

        if (!table) continue;

        console_write("\n  ");
        print_sig(table);
        console_write(" @ ");
        console_hex64(table);
        console_write(" len=");
        console_u32(mem32(table + 4));
        console_write(" checksum=");
        console_write(acpi_checksum(table, mem32(table + 4)) ? "ok" : "bad");
    }
}

void acpi_cmd_fadt(void) {
    console_write("\nFADT/FACP:");

    if (!g_acpi.fadt) {
        console_write("\n  unavailable");
        return;
    }

    console_write("\n  FADT: ");
    console_hex64(g_acpi.fadt);
    console_write("\n  DSDT: ");
    console_hex64(g_acpi.dsdt);
    console_write("\n  SMI_CMD: ");
    console_hex64(g_acpi.smi_cmd);
    console_write("\n  ACPI_ENABLE: ");
    console_u32(g_acpi.acpi_enable);
    console_write("\n  PM1a_CNT: ");
    console_hex64(g_acpi.pm1a_cnt);
    console_write("\n  PM1b_CNT: ");
    console_hex64(g_acpi.pm1b_cnt);
}

void acpi_cmd_madt(void) {
    console_write("\nMADT/APIC:");

    if (!g_acpi.madt) {
        console_write("\n  unavailable");
        return;
    }

    console_write("\n  MADT: ");
    console_hex64(g_acpi.madt);
    console_write("\n  local APIC addr: ");
    console_hex64(mem32(g_acpi.madt + 36));
    console_write("\n  flags: ");
    console_hex64(mem32(g_acpi.madt + 40));
}

void acpi_cmd_battery(void) {
    console_write("\nACPI battery / EC:");
    console_write("\n  battery ACPI device hints: ");
    console_write(g_acpi.has_battery_device ? "present" : "not found");
    console_write("\n  EC / Chromebook EC hints: ");
    console_write(g_acpi.has_ec_device ? "present" : "not found");
    console_write("\n  percentage: unavailable");
    console_write("\n  next: AML _BST/_BIF evaluator or verified Chromebook EC host-command path");
}
