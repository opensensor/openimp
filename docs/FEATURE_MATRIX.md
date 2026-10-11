# Open stack vs vendor stack: feature matrix

As of: 2026-10-11, aggregate agg-36 after RC1 (OpenIMP and open-tx-isp `claude/agg-36`; build and host tests, no soak yet; device results only where a cell says so). Before that: RC1 v2026.10.11-rc1: openimp fe9307c (agg-35 687a68e4 + docs), open-tx-isp 191be6c5 (agg-35 6e464ac7 + docs); overnight soak clean. Detail: 2026-10-10 evening (release candidate agg-35, see the update paragraph in the summary; before that agg-34: OpenIMP `agg-34` 2a1b1cd and open-tx-isp `agg-34` b15ef235 on top of `next`, mandatory device tests of the night 2026-10-09/10 included; the per-cell details below are a chronological test log, newest results win). Camera mapping of the older sections: cam-A = T31, cam-B = T23, cam-C = T20, cam-D = T21, cam-E = T10, cam-F = T41; cam-G and cam-H are further T23 cameras (other sensors) that run the same stack. The agg-32 soak has its own letters (see the changelog). Addendum 2026-10-10 afternoon: front crop vs flip, AeComp T20/T31, agg-34.1 (T31 WDR statistics ring, regression fix, T21/T23/T20/T10 not affected), T41 experimental notes; T20/T21 encoder ROI QA flake is timing only (ROI applies at the next IDR).

Branch names in brackets (`claude/...`) and the tags `[-all-N]` are historic: those branches were merged into `next` and deleted. Where a cell says "not yet in an aggregate", "device test pending" or "opt-in", check the summary below and the defaults table in [OPENIMP_BEYOND_VENDOR.md](OPENIMP_BEYOND_VENDOR.md) first.

See also: the section "Missing / incomplete functions" below (vendor IMP/SU functions per SoC that are not fully done); [OPENIMP_BEYOND_VENDOR.md](OPENIMP_BEYOND_VENDOR.md) (integration notes for streamer authors on everything marked beyond vendor).

Open stack = open-tx-isp (kernel driver) + OpenIMP (libimp) + timps. Vendor = tx-isp-*.ko + Ingenic libimp.

## Summary

