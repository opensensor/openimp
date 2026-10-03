# T23 native Helix H.264

OpenIMP can encode T23 H.264 with its own Helix command lists over
`/dev/soc_vpu`, the way it already does on T30 and T21, instead of running
the OEM encoder in the `openimp-t23-helixd` worker. With the native backend
no OEM code runs for video.

Status: implemented, host-tested and brought up on the T23 test camera
(flashed since 2026-10-03 01:32). `native` is the build default; the
`worker` backend stays available as `T23_DEFAULT_ENCODER=worker` or
`OPENIMP_T23_ENCODER=worker`.

## Selecting the backend

| Setting | Effect |
| --- | --- |
| `T23_DEFAULT_ENCODER=worker\|native ./build-t23.sh` | build default (default `native`) |
| `OPENIMP_T23_ENCODER=native\|worker` | run-time choice, overrides the build default |
| `OPENIMP_T23_NATIVE_FALLBACK=0` | do not fall back to the worker when the native encoder cannot be created |
| `OPENIMP_T23_HELIX_TIMEOUT_MS=N` | soc_vpu wait/timeout per job, 100..20000 ms (default 2000) |

The backend is chosen per H.264 channel on its first frame. If the native
encoder cannot be created (no channel, no reserved memory) the channel
falls back to the worker; nothing has reached the VPU at that point. After
8 consecutive failed pictures a native channel stops submitting work and
produces no more H.264 output (there is no automatic switch to the worker
after the VPU has been used).

Still on the worker in both modes: the unbound `IMP_Encoder_Yuv*` API and
the hardware JPEG `IMP_Decoder`. JPEG snapshots, OSD, IVS and audio do not
use the Helix core.

## Feasibility: T23 versus T30/T21

| Item | T30 | T21 | T23 | Evidence |
| --- | --- | --- | --- | --- |
| Device node | `/dev/soc_vpu` | same | same (`CONFIG_SOC_VPU`, `CONFIG_VPU_HELIX`, one Helix) | T23 kernel `.config`, `drivers/video/soc_vpu` |
| Helix bus base | 0x13200000 | 0x13200000 | **0x13100000** (0x13200000 is the ISP-VPU direct-connect block, IVDC) | `soc-t23/include/soc/base.h`, `helix.h` `VPU_BASE` |
| IRQ | `IRQ_HELIX0` | same | same | `soc-t23/include/soc/irq.h` |
| Job start register | `REG_VDMA_TASKRG` 0x10008 | 0x00084 | 0x00084 (kernel side, no userspace change) | `helix.c vpu_start` |
| `channel_node` | 56 bytes | 56 bytes | **88 bytes** (+frame_type, overflow_cnt, ivdc_mem_line, data_threshold, max_bs_act, u64 time) | `channel_vpu.h`; OEM T23 uses `0xc0586300..02` |
| ioctls | REQ/REL/RUN `0xc03863xx` | same | **`0xc05863xx`** | OEM T23 `hwicodec_pf_h264e_t21_init/enc` disassembly |
| Core id | 0x02000001 | same | same (`VPU_HELIX_ID \| 1`) | `helix.c`, OEM enc |
| Encoder generation in OEM lib | `H264E_T30_SliceInit` | `H264E_T21_SliceInit` | **`H264E_T21_SliceInit`** + `hwicodec_pf_h264e_t21_*` | symbol tables |
| Command list | T30 list | T21 list (OpenIMP `src/t21`) | **T21 list** with: Helix addresses at 0x131xxxxx, no leading TCSM flush, +0x40170=0x330, +0x40174=0x80000330, +0x801e0/0x801e4/0x90028=0x00400000, 0x60004 bit 31 | emulation of both OEM builders on identical inputs, see below |
| Writes per picture | I 611 / P 785 | I 1011 / P 1030 | I 1015 / P 1034 | same |
| Bitstream syntax | High, CABAC, flat matrices | High, CABAC, 8x8 transform, fixed scaling matrices in PPS | as T21 | same command list as T21 |
| References | 2 recon planes | 2 recon planes + 2 MiB EMC scratch | same (OEM can also share one ref/recon buffer, `BUF_SHARE_CFG`; not used) | OEM `BUF_SHARE_CFG` only active with slice field +1116 |
| Completion | IRQ, `output_len` from `REG_SDE_CFG9`, status `SCH_STAT` | same | same, plus a **bitstream-full** interrupt (`SCH_INTE_BSF`) at the 1 MiB EMC window | `helix.c vpu_start`, `vpu_interrupt` |
| Timeout handling | kernel waits `mdelay`, then resets the core | same | same; the core is also reset before every job | `helix.c vpu_wait_complete`, `soc_vpu.c soc_channel_run` |
| Clocks | gated on open/release | same | enabled at probe (450 MHz), not gated on release | `helix.c` |

