# T40XP status

The T40 implementation is a clean, OEM-free `libimp.so` for the
T40XP/GC4653 Raptor pipeline. It is maintained under `src/t40/`; the existing
flat and ported T31 builds remain unchanged.

## Current live gate

The validated camera is a Wyze Cam v3 Pro running Thingino with the stock
`tx_isp_t40` and `sensor_gc4653_t40` kernel modules. RVD, RIC, and RSD run
with only the OpenIMP library mapped; `tx_isp_t40_recovered` is not loaded.
The T40 userspace implementation does not contain a GC4653 register table,
gain LUT, or fixed sensor dimensions. It consumes the dimensions configured
by the application and relies on the loaded stock sensor module and tuning
blob for sensor-specific policy.

On the current Raptor firmware, stream 0 is published as:

```text
rtsp://CAMERA_IP/ch0
```

The live profiles cover:

- `raptor-live-640.conf`: 640x360 at 30 fps
- `raptor-live-720.conf`: 1280x720 at 30 fps
- `raptor-live-1080.conf`: 1920x1080 at 15 fps
- `raptor-live-1440.conf`: 2560x1440 at 15 fps

All profiles use H.264 High and derive capture allocation, NV12 plane
addresses, AVPU pitches, command geometry, SPS cropping/timing, rate-control
grid, reference storage, and stream-buffer sizing from the active
configuration.

OpenIMP passes the configured tuning-bin path to the stock ISP before sensor
selection. The stock ISP firmware owns AE, AWB, demosaic, gamma, denoise, CCM,
lens shading, and sensor-specific exposure/gain translation. OpenIMP forwards
public tuning controls through the OEM T40 descriptor ABI; it contains no
GC4653 register table or analog-gain LUT.

The capture buffer is returned to the stock frame channel immediately after
AVPU submission. This matches the OEM ownership window and prevents a 30 fps
channel from collapsing to approximately 15 fps, which otherwise starves the
ISP temporal filters and produces a visibly grainy image.

The July 30 live gate recorded:

- 1280x720: 302 decoder-clean frames in 10.033 seconds, with no steady-state
  ISP overflow
- 2560x1440: 152 decoder-clean frames in 10.067 seconds at 3.76 Mbit/s for a
  configured 6 Mbit/s VBR ceiling

## Milestones

- P0: standalone System state and lifecycle
- P1: stock ISP/sensor/FrameSource lifecycle and NV12 capture
- P2: open AVPU/DMA encoder lifecycle
- P3: Raptor video/audio/control import coverage with no OEM `libimp.so`

The library exports every `IMP_*` symbol imported by the target RVD/RAD
binaries. Audio effects are dynamically resolved from the separately
maintained `libaudioProcess-neo`; logging uses the target's
`ingenic-system-libs-neo` package.

## Known gaps

- Extended decoder-clean and reconnect soak testing remains part of the
  encoder gate.
- AE/AWB and image-quality processing run in the stock ISP firmware using the
  selected sensor tuning blob. OpenIMP forwards tuning requests without
  duplicating sensor policy.
- Still `ENOTSUP`: DMIC (21 calls).  Findings from the vendor 1.3.1
  disassembly (host work, 2026-10-10):
  - **DMIC**: no DMIC hardware on the test boards (the Eufy T8416 and the Wyze
    Cam v3 Pro dts only carry the generic `dmic-pc` pinmux group, no audio/mic
    node).  The vendor T40 DMIC (`__dmic_dev_deinit`, ioctl 0x4004506b, ...) is
    not the T31 oss2 interface of `src/t31/openimp_t31_dmic.c`; a port cannot be
    tested here, so the 21 calls stay `ENOTSUP` until a board with a DMIC exists.