- **Update 2026-10-11 (agg-37, build and host tests, no soak):** OpenIMP `claude/agg-37` = agg-36 + size work, T10/T20 `SetAntiFogAttr`/`SetMeshShadingScale`, T40 HEVC FixQP and 4K drain, the function-finder branches (T40 ISP controls, T40 `Fisheye`, T40 ROI and `MapRoi` opt-in, T41 `ISPBypass`, `make export-check`/`release-check`). Corrections: T41 `Encoder_Get/SetChnRoiAttr` and `SetChnMapRoi` are cache, not host (the table is written behind `OPENIMP_T41_ROI=1`, but the T41 AVPU ignores it); T23 `Decoder_*` is err (the decoder ran in the OEM Helix worker, which is off by default since agg-37). **Gap counts (progress table):** T23 22 (was 14), T41 161 (was 158).
- **Update 2026-10-11 (function finder, `make export-check`):** the T41 cells of `FrameSource_Get/SetDelay`, `Get/SetMaxDelay`, `Get/SetI2dAttr`, `GetTimedFrame`, `Get/SetChnFifoAttr` and of `ISP_Tuning_Get/SetModuleControl`, `Get/SetAutoZoom`, `Get/SetMaskBlock`, `SetScalerLv` were stale (error/cache): the T41 build exports real code (vendor 1.2.6 semantics; the kernel side of AutoZoom/MaskBlock/ScalerLv needs the open driver handlers). Corrected to host (implemented, not run on a camera). `Encoder_Get/SetChnCrop` is exported by the vendor libimp of T10/T20/T21 but not by OpenIMP: cells corrected from n.a. to missing (T10/T20/T21 function count +2). T40: 11 OSD rows (`OSD_AttachToGroup`, `RgnCreate_Query`, `RgnRegister_Query`, `SetMosaic`, and the `*_ISP` calls behind the device-tested ISP-OSD) were missing in the cell but are exported by the T40 build: corrected to host (T40 progress-table gaps 85 -> 74). New check: `make export-check` compares the exports of each built `libimp.so` with the vendor export lists (`tools/export-check/`) and with the `IMP_` names used by streamer sources.
- **Update 2026-10-11 (agg-36, after RC1; build and host tests of the aggregate, no soak):** T40 rate control, AVPU command lists and H.265 (device-tested on their branch), T40 ISP-drawn OSD, `Get/SetMask` and `SwitchBin` (device-tested), T40 BCSH getters from the vendor cache (host), T41 `SwitchBin`, `AutoZoom`, `SetMaskBlock`, `SetScalerLv` (host and module build), `SetbufshareChn` as the vendor (T31/T40/T41), log level `OPENIMP_LOG_LEVEL`, IVS post order, T30 gap work (317 of 334 exports), T10/T20/T21 and T23 vendor return codes. The T23 driver rows of the second T23/T31 gap pass stay as before (that driver branch is held back). The T40 open items below about CBR, H.265, `GetBrightness`, the pixel format and `P1_INNER` are fixed in agg-36. **Gap counts after this update (progress table):** T23 14 (was 15), T30 19 (was 54), T40 109 (was 120), T41 177 (was 179); T10 32, T20 30, T21 14, T31 8 unchanged.
- **Update 2026-10-10 evening (agg-35: OpenIMP 687a68e4, open-tx-isp 6e464ac7, built 2026-10-10; soaked 2026-10-10 18:04 to 2026-10-11 08:00 on 10 cameras: clean (0 oopses, 0 unexplained reboots, 0 streamer restarts, VA-API 54/54). RC1 = this code plus docs; features that exist only in later branches (agg-36) are not in this matrix.**
- **T40, first OpenIMP device run (cam-K, T40XP, vendor kernel driver `tx_isp_t40`, vendor libimp 1.3.0, OpenIMP via `LD_LIBRARY_PATH`, 2026-10-10; every result is OpenIMP vs vendor):** PASS/dev: `AddSensor`/`EnableSensor`; FrameSource ch0 3840x2160 and ch1 640x360 with `GetFrame` (timestamps monotonic, non-zero); H.264 on ch0 and ch1 (10 s, clean strict decode); OSD PIC and COVER (OpenIMP draws COVER on NV12, the vendor library fails with "COVER cannot support this format"); IVS move (20 s); `FrameSource_SnapFrame`; JPEG on ch1 (software JPEG since 257ba9d); ISP `GetSensorAttr`, Gamma get, CCM get, ModuleControl get, `AeScenceAttr` get/set; `GetCameraInputMode`. Fixed during the run: the stream timestamp was 0 (a5d9b03); JPEG was a grey placeholder (257ba9d).
- **T40 open:** CBR delivers only about 20 % of the target bit rate (rate control); H.265 is not supported by OpenIMP T40 yet; `GetBrightness` returns -1; the pixel-format enum is reported as 10 (vendor 0); frame size is 16-line aligned (353280 B vs 345600 B); `P1_INNER` traces always go to stderr; JPEG at 4K not tested (out of memory in the test process, 60 MB RAM). Vendor note: `GetAeExpList` hung the vendor kernel driver; the magenta cast seen with timps stopped was the IR cut left open, not the library. Only the functions above are dev; the rest of T40 stays host or `?`. The combined Get/Set rows of Gamma, CCM and ModuleControl are host (Get dev, Set host), since only the Get is device-tested.
- **T41 audio and driver controls:** `AI_SetVolMute`, `AO_SetVolMute`, `AO_Soft_Mute`/`AO_Soft_UNMute` are a software mute as in the vendor library (the vendor uses no ioctl); API-level test only (rc 0, invalid argument -1), host in the tables, because the T41 test board has no audio hardware (no microphone, no speaker). The driver controls `FrameDrop`, `SensorRegister`, `ISP_WDR_ENABLE`/`_GET` and `AfWeight` are device-tested (`claude/t41-driver-gaps`, in agg-35). `Get/SetWdrOutputMode` has no stock handler in the driver (explained, counted as vendor no-op). `AutoZoom`, `SetMaskBlock` and `SetScalerLv` stay deferred: live MSCA reprogramming carries a hang risk.
- **Audio on T10, T20, T21, T23, T31 (AEC device test 2026-10-10):** audio output (`IMP_AO` enable/`SendFrame`, playback of a known signal at several volumes, `SetVol`) played a signal that was recorded back; audio input (`IMP_AI` enable/`GetFrame`, `SetVol`/`SetGain`, mono 16 kHz) recorded it; `EnableAec` returns 0. The AO/AI rows and the single-call cells are dev for these five SoCs; the combined Get/Set volume and gain rows stay as they were (host where they were aud). T21: a microphone gain above 0 drives the noise floor up (hardware, open and vendor stack alike). T41: no audio hardware on the test board, the T41 AI, AO and AEC rows are `—` (the earlier T41 microphone figures were the noise floor of an unconnected input).
- **Gap counts after this update (progress table):** T40 120 gaps (was 171; 74 by row), T41 179 gaps (was 187; 123 by row).

**T30 and T40 columns (added 2026-10-10)** were added from host/build evidence only (no T30 camera; the T40 cells were host evidence until the cam-K run of 2026-10-10). **T40: first OpenIMP device run done on cam-K (2026-10-10, T40XP, vendor kernel driver, vendor libimp 1.3.0 side by side; headers are 1.3.1)**. In the function tables T30 has 347 vendor functions with 19 gaps (after the gap work of 2026-10-10: 11 missing exports, 8 stored only; before: 54, all missing exports) and T40 has 398 with 120 gaps after the cam-K run (92 missing, 28 error: DMIC, ISP-OSD, mask and the open items below; OSD, IVS, CCM, gamma, sensor attr and `SnapFrame` are implemented since agg-35); the other T30/T40 cells are `?` (exported, not audited, not run), `host` or (T30) `aud`.

**Release candidate agg-34 (2026-10-09 evening, device results of the night 2026-10-09/10 added)**

- Final agg-34 after the review fixes (S1 T31 front crop survives the ISP session, S2 T31 zoom only with a scaled main stream, S3 Helix absolute ROI no longer worse than the slice QP): OpenIMP 2a1b1cd, open-tx-isp b15ef235 (test images with these on all 10 test cameras since 2026-10-09 night). The first state of the candidate was:
- State: OpenIMP `agg-34` (838f8147) = agg-32 (8980cae) + the encoder ROI clamp (fec3e69, 8545fb9) + the reviewed DeepSeek patches (11abe42 and 17 patches); open-tx-isp `agg-34` (da9baf1e) = agg-32 (3cf9bdac) + front-crop fixes for T31, T23, T20 and T10 (d940ba20 to 10bac52f, 164225e5) + T20 `isp-m0` reports the reachable analog gain cap (da9baf1e). The first release still covers T10, T20, T21, T23 and T31; T41 is experimental and not part of it.
- Soak of agg-32 (2026-10-07 16:07 to 2026-10-09 11:31, 43.4 h, stopped early by the user): 0 unplanned reboots, 0 oopses, 0 guard trips, 0 snapshot failures and no memory trend on the cameras without user interaction. The only reboots were a planned OTA and two crashes from enabling a front crop on T31 (fixed in agg-34, see below). The numeric picture check found the T20 day/night regression, the T10 black frame in the dark and the T41 channel-1 colour/exposure problem (known issues below).
- Encoder ROI (region QP): **T31** has an ROI that the vendor library does not have (`IMP_Encoder_SetChnRoiAttr` through the AVPU QP table, beyond vendor): device-tested, delta -10 gives 2.7x the bit rate, a relative -20 becomes -19 (clamped), 0 decoder errors in 20 VA-API checks; the night run of 2026-10-10 went through 20 phases (deltas, absolute QPs, clamp, off/on) with the stream valid in VA-API and in strict ffmpeg (`-err_detect`). Known deviation: under FixQP 42 a window of absolute 5 or relative -25 drives the P-frames to QP about 14 almost frame-wide (bit rate about x4, stream stays valid). **T10/T20/T21** apply `SetChnROI` on the Helix/NVPU: device-tested (T21 by default, beyond vendor); T20/T10 absolute ROI re-tested on 2026-10-10: valid H.264, no inversion (fix S3). All ROI paths now keep the stream valid H.264: every `mb_qp_delta` lies in -25..+25 (a relative -26 or an absolute QP far from the picture QP produced broken pictures in hardware decoders such as VA-API, VLC and browsers), picture QP plus delta stays inside the rate control's min/max QP. **T23**: regions on the native Helix encoder, device-tested on 2026-10-10 (QP map shows the window, streams valid in VA-API and strict ffmpeg). **T41**: code only, off by default, not device-tested. Rules and numbers: `docs/ROI.md`, `docs/OPENIMP_BEYOND_VENDOR.md`.
- Front crop: **T10/T20/T21/T23** device-tested on 2026-10-09 after the fixes (T23: crop off unlocks the window, a window that does not fit is dropped instead of hanging the MSCA, the lock is released with the ISP session, module parameter `fcrop_upscale_pct`; T20/T10: a window set before stream-on is kept while no downscaled channel is open, is applied at stream on and survives substream restarts (device-tested 2026-10-10); the release of the window at tuning-session close is **not verified**). **T31**: the crash is fixed (the kernel refuses a window the MSCA would have to upscale with -EINVAL; scaler steps and latch are written; module parameter `fcrop_upscale_pct`); device-tested on 2026-10-10: a valid window zooms both streams (**proven**), too-small windows are rejected without a hang, and the window survives a restart and a flip.
- DeepSeek patches (17, reviewed, plus one corrected): struct sizes and layouts of the vendor ABI (T40/T41 encoder stream, channel attributes, frame info, sensor info, OSD attributes), encoder group bounds (6 like the vendor on T20/T21/T30/T31/T41, 8 kept on T23/T40), T31 IVS channel 64, `OSD_SetPoolSize(0)`, `GetDefaultBinPath`, idempotent IVS group create/destroy, polling-stream guards. Host tests and `make check` are green on all SoCs and the cross-builds pass; the combined agg-34 ran on the test cameras in the night 2026-10-09/10 (mandatory tests passed, streamer smoke on 9 cameras 50 of 50 without a `PollingStream` spin); not yet checked separately on cameras: OSD groups, IVS, T31 `SnapFrame` offsets and the T41 OSD size.
- T20 day/night: the driver reported an analog gain cap of 158 in `isp-m0` while the AE stops at 128, so a streamer waiting for the cap never saw it reached and stayed in Day mode in the dark. `isp-m0` now reports the smaller of the two: device-tested on T20 on 2026-10-10 (maximum analog gain 128). The streamer needs its matching change (timps 5bc4eff) for the switch to work; day/night with both is **not yet confirmed**. On T10 (jxh42) `isp-m0` still reports 144 at boot (128 once it is set explicitly): open finding.
- Streamer smoke test on 9 cameras (night 2026-10-09/10): 50 of 50 checks passed, no `PollingStream` spin (the streamer CPU stayed normal at stream restarts).

**Known issues (agg-34)**

- **T41 sub-channel (ch1):** green or flat frames at each day/night switch and the exposure of channel 1 does not follow channel 0 (channel 0 is fine). T41 is experimental and not part of the first release.
- **T20 day/night** needs the open-tx-isp fix (da9baf1e) **and** the streamer change (timps 5bc4eff); with agg-32 alone both T20 cameras stayed in Day mode in the dark. Not yet confirmed on a camera with both.
- **T10 analog gain cap:** `isp-m0` of the jxh42 camera reports a maximum analog gain of 144 at boot (128 once set explicitly); T20 reports 128 (fixed). Open.
- **ROI under FixQP:** with FixQP 42 an absolute 5 or relative -25 window lowers the P-frames to QP about 14 almost frame-wide (bit rate about x4); the stream is valid, but a streamer should not offer the extremes on a fixed-QP stream.
- **T20/T10 front crop:** the release of the window at tuning-session close is not verified.
- **T10:** the soak saw a few rounds where the camera switched to Day in the dark and delivered a black frame (3 events in 44 h).
- **720p main stream** shows only the top-left corner when the streamer reads `/proc/jz/sensor/sensor0/*` before the sensor is started (streamer side; seen on T21 and T20).
- **Streamer ROI limit:** the library keeps ROI streams valid H.264 from agg-34 on (a relative QP of -26 used to break Intel VA-API, VLC, Firefox and Edge from the ROI row on); a streamer that offers -26 should offer -25 as its lowest value.

**Release candidate agg-27/agg-28 (2026-10-06 evening; the apitest figures below are from that day)**

- State: OpenIMP `agg-27` (affb8ff) and open-tx-isp `agg-28` (79b754b4). Release plan: `next` goes to `aperto` (fast-forward) after the long soak, tagged `vYYYY.MM.DD`. The first release covers T20, T21, T23 and T31; T10 is part of it but was **not re-tested** on the open stack today (its cells mirror T20 and were not re-measured); T41 is **not** part of the first release (experimental, see below).
- Device results of the day (apitest, one call per vendor function; the camera streamer was stopped, the release libimp and the release-candidate modules were loaded from RAM): T20 on two cameras 212 PASS / 0 FAIL / 17 N/A / 30 SKIP, T21 on two cameras 228 PASS / 0 FAIL, T23 304 PASS / 4 FAIL (the four AF getters, off by default in that run; with the new default `source_af=1` they pass on a second T23 camera), T31 274 PASS / 1 FAIL (see known issues). N/A means the node or API does not exist on the board or in the vendor library (no RTC/ADC node, SU battery functions not exported, T20 `GetAeZone`, T23 `GetBlcAttr`), SKIP means the call is deliberately not exercised (writes hardware registers, reboot, cipher). 0 kernel oopses on every camera.
- What the code level "dev" means in the function tables below: the call passed apitest or imgfx on a camera of that SoC on 2026-10-06 (or an earlier documented device test). Cells of T10 were not touched by this run. The imgfx colour run of the release candidate was made in the dark (night mode), so its colour verdicts are not used; the colour image functions have to be repeated in daylight.
- New since the last matrix: `GetFrame`/`SnapFrame` wait up to 2 s like the vendor library on T20/T21/T23/T31 and the T20 capture no longer stalls (apps that call `SetFrameDepth` get frames); `Encoder_GetFd` (pipe plus pump thread); a JPEG channel feeds itself from the frame source when its video channel is not polled; two IVS groups; `InsertUserData` SEI; `PollingModuleStream` honours the channel bitmap; T20 encoder ROI and chroma QP offset; T21 `SetBrightness` acts and SEPIA works (both beyond vendor); T31 `SetFrameDrop` with the stock semantics; T23 release defaults (stock-like start/stop set, STREAMOFF drain wait), VBM pool parking and MSCA scratch buffer, AF statistics on by default, 19 ISP getters.
- Vendor no-ops that are matched, not "fixed": T21 `SetAntiFogAttr` and `SetSceneMode` (the OEM kernel accepts and ignores them), T20/T10 `GetAeZone` (the stock module does not serve the control), T23 `GetBlcAttr`/`SetScalerLv` (no handler in the stock module). T23 H.265 stays refused: there is no HEVC hardware (n.a.).

**Known issues of the agg-27 candidate (older; superseded where agg-34 says so)**

- **T23 crop hang:** the pipeline stops after a FrameSource crop change (hard hang, found with the crop test); under investigation. It is a release blocker. **Fixed in agg-34** (open-tx-isp 25b1d8c7, 164225e5; device-tested on T23, retest on the release image pending).
- **T31 H.264 stalled after the JPEG channel was torn down:** fixed: two threads polled the same encoder channel at once (`AL_Codec_Encode_Process` concurrently); `IMP_Encoder_PollingStream` now serialises per channel (openimp `claude/release-fix27` 5c2ccef), apitest on T31 FAIL 0 with the fix; goes into the next candidate (agg-29).
- **T41 is not part of the first release:** MSCA channel 1 scaling registers are staged only (a downscale above 4:1 gives garbage, buffers with index 1 and above show a band), a restart of the output hangs the SoC (watchdog reboot at 1280x720 / 1440x810 on channel 1), 38 tuning IDs are still missing (gamma, CCM, CSC, module control, auto zoom, manual exposure, DRC, DPC, defog ratio, mask, scaler level), flip is not reset on restore, the FIFO delay of the frame source is missing. The T41 branches `claude/release-t41` (OpenIMP) and `claude/t41-ch1-fix` (driver) are not merged. T41 apitest on that branch: 255 PASS / 12 FAIL.
- **T21:** the IPU needs about 2 s after a wake before the OSD blend takes effect; the first snapshot is withheld until the overlay is confirmed (beyond vendor), the cause is not found. The OSD in the live stream after a wake is checked after the candidate tests.
- **T23:** the sporadic Helix encode error (errno 5): 0 errors in 6 days on cam-B since the 0102 fix (syslog: 342, 32, 37 and 419 errors on 10-01..10-04, then 0 from 10-05 to 10-10); provisionally closed, still observed; no real WDR, as before.

**What is complete**

- All five original test cameras (cam-A T31, cam-B T23, cam-C T20, cam-D T21, cam-E T10) run fully on the open kernel driver, OpenIMP and timps from flashed images; cam-F (T41) runs the same stack from the `aperto` full OTA image; no Ingenic/neo helper libraries any more, and on T23 no helixd and no vendor libimp either.
- Core functions are backed by evidence: H.264 on all six SoCs (soaks up to 2 h 53 without errors), HEVC on T31, hardware JPEG/MJPEG, second stream, OSD (text, bitmap, rectangle, line), real motion detection, day/night, flip.
- Beyond the vendor: reload/stop robustness (0 oops in 10 cycles each), smaller libimp (~0.5–0.6 MB instead of 1.0–1.3 MB), more free video memory on T21 (2.76 MB instead of ~1.2 MB), T31 module smaller than vendor, T21 controls and noise reduction take effect (the vendor ignores them), T31 AEC with −18 dB echo, reference-frame sharing on T21/T23 (~1.5 MB less video memory, on by default).

**Open items from 2026-10-04** (still valid unless the release-candidate section above says otherwise)

- T23: the frequent Helix frame drops had a fixed cause (a residual interrupt treated as an error by the bounded-wait kernel patch; 60 min with 0 errors after the fix); the sporadic single encode error (errno 5): 0 errors in 6 days on cam-B since the 0102 fix (syslog: 342, 32, 37 and 419 errors on 10-01..10-04, then 0 from 10-05 to 10-10); provisionally closed, still observed. The root cause of the cold-start snapshot 503 on a second channel (stale MSCA FIFOs) is fixed in the driver (`msca_fifo_rearm`, 260 cold-start cycles without a failure) and awaits its soak before it enters `next`. Real WDR is missing (dynamic ADR is lifted, no WDR sensor mode).
- T41 (cam-F): runs the fully open stack with H.264 (main stream High 1080p ok) and H.265; AE compensation, gain/exposure caps and 2D noise reduction work through OpenIMP; DRC, DPC, defog, WDR, CCM, gamma, HLC and BLC are not supported on T41 (the driver says so). Open: crop/rotation (I2D), temper effect, ioctl hardening awaits its device test, live flip (timps does not call `SetHVFLIP` live), night column noise of the gc5603 (ISP), short IVS gaps (~1.2 s), OOM with three parallel streams and 30 MB rmem (26 MB works in the current image; the kernel command line needs an environment-partition image), `AddSensor` EBUSY after an OOM kill; day/night, AE/AWB quality, audio untested.
- T10 (cam-E): AE/AWB quality and image controls only partly documented.
- T21: a 4th module reload in one boot crashed once (under investigation); `ae_it_max_us` acts, which is beyond the vendor and kept by decision.
- Rate control: T10/T20 run the OEM-style controller for CBR/VBR (SMART is mapped to VBR); T10/T20/T21 accept QP steps, staticTime/changePos/qualityLvl without all of them acting on every path; T31 CBR writes no filler NAL.
- AEC: only T31 is device-tested (T23 implemented, device test open; no speaker tests on the shared test cameras). Audio output only on T31.
- Not yet exposed by timps (static caps; on T41 AE compensation and sinter are wired, the gain/exposure caps follow): DPC strength, defog/Iridix floor, DRC strength and the scene/colour effects; the timps side is decided by the timps maintainers.
- T30 has no device in the test campaign (it builds against a real kernel; not device-verified here); T40 has one camera (cam-K) since 2026-10-10.
- Opt-in and not device-tested: `OPENIMP_EPRC_QP_DOWN1`, `OPENIMP_EPRC_MBRC` (ported, 0 oracle deviations, device test not finished), `OPENIMP_T23_HELIX_BSF`.

Defaults in `next` that older cells may still call opt-in or pending: T20/T10 OEM rate controller on, T10 super-frame fix on, T20 I-aware P budget on for CBR, T21 vendor-identical eprc on, T31 Allegro RC core on, reference sharing on T21/T23 on, motion detection v2 on (`OPENIMP_MOTION_V2=0` = vendor algorithm), T23 vendor AE as default (`source_ae_oem=1`), `isp_mmap_pool_kb=0` on T10/T20 (+8 MB free RAM), rmem peak logging and shortfall hints.

State on 2026-10-04: all test cameras run full OTA images built from thingino `aperto` (Lu-Fi forks pinned to open-tx-isp `next` 40cc77ec and OpenIMP `next` db760431) since 2026-10-04 17:39-17:47, with timps v1.9.31 and the kernel VPU/rmem patches as merged upstream; cam-F (T41) rootfs rev7 with rmem 26M. A 24 h soak has run since 17:50 (5 h so far: 0 streamer restarts, 0 encoder/VPU errors, 0 oops); the first release tag follows after it.

Not adopted: OSD edge flush, reference sharing on T10/T20/T31 (not in that SoC's encoder; the vendor has none either).

## Legend

- ✅ supported: works, matches vendor behaviour, tested or backed by the changelog
- ✅+ improved beyond vendor: more robust, leaner or more functional than the vendor stack
- ⚠️ known defect / deviation: runs, but with a known defect or a deviation from the vendor
- 🔧 in progress: branch is running or a test is pending
- 📋 planned: deliberately scheduled, not started yet (mostly low priority)
- ❌ missing: not implemented
- — hardware does not have it: not applicable
- ? unknown: not documented in the sources, deliberately not guessed
- [-all-13]: historic tag (tested before the later aggregates); everything so tagged is contained in `next`

## Matrix

| # | Feature | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 |
|--:|---|---|---|---|---|---|---|---|---|
| | **ISP core and image pipeline** | | | | | | | | |
| 1 | [ISP core / sensor bring-up](#1-isp-core--sensor-bring-up) | ✅ | ✅ | ✅ | ✅ | 🔧 | ✅ | ✅ | ✅ |
| 2 | [Reload / error handling (rmmod, stop/start)](#2-reload--error-handling-rmmod-stopstart) | ✅+ | ✅+ | ✅+ | ✅+ | ? | ✅+ | ? | ✅ |
| 3 | [Boot guard (protection against boot loops)](#3-boot-guard-protection-against-boot-loops) | ✅+ | ✅ | ✅+ | ✅ | ? | ✅ | ? | — |
| | **Exposure (AE)** | | | | | | | | |
| 4 | [AE control](#4-ae-control) | ✅ | ✅ | ✅+ | ✅ | ? | ✅ | 🔧 | ✅ |
| 5 | [AE compensation, backlight, highlight](#5-ae-compensation-backlight-highlight) | ✅ | ✅ | ✅+ | ✅ | ? | ✅ | — | ✅ |
| 6 | [Max gain, IT max, sensor FPS](#6-max-gain-it-max-sensor-fps) | ✅ | ✅ | ✅+ | ✅ | ? | ✅ | 🔧 | ✅ |
| 7 | [Anti-flicker (50/60 Hz)](#7-anti-flicker-5060-hz) | ✅ | ✅ | ✅ | ✅ | ? | ✅ | ? | ✅ |
| | **Colour and image quality** | | | | | | | | |
| 8 | [White balance (AWB, presets, manual)](#8-white-balance-awb-presets-manual) | ✅ | ✅ | ✅+ | ✅ | ? | ✅ | ? | ✅ |
| 9 | [CCM / LSC (lens shading)](#9-ccm--lsc-lens-shading) | ✅ | ✅ | ✅+ | ✅ | ? | ✅ | 🔧 | ❌ |
| 10 | [Day/night switching (ISP side)](#10-daynight-switching-isp-side) | ✅ | ✅ | ✅ | ✅+ | ? | ✅ | ? | ✅ |
| 11 | [IR cut / IR LED](#11-ir-cut--ir-led) | ✅ | ✅ | ✅ | ✅ | ? | ✅ | ? | ✅ |
| 12 | [Brightness / contrast / saturation / sharpness / hue](#12-brightness--contrast--saturation--sharpness--hue) | ✅ | ✅ | ✅+ | ✅ | ? | ✅ | ⚠️ | ✅ |
| 13 | [Mirror / flip](#13-mirror--flip) | ✅ | ✅ | ✅ | ✅ | ? | ✅ | ? | ✅ |
| 14 | [WDR / ADR / DRC](#14-wdr--adr--drc) | ✅ | ✅+ | ✅+ | ✅+ | ? | ✅ | 🔧 | ❌ |
| 15 | [Defog](#15-defog) | ✅ | ✅+ | ✅+ | ✅+ | ? | ✅ | — | ❌ |
| 16 | [Noise reduction (2DNR/3DNR, Sinter, Temper)](#16-noise-reduction-2dnr3dnr-sinter-temper) | ✅+ | ✅+ | ✅+ | ✅ | ? | ✅ | — | ⚠️ |
| 17 | [DPC (defect pixels)](#17-dpc-defect-pixels) | ✅+ | ✅+ | ✅+ | ✅ | ? | ✅ | — | ❌ |
| 18 | [Scene mode / colour effects (B/W, negative, sepia, vivid)](#18-scene-mode--colour-effects-bw-negative-sepia-vivid) | ✅+ | ✅ | ✅+ | ✅ | ? | ✅ | — | ❌ |
| 19 | [Privacy mask (ISP hardware block)](#19-privacy-mask-isp-hardware-block) | ✅ | ✅ | ✅ | ✅ | ? | ✅ | ❌ | ✅ |
| 20 | [Front crop / scaler level / CSC presets](#20-front-crop--scaler-level--csc-presets) | ✅ | ✅ | ✅ | ✅ | ? | ⚠️ | ❌ | ❌ |
| 21 | [Rotation 90°/270°](#21-rotation-90270) | — | — | — | ✅ | ? | ✅ | ? | ❌ |
| | **Video encoder and streams** | | | | | | | | |
| 22 | [H.264](#22-h264) | ✅ | ✅ | ✅ | ✅+ | 🔧 | ✅ | ✅ | ✅ |
| 23 | [H.265 / HEVC](#23-h265--hevc) | — | — | — | — | ❌ | ✅ | ❌ | ✅ |
| 24 | [JPEG / MJPEG / snapshot](#24-jpeg--mjpeg--snapshot) | ✅ | ✅ | ✅+ | ✅ | 🔧 | ✅ | 🔧 | ✅ |
| 25 | [Sub-stream / scaler (ch1 640×360)](#25-sub-stream--scaler-ch1-640360) | ✅ | ✅ | ✅ | ✅ | 🔧 | ✅ | ✅ | ✅ |
| 26 | [Rate-control mode (CBR/VBR/FixQP/Capped*/SMART)](#26-rate-control-mode-cbrvbrfixqpcappedsmart) | ✅+ | ✅ | ✅ | ✅ | 🔧 | ✅ | ⚠️ | ✅ |
| 27 | [RC parameters (QP steps, staticTime, changePos, qualityLvl, I bias)](#27-rc-parameters-qp-steps-statictime-changepos-qualitylvl-i-bias) | ✅ | ✅ | ✅ | ✅ | ? | ✅ | ? | ⚠️ |
| 28 | [OSD: text, bitmap, rectangle, line, cover](#28-osd-text-bitmap-rectangle-line-cover) | ✅ | ✅ | ✅ | ✅ | ? | ✅ | ✅+ | ✅ |
| 29 | [IVS / motion detection](#29-ivs--motion-detection) | ✅ | ✅+ | ✅ | ✅ | ? | ✅+ | ✅ | ✅ |
| 30 | [Frame source / VBM pool](#30-frame-source--vbm-pool) | ✅ | ✅ | ✅+ | ✅ | 🔧 | ✅ | ✅ | ✅ |
| | **Audio (documentation only, no tests)** | | | | | | | | |
| 31 | [Audio input (AI)](#31-audio-input-ai) | ✅ | ✅ | ✅ | ✅ | ? | ✅ | ? | — |
| 32 | [Audio output (AO, speaker)](#32-audio-output-ao-speaker) | ✅ | ✅ | ✅ | ✅ | ? | ✅ | ? | — |
| 33 | [Echo cancellation (AEC)](#33-echo-cancellation-aec) | ✅ | ✅ | ✅ | ✅ | ? | ✅+ | ? | — |
| | **Memory, size, load** | | | | | | | | |
| 34 | [libimp size (code + data)](#34-libimp-size-code--data) | ✅+ | ✅+ | ✅+ | ✅+ | ? | ✅+ | ? | ✅+ |
| 35 | [Kernel module size](#35-kernel-module-size) | ✅ | ✅ | ✅+ | ✅+ | ? | ✅+ | ? | ✅ |
| 36 | [Video memory (rmem) / MemFree](#36-video-memory-rmem--memfree) | ✅ | ✅ | ✅+ | ✅+ | ? | ✅ | ? | ✅ |
| 37 | [Reference-frame sharing (BUF_SHARE_CFG)](#37-reference-frame-sharing-buf_share_cfg) | — | — | ✅+ | ✅+ | ? | — | ? | ? |
| 38 | [CPU load (documented figures)](#38-cpu-load-documented-figures) | ✅ | ✅ | ✅+ | ✅ | ? | ✅ | ? | ✅+ |
| | **Stability, helper libraries, telemetry** | | | | | | | | |
| 39 | [Kernel soc_vpu / Helix hardening](#39-kernel-soc_vpu--helix-hardening) | ✅+ | ✅+ | ✅+ | ✅+ | ? | — | — | ✅ |
| 40 | [AVPU kernel driver (T31/T40/T41) review](#40-avpu-kernel-driver-t31t40t41-review) | — | — | — | — | — | ✅+ | 🔧 | ✅ |
| 41 | [Long-term stability / hangs](#41-long-term-stability--hangs) | ✅ | ✅ | ✅ | ⚠️ | ? | ⚠️ | ? | ⚠️ |
| 42 | [Helper libraries libalog / libsysutils](#42-helper-libraries-libalog--libsysutils) | ✅+ | ✅+ | ✅+ | ✅+ | ? | ✅+ | ? | ✅+ |
| 43 | [Tuning getters / readback](#43-tuning-getters--readback) | ✅ | ✅ | ✅+ | ✅ | ? | ✅ | 🔧 | ✅ |
| 44 | [Encoder telemetry / diagnostics](#44-encoder-telemetry--diagnostics) | ✅+ | ✅+ | ✅+ | ✅+ | ? | ✅+ | ? | ⚠️ |
| | **Region of interest** | | | | | | | | |
| 45 | [Encoder ROI (region QP)](#45-encoder-roi-region-qp) | ✅ | ✅ | ✅+ | ✅ | ? | ✅+ | ❌ | 🔧 |

Columns T10..T41 (T30 and T40 added 2026-10-10): one status symbol per SoC (legend above). The number and the feature name link to the detail section below, which holds the full per-SoC text and the vendor-stack behaviour.

**T30/T40 columns added 2026-10-10.** Evidence basis: host and build evidence only. There is no T30 camera in the lab, so no T30 cell is better than 🔧 (code present, or a live result documented by the T30 contributor in `docs/T30_STATUS.md`, not repeated by us) and nothing is ✅. T40 cells rest on the branch `claude/t40-t41-api` (merged into agg-35, not yet in `next`) and, since 2026-10-10, on the cam-K device run: host tests, the cross-build and the exported symbols of its libimp.so, plus the T40 contributor's live gate in `docs/T40_STATUS.md` (2026-07-30, stock `tx_isp_t40` driver). **T40: first OpenIMP device run on cam-K (2026-10-10)**: T40XP board, vendor kernel driver `tx_isp_t40`, vendor libimp 1.3.0, OpenIMP loaded via `LD_LIBRARY_PATH`; every T40 result is OpenIMP versus the vendor library; the cells that run as dev are exactly the functions listed in the update paragraph at the top. The rest stays host or `?`. `?` = nothing documented or only an exported symbol without a test; `—` on T40 = the call does not exist in the T40 1.3.1 vendor headers. Camera letters: none for T30 (no camera); cam-K = T40 (T40XP). The function tables below hold the per-call detail.

### Matrix details

Per feature: the vendor-stack behaviour and the full per-SoC cell text (chronological test log, newest results win).


#### ISP core and image pipeline

##### 1. ISP core / sensor bring-up

- **Vendor stack:** tx-isp-*.ko loads sensor + tuning bin
- **T10:** ✅ open driver boots, boot guard auto; day/night 10x without oops
- **T20:** ✅ bring-up stable; vendor-format isp-m0
- **T21:** ✅ first open bring-up, ISP core = lifted vendor code
- **T23:** ✅ exposure readback live, ~45 empty CIDs wired up, unknown CIDs return -EINVAL
- **T30:** 🔧 open-tx-isp T30 core builds against a T30 kernel and had a hardware bring-up earlier (open-tx-isp README); the T30 contributor ran the ISP + FrameSource path live (`docs/T30_STATUS.md`); not run by us (no T30 camera)
- **T31:** ✅ reference SoC; tuning gaps closed (RGB coefficients, AE ROI, SensorAttr)
- **T40:** ✅ device-tested 2026-10-10 on cam-K (T40XP; vendor kernel driver `tx_isp_t40`, vendor libimp 1.3.0 side by side, OpenIMP via `LD_LIBRARY_PATH`): `AddSensor`/`EnableSensor`, `ISP_Tuning_GetSensorAttr` and `GetCameraInputMode` run like on the vendor library (OpenIMP vs vendor); the T40 contributor's live gate in `docs/T40_STATUS.md` (2026-07-30). Vendor note: `GetAeExpList` hung the vendor kernel driver; the magenta cast seen with timps stopped was the IR cut left open, not the library
- **T41:** ✅ runs the fully open stack from the `aperto` full OTA image (rootfs rev7, rmem 26M) since 2026-10-04; no oops; day/night and AE/AWB quality still untested

##### 2. Reload / error handling (rmmod, stop/start)

- **Vendor stack:** known oops on reload (T21 open counter, stats DMA)
- **T10:** ✅+ boot guard auto; reload cycles not documented individually
- **T20:** ✅+ rmmod during stream rejected; 10x stop/start + 10x reload, 0 oops; all 53 user copies checked
- **T21:** ✅+ 10x stop/start + rmmod/insmod, 0 oops; cause of the old oops (stats DMA into freed memory) fixed
- **T23:** ✅+ 10x stop/start incl. kill -9, 10x reload, 0 oops; 2 out-of-bounds writes (2 KB/18 KB) fixed
- **T30:** ? not documented
- **T31:** ✅+ 10x reload with kill -9; vmalloc leak of 252 KB/cycle fixed; residual drift ~45 KB/cycle
- **T40:** ? not documented (vendor kernel driver in the first test)
- **T41:** ✅ rev2 image: 10/10 rmmod/insmod cycles OK, refcnt 0, 0 oops; kill -9 of the streamer recovers 3/3 (root cause: decompiled tuning-node helper overwrote .bss) [claude/t41-matrix-fixes]

##### 3. Boot guard (protection against boot loops)

- **Vendor stack:** not present
- **T10:** ✅+ isp_open=auto
- **T20:** ✅ S10isp-guard + isp_open=auto: optional package in upstream thingino `aperto` (#1749, default off); active on the test cameras
- **T21:** ✅+ S10isp-guard, u-boot isp_open=manual|auto|off
- **T23:** ✅ S10isp-guard + isp_open=auto: optional package in upstream thingino `aperto` (#1749, default off); active on the test cameras
- **T30:** ? not documented
- **T31:** ✅ S10isp-guard + isp_open=auto: optional package in upstream thingino `aperto` (#1749, default off); active on the test cameras
- **T40:** ? not documented
- **T41:** —


#### Exposure (AE)

##### 4. AE control

- **Vendor stack:** vendor AE in the kernel
- **T10:** ✅ device-tested: ae_comp moves IT and gain (374→748 lines, gain 42→142), clamps
- **T20:** ✅ compact AE: max gain, max IT (ae_it_max_us now effective), line_us, scene IT limit; low-light AE at dusk untested
- **T21:** ✅+ AE lifted 1:1 from vendor (incl. ae_tune2), night flicker gone; a·b·b bug found
- **T23:** ✅ lifted vendor AE is the default (all-14/15); night test on cam-B: switches to night, AE regulates (IT 1200/1436 lines, analog gain 133/160), gain reported; backlight/highlight/AE comp act
- **T30:** ? AE calls exported (shared T-series ISP code), no test result for T30
- **T31:** ✅ reference SoC; 2 h 53 soak without errors
- **T40:** 🔧 host: `Get/SetAeWeight`, `Get/SetAeExprInfo` ioctl layout tests; `GetAeExpList`, `Get/SetAeSpeed`, `Get/SetAeScenceAttr` declared but not exported; AE itself runs in the stock ISP firmware
- **T41:** ✅ AE regulates correctly on cam-F (it was saturated at max gain only because the room was dark); day/night and AE/AWB quality across a full day still untested

##### 5. AE compensation, backlight, highlight

- **Vendor stack:** IMP_ISP_Tuning_SetAeComp / Backlight / Highlight
- **T10:** ✅ device-tested: ae_comp 30/230 moves Y by −78/+143, highlight works; backlight cap absent
- **T20:** ✅ device-tested: ae_comp (IT 40→562 lines, gain 0→46); highlight has a small effect; backlight not tested (reflash pending) Re-tested 2026-10-10: monotonic, luma 56 / 105 / 245 for comp 0 / 128 / 255
- **T21:** ✅+ vendor dispatcher lifted; individual test not documented
- **T23:** ✅ device-tested with the lifted vendor AE: backlight 10 luma 68→112, highlight 10 →48, AE comp works (decided: lifted vendor AE becomes the default after a pending night-switch test in the dark) [claude/t23-matrix-gaps]
- **T30:** ? `SetAeComp` exported, no test result for T30
- **T31:** ✅ device-tested: ae_comp (Y +13/−21), backlight (Y +19), highlight (Y −13) Re-tested 2026-10-10: OEM formula, luma 55 / 100 / 160 for 0 / 128 / 255; the AE is slow (about 6 s dead time, 25 to 30 s to settle), tests need at least 30 s settle
- **T40:** — no AE compensation call in the T40 1.3.1 header set
- **T41:** ✅ AE compensation reaches the ISP through OpenIMP (`AeScenceAttr.AeTargetComp`), device-tested: comp 2 lowers the target 65 → 1; wired in timps main. Backlight (BLC) and highlight (HLC) are not supported on T41: the driver now returns "not supported" instead of a silent success

##### 6. Max gain, IT max, sensor FPS

- **Vendor stack:** MaxAgain/MaxDgain/AE_IT_MAX/SetSensorFPS
- **T10:** ✅ max_again and ae_it_max_us limit the AE
- **T20:** ✅ MaxAgain clamped, line_us=29 reported; ae_it_max_us limits the AE
- **T21:** ✅+ ae_it_max_us acts (claude/t21-ae-it-max 840a57ff; beyond vendor, the vendor ignores the RANGE block; the user decided to keep it): cam-D cap 2000 us gives IT 68 lines and dgain 19 to 63, cap 5000 us gives 172 lines, cap 0 returns to 1125 lines; max gain acts as before. Caveat: a 4th module reload in the same boot gave segfaults and a watchdog reboot (under investigation)
- **T23:** ✅ MaxAgain/MaxDgain, IT max, SetSensorFPS, additional ISP digital-gain stage; t23tune passed
- **T30:** ? max-gain calls exported, no test result for T30
- **T31:** ✅ EXPR setter, AE ROI, histogram edges
- **T40:** 🔧 host: `Get/SetSensorFPS`; no max-gain call in the T40 1.3.1 header set
- **T41:** ✅ gain and exposure caps reach the ISP through OpenIMP (`AeExprInfo`: `AeMaxAGain` linear Q10, `AeMaxIntegrationTime` in sensor lines), device-tested: 8x cap holds 6.9x; timps wiring follows

##### 7. Anti-flicker (50/60 Hz)

- **Vendor stack:** POWER_LINE / flicker dispatcher
- **T10:** ✅ device-tested: IT 748/675/896 lines for 60 Hz/50 Hz/off
- **T20:** ✅ device-tested: IT 843/1011/1012 lines for 60 Hz/50 Hz/off
- **T21:** ✅ lifted vendor dispatcher
- **T23:** ✅ device-tested: vendor AE 50/60/off IT 720/900/971; HLIL 720/600/711
- **T30:** ? exported, no test result
- **T31:** ✅ device-tested with 22 ms IT cap: IT 1000/900/750 lines off/50/60 Hz, gain compensates; daylight test pending
- **T40:** ? `Get/SetAntiFlickerAttr` exported, no test result
- **T41:** ✅ device-tested: off/50/60 Hz readback in isp-m0, AE integration time follows (2092/1575/1750 lines)


#### Colour and image quality

##### 8. White balance (AWB, presets, manual)

- **Vendor stack:** vendor AWB chain
- **T10:** ✅ device-tested: manual R/B gains, Cb/Cr −21..−28
- **T20:** ✅ default simple AWB; daylight A/B vs vendor chain: gains 492/393 vs 488/395, neutral ROIs within 0.007, both converge < 4 s; artificial light sweep 2200-6500 K (2026-10-04, smart bulbs): AWB follows (CT estimate 2300/2300/2500/4100 K), 4000 K neutral (R/G 0.99), very warm light stays slightly warm (lower limit ~2300 K, typical)
- **T21:** ✅+ AWB lifted (10/10 scenes register-identical) + hysteresis + IR night freeze; dusk test open
- **T23:** ✅ [-all-13] daylight green cast fixed on claude/t23-day-color (bc70f10b), tested on cam-B in sunlight: neutral colours, WB gains kept (0x710/0x7c0); not flashed yet
- **T30:** ? AWB calls exported (6 of 8 vendor names), no test result
- **T31:** ✅ device-tested: manual R/B gains + presets 3/4/7 read back; chroma follows (Cb/Cr), auto restores
- **T40:** ? `Get/SetAwbAttr`, `Get/SetAwbWeight` exported, no test result; `Awb_Get/SetRgbCoefft` not exported; AWB itself runs in the stock ISP firmware
- **T41:** ✅ black-picture incident not reproducible; timps does not call any WB function on T41 (only AWB attr in libimp)

##### 9. CCM / LSC (lens shading)

- **Vendor stack:** CT-controlled
- **T10:** ✅ CCM/LSC read back in isp-m0 (0x13380480.., 0x380–0x39c); CCM now updated every frame like vendor (was frozen at init matrix in IR scenes), follows day/night; LSC bypassed by the jxh42 IQ bank (vendor-identical); per-CT sweep pending daylight [claude/t1x-ccm-lsc-iridix]
- **T20:** ✅ CCM/LSC read back in isp-m0; CCM follows day/night (mono at night), LSC enabled, strength 1024 day/3440 night; mesh mirror follows ISP hflip at mode reload (vendor-identical); per-CT sweep pending daylight [claude/t1x-ccm-lsc-iridix]
- **T21:** ✅+ CT-controlled CCM/LSC lifted; colour blotches gone (chroma sigma 30→6)
- **T23:** ✅ CCM follows the IQ bank (as vendor); daylight with vendor AE neutral (sun, 2026-10-04: R/G 0.94, B/G 0.93, no green or blue cast). Green cast fixed earlier (WB gains reset on every stream start, claude/t23-day-color); blue AWB flip with vendor AE fixed (GIB black level cleared by the stream-enable write, claude/t23-ae-awb-flip); LSC flip locked
- **T30:** ? no CCM call in the T30 vendor export list; LSC `SetShading` exported, no test result
- **T31:** ✅ CCM follows day/night (regs 0x5004-0x5018 differ), LSC LUT loaded and follows mode + flip; per-CT sweep not testable on a fixed scene
- **T40:** 🔧 `Get/SetCCMAttr` implemented from the vendor 1.3.1 disassembly (40-byte payload, sign/13-bit conversion): Get dev, Set host (only the Get is device-tested on cam-K 2026-10-10, OpenIMP vs vendor library); the LSC calls are not run
- **T41:** ❌ CCM is not supported on T41 (the driver returns "not supported" instead of a silent success)

##### 10. Day/night switching (ISP side)

- **Vendor stack:** bank switch + mono matrix
- **T10:** ✅ 10 switches without oops; not re-tested after the drift fix
- **T20:** ✅ BCSH + Sinter/Temper re-sent as vendor does; day 128 / night 148/140
- **T21:** ✅ night mono (chroma 0), gain stable instead of 6↔25
- **T23:** ✅+ oops (wait queue) fixed, night mono, bank error → block bypass, user bypass persists
- **T30:** ? not documented
- **T31:** ✅ 13 switches in the 4.5 h soak
- **T40:** ? `Get/SetISPRunningMode` exported, `StartNightMode` not exported; no test result
- **T41:** ✅ isp-m0 shows the run mode; forced switch test pending

##### 11. IR cut / IR LED

- **Vendor stack:** via GPIO through timps/Thingino
- **T10:** ✅ device-tested: daynight night/day switches ircut and ir850, ISP follows
- **T20:** ✅ device-tested: as T10
- **T21:** ✅ device-tested: as T10
- **T23:** ✅ device-tested: timps auto night switches IR cut + ir850 + mono
- **T30:** ? not documented
- **T31:** ✅ device-tested: daynight night/day switches ircut + ir940 (no ir850 pin on this cam), ISP follows
- **T40:** ? not documented
- **T41:** ✅ device-tested: ircut + ir850 toggle; ISP mode not readable

##### 12. Brightness / contrast / saturation / sharpness / hue

- **Vendor stack:** IMP_ISP_Tuning_Set*
- **T10:** ✅ device-tested: brightness (+150 Y), contrast, saturation, sharpness; hue cap absent
- **T20:** ✅ defaults 0x80; sharpness works (edge energy 13/230/700); image.sharpness=128
- **T21:** ✅+ getters lifted; sharpness/contrast readable; 2026-10-06: `SetBrightness` now acts (beyond vendor: the AE luma target is scaled by value/128, 128 = the vendor picture; imgfx Y 28.8/117.7/193.0 for 30/128/225, timps `image.brightness` live 44.5/130.8/223.9) [open-tx-isp release-t21-brightness]
- **T23:** ✅ brightness/contrast/saturation/hue act now (they were reset on every stream start); contrast/gain feedback: driver takes the low byte like the vendor, OpenIMP remembers the gain before sending (user contrast 100 stays) (claude/t23-bcsh-aeit-fix)
- **T30:** ? exported where the vendor has them (no hue call in the T30 vendor list), no test result
- **T31:** ✅ defaults 0x80
- **T40:** ⚠️ `GetBrightness` returns -1 on cam-K (2026-10-10; the vendor library returns the value); `Contrast`, `Saturation`, `Sharpness`, `BcshHue` exported, no test result
- **T41:** ✅ brightness 255 → Y 211, contrast 0 → flat grey, saturation 0/255 chroma 0.1/7.1 (dark scene) [claude/t41-matrix-fixes]

##### 13. Mirror / flip

- **Vendor stack:** SetHVFlip / sensor flip
- **T10:** ✅ device-tested: flipped image correlation 0.964 on both flips; isp-m0 Mirror/Flip line now shows the applied state (was always Enable)
- **T20:** ✅ vflip UV address + DMA overwrite fixed (pink stripes); isp-m0 Mirror/Flip line now shows the applied state (was always Enable)
- **T21:** ✅ flip dispatcher lifted; shvflip=1
- **T23:** ✅ flip, Bayer re-sync, LSC flip locked
- **T30:** ? `Get/SetISPHflip`/`Vflip` and (2026-10-10, host test) `Get/SetISPHVflip` exported, no device result
- **T31:** ✅ sc4336p vflip no longer reports an error [-all-13]; MSCA flip takes effect only at the next channel start, as with the vendor
- **T40:** ? `Get/SetHVFLIP` exported, no test result
- **T41:** ✅ sensor flip registers follow live (hflip → 0x022c=0x01, vflip → 0x0063=0x02, off → 0x00); picture check in daylight pending [claude/t41-matrix-fixes]

##### 14. WDR / ADR / DRC

- **Vendor stack:** ADR/DRC/WDR paths in the vendor driver
- **T10:** ✅ isp-m0 WDR flag fixed (LINEAR 0x0e was printed as Enable) [claude/t10-t20-nr-wdr]; no DRC cap
- **T20:** ✅+ DRC strength drives auto Iridix ratio: Y 92/96/122, laplacian 555/558/656 at 0/128/255; isp-m0 WDR flag fixed [all-17]
- **T21:** ✅+ ADR lifted (40/40 emulator), DRC reaches the driver; day Y 120 instead of 235
- **T23:** ✅+ dynamic ADR lifted from the vendor module (44/44 emulator-identical, claude/t23-adr-defog); DRC strength 0/255 visibly effective on cam-B; sc2336 has no WDR mode
- **T30:** ? `Get/SetRawDRC` exported; `Get/SetWDRAttr` exported since 2026-10-10 (V4L2 control 0x8000169, host test), no device result
- **T31:** ✅ WDR buffer lazy; AE1 (short frame) stub = vendor no-op without WDR sensor
- **T40:** 🔧 host: `ISP_WDR_ENABLE`/`_GET` ioctl layout test; `ISP_WDR_OPEN`, `Get/SetWdrOutputMode` not exported
- **T41:** ❌ WDR and DRC are not supported on T41 (the driver returns "not supported" instead of a silent success)

##### 15. Defog

- **Vendor stack:** vendor block
- **T10:** ✅ vendor-identical: the jxh42 IQ bank bypasses Iridix (day and night), so DRC/defog have no picture effect – same as the vendor. Forcing Iridix on was measured (Y +1.4, edges +10 %) and rejected by the maintainer as not worth it.
- **T20:** ✅+ Iridix floor (no defog block in HW): 255 → Y +27, laplacian +106 [all-17]
- **T21:** ✅+ lifted, IRQ 21 registered (40/40 emulator)
- **T23:** ✅+ lifted incl. tisp_defog_soft_process (emulator-identical), IRQ 20 + process running on cam-B, defog strength works; 0xc bit 11 follows the bank like stock
- **T30:** ? `SetAntiFogAttr` exported since 2026-10-10 (control 0x8000163, host test), no device result
- **T31:** ✅ device-tested: defog 255 (Y −10, laplacian +193)
- **T40:** — no defog call in the T40 1.3.1 header set
- **T41:** ❌ defog is not supported on T41 (the driver returns "not supported" instead of a silent success)

##### 16. Noise reduction (2DNR/3DNR, Sinter, Temper)

- **Vendor stack:** table-driven
- **T10:** ✅+ Sinter/Temper strength acts (vendor: no-op): temporal noise 7.11/2.91/1.51 at temper 0/128/255, survives day/night [claude/t10-t20-nr-wdr]
- **T20:** ✅+ Sinter/Temper take effect (vendor: no-op) [claude/t10-t20-nr-wdr + openimp claude/t20-nr-strength]: temper 0/64/128/200 → 0/42/85/132, sinter 0/17/35/69 at high gain; 128 = IQ; kept across day/night
- **T21:** ✅+ 2DNR/gain tracking repaired; Sinter/Temper strength now takes effect (vendor ignores it) [-all-13]
- **T23:** ✅ gain index now log2 (before: noise reduction too strong from 2x), Sinter non-compounding, sharpness/DPC follow the bank
- **T30:** ? Sinter/Temper strength exported; `SetTemperDnsCtl` exported since 2026-10-10 (cache-only like the T30 `SetTemperDnsAttr`)
- **T31:** ✅ vendor-identical: SDNS H-S regs 0→0, 255→15 (OEM cap 16); temper 0: temporal std 4.60 vs 1.64; sinter effect small by OEM design
- **T40:** — no Sinter/Temper/2DNR call in the T40 1.3.1 header set
- **T41:** ⚠️ 2D noise reduction (sinter) works through OpenIMP (`Module_Ratio` index 0), device-tested: sinter 255 cuts wall noise from ~7 to ~1; wired in timps main. 3D noise reduction (temper, index 1) shows no measurable effect yet and is being checked in the driver

##### 17. DPC (defect pixels)

- **Vendor stack:** vendor block
- **T10:** ✅+ DPC strength via open driver (vendor: no-op): impulses 2846/2397/1784 at 0/128/255 [claude/t1x-beyond-vendor-ctrls, in all-17]
- **T20:** ✅+ impulses 6628/5153/3667 at 0/128/255 [all-17]
- **T21:** ✅+ m1 thresholds scaled like OEM T23; impulses 3857/3841/3498 [all-17]
- **T23:** ✅ follows the IQ bank as vendor, less night noise
- **T30:** ? `SetDPStrength` exported since 2026-10-10 (DPC ratio control, host test), no device result
- **T31:** ✅ vendor-identical: m1/m3 thresholds 0→(d1000,f5), 255→(d5,f1191); impulses −6 % (defect pixels only)
- **T40:** — no DPC call in the T40 1.3.1 header set
- **T41:** ❌ DPC is not supported on T41 (the driver returns "not supported" instead of a silent success)

##### 18. Scene mode / colour effects (B/W, negative, sepia, vivid)

- **Vendor stack:** SetSceneMode/SetColorfxMode (no-op in the vendor on T21)
- **T10:** ✅+ NEGATIVE/BW work, scene presets act (TEXT laplacian +14 %) [all-17]
- **T20:** ✅ colorfx 0–3 set/get ok, sepia visible; scene ok
- **T21:** ✅+ B/W, vivid, negative work (confirmed with light on); getters return what was set [-all-13]; 2026-10-06: SEPIA works (beyond vendor, was -1: B/W plus a tinted CSC matrix, imgfx dU/dV +10/+9 and +17/+19 in two runs); `SetAntiFogAttr`/`SetSceneMode` are vendor no-ops [open-tx-isp release-t21-image]
- **T23:** ✅ device-tested: B/W, vivid, negative work; invalid values give EINVAL (claude/t23-t31-scene-colorfx, not yet in an aggregate)
- **T30:** ? `Get/SetColorfxMode`, `Get/SetSceneMode` exported, no test result
- **T31:** ✅ device-tested: as T23 (claude/t23-t31-scene-colorfx, not yet in an aggregate)
- **T40:** — no scene-mode/colour-effect call in the T40 1.3.1 header set
- **T41:** ❌ no control path on T41 (no cap, no libimp function)

##### 19. Privacy mask (ISP hardware block)

- **Vendor stack:** 4 rectangles/channel, YUV fill
- **T10:** ✅ device-tested: both streams, green fill
- **T20:** ✅ device-tested: both streams, green fill
- **T21:** ✅ device-tested: both streams, green fill
- **T23:** ✅ device-tested: both streams, green fill
- **T30:** ? no privacy-mask call in the T30 vendor export list
- **T31:** ✅ as vendor, follows mirror/flip; emulator 400/400 identical; cam-A black+red ok [-all-13]
- **T40:** ✅ device-tested 2026-10-10 (vendor driver): cover at the right place in both streams, crop SSIM 0.9998 vs the vendor library (branch `claude/t40-isp-osd`)
- **T41:** ✅ device-tested on chn1 (chn0 value missing because the camera rebooted in that run)

##### 20. Front crop / scaler level / CSC presets

- **Vendor stack:** SetFrontCrop, CSC, BLC
- **T10:** ✅ scaler device-tested (chn1 480x272, 25.0 fps); front crop device-tested 2026-10-09 (open-tx-isp agg-34: a window is kept while no downscaled channel is open, applied at stream on, released with the tuning session); CSC: no control path Front crop vs flip (device-tested 2026-10-10): the window is in sensor space before the flip; the streamer mirrors it vertically only (the horizontal flip is the ISP top mirror before the crop, the vertical flip is in DMA after it).
- **T20:** ✅ scaler device-tested (640x360 and 480x272, 15.0 fps); front crop device-tested 2026-10-09 (same fix as T10: window kept while no downscaled channel is open, applied at stream on, released with the tuning session); CSC: no control path Front crop vs flip (device-tested 2026-10-10): the window is in sensor space before the flip; the streamer mirrors it vertically only (same as T10).
- **T21:** ✅ scaler device-tested (480x272, 24.9 fps); front crop device-tested (2026-10-05, again in the 2026-10-09 crop tests); CSC: no control path Front crop vs flip (device-tested 2026-10-10): the window is in sensor space before the flip; the streamer mirrors it on both axes.
- **T23:** ✅ front crop via vendor path (960x540 crop ok); MASK -EINVAL as vendor (no stock handler); the 2026-10-06 crop hang is fixed in agg-34 and device-tested on 2026-10-09: crop off unlocks the window and restores the full sensor window, a locked window that does not fit is dropped (with a warning) instead of stalling the MSCA, the lock is released at the last close of the ISP device (a streamer restart without crop gets the full frame), module parameter `fcrop_upscale_pct` (default 0) lets the geometry check tolerate a measured upscale (T23 upscales up to 2.0; at 2.2 the MSCA stalled briefly, the crop was dropped, no reboot); flip, day/night, fast changes with many streamer restarts, corner windows, sub-stream and a reboot with a persisted crop all passed; `SetAutoZoom` as the stock control (host/build-tested only) Front crop vs flip (device-tested 2026-10-10): the window is in sensor space before the flip; the streamer mirrors it on both axes; with image hflip/vflip both the sensor (reg 0x3221) and the MSCA (0xd050) flip, `shvflip=1` only flips the LSC mesh. A streamer detects the safe crop path by `/sys/module/tx_isp_t23/parameters/fcrop_upscale_pct` (present only in fixed drivers).
- **T30:** ? no front-crop/scaler-level call in the T30 vendor export list
- **T31:** ⚠️ BLC get, CSC presets 0–4 + user matrix, scaler level (tested on cam-A); front crop: the crash is fixed (enabling a window smaller than a channel's scaler output stalled every MSCA output until reboot; the kernel now refuses such a window with -EINVAL and logs it without a hang, a main stream larger than the window drops the crop with a warning, the crop survives idle off/on, a restart and a flip, disabling restores the full frame; module parameter `fcrop_upscale_pct`, default 0) and, device-tested on 2026-10-10, a valid window zooms both streams (**proven**) Front crop vs flip (device-tested 2026-10-10): the window is in sensor space before the flip; the streamer mirrors it on both axes. Front crop is effective (picture effect proven on both streams in daylight, 2026-10-10); `/sys/module/tx_isp_t31/parameters/fcrop_upscale_pct` exists only in fixed drivers. The vendor does no transform (T23/T31 stock).
- **T40:** ❌ `SetScalerLv`, `Get/SetISPCSCAttr`, `Get/SetAutoZoom` declared but not exported
- **T41:** ❌ crop (I2D) still open: no control path. T41 experimental notes 2026-10-10 (**in branch, not yet in an aggregate**: open-tx-isp `claude/t41-driver-gaps` bf2a6a5c, OpenIMP 0997f0b): MSCA ch1 latch (`cfg_update=2`, opt-in) is correct up to 768x432, ch1 of 960x540 or more started mid-stream can hang the SoC (recommendation: T41 sub-stream at most 768x432); module-parameter heap overwrite only across rmmod/insmod; driver controls FrameDrop, SensorRegister, WDR enable and AF weight added (device-tested); `WdrOutputMode` has no stock handler (stays -1)

##### 21. Rotation 90°/270°

- **Vendor stack:** software rotation (32×32 tiles)
- **T10:** — coerced to 0 ("unsupported on this SoC")
- **T20:** — coerced to 0 ("unsupported on this SoC")
- **T21:** — coerced to 0 ("unsupported on this SoC")
- **T23:** ✅ sub-stream rotation 90/270 works via the native encoder; main stream above 704x576 refused (software rotation); IMP_Encoder_YuvSetCrop implemented (host-tested only, timps does not call it)
- **T30:** ? no rotation call in the T30 vendor export list
- **T31:** ✅ 704×1280 correct, 9 ms/frame @15 fps, before OSD/IVS/encoder
- **T40:** ? no rotation call in the T40 1.3.1 header set; not checked in the FrameSource attributes
- **T41:** ❌ open: hardware I2D path not enabled (timps build coerces 90/180/270 to 0)


#### Video encoder and streams

##### 22. H.264

- **Vendor stack:** Helix (T20/T21/T23), AVPU (T31), NVPU (T10)
- **T10:** ✅ 720p 25 fps 1501 frames error-free; drift bug (margin added twice) fixed; own command list
- **T20:** ✅ soak 1 h 44, 156,517 frames/stream, 0 errors
- **T21:** ✅ main+sub+MJPEG, 25 fps; EMC scratch as vendor (1080p 996 KiB)
- **T23:** ✅+ native without helixd/OEM libimp: 2 h 34, 231,668 frames, 0 decode errors, ~6 % CPU
- **T30:** 🔧 Helix H.264 (High profile, CABAC, 1080p main + 640x360 sub, 25 fps) in `docs/T30_STATUS.md`, decoded without errors by the T30 contributor on a T30X camera; host tests for the descriptor/encoder/level code; not run by us
- **T31:** ✅ soak 2 h 53, 260,648 frames, 1030/1030 snapshots, 0 errors
- **T40:** ✅ device-tested 2026-10-10 on cam-K (OpenIMP vs vendor library): H.264 on ch0 3840x2160 and ch1 640x360, 10 s each, strict decode clean; stream timestamp was 0 and is now copied from the frame record (a5d9b03). Open: CBR delivers only about 20 % of the target bit rate; frame size is 16-line aligned (353280 B vs 345600 B for the vendor); the pixel-format enum reads 10 (vendor 0); `P1_INNER` traces always go to stderr
- **T41:** ✅ cam-F with the fully open stack: main stream High profile 1920x1080 (earlier undecodable 1080p was the rmem exhaustion, fixed by the rmem best-fit), sub stream ok, no oops; repo docs: 2560×1440 H.264 verified on a different T41 device

##### 23. H.265 / HEVC

- **Vendor stack:** T31/T41: AVPU; T10/T20/T21/T23: no HEVC hardware (Helix is H.264/JPEG only; Radix only on T30) – vendor libimp creates an empty channel that never encodes
- **T10:** — — no HEVC hardware; OpenIMP rejects PT_H265 with -1 and a clear log (vendor: silent empty channel) [claude/h265-reject]
- **T20:** — — no HEVC hardware; OpenIMP rejects PT_H265 with -1 and a clear log (vendor: silent empty channel) [claude/h265-reject]
- **T21:** — — no HEVC hardware; OpenIMP rejects PT_H265 with -1 and a clear log (vendor: silent empty channel) [claude/h265-reject]
- **T23:** — — no HEVC hardware; OpenIMP rejects PT_H265 with -1 and a clear log (vendor: silent empty channel) [claude/h265-reject]
- **T30:** ❌ not implemented: the T30 build encodes H.264 and JPEG only (the vendor library exports `H265TransCfg` calls, no HEVC encoder in OpenIMP T30)
- **T31:** ✅ real HEVC on AVPU (VPS/SPS/PPS, CABAC); 2×900 frames, 0 errors
- **T40:** ❌ not supported by OpenIMP T40 yet (cam-K device run 2026-10-10); the HEVC headers and AVPU path from T41 are built in but not enabled on T40
- **T41:** ✅ AVPU HEVC path (as T31): 1080p H.265 decodes clean, a stuck AVPU job times out after 2 s and resets the core [claude/t41-h265, rev5 image]

##### 24. JPEG / MJPEG / snapshot

- **Vendor stack:** hardware JPEG via vendor libimp/helixd
- **2026-10-06 (all SoCs):** a JPEG channel in the group of a video channel feeds itself from the frame source when the video channel is not polled for 300 ms (libimp feeds every bound channel); T20/T21/T23: apitest JPEG polling, marker check and live `SetJpegeQl` PASS without an H.264 reader [beyond vendor, see OPENIMP_BEYOND_VENDOR.md]
- **T10:** ✅ snapshots + MJPEG 25 B/5 s; JPEG buffer 1 MiB (−328 KiB)
- **T20:** ✅ HW JPEG, MJPEG 25 B/5 s with/without video consumer
- **T21:** ✅+ HW JPEG without vendor lib, 37 ms/job, snapshots 0.05–0.18 s; stripes with RST markers
- **T23:** ✅ HW JPEG 29 ms/job; q75 = IJG tables, size matches libjpeg (scene-driven)
- **T30:** 🔧 JPEG on the Helix VPU (`src/t30/helix_jpeg.c`), host test `helix_jpeg_test`; no device result documented
- **T31:** ✅ HW JPEG, own MJPEG channel ok (24 B/5 s, before 0 bytes)
- **T40:** 🔧 snapshot channels encode the captured NV12 frame with the software baseline JPEG encoder since 257ba9d (was a flat grey placeholder): `FrameSource_SnapFrame` and JPEG on ch1 640x360 device-tested on cam-K 2026-10-10 (OpenIMP vs vendor library, decodes). JPEG at 4K not tested (out of memory in the test process, 60 MB RAM)
- **T41:** ✅ 1080p + 640x360 snapshots ok on cam-F with the open driver (claude/t41-gc5603-fix); MJPEG and the grey-JPEG TODO (T40/T41) not re-checked

##### 25. Sub-stream / scaler (ch1 640×360)

- **Vendor stack:** scaler in the ISP
- **T10:** ✅ ch1 shows the full scene (DS1 horizontal ratio bug in the shared firmware fixed)
- **T20:** ✅ ch1 different scaler path, no regression
- **T21:** ✅ WebRTC main↔sub switching confirmed (rmem fix), 32+40 cycles
- **T23:** ✅ idle teardown bug (motion detection without frames) fixed
- **T30:** 🔧 640x360 sub-stream ran together with the main stream in the contributor's sustained gate (60 s, both at 25.0 fps; `docs/T30_STATUS.md`); not run by us
- **T31:** ✅ cam-A 25 fps ch0+ch1 4.5 h
- **T40:** ✅ device-tested 2026-10-10 on cam-K: ch1 640x360 with `GetFrame` (timestamps monotonic, non-zero) and H.264 on the sub-stream, clean strict decode (OpenIMP vs vendor library)
- **T41:** ✅ 640x360 snapshot and MP4 ok on cam-F with the open driver (claude/t41-gc5603-fix)

##### 26. Rate-control mode (CBR/VBR/FixQP/Capped*/SMART)

- **Vendor stack:** all modes in the vendor libimp
- **T10:** ✅+ OEM controller (OPENIMP_T10_RC=1) with the super-frame fix on by default (OPENIMP_T10_RC_SUPERFRM=0 = vendor-exact): at 1200 kbit/s 450→822, re-encodes 800→0, CPU 8.3→5.5 % (claude/t10-rc-superfrm); OEM rate controller is the default (device-tested on cam-E)
- **T20:** ✅ vendor-identical OEM controller is the default (claude/t1x-oem-rc-default-a13): CBR 1300 (P2 I-aware budget), VBR 1044, SMART 1019 at 1200 kbit/s; quality_lvl 0/6 → 1130/800 kbit/s, change_pos 50/100 → 850/1210 kbit/s, also live via /control; decode clean, 0 oops
- **T21:** ✅ vendor-identical T21 eprc is the default (0 oracle deviations); cam-D 1200 kbit/s: CBR 1326, VBR 1096, SMART 1071 [claude/eprc-t21-default]; eprc complete: SMART/CBR/VBR at 1200 kbit/s → 1090/1305/1042; runtime HSkip N=4 gives an IDR every 4 GOPs; decode clean, 0 oops [claude/eprc-complete]; scene-cut IDR not triggered by a day/night switch (vendor condition: scene class 5); MB-level RC ported (claude/eprc-mbrc 9e2bc3a, a8b483a: 0x400c0/0x400c4 per picture type like the vendor; device test running)
- **T23:** ✅ eprc controller: 60 s at 1200 kbit/s, decode clean: SMART 1141, CBR 1253, VBR 1255 (claude/eprc-t21-t23, not yet in an aggregate); MB-level RC ported (claude/eprc-mbrc 9e2bc3a, a8b483a: 0x400c0/0x400c4 per picture type like the vendor; device test running)
- **T30:** 🔧 CBR/VBR/FixQP with the T31 GOP-level controller (`docs/T30_STATUS.md`), host test `rc_readback_test`; no device result by us
- **T31:** ✅ all modes via the vendor Allegro core (default): CBR 1210 kbit/s at 1200 target and 2973 at 3000 (legacy controller 1511 / 3786, +26 % with large peaks); VBR 1163, CappedVBR 1177, CappedQuality 1174 at 1200; decode clean, 0 oops [claude/t31-allegro-cbr]. No filler NAL is written (filler=0 in the logs also at 3000), so in practice there was no difference.
- **T40:** ⚠️ CBR delivers only about 20 % of the target bit rate on cam-K (2026-10-10); VBR 2560x1440 run reported by the T40 contributor; rate control shared with T31/T41, host `rate_control_test`
- **T41:** ✅ bitrate 400/1200/3000 → 518/1195/2777 kbit/s (30 s each) [claude/t41-cbr-overshoot]

##### 27. RC parameters (QP steps, staticTime, changePos, qualityLvl, I bias)

- **Vendor stack:** via IMP attr
- **T10:** ✅ device-tested: min/max QP and I bias read back in the encoder RC; quality_lvl/change_pos only in the video readback
- **T20:** ✅ readback returns the vendor-clamped values (staticTime 1, changePos 50, qualityLvl 0, QP steps 2/2 when the app passes 0) (claude/rc-modes) [-all-13]
- **T21:** ✅ readback as T20 [-all-13]
- **T23:** ✅ uses the vendor CreateChn clamps (1/50/2/2); parameters reach the native encoder (changePos min 50); app value 0 = vendor default 3/15/2/80 [-all-13]
- **T30:** ? not documented
- **T31:** ✅ T31 defaults like the vendor (max QP 48, max bitrate 4/3, ...) [-all-13] (claude/rc-modes-2)
- **T40:** ? not documented
- **T41:** ⚠️ device-tested: min/max QP live and effective; quality_lvl/change_pos/i_bias restart-only (readback in video block)

##### 28. OSD: text, bitmap, rectangle, line, cover

- **Vendor stack:** IPU OSD / vendor libimp
- **T10:** ✅ device-tested: 4 items (text, uptime, logo) on both streams
- **T20:** ✅ text/bitmap/lines/rectangles on both streams, clipping, 0 oops (IPU OSD hook)
- **T21:** ✅ IPU OSD hook as T20; rect/line/bitmap
- **T23:** ✅ device-tested: all 4 items, text edit
- **T30:** ? OSD calls exported (T23 services, shared), no test result for T30
- **T31:** ✅ IPU OSD; lines/rectangles; rotation: no OSD clamp in timps
- **T40:** ✅+ device-tested 2026-10-10 on cam-K (OpenIMP vs vendor library): PIC and COVER regions; OpenIMP draws COVER on NV12 while the vendor library fails with "COVER cannot support this format" (beyond vendor on this board). IPU OSD from the T41 path; mosaic regions are not drawn, `IMPOSDRgnAttr` shares only the 1772-byte prefix
- **T41:** ✅ works on cam-F with the open stack (claude/t41-libimp): PIC/COVER via IPU, text/line/rect on CPU; clock, name and logo visible; kernel oops from the rmem cache flush fixed (T41 kernel expects a physical address)

##### 29. IVS / motion detection

- **Vendor stack:** vendor IVS (T20/T21/T30 initially "always no motion" in the open stack)
- **T10:** ✅ device-tested: no event in 12 s idle, full-grid events on brightness steps (2 of 6)
- **T20:** ✅+ 6/6 events, 0 false alarms; CPU 4.1→2.7 % with motion
- **T21:** ✅ real frame-diff IVS ported
- **T23:** ✅ motion active again after sub-stream idle (WebUI grid)
- **T30:** ? IVS calls exported (T31 IVS code), no test result for T30
- **T31:** ✅+ sub-stream default: ~85 % less IVS CPU (compared with vendor libimp)
- **T40:** ✅ move detection device-tested for 20 s on cam-K 2026-10-10 (OpenIMP vs vendor library); T31/T41 IVS framework, base move and `Get/SetParam` host tests only
- **T41:** ✅ device-tested earlier on cam-F with a feeder thread (no more 10-s stalls); since then the `GetFrame` re-check after `DQBUF` (914b96b, a `DisableChn` race delivered a null frame address) and a deterministic framework test (3b849dc, 500/500 under load) are host-tested only, not re-run on the camera; short ~1.2-s gaps still open; the IVS function rows stay `aud` for T41 (no function-level device test)

##### 30. Frame source / VBM pool

- **Vendor stack:** vendor pools
- **2026-10-06:** `GetFrame`/`SnapFrame` wait up to 2 s like the vendor libimp on T20/T21/T23/T31 (apitest 16/16 frames); T20: `REQBUFS` = pool size and `SET_BANKS` as the vendor, the capture thread no longer sleeps in `DQBUF` without a queued buffer (apps that call `SetFrameDepth` got no frames before); `Encoder_GetFd` returns a pollable pipe; two IVS groups; T23: the pool rmem is parked at `DisableChn` as on T21 (40 streamer restarts, 0 oopses on a T23 camera)
- **T10:** ✅ device-tested: chn0, chn1 and snapshot concurrent, 148/147 frames decoded
- **T20:** ✅ snapshot debounce no longer polls the JPEG encoder: with 1 snapshot/s on both channels chn0 14.4 / chn1 15.0 fps (was 11.2/14.3); sub-stream height 270 is rounded to 272 with a warning (was: scaler hang) [openimp claude/openimp-t20-jpeg-align, timps claude/timps-jpeg-idle-nopoll]
- **T21:** ✅+ pool parked/reused (release at idle broke later allocations)
- **T23:** ✅ frames also recycled for callback pools
- **T30:** 🔧 FrameSource ran as the source of the contributor's H.264 gate; host test `fs_lifecycle_test`; not run by us
- **T31:** ✅ device-tested: concurrent streams ok
- **T40:** ✅ ch0 3840x2160 and ch1 640x360 `GetFrame`/`ReleaseFrame` device-tested on cam-K 2026-10-10 (timestamps monotonic, non-zero; OpenIMP vs vendor library); frame size 16-line aligned (353280 B vs 345600 B)
- **T41:** ✅ device-tested: concurrent streams ok


#### Audio (documentation only, no tests)

##### 31. Audio input (AI)

- **Vendor stack:** IMP_AI
- **T10:** ✅ device-tested: AAC 16 kHz mono 32 kbit/s in RTSP + fMP4, volume/gain/mute work, gain clamp 31 (ambient levels, gain 31 reading noisy); 2026-10-10 AEC test: `IMP_AI` enable/`GetFrame`, `SetVol`/`SetGain`, mono 16 kHz recorded the played signal back (device-tested)
- **T20:** ✅ device-tested: AAC 16 kHz mono 32 kbit/s in RTSP + fMP4, volume/gain/mute work, gain clamp 31 (little gain effect, -75 dB floor); 2026-10-10 AEC test: `IMP_AI` enable/`GetFrame`, `SetVol`/`SetGain`, mono 16 kHz recorded the played signal back (device-tested)
- **T21:** ✅ device-tested: AAC 16 kHz mono 32 kbit/s in RTSP + fMP4, volume/gain/mute work, gain clamp 31; 2026-10-10 AEC test: `IMP_AI` enable/`GetFrame`, `SetVol`/`SetGain`, mono 16 kHz recorded the played signal back (device-tested); a microphone gain above 0 drives the noise floor up (hardware, open and vendor stack alike)
- **T23:** ✅ microphone in the RTSP stream (AAC 16 kHz), real room-noise signal (mean -64 dB, peak -46 dB; T31 reference -57/-43 dB); no speech test; 2026-10-10 AEC test: `IMP_AI` enable/`GetFrame`, `SetVol`/`SetGain`, mono 16 kHz recorded the played signal back (device-tested)
- **T30:** ? AI exported (T21 /dev/dsp path, 68 of 68 vendor audio names), no test result
- **T31:** ✅ device-tested: AAC 16 kHz mono 32 kbit/s in RTSP + fMP4, volume/gain/mute work, gain clamp 31; also PCMU 8 kHz via RTSP (not in fMP4); 2026-10-10 AEC test: `IMP_AI` enable/`GetFrame`, `SetVol`/`SetGain`, mono 16 kHz recorded the played signal back (device-tested)
- **T40:** ? AI exported (`openimp_p3_audio.c`), no test result
- **T41:** — no audio hardware on the test board (no microphone); an earlier "room noise" level was only the noise floor of an unconnected input

##### 32. Audio output (AO, speaker)

- **Vendor stack:** IMP_AO
- **T10:** ✅ `IMP_AO` enable/`SendFrame`, speaker playback of a known signal at several volumes and `SetVol`, recorded back by the microphone (2026-10-10 AEC test, device-tested)
- **T20:** ✅ `IMP_AO` enable/`SendFrame`, speaker playback of a known signal at several volumes and `SetVol`, recorded back by the microphone (2026-10-10 AEC test, device-tested)
- **T21:** ✅ `IMP_AO` enable/`SendFrame`, speaker playback of a known signal at several volumes and `SetVol`, recorded back by the microphone (2026-10-10 AEC test, device-tested)
- **T23:** ✅ `IMP_AO` enable/`SendFrame`, speaker playback of a known signal at several volumes and `SetVol`, recorded back by the microphone (2026-10-10 AEC test, device-tested)
- **T30:** ? AO exported, no test result
- **T31:** ✅ volume/mute work, whole OSS fragments (tested on cam-A); `IMP_AO` enable/`SendFrame`, speaker playback of a known signal at several volumes and `SetVol`, recorded back by the microphone (2026-10-10 AEC test, device-tested)
- **T40:** ? AO exported, no test result
- **T41:** — no audio hardware on the T41 test board (no microphone, no speaker); the software mute (`SetVolMute`, `Soft_Mute`/`UNMute`) is an API-level test only (rc 0, invalid argument -1)

##### 33. Echo cancellation (AEC)

- **Vendor stack:** IMP_AI_EnableAec
- **T10:** ✅ device-tested 2026-10-10: OpenIMP AECM (WebRTC), mono 16 kHz, `EnableAec` returns 0; ERLE 25 dB on a speech segment against the room noise floor
- **T20:** ✅ device-tested 2026-10-10: OpenIMP AECM (WebRTC), mono 16 kHz, `EnableAec` returns 0; ERLE 24 dB on a speech segment against the room noise floor
- **T21:** ✅ device-tested 2026-10-10: OpenIMP AECM (WebRTC), mono 16 kHz, `EnableAec` returns 0; ERLE 20 dB (open; vendor stack on the same model 16 dB with the same signal) on a speech segment against the room noise floor
- **T23:** ✅ device-tested 2026-10-10: OpenIMP AECM (WebRTC), mono 16 kHz, `EnableAec` returns 0; ERLE 19 dB on a speech segment against the room noise floor; was 🔧
- **T30:** ? AEC code is built in (`openimp_aec.c`), no test result
- **T31:** ✅+ real AECM: echo −18 dB, ERLE 44 dB (loopback); before: fake success
- **T40:** ? AEC calls: see the audio function table, no test result
- **T41:** — no audio hardware on the T41 test board (no microphone, no speaker), so AEC cannot be measured there


#### Memory, size, load
- **Note (2026-10-10):** T41 `?`: the microphone shows no response to playback, so ERLE is not measurable. AECM does not cancel stationary far-end signals (pure tones), as known. On T21 a microphone gain above 0 drives the noise floor up to -11..-32 dBFS (hardware, open and vendor stack alike).

##### 34. libimp size (code + data)

- **Vendor stack:** T21 ~1.0 / T23 ~1.26 / T31 ~1.05 MB
- **T10:** ✅+ 626,032 B (~0.6 MB)
- **T20:** ✅+ 594 KB (was 694 KB; claude/openimp-size, gc-sections)
- **T21:** ✅+ ~0.5 MB
- **T23:** ✅+ 726 KB (was 774 KB; native, no helixd; claude/openimp-size)
- **T30:** ? 594,232 B (~0.57 MB) stripped build; the vendor size was not measured
- **T31:** ✅+ ~0.57 MB
- **T40:** ? 375,752 B (~0.36 MB) stripped build of the branch; the vendor size was not measured
- **T41:** ✅+ 465,728 B (~0.47 MB)

##### 35. Kernel module size

- **Vendor stack:** T21 616 / T23 857 / T31 829 KB
- **T10:** ✅ 731 KB stripped (was 770; claude/open-tx-isp-size2)
- **T20:** ✅ 736 KB stripped (was 775; claude/open-tx-isp-size2)
- **T21:** ✅+ 452 KB (was 760 KB; vendor 616 KB), RAM unchanged (claude/t21-size-awb-opt, 452 KB since all-17)
- **T23:** ✅+ 622 KB stripped (was 1,047; vendor 857; claude/open-tx-isp-size2, device-tested on cam-B)
- **T30:** ? not measured
- **T31:** ✅+ 711 KB stripped (was 859; vendor 829; claude/open-tx-isp-size2, device-tested on cam-A)
- **T40:** ? not measured
- **T41:** ✅ tx_isp_t41 731,488 B (vendor size not measured)

##### 36. Video memory (rmem) / MemFree

- **Vendor stack:** T21 ~23 MB for main+sub+JPEG
- **T10:** ✅ JPEG buffer −328 KiB; 40 KiB rootfs reserve in the image
- **T20:** ✅ MemFree 47 MB of 91 MB
- **T21:** ✅+ free with main+sub+MJPEG 2.76 MB (vendor ≈1.2)
- **T23:** ✅+ JPEG shares bitstream −1.44 MB; main window 2 MiB
- **T30:** ? not measured
- **T31:** ✅ drift per reload 460→45 KB
- **T40:** ? not measured
- **T41:** ✅ rmem 26 MB in the current image (was 30): stream buffers sized like the vendor (1080p 0.95 MB), capture buffers from the bottom and the rest from the top so idle/restart cycles no longer fragment rmem; 5 idle/restart cycles clean. OOM with three parallel streams and `AddSensor` EBUSY after an OOM kill are still open

##### 37. Reference-frame sharing (BUF_SHARE_CFG)

- **Vendor stack:** vendor T23: used by default (<=1080p); vendor T21: off by default
- **T10:** — not in this SoC's encoder (vendor has none either)
- **T20:** — not in this SoC's encoder (vendor has none either)
- **T21:** ✅+ works (claude/t23-ref-ring) [-all-13]: P-frames 150-300 B in a static scene on cam-D, no artefacts; saves ~1.5 MB video memory at 1080p; on by default (<=1920x1088), OPENIMP_REF_SHARE=0 disables
- **T23:** ✅+ works (claude/t23-ref-ring) [-all-13]: no artefacts on cam-B, P-frame sizes equal or smaller than without the ring; saves ~1.5 MB at 1080p; on by default (<=1920x1088, like the vendor), OPENIMP_REF_SHARE=0 disables
- **T30:** ? not documented
- **T31:** — not in this SoC's encoder (vendor has none either)
- **T40:** ? not documented
- **T41:** ? not wired in timps (SetbufshareChn exists in libimp); not tested

##### 38. CPU load (documented figures)

- **Vendor stack:** vendor comparison values mostly missing
- **T10:** ✅ timps 5–9 % with 2 streams at 25 fps (17 % momentary with 1 stream)
- **T20:** ✅ timps 2.7 % with motion; OEM AWB +4 %
- **T21:** ✅+ lifted AWB at 0.95x vendor instructions (was 1.41x), output bit-identical; cam-D isp_fw_process -10 %
- **T23:** ✅ ~6–7 % for 2 streams 25 fps
- **T30:** ? not measured
- **T31:** ✅ rotation 9 ms/frame @15 fps
- **T40:** ? not measured
- **T41:** ✅+ timps ~10–12 % with our libimp vs ~27 % with the vendor libimp (momentary values)


#### Stability, helper libraries, telemetry

##### 39. Kernel soc_vpu / Helix hardening

- **Vendor stack:** busy-wait up to 200 ms, unbounded waits
- **T10:** ✅+ error IRQ ends the wait immediately (patch 0099)
- **T20:** ✅+ patch 0099
- **T21:** ✅+ patches 0095–0099 (bounded waits, pointer checks, register ioctl restricted to the VPU window)
- **T23:** ✅+ patches 0098–0105 (0102 ignores the residual Helix interrupt, status 0x100 after a finished job), merged upstream in thingino `aperto` (#1748, #1752); 5 h soak on the `aperto` images: 0 VPU errors on all cameras
- **T30:** ? legacy `/dev/soc_vpu` Helix interface is used (`docs/T30_STATUS.md`); no hardening work or review documented
- **T31:** — SOC_VPU not built
- **T40:** — AVPU, not Helix
- **T41:** ✅ device-tested: 5 min, 3 RTSP clients plus snapshots, 0 VPU/AVPU errors; main stream High@5.1 after the rmem fix (rmem was exhausted and the main channel fell back to a broken software encoder); 5 idle/restart cycles clean

##### 40. AVPU kernel driver (T31/T40/T41) review

- **Vendor stack:** –
- **T10:** — other VPU
- **T20:** — other VPU
- **T21:** — other VPU
- **T23:** — other VPU
- **T30:** — Helix, not AVPU
- **T31:** ✅+ DeepSeek review verified; fixes on claude/avpu-review-fixes (minor-number leak, use-after-free on sysfs unbind, uninitialised dma-buf list mutex, flush clamp); kernel patch 0100 validates the rmem flush ioctl (invalid direction no longer crashes the kernel); tested on cam-A: reload, kill -9, 5× rmmod/insmod, 0 oops [-all-13]
- **T40:** 🔧 AVPU driver is in the review scope of this row; no T40 device run in the OpenIMP campaign
- **T41:** ✅ same module family runs on cam-F with the open driver (5 min, 3 RTSP clients plus snapshots, 0 AVPU errors). ioctl stack-overflow hardening (unknown or legacy commands now return -ENOTTY) is in open-tx-isp, not yet device-tested; test plan in `driver/t41/README`

##### 41. Long-term stability / hangs

- **Vendor stack:** –
- **T10:** ✅ rmmod/insmod 5× with streaming, 0 oops; the earlier 'csi clock -22' oops came from a module built against the T20 kernel tree, the T10 build now refuses that [claude/t10-reload-safe]
- **T20:** ✅ 1 h 44 soak, 0 errors
- **T21:** ✅ uptime 1:54 at the test, 0 oops
- **T23:** ✅ the frequent Helix frame drops had a fixed cause (residual interrupt 0x100 treated as an error by the bounded-wait kernel patch): 60 min 0 errors after the fix, 5 h soak on the `aperto` images 0 encoder errors. Sporadic single Helix encode error (errno 5): 0 errors in 6 days on cam-B since the 0102 fix (syslog: 342, 32, 37 and 419 errors on 10-01..10-04, then 0 from 10-05 to 10-10); provisionally closed, still observed. Cold-start snapshot 503 on a second channel (stale MSCA FIFOs): fix `msca_fifo_rearm` gave 260 cold-start cycles without a failure (before ~1-7 %), soak pending before it enters `next`; 2026-10-06: release defaults of the driver are the stock-like set that ran 7 h overnight (`chan_stop_keep_input=1`, `msca_keep_enabled=2`, `msca_fifo_rearm=0`, `msca_flip_skip_noop=1`, `msca_restart_skip=1`, `msca_session_release=1`, `crumbs=0`); the cold-start snapshot 503 is prevented by the stock STREAMOFF drain wait and a QBUF cache invalidate (20 cold starts and 20 restarts without a failure); MSCA scratch buffer parks a stopped channel (80 parks, 0 settle timeouts). **Known issue: the pipeline stops after a FrameSource crop change (under investigation).**
- **T30:** ? the contributor's gate was 60 s; no long run documented
- **T31:** ✅ 4.5 h soak ok; 2026-10-06: H.264 stall after JPEG teardown fixed (5c2ccef, agg-29).
- **T40:** ? not documented
- **T41:** ⚠️ 5-min stress without reboot earlier; still open: OOM with three parallel streams, `AddSensor` EBUSY after an OOM kill; module reload 10/10 clean

##### 42. Helper libraries libalog / libsysutils

- **Vendor stack:** shipped with vendor images
- **T10:** ✅+ removed
- **T20:** ✅+ removed (image tested)
- **T21:** ✅+ removed
- **T23:** ✅+ removed; also no helixd/vendor libimp
- **T30:** ? `libsysutils` is not part of the libimp build; not checked
- **T31:** ✅+ removed
- **T40:** ? `libsysutils` is not part of the libimp build; not checked
- **T41:** ✅+ removed (cam-F runs the open stack without both libs)

##### 43. Tuning getters / readback

- **Vendor stack:** IMP_ISP_Tuning_Get*
- **T10:** ✅ device-tested: isp-m0 Brightness/Contrast/Saturation/Sharpness/Antiflicker readback equals the set values
- **T20:** ✅ SDK control IDs, pointer semantics as vendor, isp-m0
- **T21:** ✅+ getters return what was set [-all-13] (vendor: scene/colorfx/Sinter DNS no-op)
- **T23:** ✅ expr/EV/TotalGain live, SensorAttr 20-byte layout, vendor isp-m0
- **T30:** ? getters exported (shared code), no test result
- **T31:** ✅ SensorAttr, WaitFrame per frame, isp-w02 counter
- **T40:** 🔧 host: `GetAeWeight`, `GetAfWeight`, `GetAeExprInfo`, `GetSensorFPS`, `GetFrameDrop`, `GetSensorRegister` layout tests; `GetSensorAttr` is device-tested on cam-K 2026-10-10
- **T41:** ✅ isp-m0 in vendor layout: run mode, BCSH, flip mode, anti-flicker, AE

##### 44. Encoder telemetry / diagnostics

- **Vendor stack:** IMP_Encoder_Query/ChnStat
- **T10:** ✅+ RC log line, clamp warning
- **T20:** ✅+ ditto
- **T21:** ✅+ ditto
- **T23:** ✅+ ditto
- **T30:** ? not documented
- **T31:** ✅+ ditto
- **T40:** ? `Encoder_GetChnEvalInfo`, `GetChnAveBitrate` exported, no test result
- **T41:** ⚠️ isp-m0 shows AE/AWB/anti-flicker/flip; encoder rc readback ok; query counters stayed 0 under load; other /proc/jz/isp nodes unreadable


##### 45. Encoder ROI (region QP)

- **Vendor stack:** `IMP_Encoder_SetChnROI` (T10/T20/T21/T23), `IMP_Encoder_Set/GetChnRoiAttr` (T40/T41); no ROI API in the T31 library
- **T10:** ✅ regions on the Helix/NVPU, device-tested; absolute QPs are limited to the valid H.264 range; absolute ROI re-tested 2026-10-10: valid, no inversion
- **T20:** ✅ regions and H.264 chroma QP offset, device-tested (QP 51 region blocky, QP 15 clean); valid H.264 from agg-34 (an absolute QP more than 25 from the macroblock QP broke hardware decoders before); absolute ROI re-tested 2026-10-10: valid, no inversion
- **T21:** ✅+ effective by default (beyond vendor: the vendor never programs it), device-tested; relative QP acts inside -12/+13 of the slice QP; `OPENIMP_T21_ROI=0` switches it off
- **T23:** ✅ on the native Helix encoder (default backend), the same registers the vendor 1.3.0 encoder programs; device-tested 2026-10-10: QP map shows the window (relative QP acts inside -12/+13 of the slice QP, like T21), bit rate +10 % at delta -15 and -4 % at +20/absolute 51, 9 streams valid in VA-API and strict ffmpeg; OPENIMP_T23_ROI=0 switches it off
- **T30:** ? ROI calls (4 vendor names) exported, no test result for T30
- **T31:** ✅+ beyond vendor: `IMP_Encoder_Set/GetChnRoiAttr` through the AVPU macroblock QP table (H.264 only), device-tested: delta -10 gives 2.7x the bit rate, a relative -20 is clamped to -19, absolute 51 blurs the region, 0 decoder errors in 20 VA-API checks; 2026-10-10: 20 phases valid in VA-API and strict ffmpeg (`-err_detect`); known deviation: under FixQP 42 an absolute 5 or relative -25 window drives the P-frames to QP about 14 almost frame-wide (bit rate about x4, stream valid); `OPENIMP_T31_ROI=0` refuses the call
- **T40:** 🔧 `Encoder_Get/SetChnRoiAttr` (10 windows, AVC only) through the same AVPU macroblock QP table as the T31 (one byte per 16x16 block in EP2, cmd[9] bits 25/24; the vendor 1.3.1 library builds the same one-byte table and sets the same bits), cross-built, not run on a camera: off unless `OPENIMP_T40_ROI=1`; `SetChnMapRoi` is still not exported on T40
- **T41:** 🔧 no effect: the table is written (off unless `OPENIMP_T41_ROI=1`; enable bits derived from the vendor library, not yet device-tested), but on the T41 run the AVPU ignored the table (bit rate and picture unchanged); `SetChnRoiAttr`/`SetChnMapRoi` count as cache
- **Rules (all SoCs with ROI):** window deltas -25..+25; spread of all deltas at most 25; picture QP plus delta inside 0..51 and the rate control's min/max QP; requests beyond that are clamped with a one-time warning (`SetChnRoiAttr` returns -1 for a delta outside -26..25 or an absolute QP outside 0..51)

## Missing / incomplete functions (vendor IMP/SU API, per SoC)

As of: 2026-10-10 (re-audit against agg-34; the cells below were set on 2026-10-06 from the release-candidate test day and changed since only where noted). The cells were updated from the release-candidate test day: every row/SoC whose apitest call passed on a camera of that SoC (T20, T21, T23, T31) is now **dev**; the cells changed by hand (vendor no-ops, functions that became real, functions that were fixed) are listed in the notes of the row. T10 and T41 cells were not re-measured (T10 mirrors T20 and was not re-tested; T41 is not part of the release).

State of the work: OpenIMP `claude/agg-34` (2a1b1cd), open-tx-isp `claude/agg-34` (b15ef235). Re-audit 2026-10-10: every row with a cache/stub/error/missing/? cell on T10/T20/T21/T23/T31 was checked against the exported symbols of the agg-34 libimp.so builds (T10 uses the T20 build) and the agg-34 source; the only function that became real since agg-27 is `IMP_Log_Set_Option` (now host on T10/T20/T21/T23/T31), the other gap rows are unchanged (still cache-only/error by design or not exported); `ISP_Tuning_Get/SetFrontCrop` T31 is now dev (night of 2026-10-10). The T41 column and the T41 count row were not touched (other agents work on T41). The counts are recomputed by a local helper script (parses the full lists; not in the repo). Source of the function list and of the base classes: the audit `NOT_CONNECTED_2026-10-05.md` (OpenIMP/open-tx-isp `agg-24`, built libimp.so/libsysutils.so, vendor header sets: T10/T20 331 functions, T21 330, T23 704 incl. `_Sec`/`MultiCamera_` variants, T31 405, T41 445). Rows are ALL vendor IMP_*/SU_* functions of the audit (Get/Set pairs share a row). Per area the first table shows only rows with at least one gap (cache-only, stub, error, missing, ?); the collapsible full list below it shows every row.

Gap work 2026-10-10 (branch claude/gaps-t23-t31, T23 and T31 only): the vendor libimp/libsysutils of every build (8 T23, 28 T31) were checked for the gap functions. Not exported by any vendor build, so now n.a. instead of missing: T23 `OSD_GetRegionLuma`, `SU_Battery_*` (T23, T31), T31 `ISP_Tuning_SetDPStrength`, `Decoder_*`, `EmuFrameSource_*`, `ADEC_ReleaseDecoder`, `AENC_ReleaseEncoder` (declared in some headers only). Vendor no-op matched: T23 `Encoder_Get/SetH265TransCfg` (drops the value, Get zero-fills; device-tested), T31 `Encoder_SetFrameRelease`. Implemented: T31 `ISP_SetFixedContraster` (attr pointer as control 0x8000102 like the vendor; host test, device-tested; T23 now dev too). Remaining gaps are class c (T23 driver has no handler for the controls of `ISP_Tuning_SwitchBin`, `AwbSync`, `SetWB_ALGO`, `Get/SetOSDAttr`, `Get/SetOSDBlock`, `Get/SetDrawBlock`) or class d (fisheye flag, frame-loss threshold, multi-section mode, `FB_*`, `SU_Base_Shutdown`); see the notes of the rows.

Gap work 2026-10-10 (branch claude/gaps-t23-t31-b, T23 and T31, host work only, device test pending; driver side in open-tx-isp claude/gaps-t23-t31-drv, which is not in agg-36 (held back until its T23 SwitchBin is reworked), so the T23 class c cells keep their previous state; vendor libimp T23 1.3.0 and T31 1.1.6 disassembly, stock tx-isp-t23.ko in the emulator): class a (vendor behaviour matched): T23 `ISP_Tuning_AwbSync` (not exported by libimp 1.3.0; the stock kernel rejects the control too; now n.a.). Class b (OpenIMP): none, the T23 entry points already pass all controls to the driver. Class c (open-tx-isp, emulator-identical where the stock code could be run): T23 `ISP_Tuning_Get/SetDrawBlock`, `Get/SetOSDAttr`, `Get/SetOSDBlock` (stored on the stock mscaler object, no image effect yet, as the mask blocks), `ISP_Tuning_SwitchBin` (tisp_switch_bin rewritten around the IQ loader), `ISP_Tuning_SetWB_ALGO` (accepted as stock, stub: the open AWB has no threshold window to switch); T31 `ISP_SetFixedContraster` (the driver handler now calls tisp_set_bcsh_fixed_contrast). Class d (not worth it): T23 and T31 `Encoder_Get/SetFisheyeEnableStatus`, T23 `Encoder_Get/Setframelossthd` and `SetMultiSectionMode` (T31 1.1.6 exports neither), T31 `FB_*` (only the 1.1.5-zrt display builds export them), `SU_Base_Shutdown` (libsysutils, vendor-identical). Counts: T23 gaps 15 to 7, T31 8 to 8 (`SetFixedContraster` was not counted as a gap; its cell is host now).

Gap work 2026-10-10 (branch claude/gaps-t1x, T10/T20/T21, vendor export check only, host-only, no device session): the 4 vendor libimp builds per SoC (T10 3.9.0/3.12.0, T20 3.9.0/3.12.0, T21 1.0.33) and their libsysutils were checked with nm. Exported by none, so now n.a.: `ISP_Tuning_SaveAllParam` (T10/T20/T21), `ADEC_ReleaseDecoder`/`AENC_ReleaseEncoder` (T10/T20), `SU_Battery_*` (T10/T20/T21). Gaps in functions: T10 28 to 21, T20 27 to 20, T21 14 to 9. Everything else stays open; the vendor libimp does export ChangeRef, FrmUsedMode, GOPSize, HSkipBlackEnhance, OSD_AttachToGroup, Decoder_* (T21), and the T10/T20 H264 variants, WDRAttr, ISPHVflip, MeshShadingScale, AntiFogAttr, DPStrength (T10/T20), so these are classes b/c and not yet worked (low priority: no streamer uses them).

Gap work 2026-10-10 (branch claude/gaps-t1x-impl, T10/T20/T21, vendor disassembly of libimp T21 1.0.33 and T10/T20 3.12.0, device-tested): the 3.12.0 libimp of T10 and T20 is one and the same binary. Per function: **(a/b) done**: `Encoder_Get/SetChnFrmUsedMode` (T10/T20/T21; stored like the vendor, also before CreateChn), `Encoder_GetGOPSize` (T10/T20/T21; idle channel answers 0 on T21, -1 on T20), `ISP_Tuning_SetDPStrength` (T10/T20, T21 now device-tested), `ISP_Tuning_Get/SetISPHVflip` and `Get/SetWDRAttr` (T10/T20; the WDR switch is the V4L2 control 0x98e912 with the vendor's mode cache, the HV flip uses the ISP flip pair because the open T20 driver does not serve the vendor's module-control word), `OSD_AttachToGroup` (T10/T20/T21; system_attach() splice, host test). **Stored, effect only in the closed vendor encoder (cache, classes c/d)**: `Encoder_Get/SetChangeRef`, `SetChnHSkipBlackEnhance`, `Get/SetFisheyeEnableStatus` (T10/T20/T21, vendor return codes and idle/created rules matched), `Get/SetChnDemask` and `Get/SetChnHSkip` (T10/T20). **Not worth it / not possible**: `Decoder_*` (class d: no decoder in the open stack, no user), `ChnH264Demask/Denoise/FrmUsedMode` and `ChnRcAttr` (class d: exported only by the 3.9.0 libimp, an older ABI generation than OpenIMP implements), `SetAntiFogAttr` T10/T20 (class c: the antifog Iridix preset path of the open driver hung cameras), `SetMeshShadingScale` (class c: the vendor writes an ISP register through a userspace mapping, no driver control), `SU_Base_Shutdown` (class d, unchanged). Device results with the apitest (agg-34 plus these changes, libimp from /tmp, streamer restarted, config md5 unchanged, 0 oopses): T20 148 PASS / 0 FAIL / 2 N/A / 2 SKIP, T21 155 / 0 / 2 / 2, T10 147 / 1 / 2 / 2 (the one FAIL is the pre-existing `SetSuperFrameCfg` read-back on T10). Gaps by row: T10 21 to 15, T20 20 to 14, T21 8 to 5.

Gap work 2026-10-10 (branch claude/gaps-t30, T30, host and build only, no T30 camera; vendor libimp 1.0.5 disassembled and compared with T21 1.0.33 and T31 1.1.6): the T30 build now exports 317 of the 334 vendor names (was 271; export check of `build-t30.sh`, audio 68 of 68). No streamer (timps, prudynt, raptor RVD) imports any of the 63 names that were missing (prudynt has one commented-out `IMP_Encoder_SetFisheyeEnableStatus` call), so no streamer-used T30 function was open. Per function: **(a/b) done, host-tested**: `Encoder_Get/SetChnFrmUsedMode`, `Encoder_GetGOPSize`, `ISP_Tuning_SetDPStrength`, `SetAntiFogAttr`, `Get/SetAeStrategy`, `Awb_Get/SetCwfShift`, `Get/SetWDRAttr` and `Get/SetISPHVflip` (the T30 libimp has the same code as T20/T21 for these; the ISP calls use the same tuning controls with a fake-ioctl host test, whether the open T30 driver serves each control is not checked), `OSD_AttachToGroup` (same wrapper as T21); the 21 `DMIC_*` functions by compiling the T31 implementation into the T30 build (the T30 libimp disassembles like T31 apart from buffer sizing; the T30 oss2 audio driver has the same DMIC ioctls; one microphone per device on T30; host test with a fake /dev/dsp; the AEC calls are compared, not run). **Stored only (cache)**: `Encoder_Get/SetChangeRef`, `SetChnHSkipBlackEnhance`, `Get/SetFisheyeEnableStatus`, `Get/SetChnHSkip` (the IDR period is not handed to the T30 encoder) and `ISP_Tuning_SetTemperDnsCtl`; three more vendor exports that have no matrix row are exported the same way (`Encoder_SetPoolSize`, `Encoder_SetChnRcTrigLevel`, `ISP_Tuning_SetAFThreshold`). **Wishlist (c, needs a T30 device or driver work)**: `Decoder_*` (8; Helix/Radix decoding), `Encoder_Get/SetChnCrop` (the vendor crops in 16-pixel units inside its encoder; the SPS-level crop of the Helix path needs a device check), `ISP_Tuning_SetISPLDC` (an LDC ioctl sequence on the ISP node). **Not worth it (d)**: `Encoder_Enable/DisableAllNCUDenoise` (register access of T30-class chips), `Encoder_PadFrame` and `SetPadFrameAttr` (external frame injection), `Encoder_Get/SetRencFirstFrameMult` (parameter of the closed re-encode controller, no default known); these 8 names are not matrix rows. Found on the way: the T21 build returns the stored value for `GetChangeRef` of a created channel, while the same vendor code (T21 1.0.33, T30 1.0.5) fails there with "unsupport to change ref channel", and the T21 `Get/SetChnHSkip` refuse an idle channel that the vendor accepts (reported, not changed for T21). T30 gaps: 54 to 19 (11 missing, 8 cache-only), 13 functions host and 22 aud, 0 streamer-used gaps.

Gap work 2026-10-10 (branch claude/gaps-t1x-c, T10/T20/T21, vendor disassembly, host tests only, the cameras were in a soak): three vendor deviations fixed. **T21 `GetChangeRef`** on a created channel returned the stored value; the vendor (T21 1.0.33 0x48d90, T30 1.0.5 identical; T10/T20 3.12.0 0x4b220 read the value) logs "unsupport to change ref channel" and returns -1, now matched. **T21 `Get/SetChnHSkip`** rejected an idle channel; the vendor (0x478f0/0x47ba0) stores without the maxHSkipType check (Set) and answers all zero (Get), now matched; T10/T20 stay -1 on an idle channel as their vendor. **T10/T20 `Get/SetChnDenoise`** read dnType back as 0 because the T20 build also defines PLATFORM_T21; T10/T20 now keep type and QPs like the 3.12.0 vendor (no two-pass encode, cell stays cache). Remaining gap rows re-checked against the disassembly: all are classes c/d (closed vendor encoder, driver control missing, 3.9.0-only names, vendor-equal defect), none is a cheap a/b item; T10 `SetMbRC` is a matched no-op (the vendor T10 controller has no macroblock part).

Not re-measured in this update: T10 (not re-tested on the open stack), T41 (its fixes live in `claude/release-t41`, not in the release), the `_Sec` and `MultiCamera_` variants of T23 (the apitest exercises the base function), and every function the apitest skips on purpose. T10 uses the T20 userspace build (and the T20 SDK tuning code in the driver), so its cells mirror T20 unless the note says otherwise. T30 and T40 are tabled since 2026-10-10 (see the T30/T40 paragraph below).

T30/T40 columns (added 2026-10-10, host/build evidence only; no T30 camera, T40 device results only from the cam-K run of 2026-10-10). Vendor function lists: T30 = the export list of the vendor libimp 1.0.5 (fixture `tests/t30/fixtures/t30_vendor_1.0.5_imp_exports.txt`, 334 names; headers `timps/include/T30/1.0.5/zh`); T40 = the vendor headers 1.3.1 (`timps/include/T40/1.3.1/en`; the T40 board runs vendor libimp 1.3.0). Open side: `nm -D` of `build/t30/libimp.so` built from `next` (ed7bd82, 594,232 B; `build-t30.sh` export check: 271 of 334 vendor exports, audio 68 of 68; after the gap work of 2026-10-10 on `claude/gaps-t30`: 317 of 334, audio 68 of 68) and of the T40 `libimp.so` built from `claude/t40-t41-api` (9035e30, 375,752 B). Cell rules: n.a. = not in that vendor list; miss = in the vendor list but not exported; err = exported as an ENOTSUP shim (T40, `src/t40/openimp_p3_compat.c`: all OSD, IVS, DMIC, ISP-OSD, CCM, gamma, mask, module control, sensor attr and `SnapFrame` calls; the T40 contributor's gate in `docs/T40_STATUS.md` says the same: P4 surfaces not used by the gate return ENOTSUP); host = covered by a host test of the shared code (T40: the camera-input trio in `tests/t40`, the ISP control and FrameSource delay/I2D calls in `tests/t41`, which build the shared `src/t40` code with `-DPLATFORM_T41`); ? = exported, behaviour not audited and not run on a camera. Nothing is dev on T30 (host or aud cells only come from the gap work of 2026-10-10, host tests and disassembly comparisons); the T40 dev cells come from the cam-K run of 2026-10-10; SU_* cells are ? (libsysutils is not part of the libimp build). A `?` on T30/T40 does not by itself move a row into the gap lists and the ► mark counts only cache/stub/err/miss on T30/T40. Used-by for T40: timps is binary-verified (nm of the T40 `timpsd`), the prudynt/raptor entries are the per-row source-derived ones (†) and were not re-derived for T40. Earlier finding (export coverage only): the only function raptor needed on T40 and OpenIMP lacked was `SetCameraInputMode`/`SetCameraInputSelect`; they are now implemented (9035e30, host test only, no camera) and show as host. Counted as ENOTSUP shims the streamer-used T40 gaps are many more (see below), so that earlier finding does not hold at the behaviour level. 15 vendor functions that had no row yet were added (T40: `ISP_Bypass_Bind`, `ISP_GetCameraInputMode`, `ISP_SetCameraInputSelect`, `ISP_Get/SetDrawAttr`, `ISP_Get/SetOSDAttr`, `ISP_Get/SetSingleOSDAttr`, `ISP_GetRaw`, `ISP_SetPreDqtime`, `ISP_SetScalerLv`, `ISP_Tuning_Get/SetAeSpeed`, `Get/SetFaceAwb`, `Get/SetHLDCAttr`, `GetISPBypass`, `SetFixedContraster`; T30: `ISP_Tuning_SetISPLDC`). Not tabled: names that the vendor T30 1.0.5 libimp exports without a header (24 names, for example `IMP_Alloc*`, undistort and pad-frame calls), `SU_ADC_Init` and the `IMP_LOG_*` macros.

### Legend

What every cell code means (the same short legend is repeated above each table below). Older text in this file and in `feature-matrix.html` may use the long names in brackets (done (dev), done (?) and so on); the codes are only the short form of them. In the function tables the "Used by" column is shortened to p = prudynt, r = raptor, t = timps (a long per-SoC list is given in the numbered note instead):

- **dev** (done, device-tested): implemented and device-tested on that SoC (a camera run exists: apitest or imgfx PASS on 2026-10-06 or an earlier documented device test; see the note)
- **host** (done, host tests only): implemented, host tests only (unit/layout/fake-device tests); not run on a camera yet
- **aud** (done per audit): really connected per the static audit (reaches the driver/hardware or is a real userspace implementation), but nobody tracked a device test for this call
- **no-op** (vendor no-op): the vendor stack itself does nothing visible (or the measurement could not show an effect); OpenIMP matches that
- **cache** (cache-only): the value is only stored and read back; nothing is applied
- **stub**: returns 0 (or the driver answers 0) without any effect
- **err** (error): exported but fails (returns -1/ENOTSUP, driver -EINVAL/-EPERM/-EOPNOTSUPP) or has a known defect
- **miss** (missing): symbol not exported by the open libimp/libsysutils
- **n.a.** (not in vendor API): the function does not exist in that SoC's vendor API (header set of that SoC); not "unsupported"
- **?**: cause or state not determined
- **►**: leading mark on the function name: at least one streamer (timps/prudynt/raptor) uses it and its cell on that SoC is a gap
- **†**: streamer usage derived from source only (no binary of that streamer was built for that SoC)

**Used by** (column added 2026-10-05): which streamers import the function, determined from real imports, not guesses. `timps`: `nm -D --undefined-only` of the `timpsd` binaries of the per-camera builds (one build per SoC, T20 with two identical import sets, T21 also with the vendor-stack build) incl. weak imports; the timps source has no dlsym use, so nothing is hidden behind dlsym. `prudynt`, `raptor`: T23 is binary-verified (nm of `prudynt`, `rvd`, `rad` of the T23 build, raptor-hal linked in statically); for the other SoCs the sources were run through the C preprocessor with `-DPLATFORM_Txx` and the vendor header set of that SoC and the identifiers were collected. That method reproduces the T23 binary imports exactly (19/19 prudynt, 51/51 raptor rows), so it is trusted, but a name marked **†** is **source-only** (no T10/T20/T21/T31/T41 binary of that streamer was built). Without a †, the entry is binary-verified. `–` = none of the three imports it on any SoC where the function exists. `T21: ...; T23: ...` = the set differs per SoC (only SoCs that have the function). Variants (`_Sec`, `MultiCamera_`) count for the base function. **Bold** used-by text and a leading `►` on the function name = a priority row: at least one streamer imports it and its cell on that SoC is a gap (missing, error, cache-only, stub or ?). `vendor no-op` and `done (...)` rows are not counted as priority gaps.


### Progress per SoC

The counts below were recomputed on 2026-10-11 for agg-36 from the full-list tables of this file (first count 2026-10-10, all cell changes up to agg-34 included then; script a local helper script that parses the full lists, not in the repo). Method: rows of the full lists, a Get/Set row = 2 functions, a wildcard row counts its listed functions; the totals match the audit counts within one function (T10/T20 332 vs 331, T21 330, T23 442 vs 441, T31 406 vs 405). The T41 row is the unchanged figure of 2026-10-06 (T41 is being reworked by other agents; its cells were not touched). T30 and T40 were counted the same way on 2026-10-10; their done % is low because most of their cells are `?` (exported, not audited, not run on a camera), not because they are missing.

Counts are per vendor function of that SoC (T23 folded: base function = one; Get and Set count separately). "gaps" = cache-only + stub + error + missing; "vendor no-op" is not a gap; "audit gaps" = the same sum in the audit before the work of 2026-10-05 (T23 unfolded, about 3x per base function). "done %" = dev + host + aud / vendor fns. Bar: █ dev, ▓ host, ▒ aud / ?, ○ no-op, ░ gaps (40 characters per SoC).

| SoC | fns | dev | host | aud | ? | no-op | cache | stub | err | miss | **gaps** | audit gaps | done % |
|---|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| T10 | 327 | 17 | 54 | 219 | 0 | 3 | 13 | 0 | 1 | 20 | **34** | 100 | 88.7 % |
| T20 | 327 | 196 | 16 | 82 | 0 | 1 | 11 | 0 | 1 | 20 | **32** | 100 | 89.9 % |
| T21 | 327 | 209 | 11 | 83 | 0 | 8 | 5 | 0 | 1 | 10 | **16** | 77 | 92.7 % |
| T23 | 436 | 283 | 16 | 103 | 0 | 12 | 5 | 0 | 17 | 0 | **22** | 212 | 92.2 % |
| T30 | 347 | 0 | 13 | 22 | 293 | 0 | 8 | 0 | 0 | 11 | **19** | – | 10.1 % |
| T31 | 387 | 225 | 16 | 133 | 0 | 5 | 2 | 0 | 1 | 5 | **8** | 65 | 96.6 % |
| T40 | 398 | 57 | 91 | 0 | 177 | 0 | 2 | 0 | 21 | 50 | **73** | – | 37.2 % |
| T41 | 445 | 8 | 60 | 214 | 0 | 2 | 8 | 1 | 27 | 125 | **161** | 227 | 63.4 % |

Columns: fns = vendor functions of that SoC; dev/host/aud/?/no-op/cache/stub/err/miss = counts per cell code (codes as in the legend); gaps = cache + stub + err + miss; audit gaps = the same sum in the audit before the work of 2026-10-05; done % = (dev + host + aud) / fns.

```
T10  ██▓▓▓▓▓▓▓▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒░░░░
T20  ████████████████████████▓▓▒▒▒▒▒▒▒▒▒▒░░░░
T21  ██████████████████████████▓▒▒▒▒▒▒▒▒▒▒○░░
T23  ██████████████████████████▓▓▒▒▒▒▒▒▒▒▒○░░
T30  ▓▓▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒░░
T31  ███████████████████████▓▒▒▒▒▒▒▒▒▒▒▒▒▒▒○░
T40  ██████▓▓▓▓▓▓▓▓▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒░░░░░░░░░
T41  █▓▓▓▓▓▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒▒░░░░░░░░░░░░░░░
```

### Streamer-relevant gaps per SoC (priority)

Gaps (cache-only, stub, error, missing, ?) of functions that at least one of timps / prudynt / raptor imports on that SoC; derived as described under "Used by" in the legend. Raptor and prudynt entries for T10/T20/T21/T30/T31/T40/T41 are source-only (no binary built); timps (T40 from the T40 `timpsd`) and T23 are binary-verified. `?` cells of T30/T40 are not counted. T40 uses the row-level used-by entries, which are not per SoC.

- **T10**: 0 (no gap in a function that a streamer imports)
- **T20**: 0 (no gap in a function that a streamer imports)
- **T21**: 0 (no gap in a function that a streamer imports)
- **T23**: 0 (no gap in a function that a streamer imports)
- **T30**: 0 (no gap in a function that a streamer imports)
- **T31**: 0 (no gap in a function that a streamer imports)
- **T40**: 2 (timps 0, prudynt 0, raptor 2): `DMIC_*` (error; raptor), `DMIC_DisableAecRefFrame` (error; raptor)
- **T41**: 19 (timps 0, prudynt 3, raptor 18): `ISP_Tuning_Get/SetModuleControl` (error; raptor), `ISP_Tuning_SetAutoZoom` (error; prudynt), `ISP_Tuning_SetMaskBlock` (error; raptor), `ISP_Tuning_SetScalerLv` (error; raptor), `Encoder_SetChnMaxPictureSize` (cache-only; raptor), `FrameSource_Get/SetChnFifoAttr` (cache-only; prudynt+raptor), `FrameSource_Get/SetDelay` (error; raptor), `FrameSource_Get/SetFrameDepth` (cache-only; prudynt+raptor), `FrameSource_Get/SetI2dAttr` (error; raptor), `FrameSource_Get/SetMaxDelay` (error; raptor), `FrameSource_Get/SetPool` (cache-only; raptor), `FrameSource_GetTimedFrame` (error; raptor), `ISP_Tuning_CreateOsdRgn` (error; raptor), `ISP_Tuning_DestroyOsdRgn` (error; raptor), `ISP_Tuning_SetOsdPoolSize` (stub; raptor), `ISP_Tuning_SetOsdRgnAttr` (error; raptor), `ISP_Tuning_ShowOsdRgn` (error; raptor), `DMIC_*` (error; raptor), `DMIC_DisableAecRefFrame` (error; raptor)

### ISP tuning

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|
| `ISP_Bypass_Bind` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | n.a. | – |
| `ISP_Get/SetCsccrMode` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Get/SetISPBypass` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | p† |
| `ISP_Get/SetInternalChnAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_GetRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | n.a. | – |
| `ISP_LDC_Get/SetAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_LDC_INIT` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_RAW_RwControl` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_SET_GPIO_INIT_OR_FREE` | n.a. | n.a. | n.a. | aud | n.a. | aud | n.a. | miss | – |
| `ISP_SET_GPIO_STA` | n.a. | n.a. | n.a. | aud | n.a. | aud | n.a. | miss | – |
| `ISP_SetPreDqtime` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `ISP_SetVicDoneCbFunc` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_StartNightMode` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `ISP_Tuning_Get/SetDrawBlock` [3] | n.a. | n.a. | n.a. | err | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetHLDCAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `ISP_Tuning_Get/SetISPCSCAttr` [49] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | host | – |
| `ISP_Tuning_Get/SetMask` [55] | n.a. | n.a. | n.a. | no-op | n.a. | aud | dev | n.a. | r† |
| ► `ISP_Tuning_Get/SetModuleControl` [4] | n.a. | n.a. | host | aud | n.a. | aud | host | host | T31/T41: r† |
| `ISP_Tuning_Get/SetStatisConfig` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | miss | – |
| `ISP_Tuning_Get/SetTmoCurve` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAutoZoom` [6] | n.a. | n.a. | n.a. | host | n.a. | n.a. | host | host | – |
| `ISP_Tuning_GetBrightness` [66] | aud | aud | aud | aud | ? | aud | host | aud | p† r† |
| `ISP_Tuning_GetHVFlip` | n.a. | n.a. | n.a. | aud | n.a. | aud | ? | miss | T23/T31: r† |
| `ISP_Tuning_GetISPBypass` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `ISP_Tuning_GetMaskBlock` [7] | n.a. | n.a. | n.a. | host | n.a. | n.a. | n.a. | host | – |
| `ISP_Tuning_SetAntiFogAttr` [8] | miss | miss | no-op | n.a. | host | n.a. | n.a. | n.a. | – |
| ► `ISP_Tuning_SetAutoZoom` [9] | n.a. | n.a. | n.a. | host | n.a. | host | host | host | see note |
| `ISP_Tuning_SetFixedContraster` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `ISP_Tuning_SetISPLDC` | n.a. | n.a. | n.a. | n.a. | miss | n.a. | n.a. | n.a. | – |
| ► `ISP_Tuning_SetMaskBlock` [11] | n.a. | n.a. | n.a. | host | n.a. | n.a. | n.a. | host | T41: r† |
| `ISP_Tuning_SetMeshShadingScale` [91] | miss | miss | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| ► `ISP_Tuning_SetScalerLv` [12] | n.a. | n.a. | n.a. | no-op | n.a. | aud | n.a. | host | r† |
| `ISP_Tuning_SetTemperDnsCtl` [83] | host | host | aud | n.a. | cache | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_SetTmoFaceae` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `ISP_Tuning_SwitchBin` [13] | n.a. | n.a. | n.a. | err | n.a. | n.a. | dev | host | T41: p† |
| `ISP_Tuning_WaitFrameDone` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | miss | – |
| `ISP_WDR_OPEN` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |

Notes:

1. `ISP_Get/SetFrameDrop`: T41: 40-byte request {ch, 3 entries}, device-tested (claude/t41-driver-gaps, in agg-35); T31: stock lsize/fmark semantics, the ISP drops the frames (open-tx-isp claude/release-t31-framedrop; 29.6 fps to 14.8/7.4 fps, disabled restores, lsize 32 refused)
3. `ISP_Tuning_Get/SetDrawBlock`: T23: 0x8000180 on the stock mscaler draw table (open-tx-isp claude/gaps-t23-t31-drv, class c; emulator-identical to the stock handlers including the stock Get slip of colour byte 23; a draw block above 5 is refused, beyond stock); host build only, device test pending; the stored block has no image effect yet (the MSCA update that programs it is not run, as for the mask blocks); Not in agg-36: the open-tx-isp branch claude/gaps-t23-t31-drv is held back until its T23 SwitchBin is reworked (review: memcpy over live banks), so the agg-36 driver still behaves as before (cell unchanged).
4. `ISP_Tuning_Get/SetModuleControl`: T21: tuning 0x80000e2 (agg-25); T41: driver rejects 0x8000072 (-EINVAL); T40: Get dev, Set host (only the Get is device-tested on cam-K 2026-10-10, OpenIMP vs vendor library)
5. `ISP_Tuning_Get/SetWdrOutputMode`: T41: the stock driver has no handler and refuses (claude/t41-isp-round2); explained, counted as vendor no-op
6. `ISP_Tuning_GetAutoZoom`: T23: stock autozoom control 0x80000e8 (open-tx-isp agg-28), host/build-tested only; T41: driver has no stock handler, refuses; T41 (2026-10-10): deferred, live MSCA reprogramming carries a hang risk
7. `ISP_Tuning_GetMaskBlock`: T23: 0x8000183 in claude/t23-awb-runtime only (not in agg-25); T23: 0x8000183 on the stock mscaler mask table (open-tx-isp agg-28, emulator-identical); the stored masks have no image effect yet (the mscaler update that consumes them is not run)
8. `ISP_Tuning_SetAntiFogAttr`: T21: exported like the vendor libimp; control 0x8000163 is accepted by the OEM kernel without effect; T10/T20 (class c, 2026-10-10): the vendor sends control 0x98e903 (CUSTOM_ANTI_FOG); the open T10/T20 driver serves it through the antifog Iridix preset path that hung cameras (comment in tx-isp-core-tuning.c), so it is not exported; DefogStrength is the safe equivalent; wishlist: a driver preset that does not touch Iridix state. T21: apitest PASS (ret 0); T30 1.0.5: the enum goes out as control 0x8000163, the same code as T21; host test with a fake ioctl, driver behaviour on T30 not checked
9. `ISP_Tuning_SetAutoZoom`: T23: stock autozoom control 0x80000e8: crop window and scaler output into the channel MSCA record (agg-28; windows outside the picture, below 64x64 or odd are refused, beyond stock), host/build-tested only; T31: programs scaler/crop, refuses size change; T41: driver has no stock handler, refuses; used by: T23: raptor; T31: prudynt†, raptor†; T41: prudynt†; T41 (2026-10-10): deferred, live MSCA reprogramming carries a hang risk
11. `ISP_Tuning_SetMaskBlock`: T23: 0x8000183 in claude/t23-awb-runtime only (not in agg-25); T41: driver has no handler, fails with -EPERM since agg-25 (was silent 0); vendor behaviour unverified; T23: 0x8000183 on the stock mscaler mask table (open-tx-isp agg-28, emulator-identical); the stored masks have no image effect yet (the mscaler update that consumes them is not run); T41 (2026-10-10): deferred, live MSCA reprogramming carries a hang risk
12. `ISP_Tuning_SetScalerLv`: T23: the stock module has no handler for 0x80000e9 either (-EINVAL as stock); T41: driver has no handler, fails with -EPERM (was silent 0); vendor behaviour unverified; T41 (2026-10-10): deferred, live MSCA reprogramming carries a hang risk
13. `ISP_Tuning_SwitchBin`: T23: the agg-36 driver rejects 0x8000185 (-EINVAL); a handler exists in open-tx-isp claude/gaps-t23-t31-drv (tisp_switch_bin around the driver's IQ loader: file, header, size and CRC checks, day and night banks swapped under the AE/AWB pause; module build only), held back because the bank swap must be reworked first (review: memcpy over live banks); T40/T41 (claude/switchbin-t4x, 2026-10-10): control 0x80000a5 from the vendor T40 1.3.1 and T41 1.2.6 libimp disassembly (ioctl 0xc0105436 / 0xc0105435, payload IMPISPBinAttr, 68 / 132 bytes, vendor check order and return codes); T40 device-tested on the Eufy (sc830ai, vendor kernel driver): switch to a copy of the bin, back, and enable 0 return 0, the stream keeps decoding cleanly, picture unchanged; T41 library host-tested only, the T41 kernel side is in open-tx-isp claude/switchbin-t41 (module build and host test, no device test yet; the vendor T41 module was not tested)
14. `ISP_WDR_ENABLE`: T41: driver control, device-tested (claude/t41-driver-gaps, in agg-35)
15. `ISP_WDR_ENABLE_GET`: T41: driver control, device-tested (claude/t41-driver-gaps, in agg-35)
66. `ISP_Tuning_GetBrightness`: audit: reaches the driver/kernel; T40: returned -1 on cam-K 2026-10-10 (vendor library returns the value); fixed after RC1: the getters return the vendor per-input cache (host-tested)
91. `ISP_Tuning_SetMeshShadingScale`: T10/T20 (class c): the vendor writes bits 2..4 of an ISP lens-shading register through a userspace mapping (3.12.0 0x62930); there is no driver control for it, wishlist: a tuning control in the T10/T20 driver

<details><summary>All 117 rows of this area (35 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|
| `ISP_AddSensor` [16] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `ISP_Bypass_Bind` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | n.a. | – |
| `ISP_Close` [17] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `ISP_DelSensor` [18] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `ISP_DisableSensor` [19] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `ISP_DisableTuning` [20] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `ISP_EnableSensor` [21] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `ISP_EnableTuning` [22] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `ISP_Get/SetCsccrMode` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Get/SetDefaultBinPath` [23] | n.a. | n.a. | n.a. | dev | n.a. | dev | host | aud | r† |
| ► `ISP_Get/SetFrameDrop` [1] | n.a. | n.a. | n.a. | dev | n.a. | dev | host | dev | T41: r† |
| `ISP_Get/SetISPBypass` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | p† |
| `ISP_Get/SetInternalChnAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Get/SetSensorRegister` [24] | aud | aud | aud | aud | ? | aud | host | dev | r† |
| `ISP_GetCameraInputMode` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | dev | n.a. | T40: r† |
| `ISP_GetRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | n.a. | – |
| `ISP_LDC_Get/SetAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_LDC_INIT` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Open` [25] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `ISP_RAW_RwControl` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_SET_GPIO_INIT_OR_FREE` | n.a. | n.a. | n.a. | aud | n.a. | aud | n.a. | miss | – |
| `ISP_SET_GPIO_STA` | n.a. | n.a. | n.a. | aud | n.a. | aud | n.a. | miss | – |
| `ISP_SetCameraInputMode` [26] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | host | n.a. | – |
| `ISP_SetCameraInputSelect` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | T40: r† |
| `ISP_SetFixedContraster` [2] | n.a. | n.a. | n.a. | dev | n.a. | dev | n.a. | n.a. | – |
| `ISP_SetPreDqtime` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `ISP_SetScalerLv` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `ISP_SetStreamOut` [27] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | n.a. | n.a. | – |
| `ISP_SetSwitchgpio` [28] | n.a. | n.a. | n.a. | host | n.a. | n.a. | n.a. | n.a. | – |
| `ISP_SetVicDoneCbFunc` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_StartNightMode` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `ISP_StreamCheck` [29] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_DisableMovestate` [30] | aud | aud | aud | no-op | ? | host | n.a. | n.a. | r† |
| `ISP_Tuning_EnableDRC` [31] | n.a. | n.a. | n.a. | aud | n.a. | host | n.a. | n.a. | r† |
| `ISP_Tuning_EnableDefog` [32] | n.a. | n.a. | n.a. | aud | n.a. | dev | n.a. | n.a. | r† |
| `ISP_Tuning_EnableMovestate` [33] | aud | aud | aud | no-op | ? | host | n.a. | n.a. | r† |
| `ISP_Tuning_Get/SetAntiFlickerAttr` [34] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `ISP_Tuning_Get/SetBacklightComp` [35] | n.a. | n.a. | n.a. | aud | n.a. | aud | n.a. | n.a. | p† r† t |
| `ISP_Tuning_Get/SetBcshHue` [36] | n.a. | n.a. | n.a. | aud | n.a. | aud | ? | aud | p† r† t |
| `ISP_Tuning_Get/SetCCMAttr` [37] | n.a. | n.a. | n.a. | aud | n.a. | aud | host | host | T41: r† |
| `ISP_Tuning_Get/SetColorfxMode` [38] | aud | aud | dev | n.a. | ? | n.a. | n.a. | n.a. | t |
| `ISP_Tuning_Get/SetContrast` [39] | aud | aud | aud | aud | ? | aud | ? | aud | p† r† t |
| `ISP_Tuning_Get/SetCsc_Attr` [40] | n.a. | n.a. | n.a. | aud | n.a. | aud | n.a. | n.a. | r† |
| `ISP_Tuning_Get/SetDPC_Strength` [41] | n.a. | n.a. | n.a. | aud | n.a. | aud | n.a. | n.a. | r† t |
| `ISP_Tuning_Get/SetDRC_Strength` [42] | n.a. | n.a. | aud | aud | n.a. | aud | n.a. | n.a. | p† r† t |
| `ISP_Tuning_Get/SetDefog_Strength` [43] | n.a. | n.a. | n.a. | aud | n.a. | aud | n.a. | n.a. | p† r† t |
| `ISP_Tuning_Get/SetDrawBlock` [3] | n.a. | n.a. | n.a. | err | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetFrontCrop` [44] | n.a. | n.a. | n.a. | dev | n.a. | dev | n.a. | n.a. | r† |
| `ISP_Tuning_Get/SetGamma` [45] | aud | aud | aud | dev | ? | aud | n.a. | n.a. | p† r† |
| ► `ISP_Tuning_Get/SetGammaAttr` [46] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | host | p† r† |
| `ISP_Tuning_Get/SetHLDCAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `ISP_Tuning_Get/SetHVFLIP` [47] | n.a. | n.a. | n.a. | aud | n.a. | aud | ? | aud | see note |
| `ISP_Tuning_Get/SetHiLightDepress` [48] | aud | aud | aud | aud | ? | aud | n.a. | n.a. | p† r† t |
| `ISP_Tuning_Get/SetISPCSCAttr` [49] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | host | – |
| `ISP_Tuning_Get/SetISPCustomMode` [50] | n.a. | n.a. | n.a. | aud | n.a. | host | n.a. | n.a. | r† |
| `ISP_Tuning_Get/SetISPHVflip` [89] | dev | dev | n.a. | n.a. | host | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_Get/SetISPHflip` [51] | aud | aud | aud | aud | ? | aud | n.a. | n.a. | p† t |
| `ISP_Tuning_Get/SetISPRunningMode` [52] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `ISP_Tuning_Get/SetISPVflip` [53] | aud | aud | aud | aud | ? | aud | n.a. | n.a. | p† t |
| `ISP_Tuning_Get/SetIntegrationTime` [54] | aud | dev | dev | n.a. | ? | n.a. | n.a. | n.a. | t |
| `ISP_Tuning_Get/SetMask` [55] | n.a. | n.a. | n.a. | no-op | n.a. | aud | dev | n.a. | r† |
| `ISP_Tuning_Get/SetMaxAgain` [56] | aud | dev | dev | dev | ? | dev | n.a. | n.a. | p† r† t |
| `ISP_Tuning_Get/SetMaxDgain` [57] | aud | dev | dev | dev | ? | dev | n.a. | n.a. | p† r† t |
| ► `ISP_Tuning_Get/SetModuleControl` [4] | n.a. | n.a. | host | aud | n.a. | aud | host | host | T31/T41: r† |
| `ISP_Tuning_Get/SetModule_Ratio` [58] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | aud | – |
| `ISP_Tuning_Get/SetSaturation` [59] | aud | aud | aud | aud | ? | aud | ? | aud | p† r† t |
| `ISP_Tuning_Get/SetSensorHflip` [60] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_Get/SetSensorVflip` [61] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_Get/SetSinterDnsAttr` [62] | host | host | aud | n.a. | ? | n.a. | n.a. | n.a. | p† |
| `ISP_Tuning_Get/SetStatisConfig` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | miss | – |
| `ISP_Tuning_Get/SetTemperDnsAttr` [63] | host | host | aud | n.a. | ? | n.a. | n.a. | n.a. | p† |
| `ISP_Tuning_Get/SetTmoCurve` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetWDRAttr` [90] | dev | dev | n.a. | n.a. | host | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_Get/SetWdrOutputMode` [5] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | no-op | – |
| `ISP_Tuning_Get/SetWdr_OutputMode` [64] | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | n.a. | r† |
| `ISP_Tuning_GetAutoZoom` [6] | n.a. | n.a. | n.a. | host | n.a. | n.a. | host | host | – |
| `ISP_Tuning_GetBlcAttr` [65] | n.a. | n.a. | n.a. | no-op | n.a. | dev | n.a. | n.a. | r† |
| `ISP_Tuning_GetBrightness` [66] | aud | aud | aud | aud | ? | aud | host | aud | p† r† |
| `ISP_Tuning_GetEVAttr` [67] | aud | dev | dev | dev | ? | dev | n.a. | n.a. | p† r† t |
| `ISP_Tuning_GetHVFlip` | n.a. | n.a. | n.a. | aud | n.a. | aud | ? | miss | T23/T31: r† |
| `ISP_Tuning_GetISPBypass` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `ISP_Tuning_GetMaskBlock` [7] | n.a. | n.a. | n.a. | host | n.a. | n.a. | n.a. | host | – |
| `ISP_Tuning_GetRawDRC` [68] | host | host | aud | n.a. | ? | n.a. | n.a. | n.a. | p† |
| `ISP_Tuning_GetSceneMode` [69] | aud | aud | aud | n.a. | ? | n.a. | n.a. | n.a. | – |
| ► `ISP_Tuning_GetSensorAttr` [70] | n.a. | n.a. | n.a. | dev | n.a. | dev | dev | host | see note |
| `ISP_Tuning_GetSensorFPS` [71] | aud | dev | dev | dev | ? | dev | host | aud | p† r† t |
| `ISP_Tuning_GetSharpness` [72] | aud | aud | aud | aud | ? | aud | ? | aud | p† r† |
| `ISP_Tuning_GetTotalGain` [73] | aud | dev | dev | dev | ? | dev | n.a. | n.a. | p† r† t |
| `ISP_Tuning_SaveAllParam` [88] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_SetAntiFogAttr` [8] | miss | miss | no-op | n.a. | host | n.a. | n.a. | n.a. | – |
| ► `ISP_Tuning_SetAutoZoom` [9] | n.a. | n.a. | n.a. | host | n.a. | host | host | host | see note |
| `ISP_Tuning_SetBrightness` [74] | aud | aud | dev | aud | ? | aud | ? | aud | p† r† t |
| `ISP_Tuning_SetDPStrength` [10] | dev | dev | dev | n.a. | host | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_SetFWFreeze` [75] | aud | aud | aud | n.a. | ? | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_SetFixedContraster` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `ISP_Tuning_SetISPBypass` [76] | aud | aud | aud | aud | ? | aud | ? | n.a. | p† r† |
| `ISP_Tuning_SetISPLDC` | n.a. | n.a. | n.a. | n.a. | miss | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_SetISPProcess` [77] | aud | aud | aud | n.a. | ? | n.a. | n.a. | n.a. | – |
| ► `ISP_Tuning_SetMaskBlock` [11] | n.a. | n.a. | n.a. | host | n.a. | n.a. | n.a. | host | T41: r† |
| `ISP_Tuning_SetMeshShadingScale` [91] | miss | miss | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_SetRawDRC` [78] | host | host | no-op | n.a. | ? | n.a. | n.a. | n.a. | p† |
| ► `ISP_Tuning_SetScalerLv` [12] | n.a. | n.a. | n.a. | no-op | n.a. | aud | n.a. | host | r† |
| `ISP_Tuning_SetSceneMode` [79] | aud | aud | no-op | n.a. | ? | n.a. | n.a. | n.a. | t |
| `ISP_Tuning_SetSensorFPS` [80] | aud | dev | dev | dev | ? | dev | host | host | p† r† t |
| `ISP_Tuning_SetSharpness` [81] | aud | aud | no-op | aud | ? | aud | ? | aud | p† r† t |
| `ISP_Tuning_SetSinterStrength` [82] | aud | aud | aud | aud | ? | aud | n.a. | n.a. | p† r† t |
| `ISP_Tuning_SetTemperDnsCtl` [83] | host | host | aud | n.a. | cache | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_SetTemperStrength` [84] | aud | aud | aud | aud | ? | aud | n.a. | n.a. | p† r† t |
| `ISP_Tuning_SetTmoFaceae` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_SetVideoDrop` [85] | host | host | host | host | ? | host | ? | host | r† |
| ► `ISP_Tuning_SwitchBin` [13] | n.a. | n.a. | n.a. | err | n.a. | n.a. | dev | host | T41: p† |
| `ISP_Tuning_WaitFrame` [86] | host | host | host | host | ? | aud | n.a. | n.a. | r† |
| `ISP_Tuning_WaitFrameDone` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | miss | – |
| ► `ISP_WDR_ENABLE` [14] | n.a. | n.a. | n.a. | n.a. | n.a. | aud | host | dev | r† |
| ► `ISP_WDR_ENABLE_GET` [15] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | dev | r† |
| `ISP_WDR_ENABLE_Get` [87] | n.a. | n.a. | n.a. | n.a. | n.a. | aud | n.a. | n.a. | – |
| `ISP_WDR_OPEN` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |

Notes:

16. `ISP_AddSensor`: audit: reaches the driver/kernel
17. `ISP_Close`: audit: userspace implementation
18. `ISP_DelSensor`: audit: reaches the driver/kernel
19. `ISP_DisableSensor`: audit: reaches the driver/kernel
20. `ISP_DisableTuning`: audit: userspace implementation
21. `ISP_EnableSensor`: audit: reaches the driver/kernel
22. `ISP_EnableTuning`: audit: reaches the driver/kernel
23. `ISP_Get/SetDefaultBinPath`: audit: reaches the driver/kernel; userspace implementation
1. `ISP_Get/SetFrameDrop`: T41: 40-byte request {ch, 3 entries}, device-tested (claude/t41-driver-gaps, in agg-35); T31: stock lsize/fmark semantics, the ISP drops the frames (open-tx-isp claude/release-t31-framedrop; 29.6 fps to 14.8/7.4 fps, disabled restores, lsize 32 refused)
24. `ISP_Get/SetSensorRegister`: T41: claude/t41-isp-round2 (not in agg-25); not device-tested; T23: bus type read at the stock offset (was "There isn't sensor!"); apitest read PASS, write skipped; T41: request with vinum at 32 and bus type at 36, device-tested (claude/t41-driver-gaps, in agg-35)
25. `ISP_Open`: audit: reaches the driver/kernel
26. `ISP_SetCameraInputMode`: audit: reaches the driver/kernel
2. `ISP_SetFixedContraster`: T23: driver routes 0x8000102 (agg-25), apitest PASS 2026-10-10; T31: the argument is the IMPISPFixedContrastAttr pointer (not a mode): with the tuning session open it goes to the driver as control 0x8000102 like the vendor (apitest PASS 2026-10-10, host test tests/t31/fixed_contrast_test.c); the open T31 driver handler dropped the 3 bytes before, it now calls tisp_set_bcsh_fixed_contrast like the stock s_ctrl (open-tx-isp claude/gaps-t23-t31-drv; mode, y low and the fixed value reach the BCSH MJPEG range; only matters for the MJPEG path); module build only, device test of the effect pending; Not in agg-36: the open-tx-isp branch claude/gaps-t23-t31-drv is held back until its T23 SwitchBin is reworked (review: memcpy over live banks), so the agg-36 driver still behaves as before (cell unchanged).
27. `ISP_SetStreamOut`: audit: reaches the driver/kernel
28. `ISP_SetSwitchgpio`: T23: ioctl (claude/t23t31-cacheonly, not in agg-25)
29. `ISP_StreamCheck`: audit: reaches the driver/kernel
30. `ISP_Tuning_DisableMovestate`: T23: stock driver also no-op (OEM-same); T31: vendor library logic (AE block of control 0x800002c, move.txt limits, IDR on disable), host-tested; the stock T31 driver ignores the control
31. `ISP_Tuning_EnableDRC`: T31: wired to the driver (agg-25)
32. `ISP_Tuning_EnableDefog`: T31: takes the enable flag like the vendor (the stub always wrote 1); imgfx A/B against the old library on the T31 camera
33. `ISP_Tuning_EnableMovestate`: T23: stock driver also no-op (OEM-same); T31: vendor library logic (AE block of control 0x800002c, move.txt limits, IDR on disable), host-tested; the stock T31 driver ignores the control
34. `ISP_Tuning_Get/SetAntiFlickerAttr`: audit: reaches the driver/kernel
35. `ISP_Tuning_Get/SetBacklightComp`: audit: reaches the driver/kernel
36. `ISP_Tuning_Get/SetBcshHue`: audit: reaches the driver/kernel
37. `ISP_Tuning_Get/SetCCMAttr`: T41: vendor 1.2.6 error ladder (claude/t41-isp-round2, not in agg-25); not device-tested; T40: Get dev, Set host (only the Get is device-tested on cam-K 2026-10-10, OpenIMP vs vendor library)
38. `ISP_Tuning_Get/SetColorfxMode`: T21: B/W, negative and vivid change the picture (imgfx); SEPIA works (beyond vendor, was -1): CCM saturation 0 plus a tinted CSC matrix, dU/dV +10/+9 and +17/+19 in two runs; T10/T20 re-test pending (RC imgfx ran in the dark)
39. `ISP_Tuning_Get/SetContrast`: audit: reaches the driver/kernel
40. `ISP_Tuning_Get/SetCsc_Attr`: audit: reaches the driver/kernel
41. `ISP_Tuning_Get/SetDPC_Strength`: audit: reaches the driver/kernel
42. `ISP_Tuning_Get/SetDRC_Strength`: audit: reaches the driver/kernel
43. `ISP_Tuning_Get/SetDefog_Strength`: audit: reaches the driver/kernel
3. `ISP_Tuning_Get/SetDrawBlock`: T23: 0x8000180 on the stock mscaler draw table (open-tx-isp claude/gaps-t23-t31-drv, class c; emulator-identical to the stock handlers including the stock Get slip of colour byte 23; a draw block above 5 is refused, beyond stock); host build only, device test pending; the stored block has no image effect yet (the MSCA update that programs it is not run, as for the mask blocks); Not in agg-36: the open-tx-isp branch claude/gaps-t23-t31-drv is held back until its T23 SwitchBin is reworked (review: memcpy over live banks), so the agg-36 driver still behaves as before (cell unchanged).
44. `ISP_Tuning_Get/SetFrontCrop`: T31: wired to driver 0x80000e3 / 0x80000e7 (agg-25); agg-34: the crop crash is fixed (guard against windows the MSCA would upscale); device-tested on 2026-10-10: a valid window zooms both streams (proven), a too-small window is refused with -EINVAL without a hang, the window survives a restart and a flip, hence dev; T23: dev after the 2026-10-09 crop fixes (crop off, lock release, window that does not fit is dropped); T10/T20/T21 crop device-tested but not a vendor call there (T20/T10: a window set before stream-on is applied and survives substream restarts; the release of the window at tuning-session close is not verified)
45. `ISP_Tuning_Get/SetGamma`: T23: applied at once (beyond stock); curve test, falling curve rejected, restore ok on cam-B 2026-10-05
46. `ISP_Tuning_Get/SetGammaAttr`: T41: vendor 1.2.6 error ladder (claude/t41-isp-round2, not in agg-25); not device-tested; T40: Get dev, Set host (only the Get is device-tested on cam-K 2026-10-10, OpenIMP vs vendor library)
47. `ISP_Tuning_Get/SetHVFLIP`: audit: reaches the driver/kernel; used by: T23/T31: raptor†; T41: raptor†, timps
48. `ISP_Tuning_Get/SetHiLightDepress`: audit: reaches the driver/kernel
49. `ISP_Tuning_Get/SetISPCSCAttr`: T41: vendor 1.2.6 error ladder (claude/t41-isp-round2, not in agg-25); not device-tested
50. `ISP_Tuning_Get/SetISPCustomMode`: T31: wired to driver 0x80000e3 / 0x80000e7 (agg-25); T10/T20/T21 crop device-tested but not a vendor call there
51. `ISP_Tuning_Get/SetISPHflip`: audit: reaches the driver/kernel
52. `ISP_Tuning_Get/SetISPRunningMode`: audit: reaches the driver/kernel
53. `ISP_Tuning_Get/SetISPVflip`: audit: reaches the driver/kernel
54. `ISP_Tuning_Get/SetIntegrationTime`: audit: reaches the driver/kernel
55. `ISP_Tuning_Get/SetMask`: T23: stock tx-isp-t23.ko leaves 0x80000e5 unhandled (-1)
56. `ISP_Tuning_Get/SetMaxAgain`: audit: reaches the driver/kernel
57. `ISP_Tuning_Get/SetMaxDgain`: audit: reaches the driver/kernel
4. `ISP_Tuning_Get/SetModuleControl`: T21: tuning 0x80000e2 (agg-25); T41: driver rejects 0x8000072 (-EINVAL); T40: Get dev, Set host (only the Get is device-tested on cam-K 2026-10-10, OpenIMP vs vendor library)
58. `ISP_Tuning_Get/SetModule_Ratio`: audit: reaches the driver/kernel
59. `ISP_Tuning_Get/SetSaturation`: audit: reaches the driver/kernel
60. `ISP_Tuning_Get/SetSensorHflip`: audit: reaches the driver/kernel
61. `ISP_Tuning_Get/SetSensorVflip`: audit: reaches the driver/kernel
62. `ISP_Tuning_Get/SetSinterDnsAttr`: T10+T20: reach the driver (agg-25, T20 vendor layout); T10 shares the build
63. `ISP_Tuning_Get/SetTemperDnsAttr`: T10+T20: reach the driver (agg-25, T20 vendor layout); T10 shares the build
5. `ISP_Tuning_Get/SetWdrOutputMode`: T41: the stock driver has no handler and refuses (claude/t41-isp-round2); explained, counted as vendor no-op
64. `ISP_Tuning_Get/SetWdr_OutputMode`: T31: reaches the WDR tool block (agg-25)
6. `ISP_Tuning_GetAutoZoom`: T23: stock autozoom control 0x80000e8 (open-tx-isp agg-28), host/build-tested only; T41: driver has no stock handler, refuses; T41 (2026-10-10): deferred, live MSCA reprogramming carries a hang risk
65. `ISP_Tuning_GetBlcAttr`: T23: the stock T23 module has no BLC attr control (-1 as stock; apitest N/A); T31: apitest PASS
66. `ISP_Tuning_GetBrightness`: audit: reaches the driver/kernel; T40: returned -1 on cam-K 2026-10-10 (vendor library returns the value); fixed after RC1: the getters return the vendor per-input cache (host-tested)
67. `ISP_Tuning_GetEVAttr`: audit: reaches the driver/kernel
7. `ISP_Tuning_GetMaskBlock`: T23: 0x8000183 in claude/t23-awb-runtime only (not in agg-25); T23: 0x8000183 on the stock mscaler mask table (open-tx-isp agg-28, emulator-identical); the stored masks have no image effect yet (the mscaler update that consumes them is not run)
68. `ISP_Tuning_GetRawDRC`: T10+T20: reach the driver (agg-25, T20 vendor layout); T10 shares the build
69. `ISP_Tuning_GetSceneMode`: audit: reaches the driver/kernel
70. `ISP_Tuning_GetSensorAttr`: T41: driver claude/t41-connect (agg-25); host tests 58/58; used by: T23/T31: timps; T41: raptor†, timps; T40: device-tested on cam-K 2026-10-10 (OpenIMP vs vendor library)
71. `ISP_Tuning_GetSensorFPS`: audit: reaches the driver/kernel
72. `ISP_Tuning_GetSharpness`: audit: reaches the driver/kernel
73. `ISP_Tuning_GetTotalGain`: audit: reaches the driver/kernel
88. `ISP_Tuning_SaveAllParam`: T10/T20/T21 (4 builds each) and T23/T31: declared in the SDK header, exported by none of the vendor libimp builds (nm); a vendor-stack application cannot link it either
8. `ISP_Tuning_SetAntiFogAttr`: T21: exported like the vendor libimp; control 0x8000163 is accepted by the OEM kernel without effect; T10/T20 (class c, 2026-10-10): the vendor sends control 0x98e903 (CUSTOM_ANTI_FOG); the open T10/T20 driver serves it through the antifog Iridix preset path that hung cameras (comment in tx-isp-core-tuning.c), so it is not exported; DefogStrength is the safe equivalent; wishlist: a driver preset that does not touch Iridix state. T21: apitest PASS (ret 0); T30 1.0.5: the enum goes out as control 0x8000163, the same code as T21; host test with a fake ioctl, driver behaviour on T30 not checked
9. `ISP_Tuning_SetAutoZoom`: T23: stock autozoom control 0x80000e8: crop window and scaler output into the channel MSCA record (agg-28; windows outside the picture, below 64x64 or odd are refused, beyond stock), host/build-tested only; T31: programs scaler/crop, refuses size change; T41: driver has no stock handler, refuses; used by: T23: raptor; T31: prudynt†, raptor†; T41: prudynt†; T41 (2026-10-10): deferred, live MSCA reprogramming carries a hang risk
74. `ISP_Tuning_SetBrightness`: T21: acts (beyond vendor: AE target scaled by value/128, open-tx-isp claude/release-t21-brightness); imgfx Y 28.8/117.7/193.0 for 30/128/225; the vendor kernel only stores the value
10. `ISP_Tuning_SetDPStrength`: T21: exported like the vendor (cap 200 %); reaches the open driver DPC ratio (control 0x8000062); not individually device-tested; T31: none of the 28 vendor libimp builds (1.1.1-1.1.6) exports it, the header set does not declare it either; T10/T20 3.12.0 (0x60138): the same call as T21, capped at 200 %, here mapped onto the open driver DPC ratio (0x8000062); apitest PASS (return code) on T10, T20, T21; the picture effect was not measured; T30 1.0.5: capped at 200 %, mapped onto the DPC ratio control (0x8000062) like T21; host test with a fake tuning ioctl
75. `ISP_Tuning_SetFWFreeze`: audit: reaches the driver/kernel
76. `ISP_Tuning_SetISPBypass`: audit: reaches the driver/kernel
77. `ISP_Tuning_SetISPProcess`: audit: reaches the driver/kernel
11. `ISP_Tuning_SetMaskBlock`: T23: 0x8000183 in claude/t23-awb-runtime only (not in agg-25); T41: driver has no handler, fails with -EPERM since agg-25 (was silent 0); vendor behaviour unverified; T23: 0x8000183 on the stock mscaler mask table (open-tx-isp agg-28, emulator-identical); the stored masks have no image effect yet (the mscaler update that consumes them is not run); T41 (2026-10-10): deferred, live MSCA reprogramming carries a hang risk
78. `ISP_Tuning_SetRawDRC`: T10+T20: reach the driver (agg-25, T20 vendor layout); T10 shares the build; T21: imgfx 2026-10-05: no picture change; vendor no-op or measurement issue (which one: unverified)
12. `ISP_Tuning_SetScalerLv`: T23: the stock module has no handler for 0x80000e9 either (-EINVAL as stock); T41: driver has no handler, fails with -EPERM (was silent 0); vendor behaviour unverified; T41 (2026-10-10): deferred, live MSCA reprogramming carries a hang risk
79. `ISP_Tuning_SetSceneMode`: T21: vendor kernel no-op (imgfx: no picture change, same on the vendor stack by design)
80. `ISP_Tuning_SetSensorFPS`: T41: reaches the sensor (agg-25); -EOPNOTSUPP on the gc5603 of cam-F
81. `ISP_Tuning_SetSharpness`: T21: imgfx 2026-10-05: no picture change; vendor no-op or measurement issue (which one: unverified)
82. `ISP_Tuning_SetSinterStrength`: audit: reaches the driver/kernel
83. `ISP_Tuning_SetTemperDnsCtl`: T10+T20: newly exported (agg-25); T30 1.0.5 (the vendor sends the temper type through control 0x8000167 and ignores night mode): stored in the same cache as the T30 SetTemperDnsAttr, no driver control; host test
84. `ISP_Tuning_SetTemperStrength`: audit: reaches the driver/kernel
85. `ISP_Tuning_SetVideoDrop`: all: callback after 2/4/6 s without frames (agg-25); host-tested; video demand rule
13. `ISP_Tuning_SwitchBin`: T23: the agg-36 driver rejects 0x8000185 (-EINVAL); a handler exists in open-tx-isp claude/gaps-t23-t31-drv (tisp_switch_bin around the driver's IQ loader: file, header, size and CRC checks, day and night banks swapped under the AE/AWB pause; module build only), held back because the bank swap must be reworked first (review: memcpy over live banks); T40/T41 (claude/switchbin-t4x, 2026-10-10): control 0x80000a5 from the vendor T40 1.3.1 and T41 1.2.6 libimp disassembly (ioctl 0xc0105436 / 0xc0105435, payload IMPISPBinAttr, 68 / 132 bytes, vendor check order and return codes); T40 device-tested on the Eufy (sc830ai, vendor kernel driver): switch to a copy of the bin, back, and enable 0 return 0, the stream keeps decoding cleanly, picture unchanged; T41 library host-tested only, the T41 kernel side is in open-tx-isp claude/switchbin-t41 (module build and host test, no device test yet; the vendor T41 module was not tested)
86. `ISP_Tuning_WaitFrame`: T10+T20+T21: waits for the frame end (agg-25; T20 ms, not jiffies); T23: stock 24-byte block, driver routed (agg-25), no device test
14. `ISP_WDR_ENABLE`: T41: driver control, device-tested (claude/t41-driver-gaps, in agg-35)
15. `ISP_WDR_ENABLE_GET`: T41: driver control, device-tested (claude/t41-driver-gaps, in agg-35)
87. `ISP_WDR_ENABLE_Get`: audit: userspace implementation
89. `ISP_Tuning_Get/SetISPHVflip`: T10/T20 3.12.0: apitest PASS on T10 and T20 (set both flips, read back, restore). The vendor sends one module-control word (0x80000e2 through the 0xc00c56c6 ioctl), which the open T20 driver does not serve; OpenIMP uses the ISPHflip/ISPVflip pair (V4L2 flip controls) instead; T30 1.0.5 (2026-10-10): the same pair of flip controls as T20 (the vendor sends the module-control word 0x80000e2, which the open drivers do not serve); host test with a fake tuning ioctl, no T30 device
90. `ISP_Tuning_Get/SetWDRAttr`: T10/T20 3.12.0: V4L2 control 0x98e912 with the vendor's mode cache (an unchanged mode returns 0 without the driver). apitest PASS on T10 and T20 for Get and Set of the current mode; switching WDR on was not run: the driver refuses (-EPERM) without a WDR frame buffer, as the stock module; T30 1.0.5: V4L2 control 0x8000169 with the vendor's mode cache (an unchanged mode returns 0 without the driver; a failed Get returns the cached mode with the error); host test with a fake ioctl; whether the open T30 driver serves the control is not checked
91. `ISP_Tuning_SetMeshShadingScale`: T10/T20 (class c): the vendor writes bits 2..4 of an ISP lens-shading register through a userspace mapping (3.12.0 0x62930); there is no driver control for it, wishlist: a tuning control in the T10/T20 driver

</details>

### AE / AWB / AF

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|
| `ISP_SetAeAlgoFunc` | n.a. | n.a. | n.a. | aud | n.a. | aud | miss | miss | – |
| `ISP_SetAwbAlgoFunc` | n.a. | n.a. | n.a. | aud | n.a. | aud | miss | miss | – |
| `ISP_Tuning_Get/SetAeConvergeStep` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetAeExpList` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | miss | – |
| `ISP_Tuning_Get/SetAeSpeed` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `ISP_Tuning_Get/SetAwbConvergeStep` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetAwbCtTrendOffset` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetFaceAe` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | miss | – |
| `ISP_Tuning_Get/SetFaceAeWeiget` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetFaceAwb` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `ISP_Tuning_GetAEEvList` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAEFlickerFlag` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAFMetricesInfo` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | miss | – |
| `ISP_Tuning_GetAeAtList` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAeBv` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAeEvList` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAfStatistics` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | miss | – |
| `ISP_Tuning_GetFaceAeLuma` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_SetWB_ALGO` [3] | n.a. | n.a. | n.a. | err | n.a. | aud | n.a. | n.a. | – |

Notes:

2. `ISP_Tuning_Get/SetAfWeight`: T23: AF statistics chain from the stock module, off by default (source_af=0); no device test; T31: reconstructed AF chain; metrics/zone/weight/hist verified on cam-A 2026-10-05, Get->Set roundtrip 0; T41: driver control, device-tested (claude/t41-driver-gaps, in agg-35); T23: AF statistics on by default since open-tx-isp agg-28 (source_af=1 as stock); apitest PASS on a T23 camera, no measurable CPU cost
3. `ISP_Tuning_SetWB_ALGO`: T23: 0x800000c accepted like stock (open-tx-isp claude/gaps-t23-t31-drv): mode 0..2 stored in the AWB light-source flag, other modes logged, always returns 0; the four AWB threshold registers are not rewritten, because the AWB of the open driver runs on the wide thresholds that stock mode 1 writes and has no IQ-threshold window to switch to, so the call has no effect (class d); host build only, device test pending; T31: the driver has tisp_s_awb_algo (connected per static audit); Not in agg-36: the open-tx-isp branch claude/gaps-t23-t31-drv is held back until its T23 SwitchBin is reworked (review: memcpy over live banks), so the agg-36 driver still behaves as before (cell unchanged).

<details><summary>All 60 rows of this area (19 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|
| `ISP_SetAeAlgoFunc` | n.a. | n.a. | n.a. | aud | n.a. | aud | miss | miss | – |
| `ISP_SetAwbAlgoFunc` | n.a. | n.a. | n.a. | aud | n.a. | aud | miss | miss | – |
| `ISP_Tuning_AE_Get/SetROI` [4] | aud | dev | dev | dev | ? | dev | n.a. | n.a. | see note |
| `ISP_Tuning_AwbSync` [1] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_Awb_Get/SetCwfShift` [5] | host | host | n.a. | n.a. | host | n.a. | n.a. | n.a. | – |
| ► `ISP_Tuning_Awb_Get/SetRgbCoefft` [6] | aud | dev | dev | dev | ? | dev | host | host | r† |
| `ISP_Tuning_Get/SetAeAttr` [7] | n.a. | n.a. | n.a. | dev | n.a. | dev | n.a. | n.a. | p† r† |
| `ISP_Tuning_Get/SetAeComp` [8] | aud | aud | n.a. | aud | ? | aud | n.a. | n.a. | p† r† t |
| `ISP_Tuning_Get/SetAeConvergeStep` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetAeExpList` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | miss | – |
| `ISP_Tuning_Get/SetAeExprInfo` [9] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | aud | – |
| `ISP_Tuning_Get/SetAeHist` [10] | aud | dev | dev | dev | ? | dev | n.a. | n.a. | p† r† |
| `ISP_Tuning_Get/SetAeMin` [11] | n.a. | n.a. | dev | dev | n.a. | dev | n.a. | n.a. | see note |
| `ISP_Tuning_Get/SetAeScenceAttr` [12] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | dev | aud | – |
| `ISP_Tuning_Get/SetAeSpeed` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `ISP_Tuning_Get/SetAeStrategy` [13] | host | dev | dev | n.a. | host | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_Get/SetAeTargetList` [14] | n.a. | n.a. | n.a. | dev | n.a. | dev | n.a. | n.a. | r† |
| `ISP_Tuning_Get/SetAeWeight` [15] | aud | dev | dev | dev | ? | dev | host | host | p† r† |
| `ISP_Tuning_Get/SetAfHist` [16] | aud | dev | dev | dev | ? | dev | n.a. | n.a. | r† |
| ► `ISP_Tuning_Get/SetAfWeight` [2] | n.a. | n.a. | dev | dev | n.a. | dev | host | dev | r† |
| `ISP_Tuning_Get/SetAwbAttr` [17] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | ? | aud | – |
| `ISP_Tuning_Get/SetAwbClust` [18] | n.a. | n.a. | n.a. | dev | n.a. | dev | n.a. | n.a. | r† |
| `ISP_Tuning_Get/SetAwbConvergeStep` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetAwbCtTrend` [19] | n.a. | n.a. | n.a. | dev | n.a. | dev | n.a. | n.a. | r† |
| `ISP_Tuning_Get/SetAwbCtTrendOffset` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetAwbHist` [20] | aud | dev | dev | no-op | ? | no-op | n.a. | n.a. | p† r† |
| `ISP_Tuning_Get/SetAwbWeight` [21] | aud | dev | dev | no-op | ? | no-op | ? | aud | p† r† |
| `ISP_Tuning_Get/SetAwbZoneWeight` [22] | n.a. | n.a. | n.a. | host | n.a. | n.a. | n.a. | n.a. | – |
| `ISP_Tuning_Get/SetFaceAe` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | miss | – |
| `ISP_Tuning_Get/SetFaceAeWeiget` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetFaceAwb` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `ISP_Tuning_Get/SetWB` [23] | aud | dev | dev | dev | ? | dev | n.a. | n.a. | p† r† t |
| `ISP_Tuning_GetAEEvList` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAEFlickerFlag` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAE_IT_MAX` [24] | n.a. | n.a. | n.a. | dev | n.a. | dev | n.a. | n.a. | p† |
| `ISP_Tuning_GetAFMetrices` [25] | n.a. | n.a. | dev | dev | ? | dev | n.a. | n.a. | r† |
| `ISP_Tuning_GetAFMetricesInfo` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | miss | – |
| `ISP_Tuning_GetAWBCt` [26] | n.a. | n.a. | n.a. | dev | n.a. | dev | n.a. | n.a. | see note |
| `ISP_Tuning_GetAeAtList` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAeBv` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAeEvList` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetAeHist_Origin` [27] | n.a. | n.a. | n.a. | host | n.a. | aud | n.a. | n.a. | see note |
| `ISP_Tuning_GetAeLuma` [28] | n.a. | n.a. | dev | dev | n.a. | dev | n.a. | n.a. | p† r† t |
| `ISP_Tuning_GetAeState` [29] | n.a. | n.a. | n.a. | dev | n.a. | dev | n.a. | n.a. | r† |
| `ISP_Tuning_GetAeStatistics` [30] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | ? | aud | – |
| `ISP_Tuning_GetAeZone` [31] | no-op | no-op | dev | dev | n.a. | dev | n.a. | n.a. | p† r† |
| `ISP_Tuning_GetAfStatistics` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | miss | – |
| `ISP_Tuning_GetAfZone` [32] | n.a. | n.a. | n.a. | dev | n.a. | dev | n.a. | n.a. | r† |
| `ISP_Tuning_GetAwbGlobalStatistics` [33] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | ? | aud | – |
| `ISP_Tuning_GetAwbStatistics` [34] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | ? | aud | – |
| `ISP_Tuning_GetAwbZone` [35] | host | dev | n.a. | dev | n.a. | dev | n.a. | n.a. | see note |
| `ISP_Tuning_GetExpr` [36] | aud | dev | dev | dev | ? | dev | n.a. | n.a. | r† t |
| `ISP_Tuning_GetFaceAeLuma` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetWB_GOL_Statis` [37] | n.a. | n.a. | dev | dev | n.a. | dev | n.a. | n.a. | r† |
| `ISP_Tuning_GetWB_Statis` [38] | aud | dev | dev | dev | ? | dev | n.a. | n.a. | r† |
| `ISP_Tuning_SetAeFreeze` [39] | n.a. | n.a. | n.a. | host | n.a. | aud | n.a. | n.a. | r† |
| `ISP_Tuning_SetAe_IT_MAX` [40] | n.a. | n.a. | n.a. | aud | n.a. | aud | n.a. | n.a. | p† t |
| `ISP_Tuning_SetAwbCt` [41] | n.a. | n.a. | n.a. | host | n.a. | aud | n.a. | n.a. | r† |
| `ISP_Tuning_SetExpr` [42] | aud | aud | aud | host | ? | aud | n.a. | n.a. | T23/T31: r† |
| `ISP_Tuning_SetWB_ALGO` [3] | n.a. | n.a. | n.a. | err | n.a. | aud | n.a. | n.a. | – |

Notes:

4. `ISP_Tuning_AE_Get/SetROI`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified; used by: T10/T20: prudynt†; T21/T23/T31: prudynt†, raptor†
1. `ISP_Tuning_AwbSync`: T23: the vendor libimp 1.3.0 does not export it (only IMP_ISP_MultiCamera_Tuning_AwbSync, which sends control 0x8000011 for sensor 1 only); the stock kernel module rejects 0x8000011 in s_ctrl and g_ctrl (-1, checked in the emulator on the stock module), so the open driver's -EINVAL is the vendor behaviour (class a)
5. `ISP_Tuning_Awb_Get/SetCwfShift`: T10+T20: newly exported (agg-25); T30 1.0.5: tuning control 0x8000001 (rgain << 16 | bgain), as T20; host test with a fake ioctl
6. `ISP_Tuning_Awb_Get/SetRgbCoefft`: T23: stock handlers routed (open-tx-isp agg-25), no device test; T41: driver claude/t41-connect (agg-25); host tests 58/58
7. `ISP_Tuning_Get/SetAeAttr`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified
8. `ISP_Tuning_Get/SetAeComp`: audit: reaches the driver/kernel
9. `ISP_Tuning_Get/SetAeExprInfo`: audit: reaches the driver/kernel
10. `ISP_Tuning_Get/SetAeHist`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified
11. `ISP_Tuning_Get/SetAeMin`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified; used by: T23: prudynt; T31: prudynt†, raptor†
12. `ISP_Tuning_Get/SetAeScenceAttr`: audit: reaches the driver/kernel; T40: Get and Set device-tested on cam-K 2026-10-10 (OpenIMP vs vendor library)
13. `ISP_Tuning_Get/SetAeStrategy`: T10+T20+T21: newly exported (agg-25); T10 shares the T20 build; T30 1.0.5: tuning control 0x8000022, as T20/T21; host test with a fake ioctl
14. `ISP_Tuning_Get/SetAeTargetList`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified
15. `ISP_Tuning_Get/SetAeWeight`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified; T41: driver claude/t41-connect (agg-25); host tests 58/58
16. `ISP_Tuning_Get/SetAfHist`: T23: AF statistics chain from the stock module, off by default (source_af=0); no device test; T31: reconstructed AF chain; metrics/zone/weight/hist verified on cam-A 2026-10-05, Get->Set roundtrip 0; T23: AF statistics on by default since open-tx-isp agg-28 (source_af=1 as stock); apitest PASS on a T23 camera, no measurable CPU cost
2. `ISP_Tuning_Get/SetAfWeight`: T23: AF statistics chain from the stock module, off by default (source_af=0); no device test; T31: reconstructed AF chain; metrics/zone/weight/hist verified on cam-A 2026-10-05, Get->Set roundtrip 0; T41: driver control, device-tested (claude/t41-driver-gaps, in agg-35); T23: AF statistics on by default since open-tx-isp agg-28 (source_af=1 as stock); apitest PASS on a T23 camera, no measurable CPU cost
17. `ISP_Tuning_Get/SetAwbAttr`: audit: reaches the driver/kernel
18. `ISP_Tuning_Get/SetAwbClust`: T23: the open AWB reads the cluster/trend objects like the stock module (open-tx-isp agg-28, emulator-identical); apitest roundtrip PASS, the runtime effect is host/emulator-tested only
19. `ISP_Tuning_Get/SetAwbCtTrend`: T23: the open AWB reads the cluster/trend objects like the stock module (open-tx-isp agg-28, emulator-identical); apitest roundtrip PASS, the runtime effect is host/emulator-tested only
20. `ISP_Tuning_Get/SetAwbHist`: T23+T31: stock driver also no-op (OEM-same)
21. `ISP_Tuning_Get/SetAwbWeight`: T23+T31: stock driver also no-op (OEM-same)
22. `ISP_Tuning_Get/SetAwbZoneWeight`: T23: stock handlers routed (open-tx-isp agg-25), no device test
23. `ISP_Tuning_Get/SetWB`: audit: reaches the driver/kernel
24. `ISP_Tuning_GetAE_IT_MAX`: audit: reaches the driver/kernel
25. `ISP_Tuning_GetAFMetrices`: T23: AF statistics chain from the stock module, off by default (source_af=0); no device test; T31: reconstructed AF chain; metrics/zone/weight/hist verified on cam-A 2026-10-05, Get->Set roundtrip 0; T23: AF statistics on by default since open-tx-isp agg-28 (source_af=1 as stock); apitest PASS on a T23 camera, no measurable CPU cost
26. `ISP_Tuning_GetAWBCt`: T23: stock handlers routed (open-tx-isp agg-25), no device test; used by: T23: raptor; T31: prudynt†, raptor†
27. `ISP_Tuning_GetAeHist_Origin`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified; used by: T23: prudynt; T31: prudynt†, raptor†
28. `ISP_Tuning_GetAeLuma`: audit: reaches the driver/kernel
29. `ISP_Tuning_GetAeState`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified
30. `ISP_Tuning_GetAeStatistics`: audit: reaches the driver/kernel
31. `ISP_Tuning_GetAeZone`: T10+T20: the stock T10/T20 kernel module does not serve cid 0x800002f; -1 as on the vendor stack (apitest N/A)
32. `ISP_Tuning_GetAfZone`: T23: AF statistics chain from the stock module, off by default (source_af=0); no device test; T31: reconstructed AF chain; metrics/zone/weight/hist verified on cam-A 2026-10-05, Get->Set roundtrip 0; T23: AF statistics on by default since open-tx-isp agg-28 (source_af=1 as stock); apitest PASS on a T23 camera, no measurable CPU cost
33. `ISP_Tuning_GetAwbGlobalStatistics`: audit: reaches the driver/kernel
34. `ISP_Tuning_GetAwbStatistics`: audit: reaches the driver/kernel
35. `ISP_Tuning_GetAwbZone`: T10+T20: vendor T20 ids/ABI (agg-25); T10 shares the build; T23: stock handlers routed (agg-25), no device test; used by: T10/T20: prudynt†; T23/T31: prudynt†, raptor†
36. `ISP_Tuning_GetExpr`: audit: reaches the driver/kernel
37. `ISP_Tuning_GetWB_GOL_Statis`: audit: reaches the driver/kernel
38. `ISP_Tuning_GetWB_Statis`: audit: reaches the driver/kernel
39. `ISP_Tuning_SetAeFreeze`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified
40. `ISP_Tuning_SetAe_IT_MAX`: audit: reaches the driver/kernel
41. `ISP_Tuning_SetAwbCt`: T23: stock handlers routed (open-tx-isp agg-25), no device test
42. `ISP_Tuning_SetExpr`: T23: stock handlers routed (open-tx-isp agg-25), no device test; _Sec/MultiCamera_ variants unverified
3. `ISP_Tuning_SetWB_ALGO`: T23: 0x800000c accepted like stock (open-tx-isp claude/gaps-t23-t31-drv): mode 0..2 stored in the AWB light-source flag, other modes logged, always returns 0; the four AWB threshold registers are not rewritten, because the AWB of the open driver runs on the wide thresholds that stock mode 1 writes and has no IQ-threshold window to switch to, so the call has no effect (class d); host build only, device test pending; T31: the driver has tisp_s_awb_algo (connected per static audit); Not in agg-36: the open-tx-isp branch claude/gaps-t23-t31-drv is held back until its T23 SwitchBin is reworked (review: memcpy over live banks), so the agg-36 driver still behaves as before (cell unchanged).

</details>

### Encoder (and decoder)

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|
| `Decoder_*` (8 functions) [1] | miss | miss | miss | err | miss | n.a. | miss | miss | – |
| `Encoder_Get/SetChangeRef` [65] | cache | cache | cache | dev | cache | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnCrop` [20] | miss | miss | miss | dev | miss | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnDemask` [66] | cache | cache | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| ► `Encoder_Get/SetChnDenoise` [2] | cache | cache | dev | dev | ? | n.a. | n.a. | n.a. | T21: r† |
| `Encoder_Get/SetChnH264Demask` [67] | miss | miss | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnH264Denoise` [68] | miss | miss | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnH264FrmUsedMode` [69] | miss | miss | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnHSkip` [70] | cache | cache | dev | dev | cache | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnRcAttr` [71] | miss | miss | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnRoiAttr` [63] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | ? | cache | – |
| `Encoder_Get/SetChnSeiAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `Encoder_Get/SetFisheyeEnableStatus` [4] | cache | cache | cache | cache | cache | cache | cache | miss | – |
| `Encoder_Get/SetH264TransCfg` [5] | cache | dev | dev | dev | ? | n.a. | n.a. | n.a. | T21: r† |
| `Encoder_Get/SetMbRC` [7] | no-op | dev | dev | dev | ? | n.a. | n.a. | n.a. | T21: r† |
| `Encoder_Get/Setframelossthd` [8] | n.a. | n.a. | n.a. | cache | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_InputJpege` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | miss | T23: t |
| `Encoder_InputJpege_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetAvpuBsShare` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetAvpuBsSize` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetAvpuJpegQp` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetChnHSkipBlackEnhance` [73] | cache | cache | cache | dev | cache | n.a. | n.a. | n.a. | – |
| `Encoder_SetChnMapRoi` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | cache [63] | – |
| ► `Encoder_SetChnMaxPictureSize` [9] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | cache | T41: r† |
| `Encoder_SetFrameRelease` [10] | n.a. | n.a. | n.a. | n.a. | n.a. | no-op | miss | miss | – |
| `Encoder_SetIvpuBsSize` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetMultiSectionMode` [11] | n.a. | n.a. | n.a. | cache | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_SetbufshareChn` [12] | n.a. | n.a. | n.a. | n.a. | n.a. | host | dev | host | p† r† |
| `Encoder_VbmAlloc` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | T23: t |
| `Encoder_VbmAlloc_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_VbmFree` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | T23: t |
| `Encoder_VbmFree_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_VbmP2V` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | – |
| `Encoder_VbmV2P` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | T23: t |
| `Encoder_YuvEncode` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | T23: t |
| `Encoder_YuvEncode_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_YuvExit` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | T23: t |
| `Encoder_YuvExit_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_YuvInit` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | T23: t |
| `Encoder_YuvInit_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |

Notes:

1. `Decoder_*` (8 functions): functions: CreateChn, DestroyChn, GetFrame, PollingFrame, ReleaseFrame, SendStreamTimeout, StartRecvPic, StopRecvPic; T31: none of the 28 vendor libimp builds exports any IMP_Decoder_* (only the ADEC register calls), the headers of 1.1.5+ declare them; T10/T20/T21 (class d, 2026-10-10): the vendor libimp exports all 8 calls (T21 1.0.33, T10/T20 3.12.0; a decoder channel thread around DecoderInit/DecoderExit); the open stack has no decoder (the Helix wrapper of these SoCs is encode-only), and no streamer uses them: not worth building T23 (agg-37): the decoder ran in the OEM Helix worker; that bridge is off by default since agg-37 (`OPENIMP_T23_HELIX_BRIDGE=0`, docs/SIZE.md), so `IMP_Decoder_CreateChn` fails (err); `OPENIMP_T23_HELIX_BRIDGE=1` builds it again.
2. `Encoder_Get/SetChnDenoise`: T21: done as the vendor: libimp 1.0.33 clears the denoise type at init (channel_i264e_encoder_init picks encoder mode 4 from get_cpu_id() 11..14, i264e_validate_parameters keeps denoise only for mode 1), so on the vendor T21 dnType reads back 0 and the encoder runs one Helix job per picture; dnType >= 3 with enable set at CreateChn is -1; IQp/PQp are kept. OpenIMP T21 does the same (2026-10-10: PC420 vendor stack vs open stack, readback equal, size and fps equal, ffmpeg decode clean). T10/T20 (vendor 3.12.0 has no such gate): type, IQp and PQp stored and read back as the vendor (2026-10-10: the T20 build also defines PLATFORM_T21 and read dnType back as 0 like T21; fixed, host test `p2_t1x_test_t20`; apitest roundtrip PASS on T20), not applied; the vendor encodes every frame twice with attrDenoise.enable set at CreateChn and dnType 1/2, a first pass at slice QP dnIQp (I) / dnPQp (P; type 2 forces slice type I) before the normal pass (i264e_reconfig_dn_set, i264e_slice_write). Not built: the pass-1 job needs a trace on a T20 camera, stability first
4. `Encoder_Get/SetFisheyeEnableStatus`: T23+T31: kept for getter only (documented in source); class d: the flag only has an effect in the vendor's closed fisheye/i264e path, which OpenIMP does not contain (T31: closed IVS fisheye module; T23: i264e creation option of the OEM session); T10/T20/T21: a flag in the channel record, settable only on an idle channel (-1 once created), exactly as the vendor (disassembly T21 0x45454, T20 0x4776c); apitest PASS on T10, T20, T21; class d like T23/T31; T30 1.0.5: flag kept, settable before CreateChn only, as T21 (the closed fisheye motion search of the vendor encoder is not in the Helix path); host test; vendor check 2026-10-10 (libimp T23 1.3.0 and T31 1.1.6 disassembly): T31 1.1.6 sets the flag in the channel record only while the channel does not exist (channel below 9, else -1), as OpenIMP does; T23 1.3.0 likewise; the flag is read later by the closed encoder code
5. `Encoder_Get/SetH264TransCfg`: T10: no chroma-offset register, stays 0; T20: chroma QP offset via PPS + reg 0x40120 (claude/t1x-roi), verified on a camera, no colour shift; T21: chroma QP offset, PPS rewrite; not device-tested
7. `Encoder_Get/SetMbRC`: T10 (2026-10-10, from the disassembly notes in docs/T20_RC.md, not device-tested): the vendor library is the T20 build, but its T10 rate controller has no macroblock part, so the call is stored and read back and changes nothing, as in OpenIMP (src/rc_t10; matched, no-op); T20: switches the macroblock QP table (agg-25)
8. `Encoder_Get/Setframelossthd`: T23: kept for getter only (documented in source); class d: the vendor re-encodes oversized frames at a higher QP inside its closed encoder; the Helix session has no such hook (would need a re-encode path in the encoder, low value); vendor check 2026-10-10 (libimp T23 1.3.0 and T31 1.1.6 disassembly): T23 1.3.0 validates and stores the threshold for the closed encoder; T31 1.1.6 exports no frame-loss function (n.a.)
9. `Encoder_SetChnMaxPictureSize`: T23: as the OEM stores it, re-encode on overshoot (claude/t23-enc-rest, not in agg-25); T41: written into rcAttr copy, codec not updated (T23: loss threshold kept only)
10. `Encoder_SetFrameRelease`: T31: vendor stores num/den and a helper hands the source frame back early; OpenIMP returns every frame as soon as the AVPU has consumed it, so the ratio has nothing to change (class a: no visible effect, matched)
11. `Encoder_SetMultiSectionMode`: T23: kept for getter only (documented in source); class d: slice/section split of the vendor i264e encoder, the Helix session cannot be given it; vendor check 2026-10-10 (libimp T23 1.3.0 and T31 1.1.6 disassembly): T23 1.3.0 range-checks the arguments and stores them in the global block the closed i264e reads; T31 1.1.6 exports no multi-section function (n.a.)
12. `Encoder_SetbufshareChn`: vendor T31 1.1.6 / T40 1.3.1 / T41 1.2.6 (disassembled): stores the share channel in the channel record (kept over CreateChn, set to -1 by DestroyChn; a negative value is valid and means no sharing) and CreateChn reads it for a JPEG channel only: the JPEG channel borrows the video channel's stream manager and stream contexts and skips building its own pool of stream buffers (CreateChn fails when that video channel does not exist yet; T41 also refuses the MMA option). OpenIMP: same value handling, argument checks and return codes (-1 on T31/T40, 0x80040001 on T41, both channel numbers below the channel limit); CreateChn only logs a warning where the vendor would fail. There is nothing to share: OpenIMP JPEG channels own no stream buffer pool (JPEG is software on T40/T41, T31 uses one hardware-JPEG stream buffer for all JPEG channels), so sharing would save no rmem; measured on a T40 camera (video 640x360 plus a JPEG channel in the same group, with and without the call): JPEG channel cost about 24-72 kB of free memory either way, snapshots valid, H.264 decodes without errors in software and VA-API decoders. Host tests for T31/T40/T41, device test T40 only
65. `Encoder_Get/SetChangeRef`: T10/T20: stored and read back with the vendor return codes (SetChangeRef -1 on an idle channel, GetChangeRef -1 on an idle channel; apitest PASS on T10, T20); T21 (2026-10-10, host test): SetChangeRef -1 on an idle channel; GetChangeRef answers 0 on an idle channel and, as the vendor (T21 1.0.33 and T30 1.0.5, same code), logs "unsupport to change ref channel" and returns -1 on every created channel without writing the value (it used to return the stored value); the vendor passes it to the i264e reference code (param 12), which only the HSkip reference modes read; the Helix path codes no HSkip structure (class c). Disassembly: T21 1.0.33 0x48bd4/0x48d90, T20 3.12.0 0x4b05c/0x4b220; T30 1.0.5 (same vendor code as T21): SetChangeRef stores the flag of a created channel (-1 for an idle one); GetChangeRef is 0 for an idle channel and -1 for a created one (the vendor logs "unsupport to change ref channel"); host test; T30 1.0.5 (same vendor code as T21): SetChangeRef stores the flag of a created channel (-1 for an idle one); GetChangeRef is 0 for an idle channel and -1 for a created one (the vendor logs "unsupport to change ref channel"); host test
66. `Encoder_Get/SetChnDemask`: T10/T20 3.12.0 (0x49160/0x492d8): three words stored in rcAttr.attrDemask without a created check, as the vendor (apitest PASS on T10, T20); the vendor also pokes an i264e flag word, the Helix path has no demask stage (class c)
67. `Encoder_Get/SetChnH264Demask`: T10/T20 (class d, 2026-10-10): only the 3.9.0 libimp exports the H264-named variants; 3.12.0 and the header of OpenIMP use ChnDemask/ChnDenoise/ChnFrmUsedMode, and the 3.9.0 IMPEncoderRcAttr/CHNAttr layout differs from the one OpenIMP implements, so these calls would be dead code (an app built for 3.9.0 breaks on the structures first)
68. `Encoder_Get/SetChnH264Denoise`: T10/T20 (class d): 3.9.0-only name, see Encoder_Get/SetChnH264Demask
69. `Encoder_Get/SetChnH264FrmUsedMode`: T10/T20 (class d): 3.9.0-only name, see Encoder_Get/SetChnH264Demask
70. `Encoder_Get/SetChnHSkip`: T10/T20 3.12.0 (0x49dc8/0x4a078): needs a created channel, skipType up to maxHSkipType, six words stored (apitest PASS on T10, T20); unlike T21 the IDR period is not applied (the OEM pushes it to i264e param 9; class c); T30 1.0.5: the six words are stored, also for an idle channel, which reads back as zeros as in the vendor (up to maxHSkipType, 0 until CreateChn); unlike T21 the IDR period is not handed to the encoder on T30 (the CreateChn path does not do it either), so it stays a cache until a T30 device run; host test; T21 (2026-10-10, host test; T21 1.0.33 0x478f0/0x47ba0): an idle (not yet created) channel is accepted as the vendor does, SetChnHSkip stores the six words without the maxHSkipType check and without reaching the encoder, GetChnHSkip answers all zero with 0 (both used to be -1); T21 (2026-10-10, host test; T21 1.0.33 0x478f0/0x47ba0): an idle (not yet created) channel is accepted as the vendor does, SetChnHSkip stores the six words without the maxHSkipType check and without reaching the encoder, GetChnHSkip answers all zero with 0 (both used to be -1)
71. `Encoder_Get/SetChnRcAttr`: T10/T20 (class d): 3.9.0-only call (IMPEncoderRcAttr of that SDK generation), see Encoder_Get/SetChnH264Demask
73. `Encoder_SetChnHSkipBlackEnhance`: T10/T20/T21: returns 0, stores the flag of a created channel (the vendor pushes it to i264e param 10, read only by the HSkip code; class c); apitest PASS on T10, T20, T21; T30 1.0.5: the flag is kept for a created channel, 0 for an idle one (as T21); host test

<details><summary>All 92 rows of this area (38 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|
| `Decoder_*` (8 functions) [1] | miss | miss | miss | err | miss | n.a. | miss | miss | – |
| `Encoder_CreateChn` [13] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `Encoder_CreateGroup` [14] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `Encoder_DestroyChn` [15] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `Encoder_DestroyGroup` [16] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `Encoder_FlushStream` [17] | host | dev | dev | dev | ? | dev | ? | host | p† r† |
| `Encoder_Get/SetChangeRef` [65] | cache | cache | cache | dev | cache | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnAttrRcMode` [18] | aud | dev | dev | dev | ? | dev | ? | aud | r† t |
| `Encoder_Get/SetChnColor2Grey` [19] | host | dev | dev | dev | ? | n.a. | n.a. | n.a. | T21: r† |
| `Encoder_Get/SetChnCrop` [20] | miss | miss | miss | dev | miss | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnDemask` [66] | cache | cache | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| ► `Encoder_Get/SetChnDenoise` [2] | cache | cache | dev | dev | ? | n.a. | n.a. | n.a. | T21: r† |
| `Encoder_Get/SetChnFrmRate` [21] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† |
| `Encoder_Get/SetChnFrmUsedMode` [3] | dev | dev | dev | dev | host | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnGopAttr` [22] | n.a. | n.a. | n.a. | n.a. | n.a. | dev | ? | aud | – |
| `Encoder_Get/SetChnH264Demask` [67] | miss | miss | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnH264Denoise` [68] | miss | miss | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnH264FrmUsedMode` [69] | miss | miss | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnHSkip` [70] | cache | cache | dev | dev | cache | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnROI` [23] | dev | dev | dev | dev | ? | n.a. | n.a. | n.a. | T21: r† |
| `Encoder_Get/SetChnRcAttr` [71] | miss | miss | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetChnRoiAttr` [63] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | ? | cache | – |
| `Encoder_Get/SetChnSeiAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `Encoder_Get/SetFisheyeEnableStatus` [4] | cache | cache | cache | cache | cache | cache | cache | miss | – |
| `Encoder_Get/SetGDRCfg` [24] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_Get/SetH264TransCfg` [5] | cache | dev | dev | dev | ? | n.a. | n.a. | n.a. | T21: r† |
| ► `Encoder_Get/SetH265TransCfg` [6] | n.a. | n.a. | no-op | no-op | ? | n.a. | n.a. | n.a. | T21: r† |
| `Encoder_Get/SetJpegeQl` [25] | host | dev | dev | dev | ? | n.a. | n.a. | host | see note |
| `Encoder_Get/SetMaxStreamCnt` [26] | aud | dev | dev | dev | ? | dev | ? | aud | r† |
| `Encoder_Get/SetMbRC` [7] | no-op | dev | dev | dev | ? | n.a. | n.a. | n.a. | T21: r† |
| `Encoder_Get/SetPool` [27] | n.a. | n.a. | n.a. | dev | n.a. | dev | ? | aud | r† |
| `Encoder_Get/SetQpgMode` [28] | n.a. | n.a. | no-op | dev | ? | n.a. | n.a. | n.a. | T21: r† |
| `Encoder_Get/SetStreamBufSize` [29] | n.a. | n.a. | n.a. | n.a. | n.a. | dev | ? | aud | – |
| `Encoder_Get/SetSuperFrameCfg` [30] | host | dev | host | dev | ? | n.a. | n.a. | n.a. | T21: r† |
| `Encoder_Get/Setframelossthd` [8] | n.a. | n.a. | n.a. | cache | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_GetChnAttr` [31] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† |
| `Encoder_GetChnAveBitrate` [32] | n.a. | n.a. | n.a. | n.a. | n.a. | dev | n.a. | n.a. | t |
| `Encoder_GetChnEncType` [33] | n.a. | n.a. | dev | dev | ? | dev | ? | aud | r† |
| `Encoder_GetChnEvalInfo` [34] | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | n.a. | r† |
| `Encoder_GetChnMaxPictureSize` [35] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_GetFd` [36] | n.a. | n.a. | dev | dev | ? | dev | ? | aud | r† |
| `Encoder_GetGOPSize` [72] | dev | dev | dev | dev | host | n.a. | n.a. | n.a. | – |
| `Encoder_GetStream` [37] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `Encoder_InputJpege` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | miss | T23: t |
| `Encoder_InputJpege_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_InsertUserData` [38] | aud | dev | dev | dev | ? | n.a. | n.a. | n.a. | – |
| `Encoder_PollingModuleStream` [39] | n.a. | n.a. | dev | aud | n.a. | dev | ? | aud | – |
| `Encoder_PollingStream` [40] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `Encoder_Query` [41] | aud | dev | dev | dev | ? | dev | ? | aud | r† t |
| `Encoder_RegisterChn` [42] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `Encoder_ReleaseStream` [43] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `Encoder_RequestGDR` [44] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_RequestIDR` [45] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `Encoder_SetAvpuBsShare` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetAvpuBsSize` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetAvpuJpegQp` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetChnBitRate` [46] | n.a. | n.a. | n.a. | n.a. | n.a. | dev | ? | aud | t |
| `Encoder_SetChnEntropyMode` [47] | n.a. | n.a. | n.a. | n.a. | n.a. | dev | n.a. | n.a. | – |
| `Encoder_SetChnGopLength` [48] | n.a. | n.a. | n.a. | n.a. | n.a. | dev | ? | aud | – |
| `Encoder_SetChnHSkipBlackEnhance` [73] | cache | cache | cache | dev | cache | n.a. | n.a. | n.a. | – |
| `Encoder_SetChnInitQP` [49] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_SetChnMapRoi` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | cache [63] | – |
| ► `Encoder_SetChnMaxPictureSize` [9] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | cache | T41: r† |
| `Encoder_SetChnQp` [50] | n.a. | n.a. | n.a. | n.a. | n.a. | dev | n.a. | n.a. | – |
| `Encoder_SetChnQpBounds` [51] | n.a. | n.a. | n.a. | n.a. | n.a. | dev | ? | aud | t |
| `Encoder_SetChnQpBoundsPerFrame` [52] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | aud | – |
| `Encoder_SetChnQpIPDelta` [53] | n.a. | n.a. | n.a. | n.a. | n.a. | dev | n.a. | n.a. | t |
| `Encoder_SetChnResizeMode` [54] | n.a. | n.a. | n.a. | n.a. | ? | dev | ? | aud | – |
| `Encoder_SetDefaultParam` [55] | n.a. | n.a. | n.a. | n.a. | n.a. | dev | ? | aud | t |
| `Encoder_SetFrameRelease` [10] | n.a. | n.a. | n.a. | n.a. | n.a. | no-op | miss | miss | – |
| `Encoder_SetGOPSize` [56] | aud | aud | aud | dev | ? | n.a. | n.a. | n.a. | r† |
| `Encoder_SetIvpuBsSize` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_SetMultiSectionMode` [11] | n.a. | n.a. | n.a. | cache | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_SetbufshareChn` [12] | n.a. | n.a. | n.a. | n.a. | n.a. | host | dev | host | p† r† |
| `Encoder_StartRecvPic` [57] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `Encoder_StopRecvPic` [58] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `Encoder_UnRegisterChn` [59] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `Encoder_VbmAlloc` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | T23: t |
| `Encoder_VbmAlloc_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_VbmFree` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | T23: t |
| `Encoder_VbmFree_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_VbmP2V` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | – |
| `Encoder_VbmV2P` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | T23: t |
| `Encoder_YuvEncode` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | T23: t |
| `Encoder_YuvEncode_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_YuvExit` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | T23: t |
| `Encoder_YuvExit_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_YuvGetCrop` [60] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | n.a. | – |
| `Encoder_YuvInit` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | T23: t |
| `Encoder_YuvInit_Ex` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `Encoder_YuvRequestIDR` [61] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | n.a. | t |
| `Encoder_YuvSetCrop` [62] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | n.a. | – |

Notes:

1. `Decoder_*` (8 functions): functions: CreateChn, DestroyChn, GetFrame, PollingFrame, ReleaseFrame, SendStreamTimeout, StartRecvPic, StopRecvPic; T31: none of the 28 vendor libimp builds exports any IMP_Decoder_* (only the ADEC register calls), the headers of 1.1.5+ declare them; T10/T20/T21 (class d, 2026-10-10): the vendor libimp exports all 8 calls (T21 1.0.33, T10/T20 3.12.0; a decoder channel thread around DecoderInit/DecoderExit); the open stack has no decoder (the Helix wrapper of these SoCs is encode-only), and no streamer uses them: not worth building T23 (agg-37): the decoder ran in the OEM Helix worker; that bridge is off by default since agg-37 (`OPENIMP_T23_HELIX_BRIDGE=0`, docs/SIZE.md), so `IMP_Decoder_CreateChn` fails (err); `OPENIMP_T23_HELIX_BRIDGE=1` builds it again.
13. `Encoder_CreateChn`: audit: reaches the driver/kernel
14. `Encoder_CreateGroup`: audit: userspace implementation
15. `Encoder_DestroyChn`: audit: reaches the driver/kernel
16. `Encoder_DestroyGroup`: audit: userspace implementation
17. `Encoder_FlushStream`: all: drops the encoded stream nobody fetched (agg-25)
18. `Encoder_Get/SetChnAttrRcMode`: audit: reaches the driver/kernel; userspace implementation
19. `Encoder_Get/SetChnColor2Grey`: T10+T20+T21: codes grey pictures (agg-25)
20. `Encoder_Get/SetChnCrop`: audit: userspace implementation; T10/T20/T21: the vendor libimp exports it without a header (i264e_set_param, x/y/w/h multiples of 16); OpenIMP does not (miss, found by `make export-check` 2026-10-11, a link shim is an open question)
2. `Encoder_Get/SetChnDenoise`: T21: done as the vendor: libimp 1.0.33 clears the denoise type at init (channel_i264e_encoder_init picks encoder mode 4 from get_cpu_id() 11..14, i264e_validate_parameters keeps denoise only for mode 1), so on the vendor T21 dnType reads back 0 and the encoder runs one Helix job per picture; dnType >= 3 with enable set at CreateChn is -1; IQp/PQp are kept. OpenIMP T21 does the same (2026-10-10: PC420 vendor stack vs open stack, readback equal, size and fps equal, ffmpeg decode clean). T10/T20 (vendor 3.12.0 has no such gate): type, IQp and PQp stored and read back as the vendor (2026-10-10: the T20 build also defines PLATFORM_T21 and read dnType back as 0 like T21; fixed, host test `p2_t1x_test_t20`; apitest roundtrip PASS on T20), not applied; the vendor encodes every frame twice with attrDenoise.enable set at CreateChn and dnType 1/2, a first pass at slice QP dnIQp (I) / dnPQp (P; type 2 forces slice type I) before the normal pass (i264e_reconfig_dn_set, i264e_slice_write). Not built: the pass-1 job needs a trace on a T20 camera, stability first
21. `Encoder_Get/SetChnFrmRate`: audit: reaches the driver/kernel; userspace implementation
3. `Encoder_Get/SetChnFrmUsedMode`: T23: stored in the channel attribute (claude/t23-enc-rest, not in agg-25); T10/T20/T21: three words stored/read as the vendor (also before CreateChn; NULL and channel >= limit -1), apitest PASS on T10, T20, T21; as on T23 the frame reuse/skip of the OEM channel thread is not reproduced, OpenIMP paces every channel at its frame rate; T30 1.0.5: the three words are stored as given, also before CreateChn (same vendor code as T21); host test
22. `Encoder_Get/SetChnGopAttr`: audit: userspace implementation
23. `Encoder_Get/SetChnROI`: T10: EFE ROI registers per the OEM slice init, device-tested 2026-10-09; T20: dev (apitest PASS; H.264 check: QP51 region blocky, QP15 fine); T21: the vendor 1.0.33 never programs IMP ROIs; OpenIMP applies them by default (beyond vendor), device-tested; T23: the native Helix encoder programs the regions like T21 (the vendor 1.3.0 writes the same registers), device-tested 2026-10-10, QP map and VA-API/strict ffmpeg clean; from agg-34 all Helix paths keep the stream valid H.264: deltas -25..+25, spread at most 25, absolute QPs in `[max_qp - 25, min_qp + 25]` (`Helix_H264_RoiSanitize`); before, a relative -26 or an absolute QP more than 25 from the macroblock QP gave broken pictures in hardware decoders (VA-API, VLC, browsers); details: docs/ROI.md
4. `Encoder_Get/SetFisheyeEnableStatus`: T23+T31: kept for getter only (documented in source); class d: the flag only has an effect in the vendor's closed fisheye/i264e path, which OpenIMP does not contain (T31: closed IVS fisheye module; T23: i264e creation option of the OEM session); T10/T20/T21: a flag in the channel record, settable only on an idle channel (-1 once created), exactly as the vendor (disassembly T21 0x45454, T20 0x4776c); apitest PASS on T10, T20, T21; class d like T23/T31; T30 1.0.5: flag kept, settable before CreateChn only, as T21 (the closed fisheye motion search of the vendor encoder is not in the Helix path); host test; vendor check 2026-10-10 (libimp T23 1.3.0 and T31 1.1.6 disassembly): T31 1.1.6 sets the flag in the channel record only while the channel does not exist (channel below 9, else -1), as OpenIMP does; T23 1.3.0 likewise; the flag is read later by the closed encoder code
24. `Encoder_Get/SetGDRCfg`: audit: userspace implementation
5. `Encoder_Get/SetH264TransCfg`: T10: no chroma-offset register, stays 0; T20: chroma QP offset via PPS + reg 0x40120 (claude/t1x-roi), verified on a camera, no colour shift; T21: chroma QP offset, PPS rewrite; not device-tested
6. `Encoder_Get/SetH265TransCfg`: T21+T23: checked and dropped like the vendor libimp, Get returns zeros (T23 vendor 1.3.0: channel below 9 and non-NULL return 0, nothing stored, Get zero-fills the 8 bytes; matched 2026-10-10, device-tested on a T23 camera)
25. `Encoder_Get/SetJpegeQl`: T10+T20+T21: reaches the running codec live (host test; apitest 2026-10-06: marker check and live change PASS on T20/T21); T41: live and at CreateChn (host)
26. `Encoder_Get/SetMaxStreamCnt`: audit: userspace implementation
7. `Encoder_Get/SetMbRC`: T10 (2026-10-10, from the disassembly notes in docs/T20_RC.md, not device-tested): the vendor library is the T20 build, but its T10 rate controller has no macroblock part, so the call is stored and read back and changes nothing, as in OpenIMP (src/rc_t10; matched, no-op); T20: switches the macroblock QP table (agg-25)
27. `Encoder_Get/SetPool`: audit: userspace implementation
28. `Encoder_Get/SetQpgMode`: T21: kept without range check as the vendor libimp (nothing reads it there); apitest roundtrip PASS; real QPG after the release
29. `Encoder_Get/SetStreamBufSize`: audit: userspace implementation
30. `Encoder_Get/SetSuperFrameCfg`: T10+T20: reaches the rate control (T20 apitest PASS); T21: mode DISCARD refused (the Helix eprc has no frame discard), the software modes follow after the release
8. `Encoder_Get/Setframelossthd`: T23: kept for getter only (documented in source); class d: the vendor re-encodes oversized frames at a higher QP inside its closed encoder; the Helix session has no such hook (would need a re-encode path in the encoder, low value); vendor check 2026-10-10 (libimp T23 1.3.0 and T31 1.1.6 disassembly): T23 1.3.0 validates and stores the threshold for the closed encoder; T31 1.1.6 exports no frame-loss function (n.a.)
31. `Encoder_GetChnAttr`: audit: userspace implementation
32. `Encoder_GetChnAveBitrate`: audit: userspace implementation
33. `Encoder_GetChnEncType`: audit: userspace implementation
34. `Encoder_GetChnEvalInfo`: T31: claude/t23-enc-rest (not in agg-25)
35. `Encoder_GetChnMaxPictureSize`: T23: as the OEM stores it, re-encode on overshoot (claude/t23-enc-rest, not in agg-25)
36. `Encoder_GetFd`: audit: userspace implementation; T21/T23/T31: pipe + pump thread (new 2026-10-06, was a stub): poll(POLLIN) within 1 s, apitest PASS; T20/T10 not in the vendor API
37. `Encoder_GetStream`: audit: reaches the driver/kernel
38. `Encoder_InsertUserData`: audit: userspace implementation; T20/T21/T23: emits a user_data_unregistered SEI (OEM UUID) in front of the next picture, found in the next 30 frames (apitest PASS); T10 shares the T20 build, not re-tested
39. `Encoder_PollingModuleStream`: audit: reaches the driver/kernel; T21/T23/T31: honours the input channel bitmap and polls all selected channels (apitest PASS)
40. `Encoder_PollingStream`: audit: reaches the driver/kernel
41. `Encoder_Query`: audit: userspace implementation
42. `Encoder_RegisterChn`: audit: userspace implementation
43. `Encoder_ReleaseStream`: audit: reaches the driver/kernel
44. `Encoder_RequestGDR`: audit: userspace implementation
45. `Encoder_RequestIDR`: audit: userspace implementation
46. `Encoder_SetChnBitRate`: audit: userspace implementation
47. `Encoder_SetChnEntropyMode`: audit: userspace implementation
48. `Encoder_SetChnGopLength`: audit: userspace implementation
49. `Encoder_SetChnInitQP`: audit: userspace implementation
9. `Encoder_SetChnMaxPictureSize`: T23: as the OEM stores it, re-encode on overshoot (claude/t23-enc-rest, not in agg-25); T41: written into rcAttr copy, codec not updated (T23: loss threshold kept only)
50. `Encoder_SetChnQp`: audit: userspace implementation
51. `Encoder_SetChnQpBounds`: audit: userspace implementation
52. `Encoder_SetChnQpBoundsPerFrame`: audit: userspace implementation
53. `Encoder_SetChnQpIPDelta`: audit: userspace implementation
54. `Encoder_SetChnResizeMode`: audit: userspace implementation
55. `Encoder_SetDefaultParam`: audit: userspace implementation
10. `Encoder_SetFrameRelease`: T31: vendor stores num/den and a helper hands the source frame back early; OpenIMP returns every frame as soon as the AVPU has consumed it, so the ratio has nothing to change (class a: no visible effect, matched)
56. `Encoder_SetGOPSize`: audit: reaches the driver/kernel; userspace implementation
11. `Encoder_SetMultiSectionMode`: T23: kept for getter only (documented in source); class d: slice/section split of the vendor i264e encoder, the Helix session cannot be given it; vendor check 2026-10-10 (libimp T23 1.3.0 and T31 1.1.6 disassembly): T23 1.3.0 range-checks the arguments and stores them in the global block the closed i264e reads; T31 1.1.6 exports no multi-section function (n.a.)
12. `Encoder_SetbufshareChn`: vendor T31 1.1.6 / T40 1.3.1 / T41 1.2.6 (disassembled): stores the share channel in the channel record (kept over CreateChn, set to -1 by DestroyChn; a negative value is valid and means no sharing) and CreateChn reads it for a JPEG channel only: the JPEG channel borrows the video channel's stream manager and stream contexts and skips building its own pool of stream buffers (CreateChn fails when that video channel does not exist yet; T41 also refuses the MMA option). OpenIMP: same value handling, argument checks and return codes (-1 on T31/T40, 0x80040001 on T41, both channel numbers below the channel limit); CreateChn only logs a warning where the vendor would fail. There is nothing to share: OpenIMP JPEG channels own no stream buffer pool (JPEG is software on T40/T41, T31 uses one hardware-JPEG stream buffer for all JPEG channels), so sharing would save no rmem; measured on a T40 camera (video 640x360 plus a JPEG channel in the same group, with and without the call): JPEG channel cost about 24-72 kB of free memory either way, snapshots valid, H.264 decodes without errors in software and VA-API decoders. Host tests for T31/T40/T41, device test T40 only
57. `Encoder_StartRecvPic`: audit: userspace implementation
58. `Encoder_StopRecvPic`: audit: userspace implementation
59. `Encoder_UnRegisterChn`: audit: userspace implementation
60. `Encoder_YuvGetCrop`: audit: reaches the driver/kernel
61. `Encoder_YuvRequestIDR`: audit: reaches the driver/kernel
62. `Encoder_YuvSetCrop`: audit: reaches the driver/kernel
63. `Encoder_Get/SetChnRoiAttr`: not in the T31 vendor API (T31 column stays n.a.): OpenIMP exports it on T31 as a beyond-vendor ROI through the AVPU QP table, device-tested (dev), default on, `OPENIMP_T31_ROI=0` refuses the call; T40: experimental, off unless `OPENIMP_T40_ROI=1`, code only, not device-tested; T41 (also `SetChnMapRoi`): cache, the windows/map are stored and the EP2 QP table is written (off unless `OPENIMP_T41_ROI=1`), but the T41 AVPU ignores the table (no effect on bit rate or picture in FixQP/CBR, docs/ROI.md); same valid-H.264 clamp as the Helix paths (docs/ROI.md)
64. `Log_Set_Option` (`IMP_Log_Set_Option`): exported since agg-34 on T10/T20/T21/T23/T31 (the option is the OEM field mask of the log line); host test `tests/core/imp_log_test.c`, no device test; T41 cell not re-audited
65. `Encoder_Get/SetChangeRef`: T10/T20: stored and read back with the vendor return codes (SetChangeRef -1 on an idle channel, GetChangeRef -1 on an idle channel; apitest PASS on T10, T20); T21 (2026-10-10, host test): SetChangeRef -1 on an idle channel; GetChangeRef answers 0 on an idle channel and, as the vendor (T21 1.0.33 and T30 1.0.5, same code), logs "unsupport to change ref channel" and returns -1 on every created channel without writing the value (it used to return the stored value); the vendor passes it to the i264e reference code (param 12), which only the HSkip reference modes read; the Helix path codes no HSkip structure (class c). Disassembly: T21 1.0.33 0x48bd4/0x48d90, T20 3.12.0 0x4b05c/0x4b220; T30 1.0.5 (same vendor code as T21): SetChangeRef stores the flag of a created channel (-1 for an idle one); GetChangeRef is 0 for an idle channel and -1 for a created one (the vendor logs "unsupport to change ref channel"); host test; T30 1.0.5 (same vendor code as T21): SetChangeRef stores the flag of a created channel (-1 for an idle one); GetChangeRef is 0 for an idle channel and -1 for a created one (the vendor logs "unsupport to change ref channel"); host test
66. `Encoder_Get/SetChnDemask`: T10/T20 3.12.0 (0x49160/0x492d8): three words stored in rcAttr.attrDemask without a created check, as the vendor (apitest PASS on T10, T20); the vendor also pokes an i264e flag word, the Helix path has no demask stage (class c)
67. `Encoder_Get/SetChnH264Demask`: T10/T20 (class d, 2026-10-10): only the 3.9.0 libimp exports the H264-named variants; 3.12.0 and the header of OpenIMP use ChnDemask/ChnDenoise/ChnFrmUsedMode, and the 3.9.0 IMPEncoderRcAttr/CHNAttr layout differs from the one OpenIMP implements, so these calls would be dead code (an app built for 3.9.0 breaks on the structures first)
68. `Encoder_Get/SetChnH264Denoise`: T10/T20 (class d): 3.9.0-only name, see Encoder_Get/SetChnH264Demask
69. `Encoder_Get/SetChnH264FrmUsedMode`: T10/T20 (class d): 3.9.0-only name, see Encoder_Get/SetChnH264Demask
70. `Encoder_Get/SetChnHSkip`: T10/T20 3.12.0 (0x49dc8/0x4a078): needs a created channel, skipType up to maxHSkipType, six words stored (apitest PASS on T10, T20); unlike T21 the IDR period is not applied (the OEM pushes it to i264e param 9; class c); T30 1.0.5: the six words are stored, also for an idle channel, which reads back as zeros as in the vendor (up to maxHSkipType, 0 until CreateChn); unlike T21 the IDR period is not handed to the encoder on T30 (the CreateChn path does not do it either), so it stays a cache until a T30 device run; host test; T21 (2026-10-10, host test; T21 1.0.33 0x478f0/0x47ba0): an idle (not yet created) channel is accepted as the vendor does, SetChnHSkip stores the six words without the maxHSkipType check and without reaching the encoder, GetChnHSkip answers all zero with 0 (both used to be -1); T21 (2026-10-10, host test; T21 1.0.33 0x478f0/0x47ba0): an idle (not yet created) channel is accepted as the vendor does, SetChnHSkip stores the six words without the maxHSkipType check and without reaching the encoder, GetChnHSkip answers all zero with 0 (both used to be -1)
71. `Encoder_Get/SetChnRcAttr`: T10/T20 (class d): 3.9.0-only call (IMPEncoderRcAttr of that SDK generation), see Encoder_Get/SetChnH264Demask
72. `Encoder_GetGOPSize`: T10/T20/T21: the channel GOP length of a created channel (the vendor reads i264e param 7); an idle channel answers 0 and leaves the struct alone on T21, -1 on T20; apitest PASS on T10, T20, T21; T30 1.0.5: a created channel reads its maxGop; an idle channel answers 0 and leaves the struct alone (as T21); host test
73. `Encoder_SetChnHSkipBlackEnhance`: T10/T20/T21: returns 0, stores the flag of a created channel (the vendor pushes it to i264e param 10, read only by the HSkip code; class c); apitest PASS on T10, T20, T21; T30 1.0.5: the flag is kept for a created channel, 0 for an idle one (as T21); host test

</details>

### Framesource

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|
| `EmuFrameSource_*` (4 functions) [1] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FB_*` (5 functions) [2] | n.a. | n.a. | n.a. | n.a. | n.a. | miss | n.a. | n.a. | – |
| `FrameSource_DequeueBuffer` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FrameSource_ExternInject_CreateChn` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FrameSource_ExternInject_DestroyChn` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FrameSource_ExternInject_DisableChn` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FrameSource_ExternInject_EnableChn` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| ► `FrameSource_Get/SetChnFifoAttr` [3] | host | dev | dev | dev | ? | dev | host | host | p† r† |
| ► `FrameSource_Get/SetDelay` [4] | host | dev | dev | dev | ? | dev | host | host | T31/T41: r† |
| ► `FrameSource_Get/SetFrameDepth` [5] | aud | dev | dev | dev | ? | dev | ? | cache | see note |
| ► `FrameSource_Get/SetI2dAttr` [6] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | host | r† |
| ► `FrameSource_Get/SetMaxDelay` [7] | host | dev | dev | dev | ? | dev | host | host | T31/T41: r† |
| ► `FrameSource_Get/SetPool` [8] | n.a. | n.a. | n.a. | dev | n.a. | dev | n.a. | cache | T31/T41: r† |
| `FrameSource_GetFrameEx` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| ► `FrameSource_GetTimedFrame` [9] | host | dev | dev | dev | ? | dev | host | host | r† |
| `FrameSource_QueueBuffer` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FrameSource_ReleaseFrameEx` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FrameSource_SetYuvAlign` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |

Notes:

1. `EmuFrameSource_*` (4 functions): functions: CreateChn, DestroyChn, DisableChn, EnableChn; T31: declared in the 1.1.4+ headers, exported by none of the 28 vendor libimp builds (nm)
2. `FB_*` (5 functions): functions: CreateGroup, DestroyGroup, DisableDev, EnableDev, GetDevInfo; class d: frame-buffer overlay for a display device; the IP cameras have no LCD/framebuffer, no streamer uses it; vendor check 2026-10-10: only the T31 1.1.5-zrt (display) libimp builds export the five functions, none of the camera builds 1.1.1-1.1.6, so the camera images have nothing to wire them to
3. `FrameSource_Get/SetChnFifoAttr`: T10+T20+T21+T23+T31: = SetMaxDelay(maxdepth) (agg-25); FIFO_DATA_PRIORITY refused for maxdepth>0; T41: FIFO attr stored, no FIFO behind it
4. `FrameSource_Get/SetDelay`: T10+T20+T21+T23+T31: real delay FIFO (agg-25); T10 shares the T20 build; FIFO_DATA_PRIORITY refused for maxdepth>0; T41: host (vendor 1.2.6 semantics in src/t40/openimp_p1.c, delay cache; host test only, `export-check` 2026-10-11)
5. `FrameSource_Get/SetFrameDepth`: T41: depth stored, GetFrame ignores it (T41 p1); used by: T10/T20/T21/T31/T41: prudynt†, raptor†; T23: prudynt, raptor, timps
6. `FrameSource_Get/SetI2dAttr`: T41: host (vendor 1.2.6 semantics in src/t40/openimp_p1.c, delay cache; host test only, `export-check` 2026-10-11)
7. `FrameSource_Get/SetMaxDelay`: T10+T20+T21+T23+T31: real delay FIFO (agg-25); T10 shares the T20 build; FIFO_DATA_PRIORITY refused for maxdepth>0; T41: host (vendor 1.2.6 semantics in src/t40/openimp_p1.c, delay cache; host test only, `export-check` 2026-10-11)
8. `FrameSource_Get/SetPool`: T23+T31: real memory pools (claude/t23t31-cacheonly, not in agg-25); T41: pool id recorded only
9. `FrameSource_GetTimedFrame`: T10+T20+T21+T23+T31: agg-25; T41: host (vendor 1.2.6 semantics in src/t40/openimp_p1.c, delay cache; host test only, `export-check` 2026-10-11)

<details><summary>All 30 rows of this area (18 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|
| `EmuFrameSource_*` (4 functions) [1] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FB_*` (5 functions) [2] | n.a. | n.a. | n.a. | n.a. | n.a. | miss | n.a. | n.a. | – |
| `FrameSource_ChnStatQuery` [10] | n.a. | n.a. | n.a. | n.a. | n.a. | dev | n.a. | n.a. | – |
| `FrameSource_CreateChn` [11] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `FrameSource_DequeueBuffer` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FrameSource_DestroyChn` [12] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `FrameSource_DisableChn` [13] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `FrameSource_EnableChn` [14] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `FrameSource_ExternInject_CreateChn` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FrameSource_ExternInject_DestroyChn` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FrameSource_ExternInject_DisableChn` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FrameSource_ExternInject_EnableChn` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FrameSource_Get/SetChnAttr` [15] | aud | dev | dev | dev | ? | dev | host | aud | p† r† t |
| ► `FrameSource_Get/SetChnFifoAttr` [3] | host | dev | dev | dev | ? | dev | host | host | p† r† |
| ► `FrameSource_Get/SetDelay` [4] | host | dev | dev | dev | ? | dev | host | host | T31/T41: r† |
| `FrameSource_Get/SetDirectModeAttr` [16] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | n.a. | – |
| ► `FrameSource_Get/SetFrameDepth` [5] | aud | dev | dev | dev | ? | dev | ? | cache | see note |
| ► `FrameSource_Get/SetI2dAttr` [6] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | host | r† |
| ► `FrameSource_Get/SetMaxDelay` [7] | host | dev | dev | dev | ? | dev | host | host | T31/T41: r† |
| ► `FrameSource_Get/SetPool` [8] | n.a. | n.a. | n.a. | dev | n.a. | dev | n.a. | cache | T31/T41: r† |
| `FrameSource_GetFrame` [17] | aud | dev | dev | dev | ? | dev | dev | aud | see note |
| `FrameSource_GetFrameEx` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| ► `FrameSource_GetTimedFrame` [9] | host | dev | dev | dev | ? | dev | host | host | r† |
| `FrameSource_QueueBuffer` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FrameSource_ReleaseFrame` [18] | aud | dev | dev | dev | ? | dev | dev | aud | see note |
| `FrameSource_ReleaseFrameEx` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `FrameSource_SetChnRotate` [19] | n.a. | n.a. | n.a. | n.a. | n.a. | dev | n.a. | n.a. | t |
| `FrameSource_SetSource` [20] | n.a. | n.a. | n.a. | n.a. | ? | aud | n.a. | n.a. | – |
| `FrameSource_SetYuvAlign` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `FrameSource_SnapFrame` [21] | aud | dev | dev | dev | ? | dev | dev | host | r† |

Notes:

1. `EmuFrameSource_*` (4 functions): functions: CreateChn, DestroyChn, DisableChn, EnableChn; T31: declared in the 1.1.4+ headers, exported by none of the 28 vendor libimp builds (nm)
2. `FB_*` (5 functions): functions: CreateGroup, DestroyGroup, DisableDev, EnableDev, GetDevInfo; class d: frame-buffer overlay for a display device; the IP cameras have no LCD/framebuffer, no streamer uses it; vendor check 2026-10-10: only the T31 1.1.5-zrt (display) libimp builds export the five functions, none of the camera builds 1.1.1-1.1.6, so the camera images have nothing to wire them to
10. `FrameSource_ChnStatQuery`: audit: userspace implementation
11. `FrameSource_CreateChn`: audit: reaches the driver/kernel
12. `FrameSource_DestroyChn`: audit: reaches the driver/kernel
13. `FrameSource_DisableChn`: audit: reaches the driver/kernel
14. `FrameSource_EnableChn`: audit: reaches the driver/kernel
15. `FrameSource_Get/SetChnAttr`: audit: userspace implementation
3. `FrameSource_Get/SetChnFifoAttr`: T10+T20+T21+T23+T31: = SetMaxDelay(maxdepth) (agg-25); FIFO_DATA_PRIORITY refused for maxdepth>0; T41: FIFO attr stored, no FIFO behind it
4. `FrameSource_Get/SetDelay`: T10+T20+T21+T23+T31: real delay FIFO (agg-25); T10 shares the T20 build; FIFO_DATA_PRIORITY refused for maxdepth>0; T41: host (vendor 1.2.6 semantics in src/t40/openimp_p1.c, delay cache; host test only, `export-check` 2026-10-11)
16. `FrameSource_Get/SetDirectModeAttr`: audit: userspace implementation
5. `FrameSource_Get/SetFrameDepth`: T41: depth stored, GetFrame ignores it (T41 p1); used by: T10/T20/T21/T31/T41: prudynt†, raptor†; T23: prudynt, raptor, timps
6. `FrameSource_Get/SetI2dAttr`: T41: host (vendor 1.2.6 semantics in src/t40/openimp_p1.c, delay cache; host test only, `export-check` 2026-10-11)
7. `FrameSource_Get/SetMaxDelay`: T10+T20+T21+T23+T31: real delay FIFO (agg-25); T10 shares the T20 build; FIFO_DATA_PRIORITY refused for maxdepth>0; T41: host (vendor 1.2.6 semantics in src/t40/openimp_p1.c, delay cache; host test only, `export-check` 2026-10-11)
8. `FrameSource_Get/SetPool`: T23+T31: real memory pools (claude/t23t31-cacheonly, not in agg-25); T41: pool id recorded only
17. `FrameSource_GetFrame`: audit: reaches the driver/kernel; userspace implementation; used by: T10/T20/T21/T31/T41: raptor†; T23: raptor, timps; all: GetFrame/SnapFrame wait up to 2 s like the vendor libimp (T20 REQBUFS/SET_BANKS fix, T21/T23/T31 wait), apitest PASS
9. `FrameSource_GetTimedFrame`: T10+T20+T21+T23+T31: agg-25; T41: host (vendor 1.2.6 semantics in src/t40/openimp_p1.c, delay cache; host test only, `export-check` 2026-10-11)
18. `FrameSource_ReleaseFrame`: audit: reaches the driver/kernel; used by: T10/T20/T21/T31/T41: raptor†; T23: raptor, timps
19. `FrameSource_SetChnRotate`: audit: userspace implementation
20. `FrameSource_SetSource`: audit: userspace implementation
21. `FrameSource_SnapFrame`: T41: copies the next consumer frame, packed NV12; T20/T21/T23/T31: waits up to 2 s like the vendor; T40: device-tested on cam-K 2026-10-10 (OpenIMP vs vendor library)

</details>

### OSD

All regular OSD functions (`OSD_CreateGroup`, `CreateRgn`, `RegisterRgn`, `Set/GetRgnAttr`, `Set/GetGrpRgnAttr`, `ShowRgn`, `UpdateRgnAttrData`, `Start/StopGroup` ...) are done on every SoC (OSD is device-tested on T10, T20, T21, T23, T31 and T41, see the OSD row in the matrix above); in the full list they show as done (?) because the audit tracks no per-call device test. The gap table lists the ISP-OSD variants (`*_ISP`, `ISP_Tuning_*Osd*`) that only the T23/T41 vendor API has, plus single helpers; n.a. means the function does not exist in that SoC's vendor API, not that OSD is missing.

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|
| `ISP_Get/SetDrawAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | n.a. | – |
| `ISP_Get/SetOSDAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `ISP_Get/SetSingleOSDAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | dev | n.a. | – |
| ► `ISP_Tuning_CreateOsdRgn` [1] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | dev | err | r† |
| ► `ISP_Tuning_DestroyOsdRgn` [2] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | dev | err | r† |
| `ISP_Tuning_Get/SetOSDAttr` [3] | n.a. | n.a. | n.a. | err | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetOSDBlock` [4] | n.a. | n.a. | n.a. | err | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetOsdRgnAttr` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | dev | miss | – |
| ► `ISP_Tuning_SetOsdPoolSize` [5] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | dev | stub | r† |
| ► `ISP_Tuning_SetOsdRgnAttr` [6] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | dev | err | r† |
| ► `ISP_Tuning_ShowOsdRgn` [7] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | dev | err | r† |
| `OSD_AttachToGroup` [27] | host | host | host | aud | aud | aud | host | aud | – |
| `OSD_CreateRgn_ISP` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | host | miss | – |
| `OSD_DestroyRgn_ISP` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | host | miss | – |
| `OSD_Exit_ISP` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | host | miss | – |
| `OSD_Get/SetRgnAttr_ISP` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | miss | miss | – |
| `OSD_GetRegionLuma` [26] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `OSD_GetRgnAttr_ISPPic` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | host | miss | – |
| `OSD_RgnCreate_Query` [16] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | host | aud | – |
| `OSD_RgnRegister_Query` [17] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | host | aud | – |
| `OSD_SetGroupCallback` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `OSD_SetMosaic` [18] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | host | aud | – |
| `OSD_SetPoolSize_ISP` [8] | n.a. | n.a. | n.a. | host | n.a. | n.a. | host | miss | – |
| `OSD_SetRgnAttr_PicISP` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | host | miss | – |
| `OSD_ShowRgn_ISP` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | host | miss | – |

Notes:

1. `ISP_Tuning_CreateOsdRgn`: T41: ENOTSUP stub (returns -1)
2. `ISP_Tuning_DestroyOsdRgn`: T41: ENOTSUP stub (returns -1)
3. `ISP_Tuning_Get/SetOSDAttr`: T23: 0x8000181 on the stock mscaler OSD attribute bytes (open-tx-isp claude/gaps-t23-t31-drv, class c; emulator-identical to the stock handlers); host build only, device test pending; no image effect yet (the MSCA update that programs the OSD blocks is not run); Not in agg-36: the open-tx-isp branch claude/gaps-t23-t31-drv is held back until its T23 SwitchBin is reworked (review: memcpy over live banks), so the agg-36 driver still behaves as before (cell unchanged).
4. `ISP_Tuning_Get/SetOSDBlock`: T23: 0x8000182 on the stock mscaler OSD block table (open-tx-isp claude/gaps-t23-t31-drv, class c; emulator-identical to the stock Set including its stale-size quirk; the Get reports the requested block, stock always reports block 0 because it clears its local first; a block above 7 is refused, beyond stock; a block beyond the channel limits is logged and ignored, the call still returns 0, as stock); host build only, device test pending; no image effect yet (the MSCA update that programs the OSD blocks is not run); Not in agg-36: the open-tx-isp branch claude/gaps-t23-t31-drv is held back until its T23 SwitchBin is reworked (review: memcpy over live banks), so the agg-36 driver still behaves as before (cell unchanged).
5. `ISP_Tuning_SetOsdPoolSize`: T41: 2 insns, returns ?
6. `ISP_Tuning_SetOsdRgnAttr`: T41: ENOTSUP stub (returns -1)
7. `ISP_Tuning_ShowOsdRgn`: T41: ENOTSUP stub (returns -1)
8. `OSD_SetPoolSize_ISP`: T23: ISP OSD pictures from the pool (claude/t23t31-cacheonly, not in agg-25)
26. `OSD_GetRegionLuma`: T23: declared in the 1.1.x/1.3.0 headers but exported by none of the 8 vendor libimp builds (nm); a vendor-stack application cannot link it either
27. `OSD_AttachToGroup`: T10/T20/T21: system_attach() as the vendor wrapper (src->to becomes src->from->to, rolled back on error, -1); host test with a fake bind table; the apitest skips it (deprecated, the chain is built with IMP_System_Bind); T30 1.0.5: the same system_attach() wrapper as T21 (disassembly compared); the T21 host test with a fake bind table also runs against the T30 build

<details><summary>All 39 rows of this area (24 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|
| `ISP_Get/SetDrawAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | n.a. | – |
| `ISP_Get/SetOSDAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | host | n.a. | – |
| `ISP_Get/SetSingleOSDAttr` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | dev | n.a. | – |
| ► `ISP_Tuning_CreateOsdRgn` [1] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | dev | err | r† |
| ► `ISP_Tuning_DestroyOsdRgn` [2] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | dev | err | r† |
| `ISP_Tuning_Get/SetOSDAttr` [3] | n.a. | n.a. | n.a. | err | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_Get/SetOSDBlock` [4] | n.a. | n.a. | n.a. | err | n.a. | n.a. | n.a. | miss | – |
| `ISP_Tuning_GetOsdRgnAttr` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | dev | miss | – |
| ► `ISP_Tuning_SetOsdPoolSize` [5] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | dev | stub | r† |
| ► `ISP_Tuning_SetOsdRgnAttr` [6] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | dev | err | r† |
| ► `ISP_Tuning_ShowOsdRgn` [7] | n.a. | n.a. | n.a. | aud | n.a. | n.a. | dev | err | r† |
| `OSD_AttachToGroup` [27] | host | host | host | aud | aud | aud | host | aud | – |
| ► `OSD_CreateGroup` [9] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| ► `OSD_CreateRgn` [10] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `OSD_CreateRgn_ISP` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | host | miss | – |
| ► `OSD_DestroyGroup` [11] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| ► `OSD_DestroyRgn` [12] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `OSD_DestroyRgn_ISP` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | host | miss | – |
| `OSD_Exit_ISP` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | host | miss | – |
| ► `OSD_Get/SetGrpRgnAttr` [13] | aud | dev | dev | dev | ? | dev | host | aud | p† r† t |
| ► `OSD_Get/SetRgnAttr` [14] | aud | dev | dev | dev | ? | dev | host | aud | p† r† t |
| `OSD_Get/SetRgnAttr_ISP` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | miss | miss | – |
| `OSD_GetRegionLuma` [26] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `OSD_GetRgnAttr_ISPPic` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | host | miss | – |
| ► `OSD_RegisterRgn` [15] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `OSD_RgnCreate_Query` [16] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | host | aud | – |
| `OSD_RgnRegister_Query` [17] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | host | aud | – |
| `OSD_SetGroupCallback` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `OSD_SetMosaic` [18] | n.a. | n.a. | n.a. | dev | n.a. | n.a. | host | aud | – |
| `OSD_SetPoolSize` [19] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `OSD_SetPoolSize_ISP` [8] | n.a. | n.a. | n.a. | host | n.a. | n.a. | host | miss | – |
| ► `OSD_SetRgnAttrWithTimestamp` [20] | aud | dev | dev | dev | ? | dev | host | aud | r† |
| `OSD_SetRgnAttr_PicISP` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | host | miss | – |
| ► `OSD_ShowRgn` [21] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| `OSD_ShowRgn_ISP` | n.a. | n.a. | n.a. | aud | n.a. | n.a. | host | miss | – |
| ► `OSD_Start` [22] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| ► `OSD_Stop` [23] | aud | dev | dev | dev | ? | dev | host | aud | r† |
| ► `OSD_UnRegisterRgn` [24] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| ► `OSD_UpdateRgnAttrData` [25] | aud | dev | dev | dev | ? | dev | host | aud | p† r† |

Notes:

1. `ISP_Tuning_CreateOsdRgn`: T41: ENOTSUP stub (returns -1)
2. `ISP_Tuning_DestroyOsdRgn`: T41: ENOTSUP stub (returns -1)
3. `ISP_Tuning_Get/SetOSDAttr`: T23: 0x8000181 on the stock mscaler OSD attribute bytes (open-tx-isp claude/gaps-t23-t31-drv, class c; emulator-identical to the stock handlers); host build only, device test pending; no image effect yet (the MSCA update that programs the OSD blocks is not run); Not in agg-36: the open-tx-isp branch claude/gaps-t23-t31-drv is held back until its T23 SwitchBin is reworked (review: memcpy over live banks), so the agg-36 driver still behaves as before (cell unchanged).
4. `ISP_Tuning_Get/SetOSDBlock`: T23: 0x8000182 on the stock mscaler OSD block table (open-tx-isp claude/gaps-t23-t31-drv, class c; emulator-identical to the stock Set including its stale-size quirk; the Get reports the requested block, stock always reports block 0 because it clears its local first; a block above 7 is refused, beyond stock; a block beyond the channel limits is logged and ignored, the call still returns 0, as stock); host build only, device test pending; no image effect yet (the MSCA update that programs the OSD blocks is not run); Not in agg-36: the open-tx-isp branch claude/gaps-t23-t31-drv is held back until its T23 SwitchBin is reworked (review: memcpy over live banks), so the agg-36 driver still behaves as before (cell unchanged).
5. `ISP_Tuning_SetOsdPoolSize`: T41: 2 insns, returns ?
6. `ISP_Tuning_SetOsdRgnAttr`: T41: ENOTSUP stub (returns -1)
7. `ISP_Tuning_ShowOsdRgn`: T41: ENOTSUP stub (returns -1)
9. `OSD_CreateGroup`: audit: userspace implementation
10. `OSD_CreateRgn`: audit: reaches the driver/kernel
11. `OSD_DestroyGroup`: audit: userspace implementation
12. `OSD_DestroyRgn`: audit: reaches the driver/kernel
13. `OSD_Get/SetGrpRgnAttr`: audit: userspace implementation
14. `OSD_Get/SetRgnAttr`: audit: reaches the driver/kernel; userspace implementation; T40: the PIC/COVER test uses the set path; Get not checked, so host
26. `OSD_GetRegionLuma`: T23: declared in the 1.1.x/1.3.0 headers but exported by none of the 8 vendor libimp builds (nm); a vendor-stack application cannot link it either
15. `OSD_RegisterRgn`: audit: reaches the driver/kernel; userspace implementation
16. `OSD_RgnCreate_Query`: audit: userspace implementation
17. `OSD_RgnRegister_Query`: audit: userspace implementation
18. `OSD_SetMosaic`: audit: userspace implementation
19. `OSD_SetPoolSize`: audit: userspace implementation
8. `OSD_SetPoolSize_ISP`: T23: ISP OSD pictures from the pool (claude/t23t31-cacheonly, not in agg-25)
20. `OSD_SetRgnAttrWithTimestamp`: audit: reaches the driver/kernel
21. `OSD_ShowRgn`: audit: reaches the driver/kernel; userspace implementation
22. `OSD_Start`: audit: userspace implementation
23. `OSD_Stop`: audit: userspace implementation
24. `OSD_UnRegisterRgn`: audit: userspace implementation
25. `OSD_UpdateRgnAttrData`: audit: reaches the driver/kernel
27. `OSD_AttachToGroup`: T10/T20/T21: system_attach() as the vendor wrapper (src->to becomes src->from->to, rolled back on error, -1); host test with a fake bind table; the apitest skips it (deprecated, the chain is built with IMP_System_Bind); T30 1.0.5: the same system_attach() wrapper as T21 (disassembly compared); the T21 host test with a fake bind table also runs against the T30 build

</details>

### IVS

On T40 the IVS framework is the T31/T41 code since agg-35: move detection (create/register/start/poll/result/destroy) is device-tested on cam-K (20 s, 2026-10-10), base move, `Get/SetParam` and `ReleaseData` have host tests only. The other SoCs have no IVS gap. All IVS functions are in the full list below.

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|

<details><summary>All 17 rows of this area (0 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|
| ► `IVS_CreateBaseMoveInterface` [1] | aud | dev | dev | dev | ? | dev | host | aud | r† |
| ► `IVS_CreateChn` [2] | aud | dev | dev | dev | ? | dev | dev | aud | r† t |
| ► `IVS_CreateGroup` [3] | aud | dev | dev | dev | ? | dev | dev | aud | r† t |
| ► `IVS_CreateMoveInterface` [4] | aud | dev | dev | dev | ? | dev | dev | aud | r† t |
| ► `IVS_DestroyBaseMoveInterface` [5] | aud | dev | dev | dev | ? | dev | host | aud | r† |
| ► `IVS_DestroyChn` [6] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| ► `IVS_DestroyGroup` [7] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| ► `IVS_DestroyMoveInterface` [8] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| ► `IVS_Get/SetParam` [9] | aud | dev | dev | dev | ? | dev | host | aud | r† t |
| ► `IVS_GetResult` [10] | aud | dev | dev | dev | ? | dev | dev | aud | r† t |
| ► `IVS_PollingResult` [11] | aud | dev | dev | dev | ? | dev | dev | aud | r† t |
| ► `IVS_RegisterChn` [12] | aud | dev | dev | dev | ? | dev | dev | aud | r† t |
| ► `IVS_ReleaseData` [13] | aud | aud | aud | aud | ? | aud | host | aud | r† |
| ► `IVS_ReleaseResult` [14] | aud | dev | dev | dev | ? | dev | dev | aud | r† t |
| ► `IVS_StartRecvPic` [15] | aud | dev | dev | dev | ? | dev | dev | aud | r† t |
| ► `IVS_StopRecvPic` [16] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |
| ► `IVS_UnRegisterChn` [17] | aud | dev | dev | dev | ? | dev | dev | aud | p† r† t |

Notes:

1. `IVS_CreateBaseMoveInterface`: audit: userspace implementation
2. `IVS_CreateChn`: audit: userspace implementation
3. `IVS_CreateGroup`: audit: userspace implementation; all: two IVS groups as every vendor libimp (CreateGroup checks group < 2); group 1 move/base-move device-tested on T20/T21
4. `IVS_CreateMoveInterface`: audit: userspace implementation
5. `IVS_DestroyBaseMoveInterface`: audit: userspace implementation
6. `IVS_DestroyChn`: audit: userspace implementation
7. `IVS_DestroyGroup`: audit: userspace implementation
8. `IVS_DestroyMoveInterface`: audit: userspace implementation
9. `IVS_Get/SetParam`: audit: userspace implementation
10. `IVS_GetResult`: audit: userspace implementation
11. `IVS_PollingResult`: audit: userspace implementation
12. `IVS_RegisterChn`: audit: userspace implementation
13. `IVS_ReleaseData`: audit: userspace implementation
14. `IVS_ReleaseResult`: audit: userspace implementation
15. `IVS_StartRecvPic`: audit: userspace implementation
16. `IVS_StopRecvPic`: audit: userspace implementation
17. `IVS_UnRegisterChn`: audit: userspace implementation

</details>

### Audio

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|
| `AI_DisableAlgo` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | miss | – |
| `AI_DisableGetRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `AI_DisableHs` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | – |
| `AI_EnableAlgo` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | miss | – |
| `AI_EnableGetRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `AI_EnableHs` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | – |
| `AI_Get/SetDigitalGain` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `AI_GetFrameAndRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `AO_DisableAlgo` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | miss | – |
| `AO_EnableAlgo` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | miss | – |
| `AO_Get/SetDigitalGain` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `Audio_Select_Codec` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `DMIC_*` (20 functions) [1] | n.a. | n.a. | n.a. | n.a. | aud | aud | err | err | r† |
| ► `DMIC_DisableAecRefFrame` [2] | n.a. | n.a. | n.a. | n.a. | aud | n.a. | err | err | r† |

Notes:

1. `DMIC_*` (20 functions): functions: Disable, DisableAec, DisableChn, Enable, EnableAec, EnableAecRefFrame, EnableChn, Get/SetChnParam, GetFrame, GetFrameAndRef, Get/SetGain, Get/SetPubAttr, Get/SetVol, PollingFrame, ReleaseFrame, SetUserInfo. T41: ENOTSUP stub (returns -1); T30 1.0.5: the T31 DMIC implementation is compiled into the T30 build. The 21 DMIC functions of libimp 1.0.5 disassemble like those of T31 1.1.6 (buffer sizing aside) and the T30 oss2 /dev/dsp driver has the same DMIC ioctls (SNDCTL_EXT_ENABLE_DMIC_STREAM, DMIC_GET_AI_STREAM 0x300, DMIC_SET_DMIC_GAIN 0x200); difference: one microphone per device on T30 (SetPubAttr chnCnt 1). Host test with a fake /dev/dsp (attribute rules, ioctl sequence, frames, gain, volume); the AEC calls are only compared, not run; no T30 device, and the audio driver needs its DMIC option
2. `DMIC_DisableAecRefFrame`: T41: ENOTSUP stub (returns -1); T30 1.0.5: as DMIC_*; disassembly identical to T31 apart from offsets, not run on the host

<details><summary>All 66 rows of this area (14 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|
| `ADEC_ReleaseDecoder` [53] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| `AENC_* / ADEC_*` (17 functions) [3] | host | dev | dev | dev | ? | dev | ? | host | r† |
| `AENC_ReleaseEncoder` [54] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | – |
| `AI_Disable` [4] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_DisableAec` [5] | aud | aud | aud | aud | ? | aud | ? | aud | see note |
| `AI_DisableAecRefFrame` [6] | aud | aud | aud | aud | ? | aud | ? | aud | r† |
| `AI_DisableAgc` [7] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_DisableAlgo` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | miss | – |
| `AI_DisableChn` [8] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_DisableGetRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `AI_DisableHpf` [9] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_DisableHs` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | – |
| `AI_DisableNs` [10] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_Enable` [11] | dev | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_EnableAec` [12] | dev | dev | dev | dev | ? | dev | ? | aud | see note |
| `AI_EnableAecRefFrame` [13] | aud | aud | aud | aud | ? | aud | ? | aud | r† |
| `AI_EnableAgc` [14] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_EnableAlgo` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | miss | – |
| `AI_EnableChn` [15] | dev | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_EnableGetRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `AI_EnableHpf` [16] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_EnableHs` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | miss | miss | – |
| `AI_EnableNs` [17] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_Get/SetAlcGain` [18] | n.a. | n.a. | dev | n.a. | n.a. | dev | n.a. | n.a. | t |
| `AI_Get/SetChnParam` [19] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_Get/SetDigitalGain` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `AI_Get/SetGain` [20] | host | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_Get/SetPubAttr` [21] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_Get/SetVol` [22] | host | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_GetFrame` [23] | dev | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_GetFrameAndRaw` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| `AI_GetFrameAndRef` [24] | aud | aud | aud | aud | ? | aud | ? | aud | r† |
| `AI_PollingFrame` [25] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_ReleaseFrame` [26] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `AI_SetAgcMode` [27] | n.a. | n.a. | n.a. | n.a. | n.a. | dev | n.a. | n.a. | – |
| `AI_SetHpfCoFrequency` [28] | n.a. | n.a. | n.a. | host | n.a. | host | ? | host | p† r† |
| `AI_SetVolMute` [29] | aud | dev | dev | dev | ? | dev | ? | host | r† |
| `AI_Set_WebrtcProfileIni_Path` [30] | n.a. | n.a. | n.a. | dev | n.a. | dev | ? | aud | r† |
| `AO_CacheSwitch` [31] | host | host | host | dev | ? | host | ? | host | r† |
| `AO_ClearChnBuf` [32] | aud | aud | aud | dev | ? | aud | ? | aud | see note |
| `AO_Disable` [33] | aud | aud | aud | dev | ? | aud | ? | aud | see note |
| `AO_DisableAgc` [34] | aud | aud | aud | dev | ? | aud | ? | aud | r† |
| `AO_DisableAlgo` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | miss | – |
| `AO_DisableChn` [35] | aud | aud | aud | dev | ? | aud | ? | aud | see note |
| `AO_DisableHpf` [36] | aud | aud | aud | dev | ? | aud | ? | aud | r† |
| `AO_Enable` [37] | dev | dev | dev | dev | ? | dev | ? | aud | see note |
| `AO_EnableAgc` [38] | aud | aud | aud | dev | ? | aud | ? | aud | r† |
| `AO_EnableAlgo` | n.a. | n.a. | n.a. | dev | n.a. | n.a. | n.a. | miss | – |
| `AO_EnableChn` [39] | dev | dev | dev | dev | ? | dev | ? | aud | see note |
| `AO_EnableHpf` [40] | aud | aud | aud | dev | ? | aud | ? | aud | r† |
| `AO_FlushChnBuf` [41] | aud | aud | aud | dev | ? | aud | ? | aud | see note |
| `AO_Get/SetDigitalGain` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | miss | – |
| `AO_Get/SetGain` [42] | aud | aud | aud | dev | ? | aud | ? | aud | see note |
| `AO_Get/SetPubAttr` [43] | aud | aud | aud | dev | ? | aud | ? | aud | see note |
| `AO_Get/SetVol` [44] | host | host | host | dev | ? | host | ? | aud | see note |
| `AO_PauseChn` [45] | aud | aud | aud | dev | ? | aud | ? | aud | r† |
| `AO_QueryChnStat` [46] | aud | aud | aud | dev | ? | aud | ? | aud | r† |
| `AO_ResumeChn` [47] | aud | aud | aud | dev | ? | aud | ? | aud | r† |
| `AO_SendFrame` [48] | dev | dev | dev | dev | ? | dev | ? | aud | see note |
| `AO_SetHpfCoFrequency` [49] | n.a. | n.a. | n.a. | dev | n.a. | aud | ? | aud | p† r† |
| `AO_SetVolMute` [50] | aud | aud | aud | dev | ? | aud | ? | host | p† r† |
| `AO_Soft_Mute` [51] | aud | aud | aud | dev | ? | aud | ? | host | r† |
| `AO_Soft_UNMute` [52] | aud | aud | aud | dev | ? | aud | ? | host | r† |
| `Audio_Select_Codec` | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | miss | – |
| ► `DMIC_*` (20 functions) [1] | n.a. | n.a. | n.a. | n.a. | aud | aud | err | err | r† |
| ► `DMIC_DisableAecRefFrame` [2] | n.a. | n.a. | n.a. | n.a. | aud | n.a. | err | err | r† |

Notes:

53. `ADEC_ReleaseDecoder`: T10/T20 (4 builds each) and T31 (28 builds): declared in the SDK header, exported by none of the vendor libimp builds (nm)
3. `AENC_* / ADEC_*` (17 functions): functions: ClearChnBuf, CreateChn, DestroyChn, GetStream, PollingStream, RegisterDecoder, ReleaseStream, SendStream, UnRegisterDecoder, CreateChn, DestroyChn, GetStream, PollingStream, RegisterEncoder, ReleaseStream, SendFrame, UnRegisterEncoder. T10+T20+T21+T41: claude/aenc-adec-all (in agg-25): shared software codecs; device encode test open
54. `AENC_ReleaseEncoder`: T10/T20 (4 builds each) and T31 (28 builds): declared in the SDK header, exported by none of the vendor libimp builds (nm)
4. `AI_Disable`: audit: reaches the driver/kernel
5. `AI_DisableAec`: audit: reaches the driver/kernel; used by: T10/T20/T31: raptor†, timps; T21/T23/T41: raptor†
6. `AI_DisableAecRefFrame`: audit: reaches the driver/kernel
7. `AI_DisableAgc`: audit: userspace implementation
8. `AI_DisableChn`: audit: reaches the driver/kernel; userspace implementation
9. `AI_DisableHpf`: audit: userspace implementation
10. `AI_DisableNs`: audit: userspace implementation
11. `AI_Enable`: audit: reaches the driver/kernel; T10/T20/T21/T23/T31: device-tested 2026-10-10 (mono 16 kHz recording, AEC test)
12. `AI_EnableAec`: audit: reaches the driver/kernel; used by: T10/T20/T31: raptor†, timps; T21/T23/T41: raptor†; T10/T20/T21/T23/T31: device-tested 2026-10-10 (mono 16 kHz recording, AEC test)
13. `AI_EnableAecRefFrame`: audit: reaches the driver/kernel
14. `AI_EnableAgc`: audit: userspace implementation
15. `AI_EnableChn`: audit: userspace implementation; T10/T20/T21/T23/T31: device-tested 2026-10-10 (mono 16 kHz recording, AEC test)
16. `AI_EnableHpf`: audit: userspace implementation
17. `AI_EnableNs`: audit: userspace implementation
18. `AI_Get/SetAlcGain`: audit: reaches the driver/kernel; userspace implementation
19. `AI_Get/SetChnParam`: audit: userspace implementation
20. `AI_Get/SetGain`: audit: reaches the driver/kernel; userspace implementation; Set dev, Get host on T10/T20/T21/T31 where the cell was aud (2026-10-10 AEC test: SetVol/SetGain called, getter not checked); T21: a microphone gain above 0 drives the noise floor up (hardware, open and vendor stack alike)
21. `AI_Get/SetPubAttr`: audit: reaches the driver/kernel; userspace implementation
22. `AI_Get/SetVol`: audit: reaches the driver/kernel; userspace implementation; Set dev, Get host on T10/T20/T21/T31 where the cell was aud (2026-10-10 AEC test: SetVol/SetGain called, getter not checked)
23. `AI_GetFrame`: audit: reaches the driver/kernel; userspace implementation; T10/T20/T21/T23/T31: device-tested 2026-10-10 (mono 16 kHz recording, AEC test)
24. `AI_GetFrameAndRef`: audit: reaches the driver/kernel; userspace implementation
25. `AI_PollingFrame`: audit: userspace implementation
26. `AI_ReleaseFrame`: audit: userspace implementation
27. `AI_SetAgcMode`: audit: userspace implementation
28. `AI_SetHpfCoFrequency`: T23+T31 (+T41, untested): takes effect: IMP_AI_EnableHpf designs the filter for the cut-off like the vendor and hands a float biquad to libaudioProcess-neo (beyond the original library); apitest roundtrip PASS, no audio test (shared test cameras)
29. `AI_SetVolMute`: audit: reaches the driver/kernel; userspace implementation; T41: software mute as in the vendor library (no ioctl); API-level test only 2026-10-10 (rc 0, invalid argument -1), the T41 test board has no audio hardware
30. `AI_Set_WebrtcProfileIni_Path`: audit: userspace implementation
31. `AO_CacheSwitch`: all: implemented with vendor semantics, default off (OPENIMP_AO_CACHE=1 = vendor default on); quiet device test open (audio output only on T31)
32. `AO_ClearChnBuf`: audit: reaches the driver/kernel; used by: T10/T20/T31: raptor†, timps; T21/T23/T41: raptor†
33. `AO_Disable`: audit: reaches the driver/kernel; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†
34. `AO_DisableAgc`: audit: userspace implementation
35. `AO_DisableChn`: audit: userspace implementation; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†
36. `AO_DisableHpf`: audit: userspace implementation
37. `AO_Enable`: audit: reaches the driver/kernel; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†; T10/T20/T21/T23/T31: device-tested 2026-10-10 (known signal played at several volumes and recorded back; AEC test); T41: no audio hardware on the test board (no microphone, no speaker), results are API-level only
38. `AO_EnableAgc`: audit: userspace implementation
39. `AO_EnableChn`: audit: userspace implementation; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†; T10/T20/T21/T23/T31: device-tested 2026-10-10 (known signal played at several volumes and recorded back; AEC test); T41: no audio hardware on the test board (no microphone, no speaker), results are API-level only
40. `AO_EnableHpf`: audit: userspace implementation
41. `AO_FlushChnBuf`: audit: reaches the driver/kernel; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†
42. `AO_Get/SetGain`: audit: reaches the driver/kernel; userspace implementation; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†
43. `AO_Get/SetPubAttr`: audit: reaches the driver/kernel; userspace implementation; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†
44. `AO_Get/SetVol`: audit: reaches the driver/kernel; userspace implementation; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†; Set dev, Get host on T10/T20/T21/T31 where the cell was aud (2026-10-10 AEC test: SetVol/SetGain called, getter not checked)
45. `AO_PauseChn`: audit: userspace implementation
46. `AO_QueryChnStat`: audit: userspace implementation
47. `AO_ResumeChn`: audit: userspace implementation
48. `AO_SendFrame`: audit: reaches the driver/kernel; used by: T10/T20/T31: prudynt†, raptor†, timps; T21/T23/T41: prudynt†, raptor†; T10/T20/T21/T23/T31: device-tested 2026-10-10 (known signal played at several volumes and recorded back; AEC test); T41: no audio hardware on the test board (no microphone, no speaker), results are API-level only
49. `AO_SetHpfCoFrequency`: audit: userspace implementation
50. `AO_SetVolMute`: audit: reaches the driver/kernel; userspace implementation; T41: software mute as in the vendor library (no ioctl); API-level test only 2026-10-10 (rc 0, invalid argument -1), the T41 test board has no audio hardware
51. `AO_Soft_Mute`: audit: reaches the driver/kernel; userspace implementation; T41: software mute as in the vendor library (no ioctl); API-level test only 2026-10-10 (rc 0, invalid argument -1), the T41 test board has no audio hardware
52. `AO_Soft_UNMute`: audit: reaches the driver/kernel; userspace implementation; T41: software mute as in the vendor library (no ioctl); API-level test only 2026-10-10 (rc 0, invalid argument -1), the T41 test board has no audio hardware
1. `DMIC_*` (20 functions): functions: Disable, DisableAec, DisableChn, Enable, EnableAec, EnableAecRefFrame, EnableChn, Get/SetChnParam, GetFrame, GetFrameAndRef, Get/SetGain, Get/SetPubAttr, Get/SetVol, PollingFrame, ReleaseFrame, SetUserInfo. T41: ENOTSUP stub (returns -1); T30 1.0.5: the T31 DMIC implementation is compiled into the T30 build. The 21 DMIC functions of libimp 1.0.5 disassemble like those of T31 1.1.6 (buffer sizing aside) and the T30 oss2 /dev/dsp driver has the same DMIC ioctls (SNDCTL_EXT_ENABLE_DMIC_STREAM, DMIC_GET_AI_STREAM 0x300, DMIC_SET_DMIC_GAIN 0x200); difference: one microphone per device on T30 (SetPubAttr chnCnt 1). Host test with a fake /dev/dsp (attribute rules, ioctl sequence, frames, gain, volume); the AEC calls are only compared, not run; no T30 device, and the audio driver needs its DMIC option
2. `DMIC_DisableAecRefFrame`: T41: ENOTSUP stub (returns -1); T30 1.0.5: as DMIC_*; disassembly identical to T31 apart from offsets, not run on the host

</details>

### System / sysutils / log

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|
| `Log_Get_Option` | aud | aud | aud | aud | n.a. | aud | miss | miss | – |
| `Log_Set_Option` [64] | host | host | host | host | n.a. | host | miss | miss | – |
| `SU_Base_SetWkupMode` [1] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | err | – |
| `SU_Base_Shutdown` [2] | err | err | err | err | ? | err | ? | err | – |
| `SU_Battery_GetCapacity` [48] | n.a. | n.a. | n.a. | n.a. | ? | n.a. | n.a. | miss | – |
| `SU_Battery_GetEvent` [49] | n.a. | n.a. | n.a. | n.a. | ? | n.a. | n.a. | miss | – |
| `SU_Battery_GetStatus` [50] | n.a. | n.a. | n.a. | n.a. | ? | n.a. | n.a. | miss | – |
| `SU_Battery_GetVoltageUV` [51] | n.a. | n.a. | n.a. | n.a. | ? | n.a. | n.a. | miss | – |
| `System_MemPoolRequest` [3] | n.a. | n.a. | n.a. | dev | n.a. | dev | miss | miss | – |

Notes:

1. `SU_Base_SetWkupMode`: T41: writes the mode number to /sys/power/state; neo PR #1 covers related struct overflows, unmerged
2. `SU_Base_Shutdown`: all: kill(1,SIGCHLD) does not power off busybox init; fix in neo PR #1 (SIGUSR2), unmerged; no streamer uses it; class d: lives in libsysutils (not OpenIMP); the vendor does sync() + kill(1, SIGCHLD) exactly like it, busybox init ignores SIGCHLD
3. `System_MemPoolRequest`: T23+T31: real memory pools (claude/t23t31-cacheonly, not in agg-25)
48. `SU_Battery_GetCapacity`: T10 (4 builds, 3.9.0/3.12.0), T20 (4 builds, 3.9.0/3.12.0), T21 (4 builds, 1.0.33), T23 (8 builds) and T31 (28 builds): declared in the SDK header, exported by no vendor libsysutils (nm of every build); a vendor-stack application cannot link it either; OpenIMP/libsysutils match that (no battery driver on these SoCs)
49. `SU_Battery_GetEvent`: T10 (4 builds, 3.9.0/3.12.0), T20 (4 builds, 3.9.0/3.12.0), T21 (4 builds, 1.0.33), T23 (8 builds) and T31 (28 builds): declared in the SDK header, exported by no vendor libsysutils (nm of every build); a vendor-stack application cannot link it either; OpenIMP/libsysutils match that (no battery driver on these SoCs)
50. `SU_Battery_GetStatus`: T10 (4 builds, 3.9.0/3.12.0), T20 (4 builds, 3.9.0/3.12.0), T21 (4 builds, 1.0.33), T23 (8 builds) and T31 (28 builds): declared in the SDK header, exported by no vendor libsysutils (nm of every build); a vendor-stack application cannot link it either; OpenIMP/libsysutils match that (no battery driver on these SoCs)
51. `SU_Battery_GetVoltageUV`: T10 (4 builds, 3.9.0/3.12.0), T20 (4 builds, 3.9.0/3.12.0), T21 (4 builds, 1.0.33), T23 (8 builds) and T31 (28 builds): declared in the SDK header, exported by no vendor libsysutils (nm of every build); a vendor-stack application cannot link it either; OpenIMP/libsysutils match that (no battery driver on these SoCs)

<details><summary>All 53 rows of this area (9 with a gap)</summary>

*Codes: **dev** device-tested · **host** host tests only · **aud** connected per static audit, no device test · **no-op** vendor does nothing · **cache** value only stored · **stub** returns 0, no effect · **err** fails or known defect · **miss** not exported by OpenIMP · **n.a.** not in that SoC's vendor API · **?** unknown · **►** streamer uses it, gap · **†** streamer use from source only · **[n]** note below the table · Used by: **p** prudynt, **r** raptor, **t** timps (a long list is given in the note).*

| Vendor function | T10 | T20 | T21 | T23 | T30 | T31 | T40 | T41 | Used by |
|---|---|---|---|---|---|---|---|---|---|
| `Log_Get_Option` | aud | aud | aud | aud | n.a. | aud | miss | miss | – |
| `Log_Set_Option` [64] | host | host | host | host | n.a. | host | miss | miss | – |
| `SU_ADC_DisableChn` [4] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_ADC_EnableChn` [5] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_ADC_Exit` [6] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_ADC_GetChnValue` [7] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_Base_DisableAlarm` [8] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_Base_EnableAlarm` [9] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_Base_Get/SetAlarm` [10] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_Base_Get/SetTime` [11] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_Base_GetDevID` [12] | aud | dev | dev | dev | ? | dev | ? | aud | – |
| `SU_Base_GetModelNumber` [13] | aud | dev | dev | dev | ? | dev | ? | aud | – |
| `SU_Base_GetVersion` [14] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† |
| `SU_Base_PollingAlarm` [15] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_Base_Raw2SUTime` [16] | aud | dev | dev | dev | ? | dev | ? | aud | – |
| `SU_Base_Reboot` [17] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_Base_SUTime2Raw` [18] | aud | dev | dev | dev | ? | dev | ? | aud | – |
| `SU_Base_SetWkupMode` [1] | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | n.a. | err | – |
| `SU_Base_Shutdown` [2] | err | err | err | err | ? | err | ? | err | – |
| `SU_Base_Suspend` [19] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_Battery_GetCapacity` [48] | n.a. | n.a. | n.a. | n.a. | ? | n.a. | n.a. | miss | – |
| `SU_Battery_GetEvent` [49] | n.a. | n.a. | n.a. | n.a. | ? | n.a. | n.a. | miss | – |
| `SU_Battery_GetStatus` [50] | n.a. | n.a. | n.a. | n.a. | ? | n.a. | n.a. | miss | – |
| `SU_Battery_GetVoltageUV` [51] | n.a. | n.a. | n.a. | n.a. | ? | n.a. | n.a. | miss | – |
| `SU_CIPHER_ConfigHandle` [20] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_CIPHER_CreateHandle` [21] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_CIPHER_DES_Exit` [22] | n.a. | n.a. | n.a. | n.a. | n.a. | aud | ? | aud | – |
| `SU_CIPHER_DES_Init` [23] | n.a. | n.a. | n.a. | n.a. | n.a. | aud | ? | aud | – |
| `SU_CIPHER_DES_Test` [24] | n.a. | n.a. | n.a. | n.a. | n.a. | aud | ? | aud | – |
| `SU_CIPHER_Decrypt` [25] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_CIPHER_DestroyHandle` [26] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_CIPHER_Encrypt` [27] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_CIPHER_Exit` [28] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_CIPHER_Init` [29] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_Key_CloseEvent` [30] | aud | dev | dev | dev | ? | dev | ? | aud | – |
| `SU_Key_DisableEvent` [31] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_Key_EnableEvent` [32] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_Key_OpenEvent` [33] | aud | dev | dev | dev | ? | dev | ? | aud | – |
| `SU_Key_ReadEvent` [34] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `SU_LED_Command` [35] | aud | aud | aud | aud | ? | aud | ? | aud | – |
| `System_Bind` [36] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `System_Exit` [37] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `System_GetBindbyDest` [38] | aud | dev | dev | dev | ? | dev | ? | aud | r† |
| `System_GetCPUInfo` [39] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† |
| `System_GetTimeStamp` [40] | aud | dev | dev | dev | ? | dev | ? | aud | r† t |
| `System_GetVersion` [41] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `System_Init` [42] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `System_MemPoolFree` [43] | n.a. | n.a. | n.a. | n.a. | n.a. | dev | n.a. | n.a. | – |
| `System_MemPoolRequest` [3] | n.a. | n.a. | n.a. | dev | n.a. | dev | miss | miss | – |
| `System_ReadReg32` [44] | aud | dev | dev | dev | ? | dev | ? | aud | r† |
| `System_RebaseTimeStamp` [45] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† |
| `System_UnBind` [46] | aud | dev | dev | dev | ? | dev | ? | aud | p† r† t |
| `System_WriteReg32` [47] | aud | aud | aud | aud | ? | aud | ? | aud | r† |

Notes:

4. `SU_ADC_DisableChn`: T20+T21+T23+T31: apitest N/A on the test boards, no ADC node (/dev/ingenic_adc_aux_0 or /dev/jz_adc_aux_0; the vendor libsysutils opens the same nodes)
5. `SU_ADC_EnableChn`: T20+T21+T23+T31: apitest N/A on the test boards, no ADC node (/dev/ingenic_adc_aux_0 or /dev/jz_adc_aux_0; the vendor libsysutils opens the same nodes)
6. `SU_ADC_Exit`: T20+T21+T23+T31: apitest N/A on the test boards, no ADC node (/dev/ingenic_adc_aux_0 or /dev/jz_adc_aux_0; the vendor libsysutils opens the same nodes)
7. `SU_ADC_GetChnValue`: T20+T21+T23+T31: apitest N/A on the test boards, no ADC node (/dev/ingenic_adc_aux_0 or /dev/jz_adc_aux_0; the vendor libsysutils opens the same nodes)
8. `SU_Base_DisableAlarm`: T20+T21+T23+T31: apitest N/A on the test boards, no /dev/rtc0 (the vendor libsysutils opens the same node)
9. `SU_Base_EnableAlarm`: T20+T21+T23+T31: apitest N/A on the test boards, no /dev/rtc0 (the vendor libsysutils opens the same node)
10. `SU_Base_Get/SetAlarm`: T20+T21+T23+T31: apitest N/A on the test boards, no /dev/rtc0 (the vendor libsysutils opens the same node)
11. `SU_Base_Get/SetTime`: T20+T21+T23+T31: apitest N/A on the test boards, no /dev/rtc0 (the vendor libsysutils opens the same node)
12. `SU_Base_GetDevID`: audit: sysfs/ioctl/syscall path
13. `SU_Base_GetModelNumber`: audit: sysfs/ioctl/syscall path
14. `SU_Base_GetVersion`: audit: sysfs/ioctl/syscall path
15. `SU_Base_PollingAlarm`: T20+T21+T23+T31: apitest N/A on the test boards, no /dev/rtc0 (the vendor libsysutils opens the same node)
16. `SU_Base_Raw2SUTime`: audit: sysfs/ioctl/syscall path
17. `SU_Base_Reboot`: audit: sysfs/ioctl/syscall path
18. `SU_Base_SUTime2Raw`: audit: sysfs/ioctl/syscall path
1. `SU_Base_SetWkupMode`: T41: writes the mode number to /sys/power/state; neo PR #1 covers related struct overflows, unmerged
2. `SU_Base_Shutdown`: all: kill(1,SIGCHLD) does not power off busybox init; fix in neo PR #1 (SIGUSR2), unmerged; no streamer uses it; class d: lives in libsysutils (not OpenIMP); the vendor does sync() + kill(1, SIGCHLD) exactly like it, busybox init ignores SIGCHLD
19. `SU_Base_Suspend`: audit: sysfs/ioctl/syscall path
48. `SU_Battery_GetCapacity`: T10 (4 builds, 3.9.0/3.12.0), T20 (4 builds, 3.9.0/3.12.0), T21 (4 builds, 1.0.33), T23 (8 builds) and T31 (28 builds): declared in the SDK header, exported by no vendor libsysutils (nm of every build); a vendor-stack application cannot link it either; OpenIMP/libsysutils match that (no battery driver on these SoCs)
49. `SU_Battery_GetEvent`: T10 (4 builds, 3.9.0/3.12.0), T20 (4 builds, 3.9.0/3.12.0), T21 (4 builds, 1.0.33), T23 (8 builds) and T31 (28 builds): declared in the SDK header, exported by no vendor libsysutils (nm of every build); a vendor-stack application cannot link it either; OpenIMP/libsysutils match that (no battery driver on these SoCs)
50. `SU_Battery_GetStatus`: T10 (4 builds, 3.9.0/3.12.0), T20 (4 builds, 3.9.0/3.12.0), T21 (4 builds, 1.0.33), T23 (8 builds) and T31 (28 builds): declared in the SDK header, exported by no vendor libsysutils (nm of every build); a vendor-stack application cannot link it either; OpenIMP/libsysutils match that (no battery driver on these SoCs)
51. `SU_Battery_GetVoltageUV`: T10 (4 builds, 3.9.0/3.12.0), T20 (4 builds, 3.9.0/3.12.0), T21 (4 builds, 1.0.33), T23 (8 builds) and T31 (28 builds): declared in the SDK header, exported by no vendor libsysutils (nm of every build); a vendor-stack application cannot link it either; OpenIMP/libsysutils match that (no battery driver on these SoCs)
20. `SU_CIPHER_ConfigHandle`: audit: sysfs/ioctl/syscall path
21. `SU_CIPHER_CreateHandle`: audit: sysfs/ioctl/syscall path
22. `SU_CIPHER_DES_Exit`: audit: sysfs/ioctl/syscall path
23. `SU_CIPHER_DES_Init`: audit: sysfs/ioctl/syscall path
24. `SU_CIPHER_DES_Test`: audit: sysfs/ioctl/syscall path
25. `SU_CIPHER_Decrypt`: audit: sysfs/ioctl/syscall path
26. `SU_CIPHER_DestroyHandle`: audit: sysfs/ioctl/syscall path
27. `SU_CIPHER_Encrypt`: audit: sysfs/ioctl/syscall path
28. `SU_CIPHER_Exit`: audit: sysfs/ioctl/syscall path
29. `SU_CIPHER_Init`: audit: sysfs/ioctl/syscall path
30. `SU_Key_CloseEvent`: audit: sysfs/ioctl/syscall path
31. `SU_Key_DisableEvent`: audit: sysfs/ioctl/syscall path
32. `SU_Key_EnableEvent`: audit: sysfs/ioctl/syscall path
33. `SU_Key_OpenEvent`: audit: sysfs/ioctl/syscall path
34. `SU_Key_ReadEvent`: audit: sysfs/ioctl/syscall path
35. `SU_LED_Command`: audit: sysfs/ioctl/syscall path
36. `System_Bind`: audit: userspace implementation
37. `System_Exit`: audit: reaches the driver/kernel; userspace implementation
38. `System_GetBindbyDest`: audit: userspace implementation
39. `System_GetCPUInfo`: audit: reaches the driver/kernel; userspace implementation
40. `System_GetTimeStamp`: audit: userspace implementation
41. `System_GetVersion`: audit: userspace implementation
42. `System_Init`: audit: reaches the driver/kernel
43. `System_MemPoolFree`: T31: real memory pools (claude/t23t31-cacheonly, not in agg-25)
3. `System_MemPoolRequest`: T23+T31: real memory pools (claude/t23t31-cacheonly, not in agg-25)
44. `System_ReadReg32`: audit: reaches the driver/kernel
45. `System_RebaseTimeStamp`: audit: userspace implementation
46. `System_UnBind`: audit: userspace implementation
47. `System_WriteReg32`: audit: reaches the driver/kernel

</details>

### Notes

- **T21 imgfx findings without a row of their own (2026-10-06 update):** the 2026-10-05 run found no change for Sinter/Temper strength, Sepia, Vivid and the ISP bypass bits. Sepia has worked since 2026-10-06 (beyond vendor, see `ISP_Tuning_Get/SetColorfxMode`); in the later imgfx run on a T21 camera B/W, sepia, negative, vivid, the CSC modes and the sinter/temper/DPC strengths changed the picture. Brightness, antifog and scene mode are vendor no-ops on T21 except that brightness now acts through the AE target (beyond vendor). ISP flip and `SetWB` auto gave a magenta picture in imgfx on T21 (timps' own flip path is fine): open, not a missing function; to be repeated in daylight.
- **OSD, behaviour rather than a missing function:** on T21 the IPU blend is ineffective for about 2 s after a wake from idle; `claude/t21-osd-first-jpeg` (in agg-26) withholds JPEG frames without a confirmed overlay, so the first snapshot after a start now has the OSD (device-tested 2026-10-05, first snapshot about 2 s later). The original stack shows the same missing first-snapshot OSD; the root cause (IPU/OSD group/clock) is not found. T21 OSD can still appear late after a start (2-3 min seen with several starts): unverified cause.
- **IVS:** no vendor IVS function is marked missing or incomplete in the audit. (The IVS GetParam/SetParam overflow found on 2026-10-05 is in raptor-hal, not in OpenIMP.)
- **Not determined:** whether the T41 vendor handles `SetScalerLv`/`SetMaskBlock` (the open driver has no handler); T10 device effect of every T20/T21 fix; the _Sec/MultiCamera_ variants after the T23 fixes; vendor semantics of absolute-QP ROI under CBR on T20; T41 DMIC, ISP-OSD and I2D functions were not looked at again after the audit (all still error/missing); the T10/T20/T21 `GetChnRcAttr`-style missing encoder calls were not re-checked.
- **Release-candidate imgfx run (2026-10-06):** it ran in the dark (night mode), so cases without a clear luma effect cannot be judged. The colour functions of T10/T20/T21/T31 must be repeated with daylight or room light before they are called dev; only the T21 brightness batch and the T21/T31 batches taken with light are used above.
- **SU time/alarm/ADC:** apitest shows N/A on the test boards because the nodes are missing (`/dev/rtc0`, the ADC nodes); the vendor libsysutils opens the same nodes, so this is a board property, not a gap.
