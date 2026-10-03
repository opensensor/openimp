# OpenIMP vs vendor libimp: API-level differences per SoC (input for timps `USE_OPENIMP`)

As of: 2026-10-03 (local time). Audience: timps maintainers. Cameras as in the changelog (cam-A T31, cam-B T23,
cam-C T20, cam-D T21, cam-E T10, cam-F T41).

Purpose: timps gets a **compile-time** switch `USE_OPENIMP` (Makefile `USE_OPENIMP ?= 0`, `-DUSE_OPENIMP` in
CFLAGS, set by thingino `package/timps/timps.mk` as `USE_OPENIMP=$(if $(BR2_PACKAGE_OPENIMP),1,0)`, same
pattern as `USE_FAAC`). Under it, `src/isp_caps.h` / `src/enc_caps.h` enable the extra per-SoC features
listed here. There is **no runtime query** (`IMP_ISP_QueryCaps` was withdrawn and is not part of any release).
Vendor builds must stay byte-identical: every change below sits inside `#if defined(USE_OPENIMP)`.

Sources: openimp `claude/openimp-all-12` (787d534) plus pending `claude/eprc-complete`, `claude/t31-allegro-cbr`,
`claude/openimp-t20-jpeg-align`, `claude/eprc-t21-qp-limit`; open-tx-isp `claude/open-tx-isp-all-14` (de10fed6,
contains `claude/t23-matrix-gaps`, `claude/t23-adr-defog`, `claude/t10-t20-nr-wdr`, `claude/t21-tuning-controls`);
device status from [FEATURE_MATRIX.md](FEATURE_MATRIX.md), [OPENIMP_BEYOND_VENDOR.md](OPENIMP_BEYOND_VENDOR.md)
and the matrix test log of 2026-10-03; timps `main` (`src/isp_caps.h`, `src/enc_caps.h`, `src/config.c`,
`src/hal/hal_ingenic.c`) and the vendor headers in timps `include/<SoC>/`; timps branch
`claude/timps-more-controls` (unmerged) for colorfx/scene.

## How to read the tables

Columns: **key** (timps config key / caps macro) · **vendor** (what timps does today with the vendor SDK) ·
**OpenIMP** · **status** · **API** (exact IMP call, signature, value range).

Direction marks: **[+]** OpenIMP does more than vendor · **[−]** OpenIMP does less · **[=]** parity, listed only
where timps needs to know something.

Status: **device-tested** (only these are proposed for enabling) · **in work** (built or being built, device
test pending) · **not supported**.

Header facts that matter for the prototype question (vendor headers shipped in timps `include/<SoC>/`):

| SoC | vendor `imp_isp.h` lacks (of the calls discussed here) | vendor `imp_encoder.h` |
|---|---|---|
| T10, T20, T30 | `SetDPC_Strength`, `SetDefog_Strength`, `SetDRC_Strength`, `SetBcshHue`, `SetBacklightComp`, `SetFrontCrop` | has `SetChnHSkip`, `SetChnFrmRate`, `SetGOPSize` |
| T21 | `SetDPC_Strength`, `SetDefog_Strength`, `SetAeComp`, `SetBcshHue`, `SetBacklightComp`, `SetFrontCrop` | has `SetChnHSkip`, `SetChnFrmRate`, `SetGOPSize` |
| T23 | `SetSceneMode`, `SetColorfxMode` | has `SetChnHSkip`, `SetChnFrmRate`, `SetGOPSize` |
| T31 | `SetSceneMode`, `SetColorfxMode` | `SetChnFrmRate`, `SetChnGopLength` (no HSkip) |
| T40, T41 | new tuning API (`IMPVI_NUM` + pointers); only `SetBcshHue` of this list | `SetChnFrmRate`, `SetChnGopLength` |

