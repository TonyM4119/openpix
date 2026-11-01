#include "board.h"

volatile uint32_t g_ms;
uint32_t g_sysclk;
int g_hse_ok;

void SysTick_Handler(void) { g_ms++; }

void delay_ms(uint32_t ms) {
  uint32_t t = millis();
  while ((uint32_t)(millis() - t) < ms) { wdt_kick(); }
}

void delay_us(uint32_t us) {
  uint32_t start = cyc(), n = us * (g_sysclk / 1000000u);
  while ((uint32_t)(cyc() - start) < n) {}
}

void wdt_kick(void) { IWDG->KR = 0xAAAA; }

static void wdt_init(void) {
  // LSI ~32 kHz, /128, reload 4095 -> about 16 s, same as stock.
  IWDG->KR = 0x5555;
  IWDG->PR = 5;
  IWDG->RLR = 0xFFF;
  IWDG->KR = 0xAAAA;
  IWDG->KR = 0xCCCC;
}

// mode: 0 in, 1 out, 2 AF; pull: 0 none, 1 up
static void gpio_cfg(GPIO_TypeDef *g, uint32_t pins, uint32_t mode, uint32_t pull, uint32_t af) {
  for (int p = 0; p < 16; p++) {
    if (!(pins & (1u << p))) continue;
    g->MODER   = (g->MODER   & ~(3u << (p * 2))) | (mode << (p * 2));
    g->OSPEEDR |= (3u << (p * 2));
    g->OTYPER  &= ~(1u << p);
    g->PUPDR   = (g->PUPDR   & ~(3u << (p * 2))) | (pull << (p * 2));
    if (mode == 2) {
      volatile uint32_t *r = &g->AFR[p >> 3];
      int s = (p & 7) * 4;
      *r = (*r & ~(0xFu << s)) | (af << s);
    }
  }
}

void board_init(void) {
  // 1 ms tick
  SysTick->LOAD = g_sysclk / 1000 - 1;
  SysTick->VAL = 0;
  SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;

  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN |
                  RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOEEN;
  RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
  (void)RCC->AHB1ENR;

  // Pixel outputs low first, then switch to output mode (no glitches on the strings)
  GPIOB->BSRR = (0x80FFu << 16);
  GPIOC->BSRR = (0xFF00u << 16);
  GPIOD->BSRR = (0xFFFFu << 16);
  GPIOE->BSRR = (0xFFFFu << 16);
  gpio_cfg(GPIOB, 0x80FF, 1, 0, 0);
  gpio_cfg(GPIOC, 0xFF00, 1, 0, 0);
  gpio_cfg(GPIOD, 0xFFFF, 1, 0, 0);
  gpio_cfg(GPIOE, 0xFFFF, 1, 0, 0);

  // Misc outputs held low like stock
  GPIOA->BSRR = (1u << 6) << 16;
  GPIOB->BSRR = (1u << 14) << 16;
  gpio_cfg(GPIOA, 1u << 6, 1, 0, 0);
  gpio_cfg(GPIOB, 1u << 14, 1, 0, 0);

  // Buttons
  gpio_cfg(GPIOA, (1u << 4) | (1u << 5) | (1u << 10) | (1u << 11) | (1u << 12), 0, 1, 0);

  // Ethernet RMII pins (AF11, pull-up like stock)
  gpio_cfg(GPIOA, (1u << 1) | (1u << 7), 2, 1, 11);
  gpio_cfg(GPIOB, (1u << 11) | (1u << 12) | (1u << 13), 2, 1, 11);
  gpio_cfg(GPIOC, (1u << 4) | (1u << 5), 2, 1, 11);

  wdt_init();
}

static const uint8_t btn_pins[5] = {4, 5, 10, 11, 12};
int button_raw(int i) { return (GPIOA->IDR & (1u << btn_pins[i])) == 0; }
int defaults_combo_held(void) { return button_raw(0) && button_raw(3); }

uint32_t device_id(void) {
  uint32_t v = *(volatile uint32_t *)0x0800C000;
  return v;
}

void device_mac(uint8_t mac[6]) {
  // Locally administered MAC derived from the factory device ID and chip UID.
  uint32_t id = device_id();
  if (id == 0xFFFFFFFF || id == 0) {
    volatile uint32_t *uid = (volatile uint32_t *)0x1FFF7A10;
    id = uid[0] ^ uid[1] ^ uid[2];
  }
  mac[0] = 0x02; mac[1] = 0xA9;
  mac[2] = id >> 24; mac[3] = id >> 16; mac[4] = id >> 8; mac[5] = id;
}
