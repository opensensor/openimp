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

`OPENIMP_T23_HELIX_BRIDGE=0` leaves the bridge to the OEM worker
`openimp-t23-helixd` out of the T23 `libimp.so` (a further 15 KB of `.text`).
Default 1 (built), because the worker is not only an opt-in backend:

- The native Helix encoder is the build default (`T23_DEFAULT_ENCODER=native`,
  `OPENIMP_T23_ENCODER` unset).
- When the native encoder cannot be created, `codec-t40.c` falls back to the
  worker automatically if the helper is installed
  (`OPENIMP_T23_NATIVE_FALLBACK=0` disables that). The thingino package
  installs the helper and the OEM `libimp.so` under `/opt/openimp-t23/`.
- `IMP_Decoder_*` on T23 runs in the worker.
- Encoder parameters are cached in the bridge for the worker.

With 0 the native encoder is the only H.264 backend, a channel whose native
encoder cannot be created stays without output, `IMP_Decoder_CreateChn`
fails, and encoder parameters stay local (as `OPENIMP_T23_ENC_PARAMS=0`).
The exported symbols do not change.
