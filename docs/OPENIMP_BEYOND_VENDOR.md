# OpenIMP / open-tx-isp: where we go beyond or differ from the vendor stack

Audience: streamer developers (timps, prudynt, raptor). Each item below behaves differently from, or does
more than, the vendor libimp / kernel driver. Only items that are implemented and device-tested per
[OPEN_STACK_CHANGELOG.md](OPEN_STACK_CHANGELOG.md) and [FEATURE_MATRIX.md](FEATURE_MATRIX.md) are listed;
everything else is in "Unverified" at the end. Cameras are anonymised as in the changelog (cam-A T31,
cam-B T23, cam-C T20, cam-D T21, cam-E T10, cam-F T41).

Status tags: `[-all-13]` = tested, not yet in a flashed image. Rows marked "built, device test pending" are opt-in and not yet device-tested. Branch names refer to the open-stack repos.

Integration rule of thumb: all behaviour below is reachable through the **standard IMP API** (same
signatures as the vendor SDK). Nothing needs a new call; where a value is "different", it is in what the
call now returns or what the hardware now does.

## 1. Encoder / rate control

| Feature | SoC | API / param / env | Default | How a streamer uses it | Detect / disable | Status / branch |
|---|---|---|---|---|---|---|
| Capped modes are real, not silent CBR: CappedVBR / CappedQuality run a closed-loop regulator with the vendor PSNR cap (42 dB) | T31 | `IMP_Encoder_CreateChn` with `IMP_ENC_RC_MODE_CAPPED_VBR` / `CAPPED_QUALITY` | on | Select the mode as with the vendor SDK; bitrate tracks `maxBitRate`, quality capped at 42 dB | Log line "effective rate control" per channel; `OPENIMP_T31_VBR_LOOP=0` forces open loop (debug) | cam-A, `claude/rc-modes` [-all-13] |
| **Deviation (less than vendor):** CappedQuality behaves exactly like CappedVBR. The vendor additionally keeps lowering QP while the stream runs at max bitrate (idle < 5 %) and lets the HRD removal time slip (no emergency max-QP on scene change). Decoded in `docs/RC_MODES.md` (`claude/t31-capped-quality`), not implemented yet (full port of the vendor RC core in work) | T31 | rcMode `IMP_ENC_RC_MODE_CAPPED_QUALITY` | – | Treat `capped_quality` as `capped_vbr`; expect ~1–2 QP coarser in static scenes than vendor | – | decoded 2026-10-03, port pending user decision |
| Plain VBR is closed loop | T31 | `IMP_ENC_RC_MODE_VBR` | on | Nothing to do | `OPENIMP_T31_VBR_LOOP=0` (debug A/B) | cam-A, `claude/rc-modes` [-all-13] |
| Allegro rate-control core for VBR / CappedVBR / CappedQuality (vendor behaviour, ported instruction by instruction); CBR stays on the legacy controller | T31 | env `OPENIMP_T31_RC_CORE` | allegro (= vendor behaviour) | Nothing to do | `OPENIMP_T31_RC_CORE=legacy` restores the old controller | cam-A, `claude/t31-capped-quality` |
| T10 VBR super-frame fix (P1): VBR no longer misjudges a frame spanning several super-frames; the vendor re-encodes nearly every frame. Measured at 1200 kbit/s: 450 to 822 kbit/s, re-encodes 800 to 0, CPU 8.3 to 5.5 % | T10 | env `OPENIMP_T10_RC_SUPERFRM` (inside the OEM controller `OPENIMP_T10_RC=1`) | on (inside the OEM controller) | Nothing to do | `OPENIMP_T10_RC_SUPERFRM=0` restores vendor-exact behaviour | cam-E, `claude/t10-rc-superfrm` |
| **Built, device test pending:** eprc QP-down limit (restricts how fast QP may fall) | T21, T23 (eprc controller) | env `OPENIMP_EPRC_QP_DOWN1=1` / `=2` (two limit levels) | off (opt-in) | Opt-in for A/B tests | env var | built, device test pending |
| **Deviation (opt-in, default off):** T20 OEM rate controller port, which reads the NVPU statistics registers (needs kernel patch 0101). The vendor always runs this controller; here it is off by default. Measured 60 s at 1200 kbit/s: CBR 1435 (the vendor controller overshoots by itself), VBR 1329, SMART 983; old controller CBR 942 | T20 | env `OPENIMP_T20_RC=1` | off (opt-in) | Enable to get real SMART and tighter VBR; expect a CBR overshoot like the vendor | env var; effective-RC log line | cam-C, device-tested, not yet in an aggregate |
| T20 I-aware P budget (P2): CBR spreads the I-frame cost over the following P frames, which tightens the CBR overshoot. cam-C at 1200 kbit/s: CBR 1583 to 1300 (stats 1244). VBR is left as the vendor (with P2 it fell to 866) | T20 | env `OPENIMP_T20_RC_IAWARE` (inside the OEM controller `OPENIMP_T20_RC=1`) | on for CBR, vendor for VBR/SMART | Nothing to do | `=0` is vendor; `=1` forces it for VBR/SMART too (not recommended) | cam-C, `claude/t20-rc-iaware` |
| T10 OEM-style rate controller (opt-in) | T10 | env `OPENIMP_T10_RC=1` | off (opt-in) | Opt-in for A/B tests; CBR overshoot of the old path was +55 % at 2500 kbit/s | env var; effective-RC log line | cam-E, built, device test pending |
| Effective-RC log line | all | log tag `Encoder` | on | Parse it to see what the encoder really runs (requested mode may map to a different one) | grep the log at channel creation | all cams |
| Out-of-range QP / fps clamped **with a warning** instead of silent replacement | all | `minQp/maxQp`, `fps` in `IMP_Encoder_CreateChn`/`SetChnAttr` | on | Validate your own values; the warning names the clamped field | grep log for the clamp warning | all cams |
| RC readback returns the vendor-clamped values | T20, T21 | `IMP_Encoder_GetChnAttrRcAttr` | on | Do not assume what you set equals what you read back; use the read value | compare set vs get | `claude/rc-modes-2` [-all-13] |
| T23 RC app value 0 = vendor default (QP step 3, static time 15, change position 2/80), not "off" | T23 | RC attr fields QP step, static time, change position | on | Pass 0 to get vendor behaviour; set explicit values to override | n/a | cam-B, `claude/t23-rc-app-defaults` [-all-13] |
| Mode, bitrate, QP range, GOP, fps take effect; QP steps, staticTime/changePos/qualityLvl, I/P delta, bias are **accepted but ignored** | T10, T20, T21 | `IMP_Encoder_*` | n/a | Do not expose those knobs as live controls on these SoCs | documented limitation | `claude/rc-modes-2` [-all-13] |
| Encoder error limit: after 3 failed pictures the encoder is re-created; after 2 fruitless re-creates the channel stops (instead of stalling 20 s per picture) | T20, T21 | internal | on | Expect `GetStream` to fail/return after a channel stop; restart the channel from the streamer | log lines on re-create/stop | cam-C, cam-D soak OK, `claude/helix-error-limit` [-all-13] |
| Real HEVC | T31 | H.265 profile in `IMP_Encoder_CreateChn` | on | Use H.265 like the vendor; before, streams were empty | stream has VPS/SPS/PPS | cam-A, `claude/t31-hevc` |
| Encoder channel stats complete (vendor struct layout, real average bitrate) | all | `IMP_Encoder_Query` / `GetChnStat` | on | Can be polled for bitrate telemetry | n/a | `claude/openimp-quickfixes` |
| T10 drift fix (16-pixel reference border added once) | T10 | internal | on | Nothing; P frames no longer drift diagonally | n/a | cam-E, `claude/t10-drift-fix` |
| T20 bottom-row green flicker fixed (encoder padding rows filled) | T20 | internal | on | Nothing | n/a | cam-C, `claude/t20-bottom-chroma` |

