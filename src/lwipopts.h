#pragma once
#define NO_SYS                      1
#define SYS_LIGHTWEIGHT_PROT        0
#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0
#define LWIP_STATS                  0
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_IGMP                   1
#define LWIP_DHCP                   1
#define LWIP_DHCP_DOES_ACD_CHECK    0
#define LWIP_ACD                    0
#define LWIP_AUTOIP                 0
#define LWIP_DNS                    1
#define SNTP_SERVER_DNS             1
#define SNTP_MAX_SERVERS            2
#define SNTP_UPDATE_DELAY           (3600 * 1000)
void sched_set_time(unsigned long utc);
#define SNTP_SET_SYSTEM_TIME(sec)   sched_set_time((unsigned long)(sec))
#define LWIP_RAW                    0
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_STATUS_CALLBACK  0
#define LWIP_NETIF_LINK_CALLBACK    0
#define IP_REASSEMBLY               0
#define IP_FRAG                     0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (20 * 1024)
#define MEMP_NUM_PBUF               32
#define MEMP_NUM_UDP_PCB            12
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_PCB_LISTEN     2
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_SYS_TIMEOUT        20
#define LWIP_ALTCP                  0
#define MEMP_NUM_MQTT_CLIENT        1
#define MQTT_OUTPUT_RINGBUF_SIZE    512
#define MQTT_VAR_HEADER_BUFFER_LEN  256
#define PBUF_POOL_SIZE              16
#define PBUF_POOL_BUFSIZE           1536
#define TCP_MSS                     1460
#define TCP_WND                     (4 * TCP_MSS)
#define TCP_SND_BUF                 (4 * TCP_MSS)
#define TCP_SND_QUEUELEN            (4 * TCP_SND_BUF / TCP_MSS)
#define LWIP_TCP_KEEPALIVE          0
#define CHECKSUM_GEN_IP             1
#define CHECKSUM_GEN_UDP            1
#define CHECKSUM_GEN_TCP            1
#define CHECKSUM_GEN_ICMP           1
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1
#define CHECKSUM_CHECK_TCP          1
#define ETH_PAD_SIZE                0
#define LWIP_CHKSUM_ALGORITHM       3
#define LWIP_DEBUG                  0
