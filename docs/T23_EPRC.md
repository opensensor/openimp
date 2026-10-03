# Helix picture rate control "eprc" (T21/T23)

The OEM libimp of the T21 and T23 runs i264e on top of a picture rate
controller whose entry points are `JZ_VPU_RC_VIDEO_CFG_T21`,
`JZ_VPU_RC_FRAME_START_T21`, `JZ_VPU_RC_FRAME_REPEATE_JUDGE_T21` and
`JZ_VPU_RC_FRAME_END_T21`.  This document describes it from the T23 1.3.0
libimp (addresses below are that library) and OpenIMP's reimplementation,
`src/eprc/` (platform neutral: no hardware access, libm only).

## Verification

`src/eprc` is bit-exact with the OEM code:

- under the unicorn emulator, function by function, on the OEM state
  (40 random CBR/VBR/SMART scenarios x 150 pictures and 16 scenarios with
  oversized pictures that trigger re-encoding: identical state block, model
  and outputs after every FRAME_START and FRAME_END);
- end to end on the host: `tests/eprc` (in `make check`) replays
  `tests/eprc/eprc_vectors.txt`, the picture type / QP / re-encode QP
  sequence of the OEM code for seven scenarios (873 pictures), produced by
  `tools/eprc_oracle.py` from a vendor `libimp.so`.

Not compared: the macroblock-level part of FRAME_START (`h264_api_enc`
0xc3930, `h264_get_mb_qp` 0xc1cbc: state +5184..+5343 and the per-MB slice
fields), see "Not reproduced".

## Call sequence (i264e)

| i264e | eprc | OpenIMP |
|---|---|---|
| `i264e_ratecontrol_init` 0x40c7c | `eprc_default_set_T21` 0xc55b0, `JZ_VPU_RC_VIDEO_CFG_T21` 0xc5afc | `EPRC_Init` |
| `i264e_ratecontrol_start` 0x420e8 (before each picture) | `JZ_VPU_RC_FRAME_START_T21` 0xc815c | `EPRC_FrameStart` |
| `i264e_ratecontrol_end` 0x4238c + `_is_reenc` 0x424d8 (after each picture) | `JZ_VPU_RC_FRAME_REPEATE_JUDGE_T21` 0xcc828, then `JZ_VPU_RC_FRAME_END_T21` 0xca140 | `EPRC_FrameEnd` |

The controller block "E" is i264e rc + 496; VIDEO_CFG allocates one state
block (E+1620): A = +0 (340 bytes, configuration), S = +352 (6800 bytes,
state), then arrays sized by GOP and picture size.

### Inputs

Configuration (`i264e_ratecontrol_init`, i264e parameter offsets):
mode [200] (1 CBR, 2 VBR, 3 SMART), GOP [44], fps [408]/[412], QP range
[208]/[212], bitrate [228] / max bitrate [260] (kbit/s), iBiasLvl [240],
frmQPStep [244], gopQPStep [248], staticTime [256], changePos [264],
qualityLvl [268], picture size; [292]/[296] are the VBV sizes from
`i264e_validate_parameters` (0x33ab0).  SMART differs from VBR in three
fields: E+8 = 3, E+20 = 5 (state A+148, the GOP-start picture logic) and
the SmartP GOP fields E+120 (background interval = GOP x param[2756]),
E+124/E+125 (BgQpDelta/ViQpDelta 3).  The SmartP fields reach the state
(A+28, A+77/78) but the frame QP path does not read them (A+168 is 2, so
the GOP-start QP uses the frame-rate rule, not ViQpDelta).

Per picture (`i264e_ratecontrol_start`): "IDR" flag (frames since the IDR
== 0), i264e rc+2768/+2820 (HSkip reference mode, 0 without HSkip), the
i264e picture fields +392..+404 (0 by default), no region/QP-map input.

After the picture (`i264e_ratecontrol_end`): coded bytes and 25 Helix
status registers, read through soc_vpu ioctl 0xc0586307 (`_IOWR('c', 7,
88-byte node)`, address 0x13100000 + offset in node word 0, value in word
1) from the table at 0xd7a40: 0x80120..0x8014c, 0x500e8..0x500f0,
0x80080, 0x40094..0x400a0, 0x800e8, 0x800ec, 0x800e4, 0x800e0, 0x80168
(`EPRC_StatRegs`).  FRAME_END uses them as motion/size statistics
(E+320..+419 -> S+4472..+4563, scene judging).

### Outputs

