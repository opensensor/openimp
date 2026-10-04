# Rate control: can OpenIMP beat the vendor controllers? (offline study)

Question: OpenIMP now runs bit-exact ports of the vendor picture rate
controllers (T21/T23 "eprc" `src/eprc`, T20 `src/rc_t20`, T10 `src/rc_t10`;
branches `claude/eprc-t21-t23`, `claude/t20-rc`).  Where do they behave badly,
and which small, opt-in changes measurably do better?  **Nothing here changes a
default**; every proposal below is a switch for the user to decide on.  All
numbers are from a host simulation (no camera); T31 (Allegro) is not covered
(its port is in progress elsewhere, see "Not covered").

## Harness (`tools/rc_sim/`)

| file | |
|---|---|
| `Makefile` | builds `build/librcsim.so` from the controller sources taken unchanged from their branches (`git show $(EPRC_REF)` / `$(T20_REF)`), plus one study hook (below) |
| `rcsim.c`, `rcsim.py` | uniform C wrapper + ctypes binding: init / picture start / picture end with coded size and the hardware statistics, raw access to the OEM-layout blocks |
| `rc_sim.py` | scenes, encoder model, closed loop, metrics; `baseline`, `trace`, `study`, `robust` |
| `variants.py` | the "beyond vendor" variants, the robustness study and its verdict rule |
| `h264_trace.py` | per-picture type / size / slice QP of an H.264 recording (used for the calibration) |

```
make -C tools/rc_sim
tools/rc_sim/rc_sim.py baseline                 # vendor controllers, 6 scenes
tools/rc_sim/rc_sim.py robust [variant ...]     # 5 operating points x 6 scenes x 3 seeds
tools/rc_sim/rc_sim.py trace T23 VBR day_static eprc_stable   # per picture
```

The only change to the ports is a hook at the per-picture nominal budget of
the P-picture QP (`rc_t20.c` `tgt = RI32(S, 128)`, `rc_t10.c`
`avg = RI32(S, 116)` -> `rcsim_hook_per()`), which returns its argument
unless the `iaware` variant enables it.  All other variants act only from
outside, as an OpenIMP wrapper would: change controller fields between
pictures, clamp the picture QP and write it back where the controller reads
the coded QP (eprc E+1600; T20 rcSt+32/E+217; T10 rcSt+40/E+177), or change
the re-encode thresholds.

### Encoder model

Per picture, in bytes (Mpx = picture size in megapixels):

```
I = hdr + Mpx * ci * 2^((30-QP)/7)
P = hdr + Mpx * ( cm * 2^((30-QP)/6)                                motion residual
                + cn / (1 + 2^((QP-qn)/sn)) * 1.7^clip(QPref-QP, -4, 4) )  sensor noise / skip cliff
P <= 0.9 I ;  lognormal jitter sigma 0.12 (same jitter for a re-encode)
```

`cn/qn/sn` model the "skip cliff": in a static scene a P picture is ~100 bytes
(all skip) until the QP drops below the sensor noise, then it grows x2..x4 per
QP step; the `1.7^dq` term: a P picture coded finer than its reference
re-codes the reference's noise.  Hardware statistics (SAD complexity `cmpx`,
motion-vector sum, moving / intra macroblock counts) are synthesised per
scene into the registers each controller reads (eprc 0x80080, 0x800e0,
0x800e4, 0x800e8/ec; T20 cmpx + 0x800e4/e8/ec; T10 cmpx).

Calibration against 1080p recordings of the OpenIMP T21/T23 stack with the
eprc controller (slice QP from the bitstream, `h264_trace.py`): I 344 KB at
QP 34 (-> ci = 250 KB/Mpx at QP 30), static P 100..250 B at QP 35..36 and
1..7 KB at QP 32..33 (cn, qn = 28, sn = 1).  The simulated vendor eprc then
reproduces the recorded behaviour without further tuning:

| static day scene, 1080p15 | I bytes @ QP | P bytes @ QP | P QP range | P > 5 KB | QP up-jumps >= 2 / 100 pictures |
|---|---|---|---|---|---|
| recording, T23 eprc SMART | 322 K @ 35.0 | 2.2 K @ 36.7 | 32..45 | 7 % | 25.4 |
| recording 2, T23 eprc SMART | 275 K @ 37.3 | 3.7 K @ 37.4 | 31..45 | 10 % | 25.1 |
| simulation, T23 eprc SMART | 369 K @ 33.4 | 3.8 K @ 33.9 | 26..45 | 13 % | 25.5 |
| recording, T23 without eprc (band mapping) | | | | | 0.3 |

(The other eprc recordings show 14..27 up-jumps / 100 pictures; T21 with
the T23 controller 18.)  The model is an approximation: absolute rates are
indicative, the comparisons vendor vs variant on the same input are what
the study relies on.

### Scenes (60 s each) and operating points

`day_static` (textured, little noise), `night_static` (IR, noisy),
`day_motion` (continuous traffic), `bursts` (3 s of motion every 15 s),
`scene_cut` (busier scene 20..40 s), `ir_switch` (day -> IR night at 30 s).
Operating points: A 1080p 15 fps 1500 kbit/s GOP 50 (the default below),
B 1080p 25 fps 2500 kbit/s, C 640x360 15 fps 384 kbit/s, D = A with 0.3 x
intra complexity (small I/P ratio), E = A with GOP 15.  QP range 15..45,
frmQPStep 3, gopQPStep 15 (i264e defaults), IDR every GOP, changePos 80,
qualityLvl 4.

### Metrics

`rate` = bit rate / configured bit rate (VBR/SMART: / maxBitRate; their
target is changePos = 80 %), `1 s peak` = largest 1 s window / bit rate,
`VBV s` = peak fill of a leaky bucket drained at the bit rate (seconds of
bit rate; > 1-2 s means a CBR receiver/buffer overflows), `QP` mean of all
pictures, `dQP` mean |QP step| between consecutive P pictures (flicker),
`I pulse` mean |QP(I) - QP(P before)|, `at max` share of pictures at max QP,
`re-enc` extra encodes per picture (CPU), `ovr` seconds per minute without
IDR above 1.5 x bit rate (burst control).  Verdict per run (robustness
tables): `-` regression = more VBV, or higher QP without fewer bits, or more
bits without lower QP, or dQP +0.3, or (CBR) further from the bit rate;
`+` win = 10 % fewer re-encodes, fewer bits at equal QP, lower QP at equal
bits, overshoot cut, dQP -0.3 or VBV -20 %; else `=`.

## Vendor baseline (operating point A, seed 1)

T23 SMART equals T23 VBR here (IDR every GOP: the SMART-only GOP-start
logic is not reached), so it is not listed.

