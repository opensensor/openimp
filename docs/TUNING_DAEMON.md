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
  total-gain changes. This establishes the feedback boundary without coupling
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
```

`psychedelic` names and preserves the accepted warm, saturated T41 open-ISP
baseline. It is intentionally a policy identity rather than a register replay:
the calibrated driver remains the source of truth. Individual `-b`, `-c`,
`-s`, `-S`, and `-H` options create a custom profile and opt in only the
corresponding control. `-n` disables gain feedback for controlled comparisons.

The daemon exits nonzero if an explicitly requested control is unsupported.
SIGTERM and SIGINT stop the feedback worker, close the tuning descriptor, and
leave capture/encoder ownership untouched.
