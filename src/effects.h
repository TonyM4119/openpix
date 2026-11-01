#pragma once
#include <stdint.h>
#include "looks.h"
void fx_color(const look_t *look, int fx, int pal, int out, uint32_t k, uint32_t n, uint32_t now,
              uint8_t *r, uint8_t *g, uint8_t *b);
void look_main_color(uint8_t rgb[3]);
