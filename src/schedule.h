#pragma once
#include <stdint.h>
void sched_init(void);
void sched_task(void);
void sched_set_time(unsigned long utc);
int  sched_time_valid(void);
uint32_t sched_utc(void);
int32_t sched_local(void);     // seconds, local time incl. DST
int  sched_sunset_min(void);
int  sched_sunrise_min(void);
void sched_sun_changed(void);   // location / time zone changed
