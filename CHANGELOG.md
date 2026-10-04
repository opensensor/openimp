# Changelog (OpenIMP)

Condensed from the open-stack campaign changelog; only OpenIMP (userspace libimp) changes.
Newest first, grouped by date. Everything listed was device-tested on the SoC named unless
marked otherwise. Branch names refer to the `claude/*` topic branches merged into `next`.
Release tags `vYYYY.MM.DD` are planned; until then dates are the reference.

## 2026-10-04

- T20/T10: rmem high-water logging and shortfall messages with a concrete suggestion when reserved memory is too small instead of silent degradation.
- Hardening: repeated driver errors rate-limited, silent `-1` returns logged, four NULL crashes, a buffer overflow in the module-chain dump, lost items on EINTR and an OSD size overflow fixed.
- T31: `SetChnQpIPDelta` now updates the value `GetChnAttrRcMode` returns, as the vendor does.
- T23: OSD stride fix (clean date/time text on main and sub stream); sub-stream reference fix; the OEM Helix helper option and the hybrid install are removed, T23 runs fully open. Open: a sporadic single Helix encode error.
- T21/T23: sub-stream (640x360) corruption from reference sharing on the small channel fixed.
- T41: H.265 on the AVPU path (like T31); a stuck AVPU job times out after 2 s and resets the core. CBR overshoot fix, unload/flip/BCSH fixes in `next`.
- Review fixes: complete `O_CLOEXEC`, eprc `FRAME_END` for dropped pictures, T31 Allegro RC lock, forced IDR after a YUV error, T20 MB-RC table bounds.
- Size and CPU: T20 MB-RC table 590 to 58 KiB, no per-frame malloc; libimp T31 -34 KB, T41 -20 KB, T21 -17.5 KiB text; JPEG Huffman parsing by table, OSD cache invalidation, T31 EBSP copy (timps CPU T31 8.7 to 7.9 %, T21 17.5 to 15.5 %).
- Motion detection v2 (opt-in, vendor-identical when off): background model per grid cell, suppression after IR/exposure switches, blob grouping, bounding boxes and strength through the versioned `OpenIMP_IVS_MoveGetResultEx` API; false alarms down in overnight runs.
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
