# T40 HEVC (H.265) on the AVPU

Status: device-tested on a T40XP (Eufy S350, SC830AI, vendor kernel driver), 2026-10-10; pictures correct since claude/t40-h265-rc2 (PPS tool flags, HWRC on).

## What blocked it

`IMP_Encoder_CreateChn` refused HEVC on every SoC except T31 and T41
(`openimp_p2_encoder.c`), and the codec only enabled the HEVC parts (codec
flag, VPS/SPS/PPS + slice header writer, HEVC EP1 lambda table, HEVC EP2
size, AVPU selection for codec 1) for T31/T41.  The T40 command list had no
HEVC variant.  There is no hardware or firmware difference: the vendor T40
libimp 1.3.1 uses the T31 `SliceParamToCmdRegsEnc1` packer.

## Vendor T40 HEVC command lists (T40XP, libimp on the board, CBR)

Captured with an LD_PRELOAD that dumps each pushed command list
(`0x83e0` address, `0x83e4` push) from `/dev/mem`; ch0 3840x2160 at
4 Mbit/s, ch1 1280x720 at 1 Mbit/s, 20 fps.  Differences from the T40 AVC
template:

| word | vendor HEVC | note |
|------|-------------|------|
| 0x00 | `0x91700c11` | codec 1, CTB 32, TU 4..32 (as T31) |
| 0x02 | `0xc010ad50`, P `0xc090ad50` | bit 31 always, bit 23 on P |
| 0x06/0x07/0x0b | CTB grid | as T31 |
| 0x08 | IDR `0x77000000`, P `0x33000000` | AVC P: `0x11000000` |
| 0x09 | `0xfc058000` | vendor runs scaling lists + transform skip; OpenIMP clears bits 31/30/15 and signals none (as T31) |
| 0x0a | `area_1k << 16 \| 0x5000` | AVC area, HEVC PCM size |
| 0x0c..0x11 | POC words `2n` | AVC numbering, not the T31 HEVC `n` |
| 0x12 | bit 31, bits 27:24 = `(min(512 / ceil(w/64), 128) - 1) & 0xf` | L2 cache rows (UpdateCommand); 4K 7, 720p 8 |
| 0x13 | AVC formula `ceil(w/64) - 1` | unchanged |
| 0x14 | HWRC grid on the CTB grid | 4K `0xf40001ce`, 720p `0xf4000107` |
| 0x15 | T40 AVC target x 2 on the CTB grid | 4K `0x1d1a`/`0x14ca`, 720p `0x2856`/`0x1ccf` |
| 0x16 | `0x3f000000 \| bitrate * groups / (ctbs * fps)` | `0xc4`, `0x10f` |
| 0x18 | restart bits on IDR and first P (T31 rule) | |
| 0x19..0x1f | 0 | no inline Enc2 |
| 0x2d | EP3 slot: IDR one slot above P | |
| 0x64/0x65 | 0 | AVC writes width/height |

Addresses, pitches, map sizes and the MV offset are the AVC ones.

Vendor reference streams (same settings) decode clean with
`ffmpeg -err_detect aggressive`: ch0 190 frames, ch1 168 frames,
about 4.7 and 1.05 Mbit/s.

## PPS tool flags: the T40 core ignores cmd[9] for transform skip and cu_qp_delta

Until 2026-10-10 every OpenIMP T40 HEVC stream was broken after the first
CTB rows (also with HWRC off): ffmpeg `-err_detect aggressive` mostly
reported nothing but decoded green garbage, and VA-API failed with
"internal decoding error".  The earlier "clean" results only checked
decoder messages.  The vendor T40 PPS always sets
`transform_skip_enabled_flag` and `cu_qp_delta_enabled_flag` (also for
FixQP); the T40 core codes both syntax elements whatever cmd[9] says.
Tested on the T40XP at 720p (5 s each, `OPENIMP_T40_HEVC_TOOLS`):

