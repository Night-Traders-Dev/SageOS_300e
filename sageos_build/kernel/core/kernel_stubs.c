#include <stdint.h>
#include <stddef.h>
#include "metal_vm.h"
// Dummy strncpy/strcpy/strcat (now provided by sage_libc_shim.h/c)
// ATA & Boot (implemented in ata_pio.c / virtio.c)
void* kernel_get_boot_info(void) { return NULL; }
// Runtime stubs to satisfy linker (now provided by sage_libc_shim.h/c)
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

// Memory stubs
// Memory stubs
#include "sysinfo_shared.h"
extern uint64_t pmm_total_frames;
extern uint64_t pmm_used_frames;
#define PAGE_SIZE 4096
uint64_t ram_total_bytes(void) { return pmm_total_frames * PAGE_SIZE; }
uint64_t ram_used_bytes(void) { return pmm_used_frames * PAGE_SIZE; }

// Battery & SDHCI stubs
int battery_percent(void) { return -1; }
int sdhci_is_available(void) { return 0; }
// Status stubs
void status_refresh(void) {}
void status_print(void) { console_write("\n[System Status: Normal]"); }
// Scheduler stubs have been moved to scheduler.c
// WiFi & QCA6174 stubs
void qca6174_cmd_connect(const char* s) { (void)s; console_write("\nWiFi: Not supported on this platform."); }
void qca6174_cmd_info(void) { console_write("\nWiFi: Not supported on this platform."); }
void qca6174_cmd_scan(void) { console_write("\nWiFi: Not supported on this platform."); }
void qca6174_cmd_init_rings(void) {}
void qca6174_cmd_upload(void) {}
void qca6174_cmd_reset(void) {}
void net_cmd_info(void) { console_write("\nNetwork: Not supported on this platform."); }
void net_cmd_selftest(void) { console_write("\nNetwork: Not supported on this platform."); }
// Sage & VM stubs (now implemented in sageos_bridge.c)
// VFS / sagelang bridge stubs (implemented in vfs.c)