- **ISP-drawn OSD** (`src/t40/openimp_t40_isp_osd.c`, 2026-10-10, device-tested
  on the Eufy T40XP with the vendor tx-isp-t40 driver): `IMP_ISP_Tuning_
  {SetOsdPoolSize,CreateOsdRgn,SetOsdRgnAttr,GetOsdRgnAttr,ShowOsdRgn,
  DestroyOsdRgn}`, the `IMP_OSD_*_ISP` calls behind them (vendor `isp_osd.c`)
  and `IMP_ISP_Set/GetSingleOSDAttr`, rebuilt from the vendor libimp 1.3.0
  (camera) disassembly, which matches 1.3.1 address for address here:
  - `chn` is the frame-source channel 0..5 (sensor `chn/3`), 8 handles per
    channel handed out from a FIFO free list; `CreateOsdRgn` needs `NULL`
    attributes.  The handle must equal `pic_num`; `chx` is the ISP (msca)
    output channel.
  - `SetOsdRgnAttr` checks the region against the frame-source size, copies
    `stride*height` bytes into rmem (a size change needs destroy+create),
    stores the copy address in `osd_image` (`GetOsdRgnAttr` returns it, vendor:
    channels 0/1 only) and clears `osd_enable`; nothing reaches the driver.
  - `ShowOsdRgn` sets `osd_enable`, switches off a later region that overlaps
    an earlier shown one (`IspOsdAdjust_Pic`) and sends every created region of
    the channel: tuning ioctl 0xc0105436, control 0x080000a2
    (`IMAGE_TUNING_CID_SINGLE_OSD_ATTR`), the 44-byte `IMPISPSingleOSDAttr`
    with the physical picture address.  The kernel (`tisp_s_osd_single_pic`)
    writes it into the msca shadow registers of `chx`: the picture is drawn
    after the scaler, in that output only.  `DestroyOsdRgn` hides a shown
    region first.
  - Differences: one rmem block per region instead of the vendor pool (the
    pool size still limits the total; vendor default 128 bytes, set it with
    `IMP_ISP_Tuning_SetOsdPoolSize`), cache write-back after the copy, and a
    region without picture is not sent (the vendor sends a meaningless
    address).  `IMP_ISP_Set/GetOSDAttr` (0x080000a1) are not implemented.
  - Device test (`osdtest`, both libraries, same calls; IR-cut day): a
    128x64 BGRA picture per channel (chn0/chx0 at 64,64 in 3840x2160,
    chn1/chx1 at 32,32 in 640x360) and a green `SetMask` cover per channel,
    shown at 1.3 s and hidden at 3.3 s of a 5 s encode.  OpenIMP: the
    shown-minus-hidden difference is exactly the requested rectangles in both
    streams (no stray pixels; the chx0 picture does not appear in the sub
    stream), colours as written, nothing left after hiding.  Crops vendor vs
    OpenIMP: SSIM picture 0.985 / 0.981, cover 0.9998 / 0.9998.  Decode clean
    (ffmpeg `-err_detect aggressive` and VA-API, 99 + 99 frames each).
    Host test: `tests/t40/isp_osd_test.c`.
- `IMP_ISP_Tuning_Get/SetMask` are implemented from the vendor disassembly
  (288-byte `IMPISPMASKAttr`, control 0x08000074, per-input copy, RGB->YUV for
  enabled RGB blocks, Get restores type and RGB from the copy).  Device-tested
  2026-10-10 with the ISP-OSD test above (cover at the right place in both
  streams, same as the vendor library).  The DumpMask constants are not
  readable in the disassembly, the T31 BT.601 values are used; the green
  cover came out as (0,229,0) with both libraries.
- `IMP_ISP_Tuning_Get{Brightness,Contrast,Sharpness,Saturation}` return the
  per-input byte cache written by the Set call and 0, never asking the driver
  (vendor; a driver get of these controls failed, the old getter returned -1).
- FrameSource `frame->pixfmt` is 0 on T40 (vendor value, NV12 and 0 are treated
  alike by the encoder).  `frame->size` stays the driver `sizeimage`
  (640x360: 353280): the vendor reports `w*h*3/2` (345600), but the buffer
  holds the chroma plane after the 16-aligned luma rows and the encoder
  (cache invalidation, HW JPEG `src_need`) relies on the real size, so only
  the field differs and is left as is.
- `IMP_Encoder_SetbufshareChn`: the vendor T40 stores the share channel in the
  channel record (+984) and returns 0; nothing reads it back.  OpenIMP accepts
  and drops it (vendor bound is channel < 13, OpenIMP has 8 channels).
- The `P1_INNER` phase markers on stderr need `OPENIMP_DEBUG_TRACE=1`.

## T40 on the T41 code paths (host tests; device run on cam-K 2026-10-10 below)

Compared with the vendor libimp 1.3.1 disassembly (the camera runs 1.3.0, which
is not available here; its OSD attribute is also larger than the 1.3.1 header):

- **OSD** (`IMP_OSD_*`, 18 calls): the IPU OSD of `src/t23/openimp_t23_osd.c`.
  The vendor `ipu_osd`, `ipu_init`, `_ipu_set_osdx_para` and `osd_update` are
  instruction-identical to T41 1.2.6.  `IMPOSDRgnAttr` is 1772 bytes in the
  1.3.1/en header and 1948 in the vendor libimp 1.3.1 (`colType[64]`); only the
  shared 1772-byte prefix is read and written, mosaic regions are not drawn.
- **IVS** (`IMP_IVS_*`, 18 calls) and `IMP_FrameSource_SnapFrame`: the T31/T41
  framework; the vendor IVS code is the T41 code, the T40 frame record has the
  pool at 0x20 instead of `direct_phyAddr`.
- **ISP tuning**: `GetSensorAttr` (0x33), `Get/SetGammaAttr` (0x25),
  `Get/SetModuleControl` (0x72), `Get/SetAutoZoom` (0x77), `Get/SetWdrOutputMode`
  (0x54), `Get/SetModule_Ratio` (0xa4), `Get/SetAeScenceAttr` (0x24),
  `Awb_Get/SetRgbCoefft` (0x9b, T41: 0x98), `IMP_ISP_SetScalerLv` (0xa6) and
  `Get/SetCCMAttr` (0x80, 40-byte payload with the vendor sign/13-bit
  conversion).  Control numbers are the same as on T41; the tuning ioctl is
  0xc0105436 on T40 (T41: 0xc0105435).

