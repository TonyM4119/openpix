// Built-in effects, played when no show data is coming in. All stateless and
// integer-only: every pixel is a pure function of (position, time), so they cost
// no RAM and look identical across both passes of a frame.
#include "effects.h"
#include "config.h"
#include "looks.h"
#include "audio.h"

typedef struct { uint8_t r, g, b; } rgb_t;

static uint32_t hash(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}

// 0..255..0 triangle wave
static uint32_t tri(uint32_t x) { x &= 511; return x < 256 ? x : 511 - x; }

static rgb_t mix(rgb_t a, rgb_t b, uint32_t f /*0..255*/) {
  rgb_t o = { (uint8_t)((a.r * (255 - f) + b.r * f) / 255), (uint8_t)((a.g * (255 - f) + b.g * f) / 255),
              (uint8_t)((a.b * (255 - f) + b.b * f) / 255) };
  return o;
}
static rgb_t scale(rgb_t a, uint32_t f) {
  rgb_t o = { (uint8_t)(a.r * f / 255), (uint8_t)(a.g * f / 255), (uint8_t)(a.b * f / 255) };
  return o;
}

// Twinkle envelope for pixel k: mostly 0, occasionally fades up and down.
// period in ms, rarity = 1 in N pixels lit per period.
static uint32_t twinkle(uint32_t k, uint32_t t, uint32_t period, uint32_t rarity) {
  uint32_t slot = (t + hash(k) % period) / period;
  uint32_t h = hash(k * 2654435761u ^ slot);
  if (h % rarity) return 0;
  uint32_t ph = (t + hash(k) % period) % period;
  return tri(ph * 512 / period);
}


// ---- palettes: up to 8 colour stops, evenly spaced, wrapping ----
typedef struct { uint8_t n; uint8_t c[8][3]; } pal_t;
static const pal_t PALS[PAL_COUNT] = {
  [PAL_RAINBOW]   = {6, {{255,0,0},{255,160,0},{255,255,0},{0,255,0},{0,80,255},{160,0,255}}},
  [PAL_CHRISTMAS] = {4, {{255,0,0},{0,170,0},{255,0,0},{255,150,60}}},
  [PAL_CANDY]     = {4, {{255,0,0},{255,255,255},{255,0,0},{255,255,255}}},
  [PAL_HALLOWEEN] = {4, {{255,60,0},{120,0,255},{255,90,0},{40,255,0}}},
  [PAL_USA]       = {3, {{255,0,0},{255,255,255},{0,40,255}}},
  [PAL_FIRE]      = {5, {{40,0,0},{200,20,0},{255,90,0},{255,180,20},{255,240,140}}},
  [PAL_OCEAN]     = {5, {{0,10,60},{0,60,160},{0,140,200},{0,200,220},{120,230,255}}},
  [PAL_FOREST]    = {5, {{0,50,0},{20,120,10},{80,160,20},{0,90,30},{140,200,60}}},
  [PAL_LAVA]      = {5, {{60,0,0},{180,0,0},{255,60,0},{255,140,0},{120,0,0}}},
  [PAL_PARTY]     = {6, {{255,0,120},{120,0,255},{0,120,255},{0,255,120},{255,200,0},{255,60,0}}},
  [PAL_PASTEL]    = {5, {{255,150,180},{180,160,255},{150,220,255},{170,255,190},{255,230,160}}},
  [PAL_SUNSET]    = {5, {{120,0,80},{220,30,60},{255,90,20},{255,170,40},{80,0,120}}},
  [PAL_ICE]       = {4, {{255,255,255},{150,200,255},{40,100,255},{200,230,255}}},
  [PAL_WARM]      = {3, {{255,140,50},{255,170,80},{255,120,30}}},
  [PAL_VALENTINE] = {4, {{255,0,40},{255,90,160},{255,255,255},{200,0,90}}},
  [PAL_STPAT]     = {4, {{0,200,0},{120,255,40},{255,255,255},{0,120,20}}},
  [PAL_EASTER]    = {5, {{255,180,220},{200,170,255},{170,240,255},{255,250,170},{190,255,190}}},
  [PAL_AUTUMN]    = {5, {{180,40,0},{255,110,0},{200,140,0},{120,30,0},{255,170,40}}},
};