OpenIMP's own `include/imp/imp_isp.h` declares all ISP setters on every platform (no per-SoC guards); the
encoder header guards `SetChnFrmRate` (const-pointer form on T21/T23/T30, in/out form elsewhere) and
`SetChnHSkip` (T23; T21 added on `claude/eprc-complete`). timps builds against the vendor header set, so any
call the vendor header lacks needs an own prototype under `USE_OPENIMP` (OpenIMP signatures below).

---

## T10 (cam-E)

| key | vendor (timps today) | OpenIMP | status | API |
|---|---|---|---|---|
| `sinter_strength`, `temper_strength` / `ISP_HAS_NR` | cap on (classic API), but the vendor firmware renormalises it away: no-op except 0 | **[+]** strength acts: temporal noise 7.11 / 2.91 / 1.51 at temper 0/128/255, kept across day/night; 128 = IQ table (default picture identical to vendor) | device-tested (open-tx-isp `claude/t10-t20-nr-wdr`, in all-14) | `int IMP_ISP_Tuning_SetSinterStrength(uint32_t ratio)`, `SetTemperStrength(uint32_t)`; 0..255, 128 neutral (OpenIMP maps to vendor percent `v*100/128`, cap 200) |
| `dpc_strength` / `ISP_HAS_DPC` | off (no prototype, no effect) | **[+]** being added | in work (`claude/t1x-beyond-vendor-ctrls`) | `int IMP_ISP_Tuning_SetDPC_Strength(uint32_t ratio)`; 0..255 |
| `defog_strength` / `ISP_HAS_DEFOG` | off | **[+]** being added | in work (same branch) | `int IMP_ISP_Tuning_SetDefog_Strength(uint8_t *ratio)` (pointer, one byte); 0..255 |
| `drc_strength` / `ISP_HAS_DRC` | off | **[+]** being added (isp-m0 WDR flag already fixed) | in work (same branch) | `int IMP_ISP_Tuning_SetDRC_Strength(uint32_t ratio)`; 0..255 |
| colorfx / scene / `ISP_HAS_COLORFX`, `ISP_HAS_SCENE` (timps-more-controls) | not in timps main; tuning node not reachable from outside | **[+]** being added | in work (same branch) | `int IMP_ISP_Tuning_SetColorfxMode(IMPISPColorfxMode)`, `SetSceneMode(IMPISPSceneMode)` (vendor T10 header has both) |
| `ae_it_max_us` / `ISP_HAS_AE_IT_RANGE` | on | [=] limits the AE | device-tested | `IMP_ISP_Tuning_SetIntegrationTime(IMPISPITAttr*)` (range mode) |
| `rc_mode` SMART | vendor OEM controller | **[−]** SMART mapped to VBR unless `OPENIMP_T10_RC=1` (opt-in OEM-style controller, VBR super-frame fix) | default path device-tested; opt-in controller device test pending | `IMP_Encoder_CreateChn` / `SetChnAttrRcMode` |
| `quality_lvl`, `change_pos` (classic `ENC_LIVE_KEYS`) | live | **[−]** accepted, ignored (mode, bitrate, QP range, GOP, fps act) | device-tested (readback only in the video attr) | `IMP_Encoder_SetChnAttrRcMode` |

## T20 (cam-C)

