// Small HTTP server on lwIP raw TCP. No login lock, no reboot on save.
//   GET  /                -> web UI
//   GET  /api/status      -> JSON live status
//   GET  /api/config      -> JSON settings
//   POST /api/config      -> form-encoded settings, applied and saved immediately
//   POST /api/test        -> test pattern (not saved; cleared on reboot)
//   POST /api/update      -> raw firmware .bin body (OpenPix or stock HolidayCoro)
//   POST /api/reboot
#include "board.h"
#include "http.h"
#include "config.h"
#include "pixels.h"
#include "proto.h"
#include "flash.h"
#include "eth.h"
#include "net.h"
#include "web_index.h"
#include "sensors.h"
#include "wled.h"
#include "schedule.h"
#include "lwip/tcp.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_CONN 3
#define HDR_MAX  1536
#define BODY_MAX 4096
#define OUT_MAX  4096

typedef struct {
  struct tcp_pcb *pcb;
  int used, state;            // 0 headers, 1 body, 2 responding
  int hlen;
  char method[8], path[48];
  uint32_t clen, got;
  int is_upload, upload_ok;
  int clen_chunk; uint32_t wr;
  const uint8_t *tx; uint32_t tx_left; int tx_copy;
  int close_after;
  uint32_t last_ms;
  // big buffers last (not cleared on accept)
  char hdr[HDR_MAX];
  uint8_t chunk[512];
  char body[BODY_MAX];
  char out[OUT_MAX];
} conn_t;

static conn_t conns[MAX_CONN];
volatile int http_upload_active;
static uint32_t install_len, install_at;
static uint32_t reboot_at;

// ---------- helpers ----------
static int url_decode(char *s) {
  char *o = s, *i = s;
  while (*i) {
    if (*i == '+') { *o++ = ' '; i++; }
    else if (*i == '%' && i[1] && i[2]) { char h[3] = {i[1], i[2], 0}; *o++ = (char)strtol(h, 0, 16); i += 3; }
    else *o++ = *i++;
  }
  *o = 0;
  return o - s;
}

// iterate key=value pairs; returns 0 at end
static int next_kv(char **p, char **k, char **v) {
  char *s = *p;
  if (!s || !*s) return 0;
  char *amp = strchr(s, '&');
  if (amp) { *amp = 0; *p = amp + 1; } else *p = s + strlen(s);
  char *eq = strchr(s, '=');
  if (eq) { *eq = 0; *v = eq + 1; } else *v = s + strlen(s);
  *k = s;
  url_decode(*k); url_decode(*v);
  return 1;
}

static int parse_ip(const char *s, uint8_t ip[4]) {
  unsigned a, b, c, d;
  if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return 0;
  ip[0] = a; ip[1] = b; ip[2] = c; ip[3] = d;
  return 1;
}

static void json_str(char **w, char *end, const char *s) {
  char *o = *w;
  if (o < end) *o++ = '"';
  while (*s && o < end - 2) {
    if (*s == '"' || *s == '\\') *o++ = '\\';
    *o++ = (*s >= 32) ? *s : ' ';
    s++;
  }
  if (o < end) *o++ = '"';
  *w = o;
}

#define AP(...) do { int _n = snprintf(w, end - w, __VA_ARGS__); if (_n > 0) w += (_n < end - w ? _n : end - w - 1); } while (0)

