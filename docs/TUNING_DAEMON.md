# Standalone tuning daemon

`openimp-tuningd` separates image policy from IMP graph ownership. This is
required when Raptor consumes `/dev/video0`: V4L2 owns capture and OpenIMP AVC
owns encoding, but neither process should impersonate the legacy IMP ISP
lifecycle merely to keep gain-dependent tuning feedback alive.

The reusable `OpenIMP_Tuning_*` API owns a private ISP tuning file descriptor,
applies explicitly selected static controls, and runs generation-specific
feedback:

- T31 reads total gain at 25 Hz and submits the OEM packed
  `(total_gain << 8) | contrast` update.
- T41 reads the recovered 232-byte AE expression response at 25 Hz and tracks
  total-gain changes. Profile changes use an atomic private-ioctl handoff for
  the two AWB banks. This establishes the feedback boundary without coupling
  policy to Raptor or the V4L2 queue.
- T40 currently owns the tuning node and applies selected static controls;
  dynamic feedback will be added only when its OEM rule is evidenced.

Static controls are opt-in through `control_mask`. Starting a preset never
silently rewrites an already-calibrated ISP bank. On T41, sharpness uses the
repaired DMSC writer and saturation uses the proven direct BCSH matrix writer.
Brightness, contrast, and hue return `EOPNOTSUPP` until their recovered
interpolation workspace is safe; unsupported controls cannot enter a damaged
kernel path.

## Profiles

```sh
openimp-tuningd -p security
openimp-tuningd -p psychedelic
openimp-tuningd -p psychedelic -s 160 -S 144

# A running daemon accepts live changes; capture and encoding do not restart.
openimp-tuningd -C status
openimp-tuningd -C 'profile security'
openimp-tuningd -C 'profile psychedelic'
openimp-tuningd -C 'set exposure 17600'
openimp-tuningd -C 'set awb manual 1800 3000'
openimp-tuningd -C 'set awb auto'
```

On T41, `security` replays the 1476/3524 AWB pair from a converged OEM OS04D10
daylight sample and selects the same-scene luma-matched 17600 Q8 AE target.
`psychedelic` preserves the accepted warm open-ISP baseline by replaying the
proven 1800/3000 pair and its 14500 Q8 AE target. Both are deterministic:
the recovered aggregate AWB controller is still not equivalent to the OEM's
larger model-selection routine. `set awb auto` exposes it as an explicit
experiment. The driver owns the safe two-bank update and auto/manual
transition; profile names remain userspace policy.

The Unix control socket defaults to `/var/run/openimp-tuning.sock`. `status`,
named `profile` changes, individual color/exposure controls, and auto/manual
AWB changes all operate on the existing controller. Individual `-b`, `-c`,
`-s`, `-S`, and `-H` startup options create a custom profile and opt in only
the corresponding control. `-n` disables total-gain feedback for controlled
comparisons.

The daemon exits nonzero if an explicitly requested control is unsupported.
SIGTERM and SIGINT stop the feedback worker, close the tuning descriptor, and
leave capture/encoder ownership untouched.
