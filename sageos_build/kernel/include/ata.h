#ifndef ATA_H
#define ATA_H

#include <stdint.h>

void ata_init(void);
int ata_read_sector_async(uint32_t lba, uint16_t *buffer);
int ata_write_sector_async(uint32_t lba, const uint16_t *buffer);
int ata_wait_completion(void);
void ata_timer_tick(void);

/* Legacy synchronous interface */
void ata_read_sector(uint32_t lba, uint16_t *buffer);
void ata_write_sector(uint32_t lba, const uint16_t *buffer);

#endif