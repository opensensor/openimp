# T21: IMP_Encoder_SetChnHSkip, BlackEnhance, bEnableScenecut and SetChangeRef (issue #7)

The vendor libimp 1.0.33 (T21) carries the same i264e reference-structure code as the T10/T20 library 3.12.0
(`docs/T20_HSKIP.md`), but not the same encoder behaviour.  This page lists what was read from the T21 library (two builds, gcc 4.7.2
and gcc 5.4.0, both run under the unicorn emulator with identical results) and how OpenIMP's T21 Helix encoder
(`src/t30/t30_helix_encoder.c`, T21 build) follows it.  The decisions are `src/hskip` (shared with T10/T20, `tests/hskip`), checked
with `tools/hskip_oracle.py` against the OEM functions.

## What the T21 library does

* **SMART rate control only.**  `i264e_validate_parameters` (CreateChn, and every IDR that takes a new skip header) keeps the skip
  attributes `skipType`, `m`, `n`, `bEnableScenecut`, `bBlackEnhance` only when the rate-control mode is SMART; every other mode
  (FIXQP, CBR, VBR) clears them and sets `maxSameSceneCnt` to 1 (clip 1..100).  A CBR channel that is given `maxSameSceneCnt 3` or
  a skip type therefore codes the plain GOP on the vendor, too.  In SMART the application's `m` and `n` are replaced: N1X
  m = gop-1, n = 1; N2X 1/1; N4X 1/2; HN1 1/(gop-1); H1M m = gop-1, n = 1.  `hskip_t21_validate` is the same function in C
  (1680 cases of the oracle: 0 differences).
* **Reference structure.** `i264e_init_skip_header` / `i264e_decide_slice_type_and_rd` give the same pictures as the T10/T20 code
  (skip types 0..6: frame_num, IDR, frame type IDR/LBASE/SBASE/ENHANCE, reference and reconstruction buffer, reference list
  modification, MMCO, long-term flag) with one difference: N1X and N2X use three reconstruction buffers (N2X rotates the long-term
  base picture through all three, N1X never leaves buffers 0 and 1).  `hskip_init_t21`; 76 scenarios x 240 pictures: 0 differences.
  The SPS of the T21 stream already has `max_num_ref_frames` 2 and `gaps_in_frame_num_allowed` (as OpenIMP's T21 SPS).
* **IDR rules.** New values wait for the next IDR picture (`i264e_idr_reconfig`), no extra IDR; N1X and H1M with
  `maxSameSceneCnt` > 0 code an IDR every n GOPs.  A scene cut inside that period (class 5 of the controller on the previous
  picture, CBR/VBR/SMART, from the second GOP of the period on, at a GOP boundary) makes the picture an IDR:
  `hskip_t21_scene_idr` (36 scenarios x 240 pictures with random method/mode/class: 0 differences).
* **BlackEnhance.** `i264e_icodec_black` writes the same all-skip slice as on T10/T20 (635 sizes: 0 differences with
  `src/hskip/hskip_black.c`); an ENHANCE picture is not coded, and neither the re-encode judge nor FRAME_END of the controller sees it
  (`i264e_slice_write` takes the black path before `i264e_ratecontrol_is_reenc`).
* **bEnableScenecut** (`i264e_decide_reset_slice_to_idr`) is the T10/T20 function: a base picture of the H1M types at a whole second of the
  frame rate that is larger than the last IDR picture is coded again as an IDR picture; the rate controller is set up again
  (`i264e_ratecontrol_init` with the QP of the first try).  The test comes before the re-encode judge (first try only).
  76 scenarios x 240 pictures: 0 differences.
* **No class byte, no base-picture QP rule.**  `i264e_ratecontrol_start` of T21 never reads the skip header (the T20 controller gets a
  class byte for the HSkip pictures), and `i264e_decide_slice_qp` is an empty function (T20: LBASE pictures take the QP of the last IDR).
  The eprc controller therefore needs nothing from the structure.
* **SetChangeRef has no effect.**  The second try of T10/T20 (`i264e_slice_write` codes an H1M picture again with the third buffer as
  reference) hangs on the flag word picture+0x1284.  On T21 nothing sets that flag: `i264e_ratecontrol_is_reenc` writes 0 to it,
  `i264e_ratecontrol_is_reenc_end` (the only writer of 1) is only called with the flag already set, `i264e_slice_write` only clears it, and
  the ChangeRef parameter (encoder +0x118) is read by no controller code.  `tools/t21_changeref_oracle.py` runs the vendor judge for 6952
  calls (7 scenarios, H1M_FALSE SBASE and H1M_TRUE LBASE, ChangeRef on and off): the flag was never set and no decision depended on
  ChangeRef.  `IMP_Encoder_SetChangeRef` keeps the value with the vendor return codes (GetChangeRef of a created channel fails with
  "unsupport to change ref channel", as the vendor), and there is no second try to port.

## OpenIMP

* `hs_attr`: the attributes the encoder really runs (T10/T20: as given; T21: `hskip_t21_validate`).
* Skip types 1..6 start a structure with the next IDR (`t20_hs_idr`); N1X stays on the plain GOP / scene-cut logic of the T21 encoder,
  which is the same chain of P pictures.  The structure needs separate reconstruction pictures: a channel created with the shared
  reference ring (`OPENIMP_REF_SHARE`, default on up to 1080p) switches to plain pictures at that IDR (the ring allocation becomes
  buffer 0, buffers 1 and 2 are added).  If the memory is not there the plain GOP is kept and an error is logged.
* The SMART gating also fixes the IDR period of N1X (`maxSameSceneCnt`), which was applied in every rate-control mode before.

## Proof

* Host: `make -C tests/hskip check` (T21 vectors: hskip, changeref, scenecut, validate, sceneidr; black).
* Oracle tools: `tools/hskip_oracle.py` (T21 library: all modes), `tools/t21_changeref_oracle.py`.
* QEMU and camera: see the comments on issue #7.
