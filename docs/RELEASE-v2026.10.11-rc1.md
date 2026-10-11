# OpenIMP v2026.10.11-rc1: first release candidate

**OpenIMP** is an open replacement for Ingenic's closed `libimp.so` / `libsysutils.so` (video, ISP tuning, encoder, OSD, IVS, audio and system API). With its companion **open-tx-isp** (the open ISP kernel driver replacing `tx-isp-<soc>.ko`) it forms a fully open camera stack for Ingenic T-series SoCs. The public API is the vendor IMP API, so streamers link against it unchanged. Release together with [open-tx-isp v2026.10.11-rc1](https://github.com/Lu-Fi/open-tx-isp/blob/v2026.10.11-rc1/docs/RELEASE-v2026.10.11-rc1.md); always use matching tags.

| Read more (pinned to this tag) | |
|---|---|
| **Feature matrix** (per function and SoC, open vs vendor) | [FEATURE_MATRIX.md](https://github.com/Lu-Fi/openimp/blob/v2026.10.11-rc1/docs/FEATURE_MATRIX.md) · [rendered HTML](https://htmlpreview.github.io/?https://github.com/Lu-Fi/openimp/blob/v2026.10.11-rc1/docs/feature-matrix.html) |
| Beyond the vendor stack: integration notes and env switches | [OPENIMP_BEYOND_VENDOR.md](https://github.com/Lu-Fi/openimp/blob/v2026.10.11-rc1/docs/OPENIMP_BEYOND_VENDOR.md) |
| Full campaign log with test evidence | [OPEN_STACK_CHANGELOG.md](https://github.com/Lu-Fi/openimp/blob/v2026.10.11-rc1/docs/OPEN_STACK_CHANGELOG.md) |
| Condensed changelog · ROI rules | [CHANGELOG.md](https://github.com/Lu-Fi/openimp/blob/v2026.10.11-rc1/CHANGELOG.md) · [ROI.md](https://github.com/Lu-Fi/openimp/blob/v2026.10.11-rc1/docs/ROI.md) |

## 1. Summary

| Component | Revision |
|---|---|
| OpenIMP | `v2026.10.11-rc1` = `fe9307c` (soaked agg-35 code `687a68e4` + documentation only) |
| open-tx-isp | `v2026.10.11-rc1` = `191be6c5` (soaked agg-35 code `6e464ac7` + documentation only) |
| Recommended streamer | [timps](https://github.com/Lu-Fi/timps) **v1.10.0** (tag `4432fa8`); the soaked images used `f1565f7`, whose source is identical to v1.10.0 (later commits are docs/QA only) |
| thingino | `aperto` branch, packages `openimp` / `open-tx-isp` pinned to `v2026.10.11-rc1` |

Scope: all OpenIMP changes since the start of the device-test campaign (base `9c6b0ba`, 2026-09-29), 795 commits; about 195 of them are new since the previous `aperto` state (2026-10-06).

**RC1** is exactly the code that ran the overnight soak below; only documentation changed afterwards. If no blocking problem is reported, the same code becomes the first dated release on `aperto`. Later work (agg-36) is not included (section 5).

| SoC | Status | Encoders |
|---|---|---|
| T10, T20, T21 | **release** | H.264 + HW JPEG on Helix/NVPU (T10 userspace = T20 build) |
| T23 | **release** | native Helix H.264 (no vendor code at all) + HW JPEG; no HEVC hardware |
| T31 (reference) | **release** | H.264, H.265, HW JPEG on the AVPU |
| T30 | experimental | H.264; no T30 device in our lab; an external contributor reported a live gate on a T30X (`docs/T30_STATUS.md`); test kit in `tools/testkit/t30` |
| T40 | experimental | H.264; first device run only |
| T41 | experimental | H.264, H.265; sub-stream geometry and night colour open |

**Completeness** (vendor IMP/SU functions per SoC, from the [feature matrix](https://github.com/Lu-Fi/openimp/blob/v2026.10.11-rc1/docs/FEATURE_MATRIX.md#progress-per-soc) at this tag; "done" = device-tested + host-tested + audited, "gaps" = cache-only, stub, error or missing; "streamer-used gaps" = gaps in functions that timps, prudynt or raptor import):

| SoC | Vendor functions | Done | Gaps | Streamer-used gaps | Status |
|---|--:|--:|--:|--:|---|
| T10 | 325 | 89.2 % | 32 | 0 | release |
| T20 | 325 | 90.5 % | 30 | 0 | release |
| T21 | 325 | 93.2 % | 14 | 0 | release |
| T23 | 437 | 93.8 % | 15 | 0 | release |
| T31 | 387 | 96.4 % | 8 | 0 | release |
| T30 | 347 | n/a (no device; 293 not audited) | 54 | 2 (raptor) | experimental |
| T40 | 398 | 25.4 % (177 not audited/run yet) | 120 | 7 (raptor) | experimental |
| T41 | 445 | 59.3 % | 179 | 22 (prudynt 6, raptor 19) | experimental |

Streamer-used gaps: T30 2 (raptor: `DMIC_*`), T40 7 (raptor: ISP mask, ISP-OSD regions, DMIC), T41 22 (prudynt 6, raptor 19: ISP bypass, module control, autozoom, mask block, scaler level, `SwitchBin`, bufshare, ...). Work after RC1 (agg-36) raises T30/T40/T41 further and closes most of these (T40 OSD/ISP-OSD/`SwitchBin`/bufshare, T41 zoom/mask/scaler level).

**Streamers.** timps v1.10.0 is recommended and ran the soak; some fixes need its matching changes (e.g. T20/T10 day/night). prudynt and raptor link unchanged (0 of 149 imports missing for prudynt, none for raptor) and were run on a T23 camera over the network; a T23 camera ran raptor through the soak. A stack overflow in raptor-hal's `IMP_IVS_Get/SetParam` buffer (0x420 bytes, libimp writes up to 0x458) was reported upstream.

## 2. Release evidence

**Overnight soak of the RC1 code** (2026-10-10 18:04 to 2026-10-11 08:00, about 14 h, 10 cameras: T10, T20 x2, T21 x2, T23 x2, T31, T41 and one more; full OTA images with timps v1.10.0 source on thingino `aperto`): **0 kernel oopses, 0 unexplained reboots, 0 streamer restarts, VA-API decode check 54/54.** The previous soak of this line (agg-32, 43.4 h) had 0 unplanned reboots and 0 oopses on the cameras without user interaction; the problems it found (T31 front-crop crash, T20 day/night) are fixed here.

**Release gate (apitest = one call per vendor function, imgfx = one picture per image function): every FAIL fixed or explained.**

| SoC | apitest | Remaining FAILs |
|---|---|---|
| T20 (2 cams) | 212 PASS / 0 FAIL | – (`GetAeZone` N/A: not served by the stock module) |
| T21 (2 cams) | 228 / 0 | – (33 FAILs before the frame-source fixes) |
| T23 | 304 / 4 | the 4 AF getters; pass since AF statistics are on by default |
| T31 | 274 / 1 | H.264 stall after JPEG teardown; fixed (per-channel `PollingStream` serialisation), FAIL 0 |
| T10 / T20 / T21, gap run 2026-10-10 | 147 / 148 / 155 PASS | T10 1: `SetSuperFrameCfg` read-back differs, the setting is applied (known minor issue) |
| T41 (experimental) | 260 / 9 | driver work and a tool; audio mute since done in software |

imgfx: no open FAIL. Colour functions verified in daylight on T20/T23/T31 by the streamer QA of 2026-10-10 (colour effects B/W, negative, sepia, vivid, AE compensation, crop and ROI: PASS 35/27/36 checks; the only FAIL was a QA threshold artefact, B/W saturation 25.02 % against a 25 % limit, fixed in the QA). T10/T21 colour functions were checked under lamp light or at night only. The 2026-10-10 additions (T23 ROI, T40 run, T41 controls, audio/AEC on five SoCs) were device-tested before the soak; agg-34 passed a streamer smoke test on 9 cameras (50/50).

**Host tests:** `make check` (about 100 test programs) green on all SoCs, cross-builds pass; rate-control oracles (eprc 0 deviations against the vendor; T31 Allegro core state-identical against 20 vendor traces and 72 x 400 random frames); ROI validity over 20 000 random window sets; motion v2 bit-identical to the vendor with `OPENIMP_MOTION_V2=0`.

Below, **[host]** marks items without a device test; everything else was device-tested on the SoC named.

## 3. Highlights by area

**ISP and tuning**
- Controls the vendor ignores now act: T21 brightness (via AE target; 30/128/225 give luma 28.8/117.7/193.0, 128 = vendor picture), sepia, scene/colour effects and `ae_it_max_us`; Sinter/Temper strength on T21/T20/T10; T23 brightness/contrast/saturation/hue and immediate `SetGamma`.
- Full T23 1.3.0 tuning API; T31/T23 control IDs and ABI as in the stock firmware; 19 T23 getters routed; AF statistics/getters on T23 and T31.
- T31 stack overruns fixed (`GetAfHist`, `Get/SetAeAttr`, `GetAwbZone`), `GetAWBCt`, `EnableDefog` flag, `SetFrameDrop` with stock semantics (ISP drops the frames).
- T10/T20: vendor control IDs and pointer semantics, `Get/SetIntegrationTime`, `SetDPStrength`, `Get/SetISPHVflip`, `Get/SetWDRAttr` like the 3.12.0 libimp; front crop and CSC presets (beyond vendor).
- Unknown controls return an error instead of a silent success; getters return what was set.
- T41: gamma, CCM, CSC presets/user tables, manual exposure, module control/ratios, frame drop, sensor register, AF weight, WDR enable. T40: tuning Get calls device-tested, Set calls [host].

**AE, AWB, day/night**
- T20/T10 day/night fixed with the driver: `isp-m0` reports the analog gain cap the AE really reaches (T20 128 instead of 158; T10 the stepped plateau); needs timps v1.10.0. Confirmed on a T20 camera: correct switch to Night at dusk and back to Day in the morning (ISP confirmed) in the agg-34 soak; both T20 cameras stayed correct through the RC1 soak.
- T20/T21 re-send image controls on day/night like the vendor; T23 keeps white balance across stream restarts (no green snapshots); T21 AWB hysteresis at dusk.
- `SetAeComp`: T20 monotonic; T31 OEM formula but slow AE (allow about 30 s to settle).

**Encoder**
- T31: real HEVC; vendor Allegro rate-control core (VBR closed loop, CappedVBR/CappedQuality with the 42 dB cap, CBR); H.264 recovery instead of freezes.
- T23: native Helix H.264 by default (no vendor libimp/helper), overflow handling, sub-stream rotation, `YuvSetCrop`. T21/T23: vendor eprc controller ported (0 oracle deviations). T20/T10: OEM controllers by default, I-aware CBR budget, T10 super-frame fix (re-encodes 800 to 0).
- H.265 on SoCs without HEVC (T10/T20/T21/T23) fails at `CreateChn` instead of creating a channel that never encodes.
- **ROI:** T31 gets an ROI the vendor T31 library lacks (`SetChnRoiAttr`, AVPU QP table, H.264); T21 and T23 `SetChnROI` really programmed; T10/T20 ROI and chroma QP offset. Every path stays valid H.264 (deltas clamped to -25..+25; -26 used to break hardware decoders). T41 ROI code only, off.
- JPEG: hardware JPEG on T31 (AVPU, timps CPU 70 % to 17 % at 1 snapshot/s) and T20/T21/T23 (Helix, about 95 % less CPU); quality applied; a JPEG channel feeds itself when its video channel is not polled.
- `GetFd` (pollable pipe), `InsertUserData` SEI, `PollingModuleStream` bitmap, vendor channel statistics, effective-RC log line, QP/fps clamp warnings, Helix encoder error limit, vendor denoise semantics on T20/T21.
- T40: H.264 on two channels with correct timestamps, software JPEG snapshots.

**Frame source**
- `GetFrame`/`SnapFrame` wait up to 2 s like the vendor; T20 apps that set a frame depth get frames again; delay FIFO and `GetTimedFrame`.
- Software rotation 90/270 (T31; T10/T20/T21/T23 sub-streams); front crop on all release SoCs.
- T21/T23 keep a disabled channel's pool for its next start (40 T23 streamer restarts, every snapshot OK); many lifecycle races fixed.

**OSD** · drawn on T20/T21/T30 (vendor never drew on the Helix path) and by the T31 IPU (also in HW JPEG snapshots); lines, rectangles, bitmaps; T23 OSD ABI and stride fix; first T21 snapshot waits for its OSD; T40 PIC and COVER.

**IVS** · real motion detection on T20/T21/T30 (vendor: always "no motion"); **motion v2** on by default (background model, suppression after IR/gain jumps, objects with bounding boxes via `OpenIMP_IVS_MoveGetResultEx`; overnight false alarms on a T20 about 7 of 13 to about 2 of 11); two IVS groups; T31 IVS about 85 % less CPU on the sub-stream.

**Audio** · AI/AO/AEC device-tested on T10/T20/T21/T23/T31; **real WebRTC AECM echo cancellation** (ERLE 19 to 25 dB; T21 20 dB vs 16 dB with the vendor stack), `EnableAec` errors instead of faking success; volume/mute applied, whole-fragment writes; AENC/ADEC with built-in codecs; HPF cut-off on T23/T31 [host]. T41: software mute only, no audio hardware on the test board.

**System, robustness, memory**
- No `libalog`/`libsysutils` needed; libimp 0.5-0.6 MB instead of 1.0-1.3 MB; less CPU (T20 at night: streamer 10.5 % vs 25.2 % with the vendor stack, snapshots 0.20 s vs 0.45 s).
- More video memory: T21 2.76 MB free instead of about 1.2 MB; reference sharing on T21/T23 saves about 1.5 MB.
- rmem high-water logging and shortfall messages with a concrete `rmem=` suggestion.
- 10 stop/start (incl. `kill -9`) and 10 reload cycles without an oops per SoC; hardening from audits and reviewed automated patches (overflows, NULL crashes, vendor ABI struct sizes, `O_CLOEXEC`, EINTR, rate-limited logs) [partly host].

**Kernel driver** (details in the [open-tx-isp notes](https://github.com/Lu-Fi/open-tx-isp/blob/v2026.10.11-rc1/docs/RELEASE-v2026.10.11-rc1.md)): front-crop guards on T31/T23/T20/T10, T20/T10 gain cap, T23 stock-like start/stop with MSCA scratch buffer, T21 stock AE/AWB lifted, T31 lifecycle hardening, T41 driver controls, reload safety on all SoCs.

**Streamer integration**
- Check return codes: H.265 without HEVC, unknown controls, impossible crop windows and out-of-range ROI now fail.
- Front crop is in **sensor space before the flip**: mirror the window on T23/T21/T31 (both axes) and T20/T10 (vertical only). On T31/T23 keep streams at or below the window (no upscaling). The driver drops the window when the ISP session closes, so re-apply it after a restart. Guarded drivers expose `/sys/module/tx_isp_t23|t31/parameters/fcrop_upscale_pct`.
- ROI: lowest relative QP is -25; avoid extremes on FixQP streams; T20/T21 apply ROI at the next IDR; on T23 probe the symbol `OpenIMP_Cap_T23HelixRoi`.
- T10/T20/T21 accept but ignore QP steps, static time, quality level, I/P delta and bias: do not offer them as live controls.
- T41: keep the sub-stream at 768x432 or smaller.

## 4. Beyond the vendor stack (defaults and switches)

| Feature | SoC | Default · switch |
|---|---|---|
| Encoder ROI | T31 / T21 / T23 | on · `OPENIMP_T31_ROI=0`, `OPENIMP_T21_ROI=0`, `OPENIMP_T23_ROI=0` |
| Experimental ROI table | T41 | off · `OPENIMP_T41_ROI=1` (debug) |
| Motion v2 | all with IVS | on · `OPENIMP_MOTION_V2=0` vendor, `shadow`, mask; `_BG/_SUPPRESS/_BLOBS/_OVERRIDE`, parameters |
| Reference-frame sharing | T21, T23 | on · `OPENIMP_REF_SHARE=0` |
| Hardware JPEG / IPU OSD | T31 | on · `OPENIMP_T31_HW_JPEG=0`, `OPENIMP_T31_OSD=0` |
| Pool parking at `DisableChn` | T23 (T21 always) | on · `OPENIMP_VBM_PARK=0` |
| Rate control | T31 Allegro core · T20 OEM · T10 OEM | on · `OPENIMP_T31_RC_CORE=legacy`, `OPENIMP_T20_RC=0`, `OPENIMP_T10_RC=0` |
| T10 super-frame fix · T20 I-aware CBR | T10 · T20 | on · `OPENIMP_T10_RC_SUPERFRM=0`, `OPENIMP_T20_RC_IAWARE=0/1` |
| eprc selection | T21 · T23 | T21 on, T23 SMART only · `OPENIMP_T21_EPRC=0`, `OPENIMP_T23_EPRC=0/1` |
| Macroblock RC · QP-down limit | T21/T23 (MB RC also T20) | off · `OPENIMP_EPRC_MBRC=1`, `OPENIMP_T20_MBRC=1`, `OPENIMP_EPRC_QP_DOWN1=1/2` (device test pending) |
| Real AECM, brightness/sepia/Sinter-Temper that act, front crop on T10/T20/T21, rotation, self-fed JPEG channel, H.265 refusal, rmem hints | various | on, no switch |

Also: `OPENIMP_LOG_SYSLOG=1` logs to syslog. Debug-only switches (profiling, traces, statistics, A/B rollbacks) are listed in section 7 of OPENIMP_BEYOND_VENDOR.md; do not set them in production. **Less than the vendor:** T31 CBR writes no filler NAL, T31 CappedQuality = CappedVBR, T10/T20 SMART maps to VBR, T23 has no real WDR.

## 5. Known issues and what is not in RC1

**Soak findings**
- T41 channel 1 (sub-stream) colour at night differs from channel 0; T41 stays experimental.
- T31: a black picture of about 80 s seen once during a streamer-driven day/night probe; not understood yet.
- T23 with raptor: day/night flapping at dusk. Day/night is streamer logic; OpenIMP and the driver only report sensor values.
- Day/night in poorly lit places follows the streamer's thresholds; tune the streamer, not the driver.

**Other limitations**
- T40: CBR delivers only about 20 % of the target rate; no H.265; `GetBrightness` returns -1.
- T41: no audio hardware on the test board (API level only); sub-stream above 768x432 can hang with the opt-in geometry latch; heap overwrite across driver reloads.
- T30: no T30 device in our lab; an external contributor reported a live gate on a T30X (`docs/T30_STATUS.md`); test kit in `tools/testkit/t30`. T10: `SetSuperFrameCfg` read-back differs (the setting is applied). T10: occasional Day switch with a black frame in the dark was seen in the earlier soak (3 in 44 h).
- T20/T10: release of the crop window at session close not verified on a device. T10/T21 colour functions checked under lamp light or at night only (T20/T23/T31 verified in daylight); T21 imgfx flip/`SetWB` auto gave a magenta picture (the streamer's flip path is fine).
- T21: OSD blend about 2 s late after a wake (handled for snapshots); mic gain above 0 raises the noise floor (hardware).
- Remaining vendor-function gaps: see the completeness table in section 1 (no gap in a function a known streamer imports on the release SoCs).

**Not in RC1 (agg-36 and later):** T40 rate control and H.265, T40 ISP-drawn OSD, `SwitchBin`, bufshare, default log level, IVS result ordering, further review fixes, more vendor-API gap work (T30, T10/T20/T21, T23/T31), T41 mask/zoom.

## 6. Upgrade and compatibility

- thingino `aperto`: select the open ISP stack (`BR2_PACKAGE_THINGINO_ISP_OPEN`); packages pin `v2026.10.11-rc1`. Flash a **full image** so library, kernel module and kernel VPU/rmem patches match. SDK sensor, audio and AVPU modules stay.
- Use OpenIMP and open-tx-isp of the same tag; front crop, gain cap, T21 brightness/sepia, T23 AF/scratch and T41 controls need both.
- Build timps with `USE_OPENIMP=1` for the OpenIMP-only features; prudynt/raptor need no change.
- **Changed since the previous `aperto` state:** T31 `SetFrontCrop` refused when a running stream is larger than the window; crop windows released at session close (T31/T23/T20/T10); T20/T10 `isp-m0` gain cap is the reachable value; ROI on by default on T21/T23/T31 and clamped; T20/T21 encoder denoise with vendor semantics; encoder group bound 6 (8 on T23/T40), `OSD_SetPoolSize(0)` refused, `PollingStream` on an uncreated channel returns -1; T40/T41 struct layouts follow the vendor ABI (rebuild T40/T41 apps against the vendor headers).
- Every switchable difference from the vendor can be turned off with the variables in section 4; flashing the previous image restores the previous state.

## 7. Testing, reporting, credits

- **Test:** run your normal configuration through a day and a night; watch `dmesg` (oopses, shortfall lines) and the streamer log ("clamped", "rejected", "effective rate control"). `apitest`/`imgfx` and `make check` are in the repository.
- **T30 owners, help wanted:** `tools/testkit/t30` runs OpenIMP from a temporary directory on a T30/T30X camera with the vendor stack, compares `apitest` against the vendor libimp, encodes 8 s of H.264 and packs one result archive; no flashing, no config change, streamer restarted afterwards, no pictures or credentials collected. See its README. T40/T41 results are welcome too.
- **Report** at [openimp issues](https://github.com/Lu-Fi/openimp/issues) or [open-tx-isp issues](https://github.com/Lu-Fi/open-tx-isp/issues): SoC and sensor, both tags, streamer and version, `dmesg`, streamer log, stream set, `rmem`/`ispmem`. No addresses, credentials, location names or private pictures.
- **Contribute:** changes grounded in vendor binary analysis, ABI checks, hardware logs or tests; state evidence, validation and open points; beyond-vendor behaviour must be switchable and documented.
- **Credits:** the original [opensensor/openimp](https://github.com/opensensor/openimp) and [opensensor/open-tx-isp](https://github.com/opensensor/open-tx-isp) projects; [thingino-firmware](https://github.com/themactep/thingino-firmware) and [ingenic-sdk](https://github.com/themactep/ingenic-sdk) and the Ingenic reverse-engineering community; the timps, prudynt and raptor maintainers; WebRTC AECM (BSD-3). Ported upstream patches keep their authorship in the history; per-file licences in `NOTICE`.