**Verdict: go.** The T23 Helix is the T21 generation at a different bus
address with a longer channel node. OpenIMP's T21 command-list builder
already reproduces the OEM T21 register order exactly (checked here by
emulation), and the OEM T23 builder differs from the OEM T21 builder only
by the constant deltas above, for every slice-structure field perturbed one
at a time (about 6,800 single-field variations, I and P, several picture
sizes). The only data-dependent T23 differences belong to features OpenIMP
does not use: the direct-connect (IVDC) mode (slice field +449 into
0x30000), shared reference buffers (+1116 into 0x60004 bit 30) and an
optional 0x3001c write (+856).

### How the OEM builders were compared

`tools/helix_oem_descriptor_compare.py` loads `H264E_T21_SliceInit` from a
vendor T21 1.0.33 and a vendor T23 1.3.0 `libimp.so` into the unicorn MIPS
emulator, runs both on the same slice structure and diffs the command
lists. Nothing from the vendor libraries is part of OpenIMP; the T23 test
keeps only the number of writes and an FNV-1a digest of the register order.

## Implementation

- `src/t21/t21_h264_descriptor.c`: Helix addresses derived from a base
  (`0x13100000` with `PLATFORM_T23`), plus the T23 deltas. The T21 and T20
  builds are byte-identical to before.
