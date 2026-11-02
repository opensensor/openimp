# imgfx - one picture per image-changing IMP function (OpenIMP libimp)

**Folded into apitest (2026-10-26):** the source is `tools/apitest/at_picture.c`, there is no `imgfx_<soc>` binary any more. Run it as
`apitest_<soc> SENSOR I2C W H picture OUTDIR [case-filter]` (the options and output below are unchanged; `apitest_<soc> ... picture`
replaces `imgfx_<soc> ...`). `imgfx_collage.py` stays here.

Device test for T10, T20, T21, T23, T31, T41. It sets one function at a time,
waits (300 ms + 3 frames, longer for AE/AWB cases), saves the 640x360 channel-1
frame as raw NV12, prints one result line and restores the value it read
before, so cases are independent. No encoder is used (raw frames from
`IMP_FrameSource_GetFrame`), so the per-SoC encoder API differences do not matter.

    imgfx SENSOR I2C_ADDR W H OUTDIR [case-filter]
    imgfx --list                     # case names of this build

* FS ch0 = sensor size W x H, FS ch1 = 640x360 scaled; ch1 is what is saved.
* `case-filter` = substring of the case name (`awb`, `csc`, `fcrop`, ...).
  `NN` in the file name is the stable table index, filtered runs add to
  `OUTDIR/summary-<SoC>.txt` (append).
* Output per case: `OUTDIR/<NN>-<case>.nv12` and a line
  `[R] <case> set=<ret> get=<readback>` or `[R] <case> N/A`
  (function not exported by that libimp). `base` is taken first, `base-end`
  last (noise/drift reference for the collage).
* Env: `IMGFX_MODE=night` (IR-cut open / IR on: runs the whole test in night
  mode and restores to it), `IMGFX_SETTLE_MS=n` extra settle per case,
  `IMGFX_SKIP_RISKY=1` skips ISP bypass and FPS cases (they run last).
* Stop the streamer first (timps/prudynt: `/etc/init.d/S95timps stop`), keep
  the scene static and lit. Pictures are ~337 KB each, 70-100 per run: use a
  directory with 40+ MB free (camera `/tmp` is RAM; with little free RAM use
  filters in batches or an SD/NFS path). On small T41 boards a full /tmp
  starves the system and ends in a watchdog reset (see docs/T41_STATUS.md,
  "Test tools and camera RAM"); tools that save data use `tools/save_guard.h`.
* Case results are meant to be read with the host script (below); `set=0` only
  means the call returned 0.

## Cases (what the vendor API of that SoC offers)

base, base-end; ISP flip h/v/hv (+ SetHVFLIP enum on T23/T31, SetISPHVflip on
T10/T20, sensor flip on T23/T41); front crop mid50/topleft50 (T10/T20/T21 use
the OpenIMP crop extension, T23/T31 vendor `SetFrontCrop`), FS ch1 scaler crop
(`fs1-crop-*`), ePTZ AutoZoom ch1 (T23/T31/T41); brightness, contrast,
saturation, sharpness low/high, hue and `bcsh-combo` (T23/T31/T41); gamma
steep/linear; AE compensation low/high (T10/T20/T23/T31; T41 via
`AeScenceAttr` incl. HLC/BLC); manual exposure short/long (`SetExpr`, T41
`AeExprInfo`), `aeattr-*` manual AE/gain (T23/T31); max again/dgain limits;
AWB auto / manual R high / manual B high / presets (daylight, cloudy,
incandescent, fluorescent, twilight, shade, warm fluorescent), `awbct-*` and
`wbalgo-*` (T23/T31); CCM swap-rb/mono (T23/T31/T41); CSC modes 0..4 (T23/T31
vendor, T10/T20/T21 OpenIMP extension, T41 `ISPCSCAttr`); WDR/DRC
(T10/T20 `SetWDRAttr`+`SetRawDRC`, T21 `SetRawDRC`+`SetDRC_Strength`, T23/T31
`EnableDRC`+`SetDRC_Strength`, T41 module ratio); defog (T23/T31 enable +
strength, T10/T20/T21 `SetAntiFogAttr`, T41 ratio); sinter 2D NR and temper 3D
NR 0/255; dpc 0/255; hilight depress and backlight comp; privacy mask
(T23/T31 `SetMask`, T41 `SetMaskBlock`); scaler level; module bypass bits
(`bypass-*`, T21/T23/T31/T41); colorfx and scene mode (T10/T20/T21); mesh
shading scale (T10/T20); anti-flicker off/50/60; running mode day/night; ISP
bypass and FPS 15/10 (risky, last, picture only, rate not measured).

