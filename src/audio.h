#pragma once
#include <stdint.h>
// Network audio sync (WLED "Audio Sync" V2 packets, UDP multicast 239.0.0.1:11988)
void audio_init(void);
uint8_t audio_level(void);          // 0..255 smoothed volume, decays to 0 without packets
uint8_t audio_band(uint32_t band);  // 0..15 FFT bands, 0..255
int audio_active(void);
