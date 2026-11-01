// WS2811/WS2812 output on up to 48 lines in parallel (same technique as stock:
// all lines driven through the GPIO set/reset registers every bit period).
#include "board.h"
#include "pixels.h"
#include <string.h>
#include "effects.h"
#include "sensors.h"
#include "looks.h"

uint8_t flat[FLAT_SIZE] __attribute__((section(".tcm")));   // CPU-only fast RAM

typedef struct { uint16_t bc, e, d; } slice_t;   // bc: PB0-7 (low) + PC8-15 (high)
#define SLICE_MAX (MAX_PIX * 32 + 32 + 400)   // room for APA102 (32 bits/pixel + start/end frames)
static slice_t slices[SLICE_MAX] __attribute__((section(".xram")));

static uint32_t exp_mask[(MAX_UNIV + 31) / 32];   // universes used by the config
static uint32_t got_mask[(MAX_UNIV + 31) / 32];
static uint32_t first_rx_ms, last_rx_ms, last_out_ms;
static int pending, push_req;
static uint32_t frames, fps_count, fps_val, fps_t;
uint32_t g_frame_ms;

// Built-in effect plays when selected and no show data arrived for 3 s.
int pixels_fx_active(void) {
  if ((!cfg.fx && !(cfg.pl_on && cfg.pl_count) && !ex.cal_on) || cfg.test_mode) return 0;
  return !last_rx_ms || (uint32_t)(millis() - last_rx_ms) > 3000;
}

uint32_t pixels_fps(void) { return fps_val; }
uint32_t pixels_frames(void) { return frames; }
uint32_t pixels_last_data_ms(void) { return last_rx_ms; }

void pixels_config_changed(void) {
  memset(exp_mask, 0, sizeof(exp_mask));
  for (int i = 0; i < NUM_OUT; i++) {
    out_cfg_t *o = &cfg.out[i];
    if (!o->pixels || !o->universe) continue;
    uint32_t start = (uint32_t)(o->universe - 1) * cfg.univ_size + (o->channel ? o->channel - 1 : 0);
    uint32_t end = start + (uint32_t)o->pixels * 3 - 1;
    for (uint32_t u = start / cfg.univ_size; u <= end / cfg.univ_size && u < MAX_UNIV; u++)
      exp_mask[u >> 5] |= 1u << (u & 31);
  }
  memset(got_mask, 0, sizeof(got_mask));
  pending = 1;
}

void pixels_init(void) {
  memset(flat, 0, sizeof(flat));
  pixels_config_changed();
}

void pixels_universe_rx(uint16_t u) {
  if (u < 1 || u > MAX_UNIV) return;
  u--;
  if (!pending) first_rx_ms = millis();
  got_mask[u >> 5] |= 1u << (u & 31);
  last_rx_ms = millis();
  pending = 1;
}

void pixels_push(void) { last_rx_ms = millis(); pending = 1; push_req = 1; }

static int all_received(void) {
  for (unsigned i = 0; i < sizeof(exp_mask) / 4; i++)
    if ((got_mask[i] & exp_mask[i]) != exp_mask[i]) return 0;
  return 1;
}

static void hsv(uint32_t h, uint8_t *r, uint8_t *g, uint8_t *b) {
  h %= 768;
  if (h < 256) { *r = 255 - h; *g = h; *b = 0; }
  else if (h < 512) { h -= 256; *r = 0; *g = 255 - h; *b = h; }
  else { h -= 512; *r = h; *g = 0; *b = 255 - h; }
}

static uint32_t frame_now;          // one timestamp per frame so both passes agree
uint32_t g_power_ma, g_power_scale = 256;   // estimated draw and limiter scale (256 = none)


// ---- effect rendering for one pixel of a look (segments, zones, brightness, gamma) ----
static look_t cur_look, prev_look;
static int in_transition;
static uint32_t trans_f, trans_start;
uint8_t g_preview[NUM_OUT][PREVIEW_N][3];

static void fx_pixel(const look_t *lk, int out, uint32_t k, uint32_t n, uint32_t now, uint8_t *r, uint8_t *g, uint8_t *b) {
  int fx, pal = lk->pal;
  uint32_t kk = k, nn = n;
  const seg_t *sg = 0;
  for (int i = 0; i < MAX_SEGS; i++) {
    const seg_t *s = &ex.segs[i];
    if (s->out == out + 1 && k >= s->start && k < (uint32_t)s->start + s->count) { sg = s; break; }
  }
  if (sg) { fx = sg->fx; pal = sg->pal; kk = k - sg->start; nn = sg->count; }
  else {
    uint8_t z = lk->zone ? lk->zone[out] : cfg.out[out].zone_fx;
    fx = z == ZONE_FOLLOW ? lk->fx : z;
  }
  if (fx == ZONE_DARK || !fx || fx >= FX_COUNT) { *r = *g = *b = 0; return; }
  fx_color(lk, fx, pal, out, kk, nn, now, r, g, b);
  uint32_t br = lk->bright;
  if (br < 100) { *r = *r * br / 100; *g = *g * br / 100; *b = *b * br / 100; }
  if (ex.gamma_fx) { *r = gamma8[*r]; *g = gamma8[*g]; *b = gamma8[*b]; }
}

