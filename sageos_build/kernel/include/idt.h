#ifndef SAGEOS_IDT_H
#define SAGEOS_IDT_H

#include <stdint.h>

void idt_init(void);
void irq_enable(void);
void irq_disable(void);
void pic_send_eoi(uint8_t irq);

#endif
