// Two RS-485 DMX512 outputs, as wired on the stock board:
//   DMX 1: USART1 TX on PA9 (AF7)    DMX 2: USART6 TX on PC6 (AF8)
// 250 kbaud, 8N2. The break is made by switching the pin to a plain GPIO
// driven low (same trick as the stock firmware). Bytes are fed by polling
// from the main loop, so nothing ever blocks for a whole frame; DMX allows
// gaps between slots.
#include "board.h"
#include "dmx.h"
#include "config.h"

uint8_t dmx_buf[2][513];        // [0] = start code (0), then 512 slots
uint32_t dmx_frames[2];

typedef struct {
  USART_TypeDef *u;
  GPIO_TypeDef *g;
  int pin, af;
  int state;          // 0 idle, 1 break, 2 mab, 3 data
  uint32_t t;
  int idx;
} port_t;

static port_t P[2] = {
  {USART1, GPIOA, 9, 7, 0, 0, 0},
  {USART6, GPIOC, 6, 8, 0, 0, 0},
};

static void pin_mode(port_t *p, int af) {
  int s = p->pin * 2;
  p->g->MODER = (p->g->MODER & ~(3u << s)) | ((af ? 2u : 1u) << s);
}

void dmx_init(void) {
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN | RCC_APB2ENR_USART6EN;
  (void)RCC->APB2ENR;
  for (int i = 0; i < 2; i++) {
    port_t *p = &P[i];
    volatile uint32_t *afr = &p->g->AFR[p->pin >> 3];
    int s = (p->pin & 7) * 4;
    *afr = (*afr & ~(0xFu << s)) | ((uint32_t)p->af << s);
    p->g->OSPEEDR |= 3u << (p->pin * 2);
    p->g->BSRR = 1u << p->pin;           // idle high (mark)
    pin_mode(p, 0);
    p->u->CR1 = 0;
    p->u->BRR = (g_sysclk / 2) / 250000;  // APB2 = sysclk / 2
    p->u->CR2 = USART_CR2_STOP_1;        // 2 stop bits
    p->u->CR3 = 0;
    p->u->CR1 = USART_CR1_UE | USART_CR1_TE;
    p->state = 0;
  }
}

void dmx_task(void) {
  uint32_t now = cyc(), us = g_sysclk / 1000000u;
  for (int i = 0; i < 2; i++) {
    port_t *p = &P[i];
    if (!cfg.dmx_on[i]) { p->state = 0; continue; }
    switch (p->state) {
      case 0:   // start a frame (about 40 per second)
        if ((uint32_t)(now - p->t) < 25000u * us) break;
        if (!(p->u->SR & USART_SR_TC)) break;
        pin_mode(p, 0);
        p->g->BSRR = (1u << p->pin) << 16;   // break: low
        p->t = now; p->state = 1;
        break;
      case 1:
        if ((uint32_t)(now - p->t) < 120u * us) break;
        p->g->BSRR = 1u << p->pin;           // mark after break
        p->t = now; p->state = 2;
        break;
      case 2:
        if ((uint32_t)(now - p->t) < 16u * us) break;
        pin_mode(p, 1);
        p->idx = 0; p->state = 3;
        /* fall through */
      case 3:
        while (p->idx < 513 && (p->u->SR & USART_SR_TXE)) p->u->DR = dmx_buf[i][p->idx++];
        if (p->idx >= 513) { p->state = 0; dmx_frames[i]++; }
        break;
    }
  }
}
