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
- The exact OEM `EncodingStatusRegsToSliceStatus` mapping is recovered as a
  bounded, host-tested command-slot decoder. It preserves the entropy-owned
  portion of the 0x70-byte slice status while copying the T41 counter, packed
  QP, and overflow fields. The complementary entropy-status mapping and the
  subsequent 0x28-byte `AL_RateCtrl_ExtractStatistics` projection are also
  recovered and tested. A bounded composition now initializes the slice
  status and combines both hardware blocks before projecting all statistics;
  these inputs are observed live but do not yet drive rate control.
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

## H.264 quality state

The decoder-clean fallback currently keeps the P-picture command and entropy
QP at 34 while forwarding the recovered rate-control QP through command word
179.  This produces more coded high-frequency noise and a higher bitrate than
OEM, but changing command word 24 independently is not safe yet.

A same-boot OEM/Open `/dev/rmem` trace narrowed the missing coupling:

- The first IDR EP1 image is byte-identical.  On the first P picture OEM
  changes exactly 26 lambda words from the intra lane to the inter lane;
  OpenIMP currently retains the intra form.
- After physical addresses are excluded, the first main-channel P command
  differs only at word 24 and its entropy mirror at word 513.  OEM uses its
  recovered rate-control QP there.
- OEM's P-picture EP3 state already has a nonzero word at offset `0x1400` and
  gains additional hardware-written state at `0x1360` on later pictures.
  OpenIMP's cached EP3 snapshots leave that whole tail zero.
- OEM slice headers still advertise QP 34 (`slice_qp_delta = 8`), so changing
  the generated slice header to follow command word 24 is not the missing
  operation.

Applying the measured P-picture EP1 transition by itself remained
decoder-clean, but raised a matched 100-frame capture from 1,634,021 to
1,799,978 bytes and increased mean luma temporal difference from 0.886 to
1.001.  Pairing it with either dynamic command QP or a constant P QP of 40
caused CABAC decode errors.  Those probes were reverted.  Keep the known-clean
QP-34/IDR-lambda fallback until the EP3 initialization, writeback, and cache
ownership transition is recovered as one unit.

The exact T41 OEM binary now identifies the EP3 buffer-manager transition:

- `AL_GetAllocSizeEP3PerCore` returns `0x1420`; `PreprocessHwRateCtrl` clears
  the final `0x20` bytes beginning at `0x1400` and initializes each per-core
  state in `0x1420`-byte strides. The allocator-facing buffer size is rounded
  to 128 bytes, while the three captured picture-class buffers are placed on
  `0x1500`-byte boundaries.
- `AL_HwRC_UpdateLevel` invalidates four bytes at the completed EP3 buffer's
  offset `0x1400`, reads the word, and stores it at HWRC-manager offset `0x18`.
- `AL_HwRC_SetBuffer` resolves the next EP3 buffer's physical and virtual
  addresses, copies manager offset `0x18` to that buffer's offset `0x1400`,
  flushes those four bytes, and returns both addresses to the command builder.
- The `0x1420` per-core size, `0x1500` slot stride, `0x1360` history window,
  and `0x1400` level handoff now live in a bounded, host-tested T41 component.
- The recovered three-slot EP3 initializer now has a bounded, directly
  host-tested T41 implementation behind its dedicated `0x3f00`-byte
  allocation. Its static table image and per-picture bitrate targets match
  the captured OEM IDR/P oracles; dynamic QP remains disabled while the full
  feedback transition is validated.

A live OpenIMP probe reproduced that scalar handoff and confirmed that T41
hardware updates the field. The scalar handoff alone caused unstable payload
sizes. Enabling it together with the measured P-picture command QP, entropy
mirror, and EP1 lane still produced top-row/CABAC errors in a 100-frame sample
(1,788,954 bytes; nonempty decoder error log). All runtime changes were
reverted, and the restored 100-frame fallback sample is decoder-clean
(1,664,541 bytes; empty decoder error log). This rules out the `0x1400` word
as the only missing state. The next port must include the OEM software
rate-controller/statistics update and the full EP3 manager lifecycle; the
current payload-only `openimp_t41_next_rate_control_qp` approximation is not a
safe source for command word 24.

The exact combined encoding/entropy-status decoder is now also active as a
diagnostic while the fallback remains in control. On a decoder-clean
100-frame open-stack capture,
the packed hardware fields were consistently `34/38-39/0` for the main channel
and `34/37/0` for the subchannel, with the recovered overflow flag clear. This
confirms that the hardware supplies channel-specific QP feedback that the
payload-only approximation discards; the fields are logged but not yet fed
back into the next command. The recovered entropy byte count also matched the
authoritative completed payload on both channels, its auxiliary counter was
nonzero, and all three status flags remained clear.

A read-only physical-memory check also corrected the earlier cached-alias
observation: the active main-channel P slot contains all 36 hardware-written
words at `+0x1360` and level `0x00000a13`; its IDR slot contains a distinct
history and level `0x08ab0c89`. The hardware writeback is therefore present.
The remaining lifecycle issue is coherent CPU visibility and the OEM
cross-slot handoff, not absent AVPU output.

The software-controller seam is now measured as well. The CBR controller is
an inline-method object, and its per-completion method at `+0x10` is a
4084-byte state-machine routine rather than a payload-only QP adjustment. It
receives the full combined slice status, completed access-unit bit count,
skip flag, and slice budget. An interposed OEM run showed approximately
`0x1c0` bytes of persistent state changing independently for the main and sub
channels; confirmed fields include current QP at `+0x78`, cumulative completed
bits at `+0xd0`, and completed-picture count at `+0x170`.

The first exact stage of that routine now lives in a bounded, host-tested T41
component. It combines four hardware counters at slice-status offsets
`+0x24..+0x30`, then normalizes the counters at `+0x14..+0x20` exactly as the
OEM routine does. Captured OEM fixtures produce block totals of 32640 for the
1920x1088-coded main channel and 3680 for the padded 640x368 subchannel. The
same totals are now observed live on the open stack, and a 100-frame main
sample with this observation-only path remained decoder-clean (1,909,504
bytes). These values are deliberately not connected to command QP until the
remaining persistent-state transitions reproduce the OEM trace.

## Remaining work

The zero-copy Raptor path is not yet correctness-safe. Raptor's reference-mode
consumer maps `/dev/rmem` through a different cached virtual alias from the one
OpenIMP uses to compact the completed access unit. Invalidating that foreign
alias is unsafe, and publishing it without a coherent ownership transition can
produce stale blocks. Keep embedded copy mode enabled until that cache contract
is solved explicitly.

Open TX-ISP exposure and color are now close to the measured OEM baseline, but
scene-by-scene image-quality parity is still being tuned. The native T41 AVPU
path also delivers fewer frames per second than the configured rate and an RSD
ring reopen can reset an active client's RTP epoch. Throughput work, zero-copy
optimization, and V4L2 support remain intentionally deferred until cross-SoC
correctness and image-quality work are complete.
