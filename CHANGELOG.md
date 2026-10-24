# Changelog (OpenIMP)

**Where to read what**

- **This file**: release-oriented summary per area (what changed against the vendor stack and against the original upstream line, [opensensor/openimp](https://github.com/opensensor/openimp)), followed by the condensed OpenIMP-only log by date.
- **Full campaign log**: [docs/OPEN_STACK_CHANGELOG.md](docs/OPEN_STACK_CHANGELOG.md): every change of OpenIMP, open-tx-isp and the thingino integration since 2026-09-30, per session, with device-test evidence and numbers. The same text is on the wiki page *Changelog*.
- **Current state per feature and SoC**: [docs/FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md) (HTML view: [docs/feature-matrix.html](docs/feature-matrix.html)); what goes beyond the vendor: [docs/OPENIMP_BEYOND_VENDOR.md](docs/OPENIMP_BEYOND_VENDOR.md).

Release plan: `next` goes to `aperto` (fast-forward only) after the long soak of the release candidate and is tagged `vYYYY.MM.DD`; until then dates are the reference. The first release covers T10, T20, T21, T23 and T31; T10 has not been re-tested on the open stack today ("not re-tested"); T41 is not part of the first release. Branch names (`claude/...`) are historic: the topic branches were merged into `next` and deleted. Cameras are anonymised: cam-A (T31), cam-B (T23), cam-C (T20), cam-D (T21), cam-E (T10), cam-F (T41); cam-G and cam-H are further T23 cameras, cam-I a second T20, cam-J a second T21.

## Branch `claude/telemetry-abi` (2026-10-24, merged into `next`)

- **`OpenIMPEncoderTelemetry` is versioned:** two leading fields `size` (caller sets `sizeof(t)`, library writes back the bytes filled) and `version` (`OPENIMP_ENCODER_TELEMETRY_VERSION`, 1). New fields are only appended, so a streamer built against an older header keeps working; a call with `size` < 8 returns -1. Breaking change against the first (unversioned) layout of earlier today, which no release shipped.

## Branch `claude/t4x-bufshare` (2026-10-24, merged into `next`)

- **T40/T41 default stream count 2 (vendor); T41 `IMP_Encoder_SetAvpuBsShare`:** reference-frame sharing does not exist on T40/T41 (vendor libimp 1.2.6 / 1.3.1 have none); the vendor features are `SetbufshareChn` (already matched) and, T41 only, `SetAvpuBsShare`. OpenIMP used 4 stream buffers per channel on T40/T41, the vendor 2: stream rmem for 1080p+360p 5.36 -> 2.68 MB (rmem in use 28.84 -> 26.16 MB, vendor 25.56). `SetAvpuBsShare` follows the vendor (one shared rmem bitstream buffer, default 0x23b700, heap copies with phyAddr 0, CreateChn 0x80048008 if a buffer does not fit, 0x80040100 if the allocation fails); the default stream count under sharing is 2, the vendor's 1 (beyond vendor, user decision). Device (Cinnado T41NQ .34): decode clean, synthetic pattern share vs default SSIM 1.0. T40 by code only.

## Branch `claude/t41-rcparams` (2026-10-24, merged into `next` as 518df91) and 2026-10-24 test results

- **T41 RC parameter APIs like the vendor libimp 1.2.6** (`src/t40/openimp_p2_encoder.c`, T41 only): `IMP_Encoder_SetDefaultParam` identical to the vendor for all profiles x rate modes x initial QP (40 cases compared on .34); return codes (channel >= 8 `0x80040001`, not created `0x80040008`, frame rate with a zero part `0x80040010`); QP window clamp (max 0..51, min <= max); per-frame QP read-back = I window; `uMaxPictureSize` read back x1000; IDR distance = `uGopLength * uMaxSameSenceCnt` (`GetChnGopAttr`/`GetChnAttr` report the product). `SetChnBitRate`/`SetChnQpBounds`/... accept a value and ignore range errors like the vendor. **`IMP_Encoder_SetChnAttrRcMode` at run time = `SetChnBitRate` + `SetChnQpBounds` only** (initial QP, IP/PB delta, `eRcOptions`, `uMaxPictureSize`, mode change are ignored and not read back, as in the vendor). Method: the vendor library ran on the open driver (same camera), plus disassembly. Measured equal: IDR spacing, CBR 1500 kbit/s (vendor 1580, open 1570), QP bounds 30,30. Open (RE running): `iIPDelta` at `CreateChn` has no effect (vendor: I QP 20.5 against 23.8), `iPBDelta`/`eRcOptions`/`uMaxPictureSize` stored only, per-frame windows use the union of the I and P windows, run-time changes apply immediately instead of at the next picture. T40 shares the code path but the changes are `#if PLATFORM_T41` only (T40 needs its own check against libimp 1.3.1).
- **`tests/t41/dpcscan.c`** (new tool): counts pixels brighter than all 8 neighbours in full-size Y frames to measure the DPC unit. Result (T41, true-dark test): DPC on leaves no hot pixel, the module bypassed about 330 peaks per frame; the default equals ratio 128.
- **Documentation corrections:** `docs/FEATURE_MATRIX.md` third pass 2026-10-24 (T41 defog, NR, DPC, module size, CPU load, buffer sharing, RC parameters re-judged, see its first Summary entry). T41 defog is on by default in open-tx-isp. The CPU-load comparison uses free CPU (one idle-priority spinner per core) because `/proc/stat` does not show the vendor kernel-thread time on these `NO_HZ_FULL` kernels. Pending (not merged): branch `claude/t4x-bufshare` (`IMP_Encoder_SetAvpuBsShare` as the vendor, default stream count 4 -> 2 on T40/T41, saves 2.7 MB rmem for 1080p + 360p).

## Branch `claude/t41-enc-telemetry` (2026-10-24, merged into `next`)

- **T41 `IMP_Encoder_Query` shows the real encoder state; new `OpenIMP_Encoder_GetTelemetry` (beyond vendor):** `leftPics` and `work_done` followed "a PollingStream call is in progress", which is true almost all the time (a blocking poller), so a sampler saw leftPics 1 / work_done 0 in every sample (inert). Now `leftPics` is 1 and `work_done` 0 only while the codec holds a picture (source frame handed to the codec until its stream is back); `leftStreamFrames`/`leftStreamBytes`/`curPacks` describe a finished stream waiting for GetStream (real byte length). The new function returns cumulative per-channel counters (frames, bytes, IDR count, last frame size, encode time last/avg/max, delivered fps, average kbit/s). Other SoCs unchanged. Device (Cinnado T41NQ .34, sc5336 2880x1620, CBR 2000 kbit/s 15 fps, 5 s): 496 Query samples at 10 ms show work_done 0 in 192 (38.7 %), encode time avg 25.85 ms of a 66.7 ms interval = 38.8 %; counters equal the consumer's own count (75 frames, 1401684 bytes), fps 15.02, 2073 kbit/s; a polled-not-fetched stream shows frames 1 / 6873 bytes / packs 1, 0 again after GetStream. `GetChnEvalInfo`/`GetChnAveBitrate` are not part of the T41 1.2.0 SDK.

## Branch `claude/sensor-testpattern` (2026-10-19, not merged yet)

- **Test pattern mode 4 = sensor test pattern (beyond vendor):** `OpenIMP_TestPattern_Set(4, p)` switches the sensor's own built-in pattern on (per-sensor table: SmartSens sc2336/sc2336p/sc4336p/sc5336/sc830ai/sc1a4t 0x4501 bit3 ramp; SOI jxf23/jxf22/jxh42 0x0c bit0 colour columns), so a known picture runs through the whole ISP (BLC, WB, CCM, gamma, NR) and the encoder; frames are not replaced. AE is frozen at the values of the moment mode 4 starts (`ae_hold`, default 1: T23/T31 SetAeFreeze, T20-T31 manual integration time + gain ceilings at the current gains, T40/T41 AeExprInfo.AeMode manual), AWB optionally (`awb_hold=1`: T20-T31 manual WB with the current gains, T40/T41 awb_frz); leaving mode 4 clears the bit and restores AE/AWB exactly. Register access via the stock sensor register ioctl (IMP_ISP_Set/GetSensorRegister, T40/T41 0xc040540d), chip-id guarded and read back (a no-op driver path gives EIO, nothing written). `OpenIMP_TestPattern_SensorInfo()`, `tpctl set 4 [ae_hold=0|1] [awb_hold=0|1]`. Device: T20 .107 jxf23 pattern stable 70 s with AE hold (without it the driver's gain write cleared 0x0c after 1.5 s), AWB hold visibly changes the pattern colour; T23 .23 sc2336p ramp 40 s with the open-tx-isp T23 register-ioctl fix (0xc4 -> 0xcc, read back), 3A restored after. Host test `tests/testpattern`.

## Branch `claude/testpattern-api` (2026-10-19, not merged yet)

- **Test pattern runtime API (beyond vendor, all SoCs):** `OpenIMP_TestPattern_Set(mode, params)` / `OpenIMP_TestPattern_Get(state)` (public header `include/openimp_testpattern.h`, versioned structs with a size field, capability `OpenIMP_Cap_TestPattern`) switch the synthetic picture (0 off, 1 pattern, 2 pattern + moving square + frame counter, 3 motion test with built-in object or script) at any time while streaming; `OPENIMP_TEST_PATTERN` is only the start value. The frame path reads the mode as one relaxed atomic load (no lock; off = one comparison). Optional control socket `OPENIMP_TEST_PATTERN_CTL` + `tools/testpattern/tpctl get|set` drives a running process from outside. The OpenIMP banner (dot grid, lettering, tagline; rasterised from `docs/assets/banner.svg` by `tools/testpattern/mklogo.py`) sits centred on a white quiet zone (15 % of the picture height), palette + RLE, 3.8 KB in .rodata. Device (T23 .23, 1080p H.264, switching 0, 1, 2, 3, 0, 1 under one stream): every transition clean, no mixed frames, frame counter contiguous, decode error free. Host test `tests/testpattern`.

## Branch `claude/t41-rcparams` (2026-10-24, merged into next 7396214)

- **T41 CBR rate-control parameters like the vendor libimp 1.2.6 (matrix row 27, device-tested on cam-F .34, vendor libimp on the same open driver for A/B):**
  - **iIPDelta works:** the I picture is coded at the running P QP minus the effective delta (iIPDelta -1 = 4, intra-only GOP = 0), clamped into the I window (vendor setParam/getPictureQP). Before, every I picture had the P QP. 600 kbit/s 720p15, delta -1/0/2/6: first I QP 37 and first P 41/37/39/43, later I = P-4/P/P-2/P-6, identical to the vendor.
  - **Initial QP:** iInitialQP -1 takes the vendor bits-per-pixel table (bit rate / fps * 1000 / pixels, same table as T40) as the first I QP; the P controller starts at that plus the delta (was a fixed 38). 1500 kbit/s: first I 28, P 32 as the vendor.
  - **uMaxPictureSize drives the hardware rate control:** the per-picture HW target is 95 % of it (vendor GetTargetSize). CreateChn sets the I value (uMaxPictureSize * 1000 bits), SetChnMaxPictureSize I and P; a change restarts the CBR models like the vendor setParam. maxpic 300 kbit: I 36 kB (vendor 35 kB, before 92 kB); run time 40 kbit P: 4.68 kB (vendor 4.65 kB); 200 kbit I: 25 kB (vendor 24 kB).
  - **QP windows per picture type** (SetChnQpBoundsPerFrame, was the union of I and P) and **changes apply at the next picture:** header, command and HW RC use the QP and window latched when the picture is prepared, so a setter from another thread never splits a picture.
  - Default 1500 kbit/s: 1554 kbit/s, I 81 kB, P QP 21.7 (vendor 1548, 81 kB, 22.1); all streams decode clean, consecutive-frame SSIM equal to the vendor (I->P 0.999), at 600 kbit/s I-frame pumping gone (I->P 0.964 -> 0.993).
  - **eRcOptions and the I buffer guard (2f3ab3b):** an I model like the vendor's (bits and QP of every third IDR, or of a P picture with >= 81 % intra blocks); with eRcOptions SCN_CHG_RES clear or STATIC_SCENE set the I QP is the larger of the mean P QP since the last IDR and the QP the I model needs for 3/4 of min(CPB, GOP budget), minus the delta; in every mode the I QP is raised while the I model predicts more than 3/4 of the bit-rate history room. Static scene 1000 kbit/s: eRcOptions 0 gives I QP 20-21 at P 25 (vendor 21), default 1 unchanged (21). Low rate 150 kbit/s: I/P QPs as the vendor (I 27-29 at P 31-33). Light-step scene test with eRcOptions 0: streams decode clean, consecutive-frame SSIM as the vendor (P->I 0.990 vs 0.992).
  - VBR, CappedVBR and CappedQuality now also take the vendor initial-QP table and uMaxPictureSize (same AL code in the vendor). VBR 1000/1500 kbit/s: first I QP 32, first P 37 as the vendor (was 18); uMaxPictureSize 200 kbit: I 25 kB (vendor 23.7 kB). Capped modes not device-compared.
  - **Open:** vendor P-picture uses of eRcOptions (o1II +408), DELAYED and SC_PREVENTION bits not implemented; the I-model scale stays at the vendor start value (the vendor learns it); iPBDelta has no effect (no B pictures, as the vendor); RC read-back updates at once (vendor after the next picture). Details: session notes T41_RCPARAMS2_1024.

## Branch `claude/t40-ch1-gaps` (2026-10-19, not merged)

- **T40 encoder: pack timestamp is the frame's capture time, no manufactured frame-rate timeline (fixes the "1.1 s frame gaps" on ch0 and ch1 at night):** the T40 encoder stamped every pack as first-frame time + n x 1/fps. When the sensor ran below the configured rate (night, long exposure: 11.6 fps instead of 20), these stamps fell about 1 s behind the wall clock, the streamer's PTS check then re-anchored them, and the stream showed a 1.08 s PTS jump every 2.5 s (304 jumps in 958 s) although no frame was lost (driver ch1 pops = packets received). Now as the vendor and as T41 already did: the pack carries the FrameSource frame timestamp. Device (Eufy T40, open driver, timps, night, 10 min ch0+ch1): before 119+119 jumps of 1.08 s in 300 s, after 0 jumps > 0.2 s (largest step 0.105 s at a real 11.55 fps), 6919 of 6950 driver frames per channel received, 0 oops.

## Branch `claude/i2d-t4x` (2026-10-18, not merged)

- **T40/T41 FrameSource I2D: real hardware rotate 90/180/270, flip and mirror (branch `claude/i2d-t4x`, T41 device-tested on the Cinnado .34, T40 host only):** before, `IMP_FrameSource_SetI2dAttr` returned -1 and the attribute was ignored. Now as the vendor libimp (T41 1.2.6 0x72210 / EnableChn 0x7b6ac, T40 1.3.1 `i2d_resourcedeal`): the attribute (`IMPFSChnAttr.i2dattr` at CreateChn/SetChnAttr or `SetI2dAttr`, which keeps the vendor checks) is applied at `EnableChn`; every captured frame goes through the I2D block (`/dev/i2d`, built-in kernel driver `ingenic_i2d`, `IOCTL_I2D_START`) into one output buffer per capture buffer, and the consumers (encoder, IVS, GetFrame, SnapFrame) get the output. `picWidth/picHeight` stay the capture size; 90/270 delivers picHeight x picWidth, the encoder channel is created with that size. The block needs a 32-aligned output pitch (360/720 wide outputs were striped with ALIGN16): the output pitch is reported by the internal `OpenIMP_FS_FramePitch()` and used by the encoder (T41 command word 52/53, T40 descriptor), the IVS and SnapFrame. Frames must be 8 aligned (vendor rule), 90/270 width 16 aligned. Marker `OpenIMP_Cap_I2dRotate`. Device (sc5336, 1920x1080 / 1280x720 / 640x360): 90, 180, 270, flip, mirror, flip+mirror correctly oriented, streams decode, SSIM 0.97 against the transposed unrotated picture, 25 fps and 1-7 % system CPU with and without I2D. Host test `tests/t41/i2d_test.c` (also run by tests/t40).

## Branch `claude/streamer-real-funcs` (2026-10-16, on agg-39, not released)

From the streamer function audit; the first three items are device-tested on T10 .31, T20 .163, T21 .24, T31 .21.

- **`IMP_Encoder_Query` on T10/T20/T21/T23/T31** reports the real channel state (picture in the encoder, finished stream waiting with its byte length, `work_done`) instead of a constant block.
- **`IMP_Encoder_SetJpegeQl` on T10/T20/T21** applies the quantisation tables to the JPEG encoder (apitest "JPEG size follows SetJpegeQl live": table 2 gives 86-118 kB, table 90 gives 4-8 kB per picture).
- **`IMP_System_GetCPUInfo` on T21/T23/T31** reads the variant from the SoC registers (soc id, CPPSR, sub-type word) with the vendor decoding, falling back to the old constant when /dev/mem is not readable.
- **T10/T20/T21 `SetAe_IT_MAX`, `SetBacklightComp`, T10/T20 `SetBcshHue`, `GetAeLuma`** now act (beyond vendor, needs open-tx-isp `claude/streamer-real-funcs`; see docs/OPENIMP_BEYOND_VENDOR.md). Device test pending (module change).

## After RC1: aggregate agg-38 (state of 2026-10-12, not released)

`claude/agg-38` = agg-37 (dc20d0e) + `claude/awbstats` (with `claude/awbzone`), `claude/t31-rc-collapse`, `claude/oimp-thread-names`, `claude/t21-cpu-fix`, `claude/t40-night` (with `claude/t40-devtest`), `claude/t41-isp-handlers`, `claude/funcsearch-run4` (documentation) and the device results of `claude/funcsearch-run5`. `claude/gaps-t1x-impl` is recorded as merged without changes: all of its commits were already in agg-35/agg-36 under other SHAs. The open-tx-isp side is open-tx-isp `claude/agg-38`. Build and host tests only (`make check`, `make release-check`, strict export check of T10 to T41); no soak. Device results only where an item says so.

- **In agg-38, described below:** GetAwbStatistics (T40/T41) and GetAwbZone (T23/T31) with the freshness contract, the T31 CBR debt bound, thread names, the T40 night fixes and new T40 calls, and the T41 1.2.6 ISP tuning calls (their sections and items further down).
- **Helix per-frame encode path at -O2 (`claude/t21-cpu-fix`):** on T10/T20/T21/T23/T30 the Helix frame submit, bitstream read-out, slice/CABAC header writer, frame-level rate control, hardware JPEG and the framesource dequeue/dispatch are built at `-O2` again (they run per frame in the application's threads). T21 `.text` +18 KB against agg-37, still 77 KB smaller than agg-36; exported symbols unchanged. CPU A/B on the device pending.
- **T40 `IMP_ISP_StartNightMode` and `IMP_ISP_Bypass_Bind` device-tested** (Eufy T40, vendor kernel, A/B with the vendor libimp; tested on `claude/funcsearch-run5`, which sends the same requests as the `claude/t40-night` code in agg-38): both callable between `IMP_ISP_Open` and `AddSensor` with rc 0 like the vendor; night mode gives black-and-white pictures until `SetISPRunningMode(DAY)`. `IMP_ISP_WDR_OPEN` (linear start) returns 0 like the vendor, but the linear-only sensor then delivers no frames with either library; it stays host-tested.

## T40 night branch `claude/t40-night` (2026-10-12, host only, in agg-38)

On `claude/t40-devtest` (cc50954). Host tests, `make check`, `export-check` T40 and T31/T40/T41 cross builds; no camera was used. Device test script for the next run: `tests/t40/devtest` (phases 4, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000; [docs/T40_STATUS.md](docs/T40_STATUS.md)).

- **T40 fixes of the device run of 2026-10-11 (vendor 1.3.1 disassembly first):** `IMP_ISP_Tuning_GetHVFlip` answers the cached mode (the stock kernel has no getter for 0x08000073; it returned -1), `SetHVFLIP` with the vendor checks; `GetBcshHue` answers the cached byte (no ioctl); `GetISPRunningMode` sends the get request without a payload and reads the mode from the payload word (the old pointer request left the caller's variable untouched); `IMP_Encoder_Query` fills the vendor fields (`leftStreamBytes`/`curPacks` 0, `leftStreamFrames` 1 while a finished stream waits for `GetStream`, `work_done`). The picture checks of HVFLIP, hue and running mode in the device test measured the two frames captured before the call (two buffers, no drain); the test now drops them.
- **New on T40 (host-tested):** `IMP_Encoder_Set/GetChnSeiAttr` (user_data_unregistered SEI in front of IDR or every picture, H.264 and H.265; the access-unit gate accepts it only while enabled), `IMP_ISP_Get/SetDrawAttr` (control 0x080000a0, RGB->YUV as the vendor), `IMP_ISP_StartNightMode` (ISP-device ioctl 0xc004542e before `AddSensor`), `IMP_OSD_SetGroupCallback` (called after the OSD group is drawn into a frame), `IMP_OSD_Get/SetRgnAttr_ISP` (line/rectangle via `SetDrawAttr`, cover via `SetMask`), `IMP_AI/AO_Get/SetDigitalGain` (raw codec register 0..255), `IMP_ISP_WDR_OPEN` (linear boot ioctl 0xc0045432, WDR boot = WDR enable), `IMP_ISP_Bypass_Bind` (ioctl 0xc0045431), `IMP_ISP_GetRaw` (one raw frame into an rmem buffer per sensor; unlike the vendor the buffer grows when a call needs more and the CPU cache is invalidated after the capture). `IMP_Encoder_SetFrameRelease` stores the ratio (capture frames already go back right after the encoder used them; same as T31).
- **Fixed while doing it:** the SEI codec entry points sat in a T23/T30-only block, which left `AL_Codec_Encode_SetSei` undefined in the T40 `libimp.so` (found by linking the device test; `nm -D` now shows no undefined internal symbol on T31/T40/T41).
## Branch `claude/t41-isp-handlers` (2026-10-11, in agg-38)

On agg-37 (dc20d0e); driver side: open-tx-isp `claude/t41-isp-handlers`. Host tests and builds only, not run on a camera.

- **T41 ISP tuning calls of the vendor libimp 1.2.6 now exported** (vendor control numbers and pointer pass-through from the 1.2.6 disassembly; need the open-tx-isp driver handlers of the same branch, host tests only): `Get/SetAeConvergeStep`, `Get/SetAwbConvergeStep`, `Get/SetAwbCtTrendOffset`, `GetAeEvList` (also as `GetAEEvList`, the name of the English 1.2.5/1.2.6 header), `GetAeAtList`, `Get/SetTmoCurve`, `Get/SetStatisConfig`, `WaitFrameDone`, and `GetHVFlip` (the T41 1.0.1 name of `GetHVFLIP`). `StatisConfig` and `WaitFrameDone` share the T40 code. The 1.2.0 headers lack the new types; local types with the 1.2.6 layouts are used (size-checked). Not exported: `GetAeBv`, `GetAEFlickerFlag` (they report vendor-AE state that the open driver does not keep). `AeConvergeStep` is stored only: the default open AE keeps its own step (matrix cell cache).

## After RC1: aggregate agg-37 (state of 2026-10-11, not released)

`claude/agg-37` = agg-36 + `claude/agg-37-size`, `claude/agg36-lib-drv`, `claude/agg37-t40`, `claude/funcsearch-release-check` (with `funcsearch-export-check`, `-t40-roi`, `-t40-ispctl`), `claude/agg37-rc-live`, `claude/agg37-smallfix`, `claude/agg37-shutdown`. Build and host tests only (`make check`, `make release-check`); no soak, device tests pending unless an item says otherwise.

- **T40/T41 `IMP_ISP_Tuning_GetAwbStatistics` verified and fresh, `OpenIMP_Cap_AwbStatistics` (branch `claude/awbstats` on top of `claude/awbzone`; T41 needs the open-tx-isp `claude/awbstats` driver; device-tested on the Eufy T40 and the T41 .32):** vendor semantics from the T40 1.3.1 (libimp 0x7e2c0) and T41 1.2.6 (0x55e78) disassemblies: `int32_t IMP_ISP_Tuning_GetAwbStatistics(IMPVI_NUM, IMPISPAWBStatisInfo *)` sends `{num, 1, 0x8000011, block}` with the pointer tuning ioctl (0xc0105436 / 0xc0105435) and the stock driver's `tisp_g_awb_statis_attr` -> `tisp_awb_get_zone` copies three 900-byte planes (`IMPISPStatisZone awb_r/awb_g/awb_b`, 15x15 u32 each, 2700 bytes, packed) = zone means `sum / count` of the raw, pre-white-balance R, G, B AWB statistics (T41 1.2.x AWB state +60096/+60996/+61896; the open tx-isp-t41 computes the same planes in `t41_awb_long_frame`). OpenIMP already routed the control (T40: vendor kernel driver, T41: open driver); verified now: `_Static_assert` on the 2700-byte layout, the request, and the contract that 0 is returned only with a block that holds statistics: the T41 driver answers `-EAGAIN` unless the planes come from a frame of the running stream within the last second, and libimp turns an all-zero block (the vendor T40 driver copies its state regardless, zeros before the first frame) into -1 / `EAGAIN`. New runtime capability symbol `const int OpenIMP_Cap_AwbStatistics = 1`, exported by the T40 and T41 builds only (weak extern or `dlsym`; absent = older OpenIMP or vendor libimp). Host tests: `tests/t40` and `tests/t41` `p3_controls_test` (request, block copy, zero block -1, driver error -1, argument checks, symbol), the `awbstats` / `awbstats-cost` commands of the `ispprobe` device shim; `make check`, `make export-check` (T40/T41) clean. Device, Eufy T40 (vendor kernel driver, `t40_devtest` with the vendor libimp 1.3.1 and with OpenIMP on the same night scene, IR): identical statistics, vendor global R/G 1.0786..1.0816 (0.28 %) B/G 1.1361..1.1408 (0.42 %), OpenIMP 1.0782..1.0836 (0.39 %) 1.1348..1.1410 (0.55 %) over 40 s; 36-45 us per call once a second, 7-9 us back-to-back (200 calls). Lamp (cellar bulb, OpenIMP, 40 s on, means over 28 reads against the 31-read baseline): warm 2700 K 50 % R/G +4.57 % B/G -10.13 %, warm 30 % +1.60 % / -4.51 %, cold 6500 K 30 % -2.16 % / -3.80 %, cold 50 % (repeat run after a Matter outage) -4.15 % / -8.89 %; off returns to the baseline within 0.2 % in the next read, the change shows in the first read after the switch. T41 (.32, open stack, timpsd with the shim): static R/G 1.0699..1.0728 (0.27 %) B/G 0.9692..0.9721 (0.30 %) over 40 s, 37 us per call cold, 13 us back-to-back (1000 calls, max 358 us), 3722 statistics frames, 0 dropped, 0 bad kernel lines; the T41 is not in the cellar (no lamp test). The timps 1.10.2 night colour trigger can use the call on both (pre-WB ratios, 4096 = 1.0 after its Q12 conversion).
- **T23/T31 `IMP_ISP_Tuning_GetAwbZone` correct and fresh (branch `claude/awbzone`, needs the open-tx-isp `claude/awbzone` driver; device-tested on cam-A and cam-B):** the vendor 1.1.x ABI `int IMP_ISP_Tuning_GetAwbZone(IMPISPAWBZone *)` (675 bytes: `zone_r/zone_g/zone_b[225]`, 15x15 byte means; the struct is now in `include/imp/imp_isp.h`), one request `{1, 0x800000b, block}` like the stock 1.1.6 library. The driver fills the block the stock way (`Tiziano_awb_fpga`: truncated `sum / pixels` of the AWB statistics, 0 for an empty zone) and refuses with `-EAGAIN` unless it comes from a statistics frame of the running stream within the last second, so the call returns 0 only with real, current statistics and -1 otherwise (never a zero-filled or cached block). New runtime capability symbol `const int OpenIMP_Cap_AwbZone = 1`, exported only by the T23 and T31 builds (probe it with a weak extern or `dlsym`; absent = older OpenIMP or vendor libimp). T40/T41: the vendor libraries have no `GetAwbZone` (per-zone AWB statistics are `IMP_ISP_Tuning_GetAwbStatistics(IMPVI_NUM, IMPISPAWBStatisInfo *)`, which OpenIMP T40 already routes to the vendor driver), so nothing changes there and the symbol is not exported. Cost on cam-A (T31): 2 us per call (1000 back-to-back calls, max 60 us), 11-15 us for a cold call once a second. Static scene on cam-A after convergence: global R/G and B/G (sums over the zones) within 0.21 % / 0.14 % over 9 s and 0.31 % / 0.46 % over 20 s; cam-B (T23) 25-32 us per call, 46-51 us cold, static within 0.19 % / 0.22 % over 35 s, and a lamp switch moves the raw ratios by several percent in the expected direction (warm 2700 K at 50 %: R/G +0.33 %, B/G -2.80 %; cold 6500 K at 50 %: R/G -1.51 %, B/G -2.27 % against an IR-lit baseline; off returns to the baseline within 0.2 %), so the block follows the current frame. Host tests: `tests/t30/isp_t31_awbzone_test` (ABI, request, -1 on -EAGAIN with the block untouched, capability symbol), `tests/t23/isp_tuning_test` (block size, symbol), the `awbzone` / `awbzone-cost` commands of the `ispprobe` device shim (`tests/device`). Scaling evidence: tx-isp-t31.ko 1.1.6 HLIL 0x18218 and libimp 1.1.6 HLIL 0x9428c (`docs/re`).
- **T31 CBR under an impossible scene (fix):** when the pictures stay far above the target even at the maximum QP (sensor noise with full sharpening), the buffer debt of the Allegro controller grew without limit; after ~700 pictures its 32-bit level arithmetic wrapped (also in the vendor code, `IIii` level * 3), the QP fell from the maximum to the minimum and the P pictures reached 16x the target, and after the scene got easy again the QP stayed at the maximum for minutes. The debt is now bounded to 4 buffers (12 s at the target rate, at most 256 Mbit): the QP stays at the maximum and comes down within ~15 s afterwards. The vendor library avoids the debt only with `IMP_ENC_RC_ENABLE_SKIP` (picture skip on underflow, not in the IMP defaults; not implemented). OEM traces unchanged; host test `t31-al-rc-overload-test`; device test pending.
- **Thread names:** every thread OpenIMP creates is now named (`prctl(PR_SET_NAME)` from inside the thread, helper `src/core/thread_name.h`), so `top -H` / `/proc/<pid>/task/*/comm` attribute CPU time. Vendor names where an equivalent exists: `FS(%d)-tick`, `IVS(%d)-ivs_processing` (T31, was `ivs_chn%d`), `ai-_ai_record_t` (audio capture); new `oimp-tuning`, `oimp-isptune`, `oimp-avpu-irq`, `oimp-ivs-feed` (T40/T41), `oimp-fdpump-<chn>` (T40/T41), `oimp-dmic-rec`, `oimp-helix-wrk` (T23 Helix helper process). No behaviour change; host and cross-build checked, device test pending.
- **Smaller `libimp.so`:** `-Os` for the library, `-O2` kept for the per-pixel, per-macroblock, audio-DSP and floating-point rate-control files (`OPENIMP_SIZE_OPT=0` builds all at `-O2`); Helix SoCs (T10/T20/T21/T23/T30) drop the AVPU session code (they have no AVPU); AVPU register diagnostics compiled out unless `OPENIMP_DEBUG=1`. Exported symbols unchanged. Table and per-file list: [docs/SIZE.md](docs/SIZE.md).
- **T23 OEM Helix worker bridge off by default** (`OPENIMP_T23_HELIX_BRIDGE=0`): the native Helix encoder is the only H.264 backend, no automatic worker fallback, `IMP_Decoder_*` fails on T23 (it ran in the worker; matrix cell now err). `OPENIMP_T23_HELIX_BRIDGE=1` builds the bridge again.
- **T10/T20 ISP (needs the open-tx-isp agg-37 driver; host test only, device test pending):** `IMP_ISP_Tuning_SetAntiFogAttr` (V4L2 control 0x98e903; the driver applies it between frames) and `IMP_ISP_Tuning_SetMeshShadingScale` (driver control 0x80000a3, mesh-scale field of register 0x380) are exported like the vendor 3.12.0 libimp.
- **T40 H.265 / 4K:** HEVC FixQP uses the vendor lambda tables (4K FixQP 30: bit-rate deviation +27 % -> -1 %); the stream buffer clears only the extent the last picture wrote; the drain checks the last payload byte instead of a fixed 2 ms sleep (`OPENIMP_T40_DRAIN_VERIFY=0` restores the sleep). Details: [docs/T40_HEVC.md](docs/T40_HEVC.md).
- **T40 `IMP_Encoder_SetChnMapRoi`:** per-16x16-block QP deltas (vendor 1.3.1 argument checks, same as T41) on the T31-style AVPU QP table, behind `OPENIMP_T40_ROI=1` like `SetChnRoiAttr`; host and cross-build only, device test pending (T41: the code exists behind `OPENIMP_T41_ROI=1`, but the T41 AVPU ignores the table; matrix cells `SetChnRoiAttr`/`SetChnMapRoi` T41 = cache).
- **`make release-check`:** release gate (`tools/release-check.sh`, BUILD.md "Release check"): `make check`, a build of every SoC, then `export-check` in strict mode. Plain `make check` stays host-only and fast.
- **`make export-check`:** compares the `IMP_` exports of each built `build/<soc>/libimp.so` with the vendor export lists (`tools/export-check/vendor-<SoC>.txt`) and with the `IMP_` names used by streamer sources (`STREAMER_DIRS`); fails when a name that is both vendor-exported and streamer-used is missing (documented exceptions in `streamer-ignore-<SoC>.txt`). Used to correct the matrix: T41 cells of FrameSource delay/max delay/I2D/timed frame/FIFO attr and ISP module control/auto zoom/mask block/scaler level are host (code exists), `Encoder_Get/SetChnCrop` is missing on T10/T20/T21 (was n.a.); 11 T40 OSD rows (`AttachToGroup`, `SetMosaic`, `*_ISP`) were missing but exported: host.
- **T40 `Encoder_Set/GetFisheyeEnableStatus`:** the vendor flag (settable before CreateChn only, channels 0..12); was not exported (a streamer that links it needs the symbol). Host test.
- **T40 `IMP_Encoder_Set/GetChnRoiAttr`:** the T31 macroblock QP-table path with the T40 command bits read from the vendor 1.3.1 library (AVC only); off unless `OPENIMP_T40_ROI=1`, cross-built, not run on a camera.
- **T41 `IMP_ISP_Set/GetISPBypass`:** the vendor pair (8-byte block on the ISP node, ioctl 0x80085418, vendor error codes, remembered value). The open-tx-isp T41 driver does not serve that ioctl yet, so Set answers -4095 there. Host test only.
- **T40 ISP controls:** `IMP_ISP_Tuning_Get/SetISPCSCAttr` (vendor T40 wire format: 10-bit coefficients, not the T41 one), `Get/SetHLDCAttr`, `Get/SetAeSpeed`, `Get/SetAeExpList`, `Get/SetFaceAe`, `Get/SetFaceAwb`, `Get/SetStatisConfig`, `SetFixedContraster`, `GetAfStatistics`, `GetAFMetricesInfo`, `WaitFrameDone`, `GetISPBypass` (stored flag), `IMP_ISP_Get/SetOSDAttr` and `IMP_ISP_SetPreDqtime` from the vendor 1.3.1 disassembly; 22 functions, control numbers and conversions as the vendor library. Host test only; the vendor kernel driver of the T40 board owns the semantics (device test pending).
- **Run-time rate control:** T10/T20 `IMP_Encoder_SetChnRcTrigLevel(chn, level, newMaxQp)` as the T20 3.12.0 libimp (the OEM controller lifts the QP cap from the application's max QP to the new max QP, default 51, while a GOP uses more than `level` x its budget, so a starved stream ignores a lower max QP in the stock library too; `newMaxQp` = max QP makes it a hard cap; host-tested on the OEM controller models). T41: a run-time `SetChnBitRate` regenerates the EP3 hardware rate-control row thresholds (they were built from the bit rate at channel start only; OEM `AL_ApplyNewGOPAndRCParams` / `PreprocessHwRateCtrl`), and a run-time bit rate or QP range change keeps the running controller QP instead of restarting at the constructor value 38.
- **`SU_Base_Shutdown` in `libimp.so` (all SoCs):** the vendor libsysutils call does `sync()` and `kill(1, SIGCHLD)`, which busybox init ignores. OpenIMP syncs and then sends init SIGTERM (busybox reboots) by default, so a camera that calls it comes back; `OPENIMP_SU_SHUTDOWN=poweroff` sends SIGUSR2 (power off), `OPENIMP_SU_SHUTDOWN=vendor` restores the vendor behaviour. Host test with a fake `kill`, not run on a camera.
- **T41 IVS matrix entry** corrected (device state vs host-only fixes).
- **Host tests:** the out-of-line `imp_log_emit()` is linked into every host test target.

## After RC1: aggregate agg-36 (state of 2026-10-11, not released)

Work merged after RC1 (`claude/agg-36`, on RC1 + docs). Build and host tests only for the aggregate; no soak yet. Not part of RC1.

- **T40 encoder:** frame-level rate control (CBR/VBR follow the target), AVPU command lists as the vendor libimp 1.3.1, H.265 with correct PPS and hardware rate control (device-tested on the T40 board).
- **T40 ISP:** ISP-drawn OSD regions, `Get/SetMask`, `SwitchBin` (device-tested); BCSH getters from the vendor cache, frame pixel format 0 (host-tested).
- **T41:** `SwitchBin`, `AutoZoom`, `SetMaskBlock`/`GetMaskBlock`, `SetScalerLv` in OpenIMP and open-tx-isp (host tests and module builds; not run on a camera).
- **Encoder:** `SetbufshareChn` keeps and checks the value like the vendor (T31/T40/T41); vendor return codes for T10/T20/T21 (`GetChangeRef`, `Get/SetChnHSkip`, `Get/SetChnDenoise`) and T23 `SetChnROI`/`GetChnROI`.
- **T30:** 317 of 334 vendor exports (was 271), host tests only (no T30 camera).
- **Log level:** `OPENIMP_LOG_LEVEL` (0..7, syslog numbering) gates `IMP_LOG`; default 5 = notice, per-frame information lines silent; `OPENIMP_DEBUG_TRACE=1` forces 7. Env table in [docs/OPENIMP_BEYOND_VENDOR.md](docs/OPENIMP_BEYOND_VENDOR.md).
- **Logging rework, smaller `libimp.so`:** `IMP_LOG` is one compare plus an out-of-line `imp_log_emit()` (no `getenv`/`openlog`/double printf per call site; level in a global int set by a constructor). New build switch `OPENIMP_DEBUG` (default 0): LOG_DEBUG and the developer traces (kmsg probes, startup trace, per-frame stderr helpers, codec/HW/DMA traces) are compiled out of the release build; `OPENIMP_DEBUG=1` keeps them. See section 7.1 of [docs/OPENIMP_BEYOND_VENDOR.md](docs/OPENIMP_BEYOND_VENDOR.md). T41 can never get `OPENIMP_AL_FRAME_RC` (build error instead of a possible context layout mismatch).
- **IVS:** a frame fed right after a result is no longer dropped (channel marked free before the result is posted; `OPENIMP_IVS_VENDOR_ORDER=1` for the vendor order).
- Details and checks: [docs/OPEN_STACK_CHANGELOG.md](docs/OPEN_STACK_CHANGELOG.md), per-function state: [docs/FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md).

## Release candidate RC1 (v2026.10.11-rc1, state of 2026-10-11)

RC1 v2026.10.11-rc1: openimp fe9307c (agg-35 687a68e4 + docs), open-tx-isp 191be6c5 (agg-35 6e464ac7 + docs). RC1 is exactly the soaked agg-35 code; the tag adds documentation only.

**Soak evidence:** agg-35 ran on 10 cameras from 2026-10-10 18:04 to 2026-10-11 08:00 (about 14 h): 0 kernel oopses, 0 unexplained reboots, 0 application restarts, VA-API decode check 54/54. The soaked images ran a streaming application on thingino `aperto` base a4bcc1c86.

**Supported SoCs:** release: T10, T20, T21, T23, T31. Experimental (not part of the release promise): T30 (no device in the lab, host/build evidence only), T40 (first device run only, see the matrix) and T41 (channel 1 and night colour open).

**Highlights since the last `aperto` state (c855925; open-tx-isp ca9cbf0e):** about 190 commits in each repository.
- **Encoder ROI:** T31 gets an ROI the vendor library does not have (AVPU QP table, H.264); T10/T20/T21 apply `SetChnROI` and the chroma QP offset on the Helix/NVPU; every path produces valid H.264 (deltas clamped, picture QP inside 0..51).
- **Front crop (open-tx-isp):** the T31 scaler stall on a too small window is refused with -EINVAL, the T23 crop hang is fixed, T20/T10 keep a window until the channel opens.
- **T20/T10 day/night:** `isp-m0` reports the analog gain cap the AE really reaches.
- **Hardening:** reviewed and host-tested patches (ABI struct sizes, encoder group bounds, IVS channel limits, `PollingStream` guards, idempotent IVS group create/destroy), channel-restart and reload robustness.
- **Audio:** AI/AO/AEC calls device-tested on T10, T20, T21, T23 and T31; T41 has no audio hardware on the test board.
- **T41 driver controls** (frame drop, sensor register, WDR enable, AF weight) device-tested; **T40** first OpenIMP device run (H.264 on two channels, OSD, IVS move, JPEG, ISP get/set).
- **Docs:** the feature matrix has T30 and T40 columns, strict Get/Set rows and the wording "not in this SoC's encoder" for reference-frame sharing.

**Known issues (RC1):**
- **T41 channel 1 (sub-stream) colour at night** differs from channel 0 (picture soak finding); T41 stays experimental.
- **T31, black picture of about 80 s** seen once during an application-driven day/night probe; not understood yet.
- **T23, dusk flapping:** on one T23 camera it switched between day and night several times at dusk. Day/night switching is decided by the application; the driver and OpenIMP report sensor values.
- **Day/night in a poorly lit location** also follows the application's thresholds; the driver and OpenIMP report sensor values.
- T40: CBR delivers only about 20 % of the target rate, H.265 not supported yet. T41: no audio hardware on the test board (API level only).

**Next (after RC1, agg-36):** work that is not in agg-35 and therefore not part of this release candidate: T40 rate control and H.265, T40 ISP-drawn OSD, further vendor-API gap work (T30, T1x, T23/T31), T41 mask/zoom, log level default, IVS result ordering and review fixes.

Details: [docs/OPEN_STACK_CHANGELOG.md](docs/OPEN_STACK_CHANGELOG.md); per-function state: [docs/FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md).

## Release candidate agg-34 (state of 2026-10-09 evening)

OpenIMP `agg-34` (838f8147) = agg-32 (8980cae) + encoder ROI clamp (fec3e69, 8545fb9) + reviewed DeepSeek patches (11abe42 and 17 patches); open-tx-isp `agg-34` (da9baf1e) = agg-32 (3cf9bdac) + front-crop fixes for T31, T23, T20 and T10 (d940ba20 to 10bac52f, 164225e5) + T20 `isp-m0` gain cap (da9baf1e). The first release still covers T10, T20, T21, T23 and T31; T41 is experimental and not part of it. The combination agg-34 has not run on the cameras as a whole; the individual fixes below were device-tested on the SoC named.

- **agg-32 soak (2026-10-07 16:07 to 2026-10-09 11:31, 43.4 h):** 0 unplanned reboots, 0 oopses, 0 guard trips, 0 snapshot failures and no memory trend on the seven timps cameras without user interaction; the only reboots were a planned OTA and two crashes from a front crop on T31 (fixed below). The picture check found the T20 day/night regression, a T10 black frame in the dark and the T41 channel-1 problem (known issues).
- **Encoder ROI:**
  - **T31** gets an ROI that the vendor library does not have (`IMP_Encoder_Set/GetChnRoiAttr` through the AVPU QP table, H.264, `OPENIMP_T31_ROI=0` refuses it): device-tested, relative -10 gave 2.7x the bit rate, 0 decoder errors in 20 VA-API checks.
  - **T10/T20/T21** apply `SetChnROI` and the chroma QP offset on the Helix/NVPU (T21 by default, beyond vendor, `OPENIMP_T21_ROI=0` switches it off): device-tested.
  - **Valid H.264 on every path:** window deltas -25..+25, spread at most 25, picture QP plus delta inside 0..51 and the rate control's min/max QP; a relative -26 used to give `mb_qp_delta` +26 at the window edge, which Intel VA-API, VLC, Firefox and Edge show as broken blocks (software ffmpeg hides it). Requests beyond that are clamped with a one-time warning.
  - **T23:** regions go to the vendor encoder, no visible effect (no-op). **T41:** code only, off unless `OPENIMP_T41_ROI=1`.
- **Front crop (open-tx-isp, device-tested):**
  - **T31:** a window smaller than a channel's scaler output stalled every MSCA output until reboot; the kernel now refuses it (-EINVAL), drops a crop that a larger main stream cannot fit, and writes the scaler steps so a valid window zooms. The picture effect on T31 is experimental, not proven.
  - **T23:** crop off unlocks the window, a window that does not fit is dropped instead of hanging, the lock is released with the ISP session (the 2026-10-06 crop hang is fixed).
  - **T20/T10:** a window is kept while no downscaled channel is open and applied at stream on, and released with the tuning session.
  - **T31/T23:** module parameter `fcrop_upscale_pct` (default 0) lets the geometry check tolerate a measured upscale (T23: up to 2.0).
- **T20 day/night:** `isp-m0` reported an analog gain cap of 158 while the AE stops at 128, so the streamer never saw the cap reached; it now reports the smaller value (host-tested; the streamer needs its matching change, timps 5bc4eff).
- **DeepSeek patches:** vendor ABI struct sizes (T40/T41), encoder group bounds (6; 8 kept on T23/T40), T31 IVS channel 64, `OSD_SetPoolSize(0)` rejected, idempotent T31 IVS group create/destroy, `PollingStream` guards, `GetDefaultBinPath`. Host-tested and cross-built; device checks pending.
- Details: [docs/OPEN_STACK_CHANGELOG.md](docs/OPEN_STACK_CHANGELOG.md), section "2026-10-07 to 2026-10-09: agg-32 soak and release candidate agg-34"; per-function state: [docs/FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md); ROI rules: [docs/ROI.md](docs/ROI.md); beyond-vendor items: [docs/OPENIMP_BEYOND_VENDOR.md](docs/OPENIMP_BEYOND_VENDOR.md).

### Known issues (agg-34)

- **T41 sub-channel (channel 1):** green or flat frames at each day/night switch and the exposure of channel 1 does not follow channel 0; T41 is not part of the first release.
- **T20 day/night** needs the driver fix (da9baf1e) and the streamer change (timps 5bc4eff); with agg-32 alone both T20 cameras stayed in Day mode in the dark.
- **T31 front crop:** the crash is fixed, the picture effect is not proven.
- **T10:** occasional switch to Day in the dark with a black frame (3 events in 44 h).
- The older candidate's known issues below apply unless stated here; the T23 crop hang is fixed.

## Summary by area (state of 2026-10-06 evening)

All test cameras run the open stack (open-tx-isp kernel driver + OpenIMP + timps) from full OTA images built from thingino `aperto`; no Ingenic or neo helper libraries remain, and T23 runs without helixd and without vendor libimp.

### Release candidate (OpenIMP `agg-27`, open-tx-isp `agg-28`)

| SoC | In the first release | apitest of 2026-10-06 | Notes |
|---|---|---|---|
| T10 | yes, **not re-tested** | not run | userspace is the T20 build, cells mirror T20 |
| T20 | yes | 212 PASS / 0 FAIL on two cameras | `GetAeZone` is a vendor no-op (N/A); encoder ROI and chroma QP offset |
| T21 | yes | 228 PASS / 0 FAIL on two cameras | `SetBrightness` acts and sepia works (beyond vendor); antifog/scene are vendor no-ops |
| T23 | yes | 304 PASS / 4 FAIL (AF getters, fixed by `source_af=1`) | stock-like release defaults, MSCA scratch buffer; H.265 has no hardware; **crop hang open** |
| T31 | yes | 274 PASS / 1 FAIL | `SetFrameDrop` with stock semantics; H.264 stall after JPEG teardown fixed (agg-29) |
| T41 | **no** (experimental) | 255 PASS / 12 FAIL on the experimental branch | channel 1 scaling registers staged, output-restart hang, 38 tuning IDs missing |

- **Frame source and encoder (all SoCs):** `GetFrame`/`SnapFrame` wait up to 2 s like the vendor library and the T20 capture no longer stalls (apps that set a frame depth got no frames before); `Encoder_GetFd` returns a pollable pipe; a JPEG channel feeds itself from the frame source when its video channel is not polled (beyond vendor); two IVS groups as every vendor libimp; `InsertUserData` SEI; `PollingModuleStream` honours the channel bitmap.
- **T20/T10/T21:** encoder ROI and H.264 chroma QP offset on the Helix/NVPU (T20 device-tested; T21 stores ROI/QPG/SuperFrame like the vendor and applies them after the release).
- **T21:** `SetBrightness` acts through the AE target and SEPIA works (both beyond vendor); `SetDPStrength`/`SetAntiFogAttr` exported like the vendor.
- **T23:** pool rmem parked as on T21, driver defaults are the stock-like set that ran 7 h overnight, STREAMOFF drain wait and QBUF cache invalidate (no cold-start snapshot 503), MSCA scratch buffer for stopped channels (beyond stock), AF statistics on by default, 19 ISP getters routed, `GetSensorRegister` offset fixed.
- **T31:** stack overruns of `GetAwbZone`/`GetAWBCt` fixed (a bus error in apitest), `SetFrameDrop` with the stock lsize/fmark semantics, `EnableDefog` takes its flag, vendor Movestate logic (host-tested).
- Vendor no-ops are matched, not "fixed": T21 antifog/scene, T20/T10 `GetAeZone`, T23 `GetBlcAttr`. T23 H.265 stays refused (no hardware).
- Details: [docs/OPEN_STACK_CHANGELOG.md](docs/OPEN_STACK_CHANGELOG.md), section "2026-10-06 afternoon/evening: release candidate agg-27/agg-28"; per-function state: [docs/FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md); beyond-vendor items: [docs/OPENIMP_BEYOND_VENDOR.md](docs/OPENIMP_BEYOND_VENDOR.md).

### OpenIMP (userspace libimp)

- **Against the vendor library**
  - Functions that were stubs or silent no-ops in the vendor stack now act: T21 controls and noise reduction, T10 noise-reduction strength, `ae_it_max_us` on T21, T23 brightness/contrast/saturation/hue, getters that return what was set.
  - Smaller: libimp about 0.5-0.6 MB instead of 1.0-1.3 MB (`gc-sections`, tables generated instead of copied); more free video memory on T21 (2.76 MB instead of about 1.2 MB); reference-frame sharing on T21/T23 (about 1.5 MB less video memory, on by default).
  - More robust: checked inputs, rate-limited error logs, bounded waits, clean stop/reload (0 oops in 10 cycles per SoC); clear errors instead of silent `-1`/0 (for example H.265 on SoCs without HEVC fails at once so streamers fall back to H.264).
  - Beyond the vendor API (documented for streamer authors in OPENIMP_BEYOND_VENDOR.md): motion detection v2 with bounding boxes and strength (`OPENIMP_MOTION_V2=0` restores the vendor algorithm), rmem high-water logging and shortfall hints, effective rate control logged per channel.
- **Against the original opensensor/openimp line**
  - Encoders: real HEVC and hardware JPEG on T31; native Helix H.264 on T23 (no vendor helper); hardware JPEG on T20/T21/T23 without the vendor library; H.265 on T41 (AVPU).
  - Rate control: vendor Allegro core on T31, OEM controllers on T10/T20 (default), eprc on T21/T23 (0 oracle deviations), capped modes mapped properly instead of silent CBR.
  - Image and audio: OSD (text, bitmap, rectangle, line) on T20/T21/T30, real frame-diff motion detection on T20/T21/T30, software rotation on T31, real AECM echo cancellation on T31 (-18 dB echo), volume/mute and whole-fragment audio writes.
  - SoC coverage: T41 stream buffers, flip, BCSH and unload fixes; T30 builds against a real kernel (no device in the campaign).
  - Hardening from audits and an independent review: NULL crashes, buffer overflows (T31 AF/AE getters, module-chain dump, OSD size, ABI struct sizes), EINTR and `O_CLOEXEC` handling, T23 reconfigure race.

### open-tx-isp (kernel driver)

- **Against the vendor driver**: lifecycle hardening on all SoCs (locking, use-after-free, STREAMOFF races, last-close races, bounded tuning access, checked user copies); module reload clean on every SoC including T41; modules smaller than the vendor ones on T21 (452 vs 616 KB), T23 (622 vs 857 KB) and T31 (711 vs 829 KB); sensor module pinned while the ISP is open; sensor registry under `/proc/jz/sensor`; optional 8 MB MMAP pool on T10/T20; unknown control IDs are rejected instead of silently succeeding.
- **Against the original line**: exposure readback and vendor-format `isp-m0` on all T-series; T21 stock AE, ADR and control dispatchers lifted instruction by instruction (night flicker gone); T23 about 45 control IDs wired and the vendor AE lifted (default on); T20 simple AE/AWB with limits; T31 AF/AE statistics, SensorAttr and WaitFrame through the driver; T41 gc5603 tuning fix and the channel-restart hang fix.

### timps and thingino

- timps changes are made by the timps maintainers; OpenIMP only provides what the streamer calls (for example AE IT max reset to 0).
- thingino: packages `openimp` and `open-tx-isp` are in upstream branch `aperto` ([#1756](https://github.com/themactep/thingino-firmware/pull/1756)), pinned to the Lu-Fi forks; kernel VPU/rmem stability patches (#1748, #1752), optional boot guard (#1749) and SC2336 flip fixes (#1750, #1751) are merged there.

### Known issues (release candidate)

- **T23 crop hang:** the pipeline stops after a FrameSource crop change; under investigation, release blocker.
- **T31 H.264 stalls after the JPEG channel is torn down:** fixed: two threads polled the same encoder channel at once (`AL_Codec_Encode_Process` concurrently); `IMP_Encoder_PollingStream` now serialises per channel (openimp `claude/release-fix27` 5c2ccef), apitest on T31 FAIL 0 with the fix; goes into the next candidate (agg-29).
- **T41 is not part of the first release:** channel 1 scaling registers are staged, an output restart hangs the SoC, 38 tuning IDs are missing (branches `claude/release-t41` and open-tx-isp `claude/t41-ch1-fix`, not merged).
- T21: the OSD blend takes effect only about 2 s after a wake from idle (the first snapshot waits for it); cause not found.
- The colour image functions must be re-tested in daylight (the candidate's imgfx run was made in the dark); T10 was not re-tested.

### Known open items (older)

T23 sporadic single Helix encode error (errno 5; the frequent frame drops are fixed) and T23 real WDR; T41 flip, night column noise, short IVS gaps, day/night and AE/AWB quality; T21 a 4th module reload in one boot crashed once; AEC device tests on T23/T21/T20 (no speaker tests on shared cameras); first release tag after the 24 h soak. Details: the "Still open" section of the full log and the feature matrix.

## OpenIMP changes by date (condensed)

Only OpenIMP (userspace libimp) changes, newest first. Everything listed was device-tested on the SoC named unless marked otherwise.

## 2026-10-09

- Release candidate `agg-34` (838f8147): agg-32 plus the ROI clamp and the reviewed DeepSeek patches (see the section above).
- ROI: every window delta is limited to -25..+25 (spread at most 25, picture QP plus delta inside the rate control's min/max QP) on T31 and on the T10/T20/T21 Helix command lists, so hardware decoders no longer show broken blocks; under FixQP the T31 table is not clamped to the RC range. T31 measured: relative -20 became -19, 0 decoder errors in 20 VA-API checks.
- DeepSeek review: vendor ABI layouts, encoder group bounds (6; 8 on T23/T40), T31 IVS channel 64, `OSD_SetPoolSize(0)`, idempotent T31 IVS group create/destroy, `PollingStream` guards (host-tested; device checks pending).
- open-tx-isp (not OpenIMP): front-crop guards on T31/T23/T20/T10 and the T20 `isp-m0` gain cap, see the section above.

## 2026-10-07 to 2026-10-08 (agg-30 to agg-32)

- T10/T20/T21: encoder ROI and chroma QP offset on the Helix/NVPU; T21 `SetChnROI` effective by default (beyond vendor, `OPENIMP_T21_ROI=0` switches it off); `roitest` tool.
- T31: encoder ROI through the AVPU QP table (`IMP_Encoder_Set/GetChnRoiAttr`, beyond vendor, `OPENIMP_T31_ROI=0` refuses it); T41: experimental ROI table, off by default (`OPENIMP_T41_ROI=1`).
- Soak of agg-32 for 43.4 h on ten cameras (see the section above).

## 2026-10-06

- Release candidate `agg-27` (merge of the release branches); the apitest/imgfx FAILs of the day were fixed or classified (T20 212 PASS / 0 FAIL, T21 228 / 0, T23 304 / 4 with the AF getters fixed afterwards, T31 274 / 1). See the summary above and the full log.
- FrameSource T20 (`claude/t20-fs-frames`): `REQBUFS` count = pool size (+ max delay) and `SET_BANKS` as the vendor library; before, apps that set a frame depth got `nrVBs` frames and then nothing. `SetFrameDepth(chn, 0)` succeeds.
- FrameSource T20/T21/T23/T31: `GetFrame`/`SnapFrame` wait up to 2 s like the vendor library; the T20 capture thread no longer sleeps in `DQBUF` without a queued buffer (`claude/release-fs-enc-ivs`, `claude/release-quickwins`).
- Encoder: `IMP_Encoder_GetFd` (pipe plus pump thread); a JPEG channel reads the frame source itself when its video channel is not polled; `InsertUserData` emits a `user_data_unregistered` SEI on T20/T21/T23; `PollingModuleStream` honours the input bitmap; T31 `SetMaxStreamCnt` refused on a created channel.
- IVS: two groups as every vendor libimp (group 1 move/base-move device-tested on T20/T21).
- T31: `GetAwbZone` passed a stack struct of pointers where the driver copies 675 bytes (bus error); `GetAWBCt` always returned -1; `EnableDefog` always wrote 1 (`claude/release-t23-getters`, `claude/release-deepseek-defog`).
- T23: `GetSensorRegister` bus-type offset, `FrameSource_GetPool`, monotonic audio frame stamps (`claude/release-t23-getters`); the pool rmem is parked at `DisableChn` as on T21 (`claude/release-t23-vbm`, 40 streamer restarts, every snapshot OK, 0 oopses; `OPENIMP_VBM_PARK=0` frees at once).
- T21: `SetDPStrength` and `SetAntiFogAttr` exported like the vendor T21 libimp (DP strength reaches the DPC ratio, antifog goes to control 0x8000163 which the OEM kernel accepts without effect) (`claude/release-t21-image`). `SetBrightness` acts through the AE target (beyond vendor, driver change in open-tx-isp `claude/release-t21-brightness`; imgfx Y 28.8/117.7/193.0 for 30/128/225, timps `image.brightness` live 44.5/130.8/223.9). SEPIA works (open-tx-isp `claude/release-t21-image`).
- Core: `IMP_Log_Set_Option` exported; `DisableSensor` passes -1 to `DESTROY_LINKS` like stock (patches from an automated review, verified against the vendor disassembly).
- Release quick wins (host-tested only): T21 `SetModuleControl` cache fallback, `AI_SetHpfCoFrequency(0)` accepted on T40/T41, P3 capture restart guard, T21 OSD INFO lines trace-only.
- Pending (host-tested and cross-built only): `IMP_AI_SetHpfCoFrequency` takes effect on T23/T31 (float biquad in libaudioProcess-neo, beyond the original library); T31 Enable/DisableMovestate run the vendor logic; T21 `QpgMode`/`H265TransCfg` as the vendor; `SetbufshareChn` documented as store-only.
- imgfx: the green/magenta stripe in the top rows of the saved ch1 pictures was a tool error (chroma plane read at the wrong offset); streams and snapshots were never affected.
- Known issues of the candidate: T23 crop hang (under investigation); T31 H.264 stall after JPEG teardown fixed for agg-29, T41 not part of the release.

## 2026-10-05

- T10/T20/T21: front crop and CSC presets (beyond vendor, device-tested: crop, rejection of invalid values, 10 cycles), software rotation 90/270 on sub streams (T21 with OSD, colours correct), CSC mode 4 sign fix (green picture on T10/T20), encoder ROI and chroma QP offset (T20 device-tested; QP51 region coarse, QP15 fine, absolute QP bypasses CBR).
- T20/T21/T23/T31/T41: many vendor functions connected that were stubs, cache-only or missing (audit of every vendor IMP/SU function per SoC, see the feature matrix): AE/AWB zone and statistics getters, WaitFrame, ModuleControl, delay FIFO (`SetMaxDelay`/`SetDelay`/`GetTimedFrame`), `FlushStream`, AENC/ADEC on all SoCs, `IMP_AO_CacheSwitch`, T41 `SnapFrame`.
- T31: stack overruns fixed and verified with guard arenas: `GetAfHist` wrote 88 bytes into the 24-byte vendor struct, `Get/SetAeAttr` overran the caller by 80 bytes; AF statistics chain rebuilt (focus value, zones, weights, histogram).
- T23: `SetGamma` takes effect at once (steep/linear curves visible, falling curve rejected, restore ok).
- T21: the first snapshot after a start or an idle wake had no OSD (the IPU blend has no effect for about 2 s after a wake); OpenIMP now verifies the blend on a glyph pixel and withholds JPEG frames without a confirmed overlay (beyond vendor: the vendor stack shows the same missing OSD).
- T41: restarting one channel while the other streams hung the SoC; a driver fix (flip update only on a real change, output kept like the vendor on stream-off) passed the targeted repro 35/35 and a 40 min soak; the hang returns once the MSCA output geometry is latched correctly (see the known issues above, T41 is not part of the first release).

## 2026-10-09 (branch `claude/roi-qp-clamp`, host-tested only)

- ROI QP stays valid H.264 (`src/avpu_roi.h`, `Helix_H264_RoiSanitize`): the encoder codes `mb_qp_delta` against the previous macroblock and H.264 allows -26..+25 only; outside it ffmpeg hides the error but VA-API/VLC/browsers show broken blocks (measured on T10/T20 with relative -26 and absolute 15 at QP 42). Every window delta is now in -25..25, the spread of all deltas (uncovered = 0) at most 25, picture QP + delta inside 0..51 and the RC min/max QP; larger requests are clamped with a one-time warning.
  - T31 (and the experimental T41 table): `SetChnRoiAttr` refuses a delta outside -26..25 like the vendor T41 (before: -32..31); the table follows the picture QP when the clamp depends on it.
  - T10/T20/T21: `IMP_Encoder_SetChnROI` clamps when each command list is built (relative -25..25, absolute inside max QP - 25 .. min QP + 25 of the picture, spread 25); a QP argument beyond the s8 range keeps its sign.
  - T20 (review of agg-34): an absolute QP the clamp would push to the far side of the slice QP (cap 51 at slice QP 20: absolute 15 became 26, worse than the surroundings) is left at the slice QP instead, i.e. the region becomes a no-op rather than inverting the request. Test case absolute 15 at slice QP 20, cap 51.
  - Test `tests/t31/roi_clamp_test.c` (20000 random window sets, each table walked like the encoder).

## 2026-10-07 (branch `claude/roi-all`, not part of the first release)

- T21: `IMP_Encoder_SetChnROI` is effective by default (beyond vendor: the stock T21 libimp stores the regions but never programs them). With no region set the command list is unchanged; `OPENIMP_T21_ROI=0` switches it off. Device-tested on the PC420 (jxf23, 1920x1080, FixQP 30, one 320x320 region): absolute QP 51 is a visible block, QP 15 stays clean, delta +/-15 works, live changes (P frames, no IDR) work. FixQP bitrate 657 kbit/s without ROI, 340 with QP 51, 504 with QP 15; CBR keeps the target (the bits move between the regions). Test tool `tools/roitest`.
- T41: `IMP_Encoder_SetChnRoiAttr/GetChnRoiAttr` (vendor T41 API, 10 windows, delta QP) exist but are experimental and off (`OPENIMP_T41_ROI=1`): the macroblock QP table in EP2 was identified from a vendor capture and is written, but the AVPU did not react on the test camera; default behaviour unchanged (see `docs/ROI.md`).
- T31: encoder ROI (beyond vendor, branch `claude/t31-roi`): `IMP_Encoder_SetChnRoiAttr/GetChnRoiAttr` with the T41 API, 10 windows, delta QP -32..31 or absolute 0..51, live without IDR. The vendor Allegro core shows the switch: cmd[9] bit 25 = use the EP2 macroblock QP table, bit 24 = relative; the table is one byte per macroblock after the 0x40-byte EP2 header. Device-tested on cam-A (sc4336p 2560x1440): FixQP 30 with a 768x512 window, delta +20 / absolute 51 blurs exactly the window (IDR 176 to 156 KB), absolute 10 sharpens it (247 KB), two windows at once, CBR keeps the target; 0 oops. `OPENIMP_T31_ROI=0` switches it off.
- T41 (code only): the same enable bits found in the vendor T41 1.2.6 (`SliceParamToCmdRegsEnc1`: cmd[152] bit 0 = use table, bit 3 = relative); OpenIMP set neither, which explains why the T41 table had no effect. Set now under `OPENIMP_T41_ROI=1`, not yet device-tested.

## 2026-10-06

- T31: `IMP_FrameSource_GetFrame`/`SnapFrame` wait up to 2 s like the vendor libimp (T31 1.1.6 header: default timeout 2 s) instead of failing at once (branch `claude/release-quickwins`, host-tested only). The T31 encoder pulls with `VBMGetFrame` and keeps its own deadline. T41 already waits (about 1 s of DQBUF retries, `openimp_p1.c`) and is unchanged.
- Release quick wins from the 2026-10-06 review (`claude/release-quickwins`, host-tested only): T21 `IMP_ISP_Tuning_SetModuleControl` falls back to the cache when the open T21 driver rejects control 0x80000e2 (T31 still fails); `IMP_AI_SetHpfCoFrequency(0)` is accepted on T40/T41 (0 = default, as on T31); the P3 capture start is refused while the previous capture thread has not exited (no free of the buffer it still uses); the T21 OSD retry/probe and JPEG fan-out INFO lines are trace-only (`openimp_debug_trace_enabled`).
- T23: the FrameSource pool's rmem is parked at DisableChn, as on T21 (branch `claude/release-t23-vbm`). The open T23 driver keeps a stopped MSCA output enabled while the input runs, so the pool is no longer handed to other allocations while the MSCA may still write into it. The block is freed by `DestroyChn`, or at once with `OPENIMP_VBM_PARK=0`.
  - Tested on the Jooan A6M (raptor) together with open-tx-isp `claude/release-t23-driver`: 40 streamer restarts with every snapshot OK, 0 oopses.
- T21: `IMP_ISP_Tuning_SetDPStrength` and `IMP_ISP_Tuning_SetAntiFogAttr` are exported like the vendor T21 libimp (an application built against the T21 SDK did not load without them). DP strength (vendor percentage, cap 200) reaches the open driver's DPC ratio; AntiFog goes to control 0x8000163 like the vendor, which the OEM kernel accepts without effect.
- imgfx: the green/magenta stripe in the top rows of the saved ch1 pictures was a tool error (chroma plane read at width*height instead of after ALIGN16(height) lines); JPEG snapshots and streams were never affected.
- Pending (branch `claude/streamer-gaps-t1x-t23-t31`, host-tested and cross-built only, no device test yet): `IMP_AI_SetHpfCoFrequency` takes effect on T23/T31 (and, untested, T41): `IMP_AI_EnableHpf` designs the filter for the cut-off like the vendor library (illegal for a negative value or above the sample rate; 0 = default tables) and hands it to libaudioProcess-neo as a float biquad at the stream's rate (beyond the original library, which takes libimp's coefficient pointer; neo is recognised by its DRC export); `IMP_AO_EnableHpf` reaches neo the same way. T31 `IMP_ISP_Tuning_Enable/DisableMovestate` run the vendor library's logic (AE block of control 0x800002c, day/night limits from `/etc/sensor/<sensor>move.txt` loaded by EnableTuning, IDR on channel 0 on disable; the stock T31 driver ignores the control). T21 `IMP_Encoder_Get/SetQpgMode` keep any value like the vendor library (nothing reads it there), `IMP_Encoder_Get/SetH265TransCfg` check and drop like the vendor (Get returns zeros). `IMP_Encoder_Get/SetJpegeQl` on T10/T20/T21 was already applied to the running encoder (host test added). `IMP_Encoder_SetbufshareChn` on T31 is store-only in the vendor library too.
- T21 `IMP_ISP_Tuning_SetBrightness` now acts (beyond vendor; the vendor T21 kernel only stores the value): needs open-tx-isp `claude/release-t21-brightness`, no libimp change. Device-tested on the PC420 (jxf23): brightness 30/128/225 gives Y 28.8/117.7/193.0, timps `image.brightness` live the same. 128 is the vendor picture.

## 2026-10-04

- thingino: `openimp` and `open-tx-isp` are part of upstream `aperto` ([#1756](https://github.com/themactep/thingino-firmware/pull/1756)), pinned to the Lu-Fi forks; the T23 OEM Helix helper option and hybrid install are gone. All test cameras run `aperto` images (30/30 snapshots, 0 oops, 0 VPU errors).
- T41: stream buffers sized like the vendor, rmem arena split so an idle 1080p stream can always restart, no software H.264 stub (it produced a corrupt stream); AE compensation, gain/exposure caps and noise reduction reach the ISP.
- Pending (on a branch, not yet in `next`): the audio capture read size that always covers whole driver fragments (ported from an upstream patch, author credited), after the 24 h soak.
- T20/T10: rmem high-water logging and shortfall messages with a concrete suggestion when reserved memory is too small instead of silent degradation.
- Hardening: repeated driver errors rate-limited, silent `-1` returns logged, four NULL crashes, a buffer overflow in the module-chain dump, lost items on EINTR and an OSD size overflow fixed.
- T31: `SetChnQpIPDelta` now updates the value `GetChnAttrRcMode` returns, as the vendor does.
- T23: OSD stride fix (clean date/time text on main and sub stream); sub-stream reference fix; the OEM Helix helper option and the hybrid install are removed, T23 runs fully open. The frequent Helix frame drops are fixed (residual interrupt 0x100, kernel patch merged upstream); still open: a rare single Helix encode error (errno 5).
- T21/T23: sub-stream (640x360) corruption from reference sharing on the small channel fixed.
- T41: H.265 on the AVPU path (like T31); a stuck AVPU job times out after 2 s and resets the core. CBR overshoot fix, unload/flip/BCSH fixes in `next`.
- Review fixes: complete `O_CLOEXEC`, eprc `FRAME_END` for dropped pictures, T31 Allegro RC lock, forced IDR after a YUV error, T20 MB-RC table bounds.
- Size and CPU: T20 MB-RC table 590 to 58 KiB, no per-frame malloc; libimp T31 -34 KB, T41 -20 KB, T21 -17.5 KiB text; JPEG Huffman parsing by table, OSD cache invalidation, T31 EBSP copy (timps CPU T31 8.7 to 7.9 %, T21 17.5 to 15.5 %).
- Motion detection v2 (on by default; `OPENIMP_MOTION_V2=0` restores the vendor algorithm, bit-identical): background model per grid cell, suppression after IR/exposure switches, blob grouping, bounding boxes and strength through the versioned `OpenIMP_IVS_MoveGetResultEx` API; false alarms down in overnight runs.
- T30 readiness without hardware: builds for a real T30 kernel; fixed an `IMPEncoderCHNAttr` ABI size bug (4-byte overrun).
- `next` branch created; it tracks the tested aggregate (fast-forward only).

## 2026-10-03

- T10/T20/T21/T23 H.265: `IMP_Encoder_CreateChn(PT_H265)` fails with -1 and one log line on SoCs without HEVC hardware, so streamers fall back to H.264 at once (the vendor returns 0 and creates an empty channel).
- Rate control, T31: vendor Allegro core ported (VBR, CappedVBR, CappedQuality, CBR), state-identical against traces; closed-loop VBR; no filler NAL in CBR (deviation).
- Rate control, T21/T23 (eprc): vendor controller ported with 0 oracle deviations, default on T21; FIXQP, scene-cut IDR, runtime RC/fps/GOP/HSkip, `SetChnHSkip`; opt-in macroblock-level RC (`OPENIMP_EPRC_MBRC=1`).
- Rate control, T20/T10: vendor controllers as default (T20 I-aware P budget, T10 super-frame fix: re-encodes 800 to 0, CPU 8.3 to 5.5 %).
- T41: bitrate setting had no effect (negative bucket level discarded) fixed; OpenIMP runs against the vendor T41 driver with video, JPEG, OSD and motion detection.
- T10: picture drifting diagonally fixed (reference border added twice); noise-reduction strength acts (the vendor does nothing).
- T20: snapshot debounce no longer polls the JPEG encoder (14.4/15.0 fps instead of 11.2/14.3 with 1 snapshot/s); 270-line sub stream rounded to 272 instead of a scaler hang; bottom-row chroma fix.
- T23: brightness/contrast/saturation/hue act; JPEG shares the H.264 bitstream area (-1.44 MB); sub-stream rotation 90/270 on the native encoder; `IMP_Encoder_YuvSetCrop`; daylight green cast fixed by keeping white balance across stream restarts.
- T21: reference-buffer sharing on by default (~1.4 MB less video memory; `OPENIMP_REF_SHARE=0` disables); user contrast sent instead of default.
- Helix: encoder re-created after 3 failed pictures, channel stops after 2 fruitless re-creates; JPEG bitstream buffer 1 MiB (-328 KiB on a T10).
- Vendor logging functions built into libimp; `libalog`/`libsysutils` no longer needed on images.
- Size: `gc-sections` (T23 libimp 774 to 726 KB, T20 694 to 594 KB).
- Encoder diagnostics: effective rate control logged per channel, out-of-range QP/fps clamped with a warning.

## 2026-10-02

- T31: hardware JPEG default; real HEVC on the AVPU; software rotation 90/270; lambda tables generated from a formula; real AECM echo cancellation (-18 dB echo on a speech loopback).
- Helix (T20/T21/T23): hardware JPEG without the vendor library (~95 % less CPU for snapshots); dedicated JPEG/MJPEG channel gets frames; OSD lines, rectangles and bitmaps drawn on T31/T20/T21/T30.
- T23: native Helix H.264 encoder is the default, no vendor helper or vendor libimp; RC parameters reach the encoder; bitstream overflow handling (frame dropped and QP raised instead of a stuck channel).
- T21: shared bitstream buffer sized like the vendor (main/sub switching works), EMC scratch sized like the vendor (free rmem 1.56 to 2.76 MB).
- Robustness audits: AEC reference queue heap overflow, audio-effect switch use-after-free, double stop, DQBUF/EPIPE races at channel stop.
- Independent review: T23 reconfigure divide-by-zero race fixed; top-level `NOTICE` added.

## 2026-09-30 to 2026-10-01 (campaign start)

- ISP tuning: vendor defaults for contrast/sharpness; SDK control IDs and pointer semantics on T20/T21; T23 `GetSensorAttr` buffer overrun; T31 AF IDs and driver-backed SensorAttr/WaitFrame/ModuleControl.
- Day/night on T20/T21 re-sends image controls like the vendor.
- Encoder: CappedVBR/CappedQuality/SMART mapped to VBR with a log line instead of silent CBR; JPEG quality applied; channel-stat struct layout fixed.
- Motion detection: real frame-diff IVS on T20/T21/T30 (was always "no motion").
- Audio out: volume/mute applied, whole-fragment writes.
- T20/T21 framesource: pool parking and reuse instead of freeing on idle teardown.
- Tools: `t23tune` for T23 tuning on device.
