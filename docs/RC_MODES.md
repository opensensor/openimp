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
* CappedVBR and CappedQuality differ in exactly two bytes of the controller
  state, both set from the `AL_RateCtrl_Init` mode (8 = CappedVBR, 9 =
  CappedQuality; AL mode 4 -> 8, AL mode 8 -> 9 in the `AL_EncChannel_Init`
  table).  Confirmed under emulation (unicorn, `tools/t31_rc_emu/`): mode 8
  with the two bytes patched to the mode-9 values is picture-for-picture
  identical to mode 9 and vice versa, in static, busy and scene-change
  scenarios.
  1. Core flag `state+0x12f` (`oiii` a2): 1 for CappedVBR, 0 for
     CappedQuality.  Read in exactly one place, `Ioii` 0x55218.  `Ioii`
     (the per-picture analysis, called after every picture with flag bit 1,
     i.e. every regular picture of a low-delay GOP) first computes the
     picture's per-type target (`state+0x8c` = uTargetBitRate / fps for GOP
     length < 2, otherwise the GOP budget split by the per-type size ratios
     `state+0xf8..`).  A picture *smaller* than its target starts a
     QP-lowering search: starting from delta 0, the picture size is scaled
     by the type's one-QP ratio (`state+0xf8+4*type`, 1.1225 = 2^(1/6) in
     1/10000) and delta decremented while the scaled size still stays below
     the target and delta > -4 (`state+0x22`).  Before the search, the
     controller compares the leaky-bucket idle time `state+0x84` (HRD object
     `state+0x48`, field +0x3c: accumulated ticks at 90 kHz during which the
     channel at uMaxBitRate had nothing to send because the picture would
     have arrived more than uInitialRemDelay ahead of its removal time) with
     5 % of the elapsed stream time (`pictures * clkRatio * 90000 / (fps *
     1000) * 5 / 100`, computed in `Ioii` 0x549a8).  Idle < 5 %: the stream
     has been running at the max rate; CappedVBR (flag 1) then skips the
     search and keeps the QP, CappedQuality (flag 0) searches anyway.  The
     mirrored test for a picture *larger* than its target (search up only
     while idle <= (uMaxBitRate - 1.04 * uTargetBitRate) / uMaxBitRate of
     the elapsed time) is the same in both modes.  Everything after the
     search is shared: the remaining-GOP budget check against the buffer
     (`i1Ii`/`IIIi` size predictions, a QP step +1 when the budget for the
     rest of the GOP plus the next I picture does not fit), the static-scene
     adjustment (`Ioli`), the +-4 clamp, and in `Ooii` the PSNR cap (a
     negative delta becomes 0 while PSNR > uMaxPSNR) before the delta is
     added to `state+0x20` and clamped to iMinQP..iMaxQP.
  2. HRD flag `hrd+0x15` = (mode != 9): 1 for CappedVBR, 0 for
     CappedQuality.  Read in exactly one place, the bucket update `i0Io`
     0x5674c (called from `i0ii` for every picture with its size).  With
     the flag set the function returns after accounting the picture; with
     flag 0 it additionally lets the removal clock slip: when the picture's
     arrival time (bits sent at uMaxBitRate) is later than its scheduled
     removal time (one frame period after the previous one), the removal
     time is set to the arrival time.  The bucket level (`OOlo`, removal
     minus arrival in bits) therefore never goes negative in CappedQuality:
     an overload does not produce an underflow and no emergency QP (`ooIi`
     raises the QP to the maximum when a picture exceeds 75 % of the level),
     the QP rises step by step through the remaining-GOP budget check
     instead.  CappedVBR keeps the hard delivery schedule (and in the
     emulated scene change jumped to iMaxQP and stayed there while the
     bucket recovered).
  In short: CappedVBR lowers the QP only while the bucket has been idle at
  least 5 % of the time (true VBR headroom) and enforces the HRD schedule;
  CappedQuality lowers the QP whenever a picture comes in under its target
  (bounded by the PSNR cap, the GOP budget and +-4 per update) and treats
  the delivery schedule as elastic.  The emulated static scene at 2 Mbit/s
  target / 4 Mbit/s max: CappedVBR settles at one QP per GOP, CappedQuality
  oscillates +-1 around a QP one to two steps lower and spends ~6 % more
  bits.

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

