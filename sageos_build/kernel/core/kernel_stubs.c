#include <stdint.h>
#include <stddef.h>
#include "metal_vm.h"

// Dummy strncpy/strcpy/strcat
char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}
char *strcpy(char *dest, const char *src) {
    char *d = dest; while (*src) *d++ = *src++; *d = '\0'; return dest;
}
char *strcat(char *dest, const char *src) {
    char *d = dest; while (*d) d++; while (*src) *d++ = *src++; *d = '\0'; return dest;
}

// ATA & Boot (implemented in ata_pio.c / virtio.c)
void* kernel_get_boot_info(void) { return NULL; }

// Runtime stubs to satisfy linker
typedef struct {} jmp_buf;
int setjmp(jmp_buf env) { return 0; }
void longjmp(jmp_buf env, int val) {}
double strtod(const char *nptr, char **endptr) { return 0.0; }
int access(const char *pathname, int mode) { return -1; }
int mkstemps(char *template, int suffixlen) { return -1; }
int unlink(const char *pathname) { return -1; }

// --- Virt Platform Diagnostic & Subsystem Stubs ---
#include "scheduler.h"
#include "console.h"

// Networking stubs
int net_device_count(void) { return 0; }
void net_format_hwaddr(void* a, void* b) { (void)a; (void)b; }
void* net_get_device(int i) { (void)i; return NULL; }

// Power stubs
void power_qemu_exit(void) {}
void power_halt(void) {}
void power_suspend(void) {}

// Sysinfo stubs
int sysinfo_is_qemu(void) { return 1; }
void sysinfo_cmd(void) {
    console_write("\nSystem Info:\n  Platform: Virtual Bare-metal (rv64/virt)\n  RAM: 128 MB\n");
}

// ACPI stubs
void acpi_cmd_battery(void) { console_write("\nACPI: Not supported."); }
void acpi_cmd_lid(void) { console_write("\nACPI: Not supported."); }
void acpi_cmd_madt(void) { console_write("\nACPI: Not supported."); }
void acpi_cmd_fadt(void) { console_write("\nACPI: Not supported."); }
void acpi_cmd_tables(void) { console_write("\nACPI: Not supported."); }
void acpi_cmd_summary(void) { console_write("\nACPI: Not supported."); }

// SDHCI & PCI stubs
void sdhci_cmd_info(void) { console_write("\nSDHCI: Not supported."); }
void pci_cmd_info(void) { console_write("\nPCI: Not supported."); }
void battery_cmd_info(void) { console_write("\nBattery: Not supported."); }

// SMP stubs
void smp_boot_aps(void) {}
void smp_cmd_info(void) { console_write("\nSMP: Not supported."); }
uint32_t smp_cpu_count(void) { return 1; }

// Timer stubs
void timer_cmd_info(void) { console_write("\nTimer: Not supported."); }
void timer_delay_ms(uint32_t ms) {
    volatile uint32_t i;
    for (uint32_t m = 0; m < ms; m++) {
        for (i = 0; i < 50000; i++) {}
    }
}
uint64_t timer_ticks(void) {
    static uint64_t ticks = 0;
    return ticks++;
}
uint64_t timer_seconds(void) { return timer_ticks() / 100; }
uint32_t timer_cpu_percent(void) { return 0; }
uint32_t timer_cpu_percent_at(uint32_t cpu) { (void)cpu; return 0; }
void timer_poll(void) {}

// Memory stubs
uint64_t ram_total_bytes(void) { return 128 * 1024 * 1024; }
uint64_t ram_used_bytes(void) { return 16 * 1024 * 1024; }

// Battery & SDHCI stubs
int battery_percent(void) { return -1; }
int sdhci_is_available(void) { return 0; }

// Status stubs
void status_refresh(void) {}
void status_print(void) { console_write("\n[System Status: Normal]"); }

// Scheduler stubs
static sched_stats_t dummy_stats = {100, 5, 2, 1000, 50, 1, 1};
const sched_stats_t *sched_get_stats(void) { return &dummy_stats; }
void sched_cmd_info(void) { console_write("\nScheduler: Single-tasking bare metal."); }
int sched_get_thread_info(uint32_t index, char *name, thread_state_t *state, uint32_t *cpu) {
    if (index == 0) {
        char *src = "idle";
        char *d = name;
        while (*src) *d++ = *src++;
        *d = '\0';
        *state = THREAD_STATE_RUNNING;
        *cpu = 0;
        return 1;
    }
    return 0;
}

// WiFi & QCA6174 stubs
void qca6174_cmd_connect(const char* s) { (void)s; console_write("\nWiFi: Not supported on this platform."); }
void qca6174_cmd_info(void) { console_write("\nWiFi: Not supported on this platform."); }
void qca6174_cmd_scan(void) { console_write("\nWiFi: Not supported on this platform."); }
void qca6174_cmd_init_rings(void) {}
void qca6174_cmd_upload(void) {}
void qca6174_cmd_reset(void) {}
void net_cmd_info(void) { console_write("\nNetwork: Not supported on this platform."); }
void net_cmd_selftest(void) { console_write("\nNetwork: Not supported on this platform."); }

// Sage & VM stubs
void sage_run_file(const char *path) {
    (void)path;
    console_write("\nsage: File execution not supported on this platform.");
}
void elf_exec(const char *path, uint64_t sz) {
    (void)path; (void)sz;
    console_write("\nexecelf: ELF execution not supported on this platform.");
}
void sage_import_module(void* vm, const char* name) {
    (void)vm; (void)name;
}
void sage_repl_init(void) { console_write("\nsage: REPL not supported on this platform."); }
void sage_execute(const char* mod) { (void)mod; console_write("\nsage: execution not supported on this platform."); }

// VFS / sagelang bridge stubs (implemented in vfs.c)
