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
  `tools/eprc_oracle.py` from a vendor `libimp.so`; the same for the T21
  revision (`src/eprc/eprc_t21.c`) with `tests/eprc/eprc_t21_vectors.txt`
  from the T21 1.0.33 library;
- `eprc_ext_vectors.txt` / `eprc_t21_ext_vectors.txt` (`eprc_oracle.py LIB
  --extended`): param[52] (re-encode IDR pictures only), FIXQP, the AE zone
  input (FNV-1a of the zone buffer after every FRAME_START) and, on T23,
  the picture types from the OEM `i264e_decide_slice_type_and_rd`
  (scene-cut IDRs); `eprc_random_vectors.txt` /
  `eprc_t21_random_vectors.txt` (`--random 5 30 150`: 30 random
  CBR/VBR/SMART scenarios, sizes 176x144..2560x1440 including sizes
  `i264e_validate_parameters` refuses).  Seeds 1..8 x 30 scenarios: 0
  deviations on both.

The macroblock rate control (`h264_get_mb_qp`, "Macroblock rate control"
below) is compared the same way: `eprc_mbrc_vectors.txt` /
`eprc_t21_mbrc_vectors.txt` (`eprc_oracle.py LIB --mbrc SEED N FRAMES`:
random scenes with an activity-class histogram, after every picture the
registers the OEM `H264E_T21_SliceInit` makes of the slice block and
hashes of the slice fields and the state it writes; `--mbrc-calls SEED N`:
single `h264_get_mb_qp` calls on random state, NaN shares and class
limits included).  Locally seeds 11..13 x 30 scenes x 150 pictures and
20000 calls per library: 0 deviations on both.

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
i264e picture fields +392..+404 (0 by default), and two frame inputs that
`do_channel_process` (0x4a84c) copies from the frame for T21/T23 CPU ids:
- frame +0x40/+0x48, a region input (E+640/E+644, `scene_judge_ncu`
  0xbd600 with region blocks): `VBMGetFrame` (0x1e2b4) and
  `IMP_Encoder_YuvEncode` always write 0, so it is unreachable through IMP;
- frame +0x44/+0x64, the ISP AE zones (15 x 15 words of
  `IMP_ISP_Tuning_GetAeZone`, E+641/E+672): `VBMGetFrame` fetches them for
  frame source channel 0 (+0x44 = 1 when the call succeeds).  FRAME_START
  (0xc8490) only stores them, the previous zones and, after a P picture
  from the sixth picture on, their absolute differences in the S+6796
  buffer; no code reads that buffer, the zones change no decision.
  `EprcFrameIn.ae_zone` takes them (both revisions, emulator-identical);
  the Helix encoder leaves it NULL (an ISP ioctl per picture for no
  effect).  The doc's earlier "QP map" for E+641 was this input.

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
controller the same multiplier.

Scene-cut IDR (`i264e_decide_slice_type_and_rd` 0x34f94, T21 0x2c018, the
same rule): with maxSameSceneCnt n > 0 and the skip type 0, 5 or 6, the
picture after v0 pictures since the IDR is an IDR when (v0 + 1) % GOP == 0,
the mode is not FIXQP, the class E+1596 (T21 E+1592) that FRAME_START of
the previous picture reported is 5 and v0 >= GOP (not at the first GOP
boundary); else every n x GOP.  The class comes from `scene_judge_enc_frame`
(FRAME_END) on the Helix statistics - no region input.  OpenIMP:
`helix_eprc_scene_cut` with `EPRC_PictureClass`/`EPRC21_PictureClass`;
checked against the OEM decision in `eprc_ext_vectors.txt` (scene-cut IDRs
after 15 and 10 pictures at GOP 5).

