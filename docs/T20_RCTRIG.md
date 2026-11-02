# T10 / T20: IMP_Encoder_SetChnRcTrigLevel (issues #3, #5)

`SetChnRcTrigLevel(chn, level, mode)` of the vendor libimp 3.12.0 (T10 and T20 are one binary) is a rate controller input,
not a stored value. `i264e_reconfig_rc_trig_set` only keeps the pair and flags a reconfiguration: the call itself changes
nothing in the running stream (no restart, no IDR, no size change). `i264e_idr_reconfig` takes the pair over at the next
natural IDR (GOP boundary): it copies it into the controller parameters (words 260 and 264), runs
`i264e_validate_parameters` on it (the level becomes the integer `clip(trunc(level), 2, 10)`, the new maximum QP
`clip(mode, maxQp, 51)`, so a level of 0.5 acts as 2 and a mode below the application's maximum QP as that maximum) and sets
the controller up again (`i264e_ratecontrol_init`). Order measured on the cameras: on a T20 the IDR picture itself is coded with
the new controller (start QP); on a T10 the IDR picture keeps the old controller's QP and the first P picture after it starts at
the start QP. While the bits of a GOP are above `level` times its budget the controller lifts the QP cap from the
application's maximum QP (`maxQp` of the rate control attribute) to the new maximum (defaults 3.0 and 51). A starved stream
therefore stays at the application's maximum QP only if `mode` is set to that maximum. A channel that is not created (or
H.265) ignores the call, a channel number of 6 or more returns -1.

OpenIMP: `IMP_Encoder_SetChnRcTrigLevel` hands the pair to the T10/T20 encoder (`src/t30`, controllers `src/rc_t20`,
`src/rc_t10`, both ported from the vendor code), which keeps it pending and applies it with the next natural IDR as above
(`RCT20_ClampTrig` / `RCT10_ClampTrig` are the validate step). Before 2026-11-03 OpenIMP restarted the controller at once
with an extra IDR, used the level unrounded and a level below 2 as given; that was not vendor-equal (issue #194).

T21: the vendor controller does not read the pair (see `docs/T23_EPRC.md`), OpenIMP stores it like the vendor.

## Proof

- Host: `tools/rc_t20_oracle.py --trig-wide` runs the vendor controller under unicorn with levels 0.5 to 12.5 and new maximum
  QPs 30 to 51 (also below the application maximum): `tests/rc_t20/rc_t20_vectors_trig.txt` (24 scenarios, 7357 decisions) and
  `tests/rc_t10/rc_t10_vectors_trig.txt` (24 scenarios, 6392 decisions), 0 differences.
  `--trig-restart` covers the run-time case: a new pair is taken over at a natural IDR in the middle of the stream (the vendor
  validate clamp, then `i264e_ratecontrol_init` on the used controller): `rc_t20_vectors_trigrestart.txt` (24 scenarios, 9178
  decisions) and `rc_t10_vectors_trigrestart.txt` (24 scenarios, 8001 decisions), 0 differences. The earlier oracle fed the raw
  pair to the controller and so missed the validate step (level 0.5 acts as 2) and the timing (take-over at the next IDR).
- Cameras, 2026-11-03 (the runs of 2026-11-02 used the installed libimp, which is OpenIMP, and are withdrawn): vendor 3.12.0 and
  OpenIMP next via `LD_LIBRARY_PATH` (library md5 checked, log markers: OpenIMP `[KernelIF]` / `[VBM]`, vendor `i264e`, libalog
  preloaded), `apitest` case `rctrig`, CBR 640x360, GOP 30, starved 3 kbit/s or 20 kbit/s, ISP day/night and IR-cut stable for 40 s
  before each run. Vendor and OpenIMP back to back on the same camera.
  - Call at picture 40: OpenIMP now codes on without an extra IDR and without a size change (T20: 28.6 kB against 38.3 kB
    vendor for 120 pictures, before the change 188 kB; T10 pictures 40 to 44 and the IDR at picture 60 equal, 43-46 kB against
    44.8 kB for 120 pictures, before the change 312 kB for 150).
  - The new pair counts from the IDR at picture 60 (scenario e, T10: IDR QP 35 and the P pictures after it at QP 40 as the vendor); with
    a starved stream (level 0.5 / 5.5, new maximum 51 / 48, maximum QP 36) both libraries stay at QP 36 to 38 around the IDR.
  - Lift ceilings of a starved stream (maximum QP 36): T20 maximum QP 36 against 36 (level 0.5 / mode 51 was 48 in
    OpenIMP before, level 0.5 acts as 2); T10 41 to 42 against 39 to 41.
  - Not part of the trigger: the T20 run without any call (scenario a) differs between the libraries (vendor mean QP 31, 50 kB,
    OpenIMP 38, 23 kB, night scene): same-picture input differs there, tracked outside this note.
  QEMU has no T10/T20 H.264 encoder model, so the encoder path is camera-proven only (host: rate controller oracle above).