- `src/t30/t30_helix_encoder.c` (shared T30/T21/T20/T23 encoder): T23 uses
  the T21 syntax (SPS with 2 references and VUI timing, the fixed
  High-profile PPS, canonical slice_type), the 88-byte channel node and
  ioctls, a 2 s job timeout, and these T23-only safety checks:
  - a frame is only submitted if `DMA_VirtToPhys(virAddr) == phyAddr`
    (it lies in OpenIMP's reserved memory) and it holds a full
    macroblock-aligned NV12 picture; its cache is written back first
    (OSD is drawn by the CPU);
  - an IDR programs the idle reconstruction buffer as "reference" instead
    of physical address 0;
  - a job only counts if `SCH_STAT` has ENDFLAG and none of ACFGERR,
    BSERR, ORESERR, BSFULL, and `output_len` is within the 1 MiB window;
    otherwise the picture is dropped and the next one is an IDR;
  - the bitstream allocation has a page of slack past the 1 MiB window the
    command list declares;
  - access units are sized from the payload (not 2x the window);
  - CABAC initial states are computed per slice instead of keeping the
    208 KiB table resident (`src/t30/h264enc/cabac.c`, T23 only).
- `OpenIMP_T30_HelixReconfigure` (T23): rate-control mode, bitrate, frame
  rate, GOP and QP bounds set through the IMP API (timps) are applied
  between pictures from the codec's parameters; a bitrate-only change keeps
  the rate controller's scene model.
- Helix rate-control extras of `IMPEncoderAttrRcMode` (CreateChn and
  SetChnAttrRcMode hand them over with `HW_RC_FLAG_APP`; without them the
  encoder behaves exactly as before). Vendor-verified from the T23 1.3.0
  libimp (`IMP_Encoder_YuvInit`, `i264e_param_default`,
  `i264e_ratecontrol_init`): the accepted ranges (staticTime 1..60,
  changePos 50..100, qualityLvl 0..7, iBiasLvl -3..3, SMART -10..10, QP
  steps != 0), frmQPStep = max P-to-P QP delta, gopQPStep = max I-to-P QP
  delta, SMART configured like VBR, adaptiveMode/gopRelation not part of the
  rate control. The native use is inferred from the SDK header:
  I QP = P QP + iBiasLvl; frmQPStep/gopQPStep limit the QP change against
  the last P picture; VBR/SMART target changePos% of maxBitRate and raise QP
  above it, lower QP below maxBitRate * (80 - 10 * qualityLvl)% (SMART:
  (20 + 10 * qualityLvl)%, higher = better); staticTime sets how many GOPs
  the rate must stay outside that band before QP moves. Values out of range
  disable the feature. The log shows the rate control in effect
  (`T23 Helix rc ready:` / `rc reconfigured:`); `OPENIMP_T23_RC_STATS=<s>`
  adds bitrate and I/P QP statistics every `<s>` seconds.
- `src/t40/codec-t40.c`: backend selection, fallback and failure limit.
  Rate control (CBR/VBR/FixQP), GOP, IDR requests and the output path
  (`queue_encoded_stream`, P2 pack splitting) are shared with T30.

Reserved memory per channel (all from the **top** of the reserved arena,
`DMA_AllocDescriptorTop`, so the FrameSource pools that timps frees and
re-creates whenever a channel idles keep their holes at the bottom):

| Buffer | 1920x1080 | 640x360 |
| --- | --- | --- |
| reconstruction/reference, 2 NV12 planes | 6120 KiB | 690 KiB |
| EMC per-macroblock scratch (24/64/16/64/88 B per MB, 4 KiB pages) | 2048 KiB | 260 KiB |
| bitstream window (one raw picture, 256 KiB..1 MiB) + 4 KiB | 1028 KiB | 388 KiB |
| command list | 16 KiB | 16 KiB |
| total | 9212 KiB | 1360 KiB |

The 1080p EMC layout is the one captured from the OEM T21 encoder (offsets
0x30000/0xb0000/0xd0000/0x150000 in 2 MiB), which is exactly 24, 64, 16 and
64 bytes per macroblock rounded to pages; smaller pictures scale per
macroblock (plus a spare page per buffer). The OEM sets the window size
(0x30040, KiB) per channel; OpenIMP uses one raw picture so even an
all-I_PCM picture fits.

The first device run (2 MiB EMC and 1 MiB window per channel, buffers
best-fit from the bottom) needed 13 MiB and broke the main pool's re-creation
after an idle cycle ("largest free block 5554176"); the layout test below
reproduces exactly that failure and shows the new layout surviving.

### OEM reference sharing (not used)

The OEM T23 encoder can keep reference and reconstruction in one ring per
channel (`BUF_SHARE_CFG`; only when the application enables buffer sharing
for the channel and per frame). Emulating the OEM builder shows how it works:
the ring holds the picture plus 256 lines (`(1117+1)*64`, the encoder sets
3), register 0x60004 gets bit 30, 0x60014/0x60018 are the ring start and
0x6001c/0x60020 the ring end (luma/chroma), and the reconstruction pointer
0x60008/0x6000c moves 256 lines down per picture modulo the ring while the
reference (0x5006c/0x50070, 0xb0014/0xb0018) is the previous picture's
position. For 1080p that is 3.7 MiB instead of 6.0 MiB. Implemented as
opt-in `OPENIMP_REF_SHARE=1` (`src/t21/t21_ref_ring.h`, same arithmetic
on T21 and T23).

One register more than the addresses: the 0xb reference reader (0xb0014/18
reference, 0xb0030/34 ring start, no ring end) gets the wrap row in bits
8..15 of 0xb0000, `((end_y - ref_y) / stride >> 4) - 1` (macroblock rows
from the reference position to the ring end minus one; 255 for the first
P after the IDR, whose reference is at the ring start; 0xff is also the
non-shared value, "never"). The first device run (T21 1080p, without it)
showed exactly the signature of a reference read past the ring end: P
pictures of 17/15/10/2 KiB in a static scene every 5.25 pictures (the ring
period, 84 rows / 16 rows per picture), shrinking with the number of
reference rows past the end, and chroma smears in the decoder. The OEM's
0x10014/0x10018 keep the IMP-layer reference pointer (`ctx[632/636]`),
not the ring position; OpenIMP writes the ring reference there.
`OPENIMP_REF_SHARE_DEBUG=1` logs n, recon, reference and wrap per
picture and the ring registers of the first eight command lists.

Vendor T23 (libimp 1.3.0) live registers (devmem, 1080p; the vendor uses
the ring unconditionally up to 1920x1088, `IMP_Encoder_SetRdBufShare` is
a no-op): 0x60004 = 0xc10e0780, luma ring 0x02ff6000..0x0326c000
(1920 x 1344), chroma ring 0x0326c100..0x033a7100 (1920 x 672, 0x100
after the luma end), raw 0xb0008/0c constant and outside the ring (the
source is not in the ring), recon only at multiples of 64 lines (the 21
positions of the 256-line step in a 1344-line ring), 0xb0000 =
0x0002xx62 at rest with the wrap byte xx (all 3 mod 4), 0x50000 bit 6 and
0x80030 bit 14 clear, 0x50000 = 0x544xd9b0 (bits 9/10 clear, bits 16..23
per picture), 0x50040/48/4c = 0x871f5008 / 0x02000200 / 0x08080303.
Per-picture command lists of the vendor (LD_PRELOAD dump of the RUN
ioctl, `scratchpad/hxdump`) settled the rest: the ring addresses and the
0xb0000 wrap byte are as computed, and the same wrap row goes into bits
14..21 of the MCE control word 0x50000 (0xff = never; this was the
missing piece: the ME read the reference past the ring end, hence P
pictures of hundreds of KiB with the 5.25-picture period); 0x10014/18
are 0 for the IDR and the ring start for P; the 0xb0000 low byte is 0x21
for the IDR and 0x63 for P.  With that the T23 ring encodes cleanly
(cam-B: no timeouts, no decode errors, no artifacts).  OpenIMP's ring
mode uses the 0x100 chroma gap, these bytes (T21: 0xbd/0xff), the wrap
row in both registers, and on T23 the vendor MCE words by default
(`OPENIMP_REF_SHARE_FLAGS`: 10 vendor ME words 0x50040/48/4c and 0x50000
bits 9/10, 20 vendor 0x4010c/0x801c0, 8 no wrap; `OPENIMP_REF_SHARE_B0`
overrides the P low byte).  The vendor also writes a few registers per
picture through ioctl 0xc0586307 (0x131500e8..f0 among them), values not
captured yet.

## Status (WIP)

Done:
- native encoder, device-tested by the coordinator: self-test stages
  (360p and 1080p) decode; timps with both channels native encodes
  (status 0x301), but the first layout ran rmem out on chn0's idle cycle;
- per-picture EMC scratch and bitstream window, encoder buffers from the
  top of the arena (`DMA_AllocDescriptorTop`, T23 only), layout simulation
  test.

- memory layout soak-tested on the device (2 h 34 min, both channels, no
  reconnects, MemFree stable); 218 pictures (0.09 %) were dropped for
  status 0x100, now accepted (see "Job status" below).

Next:
- device retest of the status handling (plan below, "Status retest");
- optional: OEM reference sharing (above) if more headroom is needed.

Current builds (`T23_DEFAULT_ENCODER=native` default):

| Build | md5 |
| --- | --- |
| T23 libimp.so (default worker) | ba44331b46e57cdbfd7fb9aa493e8700 |
| T23 libimp.so (`T23_DEFAULT_ENCODER=native`) | 32b2c141e629ada27084140e20fc60a9 |
| T23 openimp-t23-helix-selftest | a456fd79db9ea5bb4d202bb7f615c629 |
| T23 openimp-t23-helixd (unchanged) | 2173cdb30c4dd9e24229ac4db80111f3 |
| T30 / T31 / T20 / T21 libimp.so (unchanged) | f953cffc… / 9606fc65… / f787b889… / 6a045ced… |

## Job status

soc_vpu returns the Helix `SCH_STAT` word of the job's interrupt. A
completed job reads 0x301: ENDFLAG (bit 0) plus bits 8 and 9, which
`helix.h` does not name and which behave as per-unit done flags. The
kernel's interrupt handler (`helix.c vpu_interrupt`) clears the SDE and
deblocker done flags on ENDFLAG but not `SCH_STAT` itself, and masks the
interrupt enables. If the handler runs a second time for the same job before
the waiting thread wakes, `SCH_STAT` has lost ENDFLAG and bit 9 (residue
0x100), so the handler takes its error branch: it stores status 0x100 and
zeroes the length, but does not signal completion. `vpu_wait_complete` was
already woken by the first (ENDFLAG) invocation, sees the zero length and
re-reads it from `REG_SDE_CFG9`. So ioctl success + status 0x100 + a length
is a finished picture with its real length. The OEM T23 encoder
(`hwicodec_pf_h264e_t21_enc`) never looks at the status word: it takes
`output_len` and `cmpx` from the channel node and only treats a negative
ioctl result or "cancelled" (2, direct mode) as failure, and it neither
retries nor waits longer.

OpenIMP therefore accepts status 0x100 without error bits as success
(logged as "completed with late interrupt status", first 3 and every
100th). Any other unexpected result without error bits is re-run once with
the identical command list (deterministic, the reference is untouched);
timeouts, error bits and bitstream-full still drop the picture and restart
the GOP. `OPENIMP_T23_HELIX_STRICT_STATUS=1` restores the strict check
(with the single retry) for comparison. Failure and retry lines now name
the channel size, picture type and QP.

The lengths in the soak log (60, 192, 403 bytes) are those of P pictures
of a nearly static scene, not IDRs (tens of kB); the frame counter is per
channel, so it does not tell main from sub.

### Status retest

1. timps as before; count `late interrupt status` lines (expect about
   0.09 % of pictures) and `retrying the job` / `run failed` lines
   (expect none).
2. Record both RTSP streams for 30 minutes (`ffmpeg -i rtsp://... -c copy
   main.h264`) and check them with `ffmpeg -v error -i main.h264 -f null -`:
   no errors, and IDRs only at GOP boundaries or on request.
3. For an A/B check, run once with `OPENIMP_T23_HELIX_STRICT_STATUS=1`:
   the old drops come back as `run failed ... status=0x00000100`.

## Host tests

`make -C tests/t23 check` builds and runs:

- `t23-helix-descriptor-test`: the T23 list has the OEM T23 register order
  (count and digest) for I and P at 1920x1080 and 640x360, every
  Helix-internal address is in 0x131xxxxx, nothing points into
  0x132xxxxx, the T23 constants are present, inputs pass through, invalid
  inputs are refused.
- `t23-rmem-layout-test`: OpenIMP's arena allocator with the real encoder
  buffer sizes through timps' sequence (start, chn0 idle, sub channel
  starting while chn0 is idle, re-enable; then 20000 random idle/re-enable,
  snapshot and encoder-restart steps). Every pool re-creation must succeed;
  it prints the least free space (1556 KiB with a 256 KiB snapshot buffer
  live). The previous layout must fail with the device's numbers.
