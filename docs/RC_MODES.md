# Encoder rate-control modes: OEM behaviour and OpenIMP

What the OEM libimp does with each `IMPEncoderRcMode`, from its code, and
what OpenIMP does.  Addresses are the libraries named per section.

## T31 (AVPU, OEM libimp 1.1.6)

`channel_encoder_set_rc_param` (0x7e430) converts the IMP attribute into the
Allegro `AL_TRCParam` (channel +0x68):

| IMP mode | AL mode | fields |
|---|---|---|
| FIXQP 0 | 0 | iInitialQP |
| CBR 1 | 1 | target = max = uTargetBitRate * 1000, QPs, IP/PB delta, eRcOptions, uMaxPictureSize * 1000 |
| VBR 2 | 2 | as CBR, max = uMaxBitRate * 1000 |
| CappedVBR 4 | 4 | as VBR, plus uMaxPSNR * 100 at +0x30 |
| CappedQuality 8 | 8 | as CappedVBR |

`AL_Codec_Encode_ValidateRcParam` (0x785dc) accepts only 0/1/2/4/8, clamps
QPs and uMaxPSNR to 3000..5000 (30..50 dB) and raises uMaxBitRate to the
target.  `AL_Common_Encoder_ComputeRCParam` adds, for modes 4 and 8 only, the
pixel count (+0x2c) and the peak value 255 (8-bit) at +0x32.  `encode1`
forces SliceParam +0x6e (cmd[9] bit 27) for modes 4 and 8; every OpenIMP T31
template already has that bit set.

`AL_EncChannel_Init` (0x69610) picks the controller with a 64-entry table on
the AL mode, `AL_RateCtrl_Init` (0x50860) builds it.  The controller code is
obfuscated (short `lIoi` names); its vtable shows the structure:

* CBR, VBR, CappedVBR and CappedQuality share one controller core (`oiii`
  0x52ce4, 328-byte state).  CBR: core flag (+0x12f) 1, own update.  VBR:
  flag 1, update `OOoI` (0x546f4) = picture analysis `Ioii` + apply the QP
  delta.  CappedVBR: flag 1, update `Ooii` (0x55540).  CappedQuality: flag 0,
  update `Ooii`.
* `Ooii` = the VBR update plus a quality cap.  From the picture's sum of
  squared errors (status +0x158 high, +0x15c low word; `EncodingStatusRegsTo-
  SliceStatus`):
  `mse1000 = max(1, sse * 1000 / pixels)`,
  `psnr = (int)(1000 * log10(255^2 * 1000 / mse1000))` (dB * 100).  When
  `psnr > uMaxPSNR * 100`, a negative QP delta becomes 0: the QP is not
  lowered while the picture already looks better than the cap.  Increases
  are never blocked.
