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
    int has_lid_device;
    int lid_state;          /* 0=closed, 1=open, -1=unknown */
    int suspend_requested;
    int is_resuming;

    uint32_t gpe0_blk;
    uint8_t gpe0_blk_len;
    uint16_t sci_irq;
} AcpiInfo;

void acpi_init(SageOSBootInfo *boot);
const AcpiInfo *acpi_info(void);

uint64_t acpi_find_table(const char sig[4]);
void acpi_cmd_summary(void);
void acpi_cmd_tables(void);
void acpi_cmd_fadt(void);
void acpi_cmd_madt(void);
void acpi_cmd_battery(void);
void acpi_cmd_lid(void);

int acpi_poweroff(void);
int acpi_suspend(void);

int acpi_has_battery_device(void);
int acpi_has_ec_device(void);
int acpi_has_lid_device(void);
int acpi_get_lid_state(void);

void acpi_enable_sci(void);
void acpi_check_events(void);
void acpi_sci_handler(void);

uint32_t acpi_get_emmc_base(void);

#endif
