# OpenIMP on-device profiling

OpenIMP contains an opt-in, cross-SoC stage profiler for kernels which do not
provide a usable `perf` or ftrace interface. It is compiled into T31, T40, and
T41 builds but is inactive unless the consumer process has:

```sh
OPENIMP_PROFILE=1
```

`OPENIMP_PROFILE_INTERVAL` selects the cumulative report interval in completed
AVC frames. It defaults to 250 and has a minimum of 25. Reports go to syslog as
`openimp-profile` records. Each stage reports call count, total/average/maximum
wall time, and total/average/maximum thread CPU time. Counters separately
record dequeue and stream retries, cache-maintenance bytes, compacted bytes,
and published stream bytes.

The distinction between wall and CPU time is intentional. FrameSource dequeue,
AVPU completion, and stream publication all cross driver wait boundaries; wall
time alone incorrectly labels efficient blocking as computation.

## T41 QHD profile

The August 6, 2026 profile used the all-open TX-ISP/OpenIMP stack at
2560x1440, configured H.264 25/1, CBR, and no connected clients. The first
30-second run retained the old per-frame source-image diagnostic sampler. The
second run disabled that sampler while preserving the ISP-to-AVPU cache
ownership transition.

| Metric | Diagnostic sampling | Sampling disabled | Change |
| --- | ---: | ---: | ---: |
| delivered fps | 25.000 | 24.992 | within measurement noise |
| whole-system CPU capacity | 5.343% | 4.786% | -0.557 points (-10.4%) |
| pipeline-process CPU capacity | 4.125% | 3.597% | -0.528 points (-12.8%) |
| RVD CPU capacity | 1.882% | 1.386% | -0.496 points (-26.4%) |
| encode-submit CPU/frame | 795 us | 342 us | -57.0% |
| ISP overflow / kernel fatal / userspace fault | 0 / 0 / 0 | 0 / 0 / 0 | unchanged |

The removed default work sampled 4,096 sparse luma locations and 1,024 sparse
chroma pairs on every frame, then passed their means to a diagnostic-only
callback which discards them. Set `OPENIMP_SOURCE_STATS=1` to restore the
sampled diagnostic. `OPENIMP_T31_FULL_FRAME_STATS=1` still enables T31's
explicit full-frame diagnostic mode.

After the required reboot, a third run used the production configuration with
the profiler disabled. It delivered 25.016 fps, used 1.177% RVD CPU capacity
and 3.333% aggregate pipeline-process CPU capacity, and again produced zero
overflow, kernel-fatal, or userspace-fault events. The profiling calls are
therefore dormant in the normal process and do not erase the optimization.

The optimized profile also established these steady-state facts:

- FrameSource issued one successful blocking DQBUF per frame, with zero
  EAGAIN/ENODATA/EINTR retries. DQBUF averaged 33 us of thread CPU while its
  18.8 ms average wall time was driver wait time.
- GetStream had zero polling retries.
- The path performed nine cache-maintenance calls per completed frame. They
  averaged 25 us CPU each, including the full captured-source invalidation.
- T41 picture-state preparation averaged 62 us CPU, command construction
  25 us, the 4 KiB command-slot copy 4 us, and AVPU submit ioctls 66 us.
- Completion-status processing averaged 123 us CPU, stream finalization
  244 us, and the overlapping payload compaction only 16 us.

These numbers make the next work order explicit: reduce stream-finalization
and completion overhead before touching rate-control arithmetic or the small
command copy.

## Exact T41 stream extent

T41 completion already supplies a bounds-checked entropy payload byte count.
After moving that payload from the hardware's fixed `+0x220` boundary behind
the generated Annex-B prefix, the old finalizer nevertheless scanned every
byte of the completed access unit for start codes and trailing zeros. Besides
being redundant, that heuristic could remove a legitimate zero-valued final
entropy byte.

The finalizer now derives both pre- and post-compaction extents from one
tested layout helper and publishes the exact result. T31 and T40 retain their
generation-specific discovery paths. A matched 750-frame profiler pair on the
all-open QHD stack measured:

