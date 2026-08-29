#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

# T20 shares the T21 userspace implementation, while PLATFORM_T20 selects
# the OEM T20 V4L2-facing ISP and frame-source ABI exposed by tx-isp-t20.
export T21_PLATFORM_CPPFLAGS="-DPLATFORM_T20 -DPLATFORM_T21"
export T21_OUTPUT_DIR=${T20_OUTPUT_DIR:-"$project_dir/build/t20"}
export T21_TARGET=${T20_TARGET:-wyze_cam2_t20x_jxf23_rtl8189ftv-3.10.14-uclibc}
if [ -n "${T20_TARGET_DIR:-}" ]; then
    export T21_TARGET_DIR=$T20_TARGET_DIR
fi
if [ -n "${T20_RVD:-}" ]; then
    export T21_RVD=$T20_RVD
fi

exec "$project_dir/build-t21.sh"
