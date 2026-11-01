// OpenPix: open replacement firmware for the HolidayCoro AlphaPix Evolution.
#include "board.h"
#include "config.h"
#include "oled.h"
#include "pixels.h"
#include "net.h"
#include "http.h"
#include "proto.h"
#include "sensors.h"
#include "schedule.h"
#include <stdio.h>
#include <string.h>

static void ui_task(void) {
  static uint32_t t, page_t;
  static int page, prev_btn;
  uint32_t now = millis();
  int b = 0;
  for (int i = 0; i < 5; i++) b |= button_raw(i) << i;
  if (b && !prev_btn) { page ^= 1; page_t = now; t = 0; }
  prev_btn = b;
  if (page && (uint32_t)(now - page_t) > 30000) page = 0;
  if (t && (uint32_t)(now - t) < 1000) return;
  t = now ? now : 1;

  char l[24];
  oled_clear();
  snprintf(l, sizeof l, "%s %s", FW_NAME, FW_VERSION); oled_text(0, l); oled_invert_row(0);
  if (http_upload_active || http_install_pending()) {
    oled_text(2, "FIRMWARE UPDATE");
    oled_text(3, "IN PROGRESS...");
    oled_text(5, "DO NOT UNPLUG");
  } else if (!net_eth_up()) {
    oled_text(2, "NETWORK NOT READY");
    oled_text(3, "retrying...");
  } else if (page == 0) {
    snprintf(l, sizeof l, "IP %s", net_has_ip() ? net_ip_str() : (cfg.dhcp ? "(DHCP...)" : "-")); oled_text(1, l);
    int n = 0; uint32_t px = 0;
    for (int i = 0; i < NUM_OUT; i++) if (cfg.out[i].pixels) { n++; px += cfg.out[i].pixels; }
    snprintf(l, sizeof l, "%d outputs %lu px", n, (unsigned long)px); oled_text(3, l);
    uint32_t last = pixels_last_data_ms();
    static const char *fxn[FX_COUNT] = {"", "Christmas", "Candy cane", "Multicolor", "Halloween",
                                        "4th of July", "Twinkle", "Rainbow"};
    if (cfg.test_mode) snprintf(l, sizeof l, "TEST MODE %d", cfg.test_mode);
    else if (pixels_fx_active()) snprintf(l, sizeof l, "FX: %s", fxn[cfg.fx]);
    else if (!last) snprintf(l, sizeof l, "Waiting for data");
    else if (now - last < 2000) snprintf(l, sizeof l, "LIVE  %lu fps", (unsigned long)pixels_fps());
    else snprintf(l, sizeof l, "No data %lus", (unsigned long)((now - last) / 1000));
    oled_text(4, l);
    if (g_overtemp) snprintf(l, sizeof l, "OVER TEMP - OUTPUTS OFF");
    else if (g_sensors[0].ok) snprintf(l, sizeof l, "%d.%dV %d%c%s", g_sensors[0].volt10 / 10, g_sensors[0].volt10 % 10,
                                       cfg.units_c ? g_sensors[0].temp_c : g_sensors[0].temp_c * 9 / 5 + 32,
                                       cfg.units_c ? 'C' : 'F', g_fan_on ? " FAN" : "");
    else snprintf(l, sizeof l, "sACN %lu", (unsigned long)proto_pkts_sacn);
    oled_text(5, l);
    snprintf(l, sizeof l, "ArtNet %lu DDP %lu", (unsigned long)proto_pkts_art, (unsigned long)proto_pkts_ddp); oled_text(6, l);
  } else {
    uint8_t m[6]; device_mac(m);
    snprintf(l, sizeof l, "ID %08lu", (unsigned long)device_id()); oled_text(1, l);
    snprintf(l, sizeof l, "%02X%02X%02X%02X%02X%02X", m[0], m[1], m[2], m[3], m[4], m[5]); oled_text(2, l);
    snprintf(l, sizeof l, "Up %lus", (unsigned long)(now / 1000)); oled_text(3, l);
    snprintf(l, sizeof l, "Clock %s", g_hse_ok ? "XTAL" : "INTERNAL"); oled_text(4, l);
    snprintf(l, sizeof l, "Frame %lums", (unsigned long)g_frame_ms); oled_text(5, l);
  }
  oled_flush();
}

int main(void) {
  board_init();
  __enable_irq();
  oled_init();
  oled_text(0, FW_NAME " " FW_VERSION);
  oled_text(2, "Starting...");
  oled_flush();

  config_load();
  cfg.test_mode = 0;
  if (defaults_combo_held()) {
    config_defaults(&cfg);
    config_save();
    oled_clear();
    oled_text(2, "DEFAULTS LOADED");
    oled_text(3, "IP 10.0.0.80");
    oled_flush();
    delay_ms(2000);
  }

  pixels_init();
  sensors_init();
  net_init();
  http_init();
  sched_init();

  for (;;) {
    wdt_kick();
    net_task();
    http_task();
    if (!http_upload_active && !http_install_pending()) pixels_task();
    net_task();
    sensors_task();
    sched_task();
    ui_task();
  }
}
