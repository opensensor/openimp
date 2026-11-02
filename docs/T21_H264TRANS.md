# T21, T20, T10: IMP_Encoder_Set/GetH264TransCfg (issues #3, #5, #7)

`IMPEncoderH264TransCfg` holds one value, `chroma_qp_index_offset`.  The vendor libimp 1.0.33 (T21) and OpenIMP's T21 build
code it in both `chroma_qp_index_offset` fields of the PPS and in the command list (register 0x40120) from the next IDR picture on
(`docs/T1X_ROI_CHROMA.md`).  This page lists the call semantics that the vendor library has and that the cameras showed; OpenIMP's T21
build follows them (before: -1 for a channel that was not created or a value outside -12..12, and `Get` returned the value just set).

## What the T21 library does (disassembly of 0x493dc, 0x4957c, `i264e_reconfig_trans_set`, `i264e_idr_reconfig`; camera A/B)

* `Set`: a channel number of 6 or more, or a NULL pointer, is -1.  Of an **H.264 channel that exists**, the raw value is handed to the
  encoder without a range check (return 0); the encoder takes it over at the next IDR picture (`i264e_idr_reconfig`), **clamped to
  -12..12** (13 and 100 are coded as 12, -13 as -12) in both PPS fields and in the command list.
* `Set` of any other channel (not created yet, destroyed, JPEG) does nothing and returns **0**.
* `Get`: the structure is zeroed first; channel 6 or more or a NULL pointer is -1 (structure untouched on a channel error).  An
  H.264 channel returns the offset the encoder codes with **now**: a `Set` shows only after the next IDR picture.  Any other channel
  returns 0 with the zeroed structure.
* The vendor indexes its channel table with a negative channel number without a check and answers 0; OpenIMP answers -1.

## OpenIMP

`IMP_Encoder_SetH264TransCfg` / `GetH264TransCfg` (T21 build, `src/t40/openimp_p2_encoder.c`) have exactly these semantics;
`AL_Codec_Encode_SetChromaQpOffset` clamps instead of refusing, `AL_Codec_Encode_GetChromaQpOffset` returns the offset of the Helix encoder
(`OpenIMP_T30_HelixChromaQpOffset`), which `t30_chroma_qp_idr` changes at the IDR picture.

## Proof

* Cameras (T21 test camera, `apitest` case `transcfg`, the vendor library and OpenIMP on the same camera): return codes and read-backs of
  one call sequence (Set before CreateChn, after CreateChn at pictures 5 / 20 / 35, a JPEG channel, a destroyed channel, channel 6, NULL
  pointer) are identical; the PPS offsets of the streams (ffmpeg `trace_headers`) are 0 0 5 -4 12 12 for 5, -4, 12 and 0 0 12 -12 12 12
  for 13, -13, 100 in both libraries.
* Host: `helix_encoder_test_t21` (PPS and command list offset at the IDR, clamp).

## T10 and T20 (libimp 3.12.0, one binary; SetH264TransCfg 0x4b8d4, GetH264TransCfg 0x4ba7c)

Same `i264e_set_param(h, 14, ...)` as the T21, so `Set` is the T21 semantics (channel 6 and above and NULL -1, raw value without a range check, coded clamped
to -12..12 from the next IDR picture on).  Differences found by disassembly and camera A/B (vendor library via `LD_LIBRARY_PATH`, `apitest` case
`transcfg`, T10 and T20 test cameras):

* `Get` **never shows the offset**: it zeroes the 144-byte structure, sets `intraTransMode` and `interTransMode` to 1 and returns 0 (the value of
  `i264e_get_param` is not copied; read back during a stream with the PPS carrying 5, -4 or 12: always 0).  OpenIMP's T10/T20 build does the same.
* `Set` before `CreateChn` (a channel that does not exist) reads an invalid handle in the vendor library and ends the process on both SoCs; OpenIMP answers as
  the T21 (0, nothing happens).
* T20: the offset is coded in the PPS and in the command list (register 0x40120): decoded chroma stays where it is (mean 113.8 / 141.7 with 12 and 0).
* T10: `H264E_T10_SliceInit` has no chroma register, so the vendor T10 codes the offset **in the PPS alone**.  Camera (vendor library, FixQP 30,
  offset 12 from picture 1, next IDR at 15): decoded U mean 154.3 -> 190.6, spread 5.3 -> 13.8, V mean 159.8 -> 203.9, spread 9.0 -> 22.6, i.e. a colour
  shift from that IDR picture on (offset 0: 154.2 / 5.3 and 159.9 / 9.0 throughout).  Writing 0x40120 in the T10 command list does not change this (same
  shift, tested), so the T10 hardware has no way to apply the offset.  OpenIMP's T10 therefore accepts the call with the vendor return codes and read-back
  but keeps the PPS at 0 (`t30_chroma_qp_idr`): equal API, correct colours.  Logged once as a warning.

Proof: one call sequence (Set 5 at picture 5, -4 at 20; 13, -13, 100; 12 at picture 1, Get every 10 pictures): return codes and Get results equal in both
libraries on both cameras (identical output lines); PPS offsets of the T20 streams equal to the vendor's (5, -4 and 12, -12, 12); T10 PPS stays 0 in OpenIMP
(vendor 5, -4 / 12, -12, 12); the T10 OpenIMP stream keeps chroma mean and spread constant.  Host: `p2_t1x_test_t20`.