static int build_status(char *buf, int max) {
  char *w = buf, *end = buf + max;
  uint8_t mac[6]; device_mac(mac);
  uint32_t age = pixels_last_data_ms() ? millis() - pixels_last_data_ms() : 0xFFFFFFFF;
  AP("{\"fw\":\"%s %s\",\"name\":", FW_NAME, FW_VERSION);
  json_str(&w, end, cfg.name);
  AP(",\"ip\":\"%s\",\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"id\":\"%08lu\"",
     net_ip_str(), mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], (unsigned long)device_id());
  AP(",\"up\":%lu,\"fps\":%lu,\"frames\":%lu,\"frame_ms\":%lu,\"age\":%ld",
     (unsigned long)(millis() / 1000), (unsigned long)pixels_fps(), (unsigned long)pixels_frames(),
     (unsigned long)g_frame_ms, age == 0xFFFFFFFF ? -1L : (long)age);
  AP(",\"sacn\":%lu,\"art\":%lu,\"ddp\":%lu,\"rx\":%lu,\"tx\":%lu,\"err\":%lu,\"hse\":%d,\"test\":%d}",
     (unsigned long)proto_pkts_sacn, (unsigned long)proto_pkts_art, (unsigned long)proto_pkts_ddp,
     (unsigned long)eth_rx_count, (unsigned long)eth_tx_count, (unsigned long)eth_err_count,
     g_hse_ok, cfg.test_mode);
  w--;  // reopen object
  AP(",\"pwr\":%d,\"fx\":%u,\"fxnow\":%d,\"fxon\":%d,\"schedon\":%d", !cfg.power_off, cfg.fx, fx_current(), pixels_fx_active(), fx_scheduled_on());
  AP(",\"power_ma\":%lu,\"limited\":%d,\"fan\":%d,\"hot\":%d,\"exp\":[", (unsigned long)g_power_ma,
     g_power_scale < 256, g_fan_on, g_overtemp);
  for (int p = 0; p < 3; p++)
    AP("%s{\"ok\":%u,\"v\":%u,\"t\":%d}", p ? "," : "", g_sensors[p].ok, g_sensors[p].volt10, g_sensors[p].temp_c);
  AP("],\"time\":%d", sched_time_valid());
  if (sched_time_valid()) {
    int32_t loc = sched_local();
    AP(",\"clock\":\"%02ld:%02ld\",\"sunset\":\"%02d:%02d\"", (long)((loc % 86400) / 3600), (long)((loc % 3600) / 60),
       sched_sunset_min() / 60, sched_sunset_min() % 60);
  }
  AP("}");
  return w - buf;
}

static int build_config(char *buf, int max) {
  char *w = buf, *end = buf + max;
  AP("{\"name\":");
  json_str(&w, end, cfg.name);
  AP(",\"dhcp\":%d,\"ip\":\"%u.%u.%u.%u\",\"mask\":\"%u.%u.%u.%u\",\"gw\":\"%u.%u.%u.%u\"",
     cfg.dhcp, cfg.ip[0], cfg.ip[1], cfg.ip[2], cfg.ip[3], cfg.mask[0], cfg.mask[1], cfg.mask[2],
     cfg.mask[3], cfg.gw[0], cfg.gw[1], cfg.gw[2], cfg.gw[3]);
  AP(",\"fx\":%u,\"fxs\":%u,\"fxb\":%u", cfg.fx, cfg.fx_speed, cfg.fx_bright);
  AP(",\"pwr\":%d,\"sol\":\"%02x%02x%02x\"", !cfg.power_off, cfg.solid_rgb[0], cfg.solid_rgb[1], cfg.solid_rgb[2]);
  AP(",\"pla\":%u,\"mapc\":%u,\"fan\":%u,\"sdc\":%u", cfg.power_limit_a, cfg.ma_per_ch, cfg.fan_on_c, cfg.shutdown_c);
  AP(",\"tz\":%d,\"dst\":%u,\"sch\":%u,\"lat\":%ld,\"lon\":%ld,\"osun\":%u,\"sdays\":%u,\"onoff\":%d,\"onm\":%u,\"offm\":%u",
     cfg.tz_min, cfg.dst_us, cfg.sched_on, (long)cfg.lat_e4, (long)cfg.lon_e4, cfg.on_sunset, cfg.sched_days,
     cfg.on_offset, cfg.on_min, cfg.off_min);
  AP(",\"plon\":%u,\"pl\":[", cfg.pl_on);
  for (int i = 0; i < cfg.pl_count; i++) AP("%s[%u,%u]", i ? "," : "", cfg.pl_fx[i], cfg.pl_minutes[i]);
  AP("],\"cm\":%u,\"cust\":[", cfg.cust_mode);
  for (int i = 0; i < cfg.cust_n; i++) AP("%s\"%02x%02x%02x\"", i ? "," : "", cfg.cust_rgb[i][0], cfg.cust_rgb[i][1], cfg.cust_rgb[i][2]);
  AP("],\"ptype\":%u,\"skhz\":%u,\"dmx\":[[%u,%u],[%u,%u]]", cfg.pix_type, cfg.spi_khz,
     cfg.dmx_on[0], cfg.dmx_univ[0], cfg.dmx_on[1], cfg.dmx_univ[1]);
  AP(",\"usz\":%u,\"hold\":%u,\"test\":%u,\"tr\":%u,\"tg\":%u,\"tb\":%u,\"out\":[",
     cfg.univ_size, cfg.hold_last, cfg.test_mode, cfg.test_r, cfg.test_g, cfg.test_b);
  for (int i = 0; i < NUM_OUT; i++) {
    out_cfg_t *o = &cfg.out[i];
    AP("%s[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]", i ? "," : "", o->universe, o->channel, o->pixels, o->nulls,
       o->zigzag, o->bright, o->reverse, o->order, cfg.out_map[i], o->zone_fx);
  }
  AP("]}");
  return w - buf;
}

