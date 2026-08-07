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
  total-gain changes. The security policy uses hysteresis to select the
  stock-derived daylight or low-light AWB, AE-target, and 29-word color-model
  bank as illumination changes. Profile changes use private-ioctl handoffs;
  capture and encoding remain untouched.
- T40 currently owns the tuning node and applies selected static controls;
  dynamic feedback will be added only when its OEM rule is evidenced.

Generic static controls are opt-in through `control_mask`. On T41, named
profiles also own their documented AWB, AE, and color-model state. Sharpness uses the
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

On T41, `security` follows two matched OEM OS04D10 operating points. Daylight
uses AWB 1476/3524 with a 17600 Q8 AE target. Above the low-light entry gain it
atomically switches to AWB 1225/4850, a 15800 target, and the OEM-derived
low-light color-model bank; it returns to daylight below a separate exit gain
to prevent hunting. A nighttime reference measured normalized YUV averages of
109.568/116.295/138.708 for stock and 109.546/116.765/137.548 for open.
`psychedelic` preserves the accepted warm open-ISP baseline by replaying the
proven 1800/3000 pair, its 14500 Q8 AE target, and the daylight color bank.
The aggregate AWB controller remains an explicit `set awb auto` experiment;
the security policy does not depend on its unstable per-bank estimate. The
driver owns safe register handoffs and profile names remain userspace policy.

The Unix control socket defaults to `/var/run/openimp-tuning.sock`. `status`,
named `profile` changes, individual color/exposure controls, and auto/manual
AWB changes all operate on the existing controller. Individual `-b`, `-c`,
`-s`, `-S`, and `-H` startup options create a custom profile and opt in only
the corresponding control. `-n` disables total-gain feedback for controlled
comparisons.

The daemon exits nonzero if an explicitly requested control is unsupported.
SIGTERM and SIGINT stop the feedback worker, close the tuning descriptor, and
leave capture/encoder ownership untouched.