// called once per frame: pick up look changes and run the crossfade
static void looks_frame(uint32_t now) {
  look_t l;
  look_current(&l);
  if (memcmp(&l, &cur_look, sizeof(l)) != 0) {
    if (cur_look.fx && ex.transition_ds) { prev_look = cur_look; trans_start = now; in_transition = 1; }
    cur_look = l;
  }
  if (in_transition) {
    uint32_t len = ex.transition_ds * 100u, el = now - trans_start;
    if (el >= len) in_transition = 0; else trans_f = el * 256 / len;
  }
}

static void get_rgb(int out, uint32_t k, uint8_t *r, uint8_t *g, uint8_t *b) {
  out_cfg_t *o = &cfg.out[out];
  uint32_t now = frame_now;
  switch (cfg.test_mode) {
    case 1: *r = cfg.test_r; *g = cfg.test_g; *b = cfg.test_b; return;
    case 2: hsv(k * 8 + now / 4, r, g, b); return;
    case 3: { uint32_t pos = (now / 40) % (o->pixels ? o->pixels : 1);
              uint8_t v = (k >= pos && k < pos + 5) ? 255 : 0; *r = v; *g = v; *b = v; return; }
    case 4: {  // identify: output N lights its first N pixels white, the rest dim blue
      if (k < (uint32_t)out + 1) { *r = *g = *b = 160; } else { *r = 0; *g = 0; *b = 24; }
      return; }
    default: break;
  }
  if (pixels_fx_active()) {
    if (!fx_scheduled_on()) { *r = *g = *b = 0; return; }
    fx_pixel(&cur_look, out, k, o->pixels, now, r, g, b);
    if (in_transition) {
      uint8_t r2, g2, b2;
      fx_pixel(&prev_look, out, k, o->pixels, now, &r2, &g2, &b2);
      *r = (*r * trans_f + r2 * (256 - trans_f)) >> 8;
      *g = (*g * trans_f + g2 * (256 - trans_f)) >> 8;
      *b = (*b * trans_f + b2 * (256 - trans_f)) >> 8;
    }
    return;
  }
  uint32_t idx = (uint32_t)(o->universe - 1) * cfg.univ_size + (o->channel ? o->channel - 1 : 0) + k * 3;
  if (idx + 2 >= FLAT_SIZE || o->universe == 0) { *r = *g = *b = 0; return; }
  *r = flat[idx]; *g = flat[idx + 1]; *b = flat[idx + 2];
  if (ex.gamma_data) { *r = gamma8[*r]; *g = gamma8[*g]; *b = gamma8[*b]; }
}

static void line_bit(int line, uint16_t *mb, uint16_t *me, uint16_t *md, int *field, uint16_t *bit) {
  (void)mb; (void)me; (void)md;
  if (line < 8)       { *field = 0; *bit = 1u << line; }          // PB0-7
  else if (line < 16) { *field = 0; *bit = 1u << line; }          // PC8-15
  else if (line < 32) { *field = 1; *bit = 1u << (line - 16); }   // PE0-15
  else                { *field = 2; *bit = 1u << (line - 32); }   // PD0-15
}

// WS2801 / APA102: data on every line, shared clock on PB15
static void send_clocked(uint32_t nbits, uint32_t mb, uint32_t mc, uint32_t me, uint32_t md) {
  uint32_t half = g_sysclk / (2u * cfg.spi_khz * 1000u);
  if (half < 8) half = 8;
  __disable_irq();
  uint32_t t0 = cyc(), t = t0;
  for (uint32_t i = 0; i < nbits; i++) {
    const slice_t s = slices[i];
    GPIOB->BSRR = (s.bc & mb) | (((~s.bc) & mb) << 16);
    GPIOC->BSRR = (s.bc & mc) | (((~s.bc) & mc) << 16);
    GPIOE->BSRR = (s.e & me) | (((~s.e) & me) << 16);
    GPIOD->BSRR = (s.d & md) | (((~s.d) & md) << 16);
    while ((uint32_t)(cyc() - t) < half) {}
    t += half;
    GPIOB->BSRR = 1u << 15;                     // clock high: pixels latch the bit
    while ((uint32_t)(cyc() - t) < half) {}
    t += half;
    GPIOB->BSRR = (1u << 15) << 16;
  }
  GPIOB->BSRR = (mb << 16); GPIOC->BSRR = (mc << 16); GPIOE->BSRR = (me << 16); GPIOD->BSRR = (md << 16);
  uint32_t ms = (cyc() - t0) / (g_sysclk / 1000u);
  g_frame_ms = ms;
  if (ms > 1) g_ms += ms - 1;
  __enable_irq();
}

