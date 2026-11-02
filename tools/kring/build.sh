#!/bin/bash
# build kring (static-free uClibc mipsel; runs on any T4x/T3x thingino image)
CAM=${CAM:?set CAM=<thingino build tree with host/bin/mipsel-linux-gcc>}
D=$(cd "$(dirname "$0")" && pwd)
"$CAM/host/bin/mipsel-linux-gcc" -O2 -Wall -o "$D/kring" "$D/kring.c" && echo OK