* CappedVBR and CappedQuality differ in exactly one byte of the controller
  state: the core flag `state+0x12f` (`oiii` a2), 1 for CappedVBR
  (`AL_RateCtrl_Init` mode 8, from AL eRCMode 4) and 0 for CappedQuality
  (mode 9, from AL eRCMode 8; `AL_EncChannel_Init` table 0xe53e0).  Read in
  exactly one place, `Ioii` 0x55218.  Confirmed under emulation
  (`tools/t31_rc_emu/`): mode 8 with the byte patched is picture-for-
  picture identical to mode 9 and vice versa.  (The HRD object also has a
  flag `hrd+0x15 = (eRCMode != 9)` that lets the removal clock slip when 0,
  but eRCMode 9 does not exist on the T31, so it is always 1.)
  `Ioii` (the per-picture analysis, called after every picture with flag
  bit 1, i.e. every regular picture of a low-delay GOP) first computes the
  picture's per-type target (`state+0x8c` = uTargetBitRate / fps for GOP
  length < 2, otherwise the GOP budget split by the per-type size ratios
  `state+0xb0..`).  A picture *smaller* than its target starts a
  QP-lowering search: starting from delta 0, the picture size is scaled
  by the type's one-QP ratio (`state+0xf8+4*type`, 1.1225 = 2^(1/6) in
  1/10000) and delta decremented while the scaled size still stays below
  the target and delta > -4 (`state+0x22`).  Before the search, the
  controller compares the leaky-bucket idle time `state+0x84` (HRD object
  `state+0x48`, field +0x3c: accumulated ticks at 90 kHz during which the
  channel at uMaxBitRate had nothing to send because the picture would
  have arrived more than uInitialRemDelay ahead of its removal time) with
  the share `(uMaxBitRate - 0.95 * uTargetBitRate) / uMaxBitRate` of the
  elapsed stream time (`pictures * clkRatio * 90000 / (fps * 1000)`,
  computed in `Ioii` 0x549a8; 5 % when max == target).  Idle below that
  share: the stream has been running at the max rate; CappedVBR (flag 1)
  then skips the search and keeps the QP, CappedQuality (flag 0) searches
  anyway.  The mirrored test for a picture *larger* than its target (search
  up only while idle <= (uMaxBitRate - 1.04 * uTargetBitRate) / uMaxBitRate
  of the elapsed time) is the same in both modes.  Everything after the
  search is shared: the remaining-GOP budget check against the buffer
  (`i1Ii`/`IIIi` size predictions, a QP step +1 when the budget for the
  rest of the GOP plus the next I picture does not fit), the static-scene
  adjustment (`Ioli`), the +-4 clamp, and in `Ooii` the PSNR cap (a
  negative delta becomes 0 while PSNR > uMaxPSNR) before the delta is
  added to `state+0x20` and clamped to iMinQP..iMaxQP.
  In short: CappedVBR lowers the QP only while the bucket has been idle
  for the allowed share of the time (true VBR headroom); CappedQuality
  lowers the QP whenever a picture comes in under its target, bounded by
  the PSNR cap, the GOP budget and +-4 per update.
* CBR (`AL_RateCtrl_Init` mode 0, constructor `IOOi` 0x5468c, update
  `IIii` 0x53360): the same core and state (flag +0x12f = 1), the HRD in
  CBR mode (`hrd+0x14` = 1: no idle time, the arrival clock never waits
  for the removal clock, `IIIo` returns the bytes to stuff so the CPB does
  not overflow).  `IIii` is analysis and QP change in one, without the
  idle-time tests of `Ioii`:
  - re-encode flag: `l0ii` (+3 steps); filler bits > 0 for this picture
    (the OEM request +2848 = max(filler, 8) bytes): QP -2 steps (0 on a
    static scene while QP <= the initial QP), no analysis;
  - per-type target as `Ioii`, except: with `+0x12d` set ((eRcOptions & 5)
    != 1; the IMP default 1 clears it) the P budget comes from
    `state+0x90`, which every I picture sets to (bitrate * GOP time - I
    size) / (GOP length - 1); with it clear the P target carries a running
    correction `state+0xbc`, the I picture's miss against its target spread
    over the P pictures of the GOP; P targets are also stored at
    `state+0xec`;
  - budget = buffer level + (target per picture - predicted average P/B
    size at the current QP) * remaining GOP pictures;
  - a picture below its target searches the QP down (one-QP size ratios,
    max -4) only while budget > min(bitrate / 10, CPB - initial level) +
    initial level; above its target it searches up only while budget <
    initial level - min(bitrate / 10, initial level);
  - after the search, the budget at the new QP against the initial level
    (85 %/115 % for adaptive GOPs, 90 %/110 % for GOP mode 9, CPB/4 + the
    predicted I size and 3/4 CPB for a P picture, CPB/3 and 5/3 CPB of
    the HRD level for an I picture or GOP length 1) adds or removes one
    step; a P picture following an I picture keeps the QP; a P picture
    that lowered the QP marks +0x119 and the next one that would not lower
    it raises it by a step instead (keeps it when it is under 30 % intra
    and the budget is positive); the static-scene adjustment (`Ioli`, no increase while the
    budget is at or above the CPB), the +-4 clamp and the QP bounds close.

`IMP_Encoder_GetChnAttrRcMode` returns the stored IMP attribute (36 bytes),
so the read-back is the mode and values as given.