| key | vendor (timps today) | OpenIMP | status | API |
|---|---|---|---|---|
| `sinter_strength`, `temper_strength` / `ISP_HAS_NR` | cap on, vendor no-op | **[+]** acts: temper 0/64/128/200 gives 0/42/85/132, sinter 0/17/35/69 at high gain; 128 = IQ; kept across day/night | device-tested (open-tx-isp `claude/t10-t20-nr-wdr` + openimp `claude/t20-nr-strength`) | as T10 |
| colorfx / scene (`ISP_HAS_COLORFX`, `ISP_HAS_SCENE`, timps-more-controls) | not in timps main; vendor kernel ignores | **[+]** colorfx 0–3 set/get ok, sepia visible; scene ok | device-tested | `SetColorfxMode(IMPISPColorfxMode)` 0..3, `SetSceneMode(IMPISPSceneMode)`; vendor header has both |
| `dpc_strength`, `defog_strength` | off (no prototype) | **[+]** being added | in work (`claude/t1x-beyond-vendor-ctrls`) | see T10 |
| `drc_strength` | off (no prototype) | **[+]** OpenIMP already routes it to `TISP_CID_DRC_ATTR` (read-modify-write of the strength byte); kernel side in work | in work | `int IMP_ISP_Tuning_SetDRC_Strength(uint32_t ratio)`; 0..255 |
| `ae_it_max_us` / `ISP_HAS_AE_IT_RANGE` | on | [=] limits the AE | device-tested | as T10 |
| `rc_mode` SMART, RC strength | vendor always runs the OEM controller | **[−]** OEM controller only with `OPENIMP_T20_RC=1` (needs kernel patch 0101); default maps SMART to VBR | opt-in path device-tested (CBR 1300 at 1200 kbit/s with I-aware budget) | `SetChnAttrRcMode` |
| `quality_lvl`, `change_pos` | live | **[−]** ignored, readback returns vendor-clamped values | device-tested | `IMP_Encoder_GetChnAttrRcAttr` |
| sub-stream height not a multiple of 8 | vendor scaler hangs (480x270: no frames) | **[+]** rounded up with a warning (270 to 272) | in work (`claude/openimp-t20-jpeg-align`, device-tested with timps `claude/timps-jpeg-idle-nopoll`, not in an aggregate) | `IMP_FrameSource_SetChnAttr` |

## T21 (cam-D)

| key | vendor (timps today) | OpenIMP | status | API |
|---|---|---|---|---|
| colorfx / scene (timps-more-controls) | vendor kernel ignores both | **[+]** B/W, vivid, negative act; getters return what was set | device-tested [-all-13] (`claude/t21-tuning-controls`) | `SetColorfxMode`, `SetSceneMode` (vendor T21 header has both) |
| `sinter_strength`, `temper_strength` / `ISP_HAS_NR` | cap on, vendor ignores | **[+]** strength acts (2DNR gain tracking also repaired) | device-tested [-all-13] | as T10; 0..255, 128 neutral |
| `drc_strength` / `ISP_HAS_DRC` | on | [=]/[+] ADR lifted (40/40 emulator), DRC reaches the driver | device-tested (driver path); effect not separately measured | `SetDRC_Strength(uint32_t)` |
| `defog_strength` / `ISP_HAS_DEFOG` | off (vendor header lacks prototype) | **[+]** defog block lifted, IRQ 21 registered; control path being wired | in work (`claude/t1x-beyond-vendor-ctrls`) | `SetDefog_Strength(uint8_t *)` |
| `dpc_strength` / `ISP_HAS_DPC` | off | **[+]** being added | in work | `SetDPC_Strength(uint32_t)` |
| `ae_compensation` / `ISP_HAS_AECOMP` | off (missing from T21 SDK) | OpenIMP exports it, vendor dispatcher lifted | not device-tested individually: **do not enable** | `int IMP_ISP_Tuning_SetAeComp(int comp)` |
| `ae_it_max_us` / `ISP_HAS_AE_IT_RANGE` | on | **[−]** no effect (isp-m0 max IT unchanged) | device-tested negative (matrix test 2026-10-03) | `SetIntegrationTime(IMPISPITAttr*)` |
| HSkip (no timps key yet) | vendor header has it | **[+]** run-time `SetChnHSkip` (maxSameSceneCnt as IDR period, OEM 1.0.33) | in work (`claude/eprc-complete`, host oracle 0 deviations, device test pending) | `int IMP_Encoder_SetChnHSkip(int encChn, const IMPEncoderAttrHSkip *attr)` |
| live `fps`, `gop` (today restart keys) | timps restarts the channel | **[+]** run-time RC/fps/GOP changes reach the eprc controller at the next IDR (OEM `i264e_idr_reconfig`, no extra IDR) | in work (`claude/eprc-complete`) | `int IMP_Encoder_SetChnFrmRate(int, const IMPEncoderFrmRate*)`, `IMP_Encoder_SetGOPSize(int, const IMPEncoderGOPSizeCfg*)` |
| rate control | vendor eprc | [=] vendor-identical T21 eprc is default (CBR 1326 / VBR 1096 / SMART 1071 at 1200 kbit/s); **[−]** MB-level RC not ported; QP-down limit opt-in (`OPENIMP_EPRC_QP_DOWN1`, `claude/eprc-t21-qp-limit`, device test pending) | device-tested (default) | `SetChnAttrRcMode` |
| `quality_lvl`, `change_pos` | live | **[−]** documented as ignored on the pre-eprc path; re-check with eprc default | open | `SetChnAttrRcMode` |
| `hue`, `backlight_compensation` | off | not supported (no device evidence) | not supported | – |

