# Encoder ROI per SoC (branch `claude/roi-all`, not in the first release)

| SoC | API | state |
|---|---|---|
| T10/T20 | `IMP_Encoder_SetChnROI` (8 regions, pixel corners) | works, see `T1X_ROI_CHROMA.md` |
| T21 | the same | works by default since this branch (beyond vendor), device-tested |
| T23 | the same; native Helix encoder (default backend) programs it like T21, the OEM worker gets it through the bridge | works by default, device-tested (below) |
| T40/T41 | `IMP_Encoder_Set/GetChnRoiAttr` (10 windows, delta QP) | implemented, **on by default** since 2026-10-18 (`OPENIMP_T40_ROI=0` / `OPENIMP_T41_ROI=0` refuse it); device-tested: T40 works under FixQP and CBR, T41 works under CBR/VBR only - under FixQP the call is accepted (cache), the table is not written and syslog says "ROI has no effect under FixQP on T41"; `OPENIMP_T41_ROI=1` forces the table even under FixQP |
| T31 | none in the vendor library; OpenIMP: `IMP_Encoder_Set/GetChnRoiAttr` (T41 API) | works by default (beyond vendor), device-tested; QP clamped to valid H.264 (below) |

## T40 (cross-built, not device-tested)

The vendor T40 libimp 1.3.1 keeps the ten windows of `IMP_Encoder_SetChnRoiAttr` in the channel record and builds, per picture, a QP table whose entry is one byte per 16x16 block for AVC (eight bytes per 32x32 block for HEVC); its `SliceParamToCmdRegsEnc1` sets cmd[9] bits 25 and 24 from the same two slice-parameter bytes as on the T31. OpenIMP therefore reuses the T31 path on T40: the windows fill the EP2 table (`avpu_t31_roi_apply`, the shared `avpu_roi_fill`) and the T40 command ORs `0x03000000` into cmd[9]. AVC only (H.265 stays unsupported on T40). `IMP_Encoder_SetChnMapRoi` (vendor 1.3.1 checks as on the T41: map size = number of 16x16 blocks, mode delta, quality -26..25) writes its per-block deltas over the window values in the same table (cut to the QP range and the neighbour spread), behind the same switch. On by default (device-tested on the Eufy T40XP, FixQP and CBR); with `OPENIMP_T40_ROI=0` `SetChnRoiAttr` returns -1 and the command is as before. Device test: enable a window on an idle scene, compare the bit rate and the QP of the window with and without it, 20 phases in VA-API / strict ffmpeg as for the T31.

## T21 (device-tested)

PC420, jxf23, 1920x1080, `tools/roitest`: one 320x320 region at (240,560).
FixQP 30: absolute QP 51 blurs exactly the region, QP 15 stays clean, delta
+15/-15 work, a change on P frames (no IDR) works. Stream size 657 kbit/s
without ROI, 340 with QP 51, 504 with QP 15. CBR keeps its target; the ROI
moves the bits. Details: `T1X_ROI_CHROMA.md`. `OPENIMP_T21_ROI=0` switches
the programming off. The region corners are rounded down to macroblocks, x1/y1
are inclusive.

## T41 (family B)

Found on a T41 with the vendor libimp 1.2.6 (`IMP_Encoder_SetChnRoiAttr`
disassembly, command-list and EP2 captures with an `LD_PRELOAD` tracer):

- the vendor checks `x+w <= width`, `y+h <= height` of the channel for every
  enabled window, stores the 280-byte attribute and sets a flag; the encoding
  thread then builds a table before the next picture.
- the table is the EP2 buffer: after a 0x40-byte header one 32-bit entry per
  16x16 macroblock, raster order, row stride `ceil(width/16)`. Byte 0 is the
  relative QP (int8, -26..25), byte 3 is 0x20 in every entry (also without a
  window). Windows: corner rounded down, size to the nearest macroblock.
  Capture: windows of 40x20 and 10x6 macroblocks at 1080p changed exactly
  those 800 + 60 entries (byte 0 only).
- the AVC EP2 size of the vendor is `0x40 + 4 * macroblocks` (OpenIMP's was
  one byte per macroblock); the command words of a picture are identical with
  and without a window.

