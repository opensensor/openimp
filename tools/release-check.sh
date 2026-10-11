#!/bin/bash
# release-check: host tests, a build of every SoC, then export-check in strict mode.
# Run before tagging a release (BUILD.md, "Release check"). Needs the cross toolchain
# environment of build-<soc>.sh (see BUILD.md); `make check` stays the fast host-only gate.
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
SOCS=${SOCS:-"T20 T21 T23 T30 T31 T40 T41"}   # T10 ships the T20 build
fail=""
if [ -z "${SKIP_CHECK:-}" ]; then
  echo "release-check: make check"
  make -C "$here" check || fail="$fail check"
fi
for s in $SOCS; do
  echo "release-check: build $s"
  "$here/build-for-device.sh" "$s" || fail="$fail build-$s"
done
echo "release-check: export-check"
EXPORT_CHECK_STRICT=1 SOCS="${EXPORT_SOCS:-T10 $SOCS}" "$here/tools/export-check.sh" || fail="$fail export-check"
if [ -n "$fail" ]; then echo "release-check: FAILED:$fail"; exit 1; fi
echo "release-check: all passed"
