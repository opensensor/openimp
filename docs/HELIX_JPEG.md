# Helix hardware JPEG (T20, T21, T23, T30)

OpenIMP encodes JPEG on the VPU of the Helix SoCs, with no vendor library at
run time: `src/t30/helix_jpeg.c`. It is the same mechanism the stock libimp
uses. This page documents the interface as recovered from the vendor
binaries and the GPL `soc_vpu` kernel driver.

## Sources

| What | Where |
|---|---|
| T20 libimp 3.12.0, T21 1.0.33, T23 1.3.0, T30 1.0.5 (`libimp.a`) | `ijpege_*` (`encode.c`, `set.c`, `reconfig.c`, `frame.c`, `iicodec.c`), `hwicodec_pf_jpege_t20` (`encode.c`), `T20_JPEGE_SliceInit`, `huffenc_t20`, `qmem_t20` (`jzm_jpeg_enc.c`) |
| `soc_vpu` kernel driver (Thingino kernel trees) | `drivers/video/soc_vpu/{soc_vpu.c,channel_vpu.h,helix/helix.[ch],jz_nvpu/*}` |

Call chain in the stock library: `IMP_Encoder` JPEG channel or
`IMP_Encoder_InputJpege` -> `ijpege_encode` -> `ijpege_write_header` (CPU) ->
`ijpege_icodec_enc` -> `hwicodec_pf_jpege_t20_enc` -> `T20_JPEGE_SliceInit`
(VDMA command list) -> `ioctl(/dev/soc_vpu, CHANNEL_RUN)` -> `memcpy` of the
entropy-coded data behind the header -> `ijpege_write_tail` (EOI). There is
no software JPEG encoder in these libraries.

`T20_JPEGE_SliceInit` is instruction-for-instruction the same in the T20,
T21 and T30 libraries (T20 also carries the older T10 variant
`JPEGE_SliceInit`, unused on T20 silicon). T23 has its own build with three
differences (below). `huffenc_t20`/`qmem_t20` are byte-identical in all four.

## Kernel interface (`/dev/soc_vpu`)

The JPEG job uses the same channel ABI as the native H.264 path
(`src/t30/t30_helix_encoder.c`):

| | |
|---|---|
| `CHANNEL_REQUEST` | `_IOWR('c', 0, channel_node)`: `0xc0386300`, T23 `0xc0586300` (88-byte node) |
| `CHANNEL_RUN` | `0xc0386302` / `0xc0586302` |
| `CHANNEL_RELEASE` | `0xc0386301` / `0xc0586301`, `workphase = 2` |
| `codecdir` | `HWJPEGENC = 0x10000` |
| `vpu_id` | `0x02000001` (`VPU_HELIX_ID`), T20 `-1` (`RANDOM_ID`, JZ NVPU) |
| `mdelay` | 20000 ms in the stock library (OpenIMP T23: 2000, `OPENIMP_T23_HELIX_TIMEOUT_MS`) |
| `dma_addr` | bus address of the command list |
| `output_len` | `JPGC_STAT & 0xffffff`, the entropy-coded byte count |
| `status` | `SCH_STAT` of the completion interrupt: `ENDFLAG (0x1) | JPGEND (0x10)`; errors `ACFGERR 0x4`, `BSERR 0x80`, `ORESERR 0x400`, T23 `BSFULL 0x100000` |
| T23 `max_bs_act` | `JPGC_ACT_BS` when `JPGC_MAX_BS` bit 31 is set |

`CHANNEL_RUN` requests a free VPU (waiting up to `mdelay`), resets it, starts
the VDMA list (`REG_VDMA_TASKRG`, T21/T23 `REG_VDMA_TASKRG_T21`), waits for
the interrupt, and releases the VPU. Jobs of all channels and processes are
therefore serialised on the core and every job starts from a reset VPU: a
JPEG job can run between two H.264 pictures (native or OEM-worker encoder on
T23) without any user-space coordination. On a timeout the driver resets the
core. The interrupt handler reads the length from `JPGC_STAT` and clears
`JPGC_STAT.ENDF` when `SCH_STAT.JPGEND` is set.

## Command list

