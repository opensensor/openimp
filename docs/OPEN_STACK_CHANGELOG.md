# Open Stack Changelog

Everything changed, extended or fixed in OpenIMP, open-tx-isp, timps and the thingino
integration since the test campaign started on 2026-09-30. Kept up to date during the campaign.

Last update: 2026-10-03 01:40.

Cameras are anonymised: cam-A (T31), cam-B (T23), cam-C (T20), cam-D (T21).

## Where each camera stands

All five test cameras run the open kernel driver (open-tx-isp), OpenIMP and timps. Since -all-10 no Ingenic or neo helper libraries (libalog/libsysutils) remain on the images.
"Live" means newer builds loaded from `/tmp` that are lost on reboot.

| Camera | SoC | Stack | State |
|---|---|---|---|
| cam-A | T31 | fully open | Flashed 2026-10-03 04:15 with -all-11 / openimp-all-10 image (kernel incl. soc_vpu patch 0099) |
| cam-B | T23 | fully open (native encoder, no OEM helixd) | Flashed 2026-10-03 05:54 with -all-12 / openimp-all-10 image (HLIL AE default; lifted AE export + reconstruction memory-access fixes) |
| cam-C | T20 | fully open | Flashed 2026-10-03 04:07 with -all-11 / openimp-all-10 image (kernel incl. soc_vpu patch 0099) |
| cam-E | T10 | fully open | Flashed 2026-10-03 04:14 with -all-11 / openimp-all-10 image, boot guard auto |
| cam-D | T21 | fully open | Flashed 2026-10-03 04:07 with -all-11 / openimp-all-10 image (kernel incl. soc_vpu patch 0099) |

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
