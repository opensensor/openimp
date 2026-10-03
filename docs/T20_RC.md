# T20 rate control (OEM libimp 3.12.0)

The T20 build of the OEM libimp (3.12.0, also used on the T10) contains two
picture rate controllers.  `i264e_ratecontrol_init` (0x3ab38) selects one by
the i264e parameter `[0]`, which `channel_i264e_encoder_init` (0x40250) sets
from `get_cpu_id` (0x111ac): CPU id 0..2 (SoC id 0x0005, **T10**) -> 1,
CPU id 3..5 (SoC id 0x2000, **T20**) -> 2.

| | T20 (`[0]` = 2) | T10 (`[0]` = 1) |
|---|---|---|
| configure | `eprc_default_set_t20` 0x3a9e4, `JZ_VPU_RC_VIDEO_CFG_T20` 0xa8fb4 | `JZ_VPU_RC_VIDEO_CFG` 0xa2484 |
| per picture | `JZ_VPU_RC_FRAME_RC_T20` 0xaa20c | `JZ_VPU_RC_FRAME_RC` 0xa2934 |
| after coding | `JZ_VPU_RC_FRAME_REPEATE_JUDGE_T20` 0xab0c0 | `JZ_VPU_RC_FRAME_REPEATE_JUDGE` 0xa2b2c |
| helpers | static `RC_H264_*`, `RDModelEstimator`, `MADModelEstimator`, `H264_SMA_*`, `JZM_QPTabConv` 0xa2cb0..0xab660 | exported `RC_H264_*` 0x9eec0..0xa2b2c |
| block | i264e param + 704 | i264e param + 476 |

This document describes the **T20** controller and OpenIMP's
reimplementation `src/rc_t20/` (platform neutral, libc/libm only).  The T10
controller is a different (older) code base, reimplemented in `src/rc_t10/`;
see "T10" below.

The controller is a JM-style quadratic R-Q model (JM `rc_quadratic.c`:
`RCModelEstimator`, `MADModelEstimator`, `updateRCModel`), with a GOP bit
budget, a scene classifier fed by VPU statistics, a "NewMaxQp" bit-rate
guard, super-frame re-encoding and, for SMART, a GOP budget between the
quality-level minimum and the maximum bit rate.  It is unrelated to the
T21/T23 "eprc" controller (`docs/T23_EPRC.md`).

## Verification

`src/rc_t20` is bit-exact with the OEM code:

- under the unicorn emulator, end to end through the OEM i264e glue
  (`i264e_ratecontrol_init` / `_start` / `_is_reenc` on a synthetic i264e
  handle) against `RCT20_Init` / `RCT20_Start` / `RCT20_End` of a mipsel
  build: 100 random CBR/VBR/SMART/FIXQP scenarios x 150 pictures with
  random picture sizes and statistics, the E, rcPara and rcSt blocks
  identical after every call (including super-frame re-encodes);
- the macroblock rate control the same way, on synthetic luma pictures
  (48 scenarios x 60 pictures; `H264_SMA_CalMBFlag` uses Ingenic MXU2 SIMD
  instructions, which the emulator executes in a code hook: subua.b,
  dotpu.h/.w/.d, adduu.h, li.b/.h/.w, lu1q(x), su1q(x)), plus function
  level `H264_SMA_CalMBFlag`, `H264_SMA_CalMBQP` and `JZM_QPTabConv` on
  random inputs;
- on the host: `tests/rc_t20` (in `make check`) replays
  `tests/rc_t20/rc_t20_vectors.txt` (16 scenarios, 1920 pictures, 4347
  decisions incl. re-encodes and 481 macroblock QP tables) from
  `tools/rc_t20_oracle.py`.

