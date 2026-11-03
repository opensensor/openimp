# T10/T20/T21 encoder ROI and chroma QP offset

How the stock T20 3.12.0 (also T10) and T21 1.0.33 libimp apply
`IMP_Encoder_SetChnROI` and `IMP_Encoder_SetH264TransCfg`, and what OpenIMP
does. Addresses are in the uClibc builds of the stock libraries.

## IMP layer

`IMP_Encoder_SetChnROI` (T20 0x4899c, T21 0x467d0) builds an i264e ROI
table entry and calls `i264e_set_param(h, 2, entry)`:

| byte | value |
|---|---|
| 0 | `bEnable` |
| 1 | `bRelatedQp` |
| 2 | `s32Qp` as s8 |
| 3, 4 | min / max of `rect` +12 and +20 (the OEM `IMPRect` p0.x / p1.x) / 16 |
| 5, 6 | min / max of `rect` +16 and +24 (p0.y / p1.y) / 16 |

The division is C division (truncates toward zero), the results are stored
as bytes. T21 returns 0 without doing anything for a channel that is not
H.264. `IMP_Encoder_GetChnROI` checks the index (< 8), clears the 28 bytes
and returns the entry with the coordinates multiplied by 16.
`i264e_reconfig_roi_set` (T20 0x33e78) stores the entry in the encoder's
table (8 x 7 bytes), and `i264e_reconfig` copies the table to the codec at
the next picture, not only at an IDR.

`IMP_Encoder_SetH264TransCfg` (T20 0x4b8d4: `chroma_qp_index_offset` at
+140 of the 144-byte T20 structure; T21 0x49508: 4-byte structure) calls
`i264e_set_param(h, 14, ...)`. `i264e_reconfig_trans_set` (T20 0x348fc,
T21 0x2d9a8) stores the value and flag 0x4000; `i264e_idr_reconfig` takes
it over at the next IDR into the i264e parameter that `i264e_pps_init`
writes into the PPS (T20 param +136, T21 +144) and that the codec
hands to the slice block (T20 slice +186, T21 slice +457).

## Command list (EFE registers)

`H264E_T10_SliceInit` (0x1ef78..), `H264E_T20_SliceInit` (0x20cd8..0x20f38)
and `H264E_T21_SliceInit` (0x1cfec..0x1d248) encode the slice's 8 x 7 byte
ROI block (T10 slice +160, T20 +244, T21 +752) in the same way:

| register | value |
|---|---|
| 0x40044 | regions 0..3, one byte each: `(qp << 2 \| rel << 1 \| en) & 0xff` |
| 0x40048 | regions 4..7 |
| 0x4004c + 4 i | `x0 \| x1 << 8 \| y0 << 16 \| y1 << 24` of region i |

The chroma QP offset goes to 0x40120 = `offset & 0x1f` (T20 0x20fcc, T21
0x1d278). `H264E_T10_SliceInit` has no 0x40120: on a T10 the stock library
changes the PPS alone.

## Where the regions come from

- T10/T20: `hwicodec_pf_h264e_t10/t20_enc` copy the i264e ROI table into the
  slice block for every picture, so the regions reach the EFE registers.
  `H264_SMA_CalMBQP` (0xa6eb0) also has code that writes the regions into
  the macroblock QP table, but it runs only when the debug file `/tmp/roic`
  exists (`access()` at 0xa7740). By default the hardware registers are the
  only path.
- T21 1.0.33: the slice ROI block is written only by `h264_get_mb_qp`
  (0x93d78), from the eprc controller's own table (+5144). The QP-map modes
  0 (the default, rate-control file mode word 0x30) and 3 clear that table
  first. No path from the i264e ROI table (+10192, copied to the codec at
  +12) to the slice block was found. So with the stock T21 library
  `IMP_Encoder_SetChnROI` is stored and returned by Get but has no effect on
  the picture (inferred from the disassembly, not measured).

## OpenIMP

- `IMP_Encoder_SetChnROI`/`GetChnROI` work as the OEM IMP layer does
  (`src/t30/helix_roi.h` `Helix_H264_RoiEntry`), and the entry reaches the
  encoder through `AL_Codec_Encode_SetRoi`. The encoding thread takes it over
  before each picture.
- T10/T20: `T10_H264_BuildDescriptor` / `T30_H264_BuildDescriptor`
  (`PLATFORM_T20`) program the regions as the OEM does.
- T21: the regions are programmed by default (beyond the vendor: the stock
  library never does). With no region set the table is all zero and the
  command list is the stock one; `OPENIMP_T21_ROI=0` disables it. Device
  test (PC420 jxf23 1920x1080, FixQP 30, one region 320x320 at 240,560,
  `tools/roitest`): absolute QP 51 blurs exactly the region (rmse against
  the reference 7.7 inside, 2.4 outside), QP 15 stays clean, delta +15 / -15
  work, live changes on P frames work. FixQP stream: 657 kbit/s without
  ROI, 340 with QP 51, 504 with QP 15; IDR 74 / 70 / 86 KB. CBR holds the
  target bit rate (P frames 4.1 KB with and without ROI), the ROI only
  moves the bits. The region corners are rounded down to macroblocks, x1/y1
  are the last macroblock (inclusive).
- Chroma QP offset (T20, T21): it is taken over at the next IDR, into the
  PPS and 0x40120 together. T20 uses the generated PPS. T21 rewrites the two
  se(v) fields `chroma_qp_index_offset` and `second_chroma_qp_index_offset`
  of its fixed PPS, which has scaling matrices
  (`OpenIMP_T21_PpsWithChromaOffset`; offset 0 gives the stock PPS bit for
  bit). T10 keeps 0 in both and logs a warning.
- T30: the ROI is programmed as the OEM `H264E_T30_SliceInit` does
  (libimp T30 1.0.5 0x45590): sixteen slots, flag bytes qp << 2 | rel << 1 | en
  in `0x40044` (regions 0..3), `0x40048` (4..7), `0x40130` (8..11), `0x40134`
  (12..15), rectangles x0 | x1 << 8 | y0 << 16 | y1 << 24 in `0x4004c..0x40068`
  (0..7) and `0x40138..0x40154` (8..15); the OEM `i264e_icodec_enc` copies the
  eight IMP entries to slice +0x114, so regions 8..15 stay zero (as OpenIMP).
  The register layout is from the disassembly, the picture effect is not yet
  measured on a camera (T20/T21 are). The chroma QP offset is `0x40120`
  (`param_1[0xda] & 0x1f`); a non-zero offset is still refused on T30.
- The `/tmp/roic` software map is not reproduced.

Tests: `tests/t30/helix_encoder_test.c` `test_roi_chroma` (register words,
PPS parse) and `tests/t30/p2_t1x_test.c`. The PPS of the dumped streams
(`HELIX_TEST_DUMP=file`) parses with `ffmpeg -bsf:v trace_headers`.
Hardware effect: x1/y1 are inclusive macroblocks (T21 measured). Open: what
a relative QP does at the 6-bit field limits.

On-device test without a streamer that calls the API:
`OPENIMP_DEBUG_ROI="ch:en,rel,qp,x0,y0,x1,y1;..."` (pixel corners p0/p1;
the region index is the position in the list) and
`OPENIMP_DEBUG_CHROMA_QP="ch:offset"` are applied after `CreateChn`. A
streamer that calls `SetChnROI`/`SetH264TransCfg` later overrides them.
