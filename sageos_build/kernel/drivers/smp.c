#include <stdint.h>
#include <stddef.h>
#include "smp.h"
#include "acpi.h"
#include "console.h"
#include "timer.h"
#include "io.h"
#include "dmesg.h"
#include "scheduler.h"

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

uint32_t smp_current_cpu_index(void) {
    uint32_t apic_id;

    if (!lapic_base || cpu_count == 0) return 0;

    apic_id = (lapic_read(LAPIC_ID) >> 24) & 0xFF;
    for (uint32_t i = 0; i < cpu_count; i++) {
        if (cpus[i].apic_id == apic_id) return i;
    }

    return 0;
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

static void smp_set_single_bsp(void) {
    for (uint32_t i = 0; i < SAGEOS_MAX_CPUS; i++) {
        cpus[i].processor_id = 0;
        cpus[i].apic_id = 0;
        cpus[i].flags = 0;
        cpus[i].enabled = 0;
        cpus[i].bootstrap = 0;
        cpus[i].stack_top = 0;
        cpus[i].started = 0;
    }

    cpus[0].processor_id = 0;
    cpus[0].apic_id = 0;
    cpus[0].flags = 1;
    cpus[0].enabled = 1;
    cpus[0].bootstrap = 1;
    cpus[0].started = 1;
    cpus[0].stack_top = (uint64_t)&cpu_stacks[0][16384];
    cpu_count = 1;
    lapic_base = 0;
}

void smp_init_firmware_bsp(void) {
    smp_set_single_bsp();
}

void smp_init(void) {
    cpu_count = 0;
    lapic_base = 0;

    uint64_t madt = acpi_find_table("APIC");

    if (!madt) {
        smp_set_single_bsp();
        return;
    }

    /* MADT + 36: Local APIC Address */
    lapic_base = (uint64_t)mem32(madt + 36);

    uint32_t len = mem32(madt + 4);
    if (len < 44 || len > 65536) {
        smp_set_single_bsp();
        return;
    }

    uint64_t p = madt + 44;
    uint64_t end = madt + len;

    while (p + 2 <= end) {
        uint8_t type = *(uint8_t *)(uintptr_t)p;
        uint8_t elen = *(uint8_t *)(uintptr_t)(p + 1);

        if (elen < 2 || p + elen > end) break;

        if (type == 0 && elen >= 8 && cpu_count < SAGEOS_MAX_CPUS) {
            CpuInfo *c = &cpus[cpu_count];
            c->processor_id = *(uint8_t *)(uintptr_t)(p + 2);
            c->apic_id = *(uint8_t *)(uintptr_t)(p + 3);
            c->flags = mem32(p + 4);
            c->enabled = (c->flags & 1) ? 1 : 0;
            c->bootstrap = (cpu_count == 0) ? 1 : 0;
            c->started = c->bootstrap;
            c->stack_top = (uint64_t)&cpu_stacks[cpu_count][16384];

            char msg[32];
            msg[0] = 'S'; msg[1] = 'M'; msg[2] = 'P'; msg[3] = ':';
            msg[4] = ' '; msg[5] = 'f'; msg[6] = 'o'; msg[7] = 'u';
            msg[8] = 'n'; msg[9] = 'd'; msg[10] = ' '; msg[11] = 'c';
            msg[12] = 'p'; msg[13] = 'u'; msg[14] = ' ';
            msg[15] = (char)('0' + (cpu_count % 10));
            msg[16] = ' '; msg[17] = '('; msg[18] = 'A'; msg[19] = 'P';
            msg[20] = 'I'; msg[21] = 'C'; msg[22] = ' ';
            msg[23] = (char)('0' + (c->apic_id % 10));
            msg[24] = ')';
            msg[25] = c->bootstrap ? '*' : ' ';
            msg[26] = 0;
            dmesg_log(msg);

            cpu_count++;
        }
        p += elen;
    }

    if (cpu_count == 0) {
        smp_set_single_bsp();
        return;
    }

    if (lapic_base) {
        lapic_enable();
    }
}

void ap_kernel_main(uint32_t apic_id) {
    lapic_enable();
    
    for (uint32_t i = 0; i < cpu_count; i++) {
        if (cpus[i].apic_id == apic_id) {
            cpus[i].started = 1;
            char msg[32];
            msg[0] = 'S'; msg[1] = 'M'; msg[2] = 'P'; msg[3] = ':';
            msg[4] = ' '; msg[5] = 'c'; msg[6] = 'p'; msg[7] = 'u';
            msg[8] = ' '; msg[9] = (char)('0' + (i % 10));
            msg[10] = ' '; msg[11] = 'o'; msg[12] = 'n'; msg[13] = 'l';
            msg[14] = 'i'; msg[15] = 'n'; msg[16] = 'e';
            msg[17] = 0;
            dmesg_log(msg);
            sched_start_on_cpu(i);
            break;
        }
    }

    while (1) {
        cpu_pause();
    }
}

void smp_boot_aps(void) {
    if (!lapic_base) {
        console_write("\nSMP: native AP startup unavailable in firmware-input mode.");
        return;
    }

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
    if (!lapic_base) {
        console_write("\n  native APIC/SIPI startup disabled");
    }

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