static int apply_config_form(char *body) {
  char *p = body, *k, *v;
  int net_changed = 0;
  uint8_t ip[4];
  while (next_kv(&p, &k, &v)) {
    long n = strtol(v, 0, 10);
    if (!strcmp(k, "name")) { strncpy(cfg.name, v, sizeof(cfg.name) - 1); cfg.name[sizeof(cfg.name) - 1] = 0; }
    else if (!strcmp(k, "dhcp")) { if (cfg.dhcp != (n ? 1 : 0)) net_changed = 1; cfg.dhcp = n ? 1 : 0; }
    else if (!strcmp(k, "ip"))   { if (parse_ip(v, ip) && memcmp(ip, cfg.ip, 4)) { memcpy(cfg.ip, ip, 4); net_changed = 1; } }
    else if (!strcmp(k, "mask")) { if (parse_ip(v, ip) && memcmp(ip, cfg.mask, 4)) { memcpy(cfg.mask, ip, 4); net_changed = 1; } }
    else if (!strcmp(k, "gw"))   { if (parse_ip(v, ip) && memcmp(ip, cfg.gw, 4)) { memcpy(cfg.gw, ip, 4); net_changed = 1; } }
    else if (!strcmp(k, "usz"))  { if (n >= 3 && n <= 512) cfg.univ_size = n; }
    else if (!strcmp(k, "hold")) { cfg.hold_last = n ? 1 : 0; }
    else if (!strcmp(k, "fx"))   { if (n >= 0 && n < FX_COUNT) cfg.fx = n; }
    else if (!strcmp(k, "fxs"))  { if (n >= 1 && n <= 10) cfg.fx_speed = n; }
    else if (!strcmp(k, "fxb"))  { if (n >= 1 && n <= 100) cfg.fx_bright = n; }
    else if (!strcmp(k, "pwr"))  { cfg.power_off = n ? 0 : 1; }
    else if (!strcmp(k, "sol"))  { unsigned long x = strtoul(v[0] == '#' ? v + 1 : v, 0, 16);
                                   cfg.solid_rgb[0] = x >> 16; cfg.solid_rgb[1] = x >> 8; cfg.solid_rgb[2] = x; }
    else if (!strcmp(k, "pla"))  { if (n >= 0 && n <= 250) cfg.power_limit_a = n; }
    else if (!strcmp(k, "mapc")) { if (n >= 1 && n <= 60) cfg.ma_per_ch = n; }
    else if (!strcmp(k, "fan"))  { if (n >= 20 && n <= 80) cfg.fan_on_c = n; }
    else if (!strcmp(k, "sdc"))  { if (n == 0 || (n >= 30 && n <= 90)) cfg.shutdown_c = n; }
    else if (!strcmp(k, "tz"))   { if (n >= -720 && n <= 840) cfg.tz_min = n; }
    else if (!strcmp(k, "dst"))  { cfg.dst_us = n ? 1 : 0; }
    else if (!strcmp(k, "sch"))  { cfg.sched_on = n ? 1 : 0; }
    else if (!strcmp(k, "lat"))  { double f = strtod(v, 0); if (f >= -90 && f <= 90) cfg.lat_e4 = (int32_t)(f * 10000 + (f < 0 ? -0.5 : 0.5)); }
    else if (!strcmp(k, "lon"))  { double f = strtod(v, 0); if (f >= -180 && f <= 180) cfg.lon_e4 = (int32_t)(f * 10000 + (f < 0 ? -0.5 : 0.5)); }
    else if (!strcmp(k, "osun")) { cfg.on_sunset = n ? 1 : 0; }
    else if (!strcmp(k, "sdays")){ cfg.sched_days = n & 0x7F; }
    else if (!strcmp(k, "onoff")){ if (n >= -240 && n <= 240) cfg.on_offset = n; }
    else if (!strcmp(k, "onm"))  { if (n >= 0 && n < 1440) cfg.on_min = n; }
    else if (!strcmp(k, "offm")) { if (n >= 0 && n < 1440) cfg.off_min = n; }
    else if (!strcmp(k, "plon")) { cfg.pl_on = n ? 1 : 0; }
    else if (!strcmp(k, "pln"))  { if (n >= 0 && n <= MAX_PLAYLIST) cfg.pl_count = n; }
    else if (k[0] == 'p' && (k[1] == 'f' || k[1] == 'm') && k[2] >= '1' && k[2] <= '8' && !k[3]) {
      int i = k[2] - '1';
      if (k[1] == 'f') { if (n >= 1 && n < FX_COUNT) cfg.pl_fx[i] = n; }
      else if (n >= 1 && n <= 240) cfg.pl_minutes[i] = n;
    }
    else if (!strcmp(k, "cm"))   { if (n >= 0 && n < CUST_COUNT) cfg.cust_mode = n; }
    else if (!strcmp(k, "cn"))   { if (n >= 1 && n <= 4) cfg.cust_n = n; }
    else if (k[0] == 'c' && k[1] >= '1' && k[1] <= '4' && !k[2]) {
      const char *h = v[0] == '#' ? v + 1 : v;
      unsigned long x = strtoul(h, 0, 16);
      int i = k[1] - '1';
      cfg.cust_rgb[i][0] = x >> 16; cfg.cust_rgb[i][1] = x >> 8; cfg.cust_rgb[i][2] = x;
    }
    else if (!strcmp(k, "ptype")) { if (n >= 0 && n < PIX_COUNT) cfg.pix_type = n; }
    else if (!strcmp(k, "skhz")) { if (n >= 50 && n <= 2000) cfg.spi_khz = n; }
    else if (!strcmp(k, "dmx1")) { cfg.dmx_on[0] = n ? 1 : 0; }
    else if (!strcmp(k, "dmx2")) { cfg.dmx_on[1] = n ? 1 : 0; }
    else if (!strcmp(k, "du1"))  { if (n >= 1 && n <= MAX_UNIV) cfg.dmx_univ[0] = n; }
    else if (!strcmp(k, "du2"))  { if (n >= 1 && n <= MAX_UNIV) cfg.dmx_univ[1] = n; }
    else if (k[0] == 'f' && k[1] >= '0' && k[1] <= '9') {
      int i = atoi(k + 1) - 1;
      if (i >= 0 && i < NUM_OUT && (n == ZONE_DARK || (n >= 0 && n < FX_COUNT))) cfg.out[i].zone_fx = n;
    }
    else if (k[0] && strchr("ucpnzbrom", k[0]) && k[1] >= '0' && k[1] <= '9') {
      int i = atoi(k + 1) - 1;
      if (i < 0 || i >= NUM_OUT) continue;
      out_cfg_t *o = &cfg.out[i];
      if (n < 0) n = 0;
      switch (k[0]) {
        case 'u': if (n <= 63999) o->universe = n; break;
        case 'c': if (n >= 1 && n <= 512) o->channel = n; break;
        case 'p': o->pixels = n > MAX_PIX ? MAX_PIX : n; break;
        case 'n': o->nulls = n > 100 ? 100 : n; break;
        case 'z': o->zigzag = n > MAX_PIX ? 0 : n; break;
        case 'b': o->bright = n > 100 ? 100 : n; break;
        case 'r': o->reverse = n ? 1 : 0; break;
        case 'o': if (n <= ORD_BGR) o->order = n; break;
        case 'm': if (n >= 1 && n <= NUM_OUT) cfg.out_map[i] = n - 1; break;
      }
    }
  }
  pixels_config_changed();
  net_groups_changed();
  sched_sun_changed();
  uint8_t saved_test = cfg.test_mode;
  cfg.test_mode = 0;              // test patterns are never saved
  int ok = config_save();
  cfg.test_mode = saved_test;
  if (net_changed) net_apply_later(800);
  return ok;
}