| Metric | Annex-B rescan | Exact T41 extent | Change |
| --- | ---: | ---: | ---: |
| delivered fps | 25.000 | 25.000 | unchanged |
| whole-system CPU capacity | 20.375% | 20.388% | unchanged |
| pipeline-process CPU capacity | 7.226% | 7.067% | -0.159 points (-2.2%) |
| stream-finalize CPU/frame | 277 us | 147 us | -46.9% |
| total IRQ-completion CPU/frame | 464 us | 330 us | -28.9% |
| stream compaction CPU/frame | 19 us | 18 us | unchanged |
| ISP overflow / kernel fatal / userspace fault | 0 / 0 / 0 | 0 / 0 / 0 | unchanged |

The high system and pipeline figures in this profiler pair include the
profiler and active background services; they are useful as a matched A/B,
not as a replacement for the profiler-disabled production baseline. With the
profiler disabled and the compact open ISP module loaded, a subsequent run
delivered 24.992 fps at 3.176% pipeline CPU with zero error deltas. A
ten-second High-profile 2560x1440/25 RTSP decode completed without warnings.

## Stable per-buffer completion descriptors

The direct AVPU path previously performed two avoidable heap-allocation
round trips per frame. `AL_Codec_Encode_Process()` allocated and zeroed a
descriptor which the hardware path never published, then completion allocated
a second descriptor which `ReleaseStream()` freed. The submit descriptor is
now allocated only for the legacy and software paths. Direct AVPU completion
uses one stable descriptor embedded beside each of its sixteen possible DMA
buffer slots; the existing buffer state machine prevents that descriptor from
being reused until `ReleaseStream()` returns the slot.

The isolated submit change reduced the 750-frame `encode_submit` CPU average
from 373 to 350 us/frame while preserving the nine cache operations and exact
25 fps delivery. The completed pool run removed the remaining IRQ-side
allocation/free and delivered 25.024 fps with 1.273% RVD CPU capacity, 3.573%
aggregate pipeline-process CPU capacity, and zero overflow, kernel-fatal, or
userspace-fault deltas. Both the library file size (316,760 bytes) and the
loaded open ISP module were unchanged. A 249-frame decoder probe reported
High-profile 2560x1440 video and no warnings.

This is primarily a lifecycle and ownership improvement: the second CPU delta
was below the noise floor, but stream descriptors can no longer fragment the
heap over long runs. Binding metadata to a fixed DMA slot also matches the
queue model expected by a future V4L2 mmap/DMABUF adapter.

## T41 command/status ownership

Per-site cache timing identified the T41 submit command ring as the largest
removable pair of cache operations. Publishing its 4 KiB slot cost 26 us/frame
and invalidating the hardware-written status cost another 37 us/frame because
the T41 rmem cache ABI normalizes both requests to 1 MiB. The remaining seven
transitions are distinct source, EP1/EP3, and output-stream ownership barriers.

T41 now maps only the hardware-visible command/status ring uncached, with a
safe fallback to the original cached mapping if `/dev/mem` is unavailable.
The host command mirror remains cached. A tested publish helper uses the
recovered OEM `PrepareCommand` range table to copy its seventeen non-empty
command ranges and entropy command, while clearing both reused completion
windows. This reduces the uncached transfer from the entire 4 KiB slot to
roughly 1 KiB and prevents stale completion status from surviving slot reuse.
`OPENIMP_T41_UNCACHED_COMMAND_RING=0` retains the old cached path for a
diagnostic A/B.

Matched 750-frame profiles on the same open QHD stack measured:

| Metric | Cached full-slot ring | Sparse uncached ring | Change |
| --- | ---: | ---: | ---: |
| cache calls | 6,756 | 5,257 | -1,499 (two/frame after init) |
| cache-accounted bytes | 10,455,498,752 | 8,883,683,328 | -15.0% |
| command copy CPU/frame | 4 us | 17 us | +13 us |
| encode-submit CPU/frame | 363 us | 334 us | -8.0% |
| IRQ-completion CPU/frame | 322 us | 307 us | -4.7% |
| submit + completion CPU/frame | 685 us | 641 us | -6.4% |
| completed frames | 750 | 750 | unchanged |

