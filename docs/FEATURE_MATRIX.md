# Open stack vs vendor stack: feature matrix

As of: 2026-10-03 11:40 (local time). Camera mapping: cam-A = T31, cam-B = T23, cam-C = T20, cam-D = T21, cam-E = T10, cam-F = T41.

Open stack = open-tx-isp (kernel driver) + OpenIMP (libimp) + timps. Vendor = tx-isp-*.ko + Ingenic libimp.

## Summary

**What is complete**

- All five test cameras (cam-A T31, cam-B T23, cam-C T20, cam-D T21, cam-E T10) run fully on the open kernel driver, OpenIMP and timps; no Ingenic/neo helper libraries any more, and on T23 no helixd and no vendor libimp either.
- Core functions are backed by evidence: H.264 on all five SoCs (soaks up to 2 h 53 without errors), HEVC on T31, hardware JPEG/MJPEG, second stream, OSD (text, bitmap, rectangle, line), real motion detection, day/night, flip.
- Beyond the vendor: reload/stop robustness (0 oops in 10 cycles each), smaller libimp (~0.5–0.6 MB instead of 1.0–1.3 MB), more free video memory on T21 (2.76 MB instead of ~1.2 MB), T31 module smaller than vendor, T21 controls and noise reduction take effect (the vendor ignores them), T31 AEC with −18 dB echo.

**Biggest gaps**

- T23 daylight: the green-yellow cast is fixed on claude/t23-day-color (bc70f10b, device-tested in sunlight, neutral colours) and is no longer a gap once flashed (-all-13). Cause: the WB gains were reset to 1x on every stream start (each on-demand snapshot restarts the stream); the vendor keeps the AWB state. The HLIL substitute AE fast start (~4 s instead of ~2 min) is in the same branch. Remaining: backlight/highlight only with vendor AE, real WDR missing (port of the dynamic ADR).
- T23 stability: three unexplained hard hangs in sun and one reboot with several parallel streams (memory pressure?) – not reproduced; the conspicuously large JPEGs (~730 KB at q75) are also unexplained.
- T41 (cam-F): still on the vendor kernel driver with OpenIMP; the open T41 driver is untested (the vendor tx_isp_t41 module oopses on rmmod, an image is being prepared). OSD and IVS now work with OpenIMP (claude/t41-libimp); short ~1.2-s IVS gaps are still being looked at. T10 (cam-E): only bring-up level documented; day/night after the drift fix and AE/AWB/image controls not documented.
- T20/T21 white balance and controls: T20 uses the OpenIMP "simple AWB" as default (vendor chain behind a switch, daylight test open); table Sinter has no effect on T20, as in the vendor; RC modes Capped*/SMART are still mapped to VBR on T10/T20/T21 (T31 CappedVBR/CappedQuality now closed-loop; T23 SMART in progress); RC readback on T20/T21 now returns the vendor-clamped values.
- Feature gaps per SoC: H.265 only on T31 (T21/T23 planned, lowest priority), AEC documented only on T31 (T23 implemented, device test open), reference sharing: vendor T23 uses it by default, the open T23 port is in progress, T21 opt-in with artefacts, T21 kernel module 805 KB instead of 616 KB.

Current state: cam-A, cam-C, cam-D, cam-E on open-tx-isp-all-11 / openimp-all-10 (kernel with soc_vpu patch 0099); cam-B on -all-12 / openimp-all-10 (HLIL AE default, vendor AE optional). The next aggregate -all-13 is planned with all [-all-13] items plus the T23 day-colour fix, the AVPU fixes, kernel patch 0100, reference sharing as opt-in and timps 3da95b6; it is not flashed yet.

Marked [-all-13] = tested but not flashed yet: T31 privacy mask, T21 controls + sinter_strength, T21 debug parameters removed, T20/T21 encoder error limit, T23 RC app-0 defaults, T20/T21 RC readback, sc4336p vflip fix, timps T23 AU limit 2 MiB+64 KiB (a0fdde8), T23 day-colour fix (bc70f10b, 390bc47c, HLIL fast start), AVPU review fixes, kernel patch 0100, timps 3da95b6 (OSD clock redrawn on idle to active; the first snapshot after idle showed a stale clock, tested on another T31).

In progress: T23 reference ring port (claude/t23-ref-ring), rate-control follow-ups (claude/rc-modes-2) and vendor-equal SMART on T23 (claude/t23-smart), T41 short IVS gaps, and an image for testing the open T41 driver. Not adopted: OSD edge flush, reference sharing on T10/T20/T31 (hardware missing).

## Legend

