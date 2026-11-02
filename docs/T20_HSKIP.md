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
  next picture, default on) -> the controller flag E+216 (T10 E+225, read when the controller is
  set up).  In SMART mode the re-encode judge asks for a second try of the picture classes above
  (flag E+0x90385).  The second try itself is `i264e_slice_write`, and it is not tied to the flag:
  **every** re-encode of an H1M_FALSE SBASE or H1M_TRUE LBASE picture (also the super-frame
  re-encode of CBR / VBR) is coded with another reference.  The kept first try is saved
  (`i264e_ratecontrol_is_reenc`), the picture is coded again with the QP of the judge and the
  reference named by `i264e_get_prev_fram_dec` (the one reconstruction buffer that is neither the
  reference nor the reconstruction buffer of the picture, i.e. the previous picture of the three-buffer
  ring), `i264e_ratecontrol_is_reenc_end` keeps the smaller try (payload bytes) and
  `i264e_dec_ref_pic_remark` gives the kept second try its header commands: no reference list
  modification (the decoder's default reference is the previous picture), an LBASE picture keeps its
  MMCO, an SBASE picture gets no MMCO and the frame type ENHANCE.  The judge then looks at the kept
  picture again (up to four tries in OpenIMP).
* `i264e_decide_slice_qp` (0x428b8, every picture): LBASE pictures, and SBASE pictures of H1M_FALSE
  without an IDR period of several GOPs, are coded at the QP of the last IDR picture instead of the
  controller's QP (`E+0x90450`, written by `i264e_ratecontrol_end` for every I slice).
* `bEnableScenecut` (`i264e_decide_reset_slice_to_idr`, 0x32aa0, after the first try of a picture):
  an LBASE / SBASE picture of an H1M type, at a whole second of the frame rate
  (`counter * fps_den % fps_num == 0`), that is larger than the last IDR picture (`E+0x9044c`, bits)
  is coded again as an IDR picture: the rate controller is set up again
  (`i264e_ratecontrol_init`), the structure restarts (`i264e_decide_slice_type_and_rd` with the
  force flag), the IDR QP is not below the QP of the first try, SPS / PPS precede the IDR, and the
  next IDR follows one GOP later.

## Proof

* Host: `tests/hskip` (76 scenarios x 240 pictures: 0 differences; 635 black slices: 0 differences),
  `tests/rc_t20` / `tests/rc_t10` (28 scenarios x 200 pictures with a structure, ChangeRef on and off,
  all decisions of the OEM controller: QP, picture type, re-encode: 0 differences).
* Device: see the comments on the issues (`tools/apitest` case `hskip`, `tools/hskip_trace.py`).

## Not done

* `SetChangeRef` / second try: the rate controller of OpenIMP does not see exactly the picture sizes
  of the OEM (SMART is a port of the controller, but the hardware statistics and the first-try sizes
  differ: on the same camera and scene the OEM IDR is 3.5 kB, OpenIMP's 1.6 kB, and the OEM keeps the
  second try for all 21 SBASE pictures of a 130-picture stream while OpenIMP keeps it for 9 to 10: the
  second try has to be smaller).  The mechanism (which buffer, which header, which try is kept) is
  tested against the OEM code, the kept / not kept ratio is a result of the picture sizes.
* OEM quirks that OpenIMP does not copy: when the first try is kept after a second try, the OEM
  restores the bitstream but not the slice QP of the header (`E+0xab8`) and not the reconstruction
  buffer, so the picture header can name another QP than the data was coded with (read from the
  decompiled `i264e_slice_write`, not proven on a stream; two of the OEM scene-cut streams on the camera
  decode with errors, the cause was not looked into; OpenIMP's streams decode clean).  OpenIMP writes the header of the try it keeps; the
  reconstruction buffer then holds the second try's picture (as in the OEM), so the encoder and a
  decoder can differ until the next IDR in that case (counted in `cr_first_kept`).
* `ChnDemask` and `ChnH264Denoise`/`ChnH264Demask` are not implemented (see the issue comments).