- `t23-helix-stream-test`: the whole native encoder against a fake
  `/dev/soc_vpu` that validates every job (ioctl numbers, node, core id,
  terminated list in reserved memory, DMA addresses inside allocations) and
  answers with real CABAC data - all-I_PCM IDRs and all-skip P pictures -
  derived from the QP and slice type in the command list. ffmpeg decodes the
  640x360 stream with `-err_detect explode` and every picture must equal the
  input (lossless). It also covers GOP, IDR request, a bitstream-full job,
  a timed-out job, a bitrate change, and frames that must never reach the
  VPU.

## On-device bring-up

Do not flash; copy files to the camera (`/tmp` is RAM) and keep the stock
setup on flash. Use a serial console if at all possible.

### 0. Capture kernel messages before starting

A silent reset loses `/tmp`, so get kmsg off the camera while testing:

- serial console, or
- `cat /proc/kmsg | nc <pc> 5555 &` on the camera with `nc -l -k 5555 >
  t23-kmsg.log` on the PC (start it first), or netconsole if the kernel has
  it.

The self-test writes every step to `/dev/kmsg` (`t23-helix-selftest: ...`),
so the last line received shows what the core was doing. Note the reset
reason after a reboot (`dmesg | head`, watchdog messages).

### 1. Self-test with the streamer stopped

    # stop timps/RVD and anything else using libimp or /dev/rmem
    /etc/init.d/S*timps stop      # or: killall timps; check with ps
    fuser /dev/rmem /dev/soc_vpu 2>/dev/null   # must print nothing
    cat /proc/cmdline             # rmem=22M@0x2a00000 expected