## T23 (cam-B)

T23 already has the full classic ISP cap set in timps (hue, backlight, defog, DPC, DRC, AE comp, AE IT max).
The differences are in behaviour, not in which calls exist.

| key | vendor (timps today) | OpenIMP | status | API |
|---|---|---|---|---|
| `drc_strength` / `ISP_HAS_DRC` | on | **[+]** dynamic ADR lifted from the vendor module (44/44 emulator-identical); DRC 0/255 visibly effective | device-tested (`claude/t23-adr-defog`) | `SetDRC_Strength(uint32_t)` 0..255 |
| `defog_strength` / `ISP_HAS_DEFOG` | on | [=]/[+] defog incl. `tisp_defog_soft_process` lifted, strength works | device-tested | `SetDefog_Strength(uint8_t *)` |
| front crop (no timps key) | vendor accepts any window | **[+]** rejects windows outside the sensor, below 64x64 or odd sizes with -EINVAL; 960x540 crop ok | device-tested (`claude/t23-matrix-gaps`) | `int IMP_ISP_Tuning_SetFrontCrop(IMPISPFrontCrop *)`; vendor header has it, so a key needs no `USE_OPENIMP` gate, only error handling |
| colorfx / scene (timps-more-controls) | vendor header has no prototype | [=] B/W, vivid, negative; unsupported values (colorfx 2, scene 15) EINVAL | device-tested (open-tx-isp `claude/t23-t31-scene-colorfx` + openimp `claude/scene-colorfx-imp`, not in an aggregate) | `SetColorfxMode`, `SetSceneMode`: own prototype needed |
| brightness/contrast/saturation/hue | on | [=] act (were reset on every stream start, fixed) | device-tested (`claude/t23-bcsh-aeit-fix`) | `SetBrightness` etc. |
| `backlight_compensation`, `highlight_depress` | on | [=] only with the lifted vendor AE (`source_ae_oem=1`, now the default again on `claude/t23-matrix-gaps`); **[−]** no effect with the HLIL substitute AE | device-tested with vendor AE | `SetBacklightComp`, `SetHiLightDepress` |
| HSkip (no timps key) | vendor header has it | run-time path hands maxSameSceneCnt to the native encoder | in work (`claude/eprc-complete`) | `SetChnHSkip(int, const IMPEncoderAttrHSkip*)` |
| live `fps`, `gop` | restart | **[+]** applied at the next IDR | in work (`claude/eprc-complete`) | `SetChnFrmRate`, `SetGOPSize` (const-pointer forms) |
| rate control | vendor | [=] eprc controller, SMART 1141 / CBR 1253 / VBR 1255 at 1200 kbit/s; **[−]** MB-level RC not ported | device-tested (60 s) | `SetChnAttrRcMode` |
| rotation 90/270 | sub-stream via native encoder | [=] main stream above 704x576 refused (software rotation) | device-tested | `rotate_caps.h` unchanged |