OpenIMP writes that table (`avpu_t41_roi_apply`, EP2 enlarged) and exports
`IMP_Encoder_SetChnRoiAttr/GetChnRoiAttr`. **The encoder did not react** in
FixQP or CBR (whole-frame -20/-25/+20/+25, regions up to a quarter of the
frame; bit rate and decoded pictures unchanged). Writing the EP2 header words
the vendor library has (QP limits) was tried and hung the camera. Therefore
(historical, first attempt; superseded by the cmd[152] result below). What is missing is a
vendor reference that works: the vendor libimp on this open-tx-isp camera
gives corrupt pictures and twice hung the kernel, so an A/B against the vendor
could not be made.

IDR pictures did not respond to the table in any variant. `SetChnMapRoi` is
not implemented.

**Probable cause, found with the T31 (code only on T41):** the AVPU reads the
table only when the command enables it. In the vendor T41 1.2.6 `encode1`
copies `AL_OPT_USE_QP_TABLE` (picture option bit 0) to SliceParam+0x63 and
the "relative" flag to SliceParam+0x62 (offsets from the SliceParam base
pic+0x248, matched against the neighbouring fields), and
`SliceParamToCmdRegsEnc1` (0xe9d68) packs them into **cmd[152] bit 0 and
bit 3**. OpenIMP's T41 builder writes cmd[152] = 0xf6 (both clear). With
a window set, OpenIMP ORs 0x9 into cmd[152]. Since 2026-10-18 this is the default for CBR/VBR; under FixQP the table is not written (effect-less) and one syslog line is logged; `OPENIMP_T41_ROI=0` turns the feature off, `=1` forces it under FixQP too.
Device test 2026-10-18 (Cinnado .34 and Vanhua .32, roitest, 1280x720
15 fps, window 384x240 at 448,240): **under CBR the table works** - the
whole frame at -20 doubles the bit rate against the 800 kbit/s target, a
+20 window drops the SSIM of the window from 0.97 to 0.73 while the rest
stays at 0.97, a -20 window moves bits into the window (the rest drops to
0.91-0.93). cmd[152] is then 0xff, as in the vendor capture. **Under FixQP
the AVPU ignores the table** (bit rate and pictures unchanged), also with
the cmd[179] QP range (min bits 31..24, max bits 15..8) opened from QP..QP
to 0..51 or 15..45; the cause is open. The header-word hang above is
unrelated (do not write the EP2 header).

T40 (Eufy, open driver, same test, now default): FixQP 32 window
-20 100 -> 1194 kbit/s, whole frame -20 12.2 Mbit/s, window +20 93 kbit/s;
CBR window +20 drops the window SSIM 0.96 -> 0.89. On the vendor side the 32-bit entries with byte 3 =
0x20 suggest that the vendor sets the bit for every picture.

## T31 (device-tested, beyond vendor)

The vendor T31 libimp 1.1.6 has no ROI API, but its Allegro core shows how
the AVPU takes a macroblock QP table (HLIL in `docs/re/libimp.so_hlil.txt`):

- `AL_Common_Encoder_Process` sets picture option bit 0
  (`AL_OPT_USE_QP_TABLE`) when a QP buffer comes with the frame and uses that
  buffer as EP2; otherwise EP2 is `AL_IntermMngr_GetEp2Addr` (interm buffer
  + EP1 + WPP), which is OpenIMP's `cmd[0x23]`.
- `encode1` copies the picture option bit 0 to SliceParam+0x6c and channel
  option bit 0 to SliceParam+0x6b; `SliceParamToCmdRegsEnc1` packs them into
  **cmd[9] bit 25 (use the table) and bit 24 (table relative)**. The vendor
  watermark path (`embed_watermark`) sets both. Without bit 25 the AVPU
  ignores EP2's table; OpenIMP's command template (cmd[9] = 0xfc010000 /
  0xfc000000) had both clear.
