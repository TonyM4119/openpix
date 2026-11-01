// "Looks": presets, palettes, segments, holiday calendar, transitions, gamma,
// sleep timer, plus settings for the network integrations. Stored separately
// from the main settings, in flash sector 7.
#pragma once
#include <stdint.h>
#include "config.h"

#define MAX_PRESETS 12
#define MAX_SEGS    16
#define MAX_CAL     10
#define ITEM_PRESET 100    // playlist / calendar items: < 100 effect id, >= 100 preset (item-100)

typedef struct {
  char    name[16];
  uint8_t used, fx, pal, speed, bright, intensity, cust_mode, cust_n;
  uint8_t cust[4][3];
  uint8_t zone[NUM_OUT];   // per-output effect (ZONE_FOLLOW / FX id / ZONE_DARK)
} preset_t;

typedef struct {
  uint8_t  out;            // 1..48, 0 = unused
  uint8_t  fx, pal, pad;
  uint16_t start, count;   // pixel range inside that output
} seg_t;

typedef struct { uint8_t m1, d1, m2, d2, item, pad[3]; } cal_t;   // from m1/d1 to m2/d2 inclusive

typedef struct {
  uint32_t magic, version;
  uint8_t  pal;            // palette for the main effect (0 = effect's own colours)
  uint8_t  intensity;      // 0..255 effect "density/size" knob
  uint8_t  gamma_fx;       // gamma-correct built-in effects (default on)
  uint8_t  gamma_data;     // gamma-correct xLights/sACN data (default off)
  uint8_t  transition_ds;  // crossfade length in 0.1 s
  uint8_t  cal_on;
  uint8_t  pad0[2];
  preset_t presets[MAX_PRESETS];
  seg_t    segs[MAX_SEGS];
  cal_t    cal[MAX_CAL];
  // integrations (0.8)
  uint8_t  alexa_on;  char alexa_name[24];
  uint8_t  sync_send, sync_recv, audio_on;
  uint8_t  mqtt_on; uint8_t mqtt_ip[4]; uint16_t mqtt_port;
  char     mqtt_user[24], mqtt_pass[24], mqtt_topic[32];
  uint8_t  reserved[96];
  uint32_t crc;
} extras_t;

extern extras_t ex;
void looks_load(void);
int  looks_save(void);
void looks_defaults(extras_t *e);

// The look being shown right now (after playlist / calendar / preset)
typedef struct {
  uint8_t fx, pal, speed, bright, intensity, cust_mode, cust_n;
  uint8_t cust[4][3];
  const uint8_t *zone;     // NULL = use the per-output settings
} look_t;
void look_current(look_t *l);
int  looks_apply_preset(int idx);        // copy preset into the live settings and save
int  looks_store_preset(int idx, const char *name);

// sleep timer
void sleep_start(uint16_t minutes);
void sleep_cancel(void);
uint32_t sleep_remaining_s(void);
uint32_t sleep_scale256(void);           // 256 = full, fades to 0
void looks_task(void);

extern const uint8_t gamma8[256];