VDMA "ACFG" pairs, as for H.264: word 0 the value, word 1
`0x80000000 (VLD) | 0x40000000 (TERM, last pair) | (reg & 0xffffc)`.
Registers are VPU offsets; the SRAM between EFE and JPGC is addressed by bus
address (VPU base + `0xf0000`): `0x132f0000`, T23 `0x131f0000`.

| # | Register | Value |
|---|---|---|
| 1 | `0xc0000` TCSM_FLUSH | 0 |
| 2 | `0xe0004` JPGC_GLBI | 1 (core clock on) |
| 3-386 | `0xe1800 + 4i` JPGC_HUFE | Huffman encode table, 384 words |
| 387-642 | `0xe1400 + 4i` JPGC_QMEM | quantizer reciprocals, 256 words |
| 643 | `0xe0008` JPGC_STAT | 0 |
| 644 | `0xe000c` JPGC_BSA | bitstream buffer |
| 645 | `0xe0010` JPGC_P0A | SRAM |
| 646 | `0xe0028` JPGC_NMCU | `mb_width * mb_height - 1` |
| 647 | `0xe002c` JPGC_NRSM | 0 (no restart interval) |
| 648-650 | `0xe0030/34/38` JPGC_P0C/P1C/P2C | `0x30`, `7`, `7` (Y: table set 0; Cb, Cr: set 1) |
| T23 | `0xe0068` JPGC_MAX_BS | `enable << 31 | limit`; the stock T23 library enables it with the stream pool size |
| | `0xe0004` JPGC_GLBI | `0xa0121` (encode, 3 components, 4:2:0, raster input from the EFE) |
| | `0xe0000` JPGC_TRIG | 7 (`CORE_OPEN | BS_TRIG | PP_TRIG`) |
| | `0x40004` EFE_GEOM | `(mb_height - 1) << 16 | (mb_width - 1)` |
| | `0x40010` EFE_RAWY_SBA | luma plane |
| | `0x40014` EFE_RAWC_SBA | interleaved chroma plane |
| | `0x40034` EFE_RAWV_SBA | 0 (third plane, unused for NV12) |
| | `0x40038` EFE_RAW_STRD | `luma_stride << 16 | chroma_stride` |
| | `0x40030` EFE_RAW_DBA | SRAM |
| last | `0x40000` EFE_CTRL + TERM | `0x400b | plane` (`EFE_ID_JPEG | NV12 | EN | RUN`; NV21 `0x400f`); T23 adds bit 7 when the width is not a multiple of 16 and bit 29 for ISP-direct (IVDC) input |

659 pairs (T23: 660), 5.2 KiB; the stock library allocates 64 KiB.

The stock encoder also has a non-EFE mode (`raw_format` 0: JPGC_P0A/P1A
point at tiled planes and the list ends with `JPGC_TRIG | TERM`); the IMP
layer never uses it.

### JPGC_HUFE (Huffman encode table)

Entry = `(code_length - 1) << 8 | (code & 0xff)`; longer codes start with
ones, which the core supplies. Layout:

| Words | Content |
|---|---|
| 0-159 | luma AC, `run * 10 + size - 1` (run 0..15, size 1..10) |
| 160, 161 | luma EOB, ZRL |
| 162-167 | `0xfff` (unused) |
| 168-175 | `0xfd0..0xfd7` (RST0..7 as 16-bit codes) |
| 176-351 | chroma AC, same layout |
| 352-367 | luma DC categories 0..11, then `0xfff` |
| 368-383 | chroma DC |

The stock table is exactly the standard Annex K code set;
`HelixJpeg_HuffmanTable()` derives it from the Annex K counts/values (checked
word for word against `huffenc_t20`).

### JPGC_QMEM (quantizers)

Words 0-63 luma, 64-127 chroma, 128-255 zero. The order is the DQT order
(zigzag), i.e. `qmem[i] = recip(qt[i])` for the same 128 bytes the header's
two DQT segments carry. `recip(q) = k << 11 | round(2^(11+k) / q)` with a
per-step shift `k` (0..7) taken from the stock lookup (`qlook`,
`ijpege_reconfig`); `recip(0) = 0`, `recip(1) = 1`. The shift selection does
not follow a simple error rule, so OpenIMP keeps the 256 shifts as a 128-byte
nibble table; the mantissa is computed. `HelixJpeg_QmemEntry()` reproduces
all 256 `qlook` entries.

