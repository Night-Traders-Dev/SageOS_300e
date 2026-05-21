#include "metal_vm.h"
#include "serial.h"
#include "keyboard.h"
#include "console.h"
#include "ata.h"
#include "sdhci.h"
#include "net.h"
#include "acpi.h"
#include "wifi_qca6174.h"
#include "pci.h"
#include "smp.h"
#include "swap.h"
#include "sysinfo.h"
#include "idt.h"
#include "bootinfo.h"

extern SageOSBootInfo* console_boot_info(void);

// Native function: serial_init(void)
static MetalValue native_serial_init(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    serial_init(); return mv_nil();
}
// Keyboard
static MetalValue native_keyboard_init(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    keyboard_init(); return mv_nil();
}
// Console (Framebuffer)
static MetalValue native_console_init(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    console_init(console_boot_info()); return mv_nil();
}
// ATA
static MetalValue native_ata_init(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    ata_init(); return mv_nil();
}
// SDHCI
static MetalValue native_sdhci_init(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    sdhci_init(); return mv_nil();
}
// Net
static MetalValue native_net_init(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    net_init(); return mv_nil();
}
// ACPI
static MetalValue native_acpi_init(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    acpi_init(console_boot_info()); return mv_nil();
}
// Wi-Fi
static MetalValue native_qca6174_init(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    qca6174_init(); return mv_nil();
}
// PCI
static MetalValue native_pci_enumerate(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    pci_enumerate(); return mv_nil();
}
// SMP
static MetalValue native_smp_init(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    smp_init(); return mv_nil();
}
static MetalValue native_smp_init_firmware_bsp(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    smp_init_firmware_bsp(); return mv_nil();
}
// Swap
static MetalValue native_swap_init(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    swap_init(); return mv_nil();
}
// IDT
static MetalValue native_idt_init(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    idt_init(); return mv_nil();
}
static MetalValue native_irq_enable(MetalVM* vm, MetalValue* args, int argc) {
    (void)vm; (void)args; (void)argc;
    irq_enable(); return mv_nil();
}


void register_core_drivers_native_bindings(MetalVM* vm) {
    metal_vm_register_native(vm, "serial_init", native_serial_init);
    metal_vm_register_native(vm, "keyboard_init", native_keyboard_init);
    metal_vm_register_native(vm, "console_init", native_console_init);
    metal_vm_register_native(vm, "ata_init", native_ata_init);
    metal_vm_register_native(vm, "sdhci_init", native_sdhci_init);
    metal_vm_register_native(vm, "net_init", native_net_init);
    metal_vm_register_native(vm, "acpi_init", native_acpi_init);
    metal_vm_register_native(vm, "qca6174_init", native_qca6174_init);
    metal_vm_register_native(vm, "pci_enumerate", native_pci_enumerate);
    metal_vm_register_native(vm, "smp_init", native_smp_init);
    metal_vm_register_native(vm, "smp_init_firmware_bsp", native_smp_init_firmware_bsp);
    metal_vm_register_native(vm, "swap_init", native_swap_init);
    metal_vm_register_native(vm, "idt_init", native_idt_init);
    metal_vm_register_native(vm, "irq_enable", native_irq_enable);
}