Further items (evening 2026-10-03):

- **T41 AddSensor reclaim (pending):** after an OOM kill the T41 sensor stays registered and `AddSensor` returns EBUSY until reboot (vendor behaviour). A driver fix that re-registers the sensor (`claude/t41-sensor-rereg`) crashed on the first device load and is being analysed; not usable yet.
- **T21 AWB faster than vendor:** the lifted AWB needs 0.95x of the vendor instructions (was 1.41x), output bit-identical, cam-D isp_fw_process -10 % (`claude/t21-size-awb-opt`); the kernel module is also smaller (760 to 494 KB). No switch, nothing to integrate.

## 2. ISP tuning

| Feature | SoC | API | Default | How a streamer uses it | Detect / disable | Status / branch |
|---|---|---|---|---|---|---|
| Scene mode and colour effects act on hardware (B/W, vivid, negative; sepia also visible on T20); getters return what was set. The vendor kernel ignores them | T21 (colorfx also T20) | `IMP_ISP_Tuning_SetSceneMode`, `SetColorfxMode`, `Get*` | defaults vendor-identical | Expose them as image options on T21. On T23/T31 the same calls work (device-tested 2026-10-03 on cam-A/cam-B: colorfx 0/1/3/9 ok, unsupported values such as 2 and scene 15 return EINVAL); there it is vendor parity, not an extra, and not yet in an aggregate (`claude/t23-t31-scene-colorfx` + openimp `claude/scene-colorfx-imp`) | set then get; picture changes visibly | cam-D, `claude/t21-tuning-controls` [-all-13] |
| Sinter / Temper denoise **strength** takes effect (vendor ignores it) | T21 | ISP Sinter/Temper tuning controls, timps `sinter_strength` (128 = vendor picture) | vendor picture | Offer a denoise slider on T21 | compare images at 0/128/255 | cam-D, `claude/t21-tuning-controls`, `claude/t21-sinter-strength` [-all-13] |
| Sinter / Temper strength acts (the vendor firmware renormalises it away; only 0 acts there); 128 = IQ default; **built, T20 device-tested, T10 device test pending; decided: default on** (128 = IQ table, so the default picture is identical to the vendor; other values act, which goes beyond the vendor) | T10, T20 | ISP Sinter/Temper tuning controls, timps `sinter_strength` | on (decided 2026-10-03) | Denoise slider can be offered | compare images at 0/128/255 | cam-C, `claude/t10-t20-nr-wdr`, OpenIMP `claude/t20-nr-strength` |
| T23 front crop rejects windows outside the sensor, below 64x64 or odd sizes (-EINVAL); stock accepts any window | T23 | `IMP_ISP_Tuning_SetFrontCrop` / FRONT_CROP control | on | Check return codes; pass even sizes of at least 64x64 inside the sensor | n/a | cam-B, `claude/t23-matrix-gaps` |
| Day/night: brightness/contrast/saturation/sharpness re-sent on switch; table Sinter/Temper re-sent | T20, T21 | day/night running mode switch | on (as vendor) | Nothing; values you set persist across switches | n/a | cam-C, cam-D, `claude/tseries-daynight` |
| Day/night: a block whose parameters fail to load is bypassed instead of running with the other bank's values; **user bypass bits survive day/night switches** | T23 | `IMP_ISP_Tuning_SetModuleControl` | on | A module you bypassed stays bypassed after the switch | n/a | cam-B, `claude/t23-pkg2` |
| Max analog gain, max digital gain (additional ISP-dgain AE stage), IT max, SetSensorFPS reach the AE | T20, T21, T23 | `SetMaxAgain`, `SetMaxDgain`, AE IT max, `SetSensorFPS` | on | Use as vendor; on T23 the extra dgain stage is an addition | `t23tune` tool on T23 | cam-B (`t23tune` passed), cam-C, cam-D |
| AWB state kept across stream restarts (snapshots no longer green) | T23 | internal | on | On-demand snapshots restart the stream; no workaround needed any more | n/a | cam-B, `claude/t23-day-color` [-all-13] |
| AWB hysteresis band 10 % and night freeze (day gains restored on night to day) | T21 | kernel module parameters (both 0 = vendor behaviour) | 10 % | Nothing; avoids parameter-set flapping at dusk | set both params to 0 for vendor | cam-D night checks OK, `claude/t21-awb-hyst` [-all-13] |
| T31 privacy mask: 4 rectangles per channel, YUV fill, follows mirror/flip; RGB colours converted to YUV like vendor | T31 | `IMP_OSD`/ISP mask API (vendor signatures) | n/a | Use as vendor | get/clear OK | cam-A, `claude/t31-privacy-mask`, `claude/t31-mask-rgb2yuv` [-all-13] |
| Unknown tuning control IDs are **rejected** (vendor-style stubs returned success) | T23 | all `IMP_ISP_Tuning_*` | on | Check return codes: an error now means the control really does not exist | n/a | cam-B, `claude/t23-tuning-wiring` |