| SoC | mode | scene | kbit/s | rate | 1 s peak | VBV s | QP | I QP | P QP | dQP | I pulse | at max | re-enc |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| T23 | CBR | day_static | 1410 | 0.94 | 3.28 | 2.6 | 32.8 | 34.1 | 32.8 | 1.36 | 5.6 | 0.06 | 0.00 |
| T23 | CBR | night_static | 1418 | 0.95 | 2.09 | 1.3 | 38.3 | 36.4 | 38.3 | 1.30 | 5.6 | 0.02 | 0.00 |
| T23 | CBR | day_motion | 1409 | 0.94 | 2.39 | 1.8 | 40.4 | 38.2 | 40.5 | 0.86 | 5.5 | 0.09 | 0.00 |
| T23 | CBR | bursts | 1696 | 1.13 | 4.11 | 10.5 | 35.3 | 34.9 | 35.4 | 1.18 | 2.9 | 0.19 | 0.00 |
| T23 | CBR | scene_cut | 1425 | 0.95 | 3.24 | 2.8 | 35.8 | 35.8 | 35.8 | 1.15 | 5.2 | 0.08 | 0.00 |
| T23 | CBR | ir_switch | 1418 | 0.95 | 3.46 | 2.6 | 35.7 | 35.1 | 35.7 | 1.32 | 5.4 | 0.05 | 0.00 |
| T23 | VBR | day_static | 1318 | 0.88 | 3.20 | 2.5 | 33.8 | 34.1 | 33.8 | 1.59 | 4.1 | 0.01 | 0.00 |
| T23 | VBR | night_static | 1276 | 0.85 | 1.89 | 1.2 | 38.6 | 36.3 | 38.6 | 1.35 | 2.4 | 0.00 | 0.00 |
| T23 | VBR | day_motion | 1295 | 0.86 | 2.16 | 1.7 | 41.0 | 39.1 | 41.1 | 0.89 | 2.6 | 0.10 | 0.00 |
| T23 | VBR | bursts | 1651 | 1.10 | 4.33 | 9.8 | 36.3 | 35.5 | 36.3 | 1.34 | 3.9 | 0.18 | 0.00 |
| T23 | VBR | scene_cut | 1299 | 0.87 | 3.07 | 2.6 | 35.9 | 35.9 | 35.9 | 1.37 | 2.8 | 0.11 | 0.00 |
| T23 | VBR | ir_switch | 1308 | 0.87 | 2.94 | 2.6 | 37.2 | 35.8 | 37.2 | 1.49 | 2.8 | 0.07 | 0.00 |
| T20 | CBR | day_static | 4313 | 2.88 | 6.93 | 113.8 | 27.7 | 26.7 | 27.7 | 0.86 | 2.0 | 0.01 | 0.00 |
| T20 | CBR | night_static | 2913 | 1.94 | 3.49 | 56.7 | 35.4 | 33.9 | 35.4 | 0.83 | 2.0 | 0.01 | 0.00 |
| T20 | CBR | day_motion | 2353 | 1.57 | 4.77 | 34.5 | 36.4 | 34.7 | 36.4 | 0.88 | 2.1 | 0.01 | 0.00 |
| T20 | CBR | bursts | 4336 | 2.89 | 9.16 | 115.0 | 30.5 | 28.7 | 30.5 | 0.76 | 2.0 | 0.02 | 0.00 |
| T20 | CBR | scene_cut | 3900 | 2.60 | 9.15 | 97.5 | 30.0 | 28.8 | 30.0 | 0.70 | 2.0 | 0.01 | 0.00 |
| T20 | CBR | ir_switch | 3652 | 2.43 | 7.31 | 87.6 | 31.5 | 30.1 | 31.6 | 0.84 | 2.0 | 0.01 | 0.00 |
| T20 | VBR | day_static | 3622 | 2.41 | 6.18 | 86.2 | 28.5 | 27.4 | 28.5 | 0.86 | 2.0 | 0.01 | 0.00 |
| T20 | VBR | night_static | 2155 | 1.44 | 2.94 | 26.4 | 36.2 | 34.7 | 36.2 | 0.61 | 2.0 | 0.01 | 0.00 |
| T20 | VBR | day_motion | 2266 | 1.51 | 4.29 | 31.6 | 36.5 | 34.4 | 36.5 | 0.89 | 2.1 | 0.01 | 0.00 |
| T20 | VBR | bursts | 3732 | 2.49 | 8.17 | 90.9 | 31.2 | 29.5 | 31.3 | 0.78 | 2.1 | 0.02 | 0.00 |
| T20 | VBR | scene_cut | 3348 | 2.23 | 8.03 | 75.4 | 30.9 | 29.5 | 30.9 | 0.78 | 2.0 | 0.01 | 0.00 |
| T20 | VBR | ir_switch | 3054 | 2.04 | 6.68 | 63.7 | 32.2 | 30.8 | 32.2 | 0.77 | 2.0 | 0.01 | 0.00 |
| T20 | SMART | day_static | 3622 | 2.41 | 6.18 | 86.2 | 28.5 | 27.4 | 28.5 | 0.86 | 2.0 | 0.01 | 0.00 |
| T20 | SMART | night_static | 2330 | 1.55 | 3.04 | 34.1 | 36.0 | 34.8 | 36.0 | 0.68 | 2.0 | 0.01 | 0.00 |
| T20 | SMART | day_motion | 1939 | 1.29 | 3.94 | 20.5 | 38.1 | 35.8 | 38.1 | 0.89 | 2.1 | 0.06 | 0.00 |
| T20 | SMART | bursts | 3696 | 2.46 | 7.96 | 89.5 | 31.3 | 29.8 | 31.3 | 0.77 | 2.1 | 0.02 | 0.00 |
| T20 | SMART | scene_cut | 3301 | 2.20 | 8.03 | 73.5 | 31.0 | 29.7 | 31.0 | 0.77 | 2.0 | 0.01 | 0.00 |
| T20 | SMART | ir_switch | 3053 | 2.04 | 6.68 | 63.5 | 32.2 | 30.8 | 32.3 | 0.79 | 2.0 | 0.01 | 0.00 |
| T10 | CBR | day_static | 2407 | 1.60 | 4.26 | 38.7 | 29.6 | 30.3 | 29.5 | 0.19 | 0.4 | 0.02 | 0.00 |
| T10 | CBR | night_static | 2610 | 1.74 | 2.92 | 45.5 | 35.4 | 35.8 | 35.4 | 0.25 | 0.4 | 0.02 | 0.00 |
| T10 | CBR | day_motion | 1890 | 1.26 | 3.23 | 16.3 | 37.3 | 37.5 | 37.3 | 0.52 | 1.4 | 0.01 | 0.00 |
| T10 | CBR | bursts | 2529 | 1.69 | 6.05 | 43.6 | 33.0 | 33.4 | 33.0 | 0.47 | 2.1 | 0.06 | 0.00 |
| T10 | CBR | scene_cut | 2240 | 1.49 | 5.89 | 32.1 | 32.3 | 33.1 | 32.3 | 0.24 | 0.6 | 0.02 | 0.00 |
| T10 | CBR | ir_switch | 2595 | 1.73 | 6.43 | 45.6 | 32.6 | 33.0 | 32.6 | 0.27 | 0.6 | 0.02 | 0.00 |
| T10 | VBR | day_static | 2299 | 1.53 | 4.06 | 35.1 | 29.8 | 30.7 | 29.8 | 0.19 | 0.5 | 0.02 | 0.97 |
| T10 | VBR | night_static | 2562 | 1.71 | 2.70 | 43.7 | 35.5 | 36.0 | 35.5 | 0.27 | 0.8 | 0.03 | 0.98 |
| T10 | VBR | day_motion | 1612 | 1.07 | 3.11 | 6.2 | 38.7 | 39.0 | 38.7 | 0.54 | 1.3 | 0.01 | 1.00 |
| T10 | VBR | bursts | 2152 | 1.43 | 5.70 | 30.0 | 33.7 | 34.4 | 33.7 | 0.48 | 2.9 | 0.19 | 0.92 |
| T10 | VBR | scene_cut | 1920 | 1.28 | 5.92 | 20.0 | 33.0 | 33.8 | 33.0 | 0.26 | 0.8 | 0.02 | 0.97 |
| T10 | VBR | ir_switch | 2404 | 1.60 | 4.60 | 38.8 | 32.9 | 33.4 | 32.9 | 0.30 | 0.9 | 0.02 | 0.97 |
| T10 | SMART | day_static | 1028 | 0.69 | 4.29 | 16.4 | 33.1 | 42.4 | 32.9 | 0.43 | 10.9 | 0.04 | 0.00 |
| T10 | SMART | night_static | 1979 | 1.32 | 2.30 | 20.3 | 36.3 | 36.4 | 36.3 | 0.26 | 0.1 | 0.02 | 0.00 |
| T10 | SMART | day_motion | 1321 | 0.88 | 3.71 | 2.7 | 41.1 | 41.0 | 41.1 | 0.56 | 2.8 | 0.21 | 0.00 |
| T10 | SMART | bursts | 1454 | 0.97 | 4.88 | 11.4 | 36.0 | 41.6 | 35.9 | 0.56 | 8.9 | 0.21 | 0.00 |
| T10 | SMART | scene_cut | 1462 | 0.97 | 5.04 | 20.5 | 34.7 | 42.2 | 34.6 | 0.45 | 9.8 | 0.09 | 0.00 |
| T10 | SMART | ir_switch | 1941 | 1.29 | 5.81 | 23.0 | 34.2 | 38.6 | 34.1 | 0.43 | 5.9 | 0.04 | 0.00 |

