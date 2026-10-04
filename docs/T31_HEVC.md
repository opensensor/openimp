# T31 HEVC (H.265) on the Allegro AVPU

Reverse-engineering notes for the HEVC path of the T31 AVPU (`/dev/avpu`,
Allegro AL5E core) and how OpenIMP drives it.  Source: the vendor
`libimp.so` T31 1.1.6 HLIL (`docs/re/libimp.so_hlil.txt` in this repository).
Function names below are the vendor symbols; addresses are HLIL addresses.

Status: implemented from the HLIL only.  Nothing in this document has been
checked against a live HEVC command list yet; see "Open points" at the end
and the device test plan in the session handover.

## 1. Where the codec is selected

### 1.1 IMP and codec parameters

`channel_encoder_init` (0x8098c) copies `IMPEncoderAttr.eProfile` unchanged
into the codec parameter block at `+0x20`.  IMP profiles are Allegro
`AL_EProfile` values:

| IMP profile                 | value      | codec byte (`>> 24`) |
|-----------------------------|------------|----------------------|
| `IMP_ENC_PROFILE_AVC_*`     | 66/77/100  | 0 (AVC)              |
| `IMP_ENC_PROFILE_HEVC_MAIN` | 0x01000001 | 1 (HEVC)             |
| `IMP_ENC_PROFILE_JPEG`      | 0x04000000 | 4 (JPEG)             |

`AL_Codec_Encode_Create` (0x7950c) copies the 0x794-byte block behind a
4-byte header; `AL_TEncSettings` therefore starts at codec-param `+4`, and the
channel parameters (`AL_TEncChanParam`) are the first member of the settings.
Offsets below are channel-parameter offsets (codec-param offset = +4).

It accepts `0x1000001` only when `get_cpu_id() - 0x15 >= 2`; CPU ids 0x15 and
0x16 get "only support avc baseline, avc main, avc high, jpeg".  So some
T31 variants have no HEVC in the vendor library.  Which variants report
0x15/0x16 is not recovered; check the T31 camera's CPU id if the HEVC
command list hangs the core.

### 1.2 Channel parameter defaults (`AL_Settings_SetDefaults`, 0x3de4c)

| chan off | meaning                       | default | AVC override (`SetDefaultParam`) |
|----------|-------------------------------|---------|-----------------------------------|
| 0x1c     | eProfile                      | 0x1000001 |                                 |
| 0x1f     | codec byte of eProfile        | 1       |                                   |
| 0x20     | level (x10)                   | 0x33 (5.1) |                                |
| 0x21     | tier                          | 0       |                                   |
| 0x24     | SPS params (log2 max POC - 1 in low nibble) | | |
| 0x28     | PPS params (bit1 cabac_init_present, bit12 deblock override, bit13 deblock disable) | 0 | |
| 0x2c     | eEncOptions (bit5: ME range cap / used by cmd[2] bit15, bit18 -> cmd[3] bit24) | 0x40000 | |
| 0x30     | eEncTools: LF 0x4, LF_X_SLICE 0x8, LF_X_TILE 0x10, SCL_LST 0x20, CONST_INTRA 0x40, TRANSFO_SKIP 0x80, PCM 0x800, WPP 0x1, TILE 0x2 | 0x1c | |
| 0x34/0x35| beta / tc offset (div2)       | -1 / -1 |                                   |
| 0x38/0x39| cb / cr picture QP offset     | 0       |                                   |
| 0x3a     | diff_cu_qp_delta_depth        | 0       |                                   |
| 0x4e     | log2 max CU (CTB) size        | 5 (32)  | 4 (AVC macroblock)                |
| 0x4f     | log2 min CU size              | 3       |                                   |
| 0x50     | log2 max TU size              | 5       | 3 (High) / 2 (Baseline)           |
| 0x51     | log2 min TU size              | 2       |                                   |
| 0x52/0x53| max TU depth intra / inter    | 1 / 1   |                                   |
| 0x54     | entropy mode (1 = CABAC)      | 1       |                                   |
| 0x68..   | AL_TRCParam (mode at 0x68: 0 const QP, 1 CBR, 2 VBR, 4/8 IMP smart/capped) | | |
| 0xa4     | uMaxPictureSize * 1000 (from IMP CBR/VBR) | 0 | |
| 0xe4     | max merge candidates          | 5       |                                   |

The HEVC CTB is 32x32 (log2 5) with 8x8 minimum CU, 4x4..32x32 TUs.

### 1.3 Enc1 command list (`SliceParamToCmdRegsEnc1`, 0x6dd10)

The command list is built from `SliceParam` (request `+0x170`) by
`encode1` (0x671b8).  The fields that select the codec and the block sizes
are in `cmd[0]`:

