// SSD1306 128x64 OLED on bit-banged I2C (PA8 = SCL, PC7 = SDA), same init as stock.
#include "board.h"
#include "oled.h"
#include "font5x7.h"
#include <string.h>

static uint8_t fb[8][128];

#define SCL_H() (GPIOA->BSRR = (1u << 8))
#define SCL_L() (GPIOA->BSRR = (1u << 8) << 16)
#define SDA_H() (GPIOC->BSRR = (1u << 7))
#define SDA_L() (GPIOC->BSRR = (1u << 7) << 16)

static void dly(void) { for (volatile int i = 0; i < 12; i++) {} }

static void i2c_start(void) { SCL_H(); SDA_H(); dly(); SDA_L(); dly(); SCL_L(); dly(); }
static void i2c_stop(void)  { SDA_L(); dly(); SCL_H(); dly(); SDA_H(); dly(); }
static void i2c_byte(uint8_t b) {
  for (int i = 0; i < 8; i++) {
    if (b & 0x80) SDA_H(); else SDA_L();
    dly(); SCL_H(); dly(); SCL_L();
    b <<= 1;
  }
  SDA_H(); dly(); SCL_H(); dly(); SCL_L(); dly();  // ignore ACK, like stock
}

static void cmd(uint8_t c) { i2c_start(); i2c_byte(0x78); i2c_byte(0x00); i2c_byte(c); i2c_stop(); }

void oled_init(void) {
  // Push-pull outputs like the stock firmware
  GPIOA->MODER = (GPIOA->MODER & ~(3u << 16)) | (1u << 16);
  GPIOA->OSPEEDR |= (3u << 16);
  GPIOC->MODER = (GPIOC->MODER & ~(3u << 14)) | (1u << 14);
  GPIOC->OSPEEDR |= (3u << 14);
  SCL_H(); SDA_H();
  delay_ms(50);
  static const uint8_t init[] = {0xAE, 0x00, 0x10, 0x40, 0xB0, 0x81, 0xFF, 0xA1, 0xA6, 0xA8, 0x3F,
                                 0xC8, 0xD3, 0x00, 0xD5, 0x80, 0xD8, 0x05, 0xD9, 0xF1, 0xDA, 0x12,
                                 0xDB, 0x30, 0x8D, 0x14, 0xAF};
  for (unsigned i = 0; i < sizeof(init); i++) cmd(init[i]);
  oled_clear();
  oled_flush();
}

void oled_clear(void) { memset(fb, 0, sizeof(fb)); }

void oled_text(int row, const char *s) {
  if (row < 0 || row > 7) return;
  memset(fb[row], 0, 128);
  int x = 1;
  while (*s && x <= 122) {
    unsigned char c = (unsigned char)*s++;
    if (c < 32 || c > 126) c = '?';
    memcpy(&fb[row][x], font5x7[c - 32], 5);
    x += 6;
  }
}

void oled_invert_row(int row) {
  for (int i = 0; i < 128; i++) fb[row][i] ^= 0xFF;
}

void oled_flush(void) {
  for (int p = 0; p < 8; p++) {
    cmd(0xB0 + p); cmd(0x00); cmd(0x10);
    i2c_start(); i2c_byte(0x78); i2c_byte(0x40);
    for (int i = 0; i < 128; i++) i2c_byte(fb[p][i]);
    i2c_stop();
    wdt_kick();
  }
}