Run-time changes (`i264e_reconfig_rc_set`, `_fps_set`, `_gop_set`,
`_hskip_set`) only set a bit in h+0x265c; `i264e_encode` runs
`i264e_idr_reconfig` (0x36004) when the next picture is an IDR anyway
(0x3d804): new hSkipAttr (param +172..+192), SPS/PPS, skip header and
`i264e_ratecontrol_init` with the QP of the last picture (i264e picture
+0x2e4) as initial QP - no extra IDR.  OpenIMP does the same: run-time
`helix_eprc_start` calls are kept pending until the next IDR and restart
the controller with `init_qp` = the last QP; `IMP_Encoder_SetChnHSkip`
(T23, and new on T21 with `GetChnHSkip`) hands maxSameSceneCnt (N1X/H1M)
to the encoder through `AL_Codec_Encode_SetSameSceneGops`, the T23
`OpenIMP_T30_HelixReconfigure` now follows it.  The HSkip reference
structures themselves are not coded by the native encoder.

## FIXQP

eprc mode 0 runs FRAME_START as well (0xc83ec): the I or P QP from
VIDEO_CFG (0xc7530: E+60/E+61 = min(qp - 3, 51) unsigned and min(qp, 51),
so below QP 3 the I QP is 51), no model, no window, no QP range, no
re-encode (FRAME_REPEATE_JUDGE needs a mode).  Ported in both revisions
and in the vectors; the Helix encoder runs FIXQP through it where its mode
set includes FIXQP (T21 by default, T23 with `OPENIMP_T23_EPRC=1`).

## Macroblock rate control

`h264_api_enc` (T23 0xc3930; T21 inlined) runs `h264_get_mb_qp` (T23
0xc1cbc, T21 0x93908: same code, S offsets from 280 on 40 lower) for every
picture; `H264E_T21_SliceInit` (T23 0x25b9c, T21 0x1ce20, same encoding)
turns its slice fields into Helix registers.  It never changes the picture
QP.  OpenIMP: `src/eprc/eprc_mbrc.c` (`EPRC_MbQp` from `EPRC_FrameStart` /
`EPRC21_FrameStart`, `EPRC_MbRcRegs` -> `EprcPicture.mbrc`).

Mode words A+208 (IDR) / A+212 (P), from E+168/E+172 = i264e h+10692/
h+10696, the OEM rate-control file defaults (`i264e_ratecontrolfile_init`;
without a file 0x30): bits 0..3 the QP map (0: none), 4..7 the SAS mode
(3), 8..11 the basic-unit mode (0).  No IMP call changes them: QP maps
(modes 1..3: the S+6784 map, the run-length list at S+376 -> slice
+812/+816/+820 -> 0x30000 bit 17, 0x4006c, 0x40074 bits 22/23, regions
0x40044..0x40068), SAS modes 0..2 (CRP, skin tables) and basic units are
not reachable and not ported (`EPRC_MbQp` does nothing for another mode
word).  `IMP_Encoder_SetMbRC` (`i264e_mb_rc_set` -> h+10836 ->
param[280]): stored, re-read by `i264e_reconfig`, nothing else reads it
(T23 1.3.0 and T21 1.0.33) - the OEM always runs the macroblock rate
control (emulator: identical slice blocks with param[280] 0 and 1).

Per picture (default mode words):
- S+368 = 0, S+372 = 0, S+5184..5239 = 0 (no QP map); slice +752..807
  (regions) = S+5184.., +812 = 0, +816 = S+372, +820 = &S+376.
- SAS offsets S+5336..5342 from the activity-class histogram of the
  previous picture (statistics registers 0x40094..0x400a0 -> E+404 ->
  S+4488..4503: seven 16-bit counts): x, y, z = 100 x (classes 0-2, 3-4,
  5-6) / A+52 in float.  None (all 0) for x < 12, x >= 92 or QP < 20;
  else band b by x (12, 22, 52, 82, 92), q = QP - 20 < 16 (band 4: < 13)
  and a (y, z) class from the 9-byte table 0xee040, which the OEM indexes
  with 8 | flags: 8 (class 3) when y <= lo and z <= hi or y <= hi and z
  <= lo (lo/hi 40/80, 35/70, 20/40, 7/16 per band), else 9..15 - a read
  behind the table into uninitialised stack.  The emulator (and OpenIMP)
  read 0 there (class 0); a device reads whatever an earlier call left
  (any of the four classes).  Row `h264_sasm_ofst[class | q << 2 | b <<
  3]` (32 x 7, -8..+5).
