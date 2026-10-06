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

| # | Feature | T10 | T20 | T21 | T23 | T31 | T41 |
|--:|---|---|---|---|---|---|---|
| | **ISP core and image pipeline** | | | | | | |
| 1 | [ISP core / sensor bring-up](#1-isp-core--sensor-bring-up) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 2 | [Reload / error handling (rmmod, stop/start)](#2-reload--error-handling-rmmod-stopstart) | ✅+ | ✅+ | ✅+ | ✅+ | ✅+ | ✅ |
| 3 | [Boot guard (protection against boot loops)](#3-boot-guard-protection-against-boot-loops) | ✅+ | ✅ | ✅+ | ✅ | ✅ | — |
| | **Exposure (AE)** | | | | | | |
| 4 | [AE control](#4-ae-control) | ✅ | ✅ | ✅+ | ✅ | ✅ | ✅ |
| 5 | [AE compensation, backlight, highlight](#5-ae-compensation-backlight-highlight) | ✅ | ✅ | ✅+ | ✅ | ✅ | ✅ |
| 6 | [Max gain, IT max, sensor FPS](#6-max-gain-it-max-sensor-fps) | ✅ | ✅ | ✅+ | ✅ | ✅ | ✅ |
| 7 | [Anti-flicker (50/60 Hz)](#7-anti-flicker-5060-hz) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| | **Colour and image quality** | | | | | | |
| 8 | [White balance (AWB, presets, manual)](#8-white-balance-awb-presets-manual) | ✅ | ✅ | ✅+ | ✅ | ✅ | ✅ |
| 9 | [CCM / LSC (lens shading)](#9-ccm--lsc-lens-shading) | ✅ | ✅ | ✅+ | ✅ | ✅ | ❌ |
| 10 | [Day/night switching (ISP side)](#10-daynight-switching-isp-side) | ✅ | ✅ | ✅ | ✅+ | ✅ | ✅ |
| 11 | [IR cut / IR LED](#11-ir-cut--ir-led) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 12 | [Brightness / contrast / saturation / sharpness / hue](#12-brightness--contrast--saturation--sharpness--hue) | ✅ | ✅ | ✅+ | ✅ | ✅ | ✅ |
| 13 | [Mirror / flip](#13-mirror--flip) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 14 | [WDR / ADR / DRC](#14-wdr--adr--drc) | ✅ | ✅+ | ✅+ | ✅+ | ✅ | ❌ |
| 15 | [Defog](#15-defog) | ✅ | ✅+ | ✅+ | ✅+ | ✅ | ❌ |
| 16 | [Noise reduction (2DNR/3DNR, Sinter, Temper)](#16-noise-reduction-2dnr3dnr-sinter-temper) | ✅+ | ✅+ | ✅+ | ✅ | ✅ | ⚠️ |
| 17 | [DPC (defect pixels)](#17-dpc-defect-pixels) | ✅+ | ✅+ | ✅+ | ✅ | ✅ | ❌ |
| 18 | [Scene mode / colour effects (B/W, negative, sepia, vivid)](#18-scene-mode--colour-effects-bw-negative-sepia-vivid) | ✅+ | ✅ | ✅+ | ✅ | ✅ | ❌ |
| 19 | [Privacy mask (ISP hardware block)](#19-privacy-mask-isp-hardware-block) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 20 | [Front crop / scaler level / CSC presets](#20-front-crop--scaler-level--csc-presets) | ✅ | ✅ | ✅ | ✅ | ✅ | ❌ |
| 21 | [Rotation 90°/270°](#21-rotation-90270) | — | — | — | ✅ | ✅ | ❌ |
| | **Video encoder and streams** | | | | | | |
| 22 | [H.264](#22-h264) | ✅ | ✅ | ✅ | ✅+ | ✅ | ✅ |
| 23 | [H.265 / HEVC](#23-h265--hevc) | — | — | — | — | ✅ | ✅ |
| 24 | [JPEG / MJPEG / snapshot](#24-jpeg--mjpeg--snapshot) | ✅ | ✅ | ✅+ | ✅ | ✅ | ✅ |
| 25 | [Sub-stream / scaler (ch1 640×360)](#25-sub-stream--scaler-ch1-640360) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 26 | [Rate-control mode (CBR/VBR/FixQP/Capped*/SMART)](#26-rate-control-mode-cbrvbrfixqpcappedsmart) | ✅+ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 27 | [RC parameters (QP steps, staticTime, changePos, qualityLvl, I bias)](#27-rc-parameters-qp-steps-statictime-changepos-qualitylvl-i-bias) | ✅ | ✅ | ✅ | ✅ | ✅ | ⚠️ |
| 28 | [OSD: text, bitmap, rectangle, line, cover](#28-osd-text-bitmap-rectangle-line-cover) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 29 | [IVS / motion detection](#29-ivs--motion-detection) | ✅ | ✅+ | ✅ | ✅ | ✅+ | ✅ |
| 30 | [Frame source / VBM pool](#30-frame-source--vbm-pool) | ✅ | ✅ | ✅+ | ✅ | ✅ | ✅ |
| | **Audio (documentation only, no tests)** | | | | | | |
| 31 | [Audio input (AI)](#31-audio-input-ai) | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| 32 | [Audio output (AO, speaker)](#32-audio-output-ao-speaker) | ? | ? | ? | ? | ✅ | ? |
| 33 | [Echo cancellation (AEC)](#33-echo-cancellation-aec) | ? | ? | ? | 🔧 | ✅+ | ? |
| | **Memory, size, load** | | | | | | |
| 34 | [libimp size (code + data)](#34-libimp-size-code--data) | ✅+ | ✅+ | ✅+ | ✅+ | ✅+ | ✅+ |
| 35 | [Kernel module size](#35-kernel-module-size) | ✅ | ✅ | ✅+ | ✅+ | ✅+ | ✅ |
| 36 | [Video memory (rmem) / MemFree](#36-video-memory-rmem--memfree) | ✅ | ✅ | ✅+ | ✅+ | ✅ | ✅ |
| 37 | [Reference-frame sharing (BUF_SHARE_CFG)](#37-reference-frame-sharing-buf_share_cfg) | — | — | ✅+ | ✅+ | — | ? |
| 38 | [CPU load (documented figures)](#38-cpu-load-documented-figures) | ✅ | ✅ | ✅+ | ✅ | ✅ | ✅+ |
| | **Stability, helper libraries, telemetry** | | | | | | |
| 39 | [Kernel soc_vpu / Helix hardening](#39-kernel-soc_vpu--helix-hardening) | ✅+ | ✅+ | ✅+ | ✅+ | — | ✅ |
| 40 | [AVPU kernel driver (T31/T40/T41) review](#40-avpu-kernel-driver-t31t40t41-review) | — | — | — | — | ✅+ | ✅ |
| 41 | [Long-term stability / hangs](#41-long-term-stability--hangs) | ✅ | ✅ | ✅ | ✅ | ✅ | ⚠️ |
| 42 | [Helper libraries libalog / libsysutils](#42-helper-libraries-libalog--libsysutils) | ✅+ | ✅+ | ✅+ | ✅+ | ✅+ | ✅+ |
| 43 | [Tuning getters / readback](#43-tuning-getters--readback) | ✅ | ✅ | ✅+ | ✅ | ✅ | ✅ |
| 44 | [Encoder telemetry / diagnostics](#44-encoder-telemetry--diagnostics) | ✅+ | ✅+ | ✅+ | ✅+ | ✅+ | ⚠️ |

Columns T10..T41: one status symbol per SoC (legend above). The number and the feature name link to the detail section below, which holds the full per-SoC text and the vendor-stack behaviour.

### Matrix details

Per feature: the vendor-stack behaviour and the full per-SoC cell text (chronological test log, newest results win).


#### ISP core and image pipeline

##### 1. ISP core / sensor bring-up

- **Vendor stack:** tx-isp-*.ko loads sensor + tuning bin
- **T10:** ✅ open driver boots, boot guard auto; day/night 10x without oops
- **T20:** ✅ bring-up stable; vendor-format isp-m0
- **T21:** ✅ first open bring-up, ISP core = lifted vendor code
- **T23:** ✅ exposure readback live, ~45 empty CIDs wired up, unknown CIDs return -EINVAL
- **T31:** ✅ reference SoC; tuning gaps closed (RGB coefficients, AE ROI, SensorAttr)
- **T41:** ✅ runs the fully open stack from the `aperto` full OTA image (rootfs rev7, rmem 26M) since 2026-10-04; no oops; day/night and AE/AWB quality still untested

##### 2. Reload / error handling (rmmod, stop/start)

- **Vendor stack:** known oops on reload (T21 open counter, stats DMA)
- **T10:** ✅+ boot guard auto; reload cycles not documented individually
- **T20:** ✅+ rmmod during stream rejected; 10x stop/start + 10x reload, 0 oops; all 53 user copies checked
- **T21:** ✅+ 10x stop/start + rmmod/insmod, 0 oops; cause of the old oops (stats DMA into freed memory) fixed
- **T23:** ✅+ 10x stop/start incl. kill -9, 10x reload, 0 oops; 2 out-of-bounds writes (2 KB/18 KB) fixed
- **T31:** ✅+ 10x reload with kill -9; vmalloc leak of 252 KB/cycle fixed; residual drift ~45 KB/cycle
- **T41:** ✅ rev2 image: 10/10 rmmod/insmod cycles OK, refcnt 0, 0 oops; kill -9 of the streamer recovers 3/3 (root cause: decompiled tuning-node helper overwrote .bss) [claude/t41-matrix-fixes]

##### 3. Boot guard (protection against boot loops)

- **Vendor stack:** not present
- **T10:** ✅+ isp_open=auto
- **T20:** ✅ S10isp-guard + isp_open=auto: optional package in upstream thingino `aperto` (#1749, default off); active on the test cameras
- **T21:** ✅+ S10isp-guard, u-boot isp_open=manual|auto|off
- **T23:** ✅ S10isp-guard + isp_open=auto: optional package in upstream thingino `aperto` (#1749, default off); active on the test cameras
- **T31:** ✅ S10isp-guard + isp_open=auto: optional package in upstream thingino `aperto` (#1749, default off); active on the test cameras
- **T41:** —


#### Exposure (AE)

##### 4. AE control

- **Vendor stack:** vendor AE in the kernel
- **T10:** ✅ device-tested: ae_comp moves IT and gain (374→748 lines, gain 42→142), clamps
- **T20:** ✅ compact AE: max gain, max IT (ae_it_max_us now effective), line_us, scene IT limit; low-light AE at dusk untested
- **T21:** ✅+ AE lifted 1:1 from vendor (incl. ae_tune2), night flicker gone; a·b·b bug found
- **T23:** ✅ lifted vendor AE is the default (all-14/15); night test on cam-B: switches to night, AE regulates (IT 1200/1436 lines, analog gain 133/160), gain reported; backlight/highlight/AE comp act
- **T31:** ✅ reference SoC; 2 h 53 soak without errors
- **T41:** ✅ AE regulates correctly on cam-F (it was saturated at max gain only because the room was dark); day/night and AE/AWB quality across a full day still untested

##### 5. AE compensation, backlight, highlight

- **Vendor stack:** IMP_ISP_Tuning_SetAeComp / Backlight / Highlight
- **T10:** ✅ device-tested: ae_comp 30/230 moves Y by −78/+143, highlight works; backlight cap absent
- **T20:** ✅ device-tested: ae_comp (IT 40→562 lines, gain 0→46); highlight has a small effect; backlight not tested (reflash pending)
- **T21:** ✅+ vendor dispatcher lifted; individual test not documented
- **T23:** ✅ device-tested with the lifted vendor AE: backlight 10 luma 68→112, highlight 10 →48, AE comp works (decided: lifted vendor AE becomes the default after a pending night-switch test in the dark) [claude/t23-matrix-gaps]
- **T31:** ✅ device-tested: ae_comp (Y +13/−21), backlight (Y +19), highlight (Y −13)
- **T41:** ✅ AE compensation reaches the ISP through OpenIMP (`AeScenceAttr.AeTargetComp`), device-tested: comp 2 lowers the target 65 → 1; wired in timps main. Backlight (BLC) and highlight (HLC) are not supported on T41: the driver now returns "not supported" instead of a silent success

##### 6. Max gain, IT max, sensor FPS

- **Vendor stack:** MaxAgain/MaxDgain/AE_IT_MAX/SetSensorFPS
- **T10:** ✅ max_again and ae_it_max_us limit the AE
- **T20:** ✅ MaxAgain clamped, line_us=29 reported; ae_it_max_us limits the AE
- **T21:** ✅+ ae_it_max_us acts (claude/t21-ae-it-max 840a57ff; beyond vendor, the vendor ignores the RANGE block; the user decided to keep it): cam-D cap 2000 us gives IT 68 lines and dgain 19 to 63, cap 5000 us gives 172 lines, cap 0 returns to 1125 lines; max gain acts as before. Caveat: a 4th module reload in the same boot gave segfaults and a watchdog reboot (under investigation)
- **T23:** ✅ MaxAgain/MaxDgain, IT max, SetSensorFPS, additional ISP digital-gain stage; t23tune passed
- **T31:** ✅ EXPR setter, AE ROI, histogram edges
- **T41:** ✅ gain and exposure caps reach the ISP through OpenIMP (`AeExprInfo`: `AeMaxAGain` linear Q10, `AeMaxIntegrationTime` in sensor lines), device-tested: 8x cap holds 6.9x; timps wiring follows

##### 7. Anti-flicker (50/60 Hz)

- **Vendor stack:** POWER_LINE / flicker dispatcher
- **T10:** ✅ device-tested: IT 748/675/896 lines for 60 Hz/50 Hz/off
- **T20:** ✅ device-tested: IT 843/1011/1012 lines for 60 Hz/50 Hz/off
- **T21:** ✅ lifted vendor dispatcher
- **T23:** ✅ device-tested: vendor AE 50/60/off IT 720/900/971; HLIL 720/600/711
- **T31:** ✅ device-tested with 22 ms IT cap: IT 1000/900/750 lines off/50/60 Hz, gain compensates; daylight test pending
- **T41:** ✅ device-tested: off/50/60 Hz readback in isp-m0, AE integration time follows (2092/1575/1750 lines)


#### Colour and image quality

##### 8. White balance (AWB, presets, manual)

- **Vendor stack:** vendor AWB chain
- **T10:** ✅ device-tested: manual R/B gains, Cb/Cr −21..−28
- **T20:** ✅ default simple AWB; daylight A/B vs vendor chain: gains 492/393 vs 488/395, neutral ROIs within 0.007, both converge < 4 s; artificial light sweep 2200-6500 K (2026-10-04, smart bulbs): AWB follows (CT estimate 2300/2300/2500/4100 K), 4000 K neutral (R/G 0.99), very warm light stays slightly warm (lower limit ~2300 K, typical)
- **T21:** ✅+ AWB lifted (10/10 scenes register-identical) + hysteresis + IR night freeze; dusk test open
- **T23:** ✅ [-all-13] daylight green cast fixed on claude/t23-day-color (bc70f10b), tested on cam-B in sunlight: neutral colours, WB gains kept (0x710/0x7c0); not flashed yet
- **T31:** ✅ device-tested: manual R/B gains + presets 3/4/7 read back; chroma follows (Cb/Cr), auto restores
- **T41:** ✅ black-picture incident not reproducible; timps does not call any WB function on T41 (only AWB attr in libimp)

##### 9. CCM / LSC (lens shading)

- **Vendor stack:** CT-controlled
- **T10:** ✅ CCM/LSC read back in isp-m0 (0x13380480.., 0x380–0x39c); CCM now updated every frame like vendor (was frozen at init matrix in IR scenes), follows day/night; LSC bypassed by the jxh42 IQ bank (vendor-identical); per-CT sweep pending daylight [claude/t1x-ccm-lsc-iridix]
- **T20:** ✅ CCM/LSC read back in isp-m0; CCM follows day/night (mono at night), LSC enabled, strength 1024 day/3440 night; mesh mirror follows ISP hflip at mode reload (vendor-identical); per-CT sweep pending daylight [claude/t1x-ccm-lsc-iridix]
- **T21:** ✅+ CT-controlled CCM/LSC lifted; colour blotches gone (chroma sigma 30→6)
- **T23:** ✅ CCM follows the IQ bank (as vendor); daylight with vendor AE neutral (sun, 2026-10-04: R/G 0.94, B/G 0.93, no green or blue cast). Green cast fixed earlier (WB gains reset on every stream start, claude/t23-day-color); blue AWB flip with vendor AE fixed (GIB black level cleared by the stream-enable write, claude/t23-ae-awb-flip); LSC flip locked
- **T31:** ✅ CCM follows day/night (regs 0x5004-0x5018 differ), LSC LUT loaded and follows mode + flip; per-CT sweep not testable on a fixed scene
- **T41:** ❌ CCM is not supported on T41 (the driver returns "not supported" instead of a silent success)

##### 10. Day/night switching (ISP side)

- **Vendor stack:** bank switch + mono matrix
- **T10:** ✅ 10 switches without oops; not re-tested after the drift fix
- **T20:** ✅ BCSH + Sinter/Temper re-sent as vendor does; Wyze day 128 / night 148/140
- **T21:** ✅ night mono (chroma 0), gain stable instead of 6↔25
- **T23:** ✅+ oops (wait queue) fixed, night mono, bank error → block bypass, user bypass persists
- **T31:** ✅ 13 switches in the 4.5 h soak
- **T41:** ✅ isp-m0 shows the run mode; forced switch test pending

##### 11. IR cut / IR LED

- **Vendor stack:** via GPIO through timps/Thingino
- **T10:** ✅ device-tested: daynight night/day switches ircut and ir850, ISP follows
- **T20:** ✅ device-tested: as T10
- **T21:** ✅ device-tested: as T10
- **T23:** ✅ device-tested: timps auto night switches IR cut + ir850 + mono
- **T31:** ✅ device-tested: daynight night/day switches ircut + ir940 (no ir850 pin on this cam), ISP follows
- **T41:** ✅ device-tested: ircut + ir850 toggle; ISP mode not readable

##### 12. Brightness / contrast / saturation / sharpness / hue

- **Vendor stack:** IMP_ISP_Tuning_Set*
- **T10:** ✅ device-tested: brightness (+150 Y), contrast, saturation, sharpness; hue cap absent
- **T20:** ✅ defaults 0x80; sharpness works (edge energy 13/230/700); Wyze image.sharpness=128
- **T21:** ✅+ getters lifted; sharpness/contrast readable
- **T23:** ✅ brightness/contrast/saturation/hue act now (they were reset on every stream start); contrast/gain feedback: driver takes the low byte like the vendor, OpenIMP remembers the gain before sending (user contrast 100 stays) (claude/t23-bcsh-aeit-fix)
- **T31:** ✅ defaults 0x80
- **T41:** ✅ brightness 255 → Y 211, contrast 0 → flat grey, saturation 0/255 chroma 0.1/7.1 (dark scene) [claude/t41-matrix-fixes]

##### 13. Mirror / flip

- **Vendor stack:** SetHVFlip / sensor flip
- **T10:** ✅ device-tested: flipped image correlation 0.964 on both flips; isp-m0 Mirror/Flip line now shows the applied state (was always Enable)
- **T20:** ✅ vflip UV address + DMA overwrite fixed (pink stripes); isp-m0 Mirror/Flip line now shows the applied state (was always Enable)
- **T21:** ✅ flip dispatcher lifted; shvflip=1
- **T23:** ✅ flip, Bayer re-sync, LSC flip locked
- **T31:** ✅ sc4336p vflip no longer reports an error [-all-13]; MSCA flip takes effect only at the next channel start, as with the vendor
- **T41:** ✅ sensor flip registers follow live (hflip → 0x022c=0x01, vflip → 0x0063=0x02, off → 0x00); picture check in daylight pending [claude/t41-matrix-fixes]

##### 14. WDR / ADR / DRC

- **Vendor stack:** ADR/DRC/WDR paths in the vendor driver
- **T10:** ✅ isp-m0 WDR flag fixed (LINEAR 0x0e was printed as Enable) [claude/t10-t20-nr-wdr]; no DRC cap
- **T20:** ✅+ DRC strength drives auto Iridix ratio: Y 92/96/122, laplacian 555/558/656 at 0/128/255; isp-m0 WDR flag fixed [all-17]
- **T21:** ✅+ ADR lifted (40/40 emulator), DRC reaches the driver; day Y 120 instead of 235
- **T23:** ✅+ dynamic ADR lifted from the vendor module (44/44 emulator-identical, claude/t23-adr-defog); DRC strength 0/255 visibly effective on cam-B; sc2336 has no WDR mode
- **T31:** ✅ WDR buffer lazy; AE1 (short frame) stub = vendor no-op without WDR sensor
- **T41:** ❌ WDR and DRC are not supported on T41 (the driver returns "not supported" instead of a silent success)

##### 15. Defog

- **Vendor stack:** vendor block
- **T10:** ✅ vendor-identical: the jxh42 IQ bank bypasses Iridix (day and night), so DRC/defog have no picture effect – same as the vendor. Forcing Iridix on was measured (Y +1.4, edges +10 %) and rejected by the maintainer as not worth it.
- **T20:** ✅+ Iridix floor (no defog block in HW): 255 → Y +27, laplacian +106 [all-17]
- **T21:** ✅+ lifted, IRQ 21 registered (40/40 emulator)
- **T23:** ✅+ lifted incl. tisp_defog_soft_process (emulator-identical), IRQ 20 + process running on cam-B, defog strength works; 0xc bit 11 follows the bank like stock
- **T31:** ✅ device-tested: defog 255 (Y −10, laplacian +193)
- **T41:** ❌ defog is not supported on T41 (the driver returns "not supported" instead of a silent success)

##### 16. Noise reduction (2DNR/3DNR, Sinter, Temper)

- **Vendor stack:** table-driven
- **T10:** ✅+ Sinter/Temper strength acts (vendor: no-op): temporal noise 7.11/2.91/1.51 at temper 0/128/255, survives day/night [claude/t10-t20-nr-wdr]
- **T20:** ✅+ Sinter/Temper take effect (vendor: no-op) [claude/t10-t20-nr-wdr + openimp claude/t20-nr-strength]: temper 0/64/128/200 → 0/42/85/132, sinter 0/17/35/69 at high gain; 128 = IQ; kept across day/night
- **T21:** ✅+ 2DNR/gain tracking repaired; Sinter/Temper strength now takes effect (vendor ignores it) [-all-13]
- **T23:** ✅ gain index now log2 (before: noise reduction too strong from 2x), Sinter non-compounding, sharpness/DPC follow the bank
- **T31:** ✅ vendor-identical: SDNS H-S regs 0→0, 255→15 (OEM cap 16); temper 0: temporal std 4.60 vs 1.64; sinter effect small by OEM design
- **T41:** ⚠️ 2D noise reduction (sinter) works through OpenIMP (`Module_Ratio` index 0), device-tested: sinter 255 cuts wall noise from ~7 to ~1; wired in timps main. 3D noise reduction (temper, index 1) shows no measurable effect yet and is being checked in the driver

##### 17. DPC (defect pixels)

- **Vendor stack:** vendor block
- **T10:** ✅+ DPC strength via open driver (vendor: no-op): impulses 2846/2397/1784 at 0/128/255 [claude/t1x-beyond-vendor-ctrls, in all-17]
- **T20:** ✅+ impulses 6628/5153/3667 at 0/128/255 [all-17]
- **T21:** ✅+ m1 thresholds scaled like OEM T23; impulses 3857/3841/3498 [all-17]
- **T23:** ✅ follows the IQ bank as vendor, less night noise
- **T31:** ✅ vendor-identical: m1/m3 thresholds 0→(d1000,f5), 255→(d5,f1191); impulses −6 % (defect pixels only)
- **T41:** ❌ DPC is not supported on T41 (the driver returns "not supported" instead of a silent success)

##### 18. Scene mode / colour effects (B/W, negative, sepia, vivid)

- **Vendor stack:** SetSceneMode/SetColorfxMode (no-op in the vendor on T21)
- **T10:** ✅+ NEGATIVE/BW work, scene presets act (TEXT laplacian +14 %) [all-17]
- **T20:** ✅ colorfx 0–3 set/get ok, sepia visible; scene ok
- **T21:** ✅+ B/W, vivid, negative work (confirmed with light on); getters return what was set [-all-13]
- **T23:** ✅ device-tested: B/W, vivid, negative work; invalid values give EINVAL (claude/t23-t31-scene-colorfx, not yet in an aggregate)
- **T31:** ✅ device-tested: as T23 (claude/t23-t31-scene-colorfx, not yet in an aggregate)
- **T41:** ❌ no control path on T41 (no cap, no libimp function)

##### 19. Privacy mask (ISP hardware block)

- **Vendor stack:** 4 rectangles/channel, YUV fill
- **T10:** ✅ device-tested: both streams, green fill
- **T20:** ✅ device-tested: both streams, green fill
- **T21:** ✅ device-tested: both streams, green fill
- **T23:** ✅ device-tested: both streams, green fill
- **T31:** ✅ as vendor, follows mirror/flip; emulator 400/400 identical; cam-A black+red ok [-all-13]
- **T41:** ✅ device-tested on chn1 (chn0 value missing because the camera rebooted in that run)

##### 20. Front crop / scaler level / CSC presets

- **Vendor stack:** SetFrontCrop, CSC, BLC
- **T10:** ✅ scaler device-tested (chn1 480x272, 25.0 fps); front crop / CSC: no control path
- **T20:** ✅ scaler device-tested (640x360 and 480x272, 15.0 fps); front crop / CSC: no control path
- **T21:** ✅ scaler device-tested (480x272, 24.9 fps); front crop / CSC: no control path
- **T23:** ✅ front crop via vendor path (960x540 crop ok); MASK -EINVAL as vendor (no stock handler)
- **T31:** ✅ BLC get, CSC presets 0–4 + user matrix, crop get/set, scaler level (tested on cam-A)
- **T41:** ❌ crop (I2D) still open: no control path

##### 21. Rotation 90°/270°

- **Vendor stack:** software rotation (32×32 tiles)
- **T10:** — coerced to 0 ("unsupported on this SoC")
- **T20:** — coerced to 0 ("unsupported on this SoC")
- **T21:** — coerced to 0 ("unsupported on this SoC")
- **T23:** ✅ sub-stream rotation 90/270 works via the native encoder; main stream above 704x576 refused (software rotation); IMP_Encoder_YuvSetCrop implemented (host-tested only, timps does not call it)
- **T31:** ✅ 704×1280 correct, 9 ms/frame @15 fps, before OSD/IVS/encoder
- **T41:** ❌ open: hardware I2D path not enabled (timps build coerces 90/180/270 to 0)


#### Video encoder and streams

##### 22. H.264

- **Vendor stack:** Helix (T20/T21/T23), AVPU (T31), NVPU (T10)
- **T10:** ✅ 720p 25 fps 1501 frames error-free; drift bug (margin added twice) fixed; own command list
- **T20:** ✅ soak 1 h 44, 156,517 frames/stream, 0 errors
- **T21:** ✅ main+sub+MJPEG, 25 fps; EMC scratch as vendor (1080p 996 KiB)
- **T23:** ✅+ native without helixd/OEM libimp: 2 h 34, 231,668 frames, 0 decode errors, ~6 % CPU
- **T31:** ✅ soak 2 h 53, 260,648 frames, 1030/1030 snapshots, 0 errors
- **T41:** ✅ cam-F with the fully open stack: main stream High profile 1920x1080 (earlier undecodable 1080p was the rmem exhaustion, fixed by the rmem best-fit), sub stream ok, no oops; repo docs: 2560×1440 H.264 verified on a different T41 device

##### 23. H.265 / HEVC

- **Vendor stack:** T31/T41: AVPU; T10/T20/T21/T23: no HEVC hardware (Helix is H.264/JPEG only; Radix only on T30) – vendor libimp creates an empty channel that never encodes
- **T10:** — — no HEVC hardware; OpenIMP rejects PT_H265 with -1 and a clear log (vendor: silent empty channel) [claude/h265-reject]
- **T20:** — — no HEVC hardware; OpenIMP rejects PT_H265 with -1 and a clear log (vendor: silent empty channel) [claude/h265-reject]
- **T21:** — — no HEVC hardware; OpenIMP rejects PT_H265 with -1 and a clear log (vendor: silent empty channel) [claude/h265-reject]
- **T23:** — — no HEVC hardware; OpenIMP rejects PT_H265 with -1 and a clear log (vendor: silent empty channel) [claude/h265-reject]
- **T31:** ✅ real HEVC on AVPU (VPS/SPS/PPS, CABAC); 2×900 frames, 0 errors
- **T41:** ✅ AVPU HEVC path (as T31): 1080p H.265 decodes clean, a stuck AVPU job times out after 2 s and resets the core [claude/t41-h265, rev5 image]

##### 24. JPEG / MJPEG / snapshot

- **Vendor stack:** hardware JPEG via vendor libimp/helixd
- **T10:** ✅ snapshots + MJPEG 25 B/5 s; JPEG buffer 1 MiB (−328 KiB)
- **T20:** ✅ HW JPEG, MJPEG 25 B/5 s with/without video consumer
- **T21:** ✅+ HW JPEG without vendor lib, 37 ms/job, snapshots 0.05–0.18 s; stripes with RST markers
- **T23:** ✅ HW JPEG 29 ms/job; q75 = IJG tables, size matches libjpeg (scene-driven)
- **T31:** ✅ HW JPEG, own MJPEG channel ok (24 B/5 s, before 0 bytes)
- **T41:** ✅ 1080p + 640x360 snapshots ok on cam-F with the open driver (claude/t41-gc5603-fix); MJPEG and the grey-JPEG TODO (T40/T41) not re-checked

##### 25. Sub-stream / scaler (ch1 640×360)

- **Vendor stack:** scaler in the ISP
- **T10:** ✅ ch1 shows the full scene (DS1 horizontal ratio bug in the shared firmware fixed)
- **T20:** ✅ ch1 different scaler path, no regression
- **T21:** ✅ WebRTC main↔sub switching confirmed (rmem fix), 32+40 cycles
- **T23:** ✅ idle teardown bug (motion detection without frames) fixed
- **T31:** ✅ cam-A 25 fps ch0+ch1 4.5 h
- **T41:** ✅ 640x360 snapshot and MP4 ok on cam-F with the open driver (claude/t41-gc5603-fix)

##### 26. Rate-control mode (CBR/VBR/FixQP/Capped*/SMART)

- **Vendor stack:** all modes in the vendor libimp
- **T10:** ✅+ OEM controller (OPENIMP_T10_RC=1) with the super-frame fix on by default (OPENIMP_T10_RC_SUPERFRM=0 = vendor-exact): at 1200 kbit/s 450→822, re-encodes 800→0, CPU 8.3→5.5 % (claude/t10-rc-superfrm); OEM rate controller is the default (device-tested on cam-E)
- **T20:** ✅ vendor-identical OEM controller is the default (claude/t1x-oem-rc-default-a13): CBR 1300 (P2 I-aware budget), VBR 1044, SMART 1019 at 1200 kbit/s; quality_lvl 0/6 → 1130/800 kbit/s, change_pos 50/100 → 850/1210 kbit/s, also live via /control; decode clean, 0 oops
- **T21:** ✅ vendor-identical T21 eprc is the default (0 oracle deviations); cam-D 1200 kbit/s: CBR 1326, VBR 1096, SMART 1071 [claude/eprc-t21-default]; eprc complete: SMART/CBR/VBR at 1200 kbit/s → 1090/1305/1042; runtime HSkip N=4 gives an IDR every 4 GOPs; decode clean, 0 oops [claude/eprc-complete]; scene-cut IDR not triggered by a day/night switch (vendor condition: scene class 5); MB-level RC ported (claude/eprc-mbrc 9e2bc3a, a8b483a: 0x400c0/0x400c4 per picture type like the vendor; device test running)
- **T23:** ✅ eprc controller: 60 s at 1200 kbit/s, decode clean: SMART 1141, CBR 1253, VBR 1255 (claude/eprc-t21-t23, not yet in an aggregate); MB-level RC ported (claude/eprc-mbrc 9e2bc3a, a8b483a: 0x400c0/0x400c4 per picture type like the vendor; device test running)
- **T31:** ✅ all modes via the vendor Allegro core (default): CBR 1210 kbit/s at 1200 target and 2973 at 3000 (legacy controller 1511 / 3786, +26 % with large peaks); VBR 1163, CappedVBR 1177, CappedQuality 1174 at 1200; decode clean, 0 oops [claude/t31-allegro-cbr]. No filler NAL is written (filler=0 in the logs also at 3000), so in practice there was no difference.
- **T41:** ✅ bitrate 400/1200/3000 → 518/1195/2777 kbit/s (30 s each) [claude/t41-cbr-overshoot]

##### 27. RC parameters (QP steps, staticTime, changePos, qualityLvl, I bias)

- **Vendor stack:** via IMP attr
- **T10:** ✅ device-tested: min/max QP and I bias read back in the encoder RC; quality_lvl/change_pos only in the video readback
- **T20:** ✅ readback returns the vendor-clamped values (staticTime 1, changePos 50, qualityLvl 0, QP steps 2/2 when the app passes 0) (claude/rc-modes) [-all-13]
- **T21:** ✅ readback as T20 [-all-13]
- **T23:** ✅ uses the vendor CreateChn clamps (1/50/2/2); parameters reach the native encoder (changePos min 50); app value 0 = vendor default 3/15/2/80 [-all-13]
- **T31:** ✅ T31 defaults like the vendor (max QP 48, max bitrate 4/3, ...) [-all-13] (claude/rc-modes-2)
- **T41:** ⚠️ device-tested: min/max QP live and effective; quality_lvl/change_pos/i_bias restart-only (readback in video block)

##### 28. OSD: text, bitmap, rectangle, line, cover

- **Vendor stack:** IPU OSD / vendor libimp
- **T10:** ✅ device-tested: 4 items (text, uptime, logo) on both streams
- **T20:** ✅ text/bitmap/lines/rectangles on both streams, clipping, 0 oops (IPU OSD hook)
- **T21:** ✅ IPU OSD hook as T20; rect/line/bitmap
- **T23:** ✅ device-tested: all 4 items, text edit
- **T31:** ✅ IPU OSD; lines/rectangles; rotation: no OSD clamp in timps
- **T41:** ✅ works on cam-F with the open stack (claude/t41-libimp): PIC/COVER via IPU, text/line/rect on CPU; clock, name and logo visible; kernel oops from the rmem cache flush fixed (T41 kernel expects a physical address)

##### 29. IVS / motion detection

- **Vendor stack:** vendor IVS (T20/T21/T30 initially "always no motion" in the open stack)
- **T10:** ✅ device-tested: no event in 12 s idle, full-grid events on brightness steps (2 of 6)
- **T20:** ✅+ 6/6 events, 0 false alarms; CPU 4.1→2.7 % with motion
- **T21:** ✅ real frame-diff IVS ported
- **T23:** ✅ motion active again after sub-stream idle (WebUI grid)
- **T31:** ✅+ sub-stream default: ~85 % less IVS CPU (compared with vendor libimp)
- **T41:** ✅ works on cam-F with a feeder thread (claude/t41-libimp), no more 10-s stalls; short ~1.2-s gaps still being looked at

##### 30. Frame source / VBM pool

- **Vendor stack:** vendor pools
- **T10:** ✅ device-tested: chn0, chn1 and snapshot concurrent, 148/147 frames decoded
- **T20:** ✅ snapshot debounce no longer polls the JPEG encoder: with 1 snapshot/s on both channels chn0 14.4 / chn1 15.0 fps (was 11.2/14.3); sub-stream height 270 is rounded to 272 with a warning (was: scaler hang) [openimp claude/openimp-t20-jpeg-align, timps claude/timps-jpeg-idle-nopoll]
- **T21:** ✅+ pool parked/reused (release at idle broke later allocations)
- **T23:** ✅ frames also recycled for callback pools
- **T31:** ✅ device-tested: concurrent streams ok
- **T41:** ✅ device-tested: concurrent streams ok


#### Audio (documentation only, no tests)

##### 31. Audio input (AI)

- **Vendor stack:** IMP_AI
- **T10:** ✅ device-tested: AAC 16 kHz mono 32 kbit/s in RTSP + fMP4, volume/gain/mute work, gain clamp 31 (ambient levels, gain 31 reading noisy)
- **T20:** ✅ device-tested: AAC 16 kHz mono 32 kbit/s in RTSP + fMP4, volume/gain/mute work, gain clamp 31 (little gain effect, -75 dB floor)
- **T21:** ✅ device-tested: AAC 16 kHz mono 32 kbit/s in RTSP + fMP4, volume/gain/mute work, gain clamp 31
- **T23:** ✅ microphone in the RTSP stream (AAC 16 kHz), real room-noise signal (mean -64 dB, peak -46 dB; T31 reference -57/-43 dB); no speech test
- **T31:** ✅ device-tested: AAC 16 kHz mono 32 kbit/s in RTSP + fMP4, volume/gain/mute work, gain clamp 31; also PCMU 8 kHz via RTSP (not in fMP4)
- **T41:** ✅ microphone in the RTSP stream (AAC 16 kHz), real room-noise signal (mean -64 dB, peak -53 dB); no speech test

##### 32. Audio output (AO, speaker)

- **Vendor stack:** IMP_AO
- **T10:** ? not tested on purpose: no audio playback on the shared test cameras
- **T20:** ? not tested on purpose: no audio playback on the shared test cameras
- **T21:** ? not tested on purpose: no audio playback on the shared test cameras
- **T23:** ? not tested on purpose: no audio playback on the shared test cameras
- **T31:** ✅ volume/mute work, whole OSS fragments (tested on cam-A)
- **T41:** ? not tested on purpose: no audio playback on the shared test cameras

##### 33. Echo cancellation (AEC)

- **Vendor stack:** IMP_AI_EnableAec
- **T10:** ? changelog mentions only T31/T23
- **T20:** ? device test open
- **T21:** ? device test open
- **T23:** 🔧 implemented (WebRTC AECM, driver reference offset); device test needs speaker playback, which is not allowed on the test cameras
- **T31:** ✅+ real AECM: echo −18 dB, ERLE 44 dB (loopback); before: fake success
- **T41:** ?


#### Memory, size, load

##### 34. libimp size (code + data)

- **Vendor stack:** T21 ~1.0 / T23 ~1.26 / T31 ~1.05 MB
- **T10:** ✅+ 626,032 B (~0.6 MB)
- **T20:** ✅+ 594 KB (was 694 KB; claude/openimp-size, gc-sections)
- **T21:** ✅+ ~0.5 MB
- **T23:** ✅+ 726 KB (was 774 KB; native, no helixd; claude/openimp-size)
- **T31:** ✅+ ~0.57 MB
- **T41:** ✅+ 465,728 B (~0.47 MB)

##### 35. Kernel module size

- **Vendor stack:** T21 616 / T23 857 / T31 829 KB
- **T10:** ✅ 731 KB stripped (was 770; claude/open-tx-isp-size2)
- **T20:** ✅ 736 KB stripped (was 775; claude/open-tx-isp-size2)
- **T21:** ✅+ 452 KB (was 760 KB; vendor 616 KB), RAM unchanged (claude/t21-size-awb-opt, 452 KB since all-17)
- **T23:** ✅+ 622 KB stripped (was 1,047; vendor 857; claude/open-tx-isp-size2, device-tested on cam-B)
- **T31:** ✅+ 711 KB stripped (was 859; vendor 829; claude/open-tx-isp-size2, device-tested on cam-A)
- **T41:** ✅ tx_isp_t41 731,488 B (vendor size not measured)

##### 36. Video memory (rmem) / MemFree

- **Vendor stack:** T21 ~23 MB for main+sub+JPEG
- **T10:** ✅ JPEG buffer −328 KiB; 40 KiB rootfs reserve in the image
- **T20:** ✅ MemFree 47 MB of 91 MB
- **T21:** ✅+ free with main+sub+MJPEG 2.76 MB (vendor ≈1.2)
- **T23:** ✅+ JPEG shares bitstream −1.44 MB; main window 2 MiB
- **T31:** ✅ drift per reload 460→45 KB
- **T41:** ✅ rmem 26 MB in the current image (was 30): stream buffers sized like the vendor (1080p 0.95 MB), capture buffers from the bottom and the rest from the top so idle/restart cycles no longer fragment rmem; 5 idle/restart cycles clean. OOM with three parallel streams and `AddSensor` EBUSY after an OOM kill are still open

##### 37. Reference-frame sharing (BUF_SHARE_CFG)

- **Vendor stack:** vendor T23: used by default (<=1080p); vendor T21: off by default
- **T10:** — hardware missing
- **T20:** — hardware missing
- **T21:** ✅+ works (claude/t23-ref-ring) [-all-13]: P-frames 150-300 B in a static scene on cam-D, no artefacts; saves ~1.5 MB video memory at 1080p; on by default (<=1920x1088), OPENIMP_REF_SHARE=0 disables
- **T23:** ✅+ works (claude/t23-ref-ring) [-all-13]: no artefacts on cam-B, P-frame sizes equal or smaller than without the ring; saves ~1.5 MB at 1080p; on by default (<=1920x1088, like the vendor), OPENIMP_REF_SHARE=0 disables
- **T31:** — hardware missing
- **T41:** ? not wired in timps (SetbufshareChn exists in libimp); not tested

##### 38. CPU load (documented figures)

- **Vendor stack:** vendor comparison values mostly missing
- **T10:** ✅ timps 5–9 % with 2 streams at 25 fps (17 % momentary with 1 stream)
- **T20:** ✅ timps 2.7 % with motion; OEM AWB +4 %
- **T21:** ✅+ lifted AWB at 0.95x vendor instructions (was 1.41x), output bit-identical; cam-D isp_fw_process -10 %
- **T23:** ✅ ~6–7 % for 2 streams 25 fps
- **T31:** ✅ rotation 9 ms/frame @15 fps
- **T41:** ✅+ timps ~10–12 % with our libimp vs ~27 % with the vendor libimp (momentary values)


#### Stability, helper libraries, telemetry

##### 39. Kernel soc_vpu / Helix hardening

- **Vendor stack:** busy-wait up to 200 ms, unbounded waits
- **T10:** ✅+ error IRQ ends the wait immediately (patch 0099)
- **T20:** ✅+ patch 0099
- **T21:** ✅+ patches 0095–0099 (bounded waits, pointer checks, register ioctl restricted to the VPU window)
- **T23:** ✅+ patches 0098–0105 (0102 ignores the residual Helix interrupt, status 0x100 after a finished job), merged upstream in thingino `aperto` (#1748, #1752); 5 h soak on the `aperto` images: 0 VPU errors on all cameras
- **T31:** — SOC_VPU not built
- **T41:** ✅ device-tested: 5 min, 3 RTSP clients plus snapshots, 0 VPU/AVPU errors; main stream High@5.1 after the rmem fix (rmem was exhausted and the main channel fell back to a broken software encoder); 5 idle/restart cycles clean

##### 40. AVPU kernel driver (T31/T40/T41) review

- **Vendor stack:** –
- **T10:** — other VPU
- **T20:** — other VPU
- **T21:** — other VPU
- **T23:** — other VPU
- **T31:** ✅+ DeepSeek review verified; fixes on claude/avpu-review-fixes (minor-number leak, use-after-free on sysfs unbind, uninitialised dma-buf list mutex, flush clamp); kernel patch 0100 validates the rmem flush ioctl (invalid direction no longer crashes the kernel); tested on cam-A: reload, kill -9, 5× rmmod/insmod, 0 oops [-all-13]
- **T41:** ✅ same module family runs on cam-F with the open driver (5 min, 3 RTSP clients plus snapshots, 0 AVPU errors). ioctl stack-overflow hardening (unknown or legacy commands now return -ENOTTY) is in open-tx-isp, not yet device-tested; test plan in `driver/t41/README`

##### 41. Long-term stability / hangs

- **Vendor stack:** –
- **T10:** ✅ rmmod/insmod 5× with streaming, 0 oops; the earlier 'csi clock -22' oops came from a module built against the T20 kernel tree, the T10 build now refuses that [claude/t10-reload-safe]
- **T20:** ✅ 1 h 44 soak, 0 errors
- **T21:** ✅ uptime 1:54 at the test, 0 oops
- **T23:** ✅ the frequent Helix frame drops had a fixed cause (residual interrupt 0x100 treated as an error by the bounded-wait kernel patch): 60 min 0 errors after the fix, 5 h soak on the `aperto` images 0 encoder errors. Still listed open: a sporadic single Helix encode error (errno 5). Cold-start snapshot 503 on a second channel (stale MSCA FIFOs): fix `msca_fifo_rearm` gave 260 cold-start cycles without a failure (before ~1-7 %), soak pending before it enters `next`
- **T31:** ✅ 4.5 h soak ok
- **T41:** ⚠️ 5-min stress without reboot earlier; still open: OOM with three parallel streams, `AddSensor` EBUSY after an OOM kill; module reload 10/10 clean

##### 42. Helper libraries libalog / libsysutils

- **Vendor stack:** shipped with vendor images
- **T10:** ✅+ removed
- **T20:** ✅+ removed (image tested)
- **T21:** ✅+ removed
- **T23:** ✅+ removed; also no helixd/vendor libimp
- **T31:** ✅+ removed
- **T41:** ✅+ removed (cam-F runs the open stack without both libs)

##### 43. Tuning getters / readback

- **Vendor stack:** IMP_ISP_Tuning_Get*
- **T10:** ✅ device-tested: isp-m0 Brightness/Contrast/Saturation/Sharpness/Antiflicker readback equals the set values
- **T20:** ✅ SDK control IDs, pointer semantics as vendor, isp-m0
- **T21:** ✅+ getters return what was set [-all-13] (vendor: scene/colorfx/Sinter DNS no-op)
- **T23:** ✅ expr/EV/TotalGain live, SensorAttr 20-byte layout, vendor isp-m0
- **T31:** ✅ SensorAttr, WaitFrame per frame, isp-w02 counter
- **T41:** ✅ isp-m0 in vendor layout: run mode, BCSH, flip mode, anti-flicker, AE

##### 44. Encoder telemetry / diagnostics

- **Vendor stack:** IMP_Encoder_Query/ChnStat
- **T10:** ✅+ RC log line, clamp warning
- **T20:** ✅+ ditto
- **T21:** ✅+ ditto
- **T23:** ✅+ ditto
- **T31:** ✅+ ditto
- **T41:** ⚠️ isp-m0 shows AE/AWB/anti-flicker/flip; encoder rc readback ok; query counters stayed 0 under load; other /proc/jz/isp nodes unreadable


## Missing / incomplete functions (vendor IMP/SU API, per SoC)

As of: 2026-10-05 (late evening). Docs-only addition; the matrix above is unchanged. Extended later the same night with a legend per table, a progress summary per SoC and full per-area function lists.

State of the work: OpenIMP branch `claude/agg-25` (`claude/agg-26` = agg-25 plus the T21 OSD first-JPEG fix) plus the branches listed below, as pushed on 2026-10-05; cells saying agg-25 mean it is in that branch; open-tx-isp `claude/agg-25` and the branches named in the notes. Source of the function list and of the base classes: the audit `NOT_CONNECTED_2026-10-05.md` (OpenIMP/open-tx-isp `agg-24`, built libimp.so/libsysutils.so, vendor header sets: T10/T20 331 functions, T21 330, T23 704 incl. `_Sec`/`MultiCamera_` variants, T31 405, T41 445). Rows are ALL vendor IMP_*/SU_* functions of the audit (Get/Set pairs share a row). Functions that are class REAL in the audit and have no tracked device or host test show as done (?). Per area the first table shows only rows with at least one gap (cache-only, stub, error, missing, ?); the collapsible full list below it shows every row.

Later work is applied from the commit messages and from the device-test notes of 2026-10-05; **nothing here was re-measured**. Where a fix lives in a branch that is not part of `agg-25` the note says so (`claude/t1x-roi`, `claude/t23t31-cacheonly`, `claude/t23-enc-rest`, `claude/t41-isp-round2`, `claude/t23-awb-runtime`). The image-effect device test `claude/imgfx-tool` (one picture per function) found the T21 no-ops below.

T10 uses the T20 userspace build (and the T20 SDK tuning code in the driver), so its cells mirror T20 unless the note says otherwise; fixes whose commit names only T20/T21 are marked for T10 as shared build, device effect unverified. T23 rows cover the base function: the `_Sec` and `MultiCamera_` variants were folded into it (audit class = worst of the three); the after-audit fixes were made for the base function and are unverified for the variants. T30/T40 are not tabled (no device, no build in the audit).

### Legend

What every cell code means (the same short legend is repeated above each table below). Older text in this file and in `feature-matrix.html` may use the long names in brackets (done (dev), done (?) and so on); the codes are only the short form of them. In the function tables the "Used by" column is shortened to p = prudynt, r = raptor, t = timps (a long per-SoC list is given in the numbered note instead):

- **dev** (done, device-tested): implemented and device-tested on that SoC (a camera run exists; see the note)
- **host** (done, host tests only): implemented, host tests only (unit/layout/fake-device tests); not run on a camera yet
- **aud** (done per audit): really connected per the static audit (reaches the driver/hardware or is a real userspace implementation), but nobody tracked a device test for this call
- **no-op** (vendor no-op): the vendor stack itself does nothing visible (or the measurement could not show an effect); OpenIMP matches that
- **cache** (cache-only): the value is only stored and read back; nothing is applied
- **stub**: returns 0 (or the driver answers 0) without any effect
- **err** (error): exported but fails (returns -1/ENOTSUP, driver -EINVAL/-EPERM/-EOPNOTSUPP) or has a known defect
- **miss** (missing): symbol not exported by the open libimp/libsysutils
- **n.a.** (not in vendor API): the function does not exist in that SoC's vendor API (header set of that SoC); not "unsupported"
- **?**: cause or state not determined
- **►**: leading mark on the function name: at least one streamer (timps/prudynt/raptor) uses it and its cell on that SoC is a gap
- **†**: streamer usage derived from source only (no binary of that streamer was built for that SoC)

**Used by** (column added 2026-10-05): which streamers import the function, determined from real imports, not guesses. `timps`: `nm -D --undefined-only` of the `timpsd` binaries of the per-camera builds (T10 secuplug, T20 wyze cam2 + campan1 (identical import set), T21 victure pc420 + the vendor-stack build, T23 galayou, T31 wuuk, T41 vanhua) incl. weak imports; the timps source has no dlsym use, so nothing is hidden behind dlsym. `prudynt`, `raptor`: T23 is binary-verified (nm of `prudynt`, `rvd`, `rad` of the T23 build, raptor-hal linked in statically); for the other SoCs the sources were run through the C preprocessor with `-DPLATFORM_Txx` and the vendor header set of that SoC and the identifiers were collected. That method reproduces the T23 binary imports exactly (19/19 prudynt, 51/51 raptor rows), so it is trusted, but a name marked **†** is **source-only** (no T10/T20/T21/T31/T41 binary of that streamer was built). Without a †, the entry is binary-verified. `–` = none of the three imports it on any SoC where the function exists. `T21: ...; T23: ...` = the set differs per SoC (only SoCs that have the function). Variants (`_Sec`, `MultiCamera_`) count for the base function. **Bold** used-by text and a leading `►` on the function name = a priority row: at least one streamer imports it and its cell on that SoC is a gap (missing, error, cache-only, stub or ?). `vendor no-op` and `done (...)` rows are not counted as priority gaps.


### Progress per SoC

Counts are per vendor function of that SoC (T23 folded: base function = one; Get and Set count separately). "gaps" = cache-only + stub + error + missing; "vendor no-op" is not a gap; "audit gaps" = the same sum in the audit before the work of 2026-10-05 (T23 unfolded, about 3x per base function). "done %" = dev + host + aud / vendor fns. Bar: █ dev, ▓ host, ▒ aud / ?, ○ no-op, ░ gaps (40 characters per SoC).

| SoC | fns | dev | host | aud | ? | no-op | cache | stub | err | miss | **gaps** | audit gaps | done % |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| T10 | 331 | 0 | 47 | 231 | 2 | 0 | 4 | 0 | 3 | 44 | **51** | 100 | 84.6 % |
| T20 | 331 | 4 | 47 | 231 | 0 | 0 | 2 | 0 | 3 | 44 | **49** | 100 | 85.2 % |
| T21 | 330 | 0 | 38 | 252 | 0 | 6 | 4 | 0 | 5 | 25 | **34** | 77 | 87.9 % |
| T23 | 441 | 2 | 53 | 346 | 0 | 8 | 12 | 0 | 14 | 6 | **32** | 212 | 90.9 % |
| T31 | 405 | 6 | 23 | 338 | 0 | 4 | 4 | 4 | 1 | 25 | **34** | 65 | 90.6 % |
| T41 | 445 | 0 | 37 | 218 | 0 | 0 | 8 | 2 | 48 | 132 | **190** | 227 | 57.3 % |

Columns: fns = vendor functions of that SoC; dev/host/aud/?/no-op/cache/stub/err/miss = counts per cell code (codes as in the legend); gaps = cache + stub + err + miss; audit gaps = the same sum in the audit before the work of 2026-10-05; done % = (dev + host + aud) / fns.

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

- **T10**: 1 (timps 0, prudynt 1, raptor 1): `Encoder_Get/SetJpegeQl` (cache-only; prudynt+raptor)
- **T20**: 1 (timps 1, prudynt 1, raptor 1): `Encoder_Get/SetJpegeQl` (cache-only; prudynt+raptor+timps)
- **T21**: 4 (timps 1, prudynt 1, raptor 4): `Encoder_Get/SetChnDenoise` (error; raptor), `Encoder_Get/SetH265TransCfg` (cache-only; raptor), `Encoder_Get/SetJpegeQl` (cache-only; prudynt+raptor+timps), `Encoder_Get/SetQpgMode` (error; raptor)
- **T23**: 6 (timps 0, prudynt 1, raptor 6): `ISP_Tuning_GetBlcAttr` (error; raptor), `ISP_Tuning_SetAutoZoom` (error; raptor), `ISP_Tuning_SetScalerLv` (error; raptor), `ISP_Tuning_Get/SetAwbClust` (cache-only; raptor), `ISP_Tuning_Get/SetAwbCtTrend` (cache-only; raptor), `AI_SetHpfCoFrequency` (cache-only; prudynt+raptor)
- **T31**: 4 (timps 0, prudynt 2, raptor 4): `ISP_Tuning_DisableMovestate` (stub; raptor), `ISP_Tuning_EnableMovestate` (stub; raptor), `Encoder_SetbufshareChn` (stub; prudynt+raptor), `AI_SetHpfCoFrequency` (cache-only; prudynt+raptor)
- **T41**: 27 (timps 0, prudynt 7, raptor 24): `ISP_Get/SetFrameDrop` (error; raptor), `ISP_Get/SetISPBypass` (missing; prudynt), `ISP_Tuning_Get/SetModuleControl` (error; raptor), `ISP_Tuning_SetAutoZoom` (error; prudynt), `ISP_Tuning_SetMaskBlock` (error; raptor), `ISP_Tuning_SetScalerLv` (error; raptor), `ISP_Tuning_SwitchBin` (missing; prudynt), `ISP_WDR_ENABLE` (error; raptor), `ISP_WDR_ENABLE_GET` (error; raptor), `ISP_Tuning_Get/SetAfWeight` (error; raptor), `Encoder_SetChnMaxPictureSize` (cache-only; raptor), `Encoder_SetbufshareChn` (stub; prudynt+raptor), `FrameSource_Get/SetChnFifoAttr` (cache-only; prudynt+raptor), `FrameSource_Get/SetDelay` (error; raptor), `FrameSource_Get/SetFrameDepth` (cache-only; prudynt+raptor), `FrameSource_Get/SetI2dAttr` (error; raptor), `FrameSource_Get/SetMaxDelay` (error; raptor), `FrameSource_Get/SetPool` (cache-only; raptor), `FrameSource_GetTimedFrame` (error; raptor), `ISP_Tuning_CreateOsdRgn` (error; raptor), `ISP_Tuning_DestroyOsdRgn` (error; raptor), `ISP_Tuning_SetOsdPoolSize` (stub; raptor), `ISP_Tuning_SetOsdRgnAttr` (error; raptor), `ISP_Tuning_ShowOsdRgn` (error; raptor), `AI_SetHpfCoFrequency` (cache-only; prudynt+raptor), `DMIC_*` (error; raptor), `DMIC_DisableAecRefFrame` (error; raptor)

### ISP tuning

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by |
|---|---|---|---|---|---|---|---|
| `ISP_Get/SetCsccrMode` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `ISP_Get/SetFrameDrop` [1] | n.a. | n.a. | n.a. | aud | aud | err | T41: r† |
| ► `ISP_Get/SetISPBypass` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | p† |
| `ISP_Get/SetInternalChnAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_LDC_Get/SetAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_LDC_INIT` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_RAW_RwControl` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_SET_GPIO_INIT_OR_FREE` | n.a. | n.a. | n.a. | aud | aud | miss | – |
| `ISP_SET_GPIO_STA` | n.a. | n.a. | n.a. | aud | aud | miss | – |
| `ISP_SetFixedContraster` [2] | n.a. | n.a. | n.a. | host | stub | n.a. | – |
| `ISP_SetVicDoneCbFunc` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_StartNightMode` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `ISP_Tuning_DisableMovestate` [3] | aud | aud | aud | no-op | stub | n.a. | r† |
| ► `ISP_Tuning_EnableMovestate` [4] | aud | aud | aud | no-op | stub | n.a. | r† |
| `ISP_Tuning_Get/SetDrawBlock` [5] | n.a. | n.a. | n.a. | err | n.a. | miss | – |
| `ISP_Tuning_Get/SetISPHVflip` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| ► `ISP_Tuning_Get/SetModuleControl` [6] | n.a. | n.a. | host | aud | aud | err | T31/T41: r† |
| `ISP_Tuning_Get/SetStatisConfig` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetTmoCurve` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetWDRAttr` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_Get/SetWdrOutputMode` [7] | n.a. | n.a. | n.a. | n.a. | n.a. | err | – |
| `ISP_Tuning_GetAutoZoom` [8] | n.a. | n.a. | n.a. | err | n.a. | err | – |
| ► `ISP_Tuning_GetBlcAttr` [9] | n.a. | n.a. | n.a. | err | aud | n.a. | r† |
| `ISP_Tuning_GetHVFlip` | n.a. | n.a. | n.a. | aud | aud | miss | T23/T31: r† |
| `ISP_Tuning_GetMaskBlock` [10] | n.a. | n.a. | n.a. | host | n.a. | miss | – |
| `ISP_Tuning_SaveAllParam` | miss | miss | miss | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_SetAntiFogAttr` | miss | miss | miss | n.a. | n.a. | n.a. | – |
| ► `ISP_Tuning_SetAutoZoom` [11] | n.a. | n.a. | n.a. | err | host | err | see note |
| `ISP_Tuning_SetDPStrength` | miss | miss | miss | n.a. | miss | n.a. | – |
| ► `ISP_Tuning_SetMaskBlock` [12] | n.a. | n.a. | n.a. | host | n.a. | err | T41: r† |
| `ISP_Tuning_SetMeshShadingScale` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| ► `ISP_Tuning_SetScalerLv` [13] | n.a. | n.a. | n.a. | err | aud | err | r† |
| `ISP_Tuning_SetTmoFaceae` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `ISP_Tuning_SwitchBin` [14] | n.a. | n.a. | n.a. | err | n.a. | miss | T41: p† |
| `ISP_Tuning_WaitFrameDone` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `ISP_WDR_ENABLE` [15] | n.a. | n.a. | n.a. | n.a. | aud | err | r† |
| ► `ISP_WDR_ENABLE_GET` [16] | n.a. | n.a. | n.a. | n.a. | n.a. | err | r† |
| `ISP_WDR_OPEN` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |

Notes:

1. `ISP_Get/SetFrameDrop`: T41: ENOTSUP stub (returns -1)
2. `ISP_SetFixedContraster`: T23: driver routes 0x8000102 (agg-25); T31: (void)mode; return 0
3. `ISP_Tuning_DisableMovestate`: T23: stock driver also no-op (OEM-same); T31: driver answers 0 without effect
4. `ISP_Tuning_EnableMovestate`: T23: stock driver also no-op (OEM-same); T31: driver answers 0 without effect
5. `ISP_Tuning_Get/SetDrawBlock`: T23: driver rejects 0x8000180 (-EINVAL)
6. `ISP_Tuning_Get/SetModuleControl`: T21: tuning 0x80000e2 (agg-25); T41: driver rejects 0x8000072 (-EINVAL)
7. `ISP_Tuning_Get/SetWdrOutputMode`: T41: driver has no stock handler, refuses (claude/t41-isp-round2)
8. `ISP_Tuning_GetAutoZoom`: T23: driver rejects 0x80000e8 (-EINVAL); T41: driver has no stock handler, refuses (claude/t41-isp-round2)
9. `ISP_Tuning_GetBlcAttr`: T23: driver rejects 0x80000a5 (-EINVAL)
10. `ISP_Tuning_GetMaskBlock`: T23: 0x8000183 in claude/t23-awb-runtime only (not in agg-25)
11. `ISP_Tuning_SetAutoZoom`: T23: driver rejects 0x80000e8 (-EINVAL); T31: programs scaler/crop, refuses size change (agg-25); T41: driver has no stock handler, refuses (claude/t41-isp-round2); used by: T23: raptor; T31: prudynt†, raptor†; T41: prudynt†
12. `ISP_Tuning_SetMaskBlock`: T23: 0x8000183 in claude/t23-awb-runtime only (not in agg-25); T41: driver has no handler, fails with -EPERM since agg-25 (was silent 0); vendor behaviour unverified
13. `ISP_Tuning_SetScalerLv`: T23: driver rejects 0x80000e9 (-EINVAL); T41: driver has no handler, fails with -EPERM since agg-25 (was silent 0); vendor behaviour unverified
14. `ISP_Tuning_SwitchBin`: T23: driver rejects 0x8000185 (-EINVAL)
15. `ISP_WDR_ENABLE`: T41: ENOTSUP stub (returns -1)
16. `ISP_WDR_ENABLE_GET`: T41: ENOTSUP stub (returns -1)

<details><summary>All 107 rows of this area (38 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by |
|---|---|---|---|---|---|---|---|
| `ISP_AddSensor` [17] | aud | aud | aud | aud | aud | aud | p† r† t |
| `ISP_Close` [18] | aud | aud | aud | aud | aud | aud | p† r† t |
| `ISP_DelSensor` [19] | aud | aud | aud | aud | aud | aud | p† r† t |
| `ISP_DisableSensor` [20] | aud | aud | aud | aud | aud | aud | p† r† t |
| `ISP_DisableTuning` [21] | aud | aud | aud | aud | aud | aud | p† r† t |
| `ISP_EnableSensor` [22] | aud | aud | aud | aud | aud | aud | p† r† t |
| `ISP_EnableTuning` [23] | aud | aud | aud | aud | aud | aud | p† r† t |
| `ISP_Get/SetCsccrMode` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Get/SetDefaultBinPath` [24] | n.a. | n.a. | n.a. | aud | aud | aud | r† |
| ► `ISP_Get/SetFrameDrop` [1] | n.a. | n.a. | n.a. | aud | aud | err | T41: r† |
| ► `ISP_Get/SetISPBypass` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | p† |
| `ISP_Get/SetInternalChnAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Get/SetSensorRegister` [25] | aud | aud | aud | aud | aud | host | r† |
| `ISP_LDC_Get/SetAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_LDC_INIT` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Open` [26] | aud | aud | aud | aud | aud | aud | p† r† t |
| `ISP_RAW_RwControl` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_SET_GPIO_INIT_OR_FREE` | n.a. | n.a. | n.a. | aud | aud | miss | – |
| `ISP_SET_GPIO_STA` | n.a. | n.a. | n.a. | aud | aud | miss | – |
| `ISP_SetCameraInputMode` [27] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | – |
| `ISP_SetFixedContraster` [2] | n.a. | n.a. | n.a. | host | stub | n.a. | – |
| `ISP_SetStreamOut` [28] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | – |
| `ISP_SetSwitchgpio` [29] | n.a. | n.a. | n.a. | host | n.a. | n.a. | – |
| `ISP_SetVicDoneCbFunc` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_StartNightMode` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_StreamCheck` [30] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | – |
| ► `ISP_Tuning_DisableMovestate` [3] | aud | aud | aud | no-op | stub | n.a. | r† |
| `ISP_Tuning_EnableDRC` [31] | n.a. | n.a. | n.a. | aud | host | n.a. | r† |
| `ISP_Tuning_EnableDefog` [32] | n.a. | n.a. | n.a. | aud | aud | n.a. | r† |
| ► `ISP_Tuning_EnableMovestate` [4] | aud | aud | aud | no-op | stub | n.a. | r† |
| `ISP_Tuning_Get/SetAntiFlickerAttr` [33] | aud | aud | aud | aud | aud | aud | p† r† t |
| `ISP_Tuning_Get/SetBacklightComp` [34] | n.a. | n.a. | n.a. | aud | aud | n.a. | p† r† t |
| `ISP_Tuning_Get/SetBcshHue` [35] | n.a. | n.a. | n.a. | aud | aud | aud | p† r† t |
| `ISP_Tuning_Get/SetCCMAttr` [36] | n.a. | n.a. | n.a. | aud | aud | host | T41: r† |
| `ISP_Tuning_Get/SetColorfxMode` [37] | aud | aud | aud | n.a. | n.a. | n.a. | t |
| `ISP_Tuning_Get/SetContrast` [38] | aud | aud | aud | aud | aud | aud | p† r† t |
| `ISP_Tuning_Get/SetCsc_Attr` [39] | n.a. | n.a. | n.a. | aud | aud | n.a. | r† |
| `ISP_Tuning_Get/SetDPC_Strength` [40] | n.a. | n.a. | n.a. | aud | aud | n.a. | r† t |
| `ISP_Tuning_Get/SetDRC_Strength` [41] | n.a. | n.a. | aud | aud | aud | n.a. | p† r† t |
| `ISP_Tuning_Get/SetDefog_Strength` [42] | n.a. | n.a. | n.a. | aud | aud | n.a. | p† r† t |
| `ISP_Tuning_Get/SetDrawBlock` [5] | n.a. | n.a. | n.a. | err | n.a. | miss | – |
| `ISP_Tuning_Get/SetFrontCrop` [43] | n.a. | n.a. | n.a. | aud | host | n.a. | r† |
| `ISP_Tuning_Get/SetGamma` [44] | aud | aud | aud | dev | aud | n.a. | p† r† |
| `ISP_Tuning_Get/SetGammaAttr` [45] | n.a. | n.a. | n.a. | n.a. | n.a. | host | p† r† |
| `ISP_Tuning_Get/SetHVFLIP` [46] | n.a. | n.a. | n.a. | aud | aud | aud | see note |
| `ISP_Tuning_Get/SetHiLightDepress` [47] | aud | aud | aud | aud | aud | n.a. | p† r† t |
| `ISP_Tuning_Get/SetISPCSCAttr` [48] | n.a. | n.a. | n.a. | n.a. | n.a. | host | – |
| `ISP_Tuning_Get/SetISPCustomMode` [49] | n.a. | n.a. | n.a. | aud | host | n.a. | r† |
| `ISP_Tuning_Get/SetISPHVflip` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_Get/SetISPHflip` [50] | aud | aud | aud | aud | aud | n.a. | p† t |
| `ISP_Tuning_Get/SetISPRunningMode` [51] | aud | aud | aud | aud | aud | aud | p† r† t |
| `ISP_Tuning_Get/SetISPVflip` [52] | aud | aud | aud | aud | aud | n.a. | p† t |
| `ISP_Tuning_Get/SetIntegrationTime` [53] | aud | aud | aud | n.a. | n.a. | n.a. | t |
| `ISP_Tuning_Get/SetMask` [54] | n.a. | n.a. | n.a. | no-op | aud | n.a. | r† |
| `ISP_Tuning_Get/SetMaxAgain` [55] | aud | aud | aud | aud | aud | n.a. | p† r† t |
| `ISP_Tuning_Get/SetMaxDgain` [56] | aud | aud | aud | aud | aud | n.a. | p† r† t |
| ► `ISP_Tuning_Get/SetModuleControl` [6] | n.a. | n.a. | host | aud | aud | err | T31/T41: r† |
| `ISP_Tuning_Get/SetModule_Ratio` [57] | n.a. | n.a. | n.a. | n.a. | n.a. | aud | – |
| `ISP_Tuning_Get/SetSaturation` [58] | aud | aud | aud | aud | aud | aud | p† r† t |
| `ISP_Tuning_Get/SetSensorHflip` [59] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | – |
| `ISP_Tuning_Get/SetSensorVflip` [60] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | – |
| `ISP_Tuning_Get/SetSinterDnsAttr` [61] | host | host | aud | n.a. | n.a. | n.a. | p† |
| `ISP_Tuning_Get/SetStatisConfig` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetTemperDnsAttr` [62] | host | host | aud | n.a. | n.a. | n.a. | p† |
| `ISP_Tuning_Get/SetTmoCurve` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetWDRAttr` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_Get/SetWdrOutputMode` [7] | n.a. | n.a. | n.a. | n.a. | n.a. | err | – |
| `ISP_Tuning_Get/SetWdr_OutputMode` [63] | n.a. | n.a. | n.a. | n.a. | host | n.a. | r† |
| `ISP_Tuning_GetAutoZoom` [8] | n.a. | n.a. | n.a. | err | n.a. | err | – |
| ► `ISP_Tuning_GetBlcAttr` [9] | n.a. | n.a. | n.a. | err | aud | n.a. | r† |
| `ISP_Tuning_GetBrightness` [64] | aud | aud | aud | aud | aud | aud | p† r† |
| `ISP_Tuning_GetEVAttr` [65] | aud | aud | aud | aud | aud | n.a. | p† r† t |
| `ISP_Tuning_GetHVFlip` | n.a. | n.a. | n.a. | aud | aud | miss | T23/T31: r† |
| `ISP_Tuning_GetMaskBlock` [10] | n.a. | n.a. | n.a. | host | n.a. | miss | – |
| `ISP_Tuning_GetRawDRC` [66] | host | host | aud | n.a. | n.a. | n.a. | p† |
| `ISP_Tuning_GetSceneMode` [67] | aud | aud | aud | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_GetSensorAttr` [68] | n.a. | n.a. | n.a. | aud | aud | host | see note |
| `ISP_Tuning_GetSensorFPS` [69] | aud | aud | aud | aud | aud | aud | p† r† t |
| `ISP_Tuning_GetSharpness` [70] | aud | aud | aud | aud | aud | aud | p† r† |
| `ISP_Tuning_GetTotalGain` [71] | aud | aud | aud | aud | aud | n.a. | p† r† t |
| `ISP_Tuning_SaveAllParam` | miss | miss | miss | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_SetAntiFogAttr` | miss | miss | miss | n.a. | n.a. | n.a. | – |
| ► `ISP_Tuning_SetAutoZoom` [11] | n.a. | n.a. | n.a. | err | host | err | see note |
| `ISP_Tuning_SetBrightness` [72] | aud | aud | no-op | aud | aud | aud | p† r† t |
| `ISP_Tuning_SetDPStrength` | miss | miss | miss | n.a. | miss | n.a. | – |
| `ISP_Tuning_SetFWFreeze` [73] | aud | aud | aud | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_SetISPBypass` [74] | aud | aud | aud | aud | aud | n.a. | p† r† |
| `ISP_Tuning_SetISPProcess` [75] | aud | aud | aud | n.a. | n.a. | n.a. | – |
| ► `ISP_Tuning_SetMaskBlock` [12] | n.a. | n.a. | n.a. | host | n.a. | err | T41: r† |
| `ISP_Tuning_SetMeshShadingScale` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_SetRawDRC` [76] | host | host | no-op | n.a. | n.a. | n.a. | p† |
| ► `ISP_Tuning_SetScalerLv` [13] | n.a. | n.a. | n.a. | err | aud | err | r† |
| `ISP_Tuning_SetSceneMode` [77] | aud | aud | no-op | n.a. | n.a. | n.a. | t |
| `ISP_Tuning_SetSensorFPS` [78] | aud | aud | aud | aud | aud | host | p† r† t |
| `ISP_Tuning_SetSharpness` [79] | aud | aud | no-op | aud | aud | aud | p† r† t |
| `ISP_Tuning_SetSinterStrength` [80] | aud | aud | aud | aud | aud | n.a. | p† r† t |
| `ISP_Tuning_SetTemperDnsCtl` [81] | host | host | aud | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_SetTemperStrength` [82] | aud | aud | aud | aud | aud | n.a. | p† r† t |
| `ISP_Tuning_SetTmoFaceae` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_SetVideoDrop` [83] | host | host | host | host | host | host | r† |
| ► `ISP_Tuning_SwitchBin` [14] | n.a. | n.a. | n.a. | err | n.a. | miss | T41: p† |
| `ISP_Tuning_WaitFrame` [84] | host | host | host | host | aud | n.a. | r† |
| `ISP_Tuning_WaitFrameDone` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `ISP_WDR_ENABLE` [15] | n.a. | n.a. | n.a. | n.a. | aud | err | r† |
| ► `ISP_WDR_ENABLE_GET` [16] | n.a. | n.a. | n.a. | n.a. | n.a. | err | r† |
| `ISP_WDR_ENABLE_Get` [85] | n.a. | n.a. | n.a. | n.a. | aud | n.a. | – |
| `ISP_WDR_OPEN` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |

Notes:

17. `ISP_AddSensor`: audit: reaches the driver/kernel
18. `ISP_Close`: audit: userspace implementation
19. `ISP_DelSensor`: audit: reaches the driver/kernel
20. `ISP_DisableSensor`: audit: reaches the driver/kernel
21. `ISP_DisableTuning`: audit: userspace implementation
22. `ISP_EnableSensor`: audit: reaches the driver/kernel
23. `ISP_EnableTuning`: audit: reaches the driver/kernel
24. `ISP_Get/SetDefaultBinPath`: audit: reaches the driver/kernel; userspace implementation
1. `ISP_Get/SetFrameDrop`: T41: ENOTSUP stub (returns -1)
25. `ISP_Get/SetSensorRegister`: T41: claude/t41-isp-round2 (not in agg-25); not device-tested
26. `ISP_Open`: audit: reaches the driver/kernel
27. `ISP_SetCameraInputMode`: audit: reaches the driver/kernel
2. `ISP_SetFixedContraster`: T23: driver routes 0x8000102 (agg-25); T31: (void)mode; return 0
28. `ISP_SetStreamOut`: audit: reaches the driver/kernel
29. `ISP_SetSwitchgpio`: T23: ioctl (claude/t23t31-cacheonly, not in agg-25)
30. `ISP_StreamCheck`: audit: reaches the driver/kernel
3. `ISP_Tuning_DisableMovestate`: T23: stock driver also no-op (OEM-same); T31: driver answers 0 without effect
31. `ISP_Tuning_EnableDRC`: T31: wired to the driver (agg-25)
32. `ISP_Tuning_EnableDefog`: audit: reaches the driver/kernel
4. `ISP_Tuning_EnableMovestate`: T23: stock driver also no-op (OEM-same); T31: driver answers 0 without effect
33. `ISP_Tuning_Get/SetAntiFlickerAttr`: audit: reaches the driver/kernel
34. `ISP_Tuning_Get/SetBacklightComp`: audit: reaches the driver/kernel
35. `ISP_Tuning_Get/SetBcshHue`: audit: reaches the driver/kernel
36. `ISP_Tuning_Get/SetCCMAttr`: T41: vendor 1.2.6 error ladder (claude/t41-isp-round2, not in agg-25); not device-tested
37. `ISP_Tuning_Get/SetColorfxMode`: audit: reaches the driver/kernel
38. `ISP_Tuning_Get/SetContrast`: audit: reaches the driver/kernel
39. `ISP_Tuning_Get/SetCsc_Attr`: audit: reaches the driver/kernel
40. `ISP_Tuning_Get/SetDPC_Strength`: audit: reaches the driver/kernel
41. `ISP_Tuning_Get/SetDRC_Strength`: audit: reaches the driver/kernel
42. `ISP_Tuning_Get/SetDefog_Strength`: audit: reaches the driver/kernel
5. `ISP_Tuning_Get/SetDrawBlock`: T23: driver rejects 0x8000180 (-EINVAL)
43. `ISP_Tuning_Get/SetFrontCrop`: T31: wired to driver 0x80000e3 / 0x80000e7 (agg-25); T10/T20/T21 crop device-tested but not a vendor call there
44. `ISP_Tuning_Get/SetGamma`: T23: applied at once (beyond stock); curve test, falling curve rejected, restore ok on cam-B 2026-10-05
45. `ISP_Tuning_Get/SetGammaAttr`: T41: vendor 1.2.6 error ladder (claude/t41-isp-round2, not in agg-25); not device-tested
46. `ISP_Tuning_Get/SetHVFLIP`: audit: reaches the driver/kernel; used by: T23/T31: raptor†; T41: raptor†, timps
47. `ISP_Tuning_Get/SetHiLightDepress`: audit: reaches the driver/kernel
48. `ISP_Tuning_Get/SetISPCSCAttr`: T41: vendor 1.2.6 error ladder (claude/t41-isp-round2, not in agg-25); not device-tested
49. `ISP_Tuning_Get/SetISPCustomMode`: T31: wired to driver 0x80000e3 / 0x80000e7 (agg-25); T10/T20/T21 crop device-tested but not a vendor call there
50. `ISP_Tuning_Get/SetISPHflip`: audit: reaches the driver/kernel
51. `ISP_Tuning_Get/SetISPRunningMode`: audit: reaches the driver/kernel
52. `ISP_Tuning_Get/SetISPVflip`: audit: reaches the driver/kernel
53. `ISP_Tuning_Get/SetIntegrationTime`: audit: reaches the driver/kernel
54. `ISP_Tuning_Get/SetMask`: T23: stock tx-isp-t23.ko leaves 0x80000e5 unhandled (-1)
55. `ISP_Tuning_Get/SetMaxAgain`: audit: reaches the driver/kernel
56. `ISP_Tuning_Get/SetMaxDgain`: audit: reaches the driver/kernel
6. `ISP_Tuning_Get/SetModuleControl`: T21: tuning 0x80000e2 (agg-25); T41: driver rejects 0x8000072 (-EINVAL)
57. `ISP_Tuning_Get/SetModule_Ratio`: audit: reaches the driver/kernel
58. `ISP_Tuning_Get/SetSaturation`: audit: reaches the driver/kernel
59. `ISP_Tuning_Get/SetSensorHflip`: audit: reaches the driver/kernel
60. `ISP_Tuning_Get/SetSensorVflip`: audit: reaches the driver/kernel
61. `ISP_Tuning_Get/SetSinterDnsAttr`: T10+T20: reach the driver (agg-25, T20 vendor layout); T10 shares the build
62. `ISP_Tuning_Get/SetTemperDnsAttr`: T10+T20: reach the driver (agg-25, T20 vendor layout); T10 shares the build
7. `ISP_Tuning_Get/SetWdrOutputMode`: T41: driver has no stock handler, refuses (claude/t41-isp-round2)
63. `ISP_Tuning_Get/SetWdr_OutputMode`: T31: reaches the WDR tool block (agg-25)
8. `ISP_Tuning_GetAutoZoom`: T23: driver rejects 0x80000e8 (-EINVAL); T41: driver has no stock handler, refuses (claude/t41-isp-round2)
9. `ISP_Tuning_GetBlcAttr`: T23: driver rejects 0x80000a5 (-EINVAL)
64. `ISP_Tuning_GetBrightness`: audit: reaches the driver/kernel
65. `ISP_Tuning_GetEVAttr`: audit: reaches the driver/kernel
10. `ISP_Tuning_GetMaskBlock`: T23: 0x8000183 in claude/t23-awb-runtime only (not in agg-25)
66. `ISP_Tuning_GetRawDRC`: T10+T20: reach the driver (agg-25, T20 vendor layout); T10 shares the build
67. `ISP_Tuning_GetSceneMode`: audit: reaches the driver/kernel
68. `ISP_Tuning_GetSensorAttr`: T41: driver claude/t41-connect (agg-25); host tests 58/58; used by: T23/T31: timps; T41: raptor†, timps
69. `ISP_Tuning_GetSensorFPS`: audit: reaches the driver/kernel
70. `ISP_Tuning_GetSharpness`: audit: reaches the driver/kernel
71. `ISP_Tuning_GetTotalGain`: audit: reaches the driver/kernel
11. `ISP_Tuning_SetAutoZoom`: T23: driver rejects 0x80000e8 (-EINVAL); T31: programs scaler/crop, refuses size change (agg-25); T41: driver has no stock handler, refuses (claude/t41-isp-round2); used by: T23: raptor; T31: prudynt†, raptor†; T41: prudynt†
72. `ISP_Tuning_SetBrightness`: T21: imgfx 2026-10-05: no picture change; vendor no-op or measurement issue (which one: unverified)
73. `ISP_Tuning_SetFWFreeze`: audit: reaches the driver/kernel
74. `ISP_Tuning_SetISPBypass`: audit: reaches the driver/kernel
75. `ISP_Tuning_SetISPProcess`: audit: reaches the driver/kernel
12. `ISP_Tuning_SetMaskBlock`: T23: 0x8000183 in claude/t23-awb-runtime only (not in agg-25); T41: driver has no handler, fails with -EPERM since agg-25 (was silent 0); vendor behaviour unverified
76. `ISP_Tuning_SetRawDRC`: T10+T20: reach the driver (agg-25, T20 vendor layout); T10 shares the build; T21: imgfx 2026-10-05: no picture change; vendor no-op or measurement issue (which one: unverified)
13. `ISP_Tuning_SetScalerLv`: T23: driver rejects 0x80000e9 (-EINVAL); T41: driver has no handler, fails with -EPERM since agg-25 (was silent 0); vendor behaviour unverified
77. `ISP_Tuning_SetSceneMode`: T21: imgfx 2026-10-05: no picture change; vendor no-op or measurement issue (which one: unverified)
78. `ISP_Tuning_SetSensorFPS`: T41: reaches the sensor (agg-25); -EOPNOTSUPP on the gc5603 of cam-F
79. `ISP_Tuning_SetSharpness`: T21: imgfx 2026-10-05: no picture change; vendor no-op or measurement issue (which one: unverified)
80. `ISP_Tuning_SetSinterStrength`: audit: reaches the driver/kernel
81. `ISP_Tuning_SetTemperDnsCtl`: T10+T20: newly exported (agg-25)
82. `ISP_Tuning_SetTemperStrength`: audit: reaches the driver/kernel
83. `ISP_Tuning_SetVideoDrop`: all: callback after 2/4/6 s without frames (agg-25); host-tested; video demand rule
14. `ISP_Tuning_SwitchBin`: T23: driver rejects 0x8000185 (-EINVAL)
84. `ISP_Tuning_WaitFrame`: T10+T20+T21: waits for the frame end (agg-25; T20 ms, not jiffies); T23: stock 24-byte block, driver routed (agg-25), no device test
15. `ISP_WDR_ENABLE`: T41: ENOTSUP stub (returns -1)
16. `ISP_WDR_ENABLE_GET`: T41: ENOTSUP stub (returns -1)
85. `ISP_WDR_ENABLE_Get`: audit: userspace implementation

</details>

### AE / AWB / AF

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by |
|---|---|---|---|---|---|---|---|
| `ISP_SetAeAlgoFunc` | n.a. | n.a. | n.a. | aud | aud | miss | – |
| `ISP_SetAwbAlgoFunc` | n.a. | n.a. | n.a. | aud | aud | miss | – |
| `ISP_Tuning_AwbSync` [1] | n.a. | n.a. | n.a. | err | n.a. | n.a. | – |
| `ISP_Tuning_Get/SetAeConvergeStep` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetAeExpList` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `ISP_Tuning_Get/SetAfWeight` [2] | n.a. | n.a. | aud | host | dev | err | r† |
| ► `ISP_Tuning_Get/SetAwbClust` [3] | n.a. | n.a. | n.a. | cache | aud | n.a. | r† |
| `ISP_Tuning_Get/SetAwbConvergeStep` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `ISP_Tuning_Get/SetAwbCtTrend` [4] | n.a. | n.a. | n.a. | cache | aud | n.a. | r† |
| `ISP_Tuning_Get/SetAwbCtTrendOffset` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetFaceAe` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetFaceAeWeiget` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAEEvList` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAEFlickerFlag` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAFMetricesInfo` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAeAtList` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAeBv` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAeEvList` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAfStatistics` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetFaceAeLuma` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_SetWB_ALGO` [5] | n.a. | n.a. | n.a. | err | aud | n.a. | – |

Notes:

1. `ISP_Tuning_AwbSync`: T23: driver rejects 0x8000011 (-EINVAL)
2. `ISP_Tuning_Get/SetAfWeight`: T23: AF statistics chain from the stock module, off by default (source_af=0); no device test; T31: reconstructed AF chain; metrics/zone/weight/hist verified on cam-A 2026-10-05, Get->Set roundtrip 0; T41: ENOTSUP stub (returns -1)
3. `ISP_Tuning_Get/SetAwbClust`: T23: stock objects stored, open AWB does not read them in agg-25; effective in claude/t23-awb-runtime (host-tested)
4. `ISP_Tuning_Get/SetAwbCtTrend`: T23: stock objects stored, open AWB does not read them in agg-25; effective in claude/t23-awb-runtime (host-tested)
5. `ISP_Tuning_SetWB_ALGO`: T23: driver does not route 0x800000c (HLIL AWB has no light-source table)

<details><summary>All 58 rows of this area (21 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by |
|---|---|---|---|---|---|---|---|
| `ISP_SetAeAlgoFunc` | n.a. | n.a. | n.a. | aud | aud | miss | – |
| `ISP_SetAwbAlgoFunc` | n.a. | n.a. | n.a. | aud | aud | miss | – |
| `ISP_Tuning_AE_Get/SetROI` [6] | aud | aud | aud | host | aud | n.a. | see note |
| `ISP_Tuning_AwbSync` [1] | n.a. | n.a. | n.a. | err | n.a. | n.a. | – |
| `ISP_Tuning_Awb_Get/SetCwfShift` [7] | host | host | n.a. | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_Awb_Get/SetRgbCoefft` [8] | aud | aud | aud | host | aud | host | r† |
| `ISP_Tuning_Get/SetAeAttr` [9] | n.a. | n.a. | n.a. | host | aud | n.a. | p† r† |
| `ISP_Tuning_Get/SetAeComp` [10] | aud | aud | n.a. | aud | aud | n.a. | p† r† t |
| `ISP_Tuning_Get/SetAeConvergeStep` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetAeExpList` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetAeExprInfo` [11] | n.a. | n.a. | n.a. | n.a. | n.a. | aud | – |
| `ISP_Tuning_Get/SetAeHist` [12] | aud | aud | aud | host | aud | n.a. | p† r† |
| `ISP_Tuning_Get/SetAeMin` [13] | n.a. | n.a. | aud | host | aud | n.a. | see note |
| `ISP_Tuning_Get/SetAeScenceAttr` [14] | n.a. | n.a. | n.a. | n.a. | n.a. | aud | – |
| `ISP_Tuning_Get/SetAeStrategy` [15] | host | host | host | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_Get/SetAeTargetList` [16] | n.a. | n.a. | n.a. | host | aud | n.a. | r† |
| `ISP_Tuning_Get/SetAeWeight` [17] | aud | aud | aud | host | aud | host | p† r† |
| `ISP_Tuning_Get/SetAfHist` [18] | aud | aud | aud | host | dev | n.a. | r† |
| ► `ISP_Tuning_Get/SetAfWeight` [2] | n.a. | n.a. | aud | host | dev | err | r† |
| `ISP_Tuning_Get/SetAwbAttr` [19] | n.a. | n.a. | n.a. | n.a. | n.a. | aud | – |
| ► `ISP_Tuning_Get/SetAwbClust` [3] | n.a. | n.a. | n.a. | cache | aud | n.a. | r† |
| `ISP_Tuning_Get/SetAwbConvergeStep` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `ISP_Tuning_Get/SetAwbCtTrend` [4] | n.a. | n.a. | n.a. | cache | aud | n.a. | r† |
| `ISP_Tuning_Get/SetAwbCtTrendOffset` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetAwbHist` [20] | aud | aud | aud | no-op | no-op | n.a. | p† r† |
| `ISP_Tuning_Get/SetAwbWeight` [21] | aud | aud | aud | no-op | no-op | aud | p† r† |
| `ISP_Tuning_Get/SetAwbZoneWeight` [22] | n.a. | n.a. | n.a. | host | n.a. | n.a. | – |
| `ISP_Tuning_Get/SetFaceAe` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetFaceAeWeiget` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetWB` [23] | aud | aud | aud | aud | aud | n.a. | p† r† t |
| `ISP_Tuning_GetAEEvList` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAEFlickerFlag` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAE_IT_MAX` [24] | n.a. | n.a. | n.a. | aud | aud | n.a. | p† |
| `ISP_Tuning_GetAFMetrices` [25] | n.a. | n.a. | aud | host | dev | n.a. | r† |
| `ISP_Tuning_GetAFMetricesInfo` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAWBCt` [26] | n.a. | n.a. | n.a. | host | aud | n.a. | see note |
| `ISP_Tuning_GetAeAtList` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAeBv` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAeEvList` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAeHist_Origin` [27] | n.a. | n.a. | n.a. | host | aud | n.a. | see note |
| `ISP_Tuning_GetAeLuma` [28] | n.a. | n.a. | aud | aud | aud | n.a. | p† r† t |
| `ISP_Tuning_GetAeState` [29] | n.a. | n.a. | n.a. | host | aud | n.a. | r† |
| `ISP_Tuning_GetAeStatistics` [30] | n.a. | n.a. | n.a. | n.a. | n.a. | aud | – |
| `ISP_Tuning_GetAeZone` [31] | host | host | aud | host | aud | n.a. | p† r† |
| `ISP_Tuning_GetAfStatistics` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAfZone` [32] | n.a. | n.a. | n.a. | host | dev | n.a. | r† |
| `ISP_Tuning_GetAwbGlobalStatistics` [33] | n.a. | n.a. | n.a. | n.a. | n.a. | aud | – |
| `ISP_Tuning_GetAwbStatistics` [34] | n.a. | n.a. | n.a. | n.a. | n.a. | aud | – |
| `ISP_Tuning_GetAwbZone` [35] | host | host | n.a. | host | aud | n.a. | see note |
| `ISP_Tuning_GetExpr` [36] | aud | aud | aud | aud | aud | n.a. | r† t |
| `ISP_Tuning_GetFaceAeLuma` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetWB_GOL_Statis` [37] | n.a. | n.a. | aud | aud | aud | n.a. | r† |
| `ISP_Tuning_GetWB_Statis` [38] | aud | aud | aud | aud | aud | n.a. | r† |
| `ISP_Tuning_SetAeFreeze` [39] | n.a. | n.a. | n.a. | host | aud | n.a. | r† |
| `ISP_Tuning_SetAe_IT_MAX` [40] | n.a. | n.a. | n.a. | aud | aud | n.a. | p† t |
| `ISP_Tuning_SetAwbCt` [41] | n.a. | n.a. | n.a. | host | aud | n.a. | r† |
| `ISP_Tuning_SetExpr` [42] | aud | aud | aud | host | aud | n.a. | T23/T31: r† |
| `ISP_Tuning_SetWB_ALGO` [5] | n.a. | n.a. | n.a. | err | aud | n.a. | – |

Notes:

6. `ISP_Tuning_AE_Get/SetROI`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified; used by: T10/T20: prudynt†; T21/T23/T31: prudynt†, raptor†
1. `ISP_Tuning_AwbSync`: T23: driver rejects 0x8000011 (-EINVAL)
7. `ISP_Tuning_Awb_Get/SetCwfShift`: T10+T20: newly exported (agg-25)
8. `ISP_Tuning_Awb_Get/SetRgbCoefft`: T23: stock handlers routed (open-tx-isp agg-25), no device test; T41: driver claude/t41-connect (agg-25); host tests 58/58
9. `ISP_Tuning_Get/SetAeAttr`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified
10. `ISP_Tuning_Get/SetAeComp`: audit: reaches the driver/kernel
11. `ISP_Tuning_Get/SetAeExprInfo`: audit: reaches the driver/kernel
12. `ISP_Tuning_Get/SetAeHist`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified
13. `ISP_Tuning_Get/SetAeMin`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified; used by: T23: prudynt; T31: prudynt†, raptor†
14. `ISP_Tuning_Get/SetAeScenceAttr`: audit: reaches the driver/kernel
15. `ISP_Tuning_Get/SetAeStrategy`: T10+T20+T21: newly exported (agg-25); T10 shares the T20 build
16. `ISP_Tuning_Get/SetAeTargetList`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified
17. `ISP_Tuning_Get/SetAeWeight`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified; T41: driver claude/t41-connect (agg-25); host tests 58/58
18. `ISP_Tuning_Get/SetAfHist`: T23: AF statistics chain from the stock module, off by default (source_af=0); no device test; T31: reconstructed AF chain; metrics/zone/weight/hist verified on cam-A 2026-10-05, Get->Set roundtrip 0
2. `ISP_Tuning_Get/SetAfWeight`: T23: AF statistics chain from the stock module, off by default (source_af=0); no device test; T31: reconstructed AF chain; metrics/zone/weight/hist verified on cam-A 2026-10-05, Get->Set roundtrip 0; T41: ENOTSUP stub (returns -1)
19. `ISP_Tuning_Get/SetAwbAttr`: audit: reaches the driver/kernel
3. `ISP_Tuning_Get/SetAwbClust`: T23: stock objects stored, open AWB does not read them in agg-25; effective in claude/t23-awb-runtime (host-tested)
4. `ISP_Tuning_Get/SetAwbCtTrend`: T23: stock objects stored, open AWB does not read them in agg-25; effective in claude/t23-awb-runtime (host-tested)
20. `ISP_Tuning_Get/SetAwbHist`: T23+T31: stock driver also no-op (OEM-same)
21. `ISP_Tuning_Get/SetAwbWeight`: T23+T31: stock driver also no-op (OEM-same)
22. `ISP_Tuning_Get/SetAwbZoneWeight`: T23: stock handlers routed (open-tx-isp agg-25), no device test
23. `ISP_Tuning_Get/SetWB`: audit: reaches the driver/kernel
24. `ISP_Tuning_GetAE_IT_MAX`: audit: reaches the driver/kernel
25. `ISP_Tuning_GetAFMetrices`: T23: AF statistics chain from the stock module, off by default (source_af=0); no device test; T31: reconstructed AF chain; metrics/zone/weight/hist verified on cam-A 2026-10-05, Get->Set roundtrip 0
26. `ISP_Tuning_GetAWBCt`: T23: stock handlers routed (open-tx-isp agg-25), no device test; used by: T23: raptor; T31: prudynt†, raptor†
27. `ISP_Tuning_GetAeHist_Origin`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified; used by: T23: prudynt; T31: prudynt†, raptor†
28. `ISP_Tuning_GetAeLuma`: audit: reaches the driver/kernel
29. `ISP_Tuning_GetAeState`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified
30. `ISP_Tuning_GetAeStatistics`: audit: reaches the driver/kernel
31. `ISP_Tuning_GetAeZone`: T10+T20: vendor T20 ids/ABI (agg-25); T10 shares the build; T23: stock handlers routed (agg-25), no device test
32. `ISP_Tuning_GetAfZone`: T23: AF statistics chain from the stock module, off by default (source_af=0); no device test; T31: reconstructed AF chain; metrics/zone/weight/hist verified on cam-A 2026-10-05, Get->Set roundtrip 0
33. `ISP_Tuning_GetAwbGlobalStatistics`: audit: reaches the driver/kernel
34. `ISP_Tuning_GetAwbStatistics`: audit: reaches the driver/kernel
35. `ISP_Tuning_GetAwbZone`: T10+T20: vendor T20 ids/ABI (agg-25); T10 shares the build; T23: stock handlers routed (agg-25), no device test; used by: T10/T20: prudynt†; T23/T31: prudynt†, raptor†
36. `ISP_Tuning_GetExpr`: audit: reaches the driver/kernel
37. `ISP_Tuning_GetWB_GOL_Statis`: audit: reaches the driver/kernel
38. `ISP_Tuning_GetWB_Statis`: audit: reaches the driver/kernel
39. `ISP_Tuning_SetAeFreeze`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified
40. `ISP_Tuning_SetAe_IT_MAX`: audit: reaches the driver/kernel
41. `ISP_Tuning_SetAwbCt`: T23: stock handlers routed (open-tx-isp agg-25), no device test
42. `ISP_Tuning_SetExpr`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified
5. `ISP_Tuning_SetWB_ALGO`: T23: driver does not route 0x800000c (HLIL AWB has no light-source table)

</details>

### Encoder (and decoder)

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by |
|---|---|---|---|---|---|---|---|
| `Decoder_*` (8 functions) [1] | miss | miss | miss | aud | miss | miss | – |
| `Encoder_Get/SetChangeRef` | miss | miss | miss | aud | n.a. | n.a. | – |
| `Encoder_Get/SetChnDemask` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| ► `Encoder_Get/SetChnDenoise` [2] | err | err | err | aud | n.a. | n.a. | T21: r† |
| `Encoder_Get/SetChnFrmUsedMode` [3] | miss | miss | miss | host | n.a. | n.a. | – |
| `Encoder_Get/SetChnH264Demask` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnH264Denoise` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnH264FrmUsedMode` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnHSkip` | miss | miss | aud | aud | n.a. | n.a. | – |
| `Encoder_Get/SetChnRcAttr` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnRoiAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_Get/SetChnSeiAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_Get/SetFisheyeEnableStatus` [4] | miss | miss | miss | cache | cache | miss | – |
| `Encoder_Get/SetH264TransCfg` [5] | cache | dev | host | aud | n.a. | n.a. | T21: r† |
| ► `Encoder_Get/SetH265TransCfg` [6] | n.a. | n.a. | cache | cache | n.a. | n.a. | T21: r† |
| ► `Encoder_Get/SetJpegeQl` [7] | cache | cache | cache | aud | n.a. | host | see note |
| `Encoder_Get/SetMbRC` [8] | ? | host | aud | aud | n.a. | n.a. | T21: r† |
| ► `Encoder_Get/SetQpgMode` [9] | n.a. | n.a. | err | aud | n.a. | n.a. | T21: r† |
| `Encoder_Get/Setframelossthd` [10] | n.a. | n.a. | n.a. | cache | n.a. | n.a. | – |
| `Encoder_GetGOPSize` | miss | miss | miss | aud | n.a. | n.a. | – |
| `Encoder_InputJpege` | n.a. | n.a. | n.a. | aud | n.a. | miss | T23: t |
| `Encoder_InputJpege_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetAvpuBsShare` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetAvpuBsSize` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetAvpuJpegQp` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetChnHSkipBlackEnhance` | miss | miss | miss | aud | n.a. | n.a. | – |
| `Encoder_SetChnMapRoi` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `Encoder_SetChnMaxPictureSize` [11] | n.a. | n.a. | n.a. | host | n.a. | cache | T41: r† |
| `Encoder_SetFrameRelease` [12] | n.a. | n.a. | n.a. | n.a. | cache | miss | – |
| `Encoder_SetIvpuBsSize` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetMultiSectionMode` [13] | n.a. | n.a. | n.a. | cache | n.a. | n.a. | – |
| ► `Encoder_SetbufshareChn` [14] | n.a. | n.a. | n.a. | n.a. | stub | stub | p† r† |
| `Encoder_VbmAlloc` | n.a. | n.a. | n.a. | aud | n.a. | miss | T23: t |
| `Encoder_VbmAlloc_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_VbmFree` | n.a. | n.a. | n.a. | aud | n.a. | miss | T23: t |
| `Encoder_VbmFree_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_VbmP2V` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `Encoder_VbmV2P` | n.a. | n.a. | n.a. | aud | n.a. | miss | T23: t |
| `Encoder_YuvEncode` | n.a. | n.a. | n.a. | aud | n.a. | miss | T23: t |
| `Encoder_YuvEncode_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_YuvExit` | n.a. | n.a. | n.a. | aud | n.a. | miss | T23: t |
| `Encoder_YuvExit_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_YuvInit` | n.a. | n.a. | n.a. | aud | n.a. | miss | T23: t |
| `Encoder_YuvInit_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |

Notes:

1. `Decoder_*` (8 functions): functions: CreateChn, DestroyChn, GetFrame, PollingFrame, ReleaseFrame, SendStreamTimeout, StartRecvPic, StopRecvPic.
2. `Encoder_Get/SetChnDenoise`: T10+T20+T21: refused (-1): Helix/NVPU cannot do it (agg-25); vendor behaviour unverified
3. `Encoder_Get/SetChnFrmUsedMode`: T23: stored in the channel attribute (claude/t23-enc-rest, not in agg-25)
4. `Encoder_Get/SetFisheyeEnableStatus`: T23+T31: kept for getter only (documented in source)
5. `Encoder_Get/SetH264TransCfg`: T10: T10 has no chroma-offset register, stays 0; T20: chroma QP offset via PPS + reg 0x40120 (claude/t1x-roi, not in agg-25): verified on cam-C, no colour shift; T21: chroma QP offset, PPS rewrite (claude/t1x-roi, not in agg-25); not device-tested
6. `Encoder_Get/SetH265TransCfg`: T21+T23: stored in channel, never pushed
7. `Encoder_Get/SetJpegeQl`: T10+T20+T21: only applied at CreateChn (p2_encoder.c:1771); live change ignored; T41: live and at CreateChn (agg-25); T10/T20/T21 still applied at CreateChn only; used by: T10: prudynt†, raptor†; T20/T21/T23: prudynt†, raptor†, timps
8. `Encoder_Get/SetMbRC`: T10: commit names T20 only; T10 shares the build; T20: switches the macroblock QP table (agg-25)
9. `Encoder_Get/SetQpgMode`: T21: refused (-1) since agg-25; vendor behaviour unverified
10. `Encoder_Get/Setframelossthd`: T23: kept for getter only (documented in source)
11. `Encoder_SetChnMaxPictureSize`: T23: as the OEM stores it, re-encode on overshoot (claude/t23-enc-rest, not in agg-25); T41: written into rcAttr copy, codec not updated (T23: loss threshold kept only)
12. `Encoder_SetFrameRelease`: T31: kept for getter only (documented in source)
13. `Encoder_SetMultiSectionMode`: T23: kept for getter only (documented in source)
14. `Encoder_SetbufshareChn`: T31+T41: validates channel numbers, returns 0

<details><summary>All 92 rows of this area (44 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by |
|---|---|---|---|---|---|---|---|
| `Decoder_*` (8 functions) [1] | miss | miss | miss | aud | miss | miss | – |
| `Encoder_CreateChn` [15] | aud | aud | aud | aud | aud | aud | p† r† t |
| `Encoder_CreateGroup` [16] | aud | aud | aud | aud | aud | aud | p† r† t |
| `Encoder_DestroyChn` [17] | aud | aud | aud | aud | aud | aud | p† r† t |
| `Encoder_DestroyGroup` [18] | aud | aud | aud | aud | aud | aud | p† r† t |
| `Encoder_FlushStream` [19] | host | host | host | host | host | host | p† r† |
| `Encoder_Get/SetChangeRef` | miss | miss | miss | aud | n.a. | n.a. | – |
| `Encoder_Get/SetChnAttrRcMode` [20] | aud | aud | aud | aud | aud | aud | r† t |
| `Encoder_Get/SetChnColor2Grey` [21] | host | host | host | aud | n.a. | n.a. | T21: r† |
| `Encoder_Get/SetChnCrop` [22] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | – |
| `Encoder_Get/SetChnDemask` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| ► `Encoder_Get/SetChnDenoise` [2] | err | err | err | aud | n.a. | n.a. | T21: r† |
| `Encoder_Get/SetChnFrmRate` [23] | aud | aud | aud | aud | aud | aud | p† r† |
| `Encoder_Get/SetChnFrmUsedMode` [3] | miss | miss | miss | host | n.a. | n.a. | – |
| `Encoder_Get/SetChnGopAttr` [24] | n.a. | n.a. | n.a. | n.a. | aud | aud | – |
| `Encoder_Get/SetChnH264Demask` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnH264Denoise` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnH264FrmUsedMode` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnHSkip` | miss | miss | aud | aud | n.a. | n.a. | – |
| `Encoder_Get/SetChnROI` [25] | host | dev | no-op | aud | n.a. | n.a. | T21: r† |
| `Encoder_Get/SetChnRcAttr` | miss | miss | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnRoiAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_Get/SetChnSeiAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_Get/SetFisheyeEnableStatus` [4] | miss | miss | miss | cache | cache | miss | – |
| `Encoder_Get/SetGDRCfg` [26] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | – |
| `Encoder_Get/SetH264TransCfg` [5] | cache | dev | host | aud | n.a. | n.a. | T21: r† |
| ► `Encoder_Get/SetH265TransCfg` [6] | n.a. | n.a. | cache | cache | n.a. | n.a. | T21: r† |
| ► `Encoder_Get/SetJpegeQl` [7] | cache | cache | cache | aud | n.a. | host | see note |
| `Encoder_Get/SetMaxStreamCnt` [27] | aud | aud | aud | aud | aud | aud | r† |
| `Encoder_Get/SetMbRC` [8] | ? | host | aud | aud | n.a. | n.a. | T21: r† |
| `Encoder_Get/SetPool` [28] | n.a. | n.a. | n.a. | aud | aud | aud | r† |
| ► `Encoder_Get/SetQpgMode` [9] | n.a. | n.a. | err | aud | n.a. | n.a. | T21: r† |
| `Encoder_Get/SetStreamBufSize` [29] | n.a. | n.a. | n.a. | n.a. | aud | aud | – |
| `Encoder_Get/SetSuperFrameCfg` [30] | host | host | host | aud | n.a. | n.a. | T21: r† |
| `Encoder_Get/Setframelossthd` [10] | n.a. | n.a. | n.a. | cache | n.a. | n.a. | – |
| `Encoder_GetChnAttr` [31] | aud | aud | aud | aud | aud | aud | p† r† |
| `Encoder_GetChnAveBitrate` [32] | n.a. | n.a. | n.a. | n.a. | aud | n.a. | t |
| `Encoder_GetChnEncType` [33] | n.a. | n.a. | aud | aud | aud | aud | r† |
| `Encoder_GetChnEvalInfo` [34] | n.a. | n.a. | n.a. | n.a. | host | n.a. | r† |
| `Encoder_GetChnMaxPictureSize` [35] | n.a. | n.a. | n.a. | host | n.a. | n.a. | – |
| `Encoder_GetFd` [36] | n.a. | n.a. | aud | aud | aud | aud | r† |
| `Encoder_GetGOPSize` | miss | miss | miss | aud | n.a. | n.a. | – |
| `Encoder_GetStream` [37] | aud | aud | aud | aud | aud | aud | p† r† t |
| `Encoder_InputJpege` | n.a. | n.a. | n.a. | aud | n.a. | miss | T23: t |
| `Encoder_InputJpege_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_InsertUserData` [38] | aud | aud | aud | aud | n.a. | n.a. | – |
| `Encoder_PollingModuleStream` [39] | n.a. | n.a. | aud | aud | aud | aud | – |
| `Encoder_PollingStream` [40] | aud | aud | aud | aud | aud | aud | p† r† t |
| `Encoder_Query` [41] | aud | aud | aud | aud | aud | aud | r† t |
| `Encoder_RegisterChn` [42] | aud | aud | aud | aud | aud | aud | p† r† t |
| `Encoder_ReleaseStream` [43] | aud | aud | aud | aud | aud | aud | p† r† t |
| `Encoder_RequestGDR` [44] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | – |
| `Encoder_RequestIDR` [45] | aud | aud | aud | aud | aud | aud | p† r† t |
| `Encoder_SetAvpuBsShare` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetAvpuBsSize` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetAvpuJpegQp` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetChnBitRate` [46] | n.a. | n.a. | n.a. | n.a. | aud | aud | t |
| `Encoder_SetChnEntropyMode` [47] | n.a. | n.a. | n.a. | n.a. | aud | n.a. | – |
| `Encoder_SetChnGopLength` [48] | n.a. | n.a. | n.a. | n.a. | aud | aud | – |
| `Encoder_SetChnHSkipBlackEnhance` | miss | miss | miss | aud | n.a. | n.a. | – |
| `Encoder_SetChnInitQP` [49] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | – |
| `Encoder_SetChnMapRoi` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `Encoder_SetChnMaxPictureSize` [11] | n.a. | n.a. | n.a. | host | n.a. | cache | T41: r† |
| `Encoder_SetChnQp` [50] | n.a. | n.a. | n.a. | n.a. | aud | n.a. | – |
| `Encoder_SetChnQpBounds` [51] | n.a. | n.a. | n.a. | n.a. | aud | aud | t |
| `Encoder_SetChnQpBoundsPerFrame` [52] | n.a. | n.a. | n.a. | n.a. | n.a. | aud | – |
| `Encoder_SetChnQpIPDelta` [53] | n.a. | n.a. | n.a. | n.a. | aud | n.a. | t |
| `Encoder_SetChnResizeMode` [54] | n.a. | n.a. | n.a. | n.a. | aud | aud | – |
| `Encoder_SetDefaultParam` [55] | n.a. | n.a. | n.a. | n.a. | aud | aud | t |
| `Encoder_SetFrameRelease` [12] | n.a. | n.a. | n.a. | n.a. | cache | miss | – |
| `Encoder_SetGOPSize` [56] | aud | aud | aud | aud | n.a. | n.a. | r† |
| `Encoder_SetIvpuBsSize` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetMultiSectionMode` [13] | n.a. | n.a. | n.a. | cache | n.a. | n.a. | – |
| ► `Encoder_SetbufshareChn` [14] | n.a. | n.a. | n.a. | n.a. | stub | stub | p† r† |
| `Encoder_StartRecvPic` [57] | aud | aud | aud | aud | aud | aud | p† r† t |
| `Encoder_StopRecvPic` [58] | aud | aud | aud | aud | aud | aud | p† r† t |
| `Encoder_UnRegisterChn` [59] | aud | aud | aud | aud | aud | aud | p† r† t |
| `Encoder_VbmAlloc` | n.a. | n.a. | n.a. | aud | n.a. | miss | T23: t |
| `Encoder_VbmAlloc_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_VbmFree` | n.a. | n.a. | n.a. | aud | n.a. | miss | T23: t |
| `Encoder_VbmFree_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_VbmP2V` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `Encoder_VbmV2P` | n.a. | n.a. | n.a. | aud | n.a. | miss | T23: t |
| `Encoder_YuvEncode` | n.a. | n.a. | n.a. | aud | n.a. | miss | T23: t |
| `Encoder_YuvEncode_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_YuvExit` | n.a. | n.a. | n.a. | aud | n.a. | miss | T23: t |
| `Encoder_YuvExit_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_YuvGetCrop` [60] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | – |
| `Encoder_YuvInit` | n.a. | n.a. | n.a. | aud | n.a. | miss | T23: t |
| `Encoder_YuvInit_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_YuvRequestIDR` [61] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | t |
| `Encoder_YuvSetCrop` [62] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | – |

Notes:

1. `Decoder_*` (8 functions): functions: CreateChn, DestroyChn, GetFrame, PollingFrame, ReleaseFrame, SendStreamTimeout, StartRecvPic, StopRecvPic.
15. `Encoder_CreateChn`: audit: reaches the driver/kernel
16. `Encoder_CreateGroup`: audit: userspace implementation
17. `Encoder_DestroyChn`: audit: reaches the driver/kernel
18. `Encoder_DestroyGroup`: audit: userspace implementation
19. `Encoder_FlushStream`: all: drops the encoded stream nobody fetched (agg-25)
20. `Encoder_Get/SetChnAttrRcMode`: audit: reaches the driver/kernel; userspace implementation
21. `Encoder_Get/SetChnColor2Grey`: T10+T20+T21: codes grey pictures (agg-25)
22. `Encoder_Get/SetChnCrop`: audit: userspace implementation
2. `Encoder_Get/SetChnDenoise`: T10+T20+T21: refused (-1): Helix/NVPU cannot do it (agg-25); vendor behaviour unverified
23. `Encoder_Get/SetChnFrmRate`: audit: reaches the driver/kernel; userspace implementation
3. `Encoder_Get/SetChnFrmUsedMode`: T23: stored in the channel attribute (claude/t23-enc-rest, not in agg-25)
24. `Encoder_Get/SetChnGopAttr`: audit: userspace implementation
25. `Encoder_Get/SetChnROI`: T10: claude/t1x-roi (not in agg-25): EFE ROI registers per the OEM slice init; T10 unverified on a device; T20: claude/t1x-roi (not in agg-25): QP51 region blocky, QP15 fine on cam-C 2026-10-05; absolute QP 15 raises the bitrate 1.4 to 9.9 Mbit/s (CBR bypassed, vendor semantics unverified); T21: vendor 1.0.33 never programs IMP ROIs; ours only with OPENIMP_T21_ROI=1 (claude/t1x-roi, beyond vendor, user decision pending); without it the call is refused in agg-25
4. `Encoder_Get/SetFisheyeEnableStatus`: T23+T31: kept for getter only (documented in source)
26. `Encoder_Get/SetGDRCfg`: audit: userspace implementation
5. `Encoder_Get/SetH264TransCfg`: T10: T10 has no chroma-offset register, stays 0; T20: chroma QP offset via PPS + reg 0x40120 (claude/t1x-roi, not in agg-25): verified on cam-C, no colour shift; T21: chroma QP offset, PPS rewrite (claude/t1x-roi, not in agg-25); not device-tested
6. `Encoder_Get/SetH265TransCfg`: T21+T23: stored in channel, never pushed
7. `Encoder_Get/SetJpegeQl`: T10+T20+T21: only applied at CreateChn (p2_encoder.c:1771); live change ignored; T41: live and at CreateChn (agg-25); T10/T20/T21 still applied at CreateChn only; used by: T10: prudynt†, raptor†; T20/T21/T23: prudynt†, raptor†, timps
27. `Encoder_Get/SetMaxStreamCnt`: audit: userspace implementation
8. `Encoder_Get/SetMbRC`: T10: commit names T20 only; T10 shares the build; T20: switches the macroblock QP table (agg-25)
28. `Encoder_Get/SetPool`: audit: userspace implementation
9. `Encoder_Get/SetQpgMode`: T21: refused (-1) since agg-25; vendor behaviour unverified
29. `Encoder_Get/SetStreamBufSize`: audit: userspace implementation
30. `Encoder_Get/SetSuperFrameCfg`: T10+T20+T21: reaches the rate control or fails (agg-25)
10. `Encoder_Get/Setframelossthd`: T23: kept for getter only (documented in source)
31. `Encoder_GetChnAttr`: audit: userspace implementation
32. `Encoder_GetChnAveBitrate`: audit: userspace implementation
33. `Encoder_GetChnEncType`: audit: userspace implementation
34. `Encoder_GetChnEvalInfo`: T31: claude/t23-enc-rest (not in agg-25)
35. `Encoder_GetChnMaxPictureSize`: T23: as the OEM stores it, re-encode on overshoot (claude/t23-enc-rest, not in agg-25)
36. `Encoder_GetFd`: audit: userspace implementation
37. `Encoder_GetStream`: audit: reaches the driver/kernel
38. `Encoder_InsertUserData`: audit: userspace implementation
39. `Encoder_PollingModuleStream`: audit: reaches the driver/kernel
40. `Encoder_PollingStream`: audit: reaches the driver/kernel
41. `Encoder_Query`: audit: userspace implementation
42. `Encoder_RegisterChn`: audit: userspace implementation
43. `Encoder_ReleaseStream`: audit: reaches the driver/kernel
44. `Encoder_RequestGDR`: audit: userspace implementation
45. `Encoder_RequestIDR`: audit: userspace implementation
46. `Encoder_SetChnBitRate`: audit: userspace implementation
47. `Encoder_SetChnEntropyMode`: audit: userspace implementation
48. `Encoder_SetChnGopLength`: audit: userspace implementation
49. `Encoder_SetChnInitQP`: audit: userspace implementation
11. `Encoder_SetChnMaxPictureSize`: T23: as the OEM stores it, re-encode on overshoot (claude/t23-enc-rest, not in agg-25); T41: written into rcAttr copy, codec not updated (T23: loss threshold kept only)
50. `Encoder_SetChnQp`: audit: userspace implementation
51. `Encoder_SetChnQpBounds`: audit: userspace implementation
52. `Encoder_SetChnQpBoundsPerFrame`: audit: userspace implementation
53. `Encoder_SetChnQpIPDelta`: audit: userspace implementation
54. `Encoder_SetChnResizeMode`: audit: userspace implementation
55. `Encoder_SetDefaultParam`: audit: userspace implementation
12. `Encoder_SetFrameRelease`: T31: kept for getter only (documented in source)
56. `Encoder_SetGOPSize`: audit: reaches the driver/kernel; userspace implementation
13. `Encoder_SetMultiSectionMode`: T23: kept for getter only (documented in source)
14. `Encoder_SetbufshareChn`: T31+T41: validates channel numbers, returns 0
57. `Encoder_StartRecvPic`: audit: userspace implementation
58. `Encoder_StopRecvPic`: audit: userspace implementation
59. `Encoder_UnRegisterChn`: audit: userspace implementation
60. `Encoder_YuvGetCrop`: audit: reaches the driver/kernel
61. `Encoder_YuvRequestIDR`: audit: reaches the driver/kernel
62. `Encoder_YuvSetCrop`: audit: reaches the driver/kernel

</details>

### Framesource

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by |
|---|---|---|---|---|---|---|---|
| `EmuFrameSource_*` (4 functions) [1] | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FB_*` (5 functions) [2] | n.a. | n.a. | n.a. | n.a. | miss | n.a. | – |
| `FrameSource_DequeueBuffer` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `FrameSource_ExternInject_CreateChn` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `FrameSource_ExternInject_DestroyChn` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `FrameSource_ExternInject_DisableChn` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `FrameSource_ExternInject_EnableChn` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `FrameSource_Get/SetChnFifoAttr` [3] | host | host | host | host | host | cache | p† r† |
| ► `FrameSource_Get/SetDelay` [4] | host | host | host | host | host | err | T31/T41: r† |
| ► `FrameSource_Get/SetFrameDepth` [5] | aud | aud | aud | aud | aud | cache | see note |
| ► `FrameSource_Get/SetI2dAttr` [6] | n.a. | n.a. | n.a. | n.a. | n.a. | err | r† |
| ► `FrameSource_Get/SetMaxDelay` [7] | host | host | host | host | host | err | T31/T41: r† |
| ► `FrameSource_Get/SetPool` [8] | n.a. | n.a. | n.a. | host | host | cache | T31/T41: r† |
| `FrameSource_GetFrameEx` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `FrameSource_GetTimedFrame` [9] | host | host | host | host | host | err | r† |
| `FrameSource_QueueBuffer` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `FrameSource_ReleaseFrameEx` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `FrameSource_SetYuvAlign` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |

Notes:

1. `EmuFrameSource_*` (4 functions): functions: CreateChn, DestroyChn, DisableChn, EnableChn.
2. `FB_*` (5 functions): functions: CreateGroup, DestroyGroup, DisableDev, EnableDev, GetDevInfo.
3. `FrameSource_Get/SetChnFifoAttr`: T10+T20+T21+T23+T31: = SetMaxDelay(maxdepth) (agg-25); FIFO_DATA_PRIORITY refused for maxdepth>0; T41: FIFO attr stored, no FIFO behind it
4. `FrameSource_Get/SetDelay`: T10+T20+T21+T23+T31: real delay FIFO (agg-25); T10 shares the T20 build; FIFO_DATA_PRIORITY refused for maxdepth>0; T41: ENOTSUP stub (returns -1)
5. `FrameSource_Get/SetFrameDepth`: T41: depth stored, GetFrame ignores it (T41 p1); used by: T10/T20/T21/T31/T41: prudynt†, raptor†; T23: prudynt, raptor, timps
6. `FrameSource_Get/SetI2dAttr`: T41: ENOTSUP stub (returns -1)
7. `FrameSource_Get/SetMaxDelay`: T10+T20+T21+T23+T31: real delay FIFO (agg-25); T10 shares the T20 build; FIFO_DATA_PRIORITY refused for maxdepth>0; T41: ENOTSUP stub (returns -1)
8. `FrameSource_Get/SetPool`: T23+T31: real memory pools (claude/t23t31-cacheonly, not in agg-25); T41: pool id recorded only
9. `FrameSource_GetTimedFrame`: T10+T20+T21+T23+T31: agg-25; T41: ENOTSUP stub (returns -1)

<details><summary>All 30 rows of this area (18 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by |
|---|---|---|---|---|---|---|---|
| `EmuFrameSource_*` (4 functions) [1] | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FB_*` (5 functions) [2] | n.a. | n.a. | n.a. | n.a. | miss | n.a. | – |
| `FrameSource_ChnStatQuery` [10] | n.a. | n.a. | n.a. | n.a. | aud | n.a. | – |
| `FrameSource_CreateChn` [11] | aud | aud | aud | aud | aud | aud | p† r† t |
| `FrameSource_DequeueBuffer` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `FrameSource_DestroyChn` [12] | aud | aud | aud | aud | aud | aud | p† r† t |
| `FrameSource_DisableChn` [13] | aud | aud | aud | aud | aud | aud | p† r† t |
| `FrameSource_EnableChn` [14] | aud | aud | aud | aud | aud | aud | p† r† t |
| `FrameSource_ExternInject_CreateChn` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `FrameSource_ExternInject_DestroyChn` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `FrameSource_ExternInject_DisableChn` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `FrameSource_ExternInject_EnableChn` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `FrameSource_Get/SetChnAttr` [15] | aud | aud | aud | aud | aud | aud | p† r† t |
| ► `FrameSource_Get/SetChnFifoAttr` [3] | host | host | host | host | host | cache | p† r† |
| ► `FrameSource_Get/SetDelay` [4] | host | host | host | host | host | err | T31/T41: r† |
| `FrameSource_Get/SetDirectModeAttr` [16] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | – |
| ► `FrameSource_Get/SetFrameDepth` [5] | aud | aud | aud | aud | aud | cache | see note |
| ► `FrameSource_Get/SetI2dAttr` [6] | n.a. | n.a. | n.a. | n.a. | n.a. | err | r† |
| ► `FrameSource_Get/SetMaxDelay` [7] | host | host | host | host | host | err | T31/T41: r† |
| ► `FrameSource_Get/SetPool` [8] | n.a. | n.a. | n.a. | host | host | cache | T31/T41: r† |
| `FrameSource_GetFrame` [17] | aud | aud | aud | aud | aud | aud | see note |
| `FrameSource_GetFrameEx` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `FrameSource_GetTimedFrame` [9] | host | host | host | host | host | err | r† |
| `FrameSource_QueueBuffer` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `FrameSource_ReleaseFrame` [18] | aud | aud | aud | aud | aud | aud | see note |
| `FrameSource_ReleaseFrameEx` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `FrameSource_SetChnRotate` [19] | n.a. | n.a. | n.a. | n.a. | aud | n.a. | t |
| `FrameSource_SetSource` [20] | n.a. | n.a. | n.a. | n.a. | aud | n.a. | – |
| `FrameSource_SetYuvAlign` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `FrameSource_SnapFrame` [21] | aud | aud | aud | aud | aud | host | r† |

Notes:

1. `EmuFrameSource_*` (4 functions): functions: CreateChn, DestroyChn, DisableChn, EnableChn.
2. `FB_*` (5 functions): functions: CreateGroup, DestroyGroup, DisableDev, EnableDev, GetDevInfo.
10. `FrameSource_ChnStatQuery`: audit: userspace implementation
11. `FrameSource_CreateChn`: audit: reaches the driver/kernel
12. `FrameSource_DestroyChn`: audit: reaches the driver/kernel
13. `FrameSource_DisableChn`: audit: reaches the driver/kernel
14. `FrameSource_EnableChn`: audit: reaches the driver/kernel
15. `FrameSource_Get/SetChnAttr`: audit: userspace implementation
3. `FrameSource_Get/SetChnFifoAttr`: T10+T20+T21+T23+T31: = SetMaxDelay(maxdepth) (agg-25); FIFO_DATA_PRIORITY refused for maxdepth>0; T41: FIFO attr stored, no FIFO behind it
4. `FrameSource_Get/SetDelay`: T10+T20+T21+T23+T31: real delay FIFO (agg-25); T10 shares the T20 build; FIFO_DATA_PRIORITY refused for maxdepth>0; T41: ENOTSUP stub (returns -1)
16. `FrameSource_Get/SetDirectModeAttr`: audit: userspace implementation
5. `FrameSource_Get/SetFrameDepth`: T41: depth stored, GetFrame ignores it (T41 p1); used by: T10/T20/T21/T31/T41: prudynt†, raptor†; T23: prudynt, raptor, timps
6. `FrameSource_Get/SetI2dAttr`: T41: ENOTSUP stub (returns -1)
7. `FrameSource_Get/SetMaxDelay`: T10+T20+T21+T23+T31: real delay FIFO (agg-25); T10 shares the T20 build; FIFO_DATA_PRIORITY refused for maxdepth>0; T41: ENOTSUP stub (returns -1)
8. `FrameSource_Get/SetPool`: T23+T31: real memory pools (claude/t23t31-cacheonly, not in agg-25); T41: pool id recorded only
17. `FrameSource_GetFrame`: audit: reaches the driver/kernel; userspace implementation; used by: T10/T20/T21/T31/T41: raptor†; T23: raptor, timps
9. `FrameSource_GetTimedFrame`: T10+T20+T21+T23+T31: agg-25; T41: ENOTSUP stub (returns -1)
18. `FrameSource_ReleaseFrame`: audit: reaches the driver/kernel; used by: T10/T20/T21/T31/T41: raptor†; T23: raptor, timps
19. `FrameSource_SetChnRotate`: audit: userspace implementation
20. `FrameSource_SetSource`: audit: userspace implementation
21. `FrameSource_SnapFrame`: T41: copies the next consumer frame, packed NV12 (agg-25); user earlier did not want it

</details>

### OSD

All regular OSD functions (`OSD_CreateGroup`, `CreateRgn`, `RegisterRgn`, `Set/GetRgnAttr`, `Set/GetGrpRgnAttr`, `ShowRgn`, `UpdateRgnAttrData`, `Start/StopGroup` ...) are done on every SoC (OSD is device-tested on T10, T20, T21, T23, T31 and T41, see the OSD row in the matrix above); in the full list they show as done (?) because the audit tracks no per-call device test. The gap table lists the ISP-OSD variants (`*_ISP`, `ISP_Tuning_*Osd*`) that only the T23/T41 vendor API has, plus single helpers; n.a. means the function does not exist in that SoC's vendor API, not that OSD is missing.

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by |
|---|---|---|---|---|---|---|---|
| ► `ISP_Tuning_CreateOsdRgn` [1] | n.a. | n.a. | n.a. | aud | n.a. | err | r† |
| ► `ISP_Tuning_DestroyOsdRgn` [2] | n.a. | n.a. | n.a. | aud | n.a. | err | r† |
| `ISP_Tuning_Get/SetOSDAttr` [3] | n.a. | n.a. | n.a. | err | n.a. | miss | – |
| `ISP_Tuning_Get/SetOSDBlock` [4] | n.a. | n.a. | n.a. | err | n.a. | miss | – |
| `ISP_Tuning_GetOsdRgnAttr` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| ► `ISP_Tuning_SetOsdPoolSize` [5] | n.a. | n.a. | n.a. | aud | n.a. | stub | r† |
| ► `ISP_Tuning_SetOsdRgnAttr` [6] | n.a. | n.a. | n.a. | aud | n.a. | err | r† |
| ► `ISP_Tuning_ShowOsdRgn` [7] | n.a. | n.a. | n.a. | aud | n.a. | err | r† |
| `OSD_AttachToGroup` | miss | miss | miss | aud | aud | aud | – |
| `OSD_CreateRgn_ISP` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `OSD_DestroyRgn_ISP` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `OSD_Exit_ISP` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `OSD_Get/SetRgnAttr_ISP` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `OSD_GetRegionLuma` | n.a. | n.a. | n.a. | miss | n.a. | miss | – |
| `OSD_GetRgnAttr_ISPPic` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `OSD_SetGroupCallback` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `OSD_SetPoolSize_ISP` [8] | n.a. | n.a. | n.a. | host | n.a. | miss | – |
| `OSD_SetRgnAttr_PicISP` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `OSD_ShowRgn_ISP` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |

Notes:

1. `ISP_Tuning_CreateOsdRgn`: T41: ENOTSUP stub (returns -1)
2. `ISP_Tuning_DestroyOsdRgn`: T41: ENOTSUP stub (returns -1)
3. `ISP_Tuning_Get/SetOSDAttr`: T23: driver rejects 0x8000181 (-EINVAL)
4. `ISP_Tuning_Get/SetOSDBlock`: T23: driver rejects 0x8000182 (-EINVAL)
5. `ISP_Tuning_SetOsdPoolSize`: T41: 2 insns, returns ?
6. `ISP_Tuning_SetOsdRgnAttr`: T41: ENOTSUP stub (returns -1)
7. `ISP_Tuning_ShowOsdRgn`: T41: ENOTSUP stub (returns -1)
8. `OSD_SetPoolSize_ISP`: T23: ISP OSD pictures from the pool (claude/t23t31-cacheonly, not in agg-25)

<details><summary>All 36 rows of this area (19 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by |
|---|---|---|---|---|---|---|---|
| ► `ISP_Tuning_CreateOsdRgn` [1] | n.a. | n.a. | n.a. | aud | n.a. | err | r† |
| ► `ISP_Tuning_DestroyOsdRgn` [2] | n.a. | n.a. | n.a. | aud | n.a. | err | r† |
| `ISP_Tuning_Get/SetOSDAttr` [3] | n.a. | n.a. | n.a. | err | n.a. | miss | – |
| `ISP_Tuning_Get/SetOSDBlock` [4] | n.a. | n.a. | n.a. | err | n.a. | miss | – |
| `ISP_Tuning_GetOsdRgnAttr` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| ► `ISP_Tuning_SetOsdPoolSize` [5] | n.a. | n.a. | n.a. | aud | n.a. | stub | r† |
| ► `ISP_Tuning_SetOsdRgnAttr` [6] | n.a. | n.a. | n.a. | aud | n.a. | err | r† |
| ► `ISP_Tuning_ShowOsdRgn` [7] | n.a. | n.a. | n.a. | aud | n.a. | err | r† |
| `OSD_AttachToGroup` | miss | miss | miss | aud | aud | aud | – |
| `OSD_CreateGroup` [9] | aud | aud | aud | aud | aud | aud | p† r† t |
| `OSD_CreateRgn` [10] | aud | aud | aud | aud | aud | aud | p† r† t |
| `OSD_CreateRgn_ISP` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `OSD_DestroyGroup` [11] | aud | aud | aud | aud | aud | aud | p† r† t |
| `OSD_DestroyRgn` [12] | aud | aud | aud | aud | aud | aud | p† r† t |
| `OSD_DestroyRgn_ISP` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `OSD_Exit_ISP` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `OSD_Get/SetGrpRgnAttr` [13] | aud | aud | aud | aud | aud | aud | p† r† t |
| `OSD_Get/SetRgnAttr` [14] | aud | aud | aud | aud | aud | aud | p† r† t |
| `OSD_Get/SetRgnAttr_ISP` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `OSD_GetRegionLuma` | n.a. | n.a. | n.a. | miss | n.a. | miss | – |
| `OSD_GetRgnAttr_ISPPic` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `OSD_RegisterRgn` [15] | aud | aud | aud | aud | aud | aud | p† r† t |
| `OSD_RgnCreate_Query` [16] | n.a. | n.a. | n.a. | aud | n.a. | aud | – |
| `OSD_RgnRegister_Query` [17] | n.a. | n.a. | n.a. | aud | n.a. | aud | – |
| `OSD_SetGroupCallback` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `OSD_SetMosaic` [18] | n.a. | n.a. | n.a. | aud | n.a. | aud | – |
| `OSD_SetPoolSize` [19] | aud | aud | aud | aud | aud | aud | p† r† t |
| `OSD_SetPoolSize_ISP` [8] | n.a. | n.a. | n.a. | host | n.a. | miss | – |
| `OSD_SetRgnAttrWithTimestamp` [20] | aud | aud | aud | aud | aud | aud | r† |
| `OSD_SetRgnAttr_PicISP` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `OSD_ShowRgn` [21] | aud | aud | aud | aud | aud | aud | p† r† t |
| `OSD_ShowRgn_ISP` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `OSD_Start` [22] | aud | aud | aud | aud | aud | aud | p† r† t |
| `OSD_Stop` [23] | aud | aud | aud | aud | aud | aud | r† |
| `OSD_UnRegisterRgn` [24] | aud | aud | aud | aud | aud | aud | p† r† t |
| `OSD_UpdateRgnAttrData` [25] | aud | aud | aud | aud | aud | aud | p† r† |

Notes:

1. `ISP_Tuning_CreateOsdRgn`: T41: ENOTSUP stub (returns -1)
2. `ISP_Tuning_DestroyOsdRgn`: T41: ENOTSUP stub (returns -1)
3. `ISP_Tuning_Get/SetOSDAttr`: T23: driver rejects 0x8000181 (-EINVAL)
4. `ISP_Tuning_Get/SetOSDBlock`: T23: driver rejects 0x8000182 (-EINVAL)
5. `ISP_Tuning_SetOsdPoolSize`: T41: 2 insns, returns ?
6. `ISP_Tuning_SetOsdRgnAttr`: T41: ENOTSUP stub (returns -1)
7. `ISP_Tuning_ShowOsdRgn`: T41: ENOTSUP stub (returns -1)
9. `OSD_CreateGroup`: audit: userspace implementation
10. `OSD_CreateRgn`: audit: reaches the driver/kernel
11. `OSD_DestroyGroup`: audit: userspace implementation
12. `OSD_DestroyRgn`: audit: reaches the driver/kernel
13. `OSD_Get/SetGrpRgnAttr`: audit: userspace implementation
14. `OSD_Get/SetRgnAttr`: audit: reaches the driver/kernel; userspace implementation
15. `OSD_RegisterRgn`: audit: reaches the driver/kernel; userspace implementation
16. `OSD_RgnCreate_Query`: audit: userspace implementation
17. `OSD_RgnRegister_Query`: audit: userspace implementation
18. `OSD_SetMosaic`: audit: userspace implementation
19. `OSD_SetPoolSize`: audit: userspace implementation
8. `OSD_SetPoolSize_ISP`: T23: ISP OSD pictures from the pool (claude/t23t31-cacheonly, not in agg-25)
20. `OSD_SetRgnAttrWithTimestamp`: audit: reaches the driver/kernel
21. `OSD_ShowRgn`: audit: reaches the driver/kernel; userspace implementation
22. `OSD_Start`: audit: userspace implementation
23. `OSD_Stop`: audit: userspace implementation
24. `OSD_UnRegisterRgn`: audit: userspace implementation
25. `OSD_UpdateRgnAttrData`: audit: reaches the driver/kernel

</details>

### IVS

No IVS row has a gap; all IVS functions are in the full list below.

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

*No row of this area has a gap.*

<details><summary>All 17 rows of this area (0 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by |
|---|---|---|---|---|---|---|---|
| `IVS_CreateBaseMoveInterface` [1] | aud | aud | aud | aud | aud | aud | r† |
| `IVS_CreateChn` [2] | aud | aud | aud | aud | aud | aud | r† t |
| `IVS_CreateGroup` [3] | aud | aud | aud | aud | aud | aud | r† t |
| `IVS_CreateMoveInterface` [4] | aud | aud | aud | aud | aud | aud | r† t |
| `IVS_DestroyBaseMoveInterface` [5] | aud | aud | aud | aud | aud | aud | r† |
| `IVS_DestroyChn` [6] | aud | aud | aud | aud | aud | aud | p† r† t |
| `IVS_DestroyGroup` [7] | aud | aud | aud | aud | aud | aud | p† r† t |
| `IVS_DestroyMoveInterface` [8] | aud | aud | aud | aud | aud | aud | p† r† t |
| `IVS_Get/SetParam` [9] | aud | aud | aud | aud | aud | aud | r† t |
| `IVS_GetResult` [10] | aud | aud | aud | aud | aud | aud | r† t |
| `IVS_PollingResult` [11] | aud | aud | aud | aud | aud | aud | r† t |
| `IVS_RegisterChn` [12] | aud | aud | aud | aud | aud | aud | r† t |
| `IVS_ReleaseData` [13] | aud | aud | aud | aud | aud | aud | r† |
| `IVS_ReleaseResult` [14] | aud | aud | aud | aud | aud | aud | r† t |
| `IVS_StartRecvPic` [15] | aud | aud | aud | aud | aud | aud | r† t |
| `IVS_StopRecvPic` [16] | aud | aud | aud | aud | aud | aud | p† r† t |
| `IVS_UnRegisterChn` [17] | aud | aud | aud | aud | aud | aud | p† r† t |

Notes:

1. `IVS_CreateBaseMoveInterface`: audit: userspace implementation
2. `IVS_CreateChn`: audit: userspace implementation
3. `IVS_CreateGroup`: audit: userspace implementation
4. `IVS_CreateMoveInterface`: audit: userspace implementation
5. `IVS_DestroyBaseMoveInterface`: audit: userspace implementation
6. `IVS_DestroyChn`: audit: userspace implementation
7. `IVS_DestroyGroup`: audit: userspace implementation
8. `IVS_DestroyMoveInterface`: audit: userspace implementation
9. `IVS_Get/SetParam`: audit: userspace implementation
10. `IVS_GetResult`: audit: userspace implementation
11. `IVS_PollingResult`: audit: userspace implementation
12. `IVS_RegisterChn`: audit: userspace implementation
13. `IVS_ReleaseData`: audit: userspace implementation
14. `IVS_ReleaseResult`: audit: userspace implementation
15. `IVS_StartRecvPic`: audit: userspace implementation
16. `IVS_StopRecvPic`: audit: userspace implementation
17. `IVS_UnRegisterChn`: audit: userspace implementation

</details>

### Audio

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by |
|---|---|---|---|---|---|---|---|
| `ADEC_ReleaseDecoder` | miss | miss | n.a. | n.a. | miss | n.a. | – |
| `AENC_ReleaseEncoder` | miss | miss | n.a. | n.a. | miss | n.a. | – |
| `AI_DisableAlgo` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `AI_DisableGetRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `AI_DisableHs` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `AI_EnableAlgo` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `AI_EnableGetRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `AI_EnableHs` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `AI_Get/SetDigitalGain` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `AI_GetFrameAndRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `AI_SetHpfCoFrequency` [1] | n.a. | n.a. | n.a. | cache | cache | cache | p† r† |
| `AO_DisableAlgo` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `AO_EnableAlgo` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `AO_Get/SetDigitalGain` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Audio_Select_Codec` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `DMIC_*` (20 functions) [2] | n.a. | n.a. | n.a. | n.a. | aud | err | r† |
| ► `DMIC_DisableAecRefFrame` [3] | n.a. | n.a. | n.a. | n.a. | n.a. | err | r† |

Notes:

1. `AI_SetHpfCoFrequency`: T23+T31+T41: cutoff recorded, fixed 300 Hz HPF used
2. `DMIC_*` (20 functions): functions: Disable, DisableAec, DisableChn, Enable, EnableAec, EnableAecRefFrame, EnableChn, Get/SetChnParam, GetFrame, GetFrameAndRef, Get/SetGain, Get/SetPubAttr, Get/SetVol, PollingFrame, ReleaseFrame, SetUserInfo. T41: ENOTSUP stub (returns -1)
3. `DMIC_DisableAecRefFrame`: T41: ENOTSUP stub (returns -1)

<details><summary>All 66 rows of this area (17 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by |
|---|---|---|---|---|---|---|---|
| `ADEC_ReleaseDecoder` | miss | miss | n.a. | n.a. | miss | n.a. | – |
| `AENC_* / ADEC_*` (17 functions) [4] | host | host | host | aud | aud | host | r† |
| `AENC_ReleaseEncoder` | miss | miss | n.a. | n.a. | miss | n.a. | – |
| `AI_Disable` [5] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_DisableAec` [6] | aud | aud | aud | aud | aud | aud | see note |
| `AI_DisableAecRefFrame` [7] | aud | aud | aud | aud | aud | aud | r† |
| `AI_DisableAgc` [8] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_DisableAlgo` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `AI_DisableChn` [9] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_DisableGetRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `AI_DisableHpf` [10] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_DisableHs` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `AI_DisableNs` [11] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_Enable` [12] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_EnableAec` [13] | aud | aud | aud | aud | aud | aud | see note |
| `AI_EnableAecRefFrame` [14] | aud | aud | aud | aud | aud | aud | r† |
| `AI_EnableAgc` [15] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_EnableAlgo` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `AI_EnableChn` [16] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_EnableGetRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `AI_EnableHpf` [17] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_EnableHs` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `AI_EnableNs` [18] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_Get/SetAlcGain` [19] | n.a. | n.a. | aud | n.a. | aud | n.a. | t |
| `AI_Get/SetChnParam` [20] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_Get/SetDigitalGain` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `AI_Get/SetGain` [21] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_Get/SetPubAttr` [22] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_Get/SetVol` [23] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_GetFrame` [24] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_GetFrameAndRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `AI_GetFrameAndRef` [25] | aud | aud | aud | aud | aud | aud | r† |
| `AI_PollingFrame` [26] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_ReleaseFrame` [27] | aud | aud | aud | aud | aud | aud | p† r† t |
| `AI_SetAgcMode` [28] | n.a. | n.a. | n.a. | n.a. | aud | n.a. | – |
| ► `AI_SetHpfCoFrequency` [1] | n.a. | n.a. | n.a. | cache | cache | cache | p† r† |
| `AI_SetVolMute` [29] | aud | aud | aud | aud | aud | aud | r† |
| `AI_Set_WebrtcProfileIni_Path` [30] | n.a. | n.a. | n.a. | aud | aud | aud | r† |
| `AO_CacheSwitch` [31] | host | host | host | host | host | host | r† |
| `AO_ClearChnBuf` [32] | aud | aud | aud | aud | aud | aud | see note |
| `AO_Disable` [33] | aud | aud | aud | aud | aud | aud | see note |
| `AO_DisableAgc` [34] | aud | aud | aud | aud | aud | aud | r† |
| `AO_DisableAlgo` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `AO_DisableChn` [35] | aud | aud | aud | aud | aud | aud | see note |
| `AO_DisableHpf` [36] | aud | aud | aud | aud | aud | aud | r† |
| `AO_Enable` [37] | aud | aud | aud | aud | aud | aud | see note |
| `AO_EnableAgc` [38] | aud | aud | aud | aud | aud | aud | r† |
| `AO_EnableAlgo` | n.a. | n.a. | n.a. | aud | n.a. | miss | – |
| `AO_EnableChn` [39] | aud | aud | aud | aud | aud | aud | see note |
| `AO_EnableHpf` [40] | aud | aud | aud | aud | aud | aud | r† |
| `AO_FlushChnBuf` [41] | aud | aud | aud | aud | aud | aud | see note |
| `AO_Get/SetDigitalGain` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `AO_Get/SetGain` [42] | aud | aud | aud | aud | aud | aud | see note |
| `AO_Get/SetPubAttr` [43] | aud | aud | aud | aud | aud | aud | see note |
| `AO_Get/SetVol` [44] | aud | aud | aud | aud | aud | aud | see note |
| `AO_PauseChn` [45] | aud | aud | aud | aud | aud | aud | r† |
| `AO_QueryChnStat` [46] | aud | aud | aud | aud | aud | aud | r† |
| `AO_ResumeChn` [47] | aud | aud | aud | aud | aud | aud | r† |
| `AO_SendFrame` [48] | aud | aud | aud | aud | aud | aud | see note |
| `AO_SetHpfCoFrequency` [49] | n.a. | n.a. | n.a. | aud | aud | aud | p† r† |
| `AO_SetVolMute` [50] | aud | aud | aud | aud | aud | aud | p† r† |
| `AO_Soft_Mute` [51] | aud | aud | aud | aud | aud | aud | r† |
| `AO_Soft_UNMute` [52] | aud | aud | aud | aud | aud | aud | r† |
| `Audio_Select_Codec` | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `DMIC_*` (20 functions) [2] | n.a. | n.a. | n.a. | n.a. | aud | err | r† |
| ► `DMIC_DisableAecRefFrame` [3] | n.a. | n.a. | n.a. | n.a. | n.a. | err | r† |

Notes:

4. `AENC_* / ADEC_*` (17 functions): functions: ClearChnBuf, CreateChn, DestroyChn, GetStream, PollingStream, RegisterDecoder, ReleaseStream, SendStream, UnRegisterDecoder, CreateChn, DestroyChn, GetStream, PollingStream, RegisterEncoder, ReleaseStream, SendFrame, UnRegisterEncoder. T10+T20+T21+T41: claude/aenc-adec-all (in agg-25): shared software codecs; device encode test open
5. `AI_Disable`: audit: reaches the driver/kernel
6. `AI_DisableAec`: audit: reaches the driver/kernel; used by: T10/T20/T31: raptor†, timps; T21/T23/T41: raptor†
7. `AI_DisableAecRefFrame`: audit: reaches the driver/kernel
8. `AI_DisableAgc`: audit: userspace implementation
9. `AI_DisableChn`: audit: reaches the driver/kernel; userspace implementation
10. `AI_DisableHpf`: audit: userspace implementation
11. `AI_DisableNs`: audit: userspace implementation
12. `AI_Enable`: audit: reaches the driver/kernel
13. `AI_EnableAec`: audit: reaches the driver/kernel; used by: T10/T20/T31: raptor†, timps; T21/T23/T41: raptor†
14. `AI_EnableAecRefFrame`: audit: reaches the driver/kernel
15. `AI_EnableAgc`: audit: userspace implementation
16. `AI_EnableChn`: audit: userspace implementation
17. `AI_EnableHpf`: audit: userspace implementation
18. `AI_EnableNs`: audit: userspace implementation
19. `AI_Get/SetAlcGain`: audit: reaches the driver/kernel; userspace implementation
20. `AI_Get/SetChnParam`: audit: userspace implementation
21. `AI_Get/SetGain`: audit: reaches the driver/kernel; userspace implementation
22. `AI_Get/SetPubAttr`: audit: reaches the driver/kernel; userspace implementation
23. `AI_Get/SetVol`: audit: reaches the driver/kernel; userspace implementation
24. `AI_GetFrame`: audit: reaches the driver/kernel; userspace implementation
25. `AI_GetFrameAndRef`: audit: reaches the driver/kernel; userspace implementation
26. `AI_PollingFrame`: audit: userspace implementation
27. `AI_ReleaseFrame`: audit: userspace implementation
28. `AI_SetAgcMode`: audit: userspace implementation
1. `AI_SetHpfCoFrequency`: T23+T31+T41: cutoff recorded, fixed 300 Hz HPF used
29. `AI_SetVolMute`: audit: reaches the driver/kernel; userspace implementation
30. `AI_Set_WebrtcProfileIni_Path`: audit: userspace implementation
31. `AO_CacheSwitch`: all: implemented with vendor semantics, default off (OPENIMP_AO_CACHE=1 = vendor default on); quiet device test open (audio output only on T31)
32. `AO_ClearChnBuf`: audit: reaches the driver/kernel; used by: T10/T20/T31: raptor†, timps; T21/T23/T41: raptor†
33. `AO_Disable`: audit: reaches the driver/kernel; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†
34. `AO_DisableAgc`: audit: userspace implementation
35. `AO_DisableChn`: audit: userspace implementation; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†
36. `AO_DisableHpf`: audit: userspace implementation
37. `AO_Enable`: audit: reaches the driver/kernel; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†
38. `AO_EnableAgc`: audit: userspace implementation
39. `AO_EnableChn`: audit: userspace implementation; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†
40. `AO_EnableHpf`: audit: userspace implementation
41. `AO_FlushChnBuf`: audit: reaches the driver/kernel; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†
42. `AO_Get/SetGain`: audit: reaches the driver/kernel; userspace implementation; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†
43. `AO_Get/SetPubAttr`: audit: reaches the driver/kernel; userspace implementation; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†
44. `AO_Get/SetVol`: audit: reaches the driver/kernel; userspace implementation; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†
45. `AO_PauseChn`: audit: userspace implementation
46. `AO_QueryChnStat`: audit: userspace implementation
47. `AO_ResumeChn`: audit: userspace implementation
48. `AO_SendFrame`: audit: reaches the driver/kernel; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†
49. `AO_SetHpfCoFrequency`: audit: userspace implementation
50. `AO_SetVolMute`: audit: reaches the driver/kernel; userspace implementation
51. `AO_Soft_Mute`: audit: reaches the driver/kernel; userspace implementation
52. `AO_Soft_UNMute`: audit: reaches the driver/kernel; userspace implementation
2. `DMIC_*` (20 functions): functions: Disable, DisableAec, DisableChn, Enable, EnableAec, EnableAecRefFrame, EnableChn, Get/SetChnParam, GetFrame, GetFrameAndRef, Get/SetGain, Get/SetPubAttr, Get/SetVol, PollingFrame, ReleaseFrame, SetUserInfo. T41: ENOTSUP stub (returns -1)
3. `DMIC_DisableAecRefFrame`: T41: ENOTSUP stub (returns -1)

</details>

### System / sysutils / log

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by |
|---|---|---|---|---|---|---|---|
| `Log_Get_Option` | aud | aud | aud | aud | aud | miss | – |
| `Log_Set_Option` | miss | miss | miss | miss | miss | miss | – |
| `SU_Base_SetWkupMode` [1] | n.a. | n.a. | n.a. | n.a. | n.a. | err | – |
| `SU_Base_Shutdown` [2] | err | err | err | err | err | err | – |
| `SU_Battery_GetCapacity` | miss | miss | miss | miss | miss | miss | – |
| `SU_Battery_GetEvent` | miss | miss | miss | miss | miss | miss | – |
| `SU_Battery_GetStatus` | miss | miss | miss | miss | miss | miss | – |
| `SU_Battery_GetVoltageUV` | miss | miss | miss | miss | miss | miss | – |
| `System_MemPoolRequest` [3] | n.a. | n.a. | n.a. | host | host | miss | – |

Notes:

1. `SU_Base_SetWkupMode`: T41: writes the mode number to /sys/power/state; neo PR #1 covers related struct overflows, unmerged
2. `SU_Base_Shutdown`: all: kill(1,SIGCHLD) does not power off busybox init; fix in neo PR #1 (SIGUSR2), unmerged; no streamer uses it
3. `System_MemPoolRequest`: T23+T31: real memory pools (claude/t23t31-cacheonly, not in agg-25)

<details><summary>All 53 rows of this area (9 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T31 | T41 | Used by |
|---|---|---|---|---|---|---|---|
| `Log_Get_Option` | aud | aud | aud | aud | aud | miss | – |
| `Log_Set_Option` | miss | miss | miss | miss | miss | miss | – |
| `SU_ADC_DisableChn` [4] | aud | aud | aud | aud | aud | aud | – |
| `SU_ADC_EnableChn` [5] | aud | aud | aud | aud | aud | aud | – |
| `SU_ADC_Exit` [6] | aud | aud | aud | aud | aud | aud | – |
| `SU_ADC_GetChnValue` [7] | aud | aud | aud | aud | aud | aud | – |
| `SU_Base_DisableAlarm` [8] | aud | aud | aud | aud | aud | aud | – |
| `SU_Base_EnableAlarm` [9] | aud | aud | aud | aud | aud | aud | – |
| `SU_Base_Get/SetAlarm` [10] | aud | aud | aud | aud | aud | aud | – |
| `SU_Base_Get/SetTime` [11] | aud | aud | aud | aud | aud | aud | – |
| `SU_Base_GetDevID` [12] | aud | aud | aud | aud | aud | aud | – |
| `SU_Base_GetModelNumber` [13] | aud | aud | aud | aud | aud | aud | – |
| `SU_Base_GetVersion` [14] | aud | aud | aud | aud | aud | aud | p† r† |
| `SU_Base_PollingAlarm` [15] | aud | aud | aud | aud | aud | aud | – |
| `SU_Base_Raw2SUTime` [16] | aud | aud | aud | aud | aud | aud | – |
| `SU_Base_Reboot` [17] | aud | aud | aud | aud | aud | aud | – |
| `SU_Base_SUTime2Raw` [18] | aud | aud | aud | aud | aud | aud | – |
| `SU_Base_SetWkupMode` [1] | n.a. | n.a. | n.a. | n.a. | n.a. | err | – |
| `SU_Base_Shutdown` [2] | err | err | err | err | err | err | – |
| `SU_Base_Suspend` [19] | aud | aud | aud | aud | aud | aud | – |
| `SU_Battery_GetCapacity` | miss | miss | miss | miss | miss | miss | – |
| `SU_Battery_GetEvent` | miss | miss | miss | miss | miss | miss | – |
| `SU_Battery_GetStatus` | miss | miss | miss | miss | miss | miss | – |
| `SU_Battery_GetVoltageUV` | miss | miss | miss | miss | miss | miss | – |
| `SU_CIPHER_ConfigHandle` [20] | aud | aud | aud | aud | aud | aud | – |
| `SU_CIPHER_CreateHandle` [21] | aud | aud | aud | aud | aud | aud | – |
| `SU_CIPHER_DES_Exit` [22] | n.a. | n.a. | n.a. | n.a. | aud | aud | – |
| `SU_CIPHER_DES_Init` [23] | n.a. | n.a. | n.a. | n.a. | aud | aud | – |
| `SU_CIPHER_DES_Test` [24] | n.a. | n.a. | n.a. | n.a. | aud | aud | – |
| `SU_CIPHER_Decrypt` [25] | aud | aud | aud | aud | aud | aud | – |
| `SU_CIPHER_DestroyHandle` [26] | aud | aud | aud | aud | aud | aud | – |
| `SU_CIPHER_Encrypt` [27] | aud | aud | aud | aud | aud | aud | – |
| `SU_CIPHER_Exit` [28] | aud | aud | aud | aud | aud | aud | – |
| `SU_CIPHER_Init` [29] | aud | aud | aud | aud | aud | aud | – |
| `SU_Key_CloseEvent` [30] | aud | aud | aud | aud | aud | aud | – |
| `SU_Key_DisableEvent` [31] | aud | aud | aud | aud | aud | aud | – |
| `SU_Key_EnableEvent` [32] | aud | aud | aud | aud | aud | aud | – |
| `SU_Key_OpenEvent` [33] | aud | aud | aud | aud | aud | aud | – |
| `SU_Key_ReadEvent` [34] | aud | aud | aud | aud | aud | aud | – |
| `SU_LED_Command` [35] | aud | aud | aud | aud | aud | aud | – |
| `System_Bind` [36] | aud | aud | aud | aud | aud | aud | p† r† t |
| `System_Exit` [37] | aud | aud | aud | aud | aud | aud | p† r† t |
| `System_GetBindbyDest` [38] | aud | aud | aud | aud | aud | aud | r† |
| `System_GetCPUInfo` [39] | aud | aud | aud | aud | aud | aud | p† r† |
| `System_GetTimeStamp` [40] | aud | aud | aud | aud | aud | aud | r† t |
| `System_GetVersion` [41] | aud | aud | aud | aud | aud | aud | p† r† t |
| `System_Init` [42] | aud | aud | aud | aud | aud | aud | p† r† t |
| `System_MemPoolFree` [43] | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `System_MemPoolRequest` [3] | n.a. | n.a. | n.a. | host | host | miss | – |
| `System_ReadReg32` [44] | aud | aud | aud | aud | aud | aud | r† |
| `System_RebaseTimeStamp` [45] | aud | aud | aud | aud | aud | aud | p† r† |
| `System_UnBind` [46] | aud | aud | aud | aud | aud | aud | p† r† t |
| `System_WriteReg32` [47] | aud | aud | aud | aud | aud | aud | r† |

Notes:

4. `SU_ADC_DisableChn`: audit: sysfs/ioctl/syscall path
5. `SU_ADC_EnableChn`: audit: sysfs/ioctl/syscall path
6. `SU_ADC_Exit`: audit: sysfs/ioctl/syscall path
7. `SU_ADC_GetChnValue`: audit: sysfs/ioctl/syscall path
8. `SU_Base_DisableAlarm`: audit: sysfs/ioctl/syscall path
9. `SU_Base_EnableAlarm`: audit: sysfs/ioctl/syscall path
10. `SU_Base_Get/SetAlarm`: audit: sysfs/ioctl/syscall path
11. `SU_Base_Get/SetTime`: audit: sysfs/ioctl/syscall path
12. `SU_Base_GetDevID`: audit: sysfs/ioctl/syscall path
13. `SU_Base_GetModelNumber`: audit: sysfs/ioctl/syscall path
14. `SU_Base_GetVersion`: audit: sysfs/ioctl/syscall path
15. `SU_Base_PollingAlarm`: audit: sysfs/ioctl/syscall path
16. `SU_Base_Raw2SUTime`: audit: sysfs/ioctl/syscall path
17. `SU_Base_Reboot`: audit: sysfs/ioctl/syscall path
18. `SU_Base_SUTime2Raw`: audit: sysfs/ioctl/syscall path
1. `SU_Base_SetWkupMode`: T41: writes the mode number to /sys/power/state; neo PR #1 covers related struct overflows, unmerged
2. `SU_Base_Shutdown`: all: kill(1,SIGCHLD) does not power off busybox init; fix in neo PR #1 (SIGUSR2), unmerged; no streamer uses it
19. `SU_Base_Suspend`: audit: sysfs/ioctl/syscall path
20. `SU_CIPHER_ConfigHandle`: audit: sysfs/ioctl/syscall path
21. `SU_CIPHER_CreateHandle`: audit: sysfs/ioctl/syscall path
22. `SU_CIPHER_DES_Exit`: audit: sysfs/ioctl/syscall path
23. `SU_CIPHER_DES_Init`: audit: sysfs/ioctl/syscall path
24. `SU_CIPHER_DES_Test`: audit: sysfs/ioctl/syscall path
25. `SU_CIPHER_Decrypt`: audit: sysfs/ioctl/syscall path
26. `SU_CIPHER_DestroyHandle`: audit: sysfs/ioctl/syscall path
27. `SU_CIPHER_Encrypt`: audit: sysfs/ioctl/syscall path
28. `SU_CIPHER_Exit`: audit: sysfs/ioctl/syscall path
29. `SU_CIPHER_Init`: audit: sysfs/ioctl/syscall path
30. `SU_Key_CloseEvent`: audit: sysfs/ioctl/syscall path
31. `SU_Key_DisableEvent`: audit: sysfs/ioctl/syscall path
32. `SU_Key_EnableEvent`: audit: sysfs/ioctl/syscall path
33. `SU_Key_OpenEvent`: audit: sysfs/ioctl/syscall path
34. `SU_Key_ReadEvent`: audit: sysfs/ioctl/syscall path
35. `SU_LED_Command`: audit: sysfs/ioctl/syscall path
36. `System_Bind`: audit: userspace implementation
37. `System_Exit`: audit: reaches the driver/kernel; userspace implementation
38. `System_GetBindbyDest`: audit: userspace implementation
39. `System_GetCPUInfo`: audit: reaches the driver/kernel; userspace implementation
40. `System_GetTimeStamp`: audit: userspace implementation
41. `System_GetVersion`: audit: userspace implementation
42. `System_Init`: audit: reaches the driver/kernel
43. `System_MemPoolFree`: T31: real memory pools (claude/t23t31-cacheonly, not in agg-25)
3. `System_MemPoolRequest`: T23+T31: real memory pools (claude/t23t31-cacheonly, not in agg-25)
44. `System_ReadReg32`: audit: reaches the driver/kernel
45. `System_RebaseTimeStamp`: audit: userspace implementation
46. `System_UnBind`: audit: userspace implementation
47. `System_WriteReg32`: audit: reaches the driver/kernel

</details>

### Notes

- **T21 imgfx findings without a row of their own:** Sinter/Temper strength, Sepia (-1), Vivid, ISP bypass bits lsc/dpc/sdns/mdns/sharpen/defog and the FrameSource ch1 scaler crop showed no change in the 2026-10-05 imgfx run (agg-25, cam-J/PC420 #1); the cause (vendor no-op, driver, measurement) was not analysed: unverified. ISP flip and `SetWB` auto give a magenta picture in imgfx on T21 (timps' own flip path is fine): open, not a missing function.
- **OSD, behaviour rather than a missing function:** on T21 the IPU blend is ineffective for about 2 s after a wake from idle; `claude/t21-osd-first-jpeg` (in agg-26) withholds JPEG frames without a confirmed overlay, so the first snapshot after a start now has the OSD (device-tested 2026-10-05, first snapshot about 2 s later). The original stack shows the same missing first-snapshot OSD; the root cause (IPU/OSD group/clock) is not found. T21 OSD can still appear late after a start (2-3 min seen with several starts): unverified cause.
- **IVS:** no vendor IVS function is marked missing or incomplete in the audit. (The IVS GetParam/SetParam overflow found on 2026-10-05 is in raptor-hal, not in OpenIMP.)
- **Not determined:** whether the T41 vendor handles `SetScalerLv`/`SetMaskBlock` (the open driver has no handler); T10 device effect of every T20/T21 fix; the _Sec/MultiCamera_ variants after the T23 fixes; vendor semantics of absolute-QP ROI under CBR on T20; T41 DMIC, ISP-OSD and I2D functions were not looked at again after the audit (all still error/missing); the T10/T20/T21 `GetChnRcAttr`-style missing encoder calls were not re-checked.

