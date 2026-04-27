#include <stdint.h>
#include "io.h"
#include "console.h"
#include "idt.h"
#include "sage_alloc.h"

/* Simple ATA PIO driver for the primary master */
#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERR          0x1F1
#define ATA_PRIMARY_SECCOUNT     0x1F2
#define ATA_PRIMARY_LBA_LOW      0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HIGH     0x1F5
#define ATA_PRIMARY_DRIVE        0x1F6
#define ATA_PRIMARY_STATUS       0x1F7
#define ATA_PRIMARY_COMMAND     0x1F7

#define ATA_IRQ                 14
#define ATA_TIMEOUT_MS          5000  /* 5 second timeout */

typedef enum {
    ATA_OP_READ,
    ATA_OP_WRITE
} ata_operation_t;

typedef struct ata_request {
    ata_operation_t op;
    uint32_t lba;
    uint16_t *buffer;
    int completed;
    int success;
    struct ata_request *next;
} ata_request_t;

static ata_request_t *ata_request_queue = NULL;
static ata_request_t *ata_current_request = NULL;
static uint32_t ata_timeout_counter = 0;

/* Forward declaration for interrupt stub */
__attribute__((naked)) void ata_irq_stub(void);

static void ata_start_request(ata_request_t *req) {
    ata_current_request = req;
    ata_timeout_counter = ATA_TIMEOUT_MS;

    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((req->lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_SECCOUNT, 1);
    outb(ATA_PRIMARY_LBA_LOW, (uint8_t)req->lba);
    outb(ATA_PRIMARY_LBA_MID, (uint8_t)(req->lba >> 8));
    outb(ATA_PRIMARY_LBA_HIGH, (uint8_t)(req->lba >> 16));

    if (req->op == ATA_OP_READ) {
        outb(ATA_PRIMARY_COMMAND, 0x20); /* Read with retry */
    } else {
        outb(ATA_PRIMARY_COMMAND, 0x30); /* Write sectors */
    }
}

static void ata_process_queue(void) {
    if (ata_current_request == NULL && ata_request_queue != NULL) {
        ata_request_t *req = ata_request_queue;
        ata_request_queue = req->next;
        ata_start_request(req);
    }
}

static void ata_complete_request(int success) {
    if (ata_current_request) {
        ata_current_request->completed = 1;
        ata_current_request->success = success;
        ata_current_request = NULL;
    }
    ata_process_queue();
}

void ata_irq_handler(void) {
    if (ata_current_request) {
        if (ata_current_request->op == ATA_OP_READ) {
            /* Read data */
            for (int i = 0; i < 256; i++) {
                ata_current_request->buffer[i] = inw(ATA_PRIMARY_DATA);
            }
            ata_complete_request(1);
        } else {
            /* Write completed */
            ata_complete_request(1);
        }
    }

    pic_send_eoi(ATA_IRQ);
}

void ata_timer_tick(void) {
    if (ata_current_request && ata_timeout_counter > 0) {
        ata_timeout_counter--;
        if (ata_timeout_counter == 0) {
            console_write("ATA timeout\n");
            ata_complete_request(0);
        }
    }
}

void ata_init(void) {
    /* Set up ATA interrupt handler */
    idt_set_interrupt_handler(32 + ATA_IRQ, ata_irq_stub);
    pic_unmask_irq(ATA_IRQ);
}

static ata_request_t *ata_create_request(ata_operation_t op, uint32_t lba, uint16_t *buffer) {
    ata_request_t *req = (ata_request_t *)sage_malloc(sizeof(ata_request_t));
    if (!req) return NULL;

    req->op = op;
    req->lba = lba;
    req->buffer = buffer;
    req->completed = 0;
    req->success = 0;
    req->next = NULL;

    return req;
}

static void ata_queue_request(ata_request_t *req) {
    req->next = NULL;

    if (ata_request_queue == NULL) {
        ata_request_queue = req;
    } else {
        ata_request_t *tail = ata_request_queue;
        while (tail->next) tail = tail->next;
        tail->next = req;
    }

    ata_process_queue();
}

int ata_read_sector_async(uint32_t lba, uint16_t *buffer) {
    ata_request_t *req = ata_create_request(ATA_OP_READ, lba, buffer);
    if (!req) return 0;

    ata_queue_request(req);
    return 1;
}

int ata_write_sector_async(uint32_t lba, const uint16_t *buffer) {
    ata_request_t *req = ata_create_request(ATA_OP_WRITE, lba, (uint16_t *)buffer);
    if (!req) return 0;

    ata_queue_request(req);
    return 1;
}

int ata_wait_completion(void) {
    while (ata_current_request || ata_request_queue) {
        cpu_pause();
    }
    return 1;
}

/* Legacy synchronous interface for compatibility */
void ata_read_sector(uint32_t lba, uint16_t *buffer) {
    if (!ata_read_sector_async(lba, buffer)) return;
    ata_wait_completion();
}

void ata_write_sector(uint32_t lba, const uint16_t *buffer) {
    if (!ata_write_sector_async(lba, buffer)) return;
    ata_wait_completion();
}

__attribute__((naked)) void ata_irq_stub(void) {
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
        "call ata_irq_handler\n"
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
