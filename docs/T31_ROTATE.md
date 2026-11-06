# T31 channel rotation (IMP_FrameSource_SetChnRotate)

Source: `docs/re/libimp.so_hlil.txt` (T31 libimp 1.1.6, HLIL) and the 1.1.6
`imp_framesource.h`. Addresses are from that build.

## Vendor API

```c
int IMP_FrameSource_SetChnRotate(int chnNum, uint8_t rotTo90, int width, int height);
```

- `rotTo90`: 0 off, 1 = 90 degrees counterclockwise, 2 = 90 degrees clockwise.
  There is no 180 value.
- `width`, `height`: the size before rotation (the FrameSource channel size).
- Call it before the channel is created. Create the encoder channel with the
  rotated size (height x width). The FrameSource channel attributes stay at
  the landscape size.
- The header says it is software, needs 64-aligned sizes, allocates an rmem
  buffer, and recommends at most 1280x704 at 15 fps. It cannot be combined
  with encoder software scaling.

## Mechanism: software, on the FrameSource thread

There is no hardware rotation. The AVPU command list has no transpose, and
the T31 has no i2d.

1. `IMP_FrameSource_SetChnRotate` (0xa3a90) checks `chnNum < 0x21` and that
   the channel exists. It stores `rotTo90` at channel +0x2ed and +0x2fc,
   clears +0x2f0, and calls `nv12_rotate_init(width, height)`. It then
   returns 0 without checking `rotTo90`.
2. `nv12_rotate_init` (0x29e30) sets globals: SRC_WIDTH/STRIDE = width and
   DST_WIDTH/STRIDE = height. It also sets the split of height into a
   multiple of 64 plus a 16-line remainder (DST_WIDTH1, DST_WIDTH_16) for
   the block loops.
3. `on_framesource_group_data_update` handles every captured frame before
   the observers (OSD, IVS, encoder) are notified (0x9aa5c, 0x9adbc, 0x9b64c):
   - It reads the mode byte at channel +0x32d. Mode 1 uses
     `nv12_left_rotate_90` and mode 2 uses `nv12_right_rotate_90`. Any other
     value leaves the frame unrotated.
   - The first time, it allocates the scratch buffer at channel +0x330 with
     `video_vbm_malloc(w*h*3/2, 0x100)` (rmem).
   - It sets `frame.size = w*h*3/2` and calls
     `rotate(size, frame.virAddr, scratch)`. It then copies the scratch back
     over the frame with `memcpy(frame.virAddr, scratch, size)`.
   - It sets `frame+0x28` (`IMPFrameInfo.rotate_osdflag`) to 1.
     `frame.width` and `frame.height` keep the landscape values.
   - Frames whose pixfmt is the `'NV12'`/`'NV21'` FOURCC branch off at
     0x9aa30/0x9aa40. That branch runs the ispsnap debug dump and the
     16-line height padding, then reaches the same mode check at 0x9adb0.
     Every NV12 frame is rotated.
4. `nv12_left_rotate_90` (0x29eb8) and `nv12_right_rotate_90` (0x2e040) are
   large unrolled loops over 32x32 blocks. Luma takes 32 source rows and
   writes 32-byte destination rows. CbCr (`*_block32_uv`) moves 16-bit pairs.
   - The source CbCr plane is read at `width*height`, which assumes
     `height % 16 == 0`. That is one reason for the 64-alignment rule.
   - Left rotation is counterclockwise: source (x, y) goes to destination
     row `W-1-x`, column `y`. Right rotation is clockwise: (x, y) goes to
     row `x`, column `H-1-y`.

The stream width and height swap only because the application creates the
encoder channel with the rotated size. The encoder writes the SPS from its
own channel size. Because rotation runs before notification, OSD regions
and IVS bound to that channel also see the rotated picture.

## OpenIMP implementation (T31)

