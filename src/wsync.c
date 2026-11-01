// WLED "UDP sync" (notifier, port 21324): keep on/off, brightness and the solid
// colour in step with other WLED devices or other OpenPix controllers.
// Effects are not synced (effect numbers differ between WLED and OpenPix).
#include "board.h"
#include "wsync.h"
#include "looks.h"
#include "config.h"
#include "lwip/udp.h"
#include "lwip/netif.h"
#include <string.h>

extern struct netif g_netif;
static struct udp_pcb *pcb;
static uint8_t last_on = 0xFF, last_bri, last_col[3];
static uint32_t ignore_until;

static void rx(void *arg, struct udp_pcb *p_, struct pbuf *p, const ip_addr_t *a, u16_t port) {
  (void)arg; (void)p_; (void)port;
  uint8_t b[48];
  uint16_t n = pbuf_copy_partial(p, b, sizeof(b), 0);
  pbuf_free(p);
  if (!ex.sync_recv || n < 12 || b[0] != 0) return;
  if (ip_addr_cmp(a, netif_ip_addr4(&g_netif))) return;       // our own broadcast
  if (b[11] >= 9 && n > 36 && !(b[36] & 1)) return;            // sync group 1 only
  int bri = b[2];
  if (bri == 0) cfg.power_off = 1;
  else {
    cfg.power_off = 0;
    cfg.fx_bright = bri * 100 / 255; if (!cfg.fx_bright) cfg.fx_bright = 1;
    if (cfg.fx == FX_OFF) cfg.fx = FX_SOLID;
  }
  if (cfg.fx == FX_SOLID) { cfg.solid_rgb[0] = b[3]; cfg.solid_rgb[1] = b[4]; cfg.solid_rgb[2] = b[5]; }
  // remember so we don't echo it straight back
  last_on = !cfg.power_off; last_bri = cfg.fx_bright; memcpy(last_col, cfg.solid_rgb, 3);
  ignore_until = millis() + 1000;
  uint8_t t = cfg.test_mode; cfg.test_mode = 0; config_save(); cfg.test_mode = t;
}

void wsync_init(void) {
  pcb = udp_new();
  ip_set_option(pcb, SOF_BROADCAST);
  udp_bind(pcb, IP_ADDR_ANY, 21324);
  udp_recv(pcb, rx, 0);
}

void wsync_task(void) {
  static uint32_t t;
  if ((uint32_t)(millis() - t) < 200) return;
  t = millis();
  uint8_t on = !cfg.power_off && cfg.fx != FX_OFF;
  if (on == last_on && cfg.fx_bright == last_bri && !memcmp(cfg.solid_rgb, last_col, 3)) return;
  int first = last_on == 0xFF;
  last_on = on; last_bri = cfg.fx_bright; memcpy(last_col, cfg.solid_rgb, 3);
  if (first || !ex.sync_send || (int32_t)(millis() - ignore_until) < 0) return;
  if (ip4_addr_isany_val(*netif_ip4_addr(&g_netif))) return;
  uint8_t b[41];
  memset(b, 0, sizeof(b));
  b[1] = 1;                                         // direct change
  b[2] = on ? (cfg.fx_bright * 255 / 100) : 0;
  b[3] = cfg.solid_rgb[0]; b[4] = cfg.solid_rgb[1]; b[5] = cfg.solid_rgb[2];
  b[9] = cfg.fx_speed * 25;
  b[11] = 12;                                       // packet version
  b[16] = ex.intensity;
  b[36] = 1;                                        // sync group 1
  struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, sizeof(b), PBUF_RAM);
  if (!p) return;
  memcpy(p->payload, b, sizeof(b));
  ip_addr_t bc;
  ip_addr_set_ip4_u32(&bc, netif_ip4_addr(&g_netif)->addr | ~netif_ip4_netmask(&g_netif)->addr);
  udp_sendto(pcb, p, &bc, 21324);
  pbuf_free(p);
}
