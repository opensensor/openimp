# T40XP status

The T40 implementation is a clean, OEM-free `libimp.so` for the
T40XP/GC4653 Raptor pipeline. It is maintained under `src/t40/`; the existing
flat and ported T31 builds remain unchanged.

## Current live gate

The validated camera is a Wyze Cam v3 Pro running Thingino with the stock
`tx_isp_t40` and `sensor_gc4653_t40` kernel modules. RVD, RIC, and RSD run
with only the OpenIMP library mapped; `tx_isp_t40_recovered` is not loaded.
The T40 userspace implementation does not contain a GC4653 register table,
gain LUT, or fixed sensor dimensions. It consumes the dimensions configured
by the application and relies on the loaded stock sensor module and tuning
blob for sensor-specific policy.

On the current Raptor firmware, stream 0 is published as:

```text
rtsp://CAMERA_IP/ch0
```

The live profiles cover:

- `raptor-live-640.conf`: 640x360 at 30 fps
- `raptor-live-720.conf`: 1280x720 at 30 fps
- `raptor-live-1080.conf`: 1920x1080 at 15 fps
- `raptor-live-1440.conf`: 2560x1440 at 15 fps

All profiles use H.264 High and derive capture allocation, NV12 plane
addresses, AVPU pitches, command geometry, SPS cropping/timing, rate-control
grid, reference storage, and stream-buffer sizing from the active
configuration.

OpenIMP passes the configured tuning-bin path to the stock ISP before sensor
selection. The stock ISP firmware owns AE, AWB, demosaic, gamma, denoise, CCM,
lens shading, and sensor-specific exposure/gain translation. OpenIMP forwards
public tuning controls through the OEM T40 descriptor ABI; it contains no
GC4653 register table or analog-gain LUT.

The capture buffer is returned to the stock frame channel immediately after
AVPU submission. This matches the OEM ownership window and prevents a 30 fps
channel from collapsing to approximately 15 fps, which otherwise starves the
ISP temporal filters and produces a visibly grainy image.

The July 30 live gate recorded:

- 1280x720: 302 decoder-clean frames in 10.033 seconds, with no steady-state
  ISP overflow
- 2560x1440: 152 decoder-clean frames in 10.067 seconds at 3.76 Mbit/s for a
  configured 6 Mbit/s VBR ceiling

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

- Extended decoder-clean and reconnect soak testing remains part of the
  encoder gate.
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
