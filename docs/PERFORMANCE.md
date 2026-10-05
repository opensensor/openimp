# Performance and resources: open stack vs vendor stack

Every number below is quoted from a named source; nothing is estimated.
Cells with no measurement read **not measured**. Most cells are single-stack
measurements from the test campaign; only the T20 night pair is a same-version A/B
(see section 4). Do not turn single-stack cells into ratios.

## 1. What is compared, and how

Two stacks are contrasted:

- **open** = open-tx-isp (kernel driver) + OpenIMP (libimp) + timps, built from the
  `next` branches.
- **vendor** = `tx-isp-*.ko` + the Ingenic libimp.

The measurement procedure is the one documented in the OpenIMP wiki, page
`Testing.md`, section **"A/B method against the vendor stack"**:
two full OTA images that differ only in the ISP choice, same streamer revision, same
camera, same scene, same lighting, one warm-up period before measuring. The recorded
quantities are streamer CPU (all threads), system CPU, resident memory, thread count,
time from start to first image, snapshot latency (average of 10), bitrate main/sub at
the same target and `MemFree`.

Conditions matter and are recorded per row:

| Condition that changes the result | Why |
| --- | --- |
| day vs night | Bitrate, CPU and snapshot latency differ strongly (the AE and the rate controller behave differently). |
| streamer revision | Older timps revisions did not encode without a viewer; two vendor baselines (cam-I, cam-J) were taken with such an older build (see section 4). |
| connected viewers | Each RTSP client adds CPU and memory to timps. |
| `isp_mmap_pool_kb` (T10/T20) | An 8 MiB V4L2-MMAP pool was allocated by the driver until 2026-10-04 noon; after it was turned off by default the open stack has ~8 MB more `MemFree`. |
| `-O0` vs `-Os` for the recovered firmware unit | 2026-10-04: keeping only the firmware unit at `-O0` and turning the frame pool off changed the T20/T10 module by ≈ −140 KB. 2026-10-05: a host harness verified the firmware unit builds identically at `-O0`, `-Os` and `-O2` (375 of 517 functions executed) and the module became 590 → 409 KB (T20) / 587 → 406 KB (T10) — branch only, not device-tested. |

Two different "module size" numbers appear in the sources and they are **not the same
quantity**: `lsmod` reports the in-memory module size, while the changelog/feature
matrix figures are the stripped `.ko` file size. Both are reported below, labelled.

Cameras are anonymised as cam-A to cam-J. Camera mapping used by the sources:
cam-A = T31, cam-B/cam-G/cam-H = T23, cam-C/cam-I = T20, cam-D/cam-J = T21, cam-E = T10,
cam-F = T41.

## 2. Source abbreviations

Raw measurement logs (campaign shell output, not published as files; one line per metric):

| Tag | Camera, stack, date |
| --- | --- |
| `L1` | cam-C, T20, open, night, 2026-10-04 |
| `L2` | cam-C, T20, vendor, night, 2026-10-04 (same streamer revision as L1) |
| `L3` | cam-C, T20, open, day, 2026-10-05, no RTSP client |
| `L4` | cam-I, T20, open, night mode, first boot after flash, 2026-10-05 |
| `L5` | cam-I, T20, vendor, 2026-10-05, **older streamer build, not a same-version A/B** |
| `L6` | cam-J, T21, vendor, day, 2026-10-05, **older streamer build, not a same-version A/B** |
| `L7` | cam-J, T21, open stack after flashing, 2026-10-05 |

The L1/L2 pair is also published as a table in `C:Late morning (2026-10-04, 09:50)`;
the L3 day figures are summarised in `C:Early morning (2026-10-05, 02:30)`.

Documents (read from `origin/next`):

