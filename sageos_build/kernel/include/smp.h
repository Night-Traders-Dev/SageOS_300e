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
    
    /* New fields for AP startup */
    uint64_t stack_top;
    volatile uint32_t started;
} CpuInfo;

#define LAPIC_ID          0x0020
#define LAPIC_VER         0x0030
#define LAPIC_TPR         0x0080
#define LAPIC_EOI         0x00B0
#define LAPIC_LDR         0x00D0
#define LAPIC_DFR         0x00E0
#define LAPIC_SVR         0x00F0
#define LAPIC_ESR         0x0280
#define LAPIC_ICR_LOW     0x0300
#define LAPIC_ICR_HIGH    0x0310
#define LAPIC_LVT_TMR     0x0320
#define LAPIC_LVT_PERF    0x0340
#define LAPIC_LVT_LINT0   0x0350
#define LAPIC_LVT_LINT1   0x0360
#define LAPIC_LVT_ERR     0x0370
#define LAPIC_TMRINIT     0x0380
#define LAPIC_TMRCUR      0x0390
#define LAPIC_TMRDIV      0x03E0

void smp_init(void);
void smp_init_firmware_bsp(void);
uint32_t smp_cpu_count(void);
const CpuInfo *smp_cpu(uint32_t idx);
uint32_t smp_current_cpu_index(void);
void smp_cmd_info(void);
void smp_boot_aps(void);

uint32_t lapic_read(uint32_t reg);
void lapic_write(uint32_t reg, uint32_t val);
void lapic_eoi(void);

int smp_ap_start_supported(void);

#endif
