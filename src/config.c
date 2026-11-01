// Settings stored in flash sector 11 as an append-only log of fixed slots,
// so a save is a quick program (no erase) most of the time and nothing reboots.
#include "board.h"
#include "config.h"
#include "flash.h"
#include <string.h>

#define CFG_MAGIC   0x4F50434Bu  // "OPCK"
#define CFG_VERSION 1u
#define SLOT        1024u
#define NSLOTS      (CFG_SIZE / SLOT)

_Static_assert(sizeof(config_t) <= SLOT, "config too big");

config_t cfg;
static int cur_slot = -1;

uint32_t crc32(const void *data, uint32_t len, uint32_t crc) {
  const uint8_t *p = data;
  crc = ~crc;
  while (len--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
  }
  return ~crc;
}

void config_defaults(config_t *c) {
  memset(c, 0, sizeof(*c));
  c->magic = CFG_MAGIC;
  c->version = CFG_VERSION;
  strcpy(c->name, "AlphaPix OpenPix");
  c->dhcp = 0;
  uint8_t ip[4] = {10, 0, 0, 80}, mask[4] = {255, 255, 255, 0}, gw[4] = {10, 0, 0, 1};
  memcpy(c->ip, ip, 4); memcpy(c->mask, mask, 4); memcpy(c->gw, gw, 4);
  c->univ_size = 510;
  c->hold_last = 1;
  c->fx_speed = 5;
  c->fx_bright = 100;
  c->test_r = 255; c->test_g = 0; c->test_b = 255;
  for (int i = 0; i < NUM_OUT; i++) {
    c->out_map[i] = i;
    out_cfg_t *o = &c->out[i];
    o->universe = 1 + i * 2;   // 340 px = 2 universes each, no overlap
    o->channel = 1;
    o->pixels = i < 16 ? 340 : 0;
    o->bright = 100;
    o->order = ORD_GRB;
  }
}

static int slot_valid(int s) {
  const config_t *c = (const config_t *)(CFG_BASE + s * SLOT);
  if (c->magic != CFG_MAGIC || c->version != CFG_VERSION) return 0;
  return crc32(c, offsetof(config_t, crc), 0) == c->crc;
}

void config_load(void) {
  cur_slot = -1;
  for (int s = 0; s < (int)NSLOTS; s++) {
    if (*(const uint32_t *)(CFG_BASE + s * SLOT) == 0xFFFFFFFFu) break;
    if (slot_valid(s)) cur_slot = s;
  }
  if (cur_slot >= 0) memcpy(&cfg, (const void *)(CFG_BASE + cur_slot * SLOT), sizeof(cfg));
  else config_defaults(&cfg);
  // sanitize
  if (cfg.univ_size < 3 || cfg.univ_size > 512) cfg.univ_size = 510;
  for (int i = 0; i < NUM_OUT; i++) {
    if (cfg.out[i].pixels > MAX_PIX) cfg.out[i].pixels = MAX_PIX;
    if (cfg.out[i].bright > 100) cfg.out[i].bright = 100;
    if (cfg.out_map[i] >= NUM_OUT) cfg.out_map[i] = i;
    if (cfg.out[i].order > ORD_BGR) cfg.out[i].order = ORD_GRB;
  }
  if (cfg.fx >= FX_COUNT) cfg.fx = FX_OFF;
  if (!cfg.fx_speed || cfg.fx_speed > 10) cfg.fx_speed = 5;
  if (!cfg.fx_bright || cfg.fx_bright > 100) cfg.fx_bright = 100;
  cfg.name[sizeof(cfg.name) - 1] = 0;
}

int config_save(void) {
  cfg.magic = CFG_MAGIC;
  cfg.version = CFG_VERSION;
  cfg.crc = crc32(&cfg, offsetof(config_t, crc), 0);
  int next = cur_slot + 1;
  // Find the next blank slot (skip any partially written ones)
  while (next < (int)NSLOTS) {
    const uint32_t *p = (const uint32_t *)(CFG_BASE + next * SLOT);
    int blank = 1;
    for (uint32_t k = 0; k < SLOT / 4; k++) if (p[k] != 0xFFFFFFFFu) { blank = 0; break; }
    if (blank) break;
    next++;
  }
  if (next >= (int)NSLOTS) {
    if (!flash_erase_sector(11)) return 0;
    next = 0;
  }
  if (!flash_program(CFG_BASE + next * SLOT, &cfg, sizeof(cfg))) return 0;
  if (!slot_valid(next)) return 0;
  cur_slot = next;
  return 1;
}
