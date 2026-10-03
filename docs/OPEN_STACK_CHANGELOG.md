# Open Stack Changelog

Everything changed, extended or fixed in OpenIMP, open-tx-isp, timps and the thingino
integration since the test campaign started on 2026-09-30. Kept up to date during the campaign.

Last update: 2026-10-04 00:00.

Cameras are anonymised: cam-A (T31), cam-B (T23), cam-C (T20), cam-D (T21), cam-E (T10), cam-F (T41).

## Where each camera stands

All six test cameras run the open kernel driver (open-tx-isp), OpenIMP and timps. Since -all-10 no Ingenic or neo helper libraries (libalog/libsysutils) remain on the images.
"Live" means newer builds loaded from `/tmp` that are lost on reboot.

| Camera | SoC | Stack | State |
|---|---|---|---|
| cam-A | T31 | fully open | Flashed 2026-10-03 20:20-20:30 with open-tx-isp-all-15 / openimp-all-13 / timps-all-15 (full OTA) |
| cam-B | T23 | fully open (native encoder, no OEM helixd) | Flashed 2026-10-03 20:20-20:30 with -all-15 / openimp-all-13 (full OTA); no vendor libimp hybrid any more (/opt/openimp-t23 gone, ~328 KiB saved) |
| cam-C | T20 | fully open | Flashed 2026-10-03 20:20-20:30 with -all-15 / openimp-all-13 (full OTA, kernel patch 0101) |
| cam-E | T10 | fully open | Flashed 2026-10-03 20:20-20:30 with -all-15 / openimp-all-13 (full OTA), boot guard auto |
| cam-D | T21 | fully open | Flashed 2026-10-03 20:20-20:30 with -all-15 / openimp-all-13 (full OTA, reference sharing on); 26/30 snapshots (concurrent test restarted the streamer) |
| cam-F | T41 | fully open | Flashed 2026-10-03 14:20 with open-tx-isp-all-13 / OpenIMP T41 (kernel and rootfs flashed separately); image rev 1 flashed later (isp-m0 in vendor layout), reload still failing |

## OpenIMP (userspace libimp)

| Area | Was | Now | Branch |
|---|---|---|---|
| ISP tuning | Contrast/sharpness started at 0 instead of 0x80 | Vendor defaults | `claude/openimp-quickfixes` |
| ISP tuning T20/T21 | T31 control IDs sent; wrong AeLuma ID; TotalGain/RunningMode/FPS without result pointer | SDK control IDs, pointer semantics as vendor | `claude/openimp-quickfixes`, `claude/exposure-readback`, `claude/t20-tuning-ptr` |
| Day/night T20/T21 | Brightness/contrast/saturation/sharpness not re-sent on switch; sinter/temper sent to rejected IDs | Re-sent like vendor; table-based sinter/temper | `claude/tseries-daynight` |
| ISP tuning T23 | GetSensorAttr wrote past the caller's buffer | Vendor 20-byte layout via bounce buffer | `claude/t23-sensorattr` |
| ISP tuning T31 | AF IDs wrong; SensorAttr/WaitFrame/ModuleControl cache-only | Through the driver with vendor ABI | `claude/t31-isp-gaps` |
| Encoder rate control | CappedVBR/CappedQuality/SMART silently CBR | Mapped to VBR with log line | `claude/openimp-quickfixes` |
| JPEG | Quality ignored (fixed 75 or cached only) | Configured quality applied | `claude/openimp-quickfixes` |
| HEVC T31 | H.265 accepted but streams empty | Real HEVC on the AVPU: VPS/SPS/PPS, slice headers, CABAC init as vendor | `claude/t31-hevc` |
| Encoder telemetry | Channel-stat struct one word short; bitrate not averaged; stack overflow in ChnStatQuery | Vendor layout, real average, fixed | `claude/openimp-quickfixes` |
| T23 encoder | Vendor Helix worker only; worker zeroed all of rmem; wrong RPATH | Per-worker rmem slices; native Helix encoder without vendor code | `claude/t23-helix-worker-fixes`, `claude/t23-native-helix-2` |
| Helix T20/T21/T30 | Encoder issues on the Helix path | Fixed | `claude/t30-helix-fixes` |
| OSD | Never drawn on T20/T21 (Helix path) | IPU OSD hook | `claude/t20-osd` |
| Motion detection | T20/T21/T30 always "no motion" | Real frame-diff IVS ported from T31/T23 | `claude/tseries-ivs` |
| Audio out | Volume/mute ignored; partial OSS fragments dropped | Applied; whole-fragment writes | `claude/openimp-quickfixes`, `claude/t31-ao-fix` |
| Framesource / VBM T21 | Idle teardown freed the pool; later allocations failed in 23 MB rmem | Pool parking and reuse | `claude/t21-bringup` |
| T31 HW JPEG | Software JPEG only | Hardware JPEG path, hardened | `claude/t31-hwjpeg-default` |
| Audio AEC T31/T23 | EnableAec only set a flag (T23) or returned fake success (T31) | Real WebRTC AECM (BSD-3) on the driver's speaker reference; errors when it cannot run; OPENIMP_AEC_STATS diagnostics | `claude/aec` |
| Rotation T31 | SetChnRotate 90/270 returned -1 | Software rotation like the vendor (32x32 tiles) before OSD/IVS/encoder | `claude/t31-rotate` |
| Tools | No way to exercise T23 tuning on device | `t23tune` (show, max gain, IT max, DRC, defog, sinter, flip, max dgain) | `claude/t23-tune-tool` |

## open-tx-isp (kernel driver)

### T31 (reference SoC)
- Lifecycle hardening: locking around frame-channel ioctls, STREAMOFF races, buffers freed under a running ISP, last-close use-after-free, bounded tuning register access; rmmod oops fixed (3 clean cycles).
- Frame-channel DQBUF honours `O_NONBLOCK`.
- Tuning gaps: RGB coefficients as int16, AE ROI getter/setters and EXPR setter, AE histogram edges kept, correct isp-m0 gain lines, isp-w02 VIC error counters, SensorAttr, per-frame WaitFrame (`claude/t31-tuning-gaps`).

### T23
- Exposure readback: Expr/EV/TotalGain were constants; now live values and vendor-format isp-m0 (`claude/exposure-readback`).
- Day→night kernel oops: a zeroed array was used as a wait queue; now a real wait queue (`claude/t23-tuning-wiring`).
- About 45 control IDs silently returned success; unknown IDs are now rejected; WB, CCM, DRC, defog, DPC, CSC, module control, live WB statistics, flip and anti-flicker wired (`claude/t23-tuning-wiring`).
- Night picture stayed purple: the CSC clip register write had lost its argument; night mono restored (`claude/t23-tuning-rest`).
- Max analog gain, IT max, SensorAttr, DRC/defog enable, non-compounding sinter (`claude/t23-tuning-rest`); max ISP digital gain as a new AE stage and Bayer re-sync after sensor flip (`claude/t23-flip-dgain`); SetSensorFPS.
- Static memory: oversized decompiler placeholder arrays shrunk; module 1,598 KB → 1,095 KB (`claude/t23-bss-shrink`).

### T20
- No isp-m0 → vendor-format isp-m0; AE exposure read the table address instead of the value (`claude/exposure-readback`).
- Kernel oops on timps stop/restart: sensors released without unbind; fixed, 20 clean cycles (`claude/t20-stop-oops`).
- Max analog gain applied by the AE; line time reported instead of 0; scene-mode IT limit no longer lost (`claude/t20-ae-limits`).

### T21 (first open bring-up)
- Exposure readback, unreachable controls (0x2c–0x45), EV in wrong units fixed; day/night decided correctly (`claude/t21-exposure`).
- Night flicker (ISP gain cycling 6↔25): now the stock AE itself, lifted instruction-for-instruction from the vendor module, including the second AE stage.
- Noise: 2DNR read its parameters from a text table; denoise never followed gain; registers written without value; fixed.
- Night mono, colour blotches (lens-shading gains doubled per channel) and overexposure (fixed ADR curve) fixed; ADR and defog lifted from stock.
- Control dispatchers lifted from stock: anti-flicker, sensor FPS, readable brightness/contrast/saturation/sharpness, AE ROI/zone/histogram, flip.
- All on `claude/t21-image-fixes`.

## Robustness: better than the vendor driver (2026-10-02)

Goal: identical image behaviour, but cleaner unload/reload, less memory and checked inputs.

| SoC | Branch | Result on device |
|---|---|---|
| T23 | `claude/t23-robust` | 10x timps stop/start incl. kill -9, 10x rmmod/insmod, 0 oops; two out-of-bounds writes fixed (2 KB and 18 KB past arrays); bss 434 → 180 KB |
| T20 | `claude/t20-robust` | rmmod while streaming correctly refused; 10x stop/start + 10x reload, 0 oops; decompile fixes (1 KB copy to address 0, AE reading a kernel address); all 53 user copies checked |
| T31 | `claude/t31-robust` | 10x reload with kill -9, vmalloc leak (252 KB/cycle) fixed, no stuck firmware thread; module 716 KB vs vendor 829 KB |
| T21 | `claude/t21-robust` | 10x stop/start + rmmod/insmod with a snapshot each time, 0 oops; root cause of the old reload oops: ISP statistics DMA still writing into freed buffers, now the ISP is reset first; sensor GPIO release; bss 259 → 106 KB |

