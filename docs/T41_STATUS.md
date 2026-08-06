# T41 status

The T41 target is an in-progress port of the shared T4 OpenIMP implementation.
It is intentionally kept in the same encoder and FrameSource sources as T40;
platform branches represent measured public or kernel ABI differences.

## Verified on Wyze v4 / T41NQ

- `build-t41.sh` builds an OEM-independent `libimp.so` and resolves all 270
  IMP imports used by the target RVD and RAD binaries.
- The measured T41 public layouts are represented in the OpenIMP headers,
  including FrameSource, frame metadata, encoder channel, rate-control, and
  GOP structures.
- Both configured FrameSource channels enable against the stock T41 ISP. A
  real 1920x1080 NV12 frame reaches the shared encoder with valid physical and
  virtual addresses.
- T41 tuning uses request `0xc0105435` and the measured T41 control IDs.
- T41's rmem cache ioctl requires an OEM-sized minimum 1 MiB flush. Smaller
  `0x85c0`, `0x9000`, and `0x10000` probes faulted the caller; 1 MiB completed.
- The native AVPU register sequence is recovered from the stock T41 library
  and confirmed live: IRQ mask `0x5`, clock command `0x1`, 4 KiB command slots,
  status pointer at command `+0x5c0`, and a core-state transition from
  `0x80000000` to `0x80000003` after push. That transition is not treated as
  completion without an IRQ and valid status writeback.
- OEM `PrepareCommand` starts with a 4 KiB template and copies 17 non-empty
  command ranges through word 237 (`0x3b8` bytes), plus an optional 60-byte
  entropy block at `+0x800`. The recovered range table and bounds helpers live
  in `src/t40/t41_command_layout.c` and have a host regression test.
- `src/t40/t41_command_builder.c` now builds the complete observed command
  image without an OEM blob. Geometry, LCU history, source/reconstruction
  pitch, the `0x220` payload boundary, entropy words, and hardware-rate-control
  fields are derived from channel state. Physical buffer ownership and the two
  rotating per-picture offsets are explicit adapter inputs. Exact full-slot
  host oracles cover the captured 1920x1080 IDR and 640x360 first P picture;
  the builder also reproduces T41's padded FBC sizes (`0x214800/0x10a400` and
  `0x43800/0x21c00`) from geometry.
- The shared encoder now submits that dedicated T41 command image with owned
  EP1/EP2/EP3 storage and one geometry-sized reconstruction manager. Its two
  embedded map and motion-vector slots alternate per picture while the
  reference base stays stable, matching the measured T41 ownership model.
- The 2560x1440 full-resolution main channel, 1920x1080 main channel, and
  640x360 subchannel complete continuously through real AVPU IRQs. The first
  word of the command slot's `+0x5c0` status block is treated
  as the authoritative entropy payload size; it matches the DMA extent after
  T41's `+0x220` boundary and is validated before an access unit is published.
- The exact OEM `EncodingStatusRegsToSliceStatus` mapping is recovered as a
  bounded, host-tested command-slot decoder. It preserves the entropy-owned
  portion of the 0x70-byte slice status while copying the T41 counter, packed
  QP, and overflow fields. The complementary entropy-status mapping and the
  subsequent 0x28-byte `AL_RateCtrl_ExtractStatistics` projection are also
  recovered and tested. A bounded composition now initializes the slice
  status and combines both hardware blocks before projecting all statistics;
  the normalized block and feedback counters now drive the recovered software
  rate controller on every completed access unit.
- Decoder probes identify valid High-profile H.264 on both channels. Captured
  boundary access units contain valid SPS/PPS/IDR NAL units and decode at the
  configured geometry. Submit-time IDR state is carried with each hardware
  stream buffer, so the public encoder reports the completed picture type
  without rescanning a second cached alias of the encoded bytes.
- T41 copies the FrameSource timestamp and public rmem-alias delta into
  per-stream-buffer metadata before returning the capture descriptor. AVPU
  completion therefore does not dereference a FrameSource descriptor whose
  ownership has already returned to the driver.
