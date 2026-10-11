# T30/T30X status

OpenIMP has a source-only H.264 path for the legacy Ingenic T30 Helix block.
It uses the compatible 3.10.14 ISP, sensor, FrameSource, reserved-memory, and
`/dev/soc_vpu` interfaces, but it neither links nor loads the OEM `libimp.so`
for video.

## Verified live gate

The current test device is a Wyze VDB1 with a T30X and SC4236 sensor. The
sensor name describes the test fixture, not encoder policy: OpenIMP contains
no SC4236 register table, gain LUT, tuning data, or fixed sensor dimensions.
Sensor configuration and image processing remain owned by the loaded ISP
driver and its tuning binary. The verified persistent boot path loads the
source-built `open-tx-isp` T30 core with the board's sensor module, then starts
RVD against the source-built OpenIMP library.

Raptor's RVD process runs with `build/t30/libimp.so` and publishes both the
1920x1080 main channel and 640x360 subchannel as High-profile H.264 4:2:0.
Both rings sustain 25.3 fps. The main channel runs at about 2.9 Mbit/s and the
subchannel at about 0.85 Mbit/s with 25-picture GOPs, compared with about
3.3 Mbit/s and the same GOP cadence from the OEM main encoder under the same
Raptor configuration.
In a sustained gate, both channels ran concurrently for 1,500 frames (60
seconds) at exactly 25.0 fps. Each stream contained the expected 60 IDRs and
1,440 P pictures; RVD/RSD remained healthy and the kernel reported no VPU or
Helix faults. Camera-local captures starting at SPS/PPS/IDR also decoded every
I/P picture with zero FFmpeg diagnostics. The main and subchannel hardware jobs
return status `0x301` and use 611-pair IDR or 785-pair P command lists.

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
- P pictures use the GPL Helix H.264 interpolation definitions, two aligned
  reconstruction surfaces, and explicit previous/output plane addresses.
  The same slow GOP-level feedback controller used by T31 supplies CBR/VBR
  QP selection; fixed-QP mode remains fixed.
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

The lab's current IPv6/WireGuard path is lossy enough that multi-megabit live
RTSP cannot be evaluated at real time from the remote client: 1200-byte ICMP
tests showed 16 percent loss while camera-local ring capture remained at full
rate. Runtime codec gates therefore use lossless camera-local capture followed
by offline decoding, separately from the network-path observation.

## Vendor API coverage (host and build evidence, 2026-10-10)

`build-t30.sh` checks the exports of `libimp.so` against the T30 1.0.5 vendor
library (`tests/t30/fixtures/t30_vendor_1.0.5_imp_exports.txt`, 334 names).
Gap work of 2026-10-10 (no T30 camera, vendor libimp 1.0.5 disassembled and
compared with T21 1.0.33 and T31 1.1.6): 317 of 334 names are exported (was
271), audio 68 of 68. None of the 63 names that were missing is imported by
timps, prudynt or the raptor RVD.

- Host-tested, same code as T20/T21 in the vendor library:
  `Encoder_Get/SetChnFrmUsedMode`, `Encoder_GetGOPSize`,
  `ISP_Tuning_SetDPStrength`, `SetAntiFogAttr`, `Get/SetAeStrategy`,
  `Awb_Get/SetCwfShift`, `Get/SetWDRAttr`, `Get/SetISPHVflip` (ISP calls
  against a fake tuning ioctl: whether the open T30 driver serves each control
  is not checked), `OSD_AttachToGroup`.
- DMIC (21 functions): the T31 implementation is compiled into the T30 build.
  The T30 libimp has the same DMIC code and the T30 oss2 `/dev/dsp` driver the
  same DMIC ioctls; one microphone per device on T30. Host test with a fake
  `/dev/dsp`; the AEC calls are not run.
- Stored only (the effect is in the closed vendor encoder or needs a device):
  `Encoder_Get/SetChangeRef`, `Get/SetFisheyeEnableStatus`,
  `SetChnHSkipBlackEnhance`, `Get/SetChnHSkip` (the IDR period is not handed
  to the Helix encoder), `SetPoolSize`, `SetChnRcTrigLevel`,
  `ISP_Tuning_SetTemperDnsCtl`, `SetAFThreshold`.
- Still missing (17 names): `Decoder_*` (8) and `Encoder_Get/SetChnCrop`,
  `ISP_Tuning_SetISPLDC` (need a T30 device or driver work);
  `Encoder_Enable/DisableAllNCUDenoise`, `PadFrame`, `SetPadFrameAttr`,
  `Get/SetRencFirstFrameMult` (not worth it: closed-encoder or chip-specific).

Nothing in this list has run on a T30 camera. Wishlist for a T30 tester: the
tuning controls above against the open T30 driver, a DMIC capture, and
`Encoder_SetChnHSkip` with the IDR period.

## Build

```sh
THINGINO_DIR=/path/to/thingino-firmware-opensensor ./build-t30.sh
```

The script searches Thingino output namespaces for `T30_TARGET`, which
defaults to the validated VDB1 profile. Set `T30_TARGET_DIR` when using a
target outside those output directories. It rejects an OEM `libimp.so`
dependency and requires complete RVD IMP symbol coverage when the target RVD
binary is available.

## Help wanted: T30 testers

We have no T30 camera in the lab. If you own a T30/T30X camera running Thingino
with the vendor stack, you can test OpenIMP without flashing anything: the test
kit in `tools/testkit/t30/` (see its `README.md`) runs the vendor API test
against the vendor and the OpenIMP `libimp.so`, a short H.264 encode, and packs
the logs, `dmesg` and `/proc/jz/isp/isp-m0` into one tar.gz for you to review
and send. It restores the streamer and changes no configuration.
