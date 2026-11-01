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
extern uint32_t g_frame_ms;
#define PREVIEW_N 32
extern uint8_t g_preview[NUM_OUT][PREVIEW_N][3];   // last frame, sampled, for the live preview
extern uint32_t g_power_ma, g_power_scale;
int fx_current(void);       // effect playing now (playlist aware)
int fx_scheduled_on(void);  // schedule says lights should be on                  // ms spent with interrupts off for the last frame
