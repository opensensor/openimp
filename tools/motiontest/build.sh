#!/bin/bash
# build.sh [T20|T21|T23|T31|T40|T41]... : motiontest_<soc> with vendor headers (HDR_DIR) and build/<soc>/libimp.so
D=$(cd "$(dirname "$0")" && pwd); P=$D/../..; # thingino build output root: THINGINO_OUT=<dir> (required); CAM_<SOC>=<tree> overrides the newest matching tree
OUT=${THINGINO_OUT:?set THINGINO_OUT=<thingino build output dir>}
declare -A PFX=( [T20]=wyze_cam2_t20x_jxf23_rtl8189ftv-agg40- [T21]=victure_pc420_t21n_jxf23_eth+rtl8188ftv-agg40- [T23]=tplink_tapo_c500_t23n_sc2336p_atbm6012bx-agg39- [T31]=wuuk_y0510_t31x_sc4336p_ssv6158-agg40- [T40]=eufy_t8416_t40xp_sc830_aic8800d40-t40open2 [T41]=cinnado_d1_t41nq_sc5336_atbm6132bu-acmp- )
camdir() { local v=CAM_$1; [ -n "${!v}" ] && { echo "${!v}"; return; }; ls -d "$OUT/${PFX[$1]}"* 2>/dev/null | tail -1; }
I=${HDR_DIR:?set HDR_DIR=<vendor SDK include root>}
declare -A HDR=( [T20]=T20/3.12.0/zh [T21]=T21/1.0.33/zh [T23]=T23/1.3.0/zh [T31]=T31/1.1.6/en [T40]=T40/1.3.1/en [T41]=T41/1.2.6/zh )
for s in ${@:-T20 T21 T23 T31 T40 T41}; do
  c=$(camdir $s); [ -d "$c" ] || { echo "no thingino tree for $s under $OUT (set CAM_$s)"; continue; }; l=$(echo $s | tr A-Z a-z); H=$I/${HDR[$s]}
  nice "$c/host/bin/mipsel-linux-gcc" -O2 -Wall -Wextra -DPLATFORM_$s -I"$H" -I"$H/imp" -o "$D/motiontest_$l" "$D/motiontest.c" \
    -L"$P/build/$l" -limp -lpthread -lrt -lm -ldl -Wl,--export-dynamic -Wl,-rpath-link,"$c/target/usr/lib" && echo "OK $s" || echo "FAIL $s"
done
