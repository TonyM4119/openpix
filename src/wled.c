// A small WLED-compatible JSON API so Home Assistant's WLED integration, the
// WLED phone apps and other WLED tools can switch the lights on/off, set
// brightness, pick an effect or a solid colour.
//   GET  /json, /json/state, /json/info, /json/si, /json/eff, /json/pal, /presets.json
//   POST /json/state, /json/si, /json   (JSON body: on, bri, seg[0].fx/sx/col)
#include "board.h"
#include "wled.h"
#include "config.h"
#include "pixels.h"
#include "net.h"
#include "proto.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// WLED effect index -> OpenPix effect. Index 0 is "Solid".
static const uint8_t map_fx[] = {FX_SOLID, FX_CHRISTMAS, FX_CANDYCANE, FX_MULTI, FX_HALLOWEEN,
                                 FX_JULY4, FX_TWINKLE, FX_RAINBOW, FX_CUSTOM};
static const char *const fx_names[] = {"Solid", "Christmas", "Candy cane", "Multicolor", "Halloween",
                                       "4th of July", "Warm twinkle", "Rainbow", "My colors"};
#define NFX (int)(sizeof(map_fx) / sizeof(map_fx[0]))

static int leds(void) {
  int n = 0;
  for (int i = 0; i < NUM_OUT; i++) n += cfg.out[i].pixels;
  return n;
}
static int wled_fx(void) {
  for (int i = 0; i < NFX; i++) if (map_fx[i] == cfg.fx) return i;
  return 0;
}
static int on_state(void) { return !cfg.power_off && cfg.fx != FX_OFF; }

#define AP(...) do { int _n = snprintf(w, end - w, __VA_ARGS__); if (_n > 0) w += (_n < end - w ? _n : end - w - 1); } while (0)

static char *state(char *w, char *end) {
  uint8_t *c = cfg.solid_rgb;
  int n = leds();
  AP("{\"on\":%s,\"bri\":%d,\"transition\":7,\"ps\":-1,\"pl\":-1,\"lor\":0,\"mainseg\":0,"
     "\"nl\":{\"on\":false,\"dur\":60,\"mode\":1,\"tbri\":0,\"rem\":-1},\"udpn\":{\"send\":false,\"recv\":false,\"sgrp\":0,\"rgrp\":0},"
     "\"seg\":[{\"id\":0,\"start\":0,\"stop\":%d,\"len\":%d,\"grp\":1,\"spc\":0,\"of\":0,\"on\":true,\"frz\":false,"
     "\"bri\":255,\"cct\":127,\"col\":[[%u,%u,%u],[0,0,0],[0,0,0]],\"fx\":%d,\"sx\":%d,\"ix\":128,\"pal\":0,"
     "\"c1\":128,\"c2\":128,\"c3\":16,\"sel\":true,\"rev\":false,\"mi\":false,\"o1\":false,\"o2\":false,\"o3\":false,\"si\":0,\"m12\":0}]}",
     on_state() ? "true" : "false", cfg.fx_bright * 255 / 100, n, n, c[0], c[1], c[2], wled_fx(),
     cfg.fx_speed * 255 / 10);
  return w;
}

static char *info(char *w, char *end) {
  uint8_t m[6]; device_mac(m);
  uint32_t age = pixels_last_data_ms() ? millis() - pixels_last_data_ms() : 0xFFFFFFFF;
  AP("{\"ver\":\"0.15.0\",\"vid\":2412100,\"leds\":{\"count\":%d,\"pwr\":%lu,\"fps\":%lu,\"maxpwr\":%u,\"maxseg\":1,"
     "\"seglc\":[1],\"lc\":1,\"rgbw\":false,\"wv\":0,\"cct\":0},\"str\":false,\"name\":\"%s\",\"udpport\":21324,"
     "\"live\":%s,\"liveseg\":-1,\"lm\":\"%s\",\"lip\":\"\",\"ws\":-1,\"fxcount\":%d,\"palcount\":1,\"cpalcount\":0,\"maps\":[],"
     "\"wifi\":{\"bssid\":\"\",\"rssi\":0,\"signal\":100,\"channel\":0},\"fs\":{\"u\":0,\"t\":0,\"pmt\":0},\"ndc\":0,"
     "\"arch\":\"gd32f450\",\"core\":\"openpix-%s\",\"lwip\":2,\"freeheap\":65536,\"uptime\":%lu,\"opt\":0,"
     "\"brand\":\"WLED\",\"product\":\"OpenPix\",\"mac\":\"%02x%02x%02x%02x%02x%02x\",\"ip\":\"%s\"}",
     leds(), (unsigned long)g_power_ma, (unsigned long)pixels_fps(), cfg.power_limit_a * 1000, cfg.name,
     age < 2500 ? "true" : "false", age < 2500 ? "sACN/Art-Net/DDP" : "", NFX, FW_VERSION,
     (unsigned long)(millis() / 1000), m[0], m[1], m[2], m[3], m[4], m[5], net_ip_str());
  return w;
}