## 3. JPEG / snapshot

| Feature | SoC | API / env | Default | How a streamer uses it | Detect / disable | Status / branch |
|---|---|---|---|---|---|---|
| Adaptive quality: on bitstream truncation quality -5 (as vendor), **+5 back after 100 clean frames** | T20, T21, T23 (Helix) | internal | on | Configured quality is only the starting point; do not treat it as a fixed value | n/a | cam-C/D/B |
| Last JPEG re-delivered when the encoder is busy or video memory is short (no blocking) | Helix SoCs, T31 | `IMP_Encoder_GetStream` on JPEG channel | on | A snapshot may repeat the previous frame under load; compare timestamps/sequence if freshness matters | n/a | `claude/openimp-quickfixes` line, early-morning changelog |
| Configured JPEG quality is applied (before: fixed 75) | all | `IMP_Encoder_SetJpegeQl` / attr | on | Use it | n/a | `claude/openimp-quickfixes` |
| Hardware JPEG on T31 (about 70 % to 17 % timps CPU at 1 snapshot/s) | T31 | `OPENIMP_T31_HW_JPEG=0` selects software | on | Nothing | log: "T31 hardware JPEG enabled" / "off" | cam-A, `claude/t31-hwjpeg-default` |
| JPEG shares the H.264 bitstream area (-1.44 MB); JPEG bitstream buffer 1 MiB (-328 KiB) | T23 / T20, T10 | internal | on | Less video memory used | n/a | cam-B / cam-E, cam-C |

