# T41 temporal-image investigation (2026-09-08)

Status: the reported visual flicker is **not resolved**. Successful decoding
and timestamp checks do not establish temporal image quality.

## Isolated completion-tail race

The frozen-source probe exposed a separate regression in the shared AVPU
lease. Completion publishes the packet before the IRQ handler exits and
releases its lease. A serial caller can Dequeue/Release that packet and submit
its next frame during this tail. The old same-owner check then returned
`-EBUSY`, surfaced as `-EIO` by the standalone AVC API. The token identifies a
codec, not a thread; this is not recursive acquisition.

Acquisition now waits for any outstanding lease, including the same codec's
previous completion, using the existing bounded condition wait. Timeout never
revokes ownership, and release stays after the entire IRQ epilogue. No delay,
early IRQ release, rate-control adjustment, or sensor-specific tuning is added.

On the T41NQ, the previous library `6771738` failed rapid frozen-source runs
at zero-based frame 1 and frame 3. The fixed library completed 100 frames each
at fixed QP 40 and 26 with identical source hashes before/after encoding.
Both files decode without warnings. Host and MIPS/QEMU tests cover same-codec
completion-tail handoff, timeout retention, wrong-owner release, and 20,000
cross-codec handoffs. This proves the submission race fix, **not** a visual
flicker fix or full OEM parity.

Cold-boot verification of the fixed library: concurrent 60-second main TCP
and sub UDP recordings decode 1498/1499 H.264 frames plus AAC with empty
decode-warning logs. A simultaneous 40-second main RTP check measures
24.98648 fps, 39.9889-40.0556 ms source timestamp steps, and no backward
timestamps, missing source frames, or video/audio sequence gaps. Each MKV
stream-copy recording warns once about an unset input packet timestamp;
that muxing warning remains separate from the clean source-RTP check.
Keyframe detail pumping still appears in the fixed main recording (mean
I/P YDIF 7.91/1.73). The race fix did not remove the measured visual effect.

## Visual evidence and limitations

TCP recordings at 2560x1440, 25 fps, nominal 4 Mbit/s show large keyframe-linked
detail changes with relatively stable whole-frame average luma:

| Configuration | Captured frames | Mean I/P-frame YDIF | Largest YAVG step |
| --- | ---: | ---: | ---: |
| Open multi-output, main | 599 | 7.40 / 1.74 | 0.328 |
| Same stack, substream stopped | 399 | 7.45 / 1.75 | 2.743 |
| Pre-multistream open checkpoint | 499 | 7.14 / 1.28 | 0.384 |
| Stock ISP and OEM encoder | 499 | 7.47 / 2.54 | 0.762 |

Values are FFmpeg `signalstats` in decoded 8-bit Y units, not a perceptual
score. The means include the recording's first I frame (whose YDIF is zero).
The scene/exposure/WB were not locked; the substream-stopped sample contains
scene/light changes. These are diagnostic comparisons, not matched IQ scores.

Both open main and sub show the keyframe effect. Stopping sub is insufficient
to remove it; an older open checkpoint also has it. The stock recording has
an actual 50-frame keyframe interval, whereas open has 25, despite RVD status
reporting GOP 25 for both. That difference must be controlled before claiming
rate-control or temporal-IQ parity. Do not silently lengthen GOP or increase
bitrate and present that as an algorithmic repair.

A hot mixed stock-ISP/OpenIMP test produced no frames, with stock capture
queue `vb is null` errors. It provides no usable IQ comparison and does not
identify the flicker cause. It was rolled back by reboot.

## Reproduction

Build `tests/t41/v4l2_avc_test.c` with the T41 cross compiler, OpenIMP headers,
and the tested `libimp.so`. Stop the capture/encoder consumer first; stage the
test and library under `/tmp`, not over an active library inode. With an
exclusive standalone capture node:

```sh
LD_LIBRARY_PATH=/tmp/test-lib /tmp/v4l2-avc-test /dev/video0 /tmp/frozen.h264 100 40
```

The optional last argument enables FIXQP and frozen input. It dequeues and
returns 50 warmup frames, retains the next captured allocation after
STREAMOFF, and repeatedly encodes its unchanged NV12 pixels. The source hash
is checked before/after; timestamps are a diagnostic 25-fps timeline, not live
capture timing. Warmup does not prove AE/AWB convergence. Omitting the final
argument preserves live capture. One encoded output buffer is sufficient for
this serial Submit/Dequeue/Release test.

For live flicker analysis, record the original encoded stream, then extract
frame types with `ffprobe` and per-frame `signalstats` with `ffmpeg`. Retain
the exact config and binary hashes. Look at changes across keyframes as well
as decode errors, source/RTP timestamps, and average brightness/color. Keep
the temporal-IQ gate open until the user's reported symptom is reproduced and
an evidence-backed correction passes a matched comparison.