libm: the OEM calls `logf`, `log2f`, `log`, `pow`, `sqrt`.  uClibc's
`logf`/`log2f` are `(float)log((double)x)` / `(float)log2((double)x)`
(libuClibc-1.0.58 0x6e490 / 0x6e510); `src/rc_t20` writes them that way,
so on the camera both use the same uClibc functions.  Compiled with
`-ffp-contract=off` (the emulator models MIPS32r2 `madd.fmt` as unfused).

Not reproduced: ROI regions in the macroblock QP map, the debug switches
(files /tmp/smac0, /tmp/smac1, /tmp/smad, /tmp/roic, /tmp/rcdbg, "smooth"),
the `show` logging.

## Blocks

`E` = i264e param + 704 (0x90190 bytes in OpenIMP's `RCT20_E_SIZE`),
`P` = rcPara (128 bytes, malloc'd, pointer at E+0x90178),
`S` = rcSt (872 bytes, pointer at E+0x9017c).

### E (configuration, i264e_ratecontrol_init 0x3b334..0x3c70c)

| E | field (OEM log names) | source |
|---|---|---|
| +0 | rcMode: 0 CBR, 1 VBR, 2 FIXQP, 3 SMART | i264e method [188]: 1, 2, 0, 3 |
| +4/+8/+9/+10 | Fixqp u32Gop, u8Fps, u8IQp, u8PQp | gop, fps, qp - 3, qp |
| +12/+16/+20 | Cbr u8Fps, u32Gop, u32Bitrate | fps, gop, outBitRate [216] |
| +24/+28/+32/+36 | Vbr u8Fps, u32Gop, u32StatTime, u32MaxBitrate | fps, gop, staticTime [244], maxBitRate [248] |
| +40/+44/+48/+52 | Smart (same) | same |
| +56.. | paramH264Cbr: MaxQp, MinQp, GopQpStep, FrmQpStep, s8IQpBias, bGopRelat, NewMaxQpThr (+62), f NewMaxQpTrigLvl (+64), UseNewQpMaxAccumThr (+68), SuperI/PFrmBitsThr (+72/+76, kbit), s8IPQpDlt (+82), SkipFrmGaps, SuperFrmReencTimes (3), ReencTimes (2), f BeyondBpsThr (+88, 1.2), superFrmMode (+92, 2), bitRateUpMode (+96, 0) | [200] [196] [236] [232] [228] [241] [264] [260] 2 x fps [280]/1024 [284]/1024 |
| +104.. | paramH264Vbr: MinBitrate (0), MaxQp, MinQp, NewMaxQpThr, f NewMaxQpTrigLvl, AccumThr, IQpBias (+120), FrmQpStep, GopQpStep, ChangePos (+123), bGopRelat, SuperFrmReencTimes, ReencTimes, BeyondBpsThr, Drop/Pskip/SkipFrmGaps, SuperI/P (+136/+140), IPQpDlt (+146), QualityLvl (+147), SuperFrmMode (+148), BitRateUpMode (+152) | as CBR, changePos [252], qualityLvl [256] |
| +160.. | paramH264Smart: as VBR at +56 offset | same |
| +212 | VPUCoreNum (1) | |
| +213 | bIFrmReq: picture is IDR | per picture |
| +215/+216 | u8MbRCEn / second MB flag | [268]/[272] (i264e default 1/1) |
| +217 | out: picture QP | |
| +220/+224 | luma plane address / stride | per picture (fenc +76/+100) |
| +228/+232 | macroblock columns / rows | |
| +236 | out: picture type (1 I, 3 P) | |
| +240 | VPU complexity of the last picture (`cmpx`, channel node +44) | per P picture |
| +244 | bits of the last picture | |
| +252 | 0x800e4: sum of its two 15-bit halves | per P picture |
| +256 | (0x800ec & 0x7ffffff) + (0x800e8 & 0x7ffffff) | per P picture |
| +260/+264/+268 | 6, 32, 64 (scene thresholds) | default set |
| +272..+316 | macroblock activity thresholds 200,350,500,1500,2500,4500 / 180,310,450,1200,2200,4200 | default set and every picture ("smooth" file overrides) |
| +336.. | macroblock class map (int per MB) | CalMBFlag |
| +0x40150.. | macroblock centre luma sample (byte per MB) | CalMBFlag |
| +0x50150..+0x50156 | class QP offsets | CalMBQP |
| +0x50158.. | hardware QP table (256 KiB) | JZM_QPTabConv |
| +0x90158/+0x9015c | QP table size / MB RC active for this picture | -> i264e io +76/+80 |
| +0x90160 | mean macroblock QP | |
| +0x90168/+0x9016c | scene class S+176 / previous S+184 | -> i264e io +84 |
| +0x90170 | ROI table pointer (8 x 7 bytes) | |
| +0x90180 | GOP mode flag (0 normal; HSkip modes 1/2) | |
| +0x90181 | SMART re-encode flag | |
| +0x90182..+0x90188 | 7 macroblock tuning bytes (SMART: 0 or 1,15,15,15,1,0,0) | -> i264e io +172..+178 |

The i264e parameters come from `channel_i264e_encoder_init` (0x40250):
CBR `[200]`=maxQp, `[196]`=minQp, `[216]`=outBitRate, `[228]`=iBiasLvl,
`[232]`=frmQPStep, `[236]`=gopQPStep, `[240]`=adaptiveMode,
`[241]`=gopRelation; VBR/SMART `[200]`, `[196]`, `[244]`=staticTime,
`[248]`=maxBitRate, `[228]`, `[252]`=changePos, `[256]`=qualityLvl,
`[232]`, `[236]`, `[241]`; FIXQP `[192]`=qp.  `[260]` 3.0, `[264]` 51,
`[268]`/`[272]` 1, `[280]` 19660800, `[284]` 14043429 are
`i264e_param_default` (0x2ff10) values no IMP call changes.

### P (rcPara, VIDEO_CFG)

+0 fps, +4 gop, +8 IQpBias, +12 IPQpDlt, +16 bGopRelat, +20 frame QP
step, +24 GOP QP step, +28/+32 max/min QP, +36 bIFrmReq, +40..+52 stride,
MB columns/rows, luma, +56 VPUCoreNum (byte), +60 NewMaxQp, +64
NewMaxQpTrigLvl, +68 NewMaxQp duration, +72 NewMaxQp trigger rate,
+76 superFrmMode, +80 SuperFrmReencTimes (byte), +84/+88 super I/P bits,
+92 bitRateUpMode, +96 ReencTimes (byte), +100 rcMode, +104/+105 FIXQP I/P
QP, +108 target bit rate (bit/s; CBR bitrate x 1024, VBR/SMART maxBitRate
x changePos % x 1024), +112 min bit rate, +116 max bit rate (x 1024),
+120 SkipFrmGaps, +124 quality level (0..7).

### S (rcSt) - selected fields

+0 pictures, +4 GOPs, +12 picture in GOP, +24/+28 type now/before, +32 QP,
+36 QP of the last I, +44 last P QP, +48 mean P QP of the GOP, +56 mean of
the last 5 P QPs, +60..+72 QP window (frame step, GOP step), +76/+80/+84
NewMaxQp state, +88 P QP sum, +104..+120 P QP history, +124 bits, +128
bits per picture, +132 GOP bits so far, +152/+156/+160 SMART/VBR GOP
budget max / min / target, +172 MAD (cmpx >> 8), +176/+184 scene class,
+188..+204 VPU statistics, +208..+220 budget deviation, +224.. MAD model
window (rejected flags, picture MAD +308.., reference MAD +392..), +476/+480
MAD model C1/C2, +488.. R-Q window (rejected, Rp +572.., Qstep +656..),
+740/+744 R-Q model X1/X2, +752/+756 re-encode counters, +776..+860 VBR
long-term history, +864 macroblock RC enabled.

## Per picture

`i264e_ratecontrol_start` (0x3caa8), T20 branch: thresholds E+272..316,
E+215/216 from [268]/[272], luma/stride, MB size, E+213 = (frame % gop == 0);
for a P picture only, the statistics of the previous picture into
E+240/244/252/256 (an IDR keeps the older values); E+0x90180 = GOP mode;
`JZ_VPU_RC_FRAME_RC_T20`; QP = E+217.

`i264e_ratecontrol_end` (0x3d1c0) after each coding: bits = slice bytes x 8,
`cmpx` = soc_vpu channel node +44 (hwicodec_pf_h264e_t20_enc 0x2433c), the
three registers 0x13200000 + {0x800e4, 0x800e8, 0x800ec} (list built by
`i264e_ratecontrol_priv_init` 0x3a890, read by soc_vpu ioctl 0xc0386307).
`i264e_ratecontrol_is_reenc` (0x3d300): E+244 = bits,
`JZ_VPU_RC_FRAME_REPEATE_JUDGE_T20`; > 0: code the picture again at QP
E+217.

FIXQP (method 0) does not run the T20 controller per picture: P pictures
use qp, the first picture and IDRs `clip3(0, 51, (int)(qp - 6 x
logf(1.4)/ln 2 + 0.5))` = qp - 3 (0x3cf6c).

## Algorithm (JZ_VPU_RC_FRAME_RC_T20 0xaa20c)

1. **Statistics** -> S: MAD = max(cmpx >> 8, 1), bits, the two motion
   sums, the scene thresholds.
2. **GOP bookkeeping** (`RC_H264_CBR_updateGopInfo` 0xa50fc,
   `RC_H264_VBR_updateGopInfo` 0xa5694, SMART inline 0xaa944..0xaadfc):
   QP window S+60/64 = last QP -/+ frame step, after an I picture S+68/72 =
   I QP -/+ gopQPStep; GOP budget S+216, deviation S+208; P QP history;
   scene class (`RC_H264_CheckScene` 0xa3bb8) from the moving-MB count
   (E+252 vs 5/6 of the MBs) and the mean motion (E+256 / E+252 vs 6 and
   32): it **overwrites the frame QP step P+20** (1, 2, 3 or 5), so the
   application's frmQPStep only applies until the first P-to-P step.
   NewMaxQp (`RC_H264_UpdateMaxQp` 0xa4fc8): while the bit rate is above
   P+72 the QP cap becomes min(estimate, NewMaxQpThr) for up to
   2 x fps pictures.  At a GOP end: mean P QP S+48, the budget of the next
   GOP (CBR: bitrate x gop, carried over with gopRelation; VBR: target from
   a 5-GOP history of QP, bit rate and MAD (0xa5b1c) against the QP class
   table 0xd151c by quality level, clamped to [min, max] budget by scene
   class; SMART: max budget x SmartQualLvl[quality] when static, target or
   max budget otherwise).
3. **Model update** (`RC_H264_updateModelCoeff` 0xa65fc, after a P picture):
   Qstep = 0.85 x 2^((QP-12)/6), Rp = bits / MAD; windows of up to 4;
   `RDModelEstimator` 0xa2cb0 (X1, X2) and `MADModelEstimator` 0xa34f0 (C1,
   C2) with JM outlier rejection.
4. **QP** (`RC_H264_CBR_frames`/`VBR_frames` 0xa6474/0xa6538, SMART
   inline): first I picture `RC_H264_calcFirstIQp` 0xa543c (from bits per
   picture and picture size, + iBiasLvl, or -1/0/+2 by scene class when
   iBiasLvl is 0); later I pictures `RC_H264_calcIFrameQp` 0xa639c (the
   last I QP, or `calcOtherIQp` 0xa62b0: the GOP's mean P QP moved at
   most 2 away from the last I QP, kept below the mean of the last 5 P QPs
   and the last P QP) **+ iBiasLvl** (SMART: no iBiasLvl after the
   first I); the first P after an I: the I QP + IPQpDlt (always 0: no IMP
   call sets it); other P pictures `RC_H264_calcPFrameQp` 0xa4120 (budget
   per picture from the remaining GOP budget and the deviation, then the
   quadratic model: Qstep from X1, X2 and the predicted MAD C1 x MAD + C2,
   QP = 6 log2(Qstep / 0.85) + 12).
5. **Limits** (`RC_H264_QpLimit` 0xa3cc4): P QP within the frame-step and
   GOP-step windows, then the max QP (NewMaxQp when active) and min QP.
6. **Super frame** (`JZ_VPU_RC_FRAME_REPEATE_JUDGE_T20`, superFrmMode 2):
   a picture above the super I/P threshold (P+84/88) at QP < 49 is coded
   again, at most SuperFrmReencTimes (3) times, with QP + updateQp(bits /
   threshold) (<= 51, >= min QP).  With the default thresholds (19.2 /
   13.7 Mbit) this never happens.  With macroblock RC active, a picture at
   QP >= 49 is coded once more without the macroblock table (QP lowered by
   updateQp(bits / threshold), typically -3).

What the RC attribute fields do (T20):

| field | effect |
|---|---|
| maxQp/minQp | final clamp (I and P) |
| outBitRate (CBR) | bits per picture and GOP budget |
| maxBitRate (VBR/SMART) | max GOP budget, NewMaxQp reference |
| changePos | target = maxBitRate x changePos % |
| qualityLvl | min bit rate maxBitRate x {0.8 .. 0.1}; VBR QP class row; SMART static budget |
| staticTime | stored, **not used** |
| iBiasLvl | added to I QPs (SMART: first I only) |
| frmQPStep | P-to-P step until the scene classifier replaces it |
| gopQPStep | P QPs within the I QP +/- gopQPStep |
| gopRelation | carry the unused GOP budget over |
| adaptiveMode (CBR) | not used |
| I/P QP delta | no IMP field; FIXQP: I = qp - 3 |

## Macroblock rate control

`[268]` defaults to 1, so the OEM runs it for CBR, VBR and SMART (not
FIXQP), every picture (`src/rc_t20/rc_t20_mb.c`):

- `H264_SMA_CalMBFlag` (0xa4530, Ingenic MXU2 SIMD): per macroblock the
  vertical (|r0-r3| + |r3-r6|) and horizontal (|p - right| on rows 0, 3, 6)
  absolute luma gradients of both 8-row halves, class 1..7 by the
  thresholds E+272.. (vertical) and E+296.. (horizontal), the luma sample
  at (8, 7) into E+0x40150; inner macroblocks re-classed by their 8
  neighbours (>= 6 neighbours in classes 1-3 / 4-5 / 6-7), in place; class
  counts E+324 (0..3), E+328 (4..5), E+332 (other).
- `H264_SMA_CalMBQP` (0xa6eb0): E+0x90164 = share of flat macroblocks x
  100; QP offsets per class from a 32-row table (0xd15ec; -1 for classes
  1..4, 0 for 5, +3 for 6, +6/+7 for 7) chosen by the class shares and the
  picture QP, none when flat macroblocks are < 12 % or > 91 % or QP < 25;
  per macroblock QP = picture QP (capped at 40 from the first flat
  macroblock on) + class offset, flat dark macroblocks (sample < 128)
  further + log2(sample + 1) - 8, clamped 1..51; ROI regions; the map
  run-length coded by `JZM_QPTabConv` (0xa3dec: qp | 0x80, repeat counts
  up to 127, bytes from the top of each word) into E+0x50158, length
  E+0x90158, mean QP E+0x90160.
- With a super-frame re-encode the map is rebuilt from the classes at the
  new QP (QP >= 41: flat macroblocks 35) or, for QP >= 49, dropped.

It does not change the picture QP decisions (except the QP >= 49 re-encode
above).  `hwicodec_pf_h264e_t20_enc` hands the table to the VPU:
`H264E_T20_SliceInit` (0x20f40) writes register 0x4006c = words << 21 |
0xc5800 | enable << 31 and the table words into VPU memory 0x132c5800..;
0x40040 (macroblock QP cap) is the application's maxQp.  Independently of
the macroblock rate control, pictures of at least 51 x 39 macroblocks get a
different macroblock mode tuning while the scene class is not 0 (slice
+132..+155 -> 0x80034 = 0x896783e6 instead of 0x8202, 0x8003c =
0x09000000).  The SMART tuning bytes E+0x90182.. reach the i264e io block
but the T20 encoder does not read them.

## T10

The T10 runs `JZ_VPU_RC_VIDEO_CFG` / `JZ_VPU_RC_FRAME_RC` /
`JZ_VPU_RC_FRAME_REPEATE_JUDGE` with the exported `RC_H264_*` helpers
(0x9eec0..0xa2b2c, about 2,800 instructions, no macroblock part) on the
block at param + 476 (E; rcPara P 88 bytes and rcSt S 808 bytes behind the
pointers E+216/E+220).  `src/rc_t20` must not be used on the T10;
`src/rc_t10` is its reimplementation (same layout rules as `src/rc_t20`).

Inputs per picture (`i264e_ratecontrol_start` 0x3cb64, T10 path): E+212 =
IDR, and only for P pictures E+188 = cmpx and E+192 = bits of the previous
picture (an IDR keeps the values of the picture before; OEM).  cmpx is
`io +116` of `hwicodec_pf_h264e_t10_enc`, i.e. the channel node +44 after
the run ioctl 0xc0386302, as on the T20; bits = slice bytes x 8.  No VPU
registers.  After coding (`i264e_ratecontrol_is_reenc`): E+192 = bits,
`FRAME_REPEATE_JUDGE`, QP E+177.

Differences to the T20 controller:

- R-Q / MAD model window up to 20 pictures (T20: 4); the outlier threshold
  divides by n (T20: n - 2).
- `RC_H264_updateQp`: 13 ratio steps 1.0 .. 4.0 (index of the first step
  above the ratio; exactly 4.0 gives 0).
- Scene class S+780 from the complexity ratio of the last P picture to the
  last I picture (SMART: the GOP head): ratio >= 41 % or P > I: 2, < 26 %: 0,
  else 1.  In CBR/VBR it only matters when the GOP budget is used up or
  the estimate falls below 3/5 of the per-picture budget: class 0 then
  targets 3/5 of it, otherwise all of it.  SMART switches the GOP budget
  between the quality minimum (class 0), the bit rate (1) and the
  maximum (2).
- I QP: the first from bits per picture and picture size
  (`calcFirstIQp`, using P+44 << 8), then the average P QP of the last GOP
  minus min(gop/15, 2), limited by `frmQPStep`/`gopQPStep`.
- frmQPStep (P+24) and gopQPStep (P+28) are used as given (no scene
  overwrite): a P QP stays within +-frmQPStep of the last coded QP and
  within +-gopQPStep of the last I QP; staticTime and the I/P delta are not
  used (as on the T20).  iBiasLvl is added to every I QP.
- NewMaxQp: maxQp + (newMaxQp - maxQp) when the GOP used more than
  `trig` x its budget.
- Super frame / re-encode (`FRAME_REPEATE_JUDGE`): VBR only.  The
  thresholds are `superFrm` bits / 1024 compared with the picture's bits,
  so with the default thresholds (19660800 / 14043429) every I picture above
  19200 bits and every P picture above 13714 bits is coded once more at
  QP + 3 (E+100 = 1 time, E+101 = 3).  The SMART branch reads the VBR mode
  word E+112 (0 for SMART): SMART never re-encodes.  CBR never re-encodes.
- FIXQP does not run the controller: `i264e_ratecontrol_start` gives IDR =
  clip(qp - 6 x log2(ipFactor 1.4) + 0.5) = qp - 3, P = qp.
- Not reproduced: the start paths for param[RX+1532] = 5/6 (they set
  E+224 to 1/2; never set by the IMP encoder path) - E+224 stays 0.

Verification: `sim10.py` (OEM `i264e_ratecontrol_init/_start/_is_reenc`
with param[0] = 1 against `src/rc_t10` built for mipsel, both under
unicorn, E/P/S compared byte for byte after every call): 200 random
scenarios x 150 pictures (CBR/VBR/SMART/FIXQP, 7675 re-encodes) identical.
`tests/rc_t10` (in `make check`) replays 16 scenarios x 120 pictures
produced by `tools/rc_t20_oracle.py --t10`: 4231 decisions, 0 differ.

## In OpenIMP (T20 build, `src/t30/t30_helix_encoder.c`)

On a T20 (not a T10: `t30_soc_is_t10`), CBR, VBR and SMART channels run
`src/rc_t20` instead of OpenIMP's GOP controller (`t31_rate_control`):

- `t20_rc_start` (at create and on every rate-control change, as the OEM
  re-runs `i264e_ratecontrol_init`): the i264e parameters from
  `HWEncoderParams` with the OEM clamps of CreateChn
  (`i264e_validate_parameters`) or, after a run-time SetChnAttrRcMode
  (`HW_RC_FLAG_RUNTIME`), of `i264e_reconfig_rc_set` (see
  `src/t40/p2_rc_readback.h`); the application's QP range as given (OpenIMP
  otherwise turns a min QP of 0 into 18); bit rates in kbit/s; macroblock
  rate control off.
- per picture: `RCT20_Start` with the IDR decision of the encoder, QP =
  the controller's; after the run `RCT20_End` with the slice size (header +
  VPU payload bytes x 8; the OEM counts i264e's slice bytes), the channel
  node's `cmpx` and the registers 0x132800e4/e8/ec (soc_vpu ioctl
  0xc0386307); a re-encode request codes the picture again at the new QP
  (at most 4 times).
