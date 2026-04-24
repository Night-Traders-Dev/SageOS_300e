#include <stdint.h>
#include <stddef.h>
#include "smp.h"
#include "acpi.h"
#include "console.h"
#include "timer.h"
#include "io.h"

static CpuInfo cpus[SAGEOS_MAX_CPUS];
static uint32_t cpu_count;
static uint64_t lapic_base;
static uint8_t cpu_stacks[SAGEOS_MAX_CPUS][16384] __attribute__((aligned(16)));

static uint32_t mem32(uint64_t addr) {
    return *(volatile uint32_t *)(uintptr_t)addr;
}

static void write32(uint64_t addr, uint32_t val) {
    *(volatile uint32_t *)(uintptr_t)addr = val;
}

uint32_t lapic_read(uint32_t reg) {
    if (!lapic_base) return 0;
    return mem32(lapic_base + reg);
}

void lapic_write(uint32_t reg, uint32_t val) {
    if (!lapic_base) return;
    write32(lapic_base + reg, val);
}

void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

static void lapic_enable(void) {
    /* Set Spurious Interrupt Vector to 0xFF | bit 8 (Enable) */
    lapic_write(LAPIC_SVR, 0x1FF);
}

uint32_t smp_cpu_count(void) {
    return cpu_count;
}

const CpuInfo *smp_cpu(uint32_t idx) {
    if (idx >= cpu_count) return 0;
    return &cpus[idx];
}

int smp_ap_start_supported(void) {
    return 1;
}

/* 
 * 16-bit trampoline code to be placed at 0x8000.
 * It initializes a minimal GDT and jumps to 64-bit kernel entry.
 */
extern void ap_trampoline(void);
extern void ap_trampoline_end(void);
extern volatile uint64_t ap_stack_ptr;
extern volatile uint64_t ap_entry_ptr;
extern volatile uint64_t ap_cr3_ptr;

void smp_init(void) {
    cpu_count = 0;
    lapic_base = 0;

    uint64_t madt = acpi_find_table("APIC");

    if (!madt) {
        /* Fallback for single CPU */
        cpus[0].processor_id = 0;
        cpus[0].apic_id = 0;
        cpus[0].flags = 1;
        cpus[0].enabled = 1;
        cpus[0].bootstrap = 1;
        cpus[0].started = 1;
        cpu_count = 1;
        return;
    }

    /* MADT + 36: Local APIC Address */
    lapic_base = (uint64_t)mem32(madt + 36);

    uint32_t len = mem32(madt + 4);
    uint64_t p = madt + 44;
    uint64_t end = madt + len;

    while (p + 2 <= end) {
        uint8_t type = *(uint8_t *)(uintptr_t)p;
        uint8_t elen = *(uint8_t *)(uintptr_t)(p + 1);

        if (elen < 2) break;

        if (type == 0 && elen >= 8 && cpu_count < SAGEOS_MAX_CPUS) {
            CpuInfo *c = &cpus[cpu_count];
            c->processor_id = *(uint8_t *)(uintptr_t)(p + 2);
            c->apic_id = *(uint8_t *)(uintptr_t)(p + 3);
            c->flags = mem32(p + 4);
            c->enabled = (c->flags & 1) ? 1 : 0;
            c->bootstrap = (cpu_count == 0) ? 1 : 0;
            c->started = c->bootstrap;
            c->stack_top = (uint64_t)&cpu_stacks[cpu_count][16384];
            cpu_count++;
        }
        p += elen;
    }

    lapic_enable();
}

void ap_kernel_main(uint32_t apic_id) {
    lapic_enable();
    
    for (uint32_t i = 0; i < cpu_count; i++) {
        if (cpus[i].apic_id == apic_id) {
            cpus[i].started = 1;
            break;
        }
    }

    /* AP idle loop */
    while (1) {
        cpu_hlt();
    }
}

void smp_boot_aps(void) {
    console_write("\nSMP: Starting Application Processors...");

    /* 1. Prepare trampoline at 0x8000 */
    uint8_t *trampoline_dest = (uint8_t *)0x8000;
    size_t trampoline_size = (uintptr_t)ap_trampoline_end - (uintptr_t)ap_trampoline;
    
    for (size_t i = 0; i < trampoline_size; i++) {
        trampoline_dest[i] = ((uint8_t *)ap_trampoline)[i];
    }

    for (uint32_t i = 0; i < cpu_count; i++) {
        if (cpus[i].bootstrap || !cpus[i].enabled) continue;

        console_write("\n  booting cpu");
        console_u32(i);
        console_write(" (APIC ");
        console_u32(cpus[i].apic_id);
        console_write(")... ");

        /* Set stack and entry point for this AP */
        ap_stack_ptr = cpus[i].stack_top;
        ap_entry_ptr = (uint64_t)ap_kernel_main;
        ap_cr3_ptr = read_cr3();

        /* 2. Send INIT IPI */

        lapic_write(LAPIC_ICR_HIGH, (uint32_t)cpus[i].apic_id << 24);
        lapic_write(LAPIC_ICR_LOW, 0x00004500); /* INIT, Assert, Level */
        
        timer_delay_ms(10);

        /* 3. Send SIPI IPI (vector 0x08 for 0x8000) */
        lapic_write(LAPIC_ICR_HIGH, (uint32_t)cpus[i].apic_id << 24);
        lapic_write(LAPIC_ICR_LOW, 0x00004608); /* Start-up, Vector 0x08 */

        timer_delay_ms(1);

        /* Wait for AP to check in */
        int timeout = 100;
        while (!cpus[i].started && timeout--) {
            timer_delay_ms(1);
        }

        if (cpus[i].started) {
            console_write("online");
        } else {
            console_write("timeout/failed");
        }
    }
}

void smp_cmd_info(void) {
    console_write("\nSMP:");
    console_write("\n  discovered CPUs: ");
    console_u32(cpu_count);
    console_write("\n  LAPIC base: ");
    console_hex64(lapic_base);

    for (uint32_t i = 0; i < cpu_count; i++) {
        CpuInfo *c = &cpus[i];
        console_write("\n  cpu");
        console_u32(i);
        console_write(": apic_id=");
        console_u32(c->apic_id);
        console_write(" status=");
        console_write(c->started ? "online" : (c->enabled ? "offline" : "disabled"));
        if (c->bootstrap) console_write(" (BSP)");
    }
}