## 4. IVS / motion

| Feature | SoC | API | Default | How a streamer uses it | Detect / disable | Status |
|---|---|---|---|---|---|---|
| IVS channel already in use returns **EBUSY** (user decision) | all | IVS channel create/register | on | Handle EBUSY: another consumer owns the channel; do not retry blindly | return code | per matrix |
| Real frame-diff motion detection (vendor T20/T21/T30 reported "always no motion") | T20, T21, T30 | `IMP_IVS_*` move interface | on | Use the standard move IVS | 6/6 events, 0 false alarms (cam-C) | `claude/tseries-ivs` |
| Motion keeps working after the sub-stream goes idle; frames recycled for callback-backed pools | T23 | internal | on | Nothing | web grid not empty | cam-B |
| Sub-stream default for IVS: about 85 % less IVS CPU than the vendor libimp | T31 | internal | on | Prefer the sub-stream as IVS source | n/a | cam-A |
| Motion works without a viewer (feeder thread), CPU about 10-12 % vs 27 % with vendor libimp | T41 | internal | on | Nothing | n/a | cam-F, `claude/t41-libimp` |

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
| All user copies checked, out-of-bounds writes fixed | T20, T23 | vendor has unchecked copies | n/a | `claude/t20-robust`, `claude/t23-robust` |
| Frame source / VBM pool parked and reused at idle | T21 | vendor frees pool, later allocations fail in 23 MB rmem | Stream start/stop cycles do not exhaust video memory | `claude/t21-bringup` |
| One startup warning when pools plus fixed buffers exceed video memory | all | silent failure later | Read the warning to size your stream set | early-morning changelog |
| Real AEC (WebRTC AECM): `EnableAec` returns an error when it cannot run (vendor: fake success / flag only) | T31 (loopback: echo -18 dB, ERLE 44 dB) | n/a | Check the return of `IMP_AI_EnableAec` (EnableAec) | `claude/aec`; `OPENIMP_AEC_STATS=1` diagnostics |
| Software rotation `SetChnRotate` 90/270 (vendor-equivalent tiles) | T31 | earlier -1 | Rotation usable before OSD/IVS/encoder | `claude/t31-rotate` |
| T31 OSD drawn by the IPU, default on | T31 | `OPENIMP_T31_OSD=0` disables | OSD also in HW-JPEG snapshots; timps about 8 % CPU | cam-A |
| T20/T21 OSD via IPU hook | T20, T21 | vendor path never drew | OSD regions work | `claude/t20-osd` |

## 7. Environment variables

User-facing = safe for a streamer to set in production. Debug-only = diagnostics or A/B rollback; do **not**
set in production.

