# OpenIMP

OpenIMP is an open implementation of Ingenic's `libimp` video API. It runs
against the stock ISP, sensor, FrameSource, and AVPU kernel drivers.

## Architecture

There is one encoder implementation for T31 and T40:

- `src/t40/openimp_p2_encoder.c` owns the public encoder lifecycle.
- `src/t40/codec-t40.c` owns the shared AVPU codec backend.
- `src/al_avpu.c`, `src/device_pool.c`, `src/fifo.c`, and
  `src/hw_encoder.c` provide the common hardware path.
- `src/t31/`, `src/framesource/framesource_tseries.c`,
  `src/isp/isp_tseries.c`, `src/kernel_interface.c`, and `src/dma_alloc.c`
  are the T31 stock-driver ABI seam.
- `src/t40/openimp_p1.c` and `src/t40/openimp_p2_dma.c` are the corresponding
  T40 stock-driver seam.

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
```

Outputs:

- `build/t31/libimp.so`
- `build/t40/libimp.so`

Both build scripts reject a produced library that depends on an OEM
`libimp.so`. The T31 build additionally rejects accidental audio exports and
records RVD import coverage in `build/t31/`.

## Current status

- T40: decoder-clean, resolution-independent H.264 streaming through the
  stock T40XP ISP and AVPU drivers.
- T31: shared T40-derived encoder and AVPU backend compile cleanly; live
  validation is performed against the stock T31 ISP and AVPU drivers.
- Sensor configuration and tuning remain owned by the stock ISP driver, so
  the userspace encoder is sensor-independent.

See [`docs/T40_STATUS.md`](docs/T40_STATUS.md) for the detailed T40 runtime
gate.