`IMP_Encoder_SetDefaultParam` (0x831a0, jump table 0xe9e34 on the rc
mode), H.264/H.265: iInitialQP = the caller's value (unchecked), iMinQP 15,
iMaxQP 48, iIPDelta -1, iPBDelta -1, eRcOptions 1, uMaxPictureSize =
2 * bitrate; VBR and the capped modes uMaxBitRate = (bitrate * 4) / 3; the
capped modes uMaxPSNR 42; modes 3/5/6/7 leave the rc fields 0.  OpenIMP T31
fills the same values (T40/T41 keep the former defaults: iMaxQP 45,
iInitialQP 26, uMaxBitRate = bitrate).  Not aligned: FIXQP/JPEG iInitialQP
(OpenIMP 25 outside 1..99, OEM the value as given), encOptions 0x40028 /
encTools 0x9c, HEVC level 50/tier 1, uGopCtrlMode 2 and uMaxSameSenceCnt
>= 1.

OpenIMP: two T31 controllers exist, selected by `OPENIMP_T31_RC_CORE`
(read once at the first picture; `allegro` is the default since the device
test of 2026-10-03, `legacy` restores the former controller):

* `legacy`: CBR and VBR run the OpenIMP closed-loop controller
  (`t31_rate_control.c`); `OPENIMP_T31_VBR_LOOP=0` restores the former
  open-loop VBR picture QP from the bitrate.  CappedVBR and CappedQuality
  run it with the OEM PSNR cap (`AL_Codec_Encode_SetRcQualityCap`, uMaxPSNR
  clamped to 30..50 dB): the PSNR is computed from the status SSE as above
  (integer log10), and a QP decrease at a GOP decision is dropped while the
  last picture is above the cap.  An SSE of 0 means no measurement (logged
  once) and the cap is inactive; the OEM would read it as a perfect
  picture.  The OEM difference between the two capped modes has no
  counterpart in this controller: both behave the same.  The log shows
  `T31 capped rc: qp=.. psnr=.. cap=.. holds=..` every 250 pictures.
* `allegro` (default): the OEM Allegro core ported instruction for instruction
  (`src/t40/t31_al_rc.c`: `lI1i` init and parameter update, `lI0i`/`lo0i`
  reset, `Il1i` picture QP, `o11i`/`l01i`/`llli`/`iili` picture start,
  `Ioii` with `Ilii`/`i0ii`/`O0ii`/`ooIi`/`i1Ii`/`illi`/`IIIi`/`Ioli`/
  `l0ii`, the HRD `l0io`/`i0Io`/`IIIo`/`OOlo`/`l1Io`/`i1Io`/`iiIo`, the
  updates `IIii` (CBR), `OOoI` (VBR) and `Ooii` (capped), `l1OI`, `loii`)
  for CBR, VBR, CappedVBR and CappedQuality.  The 328-byte state keeps the OEM layout;
  `tests/t31/al_rc_trace_test.c` replays traces recorded from the OEM code
  under emulation (`tools/t31_rc_emu/cq_trace.py` and `cq_random_cbr.py`,
  20 files, 3,000 pictures with parameter changes, resets, scene-change
  flags, re-encode flags, filler and fixed-QP pictures) and requires the
  state to be byte-identical after every call; 72 further random traces of
  400 pictures matched for the VBR modes, and for CBR 72 random traces of
  400 pictures with the IMP configuration plus 80 with the GOP and rc
  parameter variants the IMP never sets (B pictures, GOP modes 3/8/9,
  special-P flag, all eRcOptions), together 1,199 of the 1,227 `IIii`
  instructions (the rest: an I target that rounds to 0, the assert, and
  two branches on an initial-level change that cannot happen inside
  `IIii`).  The parameters are built as the OEM channel does (uInitialRemDelay
  216000 and uCPBSize 270000 ticks from `AL_Codec_Encode_SetDefaultParam`,
  uMaxBitRate raised to the target, iMinQP >= 10, iInitialQP within the
  bounds, uFrameRate/uClkRatio from the reduced fps fraction, GOP mode 2,
  eRcOptions/iPBDelta/uMaxBitRate from the IMP attribute), the macroblock
  statistics from the status registers 0x10c..0x12c, the SSE from
  0x158/0x15c, the picture size as the entropy byte count * 8.  Run-time
  bitrate/fps/QP-bound changes go through the OEM parameter update.  The
  filler value is per picture as in the OEM (the request object, +2848,
  is cleared by `AddNewRequest` 0x64d0c for every picture; until the CBR
  port OpenIMP kept it per channel, which never mattered because the VBR
  HRD never asks for filler).  Not reproduced: the OEM re-encode of a
  picture that overflowed its stream buffer (the re-encode flag is always
  0; a dropped picture is accounted with the buffer size) and the filler
  data the OEM appends in CBR when the HRD says so (`WriteFillerData`
  0x47e60; the count is fed back into the model, which therefore matches
  the OEM, but no filler NAL is written: on a static scene the CBR stream
  is smaller than the target where the OEM pads it).  Deviation from the OEM (2026-10-12): the
  CBR buffer debt (arrival time ahead of the removal clock) is bounded to
  4 CPBs and 256 Mbit (`hrd_bound_debt`); unbounded, as in the OEM without
  `IMP_ENC_RC_ENABLE_SKIP`, the 32-bit level arithmetic wraps after
  ~715 Mbit of debt and the QP drops from the maximum to the minimum
  (`tests/t31/al_rc_overload_test.c`).  Read-back: the attribute as given.  The log
  shows `T31 allegro rc: pic=.. size=.. used_qp=.. next_qp=.. psnr=..
  idle=..` for the first pictures and then every 250.

