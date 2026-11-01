// Receives WLED "Audio Sync" (V2) packets so the music effects can react to
// sound picked up by another device (a WLED sound-reactive board, or a PC/phone
// audio sender). Multicast 239.0.0.1, UDP port 11988.
#include "board.h"
#include "audio.h"
#include "looks.h"
#include "lwip/udp.h"
#include "lwip/igmp.h"
#include "lwip/netif.h"
#include <string.h>

static struct udp_pcb *pcb;
static uint8_t level, bands[16];
static uint32_t last_ms;
static int joined;
extern struct netif g_netif;

static void rx(void *arg, struct udp_pcb *p_, struct pbuf *p, const ip_addr_t *a, u16_t port) {
  (void)arg; (void)p_; (void)a; (void)port;
  uint8_t b[48];
  uint16_t n = pbuf_copy_partial(p, b, sizeof(b), 0);
  pbuf_free(p);
  if (!ex.audio_on || n < 44 || memcmp(b, "00002", 5) != 0) return;
  float smth;
  memcpy(&smth, &b[12], 4);
  if (!(smth >= 0)) smth = 0;          // also catches NaN
  if (smth > 255) smth = 255;
  level = (uint8_t)smth;
  memcpy(bands, &b[18], 16);
  last_ms = millis();
}

void audio_init(void) {
  pcb = udp_new();
  udp_bind(pcb, IP_ADDR_ANY, 11988);
  udp_recv(pcb, rx, 0);
}

static void join_check(void) {
  if (ex.audio_on == joined || ip4_addr_isany_val(*netif_ip4_addr(&g_netif))) return;
  ip4_addr_t g; IP4_ADDR(&g, 239, 0, 0, 1);
  if (ex.audio_on) igmp_joingroup_netif(&g_netif, &g); else igmp_leavegroup_netif(&g_netif, &g);
  joined = ex.audio_on;
}

int audio_active(void) { join_check(); return last_ms && (uint32_t)(millis() - last_ms) < 1000; }
uint8_t audio_level(void) { return audio_active() ? level : 0; }
uint8_t audio_band(uint32_t i) { return audio_active() && i < 16 ? bands[i] : 0; }