static void apply_test_form(char *body) {
  char *p = body, *k, *v;
  while (next_kv(&p, &k, &v)) {
    long n = strtol(v, 0, 10);
    if (!strcmp(k, "mode")) cfg.test_mode = (n >= 0 && n <= 4) ? n : 0;
    else if (!strcmp(k, "r")) cfg.test_r = n;
    else if (!strcmp(k, "g")) cfg.test_g = n;
    else if (!strcmp(k, "b")) cfg.test_b = n;
  }
}

// ---------- TCP plumbing ----------
static void conn_free(conn_t *c) {
  if (c->pcb) {
    tcp_arg(c->pcb, 0); tcp_recv(c->pcb, 0); tcp_sent(c->pcb, 0); tcp_err(c->pcb, 0); tcp_poll(c->pcb, 0, 0);
    if (tcp_close(c->pcb) != ERR_OK) tcp_abort(c->pcb);
  }
  if (c->is_upload) http_upload_active = 0;
  c->pcb = 0; c->used = 0;
}

static void pump(conn_t *c) {
  while (c->tx_left) {
    u16_t room = tcp_sndbuf(c->pcb);
    if (!room) break;
    u16_t n = c->tx_left > room ? room : (u16_t)c->tx_left;
    if (tcp_write(c->pcb, c->tx, n, c->tx_copy ? TCP_WRITE_FLAG_COPY : 0) != ERR_OK) break;
    c->tx += n; c->tx_left -= n;
  }
  tcp_output(c->pcb);
}

