# T21: IMP_Encoder_SetFisheyeEnableStatus (issues #3, #5, #7)

The vendor libimp 1.0.33 (T21, built with gcc 4.7.2 and gcc 5.4.0) has the same fisheye code as the T10/T20 library 3.12.0
(`docs/T20_FISHEYE.md`), but on the T21 it has **no effect on the stream**.  This page lists what was read, what the cameras show
and what OpenIMP's T21 build does.

## What the T21 library does

* `IMP_Encoder_SetFisheyeEnableStatus` (0x45454): channel number below 6, then a flag in the channel record (+0x1ac) that can be
  set only while the channel is not created (-1 afterwards).  `Get` (0x4555c) reads it back.  Both as on T10/T20.
* `IMP_Encoder_CreateChn` hands the flag to the encoder (the word at +0x1d0 of the encoder handle; T10/T20: +0x1c8).  Its reader
  is `i264e_icodec_enc` (0x39250), which `i264e_slice_write` calls for every coding try of every picture.  The function is the T10/T20
  one: with the flag set it clears the picture's 56-byte ROI table (8 regions x 7 bytes) and, for an I slice and for every picture at a
  whole second of the frame rate (`counter * fps_den % fps_num == 0`), sets region 0 to `{1, 0, min(slice QP, 15), 0, 3, 0, 1}`
  (macroblocks x 0..3, y 0..1, the top-left 64x32 pixels).  `tools/fisheye_oracle.py` runs it under unicorn for both T21 builds:
  the 3815 vectors are byte-identical to the T10/T20 ones (`tests/fisheye/fisheye_vectors.txt`).
* **The table is never used.**  `hwicodec_pf_h264e_t21_enc` and `H264E_T21_SliceInit` do not read the i264e ROI table (the slice's
  ROI block is written by `h264_get_mb_qp` from the rate controller's own table only, see `docs/T1X_ROI_CHROMA.md`).  Camera
  proof: the vendor library on a T21 camera, flag on and off, FixQP 30 / 40, CBR, with and without an application ROI: the IDR and
  every whole-second picture show no macroblock at QP 15 or better, no QP difference between the top-left region and the rest, and
  an application ROI (`SetChnROI`) has no effect either (the T20 library shows both).

## OpenIMP

* `src/fisheye`: `fisheye_roi()` is the vendor function (unchanged, now also checked against the T21 library);
  `fisheye_roi_hw(t21, ...)` is the table the hardware is programmed with: T10/T20 the table of the vendor function, T21 nothing.
* `IMP_Encoder_CreateChn` hands the flag to the Helix encoder; `t30_fill_slice` (T21 build) drops the application's ROI table of
  the picture when it is set and codes no region 0.  The stream is the vendor's: no region, and no application ROI while the flag is
  set.  Without the flag OpenIMP's T21 programs an application ROI (beyond the vendor, `docs/OPENIMP_BEYOND_VENDOR.md`).
* Set/Get answer -1 for a channel number of 6 or more, like the vendor (OpenIMP has eight encoder channels for the other calls).

## Proof

* Host: `tests/fisheye` (vectors of the vendor function; T21 hardware table = empty with the flag), `tests/t30` `helix_encoder_test_t21`
  (`test_fisheye_t21`: no ROI in the command list of any picture with the flag, the application's window without it).
* QEMU T21 (own guest): apitest case `fisheye` (flag, flag off with ROI, flag with ROI; 40 pictures each, IDR at 0 and 25) and `transcfg`:
  return codes, frames flowing, health check 0 oopses.
* Cameras (T21 test camera, no flash, streamer restored, ISP in stable Night mode before each run; `apitest` case `fisheye` with
  `HSKIP_MAIN=1`, vendor library 1.0.33 (gcc 4.7.2), OpenIMP before and after the change on the same camera, libimp via
  `LD_LIBRARY_PATH`; the installed `/usr/lib/libimp.so` is an OpenIMP build and was not used as the vendor reference).  Macroblock QPs
  from the decoder (`tools/fisheye_mbqp.py`), 100 pictures per scenario, GOP 25 or 45: FixQP 30 with and without the flag, FixQP 30 with
  an application ROI (delta -6, whole picture), CBR, FixQP 40, FixQP 20 with and without the flag.
  * Vendor: the top-left region (macroblocks x 0..3, y 0..1) is at QP 15 or better in no picture of the FixQP 30, FixQP 40, CBR and ROI
    scenarios, flag set or not; the IDR is at the same QP in the region and everywhere else (27 at FixQP 30); with the application ROI
    the picture is unchanged (QP 27 everywhere: the ROI does nothing on a T21).  FixQP 20: the vendor's own macroblock QP spread puts
    the region at 15 or better in 94 of 100 pictures with the flag and 93 without, which is not the flag.
  * OpenIMP before: the same, except that the application ROI was active with the flag set (QP 21 everywhere, the vendor shows 27).
    After: 27 as the vendor.  OpenIMP's FixQP 20 stream never goes below QP 17 (the vendor's FixQP spread, not part of this change).
  * All streams decode clean (ffmpeg with `crccheck+bitstream+buffer+explode`, 0 errors) and show the real scene (mean luma 97 for the
    vendor library, 99 to 103 for OpenIMP).
