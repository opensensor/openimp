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
- Still `ENOTSUP`: DMIC (21 calls) and the ISP-drawn OSD
  (`IMP_ISP_Tuning_CreateOsdRgn`, `DestroyOsdRgn`, `SetOsdRgnAttr`, `ShowOsdRgn`;
  `OSD_REG_ISP_*` regions are accepted and not drawn).  Findings from the vendor
  1.3.1 disassembly (host work, 2026-10-10):
  - **DMIC**: no DMIC hardware on the test boards (the Eufy T8416 and the Wyze
    Cam v3 Pro dts only carry the generic `dmic-pc` pinmux group, no audio/mic
    node).  The vendor T40 DMIC (`__dmic_dev_deinit`, ioctl 0x4004506b, ...) is
    not the T31 oss2 interface of `src/t31/openimp_t31_dmic.c`; a port cannot be
    tested here, so the 21 calls stay `ENOTSUP` until a board with a DMIC exists.
  - **ISP-OSD**: the five `IMP_ISP_Tuning_*OsdRgn` exports are tail calls into
    `IMP_OSD_{SetPoolSize,CreateRgn,SetRgnAttr_PicISP,GetRgnAttr_ISPPic,ShowRgn,
    DestroyRgn}_ISP` (about 1500 instructions) which sit on `IMP_ISP_Set/Get
    OSDAttr`, `IMP_ISP_SetSingleOSDAttr`, `IMP_ISPOSD_Alloc` and the
    `IspOsdAdjust_Pic` clamping.  The T23 module (`src/t23/openimp_t23_isp_osd.c`)
    uses the T23 tuning calls `SetOSDAttr/SetOSDBlock`, which T40 does not have,
    and the T40 `IMPIspOsdAttrAsm` differs.  The lower ioctl layer is not
    reverse engineered and cannot be tried on the busy T40 camera, so it is not
    implemented (large effort, needs a device).
- `IMP_ISP_Tuning_Get/SetMask` are implemented from the vendor disassembly
  (288-byte `IMPISPMASKAttr`, control 0x08000074, per-input copy, RGB->YUV for
  enabled RGB blocks, Get restores type and RGB from the copy).  Host-tested
  only; the DumpMask constants are not readable in the disassembly, the T31
  BT.601 values are used.  Device check outstanding.
- FrameSource `frame->size` stays the driver `sizeimage`
  (640x360: 353280): the vendor reports `w*h*3/2` (345600), but the buffer
  holds the chroma plane after the 16-aligned luma rows and the encoder
  (cache invalidation, HW JPEG `src_need`) relies on the real size, so only
  the field differs and is left as is.
- `IMP_Encoder_SetbufshareChn`: the vendor T40 stores the share channel in the
  channel record (+984) and returns 0; nothing reads it back.  OpenIMP accepts
  and drops it (vendor bound is channel < 13, OpenIMP has 8 channels).
- The `P1_INNER` phase markers on stderr need `OPENIMP_DEBUG_TRACE=1`.

## T40 on the T41 code paths (host tests only, no camera yet)

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

## Build

```sh
./build-t40.sh
```

The script discovers the Thingino T40 1.3.1 headers, cross-compiles
`build/t40/libimp.so`, checks RVD/RAD import coverage when those binaries are
available, and rejects any dependency on OEM `libimp.so`.
