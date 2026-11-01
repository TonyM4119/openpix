#pragma once
#include <stdint.h>
typedef struct {
  uint8_t ok, kind, fails;   // kind: 1 = SPI board format, 2 = long format
  uint8_t volt10;            // 0.1 V
  int16_t temp_c;
  uint8_t x, y;              // extra bytes in long format (fuse status?)
} sensor_port_t;
extern sensor_port_t g_sensors[3];
extern int g_fan_on, g_overtemp;
void sensors_init(void);
void sensors_task(void);
