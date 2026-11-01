// sACN (E1.31), Art-Net and DDP receivers. All three are always on.
#include "board.h"
#include "proto.h"
#include "pixels.h"
#include "lwip/udp.h"
#include "lwip/igmp.h"
#include "lwip/netif.h"
#include <string.h>

static struct udp_pcb *pcb_sacn, *pcb_art, *pcb_ddp;
uint32_t proto_pkts_sacn, proto_pkts_art, proto_pkts_ddp;
static uint8_t pkt[1500];

static void put_universe(uint16_t u, const uint8_t *data, uint16_t len) {
  if (u < 1 || u > MAX_UNIV) return;
  uint32_t base = (uint32_t)(u - 1) * cfg.univ_size;
  if (len > cfg.univ_size) len = cfg.univ_size;
  if (base + len > FLAT_SIZE) return;
  memcpy(&flat[base], data, len);
  pixels_universe_rx(u);
}

static void rx_sacn(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *a, u16_t port) {
  (void)arg; (void)pcb; (void)a; (void)port;
  uint16_t n = pbuf_copy_partial(p, pkt, sizeof(pkt), 0);
  pbuf_free(p);
  if (n < 126) return;
  static const uint8_t id[12] = {'A','S','C','-','E','1','.','1','7',0,0,0};
  if (memcmp(&pkt[4], id, 12) != 0) return;
  if (pkt[18] || pkt[19] || pkt[20] || pkt[21] != 4) return;            // root: E1.31 data
  if (pkt[40] || pkt[41] || pkt[42] || pkt[43] != 2) return;            // framing: data packet
  if (pkt[112] & 0x40) return;                                          // stream terminated
  if (pkt[117] != 2 || pkt[125] != 0) return;                           // DMP set property, start code 0
  uint16_t u = ((uint16_t)pkt[113] << 8) | pkt[114];
  uint16_t cnt = ((uint16_t)pkt[123] << 8) | pkt[124];
  if (cnt < 1) return;
  cnt -= 1;
  if (126 + cnt > n) cnt = n - 126;
  proto_pkts_sacn++;
  put_universe(u, &pkt[126], cnt);
}

static void rx_art(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *a, u16_t port) {
  (void)arg; (void)pcb; (void)a; (void)port;
  uint16_t n = pbuf_copy_partial(p, pkt, sizeof(pkt), 0);
  pbuf_free(p);
  if (n < 18 || memcmp(pkt, "Art-Net", 8) != 0) return;
  uint16_t op = pkt[8] | ((uint16_t)pkt[9] << 8);
  if (op != 0x5000) return;                                             // ArtDmx only
  uint16_t u = pkt[14] | ((uint16_t)(pkt[15] & 0x7F) << 8);
  uint16_t len = ((uint16_t)pkt[16] << 8) | pkt[17];
  if (18 + len > n) len = n - 18;
  proto_pkts_art++;
  put_universe(u, &pkt[18], len);   // Art-Net universe N -> universe N
}

static void rx_ddp(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *a, u16_t port) {
  (void)arg; (void)pcb; (void)a; (void)port;
  uint16_t n = pbuf_copy_partial(p, pkt, sizeof(pkt), 0);
  pbuf_free(p);
  if (n < 10 || (pkt[0] & 0xC0) != 0x40) return;
  int hdr = (pkt[0] & 0x10) ? 14 : 10;
  if (pkt[3] != 1 && pkt[3] != 0) return;                               // display data
  uint32_t off = ((uint32_t)pkt[4] << 24) | ((uint32_t)pkt[5] << 16) | ((uint32_t)pkt[6] << 8) | pkt[7];
  uint16_t len = ((uint16_t)pkt[8] << 8) | pkt[9];
  if (hdr + len > n) len = n - hdr;
  if (off >= FLAT_SIZE) return;
  if (off + len > FLAT_SIZE) len = FLAT_SIZE - off;
  memcpy(&flat[off], &pkt[hdr], len);
  proto_pkts_ddp++;
  // mark the touched "universes" so frame completion logic works
  for (uint32_t u = off / cfg.univ_size; u <= (off + len - 1) / cfg.univ_size && u < MAX_UNIV; u++)
    pixels_universe_rx(u + 1);
  if (pkt[0] & 0x01) pixels_push();
}

void proto_join_groups(struct netif *nif) {
  // Join sACN multicast groups for every universe the outputs use.
  static uint32_t joined[(MAX_UNIV + 31) / 32];
  uint32_t want[(MAX_UNIV + 31) / 32];
  memset(want, 0, sizeof(want));
  for (int i = 0; i < NUM_OUT; i++) {
    out_cfg_t *o = &cfg.out[i];
    if (!o->pixels || !o->universe) continue;
    uint32_t start = (uint32_t)(o->universe - 1) * cfg.univ_size + (o->channel ? o->channel - 1 : 0);
    uint32_t end = start + (uint32_t)o->pixels * 3 - 1;
    for (uint32_t u = start / cfg.univ_size; u <= end / cfg.univ_size && u < MAX_UNIV; u++)
      want[u >> 5] |= 1u << (u & 31);
  }
  for (uint32_t u = 0; u < MAX_UNIV; u++) {
    int w = (want[u >> 5] >> (u & 31)) & 1, j = (joined[u >> 5] >> (u & 31)) & 1;
    if (w == j) continue;
    ip4_addr_t g;
    uint16_t un = u + 1;
    IP4_ADDR(&g, 239, 255, un >> 8, un & 0xFF);
    if (w) { igmp_joingroup_netif(nif, &g); joined[u >> 5] |= 1u << (u & 31); }
    else   { igmp_leavegroup_netif(nif, &g); joined[u >> 5] &= ~(1u << (u & 31)); }
  }
}

void proto_init(void) {
  pcb_sacn = udp_new(); udp_bind(pcb_sacn, IP_ADDR_ANY, 5568); udp_recv(pcb_sacn, rx_sacn, 0);
  pcb_art  = udp_new(); udp_bind(pcb_art,  IP_ADDR_ANY, 6454); udp_recv(pcb_art,  rx_art,  0);
  pcb_ddp  = udp_new(); udp_bind(pcb_ddp,  IP_ADDR_ANY, 4048); udp_recv(pcb_ddp,  rx_ddp,  0);
}
