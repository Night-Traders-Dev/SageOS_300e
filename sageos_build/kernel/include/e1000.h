#ifndef SAGEOS_E1000_H
#define SAGEOS_E1000_H

#include <stddef.h>
#include <stdint.h>

void e1000_init(void);
int e1000_send_packet(const void *data, size_t len);
void e1000_poll(void);

#endif
