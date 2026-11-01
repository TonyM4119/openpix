#pragma once
#include <stdint.h>
struct netif;
void proto_init(void);
void proto_join_groups(struct netif *nif);
extern uint32_t proto_pkts_sacn, proto_pkts_art, proto_pkts_ddp;