OpenIMP: CBR and VBR run the closed-loop T31 controller
(`t31_rate_control.c`), as the OEM; `OPENIMP_T31_VBR_LOOP=0` restores the
former open-loop VBR picture QP from the bitrate.
CappedVBR and CappedQuality run the controller with the OEM PSNR cap
(`AL_Codec_Encode_SetRcQualityCap`, uMaxPSNR clamped to 30..50 dB): the
PSNR is computed from the status SSE as above (integer log10), and a QP
decrease at a GOP decision is dropped while the last picture is above the
cap.  An SSE of 0 means no measurement (logged once) and the cap is
inactive; the OEM would read it as a perfect picture.  The OEM difference
between the two modes (above) is not reproduced: OpenIMP's T31 controller is
not the Allegro core (no leaky bucket at uMaxBitRate, no per-type targets,
GOP-granular decisions), so the idle-time gate and the HRD slip have no
counterpart and both modes behave the same.  A vendor-equal CappedVBR /
CappedQuality needs the Allegro VBR core itself (init `lI1i`, picture QP
`o11i`/`Il1i`, update `Ioii` with `i0ii`/`Ilii`/`O0ii`/`ooIi`/`i1Ii`/
`IIIi`/`Ioli`/`l0ii`, HRD `l0io`/`i0Io`/`OOlo`/`l1Io`/`iiIo`, cap `Ooii`,
about 2,800 instructions) and the macroblock statistics it reads from the
status block (+0x14..+0x30).  The
log shows `T31 capped rc: qp=.. psnr=.. cap=.. holds=..` every 250
pictures.  Read-back: the attribute as given (as the OEM).

## T20 / T21 (Helix, OEM libimp T20 3.12.0, T21 1.0.33)

The OEM rate control is i264e `ratecontrol.c` on top of the `eprc` controller
in `jzm_enc_api_nofpic_t21.o` (`JZ_VPU_RC_VIDEO_CFG_T21`, `FRAME_START`,
`FRAME_END`; about 13,000 instructions).  CreateChn copies the H.264
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
qp 0..51).  The native Helix encoder runs its GOP controller for
CBR/VBR/SMART alike, without the extras (unchanged).

### What a write does on T10/T20/T21 (OpenIMP)

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
(SmartP: background long-term reference, `gopSmartP.u32BgInterval = gop *
param[0xac4]`, `s8BgQpDelta 3`, `s8ViQpDelta 3`) instead of gopMode 0
(`gopNormalP.s8IPQpDelta 3`).  A vendor-equal SMART therefore needs both the
eprc controller and a SmartP GOP (long-term reference) in the native
encoder; neither exists, so SMART keeps the documented band mapping
(`T23_NATIVE_HELIX.md`).  The OEM `GetChnAttrRcMode` reads live values as
on T21: `IMP_Encoder_CreateChn` (0x4e164) copies the H.264 fields
unconditionally, `i264e_validate_parameters` (0x33780) clamps them as on
T21 (iBiasLvl -10..10), `i264e_reconfig_init` (0x35b50) copies them into
the live block `i264e_reconfig_rc_get` (0x38070) returns.  OpenIMP T23 now
reads back through `p2_rc_readback.h` as T20/T21.

The native T23 encoder uses the same values (`t23_rc_config`): the CreateChn
clamps (staticTime 0 -> 1, changePos 0 -> 50, frm/gopQPStep 0 -> 2,
qualityLvl > 6 -> 6, iBiasLvl -10..10 for every mode, maxBitRate >= 128
kbit/s) and after a run-time SetChnAttrRcMode the reconfig_rc_set clamps.
Before, it took the `i264e_param_default` values for 0 (2/80/4/3/15), a rule
of `IMP_Encoder_YuvInit` (0x585f4), not of CreateChn.  Consequence: a VBR
or SMART channel without extras now targets 50 % of maxBitRate (was 80 %).

## T10 / T40 / T41

T10 (NVPU) and T40/T41 were not changed: CappedVBR/CappedQuality still run
as VBR there (one log line).
