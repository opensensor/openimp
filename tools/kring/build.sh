#!/bin/bash
# build kring (static-free uClibc mipsel; runs on any T4x/T3x thingino image)
CAM=${CAM:-/mnt/NVMe/git/thingino-out/vanhua_t55a_t41lq_gc5603_eth-vendor-192.168.10.32}
D=$(cd "$(dirname "$0")" && pwd)
"$CAM/host/bin/mipsel-linux-gcc" -O2 -Wall -o "$D/kring" "$D/kring.c" && echo OK
