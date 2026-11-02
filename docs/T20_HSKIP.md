# T10 / T20: IMP_Encoder_SetChnHSkip, BlackEnhance and ChangeRef (issues #3, #5)

The vendor libimp 3.12.0 (T10 and T20 are one binary) codes a *reference structure* when the skip
type of `rcAttr.attrHSkip.hSkipAttr` is not `IMP_Encoder_STYPE_N1X`.  OpenIMP's own H.264 path
(`src/t30/t30_helix_encoder.c`, T10/T20 build) now does the same; the decisions are `src/hskip`
(pure C, `tests/hskip`), checked against the OEM code with `tools/hskip_oracle.py` (unicorn).

## What the OEM does (libimp 3.12.0)

* `IMP_Encoder_SetChnHSkip` -> `i264e_reconfig_hskip_set` (0x34224): the six words wait; the next
  **IDR picture** (no extra IDR) takes them over in `i264e_idr_reconfig` (0x3355c):
  `i264e_init_skip_header` (0x32110), rate control init, a second `i264e_decide_slice_type_and_rd`.
  The GOP / fps / rate-control setters wait for the IDR the same way.
* `i264e_init_skip_header` derives from (gop, type, m, n, maxSameSceneCnt) the spacing A, B and the
  number of reconstruction buffers C: N1X (1, 0, 2), N2X (1, 1, 2), N4X (2, 1, 3),
  HN1_FALSE ((gop-1)/2, 1, 3), HN1_TRUE (clip(n, 1, (gop-1)/2), 1, 3),
  H1M_* (1, maxSameSceneCnt ? gop-1 : clip(m, 0, gop-1), 3).
* `i264e_decide_slice_type_and_rd` (0x322b4): per picture the counter (frame_num), IDR or P, the
  frame type (0 IDR, 1 LBASE, 2 SBASE, 3 ENHANCE), the reference and reconstruction buffer (0..C-1),
  and the slice header commands.  For N1X and H1M with `maxSameSceneCnt > 0` the counter runs over
  `maxSameSceneCnt x gop` pictures (an IDR every n GOPs); on T20 (not T10) a scene cut reported by
  the rate controller (E+0x90168 == 4) at a GOP boundary makes that picture an IDR.
* Slice header / SPS (what a decoder sees, `tools/hskip_trace.py` of OEM streams): SPS
  `max_num_ref_frames` 2 and `gaps_in_frame_num_allowed`; IDR `long_term_reference_flag` 1 for every
  type but N1X; LBASE / SBASE pictures carry a reference list modification to long-term picture 0;
  LBASE also MMCO {1: the previous picture unused, 6: long-term index 0 for this picture}.  ENHANCE
  pictures are plain P pictures (`nal_ref_idc` 2 as every P picture) that no base picture refers to,
  so a client can drop them.
* `IMP_Encoder_SetChnHSkipBlackEnhance` (applied from the next picture): an ENHANCE picture is not
  coded by the encoder but written in software by `i264e_icodec_black` (0x3dfe8): QP 0 and the slice
  data of an all-skip P slice (CABAC), 21 bytes at 640x360 against 1 to 5 kB of a coded picture.
  `src/hskip/hskip_black.c` writes the same bytes (8165 macroblock counts compared).
* Rate controller: `i264e_ratecontrol_start` (0x3caa8) writes a class byte around the controller call
  (2 while it plans, 1 afterwards) for the HSkip pictures of N1X at a GOP boundary, H1M_FALSE SBASE
  and H1M_TRUE LBASE (T10: not N1X); the controller counts those pictures like GOP starts, and the
  re-encode judge reads the byte.
* `IMP_Encoder_SetChangeRef` -> `i264e_change_ref_set` (flag applied by `i264e_reconfig` with the
  next picture): the controller flag E+216 (T10 E+225, read when the controller is set up).  With the
  class byte 1 in SMART mode an oversized picture is coded again; the flag has an effect only
  together with an HSkip structure (default on).

## Proof

* Host: `tests/hskip` (76 scenarios x 240 pictures: 0 differences; 635 black slices: 0 differences),
  `tests/rc_t20` / `tests/rc_t10` (28 scenarios x 200 pictures with a structure, ChangeRef on and off,
  all decisions of the OEM controller: QP, picture type, re-encode: 0 differences).
* Device: see the comments on the issues (`tools/apitest` case `hskip`, `tools/hskip_trace.py`).

## Not done

* `bEnableScenecut` (i264e_decide_reset_slice_to_idr 0x32aa0): the H1M structures can turn a coded
  picture that is larger than the last IDR into an IDR once per second and code it again; the flag is
  stored and read back only.
* A super-frame re-encode of a picture inside an H1M structure (`i264e_dec_ref_pic_remark`,
  `i264e_get_prev_fram_dec`) uses the plain header of the first try.
