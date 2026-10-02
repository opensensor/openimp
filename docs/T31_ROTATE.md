# T31 channel rotation (IMP_FrameSource_SetChnRotate)

Source: `libimp.so_hlil.txt` (T31 libimp 1.1.6, HLIL) and the 1.1.6
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

- `src/framesource/nv12_rotate.c` is plain C with no MXU. It covers all
  three modes:
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

## Cost (software, T31 at 1.4 GHz, not measured on the device yet)

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
