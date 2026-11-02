#!/bin/bash
# build rctest_t41 (uClibc toolchain + vendor T41 1.2.6 headers; run against OpenIMP or the vendor libimp)
HDR=${HDR_DIR:?set HDR_DIR=<vendor SDK include root>}/T41/1.2.6/zh
CAM=${CAM:?set CAM=<thingino T41 build tree>}
D=$(cd "$(dirname "$0")" && pwd)
"$CAM/host/bin/mipsel-linux-gcc" -O2 -Wall -Wno-unused-function -DPLATFORM_T41 -I"$HDR" -I"$HDR/imp" \
    -o "$D/rctest_t41" "$D/rctest.c" -L"$CAM/target/usr/lib" -limp -lm -ldl -lpthread -lrt -Wl,--export-dynamic \
    -Wl,-rpath-link,"$CAM/target/usr/lib" && echo OK
