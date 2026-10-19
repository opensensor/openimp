# libimp and tx-isp module size

How the open stack keeps `libimp.so` and the `tx-isp-<soc>.ko` kernel module
small, which switches restore the previous build, and what it costs at run
time.

## libimp: -Os with -O2 hot paths

All SoC builds (`build-t21.sh` for T10/T20/T21, `build-t23.sh`,
`build-t30.sh`, `build-t31.sh`, `build-t40.sh`, `build-t41.sh`) compile with
`-Os`. The files below keep `-O2` (`$hot_flags` in the build scripts),
because they run per pixel, per macroblock, per audio frame or are the
floating-point rate controllers whose output is compared bit for bit with
the vendor:

| File | Why it keeps -O2 | SoCs |
|---|---|---|
| `src/framesource/nv12_rotate.c` | NV12 90/180/270 rotate, per pixel | T10, T20, T21, T31 |
| `src/audio/openimp_aec.c` (includes the WebRTC AECM and signal-processing files) | echo cancellation, FFT per 10 ms frame | T10, T20, T21, T23, T30, T31 |
| `src/t31/openimp_t31_services.c` (includes `openimp_t31_osd_draw.h`) | CPU OSD drawing (lines, rectangles, pictures), per pixel | T10, T20, T21, T23, T30, T31 |
| `src/t23/openimp_t23_osd.c` | CPU OSD drawing and colour conversion, per pixel | T23, T40, T41 |
| `src/hw_encoder.c` (includes `jpeg_dct.h`) | software JPEG (DCT, quantiser, Huffman), per 8x8 block | all |
| `src/t31/openimp_t31_ivs_move.c` | IVS motion detection core, per pixel | all |
| `src/t31/openimp_ivs_move_v2.c` | IVS motion v2, per pixel | all |
| `src/rc_t20/rc_t20_mb.c` | T20 macroblock rate control | T10, T20 |
| `src/eprc/eprc_mbrc.c` | Helix macroblock rate control | T21, T23 |
| `src/rc_t20/rc_t20.c`, `src/rc_t10/rc_t10.c` | floating-point rate controllers, `-Os` changes the fused multiply-add choice | T10, T20 |
| `src/eprc/eprc.c`, `src/eprc/eprc_t21.c` | floating-point rate controllers (kept for bit-identity) | T21, T23 |

Everything else (encoder setup, ISP and FrameSource control, ioctl
plumbing, logging, audio codecs at 8/16 kHz) runs per frame or per API call
and builds with `-Os`. The per-macroblock ROI loops in `codec-t40.c` run only
when an application changes the ROI or QP map, not per frame.

The Helix per-frame path (frame submit, bitstream read-out, slice/SPS/PPS
header writer, frame-level rate control, hardware JPEG, frame dequeue) was at
`-O2` for a while (256f50c) and is back at `-Os`: on T21 `-O2` cost 18,272 B of
`.text` (393,936 -> 412,208; T23 18,688 B, T30 17,936 B) and mainly inlined the bit writers into the header
writers (97 + 22 call sites, run once per frame or slice; the macroblock and
CABAC work is done by the Helix hardware). Upper bound of the saving: about
150k cycles/s for two 25 fps streams, 0.015 % of the CPU.

Checks done for the switch (host only):

- the exported dynamic symbol set of every SoC `libimp.so` is unchanged;
- the count of floating-point `madd`/`msub`/`nmadd`/`nmsub` instructions is
  the same per object at `-O2` and at `-Os` (no file changes how it rounds);
- `make check` passes.

`OPENIMP_SIZE_OPT=0` (`make t31 OPENIMP_SIZE_OPT=0`) builds everything at
`-O2` as before.

The `.so` file size moves in 64 KiB steps: the MIPS linker aligns the data
segment to a 64 KiB page, so the zero padding before it absorbs smaller
savings. The xz and RAM columns below show the real change.

## libimp: AVPU diagnostics only with OPENIMP_DEBUG=1

The Enc1/Enc2 command-list dumps, the busy command-list replay, the busy and
submit register snapshots and the DMA layout dump in `src/t40/codec-t40.c`
return at once unless `OPENIMP_DEBUG=1`. Their log lines were already
compiled out; now their register reads, name buffers and strings go too.

## libimp: no AVPU session code on the Helix SoCs