- FIXQP keeps OpenIMP's handling (P = qp, IDR = qp - 3 as the OEM).
- command list (`T30_H264_BuildDescriptor`, T20): 0x40040 = maxQp, the
  macroblock mode tuning in moving scenes of >= 51 x 39 macroblocks, and
  with `OPENIMP_T20_MBRC=1` the macroblock QP table (0x4006c and the table
  writes; the T20 command-list buffer is 64 KiB for it).  The luma plane is
  written back and invalidated before the classes read it.

Environment:

- `OPENIMP_T20_RC=0`: OpenIMP's GOP controller instead (as before).
- `OPENIMP_T20_MBRC=1`: the macroblock rate control (the OEM default; off
  in OpenIMP until tested on a camera: it costs about 1.3 M CPU operations
  per 1080p picture without the OEM's SIMD).
- `OPENIMP_T20_RC_STATS=<seconds>`: one log line per interval: bit rate,
  P and IDR QP average/min/max, scene class, re-encodes (also on the T10).
- `OPENIMP_T10_RC=1` (T10 only, default off): CBR, VBR and SMART run
  `src/rc_t10` instead of the GOP controller (`RCT10_Start` / `RCT10_End`
  with the IDR decision, the slice size and cmpx; re-encodes as on the
  T20, at most 4).  Off by default until tested on a T10 camera, also
  because OEM VBR codes most pictures twice (see "T10").  The log shows
  `T10 rc: OEM <mode> ...` when it starts.

The log shows `T20 rc: OEM <mode> ...` with the effective parameters when
the controller starts.  `tests/t30` (helix_encoder_test_t20) checks the
plumbing: every slice QP of the encoder against a second controller fed the
same slice sizes, cmpx and register values; with `OPENIMP_T20_MBRC=1` the
QP table, its control word and the macroblock tuning in the command list;
and that `OPENIMP_T20_RC=0` reads no registers.
