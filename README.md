<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/banner-dark.svg">
    <img src="docs/assets/banner.svg" alt="Open Ingenic - open source ISP driver &amp; libimp for Ingenic SoCs" width="560">
  </picture>
</p>

<h1 align="center">OpenIMP</h1>

<p align="center">

[![license](https://img.shields.io/badge/license-per%20file-blue)](NOTICE)
[![SoCs](https://img.shields.io/badge/SoC-T10%20%C2%B7%20T20%20%C2%B7%20T21%20%C2%B7%20T23%20%C2%B7%20T30%20%C2%B7%20T31%20%C2%B7%20T41-3e63dd)](#status)
[![status](https://img.shields.io/badge/open%20stack-device%20tested-30a46c)](#status)
[![branch next](https://img.shields.io/badge/branch-next-e5484d)](https://github.com/Lu-Fi/openimp/tree/next)
[![thingino](https://img.shields.io/badge/thingino-integrated-orange)](https://github.com/themactep/thingino-firmware)
[![platform](https://img.shields.io/badge/platform-MIPS%20%C2%B7%20Linux%203.10%20%26%204.4-lightgrey)](#build)
[![last commit](https://img.shields.io/github/last-commit/Lu-Fi/openimp/next)](https://github.com/Lu-Fi/openimp/commits/next)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen)](https://github.com/Lu-Fi/openimp/pulls)

</p>

OpenIMP is an open replacement for Ingenic's closed `libimp.so` (the IMP video,
encoder, OSD, IVS and audio API). Together with
[open-tx-isp](https://github.com/Lu-Fi/open-tx-isp) (the open ISP kernel driver) and a
streamer such as [timps](https://github.com/Lu-Fi/timps) it forms an open camera stack
for Ingenic T10, T20, T21, T23, T30, T31, T40 and T41 SoCs. The public API is the
vendor IMP API, so existing streamers (prudynt, raptor, timps) link against it unchanged.

This fork tracks [opensensor/openimp](https://github.com/opensensor/openimp) and adds the
device-test campaign work for T10/T20/T21/T23/T31/T41.

## Status

Open stack = open-tx-isp + OpenIMP + timps. State on `next` (2026-10-04):

| SoC | Status |
|---|---|
| T10 | Runs the open stack from a flashed image (H.264, hardware JPEG/MJPEG, second stream, OSD, motion detection, day/night). Image controls and AE/AWB quality only partly documented. |
| T20 | Fully open from a flashed image; long soaks (1 h 44 min at 25 fps) without errors; OEM rate controller default, A/B against the vendor stack measured. |
| T21 | Fully open from a flashed image; vendor-identical rate controller, reference-buffer sharing on. |
| T23 | Fully open, native Helix H.264 encoder, no vendor helper (2 h 34 min soak). Frequent Helix frame drops fixed (residual interrupt 0x100, kernel patch merged upstream); still open: a rare single Helix encode error (errno 5), no real WDR yet. |
| T30 | Builds against a real T30 kernel; H.264 command lists match the vendor in an emulator. No device in the test campaign, so not device-verified here. |
| T31 | Reference SoC. H.264, HEVC, hardware JPEG, OSD, rotation, AEC; 2 h 53 min soak without errors. |
| T40 | Builds; AVPU H.264 path from upstream work. Not part of the device campaign. |
| T41 | Runs from a flashed image with H.264 and H.265. Open: live flip, night column noise, short IVS gaps, OOM with three parallel streams at 30 MB rmem (26 MB in the test image works); day/night and AE/AWB quality untested. |

Details per feature and SoC: [FEATURE_MATRIX](docs/FEATURE_MATRIX.md).
History: [CHANGELOG.md](CHANGELOG.md) and the full
[OPEN_STACK_CHANGELOG](docs/OPEN_STACK_CHANGELOG.md).
Only device-tested behaviour is listed as working; everything else is marked as pending.

## Where it is better than the vendor stack

Measured on a T20 at night, same scene, same streamer, only libimp and the ISP driver differ:

| Metric | Vendor | Open |
|---|---|---|
| streamer CPU (all threads) | 25.2 % | 10.5 % |
| snapshot latency (mean of 10) | 0.45 s | 0.20 s |
| streamer RSS | 6.8 MB | 3.7 MB |

Other points, all device-tested:

- T20 and T10: about 8 MB more free RAM after the unused V4L2 frame pool was switched off.
- libimp is about 0.5-0.6 MB instead of 1.0-1.3 MB; T21 has 2.76 MB instead of ~1.2 MB free video memory.
- Reference-frame sharing on T21/T23 saves ~1.5 MB.
- Reload and stop robustness: 10 stop/start and reload cycles without an oops.
- Controls and noise reduction that the vendor ignores act on T10/T20/T21; real motion detection on T20/T21/T30; HEVC fails cleanly on SoCs without HEVC hardware.
- Motion detection v2 (per-cell background model, suppression after IR/gain jumps, bounding boxes and strength through the versioned `OpenIMP_IVS_MoveGetResultEx` API) is on by default; `OPENIMP_MOTION_V2=0` restores the vendor algorithm. Device-tested on T20 and T23 over one night.
- Memory diagnostics: rmem peak logging and a shortfall message with a concrete `rmem=<n>M` suggestion instead of silent degradation (see the wiki page Memory).

Integration notes for streamer authors, env switches and defaults:
[OPENIMP_BEYOND_VENDOR](docs/OPENIMP_BEYOND_VENDOR.md).

## Build

OpenIMP is cross-built for MIPS against a Thingino firmware checkout that already built
the target (toolchain and headers come from its `output/` tree).

```sh
export THINGINO_DIR=/path/to/thingino-firmware      # checkout with a built target
make t31                    # or t20 t21 t23 t30 t40 t41; equivalent: ./build-for-device.sh T31
```

- Each `build-<soc>.sh` takes the toolchain from `THINGINO_DIR` (override with
  `TOOLCHAIN_PREFIX`, `<SOC>_TARGET`, `<SOC>_OUTPUT_DIR`; T20 reuses the T21 script).
  T40/T41 additionally need the vendor header root (`T41_HEADERS`, auto-detected in the Thingino build tree via `thingino-raptor-hal`).
- Output: `build/<soc>/libimp.so` and `build/<soc>/openimp-tuningd`. A build is rejected if the library depends on an OEM `libimp.so`.
- `make t21 OPENIMP_SW_JPEG=1` builds the software JPEG fallback for T20/T21/T23/T30.
- `make install PLATFORM=T31 PREFIX=...` installs headers, library and tuning daemon.
- `make check` runs the host tests (rate-control oracles, T23/T30/T31/T40/T41 checks).
- `BUILD.md` describes an older host build and is outdated; the scripts above are authoritative.

## Integration in Thingino

The packages `openimp` (this repository) and `open-tx-isp` (kernel driver) are in the upstream
[thingino-firmware](https://github.com/themactep/thingino-firmware) branch `aperto`
([#1756](https://github.com/themactep/thingino-firmware/pull/1756)), selected with
`BR2_PACKAGE_THINGINO_ISP_OPEN` (menu "ISP stack") and pinned by commit SHA to the `next`
branches of the Lu-Fi forks. Both replace the proprietary `libimp.so` and `tx-isp-<soc>.ko`; the
SDK sensor, audio and AVPU modules stay; T23 runs fully on OpenIMP without any vendor helper. The
kernel VPU/rmem stability patches
([#1748](https://github.com/themactep/thingino-firmware/pull/1748),
[#1752](https://github.com/themactep/thingino-firmware/pull/1752)) and the optional boot guard
`BR2_PACKAGE_THINGINO_ISP_GUARD` ([#1749](https://github.com/themactep/thingino-firmware/pull/1749),
default off) are merged there as well. The streamer is built with `USE_OPENIMP=1` to enable
features that exist only in OpenIMP. Once the first date tag exists on `aperto`, thingino's `aperto` branch will pin that tag instead of a SHA.

## Branches and releases

- `main`: fork default branch, not the tested stack.
- `next`: tested integration branch. Everything on it was flashed and checked on cameras.
- `aperto`: release branch; fast-forward only from `next` after a clean soak (planned, not created yet; the first tag follows after the 24 h soak that started 2026-10-04). It carries the date tags, and thingino's `aperto` branch pins the tag.
- Tags `vYYYY.MM.DD` on `aperto` (planned), so firmware can pin a tag instead of a SHA.
- Work happens on `claude/<topic>` branches and is merged into `next` after device tests.

## Architecture

The public encoder graph is shared by T23, T30, T31, T40 and T41; the hardware backend is
selected only where the SoC ABI differs.

- `src/t40/openimp_p2_encoder.c`: public encoder lifecycle; `src/t40/codec-t40.c`: shared AVPU backend.
- `include/openimp/openimp_avc.h`: that backend without the IMP graph, for contiguous NV12 producers such as V4L2 DMA-BUF.
- `include/openimp/openimp_tuning.h` and `openimp-tuningd`: image policy and gain feedback, see [`docs/TUNING_DAEMON.md`](docs/TUNING_DAEMON.md).
- `src/al_avpu.c`, `src/device_pool.c`, `src/fifo.c`, `src/hw_encoder.c`: common hardware path.
- `src/t30/`: Helix descriptor and `/dev/soc_vpu` adapter (T20/T21/T23/T30 Helix encoder, native T23 encoder).
- `src/t31/`, `src/framesource/framesource_tseries.c`, `src/isp/isp_tseries.c`, `src/kernel_interface.c`, `src/dma_alloc.c`: T31 stock-driver ABI seam.
- `src/t40/openimp_p1.c`, `src/t40/openimp_p2_dma.c`: T40/T41 seam.

Platform conditionals are limited to real ABI differences (structure sizes, ioctl layouts,
device behaviour, cache maintenance). Status notes per SoC: [`docs/T30_STATUS.md`](docs/T30_STATUS.md),
[`docs/T40_STATUS.md`](docs/T40_STATUS.md), [`docs/T41_STATUS.md`](docs/T41_STATUS.md),
profiling: [`docs/PROFILING.md`](docs/PROFILING.md).

Audio processing belongs to [`libaudioProcess-neo`](https://github.com/gtxaspec/libaudioProcess-neo),
logging and system libraries to [`ingenic-system-libs-neo`](https://github.com/gtxaspec/ingenic-system-libs-neo).
OpenIMP provides the native IMP audio I/O layer on T31 and T40/T41; HPF, noise suppression and AGC are
loaded at runtime from `libaudioProcess-neo`. Acoustic echo cancellation uses the bundled WebRTC AECM.

## Documentation

- [Wiki](https://github.com/Lu-Fi/openimp/wiki): build and install, module parameters, `OPENIMP_*` variables, memory sizing, troubleshooting, release scheme (one wiki for OpenIMP and open-tx-isp).
- [`docs/FEATURE_MATRIX.md`](docs/FEATURE_MATRIX.md) (also as [`feature-matrix.html`](docs/feature-matrix.html)): what works on which SoC.
- [`docs/OPEN_STACK_CHANGELOG.md`](docs/OPEN_STACK_CHANGELOG.md) and [`CHANGELOG.md`](CHANGELOG.md): history of the open stack.
- [`docs/OPENIMP_BEYOND_VENDOR.md`](docs/OPENIMP_BEYOND_VENDOR.md) and [`docs/OPENIMP_SOC_DIFFS.md`](docs/OPENIMP_SOC_DIFFS.md): differences to the vendor stack, for streamer authors.
- [`docs/test-reports/`](docs/test-reports): raw device test reports.
- [`docs/re/`](docs/re): reverse-engineering dumps (vendor `libimp.so` HLIL, register traces) used by the RE notes.
- [`docs/archive/`](docs/archive): historical status and design notes from the early OpenIMP work (kept for reference, may be outdated).

## Reporting problems

Open an issue at [Lu-Fi/openimp](https://github.com/Lu-Fi/openimp/issues) (library, encoder, streamer integration) or [Lu-Fi/open-tx-isp](https://github.com/Lu-Fi/open-tx-isp/issues) (kernel driver, ISP, memory). Please include the SoC and sensor, the revisions of open-tx-isp, OpenIMP and the streamer, `dmesg`, the streamer log (including the `rmem peak` and "effective rate control" lines), the stream set and the `rmem`/`ispmem` values. Do not post addresses, credentials or location names. Details: [Troubleshooting](https://github.com/Lu-Fi/openimp/wiki/Troubleshooting#reporting-a-problem).

## Credits

OpenIMP started as [opensensor/openimp](https://github.com/opensensor/openimp). It builds on the
Thingino and Ingenic reverse-engineering community: [thingino-firmware](https://github.com/themactep/thingino-firmware),
[ingenic-sdk](https://github.com/themactep/ingenic-sdk), `libaudioProcess-neo` and
`ingenic-system-libs-neo` (gtxaspec). Vendored third-party code: WebRTC AECM (BSD-3-Clause),
x264 bitstream helpers (GPL-2.0-or-later).

## Licensing

Licensing is per file, and there is no project-wide license yet. See [`NOTICE`](NOTICE) for the
files that carry a license (LGPL-2.1-or-later, GPL-2.0-or-later x264-derived `src/t30/h264enc/`,
and others) and the "Third-party code" section. The vendored WebRTC AECM in `src/audio/webrtc/`
is BSD-3-Clause with its own `LICENSE`, `PATENTS` and `LICENSE_THIRD_PARTY`. Files without a
header carry no explicit grant.

---

<sub>Not affiliated with or endorsed by Ingenic Semiconductor. "Ingenic" is used only to name the SoCs this project supports.</sub>
