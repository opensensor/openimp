#!/bin/bash
# build roitest_<soc> (cross toolchains + libimp from thingino-out, vendor headers)
HDR=${HDR_DIR:?set HDR_DIR=<vendor SDK include root>}
OUT=${THINGINO_OUT:?set THINGINO_OUT=<thingino build output dir>}
# tree names: ROI_<SOC>=<dir name under THINGINO_OUT> overrides the newest match
D=$(cd "$(dirname "$0")" && pwd)
build() { # soc headerdir camdir
  local soc=$1 inc=$2 v=ROI_$1 cam; cam=${!v:+$OUT/${!v}}; [ -n "$cam" ] || cam=$(ls -d "$OUT/"$3* 2>/dev/null | tail -1)
  "$cam/host/bin/mipsel-linux-gcc" -O2 -Wall -Wno-unused-function -DPLATFORM_$soc -I"$HDR/$inc" -I"$HDR/$inc/imp" \
      -o "$D/roitest_$(echo $soc | tr A-Z a-z)" "$D/roitest.c" -L"$cam/target/usr/lib" -limp -lm -ldl -lpthread -lrt -Wl,--export-dynamic \
      -Wl,-rpath-link,"$cam/target/usr/lib" && echo "OK $soc" || echo "FAIL $soc"
}
case "${1:-all}" in
  T21|all) build T21 T21/1.0.33/zh ""victure_pc420_t21n_jxf23_eth+rtl8188ftv-"";;
esac
case "${1:-all}" in
  T23|all) build T23 T23/1.3.0/zh jooan_a6m_t23n_sc1a4t_atbm6012bx-;;
esac
case "${1:-all}" in
  T31|all) build T31 T31/1.1.6/en wuuk_y0510_t31x_sc4336p_ssv6158-;;
esac
case "${1:-all}" in
  T41|all) build T41 T41/1.2.6/zh vanhua_t55a_t41lq_gc5603_eth-;;
esac