| tools | PPS transform skip | PPS cu_qp_delta | picture |
|-------|--------------------|-----------------|---------|
| 0 | 0 | HWRC only | broken |
| 1 | 1 | HWRC only | broken |
| 4 | 0 | 1 | broken, cu_qp_delta errors |
| 5 | 1 | 1 | correct, ffmpeg and VA-API clean |
| 7 | + SPS default scaling lists, cmd[9] bits 31/30 | 1 | grey, about 330 bytes per picture |

OpenIMP now writes both PPS flags on T40 (cmd[9] bit 15 set as the
vendor), keeps scaling lists off and signals full range in the VUI like
the vendor (default `OPENIMP_T40_HEVC_TOOLS=13`).  The PPS no longer
depends on the RC mode, so a switch between FixQP and CBR does not force
an IDR on T40.

## Hardware rate control (HWRC) is on by default

The "cu_qp_delta outside the valid range" errors with HWRC on were the
same desynchronisation.  With the PPS flags fixed, HWRC (cmd[9] bit 16,
cmd[0x14..0x18]) decodes clean and is the default, as for H.264.
`OPENIMP_T40_HEVC_HWRC=0` falls back to picture-level QP.  The HWRC
targets cmd[0x15/0x16] use the vendor formula already (1.9 x and 95/70 x
bit/s per group; the HEVC capture matched word for word), cmd[0x17] gets
the rc2 min QP and IDR QP fields.

## Device results (claude/t40-h265-rc2, 2026-10-10, night, IR-cut day)

One channel per run, 30 s, 20 fps, GOP 40 x 2.  Clean = `ffmpeg
-err_detect aggressive` and VA-API without messages.  SSIM against a
vendor HEVC FixQP 22 reference taken in the same session.

| run | OpenIMP HWRC on | OpenIMP HWRC off | vendor |
|-----|-----------------|------------------|--------|
| 4K CBR 4000 | 4008 kbit/s, SSIM 0.956, 19.24 fps, clean | 4059, 0.959, 19.36 fps, clean | 4117, 0.959, 19.94 fps |
| 720p CBR 1000 | 1075, 0.964, clean | 1074, 0.963, clean | 1049, 0.964 |
| 4K FixQP 30 | 2451, 0.960, 19.37 fps, clean | (FixQP never uses HWRC) | 1840, 0.959 |
| 720p FixQP 30 | 80.0, 0.968, clean | | 74.3, 0.968 |

Two channels in one process (4K CBR 4000 + 720p CBR 1000, 20 s):
OpenIMP 337 + 349 pictures (17.0 / 17.5 fps), vendor 375 + 359
(about 18.8 / 18.0 fps), all clean.  The vendor tool process hung at
teardown after this run (killed; device and streaming application fine), so the
throughput gap is not profiled yet.

Open points (the first two were closed in claude/agg37-t40, see below):
- FixQP 4K +33 % bit rate at equal SSIM: the FixQP lambda table of H.264
  was not applied to HEVC.
- 4K throughput: 3 % (one channel) and about 10 % (two channels) fewer
  pictures than the vendor with identical command words.
- Slice header POC counts 1 per picture, the vendor 2 (cmd[0x0c..0x11]
  already use 2n); harmless for one reference.

## FixQP lambdas and 4K throughput (claude/agg37-t40, 2026-10-11)

**FixQP lambda table.** The vendor HEVC command lists (captured at QP 22
and 30, 3840x2160 and 1280x720; EP1 words 0..51, rest zero) carry exactly
the tables of the H.264 FixQP path: P pictures `00 v v v` per QP (v = 1
up to QP 19, then the `avc_fixqp_lda` ramp), IDR pictures only the intra
lane (byte 2 of the P table).  They do not depend on the QP.  OpenIMP wrote
them for AVC only; HEVC kept the generated default table.  The per-picture
rewrite (`avpu_t40_fixqp_lambdas()`) now runs for HEVC too
(`OPENIMP_T40_FIXQP_LDA=0` restores the old table).

