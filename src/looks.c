#include "board.h"
#include "looks.h"
#include "flash.h"
#include "schedule.h"
#include <string.h>

#define EX_MAGIC 0x4F505845u   // "OPXE"
#define EX_VER   1u
#define SLOT     2048u
#define NSLOTS   (EXTRA_SIZE / SLOT)
_Static_assert(sizeof(extras_t) <= SLOT, "extras too big");

extras_t ex;
static int cur = -1;

// gamma 2.2
const uint8_t gamma8[256] = {   // round(255 * (i/255)^2.2)
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,
  3,3,3,3,3,4,4,4,4,5,5,5,5,6,6,6,6,7,7,7,8,8,8,9,9,9,10,10,11,11,11,12,
  12,13,13,13,14,14,15,15,16,16,17,17,18,18,19,19,20,20,21,22,22,23,23,24,25,25,26,26,27,28,28,29,
  30,30,31,32,33,33,34,35,35,36,37,38,39,39,40,41,42,43,43,44,45,46,47,48,49,49,50,51,52,53,54,55,
  56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,73,74,75,76,77,78,79,81,82,83,84,85,87,88,89,90,
  91,93,94,95,97,98,99,100,102,103,105,106,107,109,110,111,113,114,116,117,119,120,121,123,124,126,127,129,130,132,133,135,
  137,138,140,141,143,145,146,148,149,151,153,154,156,158,159,161,163,165,166,168,170,172,173,175,177,179,181,182,184,186,188,190,
  192,194,196,197,199,201,203,205,207,209,211,213,215,217,219,221,223,225,227,229,231,234,236,238,240,242,244,246,248,251,253,255
};
_Static_assert(sizeof(gamma8) == 256, "gamma table");

void looks_defaults(extras_t *e) {
  memset(e, 0, sizeof(*e));
  e->magic = EX_MAGIC; e->version = EX_VER;
  e->intensity = 128;
  e->gamma_fx = 1;
  e->gamma_data = 0;
  e->transition_ds = 15;   // 1.5 s crossfade
  static const cal_t cal[] = {
    {10, 1, 10, 31, FX_HALLOWEEN, {0}},
    {11, 25, 1, 6, FX_CHRISTMAS, {0}},
    {7, 1, 7, 5, FX_JULY4, {0}},
  };
  memcpy(e->cal, cal, sizeof(cal));
  strcpy(e->alexa_name, "Christmas lights");
  e->mqtt_port = 1883;
  strcpy(e->mqtt_topic, "openpix");
}

static int slot_ok(int s) {
  const extras_t *e = (const extras_t *)(EXTRA_BASE + s * SLOT);
  return e->magic == EX_MAGIC && e->version == EX_VER && crc32(e, offsetof(extras_t, crc), 0) == e->crc;
}

void looks_load(void) {
  cur = -1;
  for (int s = 0; s < (int)NSLOTS; s++) {
    if (*(const uint32_t *)(EXTRA_BASE + s * SLOT) == 0xFFFFFFFFu) break;
    if (slot_ok(s)) cur = s;
  }
  if (cur >= 0) memcpy(&ex, (const void *)(EXTRA_BASE + cur * SLOT), sizeof(ex));
  else looks_defaults(&ex);
  if (ex.transition_ds > 100) ex.transition_ds = 15;
  ex.alexa_name[sizeof(ex.alexa_name) - 1] = 0;
  ex.mqtt_user[sizeof(ex.mqtt_user) - 1] = ex.mqtt_pass[sizeof(ex.mqtt_pass) - 1] = 0;
  ex.mqtt_topic[sizeof(ex.mqtt_topic) - 1] = 0;
  for (int i = 0; i < MAX_PRESETS; i++) ex.presets[i].name[15] = 0;
}

int looks_save(void) {
  ex.magic = EX_MAGIC; ex.version = EX_VER;
  ex.crc = crc32(&ex, offsetof(extras_t, crc), 0);
  int next = cur + 1;
  while (next < (int)NSLOTS) {
    const uint32_t *p = (const uint32_t *)(EXTRA_BASE + next * SLOT);
    int blank = 1;
    for (uint32_t k = 0; k < SLOT / 4; k++) if (p[k] != 0xFFFFFFFFu) { blank = 0; break; }
    if (blank) break;
    next++;
  }
  if (next >= (int)NSLOTS) {
    if (!flash_erase_sector(7)) return 0;
    next = 0;
  }
  if (!flash_program(EXTRA_BASE + next * SLOT, &ex, sizeof(ex))) return 0;
  if (!slot_ok(next)) return 0;
  cur = next;
  return 1;
}

