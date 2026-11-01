#pragma once
#include <stdint.h>

#define APP_BASE      0x08010000u
#define APP_MAX       (448u * 1024u)
#define STAGE_BASE    0x08080000u   // sectors 8..10
#define STAGE_MAX     (384u * 1024u)
#define CFG_BASE      0x080E0000u   // sector 11
#define CFG_SIZE      (128u * 1024u)

int flash_erase_sector(int sector);                 // 1 = ok
int flash_program(uint32_t addr, const void *src, uint32_t len);  // word aligned, 1 = ok
int stage_erase(void);
// Copies the staged image over the running application and resets. Never returns.
__attribute__((noreturn)) void flash_install_staged(uint32_t len);
