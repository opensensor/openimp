# RC3 release note (DRAFT, not tagged, not published)

RC3 is the third release candidate of the open camera stack (OpenIMP libimp + open-tx-isp kernel driver). It is cut when the RC3 gate of `docs/status/release.yaml` is green: (a) every vendor function that a streamer (timps, prudynt, raptor) calls works on every gate SoC (T10, T20, T21, T23, T30, T31, T40, T41; API cell `dev`, `dev+`, `run`, `rt`, a matched vendor `no-op`, or `nohw` by user decision), (b) no open issue with severity critical or high, and (c) no open issue in the RC3 milestone. State on 2026-11-07: **gate green**, 2017 of 2017 streamer-function cells work, 0 blocking issues. RC3 opens the Release 1 window: the image built from the RC3 tag must then run 14 days without an unplanned reset on at least one camera per gate SoC. Issue numbers refer to https://github.com/Lu-Fi/openimp/issues. Full details: `CHANGELOG.md` of both repositories, `docs/FEATURE_MATRIX.md`, `docs/OPENIMP_BEYOND_VENDOR.md`.

## Changes since RC2 (v2026.10.25-rc2)

### All SoCs / tooling

- API test app `openimp-apitest` on every SoC, device evidence per API cell, coverage check in `make check` (every vendor function has a case or a documented reason)
- Machine-readable release gates, generated feature matrix with gate section, daily soak summary (`make status-check`, `make status-soak`)
- Fallback frame when the sensor stops (T40/T41, default on) and a reuse marker for stand-in JPEGs (`OpenIMP_Encoder_StreamIsReused`)
- FrameSource software rotation: cheaper C kernel, optional no-copy-back spares (`OPENIMP_FS_ROTATE_SPARES`, one consumer per channel), rotation capability marker `OpenIMP_Cap_FsRotate` / `OpenIMP_FS_RotateLimits` (#212, #219)
- Optional ISP bypass graphs (RAW channel) on T23, T30, T31, T41, default off (#125, #129, #218)
- Encoder sleeps on the frame-ready event on T10/T20/T21/T23 (encoder thread CPU -30 to -48 %) (#151)
- `SU_Base_Shutdown` vendor-equal (#206); FrameSource teardown stall watch in the test app (#198)
- Kernel: unused ISP interrupt sources masked on T23/T31, Linux 7.x build and runtime fixes, libgcc-helper guard for module builds

### T10 / T20 / T21

- Decoder `IMP_Decoder_*` as a software JPEG decoder (#3, #5, #7)
- Encoder backlog closed: `SetChnHSkip` / BlackEnhance / scene-cut IDR, `SetChangeRef`, `SetChnDemask`, `SetFisheyeEnableStatus`, `Get/SetH264TransCfg`, `SetChnRcTrigLevel` as the vendor library (#3, #5, #7)
- T20: macroblock rate control on by default, FIXQP I pictures at QP - 3, I-aware CBR P budget (default on, extra) (#191)
- T10/T20 software rotation envelope = T21 (704x576, multiples of 16)
- T21: `SetFrameDepth` no longer sends a stray ioctl (#205)
- Driver reviews (#220, #221, #222, #231): fewer high-order allocations, T10/T20 module RAM about -20 KB (bring-up trace parameters only with `TX_ISP_TRACE=y`), IQ bin checked before it replaces the tables in use (extra), firmware semaphore re-init without use-after-free, T21 VIC error IRQ no longer writes the NULL page, T21 netlink/deinit serialised, T21 `warn_stack` (default off: no vendor stack dump after warnings, extra)

### T23

- FrameSource software rotation 90/270 up to 1280x720 (beyond vendor); MXU2 never probed on T23 (#229)
- Optional ISP bypass graph `OPENIMP_T23_BYPASS_GRAPH` / `isp_bypass_graph=1` (#129)
- API pass on both T23 cameras (13 cells moved), ISP OSD pictures and odd-width refusal, AutoZoom get (#127)
- Driver: AWB luminance threshold rewritten from the AE mean on every AWB interrupt like stock (`source_awb_lum_th`, #231); review: no idle IRQ threads, dead ISR chain unlinked, AWB statistics only when used (#224)

### T30 (gate SoC since 2026-11-03, T30X lab camera)

- H.264 on the device at 2048x1536 / 1080p / 720p plus a 640x360 sub-stream; heights that are not a multiple of 16 encode again (#209)
- H.265 on the Radix encoder, vendor-equal CBR (within 0.1-2.4 %), 2048x1536 where the vendor delivers nothing; capability marker `OpenIMP_Cap_H265`, `OPENIMP_T30_H265=0` off (#208)
- Live rate-control change on H.265 reads the H.265 attribute (was stuck at 361 kbit/s), vendor-equal on the device (#228)
- OEM picture rate controller (eprc T30 revision, bit-exact against the stock library) (#210, #219)
- Calm first stream window (extra beyond vendor, default on): start QP from bits per pixel, black start until the AE settled, start QP floor for the first GOP; first-2 s peak 1787 vs vendor 2671 kbit/s at 1080p15 CBR 2000 (#216)
- Software rotation through `SetChnRotate` (beyond vendor, MXU2) (#212); encoder ROI and super-frame as the vendor (#16, #19)
- ISP bypass graph device-proven: RAW 2048x1536 12-bit in 16 bits, then NV12 again (`OPENIMP_T30_BYPASS_GRAPH=1`) (#218)
- `IMP_FrameSource_GetFrame` compact NV12 like the vendor (#202); `GetAFMetrices` like the vendor; WaitFrame, RawDRC, antifog, mirror, picture controls, CSC presets (extra, opt-in) reach the driver
- Driver: tuning-bin customer init, system table, day/night bank reload, CCM light-source bank, sharpen coring, analog gain path, AWB_CWF_SHIFT set, Iridix strength fixes, direct raw writer for the bypass graph (#177, #202)

### T31

- Optional ISP bypass graph `OPENIMP_T31_BYPASS_GRAPH` / `isp_bypass_graph=1` (#129)
- `GetChnEvalInfo` record in the header layout; defog tables from the active tuning bin; `isp_memopt=1` default; WDR output-mode getter, custom mode without a custom bin, sensor register access (#113, #117, #118)
- Driver review (#223, #231): module RAM -16 KB, no idle IRQ threads, "ch0 done" counted once, `isp-w00/w01/fs` show the VIC frame count, CSI shared-line handler fixed (extra)

### T40 (open driver, Eufy T40XP / sc830ai)

- Vendor-equal picture in the fair light-box pass (#84): AE target, metering and exposure split as the OEM (#225), frame AE meters the hardware AE statistics (#232), AeTargetComp like the vendor (comp 40..200: Y 94..137 vs 92..142) (#232), antiflicker rung trade in the gain the sensor applies (#219), night antiflicker per tuning bank (night IT 215 / Y 79 vs 173 / 72)
- AWB: thresholds follow the total gain, bank refreshed on day/night switch (bit-exact thresholds, gains within 1 %); chroma NR repack threshold like the OEM, chroma noise vendor-equal (#213); CDNS day/night like the vendor (#203)
- Gamma EV follow-up (`t40_gamma_ev`, no-op on sc830ai) and WDR EV store (#204); CLM as the vendor (#85)
- The lifted colour chains (ADR, BCSH, CDNS, DPC, LCE, SDNS, YSP, CCM, DMSC, GIB, defog, AWB) are plain C, oracle-checked against the vendor module; unused lifted code removed (#85, #189, #193)
- AE/AWB algorithm callbacks (`SetAeAlgoFunc` / `SetAwbAlgoFunc`) change exposure and white balance (#65); AE tuning API on the frame AE; privacy mask and ISP draw on the scaled output (#186, #187)
- Release-gate oopses fixed, AWB close oops (#173), exit hardening, reload clean; streamer CPU 0.34 % vs 2.26 % on the vendor stack

### T41

- Custom AE/AWB algorithm libraries (`SetAeAlgoFunc`, `SetAwbAlgoFunc`, extra beyond the vendor; released when the client dies) (#76, #195, #197)
- Optional ISP bypass RAW16 channel (`OPENIMP_T41_BYPASS_GRAPH=1`) (#125)
- ISP backlog closed: LDC / GPIO vendor no-op, StartNightMode, WDR_OPEN, ISP routes, AF statistics (`t41_af=1`), user gamma curve, ISP-drawn OSD, media memory pools / `SetPool` (#72, #75, #171, #179)
- `SU_Base_SetWkupMode` RTC proof (#80); 56 in-progress API cells checked on the two T41 cameras

## Known limitations / not covered

- T23: the open AWB runs on every 4th ISP interrupt, the vendor on every interrupt (#233; needs a light-box proof).
- T30: picture quality (SSIM) against the vendor not measured; FixQP and SMART not run on the device; run-time frame-rate / GOP changes host-tested only; the start-QP map is fitted on the SC4236 only; DMIC rows count as `nohw` (no DMIC on the lab board, not verified on hardware).
- T40: only the sc830ai board is device-tested; at comp 200 the open analog gain stops at 4095 (vendor 5247); AO / AEC rows still in progress; on 7 of 37 shipped T40 bins `t40_gamma_ev=1` changes the picture with the exposure (vendor behaviour).
- #94 (T31L) and #108 (T23) were closed after a 3 h debug-kernel stress without a reset (user rule 2026-11-07); their causes stay unexplained and a recurrence re-opens them.
- T32 and T33 are planned / QEMU only and not part of RC3.
- Some API cells count at the evidence level "API round trip" (`rt`), e.g. `DMIC_*` on T31/T40/T41.

## Upgrade notes

- OpenIMP environment variables, new since RC2 (default in brackets): `OPENIMP_T30_START_QP` (auto; `vendor` = start QP 26, or `10`..`51`), `OPENIMP_T30_BLACK_START` (on; `0` off), `OPENIMP_T30_START_FLOOR` (on; `0` off), `OPENIMP_T30_H265` (on; `0` disables H.265 and clears `OpenIMP_Cap_H265`), `OPENIMP_T30_EPRC` (on; `0` = old GOP controller), `OPENIMP_T30_BYPASS_GRAPH` / `OPENIMP_T23_BYPASS_GRAPH` / `OPENIMP_T31_BYPASS_GRAPH` / `OPENIMP_T41_BYPASS_GRAPH` (off; needs the driver's `isp_bypass_graph=1` where it exists), `OPENIMP_FS_ROTATE_SPARES` (off), `OPENIMP_MXU` (on; `0` = C kernels only), `OPENIMP_FALLBACK_FRAME` (on, T40/T41) with `OPENIMP_FALLBACK_TIMEOUT_MS`, `OPENIMP_HELIX_JPEG_BUSY_WAIT_MS` (debug).
- open-tx-isp T40 module parameters, new since RC2 (default): `t40_ae_target_oem` (1), `t40_ae_stats_src` (0 = hardware statistics while they run), `t40_ae_hi_thr` (0 = bank), `t40_ae_af_night_bin` (1), `t40_ae_split_legacy` (0), `t40_ae_trade_sensor_gain` (1), `t40_gamma_ev` (1), `t40_clm` (1), `t40_dn_keep_awb1` (1), `t40_dmsc_day_follow` (1), defog fixes `t40_defog_*` (1); changed default: `cdns_thresh` 256 (was 65536, bring-up builds only). Removed: `t40_oem_ae` and the T40 bring-up CLM / ISR-literal parameters.
- Other module parameters: T21 `warn_stack` (0), T23 `source_awb_lum_th` (1), T23/T31/T41 `isp_bypass_graph` (0), T31 `isp_memopt` default 1, T23/T31/T41 `*_isr_enable` masks. T10/T20: `t20_trace`, `t10_runtime_trace` / `t20_runtime_trace`, `t20_trace_events` are no longer built by default (`TX_ISP_TRACE=y` builds them); the 3.10 kernel refuses to load a module given an unknown parameter, so remove them from load scripts.

## Components

| Component | Version |
|---|---|
| OpenIMP | `next` commit `<RC3 commit>`, tag `<RC3 tag>` (docs pass on top of 52c530d2) |
| open-tx-isp | `next` commit `<RC3 commit>`, tag `<RC3 tag>` (docs pass on top of 57e45ea8) |
| timps | v1.12.0 (branch dev/t30, f7811f7, local) |
| thingino firmware | `aperto`, sync branch `claude/aperto-sync-1107` (pending) |