Findings:

1. **T20, all modes: 2-3 x the configured bit rate in static scenes, still
   1.3-1.6 x with motion** (VBV fill 20-115 s).  Cause in the controller
   (`RC_H264_calcPFrameQp`, docs/T20_RC.md): the P target is half "remaining
   GOP budget / pictures left" and half the nominal bit rate / fps, and once
   the GOP budget is spent - always, after a large I picture - it is the
   nominal value alone; the I QP follows the P QP average.  So the I picture
   is never paid back, and with I/P size ratios of 50-150 (static 1080p, as
   recorded) every GOP overshoots by about the I picture.  The overshoot
   shrinks with the I/P ratio (op D 1.4-1.7 x) and grows with shorter GOPs
   (op E 4 x).
2. **T10 CBR/VBR: same structure, 1.1-1.7 x**, and **T10 VBR codes 92-100 %
   of all pictures twice** (super-frame thresholds compared as bits/1024,
   docs/T20_RC.md "T10"): double encoder load for no measurable rate or
   quality effect (see P1).
3. **T21/T23 eprc** holds the rate (0.86-0.95 outside bursts, VBR/SMART near changePos) but
   **oscillates in static scenes**: the P QP walks down by up to frmQPStep
   per picture into the noise cliff, a picture becomes 10-50 x larger, the
   QP jumps back (sawtooth, 18-25 jumps of >= 2 QP per 100 pictures, as in
   the recordings).  Bits go into re-coding sensor noise, and the QP
   flickers.  Motion bursts at max QP 45 overflow any controller (model:
   the burst needs more than the bit rate at QP 45).
4. T10 SMART: I QP 6-11 above the P QP (strong I-picture pulsing) in
   static scenes.  No proposal below fixes this without side effects
   (`ismooth` destabilises SMART, see rejected).

## Proposals (ranked)

| # | SoC / modes | change | measured gain (op A, 6 scenes x 3 seeds) | risk | switch (proposed, default off) |
|---|---|---|---|---|---|
| P1 | T10 VBR | super-frame thresholds compared in bits (E+88/E+92 = superI/superP bits, as the T20 controller reads them) instead of bits/1024 | re-encodes 0.97 -> 0.00 per picture (encoder load / 2), rate and QP unchanged (+-3 %, all ops) | low: only the emergency re-encode path; a real super frame (> 19.6 / 14 Mbit) still re-encodes | `OPENIMP_T10_RC_SUPERFRM=1` (write E+88/E+92 after `RCT10_Init`) |
| P2 | T20 CBR/VBR/SMART | I-aware P budget: per-picture nominal = (nominal x GOP - bits of the last I) / (GOP-1), floor nominal/8 | rate 2.0-2.4 x -> 1.05-1.26 x, VBV 61-84 s -> 8-18 s, ovr 4-28 -> 0.4-0.7 s/min; QP +2.8..3.2 (the price of meeting the rate); 17-18 of 18 runs win in every op except 360p CBR (10 win / 8 flagged for dQP +0.3) | low-medium: one line in `calcPFrameQp` behind a flag (port stays bit-exact when off); camera check of the real I/P ratio needed | `OPENIMP_T20_RC_IAWARE=1` (hook in `RCT20_CalcPFrameQp`) |
| P3 | T10 CBR, T10 VBR | P2 on the T10 controller (`calc_pframe_qp`, rcSt+116); for VBR together with P1 (`t10_vbr_fix`) | CBR 1.58 -> 1.13 x, VBV 37 -> 11 s; VBR 1.43 -> 0.91 x (below maxBitRate), VBV 29 -> 7 s, re-encodes 0; QP +1.5 / +2.5 | low-medium (as P2); not for T10 SMART (mixed: C 6/8/4, E 13/0/5) | `OPENIMP_T10_RC_IAWARE=1` |
| P4 | T21/T23 VBR/SMART | `eprc_stable`: P QP may fall by at most 1 per picture; "noise-cliff floor": when a P picture coded lower than the one before is > 3 x as large but still < 2 x the per-picture budget, its QP + 1 is a floor until the next IDR (dropped on real motion); fast attack: a P picture > 3 x budget and > 2.5 x the recent median raises the next QP by up to 6 at once | flicker dQP 1.33 -> 0.36, QP up-jumps 22.7 -> 1.0 / 100 pictures, 9 % fewer bits at 0.5 lower mean QP, burst VBV 10.0 -> 5.9 s; 15/18 win, 0 regressions in A, B, C | medium: QP override after `EPRC_FrameStart` needs the slice fields (QP window, lambda) rewritten consistently (an `EPRC_SetPictureQp()` helper) and E+1600 written back; heuristics thresholds tuned on the model | `OPENIMP_T23_EPRC_SMOOTH=1` (also T21 with `OPENIMP_T21_EPRC=1`) |
| P5 | T21/T23 CBR | `eprc_down1` only (QP falls by at most 1 per picture) | dQP -8..27 %, QP -0.1..-0.4 (GOP 15: +0.1) at equal rate, 0 regressions in 90 runs | low | `OPENIMP_T23_EPRC_SMOOTH=1` in CBR = down-step only |
| P6 | T20 (T10 CBR) on top of P2/P3 | `ibudget`: I QP >= last I QP + 7 log2(last I bits / (0.5 x GOP budget)) | op A: 1 s peak 4.2 -> 3.2, QP 34.9 -> 34.2; GOP 15: rate 2.2 -> 1.0 x; but I pulse 2 -> 4-10 QP | medium: visible I-picture pulsing; only where the link is a hard limit | `OPENIMP_T20_RC_IBUDGET=<share %>` |

Notes:
- P4 in CBR also wins on quality (QP -0.5, flicker / 3.6) but undershoots
  (rate 0.97 -> 0.89), which the CBR verdict counts as a regression; hence
  P5 for CBR.
- frmQPStep = 1 (no code, an existing parameter) also removes part of the
  flicker but slows the reaction to bursts (burst VBV 10.4 -> 13.9 s); the
  asymmetric down-step of P4/P5 does not.
- P2/P3 trade QP for rate: the vendor's lower QP on T20/T10 comes from
  sending 1.5-3 x the configured bit rate.  Whether that is wanted is the
  user's call (bit rate is a hard limit on Wi-Fi / SD recording; a user who
  likes the vendor picture can raise the bit rate instead).

### Rejected (measured, no clear win)