Restore notes: where the vendor API has no getter the default is assumed
(printed in `get=`): sinter/temper/dpc 128 (OpenIMP getters are used when
exported), antifog off, mesh shading M, defog/DRC enable via enable calls,
sensor flip 0.

## On-camera commands (binary = this directory, libimp = the camera's own)

| camera | binary | command |
|---|---|---|
| T10 secuplug .31 | imgfx_t10 | `imgfx_t10 jxh42 0x30 1280 720 /tmp/imgfx` |
| T20 wyze campan1 .163 | imgfx_t20 | `imgfx_t20 jxf22 0x40 1920 1080 /tmp/imgfx` |
| T21 victure pc420 .27 | imgfx_t21 | `imgfx_t21 jxf23 0x40 1920 1080 /tmp/imgfx` |
| T23 jooan a6m .30 | imgfx_t23 | `imgfx_t23 sc1a4t 0x30 1280 720 /tmp/imgfx` |
| T23 galayou y4 .28 | imgfx_t23 | `imgfx_t23 sc2336 0x30 1920 1080 /tmp/imgfx` |
| T31 wuuk y0510 .21 | imgfx_t31 | `imgfx_t31 sc4336p 0x30 2560 1440 /tmp/imgfx` |
| T41 vanhua t55a .179 | imgfx_t41 | `imgfx_t41 gc5603 0x31 2880 1620 /tmp/imgfx` |

(sc1a4t addr 0x30 and gc5603 0x31 / 2880x1620 are from the sensor drivers in
ingenic-sdk; the other values as given.)

Typical run:

    scp imgfx_t31 root@<cam-ip>:/tmp/
    ssh root@<cam-ip>
      /etc/init.d/S95timps stop
      mkdir -p /tmp/imgfx && cd /tmp && ./imgfx_t31 sc4336p 0x30 2560 1440 /tmp/imgfx | tee /tmp/imgfx/console.txt
      # night: IMGFX_MODE=night ./imgfx_t31 ...
    scp -r root@<cam-ip>:/tmp/imgfx ./cam-t31
    python3 imgfx_collage.py ./cam-t31          # jpg/, contact-N.jpg, report.txt

Reboot or restart the streamer afterwards (`/etc/init.d/S95timps start`).

## Host script

`imgfx_collage.py OUTDIR...` (python3 + PIL): converts every NV12 to JPEG,
writes labelled contact sheets (case + set ret + diff vs base) and
`report.txt` with mean abs diff vs base per channel (Y/U/V). Flags:
`SUSPECT-unconnected` = set returned 0 but the picture did not change,
`RET<0`, `N/A`. Threshold = max(1.0, 2.5 x noise) from base vs base-end.
Cases where no visible change is expected (anti-flicker, fps, awb-auto,
hilight depress, ...) are not flagged as suspect.

## Build

`./build.sh` cross-builds all six binaries (toolchain and libimp from
`$THINGINO_OUT/<cam>/`, vendor headers from
`$HDR_DIR/<SoC>/...`). Every vendor function is declared
weak, one source builds everywhere; a function missing in the camera's libimp
prints `N/A` at run time.

## T41

The T41 pipeline init follows the vendor T41 flow (`IMP_ISP_AddSensor(num,
info)`, `EnableSensor(num, info)`, sensor_id/vi/mclk left 0 like timps) and the
new tuning API (`num` + pointer arguments). It builds and links, but the init
has not been run on a T41 here: first run may need a sensor-info fix.
