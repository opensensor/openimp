#!/bin/sh
set -eu
# The readelf checks below parse its English output.
LC_ALL=C
export LC_ALL

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
output_dir=${T23_OUTPUT_DIR:-"$project_dir/build/t23"}
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
target_name=${T23_TARGET:-cinnado_d1_t23n_sc2336_atbm6012bx-3.10.14-uclibc}
target_dir=${T23_TARGET_DIR:-"$firmware_dir/output/master/$target_name"}
toolchain_prefix=${TOOLCHAIN_PREFIX:-"$target_dir/host/bin/mipsel-linux"}
compiler="${toolchain_prefix}-gcc"
stripper="${toolchain_prefix}-strip"

test -x "$compiler"
mkdir -p "$output_dir"

base_flags="-std=gnu99 -O2 -mabi=32 -march=mips32r2 -mabicalls"
base_flags="$base_flags -fPIC -G0 -fno-stack-protector -DPLATFORM_T23"
# JPEG runs on the Helix VPU (src/t30/helix_jpeg.c).  OPENIMP_SW_JPEG=1 also
# builds the software baseline encoder in, as a fallback for pictures the VPU
# cannot take and for OPENIMP_HELIX_HW_JPEG=0 at run time; the default leaves
# it out, as the stock libimp has none.
case "${OPENIMP_SW_JPEG:-0}" in
    0|1) ;;
    *) echo "OPENIMP_SW_JPEG must be 0 or 1" >&2; exit 1 ;;
esac
base_flags="$base_flags -DOPENIMP_SW_JPEG=${OPENIMP_SW_JPEG:-0}"

# H.264 backend when OPENIMP_T23_ENCODER is not set at run time: "worker"
# (the OEM encoder in openimp-t23-helixd) or "native" (OpenIMP's own Helix
# command lists over /dev/soc_vpu).
case "${T23_DEFAULT_ENCODER:-worker}" in
    worker) default_encoder_flag=-DOPENIMP_T23_DEFAULT_NATIVE=0 ;;
    native) default_encoder_flag=-DOPENIMP_T23_DEFAULT_NATIVE=1 ;;
    *)
        echo "T23_DEFAULT_ENCODER must be worker or native" >&2
        exit 1
        ;;
esac
repo_includes="-I$project_dir/include -I$project_dir/src"

compile()
{
    object=$1
    source=$2
    shift 2
    "$compiler" $base_flags $repo_includes -Wall -Wextra "$@" \
        -c "$project_dir/$source" -o "$output_dir/$object.o"
}

