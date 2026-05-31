// ============================================================================
// Core Drivers (SageLang)
// ============================================================================

proc init_serial(): serial_init() end
proc init_keyboard(): keyboard_init() end
proc init_console(): console_init() end
proc init_ata(): ata_init() end
proc init_sdhci(): sdhci_init() end
proc init_net(): net_init() end
proc init_acpi(): acpi_init() end
proc init_wifi(): qca6174_init() end
proc init_pci(): pci_enumerate() end
proc init_smp(): smp_init() end
proc init_smp_firmware(): smp_init_firmware_bsp() end
proc init_swap(): swap_init() end
proc init_idt(): idt_init() end
proc enable_irq(): irq_enable() end