static char *effects(char *w, char *end) {
  AP("[");
  for (int i = 0; i < NFX; i++) AP("%s\"%s\"", i ? "," : "", fx_names[i]);
  AP("]");
  return w;
}

int wled_get(const char *path, char *buf, int max) {
  char *w = buf, *end = buf + max;
  if (!strcmp(path, "/json/state")) w = state(w, end);
  else if (!strcmp(path, "/json/info")) w = info(w, end);
  else if (!strcmp(path, "/json/eff")) w = effects(w, end);
  else if (!strcmp(path, "/json/pal")) AP("[\"Default\"]");
  else if (!strcmp(path, "/json/si")) { AP("{\"state\":"); w = state(w, end); AP(",\"info\":"); w = info(w, end); AP("}"); }
  else if (!strcmp(path, "/json") || !strcmp(path, "/json/")) {
    AP("{\"state\":"); w = state(w, end); AP(",\"info\":"); w = info(w, end);
    AP(",\"effects\":"); w = effects(w, end); AP(",\"palettes\":[\"Default\"]}");
  } else if (!strcmp(path, "/presets.json")) AP("{}");
  else return -1;
  return w - buf;
}

// --- tiny JSON field readers (enough for WLED state updates) ---
static const char *key(const char *j, const char *k) {
  char pat[16];
  snprintf(pat, sizeof pat, "\"%s\"", k);
  const char *p = strstr(j, pat);
  if (!p) return 0;
  p += strlen(pat);
  while (*p == ' ' || *p == ':') p++;
  return p;
}

int wled_post_state(char *body) {
  const char *p;
  int changed = 0;
  int want_on = -1;
  if ((p = key(body, "on"))) {
    if (!strncmp(p, "true", 4)) want_on = 1;
    else if (!strncmp(p, "false", 5)) want_on = 0;
    else if (*p == '"' && p[1] == 't') want_on = !on_state();   // "t" = toggle
  }
  if ((p = key(body, "bri"))) {
    int b = atoi(p);
    if (b <= 0) want_on = 0;
    else { cfg.fx_bright = b >= 255 ? 100 : (b * 100 + 254) / 255; if (cfg.fx_bright < 1) cfg.fx_bright = 1; changed = 1; }
  }
  const char *seg = strstr(body, "\"seg\"");
  const char *s = seg ? seg : body;
  if ((p = key(s, "fx"))) {
    int f = atoi(p);
    if (f >= 0 && f < NFX) { cfg.fx = map_fx[f]; cfg.pl_on = 0; changed = 1; if (want_on < 0) want_on = 1; }
  }
  if ((p = key(s, "sx"))) { int v = atoi(p); cfg.fx_speed = v * 10 / 255; if (cfg.fx_speed < 1) cfg.fx_speed = 1; if (cfg.fx_speed > 10) cfg.fx_speed = 10; changed = 1; }
  if ((p = key(s, "col"))) {
    while (*p == '[' || *p == ' ') p++;
    int r = atoi(p);
    const char *q = strchr(p, ','); int g = q ? atoi(q + 1) : 0;
    q = q ? strchr(q + 1, ',') : 0; int b = q ? atoi(q + 1) : 0;
    cfg.solid_rgb[0] = r; cfg.solid_rgb[1] = g; cfg.solid_rgb[2] = b;
    if (!(p = key(s, "fx"))) cfg.fx = FX_SOLID;
    cfg.pl_on = 0;
    changed = 1;
    if (want_on < 0) want_on = 1;
  }
  if (want_on == 1) {
    cfg.power_off = 0;
    if (cfg.fx == FX_OFF) cfg.fx = FX_SOLID;
    cfg.test_mode = 0;
    changed = 1;
  } else if (want_on == 0) { cfg.power_off = 1; changed = 1; }
  if (changed) {
    uint8_t t = cfg.test_mode; cfg.test_mode = 0;
    config_save();
    cfg.test_mode = t;
  }
  return changed;
}
