#!/bin/bash
# build.sh T20|T21|T23|T31|T40|T41|all : rawcheck_<soc> with the thingino cross toolchain of a test camera,
# against OpenIMP's own headers and build/<soc>/libimp.so (run build-<soc>.sh first)
D=$(cd "$(dirname "$0")" && pwd); P=$D/../..; OUT=/mnt/NVMe/git/thingino-out
declare -A CAM=(
 [T20]=wyze_cam2_t20x_jxf23_rtl8189ftv-agg40-192.168.10.107
 [T21]=victure_pc420_t21n_jxf23_eth+rtl8188ftv-agg40-192.168.10.24
 [T23]=tplink_tapo_c500_t23n_sc2336p_atbm6012bx-agg39-192.168.10.23
 [T31]=wuuk_y0510_t31x_sc4336p_ssv6158-agg40-192.168.10.21
 [T40]=eufy_t8416_t40xp_sc830_aic8800d40-t40open2
 [T41]=cinnado_d1_t41nq_sc5336_atbm6132bu-acmp-192.168.10.34 )
for s in ${@:-T20 T21 T23 T31 T40 T41}; do
  [ "$s" = all ] && continue
  c=$OUT/${CAM[$s]}; l=$(echo $s | tr A-Z a-z)
  case $s in   # T40/T41: vendor headers (multi-VI ISP API), as roitest
    T40) H=/mnt/NVMe/git/timps/include/T40/1.3.1/en;;
    T41) H=/mnt/NVMe/git/timps/include/T41/1.2.6/zh;;
    *) H=$P/include;;
  esac
  nice "$c/host/bin/mipsel-linux-gcc" -O2 -Wall -Wextra -DPLATFORM_$s -I"$H" -I"$H/imp" \
    -o "$D/rawcheck_$l" "$D/rawcheck.c" -L"$P/build/$l" -limp -lpthread -lrt -lm -ldl -Wl,--export-dynamic \
    -Wl,-rpath-link,"$c/target/usr/lib" && echo "OK $s" || echo "FAIL $s"
done