| Tag | Document |
| --- | --- |
| `C:<section>` | `docs/OPEN_STACK_CHANGELOG.md`, the named dated section |
| `M:<row>` / `M:Summary` | `docs/FEATURE_MATRIX.md`, the named row or the Summary block |
| `B:<section>` | `docs/OPENIMP_BEYOND_VENDOR.md`, the named numbered section |
| `RD:<section>` | `README.md`, the named section |
| `W:Testing` | wiki page `Testing.md`, section "A/B method against the vendor stack" |
| `CH:vs` | `docs/OPEN_STACK_CHANGELOG.md`, section "Compared with the vendor stack" |
| `L1`–`L7` | the raw measurement logs above |

## 3. Per-SoC tables

### 3.1 T20 (cam-C; cam-I for the first-boot check)

| Metric | Open | Vendor | Condition / date | Source |
| --- | --- | --- | --- | --- |
| Kernel ISP module, `lsmod` in-memory | 616,608 B | 302,208 B | night, 2026-10-04 | `L1` (`lsmod_isp=tx_isp_t20 616608`), `L2` (`302208`) |
| Kernel ISP module, `lsmod` in-memory | 476,832 B (reported as "466 KB") | not measured | day, 2026-10-05 | `L3` (`lsmod tx_isp_t20=476832`), `C:Early morning (2026-10-05, 02:30)` ("ISP module 466 KB") |
| Kernel ISP module, `lsmod` in-memory | 476,896 B | not measured | cam-I, night mode, first boot | `L4` (`lsmod tx_isp_t20=476896`) |
| Kernel ISP module (T20/T10 unit rebuilt `-Os`, branch only) | `.ko` 590 → 409 KB | not measured | 2026-10-05, **not yet device-tested** | `C:Early morning (2026-10-05, 02:30)` ("Module T20 590 → 409 KB, T10 587 → 406 KB") |
| Kernel ISP module, stripped `.ko` | 736 KB | not measured | 2026-10-04, `next` | `CH:vs` (T20 not in the vendor table), `C:Night (2026-10-03, 20:39)` ("T20 775 to 736 KB") |
| libimp size (code + data) | 594 KB | not measured | 2026-10-04 | `M:libimp size (code + data)` and its note, `B:1. Encoder / rate control` (size bullet), `C:Evening (2026-10-03)` (T20 libimp 694 to 594 KB) |
| MemFree | 46,916 kB | 52,300 kB | night, 2026-10-04, streams running | `L1`, `L2` |
| MemFree (gain from the pool change) | +8 MB (cam-C 46.7 → 55 MB with streams) | 52,300 kB (measured before the pool change) | 2026-10-04 noon | `C:Noon (2026-10-04, 11:40 - 12:15)`, `RD:Where it is better than the vendor stack` |
| MemFree | 54,952 kB | not measured | day, 2026-10-05, no RTSP client | `L3` |
| MemFree | 52,400 kB | 50,444 kB (**not comparable**, see section 4) | cam-I, night mode | `L4`, `L5` |
| Cached | 13,592 kB | 13,784 kB | night, 2026-10-04 | `L1`, `L2` |
| Streamer RSS | 3,740 kB | 6,784 kB | night, 2026-10-04 | `L1`, `L2` |
| Streamer RSS | 4,352 kB | not measured | day, 2026-10-05 | `L3` |
| Threads | 21 | 30 | night, 2026-10-04 | `L1`, `L2` |
| Streamer CPU | 10.5 % | 25.2 % | night, 2026-10-04 | `L1`, `L2`, `W:Testing` (A/B method, example result), `RD:Where it is better than the vendor stack` |
| Streamer CPU | 9.1 % | not measured | day, 2026-10-05 | `L3`, `C:Early morning (2026-10-05, 02:30)` ("streamer CPU 9.1 %") |
| System CPU busy | 15.3 % | 33.5 % | night, 2026-10-04 | `L1`, `L2` |
| System CPU busy | 17.5 % | not measured | day, 2026-10-05 | `L3` |
| Time to first image | 2.529891800 s | 2.976232782 s | night, 2026-10-04 | `L1`, `L2` |
| Snapshot latency (avg / min / max) | 0.198 / 0.144 / 0.274 s | 0.448 / 0.214 / 0.596 s | night, 2026-10-04 | `L1`, `L2` |
| Snapshot latency (avg / min / max) | 0.336 / 0.199 / 0.492 s | not measured | day, 2026-10-05, 10 samples ("10x") | `L3` |
| Bitrate main (ch0) | 1,311 kbit/s | 1,248 kbit/s | night, 2026-10-04, 30.05 s capture, same configuration | `L1`, `L2` |
| Bitrate sub (ch1) | 200 kbit/s | 261 kbit/s | night, 2026-10-04, 30.05 s capture | `L1`, `L2` |
| IVS CPU with motion | 2.7 % (was 4.1 % before the IVS optimisation) | not measured | 2026-10-04 | `M:CPU load (documented figures)`, `M:IVS / motion detection` |
| Hardware JPEG CPU | "~95 % less CPU per snapshot" (generic figure, not T20-specific) | not measured | 2026-10-04 | `M:CPU load (documented figures)`, `M:JPEG / MJPEG / snapshot` (note) |
| Rate control at 1200 kbit/s target | CBR 1300, VBR 1044, SMART 1019 kbit/s | not measured | 2026-10-04, OEM controller default | `M:Rate-control mode (CBR/VBR/FixQP/Capped*/SMART)` |
| Rate control at 1200 kbit/s target (OEM controller port, 60 s) | CBR 1435, VBR 1329, SMART 983 kbit/s (old GOP controller CBR 942) | not measured | measured on cam-C at the time of the port | `B:1. Encoder / rate control` ("Measured 60 s at 1200 kbit/s: CBR 1435 ... VBR 1329, SMART 983; old GOP controller CBR 942") |
| Rate control, I-aware P budget (P2) | CBR 1583 → 1300 kbit/s (encoder stats 1244) | not measured | measured on cam-C at 1200 kbit/s | `B:1. Encoder / rate control` |
| RC parameters at 1200 kbit/s target | `quality_lvl` 0/6 → 1130/800, `change_pos` 50/100 → 850/1210 kbit/s | not measured | 2026-10-04 | `M:Rate-control mode (CBR/VBR/FixQP/Capped*/SMART)` |
| Video memory saved by JPEG sharing the H.264 bitstream area | −1.44 MB, plus 328 KiB in the JPEG bitstream buffer | not measured | T20/T23/T10 | `B:3. JPEG / snapshot` |
| IVS v2 extra CPU cost | +225 µs per 25 fps input frame (+0.6 % of a core); 1.8 ms per analysed 640×360 frame | vendor move: 1.6 ms per analysed frame | 2026-10-04 | `B:4.1 Motion v2 (on by default, beyond vendor)` |
| Snapshot rate | 30/30 non-empty, 0 oops | count not measured (only `oops=0`), 0 oops | cam-I first boot (open); cam-C night (no snapshot count recorded) | `L4` ("snapshots 30/30 ... oops=0"); `L1`, `L2` (`oops=0`), `W:Testing` (pass criterion 30/30) |

