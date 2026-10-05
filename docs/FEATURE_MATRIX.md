# Open stack vs vendor stack: feature matrix

As of: 2026-10-04 23:10 (state of `next`; the per-cell details below are a chronological test log, newest results win). Camera mapping: cam-A = T31, cam-B = T23, cam-C = T20, cam-D = T21, cam-E = T10, cam-F = T41; cam-G and cam-H are further T23 cameras (other sensors) that run the same stack.

Branch names in brackets (`claude/...`) and the tags `[-all-N]` are historic: those branches were merged into `next` and deleted. Where a cell says "not yet in an aggregate", "device test pending" or "opt-in", check the summary below and the defaults table in [OPENIMP_BEYOND_VENDOR.md](OPENIMP_BEYOND_VENDOR.md) first.

See also: the section "Missing / incomplete functions" below (vendor IMP/SU functions per SoC that are not fully done); [OPENIMP_BEYOND_VENDOR.md](OPENIMP_BEYOND_VENDOR.md) (integration notes for streamer authors on everything marked beyond vendor).

Open stack = open-tx-isp (kernel driver) + OpenIMP (libimp) + timps. Vendor = tx-isp-*.ko + Ingenic libimp.

## Summary

**What is complete**

- All five original test cameras (cam-A T31, cam-B T23, cam-C T20, cam-D T21, cam-E T10) run fully on the open kernel driver, OpenIMP and timps from flashed images; cam-F (T41) runs the same stack from the `aperto` full OTA image; no Ingenic/neo helper libraries any more, and on T23 no helixd and no vendor libimp either.
- Core functions are backed by evidence: H.264 on all six SoCs (soaks up to 2 h 53 without errors), HEVC on T31, hardware JPEG/MJPEG, second stream, OSD (text, bitmap, rectangle, line), real motion detection, day/night, flip.
- Beyond the vendor: reload/stop robustness (0 oops in 10 cycles each), smaller libimp (~0.5–0.6 MB instead of 1.0–1.3 MB), more free video memory on T21 (2.76 MB instead of ~1.2 MB), T31 module smaller than vendor, T21 controls and noise reduction take effect (the vendor ignores them), T31 AEC with −18 dB echo, reference-frame sharing on T21/T23 (~1.5 MB less video memory, on by default).

**Biggest gaps (2026-10-04)**

- T23: the frequent Helix frame drops had a fixed cause (a residual interrupt treated as an error by the bounded-wait kernel patch; 60 min with 0 errors after the fix); a sporadic single encode error (errno 5) is still listed as under investigation. The root cause of the cold-start snapshot 503 on a second channel (stale MSCA FIFOs) is fixed in the driver (`msca_fifo_rearm`, 260 cold-start cycles without a failure) and awaits its soak before it enters `next`. Real WDR is missing (dynamic ADR is lifted, no WDR sensor mode).
- T41 (cam-F): runs the fully open stack with H.264 (main stream High 1080p ok) and H.265; AE compensation, gain/exposure caps and 2D noise reduction work through OpenIMP; DRC, DPC, defog, WDR, CCM, gamma, HLC and BLC are not supported on T41 (the driver says so). Open: crop/rotation (I2D), temper effect, ioctl hardening awaits its device test, live flip (timps does not call `SetHVFLIP` live), night column noise of the gc5603 (ISP), short IVS gaps (~1.2 s), OOM with three parallel streams and 30 MB rmem (26 MB works in the current image; the kernel command line needs an environment-partition image), `AddSensor` EBUSY after an OOM kill; day/night, AE/AWB quality, audio untested.
- T10 (cam-E): AE/AWB quality and image controls only partly documented.
- T21: a 4th module reload in one boot crashed once (under investigation); `ae_it_max_us` acts, which is beyond the vendor and kept by decision.
- Rate control: T10/T20 run the OEM-style controller for CBR/VBR (SMART is mapped to VBR); T10/T20/T21 accept QP steps, staticTime/changePos/qualityLvl without all of them acting on every path; T31 CBR writes no filler NAL.
- AEC: only T31 is device-tested (T23 implemented, device test open; no speaker tests on the shared test cameras). Audio output only on T31.
- Not yet exposed by timps (static caps; on T41 AE compensation and sinter are wired, the gain/exposure caps follow): DPC strength, defog/Iridix floor, DRC strength and the scene/colour effects; the timps side is decided by the timps maintainers.
- T30 and T40 have no device in the test campaign (T30 builds against a real kernel; not device-verified here).
- Opt-in and not device-tested: `OPENIMP_EPRC_QP_DOWN1`, `OPENIMP_EPRC_MBRC` (ported, 0 oracle deviations, device test not finished), `OPENIMP_T23_HELIX_BSF`.

Defaults in `next` that older cells may still call opt-in or pending: T20/T10 OEM rate controller on, T10 super-frame fix on, T20 I-aware P budget on for CBR, T21 vendor-identical eprc on, T31 Allegro RC core on, reference sharing on T21/T23 on, motion detection v2 on (`OPENIMP_MOTION_V2=0` = vendor algorithm), T23 vendor AE as default (`source_ae_oem=1`), `isp_mmap_pool_kb=0` on T10/T20 (+8 MB free RAM), rmem peak logging and shortfall hints.

Current state: all test cameras run full OTA images built from thingino `aperto` (Lu-Fi forks pinned to open-tx-isp `next` 40cc77ec and OpenIMP `next` db760431) since 2026-10-04 17:39-17:47, with timps v1.9.31 and the kernel VPU/rmem patches as merged upstream; cam-F (T41) rootfs rev7 with rmem 26M. A 24 h soak has run since 17:50 (5 h so far: 0 streamer restarts, 0 encoder/VPU errors, 0 oops); the first release tag follows after it.

Not adopted: OSD edge flush, reference sharing on T10/T20/T31 (hardware missing).

## Legend

- ✅ supported: works, matches vendor behaviour, tested or backed by the changelog
- ✅+ improved beyond vendor: more robust, leaner or more functional than the vendor stack
- ⚠️ known defect / deviation: runs, but with a known defect or a deviation from the vendor
- 🔧 in progress: branch is running or a test is pending
- 📋 planned: deliberately scheduled, not started yet (mostly low priority)
- ❌ missing: not implemented
- — hardware does not have it: not applicable
- ? unknown: not documented in the sources, deliberately not guessed
- [-all-13]: historic tag (tested before the later aggregates); everything so tagged is contained in `next`

## Matrix