- ✅ supported: works, matches vendor behaviour, tested or backed by the changelog
- ✅+ improved beyond vendor: more robust, leaner or more functional than the vendor stack
- ⚠️ known defect / deviation: runs, but with a known defect or a deviation from the vendor
- 🔧 in progress: branch is running or a test is pending
- 📋 planned: deliberately scheduled, not started yet (mostly low priority)
- ❌ missing: not implemented
- — hardware does not have it: not applicable
- ? unknown: not documented in the sources, deliberately not guessed
- [-all-13]: tested but not flashed yet

## Matrix

| Feature | Vendor stack | T10 (cam-E) | T20 (cam-C) | T21 (cam-D) | T23 (cam-B) | T31 (cam-A) | T41 (cam-F) |
|---|---|---|---|---|---|---|---|
| **ISP core and image pipeline** | | | | | | | |
| ISP core / sensor bring-up | tx-isp-*.ko loads sensor + tuning bin | ✅ open driver boots, boot guard auto; day/night 10x without oops | ✅ bring-up stable; vendor-format isp-m0 | ✅ first open bring-up, ISP core = lifted vendor code | ✅ exposure readback live, ~45 empty CIDs wired up, unknown CIDs return -EINVAL | ✅ reference SoC; tuning gaps closed (RGB coefficients, AE ROI, SensorAttr) | 🔧 vendor-driver + OpenIMP bind-mount test passed (see H.264); open T41 driver not testable yet because the vendor tx_isp_t41 module oopses on rmmod (an image is being prepared); OSD/IVS now work with OpenIMP |
| Reload / error handling (rmmod, stop/start) | known oops on reload (T21 open counter, stats DMA) | ✅+ boot guard auto; reload cycles not documented individually | ✅+ rmmod during stream rejected; 10x stop/start + 10x reload, 0 oops; all 53 user copies checked | ✅+ 10x stop/start + rmmod/insmod, 0 oops; cause of the old oops (stats DMA into freed memory) fixed | ✅+ 10x stop/start incl. kill -9, 10x reload, 0 oops; 2 out-of-bounds writes (2 KB/18 KB) fixed | ✅+ 10x reload with kill -9; vmalloc leak of 252 KB/cycle fixed; residual drift ~45 KB/cycle | ? |
| Boot guard (protection against boot loops) | not present | ✅+ isp_open=auto | ? not documented | ✅+ S10isp-guard, u-boot isp_open=manual\|auto\|off | ? not documented | ? not documented | — |
| **Exposure (AE)** | | | | | | | |
| AE control | vendor AE in the kernel | ? only bring-up tested | ✅ compact AE: max gain, line_us, scene IT limit; low-light AE at dusk untested | ✅+ AE lifted 1:1 from vendor (incl. ae_tune2), night flicker gone; a·b·b bug found | ⚠️ default = HLIL substitute AE: slow start fixed on claude/t23-day-color (converges in ~4 s instead of ~2 min, device-tested); gaps remain (ROI/weights, flicker stages); vendor AE (optional) regulates correctly right away | ✅ reference SoC; 2 h 53 soak without errors | ? |
| AE compensation, backlight, highlight | IMP_ISP_Tuning_SetAeComp / Backlight / Highlight | ? | ? | ✅+ vendor dispatcher lifted; individual test not documented | ⚠️ AE comp works (also at night); backlight/highlight only with vendor AE, daytime test open | ? reference SoC, not documented individually | ? |
| Max gain, IT max, sensor FPS | MaxAgain/MaxDgain/AE_IT_MAX/SetSensorFPS | ? | ✅ MaxAgain clamped, line_us=29 reported | ✅+ vendor dispatcher SENSOR_FPS, AE zones/histogram/ROI | ✅ MaxAgain/MaxDgain, IT max, SetSensorFPS, additional ISP digital-gain stage; t23tune passed | ✅ EXPR setter, AE ROI, histogram edges | ? |
| Anti-flicker (50/60 Hz) | POWER_LINE / flicker dispatcher | ? | ? | ✅ lifted vendor dispatcher | ⚠️ wired up; substitute AE has no flicker stages, vendor AE ok (50 Hz emulator-identical) | ? | ? repo docs: dispatcher defect on a different T41 device, not cam-F |
| **Colour and image quality** | | | | | | | |
| White balance (AWB, presets, manual) | vendor AWB chain | ? | ⚠️ default = OpenIMP "simple AWB" (not vendor); presets/manual repaired; vendor chain behind t20_simple_awb=0, white-paper test open | ✅+ AWB lifted (10/10 scenes register-identical) + hysteresis + IR night freeze; dusk test open | ✅ [-all-13] daylight green cast fixed on claude/t23-day-color (bc70f10b), tested on cam-B in sunlight: neutral colours, WB gains kept (0x710/0x7c0); not flashed yet | ? reference SoC, not documented individually | ? |
| CCM / LSC (lens shading) | CT-controlled | ? | ? | ✅+ CT-controlled CCM/LSC lifted; colour blotches gone (chroma sigma 30→6) | ⚠️ CCM follows the IQ bank (as vendor) – green cast in daylight with vendor AE; LSC flip locked | ? | ? |
| Day/night switching (ISP side) | bank switch + mono matrix | ✅ 10 switches without oops; not re-tested after the drift fix | ✅ BCSH + Sinter/Temper re-sent as vendor does; Wyze day 128 / night 148/140 | ✅ night mono (chroma 0), gain stable instead of 6↔25 | ✅+ oops (wait queue) fixed, night mono, bank error → block bypass, user bypass persists | ✅ 13 switches in the 4.5 h soak | ? |
| IR cut / IR LED | via GPIO through timps/Thingino | ? | ? | ? | ⚠️ timps finding: auto night does not switch IR cut/LEDs (02.10., status unclear) | ? | ? |
| Brightness / contrast / saturation / sharpness / hue | IMP_ISP_Tuning_Set* | ? | ✅ defaults 0x80; sharpness works (edge energy 13/230/700); Wyze image.sharpness=128 | ✅+ getters lifted; sharpness/contrast readable | ✅ contrast day bank only (as vendor); sharpness block loaded from the IQ bank | ✅ defaults 0x80 | ? |
| Mirror / flip | SetHVFlip / sensor flip | ? | ✅ vflip UV address + DMA overwrite fixed (pink stripes) | ✅ flip dispatcher lifted; shvflip=1 | ✅ flip, Bayer re-sync, LSC flip locked | ✅ sc4336p vflip no longer reports an error [-all-13]; MSCA flip takes effect only at the next channel start, as with the vendor | ? |
| WDR / ADR / DRC | ADR/DRC/WDR paths in the vendor driver | ? | ? SDK has SetWDRAttr, status unknown | ✅+ ADR lifted (40/40 emulator), DRC reaches the driver; day Y 120 instead of 235 | ⚠️ DRC bit ok; real WDR needs a port of the dynamic ADR (open, large); sc2336 has no WDR mode | ✅ WDR buffer lazy; AE1 (short frame) stub = vendor no-op without WDR sensor | ? |
| Defog | vendor block | ? | ? | ✅+ lifted, IRQ 21 registered (40/40 emulator) | ⚠️ enable bit ok, default bypass; tisp_defog_soft_process is a stub | ? | ? |
| Noise reduction (2DNR/3DNR, Sinter, Temper) | table-driven | ? | ⚠️ table Sinter/Temper as vendor; CID 0x8000161 is missing there too → strength has no effect | ✅+ 2DNR/gain tracking repaired; Sinter/Temper strength now takes effect (vendor ignores it) [-all-13] | ✅ gain index now log2 (before: noise reduction too strong from 2x), Sinter non-compounding, sharpness/DPC follow the bank | ? | ? |
| DPC (defect pixels) | vendor block | ? | ? | ? | ✅ follows the IQ bank as vendor, less night noise | ? | ? |
| Scene mode / colour effects (B/W, negative, sepia, vivid) | SetSceneMode/SetColorfxMode (no-op in the vendor on T21) | ? | ✅ colorfx 0–3 set/get ok, sepia visible; scene ok | ✅+ B/W, vivid, negative work (confirmed with light on); getters return what was set [-all-13] | ⚠️ declared but not built (undefined symbol) | ⚠️ declared but not built | ? |
| Privacy mask (ISP hardware block) | 4 rectangles/channel, YUV fill | ? | ? | ? | ? state on vendor variables; not tested | ✅ as vendor, follows mirror/flip; emulator 400/400 identical; cam-A black+red ok [-all-13] | ? |
| Front crop / scaler level / CSC presets | SetFrontCrop, CSC, BLC | ? | ? | ? | ❌ FRONT_CROP/MASK CIDs return -EINVAL (vendor path not used) | ✅ BLC get, CSC presets 0–4 + user matrix, crop get/set, scaler level (tested on garage) | ? |
| Rotation 90°/270° | software rotation (32×32 tiles) | ? | ? | ? | ? | ✅ 704×1280 correct, 9 ms/frame @15 fps, before OSD/IVS/encoder | ? |
| **Video encoder and streams** | | | | | | | |
| H.264 | Helix (T20/T21/T23), AVPU (T31), NVPU (T10) | ✅ 720p 25 fps 1501 frames error-free; drift bug (margin added twice) fixed; own command list | ✅ soak 1 h 44, 156,517 frames/stream, 0 errors | ✅ main+sub+MJPEG, 25 fps; EMC scratch as vendor (1080p 996 KiB) | ✅+ native without helixd/OEM libimp: 2 h 34, 231,668 frames, 0 decode errors, ~6 % CPU | ✅ soak 2 h 53, 260,648 frames, 1030/1030 snapshots, 0 errors | 🔧 vendor driver + OpenIMP bind-mount test on cam-F passed: 1080p + 640x360 snapshots, MJPEG, MP4 on both channels, 0 oops; open T41 driver not tested yet (vendor tx_isp_t41 oopses on rmmod, needs an image); repo docs: 2560×1440 H.264 verified on a different T41 device |
| H.265 / HEVC | T31: AVPU; T21/T23: Helix H.265 path in the vendor | — timps: T10/T20 H.264 only | — T20 cannot do H.265 | 📋 lowest priority, vendor has the path | 📋 lowest priority | ✅ real HEVC on AVPU (VPS/SPS/PPS, CABAC); 2×900 frames, 0 errors | ? |
| JPEG / MJPEG / snapshot | hardware JPEG via vendor libimp/helixd | ✅ snapshots + MJPEG 25 B/5 s; JPEG buffer 1 MiB (−328 KiB) | ✅ HW JPEG, MJPEG 25 B/5 s with/without video consumer | ✅+ HW JPEG without vendor lib, 37 ms/job, snapshots 0.05–0.18 s; stripes with RST markers | ⚠️ HW JPEG 29 ms/job, shares the H.264 bitstream (−1.44 MB); JPEG sizes ~730 KB at q75 unexplained | ✅ HW JPEG, own MJPEG channel ok (24 B/5 s, before 0 bytes) | 🔧 snapshots (1080p + 640x360) and MJPEG ok in the vendor-driver test; TODO: grey JPEG (T40/T41) not re-checked on the open driver |
| Sub-stream / scaler (ch1 640×360) | scaler in the ISP | ✅ ch1 shows the full scene (DS1 horizontal ratio bug in the shared firmware fixed) | ✅ ch1 different scaler path, no regression | ✅ WebRTC main↔sub switching confirmed (rmem fix), 32+40 cycles | ✅ idle teardown bug (motion detection without frames) fixed | ✅ garage 25 fps ch0+ch1 4.5 h | 🔧 640x360 snapshot and MP4 ok on cam-F with the vendor driver; repo docs: 640×360 ok on a different T41 device |
| Rate-control mode (CBR/VBR/FixQP/Capped*/SMART) | all modes in the vendor libimp | ⚠️ CappedVBR/CappedQuality/SMART → VBR (log line) | ⚠️ CappedVBR/CappedQuality/SMART → VBR | ⚠️ CappedVBR/CappedQuality/SMART → VBR | 🔧 VBR/SMART mapping derived, not from vendor code (JZ_VPU_RC_VIDEO_CFG reconstruction open); vendor-equal SMART (eprc controller + SmartP long-term background reference) in progress (claude/t23-smart), device tests pending | 🔧 CappedVBR and CappedQuality run the closed-loop regulator with the vendor PSNR cap (42 dB) (claude/rc-modes); CappedQuality extra QP-lowering search not decoded; plain VBR closed loop by default like the vendor in progress (claude/rc-modes-2); SMART still → VBR | ? |
| RC parameters (QP steps, staticTime, changePos, qualityLvl, I bias) | via IMP attr | ? | ✅ readback returns the vendor-clamped values (staticTime 1, changePos 50, qualityLvl 0, QP steps 2/2 when the app passes 0) (claude/rc-modes) [-all-13] | ✅ readback as T20 [-all-13] | ✅ parameters reach the native encoder (changePos min 50); app value 0 = vendor default 3/15/2/80 [-all-13]; live readback in progress (claude/rc-modes-2) | 🔧 T31 defaults like the vendor (max QP 48, max bitrate 4/3, ...) in progress (claude/rc-modes-2) | ? |
| OSD: text, bitmap, rectangle, line, cover | IPU OSD / vendor libimp | ? | ✅ text/bitmap/lines/rectangles on both streams, clipping, 0 oops (IPU OSD hook) | ✅ IPU OSD hook as T20; rect/line/bitmap | ? not documented individually in the changelog | ✅ IPU OSD; lines/rectangles; rotation: no OSD clamp in timps | ✅ works with OpenIMP (claude/t41-libimp): PIC/COVER via IPU, text/line/rect on CPU; clock, name and logo visible on cam-F; kernel oops from the rmem cache flush fixed (T41 kernel expects a physical address) |
| IVS / motion detection | vendor IVS (T20/T21/T30 initially "always no motion" in the open stack) | ? | ✅+ 6/6 events, 0 false alarms; CPU 4.1→2.7 % with motion | ✅ real frame-diff IVS ported | ✅ motion active again after sub-stream idle (WebUI grid) | ✅+ sub-stream default: ~85 % less IVS CPU (compared with vendor libimp) | ✅ works with a feeder thread (claude/t41-libimp), no more 10-s stalls; short ~1.2-s gaps still being looked at |
| Frame source / VBM pool | vendor pools | ? | ? | ✅+ pool parked/reused (release at idle broke later allocations) | ✅ frames also recycled for callback pools | ? | ? |
| **Audio (documentation only, no tests)** | | | | | | | |
| Audio input (AI) | IMP_AI | ? | ? | ? | ? | ? | ? |
| Audio output (AO, speaker) | IMP_AO | ? | ? | ? | ? | ✅ volume/mute work, whole OSS fragments (tested on garage) | ? |
| Echo cancellation (AEC) | IMP_AI_EnableAec | ? changelog mentions only T31/T23 | ? device test open | ? device test open | 🔧 implemented (WebRTC AECM, driver reference offset); device test open | ✅+ real AECM: echo −18 dB, ERLE 44 dB (loopback); before: fake success | ? |
| **Memory, size, load** | | | | | | | |
| libimp size (code + data) | T21 ~1.0 / T23 ~1.26 / T31 ~1.05 MB | ? | ? | ✅+ ~0.5 MB | ✅+ ~0.6 MB (native, no helixd) | ✅+ ~0.57 MB | ? |
| Kernel module size | T21 616 / T23 857 / T31 829 KB | ? | ✅ vendor level | ⚠️ 805 KB (−13.8 KB after removing debug params [-all-13]); reduction planned | ⚠️ ~1,100 KB (was 1,607), bss 434→180 KB | ✅+ 716 KB | ? |
| Video memory (rmem) / MemFree | T21 ~23 MB for main+sub+JPEG | ✅ JPEG buffer −328 KiB; 40 KiB rootfs reserve in the image | ? JPEG buffer 1 MiB instead of frame-sized | ✅+ free with main+sub+MJPEG 2.76 MB (vendor ≈1.2) | ✅+ JPEG shares bitstream −1.44 MB; main window 2 MiB | ✅ drift per reload 460→45 KB | ? |
| Reference-frame sharing (BUF_SHARE_CFG) | vendor T23: used by default (≤1080p); vendor T21: off by default | — hardware missing | — hardware missing | ⚠️ opt-in only (OPENIMP_REF_SHARE=1, default off): saves ~1.4 MB but shows reference artefacts; will follow the T23 result | 🔧 vendor register capture taken; port being aligned (claude/t23-ref-ring) | — hardware missing | ? |
| CPU load (documented figures) | vendor comparison values mostly missing | ? | ✅ timps 2.7 % with motion; OEM AWB +4 % | ⚠️ lifted AWB 1.27× vendor CPU (was 5.5×) | ✅ ~6–7 % for 2 streams 25 fps | ✅ rotation 9 ms/frame @15 fps | ✅+ timps ~10–12 % with our libimp vs ~27 % with the vendor libimp (momentary values) |
| **Stability, helper libraries, telemetry** | | | | | | | |
| Kernel soc_vpu / Helix hardening | busy-wait up to 200 ms, unbounded waits | ✅+ error IRQ ends the wait immediately (patch 0099) | ✅+ patch 0099 | ✅+ patches 0095–0099 (bounded waits, pointer checks, register ioctl restricted to the VPU window) | ✅+ patches 0095–0099, IRQ_NONE, hard BSF limit optional | — SOC_VPU not built | ? |
| AVPU kernel driver (T31/T40/T41) review | – | — other VPU | — other VPU | — other VPU | — other VPU | ✅+ DeepSeek review verified; fixes on claude/avpu-review-fixes (minor-number leak, use-after-free on sysfs unbind, uninitialised dma-buf list mutex, flush clamp); kernel patch 0100 validates the rmem flush ioctl (invalid direction no longer crashes the kernel); tested on cam-A: reload, kill -9, 5× rmmod/insmod, 0 oops [-all-13] | ? same module, not tested on cam-F yet |
| Long-term stability / hangs | – | ✅ 60 s/1501 frames error-free; long-term test not documented | ✅ 1 h 44 soak, 0 errors | ? no long-term log in the sources | ⚠️ 3× hard hang in afternoon sun (watchdog) not reproduced; candidate causes patched | ✅ 4.5 h soak ok | ? |
| Helper libraries libalog / libsysutils | shipped with vendor images | ✅+ removed | ✅+ removed (image tested) | ✅+ removed | ✅+ removed; also no helixd/vendor libimp | ✅+ removed | 🔧 T41 build without both libs prepared |
| Tuning getters / readback | IMP_ISP_Tuning_Get* | ? | ✅ SDK control IDs, pointer semantics as vendor, isp-m0 | ✅+ getters return what was set [-all-13] (vendor: scene/colorfx/Sinter DNS no-op) | ✅ expr/EV/TotalGain live, SensorAttr 20-byte layout, vendor isp-m0 | ✅ SensorAttr, WaitFrame per frame, isp-w02 counter | ? |
| Encoder telemetry / diagnostics | IMP_Encoder_Query/ChnStat | ✅+ RC log line, clamp warning | ✅+ ditto | ✅+ ditto | ✅+ ditto | ✅+ ditto | ? |