- First picture (S+0 = 0): slice +842..849 = 0, nothing else (registers
  all zero, as before this port).  Then: +824 = 1, +825 = 0, +826 = 1,
  +842..848 = the offsets, +828..841 `sas_mb_thd_init` (activity limits
  150, 300, 400, 1000, 2000, 4000), +850..854 `sas_flt_thd_init` (2 3 4 4
  4), basic-unit fields +855..+924 (+862 = 1, +872 = S+88, sizes; no
  register reads them in mode 0), +892..+914 the `rc_bu_*`/`rc_mb_*` QP
  offset tables.

Registers (`H264E_T21_SliceInit`, found by perturbing the slice under
emulation; T23 and T21 alike):

| register | fields |
|---|---|
| 0x40074 | QP window/QP as before, OR +824 bit 0, +825 bit 1, +826 bits 2-3, +862 bits 4-5, +857 bit 7, +863 bits 14-15, +812 bits 22-23, +855 bit 23, +858 bits 30-31 (default 0x15) |
| 0x40078 | +850..854, 3 bits each at 4-bit steps (0x00044432) |
| 0x4007c, 0x40080, 0x40084 | 16-bit pairs +828/830, +832/834, +836/838 (0x012c0096, 0x03e80190, 0x0fa007d0) |
| 0x40088 | +840 (16 bits, 0) |
| 0x4008c, 0x40090 | +842..845, +846..849: 6-bit fields per byte (SAS offsets) |

A vendor T23 command-list capture (hxdump, 12 pictures) shows these
values after the first pictures, with zero SAS offsets in that scene.

Not part of the macroblock rate control but per picture in the OEM list
where OpenIMP keeps constants: 0x400c0/0x400c4 (`h264_api_enc` slice
+976..+1052 from A+311..+327, by picture type): T23 IDR 0x060404c1 /
0x61615921, P 0x030484c1 / 0x61615c21; T21 IDR 0x060407c1 / 0x61615921, P
0x030487c1 / 0x61615c21; OpenIMP 0x060407c1 / 0x61615921 always (the T21
IDR values).  Left as they are (device-tested), noted for a later task.

OpenIMP switch: `OPENIMP_EPRC_MBRC=1` (the OEM behaviour) or 0/unset (the
default until the device test: registers as before), per channel at run
time with `IMP_Encoder_SetMbRC` (HWEncoderParams.mb_rc, from the next
picture; `IMP_Encoder_GetMbRC` returns the switch).  It needs the eprc
controller (`OPENIMP_T23_EPRC`, `OPENIMP_T21_EPRC`); the controller
computes the fields either way.  `OPENIMP_EPRC_MBRC_LOG=n` logs the
registers every n pictures.  After a controller restart (run-time RC
change, IDR) the slice fields stay as the OEM hwicodec slice block does.

## Not reproduced

- Macroblock rate control modes other than the defaults (see above) and
  the stack bytes behind the (y, z) class table.
- The region input (`scene_judge_ncu` with regions): never set through IMP
  (see Inputs).
- The rate-distortion model (A+180 = 0), `T21_show` logging.
- VIDEO_CFG range checks that stop the OEM on invalid input (i264e only
  passes validated parameters).