## T30 (no test camera)

OpenIMP builds T30 with the T21/T23 encoder header branch and the generic ISP CID pass-through; real motion
detection was ported for T30 alongside T20/T21. Nothing is device-tested: **no `USE_OPENIMP` change for T30.**

## T31 (cam-A)

T31 already has the full classic ISP cap set in timps. OpenIMP is mostly vendor parity at the API level.

| key | vendor (timps today) | OpenIMP | status | API |
|---|---|---|---|---|
| rate control VBR / CappedVBR / CappedQuality | vendor Allegro core | [=] Allegro RC core ported instruction by instruction (20 traces + 72x400 random frames state-identical); **[−]** CappedQuality behaves like CappedVBR | device-tested (cam-A) | `IMP_Encoder_CreateChn`, `SetChnAttrRcMode` |
| CBR | vendor, pads with filler NAL | **[+]** CBR on the same Allegro core; **[−]** no filler NAL: static scenes stay below target | in work (`claude/t31-allegro-cbr`, device test pending) | `IMP_ENC_RC_MODE_CBR` |
| H.265 | vendor | [=] real HEVC on AVPU | device-tested | `CreateChn` H.265 profile |
| colorfx / scene (timps-more-controls) | vendor header has no prototype | [=] colorfx 0/1/3/9 ok, unsupported values EINVAL | device-tested (not in an aggregate) | own prototype needed |
| front crop, CSC, scaler level | vendor | [=] crop get/set, CSC presets 0–4 + user matrix | device-tested | `SetFrontCrop`, vendor header has it |
| JPEG, rotation, OSD, AEC | vendor | [+] HW JPEG default, `SetChnRotate` 90/270, IPU OSD, real AEC (`EnableAec` errors when it cannot run) | device-tested | unchanged calls; check `IMP_AI_EnableAec` return |
| live encoder keys (`ENC_LIVE_KEYS` bitrate, min/max QP, i_bias) | live | [=] | device-tested | unchanged |

## T40 (no test camera)

Shares the T40/T41 code path (`src/t40/`). No device evidence: **no `USE_OPENIMP` change for T40.**

## T41 (cam-F)

timps T41 uses the new tuning API (`ISP_NEW_TUNING_API`): brightness, contrast, saturation, sharpness, hue,
flip, anti-flicker, running mode.

| key | vendor (timps today) | OpenIMP | status | API |
|---|---|---|---|---|
| brightness / contrast / saturation | on | [=] act (brightness 255 gives Y 211, contrast 0 flat grey, saturation 0/255 chroma 0.1/7.1) | device-tested (`claude/t41-matrix-fixes`) | new-API `IMP_ISP_Tuning_Set*(IMPVI_NUM, ...)` |
| `hue` / `ISP_HAS_HUE` | on | implemented (`SetBcshHue`), effect not measured | not device-tested | `SetBcshHue` |
| flip | on (at start) | **[−]** driver writes the sensor flip synchronously, but live flip is not effective in the timps flow (timps does not call `SetHVFLIP` live on T41) | open | `IMP_ISP_Tuning_SetHVFLIP` |
| anti-flicker, AE, AWB, WDR, defog, NR, DPC, CCM | partly on | **[−]** unverified ("?" in the matrix); a manual-WB POST once left a black picture until restart (not isolated) | not device-tested | – |
| rate control | vendor | [=] bitrate 400/1200/3000 gives 518/1195/2777 kbit/s | device-tested | unchanged |
| sensor re-registration after OOM kill | vendor needs reboot | [=] same (fix crashed, under analysis) | not supported | `IMP_ISP_AddSensor` |
| module reload, kill -9 recovery | vendor | [+] 10/10 reload cycles, 0 oops; kill -9 recovers 3/3 | device-tested (rev2 image) | – |

No cap enables for T41 under `USE_OPENIMP`.

---

## Proposal for timps (diff sketch only, not applied)

