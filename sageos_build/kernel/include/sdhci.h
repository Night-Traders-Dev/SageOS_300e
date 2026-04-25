#ifndef SAGEOS_SDHCI_H
#define SAGEOS_SDHCI_H

#include <stdint.h>

/* SDHCI PCI class/subclass */
#define SDHCI_PCI_CLASS     0x08   /* System Peripheral */
#define SDHCI_PCI_SUBCLASS  0x05   /* SD Host Controller */

/* SDHCI register offsets (from BAR0 MMIO base) */
#define SDHCI_REG_CAP       0x40   /* Capabilities register (32-bit) */
#define SDHCI_REG_CAP_HI    0x44   /* Capabilities high (32-bit) */
#define SDHCI_REG_VERSION   0xFE   /* Host Controller Version (16-bit) */

/* Initialize SDHCI — discover controller via PCI. Returns 1 if found. */
int sdhci_init(void);

/* Shell command: print SDHCI / eMMC controller info */
void sdhci_cmd_info(void);

#endif /* SAGEOS_SDHCI_H */