## Quality

* Channels: the stock library starts with its built-in table set 0 (also
  sets 1 and 2 exist, quality index from the frame), which is exactly the
  IJG quality-75 scaling of the Annex K tables. `IMP_Encoder_SetJpegeQl`
  (`user_ql_en`, `qmem_table[128]` = luma then chroma, DQT order) replaces
  them: written into DQT unchanged and loaded through `qlook`.
* `IMP_Encoder_InputJpege` (T23): the stock library builds IJG tables for
  the quality argument (`MakeTables_Imp`: scale `5000/q` below 50, else
  `200 - 2q`; steps clamped to 1..255).

OpenIMP does the same (`HelixJpeg_QualityTables()`).

## Picture format and limits

* Input: NV12 (or NV21) with one stride for both planes; the stock encoder
  uses `stride = mb_width * 16`. Chroma may sit anywhere (`EFE_RAWC_SBA`):
  the framesource puts it at `stride * aligned_height`,
  `IMP_Encoder_InputJpege` right after `width * height` luma bytes.
* MCU 16x16 (4:2:0), whole macroblock rows: the EFE reads
  `aligned_height = (height + 15) & ~15` luma rows and half as many chroma
  rows. SOF0 carries the true size, decoders crop.
* `ijpege_init`: width a multiple of 16 and at least 256 (`mb_width >= 16`),
  height even and at least 16. The descriptor fields are 16 bits wide.
* Output: entropy-coded data with byte stuffing; no markers. The CPU writes
  SOI, the tables, SOF0, SOS before it and EOI after it (stock: no APP0;
  OpenIMP adds the JFIF APP0 like its software encoder).
* Bitstream buffer: the stock library sizes the JPEG output like the NV12
  picture (`aligned_w * aligned_h * 3 / 2`, `ijpege_init`); T20/T21/T30 have
  no hardware overflow guard, T23 programs `JPGC_MAX_BS` (below).

## Differences between the SoCs

| | T20 | T21 | T30 | T23 |
|---|---|---|---|---|
| VPU driver | `jz_nvpu` | `helix` | `helix` | `helix` (`CONFIG_SOC_T23`) |
| VPU base / SRAM | 0x13200000 / 0x132f0000 | same | same | 0x13100000 / 0x131f0000 |
| channel node | 56 bytes | 56 | 56 | 88 (`max_bs_act`, IVDC fields) |
| `vpu_id` | -1 | 0x02000001 | 0x02000001 | 0x02000001 |
| `JPGC_MAX_BS` | - | - | - | yes |
| EFE_CTRL extra bits | - | - | - | bit 7 (width % 16), bit 29 (IVDC) |
| VDMA start register | `VDMA_TASKRG` | `VDMA_TASKRG_T21` | `VDMA_TASKRG` | `VDMA_TASKRG_T21` (kernel side only) |

## OpenIMP integration

* `src/t30/helix_jpeg.c`: tables, command list, header, and one process-wide
  VPU channel used under a mutex; per-job rmem buffers (below).
* JPEG channels (`codec-t40.c`, T20/T21/T30/T23): while the hardware path
  is usable the P2 layer lends the capture frame to the JPEG channel
  (`p2_frame_pin.h`, as on T31) instead of copying it; the frame goes back
  to the FrameSource when the H.264 picture and the JPEG job are done (or at
  DestroyChn). The VPU reads it in place, also when the pool's frame size is
  the kernel's `width * height * 3 / 2`: like the H.264 encoder on the same
  buffer it then reads up to 7.5 lines of chroma padding past it. The
  missing bottom macroblock rows are filled from the last picture row (the
  source rows invalidated first, as for H.264). Only T23 writes the frame
  back before the job (CPU OSD). `OPENIMP_HELIX_JPEG_SRC_COPY=1` copies
  instead (heap; the encoder then copies into rmem per job).