# The T23 public ABI feeds the recovered AVPU backend through the T-series
# stock-driver seam.  Audio reuses the T31 implementation, built against the
# T23 OSS3 /dev/dsp ABI (PLATFORM_T23 selects it in openimp_t31_audio.c).
compile openimp_p0 src/t40/openimp_p0.c -Werror
compile openimp_profile src/openimp_profile.c -Werror
compile openimp_tuning src/openimp_tuning.c -Werror
compile openimp_p2_encoder src/t40/openimp_p2_encoder.c -Werror
compile openimp_avc src/t40/openimp_avc.c -Werror
compile t40_ep1 src/t40/t40_ep1.c -Werror
compile enc_hw_scaling src/alcodec/EncHwScalingList.c
compile codec src/t40/codec-t40.c -Wno-stringop-overflow \
    "$default_encoder_flag"
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
compile isp_t23_tuning src/isp/isp_t23_tuning.c -Werror
compile t23_compat src/t31/openimp_t31_compat.c
compile t23_state src/t31/openimp_t31_state.c -Werror
compile t23_services src/t31/openimp_t31_services.c -Werror
compile t23_ivs src/t31/openimp_t31_ivs.c -Werror
compile t23_ivs_move src/t31/openimp_t31_ivs_move.c -Werror
compile t23_platform_services src/t23/openimp_t23_services.c -Werror
compile t23_helix_bridge src/t23/openimp_t23_helix_bridge.c -Werror
compile t23_persist src/t23/openimp_t23_persist.c -Werror
compile t23_audio src/t31/openimp_t31_audio.c -Werror
compile openimp_aec src/audio/openimp_aec.c -Werror -I"$project_dir/src/audio"
compile t23_yuv src/t23/openimp_t23_yuv.c -Werror
compile t23_osd src/t23/openimp_t23_osd.c -Werror
compile t23_isp_osd src/t23/openimp_t23_isp_osd.c -Werror
compile audio_codec src/audio/openimp_audio_codec.c -Werror
compile audio_enc_dec src/audio/openimp_audio_enc_dec.c -Werror
compile t23_audio_ext src/t23/openimp_t23_audio_ext.c -Werror
compile t23_encoder src/t23/openimp_t23_encoder.c -Werror
compile t23_decoder src/t23/openimp_t23_decoder.c -Werror
compile t23_misc src/t23/openimp_t23_misc.c -Werror
# Native Helix H.264: the T30/T21 encoder with the T21-family command list.
compile t23_helix_native src/t30/t30_helix_encoder.c -Werror
compile t23_h264_descriptor src/t21/t21_h264_descriptor.c -Werror
compile t23_h264_common src/t30/h264enc/common.c -Werror
compile t23_h264_cabac src/t30/h264enc/cabac.c -Werror
compile t23_h264_set src/t30/h264enc/set.c -Werror
compile t23_h264_slice src/t30/h264enc/slice.c -Werror
compile t23_rate_control src/t40/t31_rate_control.c -Werror
compile t23_helix_jpeg src/t30/helix_jpeg.c -Werror

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
    "$output_dir/isp_t23_tuning.o" \
    "$output_dir/t23_compat.o" \
    "$output_dir/t23_state.o" \
    "$output_dir/t23_services.o" \
    "$output_dir/t23_ivs.o" \
    "$output_dir/t23_ivs_move.o" \
    "$output_dir/t23_platform_services.o" \
    "$output_dir/t23_helix_bridge.o" \
    "$output_dir/t23_persist.o" \
    "$output_dir/t23_audio.o" \
    "$output_dir/openimp_aec.o" \
    "$output_dir/t23_yuv.o" \
    "$output_dir/t23_osd.o" \
    "$output_dir/t23_isp_osd.o" \
    "$output_dir/audio_codec.o" \
    "$output_dir/audio_enc_dec.o" \
    "$output_dir/t23_audio_ext.o" \
    "$output_dir/t23_encoder.o" \
    "$output_dir/t23_decoder.o" \
    "$output_dir/t23_misc.o" \
    "$output_dir/t23_helix_native.o" \
    "$output_dir/t23_h264_descriptor.o" \
    "$output_dir/t23_h264_common.o" \
    "$output_dir/t23_h264_cabac.o" \
    "$output_dir/t23_h264_set.o" \
    "$output_dir/t23_h264_slice.o" \
    "$output_dir/t23_rate_control.o" \
    "$output_dir/t23_helix_jpeg.o" \
    -ldl -lpthread -lrt

"$compiler" $base_flags $repo_includes -Wall -Wextra -Werror \
    "$project_dir/tools/openimp-tuningd.c" "$output_dir/openimp_tuning.o" \
    -lpthread -o "$output_dir/openimp-tuningd"

"$stripper" --strip-unneeded "$output_dir/libimp.so"

# On-device bring-up test of the native Helix encoder (not installed by the
# firmware package; copy it to the camera by hand, see
# docs/T23_NATIVE_HELIX.md).
"$compiler" $base_flags $repo_includes -Wall -Wextra -Werror \
    "$project_dir/tools/t23_helix_selftest.c" \
    "$output_dir/t23_helix_native.o" "$output_dir/t23_h264_descriptor.o" \
    "$output_dir/t23_h264_common.o" "$output_dir/t23_h264_cabac.o" \
    "$output_dir/t23_h264_set.o" "$output_dir/t23_h264_slice.o" \
    "$output_dir/t23_rate_control.o" "$output_dir/dma_alloc.o" \
    -lpthread -o "$output_dir/openimp-t23-helix-selftest"
"$stripper" --strip-unneeded "$output_dir/openimp-t23-helix-selftest"

if readelf -d "$output_dir/libimp.so" |
    grep -q 'Shared library: \[libimp.so'
then
    echo "T23 build has an OEM libimp dependency" >&2
    exit 1
