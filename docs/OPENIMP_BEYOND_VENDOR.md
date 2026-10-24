# OpenIMP / open-tx-isp: where we go beyond or differ from the vendor stack

Audience: streamer developers (timps, prudynt, raptor). Each item below behaves differently from, or does
more than, the vendor libimp / kernel driver. Only items that are implemented and device-tested per
[OPEN_STACK_CHANGELOG.md](OPEN_STACK_CHANGELOG.md) and [FEATURE_MATRIX.md](FEATURE_MATRIX.md) are listed;
everything else is in "Unverified" at the end. Cameras are anonymised as in the changelog (cam-A T31,
cam-B T23, cam-C T20, cam-D T21, cam-E T10, cam-F T41). Items of the agg-34 release candidate name the SoC instead of a camera.

Status tags: `[-all-13]` is a historic tag (tested before the later aggregates; everything so tagged is contained in `next`). Rows marked "built, device test pending" are opt-in and not yet device-tested. Branch names (`claude/...`) are historic: those branches were merged into `next` and deleted. Defaults described here are the defaults in `next` (section 7 lists the environment variables).

Integration rule of thumb: all behaviour below is reachable through the **standard IMP API** (same
signatures as the vendor SDK). Nothing needs a new call; where a value is "different", it is in what the
call now returns or what the hardware now does.

## 1. Encoder / rate control

| Feature | SoC | API / param / env | Default | How a streamer uses it | Detect / disable | Status / branch |
|---|---|---|---|---|---|---|
| Capped modes are real, not silent CBR: CappedVBR / CappedQuality run a closed-loop regulator with the vendor PSNR cap (42 dB) | T31 | `IMP_Encoder_CreateChn` with `IMP_ENC_RC_MODE_CAPPED_VBR` / `CAPPED_QUALITY` | on | Select the mode as with the vendor SDK; bitrate tracks `maxBitRate`, quality capped at 42 dB | Log line "effective rate control" per channel; `OPENIMP_T31_VBR_LOOP=0` forces open loop (debug) | cam-A, `claude/rc-modes` [-all-13] |
| **Deviation (less than vendor):** CappedQuality behaves exactly like CappedVBR. The vendor additionally keeps lowering QP while the stream runs at max bitrate (idle < 5 %) and lets the HRD removal time slip (no emergency max-QP on scene change). Decoded in `docs/RC_MODES.md` (`claude/t31-capped-quality`), not implemented yet (full port of the vendor RC core in work) | T31 | rcMode `IMP_ENC_RC_MODE_CAPPED_QUALITY` | – | Treat `capped_quality` as `capped_vbr`; expect ~1–2 QP coarser in static scenes than vendor | – | decoded 2026-10-03, port pending user decision |
| Plain VBR is closed loop | T31 | `IMP_ENC_RC_MODE_VBR` | on | Nothing to do | `OPENIMP_T31_VBR_LOOP=0` (debug A/B) | cam-A, `claude/rc-modes` [-all-13] |
| Allegro rate-control core for VBR / CappedVBR / CappedQuality (vendor behaviour, ported instruction by instruction); CBR now uses the same core (20 trace files + 72 x 400 random frames state-identical) | T31 | env `OPENIMP_T31_RC_CORE` | allegro (= vendor behaviour) | Nothing to do | `OPENIMP_T31_RC_CORE=legacy` restores the old controller | cam-A, `claude/t31-capped-quality`; CBR in `claude/t31-allegro-cbr`, device test pending |
| **Deviation (less than vendor):** T31 CBR writes no filler NAL. The HRD model counts filler bits like the vendor (the filler value is per picture), but the stream is not padded: a static scene's CBR stream stays below the target bitrate where the vendor pads up to it | T31 | `IMP_ENC_RC_MODE_CBR` | – | Do not rely on CBR output being exactly at the target; static scenes come out below it (this saves bandwidth) | compare measured bitrate with the target | `claude/t31-allegro-cbr`, device test pending |
| T10 VBR super-frame fix (P1): VBR no longer misjudges a frame spanning several super-frames; the vendor re-encodes nearly every frame. Measured at 1200 kbit/s: 450 to 822 kbit/s, re-encodes 800 to 0, CPU 8.3 to 5.5 % | T10 | env `OPENIMP_T10_RC_SUPERFRM` (inside the OEM controller `OPENIMP_T10_RC=1`) | on (inside the OEM controller) | Nothing to do | `OPENIMP_T10_RC_SUPERFRM=0` restores vendor-exact behaviour | cam-E, `claude/t10-rc-superfrm` |
| **Built, device test pending:** eprc QP-down limit (restricts how fast QP may fall) | T21 (also in the T21 vendor revision), T23 (eprc controller) | env `OPENIMP_EPRC_QP_DOWN1=1` / `=2` (two limit levels) | off (opt-in) | Opt-in for A/B tests | env var | built, device test pending (`claude/eprc-t21-qp-limit` for the T21 vendor revision) |
| T20 OEM rate controller port, which reads the NVPU statistics registers (needs kernel patch 0101). Now the default, like the vendor (which always runs it). Measured 60 s at 1200 kbit/s: CBR 1435 (the vendor controller overshoots by itself; P2 below tightens it), VBR 1329, SMART 983; old GOP controller CBR 942 | T20 | env `OPENIMP_T20_RC=0` restores the old GOP controller | on (vendor-identical) | Real SMART and tighter VBR | env var; effective-RC log line | cam-C, device-tested, in all-20 and later |
| T20 I-aware P budget (P2): CBR spreads the I-frame cost over the following P frames, which tightens the CBR overshoot. cam-C at 1200 kbit/s: CBR 1583 to 1300 (stats 1244). VBR is left as the vendor (with P2 it fell to 866) | T20 | env `OPENIMP_T20_RC_IAWARE` (inside the OEM controller `OPENIMP_T20_RC=1`) | on for CBR, vendor for VBR/SMART | Nothing to do | `=0` is vendor; `=1` forces it for VBR/SMART too (not recommended) | cam-C, `claude/t20-rc-iaware` |
| T10 OEM-style rate controller | T10 | env `OPENIMP_T10_RC=0` restores the old GOP controller | on | CBR overshoot of the old path was +55 % at 2500 kbit/s | env var; effective-RC log line | cam-E, device-tested, in all-20 and later |
| Effective-RC log line | all | log tag `Encoder` | on | Parse it to see what the encoder really runs (requested mode may map to a different one) | grep the log at channel creation | all cams |
| Out-of-range QP / fps clamped **with a warning** instead of silent replacement | all | `minQp/maxQp`, `fps` in `IMP_Encoder_CreateChn`/`SetChnAttr` | on | Validate your own values; the warning names the clamped field | grep log for the clamp warning | all cams |
| RC readback returns the vendor-clamped values | T20, T21 | `IMP_Encoder_GetChnAttrRcAttr` | on | Do not assume what you set equals what you read back; use the read value | compare set vs get | `claude/rc-modes-2` [-all-13] |
| T23 RC app value 0 = vendor default (QP step 3, static time 15, change position 2/80), not "off" | T23 | RC attr fields QP step, static time, change position | on | Pass 0 to get vendor behaviour; set explicit values to override | n/a | cam-B, `claude/t23-rc-app-defaults` [-all-13] |
| Mode, bitrate, QP range, GOP, fps take effect; QP steps, staticTime/changePos/qualityLvl, I/P delta, bias are **accepted but ignored** | T10, T20, T21 | `IMP_Encoder_*` | n/a | Do not expose those knobs as live controls on these SoCs | documented limitation | `claude/rc-modes-2` [-all-13] |
| Encoder error limit: after 3 failed pictures the encoder is re-created; after 2 fruitless re-creates the channel stops (instead of stalling 20 s per picture) | T20, T21 | internal | on | Expect `GetStream` to fail/return after a channel stop; restart the channel from the streamer | log lines on re-create/stop | cam-C, cam-D soak OK, `claude/helix-error-limit` [-all-13] |
| Real HEVC | T31 | H.265 profile in `IMP_Encoder_CreateChn` | on | Use H.265 like the vendor; before, streams were empty | stream has VPS/SPS/PPS | cam-A, `claude/t31-hevc` |
| `CreateChn(PT_H265)` fails with -1 on SoCs without HEVC hardware. The vendor returns 0 and creates an empty channel that never encodes (no error, no frames). Log line: `H.265 not supported by the hardware on this SoC (Helix encoder is H.264/JPEG only); use H.264` | T10, T20, T21, T23 | H.265 profile in `IMP_Encoder_CreateChn` | on | Check the return value of `CreateChn`; on -1 fall back to H.264 immediately instead of waiting for frames that never arrive | return value -1; log tag `Encoder` | `claude/h265-reject` |
| Encoder channel stats complete (vendor struct layout, real average bitrate) | all | `IMP_Encoder_Query` / `GetChnStat` | on | Can be polled for bitrate telemetry | n/a | `claude/openimp-quickfixes` |
| **T40 `IMP_Encoder_Query` differs (design):** OpenIMP encodes a picture when the application polls (`PollingStream`/`GetStream`), it keeps no input queue and at most one finished stream. `leftPics` is always 0; `leftStreamFrames` is 1 while a finished stream waits for `GetStream` (live; the vendor samples it only inside `GetStream`); `leftStreamBytes` and `curPacks` are 0 as in the vendor library; `work_done` 0 while a picture is encoded | T40 | `IMP_Encoder_Query` | on | Do not use `leftPics`/`leftStreamFrames` to detect a backlog on T40: there is none while nobody polls | n/a | host test, `claude/t40-night` |
| **T40 SEI may be left out on IDR pictures:** `IMP_Encoder_SetChnSeiAttr` writes the user_data_unregistered SEI into the header area in front of the slice, which holds the headers twice (for the hardware and for the published access unit), so headers plus SEI must stay within 272 bytes. H.264 IDR pictures leave room for about 130 bytes of user data, H.265 IDR pictures (VPS/SPS/PPS) for about 90, other pictures for the full 128; a larger SEI is left out on that picture and counted | T40 | `IMP_Encoder_SetChnSeiAttr` | on (only when the application enables SEI) | Keep user data at 90 bytes or less for H.265 | count of SEI packs (nal type 6 / 39) per IDR | host test, `claude/t40-night` |
| T10 drift fix (16-pixel reference border added once) | T10 | internal | on | Nothing; P frames no longer drift diagonally | n/a | cam-E, `claude/t10-drift-fix` |
| T20 bottom-row green flicker fixed (encoder padding rows filled) | T20 | internal | on | Nothing | n/a | cam-C, `claude/t20-bottom-chroma` |

