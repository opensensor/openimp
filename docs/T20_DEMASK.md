# T10 / T20: IMP_Encoder_SetChnDemask (issues #3, #5)

The vendor libimp 3.12.0 (T10 and T20 are one binary; the older 3.9.0 library exports the same code as
`IMP_Encoder_Set/GetChnH264Demask`) has no de-mosaic stage in its encoder.  "Demask" is a control loop
between the encoder and the ISP: when the encoder's QP gets high (coarse quantisation, blocking), the
ISP sharpness is taken down step by step so that the picture costs fewer bits, and it is brought back
when the QP falls again.  OpenIMP's T10/T20 build does the same; the decisions are `src/demask` (pure C,
`tests/demask`), checked against the vendor code with `tools/demask_oracle.py` (unicorn).

## What the OEM does (libimp 3.12.0, addresses of the stripped build)

* `IMP_Encoder_SetChnDemask` (0x49160): no created check; `{enable, isAutoMode, demaskCnt, demaskThresd}`
  go into the channel record (+0x7c, the same words `rcAttr.attrDemask` of CreateChn lands in).  Enable
  0 -> 1 sets the channel's *sharpness word* (+0x7c8) to 0x80; enable 1 -> 0 calls
  `IMP_ISP_Tuning_SetSharpness_internal(0x80, 0)`, which gives the ISP the application's own sharpness
  back.  `IMP_Encoder_GetChnDemask` reads the three words.  `IMP_Encoder_CreateChn` zeroes the record and
  copies the attribute in (no transition: a demask enabled at create starts with sharpness word 0 and
  ramps up to 0x80 over the first seven pictures).
* `do_channel_process_h264e` (0x412fc), every H.264 picture:
  1. before the picture, while `IMP_Encoder_SetChnFrmUsedMode` is off: `encoder_channel_h264e_demask_strategy`
     (0x40070) runs once; its result selects the frame re-use path (and it advances the hold counter);
  2. `i264e_encode` writes the QP it gave the picture into word 0 of the picture record (the controller's
     QP of the first try, after `i264e_decide_slice_qp`; 0 for a BlackEnhance picture);
  3. after the picture, with `enable` set: the strategy runs again, the sharpness word steps by 0x14
     towards 0 while the strategy is active, towards 0x80 while it is idle, and goes to the ISP with
     `IMP_ISP_Tuning_SetSharpness_internal(word, word != 0x80)`: mode 1 = demask override (the ISP gets the
     word), mode 0 = the application's value again (the ISP gets the byte of the last
     `IMP_ISP_Tuning_SetSharpness`, 0x80 by default) and `IMP_ISP_Tuning_SetSharpness` is applied again;
  4. the picture's QP becomes the measure (+0x618) of the next picture.
  So the strategy of picture N looks at the QP of picture N-1, and the ISP gets one `VIDIOC_S_CTRL`
  (0x98091b) per picture while the demask is enabled.  Before the first picture the measure is the
  encoder's QP parameter (`channel_i264e_encoder_init`, 0x4058c): the FixQP value, or the i264e default
  30 for the other rate-control modes (`i264e_param_default`), so a FixQP channel above the threshold is
  active from its first picture on.
* The strategy (measure m = previous QP, thr = demaskThresd, cnt = demaskCnt, hold counter, last = the
  measure the counter last ran on): enable off or `isAutoMode` (the vendor logs "not supported"):
  answer `hold != 0`, nothing changes.  `m > thr`: hold = cnt, last = m.  `m` at `thr` or `thr - 1`
  right after a picture above the threshold (`last > thr`): hold = cnt again, last stays (hysteresis).
  Otherwise hold counts down (not below 0), last = m.  The answer is `hold != 0` (unsigned: a negative
  demaskCnt keeps the strategy active for good).  With the frame re-use mode off the counter is
  decremented twice per picture (the two calls), so `demaskCnt` 3 keeps the sharpness falling for two
  pictures after the QP dropped below the threshold, not three.
* `IMP_ISP_Tuning_SetSharpness_internal` (0x5c3f0): mode 2 (the public setter) stores the byte and does
  nothing while mode 1 (demask) is active; mode 1 applies the given value; mode 0 applies the stored byte.
  ISP control id 0x98091b on the tuning node, `VIDIOC_S_CTRL`.

## OpenIMP

* `src/demask/demask.c`: `demask_strategy`, `demask_set`, `demask_after_picture`, `demask_picture_qp`
  (the four pieces above; state `DemaskState` in the channel record, zero at CreateChn).
* `src/t40/openimp_p2_encoder.c` (`PLATFORM_T20`): `IMP_Encoder_Set/GetChnDemask` and, as the 3.9.0
  library, `IMP_Encoder_Set/GetChnH264Demask` (aliases; `IMP_Encoder_Set/GetChnH264Denoise` likewise for
  `ChnDenoise`); `p2_polling_stream` makes the pre-picture call for H.264 channels, and
  `p2_demask_after_picture` the post-picture block with `IMP_ISP_Tuning_SetSharpness_internal`
  (`src/isp/isp_tseries.c`, the same three modes).  `AL_Codec_Encode_GetPictureQp` reads the picture's QP
  from the Helix encoder (`OpenIMP_T30_HelixPictureQp`: the controller's QP of the first try, 0 for a
  BlackEnhance picture, the value `i264e_encode` reports).
* The frame re-use path of `SetChnFrmUsedMode` is a stored attribute in OpenIMP; the pre-picture call
  is made for its side effect on the hold counter only.

## Proof

* Host: `tests/demask` (`tools/demask_oracle.py`: 50 scenarios, 55 SetChnDemask calls, 1300 pictures
  of the vendor functions under unicorn: strategy results, sharpness word, ISP call mode and value,
  0 differences); `tests/t30/p2_t1x_test` (setter semantics); `make check`, `make status-check`.
* Device: `tools/apitest` case `demask` with the `VIDIOC_S_CTRL` log of `tools/apitest/sctrl_trace.c`
  (LD_PRELOAD) and `tools/demask_check.py` (expected calls from `src/demask` fed with the slice QPs of
  the stream, `tools/hskip_trace.py --qp`, against the calls the library issued); see the issue
  comments for the camera runs (vendor library and OpenIMP on the same camera).