Notes on the T20 rows:

- The 2026-10-04 night pair (`L1`/`L2`) is the campaign's A/B reference and is quoted in
  the wiki as the example result, but it was taken **before** the day mode was measured a
  second time in `L3` and **before** `isp_mmap_pool_kb=0` became the default. The
  `MemFree` comparison in that pair (46.9 MB open vs 52.3 MB vendor) is therefore a
  pre-fix number; the source itself states the gap "is ~5 MB lower because the open
  driver module is larger and keeps more in RAM" (`C:Late morning (2026-10-04, 09:50)`),
  and after the pool change the changelog says the open stack "now leaves more RAM free
  than the vendor stack on T20" (`C:Noon (2026-10-04, 11:40 - 12:15)`: "cam-C 46.7 → 55
  MB"). `L3` (54,952 kB) confirms the higher figure.
- The open `lsmod` sizes differ between the 2026-10-04 night run (616,608 B) and the
  2026-10-05 day run (476,832 B); this matches the change that only the recovered
  firmware unit stays `-O0` (`C:Noon (2026-10-04, 11:40 - 12:15)`). The A/B CPU/RSS/
  latency figures in `L1`/`L2` are therefore from the older, larger module.

### 3.2 T10 (cam-E)

| Metric | Open | Vendor | Condition / date | Source |
| --- | --- | --- | --- | --- |
| Kernel ISP module, stripped `.ko` | 731 KB (was 770 KB) | not measured | 2026-10-04 | `M:Kernel module size`, `C:Night (2026-10-03, 20:39)` |
| libimp size (code + data) | 626,032 B (~0.6 MB) | not measured | 2026-10-04 | `M:libimp size (code + data)` |
| MemFree with streams running | 1.9 → ~9.7 MB (after the frame pool was turned off) | not measured | 2026-10-04 before noon → after noon | `C:Noon (2026-10-04, 11:40 - 12:15)` |
| MemFree (device test) | 1.9–2.8 MB of 37.7 MB | not measured | 2026-10-03 | `M:Video memory (rmem) / MemFree` |
| ispmem | 8 → 6 MB | not measured | 2026-10-04 | `C:Afternoon (2026-10-04, 14:40)` |
| Streamer CPU | 5–9 % with 2 streams at 25 fps (17 % momentary with 1 stream) | not measured | 2026-10-04 | `M:CPU load (documented figures)` |
| Encoder CPU at 1200 kbit/s | 8.3 → 5.5 % (super-frame fix) | not measured | 2026-10-03 evening | `M:Rate-control mode (CBR/VBR/FixQP/Capped*/SMART)`, `B:1. Encoder / rate control` ("CPU 8.3 to 5.5 %") |
| Bitrate at 1200 kbit/s | 450 → 822 kbit/s, re-encodes 800 → 0 | not measured | 2026-10-03 evening | `M:Rate-control mode (CBR/VBR/FixQP/Capped*/SMART)`, `B:1. Encoder / rate control` |
| Bitrate, CBR targets | 2500 → 3872 kbit/s, 400 → 352 kbit/s | not measured | 2026-10-04 | `M:Rate-control mode` |
| CBR overshoot of the old controller | +55 % at 2500 kbit/s | not measured | before the OEM-style controller | `B:1. Encoder / rate control` |
| MemFree gain from the pool change | 1.9 → ~9.7 MB | not measured | 2026-10-04 noon | `C:Noon (2026-10-04, 11:40 - 12:15)`, `RD:Where it is better than the vendor stack` |
| RSS, threads, time to first image, snapshot latency, system CPU | not measured | not measured | — | — |

