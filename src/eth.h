#pragma once
#include <stdint.h>
#include "lwip/err.h"
struct netif;
int  eth_hw_init(const uint8_t mac[6]);
int  eth_hw_up(void);
void eth_poll(struct netif *nif);
err_t eth_netif_init(struct netif *nif);
extern uint32_t eth_rx_count, eth_tx_count, eth_err_count;
