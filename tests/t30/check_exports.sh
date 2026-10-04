#!/bin/sh
# T30 IMP export set against the vendor T30 1.0.5 libimp.so.
#
# fixtures/t30_vendor_1.0.5_imp_exports.txt: the GLOBAL IMP_* names the OEM
# T30 1.0.5 uclibc libimp.so exports (names only).
# fixtures/t30_known_missing_exports.txt: the vendor names OpenIMP does not
# export yet.  The check fails when a vendor name outside that list is
# missing (regression), when a listed name is now exported (update the list),
# or when the list contains an audio name (AI/AO/AENC/ADEC must be complete).
#
#   check_exports.sh [LIBIMP]   default: ../../build/t30/libimp.so
# Skips (exit 0) when the T30 library has not been cross-built.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
lib=${1:-"$here/../../build/t30/libimp.so"}
readelf=${READELF:-readelf}
vendor="$here/fixtures/t30_vendor_1.0.5_imp_exports.txt"
known="$here/fixtures/t30_known_missing_exports.txt"
if [ ! -f "$lib" ]; then
    echo "T30 export check: $lib not built, skipped"
    exit 0
fi
export LC_ALL=C
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
"$readelf" --dyn-syms --wide "$lib" |
    awk '$7 != "UND" && $5 == "GLOBAL" && $8 ~ /^IMP_/ {sub(/@.*/, "", $8); print $8}' |
    sort -u >"$tmp/open"
sort -u "$vendor" >"$tmp/vendor"
sort -u "$known" >"$tmp/known"
comm -23 "$tmp/vendor" "$tmp/open" >"$tmp/missing"
status=0
if grep -E '^IMP_(AI|AO|AENC|ADEC)_' "$tmp/known"; then
    echo "T30 export check: audio names in the known-missing list" >&2
    status=1
fi
if ! cmp -s "$tmp/missing" "$tmp/known"; then
    echo "T30 export check: missing set differs from fixtures/$(basename "$known"):" >&2
    diff "$tmp/known" "$tmp/missing" | sed -n 's/^[<>]/ &/p' >&2
    status=1
fi
audio_v=$(grep -cE '^IMP_(AI|AO|AENC|ADEC)_' "$tmp/vendor")
audio_o=$(comm -12 "$tmp/vendor" "$tmp/open" | grep -cE '^IMP_(AI|AO|AENC|ADEC)_')
echo "T30 export check: $(comm -12 "$tmp/vendor" "$tmp/open" | wc -l)/$(wc -l <"$tmp/vendor") vendor IMP exports, audio $audio_o/$audio_v, $(wc -l <"$tmp/known") known gaps"
exit $status