### 3.3 T21 (cam-D)

| Metric | Open | Vendor | Condition / date | Source |
| --- | --- | --- | --- | --- |
| Kernel ISP module, stripped `.ko` | **452 KB** (was 805 KB; the matrix cell gives was 760 KB, another baseline) | 616 KB | size work of 2026-10-03/04 | `CH:vs`, `M:Kernel module size` (452 KB since all-17; an older figure of 494 KB from the first size step is superseded) |
| libimp size (code + data) | ~0.5 MB | ~1.0 MB | 2026-10-03/04 | `CH:vs` |
| Kernel module RAM | unchanged by the size work | not measured | 2026-10-04 | `M:Kernel module size` |
| Free video memory (rmem) with main+sub+MJPEG | 2.76 MB | ≈1.2 MB | 2026-10-04 | `M:Summary` ("more free video memory on T21 (2.76 MB instead of ~1.2 MB)"), `M:Video memory (rmem) / MemFree` |
| rmem peak | 18.4 of 23.5 MB | not measured | 2026-10-04 | `C:Noon (2026-10-04, 11:40 - 12:15)` |
| Streamer CPU | 15.5 % (17.5 % before the optimisation) | not measured | 2026-10-03 night → 2026-10-04 | `C:Night (2026-10-04, 00:00)` ("T21 17.5 → 15.5 %") |
| ISP firmware process CPU | −10 % | not measured | 2026-10-03 evening | `M:CPU load (documented figures)` |
| AWB instruction count | 0.95x the vendor's, output bit-identical (was 1.41x) | reference | 2026-10-03 evening | `M:CPU load (documented figures)` |
| Hardware JPEG | 37 ms/job, snapshots 0.05–0.18 s | not measured | 2026-10-03 | `M:JPEG / MJPEG / snapshot` |
| Rate control at 1200 kbit/s | eprc: CBR 1326, VBR 1096, SMART 1071 kbit/s | not measured | 2026-10-03 | `M:Rate-control mode` |
| Rate control at 1200 kbit/s (complete port) | CBR 1305, VBR 1042, SMART 1090 kbit/s | not measured | 2026-10-03 | `M:Rate-control mode` |
| Kernel ISP module, `lsmod` in-memory (cam-J) | 647,232 B | 615,744 B | day, 2026-10-05, older streamer build, **not a same-version A/B** | `L6`, `L7` |
| MemFree (cam-J) | 3.4 MB (after flashing the open stack) | 2.0 MB | day, 2026-10-05, older streamer build, **not a same-version A/B** | `L6`, `L7` |
| Streamer CPU (cam-J) | not measured (open) | 14.3 % | day, 2026-10-05, older streamer build | `L6` |
| System CPU busy (cam-J) | not measured (open) | 36.4 % | day, 2026-10-05, older streamer build | `L6` |
| Streamer RSS / threads (cam-J) | not measured (open) | 5.1 MB / 27 | day, 2026-10-05, older streamer build | `L6` |
| Time to first image (cam-J) | not measured (open) | 2.38 s | day, 2026-10-05, older streamer build | `L6` |
| Snapshot latency avg / min / max (cam-J) | not measured (open) | 0.46 / 0.17 / 1.13 s | day, 2026-10-05, older streamer build | `L6` |
| Bitrate ch0 / ch1 (cam-J) | not measured (open) | 2792 / 441 kbit/s | day, 2026-10-05, older streamer build | `L6` |
| Snapshots (cam-J) | 30/30 | not measured | after flashing the open stack, 2026-10-05 | `L7` |
| Open-stack RSS, threads, time to first image, snapshot latency, CPU, system CPU | not measured | — | — | — |

