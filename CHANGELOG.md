# Changelog (OpenIMP)

**Where to read what**

- **This file**: release-oriented summary per area (what changed against the vendor stack and against the original upstream line, [opensensor/openimp](https://github.com/opensensor/openimp)), followed by the condensed OpenIMP-only log by date.
- **Full campaign log**: [docs/OPEN_STACK_CHANGELOG.md](docs/OPEN_STACK_CHANGELOG.md): every change of OpenIMP, open-tx-isp and the thingino integration since 2026-09-30, per session, with device-test evidence and numbers. The same text is on the wiki page *Changelog*.
- **Current state per feature and SoC**: [docs/FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md) (HTML view: [docs/feature-matrix.html](docs/feature-matrix.html)); what goes beyond the vendor: [docs/OPENIMP_BEYOND_VENDOR.md](docs/OPENIMP_BEYOND_VENDOR.md).

Release plan: `next` goes to `aperto` (fast-forward only) after the long soak of the release candidate and is tagged `vYYYY.MM.DD`; until then dates are the reference. The first release covers T10, T20, T21, T23 and T31; T10 has not been re-tested on the open stack today ("not re-tested"); T41 is not part of the first release. Branch names (`claude/...`) are historic: the topic branches were merged into `next` and deleted. Cameras are anonymised: cam-A (T31), cam-B (T23), cam-C (T20), cam-D (T21), cam-E (T10), cam-F (T41); cam-G and cam-H are further T23 cameras, cam-I a second T20, cam-J a second T21.

## 2026-11-02 (later), T10/T20 SetChangeRef second try, decide_slice_qp and bEnableScenecut (#3, #5, #7)

- `IMP_Encoder_SetChangeRef` and `bEnableScenecut` are no longer stored values on T10/T20. A picture of the H1M classes (H1M_FALSE SBASE, H1M_TRUE LBASE) that the rate controller asks to code again (the SMART second try with ChangeRef on, but also the CBR / VBR super-frame re-encode) is coded again with the third reconstruction buffer as reference; the smaller try is kept and a kept second try gets the vendor's header commands (no reference list modification, SBASE without MMCO). A base picture at a whole second that is larger than the last IDR picture is coded again as an IDR picture when `bEnableScenecut` is set (rate controller set up again, IDR QP not below the first try's). Base pictures take the QP of the last IDR picture, as `i264e_decide_slice_qp`.
- Proof: `tools/hskip_oracle.py --changeref/--scenecut` against the vendor code (76 scenarios x 240 pictures each: reference buffer, header commands, keep-the-smaller rule, QP rule, scene-cut decision, 0 differences), QEMU T20 and T10, then the cameras. Details in docs/T20_HSKIP.md.

## 2026-11-02 (night), T40 AE/AWB algorithm callbacks change exposure and white balance on the camera (#65, camera-tested on T40)

- **`IMP_ISP_SetAeAlgoFunc` / `IMP_ISP_SetAwbAlgoFunc` are device-verified and on by default** (`OPENIMP_ISP_ALGO_FUNC=0` switches them off: -1 / ENOTSUP). `apitest` case `customalgo_t40` got effect checks (`customalgo_ae_effect_t40`, `customalgo_awb_effect_t40`): the AE callback requests a short exposure (1/6 of the running one), then a 4x analog gain, then the starting exposure; the check reads the ISP state (`/proc/jz/isp/isp-m0`, the info the driver hands back) and the picture luma. The AWB callback returns strong red and then strong blue gains in day mode; the check reads the gains the ISP reports and the picture Cb/Cr. Camera result: luma 88 to 36 (short exposure), 133 (4x gain), 85 (start again); Cb/Cr 172/171 to 112/197 (red) and 214/141 (blue), gains followed exactly; after `DisableSensor` the normal AE (1800 lines, luma 120) and the automatic white balance are back.
- Needs the matching open-tx-isp module (same day): the custom AWB result no longer oopses (`tisp_awb_set_frz`), the AE result is committed through the open driver's sensor-apply path, held AWB gains keep serving the library, and the info handed to the libraries carries the live exposure and gains.
- Regression with the module (runtime swap): apitest all areas PASS 398 / FAIL 0, `t40dev` 0 FAIL, 6 of 6 reload cycles, 0 oopses; QEMU T40 (`slub_debug`): 0 oops, slab validate clean (the two effect checks cannot pass in QEMU, there is no picture response).

## 2026-11-02, T40 AE/AWB algorithm callbacks run on the camera (#65, camera-tested on T40; callbacks reached, picture effect not verified)

- `IMP_ISP_SetAeAlgoFunc` / `IMP_ISP_SetAwbAlgoFunc` (gated: -1 / ENOTSUP unless `OPENIMP_ISP_ALGO_FUNC=1`): with the open-tx-isp fixes of the same day (AE/AWB worker wake-ups, AE open before the first stream, close freeing once) the case `customalgo_t40` passes on the T40 camera: both libraries opened once, `handle()` about 190 calls each with delivered statistics, `notify` reached after `SetSensorFPS`, close once. Only the two AE/AWB entries of the `customalgo_t40` case ran with a registered library; a library whose result changes exposure or white balance was not verified, so the API cells stay as they are until the gate is opened by default.
- libimp sends the close ioctl once: when the worker does not leave on the cancel, the join's close is the only one (a second close freed the driver's info block twice under `slub_debug`). `apitest`: `customalgo_gate_t40` is N/A when the env var opens the gate; `run_swap.sh` swaps all loaded sensor modules, the case no longer needs non-zero init limits on a freshly loaded module.
- Camera regression with the branch module (runtime swap, test libimp): apitest all areas PASS 398 / FAIL 0, t40dev 0 FAIL, 6/6 reload cycles, 0 oops.

## 2026-11-02, T10/T20 `SetChnHSkip` reference structures and BlackEnhance (#3, #5)