static const look_t *L;   // look being rendered (set by fx_color)

// colour at position x (0..255 wraps) of palette p
static rgb_t pal_at(int p, uint32_t x) {
  if (p == PAL_MYCOLORS || p == PAL_DEFAULT) {
    int n = L->cust_n ? L->cust_n : 1;
    if (p == PAL_DEFAULT) p = PAL_RAINBOW; else {
      x &= 255;
      uint32_t pos = x * n, i = pos >> 8, f = pos & 255;
      rgb_t a = {L->cust[i % n][0], L->cust[i % n][1], L->cust[i % n][2]};
      rgb_t b = {L->cust[(i + 1) % n][0], L->cust[(i + 1) % n][1], L->cust[(i + 1) % n][2]};
      return mix(a, b, f);
    }
  }
  const pal_t *pl = &PALS[p < PAL_COUNT ? p : PAL_RAINBOW];
  int n = pl->n ? pl->n : 1;
  x &= 255;
  uint32_t pos = x * n, i = pos >> 8, f = pos & 255;
  rgb_t a = {pl->c[i % n][0], pl->c[i % n][1], pl->c[i % n][2]};
  rgb_t b = {pl->c[(i + 1) % n][0], pl->c[(i + 1) % n][1], pl->c[(i + 1) % n][2]};
  return mix(a, b, f);
}
static rgb_t add(rgb_t a, rgb_t b) {
  rgb_t o = {(uint8_t)(a.r + b.r > 255 ? 255 : a.r + b.r), (uint8_t)(a.g + b.g > 255 ? 255 : a.g + b.g),
             (uint8_t)(a.b + b.b > 255 ? 255 : a.b + b.b)};
  return o;
}
// cheap 0..255 sine from 0..255 phase
static uint32_t sin8(uint32_t x) {
  static const uint8_t q[65] = {128,131,134,137,140,143,146,149,152,155,158,162,165,167,170,173,176,179,182,185,188,190,193,
    196,198,201,203,206,208,211,213,215,218,220,222,224,226,228,230,232,234,235,237,238,240,241,243,244,245,246,248,249,250,
    250,251,252,253,253,254,254,254,255,255,255,255};
  x &= 255;
  if (x < 64) return q[x];
  if (x < 128) return q[128 - x];
  if (x < 192) return 256 - q[x - 128] > 255 ? 0 : 256 - q[x - 128];
  return 256 - q[256 - x];
}
// smooth value noise 0..255
static uint32_t noise1(uint32_t x /* 8.8 fixed */, uint32_t seed) {
  uint32_t i = x >> 8, f = x & 255;
  uint32_t a = hash(i * 374761393u + seed) & 255, b = hash((i + 1) * 374761393u + seed) & 255;
  f = f * f * (765 - 2 * f) / 65025;   // smoothstep
  return (a * (255 - f) + b * f) / 255;
}

static const rgb_t RED = {255, 0, 0}, GREEN = {0, 200, 0}, WHITE = {255, 255, 255},
                   WARM = {255, 150, 60}, BLUE = {0, 40, 255}, ORANGE = {255, 70, 0},
                   PURPLE = {140, 0, 255}, SLIME = {60, 255, 0}, OFF = {0, 0, 0};

