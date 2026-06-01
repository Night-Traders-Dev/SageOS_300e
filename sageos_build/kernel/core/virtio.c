#include <stdint.h>
#include "io.h"
#include "console.h"
#include "ata.h"
#include "dmesg.h"

#if !defined(__x86_64__)

/* VirtIO MMIO Block Driver for ARM64 and RISC-V Virt machines */

#if defined(__aarch64__)
#define VIRTIO_MMIO_BASE 0x0a000000
#define VIRTIO_MMIO_STEP 0x200
#elif defined(__riscv)
#define VIRTIO_MMIO_BASE 0x10001000
#define VIRTIO_MMIO_STEP 0x1000
#else
#define VIRTIO_MMIO_BASE 0
#define VIRTIO_MMIO_STEP 0
#endif

#define REG_MAGIC         0x000
#define REG_VERSION       0x04
#define REG_DEVICE_ID     0x008
#define REG_VENDOR_ID     0x00c
#define REG_DEVICE_FEAT   0x010
#define REG_GUEST_FEAT    0x020
#define REG_QUEUE_SEL     0x030
#define REG_QUEUE_NUM_MAX 0x034
#define REG_QUEUE_NUM     0x038
#define REG_QUEUE_READY   0x044
#define REG_QUEUE_NOTIFY  0x050
#define REG_STATUS        0x070
#define REG_DESC_LOW      0x080
#define REG_DESC_HIGH     0x084
#define REG_AVAIL_LOW     0x090
#define REG_AVAIL_HIGH    0x094
#define REG_USED_LOW      0x0a0
#define REG_USED_HIGH     0x0a4

#define VIRTIO_MAGIC 0x74726976
#define DEVICE_TYPE_BLOCK 2

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[16];
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[16];
} __attribute__((packed));

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2

#define VIRTIO_BLK_T_IN  0
#define VIRTIO_BLK_T_OUT 1

static inline void mb(void) {
#if defined(__riscv)
    __asm__ volatile ("fence rw, rw" ::: "memory");
#elif defined(__aarch64__)
    __asm__ volatile ("dmb sy" ::: "memory");
#else
    __asm__ volatile ("" ::: "memory");
#endif
}

static uint64_t mmio_base = 0;
static int virtio_present = 0;

/* Align to 4KB for simplicity */
static uint8_t vq_space[8192] __attribute__((aligned(4096)));
static struct virtq_desc *desc;
static struct virtq_avail *avail;
static struct virtq_used *used;

static uint32_t mmio_read(uint32_t reg) {
    return *(volatile uint32_t *)(mmio_base + reg);
}

static void mmio_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(mmio_base + reg) = val;
}

void ata_init(void) {
    if (VIRTIO_MMIO_BASE == 0) return;

    /* Probe for block device.
     * QEMU ARM virt has 32 MMIO transports, RISC-V virt has 8.
     * Devices are assigned from the highest index downward.       */
#if defined(__aarch64__)
    int probe_count = 32;
#else
    int probe_count = 8;
#endif
    for (int i = 0; i < probe_count; i++) {
        uint64_t base = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_STEP);
        uint32_t magic = *(volatile uint32_t *)(base + REG_MAGIC);
        if (magic == VIRTIO_MAGIC) {
            uint32_t devid = *(volatile uint32_t *)(base + REG_DEVICE_ID);
            if (devid == DEVICE_TYPE_BLOCK && !virtio_present) {
                mmio_base = base;
                virtio_present = 1;
            }
        }
    }

    if (!virtio_present) {
        dmesg_log("virtio: No block device found");
        return;
    }

    /* Reset and initialize */
    mmio_write(REG_STATUS, 0); // Reset
    mb();
    mmio_write(REG_STATUS, 1); // ACK
    mmio_write(REG_STATUS, 3); // ACK | DRIVER

    uint32_t version = mmio_read(REG_VERSION);

    if (version == 1) {
        /* ===== Legacy VirtIO MMIO (v1) ===== */

        /* Feature negotiation: simple read & echo (no FeatureSel registers) */
        uint32_t host_features = mmio_read(REG_DEVICE_FEAT);
        mmio_write(REG_GUEST_FEAT, host_features);

        /* GuestPageSize — MANDATORY for legacy, must be set before queue init */
        mmio_write(0x28, 4096);

        /* Initialize queue 0 */
        mmio_write(REG_QUEUE_SEL, 0);
        uint32_t max = mmio_read(REG_QUEUE_NUM_MAX);
        if (max < 16) {
            dmesg_log("virtio: Queue size too small");
            virtio_present = 0;
            return;
        }

        desc = (struct virtq_desc *)vq_space;
        avail = (struct virtq_avail *)(vq_space + 16 * sizeof(struct virtq_desc));
        used = (struct virtq_used *)(vq_space + 4096);

        mmio_write(REG_QUEUE_NUM, 16);
        mmio_write(0x3c, 4096); /* QueueAlign */
        uint32_t pfn = (uint32_t)((uintptr_t)vq_space >> 12);
        mmio_write(0x40, pfn); /* QueuePFN */

        mmio_write(REG_STATUS, 7); // ACK | DRIVER | DRIVER_OK
    } else {
        /* ===== Modern VirtIO MMIO (v2) ===== */

        /* Paged feature negotiation */
        mmio_write(0x14, 0);  /* DeviceFeaturesSel = page 0 */
        uint32_t host_lo = mmio_read(REG_DEVICE_FEAT);
        mmio_write(0x24, 0);  /* DriverFeaturesSel = page 0 */
        mmio_write(REG_GUEST_FEAT, host_lo);

        mmio_write(0x14, 1);  /* DeviceFeaturesSel = page 1 */
        uint32_t host_hi = mmio_read(REG_DEVICE_FEAT);
        mmio_write(0x24, 1);  /* DriverFeaturesSel = page 1 */
        mmio_write(REG_GUEST_FEAT, host_hi);

        /* FEATURES_OK — mandatory for v2 */
        mmio_write(REG_STATUS, 0x0B); // ACK | DRIVER | FEATURES_OK
        mb();
        uint32_t status_check = mmio_read(REG_STATUS);
        if (!(status_check & 0x08)) {
            dmesg_log("virtio: device rejected features (FEATURES_OK not set)");
            virtio_present = 0;
            return;
        }

        /* Initialize queue 0 */
        mmio_write(REG_QUEUE_SEL, 0);
        uint32_t max = mmio_read(REG_QUEUE_NUM_MAX);
        if (max < 16) {
            dmesg_log("virtio: Queue size too small");
            virtio_present = 0;
            return;
        }

        desc = (struct virtq_desc *)vq_space;
        avail = (struct virtq_avail *)(vq_space + 16 * sizeof(struct virtq_desc));
        used = (struct virtq_used *)(vq_space + 4096);

        mmio_write(REG_QUEUE_NUM, 16);
        mmio_write(REG_DESC_LOW, (uint32_t)(uintptr_t)desc);
        mmio_write(REG_DESC_HIGH, (uint32_t)((uint64_t)(uintptr_t)desc >> 32));
        mmio_write(REG_AVAIL_LOW, (uint32_t)(uintptr_t)avail);
        mmio_write(REG_AVAIL_HIGH, (uint32_t)((uint64_t)(uintptr_t)avail >> 32));
        mmio_write(REG_USED_LOW, (uint32_t)(uintptr_t)used);
        mmio_write(REG_USED_HIGH, (uint32_t)((uint64_t)(uintptr_t)used >> 32));
        mmio_write(REG_QUEUE_READY, 1);

        mmio_write(REG_STATUS, 0x0F); // ACK | DRIVER | FEATURES_OK | DRIVER_OK
    }

    console_write("\nvirtio-blk: Initialized on transport ");
    console_hex64(mmio_base);
    console_write(" (v");
    console_u32(version);
    console_write(")\n");
    dmesg_log("virtio-blk: Initialized successfully");
}

