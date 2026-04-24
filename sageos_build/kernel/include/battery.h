#ifndef SAGEOS_BATTERY_H
#define SAGEOS_BATTERY_H

#include <stdint.h>

void battery_init(void);
int battery_percent(void);
void battery_cmd_info(void);

#endif
