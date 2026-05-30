#ifndef SAGEOS_TIMER_H
#define SAGEOS_TIMER_H

#include <stdint.h>

void timer_init(void);
void timer_poll(void);
void timer_idle_poll(void);
void timer_irq(void);

uint64_t timer_ticks(void);
uint64_t timer_seconds(void);
uint64_t timer_elapsed_centiseconds(void);
uint32_t timer_cpu_percent(void);
uint32_t timer_cpu_percent_at(uint32_t cpu);
void timer_delay_ms(uint32_t ms);

void timer_cmd_info(void);

#endif
