// Clock (SNTP), US daylight saving, sunset, on/off schedule and playlist.
#include "board.h"
#include "schedule.h"
#include "config.h"
#include "pixels.h"
#include "lwip/apps/sntp.h"
#include "lwip/dns.h"
#include <math.h>

static uint32_t utc_at_sync, ms_at_sync;
static int synced;
static int sunset_day = -1, sunset_min = 18 * 60, sunrise_min = 7 * 60;

void sched_set_time(unsigned long utc) {   // called by lwIP SNTP (and by the web page)
  utc_at_sync = utc; ms_at_sync = millis(); synced = 1;
}
int sched_time_valid(void) { return synced; }
uint32_t sched_utc(void) { return utc_at_sync + (millis() - ms_at_sync) / 1000; }

// days since 1970-01-01 -> y/m/d (civil from days, H. Hinnant)
static void civil(int32_t z, int *y, int *m, int *d) {
  z += 719468;
  int32_t era = (z >= 0 ? z : z - 146096) / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t yy = (int32_t)yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = yy + (*m <= 2);
}
static int32_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  int32_t era = (y >= 0 ? y : y - 399) / 400;
  uint32_t yoe = (uint32_t)(y - era * 400);
  uint32_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int32_t)doe - 719468;
}
static int weekday(int32_t days) { return (int)((days + 4) % 7 + 7) % 7; }  // 0 = Sunday

// US DST: 2nd Sunday of March 2:00 local standard -> 1st Sunday of November 2:00 local daylight
static int is_dst(uint32_t utc) {
  if (!cfg.dst_us) return 0;
  int32_t loc = (int32_t)utc + cfg.tz_min * 60;
  int y, m, d; civil(loc / 86400, &y, &m, &d);
  int32_t mar1 = days_from_civil(y, 3, 1), nov1 = days_from_civil(y, 11, 1);
  int32_t start = mar1 + (7 - weekday(mar1)) % 7 + 7;  // 2nd Sunday
  int32_t end = nov1 + (7 - weekday(nov1)) % 7;        // 1st Sunday
  int32_t s = start * 86400 + 2 * 3600, e = end * 86400 + 1 * 3600;  // end: 2:00 DST = 1:00 standard
  return loc >= s && loc < e;
}

int32_t sched_local(void) {
  uint32_t u = sched_utc();
  return (int32_t)u + cfg.tz_min * 60 + (is_dst(u) ? 3600 : 0);
}

// NOAA sunrise/sunset approximation, result in local minutes of the day.
static void calc_sun(int32_t day_local) {
  int y, m, d; civil(day_local, &y, &m, &d);
  int n = day_local - days_from_civil(y, 1, 1) + 1;
  float lat = cfg.lat_e4 / 10000.0f, lon = cfg.lon_e4 / 10000.0f;
  float g = 2.0f * (float)M_PI / 365.0f * (n - 1);
  float eqt = 229.18f * (0.000075f + 0.001868f * cosf(g) - 0.032077f * sinf(g) - 0.014615f * cosf(2 * g) - 0.040849f * sinf(2 * g));
  float decl = 0.006918f - 0.399912f * cosf(g) + 0.070257f * sinf(g) - 0.006758f * cosf(2 * g) +
               0.000907f * sinf(2 * g) - 0.002697f * cosf(3 * g) + 0.00148f * sinf(3 * g);
  float la = lat * (float)M_PI / 180.0f;
  float c = cosf(90.833f * (float)M_PI / 180.0f) / (cosf(la) * cosf(decl)) - tanf(la) * tanf(decl);
  if (c > 1) c = 1;
  if (c < -1) c = -1;
  float ha = acosf(c) * 180.0f / (float)M_PI;
  int off = cfg.tz_min + (is_dst(sched_utc()) ? 60 : 0);
  float noon = 720 - 4 * lon - eqt + off;
  sunrise_min = (int)(noon - 4 * ha);
  sunset_min = (int)(noon + 4 * ha);
  sunset_day = day_local;
}

int sched_sunset_min(void) { return sunset_min; }
int sched_sunrise_min(void) { return sunrise_min; }

static int lights_on_now(void) {
  if (!cfg.sched_on) return 1;
  if (!synced) return 1;                 // no clock yet: fail on
  int32_t loc = sched_local();
  int32_t day = loc / 86400;
  int mins = (loc % 86400) / 60;
  if (day != sunset_day) calc_sun(day);
  int on = cfg.on_sunset ? sunset_min + cfg.on_offset : cfg.on_min;
  int off = cfg.off_min;
  if (on < 0) on += 1440;
  if (on >= 1440) on -= 1440;
  int today = (cfg.sched_days >> weekday(day)) & 1;
  int yesterday = (cfg.sched_days >> weekday(day - 1)) & 1;
  if (on < off) return today && mins >= on && mins < off;
  // crosses midnight
  return (today && mins >= on) || (yesterday && mins < off);
}

static int on_cache = 1;
int fx_scheduled_on(void) { return on_cache; }

int fx_current(void) {
  if (cfg.pl_on && cfg.pl_count) {
    uint32_t total = 0;
    for (int i = 0; i < cfg.pl_count; i++) total += cfg.pl_minutes[i] ? cfg.pl_minutes[i] : 1;
    uint32_t pos = (millis() / 60000u) % total;
    for (int i = 0; i < cfg.pl_count; i++) {
      uint32_t m = cfg.pl_minutes[i] ? cfg.pl_minutes[i] : 1;
      if (pos < m) return cfg.pl_fx[i] < FX_COUNT ? cfg.pl_fx[i] : FX_CHRISTMAS;
      pos -= m;
    }
  }
  return cfg.fx;
}

void sched_init(void) {
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, "pool.ntp.org");
  sntp_setservername(1, "time.google.com");
  sntp_init();
}

void sched_task(void) {
  static uint32_t t;
  if ((uint32_t)(millis() - t) < 1000) return;
  t = millis();
  on_cache = lights_on_now();
}
