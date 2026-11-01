// Flash erase/program for the STM32F4-compatible flash controller (same
// register usage as the stock firmware: PSIZE x32, sector erase via SNB).
#include "board.h"
#include "flash.h"

#define KEY1 0x45670123u
#define KEY2 0xCDEF89ABu
#define RAMFUNC __attribute__((section(".ramfunc"), noinline, long_call))

static void unlock(void) {
  if (FLASH->CR & FLASH_CR_LOCK) { FLASH->KEYR = KEY1; FLASH->KEYR = KEY2; }
  FLASH->SR = FLASH_SR_EOP | FLASH_SR_SOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR |
              FLASH_SR_PGPERR | FLASH_SR_PGSERR;
}
static void lock(void) { FLASH->CR |= FLASH_CR_LOCK; }

static int wait_busy(void) {
  while (FLASH->SR & FLASH_SR_BSY) wdt_kick();
  return (FLASH->SR & (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)) == 0;
}

int flash_erase_sector(int sector) {
  if (sector < 4 || sector > 11) return 0;  // never touch bootloader / factory data
  unlock();
  FLASH->CR = (FLASH->CR & ~(FLASH_CR_PSIZE | FLASH_CR_SNB)) | FLASH_CR_PSIZE_1 |
              FLASH_CR_SER | ((uint32_t)sector << 3);
  FLASH->CR |= FLASH_CR_STRT;
  int ok = wait_busy();
  FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);
  lock();
  return ok;
}

int flash_program(uint32_t addr, const void *src, uint32_t len) {
  if (addr < APP_BASE || addr + len > 0x08100000u || (addr & 3)) return 0;
  const uint8_t *s = src;
  unlock();
  FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE) | FLASH_CR_PSIZE_1 | FLASH_CR_PG;
  int ok = 1;
  for (uint32_t i = 0; i < len && ok; i += 4) {
    uint32_t w = 0xFFFFFFFFu;
    uint32_t n = len - i < 4 ? len - i : 4;
    for (uint32_t k = 0; k < n; k++) ((uint8_t *)&w)[k] = s[i + k];
    *(volatile uint32_t *)(addr + i) = w;
    ok = wait_busy();
    if (ok && *(volatile uint32_t *)(addr + i) != w) ok = 0;
  }
  FLASH->CR &= ~FLASH_CR_PG;
  lock();
  return ok;
}

int stage_erase(void) {
  return flash_erase_sector(8) && flash_erase_sector(9) && flash_erase_sector(10);
}

// ---- Installer: runs entirely from RAM with interrupts off ----

RAMFUNC static void r_kick(void) { IWDG->KR = 0xAAAA; }

RAMFUNC static int r_wait(void) {
  while (FLASH->SR & FLASH_SR_BSY) r_kick();
  return (FLASH->SR & (FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR)) == 0;
}

RAMFUNC static void r_erase(int sector) {
  FLASH->SR = FLASH_SR_EOP | FLASH_SR_WRPERR | FLASH_SR_PGAERR | FLASH_SR_PGPERR | FLASH_SR_PGSERR;
  FLASH->CR = (FLASH->CR & ~(FLASH_CR_PSIZE | FLASH_CR_SNB)) | FLASH_CR_PSIZE_1 |
              FLASH_CR_SER | ((uint32_t)sector << 3);
  FLASH->CR |= FLASH_CR_STRT;
  r_wait();
  FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);
}

RAMFUNC static int r_copy(uint32_t len) {
  FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE) | FLASH_CR_PSIZE_1 | FLASH_CR_PG;
  for (uint32_t i = 0; i < len; i += 4) {
    uint32_t w = *(volatile uint32_t *)(STAGE_BASE + i);
    *(volatile uint32_t *)(APP_BASE + i) = w;
    r_wait();
  }
  FLASH->CR &= ~FLASH_CR_PG;
  for (uint32_t i = 0; i < len; i += 4)
    if (*(volatile uint32_t *)(APP_BASE + i) != *(volatile uint32_t *)(STAGE_BASE + i)) return 0;
  return 1;
}

RAMFUNC __attribute__((noreturn)) static void r_install(uint32_t len) {
  if (FLASH->CR & FLASH_CR_LOCK) { FLASH->KEYR = KEY1; FLASH->KEYR = KEY2; }
  // Data/instruction caches off and reset so we read real flash contents.
  FLASH->ACR &= ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN);
  FLASH->ACR |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
  FLASH->ACR &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
  for (int attempt = 0; attempt < 3; attempt++) {
    // Sector 4 = 64 KB, sectors 5..7 = 128 KB each
    r_erase(4); r_kick();
    if (len > 64u * 1024u)  { r_erase(5); r_kick(); }
    if (len > 192u * 1024u) { r_erase(6); r_kick(); }
    if (len > 320u * 1024u) { r_erase(7); r_kick(); }
    if (r_copy(len)) break;
  }
  FLASH->CR |= FLASH_CR_LOCK;
  __DSB();
  SCB->AIRCR = (0x5FAu << SCB_AIRCR_VECTKEY_Pos) | SCB_AIRCR_SYSRESETREQ_Msk;
  __DSB();
  for (;;) {}
}

void flash_install_staged(uint32_t len) {
  len = (len + 3u) & ~3u;
  __disable_irq();
  SysTick->CTRL = 0;
  IWDG->KR = 0xAAAA;
  r_install(len);
}