## T20 / T21 (Helix, OEM libimp T20 3.12.0, T21 1.0.33)

The OEM rate control is i264e `ratecontrol.c` on top of, on the T21, the
`eprc` controller in `jzm_enc_api_nofpic_t21.o` (`JZ_VPU_RC_VIDEO_CFG_T21`,
`FRAME_START`, `FRAME_END`; about 13,000 instructions) and, on the T20,
its own `JZ_VPU_RC_*_T20` controller (`docs/T20_RC.md`; the T10 build uses a
third one).  CreateChn copies the H.264
CBR/VBR/SMART fields unconditionally; `i264e_validate_parameters` then
clamps them (maxQp 0..51, minQp 0..maxQp, iBiasLvl -10..10 on T21, -3..3 on
T20, frm/gopQPStep 2..51, staticTime <= 0 -> 1, maxBitRate >= 128,
changePos 50..100, qualityLvl 0..6).  `IMP_Encoder_GetChnAttrRcMode` reads
the live, clamped values back (`i264e_get_param` 3); `GetChnAttr` returns
the stored attribute.  An application that passes 0 therefore reads
staticTime 1, changePos 50, qualityLvl 0, frmQPStep 2, gopQPStep 2 - not
the i264e defaults 2/80/4/3/15, which CreateChn always overwrites.

A run-time `IMP_Encoder_SetChnAttrRcMode` goes through `i264e_set_param` 3
-> `i264e_reconfig_rc_set` (T23 1.3.0 0x37cf8; T20/T21 the same code) with
other clamps: minQp and maxQp each 1..51 (no minQp <= maxQp), frm/gopQPStep
only negative -> 0, changePos 0..100, staticTime/maxBitRate/qualityLvl/
iBiasLvl as above.  The read-back then shows those.

OpenIMP: `GetChnAttrRcMode` applies the same clamps
(`src/t40/p2_rc_readback.h`; the run-time set after SetChnAttrRcMode, FIXQP
qp 0..51).  T21: the native Helix encoder runs the vendor-identical T21
1.0.33 `eprc` controller (`src/eprc/eprc_t21.c`) for FIXQP/CBR/VBR/SMART
by default, with the validated extras (see `docs/T23_EPRC.md`; FIXQP: I =
qp - 3, P = qp, at most 51, below qp 3 the I QP is 51, no QP range);
`OPENIMP_T21_EPRC=0` restores the old GOP controller (no extras),
`=23` runs the T23 controller (A/B only; needs a build with
`OPENIMP_T21_EPRC_AB=1`, otherwise the T23 revision is not linked in).  Device test cam-D at
1200 kbit/s: CBR 1326, VBR 1096, SMART 1071 kbit/s, decode clean (the old
controller: CBR 570).  T10/T20 run their OEM controllers by default
(below).

