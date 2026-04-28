#ifndef SAGEOS_NET_H
#define SAGEOS_NET_H

#include <stddef.h>
#include <stdint.h>

#define NET_MAX_DEVICES    4
#define NET_NAME_LEN      16
#define NET_DRIVER_LEN    16
#define NET_HWADDR_LEN     6
#define NET_MTU_ETHERNET 1500

#define NET_ETHERTYPE_IPV4 0x0800
#define NET_ETHERTYPE_ARP  0x0806

#define NET_ARP_HTYPE_ETHERNET 1
#define NET_ARP_OPER_REQUEST   1

#define NET_IPPROTO_ICMP   1
#define NET_IPPROTO_UDP    17

#define NET_UDP_PORT_DHCP_SERVER 67
#define NET_UDP_PORT_DHCP_CLIENT 68

#define NET_DHCP_OP_BOOTREQUEST 1
#define NET_DHCP_MSG_DISCOVER   1
#define NET_DHCP_MAGIC_COOKIE   0x63825363u

typedef enum {
    NET_DEVICE_LOOPBACK = 0,
    NET_DEVICE_ETHERNET = 1,
    NET_DEVICE_WIFI     = 2
} NetDeviceKind;

typedef enum {
    NET_STATE_DOWN = 0,
    NET_STATE_PROBED,
    NET_STATE_FIRMWARE_STAGED,
    NET_STATE_READY
} NetDeviceState;

#define NETDEV_FLAG_PRESENT            (1u << 0)
#define NETDEV_FLAG_MMIO_ENABLED       (1u << 1)
#define NETDEV_FLAG_BUSMASTER_ENABLED  (1u << 2)
#define NETDEV_FLAG_FW_MAIN            (1u << 3)
#define NETDEV_FLAG_FW_BOARD           (1u << 4)
#define NETDEV_FLAG_INTX               (1u << 5)
#define NETDEV_FLAG_MSI_CAP            (1u << 6)
#define NETDEV_FLAG_MSIX_CAP           (1u << 7)
#define NETDEV_FLAG_PCIE_CAP           (1u << 8)

typedef struct {
    char            name[NET_NAME_LEN];
    char            driver[NET_DRIVER_LEN];
    NetDeviceKind   kind;
    NetDeviceState  state;
    uint32_t        mtu;
    uint32_t        flags;
    uint8_t         hwaddr[NET_HWADDR_LEN];
    uint8_t         hwaddr_valid;
    uint8_t         pci_backed;
    uint8_t         bus;
    uint8_t         device;
    uint8_t         func;
    uint8_t         irq_line;
    uint8_t         irq_pin;
} NetDevice;

typedef struct __attribute__((packed)) {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;
} NetEtherHeader;

typedef struct __attribute__((packed)) {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[6];
    uint8_t  spa[4];
    uint8_t  tha[6];
    uint8_t  tpa[4];
} NetArpHeader;

typedef struct __attribute__((packed)) {
    uint8_t  version_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t header_checksum;
    uint8_t  src[4];
    uint8_t  dst[4];
} NetIpv4Header;

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} NetUdpHeader;

typedef struct __attribute__((packed)) {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic_cookie;
} NetDhcpHeader;

void net_init(void);
int net_is_initialized(void);
int net_register_device(const NetDevice *device);
int net_device_count(void);
const NetDevice *net_get_device(int index);
uint16_t net_ipv4_checksum(const void *data, size_t len);
size_t net_build_arp_request(void *frame,
                             size_t frame_size,
                             const uint8_t *src_mac,
                             const uint8_t *src_ip,
                             const uint8_t *target_ip);
size_t net_build_ipv4_udp_frame(void *frame,
                                size_t frame_size,
                                const uint8_t *dst_mac,
                                const uint8_t *src_mac,
                                const uint8_t *src_ip,
                                const uint8_t *dst_ip,
                                uint16_t src_port,
                                uint16_t dst_port,
                                const void *payload,
                                size_t payload_size);
size_t net_build_dhcp_discover(void *frame,
                               size_t frame_size,
                               const uint8_t *src_mac,
                               uint32_t transaction_id,
                               const char *hostname);
void net_format_hwaddr(const uint8_t *addr, int valid, char *out, size_t out_size);
void net_cmd_info(void);
void net_cmd_selftest(void);

#endif /* SAGEOS_NET_H */