### 3.4 T23 (cam-B; cam-G and cam-H run the same stack)

| Metric | Open | Vendor | Condition / date | Source |
| --- | --- | --- | --- | --- |
| Kernel ISP module, stripped `.ko` | 622 KB (was 1,047 KB, originally 1,607 KB) | 857 KB | 2026-10-03/04 | `CH:vs`, `M:Kernel module size` |
| libimp size (code + data) | 726 KB in the matrix (the changelog table rounds to ~0.6 MB) | ~1.26 MB | 2026-10-03/04, native encoder, no helixd | `CH:vs`, `M:libimp size` |
| Vendor helper libraries | none (no helixd, no vendor libimp) | helixd + vendor libimp | 2026-10-03/04 | `C:Night (2026-10-03, 20:39)`, `M:Helper libraries` |
| Reference-frame sharing saving | ~1.5 MB video memory at 1080p (three sources) / ~1.4 MB (`C:`) | ring mode used by default up to 1080p | measured on cam-B | `M:Reference-frame sharing (BUF_SHARE_CFG)`, `B:5. Reference buffer sharing`, `RD:Where it is better than the vendor stack`, `C:Afternoon (2026-10-03)` ("Saves ~1.4 MB video memory") |
| Streamer CPU | ~6–7 % for 2 streams at 25 fps | not measured | 2026-10-04 | `M:CPU load (documented figures)` |
| Hardware JPEG | 29 ms/job, q75 size matches libjpeg | not measured | 2026-10-03 | `M:JPEG / MJPEG / snapshot` |
| Long-run decode result | 2 h 34, 231,668 frames, 0 decode errors | not measured | 2026-10-04 | `M:H.264` |
| Rate control at 1200 kbit/s | eprc: SMART 1141, CBR 1253, VBR 1255 kbit/s | not measured | 2026-10-03 | `M:Rate-control mode` |
| eprc macroblock RC deviations | 0 against the vendor (emulator) | reference | T23 and T21 | `B:1. Encoder / rate control` |
| IVS v2 extra CPU cost | +175 µs per 25 fps input frame (+0.4 % of a core); 1.2 ms per analysed 640×360 frame | vendor move: 1.0 ms per analysed frame | 2026-10-04 | `B:4.1 Motion v2 (on by default, beyond vendor)` |
| Module reload / kill -9 | 10× stop/start incl. `kill -9` and 10× module reload, 0 oops | vendor oopses on reload (T21), leaks 252 KB/cycle (T31) | 2026-10-02…04 | `B:6. Robustness (open-tx-isp and OpenIMP)`, `M:Reload / error handling (rmmod, stop/start)` |
| MemFree, RSS, threads, time to first image, snapshot latency, system CPU | not measured | not measured | — | — |

