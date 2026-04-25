#ifndef SAGEOS_STATUS_H
#define SAGEOS_STATUS_H

void status_init(void);
void status_refresh(void);
void status_tick_poll(void);
void status_print(void);

uint64_t ram_total_bytes(void);
uint64_t ram_used_bytes(void);

#endif
