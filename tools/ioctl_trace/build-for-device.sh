#!/usr/bin/env bash
# Simple builder for libioctl_trace.so, similar to top-level build-for-device.sh style
# Honors CROSS_COMPILE if set (e.g., mipsel-linux-gnu-)
set -euo pipefail
cd "$(dirname "$0")"

CC="${CROSS_COMPILE:-}mipsel-linux-gcc"
CFLAGS=${CFLAGS:-"-O2 -fPIC -Wall -Wextra -Wno-unused-parameter"}
LDFLAGS=${LDFLAGS:-"-shared -ldl"}

echo "[build] CC=$CC"
$CC $CFLAGS -c -o trace_ioctls.o trace_ioctls.c
$CC -o libioctl_trace.so trace_ioctls.o $LDFLAGS

ls -l libioctl_trace.so 2>/dev/null || true

echo "[ok] Built $(pwd)/libioctl_trace.so"
echo "Usage:"
echo "  LOG_AVPU_TRACE=/tmp/avpu_trace.log LD_PRELOAD=$(pwd)/libioctl_trace.so <oem_app> [args...]"
echo "Examples:"
echo "  LOG_AVPU_TRACE=/tmp/avpu_trace.log LD_PRELOAD=$(pwd)/libioctl_trace.so ./oem_encoder_demo"

