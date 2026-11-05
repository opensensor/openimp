# apitest - ONE device test app per SoC for every vendor IMP/SU function (OpenIMP libimp)

One source, one binary `apitest_<soc>`, one CLI: areas `sys,fs,enc,osd,ivs,audio,isp,su,cases`, `picture` (the former imgfx cases,
`apitest_<soc> SENSOR I2C W H picture OUTDIR [filter]`), `t40dev` (T40 only, the former t40_devtest), `--list` (functions), `--areas`,
`--cases` (case names of area cases), `--json FILE` (one JSON report of the run: function -> PASS/FAIL/RT/NA/SKIP + detail + area;
`results2json.py --report` and `merge_evidence.py` consume it). Completeness: `check_coverage.py` (part of `make check`) requires
every vendor function of every gate SoC (`tools/export-check/vendor-<SoC>.txt`) to have a case here or an entry with a reason in
`untestable.yaml` (categories sound, reboot, power, hardware, wedges-isp, no-signature, own-topology, not-in-soc-api, backlog). T40 and T41 are strict (`STRICT` in `check_coverage.py`): any function still in category backlog fails the check. T40 ISP-OSD controls (`IMP_OSD_ShowRgn_ISP`, `IMP_ISP_Get/SetOSDAttr`, `IMP_ISP_Get/SetSingleOSDAttr`) oops `tx_isp_t40` in our QEMU guest, so case `isposdapi` skips them unless `APITEST_T40_ISPOSD_CTL=1` (not run on the Eufy, see audit APITEST_COV_T4X_1028).

Device test for T10, T20, T21, T23, T31 and T41, sibling of `tools/imgfx` (which
covers the image-changing ISP functions). It calls the vendor API of the SoC
(structs from `ingenic-headers`) against the camera's own libimp and prints one
line per function:

    [A] <function> ret=<r> <PASS|FAIL|N/A|SKIP> <reason>

* `PASS`/`FAIL`: the call and a check of its effect (read back, stream parsed, ...).
* `N/A`: the function is declared in the vendor header but not exported by that
  libimp (every vendor function is weak, one source builds everywhere); also
  `ENOTSUP` from `IMP_Encoder_GetChnEvalInfo`.
* `SKIP`: deliberately not run (reason on the line): writes hardware/RTC/LED,
  reboots, blocks, or needs a topology of its own.
* `FAIL OVERWRITE`: the function wrote outside the vendor struct of this SoC.
  Every out buffer sits in an arena with 64 guard bytes on both sides.
