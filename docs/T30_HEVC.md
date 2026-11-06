# T30 H.265 (HEVC): Radix encoder (work in progress, #208)

Status 2026-11-06: `IMP_Encoder_CreateChn(PT_H265)` on T30 returns -1 unless `OPENIMP_T30_H265=1` is set; with it the
channel encodes on Radix (`src/t30/radix/t30_radix_encoder.c`); proven in the QEMU T30 machine with synthetic pictures, not
yet on a T30X. This page describes the hardware path and what of it is in the tree.

## Hardware and kernel

The T30 encodes H.265 with **Radix**, a block separate from Helix (H.264/JPEG), at 0x13600000 with its own interrupt.
Its kernel driver is GPL and built into every thingino T30 kernel (`CONFIG_VPU_RADIX=y` in
`board/ingenic/xburst1/kernel/3.10.14/t30.generic.config`; `drivers/video/soc_vpu/radix/radix.c`). It is reached through the
same `/dev/soc_vpu` channel ioctls as Helix: request with codec direction `HWH265ENC` (0x10), run with the command list
address, completion returns the slice-data length (`SDE_SLBL`). No kernel change is needed.

## Split between library and hardware

Like the vendor libimp 1.0.5 (an x265-derived encoder, "i265e"):

- the library writes VPS, SPS, PPS, SEI and the slice header, and inserts the emulation prevention bytes;
- Radix writes the slice data (CABAC) into the bitstream buffer;
- per picture the library builds a command list of (value, `0x80000000 | register`) pairs from a 0x1204-byte parameter block
  (source/reconstruction/reference addresses, size, QP, CABAC context states, mode-decision, motion-search, loop-filter, ROI,
  QP-generator and rate-control fields) and starts it.

## In the tree

| file | what |
|---|---|
| `src/t30/radix/t30_radix_sliceinfo.h` | the parameter block: field names and offsets as printed by the vendor debug dumper |
| `src/t30/radix/t30_radix_cmdlist.c/.h` | the command list builder (GPL-2.0, register map from the kernel's `radix.h`) |
| `src/t30/radix/t30_radix_intp.h` | HEVC luma/chroma interpolation formats, generated from `radix.h` by `tools/gen_radix_intp.py` |
| `tests/t30/radix/radix_cmdlist_test.c` | host test (`make -C tests/t30 check`): 64 parameter blocks must give the vendor's lists |
| `src/t30/radix/t30_radix_cabac.c/.h` | the 156 initial CABAC context states for slice type and QP (HEVC initValue tables, vendor context order) |
| `src/t30/radix/t30_radix_hevc.c/.h` | VPS, SPS (with VUI/HRD), PPS and slice header RBSP writers and the NAL framing (start code, emulation prevention) |
| `tests/t30/radix/radix_cabac_test.c`, `radix_hevc_test.c` | host tests against vendor hashes (`make -C tests/t30 check`) |
| `tests/t30/radix/hevc_diff.c` | desk tool: CABAC, header writers and NAL framing against the vendor functions under qemu-mipsel |
| `src/t30/radix/t30_radix_encoder.c/.h` | the encoder object: soc_vpu channel (codec direction 0x10, vpu_id 0x4000001), command list, NV12 reconstruction/reference pair with the vendor's 16-sample border, bitstream window, `EPRC30_*` (codec 1) for QP, parameter block and QP map, the 19 statistics registers, re-encode, VPS/SPS/PPS + slice NAL as `sliceEncap` |
| `tests/t30/radix_encoder_test.c` | host test of the encoder object with a fake soc_vpu (access units, NAL types, emulation prevention, GOP, reference swap, failure path) |
| `t30_hevc_channel_params()` (`t30_radix_hevc.c`) | the VPS/SPS/PPS values the vendor announces for a channel (size, frame rate, RC mode, bit rate) |
| `tests/t30/radix/hevc_cfg_diff.c`, `radix_hevc_chan_test.c` | desk tool (vendor `IMP_Encoder_CreateChn`/`I265e` setup + `codeVPS/SPS/PPS` under qemu-mipsel, 5000 random channels, 0 differ) and host test (500 channels, vendor hash) |
| `tests/t30/radix/radix_diff.c` | desk tool: compares the builder with the vendor function under qemu-mipsel (`make -C tests/t30 radix-diff O=<thingino output> VENDOR_LIBIMP=<libimp.so> QEMU_MIPSEL=<qemu-mipsel>`) |

The builder gives the same list as the vendor library word for word (25 000 random blocks, I and P pictures). It is not
linked into libimp yet.

## Still to do

The T30X device proof (audit DEVICE_PLAN: Radix present, OpenIMP H.265 run, decode and RC checks, vendor reference).

## QEMU proof (2026-11-06)

`tests/t30/radix/radix_smoke.c` (`make -C tests/t30 radix-smoke O=<thingino output>`) feeds synthetic NV12 pictures (gradient,
moving box) from rmem to the encoder object on the real `/dev/soc_vpu` driver of the `qemu_t30x` image; the QEMU T30 machine's
Radix model replays the command list and encodes with the AVPU HEVC core.  640x360, 60 pictures each for CBR 500 kbit/s, FIXQP
30 and VBR: one radix interrupt per picture, no oops or timeout, IDR every 25 pictures; ffmpeg decodes all three streams without an
error message (HEVC Main, level 2.1), 60 of 60 distinct pictures each, and the decoded pictures show the synthetic pattern with the
box at its position.  The model does not check most registers, the QP generator or the statistics (they read 0), so this proves the
channel, buffers, command-list addresses, parameter sets, slice header, NAL framing and the rate-control loop, not silicon
behaviour.  The IMP path (`apitest enc` with `OPENIMP_T30_H265=1`) creates the H.265 channel (CreateChn 0, attributes read back)
but gets no pictures in QEMU: FrameSource `EnableChn` fails for every channel there (`set format failed`, H.264 likewise; the
QEMU T30 frame path to the encoder is not modelled, README-T30).

## Channel parameter sets

`IMP_Encoder_CreateChn(PT_H265)` in the vendor library fills `i265e_param_t` from the channel attributes (size, frame rate
reduced to lowest terms with 25/1 for 0, GOP, RC fields, HSkip type) and builds the `I265e`, whose `configure`,
`enforceLevel`, `determineLevel` and `initVPS/SPS/PPS` set the parameter sets.  With the i265e defaults only size, frame
rate and bit rate reach them: the size is clipped to 256..4096 x 16..2304 and padded to a multiple of 16 x 8 (the vendor
codes the padded picture, there is no conformance window), the VUI timing is the frame rate, level and tier follow the x265
level table with the bit rate in kbit/s (0 for FIXQP, the CBR target or VBR/SMART maximum otherwise, clipped by
`configure`), high tier allowed.  The VPS carries `vps_extension_flag` 1 and 16 bits built from two `random()` values unless
`/tmp/closeextvpflag` exists (`Entropy::codeVPS`).
