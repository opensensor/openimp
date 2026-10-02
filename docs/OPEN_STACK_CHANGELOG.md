# Open Stack Changelog

Everything changed, extended or fixed in OpenIMP, open-tx-isp, timps and the thingino
integration since the test campaign started on 2026-09-30. Kept up to date during the campaign.

Last update: 2026-10-02 13:05.

Cameras are anonymised: cam-A (T31), cam-B (T23), cam-C (T20), cam-D (T21).

## Where each camera stands

All four test cameras run the open kernel driver (open-tx-isp), OpenIMP and timps.
"Live" means newer builds loaded from `/tmp` that are lost on reboot.

| Camera | SoC | Stack | State |
|---|---|---|---|
| cam-A | T31 | fully open | Flashed 2026-10-02 11:00 with -all-5 image (robust driver incl. t31-robust-2, HEVC, faster IVS) |
| cam-B | T23 | open, encoder via helixd (native selectable) | Flashed 2026-10-02 11:00 with -all-5 image (robust driver, smaller module); native encoder selectable |
| cam-C | T20 | fully open | Flashed 2026-10-02 11:00 with -all-5 image (robust driver, bottom-stripe fix) |
| cam-D | T21 | fully open | Flashed 2026-10-02 11:00 with -all-5 image (robust driver, sensor GPIO patch); boot guard auto |

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
- New images for all four cameras from the aggregate branches (building).
- Kernel module memory on T21; T21 camera memory headroom.
- T21 module reload oops (sensor GPIO not released); boot guard to `auto` once stable.
- AEC test on T23; native T23 encoder as default after the soak; timps clamps OSD on rotated streams to the unrotated height; T40/T41 gaps.
- Merge `tseries-daynight`, `t23-native-helix-2`, `t31-hevc`, `t23-flip-dgain`, `t23-bss-shrink`, `t20-ae-limits` and the newest `t21-image-fixes` into the next aggregates.

## Branch map

| Repository | Aggregate | Contains |
|---|---|---|
| open-tx-isp | `claude/open-tx-isp-all-7` | everything above (all SoCs, robustness, review fixes) |
| OpenIMP | `claude/openimp-all-6` | everything above (quickfixes, IVS, AEC, rotation, HEVC, native T23 encoder, review fixes) |

Numbers come from on-device measurements and host checks during the campaign.