- Picture sizes `i264e_validate_parameters` (T23 0x33780, T21 0x2a740)
  refuses - T23 width > 2336, T21 width < 256, both: width not a multiple
  of 16, odd height or < 16 lines: it returns before param[292]/[296], the
  OEM channel cannot be created.  `src/eprc` then keeps the default VBV
  sizes 19660800/14043429 as the oracle shows (this was the old T23
  random-oracle deviation "re-encode at QP 33 above maxQP" at 2560x1440:
  FRAME_REPEATE_JUDGE clamps the raised QP to 51 only, never to maxQP).

## Other SoCs

- **T21 1.0.33**: an older revision of the same controller, ported
  separately as `src/eprc/eprc_t21.c` (`EPRC21_*`, same interface).  It is
  equal to the OEM T21 code in `tools/eprc_oracle.py`: the seven test
  scenarios (`tests/eprc/eprc_t21_vectors.txt`, 873 pictures, in `make
  check`) and 12 x 30 random CBR/VBR/SMART scenarios (picture sizes 320x240
  to 2560x1440, GOP 1..120, 5..30 fps, all extras) with 0 differences, and
  under the emulator state by state (E, A, S, the arrays and the slice QP
  fields after every VIDEO_CFG, FRAME_START and FRAME_END).  The T21
  differences:
  - layout: i264e parameters without the word at +4 (every offset 4 lower,
    maxSameSceneCnt at rc+2704), E at rc+484 and one word shorter from E+44
    (no E+44 "param[52]" field: T23 E+48.. is T21 E+44..), no per-picture
    inputs (T23 E+1796..1829, S+240..279: T21 S offsets from T23 S+280 are
    40 lower), A 336 bytes, S = A + 336, header 7104 bytes;
  - defaults (`eprc_default_set_T21` 0x96574): E+180/184/188 = 63/255/255
    (T23 31/128/128), E+192 = 1 from 51 x 39 macroblocks on, E+204/206 = 1,
    E+273 = 1, E+284/285 = 3/1, E+288 = 20 (T23 E+293);
  - VIDEO_CFG (0x96ac0): A+200 (fluctuation bits), the CBR peak and the
    CBR window shares A+472/476 and the VBR lower bound A+72 in 32-bit
    integer arithmetic (x / 100) instead of float; field limits on A+158,
    160, 164, 191, 193, 194, 204, 205; the bit-rate class only when A+196
    is set (default 1); SmartP background interval clamped to [GOP, 65536];
  - FRAME_START (0x990e0): picture type from the first-picture flag and
    A+148 only (no per-picture inputs); the first IDR (no picture coded yet)
    starts at init QP E+40 or, without one, at 26 + `update_qp` (0x906b8)
    for bit rate (A+64 against 1000), frame rate (against 25) and picture
    size (against 921600), then min/max QP (T23: 36);
  - FRAME_REPEATE_JUDGE (0x9d390): size limit E+100 (IDR) / E+104 (P, no
    "IDR only" option), first picture 1433600 or 1.4 x bit rate (T23: the
    smaller of that and E+104); the QP step from `update_qp`;
  - shared with T23 otherwise (gop_init, estimate_qp, scene judging, the
    R-lambda model update, the per-picture QP window and lambda).
  OpenIMP T21 runs it by default (as the OEM); `OPENIMP_T21_EPRC=0`
  restores the old GOP controller, `OPENIMP_T21_EPRC=23` runs the T23
  controller on T21 (the earlier approximation, for A/B).
  Equal on both: the per-picture QP window and lambda (`h264_api_enc`,
  checked under emulation for FIXQP/CBR/VBR): lambda 384 + 48 / 96 + 12
  per QP above 33, window [QP - 12, min(QP + 13, 51)] - the low end is
  max(.., 1) on T23 and wraps to 51 below QP 12 on T21 (OpenIMP: 0).
  OpenIMP's T21 command list now carries this lambda too.