| Feature | Vendor stack | T10 (cam-E) | T20 (cam-C) | T21 (cam-D) | T23 (cam-B) | T31 (cam-A) | T41 (cam-F) |
|---|---|---|---|---|---|---|---|
| **ISP core and image pipeline** | | | | | | | |
| ISP core / sensor bring-up | tx-isp-*.ko loads sensor + tuning bin | ✅ open driver boots, boot guard auto; day/night 10x without oops | ✅ bring-up stable; vendor-format isp-m0 | ✅ first open bring-up, ISP core = lifted vendor code | ✅ exposure readback live, ~45 empty CIDs wired up, unknown CIDs return -EINVAL | ✅ reference SoC; tuning gaps closed (RGB coefficients, AE ROI, SensorAttr) | ✅ runs the fully open stack from the `aperto` full OTA image (rootfs rev7, rmem 26M) since 2026-10-04; no oops; day/night and AE/AWB quality still untested |
| Reload / error handling (rmmod, stop/start) | known oops on reload (T21 open counter, stats DMA) | ✅+ boot guard auto; reload cycles not documented individually | ✅+ rmmod during stream rejected; 10x stop/start + 10x reload, 0 oops; all 53 user copies checked | ✅+ 10x stop/start + rmmod/insmod, 0 oops; cause of the old oops (stats DMA into freed memory) fixed | ✅+ 10x stop/start incl. kill -9, 10x reload, 0 oops; 2 out-of-bounds writes (2 KB/18 KB) fixed | ✅+ 10x reload with kill -9; vmalloc leak of 252 KB/cycle fixed; residual drift ~45 KB/cycle | ✅ rev2 image: 10/10 rmmod/insmod cycles OK, refcnt 0, 0 oops; kill -9 of the streamer recovers 3/3 (root cause: decompiled tuning-node helper overwrote .bss) [claude/t41-matrix-fixes] |
| Boot guard (protection against boot loops) | not present | ✅+ isp_open=auto | ✅ S10isp-guard + isp_open=auto: optional package in upstream thingino `aperto` (#1749, default off); active on the test cameras | ✅+ S10isp-guard, u-boot isp_open=manual\|auto\|off | ✅ S10isp-guard + isp_open=auto: optional package in upstream thingino `aperto` (#1749, default off); active on the test cameras | ✅ S10isp-guard + isp_open=auto: optional package in upstream thingino `aperto` (#1749, default off); active on the test cameras | — |
| **Exposure (AE)** | | | | | | | |
| AE control | vendor AE in the kernel | ✅ device-tested: ae_comp moves IT and gain (374→748 lines, gain 42→142), clamps | ✅ compact AE: max gain, max IT (ae_it_max_us now effective), line_us, scene IT limit; low-light AE at dusk untested | ✅+ AE lifted 1:1 from vendor (incl. ae_tune2), night flicker gone; a·b·b bug found | ✅ lifted vendor AE is the default (all-14/15); night test on cam-B: switches to night, AE regulates (IT 1200/1436 lines, analog gain 133/160), gain reported; backlight/highlight/AE comp act | ✅ reference SoC; 2 h 53 soak without errors | ✅ AE regulates correctly on cam-F (it was saturated at max gain only because the room was dark); day/night and AE/AWB quality across a full day still untested |
| AE compensation, backlight, highlight | IMP_ISP_Tuning_SetAeComp / Backlight / Highlight | ✅ device-tested: ae_comp 30/230 moves Y by −78/+143, highlight works; backlight cap absent | ✅ device-tested: ae_comp (IT 40→562 lines, gain 0→46); highlight has a small effect; backlight not tested (reflash pending) | ✅+ vendor dispatcher lifted; individual test not documented | ✅ device-tested with the lifted vendor AE: backlight 10 luma 68→112, highlight 10 →48, AE comp works (decided: lifted vendor AE becomes the default after a pending night-switch test in the dark) [claude/t23-matrix-gaps] | ✅ device-tested: ae_comp (Y +13/−21), backlight (Y +19), highlight (Y −13) | ✅ AE compensation reaches the ISP through OpenIMP (`AeScenceAttr.AeTargetComp`), device-tested: comp 2 lowers the target 65 → 1; wired in timps main. Backlight (BLC) and highlight (HLC) are not supported on T41: the driver now returns "not supported" instead of a silent success |
| Max gain, IT max, sensor FPS | MaxAgain/MaxDgain/AE_IT_MAX/SetSensorFPS | ✅ max_again and ae_it_max_us limit the AE | ✅ MaxAgain clamped, line_us=29 reported; ae_it_max_us limits the AE | ✅+ ae_it_max_us acts (claude/t21-ae-it-max 840a57ff; beyond vendor, the vendor ignores the RANGE block; the user decided to keep it): cam-D cap 2000 us gives IT 68 lines and dgain 19 to 63, cap 5000 us gives 172 lines, cap 0 returns to 1125 lines; max gain acts as before. Caveat: a 4th module reload in the same boot gave segfaults and a watchdog reboot (under investigation) | ✅ MaxAgain/MaxDgain, IT max, SetSensorFPS, additional ISP digital-gain stage; t23tune passed | ✅ EXPR setter, AE ROI, histogram edges | ✅ gain and exposure caps reach the ISP through OpenIMP (`AeExprInfo`: `AeMaxAGain` linear Q10, `AeMaxIntegrationTime` in sensor lines), device-tested: 8x cap holds 6.9x; timps wiring follows |
| Anti-flicker (50/60 Hz) | POWER_LINE / flicker dispatcher | ✅ device-tested: IT 748/675/896 lines for 60 Hz/50 Hz/off | ✅ device-tested: IT 843/1011/1012 lines for 60 Hz/50 Hz/off | ✅ lifted vendor dispatcher | ✅ device-tested: vendor AE 50/60/off IT 720/900/971; HLIL 720/600/711 | ✅ device-tested with 22 ms IT cap: IT 1000/900/750 lines off/50/60 Hz, gain compensates; daylight test pending | ✅ device-tested: off/50/60 Hz readback in isp-m0, AE integration time follows (2092/1575/1750 lines) |
| **Colour and image quality** | | | | | | | |
| White balance (AWB, presets, manual) | vendor AWB chain | ✅ device-tested: manual R/B gains, Cb/Cr −21..−28 | ✅ default simple AWB; daylight A/B vs vendor chain: gains 492/393 vs 488/395, neutral ROIs within 0.007, both converge < 4 s; artificial light sweep 2200-6500 K (2026-10-04, smart bulbs): AWB follows (CT estimate 2300/2300/2500/4100 K), 4000 K neutral (R/G 0.99), very warm light stays slightly warm (lower limit ~2300 K, typical) | ✅+ AWB lifted (10/10 scenes register-identical) + hysteresis + IR night freeze; dusk test open | ✅ [-all-13] daylight green cast fixed on claude/t23-day-color (bc70f10b), tested on cam-B in sunlight: neutral colours, WB gains kept (0x710/0x7c0); not flashed yet | ✅ device-tested: manual R/B gains + presets 3/4/7 read back; chroma follows (Cb/Cr), auto restores | ✅ black-picture incident not reproducible; timps does not call any WB function on T41 (only AWB attr in libimp) |
| CCM / LSC (lens shading) | CT-controlled | ✅ CCM/LSC read back in isp-m0 (0x13380480.., 0x380–0x39c); CCM now updated every frame like vendor (was frozen at init matrix in IR scenes), follows day/night; LSC bypassed by the jxh42 IQ bank (vendor-identical); per-CT sweep pending daylight [claude/t1x-ccm-lsc-iridix] | ✅ CCM/LSC read back in isp-m0; CCM follows day/night (mono at night), LSC enabled, strength 1024 day/3440 night; mesh mirror follows ISP hflip at mode reload (vendor-identical); per-CT sweep pending daylight [claude/t1x-ccm-lsc-iridix] | ✅+ CT-controlled CCM/LSC lifted; colour blotches gone (chroma sigma 30→6) | ✅ CCM follows the IQ bank (as vendor); daylight with vendor AE neutral (sun, 2026-10-04: R/G 0.94, B/G 0.93, no green or blue cast). Green cast fixed earlier (WB gains reset on every stream start, claude/t23-day-color); blue AWB flip with vendor AE fixed (GIB black level cleared by the stream-enable write, claude/t23-ae-awb-flip); LSC flip locked | ✅ CCM follows day/night (regs 0x5004-0x5018 differ), LSC LUT loaded and follows mode + flip; per-CT sweep not testable on a fixed scene | ❌ CCM is not supported on T41 (the driver returns "not supported" instead of a silent success) |
| Day/night switching (ISP side) | bank switch + mono matrix | ✅ 10 switches without oops; not re-tested after the drift fix | ✅ BCSH + Sinter/Temper re-sent as vendor does; Wyze day 128 / night 148/140 | ✅ night mono (chroma 0), gain stable instead of 6↔25 | ✅+ oops (wait queue) fixed, night mono, bank error → block bypass, user bypass persists | ✅ 13 switches in the 4.5 h soak | ✅ isp-m0 shows the run mode; forced switch test pending |
| IR cut / IR LED | via GPIO through timps/Thingino | ✅ device-tested: daynight night/day switches ircut and ir850, ISP follows | ✅ device-tested: as T10 | ✅ device-tested: as T10 | ✅ device-tested: timps auto night switches IR cut + ir850 + mono | ✅ device-tested: daynight night/day switches ircut + ir940 (no ir850 pin on this cam), ISP follows | ✅ device-tested: ircut + ir850 toggle; ISP mode not readable |
| Brightness / contrast / saturation / sharpness / hue | IMP_ISP_Tuning_Set* | ✅ device-tested: brightness (+150 Y), contrast, saturation, sharpness; hue cap absent | ✅ defaults 0x80; sharpness works (edge energy 13/230/700); Wyze image.sharpness=128 | ✅+ getters lifted; sharpness/contrast readable | ✅ brightness/contrast/saturation/hue act now (they were reset on every stream start); contrast/gain feedback: driver takes the low byte like the vendor, OpenIMP remembers the gain before sending (user contrast 100 stays) (claude/t23-bcsh-aeit-fix) | ✅ defaults 0x80 | ✅ brightness 255 → Y 211, contrast 0 → flat grey, saturation 0/255 chroma 0.1/7.1 (dark scene) [claude/t41-matrix-fixes] |
| Mirror / flip | SetHVFlip / sensor flip | ✅ device-tested: flipped image correlation 0.964 on both flips; isp-m0 Mirror/Flip line now shows the applied state (was always Enable) | ✅ vflip UV address + DMA overwrite fixed (pink stripes); isp-m0 Mirror/Flip line now shows the applied state (was always Enable) | ✅ flip dispatcher lifted; shvflip=1 | ✅ flip, Bayer re-sync, LSC flip locked | ✅ sc4336p vflip no longer reports an error [-all-13]; MSCA flip takes effect only at the next channel start, as with the vendor | ✅ sensor flip registers follow live (hflip → 0x022c=0x01, vflip → 0x0063=0x02, off → 0x00); picture check in daylight pending [claude/t41-matrix-fixes] |
| WDR / ADR / DRC | ADR/DRC/WDR paths in the vendor driver | ✅ isp-m0 WDR flag fixed (LINEAR 0x0e was printed as Enable) [claude/t10-t20-nr-wdr]; no DRC cap | ✅+ DRC strength drives auto Iridix ratio: Y 92/96/122, laplacian 555/558/656 at 0/128/255; isp-m0 WDR flag fixed [all-17] | ✅+ ADR lifted (40/40 emulator), DRC reaches the driver; day Y 120 instead of 235 | ✅+ dynamic ADR lifted from the vendor module (44/44 emulator-identical, claude/t23-adr-defog); DRC strength 0/255 visibly effective on cam-B; sc2336 has no WDR mode | ✅ WDR buffer lazy; AE1 (short frame) stub = vendor no-op without WDR sensor | ❌ WDR and DRC are not supported on T41 (the driver returns "not supported" instead of a silent success) |
| Defog | vendor block | ✅ vendor-identical: the jxh42 IQ bank bypasses Iridix (day and night), so DRC/defog have no picture effect – same as the vendor. Forcing Iridix on was measured (Y +1.4, edges +10 %) and rejected by the maintainer as not worth it. | ✅+ Iridix floor (no defog block in HW): 255 → Y +27, laplacian +106 [all-17] | ✅+ lifted, IRQ 21 registered (40/40 emulator) | ✅+ lifted incl. tisp_defog_soft_process (emulator-identical), IRQ 20 + process running on cam-B, defog strength works; 0xc bit 11 follows the bank like stock | ✅ device-tested: defog 255 (Y −10, laplacian +193) | ❌ defog is not supported on T41 (the driver returns "not supported" instead of a silent success) |
| Noise reduction (2DNR/3DNR, Sinter, Temper) | table-driven | ✅+ Sinter/Temper strength acts (vendor: no-op): temporal noise 7.11/2.91/1.51 at temper 0/128/255, survives day/night [claude/t10-t20-nr-wdr] | ✅+ Sinter/Temper take effect (vendor: no-op) [claude/t10-t20-nr-wdr + openimp claude/t20-nr-strength]: temper 0/64/128/200 → 0/42/85/132, sinter 0/17/35/69 at high gain; 128 = IQ; kept across day/night | ✅+ 2DNR/gain tracking repaired; Sinter/Temper strength now takes effect (vendor ignores it) [-all-13] | ✅ gain index now log2 (before: noise reduction too strong from 2x), Sinter non-compounding, sharpness/DPC follow the bank | ✅ vendor-identical: SDNS H-S regs 0→0, 255→15 (OEM cap 16); temper 0: temporal std 4.60 vs 1.64; sinter effect small by OEM design | ⚠️ 2D noise reduction (sinter) works through OpenIMP (`Module_Ratio` index 0), device-tested: sinter 255 cuts wall noise from ~7 to ~1; wired in timps main. 3D noise reduction (temper, index 1) shows no measurable effect yet and is being checked in the driver |
| DPC (defect pixels) | vendor block | ✅+ DPC strength via open driver (vendor: no-op): impulses 2846/2397/1784 at 0/128/255 [claude/t1x-beyond-vendor-ctrls, in all-17] | ✅+ impulses 6628/5153/3667 at 0/128/255 [all-17] | ✅+ m1 thresholds scaled like OEM T23; impulses 3857/3841/3498 [all-17] | ✅ follows the IQ bank as vendor, less night noise | ✅ vendor-identical: m1/m3 thresholds 0→(d1000,f5), 255→(d5,f1191); impulses −6 % (defect pixels only) | ❌ DPC is not supported on T41 (the driver returns "not supported" instead of a silent success) |
| Scene mode / colour effects (B/W, negative, sepia, vivid) | SetSceneMode/SetColorfxMode (no-op in the vendor on T21) | ✅+ NEGATIVE/BW work, scene presets act (TEXT laplacian +14 %) [all-17] | ✅ colorfx 0–3 set/get ok, sepia visible; scene ok | ✅+ B/W, vivid, negative work (confirmed with light on); getters return what was set [-all-13] | ✅ device-tested: B/W, vivid, negative work; invalid values give EINVAL (claude/t23-t31-scene-colorfx, not yet in an aggregate) | ✅ device-tested: as T23 (claude/t23-t31-scene-colorfx, not yet in an aggregate) | ❌ no control path on T41 (no cap, no libimp function) |
| Privacy mask (ISP hardware block) | 4 rectangles/channel, YUV fill | ✅ device-tested: both streams, green fill | ✅ device-tested: both streams, green fill | ✅ device-tested: both streams, green fill | ✅ device-tested: both streams, green fill | ✅ as vendor, follows mirror/flip; emulator 400/400 identical; cam-A black+red ok [-all-13] | ✅ device-tested on chn1 (chn0 value missing because the camera rebooted in that run) |
| Front crop / scaler level / CSC presets | SetFrontCrop, CSC, BLC | ✅ scaler device-tested (chn1 480x272, 25.0 fps); front crop / CSC: no control path | ✅ scaler device-tested (640x360 and 480x272, 15.0 fps); front crop / CSC: no control path | ✅ scaler device-tested (480x272, 24.9 fps); front crop / CSC: no control path | ✅ front crop via vendor path (960x540 crop ok); MASK -EINVAL as vendor (no stock handler) | ✅ BLC get, CSC presets 0–4 + user matrix, crop get/set, scaler level (tested on cam-A) | ❌ crop (I2D) still open: no control path |
| Rotation 90°/270° | software rotation (32×32 tiles) | — coerced to 0 ("unsupported on this SoC") | — coerced to 0 ("unsupported on this SoC") | — coerced to 0 ("unsupported on this SoC") | ✅ sub-stream rotation 90/270 works via the native encoder; main stream above 704x576 refused (software rotation); IMP_Encoder_YuvSetCrop implemented (host-tested only, timps does not call it) | ✅ 704×1280 correct, 9 ms/frame @15 fps, before OSD/IVS/encoder | ❌ open: hardware I2D path not enabled (timps build coerces 90/180/270 to 0) |
| **Video encoder and streams** | | | | | | | |
| H.264 | Helix (T20/T21/T23), AVPU (T31), NVPU (T10) | ✅ 720p 25 fps 1501 frames error-free; drift bug (margin added twice) fixed; own command list | ✅ soak 1 h 44, 156,517 frames/stream, 0 errors | ✅ main+sub+MJPEG, 25 fps; EMC scratch as vendor (1080p 996 KiB) | ✅+ native without helixd/OEM libimp: 2 h 34, 231,668 frames, 0 decode errors, ~6 % CPU | ✅ soak 2 h 53, 260,648 frames, 1030/1030 snapshots, 0 errors | ✅ cam-F with the fully open stack: main stream High profile 1920x1080 (earlier undecodable 1080p was the rmem exhaustion, fixed by the rmem best-fit), sub stream ok, no oops; repo docs: 2560×1440 H.264 verified on a different T41 device |
| H.265 / HEVC | T31/T41: AVPU; T10/T20/T21/T23: no HEVC hardware (Helix is H.264/JPEG only; Radix only on T30) – vendor libimp creates an empty channel that never encodes | — — no HEVC hardware; OpenIMP rejects PT_H265 with -1 and a clear log (vendor: silent empty channel) [claude/h265-reject] | — — no HEVC hardware; OpenIMP rejects PT_H265 with -1 and a clear log (vendor: silent empty channel) [claude/h265-reject] | — — no HEVC hardware; OpenIMP rejects PT_H265 with -1 and a clear log (vendor: silent empty channel) [claude/h265-reject] | — — no HEVC hardware; OpenIMP rejects PT_H265 with -1 and a clear log (vendor: silent empty channel) [claude/h265-reject] | ✅ real HEVC on AVPU (VPS/SPS/PPS, CABAC); 2×900 frames, 0 errors | ✅ AVPU HEVC path (as T31): 1080p H.265 decodes clean, a stuck AVPU job times out after 2 s and resets the core [claude/t41-h265, rev5 image] |
| JPEG / MJPEG / snapshot | hardware JPEG via vendor libimp/helixd | ✅ snapshots + MJPEG 25 B/5 s; JPEG buffer 1 MiB (−328 KiB) | ✅ HW JPEG, MJPEG 25 B/5 s with/without video consumer | ✅+ HW JPEG without vendor lib, 37 ms/job, snapshots 0.05–0.18 s; stripes with RST markers | ✅ HW JPEG 29 ms/job; q75 = IJG tables, size matches libjpeg (scene-driven) | ✅ HW JPEG, own MJPEG channel ok (24 B/5 s, before 0 bytes) | ✅ 1080p + 640x360 snapshots ok on cam-F with the open driver (claude/t41-gc5603-fix); MJPEG and the grey-JPEG TODO (T40/T41) not re-checked |
| Sub-stream / scaler (ch1 640×360) | scaler in the ISP | ✅ ch1 shows the full scene (DS1 horizontal ratio bug in the shared firmware fixed) | ✅ ch1 different scaler path, no regression | ✅ WebRTC main↔sub switching confirmed (rmem fix), 32+40 cycles | ✅ idle teardown bug (motion detection without frames) fixed | ✅ cam-A 25 fps ch0+ch1 4.5 h | ✅ 640x360 snapshot and MP4 ok on cam-F with the open driver (claude/t41-gc5603-fix) |
| Rate-control mode (CBR/VBR/FixQP/Capped*/SMART) | all modes in the vendor libimp | ✅+ OEM controller (OPENIMP_T10_RC=1) with the super-frame fix on by default (OPENIMP_T10_RC_SUPERFRM=0 = vendor-exact): at 1200 kbit/s 450→822, re-encodes 800→0, CPU 8.3→5.5 % (claude/t10-rc-superfrm); OEM rate controller is the default (device-tested on cam-E) | ✅ vendor-identical OEM controller is the default (claude/t1x-oem-rc-default-a13): CBR 1300 (P2 I-aware budget), VBR 1044, SMART 1019 at 1200 kbit/s; quality_lvl 0/6 → 1130/800 kbit/s, change_pos 50/100 → 850/1210 kbit/s, also live via /control; decode clean, 0 oops | ✅ vendor-identical T21 eprc is the default (0 oracle deviations); cam-D 1200 kbit/s: CBR 1326, VBR 1096, SMART 1071 [claude/eprc-t21-default]; eprc complete: SMART/CBR/VBR at 1200 kbit/s → 1090/1305/1042; runtime HSkip N=4 gives an IDR every 4 GOPs; decode clean, 0 oops [claude/eprc-complete]; scene-cut IDR not triggered by a day/night switch (vendor condition: scene class 5); MB-level RC ported (claude/eprc-mbrc 9e2bc3a, a8b483a: 0x400c0/0x400c4 per picture type like the vendor; device test running) | ✅ eprc controller: 60 s at 1200 kbit/s, decode clean: SMART 1141, CBR 1253, VBR 1255 (claude/eprc-t21-t23, not yet in an aggregate); MB-level RC ported (claude/eprc-mbrc 9e2bc3a, a8b483a: 0x400c0/0x400c4 per picture type like the vendor; device test running) | ✅ all modes via the vendor Allegro core (default): CBR 1210 kbit/s at 1200 target and 2973 at 3000 (legacy controller 1511 / 3786, +26 % with large peaks); VBR 1163, CappedVBR 1177, CappedQuality 1174 at 1200; decode clean, 0 oops [claude/t31-allegro-cbr]. No filler NAL is written (filler=0 in the logs also at 3000), so in practice there was no difference. | ✅ bitrate 400/1200/3000 → 518/1195/2777 kbit/s (30 s each) [claude/t41-cbr-overshoot] |
| RC parameters (QP steps, staticTime, changePos, qualityLvl, I bias) | via IMP attr | ✅ device-tested: min/max QP and I bias read back in the encoder RC; quality_lvl/change_pos only in the video readback | ✅ readback returns the vendor-clamped values (staticTime 1, changePos 50, qualityLvl 0, QP steps 2/2 when the app passes 0) (claude/rc-modes) [-all-13] | ✅ readback as T20 [-all-13] | ✅ uses the vendor CreateChn clamps (1/50/2/2); parameters reach the native encoder (changePos min 50); app value 0 = vendor default 3/15/2/80 [-all-13] | ✅ T31 defaults like the vendor (max QP 48, max bitrate 4/3, ...) [-all-13] (claude/rc-modes-2) | ⚠️ device-tested: min/max QP live and effective; quality_lvl/change_pos/i_bias restart-only (readback in video block) |
| OSD: text, bitmap, rectangle, line, cover | IPU OSD / vendor libimp | ✅ device-tested: 4 items (text, uptime, logo) on both streams | ✅ text/bitmap/lines/rectangles on both streams, clipping, 0 oops (IPU OSD hook) | ✅ IPU OSD hook as T20; rect/line/bitmap | ✅ device-tested: all 4 items, text edit | ✅ IPU OSD; lines/rectangles; rotation: no OSD clamp in timps | ✅ works on cam-F with the open stack (claude/t41-libimp): PIC/COVER via IPU, text/line/rect on CPU; clock, name and logo visible; kernel oops from the rmem cache flush fixed (T41 kernel expects a physical address) |
| IVS / motion detection | vendor IVS (T20/T21/T30 initially "always no motion" in the open stack) | ✅ device-tested: no event in 12 s idle, full-grid events on brightness steps (2 of 6) | ✅+ 6/6 events, 0 false alarms; CPU 4.1→2.7 % with motion | ✅ real frame-diff IVS ported | ✅ motion active again after sub-stream idle (WebUI grid) | ✅+ sub-stream default: ~85 % less IVS CPU (compared with vendor libimp) | ✅ works on cam-F with a feeder thread (claude/t41-libimp), no more 10-s stalls; short ~1.2-s gaps still being looked at |
| Frame source / VBM pool | vendor pools | ✅ device-tested: chn0, chn1 and snapshot concurrent, 148/147 frames decoded | ✅ snapshot debounce no longer polls the JPEG encoder: with 1 snapshot/s on both channels chn0 14.4 / chn1 15.0 fps (was 11.2/14.3); sub-stream height 270 is rounded to 272 with a warning (was: scaler hang) [openimp claude/openimp-t20-jpeg-align, timps claude/timps-jpeg-idle-nopoll] | ✅+ pool parked/reused (release at idle broke later allocations) | ✅ frames also recycled for callback pools | ✅ device-tested: concurrent streams ok | ✅ device-tested: concurrent streams ok |
| **Audio (documentation only, no tests)** | | | | | | | |
| Audio input (AI) | IMP_AI | ✅ device-tested: AAC 16 kHz mono 32 kbit/s in RTSP + fMP4, volume/gain/mute work, gain clamp 31 (ambient levels, gain 31 reading noisy) | ✅ device-tested: AAC 16 kHz mono 32 kbit/s in RTSP + fMP4, volume/gain/mute work, gain clamp 31 (little gain effect, -75 dB floor) | ✅ device-tested: AAC 16 kHz mono 32 kbit/s in RTSP + fMP4, volume/gain/mute work, gain clamp 31 | ✅ microphone in the RTSP stream (AAC 16 kHz), real room-noise signal (mean -64 dB, peak -46 dB; T31 reference -57/-43 dB); no speech test | ✅ device-tested: AAC 16 kHz mono 32 kbit/s in RTSP + fMP4, volume/gain/mute work, gain clamp 31; also PCMU 8 kHz via RTSP (not in fMP4) | ✅ microphone in the RTSP stream (AAC 16 kHz), real room-noise signal (mean -64 dB, peak -53 dB); no speech test |
| Audio output (AO, speaker) | IMP_AO | ? not tested on purpose: no audio playback on the shared test cameras | ? not tested on purpose: no audio playback on the shared test cameras | ? not tested on purpose: no audio playback on the shared test cameras | ? not tested on purpose: no audio playback on the shared test cameras | ✅ volume/mute work, whole OSS fragments (tested on cam-A) | ? not tested on purpose: no audio playback on the shared test cameras |
| Echo cancellation (AEC) | IMP_AI_EnableAec | ? changelog mentions only T31/T23 | ? device test open | ? device test open | 🔧 implemented (WebRTC AECM, driver reference offset); device test needs speaker playback, which is not allowed on the test cameras | ✅+ real AECM: echo −18 dB, ERLE 44 dB (loopback); before: fake success | ? |
| **Memory, size, load** | | | | | | | |
| libimp size (code + data) | T21 ~1.0 / T23 ~1.26 / T31 ~1.05 MB | ✅+ 626,032 B (~0.6 MB) | ✅+ 594 KB (was 694 KB; claude/openimp-size, gc-sections) | ✅+ ~0.5 MB | ✅+ 726 KB (was 774 KB; native, no helixd; claude/openimp-size) | ✅+ ~0.57 MB | ✅+ 465,728 B (~0.47 MB) |
| Kernel module size | T21 616 / T23 857 / T31 829 KB | ✅ 731 KB stripped (was 770; claude/open-tx-isp-size2) | ✅ 736 KB stripped (was 775; claude/open-tx-isp-size2) | ✅+ 452 KB (was 760 KB; vendor 616 KB), RAM unchanged (claude/t21-size-awb-opt, 452 KB since all-17) | ✅+ 622 KB stripped (was 1,047; vendor 857; claude/open-tx-isp-size2, device-tested on cam-B) | ✅+ 711 KB stripped (was 859; vendor 829; claude/open-tx-isp-size2, device-tested on cam-A) | ✅ tx_isp_t41 731,488 B (vendor size not measured) |
| Video memory (rmem) / MemFree | T21 ~23 MB for main+sub+JPEG | ✅ JPEG buffer −328 KiB; 40 KiB rootfs reserve in the image | ✅ MemFree 47 MB of 91 MB | ✅+ free with main+sub+MJPEG 2.76 MB (vendor ≈1.2) | ✅+ JPEG shares bitstream −1.44 MB; main window 2 MiB | ✅ drift per reload 460→45 KB | ✅ rmem 26 MB in the current image (was 30): stream buffers sized like the vendor (1080p 0.95 MB), capture buffers from the bottom and the rest from the top so idle/restart cycles no longer fragment rmem; 5 idle/restart cycles clean. OOM with three parallel streams and `AddSensor` EBUSY after an OOM kill are still open |
| Reference-frame sharing (BUF_SHARE_CFG) | vendor T23: used by default (<=1080p); vendor T21: off by default | — hardware missing | — hardware missing | ✅+ works (claude/t23-ref-ring) [-all-13]: P-frames 150-300 B in a static scene on cam-D, no artefacts; saves ~1.5 MB video memory at 1080p; on by default (<=1920x1088), OPENIMP_REF_SHARE=0 disables | ✅+ works (claude/t23-ref-ring) [-all-13]: no artefacts on cam-B, P-frame sizes equal or smaller than without the ring; saves ~1.5 MB at 1080p; on by default (<=1920x1088, like the vendor), OPENIMP_REF_SHARE=0 disables | — hardware missing | ? not wired in timps (SetbufshareChn exists in libimp); not tested |
| CPU load (documented figures) | vendor comparison values mostly missing | ✅ timps 5–9 % with 2 streams at 25 fps (17 % momentary with 1 stream) | ✅ timps 2.7 % with motion; OEM AWB +4 % | ✅+ lifted AWB at 0.95x vendor instructions (was 1.41x), output bit-identical; cam-D isp_fw_process -10 % | ✅ ~6–7 % for 2 streams 25 fps | ✅ rotation 9 ms/frame @15 fps | ✅+ timps ~10–12 % with our libimp vs ~27 % with the vendor libimp (momentary values) |
| **Stability, helper libraries, telemetry** | | | | | | | |
| Kernel soc_vpu / Helix hardening | busy-wait up to 200 ms, unbounded waits | ✅+ error IRQ ends the wait immediately (patch 0099) | ✅+ patch 0099 | ✅+ patches 0095–0099 (bounded waits, pointer checks, register ioctl restricted to the VPU window) | ✅+ patches 0098–0105 (0102 ignores the residual Helix interrupt, status 0x100 after a finished job), merged upstream in thingino `aperto` (#1748, #1752); 5 h soak on the `aperto` images: 0 VPU errors on all cameras | — SOC_VPU not built | ✅ device-tested: 5 min, 3 RTSP clients plus snapshots, 0 VPU/AVPU errors; main stream High@5.1 after the rmem fix (rmem was exhausted and the main channel fell back to a broken software encoder); 5 idle/restart cycles clean |
| AVPU kernel driver (T31/T40/T41) review | – | — other VPU | — other VPU | — other VPU | — other VPU | ✅+ DeepSeek review verified; fixes on claude/avpu-review-fixes (minor-number leak, use-after-free on sysfs unbind, uninitialised dma-buf list mutex, flush clamp); kernel patch 0100 validates the rmem flush ioctl (invalid direction no longer crashes the kernel); tested on cam-A: reload, kill -9, 5× rmmod/insmod, 0 oops [-all-13] | ✅ same module family runs on cam-F with the open driver (5 min, 3 RTSP clients plus snapshots, 0 AVPU errors). ioctl stack-overflow hardening (unknown or legacy commands now return -ENOTTY) is in open-tx-isp, not yet device-tested; test plan in `driver/t41/README` |
| Long-term stability / hangs | – | ✅ rmmod/insmod 5× with streaming, 0 oops; the earlier 'csi clock -22' oops came from a module built against the T20 kernel tree, the T10 build now refuses that [claude/t10-reload-safe] | ✅ 1 h 44 soak, 0 errors | ✅ uptime 1:54 at the test, 0 oops | ✅ the frequent Helix frame drops had a fixed cause (residual interrupt 0x100 treated as an error by the bounded-wait kernel patch): 60 min 0 errors after the fix, 5 h soak on the `aperto` images 0 encoder errors. Still listed open: a sporadic single Helix encode error (errno 5). Cold-start snapshot 503 on a second channel (stale MSCA FIFOs): fix `msca_fifo_rearm` gave 260 cold-start cycles without a failure (before ~1-7 %), soak pending before it enters `next` | ✅ 4.5 h soak ok | ⚠️ 5-min stress without reboot earlier; still open: OOM with three parallel streams, `AddSensor` EBUSY after an OOM kill; module reload 10/10 clean |
| Helper libraries libalog / libsysutils | shipped with vendor images | ✅+ removed | ✅+ removed (image tested) | ✅+ removed | ✅+ removed; also no helixd/vendor libimp | ✅+ removed | ✅+ removed (cam-F runs the open stack without both libs) |
| Tuning getters / readback | IMP_ISP_Tuning_Get* | ✅ device-tested: isp-m0 Brightness/Contrast/Saturation/Sharpness/Antiflicker readback equals the set values | ✅ SDK control IDs, pointer semantics as vendor, isp-m0 | ✅+ getters return what was set [-all-13] (vendor: scene/colorfx/Sinter DNS no-op) | ✅ expr/EV/TotalGain live, SensorAttr 20-byte layout, vendor isp-m0 | ✅ SensorAttr, WaitFrame per frame, isp-w02 counter | ✅ isp-m0 in vendor layout: run mode, BCSH, flip mode, anti-flicker, AE |
| Encoder telemetry / diagnostics | IMP_Encoder_Query/ChnStat | ✅+ RC log line, clamp warning | ✅+ ditto | ✅+ ditto | ✅+ ditto | ✅+ ditto | ⚠️ isp-m0 shows AE/AWB/anti-flicker/flip; encoder rc readback ok; query counters stayed 0 under load; other /proc/jz/isp nodes unreadable |

## Missing / incomplete functions (vendor IMP/SU API, per SoC)

As of: 2026-10-05 (late evening). Docs-only addition; the matrix above is unchanged. Extended later the same night with a legend per table, a progress summary per SoC and full per-area function lists.

State of the work: OpenIMP branch `claude/agg-25` (`claude/agg-26` = agg-25 plus the T21 OSD first-JPEG fix) plus the branches listed below, as pushed on 2026-10-05; cells saying agg-25 mean it is in that branch; open-tx-isp `claude/agg-25` and the branches named in the notes. Source of the function list and of the base classes: the audit `NOT_CONNECTED_2026-10-05.md` (OpenIMP/open-tx-isp `agg-24`, built libimp.so/libsysutils.so, vendor header sets: T10/T20 331 functions, T21 330, T23 704 incl. `_Sec`/`MultiCamera_` variants, T31 405, T41 445). Rows are ALL vendor IMP_*/SU_* functions of the audit (Get/Set pairs share a row). Functions that are class REAL in the audit and have no tracked device or host test show as done (?). Per area the first table shows only rows with at least one gap (cache-only, stub, error, missing, ?); the collapsible full list below it shows every row.

Later work is applied from the commit messages and from the device-test notes of 2026-10-05; **nothing here was re-measured**. Where a fix lives in a branch that is not part of `agg-25` the note says so (`claude/t1x-roi`, `claude/t23t31-cacheonly`, `claude/t23-enc-rest`, `claude/t41-isp-round2`, `claude/t23-awb-runtime`). The image-effect device test `claude/imgfx-tool` (one picture per function) found the T21 no-ops below.

T10 uses the T20 userspace build (and the T20 SDK tuning code in the driver), so its cells mirror T20 unless the note says otherwise; fixes whose commit names only T20/T21 are marked for T10 as shared build, device effect unverified. T23 rows cover the base function: the `_Sec` and `MultiCamera_` variants were folded into it (audit class = worst of the three); the after-audit fixes were made for the base function and are unverified for the variants. T30/T40 are not tabled (no device, no build in the audit).

### Legend

What every cell value means (the same short legend is repeated above each table below):

| Cell | Meaning |
|---|---|
| done (dev) | implemented and device-tested on that SoC (a camera run exists; see the note) |
| done (host) | implemented, host tests only (unit/layout/fake-device tests); not run on a camera yet |
| done (?) | really connected per the static audit (reaches the driver/hardware or is a real userspace implementation), but nobody tracked a device test for this call |
| vendor no-op | the vendor stack itself does nothing visible (or the measurement could not show an effect); OpenIMP matches that |
| cache-only | the value is only stored and read back; nothing is applied |
| stub | returns 0 (or the driver answers 0) without any effect |
| error | exported but fails (returns -1/ENOTSUP, driver -EINVAL/-EPERM/-EOPNOTSUPP) or has a known defect |
| missing | symbol not exported by the open libimp/libsysutils |
| n.a. (not in vendor API) | the function does not exist in that SoC's vendor API (header set of that SoC); not "unsupported". Shown as `n.a.` in the cells |
| ? | cause or state not determined |
| ► | leading mark on the function name: at least one streamer (timps/prudynt/raptor) uses it and its cell on that SoC is a gap |
| † | streamer usage derived from source only (no binary of that streamer was built for that SoC) |

**Used by** (column added 2026-10-05): which streamers import the function, determined from real imports, not guesses. `timps`: `nm -D --undefined-only` of the `timpsd` binaries of the per-camera builds (T10 secuplug, T20 wyze cam2 + campan1 (identical import set), T21 victure pc420 + the vendor-stack build, T23 galayou, T31 wuuk, T41 vanhua) incl. weak imports; the timps source has no dlsym use, so nothing is hidden behind dlsym. `prudynt`, `raptor`: T23 is binary-verified (nm of `prudynt`, `rvd`, `rad` of the T23 build, raptor-hal linked in statically); for the other SoCs the sources were run through the C preprocessor with `-DPLATFORM_Txx` and the vendor header set of that SoC and the identifiers were collected. That method reproduces the T23 binary imports exactly (19/19 prudynt, 51/51 raptor rows), so it is trusted, but a name marked **†** is **source-only** (no T10/T20/T21/T31/T41 binary of that streamer was built). Without a †, the entry is binary-verified. `–` = none of the three imports it on any SoC where the function exists. `T21: ...; T23: ...` = the set differs per SoC (only SoCs that have the function). Variants (`_Sec`, `MultiCamera_`) count for the base function. **Bold** used-by text and a leading `►` on the function name = a priority row: at least one streamer imports it and its cell on that SoC is a gap (missing, error, cache-only, stub or ?). `vendor no-op` and `done (...)` rows are not counted as priority gaps.


### Progress per SoC

Counts are per vendor function of that SoC (T23 folded: base function = one; Get and Set count separately). "gaps" = cache-only + stub + error + missing; "vendor no-op" is not a gap; "audit gaps" = the same sum in the audit before the work of 2026-10-05 (T23 unfolded, about 3x per base function). "done %" = done (dev) + done (host) + done (?) / vendor fns. Bar: █ done (dev), ▓ done (host), ▒ done (?) / ?, ○ vendor no-op, ░ gaps (40 characters per SoC).

| SoC | vendor fns | done (dev) | done (host) | done (?) | ? | vendor no-op | cache-only | stub | error | missing | **gaps now** | audit gaps (before) | done % |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| T10 | 331 | 0 | 47 | 231 | 2 | 0 | 4 | 0 | 3 | 44 | **51** | 100 | 84.6 % |
| T20 | 331 | 4 | 47 | 231 | 0 | 0 | 2 | 0 | 3 | 44 | **49** | 100 | 85.2 % |
| T21 | 330 | 0 | 38 | 252 | 0 | 6 | 4 | 0 | 5 | 25 | **34** | 77 | 87.9 % |
| T23 | 441 | 2 | 53 | 346 | 0 | 8 | 12 | 0 | 14 | 6 | **32** | 212 | 90.9 % |
| T31 | 405 | 6 | 23 | 338 | 0 | 4 | 4 | 4 | 1 | 25 | **34** | 65 | 90.6 % |
| T41 | 445 | 0 | 37 | 218 | 0 | 0 | 8 | 2 | 48 | 132 | **190** | 227 | 57.3 % |

```
T10  ▓▓▓▓▓▓▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒░░░░░░
T20  ▓▓▓▓▓▓▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒░░░░░░
T21  ▓▓▓▓▓▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒○░░░░
T23  ▓▓▓▓▓▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒○░░░
T31  █▓▓▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒○░░░
T41  ▓▓▓▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒░░░░░░░░░░░░░░░░░
```

### Streamer-relevant gaps per SoC (priority)

Gaps (cache-only, stub, error, missing, ?) of functions that at least one of timps / prudynt / raptor imports on that SoC; derived as described under "Used by" in the legend. Raptor and prudynt entries for T10/T20/T21/T31/T41 are source-only (no binary built); timps and T23 are binary-verified.

| SoC | gaps used by at least one streamer | timps | prudynt | raptor | functions (state; streamers) |
|---|---|---|---|---|---|
| T10 | **1** | 0 | 1 | 1 | `Encoder_Get/SetJpegeQl` (cache-only; prudynt+raptor) |
| T20 | **1** | 1 | 1 | 1 | `Encoder_Get/SetJpegeQl` (cache-only; prudynt+raptor+timps) |
| T21 | **4** | 1 | 1 | 4 | `Encoder_Get/SetChnDenoise` (error; raptor), `Encoder_Get/SetH265TransCfg` (cache-only; raptor), `Encoder_Get/SetJpegeQl` (cache-only; prudynt+raptor+timps), `Encoder_Get/SetQpgMode` (error; raptor) |
| T23 | **6** | 0 | 1 | 6 | `ISP_Tuning_GetBlcAttr` (error; raptor), `ISP_Tuning_SetAutoZoom` (error; raptor), `ISP_Tuning_SetScalerLv` (error; raptor), `ISP_Tuning_Get/SetAwbClust` (cache-only; raptor), `ISP_Tuning_Get/SetAwbCtTrend` (cache-only; raptor), `AI_SetHpfCoFrequency` (cache-only; prudynt+raptor) |
| T31 | **4** | 0 | 2 | 4 | `ISP_Tuning_DisableMovestate` (stub; raptor), `ISP_Tuning_EnableMovestate` (stub; raptor), `Encoder_SetbufshareChn` (stub; prudynt+raptor), `AI_SetHpfCoFrequency` (cache-only; prudynt+raptor) |
| T41 | **27** | 0 | 7 | 24 | `ISP_Get/SetFrameDrop` (error; raptor), `ISP_Get/SetISPBypass` (missing; prudynt), `ISP_Tuning_Get/SetModuleControl` (error; raptor), `ISP_Tuning_SetAutoZoom` (error; prudynt), `ISP_Tuning_SetMaskBlock` (error; raptor), `ISP_Tuning_SetScalerLv` (error; raptor), `ISP_Tuning_SwitchBin` (missing; prudynt), `ISP_WDR_ENABLE` (error; raptor), `ISP_WDR_ENABLE_GET` (error; raptor), `ISP_Tuning_Get/SetAfWeight` (error; raptor), `Encoder_SetChnMaxPictureSize` (cache-only; raptor), `Encoder_SetbufshareChn` (stub; prudynt+raptor), `FrameSource_Get/SetChnFifoAttr` (cache-only; prudynt+raptor), `FrameSource_Get/SetDelay` (error; raptor), `FrameSource_Get/SetFrameDepth` (cache-only; prudynt+raptor), `FrameSource_Get/SetI2dAttr` (error; raptor), `FrameSource_Get/SetMaxDelay` (error; raptor), `FrameSource_Get/SetPool` (cache-only; raptor), `FrameSource_GetTimedFrame` (error; raptor), `ISP_Tuning_CreateOsdRgn` (error; raptor), `ISP_Tuning_DestroyOsdRgn` (error; raptor), `ISP_Tuning_SetOsdPoolSize` (stub; raptor), `ISP_Tuning_SetOsdRgnAttr` (error; raptor), `ISP_Tuning_ShowOsdRgn` (error; raptor), `AI_SetHpfCoFrequency` (cache-only; prudynt+raptor), `DMIC_*` (error; raptor), `DMIC_DisableAecRefFrame` (error; raptor) |

### ISP tuning

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by | Note |
|---|---|---|---|---|---|---|---|---|
| `ISP_Get/SetCsccrMode` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `ISP_Get/SetFrameDrop`** | n.a. | n.a. | n.a. | done (?) | done (?) | error | **T41: raptor†** | T41: ENOTSUP stub (returns -1) |
| **► `ISP_Get/SetISPBypass`** | n.a. | n.a. | n.a. | n.a. | n.a. | missing | **prudynt†** |  |
| `ISP_Get/SetInternalChnAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_LDC_Get/SetAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_LDC_INIT` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_RAW_RwControl` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_SET_GPIO_INIT_OR_FREE` | n.a. | n.a. | n.a. | done (?) | done (?) | missing | – |  |
| `ISP_SET_GPIO_STA` | n.a. | n.a. | n.a. | done (?) | done (?) | missing | – |  |
| `ISP_SetFixedContraster` | n.a. | n.a. | n.a. | done (host) | stub | n.a. | – | T23: driver routes 0x8000102 (agg-25); T31: (void)mode; return 0 |
| `ISP_SetVicDoneCbFunc` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_StartNightMode` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `ISP_Tuning_DisableMovestate`** | done (?) | done (?) | done (?) | vendor no-op | stub | n.a. | **raptor†** | T23: stock driver also no-op (OEM-same); T31: driver answers 0 without effect |
| **► `ISP_Tuning_EnableMovestate`** | done (?) | done (?) | done (?) | vendor no-op | stub | n.a. | **raptor†** | T23: stock driver also no-op (OEM-same); T31: driver answers 0 without effect |
| `ISP_Tuning_Get/SetDrawBlock` | n.a. | n.a. | n.a. | error | n.a. | missing | – | T23: driver rejects 0x8000180 (-EINVAL) |
| `ISP_Tuning_Get/SetISPHVflip` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| **► `ISP_Tuning_Get/SetModuleControl`** | n.a. | n.a. | done (host) | done (?) | done (?) | error | **T31/T41: raptor†** | T21: tuning 0x80000e2 (agg-25); T41: driver rejects 0x8000072 (-EINVAL) |
| `ISP_Tuning_Get/SetStatisConfig` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_Get/SetTmoCurve` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_Get/SetWDRAttr` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| `ISP_Tuning_Get/SetWdrOutputMode` | n.a. | n.a. | n.a. | n.a. | n.a. | error | – | T41: driver has no stock handler, refuses (claude/t41-isp-round2) |
| `ISP_Tuning_GetAutoZoom` | n.a. | n.a. | n.a. | error | n.a. | error | – | T23: driver rejects 0x80000e8 (-EINVAL); T41: driver has no stock handler, refuses (claude/t41-isp-round2) |
| **► `ISP_Tuning_GetBlcAttr`** | n.a. | n.a. | n.a. | error | done (?) | n.a. | **raptor†** | T23: driver rejects 0x80000a5 (-EINVAL) |
| `ISP_Tuning_GetHVFlip` | n.a. | n.a. | n.a. | done (?) | done (?) | missing | T23/T31: raptor† |  |
| `ISP_Tuning_GetMaskBlock` | n.a. | n.a. | n.a. | done (host) | n.a. | missing | – | T23: 0x8000183 in claude/t23-awb-runtime only (not in agg-25) |
| `ISP_Tuning_SaveAllParam` | missing | missing | missing | n.a. | n.a. | n.a. | – |  |
| `ISP_Tuning_SetAntiFogAttr` | missing | missing | missing | n.a. | n.a. | n.a. | – |  |
| **► `ISP_Tuning_SetAutoZoom`** | n.a. | n.a. | n.a. | error | done (host) | error | **T23: raptor**; T31: prudynt†, raptor†; **T41: prudynt†** | T23: driver rejects 0x80000e8 (-EINVAL); T31: programs scaler/crop, refuses size change (agg-25); T41: driver has no stock handler, refuses (claude/t41-isp-round2) |
| `ISP_Tuning_SetDPStrength` | missing | missing | missing | n.a. | missing | n.a. | – |  |
| **► `ISP_Tuning_SetMaskBlock`** | n.a. | n.a. | n.a. | done (host) | n.a. | error | **T41: raptor†** | T23: 0x8000183 in claude/t23-awb-runtime only (not in agg-25); T41: driver has no handler, fails with -EPERM since agg-25 (was silent 0); vendor behaviour unverified |
| `ISP_Tuning_SetMeshShadingScale` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| **► `ISP_Tuning_SetScalerLv`** | n.a. | n.a. | n.a. | error | done (?) | error | **raptor†** | T23: driver rejects 0x80000e9 (-EINVAL); T41: driver has no handler, fails with -EPERM since agg-25 (was silent 0); vendor behaviour unverified |
| `ISP_Tuning_SetTmoFaceae` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `ISP_Tuning_SwitchBin`** | n.a. | n.a. | n.a. | error | n.a. | missing | **T41: prudynt†** | T23: driver rejects 0x8000185 (-EINVAL) |
| `ISP_Tuning_WaitFrameDone` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `ISP_WDR_ENABLE`** | n.a. | n.a. | n.a. | n.a. | done (?) | error | **raptor†** | T41: ENOTSUP stub (returns -1) |
| **► `ISP_WDR_ENABLE_GET`** | n.a. | n.a. | n.a. | n.a. | n.a. | error | **raptor†** | T41: ENOTSUP stub (returns -1) |
| `ISP_WDR_OPEN` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |

<details><summary>All 107 rows of this area (38 with a gap)</summary>

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by | Note |
|---|---|---|---|---|---|---|---|---|
| `ISP_AddSensor` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Close` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `ISP_DelSensor` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_DisableSensor` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_DisableTuning` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `ISP_EnableSensor` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_EnableTuning` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Get/SetCsccrMode` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Get/SetDefaultBinPath` | n.a. | n.a. | n.a. | done (?) | done (?) | done (?) | raptor† | audit: reaches the driver/kernel; userspace implementation |
| **► `ISP_Get/SetFrameDrop`** | n.a. | n.a. | n.a. | done (?) | done (?) | error | **T41: raptor†** | T41: ENOTSUP stub (returns -1) |
| **► `ISP_Get/SetISPBypass`** | n.a. | n.a. | n.a. | n.a. | n.a. | missing | **prudynt†** |  |
| `ISP_Get/SetInternalChnAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Get/SetSensorRegister` | done (?) | done (?) | done (?) | done (?) | done (?) | done (host) | raptor† | T41: claude/t41-isp-round2 (not in agg-25); not device-tested |
| `ISP_LDC_Get/SetAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_LDC_INIT` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Open` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_RAW_RwControl` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_SET_GPIO_INIT_OR_FREE` | n.a. | n.a. | n.a. | done (?) | done (?) | missing | – |  |
| `ISP_SET_GPIO_STA` | n.a. | n.a. | n.a. | done (?) | done (?) | missing | – |  |
| `ISP_SetCameraInputMode` | n.a. | n.a. | n.a. | done (?) | n.a. | n.a. | – | audit: reaches the driver/kernel |
| `ISP_SetFixedContraster` | n.a. | n.a. | n.a. | done (host) | stub | n.a. | – | T23: driver routes 0x8000102 (agg-25); T31: (void)mode; return 0 |
| `ISP_SetStreamOut` | n.a. | n.a. | n.a. | done (?) | n.a. | n.a. | – | audit: reaches the driver/kernel |
| `ISP_SetSwitchgpio` | n.a. | n.a. | n.a. | done (host) | n.a. | n.a. | – | T23: ioctl (claude/t23t31-cacheonly, not in agg-25) |
| `ISP_SetVicDoneCbFunc` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_StartNightMode` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_StreamCheck` | n.a. | n.a. | n.a. | done (?) | n.a. | n.a. | – | audit: reaches the driver/kernel |
| **► `ISP_Tuning_DisableMovestate`** | done (?) | done (?) | done (?) | vendor no-op | stub | n.a. | **raptor†** | T23: stock driver also no-op (OEM-same); T31: driver answers 0 without effect |
| `ISP_Tuning_EnableDRC` | n.a. | n.a. | n.a. | done (?) | done (host) | n.a. | raptor† | T31: wired to the driver (agg-25) |
| `ISP_Tuning_EnableDefog` | n.a. | n.a. | n.a. | done (?) | done (?) | n.a. | raptor† | audit: reaches the driver/kernel |
| **► `ISP_Tuning_EnableMovestate`** | done (?) | done (?) | done (?) | vendor no-op | stub | n.a. | **raptor†** | T23: stock driver also no-op (OEM-same); T31: driver answers 0 without effect |
| `ISP_Tuning_Get/SetAntiFlickerAttr` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetBacklightComp` | n.a. | n.a. | n.a. | done (?) | done (?) | n.a. | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetBcshHue` | n.a. | n.a. | n.a. | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetCCMAttr` | n.a. | n.a. | n.a. | done (?) | done (?) | done (host) | T41: raptor† | T41: vendor 1.2.6 error ladder (claude/t41-isp-round2, not in agg-25); not device-tested |
| `ISP_Tuning_Get/SetColorfxMode` | done (?) | done (?) | done (?) | n.a. | n.a. | n.a. | timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetContrast` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetCsc_Attr` | n.a. | n.a. | n.a. | done (?) | done (?) | n.a. | raptor† | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetDPC_Strength` | n.a. | n.a. | n.a. | done (?) | done (?) | n.a. | raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetDRC_Strength` | n.a. | n.a. | done (?) | done (?) | done (?) | n.a. | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetDefog_Strength` | n.a. | n.a. | n.a. | done (?) | done (?) | n.a. | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetDrawBlock` | n.a. | n.a. | n.a. | error | n.a. | missing | – | T23: driver rejects 0x8000180 (-EINVAL) |
| `ISP_Tuning_Get/SetFrontCrop` | n.a. | n.a. | n.a. | done (?) | done (host) | n.a. | raptor† | T31: wired to driver 0x80000e3 / 0x80000e7 (agg-25); T10/T20/T21 crop device-tested but not a vendor call there |
| `ISP_Tuning_Get/SetGamma` | done (?) | done (?) | done (?) | done (dev) | done (?) | n.a. | prudynt†, raptor† | T23: applied at once (beyond stock); curve test, falling curve rejected, restore ok on cam-B 2026-10-05 |
| `ISP_Tuning_Get/SetGammaAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | done (host) | prudynt†, raptor† | T41: vendor 1.2.6 error ladder (claude/t41-isp-round2, not in agg-25); not device-tested |
| `ISP_Tuning_Get/SetHVFLIP` | n.a. | n.a. | n.a. | done (?) | done (?) | done (?) | T23/T31: raptor†; T41: raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetHiLightDepress` | done (?) | done (?) | done (?) | done (?) | done (?) | n.a. | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetISPCSCAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | done (host) | – | T41: vendor 1.2.6 error ladder (claude/t41-isp-round2, not in agg-25); not device-tested |
| `ISP_Tuning_Get/SetISPCustomMode` | n.a. | n.a. | n.a. | done (?) | done (host) | n.a. | raptor† | T31: wired to driver 0x80000e3 / 0x80000e7 (agg-25); T10/T20/T21 crop device-tested but not a vendor call there |
| `ISP_Tuning_Get/SetISPHVflip` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| `ISP_Tuning_Get/SetISPHflip` | done (?) | done (?) | done (?) | done (?) | done (?) | n.a. | prudynt†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetISPRunningMode` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetISPVflip` | done (?) | done (?) | done (?) | done (?) | done (?) | n.a. | prudynt†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetIntegrationTime` | done (?) | done (?) | done (?) | n.a. | n.a. | n.a. | timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetMask` | n.a. | n.a. | n.a. | vendor no-op | done (?) | n.a. | raptor† | T23: stock tx-isp-t23.ko leaves 0x80000e5 unhandled (-1) |
| `ISP_Tuning_Get/SetMaxAgain` | done (?) | done (?) | done (?) | done (?) | done (?) | n.a. | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetMaxDgain` | done (?) | done (?) | done (?) | done (?) | done (?) | n.a. | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| **► `ISP_Tuning_Get/SetModuleControl`** | n.a. | n.a. | done (host) | done (?) | done (?) | error | **T31/T41: raptor†** | T21: tuning 0x80000e2 (agg-25); T41: driver rejects 0x8000072 (-EINVAL) |
| `ISP_Tuning_Get/SetModule_Ratio` | n.a. | n.a. | n.a. | n.a. | n.a. | done (?) | – | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetSaturation` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetSensorHflip` | n.a. | n.a. | n.a. | done (?) | n.a. | n.a. | – | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetSensorVflip` | n.a. | n.a. | n.a. | done (?) | n.a. | n.a. | – | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetSinterDnsAttr` | done (host) | done (host) | done (?) | n.a. | n.a. | n.a. | prudynt† | T10+T20: reach the driver (agg-25, T20 vendor layout); T10 shares the build |
| `ISP_Tuning_Get/SetStatisConfig` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_Get/SetTemperDnsAttr` | done (host) | done (host) | done (?) | n.a. | n.a. | n.a. | prudynt† | T10+T20: reach the driver (agg-25, T20 vendor layout); T10 shares the build |
| `ISP_Tuning_Get/SetTmoCurve` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_Get/SetWDRAttr` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| `ISP_Tuning_Get/SetWdrOutputMode` | n.a. | n.a. | n.a. | n.a. | n.a. | error | – | T41: driver has no stock handler, refuses (claude/t41-isp-round2) |
| `ISP_Tuning_Get/SetWdr_OutputMode` | n.a. | n.a. | n.a. | n.a. | done (host) | n.a. | raptor† | T31: reaches the WDR tool block (agg-25) |
| `ISP_Tuning_GetAutoZoom` | n.a. | n.a. | n.a. | error | n.a. | error | – | T23: driver rejects 0x80000e8 (-EINVAL); T41: driver has no stock handler, refuses (claude/t41-isp-round2) |
| **► `ISP_Tuning_GetBlcAttr`** | n.a. | n.a. | n.a. | error | done (?) | n.a. | **raptor†** | T23: driver rejects 0x80000a5 (-EINVAL) |
| `ISP_Tuning_GetBrightness` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor† | audit: reaches the driver/kernel |
| `ISP_Tuning_GetEVAttr` | done (?) | done (?) | done (?) | done (?) | done (?) | n.a. | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_GetHVFlip` | n.a. | n.a. | n.a. | done (?) | done (?) | missing | T23/T31: raptor† |  |
| `ISP_Tuning_GetMaskBlock` | n.a. | n.a. | n.a. | done (host) | n.a. | missing | – | T23: 0x8000183 in claude/t23-awb-runtime only (not in agg-25) |
| `ISP_Tuning_GetRawDRC` | done (host) | done (host) | done (?) | n.a. | n.a. | n.a. | prudynt† | T10+T20: reach the driver (agg-25, T20 vendor layout); T10 shares the build |
| `ISP_Tuning_GetSceneMode` | done (?) | done (?) | done (?) | n.a. | n.a. | n.a. | – | audit: reaches the driver/kernel |
| `ISP_Tuning_GetSensorAttr` | n.a. | n.a. | n.a. | done (?) | done (?) | done (host) | T23/T31: timps; T41: raptor†, timps | T41: driver claude/t41-connect (agg-25); host tests 58/58 |
| `ISP_Tuning_GetSensorFPS` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_GetSharpness` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor† | audit: reaches the driver/kernel |
| `ISP_Tuning_GetTotalGain` | done (?) | done (?) | done (?) | done (?) | done (?) | n.a. | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_SaveAllParam` | missing | missing | missing | n.a. | n.a. | n.a. | – |  |
| `ISP_Tuning_SetAntiFogAttr` | missing | missing | missing | n.a. | n.a. | n.a. | – |  |
| **► `ISP_Tuning_SetAutoZoom`** | n.a. | n.a. | n.a. | error | done (host) | error | **T23: raptor**; T31: prudynt†, raptor†; **T41: prudynt†** | T23: driver rejects 0x80000e8 (-EINVAL); T31: programs scaler/crop, refuses size change (agg-25); T41: driver has no stock handler, refuses (claude/t41-isp-round2) |
| `ISP_Tuning_SetBrightness` | done (?) | done (?) | vendor no-op | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | T21: imgfx 2026-10-05: no picture change; vendor no-op or measurement issue (which one: unverified) |
| `ISP_Tuning_SetDPStrength` | missing | missing | missing | n.a. | missing | n.a. | – |  |
| `ISP_Tuning_SetFWFreeze` | done (?) | done (?) | done (?) | n.a. | n.a. | n.a. | – | audit: reaches the driver/kernel |
| `ISP_Tuning_SetISPBypass` | done (?) | done (?) | done (?) | done (?) | done (?) | n.a. | prudynt†, raptor† | audit: reaches the driver/kernel |
| `ISP_Tuning_SetISPProcess` | done (?) | done (?) | done (?) | n.a. | n.a. | n.a. | – | audit: reaches the driver/kernel |
| **► `ISP_Tuning_SetMaskBlock`** | n.a. | n.a. | n.a. | done (host) | n.a. | error | **T41: raptor†** | T23: 0x8000183 in claude/t23-awb-runtime only (not in agg-25); T41: driver has no handler, fails with -EPERM since agg-25 (was silent 0); vendor behaviour unverified |
| `ISP_Tuning_SetMeshShadingScale` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| `ISP_Tuning_SetRawDRC` | done (host) | done (host) | vendor no-op | n.a. | n.a. | n.a. | prudynt† | T10+T20: reach the driver (agg-25, T20 vendor layout); T10 shares the build; T21: imgfx 2026-10-05: no picture change; vendor no-op or measurement issue (which one: unverified) |
| **► `ISP_Tuning_SetScalerLv`** | n.a. | n.a. | n.a. | error | done (?) | error | **raptor†** | T23: driver rejects 0x80000e9 (-EINVAL); T41: driver has no handler, fails with -EPERM since agg-25 (was silent 0); vendor behaviour unverified |
| `ISP_Tuning_SetSceneMode` | done (?) | done (?) | vendor no-op | n.a. | n.a. | n.a. | timps | T21: imgfx 2026-10-05: no picture change; vendor no-op or measurement issue (which one: unverified) |
| `ISP_Tuning_SetSensorFPS` | done (?) | done (?) | done (?) | done (?) | done (?) | done (host) | prudynt†, raptor†, timps | T41: reaches the sensor (agg-25); -EOPNOTSUPP on the gc5603 of cam-F |
| `ISP_Tuning_SetSharpness` | done (?) | done (?) | vendor no-op | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | T21: imgfx 2026-10-05: no picture change; vendor no-op or measurement issue (which one: unverified) |
| `ISP_Tuning_SetSinterStrength` | done (?) | done (?) | done (?) | done (?) | done (?) | n.a. | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_SetTemperDnsCtl` | done (host) | done (host) | done (?) | n.a. | n.a. | n.a. | – | T10+T20: newly exported (agg-25) |
| `ISP_Tuning_SetTemperStrength` | done (?) | done (?) | done (?) | done (?) | done (?) | n.a. | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_SetTmoFaceae` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_SetVideoDrop` | done (host) | done (host) | done (host) | done (host) | done (host) | done (host) | raptor† | all: callback after 2/4/6 s without frames (agg-25); host-tested; video demand rule |
| **► `ISP_Tuning_SwitchBin`** | n.a. | n.a. | n.a. | error | n.a. | missing | **T41: prudynt†** | T23: driver rejects 0x8000185 (-EINVAL) |
| `ISP_Tuning_WaitFrame` | done (host) | done (host) | done (host) | done (host) | done (?) | n.a. | raptor† | T10+T20+T21: waits for the frame end (agg-25; T20 ms, not jiffies); T23: stock 24-byte block, driver routed (agg-25), no device test |
| `ISP_Tuning_WaitFrameDone` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `ISP_WDR_ENABLE`** | n.a. | n.a. | n.a. | n.a. | done (?) | error | **raptor†** | T41: ENOTSUP stub (returns -1) |
| **► `ISP_WDR_ENABLE_GET`** | n.a. | n.a. | n.a. | n.a. | n.a. | error | **raptor†** | T41: ENOTSUP stub (returns -1) |
| `ISP_WDR_ENABLE_Get` | n.a. | n.a. | n.a. | n.a. | done (?) | n.a. | – | audit: userspace implementation |
| `ISP_WDR_OPEN` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |

</details>

### AE / AWB / AF

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by | Note |
|---|---|---|---|---|---|---|---|---|
| `ISP_SetAeAlgoFunc` | n.a. | n.a. | n.a. | done (?) | done (?) | missing | – |  |
| `ISP_SetAwbAlgoFunc` | n.a. | n.a. | n.a. | done (?) | done (?) | missing | – |  |
| `ISP_Tuning_AwbSync` | n.a. | n.a. | n.a. | error | n.a. | n.a. | – | T23: driver rejects 0x8000011 (-EINVAL) |
| `ISP_Tuning_Get/SetAeConvergeStep` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_Get/SetAeExpList` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `ISP_Tuning_Get/SetAfWeight`** | n.a. | n.a. | done (?) | done (host) | done (dev) | error | **raptor†** | T23: AF statistics chain from the stock module, off by default (source_af=0); no device test; T31: reconstructed AF chain; metrics/zone/weight/hist verified on cam-A 2026-10-05, Get->Set roundtrip 0; T41: ENOTSUP stub (returns -1) |
| **► `ISP_Tuning_Get/SetAwbClust`** | n.a. | n.a. | n.a. | cache-only | done (?) | n.a. | **raptor†** | T23: stock objects stored, open AWB does not read them in agg-25; effective in claude/t23-awb-runtime (host-tested) |
| `ISP_Tuning_Get/SetAwbConvergeStep` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `ISP_Tuning_Get/SetAwbCtTrend`** | n.a. | n.a. | n.a. | cache-only | done (?) | n.a. | **raptor†** | T23: stock objects stored, open AWB does not read them in agg-25; effective in claude/t23-awb-runtime (host-tested) |
| `ISP_Tuning_Get/SetAwbCtTrendOffset` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_Get/SetFaceAe` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_Get/SetFaceAeWeiget` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetAEEvList` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetAEFlickerFlag` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetAFMetricesInfo` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetAeAtList` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetAeBv` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetAeEvList` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetAfStatistics` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetFaceAeLuma` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_SetWB_ALGO` | n.a. | n.a. | n.a. | error | done (?) | n.a. | – | T23: driver does not route 0x800000c (HLIL AWB has no light-source table) |

<details><summary>All 58 rows of this area (21 with a gap)</summary>

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by | Note |
|---|---|---|---|---|---|---|---|---|
| `ISP_SetAeAlgoFunc` | n.a. | n.a. | n.a. | done (?) | done (?) | missing | – |  |
| `ISP_SetAwbAlgoFunc` | n.a. | n.a. | n.a. | done (?) | done (?) | missing | – |  |
| `ISP_Tuning_AE_Get/SetROI` | done (?) | done (?) | done (?) | done (host) | done (?) | n.a. | T10/T20: prudynt†; T21/T23/T31: prudynt†, raptor† | T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified |
| `ISP_Tuning_AwbSync` | n.a. | n.a. | n.a. | error | n.a. | n.a. | – | T23: driver rejects 0x8000011 (-EINVAL) |
| `ISP_Tuning_Awb_Get/SetCwfShift` | done (host) | done (host) | n.a. | n.a. | n.a. | n.a. | – | T10+T20: newly exported (agg-25) |
| `ISP_Tuning_Awb_Get/SetRgbCoefft` | done (?) | done (?) | done (?) | done (host) | done (?) | done (host) | raptor† | T23: stock handlers routed (open-tx-isp agg-25), no device test; T41: driver claude/t41-connect (agg-25); host tests 58/58 |
| `ISP_Tuning_Get/SetAeAttr` | n.a. | n.a. | n.a. | done (host) | done (?) | n.a. | prudynt†, raptor† | T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified |
| `ISP_Tuning_Get/SetAeComp` | done (?) | done (?) | n.a. | done (?) | done (?) | n.a. | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetAeConvergeStep` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_Get/SetAeExpList` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_Get/SetAeExprInfo` | n.a. | n.a. | n.a. | n.a. | n.a. | done (?) | – | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetAeHist` | done (?) | done (?) | done (?) | done (host) | done (?) | n.a. | prudynt†, raptor† | T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified |
| `ISP_Tuning_Get/SetAeMin` | n.a. | n.a. | done (?) | done (host) | done (?) | n.a. | T23: prudynt; T31: prudynt†, raptor† | T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified |
| `ISP_Tuning_Get/SetAeScenceAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | done (?) | – | audit: reaches the driver/kernel |
| `ISP_Tuning_Get/SetAeStrategy` | done (host) | done (host) | done (host) | n.a. | n.a. | n.a. | – | T10+T20+T21: newly exported (agg-25); T10 shares the T20 build |
| `ISP_Tuning_Get/SetAeTargetList` | n.a. | n.a. | n.a. | done (host) | done (?) | n.a. | raptor† | T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified |
| `ISP_Tuning_Get/SetAeWeight` | done (?) | done (?) | done (?) | done (host) | done (?) | done (host) | prudynt†, raptor† | T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified; T41: driver claude/t41-connect (agg-25); host tests 58/58 |
| `ISP_Tuning_Get/SetAfHist` | done (?) | done (?) | done (?) | done (host) | done (dev) | n.a. | raptor† | T23: AF statistics chain from the stock module, off by default (source_af=0); no device test; T31: reconstructed AF chain; metrics/zone/weight/hist verified on cam-A 2026-10-05, Get->Set roundtrip 0 |
| **► `ISP_Tuning_Get/SetAfWeight`** | n.a. | n.a. | done (?) | done (host) | done (dev) | error | **raptor†** | T23: AF statistics chain from the stock module, off by default (source_af=0); no device test; T31: reconstructed AF chain; metrics/zone/weight/hist verified on cam-A 2026-10-05, Get->Set roundtrip 0; T41: ENOTSUP stub (returns -1) |
| `ISP_Tuning_Get/SetAwbAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | done (?) | – | audit: reaches the driver/kernel |
| **► `ISP_Tuning_Get/SetAwbClust`** | n.a. | n.a. | n.a. | cache-only | done (?) | n.a. | **raptor†** | T23: stock objects stored, open AWB does not read them in agg-25; effective in claude/t23-awb-runtime (host-tested) |
| `ISP_Tuning_Get/SetAwbConvergeStep` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `ISP_Tuning_Get/SetAwbCtTrend`** | n.a. | n.a. | n.a. | cache-only | done (?) | n.a. | **raptor†** | T23: stock objects stored, open AWB does not read them in agg-25; effective in claude/t23-awb-runtime (host-tested) |
| `ISP_Tuning_Get/SetAwbCtTrendOffset` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_Get/SetAwbHist` | done (?) | done (?) | done (?) | vendor no-op | vendor no-op | n.a. | prudynt†, raptor† | T23+T31: stock driver also no-op (OEM-same) |
| `ISP_Tuning_Get/SetAwbWeight` | done (?) | done (?) | done (?) | vendor no-op | vendor no-op | done (?) | prudynt†, raptor† | T23+T31: stock driver also no-op (OEM-same) |
| `ISP_Tuning_Get/SetAwbZoneWeight` | n.a. | n.a. | n.a. | done (host) | n.a. | n.a. | – | T23: stock handlers routed (open-tx-isp agg-25), no device test |
| `ISP_Tuning_Get/SetFaceAe` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_Get/SetFaceAeWeiget` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_Get/SetWB` | done (?) | done (?) | done (?) | done (?) | done (?) | n.a. | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_GetAEEvList` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetAEFlickerFlag` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetAE_IT_MAX` | n.a. | n.a. | n.a. | done (?) | done (?) | n.a. | prudynt† | audit: reaches the driver/kernel |
| `ISP_Tuning_GetAFMetrices` | n.a. | n.a. | done (?) | done (host) | done (dev) | n.a. | raptor† | T23: AF statistics chain from the stock module, off by default (source_af=0); no device test; T31: reconstructed AF chain; metrics/zone/weight/hist verified on cam-A 2026-10-05, Get->Set roundtrip 0 |
| `ISP_Tuning_GetAFMetricesInfo` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetAWBCt` | n.a. | n.a. | n.a. | done (host) | done (?) | n.a. | T23: raptor; T31: prudynt†, raptor† | T23: stock handlers routed (open-tx-isp agg-25), no device test |
| `ISP_Tuning_GetAeAtList` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetAeBv` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetAeEvList` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetAeHist_Origin` | n.a. | n.a. | n.a. | done (host) | done (?) | n.a. | T23: prudynt; T31: prudynt†, raptor† | T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified |
| `ISP_Tuning_GetAeLuma` | n.a. | n.a. | done (?) | done (?) | done (?) | n.a. | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_GetAeState` | n.a. | n.a. | n.a. | done (host) | done (?) | n.a. | raptor† | T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified |
| `ISP_Tuning_GetAeStatistics` | n.a. | n.a. | n.a. | n.a. | n.a. | done (?) | – | audit: reaches the driver/kernel |
| `ISP_Tuning_GetAeZone` | done (host) | done (host) | done (?) | done (host) | done (?) | n.a. | prudynt†, raptor† | T10+T20: vendor T20 ids/ABI (agg-25); T10 shares the build; T23: stock handlers routed (agg-25), no device test |
| `ISP_Tuning_GetAfStatistics` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetAfZone` | n.a. | n.a. | n.a. | done (host) | done (dev) | n.a. | raptor† | T23: AF statistics chain from the stock module, off by default (source_af=0); no device test; T31: reconstructed AF chain; metrics/zone/weight/hist verified on cam-A 2026-10-05, Get->Set roundtrip 0 |
| `ISP_Tuning_GetAwbGlobalStatistics` | n.a. | n.a. | n.a. | n.a. | n.a. | done (?) | – | audit: reaches the driver/kernel |
| `ISP_Tuning_GetAwbStatistics` | n.a. | n.a. | n.a. | n.a. | n.a. | done (?) | – | audit: reaches the driver/kernel |
| `ISP_Tuning_GetAwbZone` | done (host) | done (host) | n.a. | done (host) | done (?) | n.a. | T10/T20: prudynt†; T23/T31: prudynt†, raptor† | T10+T20: vendor T20 ids/ABI (agg-25); T10 shares the build; T23: stock handlers routed (agg-25), no device test |
| `ISP_Tuning_GetExpr` | done (?) | done (?) | done (?) | done (?) | done (?) | n.a. | raptor†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_GetFaceAeLuma` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `ISP_Tuning_GetWB_GOL_Statis` | n.a. | n.a. | done (?) | done (?) | done (?) | n.a. | raptor† | audit: reaches the driver/kernel |
| `ISP_Tuning_GetWB_Statis` | done (?) | done (?) | done (?) | done (?) | done (?) | n.a. | raptor† | audit: reaches the driver/kernel |
| `ISP_Tuning_SetAeFreeze` | n.a. | n.a. | n.a. | done (host) | done (?) | n.a. | raptor† | T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified |
| `ISP_Tuning_SetAe_IT_MAX` | n.a. | n.a. | n.a. | done (?) | done (?) | n.a. | prudynt†, timps | audit: reaches the driver/kernel |
| `ISP_Tuning_SetAwbCt` | n.a. | n.a. | n.a. | done (host) | done (?) | n.a. | raptor† | T23: stock handlers routed (open-tx-isp agg-25), no device test |
| `ISP_Tuning_SetExpr` | done (?) | done (?) | done (?) | done (host) | done (?) | n.a. | T23/T31: raptor† | T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified |
| `ISP_Tuning_SetWB_ALGO` | n.a. | n.a. | n.a. | error | done (?) | n.a. | – | T23: driver does not route 0x800000c (HLIL AWB has no light-source table) |

</details>

### Encoder (and decoder)

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by | Note |
|---|---|---|---|---|---|---|---|---|
| `Decoder_*` (8 functions) | missing | missing | missing | done (?) | missing | missing | – | functions: CreateChn, DestroyChn, GetFrame, PollingFrame, ReleaseFrame, SendStreamTimeout, StartRecvPic, StopRecvPic. |
| `Encoder_Get/SetChangeRef` | missing | missing | missing | done (?) | n.a. | n.a. | – |  |
| `Encoder_Get/SetChnDemask` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| **► `Encoder_Get/SetChnDenoise`** | error | error | error | done (?) | n.a. | n.a. | **T21: raptor†** | T10+T20+T21: refused (-1): Helix/NVPU cannot do it (agg-25); vendor behaviour unverified |
| `Encoder_Get/SetChnFrmUsedMode` | missing | missing | missing | done (host) | n.a. | n.a. | – | T23: stored in the channel attribute (claude/t23-enc-rest, not in agg-25) |
| `Encoder_Get/SetChnH264Demask` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| `Encoder_Get/SetChnH264Denoise` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| `Encoder_Get/SetChnH264FrmUsedMode` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| `Encoder_Get/SetChnHSkip` | missing | missing | done (?) | done (?) | n.a. | n.a. | – |  |
| `Encoder_Get/SetChnRcAttr` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| `Encoder_Get/SetChnRoiAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_Get/SetChnSeiAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_Get/SetFisheyeEnableStatus` | missing | missing | missing | cache-only | cache-only | missing | – | T23+T31: kept for getter only (documented in source) |
| `Encoder_Get/SetH264TransCfg` | cache-only | done (dev) | done (host) | done (?) | n.a. | n.a. | T21: raptor† | T10: T10 has no chroma-offset register, stays 0; T20: chroma QP offset via PPS + reg 0x40120 (claude/t1x-roi, not in agg-25): verified on cam-C, no colour shift; T21: chroma QP offset, PPS rewrite (claude/t1x-roi, not in agg-25); not device-tested |
| **► `Encoder_Get/SetH265TransCfg`** | n.a. | n.a. | cache-only | cache-only | n.a. | n.a. | **T21: raptor†** | T21+T23: stored in channel, never pushed |
| **► `Encoder_Get/SetJpegeQl`** | cache-only | cache-only | cache-only | done (?) | n.a. | done (host) | **T10: prudynt†, raptor†**; **T20/T21/T23: prudynt†, raptor†, timps** | T10+T20+T21: only applied at CreateChn (p2_encoder.c:1771); live change ignored; T41: live and at CreateChn (agg-25); T10/T20/T21 still applied at CreateChn only |
| `Encoder_Get/SetMbRC` | ? | done (host) | done (?) | done (?) | n.a. | n.a. | T21: raptor† | T10: commit names T20 only; T10 shares the build; T20: switches the macroblock QP table (agg-25) |
| **► `Encoder_Get/SetQpgMode`** | n.a. | n.a. | error | done (?) | n.a. | n.a. | **T21: raptor†** | T21: refused (-1) since agg-25; vendor behaviour unverified |
| `Encoder_Get/Setframelossthd` | n.a. | n.a. | n.a. | cache-only | n.a. | n.a. | – | T23: kept for getter only (documented in source) |
| `Encoder_GetGOPSize` | missing | missing | missing | done (?) | n.a. | n.a. | – |  |
| `Encoder_InputJpege` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | T23: timps |  |
| `Encoder_InputJpege_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_SetAvpuBsShare` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_SetAvpuBsSize` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_SetAvpuJpegQp` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_SetChnHSkipBlackEnhance` | missing | missing | missing | done (?) | n.a. | n.a. | – |  |
| `Encoder_SetChnMapRoi` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `Encoder_SetChnMaxPictureSize`** | n.a. | n.a. | n.a. | done (host) | n.a. | cache-only | **T41: raptor†** | T23: as the OEM stores it, re-encode on overshoot (claude/t23-enc-rest, not in agg-25); T41: written into rcAttr copy, codec not updated (T23: loss threshold kept only) |
| `Encoder_SetFrameRelease` | n.a. | n.a. | n.a. | n.a. | cache-only | missing | – | T31: kept for getter only (documented in source) |
| `Encoder_SetIvpuBsSize` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_SetMultiSectionMode` | n.a. | n.a. | n.a. | cache-only | n.a. | n.a. | – | T23: kept for getter only (documented in source) |
| **► `Encoder_SetbufshareChn`** | n.a. | n.a. | n.a. | n.a. | stub | stub | **prudynt†, raptor†** | T31+T41: validates channel numbers, returns 0 |
| `Encoder_VbmAlloc` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | T23: timps |  |
| `Encoder_VbmAlloc_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_VbmFree` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | T23: timps |  |
| `Encoder_VbmFree_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_VbmP2V` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `Encoder_VbmV2P` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | T23: timps |  |
| `Encoder_YuvEncode` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | T23: timps |  |
| `Encoder_YuvEncode_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_YuvExit` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | T23: timps |  |
| `Encoder_YuvExit_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_YuvInit` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | T23: timps |  |
| `Encoder_YuvInit_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |

<details><summary>All 92 rows of this area (44 with a gap)</summary>

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by | Note |
|---|---|---|---|---|---|---|---|---|
| `Decoder_*` (8 functions) | missing | missing | missing | done (?) | missing | missing | – | functions: CreateChn, DestroyChn, GetFrame, PollingFrame, ReleaseFrame, SendStreamTimeout, StartRecvPic, StopRecvPic. |
| `Encoder_CreateChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `Encoder_CreateGroup` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `Encoder_DestroyChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `Encoder_DestroyGroup` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `Encoder_FlushStream` | done (host) | done (host) | done (host) | done (host) | done (host) | done (host) | prudynt†, raptor† | all: drops the encoded stream nobody fetched (agg-25) |
| `Encoder_Get/SetChangeRef` | missing | missing | missing | done (?) | n.a. | n.a. | – |  |
| `Encoder_Get/SetChnAttrRcMode` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor†, timps | audit: reaches the driver/kernel; userspace implementation |
| `Encoder_Get/SetChnColor2Grey` | done (host) | done (host) | done (host) | done (?) | n.a. | n.a. | T21: raptor† | T10+T20+T21: codes grey pictures (agg-25) |
| `Encoder_Get/SetChnCrop` | n.a. | n.a. | n.a. | done (?) | n.a. | n.a. | – | audit: userspace implementation |
| `Encoder_Get/SetChnDemask` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| **► `Encoder_Get/SetChnDenoise`** | error | error | error | done (?) | n.a. | n.a. | **T21: raptor†** | T10+T20+T21: refused (-1): Helix/NVPU cannot do it (agg-25); vendor behaviour unverified |
| `Encoder_Get/SetChnFrmRate` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor† | audit: reaches the driver/kernel; userspace implementation |
| `Encoder_Get/SetChnFrmUsedMode` | missing | missing | missing | done (host) | n.a. | n.a. | – | T23: stored in the channel attribute (claude/t23-enc-rest, not in agg-25) |
| `Encoder_Get/SetChnGopAttr` | n.a. | n.a. | n.a. | n.a. | done (?) | done (?) | – | audit: userspace implementation |
| `Encoder_Get/SetChnH264Demask` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| `Encoder_Get/SetChnH264Denoise` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| `Encoder_Get/SetChnH264FrmUsedMode` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| `Encoder_Get/SetChnHSkip` | missing | missing | done (?) | done (?) | n.a. | n.a. | – |  |
| `Encoder_Get/SetChnROI` | done (host) | done (dev) | vendor no-op | done (?) | n.a. | n.a. | T21: raptor† | T10: claude/t1x-roi (not in agg-25): EFE ROI registers per the OEM slice init; T10 unverified on a device; T20: claude/t1x-roi (not in agg-25): QP51 region blocky, QP15 fine on cam-C 2026-10-05; absolute QP 15 raises the bitrate 1.4 to 9.9 Mbit/s (CBR bypassed, vendor semantics unverified); T21: vendor 1.0.33 never programs IMP ROIs; ours only with OPENIMP_T21_ROI=1 (claude/t1x-roi, beyond vendor, user decision pending); without it the call is refused in agg-25 |
| `Encoder_Get/SetChnRcAttr` | missing | missing | n.a. | n.a. | n.a. | n.a. | – |  |
| `Encoder_Get/SetChnRoiAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_Get/SetChnSeiAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_Get/SetFisheyeEnableStatus` | missing | missing | missing | cache-only | cache-only | missing | – | T23+T31: kept for getter only (documented in source) |
| `Encoder_Get/SetGDRCfg` | n.a. | n.a. | n.a. | done (?) | n.a. | n.a. | – | audit: userspace implementation |
| `Encoder_Get/SetH264TransCfg` | cache-only | done (dev) | done (host) | done (?) | n.a. | n.a. | T21: raptor† | T10: T10 has no chroma-offset register, stays 0; T20: chroma QP offset via PPS + reg 0x40120 (claude/t1x-roi, not in agg-25): verified on cam-C, no colour shift; T21: chroma QP offset, PPS rewrite (claude/t1x-roi, not in agg-25); not device-tested |
| **► `Encoder_Get/SetH265TransCfg`** | n.a. | n.a. | cache-only | cache-only | n.a. | n.a. | **T21: raptor†** | T21+T23: stored in channel, never pushed |
| **► `Encoder_Get/SetJpegeQl`** | cache-only | cache-only | cache-only | done (?) | n.a. | done (host) | **T10: prudynt†, raptor†**; **T20/T21/T23: prudynt†, raptor†, timps** | T10+T20+T21: only applied at CreateChn (p2_encoder.c:1771); live change ignored; T41: live and at CreateChn (agg-25); T10/T20/T21 still applied at CreateChn only |
| `Encoder_Get/SetMaxStreamCnt` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `Encoder_Get/SetMbRC` | ? | done (host) | done (?) | done (?) | n.a. | n.a. | T21: raptor† | T10: commit names T20 only; T10 shares the build; T20: switches the macroblock QP table (agg-25) |
| `Encoder_Get/SetPool` | n.a. | n.a. | n.a. | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| **► `Encoder_Get/SetQpgMode`** | n.a. | n.a. | error | done (?) | n.a. | n.a. | **T21: raptor†** | T21: refused (-1) since agg-25; vendor behaviour unverified |
| `Encoder_Get/SetStreamBufSize` | n.a. | n.a. | n.a. | n.a. | done (?) | done (?) | – | audit: userspace implementation |
| `Encoder_Get/SetSuperFrameCfg` | done (host) | done (host) | done (host) | done (?) | n.a. | n.a. | T21: raptor† | T10+T20+T21: reaches the rate control or fails (agg-25) |
| `Encoder_Get/Setframelossthd` | n.a. | n.a. | n.a. | cache-only | n.a. | n.a. | – | T23: kept for getter only (documented in source) |
| `Encoder_GetChnAttr` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor† | audit: userspace implementation |
| `Encoder_GetChnAveBitrate` | n.a. | n.a. | n.a. | n.a. | done (?) | n.a. | timps | audit: userspace implementation |
| `Encoder_GetChnEncType` | n.a. | n.a. | done (?) | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `Encoder_GetChnEvalInfo` | n.a. | n.a. | n.a. | n.a. | done (host) | n.a. | raptor† | T31: claude/t23-enc-rest (not in agg-25) |
| `Encoder_GetChnMaxPictureSize` | n.a. | n.a. | n.a. | done (host) | n.a. | n.a. | – | T23: as the OEM stores it, re-encode on overshoot (claude/t23-enc-rest, not in agg-25) |
| `Encoder_GetFd` | n.a. | n.a. | done (?) | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `Encoder_GetGOPSize` | missing | missing | missing | done (?) | n.a. | n.a. | – |  |
| `Encoder_GetStream` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `Encoder_InputJpege` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | T23: timps |  |
| `Encoder_InputJpege_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_InsertUserData` | done (?) | done (?) | done (?) | done (?) | n.a. | n.a. | – | audit: userspace implementation |
| `Encoder_PollingModuleStream` | n.a. | n.a. | done (?) | done (?) | done (?) | done (?) | – | audit: reaches the driver/kernel |
| `Encoder_PollingStream` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `Encoder_Query` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor†, timps | audit: userspace implementation |
| `Encoder_RegisterChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `Encoder_ReleaseStream` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `Encoder_RequestGDR` | n.a. | n.a. | n.a. | done (?) | n.a. | n.a. | – | audit: userspace implementation |
| `Encoder_RequestIDR` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `Encoder_SetAvpuBsShare` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_SetAvpuBsSize` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_SetAvpuJpegQp` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_SetChnBitRate` | n.a. | n.a. | n.a. | n.a. | done (?) | done (?) | timps | audit: userspace implementation |
| `Encoder_SetChnEntropyMode` | n.a. | n.a. | n.a. | n.a. | done (?) | n.a. | – | audit: userspace implementation |
| `Encoder_SetChnGopLength` | n.a. | n.a. | n.a. | n.a. | done (?) | done (?) | – | audit: userspace implementation |
| `Encoder_SetChnHSkipBlackEnhance` | missing | missing | missing | done (?) | n.a. | n.a. | – |  |
| `Encoder_SetChnInitQP` | n.a. | n.a. | n.a. | done (?) | n.a. | n.a. | – | audit: userspace implementation |
| `Encoder_SetChnMapRoi` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `Encoder_SetChnMaxPictureSize`** | n.a. | n.a. | n.a. | done (host) | n.a. | cache-only | **T41: raptor†** | T23: as the OEM stores it, re-encode on overshoot (claude/t23-enc-rest, not in agg-25); T41: written into rcAttr copy, codec not updated (T23: loss threshold kept only) |
| `Encoder_SetChnQp` | n.a. | n.a. | n.a. | n.a. | done (?) | n.a. | – | audit: userspace implementation |
| `Encoder_SetChnQpBounds` | n.a. | n.a. | n.a. | n.a. | done (?) | done (?) | timps | audit: userspace implementation |
| `Encoder_SetChnQpBoundsPerFrame` | n.a. | n.a. | n.a. | n.a. | n.a. | done (?) | – | audit: userspace implementation |
| `Encoder_SetChnQpIPDelta` | n.a. | n.a. | n.a. | n.a. | done (?) | n.a. | timps | audit: userspace implementation |
| `Encoder_SetChnResizeMode` | n.a. | n.a. | n.a. | n.a. | done (?) | done (?) | – | audit: userspace implementation |
| `Encoder_SetDefaultParam` | n.a. | n.a. | n.a. | n.a. | done (?) | done (?) | timps | audit: userspace implementation |
| `Encoder_SetFrameRelease` | n.a. | n.a. | n.a. | n.a. | cache-only | missing | – | T31: kept for getter only (documented in source) |
| `Encoder_SetGOPSize` | done (?) | done (?) | done (?) | done (?) | n.a. | n.a. | raptor† | audit: reaches the driver/kernel; userspace implementation |
| `Encoder_SetIvpuBsSize` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_SetMultiSectionMode` | n.a. | n.a. | n.a. | cache-only | n.a. | n.a. | – | T23: kept for getter only (documented in source) |
| **► `Encoder_SetbufshareChn`** | n.a. | n.a. | n.a. | n.a. | stub | stub | **prudynt†, raptor†** | T31+T41: validates channel numbers, returns 0 |
| `Encoder_StartRecvPic` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `Encoder_StopRecvPic` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `Encoder_UnRegisterChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `Encoder_VbmAlloc` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | T23: timps |  |
| `Encoder_VbmAlloc_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_VbmFree` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | T23: timps |  |
| `Encoder_VbmFree_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_VbmP2V` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `Encoder_VbmV2P` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | T23: timps |  |
| `Encoder_YuvEncode` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | T23: timps |  |
| `Encoder_YuvEncode_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_YuvExit` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | T23: timps |  |
| `Encoder_YuvExit_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_YuvGetCrop` | n.a. | n.a. | n.a. | done (?) | n.a. | n.a. | – | audit: reaches the driver/kernel |
| `Encoder_YuvInit` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | T23: timps |  |
| `Encoder_YuvInit_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Encoder_YuvRequestIDR` | n.a. | n.a. | n.a. | done (?) | n.a. | n.a. | timps | audit: reaches the driver/kernel |
| `Encoder_YuvSetCrop` | n.a. | n.a. | n.a. | done (?) | n.a. | n.a. | – | audit: reaches the driver/kernel |

</details>

### Framesource

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by | Note |
|---|---|---|---|---|---|---|---|---|
| `EmuFrameSource_*` (4 functions) | n.a. | n.a. | n.a. | n.a. | missing | missing | – | functions: CreateChn, DestroyChn, DisableChn, EnableChn. |
| `FB_*` (5 functions) | n.a. | n.a. | n.a. | n.a. | missing | n.a. | – | functions: CreateGroup, DestroyGroup, DisableDev, EnableDev, GetDevInfo. |
| `FrameSource_DequeueBuffer` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `FrameSource_ExternInject_CreateChn` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `FrameSource_ExternInject_DestroyChn` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `FrameSource_ExternInject_DisableChn` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `FrameSource_ExternInject_EnableChn` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `FrameSource_Get/SetChnFifoAttr`** | done (host) | done (host) | done (host) | done (host) | done (host) | cache-only | **prudynt†, raptor†** | T10+T20+T21+T23+T31: = SetMaxDelay(maxdepth) (agg-25); FIFO_DATA_PRIORITY refused for maxdepth>0; T41: FIFO attr stored, no FIFO behind it |
| **► `FrameSource_Get/SetDelay`** | done (host) | done (host) | done (host) | done (host) | done (host) | error | **T31/T41: raptor†** | T10+T20+T21+T23+T31: real delay FIFO (agg-25); T10 shares the T20 build; FIFO_DATA_PRIORITY refused for maxdepth>0; T41: ENOTSUP stub (returns -1) |
| **► `FrameSource_Get/SetFrameDepth`** | done (?) | done (?) | done (?) | done (?) | done (?) | cache-only | **T10/T20/T21/T31/T41: prudynt†, raptor†**; T23: prudynt, raptor, timps | T41: depth stored, GetFrame ignores it (T41 p1) |
| **► `FrameSource_Get/SetI2dAttr`** | n.a. | n.a. | n.a. | n.a. | n.a. | error | **raptor†** | T41: ENOTSUP stub (returns -1) |
| **► `FrameSource_Get/SetMaxDelay`** | done (host) | done (host) | done (host) | done (host) | done (host) | error | **T31/T41: raptor†** | T10+T20+T21+T23+T31: real delay FIFO (agg-25); T10 shares the T20 build; FIFO_DATA_PRIORITY refused for maxdepth>0; T41: ENOTSUP stub (returns -1) |
| **► `FrameSource_Get/SetPool`** | n.a. | n.a. | n.a. | done (host) | done (host) | cache-only | **T31/T41: raptor†** | T23+T31: real memory pools (claude/t23t31-cacheonly, not in agg-25); T41: pool id recorded only |
| `FrameSource_GetFrameEx` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `FrameSource_GetTimedFrame`** | done (host) | done (host) | done (host) | done (host) | done (host) | error | **raptor†** | T10+T20+T21+T23+T31: agg-25; T41: ENOTSUP stub (returns -1) |
| `FrameSource_QueueBuffer` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `FrameSource_ReleaseFrameEx` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `FrameSource_SetYuvAlign` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |

<details><summary>All 30 rows of this area (18 with a gap)</summary>

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by | Note |
|---|---|---|---|---|---|---|---|---|
| `EmuFrameSource_*` (4 functions) | n.a. | n.a. | n.a. | n.a. | missing | missing | – | functions: CreateChn, DestroyChn, DisableChn, EnableChn. |
| `FB_*` (5 functions) | n.a. | n.a. | n.a. | n.a. | missing | n.a. | – | functions: CreateGroup, DestroyGroup, DisableDev, EnableDev, GetDevInfo. |
| `FrameSource_ChnStatQuery` | n.a. | n.a. | n.a. | n.a. | done (?) | n.a. | – | audit: userspace implementation |
| `FrameSource_CreateChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `FrameSource_DequeueBuffer` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `FrameSource_DestroyChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `FrameSource_DisableChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `FrameSource_EnableChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `FrameSource_ExternInject_CreateChn` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `FrameSource_ExternInject_DestroyChn` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `FrameSource_ExternInject_DisableChn` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `FrameSource_ExternInject_EnableChn` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `FrameSource_Get/SetChnAttr` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| **► `FrameSource_Get/SetChnFifoAttr`** | done (host) | done (host) | done (host) | done (host) | done (host) | cache-only | **prudynt†, raptor†** | T10+T20+T21+T23+T31: = SetMaxDelay(maxdepth) (agg-25); FIFO_DATA_PRIORITY refused for maxdepth>0; T41: FIFO attr stored, no FIFO behind it |
| **► `FrameSource_Get/SetDelay`** | done (host) | done (host) | done (host) | done (host) | done (host) | error | **T31/T41: raptor†** | T10+T20+T21+T23+T31: real delay FIFO (agg-25); T10 shares the T20 build; FIFO_DATA_PRIORITY refused for maxdepth>0; T41: ENOTSUP stub (returns -1) |
| `FrameSource_Get/SetDirectModeAttr` | n.a. | n.a. | n.a. | done (?) | n.a. | n.a. | – | audit: userspace implementation |
| **► `FrameSource_Get/SetFrameDepth`** | done (?) | done (?) | done (?) | done (?) | done (?) | cache-only | **T10/T20/T21/T31/T41: prudynt†, raptor†**; T23: prudynt, raptor, timps | T41: depth stored, GetFrame ignores it (T41 p1) |
| **► `FrameSource_Get/SetI2dAttr`** | n.a. | n.a. | n.a. | n.a. | n.a. | error | **raptor†** | T41: ENOTSUP stub (returns -1) |
| **► `FrameSource_Get/SetMaxDelay`** | done (host) | done (host) | done (host) | done (host) | done (host) | error | **T31/T41: raptor†** | T10+T20+T21+T23+T31: real delay FIFO (agg-25); T10 shares the T20 build; FIFO_DATA_PRIORITY refused for maxdepth>0; T41: ENOTSUP stub (returns -1) |
| **► `FrameSource_Get/SetPool`** | n.a. | n.a. | n.a. | done (host) | done (host) | cache-only | **T31/T41: raptor†** | T23+T31: real memory pools (claude/t23t31-cacheonly, not in agg-25); T41: pool id recorded only |
| `FrameSource_GetFrame` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | T10/T20/T21/T31/T41: raptor†; T23: raptor, timps | audit: reaches the driver/kernel; userspace implementation |
| `FrameSource_GetFrameEx` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `FrameSource_GetTimedFrame`** | done (host) | done (host) | done (host) | done (host) | done (host) | error | **raptor†** | T10+T20+T21+T23+T31: agg-25; T41: ENOTSUP stub (returns -1) |
| `FrameSource_QueueBuffer` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `FrameSource_ReleaseFrame` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | T10/T20/T21/T31/T41: raptor†; T23: raptor, timps | audit: reaches the driver/kernel |
| `FrameSource_ReleaseFrameEx` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `FrameSource_SetChnRotate` | n.a. | n.a. | n.a. | n.a. | done (?) | n.a. | timps | audit: userspace implementation |
| `FrameSource_SetSource` | n.a. | n.a. | n.a. | n.a. | done (?) | n.a. | – | audit: userspace implementation |
| `FrameSource_SetYuvAlign` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `FrameSource_SnapFrame` | done (?) | done (?) | done (?) | done (?) | done (?) | done (host) | raptor† | T41: copies the next consumer frame, packed NV12 (agg-25); user earlier did not want it |

</details>

### OSD

All regular OSD functions (`OSD_CreateGroup`, `CreateRgn`, `RegisterRgn`, `Set/GetRgnAttr`, `Set/GetGrpRgnAttr`, `ShowRgn`, `UpdateRgnAttrData`, `Start/StopGroup` ...) are done on every SoC (OSD is device-tested on T10, T20, T21, T23, T31 and T41, see the OSD row in the matrix above); in the full list they show as done (?) because the audit tracks no per-call device test. The gap table lists the ISP-OSD variants (`*_ISP`, `ISP_Tuning_*Osd*`) that only the T23/T41 vendor API has, plus single helpers; n.a. means the function does not exist in that SoC's vendor API, not that OSD is missing.

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by | Note |
|---|---|---|---|---|---|---|---|---|
| **► `ISP_Tuning_CreateOsdRgn`** | n.a. | n.a. | n.a. | done (?) | n.a. | error | **raptor†** | T41: ENOTSUP stub (returns -1) |
| **► `ISP_Tuning_DestroyOsdRgn`** | n.a. | n.a. | n.a. | done (?) | n.a. | error | **raptor†** | T41: ENOTSUP stub (returns -1) |
| `ISP_Tuning_Get/SetOSDAttr` | n.a. | n.a. | n.a. | error | n.a. | missing | – | T23: driver rejects 0x8000181 (-EINVAL) |
| `ISP_Tuning_Get/SetOSDBlock` | n.a. | n.a. | n.a. | error | n.a. | missing | – | T23: driver rejects 0x8000182 (-EINVAL) |
| `ISP_Tuning_GetOsdRgnAttr` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| **► `ISP_Tuning_SetOsdPoolSize`** | n.a. | n.a. | n.a. | done (?) | n.a. | stub | **raptor†** | T41: 2 insns, returns ? |
| **► `ISP_Tuning_SetOsdRgnAttr`** | n.a. | n.a. | n.a. | done (?) | n.a. | error | **raptor†** | T41: ENOTSUP stub (returns -1) |
| **► `ISP_Tuning_ShowOsdRgn`** | n.a. | n.a. | n.a. | done (?) | n.a. | error | **raptor†** | T41: ENOTSUP stub (returns -1) |
| `OSD_AttachToGroup` | missing | missing | missing | done (?) | done (?) | done (?) | – |  |
| `OSD_CreateRgn_ISP` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `OSD_DestroyRgn_ISP` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `OSD_Exit_ISP` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `OSD_Get/SetRgnAttr_ISP` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `OSD_GetRegionLuma` | n.a. | n.a. | n.a. | missing | n.a. | missing | – |  |
| `OSD_GetRgnAttr_ISPPic` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `OSD_SetGroupCallback` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `OSD_SetPoolSize_ISP` | n.a. | n.a. | n.a. | done (host) | n.a. | missing | – | T23: ISP OSD pictures from the pool (claude/t23t31-cacheonly, not in agg-25) |
| `OSD_SetRgnAttr_PicISP` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `OSD_ShowRgn_ISP` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |

<details><summary>All 36 rows of this area (19 with a gap)</summary>

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by | Note |
|---|---|---|---|---|---|---|---|---|
| **► `ISP_Tuning_CreateOsdRgn`** | n.a. | n.a. | n.a. | done (?) | n.a. | error | **raptor†** | T41: ENOTSUP stub (returns -1) |
| **► `ISP_Tuning_DestroyOsdRgn`** | n.a. | n.a. | n.a. | done (?) | n.a. | error | **raptor†** | T41: ENOTSUP stub (returns -1) |
| `ISP_Tuning_Get/SetOSDAttr` | n.a. | n.a. | n.a. | error | n.a. | missing | – | T23: driver rejects 0x8000181 (-EINVAL) |
| `ISP_Tuning_Get/SetOSDBlock` | n.a. | n.a. | n.a. | error | n.a. | missing | – | T23: driver rejects 0x8000182 (-EINVAL) |
| `ISP_Tuning_GetOsdRgnAttr` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| **► `ISP_Tuning_SetOsdPoolSize`** | n.a. | n.a. | n.a. | done (?) | n.a. | stub | **raptor†** | T41: 2 insns, returns ? |
| **► `ISP_Tuning_SetOsdRgnAttr`** | n.a. | n.a. | n.a. | done (?) | n.a. | error | **raptor†** | T41: ENOTSUP stub (returns -1) |
| **► `ISP_Tuning_ShowOsdRgn`** | n.a. | n.a. | n.a. | done (?) | n.a. | error | **raptor†** | T41: ENOTSUP stub (returns -1) |
| `OSD_AttachToGroup` | missing | missing | missing | done (?) | done (?) | done (?) | – |  |
| `OSD_CreateGroup` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `OSD_CreateRgn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `OSD_CreateRgn_ISP` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `OSD_DestroyGroup` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `OSD_DestroyRgn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `OSD_DestroyRgn_ISP` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `OSD_Exit_ISP` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `OSD_Get/SetGrpRgnAttr` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `OSD_Get/SetRgnAttr` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel; userspace implementation |
| `OSD_Get/SetRgnAttr_ISP` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `OSD_GetRegionLuma` | n.a. | n.a. | n.a. | missing | n.a. | missing | – |  |
| `OSD_GetRgnAttr_ISPPic` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `OSD_RegisterRgn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel; userspace implementation |
| `OSD_RgnCreate_Query` | n.a. | n.a. | n.a. | done (?) | n.a. | done (?) | – | audit: userspace implementation |
| `OSD_RgnRegister_Query` | n.a. | n.a. | n.a. | done (?) | n.a. | done (?) | – | audit: userspace implementation |
| `OSD_SetGroupCallback` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `OSD_SetMosaic` | n.a. | n.a. | n.a. | done (?) | n.a. | done (?) | – | audit: userspace implementation |
| `OSD_SetPoolSize` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `OSD_SetPoolSize_ISP` | n.a. | n.a. | n.a. | done (host) | n.a. | missing | – | T23: ISP OSD pictures from the pool (claude/t23t31-cacheonly, not in agg-25) |
| `OSD_SetRgnAttrWithTimestamp` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: reaches the driver/kernel |
| `OSD_SetRgnAttr_PicISP` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `OSD_ShowRgn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel; userspace implementation |
| `OSD_ShowRgn_ISP` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `OSD_Start` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `OSD_Stop` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `OSD_UnRegisterRgn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `OSD_UpdateRgnAttrData` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor† | audit: reaches the driver/kernel |

</details>

### IVS

No IVS row has a gap; all IVS functions are in the full list below.

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

*No row of this area has a gap.*

<details><summary>All 17 rows of this area (0 with a gap)</summary>

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by | Note |
|---|---|---|---|---|---|---|---|---|
| `IVS_CreateBaseMoveInterface` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `IVS_CreateChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor†, timps | audit: userspace implementation |
| `IVS_CreateGroup` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor†, timps | audit: userspace implementation |
| `IVS_CreateMoveInterface` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor†, timps | audit: userspace implementation |
| `IVS_DestroyBaseMoveInterface` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `IVS_DestroyChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `IVS_DestroyGroup` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `IVS_DestroyMoveInterface` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `IVS_Get/SetParam` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor†, timps | audit: userspace implementation |
| `IVS_GetResult` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor†, timps | audit: userspace implementation |
| `IVS_PollingResult` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor†, timps | audit: userspace implementation |
| `IVS_RegisterChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor†, timps | audit: userspace implementation |
| `IVS_ReleaseData` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `IVS_ReleaseResult` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor†, timps | audit: userspace implementation |
| `IVS_StartRecvPic` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor†, timps | audit: userspace implementation |
| `IVS_StopRecvPic` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `IVS_UnRegisterChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |

</details>

### Audio

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by | Note |
|---|---|---|---|---|---|---|---|---|
| `ADEC_ReleaseDecoder` | missing | missing | n.a. | n.a. | missing | n.a. | – |  |
| `AENC_ReleaseEncoder` | missing | missing | n.a. | n.a. | missing | n.a. | – |  |
| `AI_DisableAlgo` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `AI_DisableGetRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `AI_DisableHs` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `AI_EnableAlgo` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `AI_EnableGetRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `AI_EnableHs` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `AI_Get/SetDigitalGain` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `AI_GetFrameAndRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `AI_SetHpfCoFrequency`** | n.a. | n.a. | n.a. | cache-only | cache-only | cache-only | **prudynt†, raptor†** | T23+T31+T41: cutoff recorded, fixed 300 Hz HPF used |
| `AO_DisableAlgo` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `AO_EnableAlgo` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `AO_Get/SetDigitalGain` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `Audio_Select_Codec` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `DMIC_*` (20 functions)** | n.a. | n.a. | n.a. | n.a. | done (?) | error | **raptor†** | functions: Disable, DisableAec, DisableChn, Enable, EnableAec, EnableAecRefFrame, EnableChn, Get/SetChnParam, GetFrame, GetFrameAndRef, Get/SetGain, Get/SetPubAttr, Get/SetVol, PollingFrame, ReleaseFrame, SetUserInfo. T41: ENOTSUP stub (returns -1) |
| **► `DMIC_DisableAecRefFrame`** | n.a. | n.a. | n.a. | n.a. | n.a. | error | **raptor†** | T41: ENOTSUP stub (returns -1) |

<details><summary>All 66 rows of this area (17 with a gap)</summary>

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by | Note |
|---|---|---|---|---|---|---|---|---|
| `ADEC_ReleaseDecoder` | missing | missing | n.a. | n.a. | missing | n.a. | – |  |
| `AENC_* / ADEC_*` (17 functions) | done (host) | done (host) | done (host) | done (?) | done (?) | done (host) | raptor† | functions: ClearChnBuf, CreateChn, DestroyChn, GetStream, PollingStream, RegisterDecoder, ReleaseStream, SendStream, UnRegisterDecoder, CreateChn, DestroyChn, GetStream, PollingStream, RegisterEncoder, ReleaseStream, SendFrame, UnRegisterEncoder. T10+T20+T21+T41: claude/aenc-adec-all (in agg-25): shared software codecs; device encode test open |
| `AENC_ReleaseEncoder` | missing | missing | n.a. | n.a. | missing | n.a. | – |  |
| `AI_Disable` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `AI_DisableAec` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | T10/T20/T31: raptor†, timps; T21/T23/T41: raptor† | audit: reaches the driver/kernel |
| `AI_DisableAecRefFrame` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: reaches the driver/kernel |
| `AI_DisableAgc` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `AI_DisableAlgo` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `AI_DisableChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel; userspace implementation |
| `AI_DisableGetRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `AI_DisableHpf` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `AI_DisableHs` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `AI_DisableNs` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `AI_Enable` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `AI_EnableAec` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | T10/T20/T31: raptor†, timps; T21/T23/T41: raptor† | audit: reaches the driver/kernel |
| `AI_EnableAecRefFrame` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: reaches the driver/kernel |
| `AI_EnableAgc` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `AI_EnableAlgo` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `AI_EnableChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `AI_EnableGetRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `AI_EnableHpf` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `AI_EnableHs` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `AI_EnableNs` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `AI_Get/SetAlcGain` | n.a. | n.a. | done (?) | n.a. | done (?) | n.a. | timps | audit: reaches the driver/kernel; userspace implementation |
| `AI_Get/SetChnParam` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `AI_Get/SetDigitalGain` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `AI_Get/SetGain` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel; userspace implementation |
| `AI_Get/SetPubAttr` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel; userspace implementation |
| `AI_Get/SetVol` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel; userspace implementation |
| `AI_GetFrame` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel; userspace implementation |
| `AI_GetFrameAndRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `AI_GetFrameAndRef` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: reaches the driver/kernel; userspace implementation |
| `AI_PollingFrame` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `AI_ReleaseFrame` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `AI_SetAgcMode` | n.a. | n.a. | n.a. | n.a. | done (?) | n.a. | – | audit: userspace implementation |
| **► `AI_SetHpfCoFrequency`** | n.a. | n.a. | n.a. | cache-only | cache-only | cache-only | **prudynt†, raptor†** | T23+T31+T41: cutoff recorded, fixed 300 Hz HPF used |
| `AI_SetVolMute` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: reaches the driver/kernel; userspace implementation |
| `AI_Set_WebrtcProfileIni_Path` | n.a. | n.a. | n.a. | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `AO_CacheSwitch` | done (host) | done (host) | done (host) | done (host) | done (host) | done (host) | raptor† | all: implemented with vendor semantics, default off (OPENIMP_AO_CACHE=1 = vendor default on); quiet device test open (audio output only on T31) |
| `AO_ClearChnBuf` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | T10/T20/T31: raptor†, timps; T21/T23/T41: raptor† | audit: reaches the driver/kernel |
| `AO_Disable` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor† | audit: reaches the driver/kernel |
| `AO_DisableAgc` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `AO_DisableAlgo` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `AO_DisableChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor† | audit: userspace implementation |
| `AO_DisableHpf` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `AO_Enable` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor† | audit: reaches the driver/kernel |
| `AO_EnableAgc` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `AO_EnableAlgo` | n.a. | n.a. | n.a. | done (?) | n.a. | missing | – |  |
| `AO_EnableChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor† | audit: userspace implementation |
| `AO_EnableHpf` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `AO_FlushChnBuf` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor† | audit: reaches the driver/kernel |
| `AO_Get/SetDigitalGain` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| `AO_Get/SetGain` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor† | audit: reaches the driver/kernel; userspace implementation |
| `AO_Get/SetPubAttr` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor† | audit: reaches the driver/kernel; userspace implementation |
| `AO_Get/SetVol` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor† | audit: reaches the driver/kernel; userspace implementation |
| `AO_PauseChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `AO_QueryChnStat` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `AO_ResumeChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `AO_SendFrame` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor† | audit: reaches the driver/kernel |
| `AO_SetHpfCoFrequency` | n.a. | n.a. | n.a. | done (?) | done (?) | done (?) | prudynt†, raptor† | audit: userspace implementation |
| `AO_SetVolMute` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor† | audit: reaches the driver/kernel; userspace implementation |
| `AO_Soft_Mute` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: reaches the driver/kernel; userspace implementation |
| `AO_Soft_UNMute` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: reaches the driver/kernel; userspace implementation |
| `Audio_Select_Codec` | n.a. | n.a. | n.a. | n.a. | n.a. | missing | – |  |
| **► `DMIC_*` (20 functions)** | n.a. | n.a. | n.a. | n.a. | done (?) | error | **raptor†** | functions: Disable, DisableAec, DisableChn, Enable, EnableAec, EnableAecRefFrame, EnableChn, Get/SetChnParam, GetFrame, GetFrameAndRef, Get/SetGain, Get/SetPubAttr, Get/SetVol, PollingFrame, ReleaseFrame, SetUserInfo. T41: ENOTSUP stub (returns -1) |
| **► `DMIC_DisableAecRefFrame`** | n.a. | n.a. | n.a. | n.a. | n.a. | error | **raptor†** | T41: ENOTSUP stub (returns -1) |

</details>

### System / sysutils / log

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by | Note |
|---|---|---|---|---|---|---|---|---|
| `Log_Get_Option` | done (?) | done (?) | done (?) | done (?) | done (?) | missing | – |  |
| `Log_Set_Option` | missing | missing | missing | missing | missing | missing | – |  |
| `SU_Base_SetWkupMode` | n.a. | n.a. | n.a. | n.a. | n.a. | error | – | T41: writes the mode number to /sys/power/state; neo PR #1 covers related struct overflows, unmerged |
| `SU_Base_Shutdown` | error | error | error | error | error | error | – | all: kill(1,SIGCHLD) does not power off busybox init; fix in neo PR #1 (SIGUSR2), unmerged; no streamer uses it |
| `SU_Battery_GetCapacity` | missing | missing | missing | missing | missing | missing | – |  |
| `SU_Battery_GetEvent` | missing | missing | missing | missing | missing | missing | – |  |
| `SU_Battery_GetStatus` | missing | missing | missing | missing | missing | missing | – |  |
| `SU_Battery_GetVoltageUV` | missing | missing | missing | missing | missing | missing | – |  |
| `System_MemPoolRequest` | n.a. | n.a. | n.a. | done (host) | done (host) | missing | – | T23+T31: real memory pools (claude/t23t31-cacheonly, not in agg-25) |

<details><summary>All 53 rows of this area (9 with a gap)</summary>

*Legend: **done (dev)** = device-tested on that SoC · **done (host)** = host tests only · **done (?)** = connected per static audit, not device-tested · **vendor no-op** = the vendor itself does nothing · **cache-only** = value only stored · **stub** = returns 0, no effect · **error** = fails or known defect · **missing** = not exported by OpenIMP · **n.a. (not in vendor API)** = function does not exist in that SoC's vendor API · **?** = unknown · **►** = streamer uses it, gap · **†** = streamer usage from source only.*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by | Note |
|---|---|---|---|---|---|---|---|---|
| `Log_Get_Option` | done (?) | done (?) | done (?) | done (?) | done (?) | missing | – |  |
| `Log_Set_Option` | missing | missing | missing | missing | missing | missing | – |  |
| `SU_ADC_DisableChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_ADC_EnableChn` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_ADC_Exit` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_ADC_GetChnValue` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Base_DisableAlarm` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Base_EnableAlarm` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Base_Get/SetAlarm` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Base_Get/SetTime` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Base_GetDevID` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Base_GetModelNumber` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Base_GetVersion` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor† | audit: sysfs/ioctl/syscall path |
| `SU_Base_PollingAlarm` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Base_Raw2SUTime` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Base_Reboot` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Base_SUTime2Raw` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Base_SetWkupMode` | n.a. | n.a. | n.a. | n.a. | n.a. | error | – | T41: writes the mode number to /sys/power/state; neo PR #1 covers related struct overflows, unmerged |
| `SU_Base_Shutdown` | error | error | error | error | error | error | – | all: kill(1,SIGCHLD) does not power off busybox init; fix in neo PR #1 (SIGUSR2), unmerged; no streamer uses it |
| `SU_Base_Suspend` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Battery_GetCapacity` | missing | missing | missing | missing | missing | missing | – |  |
| `SU_Battery_GetEvent` | missing | missing | missing | missing | missing | missing | – |  |
| `SU_Battery_GetStatus` | missing | missing | missing | missing | missing | missing | – |  |
| `SU_Battery_GetVoltageUV` | missing | missing | missing | missing | missing | missing | – |  |
| `SU_CIPHER_ConfigHandle` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_CIPHER_CreateHandle` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_CIPHER_DES_Exit` | n.a. | n.a. | n.a. | n.a. | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_CIPHER_DES_Init` | n.a. | n.a. | n.a. | n.a. | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_CIPHER_DES_Test` | n.a. | n.a. | n.a. | n.a. | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_CIPHER_Decrypt` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_CIPHER_DestroyHandle` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_CIPHER_Encrypt` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_CIPHER_Exit` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_CIPHER_Init` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Key_CloseEvent` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Key_DisableEvent` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Key_EnableEvent` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Key_OpenEvent` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_Key_ReadEvent` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `SU_LED_Command` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | – | audit: sysfs/ioctl/syscall path |
| `System_Bind` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `System_Exit` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel; userspace implementation |
| `System_GetBindbyDest` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: userspace implementation |
| `System_GetCPUInfo` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor† | audit: reaches the driver/kernel; userspace implementation |
| `System_GetTimeStamp` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor†, timps | audit: userspace implementation |
| `System_GetVersion` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `System_Init` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: reaches the driver/kernel |
| `System_MemPoolFree` | n.a. | n.a. | n.a. | n.a. | done (host) | n.a. | – | T31: real memory pools (claude/t23t31-cacheonly, not in agg-25) |
| `System_MemPoolRequest` | n.a. | n.a. | n.a. | done (host) | done (host) | missing | – | T23+T31: real memory pools (claude/t23t31-cacheonly, not in agg-25) |
| `System_ReadReg32` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: reaches the driver/kernel |
| `System_RebaseTimeStamp` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor† | audit: userspace implementation |
| `System_UnBind` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | prudynt†, raptor†, timps | audit: userspace implementation |
| `System_WriteReg32` | done (?) | done (?) | done (?) | done (?) | done (?) | done (?) | raptor† | audit: reaches the driver/kernel |

</details>

### Notes

- **T21 imgfx findings without a row of their own:** Sinter/Temper strength, Sepia (-1), Vivid, ISP bypass bits lsc/dpc/sdns/mdns/sharpen/defog and the FrameSource ch1 scaler crop showed no change in the 2026-10-05 imgfx run (agg-25, cam-J/PC420 #1); the cause (vendor no-op, driver, measurement) was not analysed: unverified. ISP flip and `SetWB` auto give a magenta picture in imgfx on T21 (timps' own flip path is fine): open, not a missing function.
- **OSD, behaviour rather than a missing function:** on T21 the IPU blend is ineffective for about 2 s after a wake from idle; `claude/t21-osd-first-jpeg` (in agg-26) withholds JPEG frames without a confirmed overlay, so the first snapshot after a start now has the OSD (device-tested 2026-10-05, first snapshot about 2 s later). The original stack shows the same missing first-snapshot OSD; the root cause (IPU/OSD group/clock) is not found. T21 OSD can still appear late after a start (2-3 min seen with several starts): unverified cause.
- **IVS:** no vendor IVS function is marked missing or incomplete in the audit. (The IVS GetParam/SetParam overflow found on 2026-10-05 is in raptor-hal, not in OpenIMP.)
- **Not determined:** whether the T41 vendor handles `SetScalerLv`/`SetMaskBlock` (the open driver has no handler); T10 device effect of every T20/T21 fix; the _Sec/MultiCamera_ variants after the T23 fixes; vendor semantics of absolute-QP ROI under CBR on T20; T41 DMIC, ISP-OSD and I2D functions were not looked at again after the audit (all still error/missing); the T10/T20/T21 `GetChnRcAttr`-style missing encoder calls were not re-checked.