void fx_color(const look_t *look, int fx, int pal, int out, uint32_t k, uint32_t n, uint32_t now,
              uint8_t *r, uint8_t *g, uint8_t *b) {
  L = look;
  if (!n) n = 1;
  // speed 1..10 -> time multiplier 0.25x..2.5x
  uint32_t t = (now / 4) * look->speed;
  uint32_t in = look->intensity;          // 0..255 "amount / size" knob
  rgb_t c = OFF;
  if (pal == PAL_DEFAULT) {
    switch (fx) {
      case FX_FIRE: case FX_CANDLE: pal = PAL_FIRE; break;
      case FX_SNOW: pal = PAL_ICE; break;
      case FX_FIREWORKS: pal = PAL_PARTY; break;
      case FX_WAVES: pal = PAL_OCEAN; break;
      default: pal = PAL_RAINBOW; break;
    }
  }
  uint32_t seed = out * 7919u;
  switch (fx) {
    case FX_CHRISTMAS: {
      // red / green blocks of 3 drifting slowly, with warm-white sparkles
      uint32_t pos = k + t / 400;
      c = ((pos / 3) & 1) ? GREEN : RED;
      uint32_t tw = twinkle(k + out * 997, t, 1400, 14);
      if (tw) c = mix(c, WARM, tw);
      break;
    }
    case FX_CANDYCANE: {
      uint32_t pos = k + t / 120;
      c = ((pos / 4) & 1) ? WHITE : RED;
      break;
    }
    case FX_MULTI: {
      // classic C9 bulbs: red, green, blue, orange, yellow, gently breathing
      static const rgb_t pal[5] = {{255, 0, 0}, {0, 200, 0}, {0, 40, 255}, {255, 90, 0}, {255, 190, 0}};
      c = pal[(k + out) % 5];
      uint32_t br = 170 + tri(t / 12 + k * 37) * 85 / 255;
      c = scale(c, br);
      break;
    }
    case FX_HALLOWEEN: {
      // slow orange/purple wave, candle flicker, rare green "slime" flashes
      uint32_t w = tri(k * 12 + t / 10);
      c = mix(ORANGE, PURPLE, w > 160 ? (w - 160) * 255 / 95 : 0);
      uint32_t fl = 150 + hash(k * 31 + t / 70) % 106;
      c = scale(c, fl);
      uint32_t tw = twinkle(k + out * 131, t, 900, 40);
      if (tw) c = mix(c, SLIME, tw);
      break;
    }
    case FX_JULY4: {
      // red / white / blue bands marching, plus white firework sparkles
      static const rgb_t pal[3] = {{255, 0, 0}, {255, 255, 255}, {0, 40, 255}};
      uint32_t pos = k + t / 150;
      c = pal[(pos / 5) % 3];
      uint32_t tw = twinkle(k + out * 577, t, 600, 10);
      if (tw) c = mix(scale(c, 90), WHITE, tw);
      break;
    }
    case FX_TWINKLE: {
      uint32_t tw = twinkle(k + out * 211, t, 2000, 4);
      c = scale(WARM, 40 + tw * 215 / 255);
      break;
    }
    case FX_RAINBOW: {
      uint32_t h = (k * 8 + t / 6) % 768;
      if (h < 256) { c.r = 255 - h; c.g = h; c.b = 0; }
      else if (h < 512) { h -= 256; c.r = 0; c.g = 255 - h; c.b = h; }
      else { h -= 512; c.r = h; c.g = 0; c.b = 255 - h; }
      break;
    }
    case FX_SOLID: c.r = cfg.solid_rgb[0]; c.g = cfg.solid_rgb[1]; c.b = cfg.solid_rgb[2]; break;
    case FX_CUSTOM: {
      // the user's own colours
      int nc = look->cust_n ? look->cust_n : 1;
      #define CC(i) ((rgb_t){look->cust[(i) % nc][0], look->cust[(i) % nc][1], look->cust[(i) % nc][2]})
      switch (look->cust_mode) {
        case CUST_SOLID: c = CC(out); break;                       // one colour per string
        case CUST_ALTERNATE: c = CC(k / 3); break;                 // blocks of 3
        case CUST_CHASE: c = CC((k + t / 150) / 4); break;         // moving bands
        case CUST_TWINKLE: {
          c = scale(CC(k), 70);
          uint32_t tw = twinkle(k + out * 313, t, 1200, 6);
          if (tw) c = mix(c, CC(k), tw);
          break; }
        default: {                                                 // slow fade through the colours
          uint32_t ph = t / 8;
          uint32_t i = (ph / 256) % nc;
          c = mix(CC(i), CC(i + 1), ph & 255);
          break; }
      }
      #undef CC
      break;
    }

    // ---------- 0.7 library ----------
    case FX_BREATHE: c = scale(pal_at(pal, t / 40), 20 + sin8(t / 6) * 235 / 255); break;
    case FX_WIPE: {           // palette colours wipe along the string one after another
      uint32_t per = n * 12 + 1, ph = t % (per * 2), pos = ph % per / 12, lap = ph / per;
      c = (k <= pos) ? pal_at(pal, (t / per) * 64 + lap * 128) : pal_at(pal, (t / per) * 64 + 64);
      break; }
    case FX_THEATER: c = ((k + t / 120) % 3 == 0) ? pal_at(pal, k * 4 + t / 30) : OFF; break;
    case FX_RUNNING: c = scale(pal_at(pal, k * 3), sin8(k * (8 + in / 16) - t / 3)); break;
    case FX_SPARKLE: {        // dim background, quick white sparkles
      c = scale(pal_at(pal, k * 2 + t / 50), 40);
      uint32_t h = hash(k + seed + (t / 40) * 2654435761u);
      if (h % (40 - in / 8) == 0) c = WHITE;
      break; }
    case FX_GLITTER: {        // palette flow with glitter
      c = pal_at(pal, k * 3 + t / 20);
      if (twinkle(k + seed, t, 500, 18 - in / 20)) c = WHITE;
      break; }
    case FX_FIRE: {           // per-string flame: hot at the bottom, flickering noise
      uint32_t hgt = n ? (n - k) * 255 / n : 0;
      uint32_t nz = noise1(k * 40 + t * 2, seed) * noise1(k * 17 + t * 3, seed + 99) / 255;
      int32_t heat = (int32_t)(hgt * (150 + in / 3) / 255) + (int32_t)nz - 90;
      if (heat < 0) heat = 0;
      if (heat > 255) heat = 255;
      c = scale(pal_at(pal, heat * 200 / 255), heat < 40 ? heat * 6 : 255);
      break; }
    case FX_METEOR: {         // bright head with a decaying, sparkly tail
      uint32_t len = n + 40, head = (t / 12) % len;
      int32_t d = (int32_t)head - (int32_t)k;
      if (d >= 0 && d < 30 + (int32_t)in / 8) {
        uint32_t f = 255 - d * 255 / (30 + in / 8);
        if (d > 2 && (hash(k + t / 30) & 3) == 0) f /= 3;
        c = scale(pal_at(pal, head * 2), f);
      }
      break; }
    case FX_COMET: {          // several comets in palette colours
      uint32_t gap = 20 + (255 - in) / 6, pos = (k + gap * 4 - (t / 10) % gap) % gap;
      c = scale(pal_at(pal, (k / gap) * 40 + t / 60), pos < 12 ? 255 - pos * 20 : 0);
      break; }
    case FX_SCANNER: {        // back and forth eye
      uint32_t per = n * 2, ph = (t / 8) % (per ? per : 1), pos = ph < n ? ph : per - ph;
      int32_t d = (int32_t)k - (int32_t)pos; if (d < 0) d = -d;
      uint32_t w = 3 + in / 32;
      c = d < (int32_t)w * 2 ? scale(pal_at(pal, t / 40), 255 - d * 255 / (w * 2)) : OFF;
      break; }
    case FX_FLOW: c = pal_at(pal, k * (2 + in / 32) + t / 15); break;
    case FX_GRADIENT: c = pal_at(pal, k * 255 / n); break;
    case FX_SNOW: {           // drifting flakes over a soft blue
      c = scale(pal_at(pal, 150), 25);
      uint32_t tw = twinkle(k + seed, t, 1600, 7 - in / 64);
      if (tw) c = mix(c, pal_at(pal, hash(k) & 255), tw);
      break; }
    case FX_FIREWORKS: {      // random bursts that expand and fade
      uint32_t slot = t / 700, cx = hash(slot * 31 + seed) % n, age = t % 700;
      int32_t d = (int32_t)k - (int32_t)cx; if (d < 0) d = -d;
      uint32_t rad = age / 25;
      if ((uint32_t)d <= rad && rad - d < 4) c = scale(pal_at(pal, hash(slot) & 255), 255 - age * 255 / 700);
      uint32_t slot2 = (t + 350) / 700, cx2 = hash(slot2 * 53 + seed) % n, age2 = (t + 350) % 700;
      int32_t d2 = (int32_t)k - (int32_t)cx2; if (d2 < 0) d2 = -d2;
      uint32_t r2 = age2 / 25;
      if ((uint32_t)d2 <= r2 && r2 - d2 < 4) c = add(c, scale(pal_at(pal, hash(slot2 + 7) & 255), 255 - age2 * 255 / 700));
      break; }
    case FX_RIPPLE: {
      uint32_t slot = t / 1500, cx = hash(slot + seed) % n, age = t % 1500;
      int32_t d = (int32_t)k - (int32_t)cx; if (d < 0) d = -d;
      uint32_t wave = sin8(d * 24 - age / 3);
      c = scale(pal_at(pal, slot * 50), wave * (1500 - age) / 1500 * (d < (int32_t)(age / 20) ? 1 : 0));
      break; }
    case FX_CANDLE: {
      uint32_t f = 140 + noise1(t * 3, seed + k * 13) * 115 / 255;
      c = scale(pal_at(pal, 150), f);
      break; }
    case FX_LIGHTNING: {      // dark, then a few quick flashes
      uint32_t slot = t / 1800, ph = t % 1800, hit = hash(slot + seed);
      uint32_t s0 = hit % (n ? n : 1), s1 = s0 + 10 + (hit >> 8) % (n ? n : 1);
      int on = (ph < 60) || (ph > 140 && ph < 180) || (ph > 260 && ph < 280 && (hit & 1));
      c = (on && k >= s0 && k < s1) ? WHITE : OFF;
      break; }
    case FX_BOUNCE: {         // three balls under gravity
      c = OFF;
      for (uint32_t i = 0; i < 3; i++) {
        uint32_t per = 1200 + i * 330, ph = (t + i * 400) % per;
        int32_t x = (int32_t)ph - (int32_t)per / 2;
        uint32_t h = (uint32_t)((int64_t)(per / 2) * (per / 2) - (int64_t)x * x) / ((per / 2) * (per / 2) / 256 + 1);
        uint32_t pos = h * (n - 1) / 256;
        if (k + 1 >= pos && k <= pos + 1) c = pal_at(pal, i * 85);
      }
      break; }
    case FX_DISSOLVE: {       // pixels randomly switch to the next colour
      uint32_t step = t / 1600, thr = (t % 1600) * 256 / 1600;
      c = pal_at(pal, ((hash(k + seed) & 255) < thr ? step + 1 : step) * 70);
      break; }
    case FX_STROBE: c = ((t / 20) % (12 - in / 32) == 0) ? pal_at(pal, t / 100) : OFF; break;
    case FX_STRIPES: c = pal_at(pal, ((k + t / 60) / (3 + in / 32)) * 51); break;
    case FX_FADE: c = pal_at(pal, t / 25); break;
    case FX_PTWINKLE: {       // palette-coloured twinkles on black
      uint32_t tw = twinkle(k + seed, t, 1500, 3 + (255 - in) / 40);
      c = scale(pal_at(pal, hash(k * 7 + seed) & 255), tw);
      break; }
    case FX_SINELON: {        // dot sweeping on a sine, fading trail
      uint32_t pos = sin8(t / 20) * (n - 1) / 255;
      int32_t d = (int32_t)k - (int32_t)pos; if (d < 0) d = -d;
      c = d < 12 ? scale(pal_at(pal, t / 30), 255 - d * 20) : OFF;
      break; }
    case FX_JUGGLE: {         // several dots weaving
      c = OFF;
      for (uint32_t i = 0; i < 5; i++) {
        uint32_t pos = sin8(t / (14 + i * 3) + i * 40) * (n - 1) / 255;
        int32_t d = (int32_t)k - (int32_t)pos; if (d < 0) d = -d;
        if (d < 6) c = add(c, scale(pal_at(pal, i * 50), 255 - d * 40));
      }
      break; }
    case FX_HEARTBEAT: {
      uint32_t ph = t % 1000;
      uint32_t f = ph < 100 ? 255 - ph * 2 : (ph > 200 && ph < 300) ? 200 - (ph - 200) * 2 : 30;
      c = scale(pal_at(pal, k * 2), f);
      break; }
    case FX_WAVES: {          // two layered ocean waves
      uint32_t a = sin8(k * 6 + t / 8), b2 = sin8(k * 11 - t / 13);
      c = scale(pal_at(pal, (a + b2) / 2), 80 + (a * b2 / 255) * 175 / 255);
      break; }
    case FX_AURORA: {         // slow green/purple curtains
      uint32_t nz = noise1(k * 20 + t / 3, seed);
      rgb_t g = {0, 255, 90}, p2 = {120, 0, 255};
      c = scale(mix(g, p2, noise1(k * 7 + t / 5, seed + 5)), nz);
      break; }
    case FX_POLICE: {         // red/blue halves flashing
      int left = k < n / 2;
      uint32_t ph = (t / 60) % 8;
      int on = left ? (ph == 0 || ph == 2) : (ph == 4 || ph == 6);
      rgb_t rc = {255, 0, 0}, bc = {0, 0, 255};
      c = on ? (left ? rc : bc) : OFF;
      break; }
    case FX_CHASE3: c = pal_at(pal, ((k + t / 80) / (2 + in / 40)) % 3 * 85); break;
    // ---------- music reactive (needs network audio sync) ----------
    case FX_VU: {
      uint32_t lvl = audio_level() * n / 255;
      c = k < lvl ? pal_at(pal, k * 255 / n) : OFF;
      break; }
    case FX_BEAT: c = scale(pal_at(pal, k * 3 + t / 30), 30 + audio_level() * 225 / 255); break;
    case FX_SPECTRUM: {
      uint32_t band = k * 16 / n;
      c = scale(pal_at(pal, band * 16), audio_band(band));
      break; }
    default: break;
  }
  (void)BLUE;
  *r = c.r; *g = c.g; *b = c.b;
}

// Representative colour of what is playing (reported to Home Assistant / WLED apps
// so other lights, e.g. Cync bulbs, can follow along).
void look_main_color(uint8_t rgb[3]) {
  static look_t l; look_current(&l); L = &l;
  rgb_t c;
  switch (l.fx) {
    case FX_SOLID: rgb[0] = cfg.solid_rgb[0]; rgb[1] = cfg.solid_rgb[1]; rgb[2] = cfg.solid_rgb[2]; return;
    case FX_CHRISTMAS: c = RED; break;
    case FX_CANDYCANE: c = RED; break;
    case FX_HALLOWEEN: c = ORANGE; break;
    case FX_JULY4: c = BLUE; break;
    case FX_TWINKLE: case FX_CANDLE: c = WARM; break;
    case FX_CUSTOM: c = (rgb_t){l.cust[0][0], l.cust[0][1], l.cust[0][2]}; break;
    default: c = pal_at(l.pal ? l.pal : PAL_RAINBOW, 0); break;
  }
  rgb[0] = c.r; rgb[1] = c.g; rgb[2] = c.b;
}
