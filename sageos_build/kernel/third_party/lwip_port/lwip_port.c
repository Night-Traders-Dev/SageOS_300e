#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#include "timer.h"
#include "net.h"
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
    // We need to send this packet out via our net_devices
    // For now, we will map this later
    return ERR_OK;
}

static err_t sage_netif_init(struct netif *netif) {
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->output = etharp_output;
    netif->linkoutput = sage_netif_output;
    netif->hwaddr_len = 6;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    return ERR_OK;
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
