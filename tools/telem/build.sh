#!/bin/bash
# telemtest build for T41: build.sh <openimp build dir with libimp.so> <out>
S=/mnt/NVMe/git/scratch-clones/session-110bf918; H=/mnt/NVMe/git/timps/include/T41/1.2.0/zh
T=/mnt/NVMe/git/thingino-ciao-t41/output/vanhua_t55a_t41lq_gc5603_eth-4.4.94-uclibc-192.168.178.179-openstack
nice $T/host/bin/mipsel-linux-gcc -O2 -Wall -DPLATFORM_T41 -I$H -I$H/imp -I$(dirname $0)/../../include -o $2 $(dirname $0)/telemtest.c -L$1 -limp -lm -ldl -lpthread -lrt -Wl,--export-dynamic -Wl,--allow-shlib-undefined -Wl,-rpath-link,$T/target/usr/lib
