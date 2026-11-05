# T30 H.265 (HEVC): Radix encoder (work in progress, #208)

Status 2026-11-05: `IMP_Encoder_CreateChn(PT_H265)` on T30 still returns -1 (as before). This page describes the
hardware path and what of it is in the tree.

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
| `tests/t30/radix/radix_diff.c` | desk tool: compares the builder with the vendor function under qemu-mipsel (`make -C tests/t30 radix-diff O=<thingino output> VENDOR_LIBIMP=<libimp.so> QEMU_MIPSEL=<qemu-mipsel>`) |

The builder gives the same list as the vendor library word for word (25 000 random blocks, I and P pictures). It is not
linked into libimp yet.

## Still to do

Parameter-block defaults and CABAC context states, the HEVC header writer for the Radix syntax, the encoder object
(buffers, channel, packs, rate control), the QEMU proof (the QEMU T30 machine has a Radix model) and a T30X device proof.