OpenIMP: T20 green flicker in the bottom rows fixed by filling the encoder padding rows (`claude/t20-bottom-chroma`; 0 green pixels in 30 frames). Faster IVS (`claude/ivs-opt`; T20 timps CPU 4.1 % → 2.7 % with motion on).

Aggregates: `claude/open-tx-isp-all-4` and `claude/openimp-all-4` (pushed); 58 merged single branches removed. `claude/open-tx-isp-all-5` adds t21-robust and t31-robust-2 (T31: sensor flip with shvflip=1, unload leaks, lazy WDR buffers; MemFree drift per reload 460 → 45 KB); all four cameras flashed with -all-5 images.

## Night (2026-10-04, 00:00)

- **H.265 on T41:** works: the HEVC channel now uses the AVPU path like T31 (it fell into a legacy probe that blocked the core and once rebooted the box). 1080p HEVC on cam-F decodes clean, 0 oops. A stuck AVPU job now times out after 2 s and resets the core instead of hanging the camera [openimp claude/t41-h265].
- **OpenIMP review fixes:** complete O_CLOEXEC, eprc gets FRAME_END for dropped pictures, T31 Allegro RC lock, forced IDR after a YUV error, T20 MB-RC table bounds; T41 CBR overshoot fix now in the main line [claude/imp-review-fixes]. Optimisations: T20 MB-RC 590 → 58 KiB, no per-frame malloc/memset; libimp T31 −34 KB, T41 −20 KB, T21 −17.5 KiB text; eprc pow() → table (bit-identical).
- **CPU:** profiling on cam-A/cam-D; optimised JPEG Huffman parsing (table), OSD cache invalidation, T31 EBSP copy: timpsd T31 8.7 → 7.9 %, T21 17.5 → 15.5 % [claude/imp-cpu-opt].
- **all-19 building:** open-tx-isp all-18 + openimp claude/openimp-all-17 (0f6ca940) + timps main a34a5a2 with USE_OPENIMP=1 (timps now offers DPC/defog/DRC on T10/T20/T21 with OpenIMP). T41 image with H.265 in preparation.
- **Motion detection v2 started:** opt-in: background model per grid cell, suppression after IR/exposure switches, blob grouping with minimum size/duration, bounding boxes and strength via a new versioned API; vendor output unchanged when off.
- **Differences to the vendor (current):** less: T31 CBR without filler NAL, T41 tuning partly unverified, MB-RC opt-in only; deliberately different: H.265 rejected on T10/T20/T21/T23, T21 ring mode without P re-encode, stricter T23 front crop; more: see OPENIMP_BEYOND_VENDOR.md and OPENIMP_SOC_DIFFS.md.

## Late night (2026-10-03, 22:30)

- **all-17 on four cameras:** open-tx-isp claude/open-tx-isp-all-17 e7c86107 / openimp claude/openimp-all-15 afd2d072 flashed 22:15–22:18 on cam-A, cam-C, cam-D, cam-E: 30/30 snapshots, MJPEG, MP4, 0 oops, boot guard not tripped. New: T23 module 622 KB (vendor 857), T31 711 KB (vendor 829), T21 452 KB (vendor 616); T20/T10 vendor-identical rate controller default with working quality_lvl/change_pos; DPC on T10/T20/T21, DRC and defog (Iridix floor) on T20, scene/colour effects on T10 (beyond vendor); MB-RC opt-in. cam-B waits for the review fixes.
- **T41 memory:** rmem 30 → 24 MB via a new u-boot env partition image (old env backed up, MAC kept). MemFree 7.8 MB idle; 5-min stress with 3 streams + 2 snapshot loops: no OOM, no reboot, min 2.6 MB free (before: OOM after 17 s).
- **Kernel module review (independent):** 0 critical, 1 high (T23: lifted AE/ADR/defog works not cancelled before freeing the stats DMA on unload → use-after-free), 6 medium, 9 low, plus optimisations (T23 .bss placeholder arrays cost up to several MB RAM). Fixes in work on claude/review-fixes-20261003.
- **Hardening:** claude/isp-hardening-qbuf-pin: QBUF buffers must lie in the rmem window from the kernel command line (qbuf_guard=0 disables); the sensor module is pinned while the ISP is open (T23 device-tested: rmmod of the sensor while streaming is refused, 6 clean cycles). OpenIMP opens all device nodes with O_CLOEXEC (claude/imp-cloexec), so helper processes no longer hold /dev/isp-m0.
- **thingino ciao (maintained by the timps session):** openimp pinned to the Lu-Fi fork (9eefbae, pushed; T23 OEM helper now opt-in, saves ~328 KiB); open-tx-isp and openimp pins to all-17/all-15 committed locally; timps 110ab65 (silent boot probe for day/night) pushed; boot guard available as an opt-in package (local).
- **Planned:** 03:00: A/B measurement vendor vs open stack (CPU, RAM, rmem, start-up, snapshot latency) on cam-B and cam-D.

- **Feature matrix:** 9 more cells closed: DPC on T10/T20/T21, DRC and defog on T20, scene/colour effects on T10, T41 video memory and stability. Remaining open: CCM/LSC readback on T10/T20 and Iridix on T10 (in work), T23 CCM in daylight, T20 AWB under artificial light (no artificial light at night; daylight A/B equal), T23 long-term hangs.
- **In work tonight:** T23 review fixes (use-after-free on unload, defog allocations, front-crop overflow, ADR/AE locking, RAM placeholders) for the cam-B image; independent review of OpenIMP; CPU profiling and optimisation of OpenIMP on cam-A/cam-D; remaining matrix '?' cells on T31/T21 (audio input only); overnight soak log of all six cameras every 10 min; A/B measurement vendor vs open stack at 03:00.
- **H.265 on SoCs without HEVC hardware:** on T10/T20/T21/T23 (Helix is H.264/JPEG only; Radix only on T30) OpenIMP now fails IMP_Encoder_CreateChn(PT_H265) with -1 and one clear log line, so streamers fall back to H.264 at once. The vendor returns 0 and creates an empty channel that never encodes (beyond vendor, documented). Host-tested, T21 and T23 libimp build; branch claude/h265-reject. T41: AVPU HEVC port from T31 in work (claude/t41-h265).
- **CCM/LSC readback on T10/T20:** CCM/LSC now readable in isp-m0 on T10/T20; T10 CCM is updated every frame like the vendor and follows day/night, T20 mesh mirror follows hflip. T10 Iridix stays bypassed by the jxh42 IQ bank (vendor-identical); enabling it beyond vendor is a pending decision. Daylight per-CT sweep pending [claude/t1x-ccm-lsc-iridix].
- **all-18 content:** open-tx-isp claude/open-tx-isp-all-18 e8ed540c / openimp claude/openimp-all-16 bec17c58, flashed 23:22–23:24 on cam-B, cam-C and cam-E: 30/30 snapshots, 0 oops; cam-B no longer carries the vendor libimp hybrid; cam-A and cam-D are still on all-17 and follow later. Contents: T23 review fixes (unload use-after-free, defog allocations, front-crop overflow, ADR/AE locking, 6 undersized objects resized to the stock size); hardening (QBUF rmem-window check, sensor module pinned while the ISP is open, O_CLOEXEC in OpenIMP); CCM/LSC readback for T10/T20; H.265 rejection on T10/T20/T21/T23.
- **T23 ADR/defog IRQ "dropout" was not a bug:** timps stops the stream when there are no clients; with a permanent client the IRQ ran at 25 runs/s for 10+ min.
- **T41 H.265 in work:** the channel is created, but there are no frames yet, and one test run rebooted the box; a timeout path is being added.
- **OpenIMP review:** 0 critical, 3 high, 8 medium and 9 low issues plus 9 optimisations; fixes are in work.

## Night (2026-10-03, 20:39)

