#include <stdint.h>
#include "idt.h"
#include "io.h"
#include "timer.h"
#include "console.h"
#include "keyboard.h"
#include "acpi.h"

typedef struct __attribute__((packed)) {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} IdtEntry;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint64_t base;
} IdtPtr;

static IdtEntry idt[256];

static void idt_set_gate(uint8_t vector, void *handler) {
    uint64_t addr = (uint64_t)(uintptr_t)handler;

    idt[vector].offset_low = (uint16_t)(addr & 0xFFFF);
    idt[vector].selector = 0x38;
    idt[vector].ist = 0;
    idt[vector].type_attr = 0x8E;
    idt[vector].offset_mid = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vector].zero = 0;
}

void idt_set_interrupt_handler(uint8_t vector, void *handler) {
    idt_set_gate(vector, handler);
}

static void lidt(IdtPtr *ptr) {
    __asm__ volatile ("lidt (%0)" : : "r"(ptr) : "memory");
}

void irq_enable(void) {
    __asm__ volatile ("sti");
}

void irq_disable(void) {
    __asm__ volatile ("cli");
}

void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(0xA0, 0x20);
    }

    outb(0x20, 0x20);
}

static void pic_remap(void) {
    uint8_t a1 = inb(0x21);
    uint8_t a2 = inb(0xA1);

    outb(0x20, 0x11);
    outb(0xA0, 0x11);

    outb(0x21, 0x20);
    outb(0xA1, 0x28);

    outb(0x21, 0x04);
    outb(0xA1, 0x02);

    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    /*
     * Unmask IRQ0 timer and IRQ1 keyboard.
     */
    uint8_t master_mask = 0xFC; /* 1111 1100 -> IRQ0, IRQ1 unmasked */
    uint8_t slave_mask = 0xFF;

    const AcpiInfo *acpi = acpi_info();
    if (acpi && acpi->sci_irq > 0) {
        if (acpi->sci_irq < 8) {
            master_mask &= (uint8_t)~(1 << acpi->sci_irq);
        } else if (acpi->sci_irq < 16) {
            slave_mask &= (uint8_t)~(1 << (acpi->sci_irq - 8));
            master_mask &= (uint8_t)~(1 << 2); /* Unmask cascade IRQ2 */
        }
    }

    outb(0x21, master_mask);
    outb(0xA1, slave_mask);

    (void)a1;
    (void)a2;
}

void irq0_handler_c(void);
void irq1_handler_c(void);
void sci_handler_c(void);
void ata_handler_c(void);

__attribute__((naked)) void sci_stub(void) {
    __asm__ volatile (
        "pushq %rax\n"
        "pushq %rbx\n"
        "pushq %rcx\n"
        "pushq %rdx\n"
        "pushq %rsi\n"
        "pushq %rdi\n"
        "pushq %rbp\n"
        "pushq %r8\n"
        "pushq %r9\n"
        "pushq %r10\n"
        "pushq %r11\n"
        "pushq %r12\n"
        "pushq %r13\n"
        "pushq %r14\n"
        "pushq %r15\n"
        "call sci_handler_c\n"
        "popq %r15\n"
        "popq %r14\n"
        "popq %r13\n"
        "popq %r12\n"
        "popq %r11\n"
        "popq %r10\n"
        "popq %r9\n"
        "popq %r8\n"
        "popq %rbp\n"
        "popq %rdi\n"
        "popq %rsi\n"
        "popq %rdx\n"
        "popq %rcx\n"
        "popq %rbx\n"
        "popq %rax\n"
        "iretq\n"
    );
}

__attribute__((naked)) void ata_stub(void) {
    __asm__ volatile (
        "pushq %rax\n"
        "pushq %rbx\n"
        "pushq %rcx\n"
        "pushq %rdx\n"
        "pushq %rsi\n"
        "pushq %rdi\n"
        "pushq %rbp\n"
        "pushq %r8\n"
        "pushq %r9\n"
        "pushq %r10\n"
        "pushq %r11\n"
        "pushq %r12\n"
        "pushq %r13\n"
        "pushq %r14\n"
        "pushq %r15\n"
        "call ata_handler_c\n"
        "popq %r15\n"
        "popq %r14\n"
        "popq %r13\n"
        "popq %r12\n"
        "popq %r11\n"
        "popq %r10\n"
        "popq %r9\n"
        "popq %r8\n"
        "popq %rbp\n"
        "popq %rdi\n"
        "popq %rsi\n"
        "popq %rdx\n"
        "popq %rcx\n"
        "popq %rbx\n"
        "popq %rax\n"
        "iretq\n"
    );
}

