#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output_dir=${T31_OUTPUT_DIR:-"$project_dir/build/t31"}
firmware_dir=${THINGINO_DIR:-}

if [ -z "$firmware_dir" ]; then
    for candidate in \
        "$project_dir/../thingino-firmware-opensensor" \
        "$project_dir/../thingino-firmware" \
        "$project_dir/../../thingino-firmware-opensensor" \
        "$project_dir/../../thingino-firmware"
    do
        if [ -d "$candidate/output/master" ]; then
            firmware_dir=$(CDPATH= cd -- "$candidate" && pwd)
            break
        fi
    done
fi

: "${firmware_dir:?set THINGINO_DIR to a Thingino firmware checkout}"
target_name=${T31_TARGET:-wyze_cam3_t31x_gc2053_rtl8189ftv-3.10.14-uclibc}
target_dir="$firmware_dir/output/master/$target_name"
toolchain_prefix=${TOOLCHAIN_PREFIX:-"$target_dir/host/bin/mipsel-linux"}
compiler="${toolchain_prefix}-gcc"
stripper="${toolchain_prefix}-strip"

test -x "$compiler"
mkdir -p "$output_dir"

base_flags="-std=gnu99 -O2 -mabi=32 -march=mips32r2 -mabicalls"
base_flags="$base_flags -fPIC -G0 -fno-stack-protector -DPLATFORM_T31"
repo_includes="-I$project_dir/include -I$project_dir/src"

compile()
{
    object=$1
    source=$2
    shift 2
    "$compiler" $base_flags $repo_includes -Wall -Wextra "$@" \
        -c "$project_dir/$source" -o "$output_dir/$object.o"
}

# One encoder implementation: the proven T40 public graph and AVPU backend.
compile openimp_p0 src/t40/openimp_p0.c -Werror
compile openimp_profile src/openimp_profile.c -Werror
compile openimp_tuning src/openimp_tuning.c -Werror
compile openimp_p2_encoder src/t40/openimp_p2_encoder.c -Werror
compile openimp_avc src/t40/openimp_avc.c -Werror
compile t40_ep1 src/t40/t40_ep1.c -Werror
compile t31_stream_layout src/t40/t31_stream_layout.c -Werror
compile enc_hw_scaling src/alcodec/EncHwScalingList.c
compile codec src/t40/codec-t40.c -Wno-stringop-overflow
compile al_avpu src/al_avpu.c -Wno-stringop-overflow
compile device_pool src/device_pool.c -Wno-stringop-overflow
compile fifo src/fifo.c -Wno-stringop-overflow
compile hw_encoder src/hw_encoder.c -Wno-stringop-overflow

# T31-only stock-driver seam: ISP/FrameSource ioctl layout, rmem/cache, and
# the small module state those adapters require. No second scheduler or RC.
compile time64_shim src/time64_shim.c
compile kernel_interface src/kernel_interface.c
compile dma_alloc src/dma_alloc.c
compile core_device src/core/device.c
compile core_group src/core/group.c
compile core_module src/core/module.c
compile framesource src/framesource/framesource_tseries.c
compile isp src/isp/isp_tseries.c
compile t31_compat src/t31/openimp_t31_compat.c
compile t31_state src/t31/openimp_t31_state.c -Werror
compile t31_services src/t31/openimp_t31_services.c -Werror
compile t31_audio src/t31/openimp_t31_audio.c -Werror

