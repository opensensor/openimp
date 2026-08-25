# T30/T30X status

OpenIMP has a source-only H.264 path for the legacy Ingenic T30 Helix block.
It uses the stock 3.10.14 ISP, sensor, FrameSource, reserved-memory, and
`/dev/soc_vpu` kernel drivers, but it neither links nor loads the OEM
`libimp.so` for video.

## Verified live gate

The current test device is a Wyze VDB1 with a T30X and SC4236 sensor. The
sensor name describes the test fixture, not encoder policy: OpenIMP contains
no SC4236 register table, gain LUT, tuning data, or fixed sensor dimensions.
Sensor configuration and image processing remain owned by the loaded ISP
driver and its tuning binary.

Raptor's RVD process runs with `build/t30/libimp.so` and publishes both the
1920x1080 main channel and 640x360 subchannel. The main RTSP stream reports
H.264 Main, NV12-derived 4:2:0, 1920x1080, and 25/1 fps. A five-second FFmpeg
decode completed without warnings. The main and subchannel hardware jobs
return status `0x301` and use 611-register-pair command lists.

The visible T30 FrameSource height is not its luma allocation height. For a
1920x1080 frame, chroma begins after 1088 luma rows. The T30 adapter derives
that address from macroblock-aligned geometry; using the visible height
produces a magenta band at the top of the decoded image.

## Kernel and hardware ABI

- FrameSource uses the measured 0x4c-byte T30 format descriptor and ioctls
  `0xc04c56c3`/`0x404c56c4`.
- The legacy VPU interface uses a 56-byte `channel_node` with request,
  release, and run ioctls on `/dev/soc_vpu`.
- `src/t30/t30_h264_descriptor.c` programs the T30 Helix VDMA command list
  from typed OpenIMP inputs. It does not copy a captured command template or
  access private OEM structures by hard-coded member offsets.
- SPS, PPS, slice headers, and CABAC initial state come from the GPL H.264
  helpers in `src/t30/h264enc/`.

The pinned Thingino kernel used by the current firmware is
`thingino-linux` commit `b1005ecceb87dae90e5535611c2a28fcfd37162a`.
T30 register definitions and the legacy `soc_vpu` contract were checked
against the T30 SDK 1.0.5 vendor-kernel import at commit `dc2e24d03f` and
against descriptor traces from the stock encoder on the test device. The
general Helix encoder concepts are also present in Ingenic's GPL XBOMX
sources. No proprietary binary, private structure dump, or captured
descriptor is part of the runtime implementation.

`tools/t30_soc_vpu_trace.c` is an opt-in development interposer for recording
the stock driver's channel calls and command memory. It is not built into or
loaded by OpenIMP.

## Current limitation

The native backend currently emits every picture as an IDR. That establishes
the source-only hardware, FrameSource, public IMP, and Raptor/RTSP path with a
decoder-clean image, but it is intentionally not the final bitrate or quality
configuration. P-picture motion estimation, reference rotation, GOP cadence,
and feedback rate control are the next T30 encoder milestone. Until then,
bandwidth is substantially higher than the stock GOP stream.

## Build

```sh
THINGINO_DIR=/path/to/thingino-firmware-opensensor ./build-t30.sh
```

The script searches Thingino output namespaces for `T30_TARGET`, which
defaults to the validated VDB1 profile. Set `T30_TARGET_DIR` when using a
target outside those output directories. It rejects an OEM `libimp.so`
dependency and requires complete RVD IMP symbol coverage when the target RVD
binary is available.
