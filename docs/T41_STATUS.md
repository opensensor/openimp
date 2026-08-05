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
- Both 1920x1080 and 640x360 channels complete continuously through real AVPU
  IRQs. The first word of the command slot's `+0x5c0` status block is treated
  as the authoritative entropy payload size; it matches the DMA extent after
  T41's `+0x220` boundary and is validated before an access unit is published.
- Decoder probes identify valid High-profile H.264 on both channels. Captured
  boundary access units contain valid SPS/PPS/IDR NAL units and decode at the
  configured geometry. Raptor receives the compacted output through its
  FrameSource virtual alias, so both RTSP endpoints publish the open encoder's
  output without an OEM `libimp.so` dependency.

## Remaining work

OpenIMP's T41 ISP tuning is not yet at OEM image-quality parity; current output
is structurally correct but visibly overexposed and color-biased. Longer RTSP
captures can also skip dependent P pictures when the current consumer/ring path
falls behind. Throughput work and V4L2 support remain intentionally deferred
until the cross-SoC correctness and image-quality work is complete.