"$compiler" -shared -nostartfiles \
    -Wl,-soname,libimp.so \
    -Wl,--version-script="$project_dir/src/t40/libimp.map" \
    -o "$output_dir/libimp.so" \
    "$output_dir/openimp_p0.o" \
    "$output_dir/openimp_profile.o" \
    "$output_dir/openimp_tuning.o" \
    "$output_dir/openimp_p2_encoder.o" \
    "$output_dir/openimp_avc.o" \
    "$output_dir/t40_ep1.o" \
    "$output_dir/t31_stream_layout.o" \
    "$output_dir/enc_hw_scaling.o" \
    "$output_dir/codec.o" \
    "$output_dir/al_avpu.o" \
    "$output_dir/device_pool.o" \
    "$output_dir/fifo.o" \
    "$output_dir/hw_encoder.o" \
    "$output_dir/time64_shim.o" \
    "$output_dir/kernel_interface.o" \
    "$output_dir/dma_alloc.o" \
    "$output_dir/core_device.o" \
    "$output_dir/core_group.o" \
    "$output_dir/core_module.o" \
    "$output_dir/framesource.o" \
    "$output_dir/isp.o" \
    "$output_dir/t31_compat.o" \
    "$output_dir/t31_state.o" \
    "$output_dir/t31_services.o" \
    "$output_dir/t31_audio.o" \
    -ldl -lpthread -lrt

"$compiler" $base_flags $repo_includes -Wall -Wextra -Werror \
    "$project_dir/tools/openimp-tuningd.c" "$output_dir/openimp_tuning.o" \
    -lpthread -o "$output_dir/openimp-tuningd"

"$stripper" --strip-unneeded "$output_dir/libimp.so"

if readelf -d "$output_dir/libimp.so" |
    grep -q 'Shared library: \[libimp.so'
then
    echo "T31 build has an OEM libimp dependency" >&2
    exit 1
fi

rvd=${T31_RVD:-"$target_dir/target/usr/bin/rvd"}
if [ -f "$rvd" ]; then
    readelf --dyn-syms --wide "$rvd" |
        awk '$7 == "UND" && $8 ~ /^IMP_/ {
            sub(/@.*/, "", $8)
            print $8
        }' | sort -u >"$output_dir/rvd-imp-imports.txt"
    readelf --dyn-syms --wide "$output_dir/libimp.so" |
        awk '$7 != "UND" && $5 == "GLOBAL" && $8 ~ /^IMP_/ {print $8}' |
        sort -u >"$output_dir/libimp-exports.txt"
    comm -23 "$output_dir/rvd-imp-imports.txt" \
        "$output_dir/libimp-exports.txt" >"$output_dir/rvd-imp-missing.txt"
    echo "RVD IMP coverage: $(comm -12 "$output_dir/rvd-imp-imports.txt" "$output_dir/libimp-exports.txt" | wc -l)/$(wc -l <"$output_dir/rvd-imp-imports.txt")"
    if [ -s "$output_dir/rvd-imp-missing.txt" ]; then
        echo "T31 build is missing RVD IMP imports:" >&2
        cat "$output_dir/rvd-imp-missing.txt" >&2
        exit 1
    fi
fi

rad=${T31_RAD:-"$target_dir/target/usr/bin/rad"}
if [ -f "$rad" ]; then
    readelf --dyn-syms --wide "$rad" |
        awk '$7 == "UND" && $8 ~ /^IMP_(AI|AO)_/ {
            sub(/@.*/, "", $8)
            print $8
        }' | sort -u >"$output_dir/rad-audio-imports.txt"
    readelf --dyn-syms --wide "$output_dir/libimp.so" |
        awk '$7 != "UND" && $5 == "GLOBAL" && $8 ~ /^IMP_(AI|AO)_/ {print $8}' |
        sort -u >"$output_dir/libimp-audio-exports.txt"
    comm -23 "$output_dir/rad-audio-imports.txt" \
        "$output_dir/libimp-audio-exports.txt" >"$output_dir/rad-audio-missing.txt"
    echo "RAD audio IMP coverage: $(comm -12 "$output_dir/rad-audio-imports.txt" "$output_dir/libimp-audio-exports.txt" | wc -l)/$(wc -l <"$output_dir/rad-audio-imports.txt")"
    if [ -s "$output_dir/rad-audio-missing.txt" ]; then
        echo "T31 build is missing RAD audio IMP imports:" >&2
        cat "$output_dir/rad-audio-missing.txt" >&2
        exit 1
    fi
fi

sha256sum "$output_dir/libimp.so"
readelf -d "$output_dir/libimp.so" | grep -E 'SONAME|NEEDED'
