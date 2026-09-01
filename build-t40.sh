#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output_dir=${T40_OUTPUT_DIR:-"$project_dir/build/t40"}
firmware_dir=${THINGINO_DIR:-}

if [ -z "$firmware_dir" ]; then
    for candidate in \
        "$project_dir/../thingino-firmware" \
        "$project_dir/../thingino-firmware-opensensor" \
        "$project_dir/../../thingino-firmware" \
        "$project_dir/../../thingino-firmware-opensensor"
    do
        if [ -d "$candidate/output/master" ]; then
            firmware_dir=$(CDPATH= cd -- "$candidate" && pwd)
            break
        fi
    done
fi

: "${firmware_dir:?set THINGINO_DIR to a Thingino firmware checkout}"
target_name=${T40_TARGET:-wyze_cam3pro_nor_t40xp_gc4653_rtl8192fs-4.4.94-uclibc}
target_dir="$firmware_dir/output/master/$target_name"
toolchain_prefix=${TOOLCHAIN_PREFIX:-"$target_dir/host/bin/mipsel-linux"}
compiler="${toolchain_prefix}-gcc"

if [ -z "${T40_HEADERS:-}" ]; then
    for candidate in \
        "$target_dir"/build/thingino-raptor-hal-*/ingenic-headers/T40/1.3.1/en
    do
        if [ -f "$candidate/imp/imp_audio.h" ]; then
            T40_HEADERS=$candidate
            break
        fi
    done
fi

: "${T40_HEADERS:?set T40_HEADERS to the T40 1.3.1 header root}"
test -x "$compiler"
test -f "$T40_HEADERS/imp/imp_audio.h"
mkdir -p "$output_dir"

base_flags="-std=gnu99 -O2 -mabi=32 -march=mips32r2 -mabicalls"
base_flags="$base_flags -fPIC -G0 -fno-stack-protector -DPLATFORM_T40"
strict_flags="$base_flags -Wall -Wextra -Werror"
repo_includes="-I$project_dir/include -I$project_dir/src"

"$compiler" $strict_flags $repo_includes \
    -c "$project_dir/src/openimp_profile.c" \
    -o "$output_dir/openimp_profile.o"

"$compiler" $strict_flags $repo_includes \
    -c "$project_dir/src/openimp_tuning.c" \
    -o "$output_dir/openimp_tuning.o"

for source in \
    openimp_p0 openimp_p1 openimp_p2_dma openimp_p2_encoder openimp_avc \
    t40_ep1 t40_stream_layout
do
    "$compiler" $strict_flags $repo_includes \
        -c "$project_dir/src/t40/$source.c" \
        -o "$output_dir/$source.o"
done

"$compiler" $base_flags $repo_includes \
    -c "$project_dir/src/alcodec/EncHwScalingList.c" \
    -o "$output_dir/backend-enc-hw-scaling-list.o"

for source in openimp_p3_controls openimp_p3_audio openimp_p3_compat
do
    "$compiler" $strict_flags -I"$T40_HEADERS" \
        -c "$project_dir/src/t40/$source.c" \
        -o "$output_dir/$source.o"
done

for source in codec al_avpu device_pool fifo hw_encoder
do
    source_path="$project_dir/src/$source.c"
    if [ "$source" = codec ]; then
        source_path="$project_dir/src/t40/codec-t40.c"
    fi
    "$compiler" $base_flags $repo_includes -Wno-stringop-overflow \
        -c "$source_path" -o "$output_dir/backend-$source.o"
done

"$compiler" -shared -nostartfiles \
    -Wl,-soname,libimp.so \
    -Wl,--version-script="$project_dir/src/t40/libimp.map" \
    -o "$output_dir/libimp.so" \
    "$output_dir/openimp_p0.o" \
    "$output_dir/openimp_profile.o" \
    "$output_dir/openimp_tuning.o" \
    "$output_dir/openimp_p1.o" \
    "$output_dir/openimp_p2_dma.o" \
    "$output_dir/openimp_p2_encoder.o" \
    "$output_dir/openimp_avc.o" \
    "$output_dir/openimp_p3_controls.o" \
    "$output_dir/openimp_p3_audio.o" \
    "$output_dir/openimp_p3_compat.o" \
    "$output_dir/t40_ep1.o" \
    "$output_dir/t40_stream_layout.o" \
    "$output_dir/backend-enc-hw-scaling-list.o" \
    "$output_dir/backend-codec.o" \
    "$output_dir/backend-al_avpu.o" \
    "$output_dir/backend-device_pool.o" \
    "$output_dir/backend-fifo.o" \
    "$output_dir/backend-hw_encoder.o" \
    -ldl -lpthread -lrt

"$compiler" $strict_flags $repo_includes \
    "$project_dir/tools/openimp-tuningd.c" "$output_dir/openimp_tuning.o" \
    -lpthread -o "$output_dir/openimp-tuningd"

imports="$output_dir/raptor-imp-imports.txt"
exports="$output_dir/libimp-exports.txt"
rvd=${T40_RVD:-"$target_dir/target/usr/bin/rvd"}
rad=${T40_RAD:-"$target_dir/target/usr/bin/rad"}

if [ -f "$rvd" ] && [ -f "$rad" ]; then
    for binary in "$rvd" "$rad"
    do
        readelf --dyn-syms --wide "$binary" |
            awk '$7 == "UND" && $8 ~ /^IMP_/ {
                sub(/@.*/, "", $8)
                print $8
            }'
    done | sort -u >"$imports"

    readelf --dyn-syms --wide "$output_dir/libimp.so" |
        awk '$7 != "UND" && $5 == "GLOBAL" && $8 ~ /^IMP_/ {print $8}' |
        sort -u >"$exports"

    missing=$(comm -23 "$imports" "$exports" || true)
    if [ -n "$missing" ]; then
        echo "T40 build is missing RVD/RAD IMP imports:" >&2
        echo "$missing" >&2
        exit 1
    fi
    echo "RVD/RAD IMP import coverage: $(wc -l <"$imports")/$(wc -l <"$imports")"
else
    echo "RVD/RAD binaries not found; skipping consumer import coverage"
fi

if readelf -d "$output_dir/libimp.so" |
    grep -q 'Shared library: \[libimp.so'
then
    echo "T40 build has an OEM libimp dependency" >&2
    exit 1
fi

sha256sum "$output_dir/libimp.so"
readelf -d "$output_dir/libimp.so" | grep -E 'SONAME|NEEDED'
