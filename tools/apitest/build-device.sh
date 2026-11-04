#!/bin/sh
# build-device.sh - cross-build apitest for one SoC against the libimp that OpenIMP just built (make apitest, thingino package).
#   env: PLATFORM      SoC whose libimp is linked (build/<soc>/libimp.so); T10 uses the T20 build
#        APITEST_SOC   SoC whose vendor headers/API apitest follows (default PLATFORM; T10 on the T20 build: APITEST_SOC=T10)
#        TOOLCHAIN_PREFIX  e.g. /path/host/bin/mipsel-linux  (compiler = ${TOOLCHAIN_PREFIX}-gcc)
#        APITEST_HEADERS   root of the vendor headers (thingino-raptor-hal: ingenic-headers/), holds <SoC>/<version>/<lang>/{imp,sysutils}
#        LIBDIR        directory with libimp.so (default build/<soc>)
#        OUT           output binary (default build/<soc>/openimp-apitest)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
platform=$(echo "${PLATFORM:?set PLATFORM}" | tr a-z A-Z)
soc=$(echo "${APITEST_SOC:-$platform}" | tr a-z A-Z)
lc=$(echo "$platform" | tr A-Z a-z)
: "${TOOLCHAIN_PREFIX:?set TOOLCHAIN_PREFIX}" "${APITEST_HEADERS:?set APITEST_HEADERS}"
out=${OUT:-$root/build/$lc/openimp-apitest}
libdir=${LIBDIR:-$root/build/$lc}
case $soc in
  T10) hv=T10/3.12.0/zh;  defs="-DPLATFORM_T10";;
  T20) hv=T20/3.12.0/zh;  defs="-DPLATFORM_T20";;
  T21) hv=T21/1.0.33/zh;  defs="-DPLATFORM_T21";;
  T23) hv=T23/1.3.0/zh;   defs="-DPLATFORM_T23";;
  T30) hv=T30/1.0.5/zh;   defs="-DPLATFORM_T30";;
  T31) hv=T31/1.1.6/en;   defs="-DPLATFORM_T31";;
  T40) hv=T40/1.3.1/en;   defs="-DPLATFORM_T41 -DAPITEST_T40";;
  T41) hv=T41/1.2.6/zh;   defs="-DPLATFORM_T41";;
  *) echo "unsupported SoC $soc" >&2; exit 2;;
esac
inc=$APITEST_HEADERS/$hv
[ -f "$inc/imp/imp_isp.h" ] || { echo "vendor headers not found: $inc" >&2; exit 2; }
[ -f "$libdir/libimp.so" ] || { echo "no $libdir/libimp.so (build the library first)" >&2; exit 2; }
src="apitest.c at_sys.c at_enc.c at_osd_ivs.c at_audio.c at_isp.c at_isp_rt.c at_cases.c at_su.c at_picture.c at_t40dev.c at_tdwatch.c"
files=""; for f in $src; do files="$files $here/$f"; done
mkdir -p "$(dirname "$out")"
${TOOLCHAIN_PREFIX}-gcc -Os -Wall -Wno-unused-function -Wno-address -Wno-format-zero-length -Wno-dangling-else -Wno-misleading-indentation \
  $defs -I"$inc" -I"$inc/imp" -I"$root/include" -I"$here" -o "$out" $files \
  -L"$libdir" -limp -lm -ldl -lpthread -lrt -Wl,-rpath-link,"$libdir"
${TOOLCHAIN_PREFIX}-strip "$out" 2>/dev/null || true
echo "built $out ($soc)"
