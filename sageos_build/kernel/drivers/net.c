#include <stddef.h>
#include <stdint.h>
#include "console.h"
#include "dmesg.h"
#include "net.h"
#include "wifi_qca6174.h"

typedef struct {
    int l2_registry_ready;
    int frame_helpers_ready;
    int arp_ready;
    int ipv4_ready;
    int icmp_ready;
    int udp_ready;
    int dhcp_ready;
    int dns_ready;
    int tcp_ready;
} NetStackState;

static NetDevice g_net_devices[NET_MAX_DEVICES];
static int g_net_device_count = 0;
static int g_net_initialized = 0;
static NetStackState g_net_stack;

static const char *net_kind_name(NetDeviceKind kind) {
    switch (kind) {
    case NET_DEVICE_LOOPBACK: return "loopback";
    case NET_DEVICE_ETHERNET: return "ethernet";
    case NET_DEVICE_WIFI:     return "wifi";
    default:                  return "unknown";
    }
}

static const char *net_state_name(NetDeviceState state) {
    switch (state) {
    case NET_STATE_DOWN:            return "down";
    case NET_STATE_PROBED:          return "probed";
    case NET_STATE_FIRMWARE_STAGED: return "fw-staged";
    case NET_STATE_READY:           return "ready";
    default:                        return "unknown";
    }
}

static void net_print_hex8(uint8_t value) {
    static const char hex[] = "0123456789abcdef";
    char text[3];
    text[0] = hex[(value >> 4) & 0xF];
    text[1] = hex[value & 0xF];
    text[2] = 0;
    console_write(text);
}

static void net_print_yes_no(int value) {
    console_write(value ? "ready" : "pending");
}

void net_init(void) {
    g_net_device_count = 0;
    g_net_initialized = 1;
    memset(g_net_devices, 0, sizeof(g_net_devices));
    memset(&g_net_stack, 0, sizeof(g_net_stack));

    g_net_stack.l2_registry_ready = 1;
    g_net_stack.frame_helpers_ready = 1;

    qca6174_init();

    if (g_net_device_count == 0) {
        dmesg_log("net: no interfaces registered");
    } else {
        dmesg_log("net: interface registry initialized");
    }
}

int net_is_initialized(void) {
    return g_net_initialized;
}

int net_register_device(const NetDevice *device) {
    if (!device) return 0;
    if (g_net_device_count >= NET_MAX_DEVICES) return 0;

    memcpy(&g_net_devices[g_net_device_count], device, sizeof(*device));
    g_net_device_count++;
    return 1;
}

int net_device_count(void) {
    return g_net_device_count;
}

const NetDevice *net_get_device(int index) {
    if (index < 0 || index >= g_net_device_count) return NULL;
    return &g_net_devices[index];
}

uint16_t net_ipv4_checksum(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += ((uint32_t)bytes[0] << 8) | bytes[1];
        bytes += 2;
        len -= 2;
    }
    if (len) sum += (uint32_t)bytes[0] << 8;

    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum);
}

void net_format_hwaddr(const uint8_t *addr, int valid, char *out, size_t out_size) {
    static const char hex[] = "0123456789abcdef";

    if (!out || out_size == 0) return;
    if (!valid || !addr) {
        const char *pending = "pending";
        size_t i = 0;
        while (pending[i] && i + 1 < out_size) {
            out[i] = pending[i];
            i++;
        }
        out[i] = 0;
        return;
    }

    if (out_size < 18) {
        out[0] = 0;
        return;
    }

    for (size_t i = 0; i < NET_HWADDR_LEN; i++) {
        size_t pos = i * 3;
        out[pos] = hex[(addr[i] >> 4) & 0xF];
        out[pos + 1] = hex[addr[i] & 0xF];
        if (i + 1 < NET_HWADDR_LEN) out[pos + 2] = ':';
    }
    out[17] = 0;
}

void net_cmd_info(void) {
    console_write("\nNetworking status:");
    console_write("\n  device registry : ");
    net_print_yes_no(g_net_stack.l2_registry_ready);
    console_write("\n  frame helpers   : ");
    net_print_yes_no(g_net_stack.frame_helpers_ready);
    console_write("\n  ARP             : ");
    net_print_yes_no(g_net_stack.arp_ready);
    console_write("\n  IPv4            : ");
    net_print_yes_no(g_net_stack.ipv4_ready);
    console_write("\n  ICMP            : ");
    net_print_yes_no(g_net_stack.icmp_ready);
    console_write("\n  UDP             : ");
    net_print_yes_no(g_net_stack.udp_ready);
    console_write("\n  DHCP            : ");
    net_print_yes_no(g_net_stack.dhcp_ready);
    console_write("\n  DNS             : ");
    net_print_yes_no(g_net_stack.dns_ready);
    console_write("\n  TCP             : ");
    net_print_yes_no(g_net_stack.tcp_ready);

    console_write("\n  interfaces      : ");
    console_u32((uint32_t)g_net_device_count);

    if (g_net_device_count == 0) {
        console_write("\n  (no network devices registered)");
        return;
    }

    for (int i = 0; i < g_net_device_count; i++) {
        const NetDevice *dev = &g_net_devices[i];
        char hwaddr[18];

        net_format_hwaddr(dev->hwaddr, dev->hwaddr_valid, hwaddr, sizeof(hwaddr));

        console_write("\n  ");
        console_write(dev->name);
        console_write("  ");
        console_write(net_kind_name(dev->kind));
        console_write("  ");
        console_write(dev->driver);
        console_write("  ");
        console_write(net_state_name(dev->state));

        if (dev->pci_backed) {
            console_write("  pci ");
            net_print_hex8(dev->bus);
            console_write(":");
            net_print_hex8(dev->device);
            console_write(".");
            console_putc((char)('0' + dev->func));
        }

        console_write("\n    mtu ");
        console_u32(dev->mtu);
        console_write("  mac ");
        console_write(hwaddr);
        console_write("  irq ");
        console_u32(dev->irq_line);

        console_write("\n    flags ");
        if (dev->flags & NETDEV_FLAG_MMIO_ENABLED) console_write("mmio ");
        if (dev->flags & NETDEV_FLAG_BUSMASTER_ENABLED) console_write("dma ");
        if (dev->flags & NETDEV_FLAG_FW_MAIN) console_write("fw-main ");
        if (dev->flags & NETDEV_FLAG_FW_BOARD) console_write("fw-board ");
        if (dev->flags & NETDEV_FLAG_MSI_CAP) console_write("msi-cap ");
        if (dev->flags & NETDEV_FLAG_MSIX_CAP) console_write("msix-cap ");
        if (dev->flags & NETDEV_FLAG_PCIE_CAP) console_write("pcie ");
        if (dev->flags & NETDEV_FLAG_INTX) console_write("intx ");
    }
}
