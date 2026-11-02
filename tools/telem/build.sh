#!/bin/bash
# telemtest build for T41: build.sh <openimp build dir with libimp.so> <out>
H=${HDR_DIR:?set HDR_DIR=<vendor SDK include root>}/T41/1.2.0/zh
T=${CAM:?set CAM=<thingino T41 build tree>}
nice $T/host/bin/mipsel-linux-gcc -O2 -Wall -DPLATFORM_T41 -I$H -I$H/imp -I$(dirname $0)/../../include -o $2 $(dirname $0)/telemtest.c -L$1 -limp -lm -ldl -lpthread -lrt -Wl,--export-dynamic -Wl,--allow-shlib-undefined -Wl,-rpath-link,$T/target/usr/lib
