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
- Still `ENOTSUP`: DMIC (21 calls), the ISP-drawn OSD (`IMP_ISP_Tuning_CreateOsdRgn`,
  `DestroyOsdRgn`, `SetOsdRgnAttr`, `ShowOsdRgn`) and `IMP_ISP_Tuning_Get/SetMask`
  (the vendor library keeps a 288-byte copy per input and converts the colour with
  `IMP_ISP_Tuning_DumpMask`; the payload the T40 kernel expects is not settled).
  `OSD_REG_ISP_*` regions are accepted and not drawn.

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

Open at the time (CBR rate control and H.265 were fixed afterwards, see below and
`T40_HEVC.md`): CBR delivered only about 20 % of the target bit rate; H.265 was
not supported by OpenIMP T40 yet; `GetBrightness` returns -1; the pixel-format enum is
reported as 10 (vendor 0); the frame size is 16-line aligned (353280 B vs 345600 B);
`P1_INNER` traces always go to stderr; JPEG at 4K not tested (out of memory in the
test process, 60 MB RAM).

Vendor note: `GetAeExpList` hung the vendor kernel driver. The magenta cast seen with
timps stopped was the IR cut left open, not the library.

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

## Build

```sh
./build-t40.sh
```

The script discovers the Thingino T40 1.3.1 headers, cross-compiles
`build/t40/libimp.so`, checks RVD/RAD import coverage when those binaries are
available, and rejects any dependency on OEM `libimp.so`.