T10, T20, T21, T23 and T30 encode on the Helix VPU and have no AVPU;
`/dev/avpu` never opens there. With `CODEC_HAS_AVPU 0` (set for these
builds in `codec-t40.c`) the AVPU open returns "no such device" without the
system call, as the open did before, so the compiler drops the AVPU session
code (open, IRQ thread, command lists, stream queue): about 25 KB of `.text`
per Helix build. The public `AL_Codec_*` and `IMP_*` graph is unchanged
(identical exported symbols); T31, T40 and T41 build bit for bit as before.

## T23: OEM Helix worker bridge (OPENIMP_T23_HELIX_BRIDGE)

Default 0 (since 2026-10-11): the bridge to the OEM worker
`openimp-t23-helixd` is not built into the T23 `libimp.so` (15 KB of
`.text` less). The native Helix encoder is the only H.264 backend:

- a channel whose native encoder cannot be created stays without output
  (no automatic fallback to the OEM worker);
- `IMP_Decoder_CreateChn` fails (the decoder ran in the worker);
- encoder parameters stay local (as `OPENIMP_T23_ENC_PARAMS=0`).

The exported symbols do not change. `OPENIMP_T23_HELIX_BRIDGE=1` builds the
bridge again. Without it, an image no longer needs `openimp-t23-helixd` or the
OEM `libimp.so` under `/opt/openimp-t23/`.

## Size table (2026-10-19)

Measured on open-tx-isp `1427ffa1` and OpenIMP `7d55619` (clean rebuild of
all SoCs). Sizes in KB (1024 bytes), per file as `file / xz / RAM`.

Method:

- Both sides stripped with `strip --strip-unneeded`; the vendor modules are
  built by thingino's ingenic-sdk package (9e87773; T40 d30c0af), the vendor
  `libimp.so` is the one thingino installs (T10/T20 3.12.0, T21 1.0.33,
  T23 1.3.0, T30 1.0.5, T31 1.1.6, T40 1.3.1, T41 1.2.6). No T30 vendor
  module exists locally.
- xz = `xz -9e --block-size=256K` per file (approximates the share in the
  xz squashfs rootfs with 256 KB blocks).
- RAM = sum of all `SHF_ALLOC` sections (text, rodata, data, bss): for a
  module what the kernel loader allocates, for `libimp.so` what one process
  maps (read-only pages are shared).
- The `libimp.so` file size moves in 64 KiB steps (data segment alignment),
  so compare xz and RAM. T10 `libimp.so` is the T20 build.
- Modules are built against the thingino kernels of the test cameras
  (T30 against the T31 kernel, T40 against the T41 kernel: compile
  baselines).

| SoC | ko ours | ko vendor | libimp ours | libimp vendor | ko+libimp xz, ours vs vendor |
|---|---|---|---|---|---|
| T10 | 390/119/321 | 389/107/293 | 453/173/511 | 921/269/1118 | 292 vs 376 |
| T20 | 392/120/323 | 392/107/295 | 453/173/511 | 921/269/1118 | 293 vs 376 |
| T21 | 419/120/662 | 697/122/601 | 453/167/491 | 798/247/996 | 288 vs 369 |
| T23 | 607/178/871 | 1108/230/836 | 518/184/567 | 1032/291/1232 | 362 vs 521 |
| T30 | 853/164/683 | n/a | 389/149/454 | 1660/452/1823 | 312 vs n/a |
| T31 | 648/195/645 | 1077/228/809 | 453/173/504 | 1063/328/1024 | 368 vs 556 |
| T40 | 3016/484/5267 | 980/230/746 | 330/123/372 | 1014/293/981 | 607 vs 523 |
| T41 | 787/283/665 | 779/227/555 | 327/123/366 | 1362/368/1322 | 406 vs 595 |

Notes:

- Only T40 is larger than the vendor pair (ko+libimp xz 607 vs 523 KB); the
  T40 module carries about 1.1 MB of bss in recovered placeholder buffers.
- T41 module: 1004 KB on the earlier 2026-10-19 build, 787 KB now (the
  translated vendor AE code became 231 KB smaller).
- T10, T20 and T30 modules are byte-identical to the earlier 2026-10-19
  build; changes of the other modules are below 2 KB.
- The vendor module figures of older notes (T21 616 KB, T23 857 KB, T31
  829 KB) are not reproduced by the vendor build above; they came from
  another build or strip method. The T21 and T41 figures here (697, 779)
  are reproducible.

### Test pattern feature cost

The test pattern runtime API (`openimp_testpattern.o`, always linked, same
object on every SoC) adds about 10 KB xz and 28 KB RAM to every `libimp.so`
(text+rodata 16.9 KB, bss 7.4 KB). The logo is about 4 KB xz of that
(3.6 KB RLE data, 2.1 KB decoder).