- picture type: 2 IDR, 0 P, 6 SMART GOP-start P (i264e sees 6 as 0);
- picture QP (E+1600), and in the slice block of `H264E_T21_SliceInit`:
  +448/+808 QP (registers 0x40000, 0x90018, 0x40074, 0x80114), +809 =
  min(QP+13, 51) (0x40040, 0x40074), +810 = max(QP-12, 1) (0x40074),
  +1058/+1060/+1062 lambda = 384 + 48 x max(QP-33, 0) and 96 + 12 x
  max(QP-33, 0) (0xb001c, 0xb0020);
- FRAME_END: re-encode request (QP raised, picture coded again).

## Algorithm (FRAME_START / FRAME_END)

- **Picture type** (0xc82b4): IDR when i264e marks it; SMART (A+148 != 0)
  types the first picture of every GOP inside a longer IDR period as 6.
- **Bit budget**: per GOP (`gop_init` 0xc1158 at every IDR / type-6
  picture): model QPs = weighted averages of the coded QPs of all / of P
  pictures (S+296..308, decay 0.95), GOP budget from the complexity class
  (cplxLvls 0xee1d4 by bit-rate class A+164) between the bounds
  S+356/S+360 (VBR/SMART) or the CBR window (staticTime x bitrate).
- **Picture budget** (P, 0xc8f6c): remaining GOP bits / remaining
  pictures, averaged with the deviation-corrected target
  (S+5956..+5964, gamma 50 %).
- **QP from budget**: R-lambda model (estimate_qp 0xbf9e4): bpp = bits /
  pixels, lambda = alpha x bpp^beta, QP = 4.2005 ln(lambda) + 13.7122 + 0.5.
  FRAME_END (0xcc3d4) updates alpha/beta from the real bpp (step sizes by
  bpp class: 0.005/0.0025 .. 0.2/0.1), clamps alpha 0.05..200, beta
  -3..-0.1; alpha 3.2003, beta -1.367 after the first picture.
- **I pictures**: QP from the P model QP (S+324) minus a frame-rate
  dependent delta, plus the ip_qp_delta table (0xee16c) from the distance
  to the last I QP, plus iBiasLvl (A+76).
- **QP limits**: change against the previous QP limited by frmQPStep
  (P->P), gopQPStep (GOP start) and the I step; a window rule (0xc93e0)
  from the picture classes around the current GOP position and the size
  ratio; then min/max QP (and min I QP for IDR).
- **Scene judging** (`scene_judge_enc_frame` 0xbdbd0, FRAME_END): picture
  class 0..6 (S+148) from the motion statistics (thresholds mv_thr_h264
  0x104f18 by bit-rate class) and the size ratio; drives the model update
  and the QP window.
- **Re-encode** (FRAME_REPEATE_JUDGE): a picture larger than the VBV size
  (E+104 IDR, E+108 P; first picture min(E+104, max(1.4 x bitrate,
  1433600))) is coded again, at most A+100 (= fps/2) times, with the QP
  raised so that 2^((QP-4)/6) scales to 95 % of the limit.

CBR keeps a statistics window of staticTime x fps pictures and a
fluctuation level (fluctLvls 0xee254); VBR/SMART target changePos % of the
maximum bit rate (qualLvls 0xee274 for the lower bound).