| Variable | Purpose | Default | Class |
|---|---|---|---|
| `OPENIMP_REF_SHARE` | `0` disables the T21/T23 reference ring | on | user-facing |
| `OPENIMP_T31_HW_JPEG` | `0` selects the software JPEG encoder on T31 | on | user-facing |
| `OPENIMP_T31_OSD` | `0` disables the IPU OSD backend on T31 | on | user-facing |
| `OPENIMP_T20_RC` | `1` enables the T20 OEM rate controller (needs kernel patch 0101); deviation: the vendor always runs it | off | user-facing (opt-in) |
| `OPENIMP_T10_RC` | `1` enables the T10 OEM-style rate controller | off | user-facing (opt-in) |
| `OPENIMP_T10_RC_SUPERFRM` | `0` restores vendor-exact T10 VBR behaviour (super-frame fix off); only inside the OEM controller | on | user-facing |
| `OPENIMP_T20_RC_IAWARE` | `0` = vendor P budget, `1` also for VBR/SMART; only inside the OEM controller | on for CBR | user-facing |
| `OPENIMP_T31_RC_CORE` | `legacy` restores the pre-Allegro rate-control core | allegro | user-facing |
| `OPENIMP_EPRC_QP_DOWN1` | `1` / `2` enables the eprc QP-down limit; built, device test pending | off | user-facing (opt-in) |
| `OPENIMP_LOG_SYSLOG` | `1` also logs to syslog | off | user-facing |
| `OPENIMP_PROFILE`, `OPENIMP_PROFILE_INTERVAL` | `1` enables a periodic profile report; interval in completed frames (docs/PROFILING.md) | off | debug-only |
| `OPENIMP_T31_VBR_LOOP` | `0` forces open-loop VBR on T31 | on (closed loop) | debug-only |
| `OPENIMP_T31_AVC_LEGACY` | `1` restores the pre-fix T31 H.264 recovery path (A/B) | off | debug-only |
| `OPENIMP_T31_COMPANION_STAGE` | `0` skips the AVPU JPEG companion core per H.264 frame (only relevant with software JPEG) | on | debug-only |
| `OPENIMP_T31_HW_JPEG_SRC_COHERENT` | `1` puts the JPEG source in coherent memory (A/B) | off | debug-only |
| `OPENIMP_RMEM_NO_REUSE` | `1` restores the old rmem behaviour (no reuse) | off | debug-only |
| `OPENIMP_DEBUG_TRACE` | per-frame detail trace (very verbose) | off | debug-only |
| `OPENIMP_T31_IVS_STATS`, `OPENIMP_AEC_STATS`, `OPENIMP_SOURCE_STATS`, `OPENIMP_T31_FULL_FRAME_STATS`, `OPENIMP_T23_RC_STATS` | periodic statistics output | off | debug-only |
| `OPENIMP_T31_DROP_IRQ_EVERY`, `OPENIMP_T31_DUMP_SOURCE_DIR` | fault injection / source dumps | off | debug-only |
| `OPENIMP_T23_HELIX_BSF` | `1` hard bitstream limit; needs a patched kernel, device test open | off | debug-only |
| `OPENIMP_T41_STREAM_COPY_MODE`, `OPENIMP_T41_RATE_CONTROL_COUPLING`, `OPENIMP_T41_UNCACHED_COMMAND_RING`, `OPENIMP_T41_UNCACHED_EP3_RING` | `=0` rolls back T41 behaviour for A/B (docs/T41_STATUS.md, PROFILING.md) | new behaviour on | debug-only |

Further `OPENIMP_*` knobs exist in the source (`P1_*`, `P2_*`, `HELIX_*`, `T23_HELIX_*`, `RMEM_*`, ...) but are
bring-up/trace switches that are not described in the docs; treat them as internal.

## 8. Unverified or not yet in this list

- T31 `OPENIMP_T31_COMPANION` (mentioned only as a proposal in T31_HW_JPEG_RE.md; the implemented knob is `..._COMPANION_STAGE`).
- SMART / vendor-equal eprc on T23, T21 (`claude/t23-smart`): in progress; SMART is still mapped to VBR on T10/T20/T21.
- T23 live RC readback and which RC writes take effect on T10/T20/T21: partly stated in the matrix, per-field test not documented.
- T21 AWB hysteresis at real dusk (night checks only).
- AEC on T23 (implemented, device test open); AENC/ADEC double-release rejection (matrix cites it, no SoC test evidence).
- T23 `OPENIMP_T23_HELIX_BSF=1` hard bitstream limit; T23 vendor AE (`source_ae_oem=1`) default switch (decided 2026-10-03: the lifted vendor AE becomes the default, after a night-switch test in the dark that is still pending).
- IVS EBUSY and JPEG last-frame reuse: documented as implemented, no dedicated test report found.
- Reference sharing on T41 and on T10/T20/T31: not applicable or unknown.
- Module reload (rmmod+insmod): T10 is device-tested, 5 cycles while streaming, 0 oops (the earlier `Failed to get csi clock -22` oops came from a module built against the T20 kernel tree; the T10 build now refuses that with #error, `claude/t10-reload-safe`). T41: the cause is found statically (`tx_isp_fs_remove` freed the channel array while the framechan0..2 misc devices were still registered; four static work items were not drained); the fix is on `claude/t41-reload-safe`, device test pending (needs a boot without the old module, boot guard `isp_open=manual`). The boot-time load is fine everywhere. Not a beyond-vendor item, listed so streamers do not rely on reload on T41 yet.