## Notes per row


### ISP core and image pipeline

- **ISP core / sensor bring-up**: Sources: changelog (open-tx-isp per SoC). T10 shares its firmware base with T20; T10L panic (wdr_mode reconstruction destroyed FSM pointers) fixed (claude/t10-fixes). T41: repo docs (T41_STATUS/T41_PARITY_STATUS) describe an earlier bring-up on a different T41 device; cam-F itself still runs the vendor stack, with OpenIMP bind-mounted for testing. OSD and IVS now work with OpenIMP on cam-F (claude/t41-libimp). The open T41 driver could not be tested yet because the vendor tx_isp_t41 module oopses on rmmod; an image is being prepared. OpenIMP T41 build without libalog/libsysutils is prepared.
- **Reload / error handling (rmmod, stop/start)**: Branches claude/*-robust (2026-10-02), review fixes (sinfo deadlock, module notifier against dangling sensor pointer). The T31 residual drift (MemFree) is open at low priority. T23 error path: a broken IQ file gives a clean STREAMON error instead of an oops (claude/t23-iq-fail).
- **Boot guard (protection against boot loops)**: The boot guard S10isp-guard comes from the T21 image; according to the changelog T10 also runs with the guard (auto). For T20/T23/T31 the sources mention no guard.

### Exposure (AE)

- **AE control**: T23: the substitute AE has gaps (ROI/weights, flicker stages, scene parameters). Vendor AE (source_ae_oem=1) is emulator-identical in 6 scenes incl. 50 Hz; it becomes the default only after further daylight tests (the colour cast itself is fixed on claude/t23-day-color [-all-13]). Incident: vendor AE as default reported gain 1x and timps never switched to night, so it was reverted in -all-11b. The T23 overexposure at stream restart (AE reset) is fixed (exposure is kept as with the vendor). The HLIL slow start (about 2 min until the exposure converged) is fixed on claude/t23-day-color: it now converges in about 4 s, tested on the device.
- **AE compensation, backlight, highlight**: T23: compensation, backlight and highlight are repaired for the vendor AE (claude/t23-image-controls); the HLIL substitute AE does not evaluate backlight/highlight.
- **Max gain, IT max, sensor FPS**: timps: AE IT max can be reset to 0 again (PR #3 merged). T23: ISP digital gain as its own AE stage (claude/t23-flip-dgain).
- **Anti-flicker (50/60 Hz)**: T23 substitute AE: flicker stages are missing (gap according to the audit). Vendor AE covers 50 Hz.

### Colour and image quality

- **White balance (AWB, presets, manual)**: T20: awb_normalise bug and swapped preset direction fixed (Wyze: preset 4 very blue, 7 warm), CPU of the vendor chain +4 %, JPEG +40 % larger. T21: colour values R/G 1.04 vs vendor 1.05, B/G 0.93 vs 0.92; CT update only above 50 K (-62 % register writes); first-snapshot delay after start fixed. Hysteresis/freeze [-all-11 flashed], restore fix from review. timps (simulation-tested, device test open): WB modes 0..9, custom WB hidden on T10/T20/T30. T23: the colour cast with a white LED was only configuration (manual WB in timps.conf). T23 daylight: the green-yellow cast is FIXED on claude/t23-day-color (bc70f10b) [-all-13]. Cause: our driver reset the WB gains to 1x on every stream start, and every on-demand snapshot restarts the stream, so snapshots were taken before AWB had re-converged; the vendor keeps the AWB state across stream restarts. Commit 390bc47c additionally writes the vendor values for top 0x1c and GIB 0x1008/0x1010 at stream start. Device-tested on cam-B in sunlight: neutral colours, gains kept (0x710/0x7c0). Not flashed yet.
- **CCM / LSC (lens shading)**: T21: LSC gain per channel doubled → blotches, fixed. T23: CCM value 0xB5742A89 examined in the daylight image with vendor AE (claude/t23-day-color).
- **Day/night switching (ISP side)**: T23: the CSC clip register had lost an argument → purple at night, fixed. Block bypass on load failure and user bypass persisting across switches are improvements beyond the vendor (claude/t23-pkg2). timps adopts an externally changed mode after 20 s (simulation). timps finding of 02.10.: auto night does not switch IR cut/LEDs (status open in the sources, not SoC-specific).
- **IR cut / IR LED**: Not a driver topic: control lives in timps/Thingino. Listed here only as an open finding because it is mentioned in the changelog/TODO.
- **Brightness / contrast / saturation / sharpness / hue**: OpenIMP quick fix: contrast/sharpness started at 0 instead of 0x80. T20: sharpness never ran in the default path (firmware worker parked), now in the compact AE loop. Hue: not documented individually anywhere in the sources.
- **Mirror / flip**: T20: DMA wrote 12 chroma lines past the buffer end (memory corruption!) – included in -all-10. T31: local sensor patch (thingino), not a driver bug.
- **WDR / ADR / DRC**: T21: overexposure from a fixed ADR curve → fixed. T23: ADR-128 reset bug fixed; defog/ADR state was mapped to the wrong memory → repaired (t23-ae-oem-export). timps: the WDR API is split per generation (WDR_ENABLE vs. SetWDRAttr; per SDK, T21/T23 have no WDR).
- **Defog**: T23: sc2336 has no WDR mode, so defog WDR is not needed (documented).
- **Noise reduction (2DNR/3DNR, Sinter, Temper)**: T21: timps sinter_strength (128 = vendor image) only takes effect with claude/t21-sinter-strength [-all-13]. DMSC/SDNS/MDNS gain_old separated instead of shared.
- **DPC (defect pixels)**: Documented in the sources only for T23 (t23-pkg2).
- **Scene mode / colour effects (B/W, negative, sepia, vivid)**: Review item: scene/colorfx declared but not built for T23/T30/T31. T21: colour effects B/W, vivid and negative confirmed with light on.
- **Privacy mask (ISP hardware block)**: Previously a stub (stored only). OpenIMP converts RGB→YUV like the vendor libimp. COVER rectangles via the OSD (IPU) are separate, see OSD.
- **Front crop / scaler level / CSC presets**: T31: setting CSC at night overwrites the night mono clip, as with the vendor. A Thingino option for CSC preset 1 (TV range) is only an idea.
- **Rotation 90°/270°**: Previously SetChnRotate returned -1. Not mentioned for other SoCs in the sources.

### Video encoder and streams

- **H.264**: T23: bitstream overflow (frame >1 MiB) fixed, window 2 MiB; the hardware ignores the window and overwrote the reference buffer – a hang candidate. timps AU limit for T23 set to 2 MiB+64 KiB (a0fdde8) [-all-13/timps]. timps OSD clock: the first snapshot after idle showed a stale clock (minutes to hours); fixed in timps 3da95b6 (text redrawn on idle to active), tested on another T31, ships with the next images. T10: NVPU writes 21/10 KB behind each reference plane, references enlarged accordingly. T20/T21: error limit (3 bad frames → encoder restart, 2 re-creations → channel stops) instead of 20 s per frame [-all-13]. IDR at FIXQP = QP−3 as vendor. T41: the test ran with the vendor driver, not the open one.
- **H.265 / HEVC**: Before: H.265 accepted, stream empty. T31: own lambda tables by formula (bit-identical, 12 documented ±1).
- **JPEG / MJPEG / snapshot**: Helix HW JPEG: ~95 % less CPU per snapshot. Quality is applied (before: fixed 75); on truncation quality −5 as vendor, +5 after 100 clean frames (beyond vendor). When VPU/rmem is tight: the last JPEG is reused instead of blocking. T21: the hardware ignores the JPEG size limit, the core loses the last 128-byte burst per job (as vendor, compensated). The dedicated JPEG channel never received frames → timps restarted the frame source (fixed on all SoCs).
- **Sub-stream / scaler (ch1 640×360)**: T10: `_update_ds()` divided the output width by the output width (ratio always 1.0); applies to the shared T20/T10 firmware code.
- **Rate-control mode (CBR/VBR/FixQP/Capped*/SMART)**: Before: silently CBR. Reconstructing the vendor registers (RC_VIDEO_CFG/FRAME_START/END) is an open improvement proposal (large). T31: CappedVBR/CappedQuality now closed-loop with PSNR cap 42 dB (claude/rc-modes). In progress (claude/rc-modes-2): T31 plain VBR closed loop by default and T31 defaults like the vendor (max QP 48, max bitrate 4/3, ...); checking which RC writes take effect on T10/T20/T21. Device tests pending. One log line per channel with the effective RC; invalid QP/fps are clamped with a warning (beyond vendor).
- **RC parameters (QP steps, staticTime, changePos, qualityLvl, I bias)**: T23: the qualityLvl/changePos mapping is partly derived from SDK text, not from vendor code. A one-off "helix vpu error interrupt status=100" after the change, unexplained. timps VBR changes as a result (target 80 %, P-QP step max 3). Correction: the vendor-clamped values returned on T20/T21 when the app passes 0 are staticTime 1, changePos 50, qualityLvl 0, QP steps 2/2 (earlier assumption 1/80/2 was wrong).
- **OSD: text, bitmap, rectangle, line, cover**: Lines/rectangles are drawn by the CPU (opaque, same as vendor). Review fixed a use-after-free on bitmap data. Not adopted: cache flush of the edge lines only (the kernel caps the band, the proposal would be slower); open ideas: IPU mask layers, alpha for lines, Bresenham thickness.
- **IVS / motion detection**: IVS channel in use → EBUSY (beyond vendor, user decision). New motion detection (background model, blobs) = "later", very low priority.
- **Frame source / VBM pool**: T21: in the 23 MB rmem the allocation failed after idle teardown.

