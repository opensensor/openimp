<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/banner-dark.svg">
    <img src="docs/assets/banner.svg" alt="OpenIMP - OSS stack for Ingenic SoCs" width="560">
  </picture>
</p>

<h1 align="center">OpenIMP</h1>

<p align="center">

[![license](https://img.shields.io/badge/license-per%20file-blue)](NOTICE)
[![SoCs](https://img.shields.io/badge/SoC-T10%20%C2%B7%20T20%20%C2%B7%20T21%20%C2%B7%20T23%20%C2%B7%20T30%20%C2%B7%20T31%20%C2%B7%20T41-3e63dd)](#status)
[![status](https://img.shields.io/badge/open%20stack-device%20tested-30a46c)](#status)
[![branch aperto](https://img.shields.io/badge/branch-aperto-e5484d)](https://github.com/opensensor/openimp/tree/aperto)
[![thingino](https://img.shields.io/badge/thingino-integrated-orange)](https://github.com/themactep/thingino-firmware)
[![platform](https://img.shields.io/badge/platform-MIPS%20%C2%B7%20Linux%203.10%20%26%204.4-lightgrey)](#build)
[![last commit](https://img.shields.io/github/last-commit/opensensor/openimp/aperto)](https://github.com/opensensor/openimp/commits/aperto)
[![PRs welcome](https://img.shields.io/badge/PRs-welcome-brightgreen)](https://github.com/opensensor/openimp/pulls)

</p>

OpenIMP is an open replacement for Ingenic's closed `libimp.so` (the IMP video,
encoder, OSD, IVS and audio API). Together with
[open-tx-isp](https://github.com/opensensor/open-tx-isp) (the open ISP kernel driver) and a
a camera application it forms an open camera stack
for Ingenic T10, T20, T21, T23, T30, T31, T40 and T41 SoCs. The public API is the
vendor IMP API, so existing camera applications link against it unchanged.

The `aperto` branch carries the released open stack (device-test campaign work for
T10/T20/T21/T23/T31/T41) on top of the original `main` line; see [Branches and releases](#branches-and-releases).

## Status

Open stack = open-tx-isp + OpenIMP. **Newest release candidate: RC3 `v2026.11.07-rc3` (OpenIMP and open-tx-isp tag `v2026.11.07-rc3`, timps v1.12.0; RC3 gate green): release note [docs/releases/RC3.md](docs/releases/RC3.md), [GitHub release](https://github.com/Lu-Fi/openimp/releases/tag/v2026.11.07-rc3), rendered feature matrix: **[https://lu-fi.github.io/openimp/feature-matrix.html](https://lu-fi.github.io/openimp/feature-matrix.html)**.** Previous: **RC2 `v2026.10.25-rc2`** ([docs/RELEASE_NOTES_RC2.md](docs/RELEASE_NOTES_RC2.md)), **RC1 v2026.10.11-rc1: openimp fe9307c (agg-35 687a68e4 + docs), open-tx-isp 191be6c5 (agg-35 6e464ac7 + docs).** RC3 gate SoCs: T10, T20, T21, T23, T30, T31, T40, T41 (T32/T33 planned). The agg-35 soak of 2026-10-10/11 on 10 cameras was clean (0 oopses, 0 unexplained reboots, 0 application restarts). Release notes and known issues: [CHANGELOG.md](CHANGELOG.md). The per-SoC table below dates from 2026-10-04; the matrix is newer.

| SoC | Status |
|---|---|
| T10 | Runs the open stack from a flashed image (H.264, hardware JPEG/MJPEG, second stream, OSD, motion detection, day/night). Image controls and AE/AWB quality only partly documented. |
| T20 | Fully open from a flashed image; long soaks (1 h 44 min at 25 fps) without errors; OEM rate controller default, A/B against the vendor stack measured. |
| T21 | Fully open from a flashed image; vendor-identical rate controller, reference-buffer sharing on. |
| T23 | Fully open, native Helix H.264 encoder, no vendor helper (2 h 34 min soak). Frequent Helix frame drops fixed (residual interrupt 0x100, kernel patch merged upstream); still open: a rare single Helix encode error (errno 5), no real WDR yet. |
| T30 | Runs the open stack on the T30X lab camera (since 2026-11-03, gate SoC for RC3): H.264 at 2048x1536/1080p/720p + sub-stream, H.265 on the Radix encoder with vendor-equal CBR and live rate-control changes, OEM picture rate controller (bit-exact), calm first stream window, software rotation, ISP bypass graph (RAW). Device tests per issue (#208, #209, #216, #218, #228). |
| T31 | Reference SoC. H.264, HEVC, hardware JPEG, OSD, rotation, AEC; 2 h 53 min soak without errors. |
| T40 | Runs the open stack (open-tx-isp driver + OpenIMP) from a flashed image on the T40XP test camera: H.264/H.265, day/night, AE/AWB and the tuning API with device tests (2026-10-31, image agg59: apitest all areas plus 28 cases PASS 270 / FAIL 0, 0 oops, image agg60). CPU load is vendor-like (ISP busy 1.6 % against 1.4 %, encoder 0.71 % against 0.77 / 0.84 %; #133, #149 closed). Open: ISP OSD picture of the Set calls (#141). |
| T41 | Runs from a flashed image with H.264 and H.265 on the test cameras, with day/night and AE/AWB; the API coverage is strict (every vendor function has a case). Open items are tracked in the GitHub issues (label `soc:T41`). |

### Completeness

Vendor IMP/SU functions per SoC on 2026-10-29 (from the "All areas" coverage summary of [FEATURE_MATRIX](docs/FEATURE_MATRIX.md#coverage-summary)). "Works" = device-tested or vendor no-op (a round trip counts as working); "implemented" = exists but not device-tested; "gaps" = cache-only, stub, error or missing; "?" = not determined yet.

| SoC | Vendor functions | Works | Implemented, not device-tested | Gaps | ? | Status |
|---|--:|--:|--:|--:|--:|---|
| T10 | 333 | 272 (81.7 %) | 26 | 31 | 4 | release |
| T20 | 333 | 273 (82.0 %) | 27 | 29 | 4 | release |
| T21 | 333 | 288 (86.5 %) | 26 | 16 | 3 | release |
| T23 | 457 | 391 (85.6 %) | 40 | 17 | 9 | release |
| T31 | 406 | 348 (85.7 %) | 34 | 7 | 17 | release |
| T30 | 354 | 0 (0.0 %) | 44 | 21 | 289 | experimental |
| T40 | 402 | 330 (82.1 %) | 27 | 18 | 28 | experimental |
| T41 | 449 | 341 (75.9 %) | 50 | 58 | 0 | experimental |

The counts follow `next` and are refreshed by hand after each merge round (the generated source is the FEATURE_MATRIX). Release notes: [docs/releases/RC3.md](docs/releases/RC3.md) (RC3), [docs/RELEASE_NOTES_RC2.md](docs/RELEASE_NOTES_RC2.md) (RC2), [docs/RELEASE-v2026.10.11-rc1.md](docs/RELEASE-v2026.10.11-rc1.md) (RC1).

Details per feature and SoC: [FEATURE_MATRIX](docs/FEATURE_MATRIX.md).
History: [CHANGELOG.md](CHANGELOG.md) and the full
[OPEN_STACK_CHANGELOG](docs/OPEN_STACK_CHANGELOG.md).
Only device-tested behaviour is listed as working; everything else is marked as pending.

## Where it is better than the vendor stack

Measured on a T20 at night, same scene, same application, only libimp and the ISP driver differ:

| Metric | Vendor | Open |
|---|---|---|
| application CPU (all threads) | 25.2 % | 10.5 % |
| snapshot latency (mean of 10) | 0.45 s | 0.20 s |
| application RSS | 6.8 MB | 3.7 MB |

Other points, all device-tested:

- T20 and T10: about 8 MB more free RAM after the unused V4L2 frame pool was switched off.
- libimp is about 0.5-0.6 MB instead of 1.0-1.3 MB; T21 has 2.76 MB instead of ~1.2 MB free video memory.
- Reference-frame sharing on T21/T23 saves ~1.5 MB.
- Reload and stop robustness: 10 stop/start and reload cycles without an oops.
- Controls and noise reduction that the vendor ignores act on T10/T20/T21; real motion detection on T20/T21/T30; HEVC fails cleanly on SoCs without HEVC hardware.
- Motion detection v2 (per-cell background model, suppression after IR/gain jumps, bounding boxes and strength through the versioned `OpenIMP_IVS_MoveGetResultEx` API) is on by default; `OPENIMP_MOTION_V2=0` restores the vendor algorithm. Device-tested on T20 and T23 over one night.
- Memory diagnostics: rmem peak logging and a shortfall message with a concrete `rmem=<n>M` suggestion instead of silent degradation (see the wiki page Memory).

Integration notes for application authors, env switches and defaults:
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
`BR2_PACKAGE_THINGINO_ISP_OPEN` (menu "ISP stack") and pinned by commit SHA to the `aperto`
branches of the opensensor repositories. Both replace the proprietary `libimp.so` and `tx-isp-<soc>.ko`; the
SDK sensor, audio and AVPU modules stay; T23 runs fully on OpenIMP without any vendor helper. The
kernel VPU/rmem stability patches
([#1748](https://github.com/themactep/thingino-firmware/pull/1748),
[#1752](https://github.com/themactep/thingino-firmware/pull/1752)) and the optional boot guard
`BR2_PACKAGE_THINGINO_ISP_GUARD` ([#1749](https://github.com/themactep/thingino-firmware/pull/1749),
default off) are merged there as well. The camera application is built with `USE_OPENIMP=1` to enable
features that exist only in OpenIMP. Once the first date tag exists on `aperto`, thingino's `aperto` branch will pin that tag instead of a SHA.

## Branches and releases

- `main`: the original author's line (opensensor). It is left untouched and is a strict ancestor of `aperto`.
- `aperto`: the open stack release line: OpenIMP + open-tx-isp, as used by the thingino `aperto` branch. Fast-forward only; every commit was flashed and checked on cameras. Releases are tagged `vYYYY.MM.DD` on this branch, and thingino pins a tag instead of a SHA.
- Development and the device-test campaign happen in the [Lu-Fi forks](https://github.com/Lu-Fi/openimp) (branch `next` = integration, `claude/<topic>` = topic branches); tested work reaches `aperto` from `next` after a clean soak.
- Companion repository: [opensensor/open-tx-isp](https://github.com/opensensor/open-tx-isp) (branch `aperto`). Both repositories are released together; use matching tags.

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
profiling: [`docs/PROFILING.md`](docs/PROFILING.md),
unexplained reboots: [`docs/RESET_DIAGNOSTICS.md`](docs/RESET_DIAGNOSTICS.md).

Audio processing belongs to [`libaudioProcess-neo`](https://github.com/gtxaspec/libaudioProcess-neo),
logging and system libraries to [`ingenic-system-libs-neo`](https://github.com/gtxaspec/ingenic-system-libs-neo).
OpenIMP provides the native IMP audio I/O layer on T31 and T40/T41; HPF, noise suppression and AGC are
loaded at runtime from `libaudioProcess-neo`. Acoustic echo cancellation uses the bundled WebRTC AECM.

## Documentation

- [Wiki](https://github.com/opensensor/openimp/wiki): build and install, module parameters, `OPENIMP_*` variables, memory sizing, troubleshooting, release scheme (one wiki for OpenIMP and open-tx-isp).
- [`docs/FEATURE_MATRIX.md`](docs/FEATURE_MATRIX.md) (also as [`feature-matrix.html`](docs/feature-matrix.html), [rendered](https://lu-fi.github.io/openimp/feature-matrix.html)): what works on which SoC.
- [`docs/OPEN_STACK_CHANGELOG.md`](docs/OPEN_STACK_CHANGELOG.md) and [`CHANGELOG.md`](CHANGELOG.md): history of the open stack.
- [`docs/OPENIMP_BEYOND_VENDOR.md`](docs/OPENIMP_BEYOND_VENDOR.md) and [`docs/OPENIMP_SOC_DIFFS.md`](docs/OPENIMP_SOC_DIFFS.md): differences to the vendor stack, for application authors.
- [`docs/PERFORMANCE.md`](docs/PERFORMANCE.md): performance and resources, open stack versus vendor stack, per SoC, with sources and caveats.
- [`docs/test-reports/`](docs/test-reports): raw device test reports.
- [`docs/re/`](docs/re): reverse-engineering dumps (vendor `libimp.so` HLIL, register traces) used by the RE notes.
- [`docs/archive/`](docs/archive): historical status and design notes from the early OpenIMP work (kept for reference, may be outdated).

## How status and issues are tracked

- **Status data**: the implementation status per feature and SoC lives in structured files, [`docs/status/features.yaml`](docs/status/features.yaml) (symbol, detail text, issue numbers per cell) and [`docs/status/history.md`](docs/status/history.md) (update log), plus [`docs/api_evidence.json`](docs/api_evidence.json) for the per-function API coverage. [`docs/FEATURE_MATRIX.md`](docs/FEATURE_MATRIX.md) and [`docs/feature-matrix.html`](docs/feature-matrix.html) are **generated** from them (`python3 tools/gen_feature_matrix_html.py`; needs PyYAML) and published with GitHub Pages; do not edit them by hand. `make check` validates the data (unknown symbols, missing SoCs, dangling issue numbers).
- **Gaps, bugs and progress** are public [GitHub Issues](https://github.com/Lu-Fi/openimp/issues) of this repository, labelled `soc:T..`, `area:..`, `type:gap|bug|wip`, `prio:release-gate|streamer|long-term` and `repo:openimp|open-tx-isp` (the kernel driver is [open-tx-isp](https://github.com/Lu-Fi/open-tx-isp)). An open issue with the label `type:wip` shows as the construction sign on the linked matrix cell; cells link to their issues. The cached issue state is `docs/status/issues.json` (`python3 tools/status_data.py --refresh-issues`, also done by the Pages workflow). The RC3 release gate is red while an open issue is labelled `severity:critical` or `severity:high` or sits in the milestone RC3 (current state: section "Release gates" of [docs/FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md)). The issues are collected on the GitHub Project "OpenIMP / open-tx-isp status" (Todo / In progress / Review / Done).

## Reporting problems

Open an issue at [opensensor/openimp](https://github.com/opensensor/openimp/issues) (library, encoder, application integration) or [opensensor/open-tx-isp](https://github.com/opensensor/open-tx-isp/issues) (kernel driver, ISP, memory). Please include the SoC and sensor, the revisions of open-tx-isp, OpenIMP and the camera application, `dmesg`, the application log (including the `rmem peak` and "effective rate control" lines), the stream set and the `rmem`/`ispmem` values. Do not post addresses, credentials or location names. Details: [Troubleshooting](https://github.com/opensensor/openimp/wiki/Troubleshooting#reporting-a-problem).

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