## QP down-step limit (OpenIMP extra, default off)
`EprcParams.qp_down_max` (not an OEM field; 0 = the OEM controller,
bit-exact): after the OEM QP limits of FRAME_START, a P picture that
follows a P picture gets at most `qp_down_max` less than the QP the last
picture was coded with (E+1600 after FRAME_END, re-encodes included);
rises are not limited.  It is applied before the picture fields, so the
slice QP, the macroblock QP window and the lambdas follow, and FRAME_END
reads the QP actually coded.  Why: in static scenes the OEM walks the P QP
down by up to frmQPStep per picture into the sensor-noise cliff, one
picture becomes 10-50 x larger and the QP jumps back (14-27 jumps of >= 2
QP per 100 pictures in recordings of OpenIMP's eprc stack).  Host study
(docs/RC_BEYOND_VENDOR_STUDY.md on `claude/rc-beyond-vendor`, P5): CBR
QP flicker -8..27 %, mean QP -0.1..-0.4 at the same bit rate, no
regression in 90 runs; VBR/SMART similar.  frmQPStep = 1 would also cut
the flicker but slows the reaction to motion (burst buffer +35 %).
Switch: `OPENIMP_EPRC_QP_DOWN1=1` (CBR), `=2` (CBR, VBR, SMART); T23, and
T21 with `OPENIMP_T21_EPRC=1`.  The start log line ends in `qp-down<=1`.
`tests/eprc` checks the OEM vectors with it off and, on a static
noise-cliff scene, that it removes every fall > 1 and keeps the slice
fields consistent.

## IDR period and long-term references (i264e, not eprc)

`i264e_decide_slice_type_and_rd` (0x34d78): IDR every GOP; with
rc[2756] > 0 (and the skip type at rc[2768] 0, 5 or 6, i.e. N1X, H1M_FALSE,
H1M_TRUE) every rc[2756] x GOP pictures, or earlier at a GOP boundary when
the picture class (E+1596) is 5.  This applies to every rate-control mode,
not only SMART.  Long-term references (MMCO, `long_term_reference_flag`)
are only generated for the HSkip reference modes 5/6 (H1M,
`i264e_dec_ref_pic_remark` 0x355dc, `i264e_reconfig_hskip_set` 0x36cdc).
**The OEM SMART mode itself uses no long-term reference.**

rc[2756] is `rcAttr.attrHSkip.hSkipAttr.maxSameSceneCnt`:
`IMP_Encoder_CreateChn` copies hSkipAttr {skipType, m, n, maxSameSceneCnt,
bEnableScenecut, bBlackEnhance} into i264e param +172..+192 (0x4ece0..),
`i264e_idr_reconfig` (0x36004) calls `i264e_init_skip_header` (0x34b60)
with the skip header at rc+2752, whose word 1 (rc+2756) is
maxSameSceneCnt and word 4 (rc+2768) skipType; the same rc[2756] is the
SmartP background interval multiplier in `i264e_ratecontrol_init`.  timps
and prudynt leave hSkipAttr zero, so the OEM codes an IDR every GOP (as
OpenIMP measured on cam-B).  OpenIMP passes maxSameSceneCnt (skip types
N1X/H1M only) from CreateChn as `HWEncoderParams.same_scene_gops`; with the
eprc controller running the encoder codes an IDR every n GOPs and gives the
controller the same multiplier.  Not reproduced: the scene-cut IDR (class
5) and run-time `IMP_Encoder_SetChnHSkip`.

## Not reproduced

- Macroblock-level rate control (`h264_api_enc` / `h264_get_mb_qp`: SAS/CRP
  offsets, skin detection, registers 0x40078..0x40084, 0x400c0/0x400c4):
  independent of the picture QP decisions; OpenIMP's command list keeps
  constants there.
- FIXQP (eprc mode 0), the rate-distortion model (A+180 = 0), region and
  macroblock QP-map inputs, `T21_show` logging: not reachable from the IMP
  API or not used by OpenIMP.
- VIDEO_CFG range checks that stop the OEM on invalid input (i264e only
  passes validated parameters).

## Other SoCs

- **T21 1.0.33**: an older revision of the same controller, *not*
  identical.  The i264e glue differs (no parameter word at +4: every
  parameter offset 4 lower, "rc enabled" is p[0] == 4; E at rc+484;
  E one word shorter from E+44, so the outputs sit at E+1588/E+1596; the
  state block offsets differ), `eprc_default_set_T21` writes other
  defaults (E+184..+196, +208/+210, +277, +288..+293 in T23 terms), and
  FRAME_START (1797 against 2041 instructions), FRAME_REPEATE_JUDGE (an
  `update_qp` helper instead of the inline 2^(QP/6) search), VIDEO_CFG and
  FRAME_END differ in code.  `tools/eprc_oracle.py` runs either library
  (layout picked by the `h264_api_enc` symbol); on the seven test
  scenarios the T21 vendor sequence differs from the T23 one from the
  first picture on (first IDR QP: T21 starts from the init QP / min QP,
  e.g. 15 or 20, T23 at 36), 447 of 847 lines differ.  src/eprc is the
  T23 controller, so OpenIMP T21 runs it only with `OPENIMP_T21_EPRC=1`
  (an approximation that gives T21 the RC extras, not the vendor
  decisions); a vendor-equal T21 needs a port of these differences.
  Equal on both: the per-picture QP window and lambda (`h264_api_enc`,
  checked under emulation for FIXQP/CBR/VBR): lambda 384 + 48 / 96 + 12
  per QP above 33, window [QP - 12, min(QP + 13, 51)] - the low end is
  max(.., 1) on T23 and wraps to 51 below QP 12 on T21 (OpenIMP: 0).
  OpenIMP's T21 command list now carries this lambda too.
- **T20 / T10 3.12.0**: a different, smaller controller
  (`JZ_VPU_RC_VIDEO_CFG_T20`, `JZ_VPU_RC_FRAME_RC_T20`,
  `eprc_default_set_t20`); needs its own port (M5).

## In the native T21/T23 encoder (M4, M5)

`src/t30/t30_helix_encoder.c` (T21 and T23): `helix_eprc_start` (create,
every rate-control change; the OEM re-runs `i264e_ratecontrol_init`) with
the parameters as `IMP_Encoder_CreateChn` + `i264e_validate_parameters`
(run time: `i264e_reconfig_rc_set`) leave them - the values
`GetChnAttrRcMode` reads back: iBiasLvl -10..10, frm/gopQPStep 2..51
(run time: negative -> 0), VBR/SMART staticTime <= 0 -> 1, changePos
50..100 (run time 0..100), qualityLvl 0..6, maxBitRate >= 128 kbit/s; CBR
has no staticTime/changePos/qualityLvl and keeps the i264e defaults 2/80/4
(CreateChn 0x4ee74 copies only the CBR fields); without application
extras all i264e defaults (steps 3/15).  `EPRC_FrameStart` per picture
(frames since IDR), `helix_eprc_statistics` (25 x soc_vpu
IOCTL_CHANNEL_WOR_VPU_REG: T23 0xc0586307 at 0x13100000, T21 0xc0386307 at
0x13200000 as the OEM `hwicodec_pf_h264e_t21_enc` 0x1ffe4; the T21 kernel
patch 0098 allows reads inside the Helix window) and `EPRC_FrameEndEx`
after the run (coded size = VPU output length; the OEM uses i264e's slice
byte count, a few header bytes apart), re-encode at the controller's QP.

Re-encoding and the shared reference ring (default on T21/T23 up to
1080p): a P picture's reconstruction overwrites the reference rows it was
predicted from (recon n lies 256 lines before reference n-1 in the ring),
so a P picture is never coded a second time in ring mode; the controller
then finishes it as a picture that was not judged
(`EPRC_FrameEndEx(.., may_repeat = 0)`).  IDR pictures (no reference read)
and pictures without the ring are re-encoded as the OEM does.

Switches: `OPENIMP_T23_EPRC` unset = SMART, `1` = CBR/VBR/SMART, `0` = off;
`OPENIMP_T21_EPRC` unset/`0` = off (the T21 GOP controller), `1` =
CBR/VBR/SMART with the T23 controller (see "Other SoCs");
`OPENIMP_T23_SMART_IDR_GOPS=n` overrides maxSameSceneCnt for SMART on T23.

## Device test plan (cam-B T23, cam-D T21)

Bind-mount the built libimp over `/usr/lib/libimp.so`, run timps on a
config copy with `video0.rc_mode=smart`, `OPENIMP_T23_RC_STATS=10`.

1. Log: `T23 Helix eprc: SMART ...` at start; no `reading 0x131... failed`
   (register reads allowed by soc_vpu; else statistics are 0 and scene
   judging degenerates); count `coding again` lines (should be rare).
2. Record 60 s static scene and 60 s with motion (RTSP, ffmpeg `-c copy`).
   `ffmpeg -v error -i rec.h264 -f null -` clean.  `ffprobe -show_frames
   -select_streams v -show_entries frame=pict_type,pkt_size` : IDR every
   GOP (or every n GOPs with `OPENIMP_T23_SMART_IDR_GOPS=n`), only I/P.
3. Bitrate: static well below maxBitRate, motion close to changePos% of
   maxBitRate; QP (`rc stats` lines) higher in motion, lower when static,
   P-to-P steps <= frmQPStep.
4. A/B: same with `OPENIMP_T23_EPRC=0` (band mapping) and the OEM stack in
   SMART mode on the same scene (IDR distance, sizes, QPs via
   `ffprobe -show_entries frame=pkt_size`, QP from the slice header with
   `ffprobe -debug qp` or `h264_analyze`).
5. `OPENIMP_T23_EPRC=1` with `rc_mode=vbr` and `rc_mode=cbr`: bitrate on
   target, decode clean.
6. Ring: the start log still says `reference sharing on`; with ring P
   pictures are not re-encoded (only IDR `coding again` lines).
7. T21 (cam-D, .24): first without switch (`T21 Helix eprc` absent, GOP
   controller, but the lambda change of the command list is active for
   QP > 33: decode clean, bitrate as before), then `OPENIMP_T21_EPRC=1`
   with cbr/vbr/smart: `T21 Helix eprc: ...` line with the clamped
   extras, no `reading 0x132... failed`, decode clean, bitrate vs target.
