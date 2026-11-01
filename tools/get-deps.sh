#!/bin/sh
# Fetch build dependencies next to this repo (../deps)
set -e
D="$(dirname "$0")/../../deps"
mkdir -p "$D" && cd "$D"
[ -d lwip ] || git clone --depth 1 --branch STABLE-2_2_0_RELEASE https://github.com/lwip-tcpip/lwip.git
[ -d cmsis_core ] || git clone --depth 1 https://github.com/STMicroelectronics/cmsis_core.git
[ -d cmsis_device_f4 ] || git clone --depth 1 https://github.com/STMicroelectronics/cmsis_device_f4.git
