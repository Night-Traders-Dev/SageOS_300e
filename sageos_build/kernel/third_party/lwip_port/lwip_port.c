#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#include "timer.h"
#include "net.h"
#include "e1000.h"
#include "dmesg.h"

uint32_t sys_now(void) {
    return (uint32_t)timer_ticks();
}

sys_prot_t sys_arch_protect(void) {
    // In NO_SYS with no threads, this is a no-op
    return 0;
}

void sys_arch_unprotect(sys_prot_t pval) {
    (void)pval;
}

static struct netif g_netif;

static err_t sage_netif_output(struct netif *netif, struct pbuf *p) {
    (void)netif;
    static uint8_t buf[2048];
    if (p->tot_len > sizeof(buf)) return ERR_BUF;
    pbuf_copy_partial(p, buf, p->tot_len, 0);
    if (e1000_send_packet(buf, p->tot_len) == 0) return ERR_OK;
    return ERR_IF;
}

static err_t sage_netif_init(struct netif *netif) {
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->output = etharp_output;
    netif->linkoutput = sage_netif_output;
    netif->hwaddr_len = 6;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    const NetDevice *dev = net_get_device(0); // Assumes e1000 is 0
    if (dev) {
        for (int i = 0; i < 6; i++) netif->hwaddr[i] = dev->hwaddr[i];
    }
    return ERR_OK;
}

void lwip_port_input(const void *data, size_t len) {
    struct pbuf *p = pbuf_alloc(PBUF_RAW, (uint16_t)len, PBUF_POOL);
    if (p) {
        pbuf_take(p, data, (uint16_t)len);
        if (g_netif.input(p, &g_netif) != ERR_OK) {
            pbuf_free(p);
        }
    }
}

void lwip_port_poll(void) {
    e1000_poll();
    sys_check_timeouts();

    struct dhcp *dhcp = netif_dhcp_data(&g_netif);
    if (dhcp && dhcp->state == 10) { // 10 is DHCP_STATE_BOUND in lwip
        // Update net subsystem with the real IP
        const ip4_addr_t *ip = netif_ip4_addr(&g_netif);
        const ip4_addr_t *mask = netif_ip4_netmask(&g_netif);
        const ip4_addr_t *gw = netif_ip4_gw(&g_netif);
        
        uint8_t ip_b[4], mask_b[4], gw_b[4];
        ip_b[0] = ip4_addr1(ip); ip_b[1] = ip4_addr2(ip); ip_b[2] = ip4_addr3(ip); ip_b[3] = ip4_addr4(ip);
        mask_b[0] = ip4_addr1(mask); mask_b[1] = ip4_addr2(mask); mask_b[2] = ip4_addr3(mask); mask_b[3] = ip4_addr4(mask);
        gw_b[0] = ip4_addr1(gw); gw_b[1] = ip4_addr2(gw); gw_b[2] = ip4_addr3(gw); gw_b[3] = ip4_addr4(gw);

        net_update_device_ip(0, ip_b, mask_b, gw_b);
    }
}

void lwip_port_init(void) {
    lwip_init();

    ip4_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr, 0,0,0,0);
    IP4_ADDR(&netmask, 0,0,0,0);
    IP4_ADDR(&gw, 0,0,0,0);

    netif_add(&g_netif, &ipaddr, &netmask, &gw, NULL, sage_netif_init, ethernet_input);
    netif_set_default(&g_netif);
    netif_set_up(&g_netif);

    dhcp_start(&g_netif);
    dmesg_log("lwIP: initialized and DHCP started");
}