The follow-up no-client benchmark delivered 24.968 fps overall with zero ISP
overflow, kernel-fatal, or userspace-fault events. A repeated ten-second
High-profile 2560x1440 decoder run completed without warnings. The first RTSP
open observed the already-documented single repeated timestamp at a ring epoch
boundary; the immediate repeat was clean and no H.264 decode errors occurred.

## T41 EP3 scalar ownership

The T41 hardware-rate-control manager exchanges one four-byte level value with
the AVPU in each selected EP3 slot. The cached implementation nevertheless
published and invalidated the entire 16 KiB manager allocation; the kernel
normalized each request to 1 MiB. T41 now initializes and publishes the EP3
manager once, then uses a coherent uncached alias for the per-picture scalar
handoff. The cached fallback remains available when `/dev/mem` cannot be
mapped, and `OPENIMP_T41_UNCACHED_EP3_RING=0` explicitly selects it for an A/B.

Matched 750-frame profiles on the same open QHD stack measured:

| Metric | Cached EP3 ring | Coherent EP3 ring | Change |
| --- | ---: | ---: | ---: |
| cache calls | 5,257 | 3,757 | -1,500 (two/frame) |
| cache-accounted bytes | 8,883,683,328 | 7,310,819,328 | -1,572,864,000 |
| picture-state CPU/frame | 61 us | 39 us | -36.1% |
| completion-status CPU/frame | 118 us | 74 us | -37.3% |
| encode-submit CPU/frame | 334 us | 313 us | -6.3% |
| IRQ-completion CPU/frame | 307 us | 243 us | -20.8% |
| submit + completion CPU/frame | 641 us | 556 us | -13.3% |
| completed frames | 750 | 750 | unchanged |

The profiler-on correctness benchmark still delivered exactly 25.000 fps and
reported zero ISP-overflow, kernel-fatal, or userspace-fault deltas. A complete
Raptor stop/start exercised alias teardown and recreation successfully, then a
ten-second High-profile 2560x1440 stream decoded 251 frames without H.264
errors. The RTSP client did report repeated timestamps at the already-known
Raptor ring-epoch boundary; that transport timestamp issue is independent of
the EP3 memory ownership change.

## MXU assessment

The T41 reports MXUv3 in `/proc/cpuinfo`. The
[`ingenic-mxu`](https://github.com/gtxaspec/ingenic-mxu) MXU3 shim test ran on
the live camera with 449 passes, zero failures, and six expected SIGILLs for
NNA-only instructions. Its broad `mxu_probe` executable segfaulted on this
kernel, so OpenIMP must not use that probe unchanged as a production feature
test.

A temporary aligned-copy microbenchmark using the
[`thingino-accel`](https://github.com/opensensor/thingino-accel) four-VPR copy
primitive measured:

| Copy size | libc memcpy | MXUv3 | Speedup |
| --- | ---: | ---: | ---: |
| 4 KiB | 1,510 ns | 467 ns | 3.23x |
| 8 KiB | 3,081 ns | 941 ns | 3.27x |
| 16 KiB | 6,026 ns | 1,902 ns | 3.17x |
| 64 KiB | 39,191 ns | 23,179 ns | 1.69x |

The primitive is real and useful, but it is not currently a worthwhile H.264
hot-path dependency. The production command copy is only 4 us/frame and
payload compaction is 16 us/frame including profiler clock overhead. Even
eliminating both would be small beside completion and cache ownership, while
MXU3 requires 64-byte alignment and a scalar tail/overlap path. The sparse
diagnostic sampler was not vectorizable without changing its sampling model
because MXU3 has no gather operation.

MXU remains a strong candidate for future large contiguous operations such as
pixel-format conversion, scaling, planar/interleaved transforms, or an
unavoidable V4L2 copy. A reusable implementation should keep a scalar baseline
and choose once per process among MXU2 (T31/T32), MXU3 (T40/T41), and scalar
paths. It must also account for T40 CPU-affinity/kernel support and the source
projects' licenses before code is imported.
