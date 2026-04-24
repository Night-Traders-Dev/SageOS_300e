#ifndef SAGEOS_ACPI_H
#define SAGEOS_ACPI_H

#include <stdint.h>
#include "bootinfo.h"

typedef struct {
    uint64_t rsdp;
    uint64_t root;
    uint8_t xsdt;
    uint32_t table_count;

    uint64_t fadt;
    uint64_t dsdt;
    uint64_t madt;

    uint32_t pm1a_cnt;
    uint32_t pm1b_cnt;
    uint32_t smi_cmd;
    uint8_t acpi_enable;

    int has_battery_device;
    int has_ec_device;
} AcpiInfo;

void acpi_init(SageOSBootInfo *boot);
const AcpiInfo *acpi_info(void);

uint64_t acpi_find_table(const char sig[4]);
void acpi_cmd_summary(void);
void acpi_cmd_tables(void);
void acpi_cmd_fadt(void);
void acpi_cmd_madt(void);
void acpi_cmd_battery(void);

int acpi_has_battery_device(void);
int acpi_has_ec_device(void);

#endif