### 3.5 T31 (cam-A)

| Metric | Open | Vendor | Condition / date | Source |
| --- | --- | --- | --- | --- |
| Kernel ISP module, stripped `.ko` | 711 KB | 829 KB | 2026-10-03/04 | `CH:vs`, `M:Kernel module size` |
| libimp size (code + data) | ~0.57 MB | ~1.05 MB | 2026-10-03/04 | `CH:vs` |
| Video memory (rmem) | 36 MB (was 50 MB) | not measured | 2026-10-04 | `C:Afternoon (2026-10-04, 14:40)` |
| MemFree drift per module reload | 460 → 45 KB per cycle | not measured | 2026-10-02, branch `t31-robust-2` | `C:Robustness: better than the vendor driver (2026-10-02)`; `M:Reload / error handling (rmmod, stop/start)` ("residual drift ~45 KB/cycle") |
| Streamer CPU | 7.9 % (was 8.7 %) | not measured | 2026-10-03/04 CPU work | `C:Night (2026-10-04, 00:00)` |
| Rotation CPU cost | 9 ms/frame at 15 fps (1280×704 → 704×1280) | software rotation (32×32 tiles), no CPU figure published | measured on cam-A | `M:Rotation 90°/270°`, `CH:vs` ("Correct picture; 9 ms/frame at 15 fps") |
| Hardware JPEG CPU, 1 snapshot/s | 17 % (was 70 % with software JPEG) | not measured | cam-A | `B:3. JPEG / snapshot` ("about 70 % to 17 % timps CPU at 1 snapshot/s") |
| OSD CPU cost | timps about 8 % CPU with the OSD drawn by the IPU | not measured | cam-A | `B:6. Robustness (open-tx-isp and OpenIMP)` |
| IVS CPU on the sub-stream | ~85 % less IVS CPU than with the vendor libimp | reference | 2026-10-04 | `M:IVS / motion detection` |
| Long-run decode result | 2 h 53, 260,648 frames, 1030/1030 snapshots, 0 errors | not measured | 2026-10-04 | `M:H.264` |
| Rate control at 1200 / 3000 kbit/s | CBR 1210 / 2973 kbit/s (legacy controller 1511 / 3786) | not measured | 2026-10-03 | `M:Rate-control mode` |
| Rate control at 1200 kbit/s | VBR 1163, CappedVBR 1177, CappedQuality 1174 kbit/s | not measured | 2026-10-03 | `M:Rate-control mode` |
| MemFree, RSS, threads, time to first image, snapshot latency, system CPU | not measured | not measured | — | — |

### 3.6 T41 (cam-F)