- `src/framesource/nv12_rotate.c` is plain C, plus an MXU2 path (see
  "MXU2 path" below). The C code covers all three modes:
  - 90/270: the plane is walked in 32x32 tiles. A tile's source and
    destination lines fit in the 32 KB L1 D-cache. Inside a tile, luma goes
    through 4x4 byte transposes (four `lw`, 24 ALU ops, four `sw` per 16
    pixels). CbCr goes through 2x2 transposes of 16-bit pairs.
  - 180: each row is copied to the mirrored row with the words reversed.
  - Odd geometry and unaligned buffers take a scalar path.
- `openimp_fs_rotate_capture()` (`framesource_tseries.c`) runs on the
  dequeue thread. `VBMKernelDequeue` calls it before the IVS capture and
  before the frame enters the ready queue. That is the same point as in the
  vendor library: before any consumer. For each frame it:
  1. invalidates the cache for the buffer,
  2. rotates into a heap scratch buffer (cached, per channel),
  3. copies the scratch back over the frame and writes back the cache,
  4. sets `rotate_osdflag` (+0x28) = 1.
- Mode 3 (180 degrees) is an OpenIMP extension. The vendor library ignores
  that value.
- Sizes need only be even, not 64-aligned. All buffers use the OpenIMP T31
  NV12 layout: pitch `ALIGN16(w)`, `ALIGN16(h)` luma lines, CbCr at
  `pitch*ALIGN16(h)`. The capture buffer, `ALIGN16(w)*ALIGN16(h)*3/2`, holds
  the rotated frame in both orientations. For 1920x1080 that is 1920x1088
  versus a 1088-pitch 1080x1920 frame, the same size.
- The AVPU source CbCr address is now `ALIGN16(enc_w)*ALIGN16(enc_h)`, in
  step with the source pitch `ALIGN16(enc_w)`. Before, it was
  `enc_w*ALIGN16(enc_h)`. The two are identical for 16-aligned widths. The
  difference matters for 1080- and 360-wide rotated streams. AVC and HEVC
  share this path, and both header writers crop the coded size back to
  `enc_w`.
- Unlike the vendor, the frame record's width and height are set to the
  rotated size. OpenIMP consumers that size themselves from the record then
  see what they get: the JPEG fan-out copy, the IPU OSD (which needs a width
  that is a multiple of 16), IVS, and SnapFrame.
- `SetChnRotate` also takes effect on a running channel from the next
  frame. It rejects odd or zero sizes and modes above 3. The rotation is
  skipped, with one warning, if the capture record matches neither
  orientation of the given size, or if the buffer is too small.
- `OPENIMP_FS_ROTATE_STATS=1` logs the average and maximum rotate time
  after the first frame and then every 100 frames.

Limits:

- Turning rotation off on a running channel leaves the record at the
  rotated size. The vendor does not support that case either.
- OSD and privacy covers: `IMP_OSD_SetRgnAttr` has no picHeight range
  check in OpenIMP. The IPU draws after rotation and clips against the frame
  record, which holds the rotated size (e.g. 704x1280). So regions may cover
  the whole portrait frame.
- IVS reads with stride = width. A rotated width that is not a multiple of
  16 (360) needs IVS on another channel.

## MXU2 path (T10/T20/T31, 2026-10-26)

`nv12_rotate` has an MXU2 path next to the C code, chosen once per process:

- The first rotation calls `nv12_rotate_get_impl()`, which runs
  `nv12_rotate_selftest()` under a SIGILL guard (`src/core/openimp_mxu2.c`):
  a pseudo-random frame is rotated by both paths in all modes over four
  sizes (with and without partial blocks) and compared byte for byte,
  padding included. Pass = MXU2, otherwise C. `OPENIMP_MXU=0` keeps C.
  The decision is logged once (`[MXU] nv12_rotate: ...`).
- Which parts grant MXU2 to a user process (device-checked): T31X, T20X,
  T10L yes. T31L no: the kernel answers the first MXU2 instruction of a
  task with SIGILL, so the library stays on C. T21 (MXU2 opcodes reserved)
  and T23 (no MXU) do not have it. On T23 the probe itself is unsafe (see
  "T23" below): the T23 build never runs it.
