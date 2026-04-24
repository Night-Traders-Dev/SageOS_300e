#ifndef SAGEOS_SMP_H
#define SAGEOS_SMP_H

#include <stdint.h>

#define SAGEOS_MAX_CPUS 32

typedef struct {
    uint8_t processor_id;
    uint8_t apic_id;
    uint32_t flags;
    uint8_t enabled;
    uint8_t bootstrap;
} CpuInfo;

void smp_init(void);
uint32_t smp_cpu_count(void);
const CpuInfo *smp_cpu(uint32_t idx);
void smp_cmd_info(void);

/*
 * v0.0.9 discovers CPUs through ACPI MADT.
 * AP startup will come after IDT/LAPIC/IPI/trampoline support.
 */
int smp_ap_start_supported(void);

#endif
