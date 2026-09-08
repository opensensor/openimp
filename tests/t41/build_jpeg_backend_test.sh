#!/bin/sh
set -eu
[ "$#" = 3 ] || { echo "usage: $0 CROSS_PREFIX T41_BUILD_DIR OUTPUT" >&2; exit 2; }
cross=$1
build=$2
output=$3
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
# Use the exact production objects, including the real AL dispatch and JPEG
# encoder. These internal AL symbols are intentionally hidden in libimp.so.
set --
for object in openimp_p0 openimp_profile openimp_tuning openimp_p1 \
    openimp_p2_dma openimp_p2_encoder openimp_avc openimp_p3_controls \
    openimp_p3_audio openimp_p3_compat t40_ep1 t41_command_layout \
    t41_command_builder t41_hw_rate_control t41_rate_control t41_stream_layout \
    backend-enc-hw-scaling-list backend-codec backend-al_avpu \
    backend-device_pool backend-fifo backend-hw_encoder; do
    if [ "$object" = backend-hw_encoder ] && [ -n "${JPEG_HW_OBJECT:-}" ]; then
        set -- "$@" "$JPEG_HW_OBJECT"
    else
        set -- "$@" "$build/$object.o"
    fi
done
"${cross}gcc" -std=gnu99 -O2 -Wall -Wextra -Werror -DPLATFORM_T41 \
    -static -I"$repo/include" -I"$repo/src" \
    "$repo/tests/t41/jpeg_backend_test.c" "$@" \
    -ldl -lpthread -lrt -o "$output"