```
cmd[0] = 0x11
       | ((sp[0] - 2) & 3) << 8     sp[0] = 2 (constant)
       | ((sp[1] - 2) & 3) << 10    sp[1] = log2 max TU   (chan 0x50)
       | ((sp[2] - 4) & 7) << 20    sp[2] = log2 min CU   (chan 0x4f)
       | ((sp[3] - 4) & 7) << 24    sp[3] = log2 CTB size (chan 0x4e)
       | (codec & 3)       << 28    sp+4  = codec (0 AVC, 1 HEVC, 3 JPEG-like)
       | sp[8]             << 31    last slice of the picture
```

| codec          | cmd[0]     |
|----------------|------------|
| AVC Baseline   | 0x80700011 |
| AVC High       | 0x80700411 |
| HEVC Main, CTB32, TU 4..32 | 0x91700c11 |

Other codec-dependent pieces of the packer:

* `cmd[9]` bits 15/18 come from `sp[0x60]/sp[0x61]` for AVC (both 0 in all
  captures) and from `sp[0x5f]` / `sp[0x65]` for HEVC.  `encode1` sets
  `sp[0x65] = 1` unconditionally (collocated_from_l0); `sp[0x5f]` is a
  per-picture flag (picture param +0xda) that stays 0 in our single-layer
  P-only GOP.  AVC bit 11 (`sp[0x62]`) is not packed for HEVC.
* `cmd[9]` bit 16 = `sp[0x63]` = hardware rate control enabled; bits 30/31 =
  `sp[0x71]/sp[0x72]` = scaling list present / load.  Bits 26..29 are the
  same for both codecs.
* `cmd[0x0a]` low 16 = `sp+0x74` = PCM bytes per CTB:
  AVC `8*256 + 0x80 + 8*128 = 0xc80`, HEVC `(8*1024 + 8*512) * 5 / 3 = 0x5000`.
  The same value feeds `cmd[0x1b]` bits 12:0 for AVC.
* LCU geometry (`cmd[0x06]`, `cmd[0x07]`, `cmd[0x0b]` bits 21:12, `cmd[0x1e]`,
  the HWRC grid in `cmd[0x14]`) is in CTB units: `encode1` computes
  `LcuWidth = ceil(w / 2^log2CTB)`.
* `cmd[2]`: bit 10 entropy (CABAC), bit 30 `sp[0x23]` = 1 (temporal MV
  prediction), bits 6:4 = max merge candidates (5), bits 2:0 =
  `diff_cu_qp_delta_depth` when QP tables / auto-QP are enabled (0 here).
  With the HEVC defaults the word is identical to the AVC High one,
  `0x4010ad50`.

### 1.4 No inline Enc2 for HEVC

In `encode1`, AVC goes to `GenerateAvcSliceHeader` and its command list
carries the entropy (Enc2) words inline (`cmd[0x1b..0x1f]`,
`SliceParamToCmdRegsEnc2`).  HEVC runs the slice loop at 0x678cc:
`FillCmdRegsEnc1` + `SliceParamToCmdRegsEnc1`, and the inline
`SliceParamToCmdRegsEnc2` is guarded by `codec == 0`.  The HEVC core writes
the final CABAC bitstream itself; only the multi-core split path uses a
separate Enc2 list (`encode2`, not used on the single-core T31).

Consequence for OpenIMP: HEVC command lists have `cmd[0x1b..0x1f] = 0`;
the stream window words `cmd[0x30..0x33]` are the same as for AVC.

### 1.5 Slice parameters from `UpdateCommand` / `InitMERange`

* Motion search range (`cmd[0x12]`): AVC uses a level table (horizontal
  0x780); HEVC uses the picture size (`InitMERange` 0x63eb8), both capped
  vertically to 0xe0 when chan 0x2c bit 5 is set.  Packed as
  `((h_range >> 6) - 1) | (((v_range >> 3) - 1) << 12)`; IDR keeps the
  `0x3ff3ff` sentinel.
* `cmd[0x08]` (search window per reference) is not codec dependent.

### 1.6 Hardware rate control

`InitHwRC_Content` passes a per-CTB bit ceiling of 0xd48 for HEVC (0x352
for AVC) to `InitHwRateCtrl`; that value only reaches the RC context, not
the command words.  `InitHwRateCtrl` (0x5a9d8) is codec independent except
that it works on `ceil(w / 2^log2CTB)` x `ceil(h / 2^log2CTB)` CTBs, so the
group grid and the per-group targets in `cmd[0x14..0x18]` must be computed
from the CTB grid.