- 90/270: 16x16 luma / 8x8 CbCr-pair blocks, four (three) rounds of the
  same perfect shuffle, one SHUFV per output register (64 SHUFV, 16 loads,
  16 stores per 256 luma pixels). `tools/gen_nv12_rotate_mxu2.py` writes
  `nv12_rotate_mxu2_gen.h` and checks the register schedule against a model
  of SHUFV. Two block rows form a band, so each destination row gets 32
  contiguous bytes, a whole cache line; aligned lines are allocated with
  `PREF 30` right before both blocks overwrite them. Bands are anchored at
  destination column 0 (CW: source bottom, CCW: source top) to keep those
  lines aligned. Rows and columns outside whole blocks go through the C
  code.
- 180: one byte- (pair-) reversing SHUFV per 16 bytes; the source row is
  prefetched front to back first and the destination written front to back.
- SHUFV semantics (device-checked): the 32 source bytes are interleaved,
  op[2i] = vt[i], op[2i+1] = vs[i]; vd[k] = 0 if vr[k] bit 7, else
  op[vr[k] & 31]. `lu1q`/`su1q` accept any alignment.

The rotation is bound by memory traffic: on T31X a 1280x1056-byte copy
runs at 1.4 ms front to back with `PREF 30`, 2.5-2.7 ms without, and about
7 ms when either side runs back to front.

Device numbers (heap buffers, microbench, every result bit-exact):

| SoC | size | CCW C / MXU2 | CW C / MXU2 | 180 C / MXU2 |
|---|---|---|---|---|
| T31X | 640x360 | 1.69 / 1.12 ms | 2.65 / 1.62 ms | 1.84 / 0.77 ms |
| T31X | 704x576 | 2.74 / 1.69 ms | 4.36 / 2.25 ms | 3.20 / 1.39 ms |
| T31X | 1280x704 | 7.35 / 4.47 ms | 9.77 / 6.05 ms | 6.92 / 3.27 ms |
| T31X | 1920x1080 | 21.1 / 14.2 ms | 32.8 / 16.3 ms | 16.0 / 8.4 ms |
| T20X | 704x576 | 4.19 / 2.26 ms | 6.39 / 3.30 ms | 4.10 / 2.37 ms |
| T20X | 640x368 | 2.47 / 1.70 ms | 3.94 / 2.12 ms | 2.69 / 1.21 ms |
| T10L | 640x368 | 3.52 / 1.96 ms | 4.98 / 2.90 ms | 3.24 / 1.66 ms |

Under timps on T31X (video0 1280x704, `video0.rotation = 270` = CW,
15 fps, `OPENIMP_FS_ROTATE_STATS=1`, 30 s per run): rotate alone 9.2 ->
5.5 ms per frame, whole capture hook 10.8 -> 7.0 ms; thread `FS(0)-tick`
16.3 -> 10.6 % of a core, timpsd 16.8 -> 11.1 %. Stream 704x1280, no
decode errors, no oops.

## Cost (software C path, T31 at 1.4 GHz, estimate before the device numbers above)

Each frame costs one rotate pass (about 2.5 instructions per luma pixel
plus about 4 per CbCr pair). It also costs one linear copy of
`w*h*1.5` bytes and a cache writeback. Estimates:

| size            | per frame | at 15 fps | at 25 fps |
|-----------------|-----------|-----------|-----------|
| 640x360         | ~1.5-2 ms | ~3%       | ~5%       |
| 1280x704        | ~6-8 ms   | ~10-12%   | ~18%      |
| 1920x1080       | ~15-20 ms | ~25-30%   | ~45%      |

On the host (x86), 1920x1080 takes about 1.0 ms against 0.2 ms for a
`memcpy` of the frame. Measure on the device with
`OPENIMP_FS_ROTATE_STATS=1`.

