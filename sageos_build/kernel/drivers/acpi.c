#include <stdint.h>
#include <stddef.h>
#include "bootinfo.h"
#include "acpi.h"
#include "console.h"
#include "io.h"

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

    size_t needle_len = 0;
    while (needle[needle_len]) needle_len++;

    if (needle_len == 0) return 1;

    for (uint32_t i = 36; i + needle_len <= len; i++) {
        uint32_t j = 0;

        while (j < needle_len && mem8(table + i + j) == (uint8_t)needle[j]) {
            j++;
        }

        if (j == needle_len) {
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
    g_acpi.sci_irq = mem16(g_acpi.fadt + 46);
    g_acpi.gpe0_blk = mem32(g_acpi.fadt + 80);
    g_acpi.gpe0_blk_len = mem8(g_acpi.fadt + 92);
}

static void acpi_enable_if_needed(void) {
    if (!g_acpi.pm1a_cnt) {
        return;
    }

    if (inw((uint16_t)g_acpi.pm1a_cnt) & 1) {
        return;
    }

    if (g_acpi.smi_cmd && g_acpi.acpi_enable) {
        outb((uint16_t)g_acpi.smi_cmd, g_acpi.acpi_enable);

        for (uint32_t i = 0; i < 1000000; i++) {
            if (inw((uint16_t)g_acpi.pm1a_cnt) & 1) {
                break;
            }

            cpu_pause();
        }
    }
}

static uint32_t aml_parse_pkg_len(uint64_t *p) {
    uint8_t lead = mem8(*p);
    (*p)++;
    uint32_t byte_count = (lead >> 6);
    if (byte_count == 0) return lead & 0x3F;

    uint32_t len = lead & 0x0F;
    for (uint32_t i = 0; i < byte_count; i++) {
        len |= ((uint32_t)mem8(*p) << (4 + i * 8));
        (*p)++;
    }
    return len;
}

static void acpi_detect_devices(void) {
    if (g_acpi.dsdt) {
        if (table_contains_ascii(g_acpi.dsdt, "PNP0C0A")) g_acpi.has_battery_device = 1;
        if (table_contains_ascii(g_acpi.dsdt, "PNP0C09")) g_acpi.has_ec_device = 1;
        if (table_contains_ascii(g_acpi.dsdt, "PNP0C0D")) g_acpi.has_lid_device = 1;
    }

    for (uint32_t i = 0; i < g_acpi.table_count; i++) {
        uint64_t table = g_acpi.xsdt
            ? mem64(g_acpi.root + 36 + i * 8)
            : mem32(g_acpi.root + 36 + i * 4);
        if (!table) continue;
        if (sig4(table, "SSDT")) {
            if (table_contains_ascii(table, "PNP0C0A")) g_acpi.has_battery_device = 1;
            if (table_contains_ascii(table, "PNP0C09")) g_acpi.has_ec_device = 1;
            if (table_contains_ascii(table, "PNP0C0D")) g_acpi.has_lid_device = 1;
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
    g_acpi.has_lid_device = 0;
    g_acpi.gpe0_blk = 0;
    g_acpi.gpe0_blk_len = 0;
    g_acpi.sci_irq = 0;

    acpi_parse_root();
    acpi_parse_fadt();

    g_acpi.madt = acpi_find_table("APIC");

    acpi_detect_devices();
}

void acpi_enable_sci(void) {
    if (g_acpi.pm1a_cnt) {
        /* Enable SCI by setting bit 0 of PM1a_CNT if needed */
        acpi_enable_if_needed();
    }
}

void acpi_check_events(void) {
    if (!g_acpi.gpe0_blk) return;

    /* Check GPE0 status (usually 1st half is status, 2nd half is enable) */
    uint32_t status_port = g_acpi.gpe0_blk;
    uint32_t status = inb((uint16_t)status_port);

    if (status) {
        /* Clear status by writing 1s back */
        outb((uint16_t)status_port, (uint8_t)status);
        
        /* Check for Lid event (often GPE 0x1D or 0x11 on Chromebooks) */
        /* For minimal impl, we just refresh all dynamic status bar metrics */
    }
}

int acpi_has_lid_device(void) {
    return g_acpi.has_lid_device;
}

void acpi_cmd_lid(void) {
    console_write("\nLid Device:");
    console_write("\n  detected: ");
    console_write(g_acpi.has_lid_device ? "yes" : "no");
    console_write("\n  SCI IRQ: ");
    console_u32(g_acpi.sci_irq);
    console_write("\n  GPE0 Block: ");
    console_hex64(g_acpi.gpe0_blk);
}

int acpi_has_battery_device(void) {
    return g_acpi.has_battery_device;
}

int acpi_has_ec_device(void) {
    return g_acpi.has_ec_device;
}


static int acpi_parse_pkg_int(uint64_t *p, uint8_t *out) {
    uint8_t op = mem8(*p);

    if (op == 0x0A) {
        *out = mem8(*p + 1);
        *p += 2;
        return 1;
    }

    if (op == 0x0B) {
        *out = (uint8_t)(mem16(*p + 1) & 0xFF);
        *p += 3;
        return 1;
    }

    if (op == 0x0C) {
        *out = (uint8_t)(mem32(*p + 1) & 0xFF);
        *p += 5;
        return 1;
    }

    if (op == 0x00 || op == 0x01) {
        *out = op;
        *p += 1;
        return 1;
    }

    return 0;
}

static int acpi_find_sleep_package(const char *name, uint8_t *typa, uint8_t *typb) {
    if (!g_acpi.dsdt) {
        return 0;
    }

    uint64_t dsdt = g_acpi.dsdt;
    uint32_t len = mem32(dsdt + 4);

    if (len < 44) {
        return 0;
    }

    for (uint64_t i = dsdt + 36; i + 16 < dsdt + len; i++) {
        if (
            mem8(i + 0) == '_' &&
            mem8(i + 1) == (uint8_t)name[1] &&
            mem8(i + 2) == (uint8_t)name[2] &&
            mem8(i + 3) == '_'
        ) {
            uint64_t p = i + 4;

            if (mem8(p) != 0x12) {
                continue;
            }

            p++;

            uint8_t pkg_len_byte = mem8(p);
            uint8_t pkg_len_bytes = (uint8_t)((pkg_len_byte >> 6) + 1);
            p += pkg_len_bytes;

            /*
             * NumElements.
             */
            p++;

            if (!acpi_parse_pkg_int(&p, typa)) {
                return 0;
            }

            if (!acpi_parse_pkg_int(&p, typb)) {
                *typb = *typa;
            }

            return 1;
        }
    }

    return 0;
}

static int acpi_enter_sleep(uint8_t typa, uint8_t typb) {
    if (!g_acpi.pm1a_cnt) {
        return 0;
    }

    acpi_enable_if_needed();

    uint16_t slp_en = (uint16_t)(1U << 13);
    uint16_t sci_en = 1;
    uint16_t val_a = (uint16_t)(((uint16_t)typa << 10) | slp_en | sci_en);
    uint16_t val_b = (uint16_t)(((uint16_t)typb << 10) | slp_en | sci_en);

    outw((uint16_t)g_acpi.pm1a_cnt, val_a);

    if (g_acpi.pm1b_cnt) {
        outw((uint16_t)g_acpi.pm1b_cnt, val_b);
    }

    /*
     * If sleep succeeds, we do not return.
     * If firmware ignores it, return to shell instead of hanging.
     */
    for (uint32_t i = 0; i < 5000000; i++) {
        cpu_pause();
    }

    return 0;
}

int acpi_poweroff(void) {
    uint8_t a = 0;
    uint8_t b = 0;

    if (!acpi_find_sleep_package("_S5_", &a, &b)) {
        return 0;
    }

    return acpi_enter_sleep(a, b);
}

int acpi_suspend(void) {
    uint8_t a = 0;
    uint8_t b = 0;

    if (!acpi_find_sleep_package("_S3_", &a, &b)) {
        return 0;
    }

    return acpi_enter_sleep(a, b);
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