static int item_now(void) {
  int item = 0;
  if (cfg.pl_on && cfg.pl_count) {
    uint32_t total = 0;
    for (int i = 0; i < cfg.pl_count; i++) total += cfg.pl_minutes[i] ? cfg.pl_minutes[i] : 1;
    uint32_t pos = (millis() / 60000u) % total;
    for (int i = 0; i < cfg.pl_count; i++) {
      uint32_t m = cfg.pl_minutes[i] ? cfg.pl_minutes[i] : 1;
      if (pos < m) { item = cfg.pl_fx[i]; break; }
      pos -= m;
    }
  }
  if (ex.cal_on && sched_time_valid()) {
    int m, d;
    sched_local_date(&m, &d);
    int today = m * 100 + d;
    for (int i = 0; i < MAX_CAL; i++) {
      const cal_t *c = &ex.cal[i];
      if (!c->item || !c->m1) continue;
      int a = c->m1 * 100 + c->d1, b = c->m2 * 100 + c->d2;
      int in = a <= b ? (today >= a && today <= b) : (today >= a || today <= b);   // wraps New Year
      if (in) { item = c->item; break; }
    }
  }
  return item;
}

void look_current(look_t *l) {
  l->fx = cfg.fx; l->pal = ex.pal; l->speed = cfg.fx_speed; l->bright = cfg.fx_bright;
  l->intensity = ex.intensity; l->cust_mode = cfg.cust_mode; l->cust_n = cfg.cust_n;
  memcpy(l->cust, cfg.cust_rgb, sizeof(l->cust));
  l->zone = 0;
  int item = item_now();
  if (item >= ITEM_PRESET && item - ITEM_PRESET < MAX_PRESETS && ex.presets[item - ITEM_PRESET].used) {
    const preset_t *p = &ex.presets[item - ITEM_PRESET];
    l->fx = p->fx; l->pal = p->pal; l->speed = p->speed; l->bright = p->bright; l->intensity = p->intensity;
    l->cust_mode = p->cust_mode; l->cust_n = p->cust_n; memcpy(l->cust, p->cust, sizeof(l->cust));
    l->zone = p->zone;
  } else if (item > 0 && item < FX_COUNT) {
    l->fx = item;
  }
  if (!l->speed || l->speed > 10) l->speed = 5;
  if (!l->bright || l->bright > 100) l->bright = 100;
  if (!l->cust_n || l->cust_n > 4) l->cust_n = 1;
}

int looks_store_preset(int idx, const char *name) {
  if (idx < 0 || idx >= MAX_PRESETS) return 0;
  preset_t *p = &ex.presets[idx];
  memset(p, 0, sizeof(*p));
  strncpy(p->name, name && *name ? name : "Preset", sizeof(p->name) - 1);
  for (char *q = p->name; *q; q++) if (*q == '"' || *q == '\\' || *q < 32) *q = ' ';
  p->used = 1; p->fx = cfg.fx; p->pal = ex.pal; p->speed = cfg.fx_speed; p->bright = cfg.fx_bright;
  p->intensity = ex.intensity; p->cust_mode = cfg.cust_mode; p->cust_n = cfg.cust_n;
  memcpy(p->cust, cfg.cust_rgb, sizeof(p->cust));
  for (int i = 0; i < NUM_OUT; i++) p->zone[i] = cfg.out[i].zone_fx;
  return looks_save();
}

int looks_apply_preset(int idx) {
  if (idx < 0 || idx >= MAX_PRESETS || !ex.presets[idx].used) return 0;
  const preset_t *p = &ex.presets[idx];
  cfg.fx = p->fx; ex.pal = p->pal; cfg.fx_speed = p->speed; cfg.fx_bright = p->bright;
  ex.intensity = p->intensity; cfg.cust_mode = p->cust_mode; cfg.cust_n = p->cust_n;
  memcpy(cfg.cust_rgb, p->cust, sizeof(cfg.cust_rgb));
  for (int i = 0; i < NUM_OUT; i++) cfg.out[i].zone_fx = p->zone[i];
  cfg.power_off = 0; cfg.pl_on = 0;
  uint8_t t = cfg.test_mode; cfg.test_mode = 0;
  int ok = config_save();
  cfg.test_mode = t;
  return looks_save() && ok;
}

// ---- sleep timer ----
static uint32_t sleep_start_ms, sleep_len_ms;
void sleep_start(uint16_t minutes) { sleep_start_ms = millis(); sleep_len_ms = (uint32_t)minutes * 60000u; if (!sleep_len_ms) sleep_len_ms = 1; }
void sleep_cancel(void) { sleep_len_ms = 0; }
uint32_t sleep_remaining_s(void) {
  if (!sleep_len_ms) return 0;
  uint32_t el = millis() - sleep_start_ms;
  return el >= sleep_len_ms ? 0 : (sleep_len_ms - el) / 1000;
}
uint32_t sleep_scale256(void) {
  if (!sleep_len_ms) return 256;
  uint32_t el = millis() - sleep_start_ms;
  if (el >= sleep_len_ms) return 0;
  return (uint32_t)((uint64_t)(sleep_len_ms - el) * 256 / sleep_len_ms);
}
void looks_task(void) {
  if (sleep_len_ms && (uint32_t)(millis() - sleep_start_ms) >= sleep_len_ms) {
    sleep_len_ms = 0;
    cfg.power_off = 1;
    uint8_t t = cfg.test_mode; cfg.test_mode = 0;
    config_save();
    cfg.test_mode = t;
  }
}