fi

for symbol in IMP_AI_GetFrame IMP_AI_PollingFrame IMP_AO_SendFrame \
    IMP_AO_FlushChnBuf IMP_Encoder_YuvInit IMP_Encoder_YuvEncode \
    IMP_Encoder_VbmAlloc IMP_Encoder_InputJpege \
    IMP_IVS_CreateMoveInterface IMP_IVS_PollingResult IMP_IVS_GetResult
do
    if ! readelf --dyn-syms --wide "$output_dir/libimp.so" |
        awk -v s="$symbol" '$7 != "UND" && $8 == s {found=1} END {exit !found}'
    then
        echo "T23 build does not export $symbol" >&2
        exit 1
    fi
done

rvd=${T23_RVD:-"$target_dir/target/usr/bin/rvd"}
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
        echo "T23 build is missing RVD IMP imports:" >&2
        cat "$output_dir/rvd-imp-missing.txt" >&2
        exit 1
    fi
fi

sha256sum "$output_dir/libimp.so"
readelf -d "$output_dir/libimp.so" | grep -E 'SONAME|NEEDED'

# Keep the proprietary T23 Helix implementation in a clean process.  RVD loads
# OpenIMP from /usr/lib, while this small worker runs on the OEM libimp from
# /opt/openimp-t23 and exchanges raw frames through tmpfs-backed shared
# memory.  Its search path names only /opt/openimp-t23: a DT_RPATH of
# /usr/lib wins over LD_LIBRARY_PATH and would load OpenIMP's libimp.so,
# which the worker refuses.  Buildroot's fix-rpath keeps the directory
# (it exists in the target) and may turn it into DT_RUNPATH, after which
# the LD_LIBRARY_PATH the bridge sets still selects the same directory.
# Link it against the OEM libimp.  With per-package directories the
# package's own staging holds the OEM copy at build time (ingenic-lib), while
# $target_dir/target only exists once the image is assembled, so a caller may
# point T23_OEM_LIB_DIR there.
oem_lib_dir=${T23_OEM_LIB_DIR:-"$target_dir/target/usr/lib"}
if [ ! -f "$oem_lib_dir/libimp.so" ] ||
    readelf --dyn-syms --wide "$oem_lib_dir/libimp.so" |
        awk '$7 != "UND" && $8 == "OpenIMP_P0_GetState" {found=1} END {exit !found}'
then
    echo "T23 Helix worker needs the OEM libimp.so in $oem_lib_dir" \
        "(set T23_OEM_LIB_DIR)" >&2
    exit 1
fi
"$compiler" $base_flags $repo_includes -Wall -Wextra -Werror \
    "$project_dir/src/t23/openimp_t23_helix_worker.c" \
    -L"$oem_lib_dir" \
    -Wl,--disable-new-dtags -Wl,-rpath,/opt/openimp-t23 \
    -Wl,-rpath-link,"$oem_lib_dir" \
    -Wl,--dynamic-list="$project_dir/src/t23/openimp_t23_helix_worker.dynlist" \
    -limp -lalog -lpthread -ldl \
    -o "$output_dir/openimp-t23-helixd"
"$stripper" --strip-unneeded "$output_dir/openimp-t23-helixd"
if ! readelf -d "$output_dir/openimp-t23-helixd" |
    grep -q 'Shared library: \[libimp.so\]'
then
    echo "T23 Helix worker is not linked to the OEM libimp ABI" >&2
    exit 1
fi
if ! readelf -d "$output_dir/openimp-t23-helixd" |
    grep -q 'Library rpath: \[/opt/openimp-t23\]'
then
    echo "T23 Helix worker does not search /opt/openimp-t23 first" >&2
    exit 1
fi
# The worker confines the OEM rmem allocator by interposing these.
for symbol in continuous_init continuous_alloc
do
    if ! readelf --dyn-syms --wide "$output_dir/openimp-t23-helixd" |
        awk -v s="$symbol" '$7 != "UND" && $8 == s {found=1} END {exit !found}'
    then
        echo "T23 Helix worker does not export $symbol" >&2
        exit 1
    fi
done
ls -l "$output_dir/openimp-t23-helixd"
