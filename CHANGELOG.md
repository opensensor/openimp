# Changelog (OpenIMP)

**Where to read what**

- **This file**: release-oriented summary per area (what changed against the vendor stack and against the original upstream line, [opensensor/openimp](https://github.com/opensensor/openimp)), followed by the condensed OpenIMP-only log by date.
- **Full campaign log**: [docs/OPEN_STACK_CHANGELOG.md](docs/OPEN_STACK_CHANGELOG.md): every change of OpenIMP, open-tx-isp, timps and the thingino integration since 2026-09-30, per session, with device-test evidence and numbers. The same text is on the wiki page *Changelog*.
- **Current state per feature and SoC**: [docs/FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md) (HTML view: [docs/feature-matrix.html](docs/feature-matrix.html)); what goes beyond the vendor: [docs/OPENIMP_BEYOND_VENDOR.md](docs/OPENIMP_BEYOND_VENDOR.md).

Release tags `vYYYY.MM.DD` on the `aperto` branch are planned (the first one after the 24 h soak that started 2026-10-04); until then dates are the reference. Branch names (`claude/...`) are historic: the topic branches were merged into `next` and deleted. Cameras are anonymised: cam-A (T31), cam-B (T23), cam-C (T20), cam-D (T21), cam-E (T10), cam-F (T41); cam-G and cam-H are further T23 cameras.

## Summary by area (state of 2026-10-05)

All test cameras run the open stack (open-tx-isp kernel driver + OpenIMP + timps) from full OTA images built from thingino `aperto`; no Ingenic or neo helper libraries remain, and T23 runs without helixd and without vendor libimp.

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

### Known open items

T23 sporadic single Helix encode error (errno 5; the frequent frame drops are fixed) and T23 real WDR; T41 flip, night column noise, short IVS gaps, day/night and AE/AWB quality; T21 a 4th module reload in one boot crashed once; AEC device tests on T23/T21/T20 (no speaker tests on shared cameras); first release tag after the 24 h soak. Details: the "Still open" section of the full log and the feature matrix.

## OpenIMP changes by date (condensed)

Only OpenIMP (userspace libimp) changes, newest first. Everything listed was device-tested on the SoC named unless marked otherwise.

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
