#pragma once
#include <stdint.h>

#define NUM_OUT      48     // 3 expansion ports x 16
#define MAX_PIX      680    // per output (4 universes of 170)
#define MAX_UNIV     96     // universes we accept (1..96)

enum { ORD_RGB, ORD_RBG, ORD_GRB, ORD_GBR, ORD_BRG, ORD_BGR };
enum { PROTO_ANY = 0 };

typedef struct {
  uint16_t universe;     // start universe (1-based)
  uint16_t channel;      // start channel within universe (1-based)
  uint16_t pixels;       // pixels connected
  uint16_t nulls;        // null pixels at the start of the string
  uint16_t zigzag;       // 0 = off, else group size
  uint8_t  bright;       // intensity limit 0..100 %
  uint8_t  reverse;      // 1 = reversed
  uint8_t  order;        // ORD_* (color order of the pixel chips)
  uint8_t  pad[3];
} out_cfg_t;

typedef struct {
  uint32_t magic;
  uint32_t version;
  char     name[32];
  uint8_t  dhcp;
  uint8_t  pad0[3];
  uint8_t  ip[4], mask[4], gw[4];
  uint16_t univ_size;    // channels per universe used for mapping (default 510)
  uint8_t  test_mode;    // 0 off, 1 solid, 2 rainbow, 3 chase, 4 identify
  uint8_t  test_r, test_g, test_b;
  uint8_t  hold_last;    // keep last frame when data stops (1) or blank (0)
  uint8_t  pad1[2];
  uint8_t  out_map[NUM_OUT];  // logical output -> physical line index (0..47)
  out_cfg_t out[NUM_OUT];
  uint32_t crc;
} config_t;

extern config_t cfg;
void config_defaults(config_t *c);
void config_load(void);
int  config_save(void);     // returns 1 on success
uint32_t crc32(const void *data, uint32_t len, uint32_t crc);