## First OpenIMP device run (cam-K, 2026-10-10)

T40XP board, vendor kernel driver `tx_isp_t40`, vendor libimp 1.3.0; OpenIMP is
loaded through `LD_LIBRARY_PATH`. Every result is OpenIMP versus the vendor library.

PASS: `AddSensor`/`EnableSensor`; FrameSource ch0 3840x2160 and ch1 640x360 with
`GetFrame` (timestamps monotonic, non-zero); H.264 on ch0 and ch1 (10 s, clean strict
decode); OSD PIC and COVER (OpenIMP draws COVER on NV12, the vendor library fails
with "COVER cannot support this format"); IVS move (20 s); `FrameSource_SnapFrame`;
JPEG on ch1 (software JPEG since 257ba9d); ISP `GetSensorAttr`, Gamma get, CCM get,
ModuleControl get, `AeScenceAttr` get/set; `GetCameraInputMode`.

Fixed during the run: the stream timestamp was 0 (a5d9b03); the JPEG was a grey
placeholder (257ba9d).

Open at the time (fixed afterwards: CBR rate control and H.265, see below and
`T40_HEVC.md`; the `GetBrightness` cache, pixel format 0 and the `P1_INNER` trace
gate, see the list above): CBR delivered only about 20 % of the target bit rate;
H.265 was not supported by OpenIMP T40 yet; `GetBrightness` returned -1; the
pixel-format enum was reported as 10 (vendor 0); the frame size is 16-line aligned
(353280 B vs 345600 B, kept on purpose); `P1_INNER` traces always went to stderr;
JPEG at 4K not tested (out of memory in the
test process, 60 MB RAM).

Vendor note: `GetAeExpList` hung the vendor kernel driver. The magenta cast seen with
the streaming application stopped was the IR cut left open, not the library.

Only the functions listed under PASS are marked device-tested in
`FEATURE_MATRIX.md`; everything else on T40 stays host or unknown.

## Encoder command list versus the vendor (H.264)

The AVC command words, EP1 (lambda table and scaling list, 8 KB) and EP2 now
match the vendor libimp 1.3.1 word by word on the Eufy T40XP for CBR, VBR and
FixQP at 3840x2160 and 640x360 (addresses aside; only cmd[0x1b] bit 16 still
differs on some P pictures).  Values are in `src/t40/t40_vendor_cmd.h`,
checked by `tests/t40/vendor_cmd_test.c`:

- IDR period `uGopLength * uMaxSameSenceCnt` (the vendor header formula; GOP
  40 x 2 gives an IDR every 80 pictures, not every 40);
- iInitialQP -1: the vendor's bits-per-pixel table (4K 6 Mbit/s: QP 39, not 26);
- FixQP: no hardware RC (cmd[9] bit 16, cmd[0x14..0x18], EP3 off), EP2 QP range
  0..51 and the fixed-QP lambda table per picture type (IDR: intra lane only);
- hardware RC targets cmd[0x15] = 1.9 x bit/s per group (I), 5/7 of it (P),
  cmd[0x16] = per-frame maximum per group, cmd[0x17] min max(min, max - 32) and
  the IDR QP in bits 23:16; cmd[0x12] bits 27:24 from 512 / 64-pixel columns;
  picture numbers in cmd[0x0c..0x11]; no picture size in cmd[0x64/0x65];
- SetDefaultParam CBR/VBR: QP 15..48, eRcOptions 1 (as vendor and T31).

`OPENIMP_T40_{FIXQP_NOHWRC,FIXQP_LDA,CMD12,POC,HWRC_TARGET}=0` restore the old
words one by one for comparisons.

### Frame delay cache costs memory bandwidth

While `IMP_FrameSource_SetMaxDelay` is above 0 (and `SetDelay` > 0), every
`GetFrame` copies the whole frame into the delay cache
(`fs_delay_store` in `src/t40/openimp_p1.c`; the capture buffer itself goes back
to the driver at once).  That is one full-frame `memcpy` per frame on the
consumer thread, for example about 12 MB per frame at 4K NV12 (3840x2160x1.5),
about 3 MB at 1080p NV12, plus up to `delay` cached frames of RAM per channel
(`delay` <= 100).  At 4K and 25 fps that is roughly 300 MB/s of extra copy
traffic.  With `SetMaxDelay(0)` (default) there is no cost.  Enable it only
on channels where `GetTimedFrame` is really used and keep `delay` small.
Host-tested only, not measured on a camera.

## Build

```sh
./build-t40.sh
```

The script discovers the Thingino T40 1.3.1 headers, cross-compiles
`build/t40/libimp.so`, checks RVD/RAD import coverage when those binaries are
available, and rejects any dependency on OEM `libimp.so`.
