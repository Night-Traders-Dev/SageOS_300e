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

static uint16_t net_cpu_to_be16(uint16_t value) {
    return (uint16_t)((value << 8) | (value >> 8));
}

static uint32_t net_cpu_to_be32(uint32_t value) {
    return ((value & 0x000000FFu) << 24) |
           ((value & 0x0000FF00u) << 8) |
           ((value & 0x00FF0000u) >> 8) |
           ((value & 0xFF000000u) >> 24);
}

static uint16_t net_be16_to_cpu(uint16_t value) {
    return net_cpu_to_be16(value);
}

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

static void net_print_hex16(uint16_t value) {
    static const char hex[] = "0123456789abcdef";
    char text[5];
    text[0] = hex[(value >> 12) & 0xF];
    text[1] = hex[(value >> 8) & 0xF];
    text[2] = hex[(value >> 4) & 0xF];
    text[3] = hex[value & 0xF];
    text[4] = 0;
    console_write(text);
}

static int net_has_required_bytes(const void *a, const void *b, const void *c) {
    return a != NULL && b != NULL && c != NULL;
}

static void net_print_yes_no(int value) {
    console_write(value ? "ready" : "pending");
}

static uint32_t net_checksum_accumulate(uint32_t sum, const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;

    while (len > 1) {
        sum += ((uint32_t)bytes[0] << 8) | bytes[1];
        bytes += 2;
        len -= 2;
    }
    if (len) sum += (uint32_t)bytes[0] << 8;
    return sum;
}

static uint16_t net_checksum_finish(uint32_t sum) {
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)(~sum);
}

static uint16_t net_udp_checksum_ipv4(const NetIpv4Header *ip,
                                      const NetUdpHeader *udp,
                                      const void *payload,
                                      size_t payload_size) {
    uint32_t sum = 0;
    uint16_t udp_len;
    uint16_t checksum;
    uint8_t pseudo[12];

    if (!ip || !udp) return 0;

    memcpy(&pseudo[0], ip->src, 4);
    memcpy(&pseudo[4], ip->dst, 4);
    pseudo[8] = 0;
    pseudo[9] = ip->protocol;
    memcpy(&pseudo[10], &udp->length, 2);

    udp_len = net_cpu_to_be16(udp->length);
    sum = net_checksum_accumulate(sum, pseudo, sizeof(pseudo));
    sum = net_checksum_accumulate(sum, udp, sizeof(*udp));
    if (payload && payload_size) {
        if (payload_size > udp_len - sizeof(*udp)) payload_size = udp_len - sizeof(*udp);
        sum = net_checksum_accumulate(sum, payload, payload_size);
    }

    checksum = net_checksum_finish(sum);
    return checksum == 0 ? 0xFFFFu : checksum;
}

void net_init(void) {
    g_net_device_count = 0;
    g_net_initialized = 1;
    memset(g_net_devices, 0, sizeof(g_net_devices));
    memset(&g_net_stack, 0, sizeof(g_net_stack));

    g_net_stack.l2_registry_ready = 1;
    g_net_stack.frame_helpers_ready = 1;
    g_net_stack.arp_ready = 1;
    g_net_stack.ipv4_ready = 1;
    g_net_stack.udp_ready = 1;
    g_net_stack.dhcp_ready = 1;

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
    return net_checksum_finish(net_checksum_accumulate(0, data, len));
}