**T20 (since claude/t20-rc, default since claude/t1x-oem-rc-default):**
CBR, VBR and SMART run the OEM T20 controller (`src/rc_t20`,
`docs/T20_RC.md`; `OPENIMP_T20_RC=0` restores the GOP controller below).
The OEM T20 build has its own controller
(`JZ_VPU_RC_*_T20`), not the T21 eprc; there iBiasLvl, gopQPStep,
changePos, qualityLvl and gopRelation take effect, frmQPStep only until
the scene classifier replaces it, staticTime and adaptiveMode are unused,
and there is no I/P QP delta (FIXQP: I = qp - 3).  The table below is the
GOP controller of the T10 and T20 with `OPENIMP_T10_RC=0` /
`OPENIMP_T20_RC=0`, and of the T21 with `OPENIMP_T21_EPRC=0`.

**T10 (since claude/t20-rc, default since claude/t1x-oem-rc-default):**
CBR, VBR and SMART run the OEM T10 controller (`src/rc_t10`, `docs/T20_RC.md` "T10"), the older code base the
same OEM library selects on a T10: frmQPStep/gopQPStep/iBiasLvl/
changePos/qualityLvl/gopRelation take effect, staticTime and the I/P delta
do not; OEM VBR re-codes pictures above superFrm/1024 bits at QP + 3,
OpenIMP compares in bits (`OPENIMP_T10_RC_SUPERFRM`, default on).
`OPENIMP_T10_RC=0` restores the GOP controller below.

### What a write does on T10/T20/T21 (OpenIMP)

(T10/T20/T21 run the vendor controllers by default, see above; the table
below describes the GOP controller of T10/T20 with `OPENIMP_T10_RC=0` /
`OPENIMP_T20_RC=0` and of T21 with `OPENIMP_T21_EPRC=0`.)

T10 runs the T20 build (`t30_soc_is_t10`, only the command list differs),
so all three share this path.  The VPU has no rate control: OpenIMP's GOP
controller (`t31_rate_control.c`) picks the slice QP.  CreateChn and
`SetChnAttrRcMode` go through `AL_Codec_Encode_SetRcParam` into
`hw_params`; `OpenIMP_T30_HelixUpdateParams` adopts them before the next
picture.

| group | CreateChn | run time | used by the encoder |
|---|---|---|---|
| rc mode FIXQP/CBR/VBR/SMART | yes | SetChnAttrRcMode | FIXQP: fixed QP; CBR, VBR, SMART: the same controller (SMART = VBR) |
| bitrate | CBR outBitRate, VBR/SMART maxBitRate | SetChnAttrRcMode, SetChnBitRate (OpenIMP extra) | controller target (bitrate only: retarget, model kept) |
| min/max QP | yes | SetChnAttrRcMode, SetChnQpBounds (extra) | controller bounds (restart) |
| FIXQP qp | yes | SetChnAttrRcMode, SetChnQp (extra) | P = qp, I = qp (range = qp) |
| I/P QP delta | no field | SetChnQpIPDelta (extra) accepted, ignored | no (I and P same QP) |
| frmQPStep/gopQPStep | stored | stored | no |
| staticTime/changePos/qualityLvl | stored | stored | no |
| iBiasLvl, adaptiveMode, gopRelation | stored | stored | no |
| GOP | maxGop | SetGOPSize/SetChnGopLength | yes |
| fps | outFrmRate | SetChnFrmRate | yes (controller, VUI on T21) |

Ignored writes are read back (clamped as the OEM) but have no effect.

## T23 (Helix, OEM libimp 1.3.0)

i264e_ratecontrol_init (same eprc code as T21): VBR -> eprc rcMode 2, SMART
-> eprc rcMode 3 with the same VBR fields and, in addition, gopMode 1
(SmartP fields: `u32BgInterval = gop * param[0xac4]`, `s8BgQpDelta 3`,
`s8ViQpDelta 3`) instead of gopMode 0 (`gopNormalP.s8IPQpDelta 3`).
`docs/T23_EPRC.md` has the controller in detail: the SmartP fields are not
read by the picture QP path, and the OEM SMART uses no long-term reference
(long-term references belong to the HSkip modes H1M).  The OEM
`GetChnAttrRcMode` reads live values as on T21: `IMP_Encoder_CreateChn`
(0x4e164) copies the H.264 fields unconditionally,
`i264e_validate_parameters` (0x33780) clamps them as on T21 (iBiasLvl
-10..10), `i264e_reconfig_init` (0x35b50) copies them into the live block
`i264e_reconfig_rc_get` (0x38070) returns.  OpenIMP T23 reads back through
`p2_rc_readback.h` as T20/T21.

