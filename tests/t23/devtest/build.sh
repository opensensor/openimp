#!/bin/bash
# Build isp_osd_devtest for a T23 camera: links dynamically against the libimp of a
# thingino-out T23 tree (the camera's own libimp is used at run time).
# usage: tests/t23/devtest/build.sh [OUT]   env: TREE (thingino-out T23 tree), HDR (vendor T23 headers)
D=$(cd "$(dirname "$0")" && pwd)
TREE=${TREE:-$(ls -d ${THINGINO_OUT:?set THINGINO_OUT or TREE}/jooan_a6m_t23n_sc1a4t_atbm6012bx-agg4* 2>/dev/null | tail -1)}
HDR=${HDR:-${HDR_DIR:?set HDR or HDR_DIR}/T23/1.3.0/zh}
OUT=${1:-$D/isp_osd_devtest}
nice "$TREE/host/bin/mipsel-linux-gcc" -O2 -Wall -Wextra -DPLATFORM_T23 -I"$HDR" -I"$HDR/imp" \
    -o "$OUT" "$D/isp_osd_devtest.c" -L"$TREE/target/usr/lib" -limp -lm -ldl -lpthread -lrt \
    -Wl,-rpath-link,"$TREE/target/usr/lib" && echo "OK $OUT"
