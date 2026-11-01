// Startup, vector table and clock setup.
#include "board.h"

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack, _estack_vector;
int main(void);
void Reset_Handler(void);
void SysTick_Handler(void);

static void Default_Handler(void) { NVIC_SystemReset(); }

void HardFault_Handler(void) {
  // Something went badly wrong: reboot rather than sit dead.
  // (The watchdog would also catch this, but this is faster.)
  NVIC_SystemReset();
}

__attribute__((section(".isr_vector"), used))
const void *const vector_table[16 + 91] = {
  &_estack_vector,
  Reset_Handler,
  Default_Handler,   // NMI
  HardFault_Handler,
  HardFault_Handler, // MemManage
  HardFault_Handler, // BusFault
  HardFault_Handler, // UsageFault
  0, 0, 0, 0,
  Default_Handler,   // SVC
  Default_Handler,   // DebugMon
  0,
  Default_Handler,   // PendSV
  SysTick_Handler,
  // All peripheral IRQs: unused (we poll), route to default.
  [16 ... 16 + 90] = Default_Handler,
};

// Bring clocks to a known state and run at 168 MHz from the 25 MHz crystal,
// exactly like the stock firmware (PLLM=25, PLLN=336, PLLP=2, PLLQ=7).
// Falls back to the internal 16 MHz oscillator if the crystal does not start.
static void clock_init(void) {
  SCB->CPACR |= (0xFu << 20);           // FPU on
  RCC->CR |= RCC_CR_HSION;
  while (!(RCC->CR & RCC_CR_HSIRDY)) {}
  RCC->CFGR = 0;                         // HSI as SYSCLK
  while ((RCC->CFGR & RCC_CFGR_SWS) != 0) {}
  RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_CSSON | RCC_CR_PLLON);
  RCC->PLLCFGR = 0x24003010;
  RCC->CR &= ~RCC_CR_HSEBYP;
  RCC->CIR = 0;

  RCC->CR |= RCC_CR_HSEON;
  uint32_t n = 0;
  while (!(RCC->CR & RCC_CR_HSERDY) && n < 500000) n++;
  int hse = (RCC->CR & RCC_CR_HSERDY) != 0;

  RCC->APB1ENR |= RCC_APB1ENR_PWREN;
  PWR->CR |= PWR_CR_VOS;
  RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE2_DIV2 | RCC_CFGR_PPRE1_DIV4;
  if (hse) {
    RCC->PLLCFGR = (25u << 0) | (336u << 6) | (0u << 16) | RCC_PLLCFGR_PLLSRC_HSE | (7u << 24);
  } else {
    RCC->CR &= ~RCC_CR_HSEON;
    RCC->PLLCFGR = (16u << 0) | (336u << 6) | (0u << 16) | (7u << 24);
  }
  RCC->CR |= RCC_CR_PLLON;
  n = 0;
  while (!(RCC->CR & RCC_CR_PLLRDY) && n < 2000000) n++;
  if (!(RCC->CR & RCC_CR_PLLRDY)) return;  // stay on 16 MHz HSI; still boots
  FLASH->ACR = FLASH_ACR_LATENCY_5WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
  while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {}
  g_hse_ok = hse;
  g_sysclk = 168000000u;
}

__attribute__((naked, noreturn)) void Reset_Handler(void) {
  __asm volatile(
    "cpsid i            \n"
    "ldr r0, =_estack   \n"
    "mov sp, r0         \n"
    "b reset_c          \n");
}

__attribute__((noreturn, used)) void reset_c(void) {
  // The bootloader may have left SysTick or peripherals running.
  SysTick->CTRL = 0;
  for (int i = 0; i < 8; i++) { NVIC->ICER[i] = 0xFFFFFFFF; NVIC->ICPR[i] = 0xFFFFFFFF; }
  SCB->VTOR = 0x08010000;

  uint32_t *s = &_sidata, *d = &_sdata;
  while (d < &_edata) *d++ = *s++;
  for (d = &_sbss; d < &_ebss; ) *d++ = 0;

  g_sysclk = 16000000u;
  clock_init();
  main();
  NVIC_SystemReset();
  for (;;) {}
}
