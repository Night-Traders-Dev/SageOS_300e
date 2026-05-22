#include <stdint.h>
#include <stddef.h>
#include "dmesg.h"
#include "pci.h"
#include "net.h"

// e1000 Registers
#define E1000_CTRL     0x0000
#define E1000_STATUS   0x0008
#define E1000_EEPROM   0x0014
#define E1000_CTRL_EXT 0x0018
#define E1000_IMASK    0x00D0
#define E1000_RCTRL    0x0100
#define E1000_TCTRL    0x0400
#define E1000_RXDESCLO 0x2800
#define E1000_RXDESCHI 0x2804
#define E1000_RXDESCLEN 0x2808
#define E1000_RXDESCHEAD 0x2810
#define E1000_RXDESCTAIL 0x2818
#define E1000_TXDESCLO 0x3800
#define E1000_TXDESCHI 0x3804
#define E1000_TXDESCLEN 0x3808
#define E1000_TXDESCHEAD 0x3810
#define E1000_TXDESCTAIL 0x3818
#define E1000_RAL      0x5400
#define E1000_RAH      0x5404

typedef struct {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint16_t checksum;
    volatile uint8_t  status;
    volatile uint8_t  errors;
    volatile uint16_t special;
} __attribute__((packed)) e1000_rx_desc;

typedef struct {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint8_t  cso;
    volatile uint8_t  cmd;
    volatile uint8_t  status;
    volatile uint8_t  css;
    volatile uint16_t special;
} __attribute__((packed)) e1000_tx_desc;

#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 32

static e1000_rx_desc __attribute__((aligned(16))) rx_descs[E1000_NUM_RX_DESC];
static e1000_tx_desc __attribute__((aligned(16))) tx_descs[E1000_NUM_TX_DESC];

static uint8_t rx_buffers[E1000_NUM_RX_DESC][2048];
static uint8_t tx_buffers[E1000_NUM_TX_DESC][2048];

static uint32_t rx_cur = 0;
static uint32_t tx_cur = 0;

static volatile uint32_t *e1000_mmio_base = NULL;
static uint8_t e1000_mac[6];

static uint32_t e1000_read_reg(uint16_t offset) {
    if (!e1000_mmio_base) return 0;
    return e1000_mmio_base[offset / 4];
}

static void e1000_write_reg(uint16_t offset, uint32_t value) {
    if (!e1000_mmio_base) return;
    e1000_mmio_base[offset / 4] = value;
}

static void e1000_init_rx(void) {
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        rx_descs[i].addr = (uint64_t)(uintptr_t)rx_buffers[i];
        rx_descs[i].status = 0;
    }
    uint64_t rx_addr = (uint64_t)(uintptr_t)rx_descs;
    e1000_write_reg(E1000_RXDESCLO, (uint32_t)(rx_addr & 0xFFFFFFFF));
    e1000_write_reg(E1000_RXDESCHI, (uint32_t)(rx_addr >> 32));
    e1000_write_reg(E1000_RXDESCLEN, E1000_NUM_RX_DESC * 16);
    e1000_write_reg(E1000_RXDESCHEAD, 0);
    e1000_write_reg(E1000_RXDESCTAIL, E1000_NUM_RX_DESC - 1);
    rx_cur = 0;
    e1000_write_reg(E1000_RCTRL, (1 << 1) | (1 << 4) | (1 << 15)); // EN, MPE, BAM
}

static void e1000_init_tx(void) {
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        tx_descs[i].addr = (uint64_t)(uintptr_t)tx_buffers[i];
        tx_descs[i].cmd = 0;
    }
    uint64_t tx_addr = (uint64_t)(uintptr_t)tx_descs;
    e1000_write_reg(E1000_TXDESCLO, (uint32_t)(tx_addr & 0xFFFFFFFF));
    e1000_write_reg(E1000_TXDESCHI, (uint32_t)(tx_addr >> 32));
    e1000_write_reg(E1000_TXDESCLEN, E1000_NUM_TX_DESC * 16);
    e1000_write_reg(E1000_TXDESCHEAD, 0);
    e1000_write_reg(E1000_TXDESCTAIL, 0);
    tx_cur = 0;
    e1000_write_reg(E1000_TCTRL, (1 << 1) | (1 << 3)); // EN, PSP
}

void e1000_init(void) {
    const PciDevice *dev = pci_find_device(0x8086, 0x100E);
    if (!dev) {
        dev = pci_find_device(0x8086, 0x153A);
    }
    if (!dev) return;

    e1000_mmio_base = (volatile uint32_t *)(uintptr_t)(dev->bar[0] & ~0xF);

    // Read MAC address
    uint32_t mac_low = e1000_read_reg(E1000_RAL);
    uint32_t mac_high = e1000_read_reg(E1000_RAH);

    e1000_mac[0] = (uint8_t)(mac_low & 0xFF);
    e1000_mac[1] = (uint8_t)((mac_low >> 8) & 0xFF);
    e1000_mac[2] = (uint8_t)((mac_low >> 16) & 0xFF);
    e1000_mac[3] = (uint8_t)((mac_low >> 24) & 0xFF);
    e1000_mac[4] = (uint8_t)(mac_high & 0xFF);
    e1000_mac[5] = (uint8_t)((mac_high >> 8) & 0xFF);

    e1000_init_rx();
    e1000_init_tx();
    e1000_write_reg(E1000_IMASK, 0);

    NetDevice netdev;
    memset(&netdev, 0, sizeof(netdev));
    for (int i = 0; i < 5; i++) netdev.name[i] = "e1000"[i];
    for (int i = 0; i < 5; i++) netdev.driver[i] = "e1000"[i];
    netdev.kind = NET_DEVICE_ETHERNET;
    netdev.state = NET_STATE_READY;
    netdev.mtu = 1500;
    netdev.flags = NETDEV_FLAG_PRESENT;
    for (int i = 0; i < 6; i++) netdev.hwaddr[i] = e1000_mac[i];
    netdev.hwaddr_valid = 1;

    net_register_device(&netdev);
    dmesg_log("e1000: initialized");
}

int e1000_send_packet(const void *data, size_t len) {
    if (!e1000_mmio_base) return -1;
    if (len > 2048) len = 2048;

    uint32_t old_tx = tx_cur;
    memcpy(tx_buffers[old_tx], data, len);
    tx_descs[old_tx].length = (uint16_t)len;
    tx_descs[old_tx].status = 0;
    tx_descs[old_tx].cmd = (1 << 0) | (1 << 1) | (1 << 3); // EOP, IFCS, RS

    tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;
    e1000_write_reg(E1000_TXDESCTAIL, tx_cur);

    while (!(tx_descs[old_tx].status & 0xF)); // Wait for transmission
    return 0;
}

extern void lwip_port_input(const void *data, size_t len);

void e1000_poll(void) {
    if (!e1000_mmio_base) return;

    while (rx_descs[rx_cur].status & (1 << 0)) { // DD (Descriptor Done)
        size_t len = rx_descs[rx_cur].length;
        lwip_port_input(rx_buffers[rx_cur], len);

        rx_descs[rx_cur].status = 0;
        uint32_t old_rx = rx_cur;
        rx_cur = (rx_cur + 1) % E1000_NUM_RX_DESC;
        e1000_write_reg(E1000_RXDESCTAIL, old_rx);
    }
}