## T10/T20/T21 (OpenIMP extension, host-tested only)

The vendor T20/T21 libimp has no `IMP_FrameSource_SetChnRotate`; OpenIMP
runs the T31 path above on the T20/T21 userspace too (one build covers
T10 and T20, one T21). Same hook (`openimp_fs_rotate_capture` in
`VBMKernelDequeue`), same in-place rotate + copy back, same
`OPENIMP_FS_ROTATE_STATS=1`. Differences:

- Width and height must be multiples of 16. The Helix (T20/T21) and NVPU
  (T10) encoders read NV12 with pitch = width and the chroma plane after
  `ALIGN16(height)` luma lines, and need a width that is a multiple of 16;
  the rotated width is the source height. 640x360 is refused, 640x368 works.
- At most 704x576 pixels (`FS_ROT_T21_MAX_PIXELS`, i.e. a sub stream).
  A main stream is refused with
  `[FS] SetChnRotate chN: WxH not rotated: software rotation is limited to ...`
  and stays unrotated (SetChnRotate returns -1).
  `OPENIMP_FS_ROTATE_MAX_PIXELS` raises the cap for measurements only.
- The T21 frame record has no `rotate_osdflag`; only width/height change.
- The caller creates the encoder channel with the rotated size; the encoder
  takes the size from the frame record anyway.

Memory per rotated stream: one heap scratch buffer of `w*h*1.5` bytes
(640x368: 345 KiB, 704x576: 594 KiB), freed on DestroyChn or rotation off.
No extra rmem: the capture buffers and the encoder references keep the same
size (both sides are multiples of 16).

CPU estimate, scaled from the T31 device number (1280x704 in 9 ms at
1.4 GHz) to the ~1 GHz T10/T20/T21 cores with slower DDR (x1.5-1.8):

| size      | per frame | at 15 fps | at 25 fps | allowed |
|-----------|-----------|-----------|-----------|---------|
| 640x368   | ~3.5-4.5 ms | ~5-7%   | ~9-11%    | yes     |
| 704x576   | ~6-7.5 ms | ~9-11%    | ~15-19%   | yes     |
| 1280x720  | ~14-17 ms | ~21-26%   | ~35-43%   | no      |
| 1920x1080 | ~30-37 ms | ~45-56%   | ~75-90%   | no      |

The rotation runs on the FrameSource dequeue thread, so it adds that much
latency to the rotated channel's frames.

## T23 (OpenIMP extension, device-tested 2026-11-05)

The vendor T23 libimp (1.3.0) has no `IMP_FrameSource_SetChnRotate`.
OpenIMP builds the T10/T20/T21 path above for T23 too (`FS_SW_ROTATE` in
`framesource_tseries.c`, the capture hook in `kernel_interface.c`,
`nv12_rotate.o` in `build-t23.sh`), with the same rules: width and height
multiples of 16, at most 704x576 pixels (`OPENIMP_FS_ROTATE_MAX_PIXELS`
for measurements), the encoder channel created with the rotated size, the
frame record (`+0x08/+0x0c`) carries the rotated size. The T23 record has
no `rotate_osdflag` (+0x28 is the capture timestamp there).

- Encoder: the T23 native Helix encoder (`src/t30/t30_helix_encoder.c`)
  reads NV12 with stride = width and chroma at `mb_width*16 * mb_height*16`,
  the T21 layout, so a rotated 16-aligned frame needs no change there.
- No MXU on T23 (BSP guide V1.1: "The MXU is not supported by the T23";
  `/proc/cpuinfo` ASEs empty). Worse, the T23 kernel does not answer a COP2
  instruction with SIGILL: one `lu1q` from user space oopsed the kernel in
  `xburst_cop2_restore` and the watchdog reset the board (device
  2026-11-05, T23N sc1a4t, sled `kernel_oops` at 0x80011c38). The T23
  `SetChnRotate` therefore fixes the C path (`nv12_rotate_set_impl(0)`)
  before the first frame, so the SIGILL-guarded selftest never runs.