Further items (evening 2026-10-03):

- **T41 AddSensor reclaim (pending):** after an OOM kill the T41 sensor stays registered and `AddSensor` returns EBUSY until reboot (vendor behaviour). A driver fix that re-registers the sensor (`claude/t41-sensor-rereg`) crashed on the first device load and is being analysed; not usable yet.
- **T21 AWB faster than vendor:** the lifted AWB needs 0.95x of the vendor instructions (was 1.41x), output bit-identical, cam-D isp_fw_process -10 % (`claude/t21-size-awb-opt`); the kernel module is also smaller (760 to 452 KB). No switch, nothing to integrate.
- **T40 encoder poll honours its deadline (agg-39 candidate, `claude/t40-poll-timeout` 78ca5d0):** when the ISP delivers no frames, `IMP_Encoder_PollingStream` on T40 returns within its timeout instead of retrying 1000 x 1 ms inside the poll; public `GetFrame` is unchanged. Host tests: no-frame 0/20/100 ms, pacing and budget; Eufy T40 (open driver 3534ee96, t40_devtest mask 5): 35 results with the known BcshHue failure, identical to the unpatched build.
- **T21/T23/T30 libimp Helix per-frame path at `-Os` again (agg-39 candidate, `claude/t21-os-revert` 06bb826):** the `-O2` exception of 256f50c bought no measurable CPU on cam-D (streamer 9.3–10.1 % with `-Os` against 9.5–10.1 % with `-O2`, 2026-10-14) and cost about 18 KB per SoC; nothing to integrate.
- **Smaller binaries (2026-10-03 evening):** gc-sections in OpenIMP (`claude/openimp-size`, 2040a03): T23 libimp 774 to 726 KB, T20 694 to 594 KB; stripped local symbols in open-tx-isp (`claude/open-tx-isp-size`, 15232deb): T23 module 1,211 to 1,047 KB, T20 819 to 775 KB. No API change; rootfs back to 0x4DE000 (T23) / 0x4DD000 (T20).

Further items (night 2026-10-03):

- **eprc macroblock RC (opt-in, `claude/eprc-mbrc` 9e2bc3a, a8b483a: registers 0x400c0/0x400c4 per picture type like the vendor, T23 IDR 0x060404c1/0x61615921, P 0x030484c1/0x61615c21, T21 IDR 0x060407c1, P 0x030487c1; device test running):** 0 deviations against the vendor in the emulator on T23 and T21. Env `OPENIMP_EPRC_MBRC=1`; `IMP_Encoder_SetMbRC` works per channel at runtime (on the vendor SetMbRC has no effect and MB-RC always runs). The vendor uses SAS mode 3 (7 activity-class QP offsets), no per-MB QP map. Vendor bug (class-table index reads past a 9-byte table): OpenIMP uses 0.
- **T21 `ae_it_max_us` acts (`claude/t21-ae-it-max` 840a57ff, beyond vendor, the user decided to keep it):** the vendor T21 ignores the RANGE block of SetIntegrationTime. cam-D: cap 2000 us gives IT 68 lines, dgain 19 to 63; cap 5000 us gives 172 lines; cap 0 returns to 1125 lines. Caveat: a 4th module reload in one boot crashed (under investigation).
- **Smaller modules (`claude/open-tx-isp-size2` a7214c75):** T23 1,047 to 622 KB (vendor 857), T31 859 to 711 KB (vendor 829), T20 775 to 736 KB, T10 770 to 731 KB; device-tested on cam-A and cam-B. No API change.
- **No vendor libimp hybrid on T23:** cam-B runs without it (~328 KiB less in the rootfs). The OEM worker option and the hybrid install were removed from the thingino package afterwards; T23 runs fully on OpenIMP.
- **T10/T20 OEM rate controller as default (device-tested on cam-C and cam-E):** `quality_lvl` / `change_pos` act as in the vendor firmware.

- **eprc complete (T21/T23, `claude/eprc-complete`):** FIXQP, scene-cut IDR, runtime RC/fps/GOP/HSkip changes applied at the next IDR like the vendor, `SetChnHSkip` on T21/T23; 0 oracle deviations. MB-level RC is ported separately (`claude/eprc-mbrc`, opt-in, device test pending). The vendor-identical T21 eprc is now the default (`claude/eprc-t21-default`); cam-D at 1200 kbit/s: CBR 1326, VBR 1096, SMART 1071.
- **T20 snapshot debounce (openimp `claude/openimp-t20-jpeg-align`, timps `claude/timps-jpeg-idle-nopoll`):** it no longer polls the JPEG encoder; with 1 snapshot/s on both channels chn0 14.4 / chn1 15.0 fps (was 11.2 / 14.3). A sub-stream height of 270 is rounded to 272 with a warning (the vendor scaler hangs on it).
- **T10 Sinter/Temper strength acts** (the vendor treats it as a no-op): temporal noise 7.11 / 2.91 / 1.51 at temper 0 / 128 / 255, survives day/night (`claude/t10-t20-nr-wdr`).
- **T41 (cam-F), device-verified:** module reload on the rev2 image, 10/10 rmmod/insmod cycles, refcnt 0, 0 oops, kill -9 of the streamer recovers 3/3 (root cause was a decompiled tuning-node helper overwriting .bss, `claude/t41-matrix-fixes`); brightness 255 gives Y 211, contrast 0 flat grey, saturation 0/255 chroma 0.1/7.1; bitrate 400/1200/3000 gives 518/1195/2777 kbit/s over 30 s each (`claude/t41-cbr-overshoot`). Open: the driver writes the sensor flip synchronously, but timps does not call SetHVFLIP live on T41; u-boot ignores the stored env (fw_env.config size mismatch), so changing rmem needs an env-partition image (user decision pending).

### 1.0 Encoder telemetry (T41, 2026-10-24)

`OpenIMP_Encoder_GetTelemetry(chn, &t)` (`include/openimp/openimp_encoder_telemetry.h`, weak-link it): frames, bytes, IDR count, last frame size, encode time last/avg/max (microseconds, source frame to stream back), delivered fps x100 and average kbit/s since CreateChn, and whether a picture is in the codec right now. The vendor library has only `IMP_Encoder_Query` (6 words). Device-tested on T41 (.34): matches the consumer's own count exactly, fps 15.02 at 15, 2073 kbit/s at CBR 2000.

### 1.1 Encoder ROI (region QP) and the valid-H.264 rules (agg-34)

