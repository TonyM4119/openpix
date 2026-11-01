// Expansion-board monitoring, fan and over-temperature protection.
//
// Recovered from the stock firmware: each expansion port has a small chip on a
// bit-banged I2C bus (PB8 = SCL, PB9 = SDA, open drain) answering at address
// 0xA0 + 2*port. It is read one byte per transaction, 6 bytes:
//   short format: [volt, temp, sum]            sum  = volt + temp + 0x55
//   long  format: [volt, temp, x, y, ?, sum]   sum  = volt + temp + x + y + 0x55
// volt is in 0.1 V steps, temp is degrees C + 50.
// The fan is on PB14 (high = on), switched with 5 C of hysteresis like stock.
#include "board.h"
#include "sensors.h"
#include "config.h"

sensor_port_t g_sensors[3];
int g_fan_on, g_overtemp;

#define SCL (1u << 8)
#define SDA (1u << 9)

static void d(void) { delay_us(5); }
static void scl(int v) { GPIOB->BSRR = v ? SCL : SCL << 16; }
static void sda(int v) { GPIOB->BSRR = v ? SDA : SDA << 16; }   // open drain: 1 = released
static int  sda_in(void) { return (GPIOB->IDR & SDA) != 0; }

static void i2c_start(void) { sda(1); scl(1); d(); sda(0); d(); scl(0); d(); }
static void i2c_stop(void)  { sda(0); d(); scl(1); d(); sda(1); d(); }
static int  i2c_write(uint8_t b) {
  for (int i = 0; i < 8; i++) { sda(b & 0x80); d(); scl(1); d(); scl(0); b <<= 1; }
  sda(1); d(); scl(1); d();
  int ack = !sda_in();
  scl(0); d();
  return ack;
}
static uint8_t i2c_read(int nack) {
  uint8_t b = 0;
  sda(1);
  for (int i = 0; i < 8; i++) { d(); scl(1); d(); b = (b << 1) | sda_in(); scl(0); }
  sda(nack); d(); scl(1); d(); scl(0); sda(1); d();
  return b;
}

void sensors_init(void) {
  GPIOB->BSRR = SCL | SDA;
  for (int p = 8; p <= 9; p++) {
    GPIOB->MODER = (GPIOB->MODER & ~(3u << (p * 2))) | (1u << (p * 2));
    GPIOB->OTYPER |= (1u << p);                       // open drain
    GPIOB->PUPDR = (GPIOB->PUPDR & ~(3u << (p * 2))) | (1u << (p * 2));
  }
  // Fan PB14 already set up as a low output by board_init
}

static uint8_t buf[6];
static int port, idx;
static uint32_t next_ms;

static void decode(int p) {
  sensor_port_t *s = &g_sensors[p];
  uint8_t sum3 = (uint8_t)(buf[0] + buf[1] + 0x55);
  uint8_t sum6 = (uint8_t)(buf[0] + buf[1] + buf[2] + buf[3] + 0x55);
  if (buf[2] == sum3) {
    s->ok = 1; s->fails = 0; s->volt10 = buf[0]; s->temp_c = (int)buf[1] - 50; s->kind = 1;
    s->x = s->y = 0;
  } else if (buf[5] == sum6) {
    s->ok = 1; s->fails = 0; s->volt10 = buf[0]; s->temp_c = (int)buf[1] - 50; s->kind = 2;
    s->x = buf[2]; s->y = buf[3];
  } else if (++s->fails > 5) {
    s->ok = 0; s->fails = 5;
  }
}

static void protect(void) {
  int hot = -100;
  for (int p = 0; p < 3; p++) if (g_sensors[p].ok && g_sensors[p].temp_c > hot) hot = g_sensors[p].temp_c;
  if (hot == -100) { g_fan_on = 0; g_overtemp = 0; }
  else {
    if (hot >= cfg.fan_on_c) g_fan_on = 1;
    else if (hot <= cfg.fan_on_c - 5) g_fan_on = 0;
    if (cfg.shutdown_c && hot >= cfg.shutdown_c) g_overtemp = 1;
    else if (!cfg.shutdown_c || hot <= cfg.shutdown_c - 5) g_overtemp = 0;
  }
  GPIOB->BSRR = g_fan_on ? (1u << 14) : (1u << 14) << 16;
}

// One I2C byte transaction every 5 ms (the expansion chip needs the gap),
// so the network is never blocked for long. All three ports take ~1 s.
void sensors_task(void) {
  uint32_t now = millis();
  if ((int32_t)(now - next_ms) < 0) return;
  next_ms = now + 5;
  i2c_start();
  int ack = i2c_write(0xA1 + 2 * port);
  buf[idx] = ack ? i2c_read(1) : 0xFF;
  i2c_stop();
  if (!ack) idx = 5;   // nobody there: skip the rest
  if (++idx >= 6) {
    decode(port);
    idx = 0;
    if (++port >= 3) { port = 0; protect(); next_ms = now + 700; }
  }
}
