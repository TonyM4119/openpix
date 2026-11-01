#pragma once
#include <stdint.h>
extern uint8_t dmx_buf[2][513];
extern uint32_t dmx_frames[2];
void dmx_init(void);
void dmx_task(void);