- Device numbers (two T23N, sensor 15 fps, test client: ch0 at the sensor
  size, ch1 rotated, H.264 + JPEG on ch1, `OPENIMP_FS_ROTATE_STATS=1`,
  15 s per case; every stream decodes with 0 errors, 15.0 fps out):

| size (before rotation) | hook per frame (rotate alone) | CPU at 15 fps |
|---|---|---|
| 640x352 | 2.9-3.9 ms (2.3-3.2 ms) | ~5 % |
| 704x576 | 6.5-6.7 ms (5.5-5.6 ms) | ~10 % |
| 960x544 (cap raised) | 9.1 ms (7.9 ms) | ~14 % |
| 1280x720 (cap raised, no main stream) | 14.7-15.0 ms (13.0-13.4 ms) | ~22 % |

  These are the numbers of the first T23 build (32x32-tile C kernel). The
  banded C kernel of 2026-11-06 roughly halves them, see "Cost per frame"
  below.

  The 704x576 cap stays: a rotated 720p main stream keeps 15 fps but costs
  about a fifth of the single core, and 1080 is not a multiple of 16. On the
  sc2336p board a 1280x720 rotated sub stream next to the 1080p main
  encoder did not fit in rmem (the main encoder failed; rotation itself
  takes no rmem).
- Orientation checked on the pictures (mode 1 CCW, mode 2 CW) at night
  (grey); the chroma rotation is proven bit-exact on the host only
  (`fs_lifecycle_test_t23`).

## Cost per frame: C kernel and copy back (2026-11-06)

Where the time went (T23N, no MXU, `nv12_rotate_bench` and a luma micro
benchmark, 704x576, cached buffers, streamer stopped):

- The whole capture hook was ~85 % rotation and ~15 % copy back plus cache
  flushes (stats line: 6.5 ms hook, 5.5 ms rotate).
- Rotation itself, luma plane of the 32x32-tile kernel: 2.4 ms, of which
  ~1.0 ms are the transposes alone (data in L1), ~0.7 ms the tile loads and
  ~1.1 ms the scattered 4-byte stores (each tile touches 32 destination
  lines 8 times). A memcpy of the same bytes takes 0.47 ms; sequential
  stores run 1.9x faster with `PREF 30`; `PREF 0` on the source made the
  rotation slower.

The C kernel (`rot90_words_banded`, used for the whole plane when it is
word-aligned in whole 4x4 / 2x2 blocks):

- A band is the source rows that fill one destination cache line (32 luma
  rows, 16 CbCr pair rows). For CW with destination rows that are not
  line-aligned (pitch 368, 720) a band fills two lines (measured 2-10 %
  faster there; CCW is faster with one). Bands are anchored at destination column 0, the remainder
  band at the far end, as in the MXU2 path.
- The walk goes along x in 4-column steps and down the band, so each
  destination line is allocated with `PREF 30` and written front to back
  in one go; the band's source lines stay in L1 across the 8 steps that
  read them.
- The 4x4 byte transpose takes 18 operations instead of 28 (16-bit 2x2 with
  `INS`, then a byte delta swap). The compiler must not hoist one pointer
  per source row out of the unrolled loop (it spilled 32 of them to the
  stack): `NV12_ROT_OPAQUE`.

T23N .30 (sc1a4t), `nv12_rotate_bench`, ms per frame, CW (CCW is similar):

| size | 32x32 tiles | banded | memcpy |
|---|---|---|---|
| 640x352 | 2.44 | 1.28 | 0.41 |
| 640x368 | 2.71 | 1.74 | 0.42 |
| 704x576 | 4.43 | 2.36 | 0.71 |
| 960x544 | 5.76 | 3.13 | 0.92 |
| 1280x720 | 12.2 | 8.6 | 1.6 |

