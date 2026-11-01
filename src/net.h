#pragma once
#include <stdint.h>
#include <string.h>
void net_init(void);
void net_task(void);
const char *net_ip_str(void);
int  net_has_ip(void);
int  net_eth_up(void);
void net_apply_later(uint32_t ms);
void net_groups_changed(void);
