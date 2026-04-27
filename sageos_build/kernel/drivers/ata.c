#include <stdint.h>
#include "io.h"
#include "console.h"
#include "idt.h"
#include "dmesg.h"
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
#define ATA_PRIMARY_ALT_STATUS   0x3F6

#define ATA_IRQ                 14
#define ATA_TIMEOUT_TICKS       200  /* PIT ticks: about 2 seconds */
#define ATA_WAIT_SPINS          1000000U

#define ATA_STATUS_ERR          0x01
#define ATA_STATUS_DRQ          0x08
#define ATA_STATUS_DF           0x20
#define ATA_STATUS_DRDY         0x40
#define ATA_STATUS_BSY          0x80

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
static int ata_present = 0;
static uint8_t ata_last_status = 0;

/* Forward declaration for interrupt stub */
__attribute__((naked)) void ata_irq_stub(void);
static void ata_complete_request(int success);

static void ata_io_delay(void) {
    (void)inb(ATA_PRIMARY_ALT_STATUS);
    (void)inb(ATA_PRIMARY_ALT_STATUS);
    (void)inb(ATA_PRIMARY_ALT_STATUS);
    (void)inb(ATA_PRIMARY_ALT_STATUS);
}

static int ata_status_is_floating(uint8_t status) {
    return status == 0xFF || status == 0x00;
}

static int ata_wait_not_busy(uint32_t spins) {
    while (spins--) {
        uint8_t status = inb(ATA_PRIMARY_STATUS);
        ata_last_status = status;
        if (ata_status_is_floating(status)) return 0;
        if ((status & ATA_STATUS_BSY) == 0) return 1;
        cpu_pause();
    }
    return 0;
}

static int ata_probe_primary_master(void) {
    uint8_t status;

    outb(ATA_PRIMARY_DRIVE, 0xA0);
    ata_io_delay();
    status = inb(ATA_PRIMARY_STATUS);
    ata_last_status = status;

    if (ata_status_is_floating(status)) return 0;
    if (!ata_wait_not_busy(ATA_WAIT_SPINS / 10)) return 0;

    status = inb(ATA_PRIMARY_STATUS);
    ata_last_status = status;
    return !ata_status_is_floating(status);
}

int ata_is_available(void) {
    return ata_present;
}

static int ata_poll_current_request(void) {
    ata_request_t *req = ata_current_request;
    uint8_t status;

    if (!req) return 1;
    if (!ata_present) {
        ata_complete_request(0);
        return -1;
    }

    status = inb(ATA_PRIMARY_STATUS);
    ata_last_status = status;

    if (ata_status_is_floating(status)) {
        ata_present = 0;
        ata_complete_request(0);
        return -1;
    }

    if (status & ATA_STATUS_BSY) return 0;

    if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) {
        ata_complete_request(0);
        return -1;
    }

    if ((status & ATA_STATUS_DRQ) == 0) return 0;

    if (req->op == ATA_OP_READ) {
        for (int i = 0; i < 256; i++) {
            req->buffer[i] = inw(ATA_PRIMARY_DATA);
        }
        ata_complete_request(1);
        return 1;
    }

    for (int i = 0; i < 256; i++) {
        outw(ATA_PRIMARY_DATA, req->buffer[i]);
    }
    ata_complete_request(1);
    return 1;
}

static void ata_start_request(ata_request_t *req) {
    ata_current_request = req;
    ata_timeout_counter = ATA_TIMEOUT_TICKS;

    if (!ata_present || !ata_wait_not_busy(ATA_WAIT_SPINS)) {
        ata_complete_request(0);
        return;
    }

    outb(ATA_PRIMARY_DRIVE, 0xE0 | ((req->lba >> 24) & 0x0F));
    ata_io_delay();
    if (!ata_wait_not_busy(ATA_WAIT_SPINS)) {
        ata_complete_request(0);
        return;
    }

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
    ata_poll_current_request();
    pic_send_eoi(ATA_IRQ);
}

void ata_timer_tick(void) {
    if (ata_current_request && ata_timeout_counter > 0) {
        ata_timeout_counter--;
        if (ata_timeout_counter == 0) {
            dmesg_log("ata: request timed out");
            ata_complete_request(0);
        }
    }
}

void ata_init(void) {
    ata_request_queue = NULL;
    ata_current_request = NULL;
    ata_timeout_counter = 0;
    ata_present = ata_probe_primary_master();

    if (!ata_present) {
        dmesg_log("ata: no primary-master ATA device");
        return;
    }

    /* Set up ATA interrupt handler */
    idt_set_interrupt_handler(32 + ATA_IRQ, ata_irq_stub);
    pic_unmask_irq(ATA_IRQ);
    dmesg_log("ata: primary-master ATA device detected");
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
    if (!ata_present) return 0;
    ata_request_t *req = ata_create_request(ATA_OP_READ, lba, buffer);
    if (!req) return 0;

    ata_queue_request(req);
    return 1;
}

int ata_write_sector_async(uint32_t lba, const uint16_t *buffer) {
    if (!ata_present) return 0;
    ata_request_t *req = ata_create_request(ATA_OP_WRITE, lba, (uint16_t *)buffer);
    if (!req) return 0;

    ata_queue_request(req);
    return 1;
}

int ata_wait_completion(void) {
    uint32_t timeout = ATA_WAIT_SPINS;

    if (!ata_present) return 0;

    while ((ata_current_request || ata_request_queue) && timeout > 0) {
        ata_poll_current_request();
        if (!ata_current_request && !ata_request_queue) break;
        cpu_pause();
        timeout--;
    }
    if (timeout == 0) {
        dmesg_log("ata: wait timed out");
        if (ata_current_request) ata_complete_request(0);
        return 0;
    }
    return 1;
}

/* Legacy synchronous interface for compatibility */
int ata_read_sector(uint32_t lba, uint16_t *buffer) {
    if (!ata_read_sector_async(lba, buffer)) return 0;
    return ata_wait_completion();
}

int ata_write_sector(uint32_t lba, const uint16_t *buffer) {
    if (!ata_write_sector_async(lba, buffer)) return 0;
    return ata_wait_completion();
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
