# T40XP status

The T40 implementation is a clean, OEM-free `libimp.so` for the
T40XP/GC4653 Raptor pipeline. It is maintained under `src/t40/`; the existing
flat and ported T31 builds remain unchanged.

## Current live gate

The validated camera is a Wyze Cam v3 Pro running Thingino with the open
TX-ISP, GC4653 sensor, and AVPU drivers. RVD, RIC, and RSD run with only the
OpenIMP library mapped.

The decoder-clean inspection endpoint is:

```text
rtsp://CAMERA_IP/stream0
```

using `tests/t40/profiles/raptor-live-640.conf`:

- H.264 High, 640x360, 30 fps
- VBR, 1 Mbit/s, QP 20..45
- forced day mode
- one RTSP client

The T40 ISP quality blocks recovered in `open-tx-isp` are required for a
finished image. Without them the AE loop reaches its luma target by driving
the GC4653 to high analog gain while demosaic, gamma, denoise, CCM, lens
shading, and white-balance processing remain bypassed. The result is bright
but visibly grainy and green/yellow. Loading those initialized quality blocks
produces the current inspection-quality stream.

## Milestones

- P0: standalone System state and lifecycle
- P1: open ISP/sensor/FrameSource lifecycle and NV12 capture
- P2: open AVPU/DMA encoder lifecycle
- P3: Raptor video/audio/control import coverage with no OEM `libimp.so`

The library exports every `IMP_*` symbol imported by the target RVD/RAD
binaries. Audio effects are dynamically resolved from the separately
maintained `libaudioProcess-neo`; logging uses the target's
`ingenic-system-libs-neo` package.

## Known gaps

- The 640x360 current-firmware VBR path is decoder-clean and stable.
- 1920x1080 scaling and AVPU submission run, but the completion/status path
  can expose a short zero-filled payload and corrupt the first macroblock.
  The 1080 profile is diagnostic until a fresh main-stream AVPU oracle closes
  that gap.
- AE is active in OpenIMP. The current color/denoise quality still depends on
  the recovered open TX-ISP tuning blocks and their load-time configuration.
- P4 surfaces not used by the active Raptor gate return `ENOTSUP`.

## Build

```sh
./build-t40.sh
```

The script discovers the Thingino T40 1.3.1 headers, cross-compiles
`build/t40/libimp.so`, checks RVD/RAD import coverage when those binaries are
available, and rejects any dependency on OEM `libimp.so`.
