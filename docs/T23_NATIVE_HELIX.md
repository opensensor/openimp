# T23 native Helix H.264

OpenIMP can encode T23 H.264 with its own Helix command lists over
`/dev/soc_vpu`, the way it already does on T30 and T21, instead of running
the OEM encoder in the `openimp-t23-helixd` worker. With the native backend
no OEM code runs for video.

Status: implemented and host-tested; **not yet run on hardware**. The build
default stays `worker` until the on-device bring-up below has passed.

## Selecting the backend

| Setting | Effect |
| --- | --- |
| `T23_DEFAULT_ENCODER=worker\|native ./build-t23.sh` | build default (default `worker`) |
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
- `src/t40/codec-t40.c`: backend selection, fallback and failure limit.
  Rate control (CBR/VBR/FixQP), GOP, IDR requests and the output path
  (`queue_encoded_stream`, P2 pack splitting) are shared with T30.

Reserved memory per channel: 16 KiB command list, 2 MiB EMC scratch,
1 MiB + 4 KiB bitstream window, two NV12 reconstruction planes. That is
about 9.3 MiB for 1920x1080 and 3.7 MiB for 640x360 (13 MiB for both).

## Host tests

`make -C tests/t23 check` builds and runs:

- `t23-helix-descriptor-test`: the T23 list has the OEM T23 register order
  (count and digest) for I and P at 1920x1080 and 640x360, every
  Helix-internal address is in 0x131xxxxx, nothing points into
  0x132xxxxx, the T23 constants are present, inputs pass through, invalid
  inputs are refused.
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
