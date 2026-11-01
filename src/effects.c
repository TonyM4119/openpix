// Built-in holiday effects, played when no show data is coming in.
// All integer math (no floats) so they are cheap even on 48 x 680 pixels.
#include "effects.h"
#include "config.h"

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

static const rgb_t RED = {255, 0, 0}, GREEN = {0, 200, 0}, WHITE = {255, 255, 255},
                   WARM = {255, 150, 60}, BLUE = {0, 40, 255}, ORANGE = {255, 70, 0},
                   PURPLE = {140, 0, 255}, SLIME = {60, 255, 0}, OFF = {0, 0, 0};

void fx_color(int fx, int out, uint32_t k, uint32_t now, uint8_t speed, uint8_t *r, uint8_t *g, uint8_t *b) {
  // speed 1..10 -> time multiplier 0.25x..2.5x
  uint32_t t = (now / 4) * speed;
  rgb_t c = OFF;
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
    default: break;
  }
  (void)BLUE;
  *r = c.r; *g = c.g; *b = c.b;
}