Rule: only device-tested items are enabled. Items in work stay NOHW (no `ISP_HAS_*`, no `F_CAP`) until their
device test passes; then they move into the enable block. Enabled keys show up in `caps.image` and are applied
and persisted (timps 9665c87 semantics).

### `src/isp_caps.h`

```diff
@@ after the vendor matrix, before #endif /* MS_ISP_CAPS_H */
+/* ---- OpenIMP + open-tx-isp extras (compile time, no runtime query) ------
+ * USE_OPENIMP=1 is set by thingino's timps.mk when BR2_PACKAGE_OPENIMP is on.
+ * Only device-tested items; see openimp-docs docs/OPENIMP_SOC_DIFFS.md. Vendor
+ * builds do not see any of this. */
+#if defined(USE_OPENIMP)
+
+/* colour effects / scene mode act on hardware (vendor T20/T21 kernels ignore
+ * them; T23/T31 vendor headers lack the prototypes). Requires the
+ * claude/timps-more-controls keys; gate them here instead of unconditionally. */
+#if defined(PLATFORM_T20)||defined(PLATFORM_T21)||defined(PLATFORM_T23)|| \
+    defined(PLATFORM_T31)
+#define ISP_HAS_COLORFX 1
+#define ISP_HAS_SCENE 1
+#endif
+
+/* T21: the AE IT cap via SetIntegrationTime has no effect on the open stack
+ * (device-tested 2026-10-03); do not advertise it. */
+#if defined(PLATFORM_T21)
+#undef ISP_HAS_AE_IT_RANGE
+#endif
+
+/* Sinter/Temper (ISP_HAS_NR) stays as is on T10/T20/T21: the cap already
+ * exists for the vendor build, where the vendor firmware ignores it; with
+ * OpenIMP it acts (128 = vendor picture). Documentation only, no macro. */
+
+/* IN WORK, keep commented until device-tested (claude/t1x-beyond-vendor-ctrls):
+ * #if defined(PLATFORM_T10)||defined(PLATFORM_T20)||defined(PLATFORM_T21)
+ * #define ISP_HAS_DPC 1
+ * #define ISP_HAS_DEFOG 1
+ * #endif
+ * #if defined(PLATFORM_T10)||defined(PLATFORM_T20)
+ * #define ISP_HAS_DRC 1
+ * #endif
+ * T10 colorfx/scene: same branch. */
+
+/* Prototypes the vendor headers lack (OpenIMP signatures). Only reachable
+ * when USE_OPENIMP, so vendor builds stay byte-identical. */
+#if defined(ISP_HAS_COLORFX) && (defined(PLATFORM_T23)||defined(PLATFORM_T31))
+/* IMPISPSceneMode / IMPISPColorfxMode enums are also missing there: plain int */
+int IMP_ISP_Tuning_SetSceneMode(int mode);
+int IMP_ISP_Tuning_GetSceneMode(int *mode);
+int IMP_ISP_Tuning_SetColorfxMode(int mode);
+int IMP_ISP_Tuning_GetColorfxMode(int *mode);
+#endif
+/* for the in-work block, when it is enabled:
+ * T10/T20/T21: int IMP_ISP_Tuning_SetDPC_Strength(uint32_t ratio);
+ *              int IMP_ISP_Tuning_SetDefog_Strength(uint8_t *ratio);
+ * T10/T20:     int IMP_ISP_Tuning_SetDRC_Strength(uint32_t ratio);
+ * (T21 vendor header already has SetDRC_Strength.) */
+
+#endif /* USE_OPENIMP */
```

Notes:
- `ISP_HAS_*` are consumed by `config.c` (`CAP_*` → `F_CAP`) and by `hal_ingenic.c`'s `isp_apply_image()`
  guards, so the block above is the only place to edit; `CAP_DPC/CAP_DEFOG/CAP_DRC` follow automatically.
