# T10 / T20: IMP_Encoder_SetFisheyeEnableStatus (issues #3, #5)

The vendor libimp 3.12.0 (T10 and T20 are one binary) does not run a fisheye motion search or a lens
correction for this flag.  `IMP_Encoder_SetFisheyeEnableStatus` stores a 0/1 flag in the channel record
(+0x140, settable only while the channel is not created, -1 afterwards; `IMP_Encoder_CreateChn` keeps it
across its record reset), `channel_i264e_encoder_init` hands it to the encoder as i264e parameter word
0x72, and the single reader of that word is `i264e_icodec_enc` (0x4d740).  OpenIMP's T10/T20 build does what
that function does (`src/fisheye`, pure C, `tests/fisheye`).

## What the OEM does

`i264e_icodec_enc` runs for every coding try of every picture (`i264e_slice_write`) and builds the command
for the encoder hardware.  With the flag set it

1. clears the picture's 56-byte ROI table (8 regions x 7 bytes `{enable, relative, qp, x0, x1, y0, y1}`,
   context +0xc): a region set by the application (`IMP_Encoder_SetChnROI`) is lost;
2. for an I slice (an IDR picture), and for any other picture at a whole second of the frame rate
   (`counter * fps_den % fps_num == 0`, `counter` the picture counter of the encoder context), sets
   region 0 to `{1, 0, min(slice QP, 15), 0, 3, 0, 1}`: macroblocks x 0..3, y 0..1, the top-left 64x32
   pixels, at an absolute QP of 15 or better.

With the flag clear the table is untouched.  Nothing else in the library reads the flag.

## OpenIMP

* `src/fisheye/fisheye.c`: `fisheye_roi()`, the same decision for the picture's table.
* `src/t30/t30_helix_encoder.c` (`PLATFORM_T20`, i.e. the T10 and the T20 build): `t30_fill_slice` applies
  it to the slice's ROI table for every try, after the application's ROI is copied in.  OpenIMP's ROI
  sanitiser (`docs/ROI.md`, QP steps of more than 25 between neighbouring macroblocks make streams that
  VA-API shows as broken blocks) still runs on the command list afterwards, so a picture at a very high QP
  gets its fisheye region raised to the lowest QP the stream can carry (the vendor writes the 15 and
  produces such a stream).
* `AL_Codec_Encode_SetFisheye` (`codec-t40.c`) is called by `IMP_Encoder_CreateChn` (`openimp_p2_encoder.c`)
  with the flag; the other builds (T21, T23, T30, T31, T40) keep the flag as stored state.

## Proof

* Host: `tests/fisheye` against `tools/fisheye_oracle.py` (the vendor `i264e_icodec_enc` under unicorn on a
  synthetic picture context: flag on/off, slice type, counters around whole seconds, six frame rates, QP
  0 to 51, an application table pre-filled with garbage): 3815 cases, 0 differences.  `tests/t30`
  `p2_t1x_test` (flag handed to the encoder at CreateChn, refused after).
* QEMU T20 and T10: apitest case `fisheye`, return codes and frames flowing.
* Camera: see the issue comments (vendor library and OpenIMP on the same camera, macroblock QPs of the
  streams with `tools/fisheye_mbqp.py`).