### Audio (documentation only, no tests)

- **Audio input (AI)**: Mentioned only indirectly in the changelog (HPF overflow, AENC/ADEC double release rejected = beyond vendor). No SoC-specific evidence.
- **Audio output (AO, speaker)**: The quick fix applies generically in OpenIMP; a SoC-specific test is documented only for T31.
- **Echo cancellation (AEC)**: Error when AEC cannot run (instead of fake success); diagnostics OPENIMP_AEC_STATS. Source: claude/aec. No audio tests as part of this matrix.

### Memory, size, load

- **libimp size (code + data)**: Figures from the changelog section "Compared with the vendor stack".
- **Kernel module size**: T21: static data from the lifted AE/ADR/AWB code; still open (functions duplicated as C reimplementation and lift, ~7–9 KB). T23 module RAM is intentionally larger because of reconstruction.
- **Video memory (rmem) / MemFree**: T21: the MemFree reserve with main+sub was originally only 0.5 MB → WebRTC switching failed; fixed. T23: one-off reboot ~08:02 with several parallel MP4 streams (MemAvailable ~0 at 38 MB), cause probably memory pressure/watchdog, not reproduced. Startup warning when pools + fixed buffers > rmem.
- **Reference-frame sharing (BUF_SHARE_CFG)**: Only T21/T23 (Helix) have ring mode. User decision: do not pursue for T10/T20/T31. NEW finding: the vendor T23 stack uses the reference ring BY DEFAULT (measured on cam-B: register 0x60004 bit 30 set, luma ring 1920×(1088+256), chroma 1920×(544+128)); the vendor libimp forces it on in CreateChn for channels up to 1920×1088 (SetRdBufShare has no effect). A vendor register capture was taken and the open port is being aligned (claude/t23-ref-ring). T21: still opt-in (claude/ref-buf-share, not in the aggregate) with reference artefacts (five register variants in claude/ref-buf-share-2 did not remove them); it will follow the T23 result.
- **CPU load (documented figures)**: HW JPEG saves ~95 % CPU per snapshot; IVS optimisation T20 4.1→2.7 %; T21: 346 runtime address translations in the lift pending. No systematic vendor-versus-open CPU comparison in the sources.