Copy `build/t23/openimp-t23-helix-selftest` to `/tmp` and run, in order,
stopping at the first failure:

    /tmp/openimp-t23-helix-selftest -c
        # channel request/release and buffer allocation only, no VPU job
    /tmp/openimp-t23-helix-selftest -w 640 -h 360 -n 1
        # one sub-stream IDR
    /tmp/openimp-t23-helix-selftest -w 640 -h 360 -n 10 -g 5
        # IDR + P pictures
    /tmp/openimp-t23-helix-selftest -w 1920 -h 1080 -n 10 -g 5 -m
        # main-stream size, moving square

Each run prints per-picture sizes and writes
`/tmp/t23_helix_selftest.h264`. Expected: `status=0x...` with bit 0 set
in the log line of each frame (`OPENIMP_DEBUG_TRACE=1` adds more), IDRs of
tens of kB, P pictures of a few hundred bytes for the static picture.
Copy the file to the PC and check:

    ffmpeg -v error -err_detect explode -i t23_helix_selftest.h264 -f null -
    ffplay t23_helix_selftest.h264      # colour bars, square, gray ramp

If the kernel logs `wait_for_completion timeout`, `vpu_stat = ...` or the
self-test reports failures, stop and keep the kmsg log: the job timed out
after 2 s and soc_vpu reset the core. `-t 500` shortens the wait.