- The `#undef ISP_HAS_AE_IT_RANGE` on T21 also removes `AE_IT_SUPERVISE` for T21; that is intended (the cap does
  nothing there).
- If timps-more-controls lands with its own colorfx/scene `#if` (T20/T21/T23/T30/T31, weak symbols), move T20,
  T21, T23, T31 under `USE_OPENIMP` as above and drop T30 (no evidence on either stack).

### `src/enc_caps.h`

```diff
 #else /* classic API: full union re-fill, H264 channels */
+#if defined(USE_OPENIMP) && (defined(PLATFORM_T10)||defined(PLATFORM_T20))
+/* OpenIMP T10/T20: quality_lvl / change_pos are accepted but ignored
+ * (device-tested); do not offer them as live keys. */
+#define ENC_LIVE_KEYS "rc_mode", "bitrate", "qp", "min_qp", "max_qp", "i_bias_lvl"
+#else
 #define ENC_LIVE_KEYS "rc_mode", "bitrate", "qp", "min_qp", "max_qp", \
                       "quality_lvl", "change_pos", "i_bias_lvl"
+#endif
 #endif
+/* IN WORK (claude/eprc-complete, device test pending): on T21/T23 with
+ * USE_OPENIMP, "fps" and "gop" become live keys (SetChnFrmRate / SetGOPSize,
+ * applied at the next IDR), plus an hskip key (SetChnHSkip). Not enabled. */
```

### `docs/ai/config-keys.md` hints (sketch)

| key | hint to add |
|---|---|
| `image.sinter_strength`, `image.temper_strength` | "T10/T20/T21: no effect with the vendor libimp (vendor firmware renormalises it); acts with USE_OPENIMP (128 = vendor picture)." |
| `image.colorfx`, `image.scene` (timps-more-controls) | "Only with USE_OPENIMP: T20 T21 T23 T31 (T21/T20 vendor kernels ignore them). T10: in work." |
| `image.ae_it_max_us` | "T21 with USE_OPENIMP: not advertised (no effect on the open stack)." |
| `image.dpc_strength`, `image.defog_strength` | "T10/T20/T21 with USE_OPENIMP: in work, not yet in caps." |
| `image.drc_strength` | "T10/T20 with USE_OPENIMP: in work, not yet in caps. T23: dynamic ADR with USE_OPENIMP." |
| `videoN.quality_lvl`, `videoN.change_pos` | "T10/T20 with USE_OPENIMP: not live (ignored by the encoder)." |
| `videoN.fps`, `videoN.gop` | "T21/T23 with USE_OPENIMP: live at the next IDR once claude/eprc-complete is device-tested (restart key until then)." |

### Summary of enables vs in work

| SoC | enable under `USE_OPENIMP` (device-tested) | remove under `USE_OPENIMP` | in work (stay NOHW) |
|---|---|---|---|
| T10 | – (NR acts, cap exists) | live `quality_lvl`, `change_pos` | DPC, defog, DRC, colorfx/scene (`claude/t1x-beyond-vendor-ctrls`) |
| T20 | `ISP_HAS_COLORFX`, `ISP_HAS_SCENE` (with timps-more-controls) | live `quality_lvl`, `change_pos` | DPC, defog, DRC |
| T21 | `ISP_HAS_COLORFX`, `ISP_HAS_SCENE` | `ISP_HAS_AE_IT_RANGE` | DPC, defog, AE comp (untested), live fps/GOP, HSkip, QP-down limit |
| T23 | `ISP_HAS_COLORFX`, `ISP_HAS_SCENE` (+ prototypes) | – | live fps/GOP, HSkip |
| T30 | – | – | – |
| T31 | `ISP_HAS_COLORFX`, `ISP_HAS_SCENE` (+ prototypes) | – | CBR on the Allegro core (no filler NAL) |
| T40 | – | – | – |
| T41 | – | – | live flip, ISP tunings beyond B/C/S unverified |