### Stability, helper libraries, telemetry

- **Kernel soc_vpu / Helix hardening**: Patch 0099 flashed in -all-11 (04:07–04:16), 0 VPU errors. Hard bitstream limit (OPENIMP_T23_HELIX_BSF=1) only with a patched kernel, bit 19 derived from disassembly, device test open.
- **AVPU kernel driver (T31/T40/T41) review**: The AVPU module is shared by T31/T40/T41. Patch 0100 is a kernel patch (rmem flush ioctl validation); the module fixes are in the OpenIMP repo (avpu/), branch claude/avpu-review-fixes.
- **Long-term stability / hangs**: T23: candidates: encoder overflow overwrites reference, soc_vpu lock leak, stats DMA into reused memory; countermeasures in the -all-9..12 state, but the daylight/sun test with vendor AE is pending. In addition an unexplained reboot with several MP4 streams (see memory).
- **Helper libraries libalog / libsysutils**: The two libs were already open neo replacements; libimp now contains the two logging functions. Syslog only via OPENIMP_LOG_SYSLOG=1. On all five cameras since -all-10.
- **Tuning getters / readback**: T21: defaults from getters are vendor behaviour (lifted OEM kernel). T23: tool t23tune for checking on the device. T31: AF getters return zeros (fixed focus, irrelevant).
- **Encoder telemetry / diagnostics**: Channel-stat struct (a word was missing), real bitrate average, stack overflow in ChnStatQuery fixed (quick fixes).

Sources: OPEN_STACK_CHANGELOG.md (branch claude/docs-open-stack-changelog, 87a4979), TODO.md, VERBESSERUNGEN.md, NOT_CONNECTED_2026-10-02.md, IMAGES-2026-10-03-*.md, OpenIMP docs (T23_NATIVE_HELIX, T30/T41_STATUS), open-tx-isp docs (T41_PARITY_STATUS), timps docs/sdk-feature-gaps.md. Cells without evidence are shown as "?". Cameras are anonymised, no IPs or credentials.