The native T23 encoder uses the same values (`t23_rc_config`): the CreateChn
clamps (staticTime 0 -> 1, changePos 0 -> 50, frm/gopQPStep 0 -> 2,
qualityLvl > 6 -> 6, iBiasLvl -10..10 for every mode, maxBitRate >= 128
kbit/s) and after a run-time SetChnAttrRcMode the reconfig_rc_set clamps.
Before, it took the `i264e_param_default` values for 0 (2/80/4/3/15), a rule
of `IMP_Encoder_YuvInit` (0x585f4), not of CreateChn.  Consequence: a VBR
or SMART channel without extras now targets 50 % of maxBitRate (was 80 %).

OpenIMP: `src/eprc` reimplements the OEM controller (bit-exact under
emulation, `tests/eprc`); the native encoder feeds it the same validated
values (`t23_rc_config`) and runs it for SMART by default and for
FIXQP/CBR/VBR with `OPENIMP_T23_EPRC=1` (`=0`: the band mapping for all, as
before).  Run-time changes (SetChnAttrRcMode, frame rate, GOP, HSkip) take
effect at the next IDR, the controller restarting from the last QP (OEM
`i264e_idr_reconfig`); with maxSameSceneCnt > 0 a scene change (picture
class 5) codes an IDR early at a GOP boundary.

## T40 (libimp 1.3.1) / T41 (1.2.6)

Both vendor libraries run CappedVBR and CappedQuality like the T31:
`AL_Codec_Encode_ValidateRcParam` (T40 0x5ef68) / `AL_Codec_Encode_Create`
(T41 0x324c4) clamp uMaxPSNR * 100 to 3000..5000, `AL_Common_Encoder_Compute-
RCParam` sets pixels and peak (255) for AL modes 4 and 8, and
`AL_RateCtrl_Init` modes 6/7 (T31: 8/9) build the shared core with the
CappedVBR flag 1/0 (T40 `liOi`, T41 `l1Oi` -> core `llio` 0xee5f0, flag at
state +410).  The update (T40 `lIii` 0x3e990, T41 `OooI` 0xeec20) is the VBR
analysis (T41 `io0o` 0xf0fc0, same idle-share test on the flag at 0xf13b8)
followed by the T31 PSNR rule (`mse1000 = sse * 1000 / pixels`,
`psnr = 1000 * log10(peak^2 * 1000 / mse1000)`, a negative delta becomes 0
while psnr > cap).  SSE: T40 status +0x158 high / +0x15c low (slice +108/
+104), T41 slice status +0x60 low / +0x64 high (status image +0x50/+0x54).

OpenIMP (`claude/capped-rc-t4x`): both SoCs run the ported Allegro core
(`t31_al_rc.c`) in modes 8/9 with the uMaxPSNR cap
(`AL_Codec_Encode_SetRcQualityCap`, 30..50 dB).  T40 feeds it the published
AU size and the picture SSE (other statistics 0, as for its CBR/VBR).  T41
runs it for VBR and the capped modes (`claude/capped-rc-followup`: plain VBR
too, vendor `AL_RateCtrl_Init` mode 1 -> update `o1l` 0xf3880 = analysis
`io0o` + QP delta + `Io0o`, the T31 VBR mode 1) with the slice statistics and
SSE; the core's QP is the picture QP of the command and the slice header.
CBR keeps the T41 controller.  The AL mode, uMaxBitRate, eRcOptions and
iPBDelta survive the lazy AVPU initialisation on T31, T40 and T41 (T31 before
`claude/capped-rc-followup`: lost when the mode was set only at CreateChn, so
CappedVBR/CappedQuality ran as VBR with max = target).

T41 AVC slice header (`claude/capped-rc-followup`): slice_qp_delta now comes
from the T41 command QP (`avpu_t41_command_picture_qp`) like HEVC.  Before it
came from the T40 open-loop picture QP, which follows only the CBR
controller: every VBR and capped stream (HWRC-only VBR as well) carried
QP 34 (or 23) in the header while the command encoded another QP, so CABAC
started with a wrong context and the AVC streams did not decode (100-240
broken pictures per minute, grey smear).  CBR and FixQP were not affected.  The runtime marker is `OpenIMP_Cap_CappedRc`
= 1 (replaces `OpenIMP_Cap_CappedRcAsVbr`).