- table layout (`AL_GetAllocSizeEP2`, `AL_RoiMngr_FillBuff`): the 0x40-byte
  EP2 header (auto-QP seed and QP range, written by `avpu_t40_init_ep2`;
  zeroing it gives QP 0 pictures, 33 Mbit/s), then **one byte per 16x16
  macroblock**, raster order: bits 5:0 QP (6-bit two's complement when
  relative), bit 6 force intra, bit 7 force skip.

Measured on a T31 test camera (cam-D) (sc4336p, 2560x1440): bit 25 alone = absolute
table (an entry 0 is QP 0), bit 24 alone = no effect, both = relative table.
OpenIMP always uses the relative table. An absolute window (`mode =
IMP_ROI_QPMODE_FIXED_QP`, QP 0..51) is written as the difference to the
picture QP of the last command and rewritten when that QP changes: exact under
FixQP, one picture late under CBR/VBR. Delta windows take -26..25 (the vendor T41 range; `SetChnRoiAttr` returns -1
outside, it used to take -32..31). A window
covers every macroblock it touches; with overlapping windows the higher index
wins. The table is rewritten (and flushed) only when the windows or, with an
absolute window, the picture QP change; with no window the command and EP2
are as before. `OPENIMP_T31_ROI=0` refuses `SetChnRoiAttr`. H.264 only.

Results (`tools/roitest`, night/IR, region 768x512 at (896,464), 10 fps):

| run | IDR bytes | P bytes | kbit/s |
|---|---|---|---|
| FixQP 30, no ROI | 176 K | 2.5 K | 1124 |
| FixQP 30, delta +20 | 157 K | 2.2 K | 1410 (3 IDRs) |
| FixQP 30, absolute 51 | 156 K | 2.2 K | 1403 (3 IDRs) |
| FixQP 30, absolute 10 / delta -20 | 247 K | 44 K | 4550 |
| FixQP 30, 2 windows (1280x720 delta +25, 1280x720 absolute 51) | 98 K | 1.2 K | 843 |
| CBR 1 Mbit/s, no ROI / absolute 45 / delta +20 | 119 / 92 / 94 K | | 1336 / 911 / 1138 |

The decoded pictures show the region visibly blurred/blocky at +20 / 51 and
the rest unchanged (collages local only). A change on P pictures without IDR
works (P frames 2.7 K to 2.5 K). (Measured before the clamp below: whole-picture
offsets were not limited to the channel QP range, CBR +31: 82 kbit/s, -32:
9.9 Mbit/s; afterwards CBR needed a few seconds to recover.) Stability: 8 live changes in one stream,
streamer restarted, 0 oops.

## Valid H.264: the QP clamp (all SoCs with ROI)

H.264 allows `mb_qp_delta` only in -26..+25, and the encoder writes it as
QP(macroblock) - QP(previous macroblock in coding order); skipped macroblocks
keep the previous QP. Anything outside is **invalid H.264**: software ffmpeg
hides it, hardware decoders (Intel VA-API, VLC, browsers) show broken blocks.
Measured on T10/T20: relative -26 and an absolute QP more than 25 from the
macroblock QP (absolute 15 at QP 42) both break the stream. A relative -26
fails on the way *out* of the window (+26 back to the neighbours).

`src/avpu_roi.h` (T31, experimental T41) and `Helix_H264_RoiSanitize` in
`src/t30/helix_roi.h` (T10/T20/T21, applied when each command list is built
with the slice QP of that picture) therefore guarantee:

- every window delta is in **-25..+25** (absolute QPs are turned into a delta
  against the picture QP first);
- the spread of all deltas, uncovered macroblocks counting as 0, is **at
  most 25**; if it is larger the higher QPs are lowered (quality wins);
- picture QP + delta stays in **0..51** and in the rate control's
  **min_qp..max_qp** (T31/T41-style table; positive deltas above max_qp were
  only capped by the hardware before, now the table says what happens);
- Helix absolute QPs lie in `[max_qp - 25, min_qp + 25]` of the picture
  (T21/T30/T10 command lists: slice QP -12/+13, which is also the saturation
  seen on the T21; T20: the `max_qp_cap`), so every macroblock QP is within
  25 of the window QP.

