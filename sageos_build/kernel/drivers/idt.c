#include <stdint.h>
#include "idt.h"
#include "io.h"
#include "timer.h"
#include "console.h"
#include "keyboard.h"

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
     * Unmask IRQ0 timer and IRQ1 keyboard only.
     */
    outb(0x21, 0xFC);
    outb(0xA1, 0xFF);

    (void)a1;
    (void)a2;
}

void irq0_handler_c(void);
void irq1_handler_c(void);

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

void idt_init(void) {
    irq_disable();

    for (int i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i, default_irq_stub);
    }

    idt_set_gate(32, irq0_stub);
    idt_set_gate(33, irq1_stub);

    IdtPtr ptr;
    ptr.limit = sizeof(idt) - 1;
    ptr.base = (uint64_t)(uintptr_t)&idt[0];

    lidt(&ptr);
    pic_remap();
}
