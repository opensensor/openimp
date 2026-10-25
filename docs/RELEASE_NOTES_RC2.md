# OpenIMP RC2 (v2026.10.25-rc2): release note

Second release candidate of the open camera stack: **OpenIMP** (replacement for the vendor `libimp.so` / `libsysutils.so`) together with **open-tx-isp** (open ISP kernel driver). Always use matching tags: [open-tx-isp v2026.10.25-rc2](https://github.com/opensensor/open-tx-isp/releases/tag/v2026.10.25-rc2).

| | |
|---|---|
| Previous | RC1 `v2026.10.11-rc1`: OpenIMP `fe9307c`, open-tx-isp `191be6c5` ([RC1 note](RELEASE-v2026.10.11-rc1.md)) |
| This release | OpenIMP `next` `7c30aa99` + documentation, open-tx-isp `next` `1bf4549d` + documentation |
| Size of the change | OpenIMP 278 commits, open-tx-isp 636 commits since RC1 |
| Feature matrix | [FEATURE_MATRIX.md](FEATURE_MATRIX.md), [rendered](https://htmlpreview.github.io/?https://github.com/opensensor/openimp/blob/aperto/docs/feature-matrix.html) (cells new since RC1 carry an "RC2" badge, filter "only changes since RC1") |
| Beyond the vendor stack | [OPENIMP_BEYOND_VENDOR.md](OPENIMP_BEYOND_VENDOR.md) |
| Full campaign log | [OPEN_STACK_CHANGELOG.md](OPEN_STACK_CHANGELOG.md), [CHANGELOG.md](../CHANGELOG.md) |

**Status per SoC:** T10, T20, T21, T23, T31 are release candidates as in RC1. T41 is complete against the matrix (all 39 applicable cells supported; RC1: 28 of 45 cells). T40 has 35 of 40 applicable cells supported (RC1: 6 of 45, 20 unknown); the rest are the three known limitations and the two audio cells below. T30 stays experimental (no camera in our lab).

## Tested on

The exact RC2 code runs on all **19 cameras** of the test fleet as image `agg46` (since 2026-10-25 ~11:30, with the matching streamer build): T10, T20, T21, T23, T31 (X and L), T40, T41 (two sensors). Before that, the state of 2026-10-24 ran an overnight soak (2026-10-24 22:45 to 2026-10-25 10:00; T41 gc5603 and sc5336, T40 sc830ai, and the rest of the fleet on the previous image): 0 oopses, 0 encoder-dead events, snapshots always 200, no streamer restarts, memory flat. Log: `audit/logs1024/soak-agg4z.log` (kept by the test campaign). Vendor comparisons are A/B runs on the same camera where a vendor stack exists (T40 Eufy, T41 Vanhua/Cinnado, T31, T21).

## T41

- **Complete against the matrix (39 of 39 applicable cells supported, 4 of them beyond vendor; RC1: 28 supported, 4 defects, 7 missing):** New or fixed since RC1: defog (root cause: the translated chain read its tables from the wrong rodata image; defog registers now equal the vendor dump), WDR/ADR/DRC ratios, DPC (true-dark test: no hot pixel left with DPC on), 2D noise reduction (night softness was the missing defog), CCM, gamma and CSC, live front crop (1920x1080 on ch0, up to 1280x720 on ch1), rotation, module reload (49 cycles in one boot, no oops, no leak; the vendor leaks a page per stream start), versioned encoder telemetry, ROI that also works under FixQP (the vendor shows no effect there), day/night with automatic return to day.
- **Vendor-equal RC parameter APIs** (`SetDefaultParam` identical for 40 profile/mode/QP cases, return codes, QP window clamp, run-time `SetChnAttrRcMode` semantics).
- **Beyond vendor:** `OpenIMP_Encoder_GetTelemetry` (frames, bytes, IDR count, encode time, fps, kbit/s) and a real `IMP_Encoder_Query` state; `IMP_Encoder_SetAvpuBsShare` plus the default stream count 2 (saves 2.7 MB video memory for 1080p + 360p); ISP interrupts 25-45 % below the vendor, streamer RSS 35-45 % below; about 4 % more free CPU than the vendor stack.
- **Known limitations:** module is 836 KB against the vendor 778 KB (accepted: extra functions); `iIPDelta`/`iPBDelta`/`eRcOptions`/`uMaxPictureSize` are stored only (under reverse engineering); a forced day mode leaves AWB at 5000 K.

## T40

- **Open driver now usable end to end** (RC1: first device run on the vendor driver): the boot-time oops is fixed (stack overflow in the anti-flicker control), `rmmod` works (30/30 reload cycles, kill -9 and streamer restarts clean, restart leak of ~344 KB closed; the vendor module oopses on `rmmod`), framechannel freed when its client dies, crashes in the lifted tuning controls fixed (`SetModuleControl`, `SetMask`, `SetWdrOutputMode`, CCM/CSC/Gamma getters, `SetScalerLv`).
- **Picture quality against the vendor stack (A/B on the Eufy sc830ai):** AE (day luma 119 against 122, light step settles < 1 s), AWB (R/G 0.98, B/G 1.00 against 1.00/1.02), DRC/Sinter/Temper ratios (Temper uses the vendor formula), CSC and BCSH like the vendor, day/night with DMSC following the night bank (dark+IR p50 8 against vendor 11; before 25).
- **Encoder:** frame-level rate control, AVPU command lists as vendor 1.3.1, H.265 with hardware rate control, 4K HEVC FixQP within -1 % of the target, ROI, SEI, vendor-equal `SetDefaultParam`, telemetry. Audio input and `IMP_AI` volume/gain like vendor 1.3.1.
- **Matrix:** boot guard supported (dry run), `libalog`/`libsysutils` not needed, long-term stability supported (first overnight soak of the T40 stack). AO speaker and AEC: not RC2-relevant, sound tests pending.
- **Known limitations (kept as defects in the matrix):** manual CCM has only a small picture effect (#9); sharpness 4-5 % below the vendor (#12); motion-detection pans proven on the 2026-10-19 code but not re-tested on the final build (#29); module 404 KB (xz) against 230 KB vendor (lifted chains and diagnostics); HLC/BLC strength and ADR bypass have a small effect.

## T31

- **MIPS vector unit (MXU2) AWB statistics by default** (`t31_awb_impl=2`): the AWB colour-temperature detection runs on the SoC vector unit after a load-time self-test, bit-exact with the reference C code, about 2.4x faster sweeps (fast C code if the self-test fails; `t31_awb_impl=0/1` turn it off). Device-tested on a T31X and a T31L.
- Front crop can enlarge up to 2.0x by default (`fcrop_upscale_pct=100`); beyond that it is refused instead of hanging the MSCA. AE minimum EV step, defog tables and `isp_memopt=1` stripe fix like the OEM; AWB statistics interrupt no longer uses a large stack buffer; ISP interrupt quiesce before `request_irq` on reload; CBR debt bound; mainline V4L2 sensor bridge and SC2336 (kernel >= 5.0).
- **Known limitations:** long-term stability stays a defect: one unexplained reboot on a T31L on 2026-10-17 (fork/allocator fault, mitigation merged) and one hang on the old agg43 image on 2026-10-25; a multi-day run of the current code ends on 2026-10-29.

## T23, T21, T20, T10 and general

- **T23:** native Helix H.264 only (the OEM worker bridge is off by default: `OPENIMP_T23_HELIX_BRIDGE=1` restores it); ISP interrupt quiesce before `request_irq` on reload. A reported "day after idle" was traced to the streamer's day/night probe, not to the driver.
- **T21:** sensor exposure/gain written only on change (i2c interrupts 50/s to 0-2/s); reload after streaming no longer livelocks (VIC parked on quiesce; QEMU-tested, not yet on a camera); per-frame Helix path at `-O2`.
- **T10/T20:** `SetAntiFogAttr` and `SetMeshShadingScale` like the vendor 3.12.0 library; `SetChnRcTrigLevel`.
- **General:** smaller `libimp.so` (`-Os`, debug code compiled out unless `OPENIMP_DEBUG=1`); `OPENIMP_LOG_LEVEL`; named threads for `top -H`; `make release-check` and `make export-check` (vendor export list comparison); `SU_Base_Shutdown` reboots busybox cameras; feature matrix of 45 features and 8 SoC columns with a rendered view that highlights the changes since RC1.
- **Beyond the vendor stack (all SoCs, details in [OPENIMP_BEYOND_VENDOR.md](OPENIMP_BEYOND_VENDOR.md)):** reload and restart robustness (0 oops in repeated cycles), smaller library and module sizes, test pattern modes with runtime API ([TESTPATTERN.md](TESTPATTERN.md)), T31 encoder ROI, per-stream telemetry, QEMU-based regression tests of the driver (found real bugs, e.g. the T40 ISR slot index and a pending interrupt at reload).

## Upgrade notes (changed defaults and interfaces)

| Area | Change |
|---|---|
| T41 defog | defog now works and `t41_defog=1` is the default (RC1: refused; an intermediate state had it off because of a diamond artefact, fixed at the root) |
| T41 bring-up parameters | experiment switches and `t41_tmo_map_selftest` are built in only with `TX_ISP_T41_BRINGUP_PARAMS=y` / `TX_ISP_T41_TMO_SELFTEST=y` (default `n`); scripts that set such parameters must be updated |
| T40 bring-up parameters | 340 experiment/diagnostic parameters and `/proc/tx_isp_t40_recovered` only with `TX_ISP_T40_BRINGUP_PARAMS=y`, `TX_ISP_T40_DEBUG_PROC=y` (default `n`); 288 parameters remain |
| T31 AWB | `t31_awb_impl=2` (MXU2 after self-test) is the default; `0`/`1` for the reference or fast C code |
| T31 crop | `fcrop_upscale_pct` default 0 -> 100 (enlarging up to 2.0x) |
| T40 DMSC | `t40_dmsc_follow=1` default; `0` restores the old night behaviour |
| T40/T41 encoder | default stream-buffer count 4 -> 2 as the vendor (T41 `SetAvpuBsShare` also defaults to 2 streams; the vendor uses 1) |
| T41 telemetry | `OpenIMPEncoderTelemetry` is versioned (`size`, `version` leading fields, append-only). Streamers built against the unversioned layout of 2026-10-24 must be rebuilt; no earlier release shipped it |
| T23 | OEM Helix bridge off (`OPENIMP_T23_HELIX_BRIDGE=0`) |
| Logging | `OPENIMP_LOG_LEVEL` default 5; developer traces need `OPENIMP_DEBUG=1` at build time |

Module and library must come from the same tag. Thingino `aperto` pins the tag instead of a SHA.