Important: for HEVC, `encode1` enables HWRC (`sp[0x63]`) only when
`rc_mode == 3`, or `chan 0x9c/0xa0/0xa4` (max picture sizes) is non-zero.
`AL_HEVC_GeneratePPS` uses the same condition for
`cu_qp_delta_enabled_flag`.  The hardware can only vary the QP inside a
picture when the PPS allows `cu_qp_delta`, so the two are tied:

| HWRC (`cmd[9]` bit 16, `cmd[0x14..0x18]`) | PPS `cu_qp_delta_enabled_flag` |
|-------------------------------------------|--------------------------------|
| on (CBR/VBR, OpenIMP default)             | 1, depth 0                      |
| off (FixQP, or `OPENIMP_T31_HEVC_HWRC=0`) | 0                               |

IMP fills `chan 0xa4` from `uMaxPictureSize`, which the usual IMP clients
set, so the vendor normally runs HEVC with HWRC on as well.

### 1.7 EP1 (lambda and scaling tables)

`AL_GetLambda` (0x702c0) copies `HEVC_NEW_DEFAULT_LDA_TABLE` (0xe5a90,
0xd0 bytes) instead of `AVC_DEFAULT_LDA_TABLE` for codec 1.  The scaling
list at EP1+0x100 is written only when scaling lists are on
(`AL_HEVC_PreprocessScalingList`).  OpenIMP runs HEVC without scaling lists
(`cmd[9]` bits 30/31 clear, SPS `scaling_list_enabled_flag = 0`), the same
way AVC Baseline runs without them.

### 1.8 Buffers (`EncBuffers.c`)

| buffer | AVC                                  | HEVC (CTB32)                           |
|--------|--------------------------------------|----------------------------------------|
| EP2    | `align128(blk16) + 0x40`             | `align128(blk32 * 8) + 0x40` (~2x AVC) |
| MV     | `(2 * blk16 + 0x10) << 4`            | `(blk32 * 4 + 0x10) << 4` (smaller)    |
| WPP    | per 16-line row                      | per 32-line row (smaller)              |
| stream budget | `384 B` PCM per 16x16         | `1536 B` PCM per 32x32                 |

OpenIMP keeps the AVC allocations and only enlarges EP2.

### 1.9 CABAC init selection (`uCabacInitIdc`)

Channel parameter 0x3b is `uCabacInitIdc` (vendor default 1; HEVC clamps
it to 0..1).  `encode1` copies it to `sp[0x10]`, packed in `cmd[2]` bits 9:8
(AVC: `cabac_init_idc 1`, the value OpenIMP's AVC slice header always
writes).  For HEVC `GenerateHevcSliceHeader` writes the same `sp[0x10]` as
the slice `cabac_init_flag`, so with the default the core initialises P
slices with initType 2.  The stream must say so: PPS
`cabac_init_present_flag 1` and `cabac_init_flag 1` in every P slice.
Signalling flag 0 (first device test, 2026-10-02) desynchronised the
decoder in the first P CTB, visible as "cu_qp_delta outside the valid
range [-26, 25]" with values -63..83.

(`cu_qp_delta` itself matches the vendor: its `AL_Codec_Encode_SetDefaultParam`
sets `eQpCtrlMode = 1` (settings 0x118), which turns on
`cu_qp_delta_enabled_flag` with `CuQpDeltaDepth` (chan 0x3a) 0 for every
HEVC stream.)

## 2. Bitstream headers

The vendor writes VPS/SPS/PPS and the slice segment header in software
(`HEVC_GenerateSections`, `GenerateHevcSliceHeader` 0x71568,
`WriteHevcSliceSegmentHdr` 0x70b14, `HEVC_RbspEncod.c` writers at
0x75378..0x76dcc).  The hardware writes only `slice_segment_data()`, starting
at a byte boundary (the header ends with `byte_alignment()`; unlike AVC
there is no splice word / prefix-bit handover, `sp+0xf8/0x100` stay 0).

### NAL unit types (`GenerateHevcSliceHeader`)

* IDR picture: `IDR_W_RADL` (19)
* reference P picture at temporal id 0: `TRAIL_R` (1)
* VPS 32, SPS 33, PPS 34; `nuh_layer_id = 0`, `nuh_temporal_id_plus1 = 1`

CRA is not used by the vendor's default GOP (IDR every GOP); OpenIMP does
the same.  `RequestIDR` forces an `IDR_W_RADL` with fresh VPS/SPS/PPS.

### Vendor syntax values (and what OpenIMP writes)

VPS: id 0, base layer internal/available 1, max layers 0, max sub layers 0,
temporal id nesting 1, PTL, `vps_sub_layer_ordering_info_present_flag 0`,
one layer set, no timing, no extension.