static void respond(conn_t *c, const char *status, const char *ctype, const void *body, uint32_t len,
                    int gz, int copy_body) {
  int h = snprintf(c->hdr, sizeof(c->hdr),
                   "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n%s"
                   "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                   status, ctype, (unsigned long)len, gz ? "Content-Encoding: gzip\r\n" : "");
  tcp_write(c->pcb, c->hdr, h, TCP_WRITE_FLAG_COPY);
  c->tx = body; c->tx_left = len; c->tx_copy = copy_body;
  c->state = 2; c->close_after = 1;
  pump(c);
}

static void respond_json(conn_t *c, int n) { respond(c, "200 OK", "application/json", c->out, n, 0, 1); }
static void respond_text(conn_t *c, const char *st, const char *msg) {
  int n = snprintf(c->out, sizeof(c->out), "%s", msg);
  respond(c, st, "text/plain", c->out, n, 0, 1);
}

static void upload_flush(conn_t *c) {
  if (!c->clen_chunk) return;
  while (c->clen_chunk & 3) c->chunk[c->clen_chunk++] = 0xFF;
  if (c->upload_ok && !flash_program(STAGE_BASE + c->wr, c->chunk, c->clen_chunk)) c->upload_ok = 0;
  c->wr += c->clen_chunk;
  c->clen_chunk = 0;
}

static void upload_data(conn_t *c, const uint8_t *d, uint32_t n) {
  while (n) {
    uint32_t k = sizeof(c->chunk) - c->clen_chunk;
    if (k > n) k = n;
    memcpy(&c->chunk[c->clen_chunk], d, k);
    c->clen_chunk += k; d += k; n -= k;
    if (c->clen_chunk == (int)sizeof(c->chunk)) upload_flush(c);
  }
}

static void upload_finish(conn_t *c) {
  upload_flush(c);
  const uint32_t *v = (const uint32_t *)STAGE_BASE;
  uint32_t sp = v[0], rv = v[1];
  int vt_ok = sp >= 0x20000000u && sp <= 0x20070000u && (rv & 1) &&
              rv >= APP_BASE + 8 && rv < APP_BASE + c->clen;
  if (!c->upload_ok) { respond_text(c, "500 Error", "Flash write failed. Old firmware still installed."); return; }
  if (!vt_ok) { respond_text(c, "400 Bad", "That file does not look like firmware for this board. Nothing was changed."); return; }
  respond_text(c, "200 OK", "Firmware received and verified. Installing and restarting...");
  install_len = c->clen;
  install_at = millis() + 1200;
}

static void handle_request(conn_t *c) {
  if (!strcmp(c->method, "GET")) {
    if (!strcmp(c->path, "/") || !strcmp(c->path, "/index.html")) {
      respond(c, "200 OK", "text/html; charset=utf-8", web_index_html_gz, sizeof(web_index_html_gz), 1, 0);
    } else if (!strcmp(c->path, "/icon.png") || !strcmp(c->path, "/apple-touch-icon.png") ||
               !strcmp(c->path, "/favicon.ico")) {
      respond(c, "200 OK", "image/png", web_icon_png, sizeof(web_icon_png), 0, 0);
    } else if (!strcmp(c->path, "/manifest.json")) {
      respond(c, "200 OK", "application/manifest+json", web_manifest_json, sizeof(web_manifest_json), 0, 0);
    } else if (!strcmp(c->path, "/api/status")) {
      respond_json(c, build_status(c->out, sizeof(c->out)));
    } else if (!strcmp(c->path, "/api/config")) {
      respond_json(c, build_config(c->out, sizeof(c->out)));
    } else {
      int n = wled_get(c->path, c->out, sizeof(c->out));
      if (n >= 0) respond_json(c, n);
      else respond_text(c, "404 Not Found", "Not found");
    }
    return;
  }
  if (!strcmp(c->method, "POST")) {
    c->body[c->got < BODY_MAX ? c->got : BODY_MAX - 1] = 0;
    if (!strcmp(c->path, "/api/config")) {
      int ok = apply_config_form(c->body);
      if (ok) respond_json(c, build_config(c->out, sizeof(c->out)));
      else respond_text(c, "500 Error", "Could not save settings to flash");
    } else if (!strcmp(c->path, "/api/test")) {
      apply_test_form(c->body);
      respond_json(c, build_status(c->out, sizeof(c->out)));
    } else if (!strcmp(c->path, "/json/state") || !strcmp(c->path, "/json/si") || !strcmp(c->path, "/json")) {
      wled_post_state(c->body);
      if (strstr(c->body, "\"v\":true")) respond_json(c, wled_get("/json/state", c->out, sizeof(c->out)));
      else { int n = snprintf(c->out, sizeof(c->out), "{\"success\":true}"); respond_json(c, n); }
    } else if (!strcmp(c->path, "/api/time")) {
      char *p = c->body, *k, *v;
      while (next_kv(&p, &k, &v)) if (!strcmp(k, "utc")) { unsigned long u = strtoul(v, 0, 10); if (u > 1700000000UL) sched_set_time(u); }
      respond_json(c, build_status(c->out, sizeof(c->out)));
    } else if (!strcmp(c->path, "/api/reboot")) {
      respond_text(c, "200 OK", "Rebooting");
      reboot_at = millis() + 800;
    } else if (!strcmp(c->path, "/api/update")) {
      upload_finish(c);
    } else respond_text(c, "404 Not Found", "Not found");
    return;
  }
  respond_text(c, "405 Method Not Allowed", "Method not allowed");
}

static int parse_headers(conn_t *c) {
  char *e = strstr(c->hdr, "\r\n\r\n");
  if (!e) return 0;
  *e = 0;
  int hl = (e + 4) - c->hdr;
  if (sscanf(c->hdr, "%7s %47s", c->method, c->path) != 2) return -1;
  char *q = strchr(c->path, '?'); if (q) *q = 0;
  c->clen = 0;
  for (char *l = strstr(c->hdr, "\r\n"); l; l = strstr(l + 2, "\r\n")) {
    if (!strncasecmp(l + 2, "content-length:", 15)) c->clen = strtoul(l + 17, 0, 10);
  }
  c->is_upload = !strcmp(c->method, "POST") && !strcmp(c->path, "/api/update");
  if (c->is_upload) {
    if (c->clen < 1024 || c->clen > APP_MAX || c->clen > STAGE_MAX) return -2;
    http_upload_active = 1;
    c->upload_ok = stage_erase();
    c->wr = 0; c->clen_chunk = 0;
  }
  return hl;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
  conn_t *c = arg;
  if (!p || err != ERR_OK) { if (p) pbuf_free(p); conn_free(c); return ERR_OK; }
  tcp_recved(pcb, p->tot_len);
  c->last_ms = millis();
  if (c->state == 2) { pbuf_free(p); return ERR_OK; }
  for (struct pbuf *q = p; q; q = q->next) {
    const uint8_t *d = q->payload; uint32_t n = q->len;
    if (c->state == 0) {
      uint32_t room = HDR_MAX - 1 - c->hlen;
      uint32_t k = n > room ? room : n;
      memcpy(&c->hdr[c->hlen], d, k); c->hlen += k; c->hdr[c->hlen] = 0;
      int hl = parse_headers(c);
      if (hl == 0) { if (c->hlen >= HDR_MAX - 1) { respond_text(c, "431 Too Large", "Header too large"); break; } continue; }
      if (hl < 0) { respond_text(c, "400 Bad", hl == -2 ? "Bad firmware size" : "Bad request"); break; }
      c->state = 1; c->got = 0;
      // body bytes already in hdr buffer
      uint32_t extra = c->hlen - hl;
      const uint8_t *eb = (const uint8_t *)&c->hdr[hl];
      if (extra) {
        if (c->is_upload) upload_data(c, eb, extra);
        else { uint32_t m = extra; if (c->got + m > BODY_MAX - 1) m = BODY_MAX - 1 - c->got; memmove(&c->body[c->got], eb, m); }
        c->got += extra;
      }
      // remaining bytes of this pbuf beyond what fit in hdr
      if (k < n) {
        d += k; n -= k;
        if (c->is_upload) upload_data(c, d, n);
        else { uint32_t m = n; if (c->got + m > BODY_MAX - 1) m = BODY_MAX - 1 - c->got; memcpy(&c->body[c->got], d, m); }
        c->got += n;
      }
    } else if (c->state == 1) {
      if (c->is_upload) upload_data(c, d, n);
      else { uint32_t m = n; if (c->got + m > BODY_MAX - 1) m = BODY_MAX - 1 - c->got; memcpy(&c->body[c->got], d, m); }
      c->got += n;
    }
    if (c->state == 1 && c->got >= c->clen) { handle_request(c); break; }
  }
  pbuf_free(p);
  return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len) {
  (void)pcb; (void)len;
  conn_t *c = arg;
  if (c->tx_left) pump(c);
  else if (c->state == 2 && c->close_after && tcp_sndqueuelen(c->pcb) == 0) conn_free(c);
  return ERR_OK;
}

static void on_err(void *arg, err_t err) {
  (void)err;
  conn_t *c = arg;
  if (!c) return;
  if (c->is_upload) http_upload_active = 0;
  c->pcb = 0; c->used = 0;
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb) {
  (void)pcb;
  conn_t *c = arg;
  if (!c) return ERR_OK;
  if (c->state == 2) { if (c->tx_left) pump(c); else conn_free(c); }
  else if (!c->is_upload || (uint32_t)(millis() - c->last_ms) > 30000) conn_free(c);  // idle too long
  return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
  (void)arg;
  if (err != ERR_OK) return err;
  conn_t *c = 0;
  for (int i = 0; i < MAX_CONN; i++) if (!conns[i].used) { c = &conns[i]; break; }
  if (!c) { tcp_abort(pcb); return ERR_ABRT; }
  memset(c, 0, offsetof(conn_t, hdr));
  c->hdr[0] = 0;
  c->used = 1; c->pcb = pcb; c->last_ms = millis();
  tcp_arg(pcb, c);
  tcp_recv(pcb, on_recv);
  tcp_sent(pcb, on_sent);
  tcp_err(pcb, on_err);
  tcp_poll(pcb, on_poll, 20);   // ~10 s idle timeout
  tcp_setprio(pcb, TCP_PRIO_MIN);
  return ERR_OK;
}

void http_init(void) {
  struct tcp_pcb *l = tcp_new();
  tcp_bind(l, IP_ADDR_ANY, 80);
  l = tcp_listen(l);
  tcp_accept(l, on_accept);
}

void http_task(void) {
  uint32_t now = millis();
  if (install_len && (int32_t)(now - install_at) >= 0) flash_install_staged(install_len);
  if (reboot_at && (int32_t)(now - reboot_at) >= 0) NVIC_SystemReset();
}

int http_install_pending(void) { return install_len != 0; }