| Metric | Open | Vendor | Condition / date | Source |
| --- | --- | --- | --- | --- |
| Kernel ISP module, stripped `.ko` | 731,488 B | not measured | 2026-10-04 | `M:Kernel module size` ("vendor size not measured") |
| libimp size (code + data) | 465,728 B (~0.47 MB) | not measured | 2026-10-04 | `M:libimp size (code + data)` |
| rmem | 26 MB (30 MB → 24 MB → 26 MB) | not measured | current image, rootfs rev7 | `M:Video memory (rmem) / MemFree` ("rmem 26 MB in the current image (was 30)"), `C:Late night (2026-10-03, 22:30)` (24 MB), `C:Afternoon (2026-10-04, 14:40)` (rev6 with rmem 26M) |
| MemFree idle | 7.8 MB | not measured | 2026-10-03 night | `C:Late night (2026-10-03, 22:30)` |
| MemFree minimum under stress | 2.6 MB (3 streams + 2 snapshot loops, 5 min, no OOM, no reboot) | not measured | 2026-10-03 night | `C:Late night (2026-10-03, 22:30)`, `M:Video memory` |
| Streamer CPU | ~10–12 % with the open libimp | ~27 % with the vendor libimp | momentary values, 2026-10-04 | `M:CPU load (documented figures)`, `B:4. IVS / motion` |
| Memory under three parallel streams | OOM at 30 MB rmem; 26 MB in the test image works | not measured | current image | `RD:Status` (T41), `M:Video memory (rmem) / MemFree` |
| Module reload / kill -9 | 10/10 `rmmod`/`insmod` cycles, refcnt 0, 0 oops; `kill -9` recovers 3/3 | not measured | rev2 image | `B:6. Robustness (open-tx-isp and OpenIMP)` |
| Stress result, 5 min (3 RTSP clients + snapshots) | 0 VPU/AVPU errors, main stream High@5.1 | not measured | 2026-10-04 | `M:Long-term stability / hangs`, `M:Kernel soc_vpu / Helix hardening` |
| Bitrate at 400 / 1200 / 3000 kbit/s targets | 518 / 1195 / 2777 kbit/s | not measured | 2026-10-04 | `M:Rate-control mode` |
| RSS, threads, time to first image, snapshot latency, system CPU | not measured | not measured | — | — |

## 4. Not comparable / caveats

1. **`L5` (cam-I, T20, vendor, 2026-10-05) and `L6` (cam-J, T21, vendor, 2026-10-05)** were
   taken with an older streamer build than the open-stack runs. That older build barely
   encodes without a viewer, so the cam-I CPU value (0.3 % streamer, 3.0 % system) is **not
   valid** and is not quoted as a vendor CPU figure. The cam-J vendor row (14.3 % CPU,
   36.4 % system) was taken with a viewer but is also **not a same-version A/B**; it is a
   baseline only. The `MemFree`, `lsmod` and bitrate values are quoted and marked.
2. **`lsmod` size vs `.ko` file size.** `L1`–`L5` report the `lsmod` in-memory size;
   `CH:vs` and `M:` report stripped `.ko` file sizes. They are different quantities and
   are labelled as such above. Do not compare across the two.
3. **cam-C 2026-10-04 night A/B predates three changes:** `isp_mmap_pool_kb=0` (+8 MB on
   T20), the module size reduction (−140 KB, only the recovered firmware unit kept at
   `-O0`), and the later day-mode run. The CPU/RSS/latency comparison is still valid
   (both images are from the same revision), the `MemFree` comparison is not current.
4. **Day vs night.** `L1`/`L2` are night; `L3` is day without an RTSP client. The
   snapshot latency and CPU differ (0.198 s vs 0.336 s) mainly because no client is
   attached in `L3`. Do not read `L3` as an improvement.
5. **Different streamer revisions across the campaign.** Only `L1`/`L2` come from one
   congruent pair (same timps revision, same scene, same lighting). Every other table
   cell is a single-stack measurement and must not be turned into a ratio.
