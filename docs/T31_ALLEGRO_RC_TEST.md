# T31 Allegro rate-control core: device test plan (cam-A, garage)

Result (2026-10-03, cam-A T31 garage, main session): passed, see the end.
Since then the Allegro core is the default; `OPENIMP_T31_RC_CORE=legacy`
restores the former controller.

Goal: confirm on the camera that the Allegro core (`OPENIMP_T31_RC_CORE=allegro`) runs VBR,
CappedVBR and CappedQuality with the ported OEM controller, stays stable, and
behaves as the OEM does on a static scene and on a scene change.  The garage
is silent: no speaker, no AEC, no audio tests.  Nothing is flashed: the
library is bind-mounted.

Build: `claude/t31-capped-quality`, `libimp.so` md5 see the hand-over report
(`scratchpad/t31cq/libimp.so`).

## Setup

1. Copy the library: `scp libimp.so root@cam-A:/tmp/libimp.so`.
2. Bind-mount over the installed one (reversible with `umount`):
   `mount --bind /tmp/libimp.so /usr/lib/libimp.so`
   (check `md5sum /usr/lib/libimp.so` afterwards).
3. Config copies in /tmp, one per mode, from the active streamer config
   (video0 only):
   `video0.rc_mode=vbr`, `video0.rc_mode=capped_vbr`,
   `video0.rc_mode=capped_quality`; bitrate 2000 kbit/s, max bitrate
   2667 kbit/s (the IMP default 4/3), fps 25, GOP 50, uMaxPSNR default 42.
4. Start the streamer with `OPENIMP_T31_RC_CORE=allegro` in its environment
   and the /tmp config; a second run per mode with `=legacy` is the
   reference (before the default changed: without the variable).

## Checks per run (3 minutes each, static garage scene)

* Log: one `T31 rate control core: allegro (OEM libimp 1.1.6 port)
  (OPENIMP_T31_RC_CORE)` line; one `T31 allegro rc: init mode=.. (AL ..)
  target=.. max=.. fps=25/1000 qp=.. bounds=15/48 ip=-1 pb=-1 opts=1 gop=50
  psnr_cap=4200`; `T31 allegro rc: pic=..` lines for pictures 1..3 and then
  every 250 pictures with `next_qp`, `psnr`, `idle` (ticks), `target/frame`,
  `ratio_i`.  No `T31 capped rc` lines (legacy) while allegro runs.
* Stream: play 60 s with a client; no stalls, no decode errors, I pictures
  every 2 s, bitrate (streamer statistics or `ffprobe`) within the target
  (VBR about the target, capped modes at or below it on the static scene).
* Stability: `dmesg` clean, no encoder restarts, memory of the streamer
  flat over the run, CPU load not higher than legacy by more than a few
  percent (the controller is integer code; one `log10` per picture in the
  capped modes).
* Expected difference between the modes on the static scene (from the
  emulation): CappedVBR settles at one QP per GOP and only lowers it once
  the leaky bucket has been idle for the allowed share of the time;
  CappedQuality steps the QP down whenever a picture comes in under its
  target and oscillates by about one QP around a lower value, so it spends
  a few percent more bits and reports a higher PSNR; both stop lowering the
  QP once `psnr` is above `cap` (42 dB).  Plain VBR has no PSNR cap and
  ends at the lowest QP the bitrate allows.

## Scene change (one run per mode)

Switch the garage light on or off (or walk through the picture) twice per
run, 30 s apart, while watching the `T31 allegro rc` lines (set
`IMP_LOG_LEVEL` to info).  Expected: `next_qp` rises by up to 4 per picture
on the big picture (the OOoI/Ooii clamp), then falls back over the following
GOP; CappedVBR holds the raised QP longer than CappedQuality; no dropped
pictures (`t31_overflow_bytes` log) at 2000 kbit/s.  If pictures are
dropped, the allegro path accounts them with the stream-buffer size and the
QP must rise (log) instead of repeating the drop.

## Fallback

* `OPENIMP_T31_RC_CORE=legacy`: the former controller runs, log line
  `T31 rate control core: legacy`; behaviour as before.  Unset or any other
  value: allegro (default).
* CBR: legacy controller in both settings (the init line of the allegro
  core does not appear).
* After the test: `umount /usr/lib/libimp.so`, restart the streamer.

## Result 2026-10-03 (cam-A, T31, garage, main session)

60 s per run, target 1200 kbit/s, no kernel oops, decode clean:

| mode | allegro core | legacy controller |
|---|---|---|
| VBR | 1163 kbit/s | 732 kbit/s |
| CappedVBR | 1177 kbit/s | - |
| CappedQuality | 1174 kbit/s | 2030 kbit/s |

The Allegro core holds all three modes at the target; the legacy
controller under- (VBR) and overshoots (CappedQuality).  The user made the
Allegro core the default.