PTL: profile space 0, tier from chan 0x21 (Main), profile idc 1,
compatibility flag 1, progressive 1, interlaced 0, non-packed 0,
frame-only 1, `general_level_idc = 3 * level` (the vendor takes chan 0x20,
default 5.1; OpenIMP writes the lowest Main-tier level of Table A.8 that
fits the picture size and luma sample rate, e.g. 4 for 1080p25, 5 for
2560x1440).

SPS: chroma 4:2:0, picture size aligned to 8 with a conformance window,
8-bit, log2 min CB 3 / CTB 5, log2 min TB 2 / max TB 5, transform depth
1/1, `amp_enabled_flag 0`, `sample_adaptive_offset_enabled_flag 0`,
PCM off, `num_short_term_ref_pic_sets 0` (RPS in the slice header),
no long-term, `sps_temporal_mvp_enabled_flag 1`,
`strong_intra_smoothing_enabled_flag = (CTB >= 32)`, VUI with
video_format 5, limited range, BT.709 colour description and timing.

PPS: no dependent slices, no extra slice header bits,
`sign_data_hiding 0`, `cabac_init_present_flag 1` (see 1.9),
`num_ref_idx_l0_default_active_minus1 0`, `init_qp_minus26 0`,
`constrained_intra_pred 0`, `transform_skip 0`, `cu_qp_delta` as in 1.6,
chroma offsets 0, no weighted prediction, no tiles/WPP,
`loop_filter_across_slices 1`, deblocking control present with
override 0, enabled, `beta_offset_div2 = tc_offset_div2 = -1`
(`cmd[4] = 0x00083f1f`), `lists_modification_present 0`,
`log2_parallel_merge_level_minus2 0`.

Slice segment header: first slice 1, `no_output_of_prior_pics_flag 0` on
IRAP, PPS id 0, slice type (I = 2, P = 1); for P: POC LSB, explicit
`st_ref_pic_set` (one negative picture, delta 1, used),
`slice_temporal_mvp_enabled_flag 1`, `num_ref_idx_active_override_flag 0`,
`cabac_init_flag 1`,
`five_minus_max_num_merge_cand 0`; then `slice_qp_delta`,
`slice_loop_filter_across_slices_enabled_flag 1`, `byte_alignment()`.

POC: increments by one per picture and restarts at every IDR.  The same
values are written to the T31 picture-number words `cmd[0x0c..0x11]`
(current, reference, collocated, collocated's reference), which the AVC
path fills with the AVC 2n numbering.

## 3. Implementation map (OpenIMP)

* `src/t40/t31_hevc_headers.[ch]` - VPS/SPS/PPS/slice header writer
  (host tested by `tests/t31/hevc_headers_test.c`).
* `src/t40/t31_stream_layout.[ch]` - HEVC NAL split and AU check.
* `src/t40/codec-t40.c` - `ctx->codec_hevc`, HEVC command words in
  `fill_cmd_regs_enc1()` (after the AVC words, AVC untouched), headers in
  `avpu_prewrite_stream_headers()`, HEVC EP1/EP2 setup.
* `src/t40/openimp_p2_encoder.c` - CreateChn accepts HEVC on T31 only;
  packs carry `h265NalType`.

Diagnostic knobs (environment, read once):

* `OPENIMP_T31_HEVC_HWRC=0` - HEVC without hardware RC (picture-level QP,
  PPS `cu_qp_delta_enabled_flag 0`).
* `OPENIMP_T31_HEVC_CABAC_INIT=0` - `uCabacInitIdc 0` in `cmd[2]` bits 9:8
  and `cabac_init_flag 0` (default 1/1, see 1.9).
* `OPENIMP_T31_HEVC_TMVP=0` - disable temporal MV prediction (`cmd[2]`
  bit 30 and the SPS/slice flags).

## 4. Open points (to verify on the device)

1. Completion status `+0x104` is assumed to report the HEVC payload size
   the same way as for inline AVC (`EncodingStatusRegsToSliceStatus` is
   codec independent).
2. `cmd[9]` bit 15 (`sp[0x5f]`) is left 0.
3. Resolved on the device (2026-10-02): P slices use initType 2, see 1.9.
4. The HWRC targets in `cmd[0x15/0x16/0x18]` reuse the recovered AVC
   formulas on the CTB grid.
5. EP3 (HWRC tables) is shared with AVC unchanged.
6. The vendor's FixQP lambda for HEVC is not recovered; the default HEVC
   table is used.

The fastest way to settle 1-5 is a stock-libimp HEVC capture with the
CL-dumping `avpu.ko` (the method used for the AVC templates) and a diff
against `fill_cmd_regs_enc1()` output for the same resolution.
