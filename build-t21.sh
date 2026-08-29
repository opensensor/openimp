#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output_dir=${T21_OUTPUT_DIR:-"$project_dir/build/t21"}
firmware_dir=${THINGINO_DIR:-}

if [ -z "$firmware_dir" ]; then
    for candidate in \
        "$project_dir/../thingino-firmware-opensensor" \
        "$project_dir/../thingino-firmware" \
        "$project_dir/../../thingino-firmware-opensensor" \
        "$project_dir/../../thingino-firmware"
    do
        if [ -d "$candidate/output" ]; then
            firmware_dir=$(CDPATH= cd -- "$candidate" && pwd)
            break
        fi
    done
fi

: "${firmware_dir:?set THINGINO_DIR to a Thingino firmware checkout}"
target_name=${T21_TARGET:-primecables_08361_t21n_sc2300_rtl8188ftv-3.10.14-uclibc}
target_dir=${T21_TARGET_DIR:-}
if [ -z "$target_dir" ]; then
    for candidate in \
        "$firmware_dir/output/master/$target_name" \
        "$firmware_dir"/output/*/"$target_name"
    do
        if [ -x "$candidate/host/bin/mipsel-linux-gcc" ]; then
            target_dir=$candidate
            break
        fi
    done
fi
: "${target_dir:?set T21_TARGET_DIR to the built Thingino target directory}"
toolchain_prefix=${TOOLCHAIN_PREFIX:-"$target_dir/host/bin/mipsel-linux"}
compiler="${toolchain_prefix}-gcc"
stripper="${toolchain_prefix}-strip"

test -x "$compiler"
mkdir -p "$output_dir"

base_flags="-std=gnu99 -O2 -mabi=32 -march=mips32r2 -mabicalls"
platform_cppflags=${T21_PLATFORM_CPPFLAGS:--DPLATFORM_T21}
base_flags="$base_flags -fPIC -G0 -fno-stack-protector $platform_cppflags -DVPU_BASE=0x13200000"
repo_includes="-I$project_dir/include -I$project_dir/src"

compile()
{
    object=$1
    source=$2
    shift 2
    "$compiler" $base_flags $repo_includes -Wall -Wextra "$@" \
        -c "$project_dir/$source" -o "$output_dir/$object.o"
}

# T21 has the legacy T20/T23 encoder ABI and Helix /dev/soc_vpu block, while
# its ISP and FrameSource ioctl layouts follow the later T31 adapter seam.
compile openimp_p0 src/t40/openimp_p0.c -Werror
compile openimp_profile src/openimp_profile.c -Werror
compile openimp_tuning src/openimp_tuning.c -Werror
compile openimp_p2_encoder src/t40/openimp_p2_encoder.c -Werror
compile openimp_avc src/t40/openimp_avc.c -Werror
compile t40_ep1 src/t40/t40_ep1.c -Werror
compile enc_hw_scaling src/alcodec/EncHwScalingList.c
compile codec src/t40/codec-t40.c -Wno-stringop-overflow
compile al_avpu src/al_avpu.c -Wno-stringop-overflow
compile device_pool src/device_pool.c -Wno-stringop-overflow
compile fifo src/fifo.c -Wno-stringop-overflow
compile hw_encoder src/hw_encoder.c -Wno-stringop-overflow

compile time64_shim src/time64_shim.c
compile kernel_interface src/kernel_interface.c
compile dma_alloc src/dma_alloc.c
compile core_device src/core/device.c
compile core_group src/core/group.c
compile core_module src/core/module.c
compile framesource src/framesource/framesource_tseries.c
compile isp src/isp/isp_tseries.c
compile t21_compat src/t31/openimp_t31_compat.c
compile t21_state src/t31/openimp_t31_state.c -Werror
compile t21_services src/t31/openimp_t31_services.c -Werror
compile t21_platform_services src/t23/openimp_t23_services.c -Werror
compile t21_persist src/t23/openimp_t23_persist.c -Werror
compile t21_audio src/t31/openimp_t31_audio.c -Werror
compile t21_helix src/t30/t30_helix_encoder.c -Werror
compile t21_h264_descriptor src/t21/t21_h264_descriptor.c -Werror
compile t30_h264_descriptor src/t30/t30_h264_descriptor.c -Werror
compile t21_h264_common src/t30/h264enc/common.c -Werror
compile t21_h264_cabac src/t30/h264enc/cabac.c -Werror
compile t21_h264_set src/t30/h264enc/set.c -Werror
compile t21_h264_slice src/t30/h264enc/slice.c -Werror
compile t21_rate_control src/t40/t31_rate_control.c -Werror

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
    "$output_dir/t21_compat.o" \
    "$output_dir/t21_state.o" \
    "$output_dir/t21_services.o" \
    "$output_dir/t21_platform_services.o" \
    "$output_dir/t21_persist.o" \
    "$output_dir/t21_audio.o" \
    "$output_dir/t21_helix.o" \
    "$output_dir/t21_h264_descriptor.o" \
    "$output_dir/t30_h264_descriptor.o" \
    "$output_dir/t21_h264_common.o" \
    "$output_dir/t21_h264_cabac.o" \
    "$output_dir/t21_h264_set.o" \
    "$output_dir/t21_h264_slice.o" \
    "$output_dir/t21_rate_control.o" \
    -ldl -lpthread -lrt

"$compiler" $base_flags $repo_includes -Wall -Wextra -Werror \
    "$project_dir/tools/openimp-tuningd.c" "$output_dir/openimp_tuning.o" \
    -lpthread -o "$output_dir/openimp-tuningd"

if [ "${T21_NO_STRIP:-0}" != "1" ]; then
    "$stripper" --strip-unneeded "$output_dir/libimp.so"
fi

if readelf -d "$output_dir/libimp.so" |
    grep -q 'Shared library: \[libimp.so'
then
    echo "T21 build has an OEM libimp dependency" >&2
    exit 1
fi

rvd=${T21_RVD:-"$target_dir/target/usr/bin/rvd"}
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
        echo "T21 build is missing RVD IMP imports:" >&2
        sed -n '1,120p' "$output_dir/rvd-imp-missing.txt" >&2
        exit 1
    fi
fi

sha256sum "$output_dir/libimp.so"
readelf -d "$output_dir/libimp.so" | grep -E 'SONAME|NEEDED'
