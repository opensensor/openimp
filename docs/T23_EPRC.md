# Helix "eprc" rate control (T21/T23) - work in progress

Reimplementation of the OEM picture rate control (`JZ_VPU_RC_*_T21`, T23
libimp 1.3.0) as a platform-neutral module, `src/eprc/` (no hardware access).

## Done (bit-exact against the OEM code under emulation)

- i264e_ratecontrol_init (0x40c7c, eprc part), eprc_default_set_T21
  (0xc55b0), JZ_VPU_RC_VIDEO_CFG_T21 (0xc5afc): `EPRC_Init`.  60 random
  CBR/VBR/SMART configurations match the OEM state block byte for byte.
  i264e param[292]/[296] follow i264e_validate_parameters (0x33ab0).

## Findings

- Interface: i264e rc block +496 ("E"); state block E+1620 (7152 bytes +
  arrays sized by GOP and MB count).  FRAME_START outputs: E+1592 type
  (2 first I, 6 SMART GOP start reported as 0, 0 P), E+1600 QP, E+1596;
  slice fields 448/808 (QP: 0x40000, 0x90018, 0x40074, 0x80114), 809
  (max QP: 0x40040), 810 (min QP), 1058..1063 (lambda: 0xb001c/0xb0020).
- FRAME_END inputs: coded bytes and 25 Helix status registers read via
  ioctl 0xc0586307 after each picture (table 0xd7a40: 0x80120..0x8014c,
  0x500e8..f0, 0x80080, 0x40094..a0, 0x800e0..ec, 0x80168).
- QP model: R-lambda (estimate_qp 0xbf9e4): lambda = a*bpp^b,
  QP = 4.2005*ln(lambda)+13.7122.
- SMART differs from VBR by +148 = 5 (GOP-start pictures of type 6 with
  gop_init 0xc1158 per GOP) and the SmartP GOP fields (+120 BgInterval,
  +124/+125 Bg/ViQpDelta 3).
- T21 1.0.33 has the same eprc (same function sizes except FRAME_START
  +976 bytes, REPEATE_JUDGE, h264_api_enc split out on T23).  T20/T10
  3.12.0 use a different, smaller controller (JZ_VPU_RC_*_T20).
- Per-MB adaptive QP (h264_api_enc/h264_get_mb_qp: SAS/CRP offsets,
  0x40078..0x40084, 0x400c0/c4) is separate from the frame RC; OpenIMP's
  T21 list emits constants there.

## Open

- FRAME_START port (draft, not built in), FRAME_END, REPEATE_JUDGE
  (OEM may re-encode a picture), gop_init.
- Unknown inputs: slice+1128 (hwicodec ctx[0], used for the CBR/VBR
  size cap flag slice+1124), E+1796..1824 (i264e picture fields 392..404),
  R+2768/R+2820 (HSkip reference mode, 0 by default).
- gop == 1: one branch reads a stale register in the OEM code.
