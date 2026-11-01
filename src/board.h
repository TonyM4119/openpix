// Board definitions for the HolidayCoro AlphaPix Evolution CPU board #951-11
// (GigaDevice GD32F450, register-compatible with STM32F407 for everything used here).
//
// Pin map, recovered from stock firmware V4.07:
//   Ethernet RMII : PA1 REF_CLK, PA7 CRS_DV, PB11 TX_EN, PB12 TXD0, PB13 TXD1,
//                   PC4 RXD0, PC5 RXD1 (no MDIO; PHY is strap-configured)
//   Pixel data    : PB0-7, PC8-15 (expansion port 1), PE0-15, PD0-15 (ports 2/3)
//   Pixel clock   : PB15
//   OLED SSD1306  : I2C bit-bang, PA8 = SCL, PC7 = SDA, address 0x78
//   Buttons       : PA4, PA5, PA10, PA11, PA12 (active low, pull-up)
//                   PA4 + PA11 held at power-up = load defaults
//   Misc outputs  : PA6, PB14 driven low at start (as stock)
//   DMX (RS-485)  : USART1 PA9/PA10?  USART6 PC6  (not used yet)
//   SWD           : PA13/PA14 - never reconfigured, so a programmer can always attach
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "stm32f407xx.h"

#define FW_NAME    "OpenPix"
#define FW_VERSION "0.8.1"

extern volatile uint32_t g_ms;
extern uint32_t g_sysclk;
extern int g_hse_ok;

static inline uint32_t millis(void) { return g_ms; }
void delay_ms(uint32_t ms);
void delay_us(uint32_t us);
void wdt_kick(void);
void board_init(void);
int  button_raw(int i);          // 1 = pressed
int  defaults_combo_held(void);  // PA4 + PA11 held
uint32_t device_id(void);        // factory ID from 0x0800C000 (shown as "Device ID" by stock)
void device_mac(uint8_t mac[6]);

// Cycle counter helpers (DWT)
static inline uint32_t cyc(void) { return DWT->CYCCNT; }
