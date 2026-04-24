#include <stdint.h>
#include "smp.h"
#include "acpi.h"
#include "console.h"

static CpuInfo cpus[SAGEOS_MAX_CPUS];
static uint32_t cpu_count;

static uint8_t mem8(uint64_t addr) {
    return *(volatile uint8_t *)(uintptr_t)addr;
}

static uint32_t mem32(uint64_t addr) {
    return *(volatile uint32_t *)(uintptr_t)addr;
}

uint32_t smp_cpu_count(void) {
    return cpu_count;
}

const CpuInfo *smp_cpu(uint32_t idx) {
    if (idx >= cpu_count) return 0;
    return &cpus[idx];
}

int smp_ap_start_supported(void) {
    return 0;
}

void smp_init(void) {
    cpu_count = 0;

    uint64_t madt = acpi_find_table("APIC");

    if (!madt) {
        cpus[0].processor_id = 0;
        cpus[0].apic_id = 0;
        cpus[0].flags = 1;
        cpus[0].enabled = 1;
        cpus[0].bootstrap = 1;
        cpu_count = 1;
        return;
    }

    uint32_t len = mem32(madt + 4);
    uint64_t p = madt + 44;
    uint64_t end = madt + len;

    while (p + 2 <= end) {
        uint8_t type = mem8(p + 0);
        uint8_t elen = mem8(p + 1);

        if (elen < 2) break;

        /*
         * Type 0: Processor Local APIC.
         */
        if (type == 0 && elen >= 8 && cpu_count < SAGEOS_MAX_CPUS) {
            CpuInfo *c = &cpus[cpu_count];

            c->processor_id = mem8(p + 2);
            c->apic_id = mem8(p + 3);
            c->flags = mem32(p + 4);
            c->enabled = (c->flags & 1) ? 1 : 0;
            c->bootstrap = cpu_count == 0 ? 1 : 0;

            cpu_count++;
        }

        /*
         * Type 9: Processor Local x2APIC.
         * Basic discovery only for now.
         */
        if (type == 9 && elen >= 16 && cpu_count < SAGEOS_MAX_CPUS) {
            CpuInfo *c = &cpus[cpu_count];

            c->processor_id = 0xFF;
            c->apic_id = mem32(p + 4) & 0xFF;
            c->flags = mem32(p + 8);
            c->enabled = (c->flags & 1) ? 1 : 0;
            c->bootstrap = cpu_count == 0 ? 1 : 0;

            cpu_count++;
        }

        p += elen;
    }

    if (cpu_count == 0) {
        cpus[0].processor_id = 0;
        cpus[0].apic_id = 0;
        cpus[0].flags = 1;
        cpus[0].enabled = 1;
        cpus[0].bootstrap = 1;
        cpu_count = 1;
    }
}

void smp_cmd_info(void) {
    console_write("\nSMP:");
    console_write("\n  discovered CPUs: ");
    console_u32(cpu_count);
    console_write("\n  AP startup: not enabled yet");
    console_write("\n  next: IDT + LAPIC + INIT/SIPI trampoline");

    for (uint32_t i = 0; i < cpu_count; i++) {
        CpuInfo *c = &cpus[i];

        console_write("\n  cpu");
        console_u32(i);
        console_write(": processor_id=");
        console_u32(c->processor_id);
        console_write(" apic_id=");
        console_u32(c->apic_id);
        console_write(" enabled=");
        console_write(c->enabled ? "yes" : "no");
        console_write(" bsp=");
        console_write(c->bootstrap ? "yes" : "no");
    }
}