size_t net_build_arp_request(void *frame,
                             size_t frame_size,
                             const uint8_t *src_mac,
                             const uint8_t *src_ip,
                             const uint8_t *target_ip) {
    static const uint8_t broadcast[NET_HWADDR_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    NetEtherHeader *eth;
    NetArpHeader *arp;

    if (!net_has_required_bytes(src_mac, src_ip, target_ip)) return 0;
    if (!frame || frame_size < sizeof(NetEtherHeader) + sizeof(NetArpHeader)) return 0;

    eth = (NetEtherHeader *)frame;
    arp = (NetArpHeader *)((uint8_t *)frame + sizeof(NetEtherHeader));

    memcpy(eth->dst, broadcast, NET_HWADDR_LEN);
    memcpy(eth->src, src_mac, NET_HWADDR_LEN);
    eth->ethertype = net_cpu_to_be16(NET_ETHERTYPE_ARP);

    arp->htype = net_cpu_to_be16(NET_ARP_HTYPE_ETHERNET);
    arp->ptype = net_cpu_to_be16(NET_ETHERTYPE_IPV4);
    arp->hlen = NET_HWADDR_LEN;
    arp->plen = 4;
    arp->oper = net_cpu_to_be16(NET_ARP_OPER_REQUEST);
    memcpy(arp->sha, src_mac, NET_HWADDR_LEN);
    memcpy(arp->spa, src_ip, 4);
    memset(arp->tha, 0, NET_HWADDR_LEN);
    memcpy(arp->tpa, target_ip, 4);

    return sizeof(NetEtherHeader) + sizeof(NetArpHeader);
}

size_t net_build_ipv4_udp_frame(void *frame,
                                size_t frame_size,
                                const uint8_t *dst_mac,
                                const uint8_t *src_mac,
                                const uint8_t *src_ip,
                                const uint8_t *dst_ip,
                                uint16_t src_port,
                                uint16_t dst_port,
                                const void *payload,
                                size_t payload_size) {
    NetEtherHeader *eth;
    NetIpv4Header *ip;
    NetUdpHeader *udp;
    size_t total_size = sizeof(NetEtherHeader) + sizeof(NetIpv4Header) +
                        sizeof(NetUdpHeader) + payload_size;
    uint16_t ip_total = (uint16_t)(sizeof(NetIpv4Header) + sizeof(NetUdpHeader) + payload_size);
    uint16_t udp_total = (uint16_t)(sizeof(NetUdpHeader) + payload_size);

    if (!net_has_required_bytes(dst_mac, src_mac, src_ip) || !dst_ip) return 0;
    if (!frame || frame_size < total_size) return 0;

    eth = (NetEtherHeader *)frame;
    ip = (NetIpv4Header *)((uint8_t *)frame + sizeof(NetEtherHeader));
    udp = (NetUdpHeader *)((uint8_t *)ip + sizeof(NetIpv4Header));

    memcpy(eth->dst, dst_mac, NET_HWADDR_LEN);
    memcpy(eth->src, src_mac, NET_HWADDR_LEN);
    eth->ethertype = net_cpu_to_be16(NET_ETHERTYPE_IPV4);

    memset(ip, 0, sizeof(*ip));
    ip->version_ihl = 0x45;
    ip->total_length = net_cpu_to_be16(ip_total);
    ip->flags_fragment = net_cpu_to_be16(0x4000);
    ip->ttl = 64;
    ip->protocol = NET_IPPROTO_UDP;
    memcpy(ip->src, src_ip, 4);
    memcpy(ip->dst, dst_ip, 4);
    ip->header_checksum = net_cpu_to_be16(net_ipv4_checksum(ip, sizeof(*ip)));

    udp->src_port = net_cpu_to_be16(src_port);
    udp->dst_port = net_cpu_to_be16(dst_port);
    udp->length = net_cpu_to_be16(udp_total);
    udp->checksum = 0;

    if (payload_size && payload) {
        memcpy((uint8_t *)udp + sizeof(*udp), payload, payload_size);
    }

    udp->checksum = net_cpu_to_be16(net_udp_checksum_ipv4(ip, udp,
                                                          (uint8_t *)udp + sizeof(*udp),
                                                          payload_size));

    return total_size;
}

size_t net_build_dhcp_discover(void *frame,
                               size_t frame_size,
                               const uint8_t *src_mac,
                               uint32_t transaction_id,
                               const char *hostname) {
    static const uint8_t dst_mac[NET_HWADDR_LEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    static const uint8_t src_ip[4] = { 0, 0, 0, 0 };
    static const uint8_t dst_ip[4] = { 255, 255, 255, 255 };
    uint8_t payload[sizeof(NetDhcpHeader) + 64];
    NetDhcpHeader *dhcp = (NetDhcpHeader *)payload;
    size_t payload_size = sizeof(NetDhcpHeader);
    size_t host_len = 0;
    uint8_t *opt = payload + sizeof(NetDhcpHeader);

    if (!frame || !src_mac) return 0;
    memset(payload, 0, sizeof(payload));

    dhcp->op = NET_DHCP_OP_BOOTREQUEST;
    dhcp->htype = NET_ARP_HTYPE_ETHERNET;
    dhcp->hlen = NET_HWADDR_LEN;
    dhcp->xid = net_cpu_to_be32(transaction_id);
    dhcp->flags = net_cpu_to_be16(0x8000);
    memcpy(dhcp->chaddr, src_mac, NET_HWADDR_LEN);
    dhcp->magic_cookie = net_cpu_to_be32(NET_DHCP_MAGIC_COOKIE);

    *opt++ = 53;
    *opt++ = 1;
    *opt++ = NET_DHCP_MSG_DISCOVER;

    *opt++ = 55;
    *opt++ = 4;
    *opt++ = 1;
    *opt++ = 3;
    *opt++ = 6;
    *opt++ = 51;

    if (hostname) {
        while (hostname[host_len] && host_len < 24) host_len++;
        if (host_len > 0) {
            *opt++ = 12;
            *opt++ = (uint8_t)host_len;
            memcpy(opt, hostname, host_len);
            opt += host_len;
        }
    }

    *opt++ = 255;
    payload_size += (size_t)(opt - (payload + sizeof(NetDhcpHeader)));

    return net_build_ipv4_udp_frame(frame, frame_size,
                                    dst_mac, src_mac, src_ip, dst_ip,
                                    NET_UDP_PORT_DHCP_CLIENT,
                                    NET_UDP_PORT_DHCP_SERVER,
                                    payload, payload_size);
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

void net_cmd_selftest(void) {
    static const uint8_t sample_mac[NET_HWADDR_LEN] = { 0x02, 0x53, 0x41, 0x47, 0x45, 0x30 };
    static const uint8_t sample_ip[4] = { 192, 168, 0, 2 };
    static const uint8_t router_ip[4] = { 192, 168, 0, 1 };
    uint8_t frame[512];
    size_t arp_len;
    size_t dhcp_len;
    NetIpv4Header *ip;
    NetUdpHeader *udp;

    console_write("\nnet selftest:");

    memset(frame, 0, sizeof(frame));
    arp_len = net_build_arp_request(frame, sizeof(frame), sample_mac, sample_ip, router_ip);
    if (arp_len == 0) {
        console_write("\n  ARP request build: failed");
        return;
    }

    console_write("\n  ARP request build: ok  len=");
    console_u32((uint32_t)arp_len);
    console_write("  ethertype=0x");
    net_print_hex16(net_be16_to_cpu(((NetEtherHeader *)frame)->ethertype));

    memset(frame, 0, sizeof(frame));
    dhcp_len = net_build_dhcp_discover(frame, sizeof(frame), sample_mac, 0x53414745u, "sageos");
    if (dhcp_len == 0) {
        console_write("\n  DHCP discover build: failed");
        return;
    }

    ip = (NetIpv4Header *)(frame + sizeof(NetEtherHeader));
    udp = (NetUdpHeader *)((uint8_t *)ip + sizeof(NetIpv4Header));

    console_write("\n  DHCP discover build: ok  len=");
    console_u32((uint32_t)dhcp_len);
    console_write("  ip=0x");
    net_print_hex16(net_be16_to_cpu(ip->header_checksum));
    console_write("  udp=0x");
    net_print_hex16(net_be16_to_cpu(udp->checksum));
}
