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
| `src/t30/t30_helix_encoder.c`, `src/t30/helix_bitstream.c`, `src/t30/helix_jpeg.c`, `src/t30/h264enc/{common,cabac,set,slice}.c`, `src/t40/t31_rate_control.c`, `src/framesource/framesource_tseries.c` | Helix per-frame path: frame submit, bitstream read-out, slice/CABAC header writer, frame-level rate control, hardware JPEG, frame dequeue and group dispatch. On T10/T20/T21/T23/T30 this runs synchronously in the streamer's threads per frame; added after the T21 CPU comparison of 2026-10-11 (A/B against -Os pending) | T10, T20, T21, T23, T30 |

Everything else (encoder setup, ISP and FrameSource control, ioctl
plumbing, logging, audio codecs at 8/16 kHz) runs per frame or per API call
and builds with `-Os`. The per-macroblock ROI loops in `codec-t40.c` run only
when an application changes the ROI or QP map, not per frame.

Checks done for the switch (host only):

- the exported dynamic symbol set of every SoC `libimp.so` is unchanged;
- the count of floating-point `madd`/`msub`/`nmadd`/`nmsub` instructions is
  the same per object at `-O2` and at `-Os` (no file changes how it rounds);
- `make check` passes.

`OPENIMP_SIZE_OPT=0` (`make t31 OPENIMP_SIZE_OPT=0`) builds everything at
`-O2` as before.

The `.so` file size moves in 64 KiB steps: the MIPS linker aligns the data
segment to a 64 KiB page, so the zero padding before it absorbs smaller
savings. The `.text` column below shows the real change.

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

## Size table (2026-10-11)

Per SoC: the file as installed and the allocated sections, in KB (1024
bytes). RAM is the sum of all allocated sections (text, rodata, data, bss):
for a module what the kernel loader allocates, for `libimp.so` what one
process maps (the read-only pages are shared).

- vendor: the Ingenic module built by thingino's ingenic-sdk package
  (9e87773; T40 from a thingino T40 image) and the vendor `libimp.so` thingino
  installs (T10/T20 3.12.0, T21 1.0.33, T23 1.3.0, T30 1.0.5, T31 1.1.6,
  T40 1.3.1, T41 1.2.6). No T30 vendor module is built locally.
- RC1: `v2026.10.11-rc1`; agg-36: openimp 7c08dad, open-tx-isp a1c28797;
  agg-37-size: this change set.
- Modules stripped like thingino (`strip --strip-unneeded`, no `.comment`),
  `libimp.so` with `--strip-unneeded`. Built against the thingino kernels of
  the test cameras (T30 against the T31 kernel, T40 against the T41 kernel:
  compile baselines).
- The `libimp.so` file size moves in 64 KiB steps (data segment alignment).
- The vendor module figures of earlier notes (T21 616 KB, T23 857 KB, T31
  829 KB) are not reproduced by the current vendor build (695, 1105, 1075 KB
  stripped like above); they came from another build or strip method.


### Kernel module tx-isp-<soc>.ko

