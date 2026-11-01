// Alexa support without any cloud skill: the controller answers like a Philips
// Hue bridge with one dimmable light (the same approach WLED and fauxmoESP use).
// "Alexa, discover devices" -> finds e.g. "Christmas lights".
//   SSDP on 239.255.255.250:1900 -> LOCATION http://<ip>/description.xml
//   Hue REST: POST /api, GET /api/<user>[/lights[/1]], PUT /api/<user>/lights/1/state
#include "board.h"
#include "hue.h"
#include "looks.h"
#include "config.h"
#include "net.h"
#include "lwip/udp.h"
#include "lwip/igmp.h"
#include "lwip/netif.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern struct netif g_netif;
static struct udp_pcb *pcb;
static int joined;

static void machex(char *o) {   // 12 hex chars
  uint8_t m[6]; device_mac(m);
  sprintf(o, "%02x%02x%02x%02x%02x%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

static void ssdp_rx(void *arg, struct udp_pcb *p_, struct pbuf *p, const ip_addr_t *a, u16_t port) {
  (void)arg; (void)p_;
  static char b[512];
  uint16_t n = pbuf_copy_partial(p, b, sizeof(b) - 1, 0);
  pbuf_free(p);
  b[n] = 0;
  if (!ex.alexa_on || strncmp(b, "M-SEARCH", 8) != 0) return;
  if (!strstr(b, "ssdp:discover")) return;
  if (!(strstr(b, "upnp:rootdevice") || strstr(b, "ssdp:all") || strstr(b, "device:basic:1") || strstr(b, "device:Basic:1"))) return;
  char mh[13]; machex(mh);
  static char r[420];
  int len = snprintf(r, sizeof(r),
    "HTTP/1.1 200 OK\r\nEXT:\r\nCACHE-CONTROL: max-age=100\r\nLOCATION: http://%s:80/description.xml\r\n"
    "SERVER: FreeRTOS/6.0.5, UPnP/1.0, IpBridge/1.17.0\r\nhue-bridgeid: %.6sFFFE%s\r\n"
    "ST: urn:schemas-upnp-org:device:basic:1\r\nUSN: uuid:2f402f80-da50-11e1-9b23-%s::upnp:rootdevice\r\n\r\n",
    net_ip_str(), mh, mh + 6, mh);
  struct pbuf *q = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
  if (!q) return;
  memcpy(q->payload, r, len);
  udp_sendto(pcb, q, a, port);
  pbuf_free(q);
}

void hue_init(void) {
  pcb = udp_new();
  udp_bind(pcb, IP_ADDR_ANY, 1900);
  udp_recv(pcb, ssdp_rx, 0);
}

void hue_task(void) {
  if (ex.alexa_on == joined || ip4_addr_isany_val(*netif_ip4_addr(&g_netif))) return;
  ip4_addr_t g; IP4_ADDR(&g, 239, 255, 255, 250);
  if (ex.alexa_on) igmp_joingroup_netif(&g_netif, &g); else igmp_leavegroup_netif(&g_netif, &g);
  joined = ex.alexa_on;
}

#define AP(...) do { int _n = snprintf(w, end - w, __VA_ARGS__); if (_n > 0) w += (_n < end - w ? _n : end - w - 1); } while (0)

static int is_on(void) { return !cfg.power_off && cfg.fx != FX_OFF; }

static char *light(char *w, char *end) {
  char mh[13]; machex(mh);
  int bri = cfg.fx_bright * 254 / 100; if (bri < 1) bri = 1;
  AP("{\"state\":{\"on\":%s,\"bri\":%d,\"alert\":\"none\",\"mode\":\"homeautomation\",\"reachable\":true},"
     "\"swupdate\":{\"state\":\"noupdates\",\"lastinstall\":null},\"type\":\"Dimmable light\",\"name\":\"%s\","
     "\"modelid\":\"LWB010\",\"manufacturername\":\"Philips\",\"productname\":\"Hue white lamp\","
     "\"capabilities\":{\"certified\":true,\"control\":{\"mindimlevel\":5000,\"maxlumen\":806},\"streaming\":{\"renderer\":false,\"proxy\":false}},"
     "\"config\":{\"archetype\":\"classicbulb\",\"function\":\"functional\",\"direction\":\"omnidirectional\"},"
     "\"uniqueid\":\"%c%c:%c%c:%c%c:%c%c:%c%c:%c%c:00:11-0b\",\"swversion\":\"1.46.13_r26312\"}",
     is_on() ? "true" : "false", bri, ex.alexa_name,
     mh[0], mh[1], mh[2], mh[3], mh[4], mh[5], mh[6], mh[7], mh[8], mh[9], mh[10], mh[11]);
  return w;
}

// returns response length, -1 = not a Hue request
int hue_http(const char *method, const char *path, char *body, char *out, int max, const char **ctype) {
  char *w = out, *end = out + max;
  *ctype = "application/json";
  if (!ex.alexa_on) return -1;
  if (!strcmp(path, "/description.xml") && !strcmp(method, "GET")) {
    char mh[13]; machex(mh);
    *ctype = "text/xml";
    AP("<?xml version=\"1.0\" ?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion>"
       "<URLBase>http://%s:80/</URLBase><device><deviceType>urn:schemas-upnp-org:device:Basic:1</deviceType>"
       "<friendlyName>%s (OpenPix)</friendlyName><manufacturer>Royal Philips Electronics</manufacturer>"
       "<manufacturerURL>http://www.philips.com</manufacturerURL><modelDescription>Philips hue Personal Wireless Lighting</modelDescription>"
       "<modelName>Philips hue bridge 2012</modelName><modelNumber>929000226503</modelNumber><modelURL>http://www.meethue.com</modelURL>"
       "<serialNumber>%s</serialNumber><UDN>uuid:2f402f80-da50-11e1-9b23-%s</UDN>"
       "<presentationURL>index.html</presentationURL></device></root>", net_ip_str(), ex.alexa_name, mh, mh);
    return w - out;
  }
  if (strncmp(path, "/api", 4) != 0) return -1;
  if (!strcmp(path, "/api") || !strcmp(path, "/api/")) {
    if (strcmp(method, "POST")) return -1;
    AP("[{\"success\":{\"username\":\"openpixalexa0001\"}}]");
    return w - out;
  }
  // /api/<user>/...
  const char *rest = strchr(path + 5, '/');
  if (!rest) {   // full state
    AP("{\"lights\":{\"1\":"); w = light(w, end); AP("},\"groups\":{},\"config\":{\"name\":\"OpenPix\",\"swversion\":\"1946071000\",\"apiversion\":\"1.46.0\"}}");
    return w - out;
  }
  if (!strcmp(rest, "/lights") && !strcmp(method, "GET")) { AP("{\"1\":"); w = light(w, end); AP("}"); return w - out; }
  if (!strcmp(rest, "/lights/1") && !strcmp(method, "GET")) { w = light(w, end); return w - out; }
  if (!strcmp(rest, "/lights/1/state") && !strcmp(method, "PUT")) {
    const char *p;
    int changed = 0;
    AP("[");
    if ((p = strstr(body, "\"on\""))) {
      p += 4; while (*p == ' ' || *p == ':') p++;
      int on = !strncmp(p, "true", 4);
      cfg.power_off = !on;
      if (on && cfg.fx == FX_OFF) cfg.fx = FX_TWINKLE;
      if (on) cfg.test_mode = 0;
      AP("{\"success\":{\"/lights/1/state/on\":%s}}", on ? "true" : "false");
      changed = 1;
    }
    if ((p = strstr(body, "\"bri\""))) {
      p += 5; while (*p == ' ' || *p == ':') p++;
      int b = atoi(p);
      if (b < 1) b = 1;
      if (b > 254) b = 254;
      cfg.fx_bright = (b * 100 + 127) / 254; if (!cfg.fx_bright) cfg.fx_bright = 1;
      AP("%s{\"success\":{\"/lights/1/state/bri\":%d}}", changed ? "," : "", b);
      changed = 1;
    }
    AP("]");
    if (changed) { uint8_t t = cfg.test_mode; cfg.test_mode = 0; config_save(); cfg.test_mode = t; }
    return w - out;
  }
  if (!strcmp(rest, "/groups") || !strcmp(rest, "/scenes") || !strcmp(rest, "/config")) { AP("{}"); return w - out; }
  return -1;
}
