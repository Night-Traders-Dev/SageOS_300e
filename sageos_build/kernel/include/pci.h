#ifndef SAGEOS_PCI_H
#define SAGEOS_PCI_H

#include <stdint.h>

/* PCI configuration space ports (Type 1 mechanism) */
#define PCI_CONFIG_ADDR  0x0CF8
#define PCI_CONFIG_DATA  0x0CFC

/* Maximum discovered devices */
#define PCI_MAX_DEVICES  64

/* PCI class codes (major) */
#define PCI_CLASS_STORAGE        0x01
#define PCI_CLASS_NETWORK        0x02
#define PCI_CLASS_DISPLAY        0x03
#define PCI_CLASS_BRIDGE         0x06
#define PCI_CLASS_SYSTEM         0x08

/* Known vendor IDs */
#define PCI_VENDOR_AMD           0x1022
#define PCI_VENDOR_QUALCOMM_ATH  0x168C
#define PCI_VENDOR_INTEL         0x8086

/* Known device IDs */
#define PCI_DEVICE_QCA6174A      0x003E

/* PCI device descriptor */
typedef struct {
    uint8_t  bus;
    uint8_t  device;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  header_type;
    uint32_t bar[6];
    uint8_t  irq_line;
} PciDevice;

/* Read 32 bits from PCI configuration space */
uint32_t pci_config_read(uint8_t bus, uint8_t device, uint8_t func,
                         uint8_t offset);

/* Write 32 bits to PCI configuration space */
void pci_config_write(uint8_t bus, uint8_t device, uint8_t func,
                      uint8_t offset, uint32_t val);

/* Scan all PCI buses and populate the device table */
void pci_enumerate(void);

/* Return device count and pointer to device array */
int pci_device_count(void);
const PciDevice *pci_get_devices(void);

/* Find first device matching vendor:device ID. Returns NULL if not found. */
const PciDevice *pci_find_device(uint16_t vendor_id, uint16_t device_id);

/* Find first device matching class:subclass. Returns NULL if not found. */
const PciDevice *pci_find_class(uint8_t class_code, uint8_t subclass);

/* Shell command: print PCI device listing */
void pci_cmd_info(void);

#endif /* SAGEOS_PCI_H */