* `[T]` lines: area headers and a summary (`PASS/FAIL/N/A/SKIP` per area,
  distinct functions exercised). A teardown that takes more than 8 s (`APITEST_TEARDOWN_WATCH_MS`, off with `APITEST_NO_TEARDOWN_WATCH=1`) writes one block (step, FrameSource state, `g_fs_lock` holder, per-thread state) to stderr and `teardown-stall-<pid>.txt` next to the `--json` file, and keeps waiting (#198). A watchdog ends the run with `[E] watchdog:`
  (naming the last function) after 60 s without a result line.

Stop the streamer first (`/etc/init.d/S95timps stop`); the test owns the ISP,
FrameSource and encoder. RAM use is small (FS ch1 640x360 x3 buffers, encoder
640x360 @ 15 fps, 512 KB test buffer, 64 KB mem pool); nothing is written to
`/tmp`. The scene may be anything, but static and lit gives meaningful luma and
motion results.

    apitest SENSOR I2C_ADDR W H [areas]    areas = comma list of sys,fs,enc,osd,ivs,audio,isp,su
    apitest --list                         vendor functions of this build's table (host-runnable builds only)

FS ch0 = sensor size W x H (as in imgfx), FS ch1 = 640x360 scaled; all encoders
and IVS/OSD use ch1. Env: `APITEST_MODE=night` (ISP running mode night),
`APITEST_AIDEV=n` (audio input device, default 1, T23 0), `APITEST_QEMU=1` (set by `qemu_run.py`) turns the picture verdicts of `customalgo_t41` (AE and AWB effect) into N/A (the QEMU test pattern does not respond to the exposure) and a refused `SetSensorFPS` of the QEMU sensor model into N/A, `APITEST_AO=1` enables the (off by default, no-sound rule)
AO return-code checks.

## What is exercised

| area | functions |
|---|---|
| sys | Init/Exit, GetVersion, GetCPUInfo, GetTimeStamp, RebaseTimeStamp (+restore), ReadReg32 (CPM base), MemPoolRequest/Free (T23/T31/T41), Bind/UnBind/GetBindbyDest (FS->ENC and FS->OSD->ENC, FS->IVS) |
| fs | Create/Destroy/Enable/Disable (+cycle), Get/SetChnAttr round trip, FrameDepth, FifoAttr, Delay/MaxDelay, Pool, GetFrame/ReleaseFrame (size, luma, timestamps, rate), SnapFrame, GetTimedFrame, ChnStatQuery/SetChnRotate (T31), Direct mode (T23), I2dAttr/GetFrameEx (T41) |
| enc | H.264 (all), H.265 (T21/T23/T31/T41), JPEG: Group/Chn create/register/start/stop, Polling/GetStream/ReleaseStream, Query; streams parsed: H.264 SPS (resolution/profile/level), PPS, IDR, H.265 VPS/SPS/PPS/IDR, JPEG SOI/SOF/EOI; RequestIDR, Get/SetChnFrmRate (+measured rate), RC attr / BitRate, GOP (SetGOPSize or GopAttr/GopLength), QP bounds, ROI (old: SetChnROI, T41: RoiAttr), MaxPictureSize, FrmUsedMode, SuperFrame, Denoise, HSkip, Demask, Color2Grey, MbRC, ChangeRef, Fisheye, QpgMode, H264/H265TransCfg, InsertUserData (SEI seen), GDR/framelossthd/Crop/InitQP (T23), MaxStreamCnt, FlushStream, GetFd, GetChnEncType, Pool, StreamBufSize, PollingModuleStream, ChnAveBitrate, EvalInfo, bufshare (JPEG on the H.264 buffer), JpegeQl live (JPEG size must follow the table), AvpuJpegQp (T41), software JPEG/YUV encoder (Vbm*, InputJpege, Yuv*; T23/T41) |
| osd | SetPoolSize, Create/Destroy group, regions RECT / PIC (BGRA) / COVER: Create/Register/SetRgnAttr/GetRgnAttr/SetGrpRgnAttr/GetGrpRgnAttr/Show/Start/Stop/UnRegister/Destroy, UpdateRgnAttrData, SetRgnAttrWithTimestamp, T23/T41: GetRegionLuma, Rgn*_Query, SetMosaic, T41 SetGroupCallback; encoder keeps running (check) |
| ivs | Group/Chn create/register/start/stop, move interface (Create/Destroy, PollingResult, GetResult, ReleaseResult, Get/SetParam), base-move interface |
| audio | AI only on the microphone: Pub attr, Enable/EnableChn, ChnParam, PollingFrame/GetFrame/ReleaseFrame (rms, timestamps), Vol/VolMute/Gain/AlcGain/DigitalGain, Ns, Hpf (+cut-off), Agc (+mode), Algo/Hs, GetFrameAndRaw (T41); AENC/ADEC G.711A software round trip of a real mic frame. AO: return codes only (muted, `IMP_AO_SendFrame` is never called, nothing is played). AEC functions are `SKIP` (speaker path) |
| isp | non-image getters and round trips: sensor attr/register, FPS, running mode, anti-flicker, max again/dgain, Expr, total gain, EV, integration time, WB / WB statistics, AWB cluster/ct/trend, AE luma/min/state/attr/targets/hist/zone/weight, AF hist/zone/metrics/weight, AWB hist/zone, blc, frame drop, default bin path; T41: statistics/exposure-info/convergence getters |
| cases | targeted cases for functions that had no case (`at_cases.c`, `APITEST_CASES=a,b` selects, names in the file): Enable/DisableMovestate, Get/SetSinter/TemperDnsAttr (+ effect check), SetTemperDnsCtl, SetISPProcess (ENABLE only), SetFWFreeze, SetAeFreeze, GetAeHist_Origin, Get/SetAutoZoom, `meshscale` (T10/T20: the four lens-shading scales read back from register 0x380 bits 2..4 and the driver parameter), T23: `maskblock_t23` / `drawblock_t23` (ISP mask and draw blocks set, read back, measured in the picture, switched off), `ispcover1_t23` (single-camera `IMP_OSD_Set/GetRgnAttr_ISP`), `isppicupd_t23` (ISP picture region and its update; measured at the mirrored place when `msca_flip_state` has the ISP flip bits), `secprobe_t23` (explicit only: read-only probe of the second-sensor getters; the `*_Sec` getters that answer -1 without a second sensor are reported N/A), Get/SetFrameDrop, WaitFrame(Done), SetVideoDrop, Awb Cwf shift, sensor chip-id read/write-back, Log option, `t41pass` (T41: SetYuvAlign argument rules, Get/SetMaskBlock, Ae scene/expr and Awb attribute round trips), `inject_t40` (T40/T41: `FrameSource_ExternInject_*` + `DequeueBuffer/QueueBuffer`, an injected NV12 channel bound to its own H.264 channel), `aeexplist` (T41 and T40), `t30pass` (T30 gate lines `t30pass_inttime`, `t30pass_waitframe` + `t30pass_waitframe_timeout`, `t30pass_aecomp`, `t30pass_antifog`, `t30pass_rawdrc` + `t30pass_rawdrc_strength`, `t30pass_daynight`: return codes and read-backs, PASS/FAIL/N/A rule and reason on each line, no picture claim; labels, not function names, so they never overrule the picture verdicts of `inttime`/`aecomp`/`rawdrc`; `t30pass_csc` = Get/SetCsc_Attr, explicit only), `t30pass2` (T30, explicit only: Get/SetWB manual round trip, SetTemperStrength, Get/Set round trips of AeHist / AeWeight / AwbHist / AwbWeight / AfHist / Awb RgbCoefft / SinterDnsAttr / TemperDnsAttr / AE ROI (Get, Set the same value, Get must be byte-identical), sensor chip id 0x3107/0x3108 and a write-back of 0x3e03, Enable/DisableMovestate, ColorfxMode B/W round trip, SceneMode, SetVideoDrop, SetFWFreeze on/off; labels `t30pass_<item>`, mapped in `results2json.py` T30PASS_ALIAS), and (explicit only) `stalesnap`: first SnapFrame/GetFrame after a 2/10/30 s pause must show the picture of NOW (brightness switched during the pause), and the re-checks HiLightDepress (0..10), IntegrationTime (gain caps pinned), SceneMode, ColorfxMode (AUTO between modes). Each restores the original value and checks the picture recovered |
| su | libsysutils (dlopen): GetModelNumber, GetVersion, GetDevID, Get/SetTime (same value), SUTime2Raw/Raw2SUTime, Set/Get/DisableAlarm, ADC, battery (where the SDK has it), Key open/close. Never called: Shutdown, Reboot, Suspend, EnableAlarm, PollingAlarm, LED, cipher |

## On-camera commands (binary = this directory, libimp = the camera's own)

Same cameras and sensor arguments as imgfx:

| camera | binary | command |
|---|---|---|
| T10 secuplug .31 | apitest_t10 | `apitest_t10 jxh42 0x30 1280 720` |
| T20 wyze campan1 .163 | apitest_t20 | `apitest_t20 jxf22 0x40 1920 1080` |
| T21 victure pc420 .27 | apitest_t21 | `apitest_t21 jxf23 0x40 1920 1080` |
| T23 jooan a6m .30 | apitest_t23 | `apitest_t23 sc1a4t 0x30 1280 720` |
| T23 galayou y4 .28 | apitest_t23 | `apitest_t23 sc2336 0x30 1920 1080` |
| T31 wuuk y0510 .21 | apitest_t31 | `apitest_t31 sc4336p 0x30 2560 1440` |
| T41 vanhua t55a .179 | apitest_t41 | `apitest_t41 gc5603 0x31 2880 1620` |

Typical run:

    scp apitest_t31 root@<cam-ip>:/tmp/
    ssh root@<cam-ip>
      /etc/init.d/S95timps stop
      /tmp/apitest_t31 sc4336p 0x30 2560 1440 | tee /tmp/apitest.txt     # about 2 minutes
      # single areas: apitest_t31 sc4336p 0x30 2560 1440 enc,osd
    scp root@<cam-ip>:/tmp/apitest.txt cam-t31-apitest.txt
    grep -E ' (FAIL|N/A) ' cam-t31-apitest.txt          # what needs attention

Reboot or restart the streamer afterwards (`/etc/init.d/S95timps start`). Exit
status is 1 when any line is FAIL.

## Build

`tools/apitest/build.sh [T10|T20|T21|T23|T31|T40|T41]` cross-builds `apitest_<soc>` and `tools/imgfx/imgfx_<soc>` (and
`t40_devtest` for T40) into `tools/apitest/build/`. Vendor headers come from `timps/include`; the toolchain and a libimp
to link against from `thingino-out/<cam>-agg46-*` (override with `HDR`, `TO`, `GEN`). The binaries use the libimp of the
camera they run on. T40 is built as the T41 API family (`-DPLATFORM_T41 -DAPITEST_T40`, T40 1.3.1 headers); imgfx has no
T40 build (the T40 image functions are covered by `tests/t40/devtest/t40_devtest.c`). `gen_have.sh` regenerates
`apitest_have.h`/`apitest_weak.h` (functions declared per SoC); `gen_rt.py` regenerates `at_isp_rt.c` (same-value Get/Set
round trips of the ISP pairs that `at_isp.c` does not round-trip itself).

## Evidence runs (`run_cam.sh`, `results2json.py`)

    tools/apitest/run_cam.sh <cam-ip> T41 [outdir]

Per camera, under the cam lock for the whole run: preflight (sensor, libimp md5, init script, dmesg baseline), stops the
streamer, copies the binaries to `/tmp/at` (< 1 MB), runs `apitest` (areas sys,fs,enc,osd,ivs,audio,isp,su; audio is
microphone only, `APITEST_AO=0`: no AO call at all), `imgfx` with `IMGFX_STATS=1` (the frame is measured in memory: mean
Y/U/V, detail energy, 8x8 luma grid; no picture is written or leaves the camera; `RISKY=1` adds the bypass/FPS cases),
on T40 `t40_devtest` (phases `T40_MASK`, default `cdff`), then removes `/tmp/at`, restarts the streamer, checks dmesg and
writes `result.json` (per evidence function key: PASS / FAIL / PART / SKIP / NA with the detail line). It refuses to run
while `audit/ab-t41/USER_IN_CELLAR` exists. Not done, by design: speaker/AO/AEC, flash, reboot, module load/unload, light.

Register references per picture case: `IMGFX_HOOK=cmd` runs `cmd` after each case has settled, with the case name in
`IMGFX_CASE`. `tools/regpeek.c` (static build: `mipsel-linux-gcc -O2 -static -o regpeek tools/regpeek.c`) reads 32-bit
registers through `/dev/mem` and prints one `[REG] <case> off=value ...` line, for example on T30
`IMGFX_HOOK="/tmp/at/regpeek 0x13380000 0x18 0x40 0x300-0x30c 0x540-0x570"` (Apical core at `0x13380000`). This works on
the vendor stack too, where the T30 vendor library returns no frames to the picture run. `IMGFX_DEPTH_LATE=1` sets the
frame depth after `EnableChn` instead of before (diagnosis of the vendor frame path); a `NO PICTURE` case also logs the
last `GetFrame` return.

## Rule: every new case runs in QEMU first (user decision 2026-10-26)

A new case never goes to a camera before it ran in our QEMU for that SoC (`$S/qemu`, READMEs `README-T2x.md`, `README-T31.md`, `README-T40.md`,
`README-T30.md`; images = agg47 flash copies, never the originals; free ports, own monitor socket, kill by PID):

    tools/apitest/qemu_run.py PORT SOC SENSOR I2C W H build/apitest_<soc> [case ...] --outdir D

It pushes the binary over the console (gzip+base64), stops the streamer and runs one case per `APITEST_CASES=<case> apitest ... cases`, each
followed by a health check (plain `fs` area: a fresh FrameSource EnableChn must work and frames must flow; dmesg without Oops).  QEMU proves
return codes, set/get round trips, oops/crash and that frames still flow afterwards; it cannot prove picture effects.  A camera then proves only
the picture effects, and only for cases that were QEMU-safe.  Anything that crashes the guest or stops the frame flow in QEMU is a bug and
never runs on a camera.  A SoC without a camera (T30) gets evidence level `qemu` (matrix 🔧+, "API round trip in QEMU, no camera").
History: `IMP_ISP_Tuning_SetISPProcess(DISABLE)` (= ISP bypass for this driver) wedged the T10 camera until reboot (agg47-cases); the case now
writes ENABLE only (`APITEST_PROCESS_BYPASS=1` runs the wedging value, QEMU only).

## On the camera (thingino package `openimp`, option `BR2_PACKAGE_OPENIMP_APITEST`)

`make apitest PLATFORM=<SoC> TOOLCHAIN_PREFIX=<host/bin/mipsel-linux> APITEST_HEADERS=<ingenic-headers> [DESTDIR=<dir>]` builds
`build/<soc>/openimp-apitest` against the libimp just built (`tools/apitest/build-device.sh`; T10: `PLATFORM=T20 APITEST_SOC=T10`) and,
with `DESTDIR`, installs it as `$(DESTDIR)$(PREFIX)/bin/openimp-apitest`. In thingino: enable *OpenIMP device test app* (default off),
build, flash, then on the camera (streamer stopped, nothing else using the ISP, no sound is played, nothing is written to flash):

    /etc/init.d/S95timps stop            # or the streamer of the image
    openimp-apitest --json /tmp/apitest.json SENSOR I2C_ADDR W H sys,fs,enc,osd,ivs,audio,isp,su,cases
    # export APITEST_AO=0 first: no speaker call at all
    scp root@CAMERA:/tmp/apitest.json .  # then rm /tmp/apitest.json, restart the streamer

The sensor arguments are those of the table above (see `/proc/jz/sensor/name`). Send `apitest.json` (one report: function ->
PASS/FAIL/RT/NA/SKIP + detail) with the issue or pull request; `results2json.py --report` and `merge_evidence.py` consume it.

## Closing comments for vendor-vs-open comparisons

Every closing or evidence comment that rests on a camera comparison states which library ran, as one line:
`which libimp ran: path + md5 + log markers` (OpenIMP logs `[KernelIF]`/`[VBM]`, the vendor library `i264e` lines; the installed
`/usr/lib/libimp.so` of a test camera is OpenIMP, a vendor run loads the vendor library via `LD_LIBRARY_PATH`). A run whose log
contradicts the expected library is invalid, not a result.
