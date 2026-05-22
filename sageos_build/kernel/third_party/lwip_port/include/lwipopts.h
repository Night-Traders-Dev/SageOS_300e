#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

#define NO_SYS 1

/* Standard library overrides */
#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_CTYPE_H 1
#define LWIP_ERRNO_STDINCLUDE   0
#define LWIP_ERRNO_INCLUDE      "arch/errno.h"

#define LWIP_LIBC_EXCLUDES_STRING_H 0

/* Memory configuration */
#define MEM_ALIGNMENT           8
#define MEM_SIZE                (256 * 1024)
#define MEMP_NUM_PBUF           128
#define MEMP_NUM_UDP_PCB        16
#define MEMP_NUM_TCP_PCB        16
#define MEMP_NUM_TCP_PCB_LISTEN 8
#define MEMP_NUM_TCP_SEG        255
#define PBUF_POOL_SIZE          128

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

/* Timers in NO_SYS */
#define LWIP_TIMERS             1

#endif