- **all-15 on all five cameras:** cam-A, B, C, D, E flashed 20:20-20:30 with full OTA. Result: 30/30 snapshots, MJPEG and MP4 on both channels, 0 oops. The one exception is cam-D at 26/30, because a concurrent test restarted the streamer; it was not a crash. Aggregates: open-tx-isp-all-15 f3f40f9e, openimp-all-13 07afe9a, timps-all-15 cc8cded.
- **cam-B without the vendor libimp hybrid:** /opt/openimp-t23 is gone, which saves ~328 KiB in the rootfs. Only the T23 hardware JPEG IMP_Decoder still needs the OEM worker; the worker is now optional (T23_BUILD_OEM_WORKER=1, openimp `claude/t23-no-oem-worker` 9eefbae). thingino will pin openimp to the Lu-Fi fork (9eefbae) for all SoCs, after a build check by the thingino maintainer session.
- **Module size (`claude/open-tx-isp-size2` a7214c75):** emulator-identical, device-tested on cam-A and cam-B: T23 1,047 to 622 KB (vendor 857), T31 859 to 711 KB (vendor 829), T20 775 to 736 KB, T10 770 to 731 KB. T23 and T31 are now smaller than the vendor module.
- **T21 `ae_it_max_us` now acts (`claude/t21-ae-it-max` 840a57ff):** the GetExpr hook had been lost when the stock dispatchers were lifted, and the RANGE block of SetIntegrationTime was ignored. The vendor T21 also ignores RANGE, so this is beyond vendor; the user decision on it is pending. cam-D: cap 2000 us gives IT 68 lines and dgain 19 to 63; cap 5000 us gives 172 lines; cap 0 returns to 1125 lines. Caveat: a 4th module reload in the same boot led to segfaults and a watchdog reboot; under investigation.
- **eprc macroblock RC ported (`claude/eprc-mbrc` 9e2bc3a):** 0 deviations against the vendor in the emulator on T23 and T21 (20000 calls + 3x30 random scenes x 150 frames). The vendor uses SAS mode 3 (7 activity-class QP offsets, registers 0x40074/78/7c-84/8c/90), with no per-MB QP map. Opt-in: `OPENIMP_EPRC_MBRC=1`, and `IMP_Encoder_SetMbRC` works per channel at runtime (on the vendor, SetMbRC has no effect and MB-RC always runs). Vendor bug: a class-table index reads past a 9-byte table into the stack; OpenIMP uses 0 there. Device test pending.
- **T20/T10 OEM rate controllers as default (`claude/t1x-oem-rc-default-a13` f05db18):** code done, device test pending. The keys quality_lvl/change_pos act as in the vendor firmware.
- **timps USE_OPENIMP:** build switch pushed (timps a2dccce, thingino ciao c55f73817). It changes nothing yet.
- **Stale cells refreshed (21:08):** T23 AE (lifted vendor AE is the default, night test on cam-B passed), T41 flip (sensor flip registers follow live: hflip 0x022c=0x01, vflip 0x0063=0x02, off 0x00; daylight picture check pending, `claude/t41-matrix-fixes`) and T41 white balance (black-picture incident not reproducible; timps calls no WB function on T41).
- **T20 OEM rate controller default device-tested (`claude/t1x-oem-rc-default-a13`):** cam-C at 1200 kbit/s: CBR 1300 (P2 I-aware budget), VBR 1044, SMART 1019; quality_lvl 0/6 gives 1130/800 kbit/s, change_pos 50/100 gives 850/1210 kbit/s, also live via /control; decode clean, 0 oops.
- **T21 `ae_it_max_us` kept:** beyond vendor, the user decided to keep it. The docs (matrix, beyond-vendor list, SoC differences) are updated accordingly.
- **MB-level RC per picture type (`claude/eprc-mbrc` a8b483a):** registers 0x400c0/0x400c4 are now set per picture type like the vendor (T23 IDR 0x060404c1/0x61615921, P 0x030484c1/0x61615c21; T21 IDR 0x060407c1, P 0x030487c1). Device test is running.
- **T10 boot guard tripped:** a flash reboot happened less than 300 s after a test restart, so the guard counted the load as unstable. The mark was cleared and the camera runs again. Idea: firmware updates should clear the guard's pending mark.

## Late evening (2026-10-03)

- **T41 module reload fixed and verified:** rev2 image on cam-F: 10/10 rmmod/insmod cycles, refcnt 0, 0 oops; kill -9 of the streamer recovers 3/3. Root cause: a decompiled tuning-node helper overwrote .bss. Branch `claude/t41-matrix-fixes`.
- **T41 picture controls and rate control:** brightness 255 gives Y 211, contrast 0 flat grey, saturation 0/255 chroma 0.1/7.1 (dark scene); `isp-m0` in vendor layout (run mode, BCSH, flip mode, anti-flicker, AE); bitrate 400/1200/3000 gives 518/1195/2777 kbit/s over 30 s each (`claude/t41-cbr-overshoot`). Forced day/night switch test pending.
- **T41 open points:** the driver now writes the sensor flip synchronously (ret 0), but timps does not call SetHVFLIP live on T41 (under investigation). u-boot ignores the stored env (fw_env.config size mismatch), so changing rmem needs an env-partition image; user decision pending.
- **T21 vendor-identical eprc is the default:** 0 oracle deviations; cam-D at 1200 kbit/s: CBR 1326, VBR 1096, SMART 1071. Branch `claude/eprc-t21-default`.
- **eprc complete (T21/T23):** FIXQP, scene-cut IDR, runtime RC/fps/GOP/HSkip changes applied at the next IDR like the vendor, `SetChnHSkip` on T21/T23; 0 oracle deviations. MB-level RC is not ported (separate task). Branch `claude/eprc-complete`.
- **T20 frame source:** the snapshot debounce no longer polls the JPEG encoder: with 1 snapshot/s on both channels chn0 14.4 / chn1 15.0 fps (was 11.2 / 14.3). A sub-stream height of 270 is rounded to 272 with a warning (was: scaler hang). Branches OpenIMP `claude/openimp-t20-jpeg-align`, timps `claude/timps-jpeg-idle-nopoll`.
- **T10 noise reduction:** Sinter/Temper strength acts (vendor: no-op): temporal noise 7.11 / 2.91 / 1.51 at temper 0 / 128 / 255, survives day/night. Branch `claude/t10-t20-nr-wdr`.
- **Unsupported keys (timps `claude/timps-unsupported-keys`):** a POST with only keys the SoC cannot apply returns 422 `not_supported_on_soc` with `ok:false`; unsupported keys are no longer persisted (audio `CAP_ALC`/`CAP_SPK` count as not supported without the hardware path). `IMP_ISP_QueryCaps` was prototyped and withdrawn by the maintainer; not part of any release.
- **T23 AE default:** the lifted vendor AE becomes the default after the night test (pending).
- **T31 Allegro CBR and T21 eprc device results (20:13):** T31 (vendor Allegro core, default): CBR 1210 kbit/s at 1200 target and 2973 at 3000 (legacy controller 1511 / 3786, +26 % with large peaks); VBR 1163, CappedVBR 1177, CappedQuality 1174 at 1200; decode clean, 0 oops; no filler NAL written (filler=0 also at 3000). Branch `claude/t31-allegro-cbr`. T21 eprc complete: SMART/CBR/VBR at 1200 kbit/s gave 1090/1305/1042; runtime HSkip N=4 gives an IDR every 4 GOPs; decode clean, 0 oops; a day/night switch does not trigger the scene-cut IDR (vendor condition: scene class 5). Branch `claude/eprc-complete`. T23 eprc-complete device test is pending.

- **Aggregate all-15:** open-tx-isp claude/open-tx-isp-all-15 f3f40f9e, openimp claude/openimp-all-13 07afe9a, timps claude/timps-all-15 cc8cded (all without the withdrawn QueryCaps). New over all-14: smaller libimp and modules (T23/T20 rootfs back to the old layout), T31 CBR on the vendor Allegro core, eprc complete on T21/T23, P5 on the T21 revision, T20 sub-stream height and snapshot frame-rate fixes, timps 422 / unsupported keys not persisted. Flashing on cam-A, B, D, E started 20:2x; cam-C follows.
- **cam-B night test with the lifted vendor AE (default since all-14):** switches to night (exposure 109303 > 4096), AE regulates (IT 1200 of 1436 lines, analog gain 133 of 160, gain reported). The all-11 problem (gain reported as 1x, never night) is gone. One night→day→night flip in the first 90 s after start, the same timps boot-measure issue as on cam-C; a timps fix is in work.
- **timps USE_OPENIMP:** build switch pushed to timps main (a2dccce) and thingino ciao (timps.mk); it changes nothing yet. OpenIMP-only features are enabled under it once device-tested and present on timps main. Two premature changes were reverted: the vendor firmware does honour T21 ae_it_max and T10/T20 quality_lvl/change_pos, so we fix the open stack instead (T21 AE limit fix and T20/T10 OEM rate controller as default are in work).
- **Per-SoC difference list:** new docs/OPENIMP_SOC_DIFFS.md: OpenIMP vs vendor per SoC in both directions, with API signatures and device-test status.

## Evening (2026-10-03)