| variant | result |
|---|---|
| `eprc_cliff` (floor without the down-step limit) | CBR, ops A-D: 0-6 wins / 9-15 regressions (QP +1.9..2.4 at 17-30 % fewer bits: the floor is too sticky) |
| `attack` alone | T23 neutral (0-4 wins / 0-2 regressions); T20/T10 small gains, superseded by P2/P3 |
| `ismooth` (I QP within mean P QP -3..+1) | T23/T20/T10 CBR neutral; T10 SMART runaway (rate x3 in ops A, B, E): the SMART GOP budget follows the I QP |
| `t10_superbudget` (super frames relative to the bit rate) | keeps 50-80 % re-encodes at night; P1 is simpler and better |

## Robustness tables

`rc_sim.py robust`: mean over 6 scenes x 3 seeds per operating point,
vendor -> variant, and the verdict counts (+ / = / -).

#### t10_superfix

| SoC | mode | op | rate v->n | sec_peak v->n | vbv v->n | qp v->n | dqp v->n | pulse v->n | reenc v->n | ovr v->n | + / = / - |
|---|---|---|---|---|---|---|---|---|---|---|---|
| T10 | VBR | A 1080p15 1.5M g50 | 1.43->1.43 | 4.33->4.36 | 28.58->28.51 | 34.0->33.9 | 0.35->0.33 | 1.2->1.0 | 0.97->0.00 | 4.7->4.6 | 15 / 0 / 3 |
| T10 | VBR | B 1080p25 2.5M g50 | 1.50->1.54 | 3.32->3.21 | 31.57->33.13 | 33.6->33.4 | 0.31->0.31 | 0.9->0.8 | 0.98->0.00 | 4.9->5.1 | 15 / 0 / 3 |
| T10 | VBR | C 360p15 384k g50 | 1.00->1.05 | 2.63->2.77 | 7.70->8.30 | 31.6->31.3 | 0.35->0.33 | 1.3->1.1 | 0.91->0.00 | 0.8->0.9 | 12 / 0 / 6 |
| T10 | VBR | D A, low texture | 1.29->1.32 | 2.81->2.86 | 21.84->22.83 | 33.3->33.1 | 0.38->0.37 | 1.3->1.2 | 0.97->0.00 | 5.4->5.3 | 12 / 0 / 6 |
| T10 | VBR | E A, GOP 15 | 2.99->3.00 | 4.92->4.76 | 120.35->120.67 | 33.3->33.2 | 0.33->0.33 | 0.4->0.4 | 0.97->0.00 | 0.0->0.0 | 18 / 0 / 0 |

#### iaware

| SoC | mode | op | rate v->n | sec_peak v->n | vbv v->n | qp v->n | dqp v->n | pulse v->n | reenc v->n | ovr v->n | + / = / - |
|---|---|---|---|---|---|---|---|---|---|---|---|
| T20 | CBR | A 1080p15 1.5M g50 | 2.38->1.26 | 6.75->4.22 | 84.08->17.92 | 31.9->34.9 | 0.80->0.75 | 2.0->2.0 | 0.00->0.00 | 27.5->0.7 | 18 / 0 / 0 |
| T20 | CBR | B 1080p25 2.5M g50 | 2.47->1.28 | 5.01->3.13 | 88.12->17.71 | 31.7->34.9 | 0.84->0.77 | 2.0->2.0 | 0.00->0.00 | 21.8->0.7 | 18 / 0 / 0 |
| T20 | CBR | C 360p15 384k g50 | 2.19->1.29 | 7.10->3.67 | 71.57->18.42 | 24.2->30.6 | 0.52->0.80 | 1.2->2.0 | 0.00->0.00 | 9.6->4.1 | 10 / 0 / 8 |
| T20 | CBR | D A, low texture | 1.72->1.44 | 3.61->3.05 | 44.70->27.94 | 32.0->32.7 | 0.78->0.79 | 2.0->2.0 | 0.00->0.00 | 25.9->10.7 | 17 / 1 / 0 |
| T20 | CBR | E A, GOP 15 | 4.37->2.22 | 7.00->4.07 | 202.50->73.79 | 31.6->36.8 | 0.85->0.78 | 2.0->2.0 | 0.00->0.00 | 0.0->0.0 | 18 / 0 / 0 |
| T20 | VBR | A 1080p15 1.5M g50 | 2.00->1.15 | 5.92->4.05 | 61.10->11.33 | 32.6->35.4 | 0.77->0.78 | 2.0->2.0 | 0.00->0.00 | 4.4->0.7 | 17 / 1 / 0 |
| T20 | VBR | B 1080p25 2.5M g50 | 2.04->1.17 | 4.21->3.12 | 62.61->10.81 | 32.5->35.4 | 0.79->0.80 | 2.0->2.0 | 0.00->0.00 | 2.7->0.7 | 18 / 0 / 0 |
| T20 | VBR | C 360p15 384k g50 | 1.77->1.02 | 5.59->3.01 | 46.95->4.49 | 27.2->31.7 | 0.73->0.76 | 1.9->2.0 | 0.00->0.00 | 1.6->0.4 | 18 / 0 / 0 |
| T20 | VBR | D A, low texture | 1.39->1.14 | 3.03->2.64 | 24.71->10.48 | 32.8->33.4 | 0.76->0.77 | 2.0->2.0 | 0.00->0.00 | 2.8->1.4 | 13 / 5 / 0 |
| T20 | VBR | E A, GOP 15 | 3.97->2.07 | 6.35->3.91 | 178.58->65.02 | 32.2->37.1 | 0.87->0.83 | 2.0->2.0 | 0.00->0.00 | 0.0->0.0 | 18 / 0 / 0 |
| T20 | SMART | A 1080p15 1.5M g50 | 1.99->1.05 | 5.87->3.83 | 60.79->7.58 | 32.8->36.0 | 0.79->0.79 | 2.0->2.1 | 0.00->0.00 | 6.7->0.4 | 18 / 0 / 0 |
| T20 | SMART | B 1080p25 2.5M g50 | 2.02->1.04 | 4.15->2.59 | 61.18->6.36 | 32.7->36.0 | 0.80->0.76 | 2.0->2.0 | 0.00->0.00 | 3.4->0.7 | 18 / 0 / 0 |
| T20 | SMART | C 360p15 384k g50 | 1.77->1.01 | 5.59->2.74 | 47.88->5.73 | 27.3->31.9 | 0.76->0.80 | 1.9->2.0 | 0.00->0.00 | 2.8->0.3 | 18 / 0 / 0 |
| T20 | SMART | D A, low texture | 1.41->1.12 | 3.10->2.55 | 27.77->11.06 | 33.0->33.7 | 0.77->0.80 | 2.0->2.0 | 0.00->0.00 | 6.7->1.2 | 18 / 0 / 0 |
| T20 | SMART | E A, GOP 15 | 3.93->1.94 | 6.32->3.54 | 175.81->58.49 | 32.4->37.8 | 0.86->0.76 | 2.0->2.0 | 0.00->0.00 | 0.0->0.0 | 18 / 0 / 0 |
| T10 | CBR | A 1080p15 1.5M g50 | 1.58->1.13 | 4.62->3.87 | 36.60->10.67 | 33.4->34.9 | 0.32->0.44 | 0.9->1.1 | 0.00->0.00 | 5.3->0.3 | 18 / 0 / 0 |
| T10 | CBR | B 1080p25 2.5M g50 | 1.61->1.14 | 3.22->2.55 | 37.63->9.55 | 33.1->34.8 | 0.31->0.44 | 0.7->0.9 | 0.00->0.00 | 5.4->0.9 | 18 / 0 / 0 |
| T10 | CBR | C 360p15 384k g50 | 1.23->1.08 | 3.09->2.81 | 16.29->7.36 | 30.3->31.2 | 0.29->0.42 | 1.0->1.0 | 0.00->0.00 | 0.9->0.5 | 16 / 2 / 0 |
| T10 | CBR | D A, low texture | 1.39->1.31 | 3.02->2.86 | 25.21->20.75 | 32.6->32.9 | 0.32->0.40 | 1.1->1.0 | 0.00->0.00 | 6.1->4.3 | 12 / 5 / 1 |
| T10 | CBR | E A, GOP 15 | 3.06->1.65 | 4.91->2.93 | 123.84->40.28 | 33.1->37.5 | 0.33->0.44 | 0.3->0.6 | 0.00->0.00 | 0.0->0.0 | 18 / 0 / 0 |
| T10 | VBR | A 1080p15 1.5M g50 | 1.43->0.88 | 4.33->2.78 | 28.58->6.40 | 34.0->36.6 | 0.35->0.39 | 1.2->1.0 | 0.97->0.94 | 4.7->0.5 | 18 / 0 / 0 |
| T10 | VBR | B 1080p25 2.5M g50 | 1.50->0.92 | 3.32->2.41 | 31.57->6.18 | 33.6->36.1 | 0.31->0.39 | 0.9->1.1 | 0.98->0.96 | 4.9->0.5 | 18 / 0 / 0 |
| T10 | VBR | C 360p15 384k g50 | 1.00->0.78 | 2.63->2.28 | 7.70->4.45 | 31.6->33.3 | 0.35->0.54 | 1.3->1.9 | 0.91->0.73 | 0.8->0.2 | 11 / 2 / 5 |
| T10 | VBR | D A, low texture | 1.29->0.99 | 2.81->2.50 | 21.84->7.09 | 33.3->34.2 | 0.38->0.42 | 1.3->1.4 | 0.97->0.97 | 5.4->2.6 | 15 / 3 / 0 |
| T10 | VBR | E A, GOP 15 | 2.99->1.57 | 4.92->2.89 | 120.35->36.44 | 33.3->38.0 | 0.33->0.50 | 0.4->0.8 | 0.97->0.88 | 0.0->0.0 | 15 / 0 / 3 |
| T10 | SMART | A 1080p15 1.5M g50 | 1.02->0.77 | 4.38->3.77 | 15.41->5.34 | 36.0->39.6 | 0.46->0.43 | 6.3->2.9 | 0.00->0.00 | 3.8->0.6 | 15 / 2 / 1 |
| T10 | SMART | B 1080p25 2.5M g50 | 1.02->0.75 | 3.44->2.77 | 15.07->5.02 | 35.8->40.0 | 0.44->0.39 | 6.4->2.8 | 0.00->0.00 | 3.8->1.0 | 15 / 3 / 0 |
| T10 | SMART | C 360p15 384k g50 | 0.78->0.65 | 2.32->2.26 | 5.64->2.23 | 33.3->36.3 | 0.38->0.54 | 2.3->3.1 | 0.00->0.00 | 0.6->0.3 | 6 / 8 / 4 |
| T10 | SMART | D A, low texture | 1.16->0.92 | 2.66->2.48 | 11.88->3.83 | 33.4->34.1 | 0.39->0.39 | 0.9->1.1 | 0.00->0.00 | 3.8->0.9 | 16 / 2 / 0 |
| T10 | SMART | E A, GOP 15 | 4.92->2.42 | 10.34->8.07 | 236.18->90.31 | 32.0->35.8 | 0.75->0.91 | 8.4->8.3 | 0.00->0.00 | 0.0->0.0 | 13 / 0 / 5 |