Sizes with a source stride of 1024 or 1280 bytes cost more per byte in
both kernels, and the numbers vary with the buffer placement, so the cause
is probably cache or DRAM aliasing (UNVERIFIED). Staging the band through
an L1 buffer and 64/128-row bands did not help.

Whole capture hook on the device (rottest client, ch0 at the sensor size
plus ch1 rotated with H.264 + JPEG, `OPENIMP_FS_ROTATE_STATS=1`, 15/15 fps,
0 decode errors, pictures checked against the old build):

| T23N | size | before | after | client CPU before -> after |
|---|---|---|---|---|
| .30 sc1a4t | 640x352 CW | 3.4-3.6 ms | 1.5-1.7 ms (spare) | 8.7 -> 5.3 % |
| .30 sc1a4t | 704x576 CW | 6.4-7.0 ms | 3.9 ms, 3.0 ms (spare) | 11.3 -> 9.0 / 7.4 % |
| .30 sc1a4t | 960x544 CW (cap raised) | 8.2-8.3 ms | 4.3-4.6 ms (spare) | 14.4 -> 8.0 % |
| .30 sc1a4t | 1280x720 CW (cap raised, no main) | 14.9-16.5 ms | 8.8-9.7 ms (spare) | 23.7 -> 15.3 % |
| .23 sc2336p | 640x352 CW | 3.7-4.3 ms | 2.2 ms | 8.2 -> 6.4 % |
| .23 sc2336p | 704x576 CW | 6.3-6.6 ms | 4.0 ms, 3.05 ms (spare) | 13.2 -> 8.8 / 7.8 % |
| .23 sc2336p | 960x544 CW (cap raised) | 8.5-8.6 ms | 5.2-5.3 ms | 15.9 -> 10.8 % |

Copy back or spare (`OPENIMP_FS_ROTATE_SPARES`, default 0):

- The vendor rotates into a scratch buffer and copies it back over the
  capture buffer. The copy cannot be avoided in place: every destination
  row takes bytes from every source row.
- With `OPENIMP_FS_ROTATE_SPARES=1` or `2`, the frame is rotated into a
  spare rmem buffer and the frame record (+0x18 phys, +0x1c virt, T23 also
  +0x20) points there. OSD, IVS, JPEG and the encoders read those fields
  of the record. The address goes back to the capture buffer when the
  record is released (`openimp_fs_rotate_release`), before the QBUF in
  `vbm_release_frame` and in the ready-queue-full requeue. So the driver
  gets the same buffer per index, and a spare is never written by the ISP.
  A destroyed pool frees every spare (`openimp_fs_rotate_pool_gone`).
- Without a free spare the frame is copied back. That happens when the
  consumers hold all spares, when the largest free rmem block would drop
  below 1 MiB, or when the allocation fails.
- It saves the memcpy, 0.25-1.0 ms per 704x576 frame. One spare was enough
  for the test client (100/100 frames).
- It is off by default because a spare costs one more frame of rmem. It is
  taken at the first rotated frame, so it can take the room of a block
  allocated later. On the .23 board (1080p main) a 960x544 spare left the
  main encoder's 2 MB `t23-helix-emc` without room, and the main stream did
  not start. Without spares it runs.
- Host tests (`fs_lifecycle_test`): the record points at the spare, release
  restores it (T23: +0x20 too), a second release does nothing, the copy
  back is used when the spares are busy, a failed allocation is not
  retried while an existing spare is still used, and a running channel
  never queues an index with another address (`fake_qbuf_moved`).
- Not tested: ASan/TSan (libasan is missing on the build host) and timps.

MXU2 (T10/T20/T30/T31): unchanged. The T30X MXU2 numbers (1280x720 8.6-10.6
ms) are close to the new C kernel on the T23N (8.6 ms). `nv12_rotate_bench
--mxu` (static build: tiles, banded, MXU2 and memcpy in one run) shows on
a T30/T31 whether the banded C kernel should be preferred there (open).

