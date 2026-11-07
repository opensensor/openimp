# T30/T30X status

OpenIMP has its own source-built H.264 path for the legacy Ingenic T30 Helix block
and an H.265 path on the Radix block ([T30_HEVC.md](T30_HEVC.md)).
It uses the compatible 3.10.14 ISP, sensor, FrameSource, reserved-memory, and
`/dev/soc_vpu` interfaces, but it neither links nor loads the OEM `libimp.so`
for video.

## Device results on the T30X lab camera (2026-11-03 to 2026-11-07)

Since 2026-11-03 the lab has a T30X camera (Jooan, SC4236, remote); T30 is an RC3 gate SoC and all 230
streamer-used function cells work. Method: runtime module swap to the open stack (no flash), audit batches
b1-b51, vendor stack on the same board as reference. Results (details and numbers: `CHANGELOG.md`,
[FEATURE_MATRIX.md](FEATURE_MATRIX.md), [releases/RC3.md](releases/RC3.md)):

- H.264 at 2048x1536, 1080p and 720p plus a 640x360 sub-stream; heights that are not a multiple of 16 encode again (#209).
- H.265 on the Radix encoder, vendor-equal CBR (within 0.1-2.4 %), 2048x1536 where the vendor delivers nothing (#208); live rate-control change on H.265 vendor-equal (#228).
- OEM picture rate controller (eprc T30 revision, bit-exact against the stock library) (#210); calm first stream window (extra beyond vendor, default on) (#216).
- Software rotation through `SetChnRotate` (beyond vendor, MXU2) (#212); encoder ROI and super-frame as the vendor (#16, #19).
- ISP bypass graph device-proven: RAW 2048x1536 12-bit in 16 bits, then NV12 again (`OPENIMP_T30_BYPASS_GRAPH=1`, default off) (#218).
- `IMP_FrameSource_GetFrame` compact NV12 like the vendor (#202); `GetAFMetrices` like the vendor; WaitFrame, RawDRC, antifog, mirror, picture controls and the CSC presets (extra, opt-in) reach the driver.
- API coverage on 2026-11-07 (matrix "All areas"): 286 of 350 vendor functions work, 44 implemented but not device-tested, 19 gaps, 1 unknown.
- Not covered yet: picture quality (SSIM) against the vendor, FixQP and SMART on the device, run-time frame-rate / GOP changes (host-tested only); DMIC rows count as `nohw` (no DMIC on the lab board).

The sections below are dated; they record the state before the lab camera where they say so.

## Verified live gate (external contributor, before 2026-11-03)

The test device of this section is a Wyze VDB1 with a T30X and SC4236 sensor. The
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

- FrameSource sets and gets the frame-channel format with the 0x5c-byte
  object of the vendor libimp, ioctls `0xc05c56c3`/`0x405c56c4` (type, the
  48-byte pix header of a 4.x kernel, crop, scaler, rate words). The 0x4c-byte
  form of the first open driver (`0xc04c56c3`/`0x404c56c4`) differs only by the
  four extra pix words and is still accepted by the open driver (#178).
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
Gap work of 2026-10-10 (no T30 camera in the lab at that time, vendor libimp 1.0.5 disassembled and
compared with T21 1.0.33 and T31 1.1.6): 317 of 334 names are exported (was
271), audio 68 of 68. None of the 63 names that were missing is imported by
any of the applications using the IMP API that were checked.

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
- Still missing on 2026-10-10 (17 names): `Decoder_*` (8) and `Encoder_Get/SetChnCrop`,
  `ISP_Tuning_SetISPLDC` (need a T30 device or driver work);
  `Encoder_Enable/DisableAllNCUDenoise`, `PadFrame`, `SetPadFrameAttr`,
  `Get/SetRencFirstFrameMult` (not worth it: closed-encoder or chip-specific).

On 2026-10-10 nothing in this list had run on a T30 camera. Since 2026-11-03 the
T30X lab camera runs these calls (see the device section at the top); the current
per-function state, including the 19 remaining T30 gaps, is the "API coverage per
function" section of [FEATURE_MATRIX.md](FEATURE_MATRIX.md).

## API run in QEMU and static audit (2026-11-01)

At that time still no T30 camera in the lab. Method: (1) the T30 1.0.5 vendor `libimp.so` was
disassembled and compared with OpenIMP's T30 build call site by call site
(tuning transport and control id, return paths); (2) `tools/apitest` now builds
for T30 and was run in the QEMU T30 machine (sc4236 sensor model, the open
`tx-isp-t30` driver and `audio.ko`): all areas plus 56 cases, one case per
run, the machine restarted after a driver crash. QEMU shows return codes,
round trips and driver reactions, not pictures, sound or timing. Cells move
only to `qemu`, `err`, `aud` or `host`; nothing is `dev`.

Result for the 14 audit issues (237 list entries; before: 222 `?`, 5 `qemu`,
5 `miss`, 5 `cache`): 160 `qemu`, 32 `err`, 24 `aud`, 6 `cache`, 5 `miss`,
2 `host`, 2 `stub`, 4 `n.a.` and 2 `?` (`GetAFMetrices`, `SetChnRcTrigLevel`).
The `miss` cells are the vendor-exported names OpenIMP's T30 build still lacks
(`Decoder_*`, `Get/SetChnCrop`, `PadFrame`, `SetPadFrameAttr`, `SetISPLDC`).

Fixed in OpenIMP (host tests in `tests/t30`): `IMP_ISP_Tuning_AE_Get/SetROI`
(control 0x8000024 by value, box check), `SetISPProcess`, `SetFWFreeze`,
`SetShading` (S_CTRL by value with the vendor cache), `IMP_FrameSource_GetFrame`
(waits up to 2 s), `Get/SetH265TransCfg`, `SetChnResizeMode`. New host tests:
`fs_lifecycle_test_t30`, `ivs_framework_test_t30`.

(Closed 2026-11-03 for the ISP tuning controls, see "Tuning controls served" below.) Open driver gaps seen in QEMU (open-tx-isp, `tx-isp-t30`; OpenIMP sends the
vendor transport): getters of `AntiFlickerAttr`, `WB`, `Expr`, `EVAttr`,
`IntegrationTime`, `AeComp`, `AE_ROI`, the histogram/weight/statistics blocks,
`Awb_RgbCoefft`, `Gamma`; the controls behind `SetSensorFPS`, `HiLightDepress`,
night running mode, Hflip/Vflip (V4L2 0x980914/15 are inline ENOSYS cases),
colour effect and scene mode, `SetFWFreeze` (0x8000165), the strengths. Stored
values of Brightness and Saturation read back as 0. `IMP_ISP_GetSensorRegister`
oopses the driver (NULL dereference). A few starts of the second FrameSource
channel (about one run in seven) ended in a fatal oops in the ISP interrupt path
(LIST_POISON2 dereference), and the `hvflip` case oopsed in the ISP state
initialisation both times; both are driver bugs found by this run, not
analysed here.

Differences between OpenIMP and the 1.0.5 vendor library that remained on 2026-11-01 (since then
`SetISPBypass` is a real bypass graph, device-proven with `OPENIMP_T30_BYPASS_GRAPH=1` (#218), `WaitFrame`
and `Get/SetRawDRC` reach the driver as the vendor (device-tested), and `GetAFMetrices` behaves like the
vendor library without a driver call):
`SetDPStrength` (the vendor scales driver table 83 with `isp_table_tuning_ratio`,
OpenIMP sends the DPC ratio control; sinter and temper strength use the tables since
2026-11-03), `SetISPBypass` (stub), `WaitFrame` (stub: wiring the vendor
block returns -1 against the current driver), `Get/SetRawDRC` (kept in
userspace, the vendor sends 0x8000168 and 0x80000a0), the temper-denoise detour
around Hflip/Vflip, `GetAFMetrices` (the vendor returns a value of its own AF
thread). The vendor sends the
FrameSource format with 0xc05c56c3/0x405c56c4 (0x5c bytes); OpenIMP uses the
0x4c-byte form of the open driver and the stock device trace.

## Tuning controls served (2026-11-03, #177)

At that time QEMU T30 only (the lab camera arrived the same day). The open driver (`tx-isp-t30`) now serves the controls the
lifted switch had lost (`driver/t30/tx_isp_t30_tuning_ctl.h`, one arm per control id, the
behaviour of the stock tuning code): apitest `isp` area 25 PASS / 0 FAIL (before 8 / 17),
the cases `fwfreeze`, `hilight`, `expr`, `gamma`, `aeroi`, `aecomp`, `aestrategy`,
`contrast`, `ispflip`, `rgbcoef`, `scene` and `colorfx` PASS, `tests/t30_qemu/tuning_strength.c`
(strengths through the driver tables, scene/colour effect, brightness/saturation read-back)
`done bad=0`, six module reload cycles with the area, the strength test and a sensor register
read: 0 oopses, no slab reports (`slub_debug=FZPU`).

OpenIMP side: `Set/GetSinterStrength` and `Set/GetTemperStrength` scale the driver tables
109 and 132 through control 0x8000161 like the vendor 1.0.5, and `Get/SetSceneMode` /
`Get/SetColorfxMode` send the V4L2 controls 0x9a091a / 0x98091f like the vendor (before:
refused). The stock T30 scene handler is a no-op; the open driver stores the value so that the
getter reads it back (as the open T31 driver), there is no picture effect.

Not shown by QEMU: any picture effect (colour effect, exposure time, the strengths): the
sensor model's frames do not respond. Still open: `Encoder_Get/SetChnROI` and
`Encoder_Get/SetSuperFrameCfg` (the encoder limits above), `Awb_SetCwfShift` (the calibration
update for the light source table faults in the lifted firmware code), and
`Encoder_Get/SetChangeRef` is a vendor no-op (GetChangeRef -1 on a created channel).

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

The lab has one T30X board (SC4236) since 2026-11-03. Other T30 boards and sensors are
not tested; if you own a T30/T30X camera running Thingino
with the vendor stack, you can test OpenIMP without flashing anything: the test
kit in `tools/testkit/t30/` (see its `README.md`) runs the vendor API test
against the vendor and the OpenIMP `libimp.so`, a short H.264 encode, and packs
the logs, `dmesg` and `/proc/jz/isp/isp-m0` into one tar.gz for you to review
and send. It restores the streamer and changes no configuration.