- Found with the T21 port and fixed in both: in a CBR P picture at GOP
  position 2 with no P pictures left (S+60 <= 0, GOPs of 1 or 2) the OEM
  sets the count to 1 (it stores its mode register) and still updates the
  picture budget; `src/eprc` had skipped the update.
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
so a P picture is never coded a second time in ring mode.  The OEM:
- T23: `IMP_Encoder_SetRdBufShare` (default 1, CreateChn 0x4e310) becomes
  i264e param[52] (0x4ed78), which turns the ring on (hwicodec cfg +0x2c ->
  `BUF_SHARE_CFG`) and is the controller's E+44: FRAME_REPEATE_JUDGE codes
  only IDR pictures again.  So the OEM never re-encodes a P picture with
  the ring; OpenIMP T23 now passes param[52] = 1 (`OPENIMP_REF_SHARE=0`:
  0, the SetRdBufShare(0) case, P pictures re-encoded without the ring).
- `BUF_SHARE_CFG` (0x258a0) would not keep the reference either: it
  advances the per-channel ring offsets on every SliceInit, so a second
  run of picture n would predict from the rejected first reconstruction of
  n (emulated: the repeated picture 3 reads line 576, its own first
  reconstruction, instead of picture 2 at line 832).
- T21 1.0.33: no param[52]/E+44; param[48] (the ring flag, hwicodec cfg
  +0x28) stays 0 (`i264e_param_default`, CreateChn never sets it), so the
  OEM T21 codes two references and re-encodes P pictures.  OpenIMP T21
  runs the ring by default and finishes such a P picture as one that was
  not judged (`EPRC_FrameEndEx(.., may_repeat = 0)`); `OPENIMP_REF_SHARE=0`
  is the OEM behaviour.

Switches: `OPENIMP_T23_EPRC` unset = SMART, `1` = FIXQP/CBR/VBR/SMART, `0`
= off; `OPENIMP_T21_EPRC` unset/`1` = FIXQP/CBR/VBR/SMART with the T21 revision
(`eprc_t21.c`, the default), `0` = off (the old T21 GOP controller), `23` =
the same with the T23 controller (see "Other SoCs");
`OPENIMP_T23_SMART_IDR_GOPS=n` overrides maxSameSceneCnt for SMART on T23.

## Device test plan (cam-B T23, cam-D T21)

Bind-mount the built libimp over `/usr/lib/libimp.so`, run timps on a
config copy, `OPENIMP_T23_RC_STATS=10`.  Day/night as the scene cut: POST
`/control` `{"daynight":{"enabled":0}}`, then alternately
`{"image":{"running_mode":1}}` / `{"image":{"running_mode":0}}` every 30 s
(IR cut and ISP mode; the lens cannot be covered remotely).  HSkip: timps
has no key for it, `tools/hskip_preload.c` (built as
`build/t2x/hskip_preload.so`) calls `IMP_Encoder_SetChnHSkip` inside the
streamer: `LD_PRELOAD=/tmp/hskip_preload.so HSKIP_CHN=0 HSKIP_N=3
HSKIP_DELAY=20 HSKIP_N2=0 HSKIP_DELAY2=40`.  Every run: record via RTSP
(`ffmpeg -c copy`), `ffmpeg -v error -i rec.h264 -f null -` clean, IDR
distances with `ffprobe -show_frames -show_entries frame=pict_type`, no
kernel oops, timps alive.

1. T23 SMART (default): `T23 Helix eprc: SMART ...`, no `reading 0x131...
   failed`.  Day/night switches: `coding again` lines only for IDR pictures
   (param[52] as the OEM); with `OPENIMP_REF_SHARE=0` P lines may appear;
   decode clean in both.
2. T23 scene cut: `OPENIMP_T23_SMART_IDR_GOPS=4`, 3 min with day/night
   switches: IDR every 4 GOPs, after a switch `scene cut, IDR after N
   pictures` with N a multiple of the GOP and >= 2 GOPs, matching ffprobe.