#### t10_vbr_fix

| SoC | mode | op | rate v->n | sec_peak v->n | vbv v->n | qp v->n | dqp v->n | pulse v->n | reenc v->n | ovr v->n | + / = / - |
|---|---|---|---|---|---|---|---|---|---|---|---|
| T10 | VBR | A 1080p15 1.5M g50 | 1.43->0.91 | 4.33->2.81 | 28.58->7.04 | 34.0->36.5 | 0.35->0.36 | 1.2->1.0 | 0.97->0.00 | 4.7->0.3 | 18 / 0 / 0 |
| T10 | VBR | B 1080p25 2.5M g50 | 1.50->0.96 | 3.32->2.21 | 31.57->6.26 | 33.6->35.9 | 0.31->0.43 | 0.9->0.9 | 0.98->0.00 | 4.9->0.5 | 18 / 0 / 0 |
| T10 | VBR | C 360p15 384k g50 | 1.00->0.79 | 2.63->2.41 | 7.70->4.93 | 31.6->33.2 | 0.35->0.45 | 1.3->1.4 | 0.91->0.00 | 0.8->0.2 | 17 / 0 / 1 |
| T10 | VBR | D A, low texture | 1.29->1.03 | 2.81->2.49 | 21.84->7.75 | 33.3->34.0 | 0.38->0.41 | 1.3->1.2 | 0.97->0.00 | 5.4->2.1 | 18 / 0 / 0 |
| T10 | VBR | E A, GOP 15 | 2.99->1.61 | 4.92->2.87 | 120.35->38.49 | 33.3->37.7 | 0.33->0.45 | 0.4->0.6 | 0.97->0.00 | 0.0->0.0 | 17 / 0 / 1 |

#### iaware_ibudget

