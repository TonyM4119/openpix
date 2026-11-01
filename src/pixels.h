#pragma once
#include <stdint.h>
#include "config.h"

#define FLAT_SIZE (MAX_UNIV * 512)
extern uint8_t flat[FLAT_SIZE];       // all received channel data, universe 1 at index 0

void pixels_init(void);
void pixels_universe_rx(uint16_t universe);  // called by protocol handlers
void pixels_push(void);                      // request immediate output (DDP push)
void pixels_task(void);                      // call from main loop
void pixels_config_changed(void);
uint32_t pixels_fps(void);
int pixels_fx_active(void);
uint32_t pixels_frames(void);
uint32_t pixels_last_data_ms(void);
extern uint32_t g_frame_ms;                  // ms spent with interrupts off for the last frame
