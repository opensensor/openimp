# OpenIMP

OpenIMP is an open implementation of Ingenic's `libimp` video API. It runs
against the stock ISP, sensor, FrameSource, and AVPU kernel drivers.

## Architecture

There is one encoder implementation for T31, T40, and the T41 bring-up:

- `src/t40/openimp_p2_encoder.c` owns the public encoder lifecycle.
- `src/t40/codec-t40.c` owns the shared AVPU codec backend.
- `src/al_avpu.c`, `src/device_pool.c`, `src/fifo.c`, and
  `src/hw_encoder.c` provide the common hardware path.
- `src/t31/`, `src/framesource/framesource_tseries.c`,
  `src/isp/isp_tseries.c`, `src/kernel_interface.c`, and `src/dma_alloc.c`
  are the T31 stock-driver ABI seam.
- `src/t40/openimp_p1.c` and `src/t40/openimp_p2_dma.c` are the corresponding
  T4 stock-driver seam, with explicit T40/T41 ABI branches.

T31 does not carry a separate scheduler, rate-control graph, or codec
implementation. Platform conditionals are restricted to real ABI differences
such as public structure sizes, ioctl layouts, device behavior, and cache
maintenance.

Audio processing belongs to
[`libaudioProcess-neo`](https://github.com/gtxaspec/libaudioProcess-neo).
Logging and system support libraries belong to
[`ingenic-system-libs-neo`](https://github.com/gtxaspec/ingenic-system-libs-neo).
OpenIMP's normal builds do not export audio APIs or build replacement
`libsysutils`/`libalog` libraries.

## Build

```sh
# T31 (default)
./build-for-device.sh
# or
make t31

# T40/T40XP
./build-for-device.sh T40
# or
make t40

# T41/T41NQ
./build-for-device.sh T41
# or
make t41
```

Outputs:

- `build/t31/libimp.so`
- `build/t40/libimp.so`
- `build/t41/libimp.so`

Both build scripts reject a produced library that depends on an OEM
`libimp.so`. The T31 build additionally rejects accidental audio exports and
records RVD import coverage in `build/t31/`.

## Current status

- T40: decoder-clean, resolution-independent H.264 streaming through the
  stock T40XP ISP and AVPU drivers.
- T31: the shared T40-derived encoder ran for more than 1,000 hardware frames
  on a stock-driver GC2053 camera. Main 1920x1080 H.264 and AAC probed and
  decoded without H.264 errors during the device smoke cycle. The T31
  FrameSource seam now preserves the kernel DQBUF completion timestamp in the
  OEM `frameInfo.timeStamp` slot at offset `0x20`; the shared encoder copies
  that capture timestamp into each public pack instead of substituting
  encoder-start wall time. A 30-second live decode completed 721 frames with
  zero duplicate/non-monotonic DTS warnings, compared with 11 warnings in the
  prior 12-second checkpoint.
- Capture ownership is an explicit platform policy: T31 returns a frame after
  AVPU completion, while T40 returns it immediately after submission. This
  preserves full-rate capture on both stock-driver ABIs.
- T41: active correctness bring-up. The build covers every RVD/RAD IMP import,
  the shared pipeline runs dual-channel FrameSource capture, and the native
  AVPU backend emits decoder-clean High-profile H.264 at both configured
  geometries. Raptor currently uses embedded-ring copy mode for correctness;
  its cross-process cached-rmem reference path is not yet coherent. ISP parity
  and configured-rate delivery remain in progress.
- Sensor configuration and tuning remain owned by the stock ISP driver, so
  the userspace encoder is sensor-independent.

See [`docs/T40_STATUS.md`](docs/T40_STATUS.md) and
[`docs/T41_STATUS.md`](docs/T41_STATUS.md) for the detailed runtime gates.