3. T23 HSkip at run time (preload above, SMART, no IDR_GOPS env): the
   preload logs `= 0` and the read-back; no IDR at the call; from the next
   IDR after 20 s every 3 GOPs, with `T23 Helix eprc: ... idr=3 gop(s)` at
   that IDR (controller restarted with the last QP as init QP); after
   60 s back to every GOP at the next IDR.
4. T23 FIXQP: `OPENIMP_T23_EPRC=1`, `rc_mode=fixqp`, qp 30: `FIXQP` in
   the eprc line, slice QP I 27 / P 30 (`ffprobe -debug qp` or
   `h264_analyze`); CBR/VBR with `OPENIMP_T23_EPRC=1`: bitrate on target.
5. T21 cam-D (eprc default for all modes): 1 with the T21 rules (ring on:
   only IDR re-encodes; `OPENIMP_REF_SHARE=0`: P re-encodes as the OEM
   T21, decode clean); 2 and 3 with the preload (`HSKIP_N=4
   HSKIP_DELAY=5` before the day/night switches - T21 has no IDR_GOPS
   env); 4 without env (FIXQP I = qp - 3).

### Macroblock rate control (cam-B T23, cam-D T21)

Same setup; the camera fixed on a scene with flat areas (wall, sky) and
texture (foliage, text) side by side, daylight, no motion for the
quality runs.  T23 with eprc on (SMART default, or `OPENIMP_T23_EPRC=1`
for CBR/VBR), T21 default.  Each run 3 min, recorded via RTSP.

1. Off (no env): log `macroblock rate control` absent; 
   `OPENIMP_EPRC_MBRC_LOG=25` prints nothing.  Baseline recording A.
2. On (`OPENIMP_EPRC_MBRC=1 OPENIMP_EPRC_MBRC_LOG=25`): the log shows
   `0x40074|=15 0x40078=00044432 0x4007c..88=012c0096 03e80190 0fa007d0
   00000000` from the second picture on, SAS offsets changing with the
   scene (6-bit two's complement bytes, -8..+5).  Recording B.  Optional:
   hxdump of the command list (LD_PRELOAD on the RUN ioctl) compared with
   the OEM stack on the same scene: 0x40074 low byte, 0x40078..0x40090
   equal (SAS offsets may differ where the OEM reads stack bytes, see
   "Macroblock rate control").
3. Compare A and B per mode CBR (fixed bitrate: quality differs) and VBR /
   SMART (quality level: bitrate differs): average bitrate over 60 s
   (`ffprobe -show_packets`), mean frame QP; PSNR/SSIM of decoded B and A
   against a lossless-ish reference (FIXQP 15 recording of the same
   static scene, `ffmpeg -lavfi psnr/ssim` on cropped flat and textured
   regions separately): expected with MB-RC lower QP (more detail, fewer
   blocking/banding steps) in flat regions, higher QP in texture, similar
   overall bitrate in CBR.  Visual check of flat gradients (banding) and
   fine texture at 1:1.
4. Decode cleanliness, on and off: `ffmpeg -v error -i rec.h264 -f null -`
   no errors, no green/smeared macroblocks, no `T23 Helix` failures or
   retries in the log, no kernel messages, 30 min soak with on.
5. Run-time switch: `tools/mbrc_preload.c` (`LD_PRELOAD=/tmp/mbrc_preload.so
   MBRC_CHN=0 MBRC_PERIOD=20 MBRC_COUNT=6`, without the env of 2: on, off,
   ... every 20 s): log `macroblock rate control on/off`, the
   register log follows from the next picture, no IDR, decode clean
   across the switches; `IMP_Encoder_GetMbRC` returns the value set.
6. Day/night switch and an RC change at run time (restart at the next
   IDR) with MB-RC on: no failures; the first picture after the restart
   keeps 0x40074|=15 with SAS offsets 0.

If 1-6 pass on both cameras, make `OPENIMP_EPRC_MBRC` default to on (the
OEM behaviour).