| SoC | mode | op | rate v->n | sec_peak v->n | vbv v->n | qp v->n | dqp v->n | pulse v->n | reenc v->n | ovr v->n | + / = / - |
|---|---|---|---|---|---|---|---|---|---|---|---|
| T20 | CBR | A 1080p15 1.5M g50 | 2.38->1.26 | 6.75->3.21 | 84.08->17.21 | 31.9->34.2 | 0.80->0.81 | 2.0->4.2 | 0.00->0.00 | 27.5->0.9 | 18 / 0 / 0 |
| T20 | CBR | B 1080p25 2.5M g50 | 2.47->1.28 | 5.01->2.56 | 88.12->17.28 | 31.7->34.2 | 0.84->0.81 | 2.0->4.1 | 0.00->0.00 | 21.8->0.7 | 18 / 0 / 0 |
| T20 | CBR | C 360p15 384k g50 | 2.19->1.28 | 7.10->3.01 | 71.57->17.68 | 24.2->30.4 | 0.52->0.76 | 1.2->1.5 | 0.00->0.00 | 9.6->4.7 | 13 / 0 / 5 |
| T20 | CBR | D A, low texture | 1.72->1.44 | 3.61->3.05 | 44.70->27.94 | 32.0->32.7 | 0.78->0.79 | 2.0->2.0 | 0.00->0.00 | 25.9->10.7 | 17 / 1 / 0 |
| T20 | CBR | E A, GOP 15 | 4.37->1.02 | 7.00->1.51 | 202.50->3.72 | 31.6->38.7 | 0.85->1.19 | 2.0->9.5 | 0.00->0.00 | 0.0->0.0 | 7 / 0 / 11 |
| T20 | VBR | A 1080p15 1.5M g50 | 2.00->1.09 | 5.92->2.75 | 61.10->8.46 | 32.6->34.5 | 0.77->0.76 | 2.0->5.2 | 0.00->0.00 | 4.4->1.1 | 17 / 1 / 0 |
| T20 | VBR | B 1080p25 2.5M g50 | 2.04->1.11 | 4.21->2.28 | 62.61->8.05 | 32.5->34.4 | 0.79->0.78 | 2.0->5.3 | 0.00->0.00 | 2.7->0.7 | 18 / 0 / 0 |
| T20 | VBR | C 360p15 384k g50 | 1.77->1.02 | 5.59->2.52 | 46.95->4.00 | 27.2->31.5 | 0.73->0.74 | 1.9->1.8 | 0.00->0.00 | 1.6->0.6 | 18 / 0 / 0 |
| T20 | VBR | D A, low texture | 1.39->1.14 | 3.03->2.65 | 24.71->10.47 | 32.8->33.4 | 0.76->0.77 | 2.0->2.0 | 0.00->0.00 | 2.8->1.4 | 13 / 5 / 0 |
| T20 | VBR | E A, GOP 15 | 3.97->1.01 | 6.35->1.48 | 178.58->3.30 | 32.2->38.8 | 0.87->1.24 | 2.0->10.1 | 0.00->0.00 | 0.0->0.0 | 6 / 0 / 12 |
| T20 | SMART | A 1080p15 1.5M g50 | 1.99->1.02 | 5.87->2.56 | 60.79->5.88 | 32.8->35.1 | 0.79->0.79 | 2.0->5.0 | 0.00->0.00 | 6.7->0.7 | 18 / 0 / 0 |
| T20 | SMART | B 1080p25 2.5M g50 | 2.02->1.03 | 4.15->2.21 | 61.18->5.65 | 32.7->35.1 | 0.80->0.77 | 2.0->5.1 | 0.00->0.00 | 3.4->0.7 | 18 / 0 / 0 |
| T20 | SMART | C 360p15 384k g50 | 1.77->1.00 | 5.59->2.42 | 47.88->5.10 | 27.3->31.7 | 0.76->0.75 | 1.9->1.8 | 0.00->0.00 | 2.8->0.6 | 18 / 0 / 0 |
| T20 | SMART | D A, low texture | 1.41->1.12 | 3.10->2.53 | 27.77->11.00 | 33.0->33.7 | 0.77->0.80 | 2.0->2.0 | 0.00->0.00 | 6.7->1.2 | 18 / 0 / 0 |
| T20 | SMART | E A, GOP 15 | 3.93->0.88 | 6.32->1.35 | 175.81->1.28 | 32.4->39.7 | 0.86->1.12 | 2.0->9.0 | 0.00->0.00 | 0.0->0.0 | 7 / 0 / 11 |
| T10 | CBR | A 1080p15 1.5M g50 | 1.58->1.11 | 4.62->2.87 | 36.60->8.79 | 33.4->34.6 | 0.32->0.50 | 0.9->3.5 | 0.00->0.00 | 5.3->0.3 | 13 / 0 / 5 |
| T10 | CBR | B 1080p25 2.5M g50 | 1.61->1.13 | 3.22->2.47 | 37.63->9.00 | 33.1->34.4 | 0.31->0.51 | 0.7->3.5 | 0.00->0.00 | 5.4->0.9 | 12 / 0 / 6 |
| T10 | CBR | C 360p15 384k g50 | 1.23->1.08 | 3.09->2.92 | 16.29->7.57 | 30.3->31.2 | 0.29->0.43 | 1.0->1.1 | 0.00->0.00 | 0.9->0.5 | 16 / 2 / 0 |
| T10 | CBR | D A, low texture | 1.39->1.31 | 3.02->2.86 | 25.21->20.75 | 32.6->32.9 | 0.32->0.40 | 1.1->1.0 | 0.00->0.00 | 6.1->4.3 | 12 / 5 / 1 |
| T10 | CBR | E A, GOP 15 | 3.06->1.06 | 4.91->1.61 | 123.84->4.51 | 33.1->38.3 | 0.33->1.23 | 0.3->8.8 | 0.00->0.00 | 0.0->0.0 | 1 / 0 / 17 |

#### eprc_down1

| SoC | mode | op | rate v->n | sec_peak v->n | vbv v->n | qp v->n | dqp v->n | pulse v->n | reenc v->n | ovr v->n | + / = / - |
|---|---|---|---|---|---|---|---|---|---|---|---|
| T23 | CBR | A 1080p15 1.5M g50 | 0.97->0.96 | 3.06->2.90 | 3.55->3.50 | 36.5->36.1 | 1.21->1.04 | 5.2->5.4 | 0.00->0.00 | 0.5->0.4 | 11 / 7 / 0 |
| T23 | CBR | B 1080p25 2.5M g50 | 1.07->1.05 | 2.20->2.21 | 4.87->4.43 | 35.4->35.3 | 1.04->0.96 | 2.8->2.3 | 0.00->0.00 | 0.3->0.3 | 3 / 15 / 0 |
| T23 | CBR | C 360p15 384k g50 | 0.95->0.94 | 2.47->2.49 | 1.73->1.80 | 32.2->32.1 | 1.03->0.95 | 6.6->6.0 | 0.00->0.00 | 0.1->0.2 | 0 / 18 / 0 |
| T23 | CBR | D A, low texture | 0.97->0.96 | 2.36->2.24 | 2.47->2.37 | 34.4->34.2 | 1.18->1.07 | 6.6->6.2 | 0.00->0.00 | 0.4->0.4 | 5 / 13 / 0 |
| T23 | CBR | E A, GOP 15 | 1.23->1.21 | 2.10->2.32 | 14.50->13.49 | 40.9->41.0 | 0.97->0.71 | 2.8->3.0 | 0.00->0.00 | 0.0->0.0 | 9 / 9 / 0 |
| T23 | VBR | A 1080p15 1.5M g50 | 0.91->0.90 | 2.94->2.90 | 3.46->3.49 | 37.1->36.5 | 1.33->1.20 | 2.9->2.5 | 0.00->0.00 | 0.7->0.4 | 11 / 7 / 0 |
| T23 | VBR | B 1080p25 2.5M g50 | 1.00->0.99 | 2.15->2.06 | 3.21->3.01 | 36.6->35.8 | 1.31->1.16 | 2.7->2.2 | 0.00->0.00 | 0.3->0.3 | 11 / 7 / 0 |
| T23 | VBR | C 360p15 384k g50 | 0.79->0.78 | 2.13->2.13 | 1.60->1.61 | 33.0->32.9 | 0.91->0.85 | 3.3->3.2 | 0.00->0.00 | 0.0->0.0 | 0 / 18 / 0 |
| T23 | VBR | D A, low texture | 0.87->0.87 | 2.13->2.15 | 1.99->2.03 | 34.7->34.5 | 1.11->1.04 | 2.7->2.9 | 0.00->0.00 | 0.6->0.6 | 2 / 16 / 0 |
| T23 | VBR | E A, GOP 15 | 1.23->1.20 | 1.92->1.89 | 14.40->13.10 | 41.0->40.8 | 1.18->1.09 | 3.8->3.4 | 0.00->0.00 | 0.0->0.0 | 6 / 12 / 0 |

#### eprc_cliff_down1

