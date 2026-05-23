#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

#define NO_SYS 1

/* Standard library overrides */
#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_CTYPE_H 1
#define LWIP_PROVIDE_ERRNO      1

#define LWIP_LIBC_EXCLUDES_STRING_H 0

/* Memory configuration */
#define MEM_ALIGNMENT           8
#define MEM_SIZE                (2048 * 1024)
#define MEMP_NUM_PBUF           512
#define MEMP_NUM_UDP_PCB        16
#define MEMP_NUM_TCP_PCB        16
#define MEMP_NUM_TCP_PCB_LISTEN 8
#define MEMP_NUM_TCP_SEG        255
#define PBUF_POOL_SIZE          512

/* TCP configuration */
#define TCP_MSS                 1460
#define TCP_WND                 (32 * 1024)
#define TCP_SND_BUF             (16 * 1024)

/* Protocols */
#define LWIP_ARP                1
#define LWIP_ETHERNET           1
#define LWIP_IPV4               1
#define LWIP_ICMP               1
#define LWIP_UDP                1
#define LWIP_TCP                1
#define LWIP_DHCP               1
#define LWIP_DNS                1

#define LWIP_IPV6               0

/* Disable APIs requiring an OS */
#define LWIP_RAW                0
#define LWIP_NETCONN            0
#define LWIP_SOCKET             0

#define LWIP_STATS              0

/* ALTCP and TLS */
#define LWIP_ALTCP              1
#define LWIP_ALTCP_TLS          1
#define LWIP_ALTCP_TLS_MBEDTLS  1

/* Timers in NO_SYS */
#define LWIP_TIMERS             1

/* Debugging */
#define LWIP_DEBUG              1
#define ALTCP_MBEDTLS_DEBUG     LWIP_DBG_ON
#define ALTCP_MBEDTLS_LIB_DEBUG LWIP_DBG_ON
#define HTTPC_DEBUG             LWIP_DBG_ON
#define DNS_DEBUG               LWIP_DBG_ON
#define TCP_DEBUG               LWIP_DBG_ON

#endif