- The vendor libimp 3.12.0 (T10 and T20 are one binary) codes a *reference structure* for the skip types N2X, N4X, HN1 and H1M: a long-term base layer (reference list modification and MMCO in the slice header) and drop-able pictures between the base pictures; for N1X and H1M the same-scene count sets an IDR period of that many GOPs, and the T20 rate controller can turn a GOP boundary into an IDR (scene cut). OpenIMP's own H.264 path now does the same (`src/hskip`, `docs/T20_HSKIP.md`): picture decision, three reference buffers for the types above N2X, SPS with two reference frames, a new `SetChnHSkip` value starts with the next IDR like the vendor. `SetChnHSkipBlackEnhance` codes the drop-able pictures as all-skip slices at QP 0 (21 bytes at 640x360, byte-identical to the vendor's software writer), and the rate controller gets the vendor's HSkip class byte.
- Proof: unicorn oracle of the vendor code (`tools/hskip_oracle.py`: 76 scenarios x 240 pictures, 635 black slices; `tools/rc_t20_oracle.py --hskip`: 28 scenarios x 200 pictures of the OEM controller on T20 and on T10, 0 differences), QEMU T10/T20 first, then the cameras: for 18 (T20) and 14 (T10) scenarios of 130 pictures the per-picture structure (NAL ref_idc, slice type, frame_num, list modification, MMCO, long_term_reference_flag) equals the vendor stack on the same camera (`tools/hskip_trace.py`, `apitest` case `hskip`), the streams decode clean in ffmpeg strict mode with a real picture, and still decode with the drop-able pictures removed.
- Not done (stay open on #3 / #5): `bEnableScenecut` (stored only), and `SetChangeRef`: the vendor codes a SMART picture of an H1M / long-IDR-period structure a second time with the previous frame as reference and keeps the smaller try; OpenIMP keeps the value and does not code that second try (SMART streams with these skip types differ from the vendor in size).

## 2026-11-01, documentation round 7 (camera proofs, analyses, board statuses)

- **Lifted-code audit (#189, camera runs of both defect classes):** T40 (image agg65m): apitest all areas PASS 398 / FAIL 0, area t40dev 82 / 0, 6 planned reload cycles, 0 oopses. T41 (image agg5y, 21 retyped functions plus 5 restored delay-slot statements): apitest areas sys,fs,enc,osd,ivs,isp,su PASS 266 / FAIL 3 (AF statistics and metrics and LDC answer "no handler": AF block off by default, no LDC), WDR on -> off toggle, SwitchBin day and night, 13 FrameSource cycles, 7 planned reload cycles, 0 oopses. The functions are not instrumented: "reached" means the code path ran clean, not that a register or picture effect was measured. Not exercised: the DVP leg of `tx_isp_vic_start`, suspend/resume, `api_ae_set_exprinfo_attr`, `tisp_msca_ch_curve_write`. The issue stays open for the untriaged classes.
- **T41 backlog (#77 #78 #181, closed):** `Get/SetInternalChnAttr` round trip on the camera (types 0..2 accepted, 3..5 answer EOPNOTSUPP because the open VIC path has no internal inputs); `EmuFrameSource_*` is not exported by the T41 vendor library (n.a., ENOTSUP here); `Encoder_SetFrameRelease` device-run; `Decoder_*` answers ENOTSUP like the missing vendor decoder.
- **T21 `SetChnRcTrigLevel` (#7):** the vendor 1.0.33 controller stores the pair but never reads it (host oracle: five different pairs on seven scenarios, identical picture types, QPs and sizes), so the stored-only behaviour equals the vendor; API cell cache -> no-op. T10/T20 controllers do use the pair.
- **T10/T20/T21 `Decoder_*`:** software decoder, camera-proven; not vendor-equal in speed (the vendor decodes on the VPU) and the picture is a heap buffer without physical address.
- **Analyses, host only, no code change:** #82 (T40 AE/AWB from the hardware statistics): AWB already follows the vendor flow; the AE decision still comes from a sparse sample of the finished frame. Keep or change needs a light box A/B (pending). #85 (T40 module size): the open `.text` is 765,612 bytes against 510,672; the analysis lists what is translated, lifted, literal and clean C, and a stepwise plan (the first three steps are expected to reach about 570-600 KB).
- **T30 (QEMU only, no camera):** #174, #175, #176 are fixed in open-tx-isp; the issues stay open until a T30 camera confirms them. **#170** (review leftovers) is closed.
- **Project board:** the status column has two new values: **Monitoring** (diagnostics are running, waiting for the next event, e.g. the reset issues #94 and #108) and **Waiting** (blocked from outside, e.g. on test hardware or a measurement setup such as the light box). Both stay open and count for the release gates; see the generated "Release gates" section of the feature matrix.

## 2026-11-01, `IMP_Decoder_*` on T10, T20 and T21 (#3, #5, #7)

- The vendor libimp of T10/T20 (3.9.0 and 3.12.0) and T21 (1.0.33) exports all eight `IMP_Decoder_*` calls (nm -D) and decodes JPEG on the VPU (`ijpegd`, `hwicodec_pf_jpegd_t20`, a decoder channel thread); OpenIMP answered ENOTSUP. The T10/T20/T21 builds now use the T23 channel code with the software decoder (`OPENIMP_DECODER_SW_ONLY`): channel 0, JPEG into YUV420P / NV12 / NV21, `nrKeepStream` pictures in flight, `SendStreamTimeout` waits for a free buffer, oversized (above `maxWidth` x `maxHeight`), progressive and damaged files return -1. The picture is a heap buffer (no physical address).
- The software decoder's IDCT is now integer (the XBurst cores of these SoCs have no FPU, and libimp does not link libm): output equals the libjpeg integer IDCT (`make -C tests/t23 check`, 0.02 mean error against ffmpeg on 4:2:0).
- Proof: host test `tests/t20/decoder_test.c`; QEMU T10/T20/T21 8/8; cameras T10, T20, T21 (library from /tmp, stock module and streamer restored): apitest case 8/8 PASS and a snapshot JPEG of the same camera decoded to NV12 matches a host decode of the same file (Y PSNR 68.8 to 69.5 dB, SSIM 0.9999, max difference 1 level; about 2 s for 1080p on T20/T21, 1 s for 720p on T10).

## 2026-11-01, T10/T20/T21 encoder backlog (#3, #5, #7)

- `IMP_Encoder_Get/SetChnCrop` is camera-proven on T10, T20 and T21 (SPS visible size follows the window, stream decodes clean and shows a real picture); apitest encoder area and pad-frame case 0 FAIL on all three. The names that only the 3.9.0 vendor library exports are marked n.a. for T10/T20. Open: stored-only encoder options, `SetChnRcTrigLevel` effect on T21.

## 2026-11-01, apitest: T41 full-order failures triaged (#189)

- **apitest only, no library change:** the 5 T41 default-case FAILs that showed up only in the full case order were wrong case expectations, not libimp or driver faults. `IMP_MemPool_InitPool` / `GetById`: T40/T41 have the vendor form (`InitPool(id, size, name)`, `GetById(id)` returns the pool or NULL, no output struct); the case used the T10..T31 descriptor form. `IMP_ISP_Tuning_GetOSDBlock` / `SetOSDBlock`: the block of the created ISP OSD picture was read with pinum 0; the handle is 0 only when no earlier case created an ISP OSD region, in the full order it is 1 (now `pinum = handle`). `IMP_ISP_Tuning_SetDrawBlock` (and the two other draw cases): alpha 7 is outside the vendor range 0..4 and reads back black (#188), which only passed on bright scenes; now alpha 4, and "disabled" accepts a scene that is still settling (back within 20 luma or 75 % of the way). New explicit cases `wdrtoggle_t41` (real WDR on/off switch with read back) and `fscycle` (13 FrameSource disable/enable cycles, `APITEST_FSCYCLES`). Device (T41 sc5336, normal image with open-tx-isp a84b6be2): default cases 164 pass / 0 fail (was 159/5), areas sys,fs,enc,osd,ivs,isp,su 266/3 unchanged (the 3 = AF/LDC no-handler returns).

## 2026-11-01, ISP bypass feasibility (#125, #129)

- **T10/T20 `IMP_ISP_Tuning_SetISPBypass` (#129):** the call now does what the OEM 3.9.0/3.12.0 library does (one binary for both SoCs, `IMP_ISP_Tuning_SetISPBypass` at 0x5a334): a single `S_CTRL` 0x98e907 with the mode on the tuning fd, 0 on success, -1 on failure; no link rebuild. `ENABLE` and `DISABLE` both reach the driver (it switches the ISP core: processing_bypass_mode 2, raw_bypass 1, DS1 DMA source 1). Before, `ENABLE` was a no-op and `DISABLE` returned -1 without touching the driver. Camera proof (T10 and T20 test cameras, new library in a separate process, timps stopped): runtime ENABLE -> DISABLE -> ENABLE, ISP top registers read back 0/0/0 -> 1/1/2 -> 0/0/0, the picture goes dark and flat in the bypass (SSIM 0.37 / 0.20 vs processed) and returns (SSIM 0.98 / 0.99 vs before), RTSP decode clean, snapshot 200, 0 oopses. Not built: a FrameSource created while the bypass is on (it has to ask for the raw format); use the call at run time and finish with `ENABLE`. Host test `tests/t20/bypass_test.c`. T21/T23/T31 keep the refusal (no usable vendor reference, no bypass graph in the open drivers).
- **T41 `IMP_ISP_SetISPBypass` (#125):** a failing driver ioctl now returns -4095 like the vendor 1.2.6 library (0x47418) instead of -1; the open driver still refuses the real bypass until its VIC capture path exists. Host test only. Design notes for #125 and #129 (vendor call per SoC, what the open drivers lack): `audit/ISP_BYPASS_FEASIBILITY_1101.md` of the campaign.

## 2026-11-01, review leftovers (#170)

- **T31 `IMP_OSD_MemPoolRequest` (#170 L10):** the once-per-group request is now cleared by `IMP_System_Exit` like the stock OSD exit (the stock `DestroyGroup` does not clear it, so a second request in the same session still returns -1); host test in `tests/t30/osd_attach_test.c`. `DiscardInitialFrame` (L8), `SetChnCallback` (L9) and `GetNCUAlloc` (L11) were compared with the stock disassembly and match; comments only.

## 2026-11-01, documentation round 6 (T40 camera proofs)

- **T40 (Eufy test camera, images agg65i to agg65k, 6 planned reload cycles each, 0 oopses):** ISP privacy mask on channel 1 (#186) and ISP draw window / line (#187) fixed in the driver and device-proven (mask luma 160 -> 255 and back, draw window edge pixels read the exact colour). The draw alpha range is 0..4 as in the vendor (4 opaque); the earlier all-zero readings came from a test using alpha 7 (#188, no library or driver change; the apitest case now uses alpha 4). apitest all areas PASS 398 / FAIL 0, area t40dev 83 PASS / 0 FAIL.
- **T40 typed-pointer audit (#189):** a scanner for lifted code that steps typed pointers runs in the driver's `make check`; 12 of the 13 fixed functions were reached on the camera (11 FrameSource cycles, 6 reload cycles). Not proven: the 21-count wait loop of `__frame_channel_vb2_streamoff` and the DVP leg of `tx_isp_vic_start`; the issue stays open.
- **T40 AWB special-light fix (#163, closed):** camera A/B showed no regression; the camera scene never reached the changed branch, so the fix itself is proven by the host replay only.
- **T41:** AF statistics only with `t41_af=1` (default off, #179); the ADC calls (#80) pass on a test image with the kernel RTC/SADC drivers enabled, not on the stock image. **T30 (QEMU only):** second-channel start oops fixed (#175).
- **Feature matrix:** no symbol moved; cell texts of the rows above carry the evidence, API cell `ISP_Get/SetDrawAttr` (T40) err -> dev (see docs/status/history.md).

## 2026-11-01, documentation round 5 (driver fixes with device or QEMU evidence)

- **T40 (Eufy test camera, 6 planned reload cycles per image, 0 oopses):** the pipeline-top block is zeroed at ISP open like the vendor (#150); the ISP/CSI clock teardown at rmmod follows the vendor, `div_isp`, `gate_isp` and `gate_csi` read 0 after every unload (#152); WDR-enable/LCE arrays (#182), `tisp_g_draw_attr` NULL dereference (#183) and the AWB close with the algorithm callbacks on (#173) no longer oops. apitest all areas PASS 398 / FAIL 0 / N/A 24. Still open: ISP privacy mask on channel 1 (#186) and ISP draw window (#187).
- **T40 host evidence (#163):** the bit-exact chains, the ADR alignment trap and the AWB replay run in the driver's `make check` on generated inputs. The replay found and fixed a C bug in the special-light cluster test; the camera A/B of that fix is pending.
- **T41 (camera .34):** `GetAfStatistics` / `GetAFMetricesInfo` exist behind the module parameter `t41_af=1` (#179, device-tested: 180/180 reads in 30 min, 6 reload cycles); the default is off and the calls answer EPERM as before. `SetTmoFaceae` (#180) and `GetInternalChnAttr` (#181, set side QEMU only) are served. The RTC/ADC calls (#80) ran on a test image with extra kernel options only.
- **T30 (QEMU only, no camera):** `GetSensorRegister` oops (#174), oops at the start of a second FrameSource channel (#175) and the ISP init interrupt window (#176) fixed; the issues stay open until a T30 camera confirms them.
- **Feature matrix:** no symbol moved; the *As of* line is 2026-11-01 and the cell texts of the rows above carry the evidence (see docs/status/history.md).

## 2026-11-01, T21 encoder sleeps on the frame-ready event (#151)

The T21 encoder polled the capture queue every millisecond. It now takes the VBM ready sequence before each pull and sleeps until the capture worker publishes a frame, as on T31, T23 and T10/T20: the guard covers T21, both queue-publish sites notify, and StopRecvPic wakes a waiting reader. The T21 open driver wakes its frame-channel waiters when a buffer is done (`wake_up` on the channel wait queue at frame completion, `poll` and DQBUF wait on the done list), so the encoder wait only moves behind the capture worker's own wake-up. Camera run on cam-D (T21, jxf23, 1920x1080 25 fps, timps with one RTSP client, 60 s windows, libimp swapped by bind mount, same image and settings, four runs each, interleaved): encoder (video) thread 3.50 / 2.58 / 2.99 / 3.63% of all CPU before, 2.74 / 2.96 / 2.91 / 2.88% after (mean 3.18 to 2.87, about -10%, and a narrower spread); streamer total 11.9% before, 12.0% after (no measurable change; the gain on this single-core camera is within the run-to-run noise, unlike T10 and T23). Streams decode clean (about 2000 frames per 80 s, ffmpeg strict, no errors), timestamps strictly increasing, picture real (SSIM before vs after equals before vs before on the live scene), snapshot 200, 0 oopses, stock libimp restored with no stacked mounts. Two of the eight runs show bursts of dropped frames; both coincide with an external snapshot client (jpeg thread active) and not with the change.

## 2026-11-01, T23 encoder sleeps on the frame-ready event (#151)

The T23 encoder polled the capture queue every millisecond (the helper functions and nothing else were compiled in). It now takes the VBM ready sequence before each pull and sleeps until the capture worker publishes a frame, as on T31 and T10/T20: the guard covers T23, both queue-publish sites notify, and StopRecvPic wakes a waiting reader. The T23 frame channel already wakes the capture worker on the driver's frame-done event (`poll_wait` on the done queue, DQBUF waits on it), the encoder wait only moves behind that. Camera run on a T23 test camera (sc1a4t, 1280x720 15 fps, timps with one RTSP client, 60 s window, libimp swapped by bind mount, same image and stream settings, two runs each): encoder (video) thread 3.40% / 3.41% of all CPU before, 1.79% / 1.78% after (about 48% less); whole streamer 8.54% / 8.64% before, 6.63% / 7.02% after. Streams decode without errors (1200 frames, 15 fps), the picture is real (SSIM 0.98 between a before and an after capture, the same as between two before captures), snapshot 200, 0 oopses, no flash, no audio test. T21 keeps the poll (not changed, not measured).

## 2026-11-01, T40 ISP draw colour (#188): no driver fault, the test used an out-of-range alpha

- **Finding:** the `ISP_Get/SetDrawAttr` case drew with `wind_alpha` 7 and read Y/U/V 0/0/0 for every colour. The registers were right (colour word, line width and alpha as in the OEM `Tzn_Msca_Shd_line_chx`; read back with devmem), the drawn window geometry was right; the draw alpha is documented as 0..4 (vendor sample: smaller = more transparent, 4 = opaque). The hardware blends 1..3 and writes zeros for 5..7. With alpha 4 the top-edge rows read exactly the library's YUV for green, red, blue and white (device-tested on the Eufy T40).
- **apitest:** the case draws a green and then a red window with alpha 4, requires the exact YUV of each, that the two differ, and that disabling restores the picture. No library or driver change (the vendor library does not clamp the alpha either).

## 2026-11-01, T40 apitest area t40dev measured fallback frames (#185)

- **Cause of most of the 19 FAIL lines:** the area enabled only the scaled channel 1. On the T40 that channel is fed from the ISP main output, so without channel 0 the driver delivers no picture and libimp hands out its "no sensor signal" fallback frames after 2 s (constant mean Y 143.3 to 143.5, no control has any effect on them, encoder rate stuck at 59 kbps). The area now creates and enables channel 0 beside channel 1 (`T40DEV_CH0=0` restores the old setup for an A/B check); Brightness, Contrast, Saturation, Hue, flip, AWB attribute, bit-rate and RC-mode lines, the AE target/cap lines and the polling line are real measurements now.
- **Case errors fixed:** `System_GetBindbyDest` expects the OSD group as the source when the OSD is attached (FS -> OSD -> ENC); `W44_Telemetry` sets `size` before the call (the ABI requires it, `-1` otherwise); the SEI scan looks at every NAL of an IDR pack, not only the first; `W9_LSC_bypass` and `W14_ADR_bypass` report SKIP when the risky mask bit is clear instead of FAIL with the `-77` marker; Saturation and Hue report SKIP on a grey scene (night mode).
- **Split into their own issues:** ISP privacy mask without visible effect on channel 1 (#186), ISP draw window never drawn (#187).
- **Device proof (Eufy T40, image agg65e = open-tx-isp next 32f4ccfe with #152 and #183, OpenIMP 688f458b):** apitest all areas PASS 398 / FAIL 0 / N/A 24; `t40dev` only the two lines of #186 and #187 FAIL, 0 oopses, no extra module reference. Image agg65d was built from a branch without the #183 fix (the draw-attribute oops was back there), agg65e replaces it.

## 2026-11-01, T10/T20 encoder sleeps on the frame-ready event (#151)

The T10/T20 encoder polled the capture queue every millisecond. It now takes the VBM ready sequence before each pull and sleeps until the capture worker publishes a frame, as T31 does (the worker itself wakes on the driver's frame event: the T10/T20 frame channel is a vb2 queue and `select()` returns when a frame is done). T23 and T21 follow in the entries above. Camera run on cam-E (T10, jxh42, 1280x720 25 fps, timps with one RTSP client, 60 s window, libimp swapped by bind mount, same image and stream settings, two runs each): encoder thread 5.13% / 4.84% of all CPU before, 3.53% / 3.50% after (about 30% less); whole streamer 14.25% / 13.36% before, 12.70% / 12.60% after. Streams decode without errors (2000 frames in 80 s, 25 fps), the picture is intact (SSIM 0.95 between a before and an after capture of the live scene), snapshot 200, 0 oopses, no flash.

## 2026-11-01, T41 backlog cells proven on the camera (#75, #76, #79, #80)

Open-tx-isp next (#171, #180, #181) now serves the T41 controls that the OpenIMP pass-throughs of `claude/t41-backlog` call. Camera run (T41 sc5336 test camera, image agg5u, libimp of the image, apitest cases `t41isp`, `rgnisp`, `drawblock`, no risky calls, 0 oopses, snapshot ok after the run): `Get/SetCsccrMode` (round trip), `Get/SetFaceAeWeiget` (round trip), `GetFaceAeLuma`, `GetAEFlickerFlag`, `GetAeBv`, `SetTmoFaceae` (round trip, registers read back equal), `Get/SetDrawBlock` (24 px frame: top-edge luma 165 to 0 and back), `OSD_Get/SetRgnAttr_ISP` (line/rect draws the same frame, cover block reads back). Still open: `GetAFMetricesInfo` / `GetAfStatistics` (open-tx-isp #179: AF block is off by design), LDC / GPIO / WDR_OPEN / StartNightMode (the OEM 1.2.5 module has no handler; the calls are risky and are not run on a camera), `Set/GetInternalChnAttr` set round trip (risky), `SU_Base_SetWkupMode` (the test board has no /dev/rtc0; host test only), `SU_ADC_Init` (no ADC node on the board).

## 2026-11-01, T40 apitest expectations (#184, noticed in #183)

- **apitest cases corrected (#184):** `tisp_node_par_get/set` report N/A with the reason when the module answers `-EFAULT` (release module, the `/dev/tisp` parameter commands are debug-only since #164) and stay a full test on a debug module; `IMP_ISP_Tuning_SetModuleControl` takes the gamma-bypass verdict from the early window (~250 ms) like the SetGammaAttr case, because the closed-loop AE compensates the bypass within a second (camera: Y 111.7 -> 92.0 early, 116.8 settled).

  Device proof (Eufy T40, image agg65c): apitest all areas PASS 398 / FAIL 0 / N/A 24 (the three FAILs of agg65b are gone); modctl case Y 111.7 -> 92.0 after 250 ms; tisp_node_par_get/set N/A on the release module.
- **Found, tracked in open-tx-isp (#183):** the extra `tx_isp_t40` reference after the `t40dev` area comes from an oops in `tisp_g_draw_attr` (IMP_ISP_GetDrawAttr), not from the library.

## 2026-11-01, T40 WDR-enable / LCE-tgain arrays in open-tx-isp (#182)

- **open-tx-isp #182 closed:** `tisp_dmsc/ydns/ysp_dn_params_refresh` and `tisp_lce_tgain_interp_strength` read the per-channel WDR enable / LCE tgain through the `isp_memopt` module parameter while the writers stored elsewhere (including stray stores 68 KB past a 4-byte object); readers and writers now meet on the OEM arrays, the refresh tails pass the per-channel gain. Camera (T40 test camera, image agg65b): full OTA, snapshot 200, 0 oopses, 6 of 6 module reload cycles. No feature cell changes.
- **Noticed, not fixed:** the apitest area `t40dev` leaves one extra reference on `tx_isp_t40` (module cannot be unloaded until the next boot; the other areas leave none).

## 2026-11-01, T40 AWB-close oops fixed in open-tx-isp (#173)

- **open-tx-isp #173 closed:** `DisableSensor` with the algorithm callbacks on no longer oopses `tx_isp_t40` (the AWB close indexed an absolute address instead of the AWB enable array; fix in open-tx-isp next, c639fb86 and a7538b66). Camera proof on the T40 test camera (image agg65a, `OPENIMP_ISP_ALGO_FUNC=1`): apitest PASS 285 / FAIL 0, 6 of 6 module reload cycles, 0 oopses.
- **#65 unchanged:** the callbacks stay off by default and the `customalgo_t40` case has not run on a camera, so the API cells stay `err`. Docs and code comments that named the oops as the blocker now say the driver is fixed and only the camera run of the callbacks is missing.

## 2026-11-01, T30 audit (#14-#27, branch claude/t30-audit)

No T30 camera: static audit of the vendor T30 1.0.5 libimp (disassembly) against OpenIMP, plus an API run in the QEMU T30 machine (sc4236 model, open-tx-isp tx-isp-t30 + audio.ko, 2026-10-31/11-01; apitest areas and 56 cases, a few extra runs). Cells move only to `qemu` (API round trip in QEMU), `err` (the call fails in QEMU), `aud` (static) or `host`; no camera evidence, so no streamer-function cell is `dev` and no issue of the list is closed.

- **ISP / AE (#23):** `IMP_ISP_Tuning_AE_Get/SetROI` sent control 0x8000035 through a pointer, which the T30 driver refuses; the vendor 1.0.5 (0xd9688/0xd978c) sends 0x8000024 with the four ROI bytes by value and rejects a box with b[3] >= b[1] or b[2] >= b[0]. Fixed, host test `isp_gap_test_t30`. QEMU T30: Set reaches the driver, Get returns -1 (the open T30 driver has no get case for 0x8000024, an open-tx-isp gap): cell err. `ISP_Tuning_SetExpr`: pointer and control equal to the vendor, not run (cell aud).
- **ISP (#22):** `SetISPProcess` took a pointer on the tuning ioctl (mode 0 was refused as NULL) and `SetFWFreeze` used the tuning ioctl; the vendor 1.0.5 (0xdb8f8/0xdba84) sends the mode as the S_CTRL value of 0x8000164/0x8000165 and answers an unchanged mode from a cache, and `SetShading` (0xdb9b0) sends 0x8000166 the same way. Fixed for T30 (host test `isp_gap_test_t30`); QEMU T30: `SetISPProcess` reaches the driver (cell qemu), `SetFWFreeze` is rejected by the driver (cell err). `GetSceneMode`: refused by design (err). `SetISPLDC` stays missing (vendor-exported); `SetTemperDnsCtl` stays cache.
- **FrameSource (#25):** `IMP_FrameSource_GetFrame` did not wait in the T30 build; the vendor 1.0.5 (0xe4118) waits up to 2 s on the depth list like T20/T21/T23/T31, so a caller without a retry loop failed at once. Fixed; QEMU T30: the apitest case failed ("0 frames") before and passes now (16 frames, 19 fps). New host tests `fs_lifecycle_test_t30` and `ivs_framework_test_t30` (both run in `make check`). `Get/SetDelay`, `Get/SetMaxDelay`, `Get/SetFrameDepth`, `ReleaseFrame`: QEMU PASS; `SetSource`: bounds equal to the vendor, static only (cell aud).
- **FrameSource (#17):** the eight streamer functions (`CreateChn`, `DestroyChn`, `EnableChn`, `DisableChn`, `Get/SetChnAttr`, `Get/SetChnFifoAttr`, `GetTimedFrame`, `SnapFrame`) pass the apitest in QEMU T30 (two channels, 2304x1536 and 640x360, frames flow, disable/enable cycle) and the new host test. Finding: the 1.0.5 vendor libimp sends the format descriptor with 0xc05c56c3/0x405c56c4 (0x5c bytes), OpenIMP with 0xc04c56c3/0x404c56c4 (0x4c bytes) as the open T30 driver and the stock device trace expect; the difference is not explained here.
- **AE / AWB / AF (#15):** QEMU T30 reaches the open driver with the vendor control ids for ten of the eleven functions, but the getters are not served: `GetWB`, `GetExpr`, `GetWB_Statis`, `Get/SetAeHist`, `AwbHist`, `AfHist`, `AeWeight`, `AwbWeight`, `Awb_Get/SetRgbCoefft`, `Get/SetAeComp` return -1 from the tuning handler of the lifted T30 driver (cells err; open-tx-isp gaps, OpenIMP matches the vendor transport). `GetAFMetrices` differs from the vendor (the vendor returns a value from its own AF thread without an ioctl), not run.
- **Audio backlog (#26):** `AI_Enable/DisableAec`, `AO_Enable/Disable/EnableChn/DisableChn`, `Get/SetPubAttr`, `Get/SetVol`, `Get/SetGain`, `SendFrame`, `ClearChnBuf`, `FlushChnBuf` pass in QEMU T30 with audio.ko (AO muted, silence, no speaker in the model). Cells qemu; no codec or microphone hardware was exercised.
- **System / sysutils / log (#21):** `System_Init/Exit/Bind/UnBind/GetVersion/GetCPUInfo/GetTimeStamp/RebaseTimeStamp/ReadReg32/WriteReg32/GetBindbyDest` and `SU_Base_GetVersion` pass in QEMU T30 (`ReadReg32`/`WriteReg32` round trip on the UART scratch register). `System_GetVersion` returns a neutral OpenIMP string by design. The `SU_*` functions are the unchanged vendor libsysutils 1.0.5 (OpenIMP ships no `SU_` code for T30).
- **OSD (#18):** all 14 functions pass the apitest in QEMU T30 (group/region create, attribute get/set, show, update data, timestamp attribute, pool size, start/stop); the OSD is a userspace implementation shared with T21/T31. The picture effect is not checked (no frames to the encoder in the model). `osd_attach_test_t30` covers `AttachToGroup`.
- **IVS (#19):** all 17 functions pass the apitest in QEMU T30 and the new host test `ivs_framework_test_t30` (the T31 IVS framework and motion interface built for T30; 500 frames back to back, no drops). Detection quality on a real scene is not covered.
- **Encoder backlog (#24):** `Get/SetH265TransCfg` now behave as the vendor 1.0.5 (0xc5688/0xc585c) on a channel that is not H.265 (T30 has none): channel and pointer checks only, nothing stored, the getter clears the struct; before, OpenIMP range-checked and stored the values (host test `p2_gap_test_t30`). QEMU T30 apitest: 9 functions pass; `Get/SetChangeRef`, `Get/SetChnROI` and `Get/SetSuperFrameCfg` fail on documented T30 limits (ROI registers unknown, no super-frame re-encode in the GOP controller, ChangeRef not read back); cells err. Still not exported although the vendor 1.0.5 has them: `Decoder_*` (8), `Get/SetChnCrop`, `PadFrame`, `SetPadFrameAttr` (cells miss). `SetChnResizeMode` now accepts any value like the vendor (host test).
- **Encoder (#16):** the 21 streamer functions (create/destroy/register channel and group, start/stop receive, `GetStream`/`PollingStream`/`ReleaseStream`, `RequestIDR`, `FlushStream`, `Get/SetChnAttrRcMode`, `Get/SetChnFrmRate`, `GetChnAttr`, `Query`, `Get/SetMaxStreamCnt`, `GetChnEncType`, `GetFd`, `SetGOPSize`) pass the apitest in QEMU T30 (cell qemu). The model has no VPU frames, so the H.264 stream content is not covered by this run; the earlier host tests (Helix descriptor, bit-exact slice order against the OEM routine) are unchanged.
- **Encoder (#16):** the 21 streamer functions (create/destroy/register channel and group, start/stop receive, `GetStream`/`PollingStream`/`ReleaseStream`, `RequestIDR`, `FlushStream`, `Get/SetChnAttrRcMode`, `Get/SetChnFrmRate`, `GetChnAttr`, `Query`, `Get/SetMaxStreamCnt`, `GetChnEncType`, `GetFd`, `SetGOPSize`) pass the apitest in QEMU T30 (cell qemu): `GetStream` returns an H.264 stream (3 packs, 335 bytes) and `RequestIDR` yields an IDR within one frame. A 335-byte stream says the model feeds trivial pictures, so picture and bit-rate quality are not covered; the host tests (Helix descriptor, slice order against the OEM routine) are unchanged.
- **Audio (#20):** the 33 streamer functions pass in QEMU T30 with audio.ko: AI enable/channel/frame stream (25 of 25 frames, silence), volume, gain, mute, NS, HPF, AGC, AEC reference frame, AENC/ADEC (G.711A round trip), and the AO set (muted, zero-filled frames). No codec or speaker hardware is involved in the model, so levels and sound are not covered.
- **System backlog (#27):** `Phys_to_Virt`, `Virt_to_Phys`, `Sp_Alloc`, `SU_Base_GetDevID/GetModelNumber/Raw2SUTime/SUTime2Raw` and `SU_Key_Open/CloseEvent` pass in QEMU T30. The other `SU_*` functions (ADC, RTC alarm and time, CIPHER, key events, LED, reboot, suspend) are the unchanged vendor libsysutils 1.0.5 and need hardware or are unsafe to run: cell aud, as on T21/T23/T31. `SU_Battery_*` are declared in the header but not exported by the vendor library: n.a.
- **ISP tuning (#14):** the apitest now builds and runs for T30 (picture cases still absent) and the 41 functions were run in QEMU T30 and compared with the 1.0.5 disassembly by call site (transport and control id). Lifecycle (`Open`, `AddSensor`, `EnableSensor`, `EnableTuning` and back), `Contrast`, `Sharpness`, `SensorFPS` get, `TotalGain`, `SetBrightness`, movestate and `SetVideoDrop` pass; the open T30 driver does not serve many getters and controls (`AntiFlicker` get, `Hflip`/`Vflip`, `Gamma`, `IntegrationTime`, `EVAttr`, night mode, `HiLightDepress`, `SetSensorFPS`, the strengths) and `GetSensorRegister` oopses it: those cells are err and listed in docs/T30_STATUS.md. OpenIMP differs from the vendor in `Set/GetSinterStrength`, `SetTemperStrength` (vendor scales driver tables 109/132, OpenIMP sends other controls), `SetISPBypass` (stub), `WaitFrame` (stub, wiring it returns -1 against the driver), `Get/SetRawDRC` (stored in userspace) and the Hflip/Vflip temper detour; `Scene`/`Colorfx` are refused by design.

## 2026-11-01, state of `next` (backlog rounds merged, T40 agg63 camera proof)

- **Backlogs closed:** T31 (#28 #29 #30; 14 + 4 T31 cells `dev` on the two T31L cameras), T23 (#9 #10 #11 #12 #13 #172: software JPEG decoder in the default build, SwitchBin, WB algorithm, OSD alloc; Jooan T23 camera PASS 68), T41 (#77 #78; T41 camera PASS 44 / FAIL 0 with 17 N/A), T40 (#66 to #70), T10/T20/T21 (#4 #6 #8; T20 127/0, T10 127/0, T21 133/0 after the libimp swap).
- **Driver hardening closed:** #158 #160 #161 #164 #165 #166. The `/dev/tisp` parameter node exists only with `TX_ISP_T40_TISP_PAR=y`.
- **T40 camera proof (image agg63, includes #161):** apitest all areas PASS 279, 6 of 6 reload cycles of our module clean, dark IR scene stable. #141 (OSD pictures on T40) is proven on the camera. #148 (dark-scene livelock in the libimp gap flush) is fixed in OpenIMP (stale bound). #149: encoder CPU at vendor level (0.71 % against 0.77 / 0.84 %).
- **Other closed:** #2 (AE limit cycle), #88, #92, #93, #96 (full tmpfs), #104 (Linux 7.2: the T31L boots 7.2.11 with the IRQ balance fix), #131 (no DMIC hardware), #133.
- **New doc:** [docs/RESET_DIAGNOSTICS.md](docs/RESET_DIAGNOSTICS.md): how to capture the cause of an unplanned reboot (RTC scratch record on the T31, reset-PC sled on the T23, SD ring log).
- **Still open:** RC3 blockers #84 (waits for the light measurement box), #94 and #108 (unexplained resets). #65 (T40 AE/AWB algorithm callbacks: off by default; driver bug #173 is fixed, `customalgo_t40` has not run on a camera yet). #3 #5 #7 (T10/T20/T21 stored-only stubs). #171 (T41 ISP routes: merged and camera-proven, see the later entries). T30 audits run separately; the T32/T33 backlog is open. A fleet image needs the next modules (T23 ISP OSD pictures, SU CIPHER in libimp).

## 2026-10-31, T10/T20/T21 backlog (#3 to #8, branch claude/t2x-backlog)

- **System (#4, #6, #8):** the vendor `IMP_Sp_Alloc` of the T10/T20 3.12.0 and T21 1.0.33 libraries was disassembled: it is not an allocation of a size but the allocator's reference-count increment (descriptor plus the physical address of a live buffer; `IMP_Free` releases the last reference). OpenIMP had the T23 call form on these SoCs; it now has the vendor form. `Phys_to_Virt` and `Virt_to_Phys` forward to the allocator's linear region conversion in the vendor library; OpenIMP converts inside live buffers (documented difference). apitest case `alloc` checks the reference count.
- **Encoder (#3, #5, #7):** `Encoder_Get/SetChnCrop` on T10/T20/T21 (vendor: multiples of 16, H.264 only, uncreated channel answers 0): the window becomes the SPS frame cropping of the Helix encoder, applied between two pictures; apitest `enc` checks the SPS size with the crop on and off. `IMP_Decoder_*` (8 functions) is exported on T10/T20/T21 as ENOTSUP stub (no JPEG decoder in OpenIMP there; case `decoder_t2x`). The four 3.9.0-only name pairs (`ChnH264Demask`, `ChnH264Denoise`, `ChnH264FrmUsedMode`, `ChnRcAttr`) are taken off the T10/T20 vendor lists: the 3.12.0 library dropped or renamed them.
- **Data:** PadFrame/SetPadFrameAttr cells on T10/T20/T21 `dev` (camera runs of 2026-10-29), `SetChnRcTrigLevel` `rt` (T10/T20) and `cache` (T21: the pair is kept, the T21 rate control has no trigger yet). Crop, Decoder and Sp_Alloc cells follow the camera runs.
- **Camera evidence (agg63 merge):** apitest areas enc,cases with CASES alloc,decoder_t2x on the three test cameras after the libimp swap: T20 Wyze 127/0, T10 Secuplug 127/0, T21 PC420 133/0 (N/A 1); the API cells with PASS moved to `dev` (cells that record a known stub keep their value).

## 2026-10-31, T41 backlog (#75-#80, branch claude/t41-backlog)

- **FrameSource (#78):** `IMP_EmuFrameSource_*` is not exported by the T41 1.2.5 vendor library (nm); the names come from older vendor builds. OpenIMP keeps its five exports as a documented ENOTSUP (-1, errno ENOTSUP, no state change). New apitest case `emu` (5/5 PASS in QEMU T41), API reference entry, evidence action `enotsup`. No matrix cell changed (no camera run yet).
- **OSD (#79):** `IMP_OSD_SetRgnAttr_ISP` / `GetRgnAttr_ISP` and `IMP_ISP_Tuning_Set/GetDrawBlock` are new on T41, rebuilt from the T41 1.2.5 vendor library (draw block at +48, OSD format at +80, cover block at +100 of the region attribute; vendor return codes 0x80020001 / 0x80020010 / 0x80020040; DrawBlock control 0x080000a0, 32 bytes). `IMP_OSD_GetRegionLuma` is not exported by any T41 library we hold (n.a.). New apitest case `rgnisp`. QEMU T41: cover type, error codes and show flag as the vendor, 0 oopses; line/rect and Get report N/A because the open-tx-isp T41 module answers EPERM to control 0x080000a0 ("not implemented"): that module route is the open part. Matrix: DrawBlock and RgnAttr_ISP cells `err` (was `miss`).
- **System (#80):** `SU_Battery_*` (4 functions) are n.a. on T41: neither the 1.2.5 libsysutils nor the board's neo-sysutils exports them. `SU_ADC_Init` is exported by both libraries (not by libimp); the matrix row was a stale `miss`, now `aud`. `SU_Base_SetWkupMode`: the board's neo-sysutils writes the mode into `/sys/power/state` (the kernel refuses, the call never worked); `libimp.so` now exports the vendor request (`/dev/rtc0`, ioctl 0x80045209, `{0, 0x2c, 0xd2ad2d28 / 0xd2ad2d29}`), a mode other than 1/2 is refused before the device is touched. Host test with fake open/ioctl (`make -f tests/core/su_base_wkup.mk check`); QEMU T41: the bad-mode refusal is answered by `libimp.so`; modes 1/2 are never sent by apitest (they arm an RTC wake-up). Cell `host`.
- **Encoder (#77):** the 9 rows were checked against the T41 vendor libraries (1.2.5 libimp, 1.2.6 library, name lists). `Encoder_Get/SetChnSeiAttr`, `Get/SetFisheyeEnableStatus` and `SetAvpuBsSize` do not exist for T41 (the T41 name is `SetAvpuBsShare`, already exported): n.a. `IMP_Encoder_SetFrameRelease` is new on T41 (stores the ratio, vendor codes 0x80040001 for a channel of 8 or more and 0x80040008 for an uncreated one; apitest case `framerel` PASS in QEMU T41, frames flow). `IMP_Decoder_*` keeps its documented ENOTSUP exports (new case `dec`, 8/8 PASS in QEMU T41). The T41 ROI rows (`Encoder_Get/SetChnRoiAttr`, `SetChnMapRoi`) were stale `cache`: ROI works on the T41 cameras (CBR, VBR and, beyond the vendor, FixQP; docs/ROI.md), cells `dev` with `better`.
- **AE / AWB / AF (#76):** new T41 file `src/t40/openimp_t41_isp_extra.c`, rebuilt from the vendor libimp 1.2.5 / 1.2.6 disassembly with the vendor return codes (-4087 NULL, -4084 sensor 2 or more, -4091 tuning not enabled, -4090 module error). Pass-throughs: `Get/SetFaceAeWeiget` (control 0x8000036), `GetFaceAeLuma` (0x8000037), `GetAEFlickerFlag` (0x800003a), `GetAeBv` (0x80000b3, 1.2.6 only), `GetAFMetricesInfo` (0x8000034), `GetAfStatistics` (0x8000031, 0x1518 bytes). `SetAeAlgoFunc` / `SetAwbAlgoFunc` need a user callback thread fed by the ISP module: exported as a documented ENOTSUP. `AeConvergeStep` row was a stale `cache`: the round trip passed on the T41 test camera (2026-10-27), cell `rt`. New apitest case `t41isp`. QEMU T41: 0 oopses, frames flow; the open-tx-isp T41 module answers EPERM to all six controls ("not implemented"), so the calls return -4090 and the case reports N/A. Cells `err`; the open part is the module.
- **ISP tuning (#75):** the other pass-throughs of `openimp_t41_isp_extra.c`: `Get/SetCsccrMode` (control 0x8000097), `SetTmoFaceae` (0x8000062) and, on the ISP device, `StartNightMode` (ioctl 0xc004542e), `Get/SetInternalChnAttr` (0xc004542f / 0xc0045430), `LDC_INIT` / `LDC_Get/SetAttr` (0xc0045479 / 7a / 7b), `WDR_OPEN` (0xc004548d), `SET_GPIO_INIT_OR_FREE` / `SET_GPIO_STA` (0xc004548e / f; a refusal is returned, the vendor logs it and returns 0). `SetVicDoneCbFunc` and `RAW_RwControl` are documented ENOTSUP (callback thread / RAW exchange). `Get/SetDrawBlock` came with #79. QEMU T41 (case `t41isp`, the ISP-reprogramming calls with APITEST_RISKY=1): 0 oopses, frames flow, the NULL / sensor-2 codes match the vendor; the open-tx-isp T41 module has no handler for any of these (EPERM / ENOTTY), so the cells are `err` and the case reports N/A. The module routes are the open work (one issue per control group is proposed in the issue comments).
## 2026-10-31, T23 backlog (#9 to #13, branch claude/t23-backlog)

- **#10 AE / AWB / AF (`ISP_Tuning_SetWB_ALGO`, T23):** the libimp side was checked against the 1.3.0 disassembly (algorithm number passed to control `0x800000c`, sensor number below 4); the open driver stores 0 to 2 and has no picture effect (documented class d). New apitest case `wbalgo_t23` (algorithm 1, 2, 0 return 0, picture and frame flow unchanged): QEMU T23 PASS; the camera run is missing, so the matrix cell stays.
- **#9 ISP tuning (`ISP_Tuning_SwitchBin`, `ISP_Tuning_Get/SetDrawBlock`, T23):** the driver handlers are in open-tx-isp next since agg-38. The stock T23 driver refuses `SwitchBin` with enable 0 ("enable is 0", -EINVAL) and so does the open one, so there is no way back to a default bin; new case `switchbin_t23` switches to the bin file in use twice, checks both refusals (enable 0, a file that is no bin) and that the picture and frame flow stay: QEMU T23 PASS. `DrawBlock` keeps its case `ispdraw_t23`; the picture effect cannot be shown in QEMU. Camera runs are missing, so both cells stay.
- **#12 OSD (`OSD_Alloc`, `ISPOSD_Alloc`, ISP OSD calls, T23):** the two undocumented rmem helpers were checked against the 1.3.0 disassembly (stock: `IMP_Alloc` plus `memcpy`, always 0; OpenIMP additionally refuses a NULL output and a size of 0 or less) and got the case `osdalloc_t23` (content seen through `IMP_Phys_to_Virt` equals the source, address known to `IMP_Virt_to_Phys`): QEMU T23 PASS. `Get/SetOSDAttr`, `Get/SetOSDBlock`, `OSD_Init_ISP` and `OSD_UpdateRgnAttrData_ISP` already have cases and device evidence of 2026-10-27/29 (.30); the issue list was out of date for them. Camera run of the new case missing.
- **#11 Encoder (T23):** `IMP_Decoder_*` now works in the default build: without the OEM helper the JPEG is decoded by OpenIMP's own baseline decoder (`src/t23/openimp_t23_jpegdec.c`, 4:4:4 / 4:2:2 / 4:4:0 / 4:2:0 / gray, restart intervals; progressive files are refused, as is a picture above the channel size). Host test `tests/t23/jpegdec_check.py` against libjpeg and ffmpeg (4:2:0 and gray identical within 0.02 grey levels, 77 checks) and case `decoder_t23` (QEMU T23 PASS); the helper-based VPU decoder stays for `OPENIMP_T23_HELIX_BRIDGE=1`. `Encoder_SetMultiSectionMode` now refuses once the encoder module is initialised (1.3.0 disassembly). `PadFrame`, `SetPadFrameAttr`, `SetChnRcTrigLevel`, `SetRdBufShare` were already implemented to the 1.3.0 semantics; their cases pass in QEMU T23. Fisheye flag and frame-loss threshold stay stored-only (the closed vendor encoder reads them; documented). Camera runs missing, cells stay.
- **#13 System (T23):** `Phys_to_Virt`, `Virt_to_Phys`, `ISPOSD_Alloc` and the Pool/MemPool helpers already have the `alloc` and `osdalloc_t23` cases; QEMU T23 PASS (the older device run of 2026-10-27 .30 covers the Pool functions, the issue list was out of date for them). The stock `Virt_to_Phys` / `Phys_to_Virt` forward to a pointer in the allocator table and return 0 when it is unset; OpenIMP converts inside its own arena. Camera run for `Phys_to_Virt`, `Virt_to_Phys`, `ISPOSD_Alloc` missing, cells stay.

## 2026-10-31, T31 System backlog (#30, branch claude/t31-backlog-sys)

- **Pool and address helpers (14 functions of #30):** the T31 vendor library was disassembled. `PoolAlloc_Get_Attr`, `PoolAlloc_Set_Attr`, `PoolFlushCache`, `PoolPhys_to_Virt` and `PoolVirt_to_Phys` are empty stubs there (return 0, nothing stored or converted); OpenIMP implements them (extra, documented in the API reference). `IMP_Sp_Alloc` has a different call form on the T31 vendor library than the one OpenIMP uses (open question, no streamer calls it). The other functions take the vendor arguments.
- apitest case `alloc` now also checks `IMP_MemPool_Release`; it passes 44/0 in QEMU T31. Matrix cells stay `?` until the camera run on the T31L.
## 2026-10-31, night (T31 backlog #28, #29)

- **T31 API data:** `Encoder_ClearPoolId`, `FrameSource_ClearPoolId` and `FrameSource_SetFrameDepthCopyType` were already implemented to the 1.1.6 libimp semantics (pool tables dropped, argument unused; copy type only while the frame depth is 0, any non-zero value counts as 1); the 2026-10-29 device run (two T31 cameras) is now recorded as evidence. `FB_*` is not exported by the 1.1.6 libimp and not implemented (no display); `Encoder_Get/SetFisheyeEnableStatus` stays a stored flag like the vendor.
- **apitest:** the `fsset_t31` copy-type case now also runs the success path (frame depth reset to 0, then restored).

## 2026-10-31, night (T40 API backlog #65 to #70, branch claude/t40-backlog)

- **#67 `IMP_EmuFrameSource_*`: not applicable on T40.** The 1.3.1 SDK declares them in `imp_emu_framesource.h`, no shipped vendor library exports them (`nm -D`, `strings` on uclibc and glibc `libimp.so`, `libimp.a`); API evidence cell `miss` -> `n.a.`.
- **#68 `IMP_OSD_Init_ISP`:** already implemented (lazy table, idempotent, cannot fail; the vendor builds the pool and the 6x8 table and returns -1 if the allocation fails, documented as a difference). New apitest line `IMP_OSD_Init_ISP` (two calls in a row), QEMU T40 PASS; API cell `?` -> `qemu` (camera run missing).
- **#66 `IMP_Decoder_*`: not applicable on T40** (declared in `imp_decoder.h`, exported by no T40 1.3.1 / T41 1.2.5 / T41 1.2.6 vendor library; API cell `miss` -> `n.a.`). **`IMP_Encoder_Get/SetFisheyeEnableStatus`: matches the vendor, stays "cache" by design:** vendor 1.3.1 (0x6bd4c / 0x6be44) stores the flag in the channel record only while the channel is not created (-1 afterwards), channels 0..12; the only reader is `channel_encoder_init`, which hands it to the closed encoder code; OpenIMP is identical and has no consumer for it. Camera: apitest agg60 .184 PASS (refused on a created channel, idle round trip).
- **#65 `IMP_ISP_SetAeAlgoFunc` / `SetAwbAlgoFunc` (T40):** the vendor protocol is ported (disassembly of libimp 1.3.1: callback record kept by `Set*`, registered with ioctl 0xc004541d / 0xc0045425 at `EnableSensor`, worker threads with 0xc004541b/c and 0xc0045423/4, close at `DisableSensor`, `SetSensorFPS` notifies the AE library; new `src/t40/openimp_p1_algo.c`, apitest case `customalgo_t40`). **Switched off by default (-1 / ENOTSUP, `OPENIMP_ISP_ALGO_FUNC=1`) because the open driver is not ready:** QEMU T40 shows the registration working (the driver fills the init attribute) but no statistics reach `handle()`, and DisableSensor oopses `tx_isp_t40` in the AWB close (open-tx-isp `tisp_awb_algo_deinit_internal` indexes the absolute address 16712 instead of the AWB enable array; the AE twin uses `&ta_custom_en`). API cell `miss` -> `err`; never run the case on a camera before that fix.
- **#70 `SU_*` (26 functions) in libimp:** the vendor libsysutils is not shipped by the open stack, so the functions are ported into libimp (new `src/core/su_sysutils.c`, exported by the T40 map): RTC time / alarm / polling (`/dev/rtc0`), `Reboot` (sync + SIGTERM to init), `Suspend` (`/sys/power/state`), ADC (`/dev/ingenic_adc_aux_0..5`), key events (the `gpio_keys` input device and its sysfs lists), LED (sysfs GPIO). **Host-tested** (`tests/core/su_sysutils_test.c`, fake ioctl and temp files, part of `make check`), API cells `?` / `miss` -> `host`; the board of the camera decides whether the nodes exist, apitest reports N/A where they do not. `SU_CIPHER_*` (10 functions) answer -1 / ENOTSUP by design (no AES/DES kernel driver in the open stack, QEMU PASS; documented ENOTSUP). apitest `su` area resolves the SU_* of libimp first.
- **#69 Audio (12 functions): nothing left to implement.** The issue list is stale: all twelve (`AO_Enable/Disable/EnableChn/DisableChn/SendFrame/ClearChnBuf/FlushChnBuf`, `AO_Get/SetPubAttr/Vol/Gain`, `AI_Enable/DisableAec`) have API cell `rt` since the apitest run on the Eufy .184 (2026-10-27, `audit/rc3-cases/T40-audio`: muted, volume at the minimum, only zero-filled PCM, no sound). What stays unproven by the no-sound rule: audible output and the echo-cancellation effect.

## 2026-10-31, late evening (image agg60)

- **Device proof (Eufy T40XP, image agg60 = all #140 batches incl. 19 sensor/WDR ioctl sites and 2 AE sites):** apitest all areas plus 28 cases PASS 270 / FAIL 0, 0 oopses, 0 `unresolved jalr`; the lost-argument counters (`cnt_core_ops_init`, `cnt_sec_init`, `cnt_main_long_tgain_update`) stay 0; raw ioctl probe ok.
- **CPU, vendor-like (T40):** #133 closed (ISP busy 1.6 % against 1.4 % for the vendor); #149 closed (encoder load 0.71 % against 0.77 / 0.84 %; `t40_dqbuf_wait_ms` stays 0).
- **Closed with camera proof:** #92 (T31X `isp_memopt=1`, 5 days plus a 10 min check; the firmware pin to the RC3 tag is open), #93, #133, #139, #140, #149, #167, #168, #169.
- **Closed with evidence or decision:** #88 (not reproducible); #96 (the silent reset was a full tmpfs, reproduced on the T41 test camera; mitigations: save guard in the tools and the /tmp rule, a firmware size cap is prepared); #131 (no DMIC hardware: the T40/T41 flow runs in QEMU only, the T31 as API round trip only).
- **Open RC3 blockers (gate red, 3 blocking issues; streamer-function cells 1787 of 1787):** #84 (paused until the light measurement box exists), #94 and #108 (unexplained resets).
- **Reboot diagnostics:** the RTC scratch register keeps the last panic record across a reset (proven on the T31L and the T31X); the T23 has no store that survives a reset (load sampler and reset-cause register only); DRAM pstore is useless; the SD-card autoupdate recovery is proven. Lesson: the kernel symbol table (KALLSYMS) must match the module ABI.
- Docs: feature matrix #38 (T40 CPU load) ⚠️ -> ✅, notes on #31, #36, #41; issues cache refreshed.

## 2026-10-31, evening (OpenIMP next 10d3fe59, open-tx-isp next 71d00b99 = agg59)

- **Device proof (Eufy T40XP, image agg59, ko 176f0f5b):** apitest all areas plus 28 cases PASS 270 / FAIL 0, 0 oopses, 0 `unresolved jalr` (agg58: 268 / 2).
- **Closed with camera proof:** #167 `SetGammaAttr` (the case is judged after the AE settled; the driver's soft-writer set path works, Y 156 against 64 at 250 ms); #168 stale frame after a pause (open-tx-isp: STREAMOFF now runs `__vb2_queue_cancel`, FREE_BUFFER and clears the MSCA FIFO; A/B with the old libimp: agg57 fails 6/6, agg58 passes 49/0; the libimp requeue mitigation stays as a safety net); #169 `SetScalerLv` (test baseline settles first).
- **In the image, clean on the camera, effect not measured:** #139c `/dev/tisp` parameter handlers; #140 batch 3, anchor sweep and jalr batches 1 and 2 (7 class-A sites fixed, 58 sites left and classified in the issue); #149b `t40_dqbuf_wait_ms` (default 0); #133 CPU branch (`claude/issue-133-cpu` is in next, the CPU effect is not measured yet, plan in the #133 comment).
- **QEMU only:** rmmod fix for two work items (10 of 10 reload cycles). Unloading only the vendor module is dangerous; ours reloads cleanly since 2026-10-24.
- **Reboot class (#94, #108), diagnostics:** no panic text reached the syslog host before the three unplanned resets of one T23; DRAM pstore proved useless on a T31L (the pattern is lost on reset); the second diagnostic uses RTCSAR, which survives a reset (proven on the T31L); the first runs on the T31L, the second is pending on the T31L and the T23.
- Docs: feature matrix cells T40 #20 and #24 back to ✅+ / ✅, API cells `SetScalerLv` and `SetGammaAttr` dev again, README T40 row. RC3 gate: red (12 blocking issues; streamer-function cells 1787 of 1787).

## 2026-10-31 merge round (OpenIMP next 772614e1, open-tx-isp next 24c2c556)

- **Device proof (Eufy T40XP, image agg57, ko f7960883):** apitest all areas plus 28 cases PASS 262 / FAIL 8, 0 oopses. The run sends the board information before the cases (as the streamer does), so it sees real sensor frames; the apitest runs of agg54-56 without it saw only fallback frames and their PASS counts are void.
- **open-tx-isp (T40) in the image:** slot 0 = core fix (agg54 oops at AddSensor/MDNS), frame-channel mapping fix, re-translated AE zones / second-sensor ISR / custom AE / LSC / LCE chains, `/dev/tisp` parameter handlers for the reviewed setters (#139c), optional blocking frame-channel DQBUF `t40_dqbuf_wait_ms` (default 0, #149 part 2), `&sclk_name` anchor sweep (#140). #139c, #140 and #149 part 2 are not behaviour-checked on a camera yet; #155/#157 (`/dev/tisp`) and the T41 #134 fix are QEMU/host-tested only.
- **Remaining FAILs (open):** #167 `SetGammaAttr` without picture effect (fix on branch `claude/issue-167-gamma`, QEMU only, not merged), #168 stale frame after a pause in the full run (`stalesnap` alone passes), #169 `SetScalerLv` on channel 1 without effect. New: #170 (review leftovers L6, L8-L11).
- **Closed:** #134, #137, #138, #142, #143, #145, #147, #154, #155, #156, #157, #159. RC3 gate: red (15 blocking issues, 2 streamer cells missing).
- Docs: feature matrix cells T40 #20 and #24 now ⚠️, API cells `SetScalerLv` and `SetGammaAttr` ERR (docs/api_evidence.json, merged from the agg57 run); Update 2026-10-31 in the matrix summary.

## Branch `claude/issue-143` (2026-10-29)

- apitest T40/T41: the 19 "no-signature" entries (`IMP_LOG`, `IMP_LOG_{UNK,VERBOSE,DBG,INFO,WARN,ERR,FATAL,SILENT,PRINT,TO_FILE,TO_SERVER}`, `IMP_LOG_OP_*`) are not exported symbols: they are macros of the vendor `imp/imp_log.h` (T40 1.3.1 = T41 1.2.5) that expand to `imp_log_fun(level, option, sink, tag, file, line, func, fmt, ...)` and `IMP_Log_Get_Option()`. The new case `logmacro` uses the vendor macro definitions against libimp and checks level filter, printf arguments, the option mask (file/line/function/tag) and the three sinks; the yaml entry is gone (T40/T41: no no-signature left).
- libimp `imp_log_fun`: the sink argument is honoured like the vendor libalog (RE of `imp_log_fun`/`imp_log_print_buf`/`imp_log_buf`): sink 0 (`IMP_LOG_OUT_STDOUT`) prints to stdout, sink 1 (`IMP_LOG_OUT_LOCAL_FILE`) appends to `timp.log` (`$OPENIMP_LOG_FILE` overrides the name), sink 2 (`IMP_LOG_OUT_SERVER`, the default of `IMP_LOG`) writes a logcat record like the vendor `__android_log_write(level, tag, text)` (libalog, `logd_write.c`): `writev` of priority byte, tag, message to `/dev/log_main` (fallback `/dev/log/main`, then stderr when no logger device exists; `$OPENIMP_LOGGER_DEV` overrides the device for tests). Sink 0 is `stdout` (libalog GOT symbol of `imp_log_print_buf`). Before, every sink ended on stderr.

## Branch `claude/issue-139-case` (2026-10-29)

- **apitest T40: cases for the lifted-handler sweep of open-tx-isp #139** (tools/apitest). `aeinfo_t40`: tuning node
  `/dev/tisp` AE manual block get/set/get (0x20007404/05) per sensor block, reported as `tisp_node_ae_info`;
  `faceae_t40`: SetFaceAe/GetFaceAe round trip without a picture check; `switchbin_t40`: SwitchBin to the running
  sensor's `/etc/sensor/<sensor>-t40.bin` and back to the default (the T41 case needs GetDefaultBinPath, -1 on T40).
  `qemu_run.py` keeps `tisp_node_*` result lines. QEMU T40 only (no camera yet): all PASS with the #139 driver; on
  origin/next `aeinfo_t40` FAILs (every tuning-node command failed), `faceae_t40` and `framedrop` pass.

## Branch `claude/apitest-cov` (2026-10-27)

- **apitest: "result unclear" cells resolved, T23 backlog 56 -> 18** (tools/apitest). Pass criteria that prove the effect:
  `ISP_Tuning_Get/SetModuleControl` is proven by one block whose bypass visibly changes the picture (bit flips are read back
  per block; a colour pass with saturation 230 gives CCM/BCSH a measurable U/V change, drift taken from base/base-end);
  the CCM picture cases switch a bypassed CCM on first (T31 IQ file ships it bypassed) and restore it; T23 sensor H/V flip
  has Get round trips (toggle + restore) next to the picture mirror check; `Set/GetAeStrategy` round trip with the exposure
  time of both strategies (T10); `Module_Ratio` is proven by any module with an effect; lens shading is judged by the
  corner/centre luma ratio and reports "scene too dark" instead of a silent PART. New cases `sensorflip`, `aestrategy`,
  `alloc` (22 allocator exports: IMP_Alloc/Free/Sp_Alloc, Phys/Virt conversion, FlushCache, dumps, attrs, pools, MemPool);
  QEMU first (T23 sensorflip + alloc, T10 aestrategy), then cameras. `run_cam.sh` runs the colour pass (COLOR=0 skips).
  16 T23 exports that no SDK header declares are listed as `no-signature` in untestable.yaml. Resolved: T10 AeStrategy (rt),
  T21 ModuleControl, T23 CCMAttr / ModuleControl / SensorHflip / SensorVflip, T31 CCMAttr, T41 Module_Ratio. Still open:
  T20 MeshShadingScale (needs a bright scene), T40 AeExpList / FaceAe (need the open ISP driver of T41, not on the Eufy image).

## Branch `claude/movestate-t21-t30` (2026-10-27)

- **T21 / T30 IMP_ISP_Tuning_Enable/DisableMovestate**: the vendor T21 1.0.33 and T30 1.0.5 libimps run the same
  gmovestate logic as T31 and T10/T20 (SYSTEM_TAB motion block on 0x800002c, move.txt limits, restore + IDR on
  Disable); OpenIMP T21/T30 sent a plain 0/1 (T21 driver: no-op). The T31/T20 implementation now also builds for
  PLATFORM_T21 and PLATFORM_T30 (move.txt load in EnableTuning). Needs open-tx-isp `claude/movestate-t21-t30`
  (T30: SYSTEM_TAB set/get fixed; T21: the motion block applies its exposure cap only). Host tests
  `isp_t21_movestate_test`, `isp_t30_movestate_test`; apitest movestate prints the exposure ceiling before/during/after.
  QEMU: T21 2246 -> 1347 -> 2246, T30 1916 -> 1149 -> 1916, frames flow, no oops. Camera proof open (T21).

## Branch `claude/issue-125` (2026-10-27)

- **T41 IMP_ISP_SetISPBypass** (issue #125) sends ioctl 0x80085418 as soon as the ISP device is open, like the
  vendor: the header requires the call before IMP_ISP_AddSensor, and the sensor-enabled ioctl path dropped it there
  (it returned 0 and only remembered the value). The open-tx-isp T41 driver (`claude/issue-125`) refuses the real
  bypass (value != 0) until its VIC capture path works; a refused value returns -1 (errno in syslog, was
  -4095) and is not remembered.

## Branch `claude/issue-119` (2026-10-27)

- **IMP_ISP_Tuning_SetISPBypass** on T10/T20/T21/T23/T31 (issues #119, #126): OEM semantics of the bypass control
  (T31 `apical_isp_core_ops_s_ctrl` 0x8000164): ENABLE (1) = full ISP pipeline, any other value = the real bypass.
  ENABLE is the state after EnableSensor and what prudynt writes at startup (`isp_bypass=true`); it is now a no-op
  returning 0. Before, T10/T23/T31 tore down and rebuilt the link graph around the control on every call, which
  stopped the frame channels of a running pipeline for good, and T23 returned -1; T21 rebuilt its graph for ENABLE.
  The real bypass (DISABLE) returns -1 without touching anything, like the vendor T21 library (measured on the
  vendor-stack T21: ENABLE 0, DISABLE -1): on the open drivers the bypassed pipeline delivers no frames (QEMU T31:
  none after ISP_PROCESS=0, none at all when set before the FrameSource). A real runtime bypass is a gap (issue).
  QEMU T31/T23/T20/T21: prudynt's order (bypass between EnableTuning and FrameSource) and a runtime DISABLE/ENABLE
  keep the frames flowing. apitest `ispbypass_old` now round-trips ENABLE; `APITEST_BYPASS_DISABLE=1` also checks
  that DISABLE is refused with frames still flowing (opt-in: an older libimp wedges T10/T20/T31 on DISABLE).

## Branch `claude/api-wiki-review3` (2026-10-27)

- **Wiki API reference: 62 more entries verified against src/** (ISP 60, Encoder 2; FrameSource, which was already complete, and the
  T31 `GetChnEvalInfo` entry were updated for today's merges: T41 real pools, T41 FrameDepth queue, header layout of the record).
  ISP-drawn OSD entries now name the SoCs that have them (T23, T40, T41), the T23 odd-width guard and the vendor-shift note
  are in. 53 ISP and 26 Encoder entries stay marked unverified. Docs only (`docs/api_reference/*.yaml`).

## Branch `claude/t41-fs-pool` (2026-10-27, issue 72)

- **T41 IMP_FrameSource_SetPool / GetPool: real media memory pools** like the vendor 1.2.5/1.2.6.
  `IMP_System_MemPoolRequest` now carves the pool out of rmem (`IMP_MemPool_InitPool`, `IMP_MemPool_GetById`,
  `IMP_PoolAlloc` inside the pool, src/t40/openimp_p2_dma.c); `IMP_FrameSource_SetPool` checks chn < 33 (0x80010001),
  that the pool exists (0x80010040) and refuses a second binding (0x80010020); a bound channel's capture buffers come from
  the pool at EnableChn, a too small pool fails EnableChn cleanly. Unbound channels: unchanged. Beyond vendor:
  `IMP_System_MemPoolFree` releases an idle pool and unbinds its channels (docs/OPENIMP_BEYOND_VENDOR.md 5.1). T40 keeps
  the name-only pools. Host tests `tests/t41/mempool_ledger_test.c`, `fs_pool_test.c`; apitest case `fspool` (QEMU T41,
  then cam .34 libimp-only swap: 3 cycles, ch1 frames 0x058f0000..0x0599e000 inside the 2 MB pool at 0x058f0000, rmem in
  use 22032 KB before and after, 64 KB pool refused; sys/fs/enc/cases PASS 158 FAIL 0). The apitest fs area expects the
  vendor codes for SetPool(1, 0) without a requested pool (0x80010040) and GetPool (0x80010020) on T41.

## Branch `claude/t31-evalinfo-layout` (2026-10-27)

- **T31 IMP_Encoder_GetChnEvalInfo: header layout instead of the vendor's shifted bytes** (beyond vendor, deliberate ABI
  difference). The 36-byte record is now laid out exactly as `IMPEncoderStreamInfo` of the vendor header: `iNumBytes`
  (entropy bytes of the picture) @0, the six counters @4..27, slice / min / max QP @28/30/32. The vendor 1.1.6 copy
  starts one word late (no `iNumBytes`, every field 4 bytes early), which a31d7c7b reproduced. JPEG (8 bytes):
  `iNumBytes` = JPEG size @0, quality factor @4. A streamer that compensated for the vendor shift must not do so with
  OpenIMP (docs/OPENIMP_BEYOND_VENDOR.md 0.9). Host tests `tests/t31/eval_info_test.c` (offsets vs the header struct),
  `p2_jpeg_source_test`; apitest enc on T31L .26 (libimp-only swap): PASS 122 FAIL 0, AVC iNumBytes 3993 of a 4003-byte
  access unit, intra 4 / skip 820 / CU8 444 / CU16 809, QP 20/20/23; JPEG 40209 bytes, quality 75.

## Branch `claude/t23-osd-oddwidth` (2026-10-27)

- **T23 ISP OSD: odd picture width refused** (beyond vendor, stability guard). `IMP_ISP_Tuning_SetOSDBlock` (all three
  sensor entry points) returns -1 without an ioctl for an enabled block with an odd width, and
  `IMP_ISP_Tuning_SetOsdRgnAttr` / `IMP_OSD_SetRgnAttr_PicISP` returns -1 before any rmem is touched. The vendor library
  and ISP take it, and an odd width stops the T23 ISP (QEMU T23: frames stop). open-tx-isp `claude/t23-osd-oddwidth`
  refuses it in the driver as well (-EINVAL). Host test `tests/t23/isp_tuning_test.c`; device test
  `tests/t23/devtest/isp_osd_devtest.c` phase p5oddw (QEMU T23: next driver + image libimp stopped both channels,
  guarded driver refused and frames kept flowing at 20.4 fps).

## Branch `claude/dmic` (2026-10-27)

- **IMP_DMIC_* on T40 and T41** (issues #131, #61, #74): the 21 calls were `ENOTSUP` stubs, now the vendor `dmic.c` rebuilt
  (libimp 1.3.1 / 1.2.5 disassembly, no vendor code): `src/t31/openimp_t31_dmic.c` serves T30/T31 (oss2 ioctls) and T40/T41
  (OSS3 `/dev/dsp`: SET_PARAM 0x73, ENABLE_STREAM 0x6a, GET_STREAM 0x69 with the 16-byte (T40) / 20-byte (T41) stream struct,
  ENABLE/DISABLE_AEC 0x68/0x67, gain 0x57, T40 DISABLE_STREAM 0x6b). Argument validation, Set/Get round trips, clamps (volume
  -30..120, gain 0..31, frame = whole 10 ms, chnCnt 1..4) and the not-enabled return codes follow the vendor; T31 and T40/T41 share
  one module. T31 existed (cell "aud") and is now run.
- **Beyond the vendor:** T40/T41 `IMP_DMIC_Enable` reads `/sys/module/audio/parameters/dmic_enable` and returns -1 when it is 0.
  The ingenic-sdk audio module then has no DMIC route and the vendor library's first ioctl dereferences NULL (kernel oops,
  reproduced in QEMU T41); OpenIMP fails cleanly instead.
- **apitest area/case `dmic`** (all 21 / T31 20 vendor functions, round-trip level, evidence level rt; with a DMIC board the stream
  flow Enable..GetFrame, gain and clamps run as well); `apitest ... dmic` needs no ISP/sensor. `IMP_DMIC_*` removed from
  `untestable.yaml`. `qemu_run.py`: `QR_LIB=<libimp.so>` pushes and uses a test libimp.
  Evidence: QEMU T31/T40/T41 21/21 (T31 20/20) PASS, no oops; cameras .26 (T31), .184 (T40), .34 (T41) libimp-only runs all PASS
  ("API round trip, no DMIC hardware on the test board"). Matrix: DMIC_* T31/T40/T41 aud/err -> rt; streamer-function cells 1713 -> 1779 of 1787.
  Not proven (needs a DMIC board): capture, gain effect, AEC.

## Branch `claude/issue-90` (2026-10-27)

- **T41 IMP_ISP_Tuning_Set/GetFaceAe and Set/GetAeExpList** are exported (vendor 1.2.6 layout: pointer tuning ioctl,
  controls 0x8000035 and 0x8000027; the T40 exposure list stays 0x8000099). The driver side (open-tx-isp `claude/issue-90`)
  serves them from the vendor AE, and GetAeStatistics / SetStatisConfig histThresh now report what the vendor AE computes
  (issue #90). AeSpeed is not in the T41 vendor API.
- **apitest cases `aestats`, `faceae`, `aeexplist`** (T41): 256-bin sum = width x height, 5-bin split against the 256 bins for the
  current and changed histThresh (original restored), zone spread; face window target 40 vs 180 must move the picture
  (optional Y snapshots with `APITEST_SNAPDIR`, hold time `APITEST_FACE_MS`); list mode on/off with the calibration's own
  list, read back, invalid mode refused. `run_swap.sh`: `TOPIC` for the STATUS lines, guard/oops lines kept in dmesg-tail.txt.
  .34 (sc5336, runtime swap): all PASS (face: Y 98.2 -> 88.4 at target 40, -> 105.8 at target 180); QEMU T41 the same.

## Branch `claude/issue-89` (2026-10-27)

- **T40 stale-first-snapshot fix verified on the device** (issue #89, no libimp change): new apitest case
  `stalesnap` (explicit only, `APITEST_CASES=stalesnap`, pauses `APITEST_STALE_PAUSES`, default 2,10,30 s). T40
  frame timestamps are taken at DQBUF (the wire has no completion time), so they cannot show staleness; the case
  switches the ISP brightness while nobody reads (the capture buffers are full) and checks that the first
  SnapFrame / GetFrame after the pause shows the new brightness. Eufy T40XP, libimp from next via
  LD_LIBRARY_PATH: fresh for 2, 10 and 30 s with both calls; the same libimp with the stale-drop disabled returns
  the old picture every time (test sensitivity). `run_swap.sh` in libimp-only mode no longer rmmods the sensor/ISP
  modules in its restore step (hang risk on the Eufy).

## Branch `claude/issue-127` (2026-10-27)

- **T23 ISP OSD (IMP_ISP_Tuning_*OsdRgn, IMP_OSD_*_ISP)** (issue #127): the picture now appears in the frame. The
  cause was in the driver (open-tx-isp `claude/issue-127`: the stored OSD pictures were never written into the
  MSCA); OpenIMP's T23 path (`src/t23/openimp_t23_isp_osd.c`, rmem picture copy + SetOSDAttr/SetOSDBlock) was
  already right and is unchanged. New device tool `tests/t23/devtest/isp_osd_devtest.c` (+ `build.sh`): the
  raptor-hal call sequence in 7 phases (picture, second picture, move, second region, hide, destroy) with one
  NV12 frame of ch0/ch1 per phase on stdout for a host collage. Cam .30 with the open ko: apitest isposd FAIL ->
  PASS (Y 111.3 -> 234.0), all devtest phases as requested; QEMU T23 the same.

## Branch `claude/rc3-gaps2` (2026-10-27)

- **System_WriteReg32 is tested on all 7 SoCs (no library change):** the case writes the scratch register SPR (offset 0x1c, 8 bit) of the UART that carries the
  kernel console (`console=ttyS<n>` of `/proc/cmdline`, 0x10030000 + n * 0x1000; ttyS1 = 0x1003101c on all test boards) with a different value, reads it back
  with `IMP_System_ReadReg32`, writes the original back and reads again. That UART is clocked (the console runs on it) and the 8250 driver uses SPR only in the
  one-time probe, so the round trip has no side effect. Without a console on the command line the case SKIPs. Removed from `untestable.yaml`.
- **IVS_ReleaseData is tested on all 7 SoCs:** a minimal custom algorithm (`IMPIVSInterface` of the vendor header: init, exit, preProcessSync, processAsync,
  getResult, releaseResult, getParam, setParam, flushFrame) measures the frame, releases it with `IMP_IVS_ReleaseData(frame->virAddr)` and leaves a result for
  `IMP_IVS_GetResult` / `IMP_IVS_ReleaseResult`; the case needs every processAsync release to return 0 and the result to come back through the interface.
  Removed from `untestable.yaml`. (Vendor T31 `IMP_IVS_ReleaseData` is a tail call of `VBMUnlockFrameByVaddr`; OpenIMP never locks capture frames for IVS, so
  it returns 0 for any non-NULL address.)
- QEMU first on all 7 SoCs (`tools/apitest/qemu_run.py` now prints `IMP_System_*` and `IMP_IVS_*` lines too), then the cameras (`run_cam.sh` areas sys,ivs): 0 FAIL.
  T40 needs `W=1920 H=1080 EXTRA_ENV="APITEST_CH0_SCALE=1 APITEST_KEEP_FPS=1"` for the apitest areas (3840x2160 ch0 without scaler starves the IVS).
  `run_cam.sh` takes `IMGFX_FILTER` for the picture area.
- **T31 IMP_Encoder_GetChnEvalInfo** (was ENOTSUP): vendor 1.1.6 returns 0 and copies the 36-byte record the Allegro EndEncoding left in the stream slot (JPEG: 8
  bytes). Bytes as the vendor: six hardware counters of the merged slice status (status +0x1c..+0x30; values on the T31 camera e.g. 4 2896 136 886 0 0), then
  int16 slice QP, min QP, max QP (20/20/21), the rest zero; JPEG channel: int16 quality factor (75), rest zero. The vendor copy starts one word later than
  the public `IMPEncoderStreamInfo` (iNumBytes is not in it); we keep the vendor bytes. Host tests `eval_info_test`, `p2_jpeg_source_test`; QEMU; camera .26.
- **T41 FrameSource_GetFrame follows the frame depth** (vendor 1.2.5 disassembly): depth 0 -> ret 0 and *frame untouched; depth N -> N frames can be held, the next
  GetFrame waits 2 s, then 0x80012000. OpenIMP's own consumers use `OpenIMP_FrameSource_ConsumerGetFrame` (not bound to the depth), GetFrameEx stays depth-less.
  Not like the vendor: the capture queue is shared and keeps the oldest frames when nobody pulls (the vendor depth queue replaces its oldest waiting frame).
  QEMU T41 and camera .34 (fs, enc, ivs): 0 FAIL. FrameSource_Get/SetPool stays `cache` (vendor: pool must exist, one set per channel, buffers from the pool;
  OpenIMP only records the id).
- **T23 SetAutoZoom** (open-tx-isp `claude/rc3-gaps2`): cause 1: the driver refused GetAutoZoom (-EINVAL), so the round trip case failed; now served from the
  channel record. Cause 2: crop off reset the window to the channel's output size, i.e. the top-left corner of the sensor picture (measured: layout distance
  back-picture to corner 0.08, to whole 0.22); now the whole sensor picture (0.000). Case checks where the content sits. Camera .30 (ko swap): zoom shows the centre.
- **T23 SetExpr** (rule): the manual integration time follows (13 vs 1363 lines) but the AE gain holds the brightness (Y +3); the picture detail differs 2.3x.
- **T21 Get/SetSuperFrameCfg**: case on .24: mode NONE with thresholds stored and read back, DISCARD refused with -1 and the stored configuration unchanged.
- Gate cells (RC3 missing): 14 cells to dev (WriteReg32 and IVS_ReleaseData on T10, T20, T21, T23, T31, T40, T41); works 1692 -> 1704 of 1787 (items 1+2), 1711 of 1787 with the items below.

## Branch `claude/issue-109` (2026-10-27)

- **T10/T20 IMP_ISP_Tuning_Enable/DisableMovestate** (issue #109) return 0 like the vendor and run the vendor
  library logic (T10/T20 libimp 3.12.0, one binary for both, the same as T31 1.1.x): Enable reads the 0x70-byte
  `isp_core_stab_attr` of control 0x800002c (IMAGE_TUNING_CID_SYSTEM_TAB), then sends it back with the motion
  settings (day: maximum integration time 3/5 of the current one, manual sinter and temper, sharpening, sinter and
  temper limits from `/etc/sensor/<sensor>move.txt`; night: the night limits of that file); Disable puts the
  captured block back when the running mode is unchanged and asks for an IDR on channel 0. The T10/T20 driver
  applies this block. Before, OpenIMP sent a plain 0/1 as the block pointer, the driver's copy failed and both
  calls returned -1. Host test `tests/t30` `isp_t20_movestate_test` (the T31 test built for T20). QEMU T10/T20 and
  cameras (T10 jxh42, T20 jxf23): -1/-1 before, 0/0 after, neighbour cases and frame flow unchanged, no oops.

## Branch `claude/rc3-gaps` (2026-10-27)

- **Runtime-swap evidence:** `tools/apitest/run_swap.sh` runs apitest on a camera with the open ISP ko and/or libimp of `next` swapped in at run time (no flash,
  no reboot, stock ko and streamer restored); the T41 and T20 gate cells were re-judged on that stack.
- **Test fixes (no library change):** the T41 sensor table uses the real sc5336 mode 2880x1620 (2592x1944 distorted every geometry case, e.g. ISP OSD);
  the ISP OSD case uses ARGB order on T41 (BGRA reads byte 0 as alpha) and an encoder on FS ch0; WaitFrameDone drains ch0 (MSCA output 0 only delivers
  while a consumer takes frames); T41 AutoZoom accepts the vendor normalisation of disabled outputs; SetChnAttrRcMode runs on T41 (header 1.2.0 lacks the
  declaration); SetChnMaxPictureSize is measured (biggest picture follows the limit); Sharpness: one visible direction proves it.
- **Gate cells** (RC3 missing, before -> after): T10 22 -> 5, T40 43 -> 26, T41 57 -> 27 (AENC/ADEC on T10/T40/T41 from the audio runs, T41 encoder
  FrameRate/BitRate/QpBounds/MaxPictureSize/RcMode, ISP OSD with open-tx-isp next, AutoZoom, WaitFrameDone, Sharpness).
- **T31 AutoZoom (issue #124) was a test misuse:** the crop window is cut from the scaled picture and must keep the channel size; the case now zooms 2x
  (scaler 2x, centre crop in the channel size): PASS with visible effect (detail 5.27 -> 2.18). T31 run on .26 with the open ko/libimp of next: 0 FAIL.
  HVFLIP pair spelled differently by the vendor header (GetHVFlip / SetHVFLIP) is mapped; T40 DefaultBinPath Get reports after the first Set.
- Gate (RC3 missing): T31 29 -> 26, T40 43 -> 24, T23 8 -> 6 (HVFLIP, plus the T41/T10 figures above). T23 ISP OSD (#127) is driver work (no image path in
  driver/t23), commented on the issue.
- Merge tool: a later PASS/RT clears an `err` cell this tool wrote from an earlier FAIL of any image tag.

## Branch `claude/rc3-cases` (2026-10-26/27)

- **One device test app:** `tools/imgfx` and `tests/t40/devtest/t40_devtest.c` are folded into apitest (areas `picture` and `t40dev`); one source, one
  binary `apitest_<soc>`, one CLI (`--list`, `--areas`, `--cases`, `--json FILE`). The JSON report (function -> PASS/FAIL/RT/NA/SKIP + detail) is read by
  `results2json.py --report` and `merge_evidence.py`. `run_cam.sh` and `qemu_run.py` (`auto`, `area:NAME`, `--nopush`) use the per-SoC build.
- **Completeness check:** `tools/apitest/check_coverage.py` (in `make status-check`): every vendor function of the seven gate SoCs has a case or an entry with
  category and reason in `tools/apitest/untestable.yaml`.
- **`make apitest`** (`tools/apitest/build-device.sh`) builds and installs `openimp-apitest` for the thingino package option `BR2_PACKAGE_OPENIMP_APITEST`.
- **RC3 cases** (every case ran in QEMU of its SoC first): gamma/module control/CCM/RGB coefficients/ISP bypass/bin switch/scaler level (T41), contrast, hue,
  AE compensation, backlight, DPC, custom mode, WDR, flips, CSC, AWB CT, gamma, raw DRC, AE ROI, camera input select, ISP OSD, AutoZoom without getter.
  Audio: AO/AEC with silence only (muted, volume minimum, zero PCM frames; user decision 2026-10-26), counted as rt.
- Findings (issues): 114 T41 gamma curve without picture effect, 115 T41 SetISPBypass -4095, 116 T41 AddSensor fails after SetDefaultBinPath,
  117/118 T31 CustomMode / Wdr_OutputMode, 119 SetISPBypass(DISABLE) wedges the frame flow in QEMU (T31, T20, T10), 121 T20 GetGamma zeros,
  122 T23 GetISPCustomMode, 124 T31 AutoZoom crop, 126 T23 SetISPBypass.

## Branch `claude/issue-116` (2026-10-26)

- **T41 IMP_ISP_SetDefaultBinPath before IMP_ISP_AddSensor** (issue #116): the bin path record OpenIMP sends with
  TISP_VIDIOC_SET_DEFAULT_BIN_PATH (0xc004542a) after REGISTER_SENSOR is now the 132-byte T41 layout
  `{vinum, path[128]}` the T41 driver copies (stock `tx_isp_set_default_bin_path`, T41 1.2.5 tx-isp-t41.ko); it was
  the 68-byte T40 record. AddSensor failed (-1) because the open T41 driver refused the ioctl (fixed in open-tx-isp
  `claude/issue-116`); with both, the driver loads the given file instead of `/etc/sensor/<sensor>-t41.bin`
  (QEMU T41: `/tmp/at/alt.bin` loaded, a missing file fails AddSensor; cam .34 with the fixed driver: apitest
  binpath PASS, AddSensor 0 instead of -1). Paths up to 127 characters on T41.
  Host test `tests/t41/bin_path_test.c` (T40 and T41 record, ioctl order, length bound, driver refusal).

## Branch `claude/issue-113` (2026-10-26)

- **T31 IMP_ISP_GetSensorRegister** clears the value field of the request before the ioctl (the stock library leaves
  it uninitialised), so a kernel that does not fill it yields 0 instead of stack contents. The wrong sc2336 chip id
  (0x0404 instead of 0xcb3a, issue #113) was the open T31 driver not forwarding the register events to the sensor
  (fixed in open-tx-isp `claude/issue-113`). T10/T20/T21/T23/T40/T41 already read the right chip ids.

## Branch `claude/status-data` (2026-10-26)

- **Status data instead of a hand-written matrix:** the feature matrix source moved to `docs/status/features.yaml` (45 features x 10 SoCs:
  symbol, detail text, issue numbers, evidence) and `docs/status/history.md` (the update log, verbatim); `docs/FEATURE_MATRIX.md` and
  `docs/feature-matrix.html` are generated from them (`tools/gen_feature_matrix_html.py`, `tools/status_data.py`). `make check` validates the
  data and that the generated Markdown is current. `docs/api_evidence.json` stays the API source (machine-written by the apitest tools).
  Gaps, bugs and progress are tracked as public GitHub issues; `docs/matrix_wip.json` is replaced by the issue state
  (`docs/status/issues.json`, open issue labelled `type:wip` = construction sign). Counts unchanged.
- **API tables:** every cell explains itself (hover and click: state, evidence, note, issue); `n.a.` cells show a faint "–" with the reason; "implemented, not
  device-tested" cells give the reason (no test case yet / test ran, result unclear / not run on this SoC; `docs/status/untested.json`, built by
  `tools/status_untested.py`); state, SoC and area filters, and the summary numbers apply them.

## Branch `claude/gate-1026` (2026-10-26)

- **T40 SetMaxStreamCnt on a created channel** fails and keeps the count, like the T40 1.3.1 libimp (channel index
  >= 0 -> -1) and T31/T41 already did (host check in the shared bufshare test).
- **apitest, T41 encoder read-back:** the T41 RC/GOP/frame-rate setters store a pending value that the vendor encoder
  applies before the next picture (OpenIMP: deferred read-back since 0b89cc9b), and GetChnGopAttr reports the IDR
  distance uGopLength x uMaxSameSenceCnt. The test now reads back after two pictures and expects the product; the
  2026-10-26 FAILs on .34 (FrmRate 15/1, BitRate 512, GopLength 60, QpBounds 20..45) were the immediate read-back,
  not a regression (output rate 10.0 fps after SetChnFrmRate 10/1 in the same run). Re-run .34: all PASS.
- **apitest, T40:** Awb_GetRgbCoefft is called again (driver fix on open-tx-isp `claude/gate-1026`; older T40 drivers:
  `APITEST_SKIPFN=IMP_ISP_Tuning_Awb_GetRgbCoefft`).

## Branch `claude/gate-1026-enc` (2026-10-26)

- **Release-gate encoder FAILs of the agg47 apitest closed as vendor-equal (test expectations corrected, no library change):** vendor libimp disassembly (T10 3.12.0, T21 1.0.33, T23 1.3.0 from `weak-imports/vendor-libs`) and the device runs agree. `SetChnDenoise` T10/T20/T21: the CreateChn enable switch stays as created, so on a channel created with denoise off the type is forced to 0, IQp/PQp are kept and Get leaves `.enable` untouched (T21 additionally keeps type 0 whatever was created, mode 4). `SetSuperFrameCfg` T10: re-encode only like T20 (the test sent DISCARD, which the OEM i264e parameter 13 path of this encoder does not take). `Get/SetChangeRef` T21: 1.0.33 `GetChangeRef` fails on a created channel (logs "unsupport to change ref channel"), Set returns 0. `SetChnROI` T23: 1.3.0 stores the corners `sra 4` and returns them `sll 4` like T10/T20/T21, so (64,64)-(319,255) reads back (64,64)-(304,240). PASS lines on T10 .31, T20 .107, T21 .24, T23 .30 (`1026b-*-enc` runs, enc area, 96/96/101/126 PASS, 0 FAIL).
- **T41 `IMP_Encoder_YuvEncode` -1 was a test unit error:** the vendor T41 1.2.6 `YuvInit` multiplies `targetBitrate`/`maxBitrate` by 1000 (kbit/s, like `SetDefaultParam`), the test passed 512000 (= 512 Mbit/s); OpenIMP then asked for a 96 MB bit-stream buffer, the AVPU allocation failed ("Can't alloc DMA buffer", partial DMA layout) and the lazy codec start in the first YuvEncode returned -1. With 512 kbit/s: YuvInit/YuvEncode/YuvExit PASS on the T41 .34 (7340 bytes, SPS/PPS/IDR). Possible improvement (beyond vendor, not done): fail `YuvInit` early when the stream buffer cannot be allocated instead of at the first encode.
- **imgfx cases reach the API evidence (`tools/apitest/results2json.py`):** every imgfx case line (`[R] <case> set= get= | ST Y U V H G`) is now judged by a rule table: set returns 0, readback equals the set value, and the picture statistics move the expected way against the base case or the opposite case of the pair (brightness Y, contrast/sharpness/noise detail energy, saturation/colour effects U/V, gamma/AE/DRC/backlight Y, flips by mirroring the 8x8 luma grid, WB presets by chroma shift). Conservative: anything unclear is PART (not merged), a PART never takes back an apitest PASS, a picture-proven PASS upgrades an apitest PART. bypass-* needs a visible picture change against the neutral median of the run (AE drifts, so base is no reference), otherwise PART (readback only). Cases where no picture effect is expected (antiflicker, awb-auto, csc mode, WDR flag, day mode, gain caps, saturation-high on a grey scene) are verdict RT = new evidence level `rt` (API round trip on device, counted as works, shown as ✅rt in the API tables). 77 more cells `dev` on T10/T20/T21/T23/T31/T41 from the 2026-10-26 runs; the per-case verdicts are in `result.json` under `imgfx_cases`.
- **API evidence merged:** `docs/api_exports.json` refreshed from the current builds (includes the t4x-api-gaps functions), `tools/apitest/merge_evidence.py` merges a PASS onto a `miss` cell of an exported function and clears an `err` cell the tool itself wrote from an earlier FAIL of the same image; T40/T41 Yuv/Vbm/GetFrameEx/AI_*Hs cells are `dev`, the other t4x-api-gaps functions without a camera run `host`/`aud`; matrix regenerated without warnings.

## Branch `claude/apitest-evidence` (2026-10-26)

- **Device API test tools in the repo (`tools/apitest`, `tools/imgfx`):** the apitest (every non-image vendor IMP/SU function, PASS/FAIL/N-A/SKIP per function, guard arenas against struct overwrites) and imgfx (one image-changing ISP function at a time) sources moved from the session directory into the repo; T40 build added (T41 API family with the T40 1.3.1 headers: sensor FPS/register calls of the T40 signature, declared-function guards in the T41 statistics block); generated same-value Get/Set round trips for the ISP pairs not yet round-tripped (`at_isp_rt.c`, `gen_rt.py`); imgfx `IMGFX_STATS=1` (in-memory picture statistics instead of files); `run_cam.sh` per-camera runner (cam lock, streamer stop/restart, no sound) and `results2json.py` (logs to per-function results for `docs/api_evidence.json`; apitest, imgfx and the T40 devtest `RES` lines).

## Branch `claude/t40-3a-noconsumer` (2026-10-26)

- **T40: AE/AWB/day-night keep running with no stream client.** The open driver's frame 3A meters completed frames;
  with the FrameSource kept open and nobody pulling (timps `general.fs_keepalive=1`, no client) all capture buffers
  filled after 4 frames, the MSCA stalled and the AE froze (.184 stock: 0 AE samples / 10 s, `core_isr_frame_deliver_count`
  stuck at 4, mode "day" at IT 1919 / gain 1x in a dark cellar). The vendor capture thread keeps dequeueing. The P1 frame
  feeder (the IVS feeder thread) now also recycles the idle 3A channel (lowest enabled channel, normally ch0; quiet as
  soon as a consumer dequeues for three frame intervals): paced at the channel's frame interval with a single,
  non-waiting DQBUF (the ring is full of completed frames), so the driver keeps its `frame_3a_interval` rate.
  `OPENIMP_3A_FEED=0` off, `=hi` feeds the highest enabled channel, `OPENIMP_3A_FEED_DIV=n` every n-th interval.
  T41 needs no change (its AE runs from the per-frame AE statistics IRQ without a consumer).
  Device proof .184 (aeapi4 driver = next, no client): 6.75 AE samples/s (20 fps / interval 3); light 255 -> 40: EV at
  max in 2 s, auto night after 9 s; 40 -> 255: EV 5034 -> 2503 in 2 s; day -> light off: EV max in 2 s, auto night
  after 9 s; with a client ch0/ch1 301/301 frames in 15 s (20 fps, unchanged); 0 oops. timpsd CPU (20 s window, % of one
  CPU): feed off 0.15 %, paced feed 0.65 % (ch0) / 0.45 % (ch1 360p); the first version (blocking GetFrame per frame) cost
  5.65 % / 5.0 %: the cost was the frame channel's 1 ms wait polling, not the resolution.
  Host test `tests/t41/snap_test.c` (`t40_3a_feed_channel`).

## Branch `claude/t41-isp-osd` (2026-10-26)

- **T41 ISP-drawn OSD:** `IMP_ISP_Tuning_SetOsdPoolSize/CreateOsdRgn/SetOsdRgnAttr/GetOsdRgnAttr/ShowOsdRgn/DestroyOsdRgn`,
  the `IMP_OSD_*_ISP` calls behind them (`Init/Exit/SetPoolSize/CreateRgn/DestroyRgn/SetRgnAttr_PicISP/GetRgnAttr_ISPPic/
  ShowRgn`) and `IMP_ISP_Tuning_Set/GetOSDAttr`, `Set/GetOSDBlock` (controls 0x080000a1 / 0x080000a2, vendor return
  codes -4087/-4084/-4091/-4090). The T40 region code (`src/t40/openimp_t40_isp_osd.c`) is shared; T41 differences
  from the vendor 1.2.6 isp_osd.c: chn 0/1, `IMPIspOsdAttrAsm` 36 bytes with `pic.pinum`, OsdnotifyIsp sends
  SetOSDAttr + SetOSDBlock per region on sensor 0. Before, the four Tuning region calls returned ENOTSUP and the
  rest did not exist (raptor-hal's T41 ISP OSD failed). Needs open-tx-isp `claude/t41-isp-osd`.
  `IMP_OSD_SetGroupCallback` is now also exported on T41 (groups 0..3, the IPU group OSD callback as on T40).
  Device proof .34 / .32 with `tests/t41/devtest/isp_osd_devtest.c` (raptor-hal sequence): ch0 NV12 + JPEG exact
  colours, move/update/two regions/hide/destroy, fps unchanged, 0 oops; the hardware draws into the main stream only
  (not ch1). Bitmaps with the bytes B,G,R,A need `IMP_ISP_ARGB_TYPE_ARGB`; raptor-hal's `BGRA` shows only the blue/
  white parts (alpha from byte 0). Host test `tests/t41/isp_osd_test.c`.

## Branch `claude/t40-ae-api` (2026-10-26)

- T40: the AE tuning calls are served by the open driver's frame AE (open-tx-isp `claude/t40-ae-api`); the T40 libimp
  already passes them 1:1, no library change. New devtest phase `0x1000000` (AE API cells, PGM snapshots with
  `T40_SNAPDIR`) and `tests/t40/devtest/t40_aetune` (sends the AE tuning ioctls beside a running streamer).
  .184: GetAeStatistics sum 8294400 / 5-bin 65535 / zones vary, ExprInfo 220 bytes (no overrun), comp, HLC/BLC,
  manual/freeze/min gain, weights, speed, face AE and antiflicker act on the exposure. Details:
  open-tx-isp CHANGELOG 2026-10-26 (7).

## Branch `claude/t4x-api-gaps` (2026-10-26)

- **`IMP_Decoder_*` on T40/T41:** exported with -1 and errno ENOTSUP (the vendor decoder is the hardware JPEG decoder, OpenIMP has none on these SoCs), so an application finds out at CreateChn instead of failing to link; host-checked in `tests/t41/yuv_encode_test.c`.
- **FrameSource on T40/T41: external frame injection and the Ex calls:** `IMP_FrameSource_ExternInject_CreateChn/EnableChn/DisableChn/DestroyChn` with `IMP_FrameSource_DequeueBuffer/QueueBuffer` give an NV12 channel the application fills (rmem frames, pitch = width, chroma after ALIGN16(height) lines, `size` says so); it binds and encodes like a sensor channel (the P1 GetFrame/ReleaseFrame of the encoder, IVS and OSD take the queued frames in order and give them back). Vendor checks: chnNum < 7, picWidth % 16, NV12, not a sensor channel, Disable/Destroy order. `IMP_FrameSource_GetFrameEx/ReleaseFrameEx` = GetFrame/ReleaseFrame (OpenIMP needs no depth queue there either); `IMP_FrameSource_SetYuvAlign` keeps the vendor codes (0x80010001/0x80010008) and refuses (ENOTSUP) an alignment that would need padding, which the open-tx-isp frame channel cannot do; `IMP_EmuFrameSource_*` (vendor emulator) -1/ENOTSUP. Device proof: injected 1280x720 pattern -> bound H.264 channel, 50/50 frames in order with the queued time stamps, SSIM 0.9983 (T41 .34) / 0.9989 (T40 .184). T40 .184 also ran the unbound encoder: H.264 720p 0.9989, H.265 0.9996, 1080p 0.9992, packed 360p 0.9989, YuvInit-JPEG 0.9990. Host test `tests/t41/fs_inject_test.c` (T40 + T41).
- **T41/T40 audio input extras and log options:** T41 `IMP_AI/AO_Set/GetDigitalGain` (were T40-only; same checks, clamp and ioctls as the vendor 1.2.5 ai.c/ao.c), `IMP_AI_EnableGetRaw/DisableGetRaw/GetFrameAndRaw` (the capture before howling suppression, HPF, NS, AGC and the software mute; one ReleaseFrame frees both), `IMP_AI/AO_EnableAlgo/DisableAlgo` (webrtc_profile.ini: NS/AGC/HPF, the AGC mode from `set_mode`), `IMP_AI_EnableHs/DisableHs` (T40 too; libaudioProcess howling suppression ahead of the other effects), `IMP_Audio_Select_Codec` (ioctl `_IOR('P', 74)`; the T41 audio driver on the cameras answers "not supported", so it returns -1 there). The T23 EnableAlgo/Hs code (`src/t23/openimp_t23_audio_ext.c`) now runs on the P3 audio path through the same hooks. `IMP_Log_Get/Set_Option` and `imp_log_fun` are exported on T40/T41 as on T31 (vendor: libalog.so). Device proof T41 .34 (microphone only, no output): digital gain 150/200/250 read back, capture RMS 1.4 / 22.5 / 407.7 (0.5 dB steps), 300 clamps to 255; GetFrameAndRaw without effects bit-identical, software mute: processed 0 / raw 25 RMS, EnableAlgo (HPF+NS profile): processed 2.6 / raw 23.5 RMS, DisableAlgo identical again; EnableHs 0 (no howling in the room: output unchanged); log option read-back; 0 oops. Host test `tests/t41/audio_ext_test.c`.
- **Unbound encoder on T40/T41 (vendor T40 1.3.1 / T41 1.2.6 API):** `IMP_Encoder_YuvInit/YuvEncode/YuvExit` encode caller-made NV12 frames (H.264, H.265, JPEG) synchronously into the caller's buffer, `IMP_Encoder_VbmAlloc/VbmFree/VbmV2P/VbmP2V` give rmem buffers for them; T41 also `IMP_Encoder_InputJpege` (standalone NV12 -> JPEG), `SetIvpuBsSize`, `SetAvpuJpegQp` and the `_Ex` variants. Vendor semantics from the static `libimp.a` (width % 16 / height % 8, standalone codec without a channel, AU copied to `outAddr`, return codes 0x80040001/2/8/40). OpenIMP runs the bound-channel codec in a hidden encoder slot (4 unbound encoders, no public channel used). Beyond vendor: a non-zero `outLen` on entry is the capacity (no overrun), a packed NV12 frame (chroma at w*h, timps' rotate layout) or a frame outside rmem is restaged, InputJpege honours q 1..100. `_Ex` (vendor: secondary process through a message queue) works only in the process that owns the encoder, else -1/ENOTSUP. Device proof T41 .34 (synthetic pattern, SSIM vs source): H.264 1280x720 0.9989, H.265 0.9996, H.264 1920x1080 0.9991, packed 640x360 0.9988, InputJpege 1280x720 q90 0.9990, 640x360 0.9985, YuvInit-JPEG 0.9990. Host test `tests/t41/yuv_encode_test.c` (T40 and T41 builds).

## Branch `claude/matrix-api-coverage` (2026-10-26)

- **Evidence level `run` (2026-10-26):** a vendor function that timps imports on a fleet camera of that SoC which passed the agg46 soak counts as working (`docs/api_run.json`, refreshed with `--refresh-run`); summary cells show works / 🔧 / gaps / ?.

- **API coverage per function (generated):** the function tables of `docs/FEATURE_MATRIX.md` are now generated by `tools/gen_api_coverage.py` (run by `tools/gen_feature_matrix_html.py`) from `docs/api_evidence.json` (evidence per function and SoC, de-duplicated, stale cells corrected: exported functions are no longer "missing", T41 functions covered by device-tested features count as device-tested), `docs/api_exports.json` (nm of the OpenIMP builds, cross-checked on every run), `docs/matrix_wip.json` (work in progress, construction sign) and the vendor export lists. Per area and SoC summary counting only functions of that SoC's vendor API, priorities for streamers (gaps that timps/prudynt/raptor call, by number of streamers) at the top of the HTML, API function lists in the click panel of the main matrix. Long-term backlog list per SoC (missing/partial/unknown functions no streamer calls, grouped by area with suggested action). T41 #27 note: exact vendor P-path regime deliberately not rebuilt. Columns T32 and T33 added (T32: QEMU only, vendor libimp; T33 planned); vendor lists `tools/export-check/vendor-T32.txt` (libimp 2.2.0) and `vendor-T33.txt` (SDK 2.0.0).

## Branch `claude/matrix-html-v2` (2026-10-26)

- **Feature matrix HTML v2** (`tools/gen_feature_matrix_html.py`, docs only): compact symbols-only table (SoC columns, sticky header and first column, RC2/chg badges), tag chips per feature (derived from the matrix sections), filters for SoC, status, tags, text search and "only changes since RC1", hover tooltip with the first sentence of a cell, click/Enter side panel with the full detail text (feature name: all SoCs), counts per SoC and RC1 comparison in a collapsible block, long update paragraphs and the Summary moved into a collapsible History section. Cell symbol counts and RC1 change counts identical to the previous HTML.
- **Feature matrix HTML v2.1:** always-visible legend bar (symbol and badge texts from the matrix legend, also shown as tooltips on hover/focus); badges now compare three states (RC1 tag, RC2 tag, current file): `RC2` = improved between RC1 and RC2, `new` = improved after the RC2 tag (in next, not released), `changed` = changed without improving (other symbol or new text); filters "only changed since RC1" / "since RC2".

## Branch `claude/fallback-default-on` (2026-10-26)

- **Fallback frame is ON by default (T40/T41), user decision 26.10.:** `OPENIMP_FALLBACK_FRAME=0` switches it off; API unchanged. T10-T31 not covered.

## Branch `claude/fallback-frame` (2026-10-26)

- **Fallback frame when the sensor stops (T40/T41, opt-in at first, ON by default since 26.10., beyond vendor):** with `OPENIMP_FALLBACK_FRAME=1` or `OpenIMP_FrameSource_SetFallback(chn, 1)` a FrameSource channel that gets no frame for 2 s (`OPENIMP_FALLBACK_TIMEOUT_MS`) while a consumer waits delivers synthetic NV12 frames at the channel rate (test pattern, black band "NO SENSOR SIGNAL", frame counter, blinking square), logs `no sensor signal ... (error 67)` and switches back by itself with monotonic time stamps; `OpenIMP_FrameSource_GetFallback` reports the state, `OpenIMP_Cap_FallbackFrame` marks the API. Default OFF. Needed a time-limited DQBUF: the T41 frame channel blocks in the kernel, so with the fallback armed a worker thread per channel does the DQBUF (not armed: unchanged code path). Not covered: I2D (rotation/flip) channels, IVS, SnapFrame, T20-T31.
- Device proof (T41 sc5336, `tools/fbtest`, sensor stream stopped/started via `IMP_ISP_SetSensorRegister` 0x0100, modules stay loaded): real 15.0 fps; stopped without fallback 0 frames; armed: first message frame after 2.0 s, 15.0 fps afterwards (105 frames in 9 s), counter in the decoded pictures consecutive 0..103 without gaps; sensor back: 15.0 fps real frames, 0 time stamp steps back; fallback switched off while active: no frames again, recovery on resume; 0 oops, timps restarted. Host test `tests/fallback` (dwell time, 25 fps pacing, no burst after a gap, consumer pause, blocking-call loop, picture).

## Branch `claude/jpeg-reuse-mark` (2026-10-26)

- **`OpenIMP_Encoder_StreamIsReused(chn, &stream)`** (beyond vendor, `openimp/openimp_encoder_telemetry.h`): a JPEG that OpenIMP delivers again because the encoder was busy (or rmem short) carries the current time stamp; the query returns 1 for such a stand-in, 0 for a freshly encoded picture, -1 for a stream that is not the one held. Wish of the timps session. No change to `IMPEncoderStream`/packs; host test in `tests/t31/p2_jpeg_source_test.c`.
- **Device proof (T21 PC420 .24, `tools/jpegreuse`):** the reuse path only exists where a JPEG can be skipped (Helix JPEG on T20/T21/T30 with the shared bitstream buffer, T31 hardware JPEG); T40/T41 encode every JPEG in software and never re-deliver one, so there is nothing to mark there. New debug variable `OPENIMP_HELIX_JPEG_BUSY_WAIT_MS` (default 200) forces the path. With 0 and with 25 ms, 20 s at 1080p next to a running H.264 channel: 482 JPEG, 477 reused / 5 fresh; every `reused=1` picture byte-identical to the one before (hash) with a new, rising time stamp, every `reused=0` picture different, all valid JPEG, 0 query errors, 0 oops, timps restarted afterwards.

## Post-RC2 (2026-10-25/26)

## Post-RC2 (2026-10-26)

- **Channel rotation (`SetChnRotate`) 1.5-2.4x faster with MXU2 on T10/T20/T31 (branch `claude/mxu-simd`).** `nv12_rotate` gets an MXU2 path: 16x16 luma / 8x8 CbCr-pair blocks transposed in vector registers (4 rounds of SHUFV, generated and schedule-checked by `tools/gen_nv12_rotate_mxu2.py`), 180 degrees as one byte-reversing SHUFV per 16 bytes. The rotation is bound by memory traffic, so the access pattern counts as much as the shuffles: two block rows per band so every destination row gets a whole 32-byte cache line, those lines allocated with `PREF 30` (no fill), bands anchored at destination column 0 so the lines are aligned, loads in ascending order, and the 180 path prefetches the source row and writes front to back (a back-to-front stream runs at about a fifth of memcpy speed on T31X). Runtime selection (`src/core/openimp_mxu2.c`): the first rotation runs a bit-exact selftest MXU2 vs C (all modes, sizes with partial blocks, padding compared) with SIGILL caught; mismatch, SIGILL or `OPENIMP_MXU=0` keep the C path, one `[MXU]` line in the log. Which parts grant MXU2 to user processes (device-checked): T31X, T20X, T10L yes; T31L no (the kernel answers the first MXU2 instruction with SIGILL, the library falls back to C); T21/T23 no MXU2. Device (T31X .21, microbench, bit-exact on every size and mode): 1280x704 CCW/CW/180 7.35/9.77/6.92 -> 4.47/6.05/3.27 ms, 704x576 2.7/4.4/3.2 -> 1.7/2.3/1.4 ms, 1920x1080 21/33/16 -> 14/16/8.4 ms; T20X .107 704x576 4.2/6.4/4.1 -> 2.3/3.3/2.4 ms; T10L .31 640x368 3.5/5.0/3.2 -> 2.0/2.9/1.7 ms. Under timps (.21, video0 1280x704 rotation 270, 15 fps, 30 s per run): rotate 9.2 -> 5.5 ms/frame, whole capture hook (invalidate, rotate, copy back, flush) 10.8 -> 7.0 ms, FS(0)-tick thread 16.3 -> 10.6 % of a core, timpsd total 16.8 -> 11.1 %; stream 704x1280, 0 decode errors, 0 oops; 3-min continuous MXU2 run without SIGILL; T31L .26 falls back to C and keeps running. `OPENIMP_FS_ROTATE_STATS=1` now also prints the rotate time alone and the path (MXU2/C). Host test `tests/t31/nv12_rotate_test.c` runs every case through C and through a lane model of the MXU2 ops, plus selftest mismatch, SIGILL and `OPENIMP_MXU=0` fallbacks.

## Post-RC2 (2026-10-25)

- **T40: the first snapshot after an idle period is fresh too (branch `claude/t40-stale-snap`).** Same cause as on T41 (stream kept alive by `fs_keepalive`, no consumer: every capture buffer stays done, the first DQBUF hands out the oldest, seconds to minutes old; the grey first frame 12 s after night to day on the Eufy T40 on 25.10.). The T41 fix cannot be used as is: the T40 frame-channel DQBUF returns no completion time (wire words 5/6 are 0, measured on the device: the computed age was the uptime; the V4L2 adapter in open-tx-isp falls back to `ktime_get_ns()` for the same reason), whereas T41 fills them with CLOCK_MONOTONIC_RAW. T40 therefore bounds the age from the QBUF time instead: the driver fills the queued buffers in order, one per frame, so a buffer is done at the latest `buffer_count` frame intervals after its QBUF; a buffer queued longer ago than that plus max(300 ms, 6 intervals) completed more than that ago and is requeued (at most one pass over the queue, log line `dropped stale capture buffers (queued N ms ago)`; never fires in steady state, counted over 2 min on both channels). Host test `tests/t41/snap_test.c` (`t40_buffer_is_stale`). Device (Eufy T40XP, runtime libimp via bind mount, light untouched, 2 buffers per channel): 12 cycles of 30-90 s idle, day/night switched through the timps `running_mode` POST: every first snapshot matches the mode (night saturation 0, day coloured) and the log shows 18-74 s old buffers dropped in each cycle (the stock library would have delivered those). 5 min RTSP ch0+ch1: 20.00 fps, 6000 packets each, no gap above 72 ms (stock 59 ms), decodes. Known side effect: the drop at the start of a stream changes the phase of the encoder's poll against the frame completion; PTS jitter rises from sd 0.2/0.7 ms to about 1.0/1.6 ms (0.1-0.7 % of the frames deviate by more than 10 ms from 50 ms, none by more than 22 ms); isolated with a build that drops only at the start (same jitter) and one that never drops (low jitter), not a per-frame cost (clock_gettime 0.5 us).

- **T41: the first snapshot after an idle period is fresh again (branch `claude/t4x-stale-snap`).** With the stream kept alive and no consumer (timps `fs_keepalive=1`), the T41 frame channel fills every capture buffer once and stays full; the first DQBUF after the idle time returned the oldest of them, a picture as old as the idle time (a day-coloured JPEG in a night scene, a too dark first frame after dark). GetFrame now reads the buffer's completion time (CLOCK_MONOTONIC_RAW, wire words 5/6), requeues buffers older than max(300 ms, 6 frame intervals) and takes the next one, at most one pass over the queue (log line `dropped stale capture buffers`). Not the JPEG reuse path (that one is limited to 5 s) and not timps (the hub grab subscribes an empty queue). Device (.32, running_mode day/night via POST, light unchanged): before, first vs second snapshot differed (|A-B| 40.4 and 4.3 mean luma/chroma levels in 2 of 4 cycles, 90 s idle); with the fix 14 of 14 first snapshots equal the second (|A-B| <= 0.2) and match the mode (night sat 0, day sat 25-27); the log shows 14-92 s old buffers dropped. Host test `tests/t41/snap_test.c`.

- **T40/T41 software JPEG: 13-22 % less CPU per picture, output unchanged (branch `claude/t41-jpeg-cpu`).** The snapshot (JPEG) channel on T40/T41 is encoded by the CPU (the Allegro core is not used for JPEG there). The bit writer checked buffer capacity per output byte through a function call; it now reserves one worst-case MCU (4096 B) up front and writes unchecked. Output is byte-identical (md5 equal at 1920x1080, 1366x770, 641x359, 17x9, 2880x1620). Device (T41 .32, 1 core of XBurst2, same synthetic 1080p NV12 picture, user-space microbench): 0.189 -> 0.148 s per picture; 2880x1620 0.396 -> 0.310 s. Where the rest goes (1080p): load + DCT + quantise 0.098 s, entropy coding 0.045 s. The 20 % of one core that `timpsd` showed in its `jpeg` thread with one snapshot per 10 s is mostly not this cost: timps' jpeg thread keeps pulling JPEGs at `jpeg.fps` (5) during the 2 s idle-stop linger after each snapshot and throws them away (about 10 encodes of 0.17 s per snapshot, from the code and consistent with the measured ~2 s, not counted directly); reported to the timps session.

- **T41 H.265 FixQP: decodable again, and the vendor's lambda (branch `claude/t41-pframe-hevc`).** (1) H.265 FixQP streams on T41 did not decode (garbage after the first CTB rows, also before the H.264 lambda change): OpenIMP wrote `cu_qp_delta_enabled_flag 0` into the PPS under FixQP while the T41 core still codes cu_qp_delta; the vendor 1.2.6 PPS has 1. FixQP now keeps the flag (`OPENIMP_T41_HEVC_FIXQP_CUQPD=0` = old). (2) The vendor's `AL_GetLambda` (wrapped via LD_PRELOAD on the device, no memory tracer) runs the same dynamic lambda for H.265 as for H.264, bit-exact for all 52 QPs of IDR, P and full-weight P; OpenIMP now uses it for both codecs under FixQP. Device (.34, 1280x720 15 fps, interleaved): QP 28 P vendor 424/437 B, new 372/388 B, default table 547 B; I 110675 vs new 111085/107012 B (old 100459); QP 36 P vendor 99/110, new 117/116, old 97; I 50253 vs 49350 (old 43338). SSIM/PSNR against the captured source, P / I: QP 28 new 0.951 / 28.95 dB, 0.906 / 24.84 dB; vendor 0.948 / 28.9, 0.895 / 24.7; old lambda 0.949 / 28.85, 0.902 / 24.70. QP 36 new 0.907 / 27.6, 0.849 / 23.7; vendor 0.903 / 27.5, 0.845 / 23.6; old 0.899 / 27.5, 0.840 / 23.5.

- Test pattern: `OpenIMP_TestPattern_Supported(mode)` answers up front whether a mode works on this build/SoC/sensor (1/0, no switching), so a streamer UI can grey out switches; modes 0-3 always, mode 4 with a sensor table entry, mode 5 on T23. Host test.

- **T41 H.264 FixQP: P pictures as small as the vendor's (branch `claude/t41-pframe`).** At the same FixQP the P pictures were 37-65 % larger than with libimp 1.2.6. Cause: the vendor resolves its lambda mode to the dynamic lambda for constant-QP channels (`AL_GetLambda`, EP1 captured per picture on the device): lanes = isqrt(round(16 * 2^(QP/3)) * w), I pictures w = 51/90, P pictures 90/90 times clip((QP-12)/6, 2, 4), full weight (no clip) for the IDR and every uGopLength-th P after it (picture 30 of a 60-picture IDR period at uGopLength 30 x 2 scenes). OpenIMP wrote the integer default table (P lambda e.g. 5.0 instead of 6.13 at QP 28). Now `openimp_t41_update_ep1_lambda_fixqp` (bit-exact against the capture for all 52 QPs of the three picture classes) under FixQP; CBR/VBR keep the default table (as the vendor). FixQP also runs without the hardware rate-control words and EP3 pointer (cmd[176..182] = 0, as the vendor capture). Device (Cinnado .34 sc5336, 1280x720 15 fps, 10 s per run, interleaved): QP 28 P average vendor 629/601 B, new 620/610 B, old 995 B; QP 36 vendor 175/179 B, new 182/184 B, old 246 B; IDR now 21010 vs vendor 21030 B (old -3 %). P-picture SSIM/PSNR against the captured source (mean of 5 frame-source frames): QP 28 new 0.966 / 28.46 dB vs old 0.966 / 28.48 dB, QP 36 new 0.956 / 28.31 dB vs old 0.954 / 28.23 dB (vendor 0.963 / 0.952). The "first P after the IDR 4x larger" of the I-frame study was a frame-source start-up artefact (the vendor's second picture is a near-black frame), not the encoder. `OPENIMP_T41_FIXQP_LDA=0` / `OPENIMP_T41_FIXQP_HWRC=1` restore the old behaviour. HEVC FixQP is unchanged (not device-tested).

- **Test pattern mode 5 = ISP-middle picture (T23, beyond vendor):** `OpenIMP_TestPattern_Set(OPENIMP_TP_ISP_MID, p)` (`tpctl set 5`) writes the mode 1 pattern into the ISP's MDNS reference frame (the "ncubuf" OpenIMP allocates in AddSensor; Y/chroma addresses and stride read from the ISP registers `0x7820/0x7824/0x7828`, layout validated) once per frame from the channel 0 capture path and holds the MDNS reference weights (`0x799c` luma, `0x7a6c` chroma = `0x00FFFFFF`) at maximum, so the picture runs through MDNS, BCSH, scaler and encoder without any driver change; leaving the mode restores the saved weights. Register access through the `/dev/mem` fd of `IMP_ISP_EnableTuning` (the T23 ISP core has no register ioctl). ENOTSUP on other SoCs, ENODEV without a running ISP, EIO on an unexpected buffer layout. Device (T23 .30 sc1a4t, test libimp bind-mounted, timps restarted with the control socket): 8 s on-phase Y SSIM 0.943 mean / 0.958 median vs the ideal picture (live scene alone 0.42), U 0.87, V 0.89; 5 on/off cycles clean, weights restored, live picture back, no new oops, timps pid constant during the test. Known: 3 % of frames blend with the live scene when the driver rewrites the weights; chroma weights `0x7a6c` did not visibly purify chroma. Host tests `tests/testpattern`, `tests/t23`.
- **Test pattern mode 5: weights held by the driver (T23, open-tx-isp `mdns_ref_hold`):** mode 5 now sets the module parameter `/sys/module/tx_isp_t23/parameters/mdns_ref_hold` to 1 when it is entered (and 0 when left) instead of writing `0x799c`/`0x7a6c` every frame; the driver writes max weights itself on its gain-step rewrite, so the race is gone. With a driver without the parameter (older open driver, vendor driver) the old per-frame register write is the fallback. Device (T23 .30, test ko + test libimp): Y SSIM mean 0.967, min 0.9666, p1 0.9666 over 100 frames (before: mean 0.943, 3 % of frames ~0.4); U 0.816 / V 0.843 vs live 0.778 / 0.805 (chroma only about +0.04: the chroma leak is not in the reference weight words, see `audit/ISPMID_1025.md`); 5 on/off cycles clean, live picture back, 0 new oops. Host tests `tests/t23`.

- Test pattern mode 4 on T20/T21 jxf23 (and other SOI JX sensors): the SDK sensor driver clears the test bit (reg 0x0c) in `set_analog_gain`, and the `ae_hold` gain step arrives a few frames after the bit was set, so the colour columns never stayed. Mode 4 now re-asserts the bit until it has held for 4 polls (<= 4 s). T21 PC420 (jxf23): bit stays 0x01 for 8 s, vertical column pattern in the snapshot, mode 0 restores the scene.

- **T40/T41: `IMP_ISP_Tuning_SetDefog_Strength` / `GetDefog_Strength` (uint8_t *, 0..255, 128 neutral) as an extension** (not in any vendor libimp; T41 SDK 1.2.6 only has `Module_Set/GetDefog`, its kernel strength stubs return 0). Implemented over `Module_Ratio` index 4 (read-modify-write, other units untouched). Device (Vanhua T55A .32, test libimp via bind mount, defog chain on): readback 0/128/255 exact; picture, settled frames of two sweeps (night mode, ambient light): luma std 45.0 / 48.0 / 55.1, edge energy 12.8 / 17.4 / 24.0, p1 24 / 18 / 11 at 0 / 128 / 255; mean luma unchanged (98-99), no oops. `SetModule_Ratio` may return an error word although applied (set returns -2134069986 for non-neutral values, the Get reads the value back). The streamer key `image.defog_strength` is not wired on T41 in timps (reported to the timps session).

## RC2 (v2026.10.25-rc2, 2026-10-25; OpenIMP next 7c30aa99 + docs, open-tx-isp next 1bf4549d + docs)

- 278 OpenIMP and 636 open-tx-isp commits since RC1 (`v2026.10.11-rc1`). The exact code runs on all 19 test cameras as image agg46; the overnight soak of 2026-10-24/25 on the T40/T41 cameras was clean.
- T41 complete against the matrix (defog, DRC, DPC, NR, CCM/gamma/CSC, live crop, reload, versioned telemetry, vendor-equal RC parameter APIs); T40 on the open driver (reload, AE/AWB/day-night/NR/CSC against the vendor, encoder); T31 MXU2 AWB statistics by default with load-time self-test; changed defaults and interfaces are listed as upgrade notes.
- Matrix: T40 #3, #41, #42 now supported; #32/#33 "not RC2-relevant, sound tests pending"; T31 #41 stays a defect. Known limitations: T40 manual CCM effect small, sharpness 4-5 % below the vendor, motion pans not re-tested.
- Read: [docs/RELEASE_NOTES_RC2.md](docs/RELEASE_NOTES_RC2.md) (per SoC, beyond vendor, known limitations, upgrade notes) and the rendered matrix [docs/feature-matrix.html](docs/feature-matrix.html) (cells new since RC1 are badged).

## 2026-10-24 evening summary (OpenIMP next f55f620f, open-tx-isp next e5030fd4)

- Merged since the entries below were written: `claude/t40-wip-cells` (T40 encoder query/telemetry, devtest phases), `claude/t41-rcparams` rounds 3 and 4 (read-back after the next picture, learnt I-model scale, SC_PREVENTION gated on skipped pictures like the vendor), `claude/t41-load-reboot` (test tools stop writing files when RAM runs low), `claude/telemetry-abi`, `claude/t4x-bufshare`, `claude/t40-unknowns`.
- Driver side (open-tx-isp): T40 reload/unload, dead-client framechan release, tuning-control oops fixes, CCM/CSC/Gamma, AE EV fan-out, AE caps and `AeTargetComp`, DRC/Sinter/Temper ratios, DMSC night follow, T40 and T41 module size, T31 front crop 2.0x, ISP IRQ quiesce on T31/T23/T21 (QEMU finding): see [docs/OPEN_STACK_CHANGELOG.md](docs/OPEN_STACK_CHANGELOG.md) and the open-tx-isp CHANGELOG.
- Matrix: `docs/FEATURE_MATRIX.md` evening pass (T40 3 ✅+ / 29 ✅ / 4 ⚠️ / 4 ? / 5 —, T41 #27 ✅, T31 #20 ✅); `docs/OPENIMP_BEYOND_VENDOR.md` has the new robustness rows and the T41 scene-change note. A fleet soak of this state runs until 2026-10-25 10:00; the long-term stability cells (#41) stay ⚠️ until it is evaluated.

## Branch `claude/t40-wip-cells` (2026-10-24, merged into `next`)

- **T40 `IMP_Encoder_Query`:** `work_done`/`leftPics` follow the codec activity (the `encoding` flag, like T41) instead of "a PollingStream is waiting": on the Eufy 17.5 % of the samples read busy at 20 fps (8.7 ms per frame), before 100 %. `OpenIMP_Encoder_GetTelemetry` and `GetChnAveBitrate` device-checked (139 frames = consumer count, 20.02 fps, 1920 vs 2033 kbit/s).
- `tests/t40/devtest` phases 0x4000 (AE scene/caps, SensorFPS, LSC bypass, flip modes, WDR/DRC/NR ratios, mask, 14 getters) and 0x8000 (encoder telemetry); mask bits 0x20000..0x400000 guard the calls that oopsed the stock tx-isp-t40 (see open-tx-isp `claude/t40-wip-cells`).

## Branch `claude/t40-unknowns` (2026-10-24, merged into `next`)

- **T40 IMP_AI volume/gain like vendor libimp 1.3.1 (Eufy T40XP, microphone only, no playback):** `GetVol`/`GetGain` returned stale codec values (always 60 / 0 after a Set, the kernel `AMIC_AI_GET_*` ioctls do not read the request); `SetGain` did not clamp. Now as the vendor library, read back on the same camera with the vendor 1.3.1 `libimp.so` running the same test binary: device < 2 and channel 0 else -1, volume clamped to -30..120, gain to 0..31, the value is stored and `Get` answers the stored value (volume 60 and gain 0 before the first `Set`); a failing codec ioctl is only logged by `SetGain`. 50 frames of 640 samples in 0.79 s (40 ms pace), `SetVolMute`/gain/volume still change the captured level. T41 keeps the old code.
- **T40 `IMP_Encoder_SetDefaultParam` like vendor 1.3.1:** GOP mode DEFAULT (2), `encOptions` 0x40828, `encTools` 0x9c for every codec (read back with the vendor library: before OpenIMP returned 0/0/0), `iInitialQP` is the caller's value unchecked (`-1` stays `-1`, OpenIMP returned 25), a JPEG channel is forced to FIXQP. CBR/VBR/capped values (15/48, ipd -1, opt 1, 2 x kbps, 4/3 max, PSNR 42) already matched.
- Findings, not changed: the vendor T40 crashes (SIGSEGV) in `IMP_Encoder_SetChnQpBounds` for channel 8 (OpenIMP returns -1); vendor `SetPubAttr` accepts bit width 0 / sound mode 7 (OpenIMP refuses); vendor `EnableAec` needs the AI channel enabled and runs libaudioProcess `audio_process_aec_*`, OpenIMP T40 only toggles the kernel reference path (no echo processing yet; needs a playback test).

## Branch `claude/telemetry-abi` (2026-10-24, merged into `next`)

- **`OpenIMPEncoderTelemetry` is versioned:** two leading fields `size` (caller sets `sizeof(t)`, library writes back the bytes filled) and `version` (`OPENIMP_ENCODER_TELEMETRY_VERSION`, 1). New fields are only appended, so a streamer built against an older header keeps working; a call with `size` < 8 returns -1. Breaking change against the first (unversioned) layout of earlier today, which no release shipped.

## Branch `claude/t4x-bufshare` (2026-10-24, merged into `next`)

- **T40/T41 default stream count 2 (vendor); T41 `IMP_Encoder_SetAvpuBsShare`:** reference-frame sharing does not exist on T40/T41 (vendor libimp 1.2.6 / 1.3.1 have none); the vendor features are `SetbufshareChn` (already matched) and, T41 only, `SetAvpuBsShare`. OpenIMP used 4 stream buffers per channel on T40/T41, the vendor 2: stream rmem for 1080p+360p 5.36 -> 2.68 MB (rmem in use 28.84 -> 26.16 MB, vendor 25.56). `SetAvpuBsShare` follows the vendor (one shared rmem bitstream buffer, default 0x23b700, heap copies with phyAddr 0, CreateChn 0x80048008 if a buffer does not fit, 0x80040100 if the allocation fails); the default stream count under sharing is 2, the vendor's 1 (beyond vendor, user decision). Device (Cinnado T41NQ .34): decode clean, synthetic pattern share vs default SSIM 1.0. T40 by code only.

## Branch `claude/t41-rcparams` (2026-10-24, merged into `next` as 518df91) and 2026-10-24 test results

- **T41 RC parameter APIs like the vendor libimp 1.2.6** (`src/t40/openimp_p2_encoder.c`, T41 only): `IMP_Encoder_SetDefaultParam` identical to the vendor for all profiles x rate modes x initial QP (40 cases compared on .34); return codes (channel >= 8 `0x80040001`, not created `0x80040008`, frame rate with a zero part `0x80040010`); QP window clamp (max 0..51, min <= max); per-frame QP read-back = I window; `uMaxPictureSize` read back x1000; IDR distance = `uGopLength * uMaxSameSenceCnt` (`GetChnGopAttr`/`GetChnAttr` report the product). `SetChnBitRate`/`SetChnQpBounds`/... accept a value and ignore range errors like the vendor. **`IMP_Encoder_SetChnAttrRcMode` at run time = `SetChnBitRate` + `SetChnQpBounds` only** (initial QP, IP/PB delta, `eRcOptions`, `uMaxPictureSize`, mode change are ignored and not read back, as in the vendor). Method: the vendor library ran on the open driver (same camera), plus disassembly. Measured equal: IDR spacing, CBR 1500 kbit/s (vendor 1580, open 1570), QP bounds 30,30. Open (RE running): `iIPDelta` at `CreateChn` has no effect (vendor: I QP 20.5 against 23.8), `iPBDelta`/`eRcOptions`/`uMaxPictureSize` stored only, per-frame windows use the union of the I and P windows, run-time changes apply immediately instead of at the next picture. T40 shares the code path but the changes are `#if PLATFORM_T41` only (T40 needs its own check against libimp 1.3.1).
- **`tests/t41/dpcscan.c`** (new tool): counts pixels brighter than all 8 neighbours in full-size Y frames to measure the DPC unit. Result (T41, true-dark test): DPC on leaves no hot pixel, the module bypassed about 330 peaks per frame; the default equals ratio 128.
- **Documentation corrections:** `docs/FEATURE_MATRIX.md` third pass 2026-10-24 (T41 defog, NR, DPC, module size, CPU load, buffer sharing, RC parameters re-judged, see its first Summary entry). T41 defog is on by default in open-tx-isp. The CPU-load comparison uses free CPU (one idle-priority spinner per core) because `/proc/stat` does not show the vendor kernel-thread time on these `NO_HZ_FULL` kernels. Pending (not merged): branch `claude/t4x-bufshare` (`IMP_Encoder_SetAvpuBsShare` as the vendor, default stream count 4 -> 2 on T40/T41, saves 2.7 MB rmem for 1080p + 360p).

## Branch `claude/t41-enc-telemetry` (2026-10-24, merged into `next`)

- **T41 `IMP_Encoder_Query` shows the real encoder state; new `OpenIMP_Encoder_GetTelemetry` (beyond vendor):** `leftPics` and `work_done` followed "a PollingStream call is in progress", which is true almost all the time (a blocking poller), so a sampler saw leftPics 1 / work_done 0 in every sample (inert). Now `leftPics` is 1 and `work_done` 0 only while the codec holds a picture (source frame handed to the codec until its stream is back); `leftStreamFrames`/`leftStreamBytes`/`curPacks` describe a finished stream waiting for GetStream (real byte length). The new function returns cumulative per-channel counters (frames, bytes, IDR count, last frame size, encode time last/avg/max, delivered fps, average kbit/s). Other SoCs unchanged. Device (Cinnado T41NQ .34, sc5336 2880x1620, CBR 2000 kbit/s 15 fps, 5 s): 496 Query samples at 10 ms show work_done 0 in 192 (38.7 %), encode time avg 25.85 ms of a 66.7 ms interval = 38.8 %; counters equal the consumer's own count (75 frames, 1401684 bytes), fps 15.02, 2073 kbit/s; a polled-not-fetched stream shows frames 1 / 6873 bytes / packs 1, 0 again after GetStream. `GetChnEvalInfo`/`GetChnAveBitrate` are not part of the T41 1.2.0 SDK.

## Branch `claude/sensor-testpattern` (2026-10-19, not merged yet)

- **Test pattern mode 4 = sensor test pattern (beyond vendor):** `OpenIMP_TestPattern_Set(4, p)` switches the sensor's own built-in pattern on (per-sensor table: SmartSens sc2336/sc2336p/sc4336p/sc5336/sc830ai/sc1a4t 0x4501 bit3 ramp; SOI jxf23/jxf22/jxh42 0x0c bit0 colour columns), so a known picture runs through the whole ISP (BLC, WB, CCM, gamma, NR) and the encoder; frames are not replaced. AE is frozen at the values of the moment mode 4 starts (`ae_hold`, default 1: T23/T31 SetAeFreeze, T20-T31 manual integration time + gain ceilings at the current gains, T40/T41 AeExprInfo.AeMode manual), AWB optionally (`awb_hold=1`: T20-T31 manual WB with the current gains, T40/T41 awb_frz); leaving mode 4 clears the bit and restores AE/AWB exactly. Register access via the stock sensor register ioctl (IMP_ISP_Set/GetSensorRegister, T40/T41 0xc040540d), chip-id guarded and read back (a no-op driver path gives EIO, nothing written). `OpenIMP_TestPattern_SensorInfo()`, `tpctl set 4 [ae_hold=0|1] [awb_hold=0|1]`. Device: T20 .107 jxf23 pattern stable 70 s with AE hold (without it the driver's gain write cleared 0x0c after 1.5 s), AWB hold visibly changes the pattern colour; T23 .23 sc2336p ramp 40 s with the open-tx-isp T23 register-ioctl fix (0xc4 -> 0xcc, read back), 3A restored after. Host test `tests/testpattern`.

## Branch `claude/testpattern-api` (2026-10-19, not merged yet)

- **Test pattern runtime API (beyond vendor, all SoCs):** `OpenIMP_TestPattern_Set(mode, params)` / `OpenIMP_TestPattern_Get(state)` (public header `include/openimp_testpattern.h`, versioned structs with a size field, capability `OpenIMP_Cap_TestPattern`) switch the synthetic picture (0 off, 1 pattern, 2 pattern + moving square + frame counter, 3 motion test with built-in object or script) at any time while streaming; `OPENIMP_TEST_PATTERN` is only the start value. The frame path reads the mode as one relaxed atomic load (no lock; off = one comparison). Optional control socket `OPENIMP_TEST_PATTERN_CTL` + `tools/testpattern/tpctl get|set` drives a running process from outside. The OpenIMP banner (dot grid, lettering, tagline; rasterised from `docs/assets/banner.svg` by `tools/testpattern/mklogo.py`) sits centred on a white quiet zone (15 % of the picture height), palette + RLE, 3.8 KB in .rodata. Device (T23 .23, 1080p H.264, switching 0, 1, 2, 3, 0, 1 under one stream): every transition clean, no mixed frames, frame counter contiguous, decode error free. Host test `tests/testpattern`.

## Branch `claude/t41-rcparams` round 4 (2026-10-24)

- **T41 SC_PREVENTION gate resolved (vendor SDK 1.2.5 ChannelMngr.c `UpdateRateCtrl` -> o1II):** the vendor applies the scene-change step only to a picture the encoder skipped (update argument 5) or in its filler-data path; round 3 applied it to every P picture. The T41 OpenIMP encoder neither skips pictures nor inserts filler, so the bit now has no effect, as in the vendor library without skip/filler.
- **Analysed, not ported:** the vendor P-picture regime of eRcOptions (o1II +408: after a P picture with >= 81 % intra blocks the GOP budget is cut, which on .34 held P at QP 45 with near-empty pictures for 2.5 s after a light step) and DELAYED (o0OI +430: P QP corrected by the picture's hardware average QP, status +60, against the previous instead of the current picture QP). Both live in the 4 kB o1II / 4.4 kB o0OI routines whose state the recovered OpenIMP P controller does not mirror; an exact port needs the instruction-level approach used for T31 (tools/t31_rc_emu), now possible with the relocatable SDK 1.2.5 objects. OpenIMP recovers from the light step in about a second (P 43 -> 28-32) instead of holding QP 45.
- **I-picture size at equal QP (1000 kbit/s, detailed scene, open 194-208 kB vs vendor 141-149 kB):** SPS/PPS/slice headers are bit-identical (trace_headers) and the mean macroblock QP of the I pictures is the same (22.0-22.5 both, ffmpeg -debug qp), so the difference is in the hardware mode decision (intra lambdas / EP1 tables for I pictures), not in header flags or quantisation. Not fixed.

## Branch `claude/t41-rcparams` round 3 (2026-10-24)

- **T41 RC read-back after the next picture, learnt I-model scale, SC_PREVENTION (0b89cc9b):** RC setters (bit rate, QP bounds, per-frame bounds, MaxPictureSize, GOP, frame rate, SetChnAttrRcMode) keep the old read-back until the next picture went through the encoder, as the vendor applies its pending values (.34: GetChnAttrRcMode right after SetChnQpBounds(20,45) returns the old 18..45 in both). The I-model per-QP scale is learnt from consecutive samples (vendor estimator, 11180..20000). eRcOptions SC_PREVENTION (0x10): a P picture that looks like a scene change raises the next QP by 3 (vendor OIIo/IiIo), not with SCN_CHG_RES while the QP is at or above its start.
- Device (.34, vendor libimp on the same driver): I-buffer guard at 80 kbit/s triggers in both (I QP raised to P QP or max instead of P-4: vendor at 3 of 5 IDRs, open at 2 of 5). Scene change (eRcOptions 0, one light step 255->30): both streams decode clean, consecutive-frame SSIM P->I 0.978 (vendor 0.962). After the dark step the vendor P QP sits at 45 for 2.5 s with near-empty pictures (its P-path eRcOptions logic), OpenIMP rises to 43 and recovers to 28-32; the following IDR is 36 (vendor, after P 45) vs 25 (open, after P 26).
- **Not implemented (documented):** the vendor P-picture uses of eRcOptions (o1II +408: the regime that holds P at max QP after a scene change) and DELAYED (+430, the hardware-average-QP feedback with the previous picture QP); OpenIMP's recovered P controller has no equivalent state, an exact port is a larger job. Open I pictures at 1000 kbit/s on a detailed scene are ~35 % larger than the vendor's at the same QP (194 vs 149 kB); not investigated.

## Branch `claude/t41-rcparams` (2026-10-24, merged into next 7396214)

- **T41 CBR rate-control parameters like the vendor libimp 1.2.6 (matrix row 27, device-tested on cam-F .34, vendor libimp on the same open driver for A/B):**
  - **iIPDelta works:** the I picture is coded at the running P QP minus the effective delta (iIPDelta -1 = 4, intra-only GOP = 0), clamped into the I window (vendor setParam/getPictureQP). Before, every I picture had the P QP. 600 kbit/s 720p15, delta -1/0/2/6: first I QP 37 and first P 41/37/39/43, later I = P-4/P/P-2/P-6, identical to the vendor.
  - **Initial QP:** iInitialQP -1 takes the vendor bits-per-pixel table (bit rate / fps * 1000 / pixels, same table as T40) as the first I QP; the P controller starts at that plus the delta (was a fixed 38). 1500 kbit/s: first I 28, P 32 as the vendor.
  - **uMaxPictureSize drives the hardware rate control:** the per-picture HW target is 95 % of it (vendor GetTargetSize). CreateChn sets the I value (uMaxPictureSize * 1000 bits), SetChnMaxPictureSize I and P; a change restarts the CBR models like the vendor setParam. maxpic 300 kbit: I 36 kB (vendor 35 kB, before 92 kB); run time 40 kbit P: 4.68 kB (vendor 4.65 kB); 200 kbit I: 25 kB (vendor 24 kB).
  - **QP windows per picture type** (SetChnQpBoundsPerFrame, was the union of I and P) and **changes apply at the next picture:** header, command and HW RC use the QP and window latched when the picture is prepared, so a setter from another thread never splits a picture.
  - Default 1500 kbit/s: 1554 kbit/s, I 81 kB, P QP 21.7 (vendor 1548, 81 kB, 22.1); all streams decode clean, consecutive-frame SSIM equal to the vendor (I->P 0.999), at 600 kbit/s I-frame pumping gone (I->P 0.964 -> 0.993).
  - **eRcOptions and the I buffer guard (2f3ab3b):** an I model like the vendor's (bits and QP of every third IDR, or of a P picture with >= 81 % intra blocks); with eRcOptions SCN_CHG_RES clear or STATIC_SCENE set the I QP is the larger of the mean P QP since the last IDR and the QP the I model needs for 3/4 of min(CPB, GOP budget), minus the delta; in every mode the I QP is raised while the I model predicts more than 3/4 of the bit-rate history room. Static scene 1000 kbit/s: eRcOptions 0 gives I QP 20-21 at P 25 (vendor 21), default 1 unchanged (21). Low rate 150 kbit/s: I/P QPs as the vendor (I 27-29 at P 31-33). Light-step scene test with eRcOptions 0: streams decode clean, consecutive-frame SSIM as the vendor (P->I 0.990 vs 0.992).
  - VBR, CappedVBR and CappedQuality now also take the vendor initial-QP table and uMaxPictureSize (same AL code in the vendor). VBR 1000/1500 kbit/s: first I QP 32, first P 37 as the vendor (was 18); uMaxPictureSize 200 kbit: I 25 kB (vendor 23.7 kB). Capped modes not device-compared.
  - **Open:** vendor P-picture uses of eRcOptions (o1II +408), DELAYED and SC_PREVENTION bits not implemented; the I-model scale stays at the vendor start value (the vendor learns it); iPBDelta has no effect (no B pictures, as the vendor); RC read-back updates at once (vendor after the next picture). Details: session notes T41_RCPARAMS2_1024.

## Branch `claude/t40-ch1-gaps` (2026-10-19, not merged)

- **T40 encoder: pack timestamp is the frame's capture time, no manufactured frame-rate timeline (fixes the "1.1 s frame gaps" on ch0 and ch1 at night):** the T40 encoder stamped every pack as first-frame time + n x 1/fps. When the sensor ran below the configured rate (night, long exposure: 11.6 fps instead of 20), these stamps fell about 1 s behind the wall clock, the streamer's PTS check then re-anchored them, and the stream showed a 1.08 s PTS jump every 2.5 s (304 jumps in 958 s) although no frame was lost (driver ch1 pops = packets received). Now as the vendor and as T41 already did: the pack carries the FrameSource frame timestamp. Device (Eufy T40, open driver, timps, night, 10 min ch0+ch1): before 119+119 jumps of 1.08 s in 300 s, after 0 jumps > 0.2 s (largest step 0.105 s at a real 11.55 fps), 6919 of 6950 driver frames per channel received, 0 oops.

## Branch `claude/i2d-t4x` (2026-10-18, not merged)

- **T40/T41 FrameSource I2D: real hardware rotate 90/180/270, flip and mirror (branch `claude/i2d-t4x`, T41 device-tested on the Cinnado .34, T40 host only):** before, `IMP_FrameSource_SetI2dAttr` returned -1 and the attribute was ignored. Now as the vendor libimp (T41 1.2.6 0x72210 / EnableChn 0x7b6ac, T40 1.3.1 `i2d_resourcedeal`): the attribute (`IMPFSChnAttr.i2dattr` at CreateChn/SetChnAttr or `SetI2dAttr`, which keeps the vendor checks) is applied at `EnableChn`; every captured frame goes through the I2D block (`/dev/i2d`, built-in kernel driver `ingenic_i2d`, `IOCTL_I2D_START`) into one output buffer per capture buffer, and the consumers (encoder, IVS, GetFrame, SnapFrame) get the output. `picWidth/picHeight` stay the capture size; 90/270 delivers picHeight x picWidth, the encoder channel is created with that size. The block needs a 32-aligned output pitch (360/720 wide outputs were striped with ALIGN16): the output pitch is reported by the internal `OpenIMP_FS_FramePitch()` and used by the encoder (T41 command word 52/53, T40 descriptor), the IVS and SnapFrame. Frames must be 8 aligned (vendor rule), 90/270 width 16 aligned. Marker `OpenIMP_Cap_I2dRotate`. Device (sc5336, 1920x1080 / 1280x720 / 640x360): 90, 180, 270, flip, mirror, flip+mirror correctly oriented, streams decode, SSIM 0.97 against the transposed unrotated picture, 25 fps and 1-7 % system CPU with and without I2D. Host test `tests/t41/i2d_test.c` (also run by tests/t40).

## Branch `claude/streamer-real-funcs` (2026-10-16, on agg-39, not released)

From the streamer function audit; the first three items are device-tested on T10 .31, T20 .163, T21 .24, T31 .21.

- **`IMP_Encoder_Query` on T10/T20/T21/T23/T31** reports the real channel state (picture in the encoder, finished stream waiting with its byte length, `work_done`) instead of a constant block.
- **`IMP_Encoder_SetJpegeQl` on T10/T20/T21** applies the quantisation tables to the JPEG encoder (apitest "JPEG size follows SetJpegeQl live": table 2 gives 86-118 kB, table 90 gives 4-8 kB per picture).
- **`IMP_System_GetCPUInfo` on T21/T23/T31** reads the variant from the SoC registers (soc id, CPPSR, sub-type word; CPPSR is a clock-module part register that tells full from "-L" parts, not an MXU flag; the T21N has CU2 but reserved MXU2 instructions) with the vendor decoding, falling back to the old constant when /dev/mem is not readable.
- **T10/T20/T21 `SetAe_IT_MAX`, `SetBacklightComp`, T10/T20 `SetBcshHue`, `GetAeLuma`** now act (beyond vendor, needs open-tx-isp `claude/streamer-real-funcs`; see docs/OPENIMP_BEYOND_VENDOR.md). Device test pending (module change).

## After RC1: aggregate agg-38 (state of 2026-10-12, not released)

`claude/agg-38` = agg-37 (dc20d0e) + `claude/awbstats` (with `claude/awbzone`), `claude/t31-rc-collapse`, `claude/oimp-thread-names`, `claude/t21-cpu-fix`, `claude/t40-night` (with `claude/t40-devtest`), `claude/t41-isp-handlers`, `claude/funcsearch-run4` (documentation) and the device results of `claude/funcsearch-run5`. `claude/gaps-t1x-impl` is recorded as merged without changes: all of its commits were already in agg-35/agg-36 under other SHAs. The open-tx-isp side is open-tx-isp `claude/agg-38`. Build and host tests only (`make check`, `make release-check`, strict export check of T10 to T41); no soak. Device results only where an item says so.

- **In agg-38, described below:** GetAwbStatistics (T40/T41) and GetAwbZone (T23/T31) with the freshness contract, the T31 CBR debt bound, thread names, the T40 night fixes and new T40 calls, and the T41 1.2.6 ISP tuning calls (their sections and items further down).
- **Helix per-frame encode path at -O2 (`claude/t21-cpu-fix`):** on T10/T20/T21/T23/T30 the Helix frame submit, bitstream read-out, slice/CABAC header writer, frame-level rate control, hardware JPEG and the framesource dequeue/dispatch are built at `-O2` again (they run per frame in the application's threads). T21 `.text` +18 KB against agg-37, still 77 KB smaller than agg-36; exported symbols unchanged. CPU A/B on the device pending.
- **T40 `IMP_ISP_StartNightMode` and `IMP_ISP_Bypass_Bind` device-tested** (Eufy T40, vendor kernel, A/B with the vendor libimp; tested on `claude/funcsearch-run5`, which sends the same requests as the `claude/t40-night` code in agg-38): both callable between `IMP_ISP_Open` and `AddSensor` with rc 0 like the vendor; night mode gives black-and-white pictures until `SetISPRunningMode(DAY)`. `IMP_ISP_WDR_OPEN` (linear start) returns 0 like the vendor, but the linear-only sensor then delivers no frames with either library; it stays host-tested.

## T40 night branch `claude/t40-night` (2026-10-12, host only, in agg-38)

On `claude/t40-devtest` (cc50954). Host tests, `make check`, `export-check` T40 and T31/T40/T41 cross builds; no camera was used. Device test script for the next run: `tests/t40/devtest` (phases 4, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000; [docs/T40_STATUS.md](docs/T40_STATUS.md)).

- **T40 fixes of the device run of 2026-10-11 (vendor 1.3.1 disassembly first):** `IMP_ISP_Tuning_GetHVFlip` answers the cached mode (the stock kernel has no getter for 0x08000073; it returned -1), `SetHVFLIP` with the vendor checks; `GetBcshHue` answers the cached byte (no ioctl); `GetISPRunningMode` sends the get request without a payload and reads the mode from the payload word (the old pointer request left the caller's variable untouched); `IMP_Encoder_Query` fills the vendor fields (`leftStreamBytes`/`curPacks` 0, `leftStreamFrames` 1 while a finished stream waits for `GetStream`, `work_done`). The picture checks of HVFLIP, hue and running mode in the device test measured the two frames captured before the call (two buffers, no drain); the test now drops them.
- **New on T40 (host-tested):** `IMP_Encoder_Set/GetChnSeiAttr` (user_data_unregistered SEI in front of IDR or every picture, H.264 and H.265; the access-unit gate accepts it only while enabled), `IMP_ISP_Get/SetDrawAttr` (control 0x080000a0, RGB->YUV as the vendor), `IMP_ISP_StartNightMode` (ISP-device ioctl 0xc004542e before `AddSensor`), `IMP_OSD_SetGroupCallback` (called after the OSD group is drawn into a frame), `IMP_OSD_Get/SetRgnAttr_ISP` (line/rectangle via `SetDrawAttr`, cover via `SetMask`), `IMP_AI/AO_Get/SetDigitalGain` (raw codec register 0..255), `IMP_ISP_WDR_OPEN` (linear boot ioctl 0xc0045432, WDR boot = WDR enable), `IMP_ISP_Bypass_Bind` (ioctl 0xc0045431), `IMP_ISP_GetRaw` (one raw frame into an rmem buffer per sensor; unlike the vendor the buffer grows when a call needs more and the CPU cache is invalidated after the capture). `IMP_Encoder_SetFrameRelease` stores the ratio (capture frames already go back right after the encoder used them; same as T31).
- **Fixed while doing it:** the SEI codec entry points sat in a T23/T30-only block, which left `AL_Codec_Encode_SetSei` undefined in the T40 `libimp.so` (found by linking the device test; `nm -D` now shows no undefined internal symbol on T31/T40/T41).
## Branch `claude/t41-isp-handlers` (2026-10-11, in agg-38)

On agg-37 (dc20d0e); driver side: open-tx-isp `claude/t41-isp-handlers`. Host tests and builds only, not run on a camera.

- **T41 ISP tuning calls of the vendor libimp 1.2.6 now exported** (vendor control numbers and pointer pass-through from the 1.2.6 disassembly; need the open-tx-isp driver handlers of the same branch, host tests only): `Get/SetAeConvergeStep`, `Get/SetAwbConvergeStep`, `Get/SetAwbCtTrendOffset`, `GetAeEvList` (also as `GetAEEvList`, the name of the English 1.2.5/1.2.6 header), `GetAeAtList`, `Get/SetTmoCurve`, `Get/SetStatisConfig`, `WaitFrameDone`, and `GetHVFlip` (the T41 1.0.1 name of `GetHVFLIP`). `StatisConfig` and `WaitFrameDone` share the T40 code. The 1.2.0 headers lack the new types; local types with the 1.2.6 layouts are used (size-checked). Not exported: `GetAeBv`, `GetAEFlickerFlag` (they report vendor-AE state that the open driver does not keep). `AeConvergeStep` is stored only: the default open AE keeps its own step (matrix cell cache).

## After RC1: aggregate agg-37 (state of 2026-10-11, not released)

`claude/agg-37` = agg-36 + `claude/agg-37-size`, `claude/agg36-lib-drv`, `claude/agg37-t40`, `claude/funcsearch-release-check` (with `funcsearch-export-check`, `-t40-roi`, `-t40-ispctl`), `claude/agg37-rc-live`, `claude/agg37-smallfix`, `claude/agg37-shutdown`. Build and host tests only (`make check`, `make release-check`); no soak, device tests pending unless an item says otherwise.

- **T40/T41 `IMP_ISP_Tuning_GetAwbStatistics` verified and fresh, `OpenIMP_Cap_AwbStatistics` (branch `claude/awbstats` on top of `claude/awbzone`; T41 needs the open-tx-isp `claude/awbstats` driver; device-tested on the Eufy T40 and the T41 .32):** vendor semantics from the T40 1.3.1 (libimp 0x7e2c0) and T41 1.2.6 (0x55e78) disassemblies: `int32_t IMP_ISP_Tuning_GetAwbStatistics(IMPVI_NUM, IMPISPAWBStatisInfo *)` sends `{num, 1, 0x8000011, block}` with the pointer tuning ioctl (0xc0105436 / 0xc0105435) and the stock driver's `tisp_g_awb_statis_attr` -> `tisp_awb_get_zone` copies three 900-byte planes (`IMPISPStatisZone awb_r/awb_g/awb_b`, 15x15 u32 each, 2700 bytes, packed) = zone means `sum / count` of the raw, pre-white-balance R, G, B AWB statistics (T41 1.2.x AWB state +60096/+60996/+61896; the open tx-isp-t41 computes the same planes in `t41_awb_long_frame`). OpenIMP already routed the control (T40: vendor kernel driver, T41: open driver); verified now: `_Static_assert` on the 2700-byte layout, the request, and the contract that 0 is returned only with a block that holds statistics: the T41 driver answers `-EAGAIN` unless the planes come from a frame of the running stream within the last second, and libimp turns an all-zero block (the vendor T40 driver copies its state regardless, zeros before the first frame) into -1 / `EAGAIN`. New runtime capability symbol `const int OpenIMP_Cap_AwbStatistics = 1`, exported by the T40 and T41 builds only (weak extern or `dlsym`; absent = older OpenIMP or vendor libimp). Host tests: `tests/t40` and `tests/t41` `p3_controls_test` (request, block copy, zero block -1, driver error -1, argument checks, symbol), the `awbstats` / `awbstats-cost` commands of the `ispprobe` device shim; `make check`, `make export-check` (T40/T41) clean. Device, Eufy T40 (vendor kernel driver, `t40_devtest` with the vendor libimp 1.3.1 and with OpenIMP on the same night scene, IR): identical statistics, vendor global R/G 1.0786..1.0816 (0.28 %) B/G 1.1361..1.1408 (0.42 %), OpenIMP 1.0782..1.0836 (0.39 %) 1.1348..1.1410 (0.55 %) over 40 s; 36-45 us per call once a second, 7-9 us back-to-back (200 calls). Lamp (cellar bulb, OpenIMP, 40 s on, means over 28 reads against the 31-read baseline): warm 2700 K 50 % R/G +4.57 % B/G -10.13 %, warm 30 % +1.60 % / -4.51 %, cold 6500 K 30 % -2.16 % / -3.80 %, cold 50 % (repeat run after a Matter outage) -4.15 % / -8.89 %; off returns to the baseline within 0.2 % in the next read, the change shows in the first read after the switch. T41 (.32, open stack, timpsd with the shim): static R/G 1.0699..1.0728 (0.27 %) B/G 0.9692..0.9721 (0.30 %) over 40 s, 37 us per call cold, 13 us back-to-back (1000 calls, max 358 us), 3722 statistics frames, 0 dropped, 0 bad kernel lines; the T41 is not in the cellar (no lamp test). The timps 1.10.2 night colour trigger can use the call on both (pre-WB ratios, 4096 = 1.0 after its Q12 conversion).
- **T23/T31 `IMP_ISP_Tuning_GetAwbZone` correct and fresh (branch `claude/awbzone`, needs the open-tx-isp `claude/awbzone` driver; device-tested on cam-A and cam-B):** the vendor 1.1.x ABI `int IMP_ISP_Tuning_GetAwbZone(IMPISPAWBZone *)` (675 bytes: `zone_r/zone_g/zone_b[225]`, 15x15 byte means; the struct is now in `include/imp/imp_isp.h`), one request `{1, 0x800000b, block}` like the stock 1.1.6 library. The driver fills the block the stock way (`Tiziano_awb_fpga`: truncated `sum / pixels` of the AWB statistics, 0 for an empty zone) and refuses with `-EAGAIN` unless it comes from a statistics frame of the running stream within the last second, so the call returns 0 only with real, current statistics and -1 otherwise (never a zero-filled or cached block). New runtime capability symbol `const int OpenIMP_Cap_AwbZone = 1`, exported only by the T23 and T31 builds (probe it with a weak extern or `dlsym`; absent = older OpenIMP or vendor libimp). T40/T41: the vendor libraries have no `GetAwbZone` (per-zone AWB statistics are `IMP_ISP_Tuning_GetAwbStatistics(IMPVI_NUM, IMPISPAWBStatisInfo *)`, which OpenIMP T40 already routes to the vendor driver), so nothing changes there and the symbol is not exported. Cost on cam-A (T31): 2 us per call (1000 back-to-back calls, max 60 us), 11-15 us for a cold call once a second. Static scene on cam-A after convergence: global R/G and B/G (sums over the zones) within 0.21 % / 0.14 % over 9 s and 0.31 % / 0.46 % over 20 s; cam-B (T23) 25-32 us per call, 46-51 us cold, static within 0.19 % / 0.22 % over 35 s, and a lamp switch moves the raw ratios by several percent in the expected direction (warm 2700 K at 50 %: R/G +0.33 %, B/G -2.80 %; cold 6500 K at 50 %: R/G -1.51 %, B/G -2.27 % against an IR-lit baseline; off returns to the baseline within 0.2 %), so the block follows the current frame. Host tests: `tests/t30/isp_t31_awbzone_test` (ABI, request, -1 on -EAGAIN with the block untouched, capability symbol), `tests/t23/isp_tuning_test` (block size, symbol), the `awbzone` / `awbzone-cost` commands of the `ispprobe` device shim (`tests/device`). Scaling evidence: tx-isp-t31.ko 1.1.6 HLIL 0x18218 and libimp 1.1.6 HLIL 0x9428c (`docs/re`).
- **T31 CBR under an impossible scene (fix):** when the pictures stay far above the target even at the maximum QP (sensor noise with full sharpening), the buffer debt of the Allegro controller grew without limit; after ~700 pictures its 32-bit level arithmetic wrapped (also in the vendor code, `IIii` level * 3), the QP fell from the maximum to the minimum and the P pictures reached 16x the target, and after the scene got easy again the QP stayed at the maximum for minutes. The debt is now bounded to 4 buffers (12 s at the target rate, at most 256 Mbit): the QP stays at the maximum and comes down within ~15 s afterwards. The vendor library avoids the debt only with `IMP_ENC_RC_ENABLE_SKIP` (picture skip on underflow, not in the IMP defaults; not implemented). OEM traces unchanged; host test `t31-al-rc-overload-test`; device test pending.
- **Thread names:** every thread OpenIMP creates is now named (`prctl(PR_SET_NAME)` from inside the thread, helper `src/core/thread_name.h`), so `top -H` / `/proc/<pid>/task/*/comm` attribute CPU time. Vendor names where an equivalent exists: `FS(%d)-tick`, `IVS(%d)-ivs_processing` (T31, was `ivs_chn%d`), `ai-_ai_record_t` (audio capture); new `oimp-tuning`, `oimp-isptune`, `oimp-avpu-irq`, `oimp-ivs-feed` (T40/T41), `oimp-fdpump-<chn>` (T40/T41), `oimp-dmic-rec`, `oimp-helix-wrk` (T23 Helix helper process). No behaviour change; host and cross-build checked, device test pending.
- **Smaller `libimp.so`:** `-Os` for the library, `-O2` kept for the per-pixel, per-macroblock, audio-DSP and floating-point rate-control files (`OPENIMP_SIZE_OPT=0` builds all at `-O2`); Helix SoCs (T10/T20/T21/T23/T30) drop the AVPU session code (they have no AVPU); AVPU register diagnostics compiled out unless `OPENIMP_DEBUG=1`. Exported symbols unchanged. Table and per-file list: [docs/SIZE.md](docs/SIZE.md).
- **T23 OEM Helix worker bridge off by default** (`OPENIMP_T23_HELIX_BRIDGE=0`): the native Helix encoder is the only H.264 backend, no automatic worker fallback, `IMP_Decoder_*` fails on T23 (it ran in the worker; matrix cell now err). `OPENIMP_T23_HELIX_BRIDGE=1` builds the bridge again.
- **T10/T20 ISP (needs the open-tx-isp agg-37 driver; host test only, device test pending):** `IMP_ISP_Tuning_SetAntiFogAttr` (V4L2 control 0x98e903; the driver applies it between frames) and `IMP_ISP_Tuning_SetMeshShadingScale` (driver control 0x80000a3, mesh-scale field of register 0x380) are exported like the vendor 3.12.0 libimp.
- **T40 H.265 / 4K:** HEVC FixQP uses the vendor lambda tables (4K FixQP 30: bit-rate deviation +27 % -> -1 %); the stream buffer clears only the extent the last picture wrote; the drain checks the last payload byte instead of a fixed 2 ms sleep (`OPENIMP_T40_DRAIN_VERIFY=0` restores the sleep). Details: [docs/T40_HEVC.md](docs/T40_HEVC.md).
- **T40 `IMP_Encoder_SetChnMapRoi`:** per-16x16-block QP deltas (vendor 1.3.1 argument checks, same as T41) on the T31-style AVPU QP table, behind `OPENIMP_T40_ROI=1` like `SetChnRoiAttr`; host and cross-build only, device test pending (T41: the code exists behind `OPENIMP_T41_ROI=1`, but the T41 AVPU ignores the table; matrix cells `SetChnRoiAttr`/`SetChnMapRoi` T41 = cache).
- **`make release-check`:** release gate (`tools/release-check.sh`, BUILD.md "Release check"): `make check`, a build of every SoC, then `export-check` in strict mode. Plain `make check` stays host-only and fast.
- **`make export-check`:** compares the `IMP_` exports of each built `build/<soc>/libimp.so` with the vendor export lists (`tools/export-check/vendor-<SoC>.txt`) and with the `IMP_` names used by streamer sources (`STREAMER_DIRS`); fails when a name that is both vendor-exported and streamer-used is missing (documented exceptions in `streamer-ignore-<SoC>.txt`). Used to correct the matrix: T41 cells of FrameSource delay/max delay/I2D/timed frame/FIFO attr and ISP module control/auto zoom/mask block/scaler level are host (code exists), `Encoder_Get/SetChnCrop` is missing on T10/T20/T21 (was n.a.); 11 T40 OSD rows (`AttachToGroup`, `SetMosaic`, `*_ISP`) were missing but exported: host.
- **T40 `Encoder_Set/GetFisheyeEnableStatus`:** the vendor flag (settable before CreateChn only, channels 0..12); was not exported (a streamer that links it needs the symbol). Host test.
- **T40 `IMP_Encoder_Set/GetChnRoiAttr`:** the T31 macroblock QP-table path with the T40 command bits read from the vendor 1.3.1 library (AVC only); off unless `OPENIMP_T40_ROI=1`, cross-built, not run on a camera.
- **T41 `IMP_ISP_Set/GetISPBypass`:** the vendor pair (8-byte block on the ISP node, ioctl 0x80085418, vendor error codes, remembered value). The open-tx-isp T41 driver does not serve that ioctl yet, so Set answers -4095 there. Host test only.
- **T40 ISP controls:** `IMP_ISP_Tuning_Get/SetISPCSCAttr` (vendor T40 wire format: 10-bit coefficients, not the T41 one), `Get/SetHLDCAttr`, `Get/SetAeSpeed`, `Get/SetAeExpList`, `Get/SetFaceAe`, `Get/SetFaceAwb`, `Get/SetStatisConfig`, `SetFixedContraster`, `GetAfStatistics`, `GetAFMetricesInfo`, `WaitFrameDone`, `GetISPBypass` (stored flag), `IMP_ISP_Get/SetOSDAttr` and `IMP_ISP_SetPreDqtime` from the vendor 1.3.1 disassembly; 22 functions, control numbers and conversions as the vendor library. Host test only; the vendor kernel driver of the T40 board owns the semantics (device test pending).
- **Run-time rate control:** T10/T20 `IMP_Encoder_SetChnRcTrigLevel(chn, level, newMaxQp)` as the T20 3.12.0 libimp (the OEM controller lifts the QP cap from the application's max QP to the new max QP, default 51, while a GOP uses more than `level` x its budget, so a starved stream ignores a lower max QP in the stock library too; `newMaxQp` = max QP makes it a hard cap; host-tested on the OEM controller models). T41: a run-time `SetChnBitRate` regenerates the EP3 hardware rate-control row thresholds (they were built from the bit rate at channel start only; OEM `AL_ApplyNewGOPAndRCParams` / `PreprocessHwRateCtrl`), and a run-time bit rate or QP range change keeps the running controller QP instead of restarting at the constructor value 38.
- **`SU_Base_Shutdown` in `libimp.so` (all SoCs):** the vendor libsysutils call does `sync()` and `kill(1, SIGCHLD)`, which busybox init ignores. OpenIMP syncs and then sends init SIGTERM (busybox reboots) by default, so a camera that calls it comes back; `OPENIMP_SU_SHUTDOWN=poweroff` sends SIGUSR2 (power off), `OPENIMP_SU_SHUTDOWN=vendor` restores the vendor behaviour. Host test with a fake `kill`, not run on a camera.
- **T41 IVS matrix entry** corrected (device state vs host-only fixes).
- **Host tests:** the out-of-line `imp_log_emit()` is linked into every host test target.

## After RC1: aggregate agg-36 (state of 2026-10-11, not released)

Work merged after RC1 (`claude/agg-36`, on RC1 + docs). Build and host tests only for the aggregate; no soak yet. Not part of RC1.

- **T40 encoder:** frame-level rate control (CBR/VBR follow the target), AVPU command lists as the vendor libimp 1.3.1, H.265 with correct PPS and hardware rate control (device-tested on the T40 board).
- **T40 ISP:** ISP-drawn OSD regions, `Get/SetMask`, `SwitchBin` (device-tested); BCSH getters from the vendor cache, frame pixel format 0 (host-tested).
- **T41:** `SwitchBin`, `AutoZoom`, `SetMaskBlock`/`GetMaskBlock`, `SetScalerLv` in OpenIMP and open-tx-isp (host tests and module builds; not run on a camera).
- **Encoder:** `SetbufshareChn` keeps and checks the value like the vendor (T31/T40/T41); vendor return codes for T10/T20/T21 (`GetChangeRef`, `Get/SetChnHSkip`, `Get/SetChnDenoise`) and T23 `SetChnROI`/`GetChnROI`.
- **T30:** 317 of 334 vendor exports (was 271), host tests only (no T30 camera).
- **Log level:** `OPENIMP_LOG_LEVEL` (0..7, syslog numbering) gates `IMP_LOG`; default 5 = notice, per-frame information lines silent; `OPENIMP_DEBUG_TRACE=1` forces 7. Env table in [docs/OPENIMP_BEYOND_VENDOR.md](docs/OPENIMP_BEYOND_VENDOR.md).
- **Logging rework, smaller `libimp.so`:** `IMP_LOG` is one compare plus an out-of-line `imp_log_emit()` (no `getenv`/`openlog`/double printf per call site; level in a global int set by a constructor). New build switch `OPENIMP_DEBUG` (default 0): LOG_DEBUG and the developer traces (kmsg probes, startup trace, per-frame stderr helpers, codec/HW/DMA traces) are compiled out of the release build; `OPENIMP_DEBUG=1` keeps them. See section 7.1 of [docs/OPENIMP_BEYOND_VENDOR.md](docs/OPENIMP_BEYOND_VENDOR.md). T41 can never get `OPENIMP_AL_FRAME_RC` (build error instead of a possible context layout mismatch).
- **IVS:** a frame fed right after a result is no longer dropped (channel marked free before the result is posted; `OPENIMP_IVS_VENDOR_ORDER=1` for the vendor order).
- Details and checks: [docs/OPEN_STACK_CHANGELOG.md](docs/OPEN_STACK_CHANGELOG.md), per-function state: [docs/FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md).

## Release candidate RC1 (v2026.10.11-rc1, state of 2026-10-11)

RC1 v2026.10.11-rc1: openimp fe9307c (agg-35 687a68e4 + docs), open-tx-isp 191be6c5 (agg-35 6e464ac7 + docs). RC1 is exactly the soaked agg-35 code; the tag adds documentation only.

**Soak evidence:** agg-35 ran on 10 cameras from 2026-10-10 18:04 to 2026-10-11 08:00 (about 14 h): 0 kernel oopses, 0 unexplained reboots, 0 application restarts, VA-API decode check 54/54. The soaked images ran a streaming application on thingino `aperto` base a4bcc1c86.

**Supported SoCs:** release: T10, T20, T21, T23, T31. Experimental (not part of the release promise): T30 (no device in the lab, host/build evidence only), T40 (first device run only, see the matrix) and T41 (channel 1 and night colour open).

**Highlights since the last `aperto` state (c855925; open-tx-isp ca9cbf0e):** about 190 commits in each repository.
- **Encoder ROI:** T31 gets an ROI the vendor library does not have (AVPU QP table, H.264); T10/T20/T21 apply `SetChnROI` and the chroma QP offset on the Helix/NVPU; every path produces valid H.264 (deltas clamped, picture QP inside 0..51).
- **Front crop (open-tx-isp):** the T31 scaler stall on a too small window is refused with -EINVAL, the T23 crop hang is fixed, T20/T10 keep a window until the channel opens.
- **T20/T10 day/night:** `isp-m0` reports the analog gain cap the AE really reaches.
- **Hardening:** reviewed and host-tested patches (ABI struct sizes, encoder group bounds, IVS channel limits, `PollingStream` guards, idempotent IVS group create/destroy), channel-restart and reload robustness.
- **Audio:** AI/AO/AEC calls device-tested on T10, T20, T21, T23 and T31; T41 has no audio hardware on the test board.
- **T41 driver controls** (frame drop, sensor register, WDR enable, AF weight) device-tested; **T40** first OpenIMP device run (H.264 on two channels, OSD, IVS move, JPEG, ISP get/set).
- **Docs:** the feature matrix has T30 and T40 columns, strict Get/Set rows and the wording "not in this SoC's encoder" for reference-frame sharing.

**Known issues (RC1):**
- **T41 channel 1 (sub-stream) colour at night** differs from channel 0 (picture soak finding); T41 stays experimental.
- **T31, black picture of about 80 s** seen once during an application-driven day/night probe; not understood yet.
- **T23, dusk flapping:** on one T23 camera it switched between day and night several times at dusk. Day/night switching is decided by the application; the driver and OpenIMP report sensor values.
- **Day/night in a poorly lit location** also follows the application's thresholds; the driver and OpenIMP report sensor values.
- T40: CBR delivers only about 20 % of the target rate, H.265 not supported yet. T41: no audio hardware on the test board (API level only).

**Next (after RC1, agg-36):** work that is not in agg-35 and therefore not part of this release candidate: T40 rate control and H.265, T40 ISP-drawn OSD, further vendor-API gap work (T30, T1x, T23/T31), T41 mask/zoom, log level default, IVS result ordering and review fixes.

Details: [docs/OPEN_STACK_CHANGELOG.md](docs/OPEN_STACK_CHANGELOG.md); per-function state: [docs/FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md).

## Release candidate agg-34 (state of 2026-10-09 evening)

OpenIMP `agg-34` (838f8147) = agg-32 (8980cae) + encoder ROI clamp (fec3e69, 8545fb9) + reviewed DeepSeek patches (11abe42 and 17 patches); open-tx-isp `agg-34` (da9baf1e) = agg-32 (3cf9bdac) + front-crop fixes for T31, T23, T20 and T10 (d940ba20 to 10bac52f, 164225e5) + T20 `isp-m0` gain cap (da9baf1e). The first release still covers T10, T20, T21, T23 and T31; T41 is experimental and not part of it. The combination agg-34 has not run on the cameras as a whole; the individual fixes below were device-tested on the SoC named.

- **agg-32 soak (2026-10-07 16:07 to 2026-10-09 11:31, 43.4 h):** 0 unplanned reboots, 0 oopses, 0 guard trips, 0 snapshot failures and no memory trend on the seven timps cameras without user interaction; the only reboots were a planned OTA and two crashes from a front crop on T31 (fixed below). The picture check found the T20 day/night regression, a T10 black frame in the dark and the T41 channel-1 problem (known issues).
- **Encoder ROI:**
  - **T31** gets an ROI that the vendor library does not have (`IMP_Encoder_Set/GetChnRoiAttr` through the AVPU QP table, H.264, `OPENIMP_T31_ROI=0` refuses it): device-tested, relative -10 gave 2.7x the bit rate, 0 decoder errors in 20 VA-API checks.
  - **T10/T20/T21** apply `SetChnROI` and the chroma QP offset on the Helix/NVPU (T21 by default, beyond vendor, `OPENIMP_T21_ROI=0` switches it off): device-tested.
  - **Valid H.264 on every path:** window deltas -25..+25, spread at most 25, picture QP plus delta inside 0..51 and the rate control's min/max QP; a relative -26 used to give `mb_qp_delta` +26 at the window edge, which Intel VA-API, VLC, Firefox and Edge show as broken blocks (software ffmpeg hides it). Requests beyond that are clamped with a one-time warning.
  - **T23:** regions go to the vendor encoder, no visible effect (no-op). **T41:** code only, off unless `OPENIMP_T41_ROI=1`.
- **Front crop (open-tx-isp, device-tested):**
  - **T31:** a window smaller than a channel's scaler output stalled every MSCA output until reboot; the kernel now refuses it (-EINVAL), drops a crop that a larger main stream cannot fit, and writes the scaler steps so a valid window zooms. The picture effect on T31 is experimental, not proven.
  - **T23:** crop off unlocks the window, a window that does not fit is dropped instead of hanging, the lock is released with the ISP session (the 2026-10-06 crop hang is fixed).
  - **T20/T10:** a window is kept while no downscaled channel is open and applied at stream on, and released with the tuning session.
  - **T31/T23:** module parameter `fcrop_upscale_pct` (default 0) lets the geometry check tolerate a measured upscale (T23: up to 2.0).
- **T20 day/night:** `isp-m0` reported an analog gain cap of 158 while the AE stops at 128, so the streamer never saw the cap reached; it now reports the smaller value (host-tested; the streamer needs its matching change, timps 5bc4eff).
- **DeepSeek patches:** vendor ABI struct sizes (T40/T41), encoder group bounds (6; 8 kept on T23/T40), T31 IVS channel 64, `OSD_SetPoolSize(0)` rejected, idempotent T31 IVS group create/destroy, `PollingStream` guards, `GetDefaultBinPath`. Host-tested and cross-built; device checks pending.
- Details: [docs/OPEN_STACK_CHANGELOG.md](docs/OPEN_STACK_CHANGELOG.md), section "2026-10-07 to 2026-10-09: agg-32 soak and release candidate agg-34"; per-function state: [docs/FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md); ROI rules: [docs/ROI.md](docs/ROI.md); beyond-vendor items: [docs/OPENIMP_BEYOND_VENDOR.md](docs/OPENIMP_BEYOND_VENDOR.md).

### Known issues (agg-34)

- **T41 sub-channel (channel 1):** green or flat frames at each day/night switch and the exposure of channel 1 does not follow channel 0; T41 is not part of the first release.
- **T20 day/night** needs the driver fix (da9baf1e) and the streamer change (timps 5bc4eff); with agg-32 alone both T20 cameras stayed in Day mode in the dark.
- **T31 front crop:** the crash is fixed, the picture effect is not proven.
- **T10:** occasional switch to Day in the dark with a black frame (3 events in 44 h).
- The older candidate's known issues below apply unless stated here; the T23 crop hang is fixed.

## Summary by area (state of 2026-10-06 evening)

All test cameras run the open stack (open-tx-isp kernel driver + OpenIMP + timps) from full OTA images built from thingino `aperto`; no Ingenic or neo helper libraries remain, and T23 runs without helixd and without vendor libimp.

### Release candidate (OpenIMP `agg-27`, open-tx-isp `agg-28`)

| SoC | In the first release | apitest of 2026-10-06 | Notes |
|---|---|---|---|
| T10 | yes, **not re-tested** | not run | userspace is the T20 build, cells mirror T20 |
| T20 | yes | 212 PASS / 0 FAIL on two cameras | `GetAeZone` is a vendor no-op (N/A); encoder ROI and chroma QP offset |
| T21 | yes | 228 PASS / 0 FAIL on two cameras | `SetBrightness` acts and sepia works (beyond vendor); antifog/scene are vendor no-ops |
| T23 | yes | 304 PASS / 4 FAIL (AF getters, fixed by `source_af=1`) | stock-like release defaults, MSCA scratch buffer; H.265 has no hardware; **crop hang open** |
| T31 | yes | 274 PASS / 1 FAIL | `SetFrameDrop` with stock semantics; H.264 stall after JPEG teardown fixed (agg-29) |
| T41 | **no** (experimental) | 255 PASS / 12 FAIL on the experimental branch | channel 1 scaling registers staged, output-restart hang, 38 tuning IDs missing |

- **Frame source and encoder (all SoCs):** `GetFrame`/`SnapFrame` wait up to 2 s like the vendor library and the T20 capture no longer stalls (apps that set a frame depth got no frames before); `Encoder_GetFd` returns a pollable pipe; a JPEG channel feeds itself from the frame source when its video channel is not polled (beyond vendor); two IVS groups as every vendor libimp; `InsertUserData` SEI; `PollingModuleStream` honours the channel bitmap.
- **T20/T10/T21:** encoder ROI and H.264 chroma QP offset on the Helix/NVPU (T20 device-tested; T21 stores ROI/QPG/SuperFrame like the vendor and applies them after the release).
- **T21:** `SetBrightness` acts through the AE target and SEPIA works (both beyond vendor); `SetDPStrength`/`SetAntiFogAttr` exported like the vendor.
- **T23:** pool rmem parked as on T21, driver defaults are the stock-like set that ran 7 h overnight, STREAMOFF drain wait and QBUF cache invalidate (no cold-start snapshot 503), MSCA scratch buffer for stopped channels (beyond stock), AF statistics on by default, 19 ISP getters routed, `GetSensorRegister` offset fixed.
- **T31:** stack overruns of `GetAwbZone`/`GetAWBCt` fixed (a bus error in apitest), `SetFrameDrop` with the stock lsize/fmark semantics, `EnableDefog` takes its flag, vendor Movestate logic (host-tested).
- Vendor no-ops are matched, not "fixed": T21 antifog/scene, T20/T10 `GetAeZone`, T23 `GetBlcAttr`. T23 H.265 stays refused (no hardware).
- Details: [docs/OPEN_STACK_CHANGELOG.md](docs/OPEN_STACK_CHANGELOG.md), section "2026-10-06 afternoon/evening: release candidate agg-27/agg-28"; per-function state: [docs/FEATURE_MATRIX.md](docs/FEATURE_MATRIX.md); beyond-vendor items: [docs/OPENIMP_BEYOND_VENDOR.md](docs/OPENIMP_BEYOND_VENDOR.md).

### OpenIMP (userspace libimp)

- **Against the vendor library**
  - Functions that were stubs or silent no-ops in the vendor stack now act: T21 controls and noise reduction, T10 noise-reduction strength, `ae_it_max_us` on T21, T23 brightness/contrast/saturation/hue, getters that return what was set.
  - Smaller: libimp about 0.5-0.6 MB instead of 1.0-1.3 MB (`gc-sections`, tables generated instead of copied); more free video memory on T21 (2.76 MB instead of about 1.2 MB); reference-frame sharing on T21/T23 (about 1.5 MB less video memory, on by default).
  - More robust: checked inputs, rate-limited error logs, bounded waits, clean stop/reload (0 oops in 10 cycles per SoC); clear errors instead of silent `-1`/0 (for example H.265 on SoCs without HEVC fails at once so streamers fall back to H.264).
  - Beyond the vendor API (documented for streamer authors in OPENIMP_BEYOND_VENDOR.md): motion detection v2 with bounding boxes and strength (`OPENIMP_MOTION_V2=0` restores the vendor algorithm), rmem high-water logging and shortfall hints, effective rate control logged per channel.
- **Against the original opensensor/openimp line**
  - Encoders: real HEVC and hardware JPEG on T31; native Helix H.264 on T23 (no vendor helper); hardware JPEG on T20/T21/T23 without the vendor library; H.265 on T41 (AVPU).
  - Rate control: vendor Allegro core on T31, OEM controllers on T10/T20 (default), eprc on T21/T23 (0 oracle deviations), capped modes mapped properly instead of silent CBR.
  - Image and audio: OSD (text, bitmap, rectangle, line) on T20/T21/T30, real frame-diff motion detection on T20/T21/T30, software rotation on T31, real AECM echo cancellation on T31 (-18 dB echo), volume/mute and whole-fragment audio writes.
  - SoC coverage: T41 stream buffers, flip, BCSH and unload fixes; T30 builds against a real kernel (no device in the campaign).
  - Hardening from audits and an independent review: NULL crashes, buffer overflows (T31 AF/AE getters, module-chain dump, OSD size, ABI struct sizes), EINTR and `O_CLOEXEC` handling, T23 reconfigure race.

### open-tx-isp (kernel driver)

- **Against the vendor driver**: lifecycle hardening on all SoCs (locking, use-after-free, STREAMOFF races, last-close races, bounded tuning access, checked user copies); module reload clean on every SoC including T41; modules smaller than the vendor ones on T21 (452 vs 616 KB), T23 (622 vs 857 KB) and T31 (711 vs 829 KB); sensor module pinned while the ISP is open; sensor registry under `/proc/jz/sensor`; optional 8 MB MMAP pool on T10/T20; unknown control IDs are rejected instead of silently succeeding.
- **Against the original line**: exposure readback and vendor-format `isp-m0` on all T-series; T21 stock AE, ADR and control dispatchers lifted instruction by instruction (night flicker gone); T23 about 45 control IDs wired and the vendor AE lifted (default on); T20 simple AE/AWB with limits; T31 AF/AE statistics, SensorAttr and WaitFrame through the driver; T41 gc5603 tuning fix and the channel-restart hang fix.

### timps and thingino

- timps changes are made by the timps maintainers; OpenIMP only provides what the streamer calls (for example AE IT max reset to 0).
- thingino: packages `openimp` and `open-tx-isp` are in upstream branch `aperto` ([#1756](https://github.com/themactep/thingino-firmware/pull/1756)), pinned to the Lu-Fi forks; kernel VPU/rmem stability patches (#1748, #1752), optional boot guard (#1749) and SC2336 flip fixes (#1750, #1751) are merged there.

### Known issues (release candidate)

- **T23 crop hang:** the pipeline stops after a FrameSource crop change; under investigation, release blocker.
- **T31 H.264 stalls after the JPEG channel is torn down:** fixed: two threads polled the same encoder channel at once (`AL_Codec_Encode_Process` concurrently); `IMP_Encoder_PollingStream` now serialises per channel (openimp `claude/release-fix27` 5c2ccef), apitest on T31 FAIL 0 with the fix; goes into the next candidate (agg-29).
- **T41 is not part of the first release:** channel 1 scaling registers are staged, an output restart hangs the SoC, 38 tuning IDs are missing (branches `claude/release-t41` and open-tx-isp `claude/t41-ch1-fix`, not merged).
- T21: the OSD blend takes effect only about 2 s after a wake from idle (the first snapshot waits for it); cause not found.
- The colour image functions must be re-tested in daylight (the candidate's imgfx run was made in the dark); T10 was not re-tested.

### Known open items (older)

T23 sporadic single Helix encode error (errno 5; the frequent frame drops are fixed) and T23 real WDR; T41 flip, night column noise, short IVS gaps, day/night and AE/AWB quality; T21 a 4th module reload in one boot crashed once; AEC device tests on T23/T21/T20 (no speaker tests on shared cameras); first release tag after the 24 h soak. Details: the "Still open" section of the full log and the feature matrix.

## OpenIMP changes by date (condensed)

Only OpenIMP (userspace libimp) changes, newest first. Everything listed was device-tested on the SoC named unless marked otherwise.

## 2026-10-09

- Release candidate `agg-34` (838f8147): agg-32 plus the ROI clamp and the reviewed DeepSeek patches (see the section above).
- ROI: every window delta is limited to -25..+25 (spread at most 25, picture QP plus delta inside the rate control's min/max QP) on T31 and on the T10/T20/T21 Helix command lists, so hardware decoders no longer show broken blocks; under FixQP the T31 table is not clamped to the RC range. T31 measured: relative -20 became -19, 0 decoder errors in 20 VA-API checks.
- DeepSeek review: vendor ABI layouts, encoder group bounds (6; 8 on T23/T40), T31 IVS channel 64, `OSD_SetPoolSize(0)`, idempotent T31 IVS group create/destroy, `PollingStream` guards (host-tested; device checks pending).
- open-tx-isp (not OpenIMP): front-crop guards on T31/T23/T20/T10 and the T20 `isp-m0` gain cap, see the section above.

## 2026-10-07 to 2026-10-08 (agg-30 to agg-32)

- T10/T20/T21: encoder ROI and chroma QP offset on the Helix/NVPU; T21 `SetChnROI` effective by default (beyond vendor, `OPENIMP_T21_ROI=0` switches it off); `roitest` tool.
- T31: encoder ROI through the AVPU QP table (`IMP_Encoder_Set/GetChnRoiAttr`, beyond vendor, `OPENIMP_T31_ROI=0` refuses it); T41: experimental ROI table, off by default (`OPENIMP_T41_ROI=1`).
- Soak of agg-32 for 43.4 h on ten cameras (see the section above).

## 2026-10-06

- Release candidate `agg-27` (merge of the release branches); the apitest/imgfx FAILs of the day were fixed or classified (T20 212 PASS / 0 FAIL, T21 228 / 0, T23 304 / 4 with the AF getters fixed afterwards, T31 274 / 1). See the summary above and the full log.
- FrameSource T20 (`claude/t20-fs-frames`): `REQBUFS` count = pool size (+ max delay) and `SET_BANKS` as the vendor library; before, apps that set a frame depth got `nrVBs` frames and then nothing. `SetFrameDepth(chn, 0)` succeeds.
- FrameSource T20/T21/T23/T31: `GetFrame`/`SnapFrame` wait up to 2 s like the vendor library; the T20 capture thread no longer sleeps in `DQBUF` without a queued buffer (`claude/release-fs-enc-ivs`, `claude/release-quickwins`).
- Encoder: `IMP_Encoder_GetFd` (pipe plus pump thread); a JPEG channel reads the frame source itself when its video channel is not polled; `InsertUserData` emits a `user_data_unregistered` SEI on T20/T21/T23; `PollingModuleStream` honours the input bitmap; T31 `SetMaxStreamCnt` refused on a created channel.
- IVS: two groups as every vendor libimp (group 1 move/base-move device-tested on T20/T21).
- T31: `GetAwbZone` passed a stack struct of pointers where the driver copies 675 bytes (bus error); `GetAWBCt` always returned -1; `EnableDefog` always wrote 1 (`claude/release-t23-getters`, `claude/release-deepseek-defog`).
- T23: `GetSensorRegister` bus-type offset, `FrameSource_GetPool`, monotonic audio frame stamps (`claude/release-t23-getters`); the pool rmem is parked at `DisableChn` as on T21 (`claude/release-t23-vbm`, 40 streamer restarts, every snapshot OK, 0 oopses; `OPENIMP_VBM_PARK=0` frees at once).
- T21: `SetDPStrength` and `SetAntiFogAttr` exported like the vendor T21 libimp (DP strength reaches the DPC ratio, antifog goes to control 0x8000163 which the OEM kernel accepts without effect) (`claude/release-t21-image`). `SetBrightness` acts through the AE target (beyond vendor, driver change in open-tx-isp `claude/release-t21-brightness`; imgfx Y 28.8/117.7/193.0 for 30/128/225, timps `image.brightness` live 44.5/130.8/223.9). SEPIA works (open-tx-isp `claude/release-t21-image`).
- Core: `IMP_Log_Set_Option` exported; `DisableSensor` passes -1 to `DESTROY_LINKS` like stock (patches from an automated review, verified against the vendor disassembly).
- Release quick wins (host-tested only): T21 `SetModuleControl` cache fallback, `AI_SetHpfCoFrequency(0)` accepted on T40/T41, P3 capture restart guard, T21 OSD INFO lines trace-only.
- Pending (host-tested and cross-built only): `IMP_AI_SetHpfCoFrequency` takes effect on T23/T31 (float biquad in libaudioProcess-neo, beyond the original library); T31 Enable/DisableMovestate run the vendor logic; T21 `QpgMode`/`H265TransCfg` as the vendor; `SetbufshareChn` documented as store-only.
- imgfx: the green/magenta stripe in the top rows of the saved ch1 pictures was a tool error (chroma plane read at the wrong offset); streams and snapshots were never affected.
- Known issues of the candidate: T23 crop hang (under investigation); T31 H.264 stall after JPEG teardown fixed for agg-29, T41 not part of the release.

## 2026-10-05

- T10/T20/T21: front crop and CSC presets (beyond vendor, device-tested: crop, rejection of invalid values, 10 cycles), software rotation 90/270 on sub streams (T21 with OSD, colours correct), CSC mode 4 sign fix (green picture on T10/T20), encoder ROI and chroma QP offset (T20 device-tested; QP51 region coarse, QP15 fine, absolute QP bypasses CBR).
- T20/T21/T23/T31/T41: many vendor functions connected that were stubs, cache-only or missing (audit of every vendor IMP/SU function per SoC, see the feature matrix): AE/AWB zone and statistics getters, WaitFrame, ModuleControl, delay FIFO (`SetMaxDelay`/`SetDelay`/`GetTimedFrame`), `FlushStream`, AENC/ADEC on all SoCs, `IMP_AO_CacheSwitch`, T41 `SnapFrame`.
- T31: stack overruns fixed and verified with guard arenas: `GetAfHist` wrote 88 bytes into the 24-byte vendor struct, `Get/SetAeAttr` overran the caller by 80 bytes; AF statistics chain rebuilt (focus value, zones, weights, histogram).
- T23: `SetGamma` takes effect at once (steep/linear curves visible, falling curve rejected, restore ok).
- T21: the first snapshot after a start or an idle wake had no OSD (the IPU blend has no effect for about 2 s after a wake); OpenIMP now verifies the blend on a glyph pixel and withholds JPEG frames without a confirmed overlay (beyond vendor: the vendor stack shows the same missing OSD).
- T41: restarting one channel while the other streams hung the SoC; a driver fix (flip update only on a real change, output kept like the vendor on stream-off) passed the targeted repro 35/35 and a 40 min soak; the hang returns once the MSCA output geometry is latched correctly (see the known issues above, T41 is not part of the first release).

## 2026-10-09 (branch `claude/roi-qp-clamp`, host-tested only)

- ROI QP stays valid H.264 (`src/avpu_roi.h`, `Helix_H264_RoiSanitize`): the encoder codes `mb_qp_delta` against the previous macroblock and H.264 allows -26..+25 only; outside it ffmpeg hides the error but VA-API/VLC/browsers show broken blocks (measured on T10/T20 with relative -26 and absolute 15 at QP 42). Every window delta is now in -25..25, the spread of all deltas (uncovered = 0) at most 25, picture QP + delta inside 0..51 and the RC min/max QP; larger requests are clamped with a one-time warning.
  - T31 (and the experimental T41 table): `SetChnRoiAttr` refuses a delta outside -26..25 like the vendor T41 (before: -32..31); the table follows the picture QP when the clamp depends on it.
  - T10/T20/T21: `IMP_Encoder_SetChnROI` clamps when each command list is built (relative -25..25, absolute inside max QP - 25 .. min QP + 25 of the picture, spread 25); a QP argument beyond the s8 range keeps its sign.
  - T20 (review of agg-34): an absolute QP the clamp would push to the far side of the slice QP (cap 51 at slice QP 20: absolute 15 became 26, worse than the surroundings) is left at the slice QP instead, i.e. the region becomes a no-op rather than inverting the request. Test case absolute 15 at slice QP 20, cap 51.
  - Test `tests/t31/roi_clamp_test.c` (20000 random window sets, each table walked like the encoder).

## 2026-10-07 (branch `claude/roi-all`, not part of the first release)

- T21: `IMP_Encoder_SetChnROI` is effective by default (beyond vendor: the stock T21 libimp stores the regions but never programs them). With no region set the command list is unchanged; `OPENIMP_T21_ROI=0` switches it off. Device-tested on the PC420 (jxf23, 1920x1080, FixQP 30, one 320x320 region): absolute QP 51 is a visible block, QP 15 stays clean, delta +/-15 works, live changes (P frames, no IDR) work. FixQP bitrate 657 kbit/s without ROI, 340 with QP 51, 504 with QP 15; CBR keeps the target (the bits move between the regions). Test tool `tools/roitest`.
- T41: `IMP_Encoder_SetChnRoiAttr/GetChnRoiAttr` (vendor T41 API, 10 windows, delta QP) exist but are experimental and off (`OPENIMP_T41_ROI=1`): the macroblock QP table in EP2 was identified from a vendor capture and is written, but the AVPU did not react on the test camera; default behaviour unchanged (see `docs/ROI.md`).
- T31: encoder ROI (beyond vendor, branch `claude/t31-roi`): `IMP_Encoder_SetChnRoiAttr/GetChnRoiAttr` with the T41 API, 10 windows, delta QP -32..31 or absolute 0..51, live without IDR. The vendor Allegro core shows the switch: cmd[9] bit 25 = use the EP2 macroblock QP table, bit 24 = relative; the table is one byte per macroblock after the 0x40-byte EP2 header. Device-tested on cam-A (sc4336p 2560x1440): FixQP 30 with a 768x512 window, delta +20 / absolute 51 blurs exactly the window (IDR 176 to 156 KB), absolute 10 sharpens it (247 KB), two windows at once, CBR keeps the target; 0 oops. `OPENIMP_T31_ROI=0` switches it off.
- T41 (code only): the same enable bits found in the vendor T41 1.2.6 (`SliceParamToCmdRegsEnc1`: cmd[152] bit 0 = use table, bit 3 = relative); OpenIMP set neither, which explains why the T41 table had no effect. Set now under `OPENIMP_T41_ROI=1`, not yet device-tested.

## 2026-10-06

- T31: `IMP_FrameSource_GetFrame`/`SnapFrame` wait up to 2 s like the vendor libimp (T31 1.1.6 header: default timeout 2 s) instead of failing at once (branch `claude/release-quickwins`, host-tested only). The T31 encoder pulls with `VBMGetFrame` and keeps its own deadline. T41 already waits (about 1 s of DQBUF retries, `openimp_p1.c`) and is unchanged.
- Release quick wins from the 2026-10-06 review (`claude/release-quickwins`, host-tested only): T21 `IMP_ISP_Tuning_SetModuleControl` falls back to the cache when the open T21 driver rejects control 0x80000e2 (T31 still fails); `IMP_AI_SetHpfCoFrequency(0)` is accepted on T40/T41 (0 = default, as on T31); the P3 capture start is refused while the previous capture thread has not exited (no free of the buffer it still uses); the T21 OSD retry/probe and JPEG fan-out INFO lines are trace-only (`openimp_debug_trace_enabled`).
- T23: the FrameSource pool's rmem is parked at DisableChn, as on T21 (branch `claude/release-t23-vbm`). The open T23 driver keeps a stopped MSCA output enabled while the input runs, so the pool is no longer handed to other allocations while the MSCA may still write into it. The block is freed by `DestroyChn`, or at once with `OPENIMP_VBM_PARK=0`.
  - Tested on the Jooan A6M (raptor) together with open-tx-isp `claude/release-t23-driver`: 40 streamer restarts with every snapshot OK, 0 oopses.
- T21: `IMP_ISP_Tuning_SetDPStrength` and `IMP_ISP_Tuning_SetAntiFogAttr` are exported like the vendor T21 libimp (an application built against the T21 SDK did not load without them). DP strength (vendor percentage, cap 200) reaches the open driver's DPC ratio; AntiFog goes to control 0x8000163 like the vendor, which the OEM kernel accepts without effect.
- imgfx: the green/magenta stripe in the top rows of the saved ch1 pictures was a tool error (chroma plane read at width*height instead of after ALIGN16(height) lines); JPEG snapshots and streams were never affected.
- Pending (branch `claude/streamer-gaps-t1x-t23-t31`, host-tested and cross-built only, no device test yet): `IMP_AI_SetHpfCoFrequency` takes effect on T23/T31 (and, untested, T41): `IMP_AI_EnableHpf` designs the filter for the cut-off like the vendor library (illegal for a negative value or above the sample rate; 0 = default tables) and hands it to libaudioProcess-neo as a float biquad at the stream's rate (beyond the original library, which takes libimp's coefficient pointer; neo is recognised by its DRC export); `IMP_AO_EnableHpf` reaches neo the same way. T31 `IMP_ISP_Tuning_Enable/DisableMovestate` run the vendor library's logic (AE block of control 0x800002c, day/night limits from `/etc/sensor/<sensor>move.txt` loaded by EnableTuning, IDR on channel 0 on disable; the stock T31 driver ignores the control). T21 `IMP_Encoder_Get/SetQpgMode` keep any value like the vendor library (nothing reads it there), `IMP_Encoder_Get/SetH265TransCfg` check and drop like the vendor (Get returns zeros). `IMP_Encoder_Get/SetJpegeQl` on T10/T20/T21 was already applied to the running encoder (host test added). `IMP_Encoder_SetbufshareChn` on T31 is store-only in the vendor library too.
- T21 `IMP_ISP_Tuning_SetBrightness` now acts (beyond vendor; the vendor T21 kernel only stores the value): needs open-tx-isp `claude/release-t21-brightness`, no libimp change. Device-tested on the PC420 (jxf23): brightness 30/128/225 gives Y 28.8/117.7/193.0, timps `image.brightness` live the same. 128 is the vendor picture.

## 2026-10-04

- thingino: `openimp` and `open-tx-isp` are part of upstream `aperto` ([#1756](https://github.com/themactep/thingino-firmware/pull/1756)), pinned to the Lu-Fi forks; the T23 OEM Helix helper option and hybrid install are gone. All test cameras run `aperto` images (30/30 snapshots, 0 oops, 0 VPU errors).
- T41: stream buffers sized like the vendor, rmem arena split so an idle 1080p stream can always restart, no software H.264 stub (it produced a corrupt stream); AE compensation, gain/exposure caps and noise reduction reach the ISP.
- Pending (on a branch, not yet in `next`): the audio capture read size that always covers whole driver fragments (ported from an upstream patch, author credited), after the 24 h soak.
- T20/T10: rmem high-water logging and shortfall messages with a concrete suggestion when reserved memory is too small instead of silent degradation.
- Hardening: repeated driver errors rate-limited, silent `-1` returns logged, four NULL crashes, a buffer overflow in the module-chain dump, lost items on EINTR and an OSD size overflow fixed.
- T31: `SetChnQpIPDelta` now updates the value `GetChnAttrRcMode` returns, as the vendor does.
- T23: OSD stride fix (clean date/time text on main and sub stream); sub-stream reference fix; the OEM Helix helper option and the hybrid install are removed, T23 runs fully open. The frequent Helix frame drops are fixed (residual interrupt 0x100, kernel patch merged upstream); still open: a rare single Helix encode error (errno 5).
- T21/T23: sub-stream (640x360) corruption from reference sharing on the small channel fixed.
- T41: H.265 on the AVPU path (like T31); a stuck AVPU job times out after 2 s and resets the core. CBR overshoot fix, unload/flip/BCSH fixes in `next`.
- Review fixes: complete `O_CLOEXEC`, eprc `FRAME_END` for dropped pictures, T31 Allegro RC lock, forced IDR after a YUV error, T20 MB-RC table bounds.
- Size and CPU: T20 MB-RC table 590 to 58 KiB, no per-frame malloc; libimp T31 -34 KB, T41 -20 KB, T21 -17.5 KiB text; JPEG Huffman parsing by table, OSD cache invalidation, T31 EBSP copy (timps CPU T31 8.7 to 7.9 %, T21 17.5 to 15.5 %).
- Motion detection v2 (on by default; `OPENIMP_MOTION_V2=0` restores the vendor algorithm, bit-identical): background model per grid cell, suppression after IR/exposure switches, blob grouping, bounding boxes and strength through the versioned `OpenIMP_IVS_MoveGetResultEx` API; false alarms down in overnight runs.
- T30 readiness without hardware: builds for a real T30 kernel; fixed an `IMPEncoderCHNAttr` ABI size bug (4-byte overrun).
- `next` branch created; it tracks the tested aggregate (fast-forward only).

## 2026-10-03

- T10/T20/T21/T23 H.265: `IMP_Encoder_CreateChn(PT_H265)` fails with -1 and one log line on SoCs without HEVC hardware, so streamers fall back to H.264 at once (the vendor returns 0 and creates an empty channel).
- Rate control, T31: vendor Allegro core ported (VBR, CappedVBR, CappedQuality, CBR), state-identical against traces; closed-loop VBR; no filler NAL in CBR (deviation).
- Rate control, T21/T23 (eprc): vendor controller ported with 0 oracle deviations, default on T21; FIXQP, scene-cut IDR, runtime RC/fps/GOP/HSkip, `SetChnHSkip`; opt-in macroblock-level RC (`OPENIMP_EPRC_MBRC=1`).
- Rate control, T20/T10: vendor controllers as default (T20 I-aware P budget, T10 super-frame fix: re-encodes 800 to 0, CPU 8.3 to 5.5 %).
- T41: bitrate setting had no effect (negative bucket level discarded) fixed; OpenIMP runs against the vendor T41 driver with video, JPEG, OSD and motion detection.
- T10: picture drifting diagonally fixed (reference border added twice); noise-reduction strength acts (the vendor does nothing).
- T20: snapshot debounce no longer polls the JPEG encoder (14.4/15.0 fps instead of 11.2/14.3 with 1 snapshot/s); 270-line sub stream rounded to 272 instead of a scaler hang; bottom-row chroma fix.
- T23: brightness/contrast/saturation/hue act; JPEG shares the H.264 bitstream area (-1.44 MB); sub-stream rotation 90/270 on the native encoder; `IMP_Encoder_YuvSetCrop`; daylight green cast fixed by keeping white balance across stream restarts.
- T21: reference-buffer sharing on by default (~1.4 MB less video memory; `OPENIMP_REF_SHARE=0` disables); user contrast sent instead of default.
- Helix: encoder re-created after 3 failed pictures, channel stops after 2 fruitless re-creates; JPEG bitstream buffer 1 MiB (-328 KiB on a T10).
- Vendor logging functions built into libimp; `libalog`/`libsysutils` no longer needed on images.
- Size: `gc-sections` (T23 libimp 774 to 726 KB, T20 694 to 594 KB).
- Encoder diagnostics: effective rate control logged per channel, out-of-range QP/fps clamped with a warning.

## 2026-10-02

- T31: hardware JPEG default; real HEVC on the AVPU; software rotation 90/270; lambda tables generated from a formula; real AECM echo cancellation (-18 dB echo on a speech loopback).
- Helix (T20/T21/T23): hardware JPEG without the vendor library (~95 % less CPU for snapshots); dedicated JPEG/MJPEG channel gets frames; OSD lines, rectangles and bitmaps drawn on T31/T20/T21/T30.
- T23: native Helix H.264 encoder is the default, no vendor helper or vendor libimp; RC parameters reach the encoder; bitstream overflow handling (frame dropped and QP raised instead of a stuck channel).
- T21: shared bitstream buffer sized like the vendor (main/sub switching works), EMC scratch sized like the vendor (free rmem 1.56 to 2.76 MB).
- Robustness audits: AEC reference queue heap overflow, audio-effect switch use-after-free, double stop, DQBUF/EPIPE races at channel stop.
- Independent review: T23 reconfigure divide-by-zero race fixed; top-level `NOTICE` added.

## 2026-09-30 to 2026-10-01 (campaign start)

- ISP tuning: vendor defaults for contrast/sharpness; SDK control IDs and pointer semantics on T20/T21; T23 `GetSensorAttr` buffer overrun; T31 AF IDs and driver-backed SensorAttr/WaitFrame/ModuleControl.
- Day/night on T20/T21 re-sends image controls like the vendor.
- Encoder: CappedVBR/CappedQuality/SMART mapped to VBR with a log line instead of silent CBR; JPEG quality applied; channel-stat struct layout fixed.
- Motion detection: real frame-diff IVS on T20/T21/T30 (was always "no motion").
- Audio out: volume/mute applied, whole-fragment writes.
- T20/T21 framesource: pool parking and reuse instead of freeing on idle teardown.
- Tools: `t23tune` for T23 tuning on device.