- **T10 rate control: super-frame fix default**: the super-frame fix (P1) is now the default inside the T10 OEM controller (`OPENIMP_T10_RC=1`; `OPENIMP_T10_RC_SUPERFRM=0` restores vendor-exact behaviour). cam-E at 1200 kbit/s: 450 to 822 kbit/s, re-encodes 800 to 0, CPU 8.3 to 5.5 %. Branch `claude/t10-rc-superfrm`.
- **T20 rate control: I-aware P budget**: P2 is the default for CBR (`OPENIMP_T20_RC_IAWARE=0` = vendor; `=1` forces it for VBR/SMART too). cam-C: CBR 1583 to 1300 kbit/s at 1200 (stats 1244). VBR stays vendor (with P2 it fell to 866). Branch `claude/t20-rc-iaware`.
- **T31 rate control:** the Allegro rate-control core is the default (`OPENIMP_T31_RC_CORE=legacy` restores the old one; CBR stays legacy). Branch `claude/t31-capped-quality`.
- **T23 fixes:** brightness/contrast/saturation/hue now act (they were reset on every stream start); T10/T20 `ae_it_max_us` now limits the AE; T23 sub-stream rotation 90/270 works via the native encoder; T23 `IMP_Encoder_YuvSetCrop` implemented (host-tested only, timps does not call it); T23 contrast/gain feedback: the driver takes the low byte like the vendor and OpenIMP remembers the gain before sending (cam-B: user contrast 100 stays). Branches open-tx-isp `claude/t23-bcsh-aeit-fix`, openimp `claude/t23-yuv-native-aeit`.
- **T21/T31 contrast:** OpenIMP sends the user contrast instead of the default 128 and remembers the gain before sending (commit 6ba6f17). Code done, device test pending.
- **T10 build:** duplicate `isp_printf` export fixed in open-tx-isp (no local patch needed).
- **Boot guard for T20, T23, T31:** `S10isp-guard` + `isp_open=auto` added (local image overlay, ships with the next image, not on the device yet). A load counts as stable after 300 s uptime with timpsd running; otherwise the next boot skips the ISP/sensor modules and timps until `S10isp-guard clear`.
- **T21 smaller and faster:** kernel module 760 to 494 KB (RAM unchanged); the lifted AWB now needs 0.95x of the vendor instructions (was 1.41x), output bit-identical, cam-D isp_fw_process -10 %. Branch `claude/t21-size-awb-opt`.
- **T41 (cam-F):** the bitrate setting had no effect because the OpenIMP T41 controller discarded a negative bucket level; fixed (`claude/t41-cbr-overshoot`), host-simulated, device test pending. In the dark the gc5603 shows strong column noise, so about 10 Mbit/s even at QP 45 (separate ISP issue). The spontaneous reboot is an OOM: rmem=30M leaves 29.6 MB for Linux; 3 parallel streams plus snapshots exhaust it and the watchdog resets. Proposal: rmem about 24 MB (the vendor image uses 19 MB). After an OOM kill the sensor stays registered and AddSensor returns EBUSY until reboot; a driver fix (`claude/t41-sensor-rereg`) crashed on the first device load and is being analysed. The black picture after a WB POST was not reproducible (timps does not call any WB function on T41).
- **T23 matrix gaps (cam-B, `claude/t23-matrix-gaps`, open-tx-isp, device-tested):** front crop now uses the vendor path (960x540 crop OK); the MASK control returns -EINVAL exactly as the vendor does (no stock handler). The lifted vendor AE now honours anti-flicker and reports AE luma, so backlight/highlight/AE comp work with it (backlight 10: luma 68 to 112, highlight 10: 48). Making the lifted AE the default is a pending user decision; as default, AE IT max has no effect, exactly as on the vendor. Anti-flicker device values: vendor AE 50/60/off gives IT 720/900/971, HLIL AE 720/600/711. Not bugs: IR cut/LED auto night (timps auto switches IR cut, ir850 and mono, and back) and the JPEG size (q75 tables are the IJG tables, size matches libjpeg, scene-driven).
- **T41 module reload (cam-F):** a rebuilt tx-isp-t41.ko with the sensor re-registration fix (`claude/t41-sensor-rereg`, not in any aggregate) crashed on insmod twice (rc 139) and the box needed a power cycle. Likely cause: rmmod+insmod of tx_isp_t41 is not safe in general; a control test with the installed module is pending. The T41 kernel has no netconsole/pstore, so an oops cannot be captured after the network dies.
- **T10 module reload (cam-E):** rmmod/insmod of tx_isp_t10 gives 'Failed to get csi clock -22' and a NULL oops in isp_csi_set_clk at stream start; module reload is unsafe on T10, the boot-time load is fine.
- **Noise reduction strength (T10/T20, `claude/t10-t20-nr-wdr` + OpenIMP `claude/t20-nr-strength`):** the vendor firmware renormalises the scaled Sinter/Temper table onto the IQ min/max, so only 0 acts. Now strength acts: T20 device-tested, temper 0/64/128/200 gives 0/42/85/132, sinter 0/17/35/69 at high gain, 128 = IQ, kept across day/night. T10 device test pending (reload oops). Default for T10/T20 is a pending user decision. T31: SDNS (H-S regs 0 to 0, 255 to 15), DPC thresholds and impulses vendor-identical on cam-A; T10 isp-m0 WDR flag fixed.
- **Other results:** T20 daylight A/B of simple AWB vs vendor chain: gains 492/393 vs 488/395, neutral ROIs within 0.007, both converge in under 4 s, tungsten test open. T31 anti-flicker with a 22 ms IT cap: IT 1000/900/750 lines for off/50/60 Hz, gain compensates, daylight test pending.
- **T23 dynamic ADR and defog lifted (cam-B, `claude/t23-adr-defog`, 17:50):** lifted from the vendor module including `tisp_defog_soft_process`; 44/44 emulator cases are identical. The core ISR now dispatches the ADR/defog IRQ callbacks (ADR was static before). Device: DRC 255 gives meanY 158 vs 119, DRC 0 gives 114; defog 255 and day/night switching work, 0 oops. Module parameter `source_adr_oem=1` is the default, `0` selects the old static path.
- **T23 module size (cam-B, `claude/t23-ko-size`, 17:50):** 1,282,492 to 1,071,956 B stripped (tparams zero tail moved to .bss, `-mno-pdr`); stream OK on cam-B. The ADR lift adds ~138 KB; with both branches merged the module is ~1.21 MB.
- **T10 module reload (cam-E, `claude/t10-reload-safe`):** 5 rmmod/insmod cycles while streaming, 0 oops. The earlier 'csi clock -22' oops came from a module built against the T20 kernel tree; the T10 build now refuses that with #error.
- **T41 module reload (cam-F, `claude/t41-reload-safe`):** cause found statically. tx_isp_fs_remove freed the channel array while the framechan0..2 misc devices were still registered, so the next insmod oopses in misc_register. Four static work items were also not drained on unload. The fix is not yet device-tested. Testing needs the box booted without the old module (boot guard isp_open=manual), because the old module's unload leaves the bug behind.
- **User decisions (2026-10-03):** (a) T23 default AE becomes the lifted vendor AE (vendor default), after a night-switch test in the dark that is still pending. (b) T20/T10 Sinter/Temper strength acts by default: 128 = the IQ table, so the default picture is identical to the vendor; other values act, which goes beyond the vendor.
- **All-14 aggregates flashed (19:20):** open-tx-isp `claude/open-tx-isp-all-14` (de10fed6), OpenIMP `claude/openimp-all-12` (787d534), timps `claude/timps-all-14` (9490547). Flashed 2026-10-03 ~19:07 on cam-A, cam-C, cam-D and cam-E (full OTA). cam-A, cam-D, cam-E: 30/30 snapshots, MJPEG, MP4 on both channels, 0 oops. cam-B waits for a smaller rootfs; cam-F stays on -all-13 (image rev 1).
- **Image size fixes:** the rootfs of T23/T20 had grown past 0x4E0000. `claude/openimp-size` (2040a03, gc-sections) and `claude/open-tx-isp-size` (15232deb, strip local symbols): T23 libimp 774 to 726 KB, T20 libimp 694 to 594 KB, T23 module 1,211 to 1,047 KB, T20 module 819 to 775 KB; rootfs back to 0x4DE000 (T23) / 0x4DD000 (T20). Further driver shrinking is in work.
- **T31 CBR on the vendor Allegro core:** `claude/t31-allegro-cbr`: CBR now uses the ported vendor core too (20 trace files + 72 x 400 random frames state-identical). Deviation: no filler NAL is written (the HRD model counts filler bits like the vendor, but a static scene's CBR stream stays below target where the vendor pads); the filler value is per picture like the vendor. Device test pending.
- **eprc QP-down limit (P5) in the T21 vendor revision:** `claude/eprc-t21-qp-limit`, opt-in (`OPENIMP_EPRC_QP_DOWN1/2`). Device test pending.
- **T41 module reload: root cause:** a decompiled tuning-node helper used the 4-byte module parameter `ivdc_threshold_line` as a struct cdev and overwrote about 60 B of .bss including `tx_isp_bringup_level`, so `tx_isp_exit()` bailed out early and left platform drivers, misc devices, IRQs and kthreads registered. Fixed in `claude/t41-matrix-fixes` (b4ef2cf8), device test pending. T41 image rev 1 (flashed): isp-m0 now in vendor layout; reload still failed with rev 1.
- **Feature matrix: ? cells filled:** results of the evening matrix tests (audio input only, no sound played): T31 AWB presets and IR cut/IR LED device-tested; T31 CCM/LSC follow day/night and flip; T20/T10 ISP state not observable; defog/DPC on T10/T20/T21 have no control path (keys accepted but ignored); scaler tested on T10/T20/T21; T20 snapshots on both channels cost video frames; audio input device-tested on T10/T20/T21/T31.
- **Feature detection:** the static caps matrix stays (agreed with the timps session); `IMP_ISP_QueryCaps` was withdrawn by the maintainer.

## Late afternoon (2026-10-03)

- **T31 rate control: vendor core ported:** the Allegro VBR/CappedVBR/CappedQuality controller is ported instruction by instruction and matches the vendor code frame by frame in an emulator (2,100 trace frames + 72 random traces, 0 differences). cam-A at 1200 kbit/s: VBR 1163, CappedVBR 1177, CappedQuality 1174 kbit/s (old controller: 732 / – / 2030). Becomes the default; `OPENIMP_T31_RC_CORE=legacy` restores the old one; CBR stays on the old controller for now.
- **T21 rate control: vendor-identical:** the T21 vendor controller (an older eprc revision) is ported; 0 differences in 873 oracle frames and 360 random scenarios. cam-D: CBR 1326, VBR 1096, SMART 1071 kbit/s at 1200. Opt-in via `OPENIMP_T21_EPRC=1`; a bug found on the way also affected T23 (CBR with very short GOPs).
- **T20/T10 rate control:** kernel patch 0101 lets the vendor T20 controller read its statistics registers (the hardened kernel denied it). cam-C re-flashed: CBR 1435 (+19 %, the vendor controller itself overshoots), VBR 1329, SMART 983 kbit/s — SMART was treated as VBR before, fixed. Opt-in via `OPENIMP_T20_RC=1` / `OPENIMP_T10_RC=1`.
- **Better than the vendor: rate-control study:** an offline simulation of all ported controllers found six improvements. Device-tested on cam-E: the T10 VBR super-frame fix stops the vendor's double encoding of nearly every frame (re-encodes 800 → 0, CPU 8.3 → 5.5 %) and becomes the default inside the T10 controller. Being built: an I-frame-aware P budget against the T20 overshoot (opt-in). The eprc QP-step limit showed no measurable effect in a day scene and stays opt-in.
- **Fixes from the feature-matrix tests:** T23 brightness/contrast/saturation/hue now act (they were reset on every stream start); T10/T20 max integration time now limits the AE (cam-C 300 µs → 10 lines); T23 sub-stream rotation 90°/270° works with the native encoder (the vendor helper process is not needed). 56 of 105 open matrix cells tested.
- **Open:** T41 (cam-F): bitrate setting has no effect (~8.2 Mbit/s), one unexplained reboot, white-balance POST once gave a black picture — in work.

## Afternoon (2026-10-03)

- **All six cameras on open-tx-isp-all-13 / openimp-all-11:** flashed 14:04–14:20: 30/30 snapshots, MJPEG and MP4, 0 oops; reference-buffer sharing active on T21/T23. cam-F (T41) now boots our driver and OpenIMP from flash (kernel and rootfs flashed separately, the full image does not fit RAM for OTA).
- **Reference-buffer sharing on by default for T21/T23:** the artefacts came from a missing wrap byte in the ring register; with it the picture is clean. Saves ~1.4 MB video memory; `OPENIMP_REF_SHARE=0` turns it off.
- **Rate control:** cam-A T31: plain VBR now closed loop (1514 kbit/s at 1500 target, before 280). cam-B T23: vendor eprc controller with the vendor's CreateChn clamps — SMART 1141, CBR 1253, VBR 1255 kbit/s at 1200, decode clean (old mapping: 3203). cam-D T21: eprc approximation behind `OPENIMP_T21_EPRC=1` (CBR 1338 vs 570 with the old controller); a vendor-identical T21 port is in work because the T21 vendor controller is an older revision. T20/T10: vendor controllers ported, bit-exact in the emulator (200×150 frames); on cam-C CBR overshoots 25 % because the hardened kernel denies one statistics register read — kernel allowlist fix in work, default off until then.
- **T31 CappedQuality decoded:** differs from CappedVBR in two places: it keeps improving quality while at max bitrate and it never falls into the emergency max-QP after a scene change. A full port of the vendor rate-control core is in work, selectable and verified frame by frame in an emulator.
- **Scene mode and colour effects on T23 and T31:** driver and OpenIMP support; device-tested on cam-B and cam-A: B/W, vivid, negative visible, invalid values rejected, 0 oops. timps gets `image.colorfx`/`image.scene` plus live fps/GOP (built, device test with next images).
- **Docs for streamer authors:** new `docs/OPENIMP_BEYOND_VENDOR.md` lists everything where OpenIMP behaves beyond or differently from the vendor libimp, with env switches and how to integrate or disable it. Rule: only device-tested features go in.
- **Branch cleanup:** forks pruned after a bundle backup: open-tx-isp 56 → 6 branches, openimp 13 → 10; the 50 old non-claude branches were unchanged copies of upstream. Test-report branches moved to `docs/test-reports/`.

## Late morning (2026-10-03)

- **T23 daylight green cast — found and fixed** (`claude/t23-day-color`, device-tested on cam-B, not flashed yet): every on-demand snapshot restarts the stream, and our driver reset the white-balance gains to 1× on every stream start, so the snapshot was taken before AWB had re-converged. The vendor keeps the AWB state across stream restarts; now we do too. A side-by-side run of the original vendor stack in the same sunlit scene gave neutral colours and confirmed the cause was ours; the register comparison also corrected three stream-start values (top 0x1c, GIB 0x1008/0x1010). The HLIL AE now reaches correct exposure ~4 s after a driver reload (was ~2 min).
- **Vendor T23 uses reference-buffer sharing by default:** measured on cam-B with the vendor stack (ring bit set, luma ring = picture + 256 lines); the vendor libimp forces it on for ≤1080p. A register capture is being used to align our port (`claude/t23-ref-ring`); the T21 opt-in stays off meanwhile.
- **Rate control** (`claude/rc-modes`): T31 CappedVBR/CappedQuality now run the closed-loop regulator with the vendor's PSNR cap (42 dB); T20/T21 report the vendor-clamped RC values. In work: T31 plain VBR closed loop by default and vendor defaults, T23 live readback, which RC writes take effect on T10/T20/T21, and vendor-equal SMART on T23 (eprc controller + long-term background reference).
- **AVPU kernel module review** (outside review, verified): fixes for a minor-number leak, a use-after-free on sysfs unbind, an uninitialised list mutex and the flush range (`claude/avpu-review-fixes`); kernel patch 0100 makes the rmem flush ioctl reject invalid directions instead of crashing. cam-A: reload, 3× kill -9, 5× rmmod/insmod, 0 oops.
- **New T41 test camera (cam-F):** OpenIMP runs against the vendor T41 driver with video, JPEG, OSD and motion detection (`claude/t41-libimp`): a kernel oops from the cache flush was fixed (the T41 kernel expects a physical address), motion detection gets frames without a viewer. timps CPU ~10–12 % vs ~27 % with the vendor libimp. Our T41 driver needs an image for testing (vendor module oopses on unload); image being prepared.
- **timps:** the OSD clock in the first snapshot after an idle period was stale (minutes to hours) — fixed (redraw on idle→active), comes with the next images; T23 access-unit limit 2 MiB + 64 KiB.
- **Feature matrix** (English) now in `docs/FEATURE_MATRIX.md` / `docs/feature-matrix.html`.

## Morning (2026-10-03)

Done and device-tested, waiting for the next aggregate (-all-13):
- **T31 privacy mask** (`claude/t31-privacy-mask`): the ISP mask block is now implemented like the vendor driver (4 rectangles per channel, YUV fill, follows mirror/flip). Emulator: 400/400 random sequences register-identical to the vendor module. cam-A: black and red rectangles at the right place and colour, get/clear OK. OpenIMP converts RGB mask colours to YUV like the vendor libimp (`claude/t31-mask-rgb2yuv`).
- **T21 tuning controls** (`claude/t21-tuning-controls`, beyond vendor — the vendor kernel ignores them): scene is stored, colour effects black-and-white / vivid / negative work, Sinter and Temper denoise strength act on the hardware, getters return what was set; defaults stay vendor-identical. timps' `sinter_strength` now works on T21 (`claude/t21-sinter-strength`, 128 = vendor picture). cam-D: effects and denoise visible, 0 oops.
- **T21 debug parameters removed** (`claude/t21-drop-debug`): module 13.8 KB smaller.
- **T20/T21 encoder error limit** (`claude/helix-error-limit`): after 3 failed pictures the encoder is re-created, after 2 fruitless re-creates the channel stops instead of waiting 20 s per picture. cam-C and cam-D soak OK.
- **T23 RC defaults** (`claude/t23-rc-app-defaults`): app value 0 for QP step / static time / change position now means the vendor default (3/15/2/80) instead of "off", taken from the vendor libimp 1.3.0.
- **cam-A sensor driver:** vertical flip no longer reports a false error (local thingino patch).
- **timps:** T23 access-unit limit raised to 2 MiB + 64 KiB to match the encoder window, so large night IDRs are no longer dropped (other session, after review).

Investigated, not adopted:
- **Reference buffer sharing** (vendor BUF_SHARE_CFG): only the T21/T23 Helix hardware has the ring mode. On cam-D it saved ~1.4 MB video memory but produced magenta/green reference artefacts in the first seconds; stays off while the cause is analysed.
- **timps flip reset on client connect:** not a bug — timps re-applies the live config value; the test had written the register behind timps' back.

In work:
- **T23 by day:** with the lifted vendor AE exposure is right at once and AE compensation/highlight work, but the picture is green; with the HLIL AE the first ~2 minutes after a driver reload are overexposed. Cause under analysis (CCM bypassed since it follows the IQ bank). The lifted AE becomes default only after this is fixed.
- **New T41 test camera** (vendor stack): build fixes for the T41 driver and an OpenIMP T41 build are being prepared.

## Early morning (2026-10-03)

- **Aggregates -all-10 / openimp-all-9 and -all-11 / openimp-all-10** flashed on all five cameras (incl. cam-A, which moved up from -all-5). Checks on every camera: 30/30 valid snapshots on both channels, MJPEG and both MP4 streams, 0 oops, 0 encoder errors. -all-11 was flashed staged (cam-D and cam-C first) because it carries a new kernel.
- **Vendor helper libraries gone:** libimp now contains the two logging functions it used from libalog; libalog/libsysutils are no longer built into the images.
- **Kernel soc_vpu (patch 0099):** requesting a busy VPU sleeps instead of busy-waiting up to 200 ms; on T20/T10 an encoder error interrupt now ends the wait immediately (with reset) instead of running into the timeout; per-instance bitstream counter on Helix.
- **T23 motion detection fixed:** after the last sub-stream viewer left, capture buffers stayed parked and motion detection got no frames (empty motion grid in the web UI). Frames are now recycled for callback-backed pools too.
- **T23 memory:** JPEG shares the H.264 bitstream area like the vendor pool (1.44 MB less video memory, main window back to 2 MiB).
- **T23 image pipeline:** DPC and CCM follow the IQ bank flags like the vendor (less night noise); a block whose parameters fail to load on a day/night switch is bypassed instead of running with the other bank's values; user bypass bits survive day/night switches.
- **T23 vendor AE (lifted, source_ae_oem=1):** first picture after a stream restart no longer black, day/night refresh and gain limits wired like the vendor, AE compensation works at night. Default briefly switched to it in -all-11, but the driver then reported a constant 1× gain, so timps never switched cam-B to night — reverted in -all-11b (HLIL AE default). Follow-up branch `claude/t23-ae-oem-export` exports the lifted AE's live gain/EV exactly like the vendor getters, feeds AWB/CCM/BCSH/ADR/Defog with the real EV, and repairs ADR/Defog state that the reconstruction had mapped onto unrelated memory (an event table, a CLM LUT word, a module parameter). Verified on cam-B at night in both AE modes; default stays HLIL until a daylight test.
- **T21:** colour-temperature updates only when CT moves by more than 50 K (emulator: 62 % fewer register writes, same final state); module reload on cam-D OK.
- **T20/T30 memory:** JPEG bitstream buffer 1 MiB instead of a frame-sized buffer (cam-E −328 KiB; more on 1080p). Allocating encoder buffers at channel creation was measured to raise the peak and stays opt-in.
- **Encoder diagnostics:** one log line with the effective rate control per channel; out-of-range QP/fps values are clamped with a warning instead of silently replaced.
- **JPEG robustness:** if the encoder is busy or video memory is short, the last JPEG is delivered again instead of blocking; one startup warning when pools plus fixed buffers exceed video memory.
- **T23 reconstruction audit (-all-12):** a call-graph audit of all mis-resolved memory accesses in the decompiled T23 code found none on the default path; every reachable one (ops-table ISR and IVDC ISR counters, sensor release list walk, CCM state, an AE histogram stack overflow, AF parameter copies, MDNS/DPC/mask state) now targets the vendor's variables. cam-B: 3 module reload cycles and 90 snapshots, 0 oops; flashed 05:54.
- **In work:** T20/T21 encoder error limit (re-create after 3 failures, stop after 2 re-creates) waits for a device test.

## Night (2026-10-03)

- **T10 picture drifting diagonally fixed** (`claude/t10-drift-fix`, OpenIMP): the T10 encoder added the 16-pixel reference border twice, once for motion prediction and once for the deblocker output, which adds the border itself. Every P frame was predicted from a reference shifted by 16×16 pixels, accumulating until the next I frame (16 px after 1 frame, 160 px after 10). Now the border is added only for the prediction read, like the vendor command lists show. The overrun past the reference planes measured the evening before was the same bug. cam-E: shift 0 on both streams, 0 decode errors, flashed 01:50. Day/night switch on T10 not yet tested.
- **T21 white balance at dusk** (`claude/t21-awb-hyst`, kernel): hysteresis band (default 10 %) on the three AWB brightness thresholds, so the parameter set and the low-light register stop toggling at dusk (emulator: 39 switches → 0 in 40 frames). While in night mode AWB is frozen and the day gains are restored after night→day, so the first day picture no longer starts orange from IR light. Both settings at 0 give the vendor behaviour (10/10 scenes identical). cam-D: night checks OK, 0 oops; dusk itself still to test.
- **timps** (other session): an externally changed day/night mode is adopted after 20 s instead of a permanent desync warning; WB mode 0..9; firmware hides custom WB on T10/T20/T30, highlights slider 0..10. Simulation only, device test pending.
- **Remaining Ingenic libraries:** `libalog.so`/`libsysutils.so` on the images were already the open `ingenic-system-libs-neo` rebuilds, linked only by timps. Nothing from libsysutils is used; OpenIMP needed two logging symbols from libalog, now built into libimp (`claude/open-sysutils`). cam-C image without both libraries built and tested by bind mount (snapshot/MJPEG OK); not flashed.
- **T10 sub-stream picture wrong — fixed** (found via the web preview): channel 1 (640×360) showed the left half of the main picture at 1:1, vertically scaled — in both H.264 and MJPEG. libimp ruled out. Cause in the lifted firmware shared by T20/T10: `_update_ds()` computed the horizontal downscaler ratio as output width / output width (always 1.0) instead of input / output (`claude/t10-ch1-scaler`). cam-E: ratio 2.0, full scene on channel 1, H.264 error-free; flashed 02:31. cam-C (T20) tested by module reload: no regression (its sub-stream uses the other scaler).
- **Independent review** of the night's seven branches: no blocking bugs. Follow-ups done (`helix-emc-size-2`, `t21-awb-hyst-2`, `open-sysutils-2`, two doc branches): T21 encoder scratch layout no longer applies to T20/T10 builds; T21 AWB day-gain restore now effective (the first AWB frame used to overwrite it) and manual WB works at night (cam-D confirmed); logging fixes with syslog opt-in (`OPENIMP_LOG_SYSLOG=1`). Originally: T21 encoder scratch layout must not silently apply to T20/T10 builds (only measured on T21); T21 AWB day-gain restore made effective and manual WB allowed at night; logging fixes (empty-buffer read, one prototype, syslog opt-in); stale docs.
- **OSD flush of edge rows only** (outside contribution): not adopted. The kernel already bounds a large flush to one whole-L1/L2 index pass (`sc-jz.c`), while the change raised rmem ioctls per frame from 4 to thousands for a full-frame rectangle. Only a comment explaining the band flush was taken (`claude/osd-flush-band-note`).
- **In work:** T23 JPEG shares the H.264 bitstream buffer, DPC/CCM follow the IQ bank, block bypass on load failure, vendor AE as T23 default, T23 motion grid empty in the web UI; replacing the remaining Ingenic libraries (libalog, libsysutils).

## In progress after the -all-7/-all-6 aggregates (2026-10-02 afternoon)

Committed on single branches, tested as stated, **not yet in an aggregate** (next: -all-8 after the open items below).

Kernel (open-tx-isp):
- **T23 failed stream start and reload** (`claude/t23-iq-fail`): a reload test crashed cam-B in cycle 5: statistics DMA kept writing into freed buffers, corrupted the IQ file (CRC error -77), the ISP core refused to start but the scaler started anyway, then oopses. Now the core stats DMA is stopped on a refused start and at module exit, and STREAMON fails cleanly. cam-B: 10 reloads with kill -9, missing-IQ-file test (start refused, scaler not started), 0 oops.
- **T23 sharpen block** (`claude/t23-sharpen-modeflags`): the sharpen parameters were never loaded (all 49 arrays read from offset 0, a latent NULL read); the block actually ran on reset values. Now loaded from the active IQ bank in vendor layout, refreshed with gain and on day/night switch; bypass bit follows the bank like vendor. cam-B: registers exactly as predicted from the IQ file.
- **T23 overexposure after stream restarts** (`claude/t23-ae-minit`): every stream start reset the exposure to the longest step; AE needed ~4 s to come back, so on-demand snapshots in sun were blown out (64 % white). The vendor keeps the exposure across stream restarts; now the open driver does too (luma at target from the first frame). **Open:** cam-B hung hard twice ~45 s after loading this build (no oops, watchdog reboot); bisecting sharpen vs AE change.
- **T21 white balance lifted from vendor** (`claude/t21-awb-lift`): AWB, CT detection, CT-driven CCM/LSC now vendor code (emulator: 10/10 scenes register-identical). cam-D: day colours match vendor (R/G 1.04 vs 1.05, B/G 0.93 vs 0.92), night mono fine, 0 oops. **Open:** first snapshot after start delayed (503 in 7/10 cycles at 25 s) and an "event free empty" burst at start.
- **T31 tuning controls** (`claude/t31-tuning-stubs`): black level read-back, colour matrix presets 0–4 + user matrix, front crop get/set, scaler level now real (were stubs). Review found and fixed: crop read-back returned only the low byte, crop check used swapped axes, a colour-matrix register written with swapped bytes (wrong for limited-range presets). Boot image unchanged. Device test pending (cam-A).

OpenIMP:
- **Hardware JPEG on Helix without vendor library** (`claude/helix-jpeg`, T20/T21/T23): ~95 % less CPU for snapshots. cam-C passed; cam-D 10/10 (37 ms/job); cam-B 10/10 (29 ms/job). Fixed during testing: per-job memory exhaustion (buffer now allocated once per channel), truncated JPEGs could be served on T23 (now detected via the vendor's ACT_BS bit; retried with coarser tables, then quality lowered by 5 like the vendor, slowly recovered). Software fallback optional at build time (size).
- **Dedicated JPEG channel got no frames** (same branch): timps' snapshot channel has its own frame source, OpenIMP only fed JPEG from video channels. timps then restarted the frame source every few seconds (exposure reset, snapshots 1.5–9 s). Fixed for T20/T21/T23/T30: cam-D snapshots 0.05–0.18 s, no restarts. Affects all earlier OpenIMP images.
- **OSD lines, rectangles, bitmaps** (`claude/osd-line-rect`, T31/T20/T21/T30): drawn like the vendor (were ignored). Review fixed a use-after-free on bitmap data and a cache hazard. cam-C: all shapes correct on both streams, clipping at the frame edge, 0 oops.
- **T23 rate-control parameters** (`claude/t23-enc-rc-params`): quality level, change point, static time, QP steps, I-frame bias and SMART now reach the encoder at channel creation (were hard-coded) — in the vendor worker path and in the native encoder. Device test pending.
- **Robustness audit, parts 2 and 3** (`claude/oimp-robust-2`, `claude/oimp-robust-3`): harmless DQBUF/EPIPE races at channel stop now quiet; atomic worker flags; AEC reference queue heap overflow; audio-effect switch during capture (use-after-free); double stream release in the audio codec; HPF overflow; spin lock without yield on a single core. cam-C: 5 restarts, fd count constant, 0 errors.


Evening additions (all single branches now pushed, still not aggregated):
- **T21 rmem on main↔sub switching** (`claude/rmem-keep`, on helix-jpeg): our T21 build needed ~27.4 MB rmem for main+sub+JPEG (23 MB available) → WebRTC main↔sub switch failed and the camera restarted. Now one shared bitstream buffer sized exactly like the vendor's vpuBs (2,073,600 B), encoder buffers per picture size and allocated at CreateChn, long-lived buffers at the top. cam-D: free rmem with main+sub 0.5 → 1.56 MB (vendor ≈1.2 MB), 32+40 switch cycles OK. T21 ignores the JPEG size-limit register → JPEG now encoded in stripes with restart markers so it can never overrun; the core also drops the last partial 128-byte burst of every job (vendor too) — compensated.
- **Dedicated JPEG/MJPEG channel on T31** fixed too (cam-A MJPEG 24 frames/5 s, was 0 bytes).
- **T21 white balance**: event callbacks run with IRQs off like the vendor (pool overflow at stream start gone; worst IRQs-off 1.05 ms, like vendor); review fixes incl. a real lifted-code bug (`fix_point_mult3_64` returned a·b·b instead of a·b·c in ae_tune2) (`claude/t21-review-fixes`); module RAM 688 → 638 KB (vendor 616) (`claude/t21-mem`).
- **T23**: gain index for all gain-driven blocks was linear instead of log2 (denoise/sharpen far too strong from 2× gain) (`claude/t23-gain-index`); vendor AE0 chain lifted and emulator-identical in 6 scenes incl. 50 Hz flicker, behind `source_ae_oem` (default off) (`claude/t23-ae-lift`); AE resume by EV (`claude/t23-ae-resume-ev`); frame-path fixes (`claude/t23-hang-debug` 903b9e18).
- **T23 hard hang** (3× in afternoon sun, silent, watchdog reboot): not reproduced in ~3 h of evening stress; kernel soc_vpu/helix defects found and patched for a future image (not yet built); an encoder that stops producing frames at very large frames (QP 10) was found and is being examined.
- **T20 white balance**: presets/manual never applied (T20 uses OpenIMP's simple AWB; recovered firmware had several decompilation errors) — fixed, presets in the correct direction on cam-C (`claude/t20-wb-presets`); work on the vendor AWB chain continues (`claude/t20-oem-awb`).
- **OpenIMP robustness 2+3, ISP gaps (scene mode, colour effects, T21 DRC/DNS), ISP probe tool, T20 log flood silenced**: pushed by a second session.
- **T10L** (report from a Thingino maintainer): day/night panic, EFE job never completes, 8 MiB probe pool — under analysis.


Late evening (aggregates built, more fixes on single branches):
- **All five test cameras now run the open stack.** cam-B (T23) is fully open: native H.264 encoder is the default (`claude/t23-native-default`), the image no longer contains the OEM helixd worker or the vendor libimp (rootfs 324 KB smaller). cam-E (T10) booted the open stack for the first time (driver/t10, OpenIMP T20 build with runtime T10 detection, current timps/WebUI, boot guard). Both: MJPEG 25 frames/5 s, snapshots OK, no oops.
- **T20 fixes** (`claude/t20-flip-sharpness`, not yet in an image): vertical flip computed the UV start from the 16-aligned height — the DMA wrote 12 chroma lines past the end of the frame buffer and the top rows got no colour (pink/green band); sharpness never applied in the default path because the firmware worker is parked — now updated in the compact AE loop (edge energy 13/230/700 for 0/128/255, was flat).
- **T23 image controls** (`claude/t23-image-controls`): contrast only acts in the day bank (the sc2336 night bank disables the contrast curve, same on the vendor stack); AE compensation works; backlight/highlights and AE compensation fixed for the lifted vendor AE; WDR needs the dynamic ADR port (open).
- **T10 integrated into the aggregates** (`open-tx-isp-all-9` fa7ac42b, `openimp-all-8` 9a2e33d2): merging uncovered a real bug — the T10 NVPU writes 21 KB (luma) / 10 KB (chroma) past each padded reference plane on every picture, which in the aggregate's top-down layout hit the bitstream window and made the stream undecodable; reference planes are now sized for it. `isp_printf` is exported only in the T10 module build. cam-E: 60 s / 1501 frames error-free, MJPEG, 3 day/night switches without oops. An open-stack image for cam-E is being built.
- **cam-B to become fully open:** native H.264 encoder as T23 default and an image without the OEM helixd worker / vendor libimp are being prepared (until now the T23 default still used the OEM worker).
- **New user reports being worked on:** T23 contrast, AE compensation, WDR and backlight without visible effect; T20 vertical flip gives pink stripes; T20 sharpness without effect.
- **cam-D memory:** T21 H.264 EMC scratch sized exactly like the vendor (1080p 996 KiB instead of 2 MiB; vendor offsets reproduced; only one sub-buffer is written by the hardware, measured on the device). Free rmem with main+sub+MJPEG 1.56 → 2.76 MB (vendor ≈1.2 MB). FIXQP IDR pictures now at QP−3 like the vendor (`claude/helix-emc-size`, not yet in an aggregate).
- **-all-9 / openimp-all-8 flashed** on cam-B, cam-C, cam-D (00:35): `open-tx-isp-all-9` (T23 IRQ_NONE, T20 vendor-AWB chain behind a switch, T10 fixes, T23 vendor AE behind a switch) and `openimp-all-8` (MJPEG fix, T23 overflow v2 + optional hard limit, review nits), plus kernel soc_vpu patches 0095–0098. All up, no oops, MJPEG 25 frames/5 s, snapshots OK; flashed without the usual pre-reboot thanks to a fixed OTA script. T10 encoder support (`t10-cpuid`) is being merged into the aggregate and re-tested on cam-E.
- **After midnight:** MJPEG regression fixed (`claude/oimp-jpeg-src-fix`: the fan-out decision is per poll again but only waits for a video channel that is actually receiving; cam-C 25 frames/5 s with and without a video consumer). T23 IRQ handlers return IRQ_NONE when nothing is pending, unused IVDC IRQ stays off (`claude/t23-irq-none`; cam-B clean). Kernel soc_vpu hardening patch 0098 (bounded waits, user-pointer validation, register ioctl restricted to the VPU window, per-file channel release) plus an optional hard bitstream limit for the T23 native encoder (`claude/t23-bsf-limit`, only active with the patched kernel). Small OpenIMP review fixes (`claude/oimp-nits`, written by another model, reviewed). Night device tests on cam-B/cam-D: restarts and channel cycling clean; T23 native rate-control parameters take effect; T21 tuning getters return defaults (being checked). 98 merged local worktrees removed. Next aggregates `open-tx-isp-all-9` / `openimp-all-8` and images for all four cameras are being built (not flashed).
- **Status of the beyond-vendor improvement ideas** (maintainer decides each one): 16 implemented or approved, 2 rejected, 42 open. Approved tonight and in work: hardening of the kernel soc_vpu driver (bounded waits, user-pointer validation, register-ioctl restricted), a hard T23 bitstream limit via the BSF interrupt (needs the patched kernel), and T23 IRQ handlers returning IRQ_NONE when nothing is pending.
- **-all-8 images flashed** on cam-B, cam-C, cam-D (23:42; cam-A pending): all up, no oops. Known regression found right after: the dedicated MJPEG/JPEG channel delivers no frames again (merge interaction in openimp-all-7), fix in progress. T23 overflow fix v2 (2 MiB window, scratch between bitstream and references) tested on cam-B: 0 decode errors, forced overflows handled without reference damage. T23 vendor AE (default off) tested at night: picture at target brightness where the substitute stayed black.
- **New aggregates pushed:** `claude/open-tx-isp-all-8` (bfdb0e3e) and `claude/openimp-all-7` (cb85922d) — everything from today except the T23 debug commit, the default-off T23 vendor-AE lift and work in progress. All modules/libimps build without new warnings; all host tests green. 24 merged single branches deleted.
- **cam-D WebRTC main↔sub switching** confirmed working by the user with the rmem fix.
- **T23 native H.264 bitstream overflow** (`claude/t23-enc-overflow`, local): a frame larger than the 1 MiB window was retried at the same QP until the channel stopped (timps then restarted the camera). Now dropped + QP raised (+4, decaying), IDR if the reference was damaged. Device finding: the core ignores the window and keeps writing up to 1.79 MB, overwriting the reference buffer behind it — a serious candidate for the afternoon hangs (sun → ~1 MB IDRs). The vendor only truncates the length.
- **T10L** (report by a Thingino maintainer, now with a T10 test camera): day/night panic root cause found — our reconstruction of `wdr_mode()` zeroed the general FSM manager pointer (only hit when ispmem leaves room for WDR, i.e. on T10); the T10 encoder needs its own command list (captured from the vendor encoder, word-for-word host test) selected at run time; 8 MiB MMAP pool made optional. cam-E (T10): 720p H.264 25 fps error-free, 10 day/night switches without oops. Branches `claude/t10-fixes`, `claude/t10-efe` pushed.
- **T20 vendor AWB chain** (`claude/t20-oem-awb`, behind `t20_simple_awb=0`, default unchanged): several decompilation errors fixed (mesh never called, wrong offsets, NR event FSM broken, firmware worker parked after the first pass); runs stable on cam-C, white-paper check in daylight pending.
- **cam-B colours with IR/white LED**: not a driver bug — white balance was stored as manual in the streamer config (left over from the old WB-mode clamp).

## Independent review and fixes (2026-10-02)

An independent code review (no critical findings) led to these fixes, now in the -all-6 aggregates:

- Kernel: deadlock between sensor unload and reading `/proc/jz/sensor/*` (shared sinfo code); orphan sensor slots no longer point at unloaded modules; T21 open/release counted every open as the first (same bug in the vendor driver) — now counted and serialised; T23/T31 last-close races with foreign frame-channel users; dead global tuning buffer removed (T31); T20 refuses to release the active sensor; T21 error paths; T23 LSC flip locked, small leak fixed; unreachable decompiled T23 setters disabled. Tested on cam-A (T31), cam-D (T21) and cam-C (T20): foreign open/close while streaming, proc reads during sensor unload, repeated reloads. The T20 test exposed one more case: the T20 sensor module unloads without unregistering, leaving a dangling driver pointer that `/proc/jz/sensor/*/name` read after unload (oops). Fixed by a module notifier that clears slots of any module being unloaded (`claude/open-tx-isp-all-7`); retest on cam-C passed (10 sensor reloads during proc reads, 0 oops).
- OpenIMP: T23 native reconfigure uses a parameter snapshot (no divide-by-zero race); T31 lambda tables are generated from a formula instead of being copied from the vendor binary (output bit-identical, 12 documented ±1 entries); top-level NOTICE incl. WebRTC AECM (BSD-3) and x264-derived H.264 code (GPL-2.0+); committed test binary removed; width alignment check; level recomputed on bitrate change; rotation state published atomically; T23 AEC uses the driver's reference offset.
- timps (separate session): no OSD clamp on rotated streams under OpenIMP (tested on cam-A); motion detection uses the sub stream by default (walk test on cam-A, no false alarms; on a T31 with vendor libimp about 85 % less IVS CPU).

## Branch consolidation

Current aggregates: `claude/open-tx-isp-all-7` (all-6 + sinfo module-notifier fix) and `claude/openimp-all-6`. All single branches and older aggregates contained in them were deleted (2026-10-02: 58 + 3 + 4 branches). Kept: docs and test-result branches, plus three old unmerged branches pending a decision (`t31-isp-lifecycle`, `t31-isp-perf`, OpenIMP `t31-series`).

## timps and thingino
- timps: AE IT max can be reset to 0 again (PR #3, merged).
- thingino: per-camera pins for both packages; the open-stack switch (`THINGINO_ISP_OPEN`) set per device so OpenIMP replaces the vendor library; T23 keeps the vendor library only under `/opt/openimp-t23` for helixd.
- T21 boot guard `S10isp-guard` with u-boot `isp_open=manual|auto|off`, so a bad driver cannot boot-loop the camera.
- T21 image: sensor `shvflip=1`, TLS and WebRTC enabled.

## Evidence

| Check | SoC | Result |
|---|---|---|
| Full-stack soak, 25 fps | T31 | 2 h 53 min, 260,648 frames, 1030/1030 snapshots, 0 errors |
| HEVC decode | T31 | 2 × 900 frames, 0 decode errors |
| Native Helix soak, 25 fps | T23 | 2 h 34 min, 231,668 frames, 0 decode errors, ~6 % CPU |
| Tuning checks with t23tune | T23 | Gain/IT limits with AE retime, DRC/defog bits, idempotent sinter, flip, max dgain: all pass |
| Night mono | T23, T21 | Chroma exactly 0 |
| Stop/start cycles | T20 | 20 cycles, 0 oops, no hung process |
| Motion detection | T20 | 6/6 events, 0 false alarms in 2 min |
| Long soak, 25 fps | T20 | 1 h 44 min, 156,517 frames per stream, 0 errors |
| Night stability | T21 | ISP gain constant (was cycling 6↔25) |
| Colour blotches | T21 | Chroma spatial σ 30 → 6 |
| Day exposure | T21 | Mean Y 235 (blown out) → 120, natural colour |
| AEC speech loopback | T31 | Echo vs pauses 29 → 9 dB (−18 dB); AECM ERLE 44 dB |
| Rotation 1280x704 → 704x1280 | T31 | Correct picture; 9 ms/frame at 15 fps |
| Native encoder soak (in progress) | T23 | 25 fps both streams, 0 encoder errors, ~6–7 % CPU |
| Stock-lift equivalence (emulator) | T21 | AE 8/8, ADR 40/40, defog 40/40, dispatchers identical |

## Compared with the vendor stack

| Item | Vendor | Open | Comment |
|---|---|---|---|
| libimp code + data, T21 | ~1.0 MB | ~0.5 MB | Open saves about half |
| libimp code + data, T23 | ~1.26 MB | ~0.6 MB | Native encoder also drops the helixd vendor library |
| libimp code + data, T31 | ~1.05 MB | ~0.57 MB | |
| Kernel module, T21 | 616 KB | 805 KB | Static frames from lifted AE/ADR; reduction planned |
| Kernel module, T23 | 857 KB | ~1,100 KB | After `t23-bss-shrink` (was 1,607 KB) |

## Still open
- T23: hard hang with the AE/sharpen builds (bisecting); then -all-8 aggregates and images (user decision).
- T21 AWB: delayed first snapshot and event-pool burst at start.
- T23 JPEG sizes on cam-B are unusually large (~730 KB at q75 vs ~100 KB expected); dump tool added to find out why.
- T20 driver floods the kernel log with debug trace lines (being silenced).
- AEC device tests on T23/T21/T20; native T23 encoder as default; T31 tuning controls on cam-A.
- Kernel module memory on T21; T40/T41 gaps.
- Improvements beyond vendor behaviour are collected separately and decided by the maintainer.

## Branch map

| Repository | Aggregate | Contains |
|---|---|---|
| open-tx-isp | `claude/open-tx-isp-all-7` | everything above (all SoCs, robustness, review fixes) |
| OpenIMP | `claude/openimp-all-6` | everything above (quickfixes, IVS, AEC, rotation, HEVC, native T23 encoder, review fixes) |

Numbers come from on-device measurements and host checks during the campaign.