| Feature | SoC | API / param / env | Default | How a streamer uses it | Detect / disable | Status |
|---|---|---|---|---|---|---|
| **T31 has an encoder ROI** (the vendor T31 library has none): macroblock QP table through the AVPU, up to 10 windows, relative (delta) or absolute QP, H.264 only | T31 | `IMP_Encoder_SetChnRoiAttr` / `GetChnRoiAttr` (the T40/T41 call and struct) | on | Call it like on T40/T41 after `CreateChn`; it takes effect on the next picture, no IDR needed; windows are rounded to macroblocks and cover every macroblock they touch | `SetChnRoiAttr` returns -1 for a delta outside -26..25 or an absolute QP outside 0..51; `OPENIMP_T31_ROI=0` refuses the call | Device-tested: relative -10 gave 2.7x the bit rate, a relative -20 became -19 (clamped), an absolute 51 blurs the region, 0 decoder errors in 20 VA-API checks, 8 live changes and a streamer restart with 0 oops; end to end with the streamer |
| **T21 `SetChnROI` is effective** (the vendor T21 library never programs ROIs) | T21 | `IMP_Encoder_SetChnROI` | on | Regions are applied to the Helix command list; a relative QP acts within the slice QP -12/+13 | `OPENIMP_T21_ROI=0` switches it off | Device-tested (absolute QP 51 blurs exactly the region, QP 15 stays clean) |
| T10/T20 `SetChnROI` and H.264 chroma QP offset on the Helix/NVPU | T10, T20 | `IMP_Encoder_SetChnROI`, `SetH264TransCfg` | on | As the vendor API | n/a | Device-tested on both |
| **Streams stay valid H.264 with every ROI** (new in agg-34): window deltas are limited to -25..+25, the spread of all deltas to 25, picture QP plus delta to 0..51 and to the rate control's min/max QP; absolute QPs are turned into deltas against the picture QP first | T10, T20, T21, T31 (T41 experimental) | same calls | on | Requests beyond that are clamped, one warning `ROI: requested QP delta ... clamped` is logged. **Do not offer -26 as the lowest relative QP:** -26 produces `mb_qp_delta` +26 on the way out of the window, which is outside H.264; software decoders hide it, Intel VA-API, VLC, Firefox and Edge show broken blocks from the ROI row on. -25 is the lowest valid value | The warning in the log; host test `tests/t31/roi_clamp_test.c` (20000 random window sets walked like the encoder) | Device-tested on T31 (VA-API, 20 checks); T10/T20/T21 measured before and after the clamp |
| **T23 `SetChnROI` is effective** (the vendor T23 library accepts regions but they had no visible effect): the native Helix encoder programs them like T21 (the vendor 1.3.0 encoder writes the same registers); relative QP acts within -12/+13 of the slice QP | T23 | `IMP_Encoder_SetChnROI` | on (`OPENIMP_T23_ROI=0` switches it off) | Use ROI like on T21; **probe `OpenIMP_Cap_T23HelixRoi`** (const symbol, weak extern or `dlsym`) before relying on it, older libimps drop the regions silently | QP map shows the window; bit rate +10 % at delta -15, -4 % at +20 / absolute 51; streams valid in VA-API and strict ffmpeg | device-tested on cam-B 2026-10-10, **in branch `claude/t23-roi`, not yet in an aggregate** |
| Runtime capability marker `OpenIMP_Cap_T23HelixRoi` (exported const int, new `OpenIMP_Cap_*` glob in the symbol map) | T23 | symbol lookup | on | `extern const int OpenIMP_Cap_T23HelixRoi __attribute__((weak));` and test the address; absent = old libimp or vendor libimp | n/a | **in branch `claude/t23-roi`, not yet in an aggregate** |
| T40 `SetChnRoiAttr` | T40 | `IMP_Encoder_SetChnRoiAttr` | **off** (`OPENIMP_T40_ROI=1`) | The T31 QP-table path with the T40 command bits found in the vendor library; with `OPENIMP_T40_ROI=1` device-tested on the T40XP (2026-10-11): FixQP 34, full-frame delta -10, bit rate 81 -> 155 kbps (x1.9). Default on is not decided yet | `SetChnRoiAttr` returns -1 while off | Experimental, not in the release |
| T41 `SetChnRoiAttr`: code only | T41 | `IMP_Encoder_SetChnRoiAttr` | **off** (`OPENIMP_T41_ROI=1`) | Not usable yet: the encoder did not react in earlier runs; the enable bits found in the vendor library are not device-tested | `SetChnRoiAttr` returns -1 while off | Experimental, not in the release |

Rules, tables and numbers: `docs/ROI.md`.

## 2. ISP tuning

