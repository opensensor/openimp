# T31 Allegro rate-control core: device test plan (cam-A)

Result (2026-10-03, cam-A T31, main session): passed, see the end.
Since then the Allegro core is the default; `OPENIMP_T31_RC_CORE=legacy`
restores the former controller.

Goal: confirm on the camera that the Allegro core (`OPENIMP_T31_RC_CORE=allegro`) runs VBR,
CappedVBR and CappedQuality with the ported OEM controller, stays stable, and
behaves as the OEM does on a static scene and on a scene change.  The scene
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

## Checks per run (3 minutes each, static scene)

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

Switch the room light on or off (or walk through the picture) twice per
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
* CBR: since `claude/t31-allegro-cbr` the Allegro core (`IIii`) by
  default, the legacy controller with `=legacy` (see the CBR plan below).
* After the test: `umount /usr/lib/libimp.so`, restart the streamer.

## Result 2026-10-03 (cam-A, T31, main session)

60 s per run, target 1200 kbit/s, no kernel oops, decode clean:

| mode | allegro core | legacy controller |
|---|---|---|
| VBR | 1163 kbit/s | 732 kbit/s |
| CappedVBR | 1177 kbit/s | - |
| CappedQuality | 1174 kbit/s | 2030 kbit/s |

The Allegro core holds all three modes at the target; the legacy
controller under- (VBR) and overshoots (CappedQuality).  The user made the
Allegro core the default.

## CBR (claude/t31-allegro-cbr): device test plan

Build `claude/t31-allegro-cbr`, `scratchpad/t31cbr/libimp.so` (md5 in the
hand-over report).  cam-A only, silent: no speaker playback, no AEC, no
audio tests at all; nothing flashed, bind-mount as above.

Config: `video0.rc_mode=cbr`, fps 25, GOP 50, iInitialQP/bounds as the
streamer sets them (IMP default iMinQP 15, iMaxQP 48, eRcOptions 1).  Four
runs of 60 s each on the same ((static)) scene, alternating so the
light does not drift between the pair:

| run | bitrate | core |
|---|---|---|
| 1 | 1200 kbit/s | allegro (no variable) |
| 2 | 1200 kbit/s | `OPENIMP_T31_RC_CORE=legacy` |
| 3 | 3000 kbit/s | allegro |
| 4 | 3000 kbit/s | legacy |

Optional runs 5/6: 1200 kbit/s allegro/legacy with one scene change
(walk through the picture or switch the light at 20 s and 40 s).

Measure per run (record the stream with a client, e.g. `ffmpeg -i
rtsp://.. -c copy -t 60 run.mp4`, then `ffprobe -show_packets`):

* Bitrate accuracy: mean over the 60 s (bytes * 8 / 60) against the
  target, and the mean of each 2 s GOP window (min/max of the 30 windows).
* Peak behaviour: largest 1 s window (25 pictures), largest I picture and
  largest P picture in bytes, the I/P size ratio, and the largest picture
  against the HRD buffer (CPB 270000 ticks = 3 s * bitrate).
* Log: `T31 rate control core: allegro (OEM libimp 1.1.6 port)`, `T31
  allegro rc: init mode=0 (AL 1) target=.. max=<target> ..`, then
  `pic=..` lines with `next_qp`, `filler=.. (N pictures)` (N = pictures
  for which the OEM would have written filler data); legacy runs show no
  `T31 allegro rc` lines.
* Stability as before: decode clean, no stalls, `dmesg` clean, no encoder
  restarts, streamer memory flat.

Expected: allegro holds the GOP windows close to the target as long as the
scene needs the bits; on a static scene at 3000 kbit/s the QP reaches
iMinQP or the HRD asks for filler (`filler` count grows), and the stream
then stays below the target because OpenIMP writes no filler NAL (the OEM
pads to the target there).  QP steps of at most 4 per picture (more only
for a mostly-intra P picture larger than 3/4 of the buffer level), a P
picture right after an I picture keeps the QP.  Legacy: the former
closed-loop result as reference.
