// Ethernet MAC/DMA driver (STM32F4-compatible ETH block in the GD32F450) + lwIP glue.
// Mirrors the stock setup: RMII, MAC forced to 100 Mbit full duplex, no MDIO.
#include "board.h"
#include "eth.h"
#include "lwip/opt.h"
#include "lwip/pbuf.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "netif/ethernet.h"
#include <string.h>

#define RX_N 20
#define TX_N 4
#define BUF_SZ 1536

typedef struct { volatile uint32_t d0, d1, d2, d3; } desc_t;

static desc_t rxd[RX_N] __attribute__((aligned(4)));
static desc_t txd[TX_N] __attribute__((aligned(4)));
static uint8_t rxb[RX_N][BUF_SZ] __attribute__((aligned(4)));
static uint8_t txb[TX_N][BUF_SZ] __attribute__((aligned(4)));
static int rx_i, tx_i;
static int eth_ok;
uint32_t eth_rx_count, eth_tx_count, eth_err_count;

#define DES_OWN (1u << 31)

static void mac_delay(void) { delay_us(50); }

int eth_hw_up(void) { return eth_ok; }

int eth_hw_init(const uint8_t mac[6]) {
  eth_ok = 0;
  // RMII must be selected while the MAC is held in reset / clocks off.
  RCC->AHB1ENR &= ~(RCC_AHB1ENR_ETHMACEN | RCC_AHB1ENR_ETHMACTXEN | RCC_AHB1ENR_ETHMACRXEN);
  RCC->AHB1RSTR |= RCC_AHB1RSTR_ETHMACRST;
  SYSCFG->PMC |= SYSCFG_PMC_MII_RMII_SEL;
  (void)SYSCFG->PMC;
  RCC->AHB1ENR |= RCC_AHB1ENR_ETHMACEN | RCC_AHB1ENR_ETHMACTXEN | RCC_AHB1ENR_ETHMACRXEN;
  delay_us(100);
  RCC->AHB1RSTR &= ~RCC_AHB1RSTR_ETHMACRST;
  delay_us(100);

  // DMA software reset. This only completes if the PHY is feeding the 50 MHz
  // REF_CLK, so it doubles as a "is the PHY alive" check. Never wait forever
  // (the stock firmware does, which is one way it ends up dead after a reboot).
  ETH->DMABMR |= ETH_DMABMR_SR;
  uint32_t t = millis();
  while (ETH->DMABMR & ETH_DMABMR_SR) {
    if ((uint32_t)(millis() - t) > 200) return 0;
    wdt_kick();
  }

  ETH->MACCR = ETH_MACCR_FES | ETH_MACCR_DM | ETH_MACCR_RD;  // 100M, full duplex, no retry
  mac_delay();
  ETH->MACFFR = ETH_MACFFR_PAM;                               // accept all multicast (sACN)
  mac_delay();
  ETH->MACHTHR = 0; ETH->MACHTLR = 0;
  ETH->MACFCR = 0;
  ETH->MACVLANTR = 0;
  ETH->MACA0HR = ((uint32_t)mac[5] << 8) | mac[4];
  ETH->MACA0LR = ((uint32_t)mac[3] << 24) | ((uint32_t)mac[2] << 16) | ((uint32_t)mac[1] << 8) | mac[0];
  ETH->MMCRIMR = 0xFFFFFFFF; ETH->MMCTIMR = 0xFFFFFFFF;  // mask MMC counter interrupts

  // Descriptors, chained mode
  for (int i = 0; i < RX_N; i++) {
    rxd[i].d0 = DES_OWN;
    rxd[i].d1 = (1u << 14) | BUF_SZ;          // RCH | RBS1
    rxd[i].d2 = (uint32_t)rxb[i];
    rxd[i].d3 = (uint32_t)&rxd[(i + 1) % RX_N];
  }
  for (int i = 0; i < TX_N; i++) {
    txd[i].d0 = (1u << 20);                    // TCH
    txd[i].d1 = 0;
    txd[i].d2 = (uint32_t)txb[i];
    txd[i].d3 = (uint32_t)&txd[(i + 1) % TX_N];
  }
  rx_i = tx_i = 0;
  ETH->DMARDLAR = (uint32_t)rxd;
  ETH->DMATDLAR = (uint32_t)txd;

  ETH->DMABMR = ETH_DMABMR_AAB | ETH_DMABMR_USP | ETH_DMABMR_RDP_32Beat | ETH_DMABMR_FB |
                ETH_DMABMR_PBL_32Beat;
  mac_delay();
  ETH->DMAOMR = ETH_DMAOMR_RSF | ETH_DMAOMR_TSF;
  mac_delay();
  ETH->DMAIER = 0;

  ETH->MACCR |= ETH_MACCR_TE; mac_delay();
  ETH->DMAOMR |= ETH_DMAOMR_FTF; mac_delay();
  ETH->MACCR |= ETH_MACCR_RE; mac_delay();
  ETH->DMAOMR |= ETH_DMAOMR_ST | ETH_DMAOMR_SR;
  eth_ok = 1;
  return 1;
}

static err_t low_output(struct netif *nif, struct pbuf *p) {
  (void)nif;
  if (!eth_ok) return ERR_IF;
  desc_t *d = &txd[tx_i];
  uint32_t t = millis();
  while (d->d0 & DES_OWN) {
    if ((uint32_t)(millis() - t) > 5) { eth_err_count++; return ERR_MEM; }
  }
  if (p->tot_len > BUF_SZ) return ERR_BUF;
  pbuf_copy_partial(p, txb[tx_i], p->tot_len, 0);
  d->d1 = p->tot_len;
  d->d0 = DES_OWN | (1u << 29) | (1u << 28) | (1u << 20);  // OWN | LS | FS | TCH
  tx_i = (tx_i + 1) % TX_N;
  if (ETH->DMASR & ETH_DMASR_TBUS) ETH->DMASR = ETH_DMASR_TBUS;
  ETH->DMATPDR = 0;
  eth_tx_count++;
  return ERR_OK;
}

void eth_poll(struct netif *nif) {
  if (!eth_ok) return;
  for (int n = 0; n < RX_N; n++) {
    desc_t *d = &rxd[rx_i];
    uint32_t s = d->d0;
    if (s & DES_OWN) break;
    int good = (s & (1u << 9)) && (s & (1u << 8)) && !(s & (1u << 15));  // FS, LS, !ES
    uint32_t len = (s >> 16) & 0x3FFF;
    if (good && len > 4) {
      len -= 4;  // strip CRC
      struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
      if (p) {
        pbuf_take(p, rxb[rx_i], (u16_t)len);
        if (nif->input(p, nif) != ERR_OK) pbuf_free(p);
        eth_rx_count++;
      } else eth_err_count++;
    } else if (!good) eth_err_count++;
    d->d0 = DES_OWN;
    rx_i = (rx_i + 1) % RX_N;
  }
  if (ETH->DMASR & ETH_DMASR_RBUS) { ETH->DMASR = ETH_DMASR_RBUS; ETH->DMARPDR = 0; }
}

err_t eth_netif_init(struct netif *nif) {
  nif->name[0] = 'e'; nif->name[1] = '0';
  nif->output = etharp_output;
  nif->linkoutput = low_output;
  nif->mtu = 1500;
  nif->hwaddr_len = 6;
  nif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;
  return ERR_OK;
}