| SoC | stage | file KB | text/rodata/data/bss KB | RAM KB |
|---|---|---|---|---|
| T10 | vendor | 386 | 196/22/43/33 | 293 |
| T10 | RC1 | 402 | 208/35/45/36 | 324 |
| T10 | agg-36 | 402 | 208/35/45/36 | 324 |
| T10 | agg-37-size | 383 | 201/35/45/36 | 317 |
| T20 | vendor | 389 | 198/22/43/33 | 295 |
| T20 | RC1 | 405 | 210/35/45/36 | 326 |
| T20 | agg-36 | 405 | 210/35/45/36 | 326 |
| T20 | agg-37-size | 385 | 203/35/45/36 | 320 |
| T21 | vendor | 695 | 208/18/277/98 | 602 |
| T21 | RC1 | 414 | 233/46/21/359 | 658 |
| T21 | agg-36 | 414 | 233/46/21/359 | 658 |
| T21 | agg-37-size | 414 | 233/46/21/359 | 658 |
| T23 | vendor | 1105 | 433/31/212/161 | 837 |
| T23 | RC1 | 637 | 378/72/58/386 | 894 |
| T23 | agg-36 | 637 | 378/72/58/386 | 894 |
| T23 | agg-37-size | 599 | 362/60/58/386 | 866 |
| T30 | vendor | n/a | n/a | n/a |
| T30 | RC1 | 845 | 559/34/48/36 | 677 |
| T30 | agg-36 | 845 | 559/34/48/36 | 677 |
| T30 | agg-37-size | 845 | 559/34/48/36 | 677 |
| T31 | vendor | 1075 | 424/35/191/159 | 809 |
| T31 | RC1 | 687 | 397/86/19/168 | 669 |
| T31 | agg-36 | 687 | 397/86/19/168 | 669 |
| T31 | agg-37-size | 641 | 377/77/19/168 | 641 |
| T40 | vendor | 975 | 501/37/186/22 | 747 |
| T40 | RC1 | 2571 | 1832/103/104/3384 | 5422 |
| T40 | agg-36 | 2571 | 1832/103/104/3384 | 5422 |
| T40 | agg-37-size | 2571 | 1832/103/104/3384 | 5422 |
| T41 | vendor | 773 | 468/43/24/20 | 556 |
| T41 | RC1 | 593 | 385/59/27/30 | 500 |
| T41 | agg-36 | 603 | 391/60/27/30 | 508 |
| T41 | agg-37-size | 599 | 391/56/27/30 | 504 |

### libimp.so

| SoC | stage | file KB | text/rodata/data/bss KB | RAM KB |
|---|---|---|---|---|
| T10 | vendor | 921 | 642/198/44/235 | 1118 |
| T10 | RC1 | 709 | 528/135/4/76 | 744 |
| T10 | agg-36 | 517 | 419/75/4/76 | 574 |
| T10 | agg-37-size | 453 | 332/71/4/76 | 482 |
| T20 | vendor | 921 | 642/198/44/235 | 1118 |
| T20 | RC1 | 709 | 528/135/4/76 | 744 |
| T20 | agg-36 | 517 | 419/75/4/76 | 574 |
| T20 | agg-37-size | 453 | 332/71/4/76 | 482 |
| T21 | vendor | 798 | 586/147/27/237 | 997 |
| T21 | RC1 | 709 | 512/138/4/74 | 729 |
| T21 | agg-36 | 517 | 402/76/4/74 | 556 |
| T21 | agg-37-size | 453 | 312/72/4/74 | 461 |
| T23 | vendor | 1032 | 758/195/28/252 | 1233 |
| T23 | RC1 | 774 | 568/169/5/77 | 819 |
| T23 | agg-36 | 582 | 460/110/4/77 | 652 |
| T23 | agg-37-size | 518 | 370/105/4/77 | 556 |
| T30 | vendor | 1660 | 1136/378/50/259 | 1824 |
| T30 | RC1 | 645 | 462/131/4/73 | 669 |
| T30 | agg-36 | 453 | 366/72/4/73 | 514 |
| T30 | agg-37-size | 389 | 282/68/4/73 | 425 |
| T31 | vendor | 1059 | 804/183/7/28 | 1022 |
| T31 | RC1 | 709 | 518/129/4/71 | 722 |
| T31 | agg-36 | 517 | 401/62/4/71 | 538 |
| T31 | agg-37-size | 453 | 336/62/4/71 | 473 |
| T40 | vendor | 1014 | 752/187/7/35 | 982 |
| T40 | RC1 | 390 | 296/71/5/49 | 422 |
| T40 | agg-36 | 330 | 280/36/9/50 | 376 |
| T40 | agg-37-size | 330 | 224/36/9/50 | 319 |
| T41 | vendor | 1362 | 1038/221/29/34 | 1322 |
| T41 | RC1 | 455 | 330/75/6/52 | 462 |
| T41 | agg-36 | 327 | 262/35/6/53 | 355 |
| T41 | agg-37-size | 263 | 214/35/6/53 | 307 |