| Feature | SoC | API | Default | How a streamer uses it | Detect / disable | Status / branch |
|---|---|---|---|---|---|---|
| **`GetAwbZone` returns 0 only with fresh statistics** (the vendor copies its static block regardless: zeros before the first frame, the last frame after a stop): the open driver answers `-EAGAIN` unless the 675-byte block comes from a statistics frame of the running stream within the last second; the block itself is the vendor one (15x15 truncated byte means of the AWB statistics, stock `Tiziano_awb_fpga`) | T23, T31 | `IMP_ISP_Tuning_GetAwbZone(IMPISPAWBZone *)` | on | Treat -1 as "no measurement now" and retry next second; a global R/G or B/G is the sum of `zone_r` (`zone_b`) over the sum of `zone_g` | **probe `OpenIMP_Cap_AwbZone`** (const int, weak extern or `dlsym`); absent = older OpenIMP (three-pointer signature, stale data as success) or vendor libimp | device-tested 2026-10-12 on cam-A and cam-B, **in branch `claude/awbzone`** (with the open-tx-isp branch of the same name) |
| **`GetAwbStatistics` returns 0 only with a block that holds statistics** (the vendor copies its AWB state regardless: zeros before the first frame, the last frame after a stop): on T41 the open driver answers `-EAGAIN` unless the 2700-byte block (three 15x15 u32 planes, zone means of the raw pre-WB R, G, B statistics, the vendor's own planes) comes from a frame of the running stream within the last second; on T40 (vendor kernel driver) libimp turns an all-zero block into -1 | T40, T41 | `IMP_ISP_Tuning_GetAwbStatistics(IMPVI_NUM, IMPISPAWBStatisInfo *)` | on | Treat -1 as "no measurement now" and retry next second; a global R/G or B/G is the sum of `awb_r` (`awb_b`) over the sum of `awb_g`; the ratios are pre-white-balance (a warm lamp raises R/G and lowers B/G) | **probe `OpenIMP_Cap_AwbStatistics`** (const int, weak extern or `dlsym`); absent = older OpenIMP or vendor libimp | device-tested 2026-10-12 on the Eufy T40 (same numbers as the vendor libimp) and the T41, **in branch `claude/awbstats`** (with the open-tx-isp branch of the same name) |
| **T40 `IMP_ISP_GetRaw` is safer than the vendor:** the vendor keeps the first rmem buffer even when a later call asks for more (the kernel would write past it); OpenIMP allocates a larger one. The CPU cache of the buffer is invalidated after the capture, so `buf.vaddr` shows the new raw frame | T40 | `IMP_ISP_GetRaw` | on | Read the raw picture from `attr.buf.vaddr` right after the call | n/a | host test, `claude/t40-night` (decision pending: vendor-exact would drop both) |
| **T40 digital gain and volume share a register:** `IMP_AI/AO_SetDigitalGain` program the codec volume register (vendor); OpenIMP's `IMP_AI_SetVol`/`IMP_AO_SetVol` program the same register (the vendor library scales the samples in software instead), so the last of the two calls wins | T40 | `IMP_AI/AO_SetDigitalGain`, `IMP_AI/AO_SetVol` | – | Use one of the two, not both | n/a | host test, `claude/t40-night` |
| Scene mode and colour effects act on hardware (B/W, vivid, negative; sepia also visible on T20); getters return what was set. The vendor kernel ignores them | T21 (colorfx also T20) | `IMP_ISP_Tuning_SetSceneMode`, `SetColorfxMode`, `Get*` | defaults vendor-identical | Expose them as image options on T21. On T23/T31 the same calls work (device-tested 2026-10-03 on cam-A/cam-B: colorfx 0/1/3/9 ok, unsupported values such as 2 and scene 15 return EINVAL); there it is vendor parity, not an extra, and not yet in an aggregate (`claude/t23-t31-scene-colorfx` + openimp `claude/scene-colorfx-imp`) | set then get; picture changes visibly | cam-D, `claude/t21-tuning-controls` [-all-13] |
| Sinter / Temper denoise **strength** takes effect (vendor ignores it) | T21 | ISP Sinter/Temper tuning controls, timps `sinter_strength` (128 = vendor picture) | vendor picture | Offer a denoise slider on T21 | compare images at 0/128/255 | cam-D, `claude/t21-tuning-controls`, `claude/t21-sinter-strength` [-all-13] |
| Sinter / Temper strength acts (the vendor firmware renormalises it away; only 0 acts there); 128 = IQ default; **built, T20 device-tested, T10 device test pending; decided: default on** (128 = IQ table, so the default picture is identical to the vendor; other values act, which goes beyond the vendor) | T10, T20 | ISP Sinter/Temper tuning controls, timps `sinter_strength` | on (decided 2026-10-03) | Denoise slider can be offered | compare images at 0/128/255 | cam-C, `claude/t10-t20-nr-wdr`, OpenIMP `claude/t20-nr-strength` |
| T23 front crop rejects windows outside the sensor, below 64x64 or odd sizes (-EINVAL); stock accepts any window | T23 | `IMP_ISP_Tuning_SetFrontCrop` / FRONT_CROP control | on | Check return codes; pass even sizes of at least 64x64 inside the sensor | n/a | cam-B, `claude/t23-matrix-gaps` |
| **T20/T10 `isp-m0` reports the reachable analog gain cap** (agg-34): the AE stops at the smaller of the sensor maximum and the `SetMaxAgain` ceiling (128 on the tested sensors), but the compact dump printed only the sensor maximum (158), so a day/night switch waiting for the cap never saw it reached | T20, T10 | `/proc/jz/isp/isp-m0` ("MAX SENSOR analog gain") | on | A streamer that reads the cap from `isp-m0` now gets a reachable value; a switch that waits for the cap works | n/a | Host-tested; device check with the streamer pending |
| **Front crop guards (agg-34)**, beyond stock: T31 refuses (-EINVAL, one log line) a window smaller than the output of any running channel, because the MSCA cannot upscale and such a window stalls every output until reboot; a main stream larger than the window drops the crop with a warning; scaler steps and latch are written so a valid window really zooms (picture effect on T31 **experimental, not proven**). T23: crop off unlocks the window and restores the full sensor window, a locked window that does not fit is dropped instead of hanging the MSCA, the lock is released at the last close of the ISP device (a streamer restart without crop gets the full frame). T20/T10: a window is kept while no downscaled channel is open and applied at stream on, released with the tuning session | T31, T23, T20, T10 | `IMP_ISP_Tuning_SetFrontCrop`; open-tx-isp module parameter `fcrop_upscale_pct` (T31, T23) | on; `fcrop_upscale_pct=0` | Check the return code of the crop call (-EINVAL means refused); on T31 and T23 keep every stream at or below the window size, or accept that the crop is dropped; the streamer must not rely on the driver keeping a crop across its own restart | `/sys/module/<module>/parameters/fcrop_upscale_pct` (writable at run time): a percentage by which the geometry check tolerates an upscale; T23 was measured to upscale up to 2.0 (a 2.2 stall dropped the crop, no reboot), the T31 limit was not established, leave at 0 | T23, T20, T10 device-tested 2026-10-09 (T23: 18 cases incl. flip, day/night, restarts, corners, sub-stream, reboot with a persisted crop); T31: crash fixed and refusal device-tested, zoom effect experimental |
| **Front crop is applied in sensor space, before the flip** (device-tested with the streamer on all SoCs): a streamer that mirrors the picture must mirror the crop window too. T23, T21, T31: mirror both axes. T20, T10: vertical only (the horizontal flip is the ISP top mirror before the crop, the vertical flip is in DMA after it). T23: with image hflip/vflip both the sensor (reg 0x3221) and the MSCA (0xd050) flip, `shvflip=1` only flips the LSC mesh. The vendor does no transform (T23/T31 stock) and T10/T20/T21 have no vendor `SetFrontCrop` | T10, T20, T21, T23, T31 | `IMP_ISP_Tuning_SetFrontCrop` + hflip/vflip | n/a | Convert the window from picture to sensor coordinates before calling `SetFrontCrop`, then verify with a labelled picture | n/a | agg-34, device-tested; streamer-side |
| **Runtime detection of the safe front-crop path**: the module parameter exists only in drivers with the crop guards, so a streamer can enable crop only where it is safe | T23, T31 | `/sys/module/tx_isp_t23/parameters/fcrop_upscale_pct` or `/sys/module/tx_isp_t31/parameters/fcrop_upscale_pct` | n/a | Test for the file before offering front crop; absent means an older driver (a bad window may hang the MSCA) | file absent | agg-34 drivers, device-tested (T31 crop effect proven on both streams in daylight) |
| **AE compensation timing on T31**: AeComp works (OEM formula, luma 55 / 100 / 160 for 0 / 128 / 255), but the AE has about 6 s dead time and needs 25 to 30 s to settle. T20: monotonic, luma 56 / 105 / 245 | T31, T20 | `IMP_ISP_Tuning_SetAeComp` | n/a | Do not judge the result or re-adjust earlier than about 30 s after a change on T31 | n/a | device-tested |
| Day/night: brightness/contrast/saturation/sharpness re-sent on switch; table Sinter/Temper re-sent | T20, T21 | day/night running mode switch | on (as vendor) | Nothing; values you set persist across switches | n/a | cam-C, cam-D, `claude/tseries-daynight` |
| Day/night: a block whose parameters fail to load is bypassed instead of running with the other bank's values; **user bypass bits survive day/night switches** | T23 | `IMP_ISP_Tuning_SetModuleControl` | on | A module you bypassed stays bypassed after the switch | n/a | cam-B, `claude/t23-pkg2` |
| Max analog gain, max digital gain (additional ISP-dgain AE stage), IT max, SetSensorFPS reach the AE | T20, T21, T23 | `SetMaxAgain`, `SetMaxDgain`, AE IT max, `SetSensorFPS` | on | Use as vendor; on T23 the extra dgain stage is an addition | `t23tune` tool on T23 | cam-B (`t23tune` passed), cam-C, cam-D |
| AWB state kept across stream restarts (snapshots no longer green) | T23 | internal | on | On-demand snapshots restart the stream; no workaround needed any more | n/a | cam-B, `claude/t23-day-color` [-all-13] |
| AWB hysteresis band 10 % and night freeze (day gains restored on night to day) | T21 | kernel module parameters (both 0 = vendor behaviour) | 10 % | Nothing; avoids parameter-set flapping at dusk | set both params to 0 for vendor | cam-D night checks OK, `claude/t21-awb-hyst` [-all-13] |
| T31 privacy mask: 4 rectangles per channel, YUV fill, follows mirror/flip; RGB colours converted to YUV like vendor | T31 | `IMP_OSD`/ISP mask API (vendor signatures) | n/a | Use as vendor | get/clear OK | cam-A, `claude/t31-privacy-mask`, `claude/t31-mask-rgb2yuv` [-all-13] |
| Unknown tuning control IDs are **rejected** (vendor-style stubs returned success) | T23 | all `IMP_ISP_Tuning_*` | on | Check return codes: an error now means the control really does not exist | n/a | cam-B, `claude/t23-tuning-wiring` |
| **Brightness acts on T21** (the vendor T21 kernel only stores the value and the OEM AE never reads it, so `SetBrightness` is a no-op there): the value works as an exposure compensation, it scales the AE luma target by value/128 (target clamped to 1..255, values below 16 treated as 16); colours are not clipped because the ISP colour path is untouched, only the exposure the AE settles on moves. 128 is the vendor picture (code path bit-identical) | T21 | `IMP_ISP_Tuning_SetBrightness`, timps `image.brightness` | on (128 = vendor picture) | Offer a brightness slider on T21; allow a few seconds for the AE to converge after a change (2 to 7 s) | `IMP_ISP_Tuning_GetBrightness` reads back the value; compare images at 30/128/225 (imgfx mean Y 28.8 / 117.7 / 193.0, timps snapshots 44.5 / 130.8 / 223.9) | cam-D, open-tx-isp `claude/release-t21-brightness`, in the release candidate (2026-10-06) |
| **Sepia works on T21** (`SetColorfxMode(SEPIA)` returned -1 before; the vendor kernel ignores colour effects): the CCM saturation list goes to 0 like B/W and the CSC U/V row sums get a tint (U below, V above neutral for a grey input, luma unchanged); a CSC attribute set during sepia keeps the tint, leaving sepia restores the stored matrix | T21 | `IMP_ISP_Tuning_SetColorfxMode(IMPISP_COLORFX_SEPIA)`, `GetColorfxMode` | off until set | Offer sepia next to B/W/negative on T21 | imgfx: dU/dV +10/+9 and +17/+19 against the plain picture in two runs; the getter returns the mode | cam-D, open-tx-isp `claude/release-t21-image`, in the release candidate (2026-10-06) |
| **AE ceiling, AE luma, backlight compensation and hue on T10/T20; AE ceiling and backlight compensation on T21** (the vendor T10/T20 libimp has none of the four calls, the vendor T21 kernel ignores the ceiling and has no backlight control). `SetAe_IT_MAX(lines)` / `GetAE_IT_MAX` set and read the AE exposure ceiling in sensor lines (0 = sensor maximum; T10/T20: the compact AE ceiling stab.global_max_integration_time, T21: tisp_ae_ctrls max_integration_time, the same field SetIntegrationTime MODE_RANGE sets). `GetAeLuma` (T10/T20) returns the mean the compact AE meters, 0..255 (T21 already had it). `SetBacklightComp(0..10)` / `Get`: 0 = off; T10/T20 make the brightest histogram quarter count less in the AE mean (weight 1.0 down to 0.25 at level 10), T21 lifts the AE luma target by 5 % per level (+50 % at 10). `SetBcshHue(0..255)` / `Get` (T10/T20 only): 128 = unchanged, rotates the U/V rows of the RGB2YUV table by up to +-30 degrees; goes back to the stock table at 128. T21 hue is not possible: its CSC block takes magnitudes only (fixed signs), a rotation needs the colour matrix | T10, T20 (all four), T21 (ceiling, backlight) | `IMP_ISP_Tuning_SetAe_IT_MAX`, `GetAE_IT_MAX`, `GetAeLuma`, `SetBacklightComp`, `GetBacklightComp`, `SetBcshHue`, `GetBcshHue`; control ids 0x8000032 / 0x8000037 / 0x8000101, T10/T20 GetAeLuma 0x80001a0 (0x8000033 is the AeMin id there) | off (ceiling = sensor maximum, level 0, hue 128 = stock picture) | Offer a backlight slider and a hue slider on T10/T20, ceiling and backlight on T21; read the luma to show what the AE sees; allow a few seconds for the AE to converge | Set then get; GetAeLuma moves with the scene; backlight: a backlit scene gets brighter at the subject (imgfx `blc` case); hue: imgfx `hue` case, colours rotate and 128 restores the picture | **built and linked, device test pending**: needs the new open-tx-isp modules (`claude/streamer-real-funcs` 56229bf4), which cannot be loaded without a flash; host test `t21_ae_comp_host_test`, module builds T10/T20/T21 clean |
| **Autofocus statistics and focus getters on T23 are on by default** (as the stock module, which always runs the AF init): `GetAfHist`, `GetAFMetrices`, `GetAfWeight`, `GetAfZone` return live values; no measurable CPU or interrupt cost (47.8 % vs 48.1 % CPU busy, 88.7 IRQ/s in both cases) | T23 | `IMP_ISP_Tuning_GetAfHist/GetAFMetrices/Get/SetAfWeight/GetAfZone`; module parameter `source_af` (0 = off) | on (`source_af=1`) | Use the AF getters as on T31; the zone grid is 8x8 on the tested sensor | apitest PASS with live values | a T23 camera, open-tx-isp `claude/release-t23-af-scratch`, in the release candidate (2026-10-06); same as the stock module, listed because earlier open builds had it off |
| T23 ISP controls `SetOSDBlock`/`GetOSDBlock` (0x8000182), `SetDrawBlock`/`GetDrawBlock` (0x8000180) refuse a block index outside the 8 OSD / 6 draw entries the hardware update walks (-EINVAL; stock indexes its table unchecked), the Get calls return the block you ask for (stock Get has no input and reports block 0 / stack garbage), and `SwitchBin` takes only an IQ file of the standard size and leaves the banks untouched when a file is rejected. **Host build only, device test pending; the stored OSD and draw blocks have no image effect yet. Not in agg-36: the driver branch is held back until its SwitchBin bank swap is reworked** | T23 | `IMP_ISP_Tuning_SetOSDBlock`, `GetOSDBlock`, `SetDrawBlock`, `GetDrawBlock`, `SwitchBin` | on | Check return codes; do not rely on OSD/draw blocks to change the picture yet | `isp-m0` / kernel log line "SwitchBin ... rejected" | open-tx-isp `claude/gaps-t23-t31-drv` |
| T41 ISP calls of the 1.2.6 library fixed against the vendor kernel: `GetTmoCurve` returns the curve of the id the caller sets (the vendor kernel reads the id from uninitialised stack), `GetAeEvList`/`GetAeAtList` report enable 1 (vendor: stack garbage), `WaitFrameDone` sleeps without blocking other tuning calls; `SetTmoCurve` values above 8191 and ids above 9, a zero `SetAeConvergeStep` and an AWB `SetStatisConfig` that changes the zone grid while streaming are refused (-1) | T41 | `IMP_ISP_Tuning_Get/SetTmoCurve`, `GetAeEvList`, `GetAeAtList`, `WaitFrameDone`, `SetAeConvergeStep`, `SetStatisConfig` | on | Set the curve id before `GetTmoCurve`; check return codes. Note: `SetAeConvergeStep` is stored only (the open AE keeps its own step), `SetStatisConfig` applies only the AWB zone grid | return codes | host tests only, `claude/t41-isp-handlers` |
| **Deviation: T41 `WaitFrameDone` reports a timeout** (beyond vendor). The vendor kernel returns 0 when no frame arrived within the timeout, so the caller can only notice by comparing `cnt`. The open driver returns -ETIMEDOUT (a signal: -ERESTARTSYS), and OpenIMP returns -1 with `errno` ETIMEDOUT and leaves the caller's structure unchanged. The wait now also runs outside the library lock: before, a waiting `WaitFrameDone` made every other tuning call of the process spin for up to the timeout (measured: `GetBrightness` up to 1.8 s), and each timeout wrote a syslog line. Frames are counted only while frame source channel 0 runs; timps stops it when no client is connected, so every wait then times out | T41 | `IMP_ISP_Tuning_WaitFrameDone` | on | Treat -1/ETIMEDOUT as "no frame within the timeout" and call again; other errors are real errors. No current streamer (timps, prudynt, raptor) calls it | return value -1, `errno` ETIMEDOUT; no syslog line for timeouts | cam-F: with channel 0 running 9/10 waits return 0 at 40 ms (25 fps), without it 10/10 return -1/ETIMEDOUT after 200 ms; a parallel `GetBrightness` takes < 0.1 ms (was up to 1780 ms); open-tx-isp `claude/t41-wait-frame-done`, openimp `claude/t41-wfd-timeout` |
| **T41 day/night works without a stream consumer and returns to day by itself** (beyond vendor, 2026-10-24): the night/day parameter bank is stepped from the per-frame AE statistics IRQ (after the first AE exposure), not from the frame-completion path, so a camera with no client still switches; in the A/B on .32 the open stack returned to day after 29.8 s, the vendor stack stayed in night for 120 s and had to be forced | T41 | `IMP_ISP_Tuning_SetISPRunningMode`, timps `image.running_mode` | on | Nothing; do not force `running_mode=0` in a bright room (the AWB stays at 5000 K, the vendor behaves the same) | `t41_dn_bank` (bit mask, 0 = no bank copy) | .32, .34, open-tx-isp next 2c29ce79 (`6119ab13`, `2df2f6f4`) |
| **T41 defog default is OFF** (behaviour change 2026-10-24): the OEM defog chain runs when enabled, but its picture showed diamond artefacts, so `t41_defog` now defaults to 0; with it on, `Module_Ratio` index 4 (0..255) acts (block contrast +40 % at 255, small below 128, device-tested on .32); `SetModule_Ratio` may return an error word although the value was applied. A `IMP_ISP_Tuning_SetDefog_Strength` wrapper exists on a local branch only (the vendor T40/T41 library has no such call) | T41 | `IMP_ISP_Tuning_SetModule_Ratio` (index 4) | off | Offer defog only after a visual check on the target scene | kernel parameter `t41_defog` (0444, needs a reload) | .32, `af9b3519`; strength API `claude/t4x-defog-strength` not merged |
| T41 TMO map computed with MXU3 code (`t41_tmo_map_impl=2`), bit-exact with the C map; lowers the kernel load of the per-frame tone worker | T41 | none (kernel) | on | Nothing | `t41_tmo_map_impl=0` selects the C map | .32, .34, open-tx-isp next (`92996ea6`, `2c30af59`) |

## 3. JPEG / snapshot

| Feature | SoC | API / env | Default | How a streamer uses it | Detect / disable | Status / branch |
|---|---|---|---|---|---|---|
| Adaptive quality: on bitstream truncation quality -5 (as vendor), **+5 back after 100 clean frames** | T20, T21, T23 (Helix) | internal | on | Configured quality is only the starting point; do not treat it as a fixed value | n/a | cam-C/D/B |
| Last JPEG re-delivered when the encoder is busy or video memory is short (no blocking) | Helix SoCs, T31 | `IMP_Encoder_GetStream` on JPEG channel | on | A snapshot may repeat the previous frame under load; compare timestamps/sequence if freshness matters | n/a | `claude/openimp-quickfixes` line, early-morning changelog |
| Configured JPEG quality is applied (before: fixed 75) | all | `IMP_Encoder_SetJpegeQl` / attr | on | Use it | n/a | `claude/openimp-quickfixes` |
| Hardware JPEG on T31 (about 70 % to 17 % timps CPU at 1 snapshot/s) | T31 | `OPENIMP_T31_HW_JPEG=0` selects software | on | Nothing | log: "T31 hardware JPEG enabled" / "off" | cam-A, `claude/t31-hwjpeg-default` |
| JPEG shares the H.264 bitstream area (-1.44 MB); JPEG bitstream buffer 1 MiB (-328 KiB) | T23 / T20, T10 | internal | on | Less video memory used | n/a | cam-B / cam-E, cam-C |
| **A JPEG channel is fed from the frame source when its video channel is not polled** (libimp's own encoder thread feeds every channel bound to a group; OpenIMP encodes inside `PollingStream`, so the JPEG channel used to receive frames only while the application also polled the video channel). A receiving video channel that has not been polled for 300 ms no longer counts as the source; the JPEG wait rechecks every 300 ms and reads the frame source itself. Streamers that poll the video channel continuously keep the fan-out as before | T20, T21, T23 (Helix), T31 | `IMP_Encoder_PollingStream` on a JPEG channel | on | Poll only the JPEG channel for snapshot-only use; no H.264 reader needed | apitest: JPEG polling, marker check, live `SetJpegeQl` and `FlushStream` PASS without an H.264 reader on T20/T21/T23 | a T20 and a T21 camera, `claude/release-fs-enc-ivs`, release candidate (2026-10-06) |
| **`IMP_Encoder_GetFd` returns a pollable pipe** (difference rather than an addition: the vendor returns the channel's encoder fd, readable when a stream is ready). OpenIMP encodes inside `PollingStream`, so the first `GetFd` starts a pump thread that polls the channel in 100 ms slices and makes a pipe readable while a stream waits; `ReleaseStream`/`FlushStream` clear it. Use it only for `select`/`poll`; it is not a device fd (no ioctl) | T21, T23, T31 (T10/T20 have no `GetFd` in the vendor API) | `IMP_Encoder_GetFd` | on, started by the first call | Wait for streams on several channels with one `poll` | apitest: `poll(POLLIN)` returns within 1 s with a stream waiting | cam-J, `claude/release-fs-enc-ivs`, release candidate (2026-10-06) |

## 4. IVS / motion

| Feature | SoC | API | Default | How a streamer uses it | Detect / disable | Status |
|---|---|---|---|---|---|---|
| IVS channel already in use returns **EBUSY** (user decision) | all | IVS channel create/register | on | Handle EBUSY: another consumer owns the channel; do not retry blindly | return code | per matrix |
| Real frame-diff motion detection (vendor T20/T21/T30 reported "always no motion") | T20, T21, T30 | `IMP_IVS_*` move interface | on | Use the standard move IVS | 6/6 events, 0 false alarms (cam-C) | `claude/tseries-ivs` |
| Motion keeps working after the sub-stream goes idle; frames recycled for callback-backed pools | T23 | internal | on | Nothing | web grid not empty | cam-B |
| Sub-stream default for IVS: about 85 % less IVS CPU than the vendor libimp | T31 | internal | on | Prefer the sub-stream as IVS source | n/a | cam-A |
| Motion works without a viewer (feeder thread), CPU about 10-12 % vs 27 % with vendor libimp | T41 | internal | on | Nothing | n/a | cam-F, `claude/t41-libimp` |
| Fewer dropped IVS frames: the channel is marked free **before** the result is announced, so a frame fed right after `IMP_IVS_PollingResult`/`GetResult`/`ReleaseResult` is accepted (the vendor posts the result first and drops such a frame as busy) | T23, T30, T31, T40, T41 | `IMP_IVS_*` (any interface) | on | Nothing; applications that feed frames themselves keep every frame. Result slots stay valid until `ReleaseResult` (ring of 6, unchanged). For the vendor order set `OPENIMP_IVS_VENDOR_ORDER=1` | host test: 500 back-to-back frames under CPU load, 0 dropped (not device-tested) | `claude/ivs-order` |

### 4.1 Motion v2 (on by default, beyond vendor)

**On by default since 2026-10-04 (in `next`); `OPENIMP_MOTION_V2=0` restores the vendor algorithm, and then
`IMP_IVS_MoveOutput` is bit-identical to the vendor** (host test `tests/t23/ivs_move_v2_test.c`, also in shadow
mode). Status: built for T20/T21/T23/T30/T31/T41; **device-tested on cam-C (T20) and cam-B (T23) for one night in shadow
and override mode** (results below).

Device results 2026-10-04 (timps, sub stream 640x360, 5x5 grid, sensitivity 128, skip 5; legacy and v2 counted on the same
frames in shadow mode; events merged within 5 s, classified from recordings):

| Period | cam-C T20 vendor / v2 | cam-B T23 vendor / v2 |
|---|---|---|
| 00:38-04:27 shadow | 13 (about 7 false: blinking lights, room light) / 11 (about 2 false) | 3 (2 false) / 2 (1 car, its headlights) |
| 04:27-06:41 OVERRIDE (timps sees v2) | 0 / 0 | 5 / 1 (headlights) |
| 06:41-07:20 day/night on, IR probe + switch | not tested (indoor) | 5 / 0 (suppressed: DAYNIGHT, GAIN, LUMA) |

Known limit: car headlights sweeping the scene can still form a moving object. CPU (in timps, per 25 fps input frame,
`OPENIMP_T31_IVS_STATS`): v2 adds about 225 us on T20 and 175 us on T23 (+0.6 % / +0.4 % of a core); bench per
analysed 640x360 frame: v2 1.8 ms (T20), 1.2 ms (T23), vendor move 1.6 / 1.0 ms.

What it does, next to the vendor frame difference of `IMP_IVS_CreateMoveInterface`:

| Feature (bit) | Behaviour |
|---|---|
| `BACKGROUND` (0x1) | Per-cell background model (8x8-pixel cells at 640x360, grid at most 80x60): running mean, texture and noise level per cell, global brightness compensation (median cell ratio). A brightness change that keeps the cell's texture is light (shadow, lamp, reflection), not an object; cells that keep moving (flicker, foliage) need 3x the threshold. Without it the reference is the previous analysed frame |
| `SUPPRESS` (0x2) | Hold-off of `suppress_ms` (default 4 s, at least 3 analysed frames) after an ISP running-mode change (IR / day-night), a total-gain jump or a global brightness jump above `jump_pct` (default 25 %); the background re-learns fast meanwhile |
| `BLOBS` (0x4) | Moving cells are grouped 8-connected into objects; an object needs the vendor-equivalent area (below) and at least `min_cells` (3) cells, must be seen `min_frames` (2) analysed frames in a row and its centre must travel `min_move` (3) cells; leaves, noise, single-cell flicker and lamps switching on in place drop out |
| `OVERRIDE` (0x8) | `retRoi[i]` comes from v2 (an object covers ROI i) instead of the vendor difference, so an unmodified streamer benefits. Without it v2 runs in **shadow mode**: vendor `retRoi`, v2 only in the extension result |

The vendor `sense` of the most sensitive ROI scales the v2 thresholds (2 = as configured, 0 = x2, 4 = x0.5) and sets the
minimum object area to the vendor ROI threshold (4 x T[sense] pixels, sense 2 = 604 px = 9 cells at 640x360), so an
existing sensitivity slider keeps working and means the same object size. Analysis runs on the frames the channel already receives, every `skipFrameCnt + 1`-th
frame.

**Switches**

| Variable | Meaning |
|---|---|
| `OPENIMP_MOTION_V2` | unset (default since 2026-10-04) or `1`/`on`/`all` = all four features; `0`/`off`/`vendor` = vendor algorithm; `shadow` = all but OVERRIDE; a number = feature mask |
| `OPENIMP_MOTION_V2_BG`, `_SUPPRESS`, `_BLOBS`, `_OVERRIDE` | `0`/`1` clears/sets one feature on top of the above |
| `OPENIMP_MOTION_V2_LEARN`, `_K`, `_MIN_DELTA`, `_SUPPRESS_MS`, `_JUMP_PCT`, `_MIN_CELLS`, `_MIN_FRAMES`, `_MIN_MOVE` | parameters, see the config struct |
| `OPENIMP_TEST_PATTERN` (`_STEP`, `_NOISE`, `_EVENTS`, `OPENIMP_TP_LOGO`) | start value of the test pattern (0..3, section 6.1); `OPENIMP_TEST_PATTERN_CTL=<path>` opens the control socket for `tpctl` | off | debug-only |
| `OPENIMP_MOTION_V2_LOG` | `1` = one stderr line per change of the vendor or the v2 decision (debug, A/B) |

The environment is read when the move interface is created. A streamer can instead (or later) set the configuration per
channel with `OpenIMP_IVS_MoveSetConfigEx()`.

**API** (header `include/imp/openimp_ivs_move_ex.h`, exported as `OpenIMP_IVS_Move*`)

```c
int OpenIMP_IVS_MoveGetResultEx(int chn, OpenIMP_IVS_MoveOutputEx *out); /* after IMP_IVS_GetResult */
int OpenIMP_IVS_MoveSetConfigEx(int chn, const OpenIMP_IVS_MoveConfigEx *cfg);
int OpenIMP_IVS_MoveGetConfigEx(int chn, OpenIMP_IVS_MoveConfigEx *cfg);
```

- `OpenIMP_IVS_MoveOutputEx` (version 1, 336 bytes): `size`, `version`, `timestamp` (frame), `seq`, `flags`
  (`ACTIVE`, `SUPPRESSED`, `OVERRIDE`, `WARMUP`), `suppress` (reasons `DAYNIGHT`, `GAIN`, `LUMA`), `frame_w`/`frame_h`
  (the IVS input = the bound stream, e.g. 640x360), `grid_w`/`grid_h`, `legacy_roi[2]` (vendor `retRoi` bits),
  `v2_roi[2]`, `obj_cnt` and up to 16 `OpenIMP_IVS_MoveObject {x0, y0, x1, y1 (inclusive, frame pixels), strength 0..1000,
  cells, age (frames), id}`. Scale the box by `stream_w / frame_w` for another stream.
- It returns the extension data of the result the last `IMP_IVS_GetResult()` handed out; call it before the next
  `GetResult`. With v2 off it still fills `seq`, `frame_*` and `legacy_roi`, `flags` is 0.
- `OpenIMP_IVS_MoveConfigEx` (version 1, 64 bytes): `features`, `learn_shift` (1..10, 4), `thresh_k` (16..255, 64 =
  4x noise), `min_delta` (luma levels, 10), `suppress_ms` (4000), `jump_pct` (25), `min_cells` (3), `min_frames` (2),
  `min_move` (cells, 3; negative = report objects that do not move).
  Parameter fields that are 0 or beyond the caller's `size` take the default; out-of-range values are clamped.
  `features = 0` turns v2 off.
  The configuration takes effect with the next analysed frame; switching features restarts a 4-frame warm-up.

**ABI rules:** every struct starts with `size` and `version`; set `size = sizeof(struct)` and `version =
OPENIMP_IVS_MOVE_EX_VERSION`. The library reads and writes at most `min(size, own size)` bytes and reports its version.
Fields are only appended, never moved. Errors: -1 with `errno` `EINVAL` (channel, NULL, size too small) or `ENOENT` (no
move interface on the channel).

**Use from a streamer (weak symbols, works against vendor libimp too):**

```c
#include "openimp_ivs_move_ex.h"   /* copy the header; it has no other dependencies */
#pragma weak OpenIMP_IVS_MoveGetResultEx
#pragma weak OpenIMP_IVS_MoveSetConfigEx

/* after IMP_IVS_CreateChn/StartRecvPic, optional: switch v2 on for this channel */
if (OpenIMP_IVS_MoveSetConfigEx) {
    OpenIMP_IVS_MoveConfigEx c = { .size = sizeof c, .version = OPENIMP_IVS_MOVE_EX_VERSION,
                                   .features = OPENIMP_MOVE_F_ALL };   /* parameters 0 = defaults */
    OpenIMP_IVS_MoveSetConfigEx(chn, &c);
}
/* in the result loop */
IMP_IVS_GetResult(chn, (void **)&res);           /* vendor yes/no per ROI as before */
if (OpenIMP_IVS_MoveGetResultEx) {
    OpenIMP_IVS_MoveOutputEx ex = { .size = sizeof ex };
    if (OpenIMP_IVS_MoveGetResultEx(chn, &ex) == 0 && (ex.flags & OPENIMP_MOVE_EX_ACTIVE))
        for (unsigned i = 0; i < ex.obj_cnt; i++)
            publish_box(ex.obj[i].x0, ex.obj[i].y0, ex.obj[i].x1, ex.obj[i].y1, ex.obj[i].strength, ex.obj[i].id);
}
IMP_IVS_ReleaseResult(chn, res);
```

Detect / disable: the symbols are absent on vendor libimp and older OpenIMP; `GetConfigEx` shows the features in
force; `OPENIMP_MOTION_V2=0` (or `features = 0` via `SetConfigEx`) is the vendor behaviour; since 2026-10-04 v2 is on by default.

## 5. Reference buffer sharing

| Feature | SoC | Env | Default | How a streamer uses it | Detect / disable | Status |
|---|---|---|---|---|---|---|
| Reference-frame ring (BUF_SHARE_CFG): saves about 1.5 MB video memory at 1080p, P frames equal or smaller, no artefacts. Vendor T23 does the same; vendor T21 has it off | T21, T23 only (Helix hardware; T10/T20/T31 have none) | `OPENIMP_REF_SHARE=0` disables | **on** for channels up to 1920x1088 and when it actually saves memory | Nothing; plan memory budget with the saving | log "reference sharing skipped" for larger/small pictures; `=0` to disable | cam-D, cam-B, `claude/t23-ref-ring` [-all-13] |

## 6. Robustness (open-tx-isp and OpenIMP)

| Feature | SoC | Behaviour vs vendor | How a streamer benefits | Status |
|---|---|---|---|---|
| rmmod while streaming is refused (no oops) | T20 | vendor can oops | Safe to supervise/restart the stack | cam-C, `claude/t20-robust` |
| 10x stop/start incl. kill -9 and 10x module reload, 0 oops | T20, T21, T23, T31 | vendor oopses on reload (T21), leaks (T31 252 KB/cycle) | Crash-restart loops are safe | `claude/t20-robust`, `t21-robust`, `t23-robust`, `t31-robust` |
| Frame-channel DQBUF honours `O_NONBLOCK` | T31 | vendor blocks | Poll-style frame readers work | `claude/t31-tuning-gaps` |
| Kernel patch 0100: invalid rmem flush direction rejected | T31 | vendor oopses | Bad flush args return an error | cam-A, 0 oops [-all-13] |
| soc_vpu: busy VPU sleeps (no 200 ms busy-wait); encoder error IRQ ends the wait immediately | T20, T10, T23 | vendor busy-waits up to 200 ms | Lower CPU, faster error recovery | patch 0099, flashed |
| Double release of AENC/ADEC channels rejected | all | vendor can crash | Idempotent teardown is safe | per matrix |
| WDR statistics ring is persistent across STREAMOFF; its DMA engine (0x2024) is disabled before the free and the IRQ window is closed (the ring was freed with the engine still running) | T31 | vendor frees it at every stream-off (not verified to corrupt in our tests) | Stop/start loops are safe; 100 cycles + 20 reloads without anomaly (regression fix, original corruption not reproducible) | open-tx-isp `agg-34.1` (56981169), cam-A |
| All user copies checked, out-of-bounds writes fixed | T20, T23 | vendor has unchecked copies | n/a | `claude/t20-robust`, `claude/t23-robust` |
| Frame source / VBM pool parked and reused at idle | T21 | vendor frees pool, later allocations fail in 23 MB rmem | Stream start/stop cycles do not exhaust video memory | `claude/t21-bringup` |
| One startup warning when pools plus fixed buffers exceed video memory | all | silent failure later | Read the warning to size your stream set | early-morning changelog |
| Real AEC (WebRTC AECM): `EnableAec` returns an error when it cannot run (vendor: fake success / flag only) | T31 (loopback: echo -18 dB, ERLE 44 dB) | n/a | Check the return of `IMP_AI_EnableAec` (EnableAec) | `claude/aec`; `OPENIMP_AEC_STATS=1` diagnostics |
| Software rotation `SetChnRotate` 90/270 (vendor-equivalent tiles) | T31 | earlier -1 | Rotation usable before OSD/IVS/encoder | `claude/t31-rotate` |
| T31 OSD drawn by the IPU, default on | T31 | `OPENIMP_T31_OSD=0` disables | OSD also in HW-JPEG snapshots; timps about 8 % CPU | cam-A |
| T20/T21 OSD via IPU hook | T20, T21 | vendor path never drew | OSD regions work | `claude/t20-osd` |
| Frame source / VBM pool of a disabled channel parked for the next pool (as on T21) instead of being freed: the open T23 driver keeps a stopped MSCA output enabled while the input runs, so a freed pool could be handed to another allocation while the hardware still writes into it (`OPENIMP_VBM_PARK=0` frees at once) | T23 | internal | on | Stream start/stop cycles are safe; no workaround | 40 streamer restarts with every snapshot OK, 0 oopses | a T23 camera, `claude/release-t23-vbm`, release candidate (2026-10-06) |
| **T23 MSCA scratch buffer** (beyond stock): a channel stopped with its output kept enabled and the input running made the MSCA write every further frame into the stream's last buffer, which user space frees after the close. STREAMOFF now queues a scratch address (the tail of the ISP buffer in rmem, one Y plane at sensor size, 900 KiB at 720p) behind the stream's buffers, QBUFs of a parked channel are held back until STREAMON, and the input waits until each parked scratch address has been consumed before it stops. The stock module has no such buffer | T23 | module parameter `msca_scratch` (0 = off); counters `msca_scratch_parks/_frames/_deferred/_skips/_settle_timeouts` | on (`msca_scratch=1`) | Nothing to do; if a streamer hangs on a stop/start of one channel, set `msca_scratch=0` as a first test | 20 cold starts and 20 streamer restarts OK, day/night switch OK, 80 parks / 81 scratch completions, 0 timeouts, longest settle 30 ms; 0 oopses | a T23 camera, open-tx-isp `claude/release-t23-af-scratch`, release candidate (2026-10-06); docs `driver/t23/docs/MSCA_SCRATCH.md` in that branch |

### 6.1 Test pattern with runtime control (extension, all SoCs)

Complete description (modes 0-4, API, tpctl, host tools, per-SoC status table, limits): [TESTPATTERN.md](TESTPATTERN.md). Mode 4 (sensor test pattern through the whole ISP, AE/AWB hold) is device-tested on T20, T21 and T23.

For streamers and test setups: `#include "openimp_testpattern.h"`, `OpenIMP_TestPattern_Set(mode, &params)` replaces the encoder/IVS input by a synthetic picture (1 pattern with the OpenIMP logo in the middle, 2 moving square + frame counter, 3 motion test with object size/contrast/speed or script), `OpenIMP_TestPattern_Get` reads the state back; switchable while streaming, no lock in the frame path. Detect: symbol `OpenIMP_Cap_TestPattern` (absent on vendor libimp, use `dlsym`/weak). Status: T23 device-tested (switching during one stream); other SoCs build + host test. Outside a streamer: `OPENIMP_TEST_PATTERN_CTL=<socket>` and `tools/testpattern/tpctl`.

## 7. Environment variables

User-facing = safe for a streamer to set in production. Debug-only = diagnostics or A/B rollback; do **not**
set in production.

| Variable | Purpose | Default | Class |
|---|---|---|---|
| `OPENIMP_IVS_VENDOR_ORDER` | `1` posts the IVS result before the channel-free semaphore as the vendor does (a frame fed right after the result is dropped as busy) | off | user-facing (opt-in) |
| `OPENIMP_SU_SHUTDOWN` | `SU_Base_Shutdown`: unset or `reboot` = sync, then SIGTERM to init (busybox reboots); `poweroff` = SIGUSR2 (busybox powers off, opt-in); `vendor` = vendor behaviour (sync, SIGCHLD, which busybox init ignores, opt-in) | `reboot` | user-facing |
| `OPENIMP_REF_SHARE` | `0` disables the T21/T23 reference ring | on | user-facing |
| `OPENIMP_T31_HW_JPEG` | `0` selects the software JPEG encoder on T31 | on | user-facing |
| `OPENIMP_T31_OSD` | `0` disables the IPU OSD backend on T31 | on | user-facing |
| `OPENIMP_T31_ROI` | `0` refuses `IMP_Encoder_SetChnRoiAttr` on T31 | on | user-facing |
| `OPENIMP_T21_ROI` | `0` switches the T21 encoder ROI programming off | on | user-facing |
| `OPENIMP_T41_ROI` | `1` enables the experimental T41 ROI table (not device-tested) | off | debug-only |
| `OPENIMP_T40_ROI` | `1` enables the experimental T40 ROI table (cross-built, not device-tested) | off | debug-only |
| `OPENIMP_T20_RC` | `0` disables the T20 OEM rate controller (needs kernel patch 0101) and falls back to the GOP controller | on (as vendor) | user-facing |
| `OPENIMP_EPRC_MBRC` | `1` enables the eprc macroblock RC on T21/T23 (device test pending) | off | user-facing (opt-in) |
| `OPENIMP_T10_RC` | `0` disables the T10 OEM-style rate controller | on | user-facing |
| `OPENIMP_T10_RC_SUPERFRM` | `0` restores vendor-exact T10 VBR behaviour (super-frame fix off); only inside the OEM controller | on | user-facing |
| `OPENIMP_T20_RC_IAWARE` | `0` = vendor P budget, `1` also for VBR/SMART; only inside the OEM controller | on for CBR | user-facing |
| `OPENIMP_T31_RC_CORE` | `legacy` restores the pre-Allegro rate-control core | allegro | user-facing |
| `OPENIMP_T21_EPRC` | T21: `0` restores the old GOP controller (the vendor-identical eprc is the default) | on | user-facing |
| `OPENIMP_T23_EPRC` | T23: `1` runs eprc for CBR/VBR too, `0` never; unset = eprc for SMART only | SMART only | user-facing |
| `OPENIMP_T20_MBRC` | `1` enables the macroblock-level rate control inside the T20 OEM controller | off | user-facing (opt-in) |
| `OPENIMP_EPRC_QP_DOWN1` | `1` / `2` enables the eprc QP-down limit; built, device test pending | off | user-facing (opt-in) |
| `OPENIMP_MOTION_V2` (+ `_BG`, `_SUPPRESS`, `_BLOBS`, `_OVERRIDE`, parameters) | motion v2, section 4.1; `0`/`off`/`vendor` = vendor algorithm; `shadow` = analysis without changing `retRoi` | on | user-facing |
| `OPENIMP_MOTION_V2_LOG` | `1` logs every vendor / v2 decision change (A/B) | off | debug-only |
| `OPENIMP_LOG_LEVEL` | `0`..`7` (syslog numbering, `3` error, `4` warning, `5` notice, `6` info, `7` debug): `IMP_LOG` lines above the level are dropped from syslog and stderr. Default `5` = notice: errors, warnings and the one-shot status lines (encoder ready, rate-control core, HWJPEG on/off, ISP driver) are visible, per-frame INFO (per-IDR rate-control line, HWJPEG counter, rmem peak) is silent. `6`/`7` or `OPENIMP_DEBUG_TRACE=1` for verbose. Read once into a global int by a constructor (the call site is one compare; formatting, syslog and stderr are one out-of-line function, no `getenv` or `openlog` per call site); `imp_log_fun` is unchanged | `5` | user-facing |
| `OPENIMP_LOG_SYSLOG` | `1` also logs to syslog | off | user-facing |
| `OPENIMP_PROFILE`, `OPENIMP_PROFILE_INTERVAL` | `1` enables a periodic profile report; interval in completed frames (docs/PROFILING.md) | off | debug-only |
| `OPENIMP_T31_VBR_LOOP` | `0` forces open-loop VBR on T31 | on (closed loop) | debug-only |
| `OPENIMP_T31_AVC_LEGACY` | `1` restores the pre-fix T31 H.264 recovery path (A/B) | off | debug-only |
| `OPENIMP_T31_COMPANION_STAGE` | `0` skips the AVPU JPEG companion core per H.264 frame (only relevant with software JPEG) | on | debug-only |
| `OPENIMP_T31_HW_JPEG_SRC_COHERENT` | `1` puts the JPEG source in coherent memory (A/B) | off | debug-only |
| `OPENIMP_RMEM_NO_REUSE` | `1` restores the old rmem behaviour (no reuse) | off | debug-only |
| `OPENIMP_DEBUG_TRACE` | per-frame detail trace (very verbose); the traces exist only in a build with `OPENIMP_DEBUG=1` (see below), in a release build it only raises the log level to 7 | off | debug-only |
| `OPENIMP_T31_IVS_STATS`, `OPENIMP_AEC_STATS`, `OPENIMP_SOURCE_STATS`, `OPENIMP_T31_FULL_FRAME_STATS`, `OPENIMP_T23_RC_STATS` | periodic statistics output | off | debug-only |
| `OPENIMP_T31_DUMP_SOURCE_DIR` | dump source frames to a directory | off | debug-only |
| `OPENIMP_T23_HELIX_BSF` | `1` hard bitstream limit; needs a patched kernel, device test open | off | debug-only |
| `OPENIMP_T41_STREAM_COPY_MODE`, `OPENIMP_T41_RATE_CONTROL_COUPLING`, `OPENIMP_T41_UNCACHED_COMMAND_RING`, `OPENIMP_T41_UNCACHED_EP3_RING` | `=0` rolls back T41 behaviour for A/B (docs/T41_STATUS.md, PROFILING.md) | new behaviour on | debug-only |

Further `OPENIMP_*` knobs exist in the source. The rmem, reference-sharing tuning, startup-trace, Helix SoC/timeout
and per-SoC statistics variables are listed in the wiki page "Environment variables"; the remaining bring-up/trace
switches (`P1_*`, `P2_*`, ...) are not described anywhere; treat them as internal.

### 7.1 Build-time switch `OPENIMP_DEBUG`

`OPENIMP_DEBUG=0` (default) builds a release `libimp.so`: `LOG_DEBUG` lines and the pure developer traces (the
`OPENIMP_DEBUG_TRACE` bench and `/dev/kmsg` probes, `OPENIMP_STARTUP_TRACE`, the per-frame stderr helpers, the
`LOG_CODEC`/`LOG_HW`/`LOG_DMA` codec traces, the ISP `kmsg_trace`) are compiled to nothing, code and strings.
`INFO`, `NOTICE`, `WARN` and `ERR` stay in every build; INFO is silent by default through `OPENIMP_LOG_LEVEL`.
`OPENIMP_DEBUG=1` builds everything, switched at run time by the variables above.

    make t31 OPENIMP_DEBUG=1          # or: OPENIMP_DEBUG=1 ./build-t31.sh   (T20..T41, same for all)

A firmware package passes the same variable to the build (`OPENIMP_DEBUG=1` in the package's make environment),
for a debug image; the default image carries the release build.

## 8. Unverified or not yet in this list

- `SU_Base_Shutdown` (host-tested only, fake `kill`/`sync`, not run on a camera): the vendor libsysutils call does `sync()` and `kill(1, SIGCHLD)`, which busybox init ignores, so the call returns and the device keeps running. OpenIMP now exports `SU_Base_Shutdown` itself: after the `sync()` it sends init the signal busybox acts on (`SIGTERM` = reboot by default, so a camera that calls it comes back; `SIGUSR2` = power off only with `OPENIMP_SU_SHUTDOWN=poweroff`; `OPENIMP_SU_SHUTDOWN=vendor` restores the vendor behaviour). Returns 0 once init was signalled (the call returns before init is done), -1 with errno if `kill` fails. Test: `make -f tests/core/su_base_shutdown.mk check`. Caveats: the symbol is now part of `libimp.so`, so a separately installed libsysutils that loads later does not override it; before relying on it, check the init system of the image (a non-busybox init may treat the signals differently) and run it once on a camera.
- T31 `OPENIMP_T31_COMPANION` (mentioned only as a proposal in T31_HW_JPEG_RE.md; the implemented knob is `..._COMPANION_STAGE`).
- SMART / vendor-equal eprc on T23, T21: done (`claude/eprc-complete`, 0 oracle deviations); SMART is still mapped to VBR on T10/T20; MB-level RC ported (opt-in, `claude/eprc-mbrc`, a8b483a, device test running).
- T23 live RC readback and which RC writes take effect on T10/T20/T21: partly stated in the matrix, per-field test not documented.
- T21 AWB hysteresis at real dusk (night checks only).
- AEC on T23 (implemented, device test open); AENC/ADEC double-release rejection (matrix cites it, no SoC test evidence).
- T23 `OPENIMP_T23_HELIX_BSF=1` hard bitstream limit; T23 vendor AE (`source_ae_oem=1`) is the default since all-20 (night and daylight tested; the AWB flip it caused in daylight is fixed by restoring the GIB black level after stream enable, open-tx-isp next).
- IVS EBUSY and JPEG last-frame reuse: documented as implemented, no dedicated test report found.
- Reference sharing on T41 and on T10/T20/T31: not applicable or unknown.
- Module reload (rmmod+insmod): T10 is device-tested, 5 cycles while streaming, 0 oops (the earlier `Failed to get csi clock -22` oops came from a module built against the T20 kernel tree; the T10 build now refuses that with #error, `claude/t10-reload-safe`). T41: the cause is found statically (`tx_isp_fs_remove` freed the channel array while the framechan0..2 misc devices were still registered; four static work items were not drained); the fix (`claude/t41-matrix-fixes`) is device-verified on the rev2 image: 10/10 cycles, 0 oops. The boot-time load is fine everywhere. Not a beyond-vendor item, listed so streamers know reload is now safe on T41 with the rev2 image.
- `IMP_ISP_QueryCaps` was prototyped (openimp `claude/imp-querycaps`, timps `claude/timps-querycaps`) and withdrawn by the maintainer; not part of any release.
- T23/T31 `IMP_AI_SetHpfCoFrequency` (and `IMP_AO_EnableHpf`) take effect since the release candidate (the filter is designed for the cut-off like the vendor library and handed to libaudioProcess-neo as a float biquad at the stream's rate; beyond the original library): apitest round trip only, no audio test on the shared test cameras yet.
- Encoder ROI: the vendor semantics of an absolute ROI QP under CBR are not verified (T20, T10); T23 passes regions to the vendor encoder with no measured effect; T41 is code only (section 1.1). The T21 ROI/QPG/SuperFrame calls are applied since the release candidate (section 1.1).
- T23 AWB cluster/trend objects and mask block (open-tx-isp `claude/release-t23-driver`, merged in the release candidate): identical to the stock module in the MIPS emulator, no device run of the runtime effect.
- T40/T41 FrameSource delay cache (`SetMaxDelay`/`SetDelay`/`GetTimedFrame`, host-tested only): costs one full-frame copy per `GetFrame` while the delay is above 0 (about 12 MB per frame at 4K NV12, about 3 MB at 1080p) plus `delay` frames of RAM; off by default. Details in `docs/T40_STATUS.md`.