6. **No systematic vendor comparison exists for T10, T21, T23, T31, T41.** The vendor
   column of those tables is mostly "not measured". The one full same-version A/B pair in the campaign
   is T20 (`L1`/`L2`); T21 has the cam-J vendor baseline of 2026-10-05 (older streamer build); the second-best-sourced comparison is the T20 day figure against a
   vendor value recorded earlier (`C:Early morning (2026-10-05, 02:30)`: "MemFree 54.9 MB
   (vendor measured earlier 52.3 MB)").
8. **T41 day/night, AE/AWB quality, audio and the temper effect are untested**
   (`M:ISP core / sensor bring-up` (T41 cell), `C:Still open (2026-10-04)`), so no
   performance claim should be derived from T41 beyond the rows above.
9. **T21 module size.** The newest value is 452 KB (`CH:vs`, since all-17, vendor 616 KB).
   494 KB was an earlier figure of the first size step and is superseded. The `lsmod`
   in-memory size of cam-J (647,232 B open, 615,744 B vendor) is a different quantity
   from the stripped `.ko` size and must not be compared with it.
10. **T20/T10 module size changed again after the campaign numbers were recorded.**
    The `-Os` firmware build of 2026-10-05 (T20 409 KB, T10 406 KB) is a branch, not
    device-tested; the measured `lsmod` and `.ko` values above are older builds.
11. **Snapshot latency sampling differs.** `L3` explicitly records "(10x)"; `L1`, `L2` and
    `L5` give an average with min/max over the same short sample, so a single outlier
    moves the average. `L1`, `L2` and `L4` record `oops=0`; only `L4` records the
    snapshot count (30/30).

## 5. Open measurements (what is missing and what it would take)

| Missing cell | What would be needed |
| --- | --- |
| Vendor column for T10, T21, T23, T31, T41 | One A/B image pair per SoC (same camera, same scene, same timps revision) as in `W:Testing`. |
| Streamer RSS and thread count for T10, T21, T23, T31, T41 (open and vendor) | The same A/B runs, recording RSS/threads. |
| Time to first image and snapshot latency for T10, T21, T23, T31, T41 | Same runs; snapshot latency as "average of 10". |
| System CPU for T10, T21, T23, T31, T41 | Same runs. |
| MemFree for T21, T23, T31, T41 (and a current vendor value for T20) | `MemFree` before and after stream start, both stacks, same revision. |
| T20 night A/B repeated on the current `next` | A fresh image pair after `isp_mmap_pool_kb=0` and the `-Os` firmware change, to replace the pre-fix `MemFree` row. |
| Per-SoC hardware-JPEG CPU (the "~95 %" figure is generic) | CPU per snapshot job per SoC, or jobs/s measured the same way on both stacks. |
| Per-SoC IVS CPU against the vendor IVS | T20/T23 IVS benchmark with the vendor algorithm (`OPENIMP_MOTION_V2=0`) and the vendor libimp. |
| Vendor rate-control numbers per SoC | Same target bitrates (CBR/VBR/SMART, 60 s each) with the vendor libimp. |
| Viewer count standardisation | Define a fixed viewer set (e.g. 1 RTSP main + 1 sub) for all A/B runs. |
| T41 CPU under identical conditions (10–12 % vs 27 % are "momentary") | A steady-state measurement with a fixed viewer set and a defined scene. |
| T20/T10 `-Os` firmware module, device-tested | Flash the 2026-10-05 harness-verified build and repeat the T20 A/B. |
| T21 same-version A/B (cam-J has only an older-build vendor baseline) | Re-measure the vendor stack with the current streamer build, same scene and viewer set. |

## 6. Provenance

All figures are quoted from the sources listed in section 2: `docs/OPEN_STACK_CHANGELOG.md`,
`docs/FEATURE_MATRIX.md`, `docs/OPENIMP_BEYOND_VENDOR.md` and `README.md` from branch
`origin/next` of `openimp`; the wiki page `Testing.md`; and the raw measurement logs in
the raw measurement logs L1 to L7. Where a number appears in more than one source the changelog section
is given as the primary citation. Nothing on this page was estimated, extrapolated or
converted between units.
