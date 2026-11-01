LWIP   = ../deps/lwip/src
CMSIS  = ../deps/cmsis_core/CMSIS/Core/Include
DEV    = ../deps/cmsis_device_f4/Include
CC     = arm-none-eabi-gcc
OBJCOPY= arm-none-eabi-objcopy
CFLAGS = -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -O2 -g -std=gnu11 \
         -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
         -MMD -MP -DSTM32F407xx -Isrc -Ibuild -I$(LWIP)/include -I$(CMSIS) -I$(DEV)
LDFLAGS= -T link.ld -Wl,--gc-sections -Wl,-Map=build/openpix.map --specs=nano.specs  \
         -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
SRCS = $(wildcard src/*.c)
LW = core/init.c core/def.c core/inet_chksum.c core/ip.c core/mem.c core/memp.c core/netif.c core/pbuf.c \
     core/stats.c core/sys.c core/tcp.c core/tcp_in.c core/tcp_out.c core/timeouts.c core/udp.c \
     core/ipv4/dhcp.c core/ipv4/etharp.c core/ipv4/icmp.c core/ipv4/igmp.c core/ipv4/ip4.c \
     core/ipv4/ip4_addr.c core/ipv4/ip4_frag.c core/ipv4/acd.c netif/ethernet.c
OBJS = $(SRCS:src/%.c=build/%.o) $(LW:%.c=build/lwip/%.o)

all: build/openpix.bin

build/web_index.h: web/index.html tools/embed.py
	@mkdir -p build
	python3 tools/embed.py $< $@

build/%.o: src/%.c build/web_index.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/lwip/%.o: $(LWIP)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -w -c $< -o $@

build/openpix.elf: $(OBJS) link.ld
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	arm-none-eabi-size $@

build/openpix.bin: build/openpix.elf
	$(OBJCOPY) -O binary $< $@
	python3 tools/pad.py $@
	@ls -l $@

clean:
	rm -rf build
-include $(OBJS:.o=.d)