Device (T41 Cinnado, sc5336, 1280x720@15, static scene, 2026-10-18):
target/max 3000/4000 kbit/s: CappedVBR cap 50 dB 3111, cap 38 dB 1665
(-46 %), CappedQuality cap 38 dB 1745; target/max 300/400: CappedVBR 300
(plain T41 VBR 585); target/max 800/1600: CappedVBR 774 (below max),
encoder PSNR 37.4..38.9 dB at cap 38.  0 oops.  T40: host test only.
These T41 streams did not decode (slice header QP, see above); the bit rates
stand, the pictures were broken.

Re-measured with the header fix (`claude/capped-rc-followup`, 2026-10-18
night, static scenes, 1280x720@15, 40 s, GOP 30, QP 15..45, every stream
decodes with 0 errors).  SSIM/PSNR (luma) against a FixQP 12 encode of the
same pictures on channel 1 (`ROITEST_CH1_QP`), frames 300..600:

| SoC | mode | target/max | kbit/s | encoder PSNR | SSIM / PSNR vs ref |
|---|---|---|---|---|---|
| T41 | VBR before (HWRC only) | 300/400 | 389 | - | broken (0.58) |
| T41 | VBR | 300/400 | 291 | 42.6 | 0.974 / 41.6 |
| T41 | VBR before (HWRC only) | 800/1600 | 384 | - | broken (0.60) |
| T41 | VBR | 800/1600 | 829 | 44.0 | 0.985 / 44.7 |
| T41 | VBR | 3000/4000 | 2981 | 46.1 | 0.990 / 47.0 |
| T41 | CappedVBR 38 dB | 3000/4000 | 1696 | 46.1 | 0.989 / 46.7 |
| T41 | CappedQuality 38 dB | 3000/4000 | 1695 | 46.1 | 0.989 / 46.7 |
| T41 | CappedQuality 50 dB | 3000/4000 | 3309 | 45.1 | 0.985 / 42.6 |
| T41 | CappedVBR 50 dB | 300/400 | 278 | 35.7 | 0.963 / 38.2 |
| T41 | CBR before / after (2 runs each) | 800 | 797-799 / 788-799 | - | 0.982-0.983 / 0.982-0.983 |
| T41 | FixQP 30 before / after | - | 141 / 141 | - | 0.969 / 0.969 |
| T31 | VBR before / after | 800/1600 | 766 / 770 | 39.4 / 39.4 | 0.967 / 0.965 |
| T31 | VBR | 3000/4000 | 3108 | 43.4 | 0.981 / 43.2 |
| T31 | CappedVBR 38 dB before (ran as VBR) | 3000/4000 | 2895 | - | - |
| T31 | CappedVBR 38 dB | 3000/4000 | 507 | 38.5 | 0.963 / 39.1 |
| T31 | CappedQuality 38 dB | 3000/4000 | 508 | 38.5 | 0.963 / 39.1 |
| T31 | CappedQuality 50 dB | 3000/4000 | 3338 | 42.3 | 0.981 / 43.3 |
| T31 | CappedVBR 50 dB | 300/400 | 283 | - | - |
| T31 | CBR before / after | 800 | 802 / 807 | - | 0.969 / 0.967 |

CappedVBR/CappedQuality stay at or below uMaxBitRate and fall well below it
where the picture already meets the cap (T31 lit scene: the encoder PSNR
settles at the 38 dB cap, 507 instead of 3108 kbit/s; T41 IR scene: QP held
at 19, -43 %).  A 50 dB cap is not reached by these scenes (encoder PSNR
42-45 dB), so the modes run as VBR up to uMaxBitRate.  The CappedQuality
50 dB run was at night (IR on T41, garage light on T31); a daylight repeat is
still open.  T31 plain VBR now uses uMaxBitRate and eRcOptions 1 like the
vendor; at this scene the result is unchanged.

T10 (NVPU) was not changed: CappedVBR/CappedQuality run as VBR there.