| SoC | mode | op | rate v->n | sec_peak v->n | vbv v->n | qp v->n | dqp v->n | pulse v->n | reenc v->n | ovr v->n | + / = / - |
|---|---|---|---|---|---|---|---|---|---|---|---|
| T23 | CBR | A 1080p15 1.5M g50 | 0.97->0.90 | 3.06->3.26 | 3.55->3.81 | 36.5->36.0 | 1.21->0.34 | 5.2->3.3 | 0.00->0.00 | 0.5->0.6 | 2 / 3 / 13 |
| T23 | CBR | B 1080p25 2.5M g50 | 1.07->0.96 | 2.20->2.24 | 4.87->2.46 | 35.4->35.5 | 1.04->0.30 | 2.8->2.4 | 0.00->0.00 | 0.3->0.3 | 12 / 3 / 3 |
| T23 | CBR | C 360p15 384k g50 | 0.95->0.89 | 2.47->2.81 | 1.73->2.05 | 32.2->32.1 | 1.03->0.38 | 6.6->4.8 | 0.00->0.00 | 0.1->0.3 | 4 / 3 / 11 |
| T23 | CBR | D A, low texture | 0.97->0.84 | 2.36->2.74 | 2.47->2.10 | 34.4->34.4 | 1.18->0.40 | 6.6->4.5 | 0.00->0.00 | 0.4->0.9 | 3 / 3 / 12 |
| T23 | CBR | E A, GOP 15 | 1.23->1.21 | 2.10->2.33 | 14.50->13.24 | 40.9->41.0 | 0.97->0.57 | 2.8->3.1 | 0.00->0.00 | 0.0->0.0 | 9 / 9 / 0 |
| T23 | VBR | A 1080p15 1.5M g50 | 0.91->0.83 | 2.94->2.89 | 3.46->2.95 | 37.1->36.6 | 1.33->0.35 | 2.9->2.5 | 0.00->0.00 | 0.7->0.4 | 15 / 3 / 0 |
| T23 | VBR | B 1080p25 2.5M g50 | 1.00->0.89 | 2.15->2.09 | 3.21->2.26 | 36.6->36.1 | 1.31->0.34 | 2.7->2.3 | 0.00->0.00 | 0.3->0.3 | 15 / 3 / 0 |
| T23 | VBR | C 360p15 384k g50 | 0.79->0.75 | 2.13->2.35 | 1.60->1.77 | 33.0->32.9 | 0.91->0.35 | 3.3->3.1 | 0.00->0.00 | 0.0->0.0 | 15 / 3 / 0 |
| T23 | VBR | D A, low texture | 0.87->0.76 | 2.13->2.28 | 1.99->1.68 | 34.7->34.7 | 1.11->0.37 | 2.7->2.7 | 0.00->0.00 | 0.6->0.8 | 15 / 3 / 0 |
| T23 | VBR | E A, GOP 15 | 1.23->1.18 | 1.92->1.87 | 14.40->13.01 | 41.0->40.8 | 1.18->0.86 | 3.8->3.4 | 0.00->0.00 | 0.0->0.0 | 6 / 12 / 0 |

#### eprc_stable

| SoC | mode | op | rate v->n | sec_peak v->n | vbv v->n | qp v->n | dqp v->n | pulse v->n | reenc v->n | ovr v->n | + / = / - |
|---|---|---|---|---|---|---|---|---|---|---|---|
| T23 | CBR | A 1080p15 1.5M g50 | 0.97->0.89 | 3.06->3.03 | 3.55->3.19 | 36.5->36.0 | 1.21->0.34 | 5.2->3.3 | 0.00->0.00 | 0.5->0.4 | 3 / 3 / 12 |
| T23 | CBR | B 1080p25 2.5M g50 | 1.07->0.95 | 2.20->2.15 | 4.87->2.20 | 35.4->35.6 | 1.04->0.29 | 2.8->2.4 | 0.00->0.00 | 0.3->0.3 | 12 / 3 / 3 |
| T23 | CBR | C 360p15 384k g50 | 0.95->0.89 | 2.47->2.66 | 1.73->1.91 | 32.2->32.0 | 1.03->0.38 | 6.6->4.7 | 0.00->0.00 | 0.1->0.0 | 4 / 3 / 11 |
| T23 | CBR | D A, low texture | 0.97->0.81 | 2.36->2.27 | 2.47->1.59 | 34.4->34.3 | 1.18->0.38 | 6.6->4.4 | 0.00->0.00 | 0.4->0.5 | 2 / 3 / 13 |
| T23 | CBR | E A, GOP 15 | 1.23->1.21 | 2.10->2.33 | 14.50->13.24 | 40.9->41.0 | 0.97->0.57 | 2.8->3.1 | 0.00->0.00 | 0.0->0.0 | 9 / 9 / 0 |
| T23 | VBR | A 1080p15 1.5M g50 | 0.91->0.83 | 2.94->2.84 | 3.46->2.79 | 37.1->36.6 | 1.33->0.36 | 2.9->2.5 | 0.00->0.00 | 0.7->0.3 | 15 / 3 / 0 |
| T23 | VBR | B 1080p25 2.5M g50 | 1.00->0.89 | 2.15->2.05 | 3.21->2.11 | 36.6->36.1 | 1.31->0.34 | 2.7->2.3 | 0.00->0.00 | 0.3->0.3 | 15 / 3 / 0 |
| T23 | VBR | C 360p15 384k g50 | 0.79->0.75 | 2.13->2.29 | 1.60->1.71 | 33.0->32.9 | 0.91->0.35 | 3.3->3.1 | 0.00->0.00 | 0.0->0.0 | 15 / 3 / 0 |
| T23 | VBR | D A, low texture | 0.87->0.75 | 2.13->2.09 | 1.99->1.47 | 34.7->34.6 | 1.11->0.36 | 2.7->2.8 | 0.00->0.00 | 0.6->0.5 | 14 / 3 / 1 |
| T23 | VBR | E A, GOP 15 | 1.23->1.18 | 1.92->1.87 | 14.40->13.01 | 41.0->40.8 | 1.18->0.86 | 3.8->3.4 | 0.00->0.00 | 0.0->0.0 | 6 / 12 / 0 |

#### eprc_cliff

| SoC | mode | op | rate v->n | sec_peak v->n | vbv v->n | qp v->n | dqp v->n | pulse v->n | reenc v->n | ovr v->n | + / = / - |
|---|---|---|---|---|---|---|---|---|---|---|---|
| T23 | CBR | A 1080p15 1.5M g50 | 0.97->0.78 | 3.06->3.65 | 3.55->3.08 | 36.5->38.3 | 1.21->0.60 | 5.2->3.3 | 0.00->0.00 | 0.5->0.8 | 1 / 3 / 14 |
| T23 | CBR | B 1080p25 2.5M g50 | 1.07->0.89 | 2.20->2.88 | 4.87->2.48 | 35.4->37.3 | 1.04->0.49 | 2.8->2.9 | 0.00->0.00 | 0.3->0.3 | 6 / 3 / 9 |
| T23 | CBR | C 360p15 384k g50 | 0.95->0.71 | 2.47->2.86 | 1.73->2.18 | 32.2->34.3 | 1.03->0.54 | 6.6->4.4 | 0.00->0.00 | 0.1->1.2 | 0 / 3 / 15 |
| T23 | CBR | D A, low texture | 0.97->0.68 | 2.36->2.67 | 2.47->2.20 | 34.4->36.8 | 1.18->0.64 | 6.6->4.8 | 0.00->0.00 | 0.4->2.0 | 0 / 3 / 15 |
| T23 | CBR | E A, GOP 15 | 1.23->1.14 | 2.10->2.05 | 14.50->9.57 | 40.9->41.3 | 0.97->0.57 | 2.8->2.3 | 0.00->0.00 | 0.0->0.0 | 15 / 3 / 0 |

#### attack