Requests beyond that are clamped, a one-time warning is logged
(`ROI: requested QP delta ... clamped`), `SetChnRoiAttr` (T31, T41) returns -1
for a delta outside -26..25 or an absolute QP outside 0..51. The T31/T41
table is rewritten when the picture QP changes and the clamp depends on it
(absolute window, or a delta that touched the QP range). Under CBR/VBR the
picture QP is the one of the last command, so the clamp can be one picture
late. Host test: `tests/t31/roi_clamp_test.c` (fuzz of 20000 random window
sets, every table is walked like the encoder and checked against -26..25).
T23 (native Helix) uses the same `Helix_H264_RoiSanitize` as T21.

## T23 (native Helix, branch `claude/t23-roi`)

Vendor libimp 1.3.0 (uclibc) does program the regions; the hardware has them:

- `i264e_reconfig_roi_set` (0x3699c) stores the 7-byte entry at
  i264e+10372+7*index and sets reconfig flag 0x4 (i264e+9828);
- `i264e_reconfig` (0x35e44) copies the 56-byte table to the frame context
  (`*(i264e+15832)`) +12 and clears the flag;
- `i264e_icodec_enc` (0x42a2c) hands that table to the platform encoder: its
  parameter block is frame+2768 and frame+3756 = parameter+988 = &frame[12];
  (with i264e parameter +468 set it zeroes the table first);
- `hwicodec_pf_h264e_t21_enc` (0x29468) copies parameter+988 to slice +752,
  `H264E_T21_SliceInit` (0x25d80..0x25fd0) writes 0x40044..0x40068.

So on T23 this is not a hardware no-op (unlike the T21 1.0.33 libimp). The
native backend (`T23_DEFAULT_ENCODER=native`, the default) builds the T21
command list, which already emits 0x40044..0x40068 (zero so far). OpenIMP now
passes `IMP_Encoder_SetChnROI` entries to the native encoder per picture
(`AL_Codec_Encode_SetRoi` -> `OpenIMP_T30_HelixSetRoi`), clamped by
`Helix_H264_RoiSanitize` exactly as on T21; the worker backend still gets them
through the bridge. With no region set the command list is unchanged
(`make -C tests/t23 check`: same OEM register order and digest).
`OPENIMP_T23_ROI=0` switches the programming off. `OPENIMP_DEBUG_ROI` works on
T23 too.

Device run 2026-10-10 (T23, sc2336p, 1920x1080, `tools/roitest`, dark
room, maximum gain, very noisy picture; window 640x384 at (640,352) = 12 % of
the picture; 4 s phases at 10 fps):

| FixQP 30 phase | kbit/s |
|---|---|
| no ROI | 63247 |
| delta -15 | 67440 |
| delta +20 | 58176 |
| absolute 51 (clamped to slice QP +13) | 50250 |
| delta -25 | 71971 |
| absolute 10 (clamped to slice QP -12) | 67460 |
| delta +20 without IDR | 61645 |
| off | 64825 |

CBR 1000 kbit/s (QP 15..45, not reachable in that noise): delta -15 16380,
delta +20 10068, off 11160 kbit/s. Bit rate moves in the expected direction
in every phase, no Helix failure, 0 oops. The base stream is valid
(VA-API 0 errors, `ffmpeg -err_detect aggressive` clean). Second run (same day, one roitest per phase, 1080p, 5 fps, window 320x160
at (160,96)): the QP map shows the window in every phase (delta -15/-25 and
absolute 10 give slice QP -12, delta +20 and absolute 51 give +13, the T21
saturation), FixQP 603 kbit/s base, 663 at -15, 673 at -25, 581 at +20, 578
at absolute 51; CBR 449 / 489 (-15) / 439 (+20). All 9 streams: VA-API 0
errors, `ffmpeg -err_detect aggressive` clean, steps within -13..13; 0 oops.
(`ROITEST_OUT` did not scale on T23.) `roitest` built with `--export-dynamic` replaced libimp's
`imp_log_fun` with a different signature and crashed on T23 with
`ROITEST_LOG=1` (fixed in the tool).

For timps (`src/roi_caps.h`): T23 + OpenIMP can move from "unsupported" to
"effective" with this OpenIMP; vendor T23 stays
as it is (the vendor worker path was measured without effect before).
