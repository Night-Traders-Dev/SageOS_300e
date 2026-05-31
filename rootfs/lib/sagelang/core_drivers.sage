// ============================================================================
// Core Drivers (SageLang)
// ============================================================================

fn init_serial() { serial_init() }
fn init_keyboard() { keyboard_init() }
fn init_console() { console_init() }
fn init_ata() { ata_init() }
fn init_sdhci() { sdhci_init() }
fn init_net() { net_init() }
fn init_acpi() { acpi_init() }
fn init_wifi() { qca6174_init() }
fn init_pci() { pci_enumerate() }
fn init_smp() { smp_init() }
fn init_smp_firmware() { smp_init_firmware_bsp() }
fn init_swap() { swap_init() }
fn init_idt() { idt_init() }
fn enable_irq() { irq_enable() }
