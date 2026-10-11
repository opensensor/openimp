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
teardown after this run (killed; device and timps fine), so the
throughput gap is not profiled yet.

Open points:
- FixQP 4K +33 % bit rate at equal SSIM (IDR smaller, P larger than the
  vendor): the FixQP lambda table of H.264 (`avpu_t40_fixqp_lambdas`) is
  not applied to HEVC yet.
- 4K throughput: 3 % (one channel) and about 10 % (two channels) fewer
  pictures than the vendor with identical command words; the H.264 path
  reaches 19.9 fps at 4K.  Candidates: CPU work per picture (AU check /
  EBSP scan of the payload), missing overlap of the next submit with the
  previous completion.  Needs per-picture timing on the device.
- Slice header POC counts 1 per picture, the vendor 2 (cmd[0x0c..0x11]
  already use 2n); harmless for one reference.

## Implementation

`avpu_t40_hevc_fill_cmd()` in `src/t40/codec-t40.c`; headers, EP1/EP2 and
knobs are the T31 ones (`OPENIMP_T40_HEVC_HWRC`, `_CABAC_INIT`, `_TMVP`)
plus `OPENIMP_T40_HEVC_TOOLS` (`avpu_t40_hevc_tools()`).
T40 publishes one pack per access unit (`h265NalType` IDR_W_RADL/TRAIL_R),
like T41.