__attribute__((naked)) void irq0_stub(void) {
    __asm__ volatile (
        "pushq %rax\n"
        "pushq %rbx\n"
        "pushq %rcx\n"
        "pushq %rdx\n"
        "pushq %rsi\n"
        "pushq %rdi\n"
        "pushq %rbp\n"
        "pushq %r8\n"
        "pushq %r9\n"
        "pushq %r10\n"
        "pushq %r11\n"
        "pushq %r12\n"
        "pushq %r13\n"
        "pushq %r14\n"
        "pushq %r15\n"
        "call irq0_handler_c\n"
        "popq %r15\n"
        "popq %r14\n"
        "popq %r13\n"
        "popq %r12\n"
        "popq %r11\n"
        "popq %r10\n"
        "popq %r9\n"
        "popq %r8\n"
        "popq %rbp\n"
        "popq %rdi\n"
        "popq %rsi\n"
        "popq %rdx\n"
        "popq %rcx\n"
        "popq %rbx\n"
        "popq %rax\n"
        "iretq\n"
    );
}

__attribute__((naked)) void irq1_stub(void) {
    __asm__ volatile (
        "pushq %rax\n"
        "pushq %rbx\n"
        "pushq %rcx\n"
        "pushq %rdx\n"
        "pushq %rsi\n"
        "pushq %rdi\n"
        "pushq %rbp\n"
        "pushq %r8\n"
        "pushq %r9\n"
        "pushq %r10\n"
        "pushq %r11\n"
        "pushq %r12\n"
        "pushq %r13\n"
        "pushq %r14\n"
        "pushq %r15\n"
        "call irq1_handler_c\n"
        "popq %r15\n"
        "popq %r14\n"
        "popq %r13\n"
        "popq %r12\n"
        "popq %r11\n"
        "popq %r10\n"
        "popq %r9\n"
        "popq %r8\n"
        "popq %rbp\n"
        "popq %rdi\n"
        "popq %rsi\n"
        "popq %rdx\n"
        "popq %rcx\n"
        "popq %rbx\n"
        "popq %rax\n"
        "iretq\n"
    );
}

__attribute__((naked)) void default_irq_stub(void) {
    __asm__ volatile ("iretq\n");
}

void irq0_handler_c(void) {
    timer_irq();
    pic_send_eoi(0);
}

void irq1_handler_c(void) {
    keyboard_irq();
    pic_send_eoi(1);
}

void sci_handler_c(void) {
    const AcpiInfo *acpi = acpi_info();
    acpi_sci_handler();
    if (acpi) pic_send_eoi((uint8_t)acpi->sci_irq);
}

void ata_handler_c(void) {
    extern void ata_irq_handler(void);
    ata_irq_handler();
}

void pic_unmask_irq(uint8_t irq) {
    if (irq < 8) {
        uint8_t mask = inb(0x21);
        mask &= (uint8_t)~(1 << irq);
        outb(0x21, mask);
    } else if (irq < 16) {
        uint8_t mask = inb(0xA1);
        mask &= (uint8_t)~(1 << (irq - 8));
        outb(0xA1, mask);
        /* Also unmask cascade IRQ2 */
        mask = inb(0x21);
        mask &= (uint8_t)~(1 << 2);
        outb(0x21, mask);
    }
}

void idt_init(void) {
    irq_disable();

    for (int i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i, default_irq_stub);
    }

    idt_set_gate(32, irq0_stub);
    idt_set_gate(33, irq1_stub);
    idt_set_gate(46, ata_stub); /* IRQ 14 = 32 + 14 */

    const AcpiInfo *acpi = acpi_info();
    if (acpi && acpi->sci_irq > 0 && acpi->sci_irq < 16) {
        idt_set_gate((uint8_t)(32 + acpi->sci_irq), sci_stub);
    }

    IdtPtr ptr;
    ptr.limit = sizeof(idt) - 1;
    ptr.base = (uint64_t)(uintptr_t)&idt[0];

    lidt(&ptr);
    pic_remap();
}
