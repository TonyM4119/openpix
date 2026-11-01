// Network bring-up and housekeeping. Never blocks forever: if the Ethernet
// block does not come up, it keeps retrying in the background.
#include "board.h"
#include "net.h"
#include "eth.h"
#include "config.h"
#include "proto.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
#include "lwip/dhcp.h"
#include "lwip/igmp.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "netif/ethernet.h"
#include <stdio.h>

struct netif g_netif;
static uint32_t retry_at, apply_at;
static int groups_dirty = 1, started;
static char ipstr[20];

uint32_t sys_now(void) { return millis(); }
static uint32_t rnd = 0x1234567;
uint32_t lwip_rand(void) { rnd = rnd * 1103515245u + 12345u + cyc(); return rnd; }
void NVIC_SystemReset_wrapper(void) { NVIC_SystemReset(); }

const char *net_ip_str(void) {
  const ip4_addr_t *a = netif_ip4_addr(&g_netif);
  snprintf(ipstr, sizeof(ipstr), "%s", ip4addr_ntoa(a));
  return ipstr;
}

int net_has_ip(void) { return !ip4_addr_isany_val(*netif_ip4_addr(&g_netif)); }
int net_eth_up(void) { return eth_hw_up(); }

static void set_addrs(void) {
  ip4_addr_t ip, mask, gw;
  IP4_ADDR(&ip, cfg.ip[0], cfg.ip[1], cfg.ip[2], cfg.ip[3]);
  IP4_ADDR(&mask, cfg.mask[0], cfg.mask[1], cfg.mask[2], cfg.mask[3]);
  IP4_ADDR(&gw, cfg.gw[0], cfg.gw[1], cfg.gw[2], cfg.gw[3]);
  if (cfg.dhcp) {
    netif_set_addr(&g_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
    dhcp_start(&g_netif);
  } else {
    dhcp_stop(&g_netif);
    netif_set_addr(&g_netif, &ip, &mask, &gw);
    ip_addr_t d1, d2;
    ip_addr_copy_from_ip4(d1, gw);
    IP_ADDR4(&d2, 1, 1, 1, 1);
    dns_setserver(0, &d1);
    dns_setserver(1, &d2);
  }
  groups_dirty = 1;
}

void net_apply_later(uint32_t ms) { apply_at = millis() + ms; if (!apply_at) apply_at = 1; }
void net_groups_changed(void) { groups_dirty = 1; }

static void try_start_eth(void) {
  uint8_t mac[6];
  device_mac(mac);
  memcpy(g_netif.hwaddr, mac, 6);
  if (eth_hw_init(mac)) {
    netif_set_link_up(&g_netif);
    if (!started) { set_addrs(); started = 1; }
    else igmp_report_groups(&g_netif);
  } else {
    retry_at = millis() + 2000;
  }
}

void net_init(void) {
  lwip_init();
  ip4_addr_t z = {0};
  netif_add(&g_netif, &z, &z, &z, 0, eth_netif_init, ethernet_input);
  netif_set_hostname(&g_netif, "openpix");
  netif_set_default(&g_netif);
  netif_set_up(&g_netif);
  proto_init();
  try_start_eth();
}

void net_task(void) {
  if (!eth_hw_up()) {
    if ((int32_t)(millis() - retry_at) >= 0) try_start_eth();
    return;
  }
  eth_poll(&g_netif);
  sys_check_timeouts();
  if (apply_at && (int32_t)(millis() - apply_at) >= 0) { apply_at = 0; set_addrs(); }
  if (groups_dirty && net_has_ip()) { groups_dirty = 0; proto_join_groups(&g_netif); }
}
