# T40XP status

The T40 implementation is a clean, OEM-free `libimp.so` for the
T40XP/GC4653 Raptor pipeline. It is maintained under `src/t40/`; the existing
flat and ported T31 builds remain unchanged.

## Current live gate

The validated camera is a Wyze Cam v3 Pro running Thingino with the stock
`tx_isp_t40` and `sensor_gc4653_t40` kernel modules. RVD, RIC, and RSD run
with only the OpenIMP library mapped; `tx_isp_t40_recovered` is not loaded.

The live inspection endpoint is:

```text
rtsp://CAMERA_IP/stream0
```

using `tests/t40/profiles/raptor-live-640.conf`:

- H.264 High, 640x360, 30 fps
- VBR, 1 Mbit/s, QP 20..45
- forced day mode
- one RTSP client

OpenIMP passes the configured tuning-bin path to the stock ISP before sensor
selection. The stock ISP firmware owns AE, AWB, demosaic, gamma, denoise, CCM,
lens shading, and sensor-specific exposure/gain translation. OpenIMP forwards
public tuning controls through the OEM T40 descriptor ABI; it contains no
GC4653 register table or analog-gain LUT.

## Milestones

- P0: standalone System state and lifecycle
- P1: stock ISP/sensor/FrameSource lifecycle and NV12 capture
- P2: open AVPU/DMA encoder lifecycle
- P3: Raptor video/audio/control import coverage with no OEM `libimp.so`

The library exports every `IMP_*` symbol imported by the target RVD/RAD
binaries. Audio effects are dynamically resolved from the separately
maintained `libaudioProcess-neo`; logging uses the target's
`ingenic-system-libs-neo` package.

## Known gaps

- The 640x360 current-firmware VBR path is live and decodable. Extended
  decoder-clean soak testing remains part of the encoder gate.
- 1920x1080 scaling and AVPU submission run, but the completion/status path
  can expose a short zero-filled payload and corrupt the first macroblock.
  The 1080 profile is diagnostic until a fresh main-stream AVPU oracle closes
  that gap.
- AE/AWB and image-quality processing run in the stock ISP firmware using the
  selected sensor tuning blob. OpenIMP forwards tuning requests without
  duplicating sensor policy.
- P4 surfaces not used by the active Raptor gate return `ENOTSUP`.

## Build

```sh
./build-t40.sh
```

The script discovers the Thingino T40 1.3.1 headers, cross-compiles
`build/t40/libimp.so`, checks RVD/RAD import coverage when those binaries are
available, and rejects any dependency on OEM `libimp.so`.