* rmem: the stock encoder allocates, per JPEG channel at channel creation
  (`ijpege_init` -> `hwicodec_pf_jpege_init_bpool`, `hwicodec_cal_bufsize`),
  one buffer of `width * (height + 16) * 3 / 2 + 256` bytes (1 KiB aligned)
  plus 20 KiB for the command list, from the VBM (rmem), and keeps it.
  OpenIMP allocates one buffer shared by all JPEG channels (jobs are
  serialised) when a JPEG channel is created (`OpenIMP_HelixJpeg_Reserve`,
  `OPENIMP_HELIX_JPEG_RESERVE_AT_CREATE=0` defers it to the first picture),
  grows it for a larger channel and keeps it: the 8 KiB command list
  followed by the bitstream (the kernel's RUN writes back and invalidates
  the first MiB from the command list). Bitstream: T20/T21/T30 have no
  hardware limit (their kernel and libimp never use `JPGC_MAX_BS`), so it
  holds the whole NV12 picture as in the stock library (3.0 MiB at 1080p).
  A strict worst case cannot be bounded below that: with Annex K codes a
  block can take up to ~1500 bits at quality 75 and byte stuffing can
  double it, several times the NV12 size; the stock library relies on
  real pictures. T23 uses the same NV12 size and programs `JPGC_MAX_BS`
  with it (the stock T23 library programs its encoder pool size, 2.4 MB or
  600 KB by SoC variant, `IMP_Encoder_SetPoolSize`).
  `OPENIMP_HELIX_JPEG_BS_DIVISOR=n` (only with the limit) uses 1/n of NV12
  and repeats a job that reaches the limit with the NV12 size.
* Bitstream limit reached (T23): the job completes normally
  (ENDFLAG|JPGEND) with a truncated bitstream; the kernel returns
  `JPGC_ACT_BS` in `max_bs_act` and bit 29 flags the truncation (the stock
  library tests it in `do_channel_process_jpege` and
  `IMP_Encoder_InputJpege`). The stock T23 library then drops the picture
  and lowers the channel's JPEG quality by 5 (starting from 70,
  `MakeTables_Imp` tables) for the following pictures, down to 0 (error
  log about the pool size). OpenIMP never serves a truncated picture:
  bit 29 (other cores: a length within 4 KiB of the limit), BSFULL or a
  length at the buffer end fails the job; with the limit it repeats the
  picture once with doubled quantizers (the JPEG header carries the tables
  used), otherwise the picture fails.
A source copy for frames
  outside rmem is per job. No allocation leaves less than the reserve free
  in rmem (1/16 of the arena, at least 512 KiB,
  `OPENIMP_HELIX_JPEG_RMEM_RESERVE_KB`); a miss skips the picture without
  counting as a hardware failure.
* `OPENIMP_HELIX_JPEG_PROBE_MAX_BS_KB=n` (device probe): every job gets an
  n KiB bitstream limit with a 1 MiB guard behind it and logs
  `Helix JPEG probe: ... guard_bytes_written=... -> limit respected` or
  `LIMIT IGNORED`.
* Completion: RUN must succeed with `SCH_STAT` ENDFLAG and JPGEND (0x11),
  no error bits, and a length below the buffer size. The kernel completes
  only on ENDFLAG or BSFULL, so an error interrupt makes RUN wait for the
  whole timeout: 2 s (stock 20 s; `OPENIMP_HELIX_JPEG_TIMEOUT_MS`).
* `OPENIMP_HELIX_JPEG_STATS=1` logs every job: path (direct/copy), result
  (ok/FAIL/SKIP), status, core length, JPEG size, bitstream buffer, first
  quantizer, time, rmem use and largest free block, and the failure reason.
  `OPENIMP_HELIX_JPEG_MAX_BS=1` also programs `JPGC_MAX_BS` on T20/T21/T30
  (and then sizes the buffer as on T23) to test whether the register works
  there.
* `IMP_Encoder_InputJpege` (T23): reads VBM (rmem) sources in place, others
  from a copy.
* Build: `OPENIMP_SW_JPEG=0` (default on these SoCs) leaves the software
  baseline encoder out, like the stock library; a hardware error then fails
  that picture. With `OPENIMP_SW_JPEG=1` the software encoder takes pictures
  the VPU cannot (width < 256 or not a multiple of 16), takes over for good
  after three consecutive hardware errors, and `OPENIMP_HELIX_HW_JPEG=0`
  selects it at run time.
* Host test: `tests/t30/helix_jpeg_test.c` (`make -C tests/t30 check`) runs
  the encoder against a fake `/dev/soc_vpu` that executes the command list
  with the loaded tables, and decodes the resulting files with a decoder that
  only reads the file.