| SoC | mode | op | rate v->n | sec_peak v->n | vbv v->n | qp v->n | dqp v->n | pulse v->n | reenc v->n | ovr v->n | + / = / - |
|---|---|---|---|---|---|---|---|---|---|---|---|
| T23 | VBR | A 1080p15 1.5M g50 | 0.91->0.90 | 2.94->2.84 | 3.46->3.36 | 37.1->37.0 | 1.33->1.38 | 2.9->2.7 | 0.00->0.00 | 0.7->0.6 | 4 / 14 / 0 |
| T23 | VBR | B 1080p25 2.5M g50 | 1.00->1.00 | 2.15->2.06 | 3.21->3.06 | 36.6->36.5 | 1.31->1.39 | 2.7->2.5 | 0.00->0.00 | 0.3->0.3 | 3 / 13 / 2 |
| T23 | VBR | C 360p15 384k g50 | 0.79->0.78 | 2.13->2.13 | 1.60->1.59 | 33.0->33.0 | 0.91->0.91 | 3.3->3.3 | 0.00->0.00 | 0.0->0.0 | 0 / 18 / 0 |
| T23 | VBR | D A, low texture | 0.87->0.86 | 2.13->2.06 | 1.99->1.52 | 34.7->34.7 | 1.11->1.14 | 2.7->2.7 | 0.00->0.00 | 0.6->0.5 | 3 / 15 / 0 |
| T23 | VBR | E A, GOP 15 | 1.23->1.22 | 1.92->1.90 | 14.40->14.02 | 41.0->41.1 | 1.18->1.21 | 3.8->3.7 | 0.00->0.00 | 0.0->0.0 | 0 / 18 / 0 |
| T20 | CBR | A 1080p15 1.5M g50 | 2.38->2.03 | 6.75->5.91 | 84.08->63.03 | 31.9->32.6 | 0.80->0.79 | 2.0->2.1 | 0.00->0.00 | 27.5->2.6 | 15 / 3 / 0 |
| T20 | CBR | B 1080p25 2.5M g50 | 2.47->2.09 | 5.01->4.20 | 88.12->65.13 | 31.7->32.5 | 0.84->0.83 | 2.0->2.1 | 0.00->0.00 | 21.8->1.3 | 15 / 3 / 0 |
| T20 | CBR | C 360p15 384k g50 | 2.19->2.12 | 7.10->6.77 | 71.57->67.57 | 24.2->24.7 | 0.52->0.60 | 1.2->1.3 | 0.00->0.00 | 9.6->2.8 | 6 / 9 / 3 |
| T20 | CBR | D A, low texture | 1.72->1.44 | 3.61->2.96 | 44.70->27.89 | 32.0->32.7 | 0.78->0.80 | 2.0->2.1 | 0.00->0.00 | 25.9->3.2 | 15 / 3 / 0 |
| T20 | CBR | E A, GOP 15 | 4.37->3.97 | 7.00->6.31 | 202.50->178.65 | 31.6->32.3 | 0.85->1.00 | 2.0->2.1 | 0.00->0.00 | 0.0->0.0 | 15 / 3 / 0 |
| T10 | CBR | A 1080p15 1.5M g50 | 1.58->1.53 | 4.62->4.37 | 36.60->33.81 | 33.4->33.5 | 0.32->0.40 | 0.9->1.1 | 0.00->0.00 | 5.3->3.2 | 6 / 11 / 1 |
| T10 | CBR | B 1080p25 2.5M g50 | 1.61->1.58 | 3.22->3.12 | 37.63->35.29 | 33.1->33.3 | 0.31->0.40 | 0.7->0.9 | 0.00->0.00 | 5.4->3.6 | 1 / 15 / 2 |
| T10 | CBR | C 360p15 384k g50 | 1.23->1.22 | 3.09->2.88 | 16.29->15.38 | 30.3->30.4 | 0.29->0.35 | 1.0->1.0 | 0.00->0.00 | 0.9->1.1 | 3 / 14 / 1 |
| T10 | CBR | D A, low texture | 1.39->1.33 | 3.02->2.75 | 25.21->21.82 | 32.6->33.0 | 0.32->0.51 | 1.1->1.3 | 0.00->0.00 | 6.1->4.3 | 4 / 11 / 3 |
| T10 | CBR | E A, GOP 15 | 3.06->3.00 | 4.91->4.66 | 123.84->120.44 | 33.1->33.3 | 0.33->0.40 | 0.3->0.4 | 0.00->0.00 | 0.0->0.0 | 6 / 11 / 1 |

#### ismooth

| SoC | mode | op | rate v->n | sec_peak v->n | vbv v->n | qp v->n | dqp v->n | pulse v->n | reenc v->n | ovr v->n | + / = / - |
|---|---|---|---|---|---|---|---|---|---|---|---|
| T23 | CBR | A 1080p15 1.5M g50 | 0.97->0.97 | 3.06->3.05 | 3.55->3.64 | 36.5->36.5 | 1.21->1.21 | 5.2->5.4 | 0.00->0.00 | 0.5->0.5 | 0 / 18 / 0 |
| T23 | CBR | B 1080p25 2.5M g50 | 1.07->1.07 | 2.20->2.22 | 4.87->4.82 | 35.4->35.4 | 1.04->1.04 | 2.8->2.7 | 0.00->0.00 | 0.3->0.3 | 0 / 18 / 0 |
| T23 | CBR | C 360p15 384k g50 | 0.95->0.95 | 2.47->2.48 | 1.73->1.74 | 32.2->32.2 | 1.03->1.02 | 6.6->6.5 | 0.00->0.00 | 0.1->0.1 | 0 / 18 / 0 |
| T23 | CBR | D A, low texture | 0.97->0.97 | 2.36->2.40 | 2.47->2.40 | 34.4->34.4 | 1.18->1.18 | 6.6->6.4 | 0.00->0.00 | 0.4->0.7 | 0 / 18 / 0 |
| T23 | CBR | E A, GOP 15 | 1.23->1.25 | 2.10->2.31 | 14.50->15.76 | 40.9->40.9 | 0.97->0.98 | 2.8->2.8 | 0.00->0.00 | 0.0->0.0 | 0 / 15 / 3 |
| T10 | SMART | A 1080p15 1.5M g50 | 1.02->3.28 | 4.38->12.06 | 15.41->139.21 | 36.0->28.8 | 0.46->0.38 | 6.3->2.4 | 0.00->0.00 | 3.8->20.8 | 0 / 3 / 15 |
| T10 | SMART | B 1080p25 2.5M g50 | 1.02->3.47 | 3.44->10.24 | 15.07->149.85 | 35.8->28.5 | 0.44->0.35 | 6.4->2.0 | 0.00->0.00 | 3.8->14.6 | 0 / 3 / 15 |
| T10 | SMART | C 360p15 384k g50 | 0.78->0.78 | 2.32->2.22 | 5.64->5.28 | 33.3->33.1 | 0.38->0.39 | 2.3->1.6 | 0.00->0.00 | 0.6->0.3 | 6 / 11 / 1 |
| T10 | SMART | D A, low texture | 1.16->1.15 | 2.66->2.63 | 11.88->11.22 | 33.4->33.4 | 0.39->0.39 | 0.9->1.2 | 0.00->0.00 | 3.8->3.4 | 1 / 17 / 0 |
| T10 | SMART | E A, GOP 15 | 4.92->13.42 | 10.34->21.60 | 236.18->745.82 | 32.0->20.5 | 0.75->0.22 | 8.4->0.9 | 0.00->0.00 | 0.0->0.0 | 0 / 6 / 12 |

## Not covered / next steps

- T31 (Allegro core, CappedVBR/CappedQuality): the vendor controller runs
  only under the unicorn emulator (`openimp-cq` `tools/t31_rc_emu`); once its
  port lands it can be added to `rcsim.c` like the others.
- Camera validation of the model (I/P ratio of the static scenes on T20/T10
  sensors) before switching anything on; the test for each switch is the
  one in docs/T23_EPRC.md / docs/T20_RC.md (60 s static + motion, ffprobe
  sizes and slice QPs, A/B against the vendor stack), plus the re-encode
  count in the `OPENIMP_T20_RC_STATS` line for P1.
- Macroblock-level rate control (T20 MB RC, eprc `h264_api_enc`) is not
  modelled; none of the proposals touches it.