- The correctness-first Raptor integration uses
  `OPENIMP_T41_STREAM_COPY_MODE=1` with `ring.refmode=false`. OpenIMP then
  returns the allocation alias containing the authoritative compacted access
  unit and Raptor copies it into its embedded shared-memory ring. Both RTSP
  endpoints start reliably in this mode, and repeated decoded samples are
  free of the macroblock corruption seen through the cross-process rmem alias.
- OpenIMP and the open T41 TX-ISP driver run together on the Wyze v4. A current
  1920x1080 sample has balanced global luma/chroma statistics and no visible
  block corruption under mixed daylight and warm interior lighting.
- The Wyze v4 full-resolution profile now runs the same all-open stack at
  2560x1440. A 100-frame High-profile H.264 sample reported 2560x1440 at
  25/1 fps, decoded without errors, and contained 4,237,360 bytes after the
  completion controller settled against a 4,000,000-byte CBR budget. Its live
  software-rate-control block total is 57,600, matching the coded 320x180
  8x8-block grid. A second userspace stop/start without unloading ISP modules
  produced another decoder-clean 50-frame sample, and a subsequent device
  reboot returned the same open stack and full-resolution stream.

## H.264 quality state

The previously incomplete completion path is now coupled end to end:

- T41's exact combined encoding/entropy status decoder feeds the recovered
  hardware-counter normalizer and the exact 0x40-byte bitrate-history update.
  Captured OEM fixtures and all 40 recorded history transitions remain host
  regression oracles.
- The active T41 CBR path through OEM's 4,084-byte `o1II` model selector and
  its 4,384-byte picture-model updater is recovered for IDR and normal P
  pictures. It includes fixed-point scale learning, the three prediction
  classes, feedback-model bound adjustment, GOP allocation compensation,
  bitrate-history correction, cadence, and both hysteresis latches. A
  constructor-initialized OpenIMP controller reproduces a 40-completion OEM
  trace after every call; the regression hashes all 16 history words and all
  recovered semantic model state, rather than checking QP alone. A separate
  deterministic matrix exercises 1-12 Mbit/s, integer and fractional frame
  rates, GOP lengths 25-60, and several QP ranges under ASAN and UBSAN.
- The exact controller is gated to OEM rate-control mode 1 (CBR). FIXQP and
  VBR do not enter it, and changing away from CBR invalidates its history so a
  later CBR transition reconstructs the recovered constructor state.
- The selected QP is applied consistently to the generated slice header,
  command word 24, its entropy mirror, and both QP fields in word 179. This
  removes the former syntax/hardware split that produced CABAC corruption when
  command QP was changed in isolation.
- `AL_GetLambda` was recovered from the OEM binary. T41's two 16-bit EP1
  lambda lanes now select AVC component 2 for IDR/I pictures and component 1
  for P pictures while retaining component 3. The first IDR-to-P transition
  changes exactly the 26 words measured in the OEM trace.
- The full three-slot EP3 manager lifecycle is active. The completed slot is
  invalidated, its hardware-written level at `+0x1400` is retained in manager
  state, and that value is written and flushed into the next IDR/P slot. The
  hardware-owned 36-word history at `+0x1360` remains in place. T41's required
  1 MiB cache-operation normalization is used for both EP1 and EP3.

On the live 8-Mbit/s, 2560x1440/25 stream, the exact controller settles around
QP 38-39. A 120-frame sample was 4,877,928 bytes against a 4,800,000-byte
nominal budget and decoded with an empty error log. All completions returned
success. The required cleanup reboot then loaded the same open library and
open TX-ISP modules; a 60-frame full-resolution sample decoded cleanly with no
ISP overflow or controller errors.

Set `OPENIMP_T41_RATE_CONTROL_COUPLING=0` only for diagnostic A/B rollback to
the fixed command-QP path. Coupling is enabled by default.

## QHD cadence baseline

The on-device benchmark now measures the configured 2560x1440, 25/1 H.264
stream at its shared-memory publication boundary. With the same Raptor CBR
profile and no connected clients, the August 6, 2026 isolation runs measured:

| ISP | IMP | Delivered fps | System CPU | Pipeline CPU |
| --- | --- | ---: | ---: | ---: |
| stock | OEM | 25.008 | 11.110% | 3.587% |
| stock | OpenIMP before fix | 16.266 | 33.225% | 27.973% |
| stock | OpenIMP current | 25.000 | 12.868% | 3.738% |
| open | OpenIMP current | 24.984 | 17.900% | 8.497% |

T41 completion already provides the exact entropy payload size at command
slot `+0x5c0`. The old path nevertheless scanned and cleared the entire
multi-megabyte stream buffer, invalidated the host-only command mirror, and
repeated command-ring cache operations when packaging every access unit.
The current path trusts the validated completion length, clears only the
header/payload prefix before submission, flushes only the hardware-visible
command ring, and does not clear bytes beyond the published access unit.
T31 and T40 retain their existing status discovery and cache behavior.

A separate RTSP decoder probe on the all-open stack reported H.264,
2560x1440, and 25/1 fps and decoded for ten seconds without warnings.

## Stage profile and first optimization

The vendor 4.4 kernel exposes neither a usable perf-event interface nor
ftrace, so the shared OpenIMP backend now contains an environment-gated stage
profiler. A controlled pair of all-open, no-client QHD runs found that the
FrameSource DQBUF blocks efficiently with zero retries and that the small
rate-control and command-copy operations are not the main CPU cost.

The dominant avoidable submit work was a diagnostic-only scalar traversal of
4,096 sparse luma samples and 1,024 sparse chroma pairs on every frame. Its
consumer intentionally discards the resulting means. Making that traversal
opt-in reduced RVD CPU capacity from 1.882% to 1.386%, pipeline-process CPU
from 4.125% to 3.597%, and whole-system CPU from 5.343% to 4.786%, while
delivery remained 25 fps and all overflow/fatal/fault deltas remained zero.
The required captured-source cache invalidation is unchanged.

The post-reboot production run had profiling disabled and delivered 25.016
fps with 1.177% RVD CPU capacity, 3.333% aggregate pipeline-process CPU
capacity, and zero overflow/fatal/fault deltas.

The next pass removed the redundant Annex-B rescan from T41's exact
completion-length path. In a matched 750-frame profile, stream-finalization
CPU fell from 277 to 147 us/frame and total IRQ-completion CPU fell from 464
to 330 us/frame, with both runs delivering exactly 25 fps and reporting zero
error deltas. The exact extent is validated by a host boundary suite; T31 and
T40 retain their existing fallback sizing behavior. A later profiler-disabled
run with the compact open ISP module delivered 24.992 fps at 3.176% aggregate
pipeline CPU and decoded ten seconds of High-profile 2560x1440/25 video
without warnings.

The detailed stage data and T41 MXUv3 measurements are in
[`PROFILING.md`](PROFILING.md).

## Remaining work

The zero-copy Raptor path is not yet correctness-safe. Raptor's reference-mode
consumer maps `/dev/rmem` through a different cached virtual alias from the one
OpenIMP uses to compact the completed access unit. Invalidating that foreign
alias is unsafe, and publishing it without a coherent ownership transition can
produce stale blocks. Keep embedded copy mode enabled until that cache contract
is solved explicitly.

Open TX-ISP exposure and color are now close to the measured OEM baseline, but
scene-by-scene image-quality parity is still being tuned. The native T41 AVPU
path now averages the configured 25 fps in decoded captures, though an RSD
ring reopen can still reset an active client's RTP epoch. One staged library
restart captured a short ISP-overflow burst, but an immediate controlled
stop/start plus decoded capture did not reproduce it; clean boots and
steady-state captures are also clean. OEM selector branches for configurations
other than the captured normal-P CBR profile remain future correctness work.
Throughput work, zero-copy
optimization, and V4L2 support remain intentionally deferred until cross-SoC
correctness and image-quality work are complete.
