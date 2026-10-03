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
* The core flag (+0x12f), the only difference between CappedVBR and
  CappedQuality, gates a QP-lowering search inside `Ioii` (0x55218): with
  flag 0 (CappedQuality) the search runs in a case where VBR/CappedVBR skip
  it.  The condition compares state fields that were not identified, so the
  CappedQuality-specific part is **not determined**.

`IMP_Encoder_GetChnAttrRcMode` returns the stored IMP attribute (36 bytes),
so the read-back is the mode and values as given.

`IMP_Encoder_SetDefaultParam` (0x831a0), H.264/H.265: iInitialQP = the
caller's value, iMinQP 15, iMaxQP 48, iIPDelta -1, iPBDelta -1, eRcOptions 1,
uMaxPictureSize = 2 * bitrate; VBR and the capped modes uMaxBitRate =
4/3 * bitrate; the capped modes uMaxPSNR 42.  OpenIMP sets uMaxPSNR 42 for
the capped modes; its other defaults (iMaxQP 45, iInitialQP 26, uMaxBitRate
= bitrate, no PB delta/options/picture size) differ and are unchanged.

OpenIMP: CBR runs the closed-loop T31 controller (`t31_rate_control.c`),
VBR an open-loop picture QP from the bitrate (unchanged; the OEM VBR is
closed-loop, `OPENIMP_T31_VBR_LOOP=1` runs the controller for VBR too).
CappedVBR and CappedQuality run the controller with the OEM PSNR cap
(`AL_Codec_Encode_SetRcQualityCap`, uMaxPSNR clamped to 30..50 dB): the
PSNR is computed from the status SSE as above (integer log10), and a QP
decrease at a GOP decision is dropped while the last picture is above the
cap.  An SSE of 0 means no measurement (logged once) and the cap is
inactive; the OEM would read it as a perfect picture.  The undetermined
CappedQuality search is not reproduced: both modes behave the same.  The
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

OpenIMP: `GetChnAttrRcMode` applies the same clamps
(`src/t40/p2_rc_readback.h`).  The native Helix encoder runs its GOP
controller for CBR/VBR/SMART alike, without the extras (unchanged).

## T23 (Helix, OEM libimp 1.3.0)

i264e_ratecontrol_init (same eprc code as T21): VBR -> eprc rcMode 2, SMART
-> eprc rcMode 3 with the same VBR fields and, in addition, gopMode 1
(SmartP fields: `u32BgInterval = gop * param[0xac4]`, `s8BgQpDelta 3`,
`s8ViQpDelta 3`) instead of gopMode 0 (`gopNormalP.s8IPQpDelta 3`).
`docs/T23_EPRC.md` has the controller in detail: the SmartP fields are not
read by the picture QP path, and the OEM SMART uses no long-term reference
(long-term references belong to the HSkip modes H1M).

OpenIMP: `src/eprc` reimplements the OEM controller (bit-exact under
emulation, `tests/eprc`); the native encoder runs it for SMART by default
and for CBR/VBR with `OPENIMP_T23_EPRC=1` (`=0`: the band mapping for all,
as before).  The OEM `GetChnAttrRcMode` reads live values as on T21;
OpenIMP T23 returns the attribute as given (unchanged).

## T10 / T40 / T41

T10 (NVPU) and T40/T41 were not changed: CappedVBR/CappedQuality still run
as VBR there (one log line).
