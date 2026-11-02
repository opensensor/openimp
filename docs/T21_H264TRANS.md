# T21: IMP_Encoder_Set/GetH264TransCfg (issues #3, #5, #7)

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
