// MQTT client (WLED-style topics) for Home Assistant / Node-RED / other systems.
//   <topic>        "ON" | "OFF" | "T" (toggle) | 0..255 brightness
//   <topic>/col    "#RRGGBB"  -> solid colour
//   <topic>/api    WLED JSON state, e.g. {"seg":[{"fx":16}]}
// Published (retained): <topic>/g brightness, <topic>/c colour, <topic>/status online/offline
#include "board.h"
#include "mqttc.h"
#include "looks.h"
#include "config.h"
#include "wled.h"
#include "effects.h"
#include "lwip/apps/mqtt.h"
#include "lwip/netif.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern struct netif g_netif;
static mqtt_client_t *cl;
static char topic_in[64];
static char payload[600];
static int plen, connecting;
static uint32_t retry_at, pub_at;
static uint8_t last_on = 0xFF, last_bri, last_col[3];
int g_mqtt_connected;
volatile int g_mqtt_reset;

static void t(char *o, int n, const char *suf) { snprintf(o, n, "%s%s", ex.mqtt_topic, suf); }

static void incoming_pub(void *arg, const char *topic, u32_t tot_len) {
  (void)arg; (void)tot_len;
  strncpy(topic_in, topic, sizeof(topic_in) - 1);
  plen = 0;
}

static void handle(void) {
  payload[plen] = 0;
  const char *suf = topic_in + strlen(ex.mqtt_topic);
  if (strncmp(topic_in, ex.mqtt_topic, strlen(ex.mqtt_topic)) != 0) return;
  int changed = 0;
  if (*suf == 0) {
    if (!strcmp(payload, "ON")) { cfg.power_off = 0; if (cfg.fx == FX_OFF) cfg.fx = FX_TWINKLE; changed = 1; }
    else if (!strcmp(payload, "OFF")) { cfg.power_off = 1; changed = 1; }
    else if (!strcmp(payload, "T")) { cfg.power_off = !cfg.power_off; changed = 1; }
    else if (payload[0] >= '0' && payload[0] <= '9') {
      int b = atoi(payload);
      if (b <= 0) cfg.power_off = 1;
      else { cfg.power_off = 0; cfg.fx_bright = b >= 255 ? 100 : b * 100 / 255 + 1; }
      changed = 1;
    }
  } else if (!strcmp(suf, "/col")) {
    const char *h = payload[0] == '#' ? payload + 1 : payload;
    unsigned long x = strtoul(h, 0, 16);
    cfg.solid_rgb[0] = x >> 16; cfg.solid_rgb[1] = x >> 8; cfg.solid_rgb[2] = x;
    cfg.fx = FX_SOLID; cfg.power_off = 0; cfg.pl_on = 0;
    changed = 1;
  } else if (!strcmp(suf, "/api")) {
    wled_post_state(payload);
    return;
  }
  if (changed) { uint8_t tm = cfg.test_mode; cfg.test_mode = 0; config_save(); cfg.test_mode = tm; }
}

static void incoming_data(void *arg, const u8_t *data, u16_t len, u8_t flags) {
  (void)arg;
  int n = len;
  if (plen + n > (int)sizeof(payload) - 1) n = sizeof(payload) - 1 - plen;
  if (n > 0) { memcpy(&payload[plen], data, n); plen += n; }
  if (flags & MQTT_DATA_FLAG_LAST) handle();
}

static void sub_cb(void *arg, err_t err) { (void)arg; (void)err; }

static void conn_cb(mqtt_client_t *c, void *arg, mqtt_connection_status_t st) {
  (void)arg;
  connecting = 0;
  if (st != MQTT_CONNECT_ACCEPTED) { g_mqtt_connected = 0; retry_at = millis() + 10000; return; }
  g_mqtt_connected = 1;
  char tp[64];
  mqtt_set_inpub_callback(c, incoming_pub, incoming_data, 0);
  t(tp, sizeof tp, ""); mqtt_subscribe(c, tp, 0, sub_cb, 0);
  t(tp, sizeof tp, "/col"); mqtt_subscribe(c, tp, 0, sub_cb, 0);
  t(tp, sizeof tp, "/api"); mqtt_subscribe(c, tp, 0, sub_cb, 0);
  t(tp, sizeof tp, "/status"); mqtt_publish(c, tp, "online", 6, 0, 1, 0, 0);
  last_on = 0xFF;   // force a status publish
}

void mqttc_task(void) {
  uint32_t now = millis();
  if (!ex.mqtt_on || ip4_addr_isany_val(*netif_ip4_addr(&g_netif)) || !ex.mqtt_ip[0]) {
    if (cl && mqtt_client_is_connected(cl)) mqtt_disconnect(cl);
    g_mqtt_connected = 0;
    return;
  }
  if (!cl) { cl = mqtt_client_new(); if (!cl) return; }
  if (g_mqtt_reset) { g_mqtt_reset = 0; if (mqtt_client_is_connected(cl)) mqtt_disconnect(cl); connecting = 0; retry_at = now + 500; return; }
  if (!mqtt_client_is_connected(cl)) {
    g_mqtt_connected = 0;
    if (connecting || (int32_t)(now - retry_at) < 0) return;
    static char cid[24], will[64];
    uint8_t m[6]; device_mac(m);
    snprintf(cid, sizeof cid, "openpix-%02x%02x%02x", m[3], m[4], m[5]);
    t(will, sizeof will, "/status");
    struct mqtt_connect_client_info_t ci;
    memset(&ci, 0, sizeof ci);
    ci.client_id = cid;
    ci.client_user = ex.mqtt_user[0] ? ex.mqtt_user : 0;
    ci.client_pass = ex.mqtt_pass[0] ? ex.mqtt_pass : 0;
    ci.keep_alive = 60;
    ci.will_topic = will; ci.will_msg = "offline"; ci.will_qos = 0; ci.will_retain = 1;
    ip_addr_t ip;
    IP_ADDR4(&ip, ex.mqtt_ip[0], ex.mqtt_ip[1], ex.mqtt_ip[2], ex.mqtt_ip[3]);
    connecting = 1;
    retry_at = now + 15000;
    if (mqtt_client_connect(cl, &ip, ex.mqtt_port, conn_cb, 0, &ci) != ERR_OK) connecting = 0;
    return;
  }
  if ((uint32_t)(now - pub_at) < 500) return;
  pub_at = now;
  uint8_t on = !cfg.power_off && cfg.fx != FX_OFF, col[3];
  look_main_color(col);
  if (on == last_on && cfg.fx_bright == last_bri && !memcmp(col, last_col, 3)) return;
  last_on = on; last_bri = cfg.fx_bright; memcpy(last_col, col, 3);
  char tp[64], v[16];
  t(tp, sizeof tp, "/g"); int n = snprintf(v, sizeof v, "%d", on ? cfg.fx_bright * 255 / 100 : 0);
  mqtt_publish(cl, tp, v, n, 0, 1, 0, 0);
  t(tp, sizeof tp, "/c"); n = snprintf(v, sizeof v, "#%02X%02X%02X", col[0], col[1], col[2]);
  mqtt_publish(cl, tp, v, n, 0, 1, 0, 0);
}