int ata_is_available(void) {
    return virtio_present;
}

static struct virtio_blk_req req __attribute__((aligned(16)));
static uint8_t blk_status __attribute__((aligned(16)));
static uint16_t last_used_idx = 0;

int ata_read_sector(uint32_t lba, uint16_t *buffer) {
    if (!virtio_present) return 0;

    req.type = VIRTIO_BLK_T_IN;
    req.sector = lba;
    blk_status = 0xFF;

    /* Fill descriptors */
    desc[0].addr = (uintptr_t)&req;
    desc[0].len = sizeof(req);
    desc[0].flags = VRING_DESC_F_NEXT;
    desc[0].next = 1;

    desc[1].addr = (uintptr_t)buffer;
    desc[1].len = 512;
    desc[1].flags = VRING_DESC_F_NEXT | VRING_DESC_F_WRITE;
    desc[1].next = 2;

    desc[2].addr = (uintptr_t)&blk_status;
    desc[2].len = 1;
    desc[2].flags = VRING_DESC_F_WRITE;
    desc[2].next = 0;

    mb();
    avail->ring[avail->idx % 16] = 0;
    mb();
    avail->idx++;
    mb();

    mmio_write(REG_QUEUE_NOTIFY, 0);

    /* Poll for completion */
    int timeout = 0;
    while (used->idx == last_used_idx) {
        mb();
        cpu_pause();
        if (++timeout > 10000000) {
            return 0;
        }
    }
    last_used_idx = used->idx;
    mb();

    return blk_status == 0;
}

int ata_write_sector(uint32_t lba, const uint16_t *buffer) {
    if (!virtio_present) return 0;

    req.type = VIRTIO_BLK_T_OUT;
    req.sector = lba;
    blk_status = 0xFF;

    desc[0].addr = (uintptr_t)&req;
    desc[0].len = sizeof(req);
    desc[0].flags = VRING_DESC_F_NEXT;
    desc[0].next = 1;

    desc[1].addr = (uintptr_t)buffer;
    desc[1].len = 512;
    desc[1].flags = VRING_DESC_F_NEXT;
    desc[1].next = 2;

    desc[2].addr = (uintptr_t)&blk_status;
    desc[2].len = 1;
    desc[2].flags = VRING_DESC_F_WRITE;
    desc[2].next = 0;

    mb();
    avail->ring[avail->idx % 16] = 0;
    mb();
    avail->idx++;
    mb();

    mmio_write(REG_QUEUE_NOTIFY, 0);

    while (used->idx == last_used_idx) {
        mb();
        cpu_pause();
    }
    last_used_idx = used->idx;
    mb();

    return blk_status == 0;
}

int ata_read_sector_async(uint32_t lba, uint16_t *buffer) { return ata_read_sector(lba, buffer); }
int ata_write_sector_async(uint32_t lba, const uint16_t *buffer) { return ata_write_sector(lba, buffer); }
int ata_wait_completion(void) { return 1; }
void ata_timer_tick(void) {}

#endif
