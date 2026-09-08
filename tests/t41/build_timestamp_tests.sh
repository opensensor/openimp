#!/bin/sh
set -eu
[ "$#" = 2 ] || { echo "usage: $0 CROSS_PREFIX OUTPUT_DIR" >&2; exit 2; }
cross=$1
output=$2
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mkdir -p "$output"
# Exercise the real 32-bit ABI and adapter bodies, not a host-sized imitation.
for test in frame_timestamp encoder_timestamp capture_allocator jpeg_concurrency jpeg_dct; do
    "${cross}gcc" -std=gnu99 -O2 -Wall -Wextra -Werror -DPLATFORM_T41 \
        -ffunction-sections -fdata-sections -Wl,--gc-sections -static \
        -I"$repo/include" -I"$repo/src" "$repo/tests/t41/${test}_test.c" \
        "$repo/src/openimp_profile.c" -pthread -o "$output/$test-test"
done
# The capture allocator is shared with T40; exercise that real ABI as well.
"${cross}gcc" -std=gnu99 -O2 -Wall -Wextra -Werror -DPLATFORM_T40 \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -static \
    -I"$repo/include" -I"$repo/src" "$repo/tests/t41/capture_allocator_test.c" \
    "$repo/src/openimp_profile.c" -pthread -o "$output/capture_allocator-t40-test"
