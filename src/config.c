// Settings stored in flash sector 11 as an append-only log of fixed slots,
// so a save is a quick program (no erase) most of the time and nothing reboots.
#include "board.h"
#include "config.h"
#include "flash.h"
#include <string.h>

#define CFG_MAGIC   0x4F50434Bu  // "OPCK"
#define CFG_VERSION 2u
#define V1_LEN offsetof(config_t, power_limit_a)   // where version 1 stored its CRC
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

static void config_v2_defaults(config_t *c) {
  c->power_limit_a = 50;      // the stock power supply is 60 A; keep some headroom
  c->ma_per_ch = 20;
  c->fan_on_c = 38;           // 100 F
  c->shutdown_c = 60;         // stock default 140F / 60C
  c->tz_min = -300;           // US Eastern
  c->dst_us = 1;
  c->lat_e4 = 409000; c->lon_e4 = -778000;   // middle of Pennsylvania; pick your state in the UI
  c->on_sunset = 1; c->on_offset = 0; c->on_min = 17 * 60; c->off_min = 23 * 60;
  c->sched_days = 0x7F;
  c->pl_count = 3;
  c->pl_fx[0] = FX_CHRISTMAS; c->pl_fx[1] = FX_CANDYCANE; c->pl_fx[2] = FX_TWINKLE;
  c->pl_minutes[0] = c->pl_minutes[1] = c->pl_minutes[2] = 10;
  c->cust_mode = CUST_ALTERNATE; c->cust_n = 2;
  uint8_t cc[4][3] = {{255, 0, 0}, {0, 0, 255}, {255, 255, 255}, {0, 255, 0}};
  memcpy(c->cust_rgb, cc, sizeof(cc));
  c->pix_type = PIX_WS281X;
  c->dmx_univ[0] = 1; c->dmx_univ[1] = 2;
  c->spi_khz = 500;
  c->power_off = 0;
  c->solid_rgb[0] = 255; c->solid_rgb[1] = 160; c->solid_rgb[2] = 80;
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
  config_v2_defaults(c);
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

// 0 = invalid, 1 = version-1 record, 2 = current
static int slot_valid(int s) {
  const config_t *c = (const config_t *)(CFG_BASE + s * SLOT);
  if (c->magic != CFG_MAGIC) return 0;
  if (c->version == CFG_VERSION) return crc32(c, offsetof(config_t, crc), 0) == c->crc ? 2 : 0;
  if (c->version == 1) {
    uint32_t stored = *(const uint32_t *)((const uint8_t *)c + V1_LEN);
    return crc32(c, V1_LEN, 0) == stored ? 1 : 0;
  }
  return 0;
}

void config_load(void) {
  cur_slot = -1;
  int kind = 0;
  for (int s = 0; s < (int)NSLOTS; s++) {
    if (*(const uint32_t *)(CFG_BASE + s * SLOT) == 0xFFFFFFFFu) break;
    int k = slot_valid(s);
    if (k) { cur_slot = s; kind = k; }
  }
  if (kind == 2) memcpy(&cfg, (const void *)(CFG_BASE + cur_slot * SLOT), sizeof(cfg));
  else if (kind == 1) {   // upgrade from 0.1-0.3: keep everything, add new defaults
    config_defaults(&cfg);
    memcpy(&cfg, (const void *)(CFG_BASE + cur_slot * SLOT), V1_LEN);
    for (int i = 0; i < NUM_OUT; i++) { cfg.out[i].zone_fx = 0; cfg.out[i].pad[0] = cfg.out[i].pad[1] = 0; }
    config_v2_defaults(&cfg);
    cfg.version = CFG_VERSION;
  } else config_defaults(&cfg);
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
  if (!cfg.ma_per_ch || cfg.ma_per_ch > 60) cfg.ma_per_ch = 20;
  if (cfg.fan_on_c == 37) cfg.fan_on_c = 38;   // old default showed as 99 F; 38 C = 100 F
  if (cfg.pl_count > MAX_PLAYLIST) cfg.pl_count = MAX_PLAYLIST;
  if (!cfg.cust_n || cfg.cust_n > 4) cfg.cust_n = 2;
  if (cfg.cust_mode >= CUST_COUNT) cfg.cust_mode = CUST_ALTERNATE;
  if (cfg.power_off > 1) cfg.power_off = 0;
  if (cfg.pix_type >= PIX_COUNT) cfg.pix_type = PIX_WS281X;
  if (cfg.spi_khz < 50 || cfg.spi_khz > 2000) cfg.spi_khz = 500;
  if (cfg.on_min > 1439) cfg.on_min = 17 * 60;
  if (cfg.off_min > 1439) cfg.off_min = 23 * 60;
  for (int i = 0; i < NUM_OUT; i++)
    if (cfg.out[i].zone_fx != ZONE_DARK && cfg.out[i].zone_fx >= FX_COUNT) cfg.out[i].zone_fx = 0;
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
