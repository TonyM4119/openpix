#pragma once
#include <stdint.h>

#define NUM_OUT      48     // 3 expansion ports x 16
#define MAX_PIX      680    // per output (4 universes of 170)
#define MAX_UNIV     96     // universes we accept (1..96)

enum { ORD_RGB, ORD_RBG, ORD_GRB, ORD_GBR, ORD_BRG, ORD_BGR };
enum { FX_OFF, FX_CHRISTMAS, FX_CANDYCANE, FX_MULTI, FX_HALLOWEEN, FX_JULY4, FX_TWINKLE, FX_RAINBOW,
       FX_CUSTOM, FX_COUNT };
#define ZONE_FOLLOW 0      // out.zone_fx: follow the main effect / playlist
#define ZONE_DARK   0xFF   //              stay dark during effects
enum { CUST_SOLID, CUST_ALTERNATE, CUST_CHASE, CUST_TWINKLE, CUST_FADE, CUST_COUNT };
enum { PIX_WS281X, PIX_WS2801, PIX_APA102, PIX_COUNT };
#define MAX_PLAYLIST 8

typedef struct {
  uint16_t universe;     // start universe (1-based)
  uint16_t channel;      // start channel within universe (1-based)
  uint16_t pixels;       // pixels connected
  uint16_t nulls;        // null pixels at the start of the string
  uint16_t zigzag;       // 0 = off, else group size
  uint8_t  bright;       // intensity limit 0..100 %
  uint8_t  reverse;      // 1 = reversed
  uint8_t  order;        // ORD_* (color order of the pixel chips)
  uint8_t  zone_fx;      // ZONE_FOLLOW, an FX_* id, or ZONE_DARK
  uint8_t  pad[2];
} out_cfg_t;

typedef struct {
  uint32_t magic;
  uint32_t version;
  char     name[32];
  uint8_t  dhcp;
  uint8_t  fx_bright;    // built-in effect brightness % (0 = treat as 100)
  uint8_t  pad0[2];
  uint8_t  ip[4], mask[4], gw[4];
  uint16_t univ_size;    // channels per universe used for mapping (default 510)
  uint8_t  test_mode;    // 0 off, 1 solid, 2 rainbow, 3 chase, 4 identify
  uint8_t  test_r, test_g, test_b;
  uint8_t  hold_last;    // keep last frame when data stops (1) or blank (0)
  uint8_t  fx;           // built-in effect when no show data (0 = off), FX_*
  uint8_t  fx_speed;     // 1..10 (0 = treat as 5)
  uint8_t  out_map[NUM_OUT];  // logical output -> physical line index (0..47)
  out_cfg_t out[NUM_OUT];
  // ---- version 2 additions (everything below is new in 0.4.0) ----
  uint8_t  power_limit_a;   // amps for the whole controller, 0 = no limit
  uint8_t  ma_per_ch;       // mA drawn by one LED colour at full brightness
  uint8_t  fan_on_c;        // fan on at/above this temperature (C)
  uint8_t  shutdown_c;      // outputs off at/above this temperature (C), 0 = never
  // time & schedule
  int16_t  tz_min;          // offset from UTC in minutes (e.g. -300 for US Eastern)
  uint8_t  dst_us;          // apply US daylight saving rules
  uint8_t  sched_on;        // schedule enabled
  int32_t  lat_e4, lon_e4;  // location for sunset (degrees x 10000)
  uint8_t  on_sunset;       // 1 = turn on at sunset + on_offset, 0 = at on_min
  uint8_t  sched_days;      // bit0 = Sunday ... bit6 = Saturday
  int16_t  on_offset;       // minutes after sunset (can be negative)
  uint16_t on_min, off_min; // minute of the day (0..1439)
  // playlist
  uint8_t  pl_on, pl_count;
  uint8_t  pl_fx[MAX_PLAYLIST];
  uint8_t  pl_minutes[MAX_PLAYLIST];
  // custom colours effect
  uint8_t  cust_mode;       // CUST_*
  uint8_t  cust_n;          // number of colours used (1..4)
  uint8_t  cust_rgb[4][3];
  // outputs
  uint8_t  pix_type;        // PIX_* for all outputs
  uint8_t  dmx_on[2];       // RS-485 DMX outputs
  uint16_t dmx_univ[2];
  uint16_t spi_khz;         // clock speed for WS2801 / APA102
  uint8_t  reserved[32];
  uint32_t crc;
} config_t;

extern config_t cfg;
void config_defaults(config_t *c);
void config_load(void);
int  config_save(void);     // returns 1 on success
uint32_t crc32(const void *data, uint32_t len, uint32_t crc);