static void build_and_send(void) {
  uint32_t maxlen = 0;
  uint16_t mbc = 0, me = 0, md = 0;
  for (int i = 0; i < NUM_OUT; i++) {
    uint32_t n = cfg.out[i].pixels ? cfg.out[i].pixels + cfg.out[i].nulls : 0;
    if (n > MAX_PIX) n = MAX_PIX;
    if (n > maxlen) maxlen = n;
  }
  if (!maxlen) return;
  const int apa = cfg.pix_type == PIX_APA102;
  const uint32_t bpp = apa ? 32 : 24, pre = apa ? 32 : 0;
  const uint32_t post = apa ? ((maxlen + 15) / 16 + 1) * 8 : 0;   // APA102 end frame (ones)
  uint32_t nbits = pre + maxlen * bpp + post;
  if (nbits > SLICE_MAX) nbits = SLICE_MAX;
  memset(slices, 0, nbits * sizeof(slice_t));

  // Pass 1: estimate current draw for the power limiter
  frame_now = millis();
  looks_frame(frame_now);
  uint32_t sum = 0;
  int dark = g_overtemp || (cfg.power_off && !cfg.test_mode);
  if (!dark && cfg.power_limit_a) {
    for (int i = 0; i < NUM_OUT; i++) {
      out_cfg_t *o = &cfg.out[i];
      for (uint32_t k = 0; k < o->pixels && k < MAX_PIX; k++) {
        uint8_t r, g, b;
        get_rgb(i, k, &r, &g, &b);
        sum += (uint32_t)(r + g + b) * o->bright;
      }
    }
  }
  // sum/(255*100) = full-on LED colours; x mA each
  g_power_ma = (uint32_t)((uint64_t)sum * cfg.ma_per_ch / (255u * 100u));
  uint32_t limit = (uint32_t)cfg.power_limit_a * 1000u;
  g_power_scale = (cfg.power_limit_a && g_power_ma > limit) ? (uint32_t)((uint64_t)limit * 256 / g_power_ma) : 256;

  // Pass 2: build the bit slices
  for (int i = 0; i < NUM_OUT; i++) {
    out_cfg_t *o = &cfg.out[i];
    if (!o->pixels) continue;
    int field; uint16_t bit;
    line_bit(cfg.out_map[i], 0, 0, 0, &field, &bit);
    if (field == 0) mbc |= bit; else if (field == 1) me |= bit; else md |= bit;
    if (dark) continue;   // over temperature: send all-black frames
    uint32_t total = o->pixels + o->nulls;
    if (total > MAX_PIX) total = MAX_PIX;
    uint32_t sc = (uint32_t)o->bright * g_power_scale * sleep_scale256() / 256;   // /25600
    uint32_t pstep = o->pixels / PREVIEW_N + 1;
    for (uint32_t p = o->nulls; p < total; p++) {
      uint32_t k = p - o->nulls;
      if (o->reverse) k = o->pixels - 1 - k;
      if (o->zigzag > 1) {
        uint32_t g = k / o->zigzag;
        if (g & 1) k = g * o->zigzag + (o->zigzag - 1 - k % o->zigzag);
        if (k >= o->pixels) k = o->pixels - 1;
      }
      uint8_t r, g, b;
      get_rgb(i, k, &r, &g, &b);
      if (sc < 25600) { r = r * sc / 25600; g = g * sc / 25600; b = b * sc / 25600; }
      if (k % pstep == 0 && k / pstep < PREVIEW_N) { uint8_t *pv = g_preview[i][k / pstep]; pv[0] = r; pv[1] = g; pv[2] = b; }
      uint8_t c[3];
      switch (o->order) {
        case ORD_RGB: c[0] = r; c[1] = g; c[2] = b; break;
        case ORD_RBG: c[0] = r; c[1] = b; c[2] = g; break;
        case ORD_GRB: c[0] = g; c[1] = r; c[2] = b; break;
        case ORD_GBR: c[0] = g; c[1] = b; c[2] = r; break;
        case ORD_BRG: c[0] = b; c[1] = r; c[2] = g; break;
        default:      c[0] = b; c[1] = g; c[2] = r; break;
      }
      slice_t *s = &slices[pre + p * bpp];
      if (apa) {   // APA102 pixel header: 111 + global brightness 31
        for (int bi = 0; bi < 8; bi++, s++) { if (field == 0) s->bc |= bit; else if (field == 1) s->e |= bit; else s->d |= bit; }
      }
      for (int ch = 0; ch < 3; ch++) {
        uint8_t v = c[ch];
        for (int bi = 0; bi < 8; bi++, s++) {
          if (v & 0x80) {
            if (field == 0) s->bc |= bit; else if (field == 1) s->e |= bit; else s->d |= bit;
          }
          v <<= 1;
        }
      }
    }
  }

  if (apa) {
    // headers for null pixels and pixels past each string's end, then the end frame
    for (uint32_t p = 0; p < maxlen; p++)
      for (int bi = 0; bi < 8; bi++) {
        slice_t *s = &slices[pre + p * bpp + bi];
        s->bc = mbc; s->e = me; s->d = md;
      }
    for (uint32_t i = pre + maxlen * bpp; i < nbits; i++) { slices[i].bc = mbc; slices[i].e = me; slices[i].d = md; }
  }

  const uint32_t mb = mbc & 0x00FF, mc = mbc & 0xFF00;
  const uint32_t f = g_sysclk / 1000000u;     // cycles per us
  if (cfg.pix_type != PIX_WS281X) { send_clocked(nbits, mb, mc, me, md); frames++; fps_count++; return; }
  // WS2811 (fast) / WS2812: 0 = ~0.32 us high, 1 = ~0.70 us high, 1.25 us period
  const uint32_t T0H = f * 30 / 100, T1H = f * 70 / 100, TP = f * 125 / 100;

  __disable_irq();
  uint32_t t0 = cyc();
  uint32_t t = t0;
  for (uint32_t i = 0; i < nbits; i++) {
    const slice_t s = slices[i];
    // compute the "zero bit" pattern before the period starts so the
    // high-time is set only by the wait below
    const uint32_t lb = ((~s.bc) & mb) << 16, lc = ((~s.bc) & mc) << 16;
    const uint32_t le = ((~s.e) & me) << 16, ld = ((~s.d) & md) << 16;
    while ((uint32_t)(cyc() - t) < TP) {}
    t += TP;
    GPIOB->BSRR = mb; GPIOC->BSRR = mc; GPIOE->BSRR = me; GPIOD->BSRR = md;
    while ((uint32_t)(cyc() - t) < T0H) {}
    GPIOB->BSRR = lb; GPIOC->BSRR = lc; GPIOE->BSRR = le; GPIOD->BSRR = ld;
    while ((uint32_t)(cyc() - t) < T1H) {}
    GPIOB->BSRR = mb << 16; GPIOC->BSRR = mc << 16; GPIOE->BSRR = me << 16; GPIOD->BSRR = md << 16;
  }
  uint32_t el = cyc() - t0;
  // keep millis() honest: SysTick could only fire once while interrupts were off
  uint32_t ms = el / (g_sysclk / 1000u);
  g_frame_ms = ms;
  if (ms > 1) g_ms += ms - 1;
  __enable_irq();
  frames++; fps_count++;
}

void pixels_task(void) {
  uint32_t now = millis();
  if ((uint32_t)(now - fps_t) >= 1000) { fps_val = fps_count; fps_count = 0; fps_t = now; }
  // Need >= 300 us of low time between frames (latch); keep a 1 ms gap.
  if ((uint32_t)(now - last_out_ms) < 2) return;

  int go = 0;
  if (cfg.test_mode || pixels_fx_active()) {
    go = (uint32_t)(now - last_out_ms) >= 25;
  } else if (pending) {
    if (push_req || all_received()) go = 1;
    else if ((uint32_t)(now - first_rx_ms) >= 30) go = 1;
  } else if (!cfg.hold_last && last_rx_ms && (uint32_t)(now - last_rx_ms) > 2500) {
    memset(flat, 0, sizeof(flat));
    last_rx_ms = 0;
    go = 1;
  } else if ((uint32_t)(now - last_out_ms) >= 1000) {
    go = 1;  // refresh once a second so strings recover from glitches
  }
  if (!go) return;
  pending = 0; push_req = 0;
  memset(got_mask, 0, sizeof(got_mask));
  build_and_send();
  last_out_ms = millis();
}
