#pragma once
#include <stdint.h>
#include <stdlib.h>
#define LWIP_NO_STDINT_H 0
#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_PLATFORM_DIAG(x) do {} while (0)
#define LWIP_PLATFORM_ASSERT(x) do { NVIC_SystemReset_wrapper(); } while (0)
void NVIC_SystemReset_wrapper(void);
#define LWIP_RAND() ((u32_t)lwip_rand())
uint32_t lwip_rand(void);
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x