**Throughput.** The profile (`OPENIMP_PROFILE=1`) showed that the AVPU was
idle for about 8 ms per picture between the completion interrupt and the
next command push, which is 16 % of the 50 ms picture period at 20 fps:

| stage (4K HEVC CBR, average per picture) | before | after |
|------------------------------------------|--------|-------|
| `stream_clear` (memset of the whole stream buffer) | 3560 us | 49 us |
| `stream_finalize` (fixed `usleep(2000)` drain)     | 2160 us | 69 us |
| `encode_submit` (CPU time of the submit)           | 3920 us | 339 us |
| `irq_completion` (wall)                            | 2335 us | 254 us |
| `encoder_poll` (CPU time)                          | 3786 us | 507 us |

1. The stream buffer was cleared completely before every picture so that
   the fallback end scan could not see stale bytes.  Now only what the
   previous picture left behind is cleared (`stream_dirty_by_buf`: the
   payload end of the last completion, 0 = unknown = clear all, set to 0
   again at every submit until the completion has validated the extent) and
   only that range plus the header prefix is written back.
   `OPENIMP_T40_STREAM_CLEAR_EXTENT=0` clears everything again.
2. The 2 ms sleep before reading the completion count was a guess at the
   time the last DMA burst needs.  The buffer is zero beyond what the AVPU
   wrote and the last byte of a slice is never zero (stop bit, or the 0x03
   of `cabac_zero_words`), so after the cache invalidate a zero last byte
   means the burst is late; it is awaited in 100 us steps (limit 4 ms)
   instead of always sleeping.  `OPENIMP_T40_DRAIN_VERIFY=0` restores the
   fixed 2 ms.  `OPENIMP_T40_DRAIN_CHECK=1` (debug) additionally waits 2 ms
   and compares the whole payload and the 0x8304 count with the early
   reading: 297 + 237 pictures at 4K, 0 mismatches, 0 late bursts.

Device results (Eufy T40XP, IR-cut day, streaming application stopped, one session each,
`ffmpeg -err_detect aggressive` and VA-API clean for every stream below;
SSIM against the vendor FixQP 22 stream of the same session):

| 4K HEVC | vendor | before | after |
|---------|--------|--------|-------|
| CBR 4000, 20 s | 19.91 fps, 4100 kbit/s | 19.30 fps, 4081 | 19.90 fps, 4123 |
| FixQP 30, 10 s | 19.83 fps, 2890 kbit/s (I 128.5 KB, P 16.5 KB) | 19.25 fps, 3682 (+27 %; I 86.9 KB, P 22.9 KB) | 19.79 fps, 2862 (I 129.5 KB, P 16.4 KB) |
| FixQP 30 SSIM | 0.9260 | 0.9256 | 0.9249 |
| 720p FixQP 30, 8 s | 201.2 kbit/s | 234.7 (+17 %) | 201.5 |
| two channels, 20 s (4K CBR 4000 + 720p CBR 1000) | 377 + 356 pictures | 338 + 341 | 393 + 379 |

H.264 (same code path, 4K CBR 6000, 15 s): vendor 19.90 fps, before 19.81,
after 19.83; 640x360 CBR 1000: 19.90 / 19.88.  The vendor two-channel test
process hung at teardown again (killed; device and streaming application
fine).

## Implementation

`avpu_t40_hevc_fill_cmd()` in `src/t40/codec-t40.c`; headers, EP1/EP2 and
knobs are the T31 ones (`OPENIMP_T40_HEVC_HWRC`, `_CABAC_INIT`, `_TMVP`)
plus `OPENIMP_T40_HEVC_TOOLS` (`avpu_t40_hevc_tools()`).
T40 publishes one pack per access unit (`h265NalType` IDR_W_RADL/TRAIL_R),
like T41.
