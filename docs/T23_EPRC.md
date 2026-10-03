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

## IDR period and long-term references (i264e, not eprc)

`i264e_decide_slice_type_and_rd` (0x34d78): IDR every GOP; with
param[2756] > 0 (and the reference mode at +2768 0, 5 or 6) every
param[2756] x GOP pictures, or earlier at a GOP boundary when the picture
class (E+1596) is 5.  Long-term references (MMCO, `long_term_reference_flag`
in `i264e_slice_header_write` 0x39xxx) are only generated for the HSkip
reference modes 5/6 (H1M, `i264e_dec_ref_pic_remark` 0x355dc,
`i264e_reconfig_hskip_set` 0x36cdc).  **The OEM SMART mode itself uses no
long-term reference.**  Which IMP field sets param[2756] was not
determined (no direct store in the library; it is not set by
`i264e_param_default`); if it is 0, the OEM SMART codes an IDR every GOP and
never produces type-6 pictures.  A capture of the OEM stream in SMART mode
(IDR distance, slice types) settles it.

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

- **T21 1.0.33**: the same eprc (identical function sizes except
  FRAME_START +976 bytes and REPEATE_JUDGE, where T23 calls the
  macroblock part `h264_api_enc` separately); module usable as is.
- **T20 / T10 3.12.0**: a different, smaller controller
  (`JZ_VPU_RC_VIDEO_CFG_T20`, `JZ_VPU_RC_FRAME_RC_T20`,
  `eprc_default_set_t20`); needs its own port (M5).