### 2. timps with the native backend

    OPENIMP_T23_ENCODER=native OPENIMP_T23_NATIVE_FALLBACK=0 timps ...

(add the variable to the timps init script environment, or run timps by
hand in the foreground). Check the log for `T23 H.264 backend: native
Helix`, one `T23 Helix: native encoder ready` per H.264 channel with the
expected size, bitrate, fps and GOP, and the first frames' `status`. Then:

- RTSP main and sub stream play; IDR cadence equals the GOP; change bitrate
  and GOP from the timps configuration and look for `reconfigured`;
- request an IDR (new RTSP client) and see it arrive at once;
- OSD (time stamp) visible; JPEG snapshot, motion detection and audio
  unchanged;
- `free`, `cat /proc/meminfo` and the rmem high-water mark
  (`OPENIMP_DEBUG_TRACE=1`) for memory headroom;
- leave it running for an hour, watch kmsg for `vpu`/`helix` messages.

Afterwards drop `OPENIMP_T23_NATIVE_FALLBACK=0` (fallback to the worker on
create failure) and, once proven, build with `T23_DEFAULT_ENCODER=native`.

### Memory retest (after the first device run)

1. timps with `OPENIMP_T23_ENCODER=native OPENIMP_T23_NATIVE_FALLBACK=0`
   and `OPENIMP_DEBUG_TRACE=1` once (DMA allocation lines). Check both
   "native encoder ready" lines: `emc=.../2048K bs=.../1024K` for 1080p and
   `emc=.../260K bs=.../384K` for 360p, addresses near the top of rmem
   (main below 0x04000000, sub just below the main buffers).
2. Let timps idle chn0 and chn1 (no clients) and bring them back several
   times (open/close RTSP on each channel, take snapshots in between). No
   `rmem out of memory`, no `EnableChn failed`.
3. While both channels stream: the trace's `used=` for rmem should be about
   21 MiB of 22 MiB (1.5 MiB free); note the value.
4. Run for an hour with clients coming and going (Frigate on the sub
   stream, an RTSP player on the main), watch kmsg for vpu/helix messages
   and timps for encoder failures.

### Hang precautions

- The first VPU job happens only in step 1, with nothing else using the
  reserved memory, so a wrong command list cannot hit live ISP buffers.
- Each job is bounded: soc_vpu waits at most `mdelay` (2 s here, OEM 20 s)
  and resets the Helix core on timeout and before every job.
- If the camera resets anyway, the hardware watchdog is the likely
  trigger; the kmsg capture shows the last submitted frame. Power-cycle,
  then retry with `-n 1` and `OPENIMP_DEBUG_TRACE=1`.
- Never run the self-test while timps is running: both would allocate the
  same reserved memory.
