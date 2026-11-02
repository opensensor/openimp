# T10 / T20: IMP_Encoder_SetChnRcTrigLevel (issues #3, #5)

`SetChnRcTrigLevel(chn, level, mode)` of the vendor libimp 3.12.0 (T10 and T20 are one binary) is a rate controller input,
not a stored value. `i264e_reconfig_rc_trig_set` keeps the pair and flags a reconfiguration, `i264e_idr_reconfig` copies it
into the controller parameters (words 260 and 264) with the next IDR and restarts the controller. While the bits of a GOP are
above `level` times its budget the controller lifts the QP cap from the application's maximum QP (`maxQp` of the rate
control attribute) to the new maximum `mode` (defaults 3.0 and 51). A starved stream therefore stays at the application's
maximum QP only if `mode` is set to that maximum. A channel that is not created (or H.265) ignores the call, a channel number
of 6 or more returns -1.

OpenIMP: `IMP_Encoder_SetChnRcTrigLevel` hands the pair to the T10/T20 controllers (`src/rc_t20`, `src/rc_t10`, both ported
from the vendor code). The level is used as given (the vendor rounds it to an integer between 2 and 10 only when the channel is
created, not for the run-time call). `mode` is clamped to 1..51 and raised to the application's maximum QP: the vendor stores
the raw value and lets the controller compute a byte difference that wraps for a value below the maximum; OpenIMP keeps
"no lift" instead.

T21: the vendor controller does not read the pair (see `docs/T23_EPRC.md`), OpenIMP stores it like the vendor.

## Proof

- Host: `tools/rc_t20_oracle.py --trig-wide` runs the vendor controller under unicorn with levels 0.5 to 12.5 and new maximum
  QPs 30 to 51 (also below the application maximum): `tests/rc_t20/rc_t20_vectors_trig.txt` (24 scenarios, 7357 decisions) and
  `tests/rc_t10/rc_t10_vectors_trig.txt` (24 scenarios, 6392 decisions), 0 differences.
- Cameras (T20 and T10 test cameras, vendor library and OpenIMP on the same camera, no flash, no module touched):
  `apitest` case `rctrig` (`APITEST_CASES=rctrig`, variables `RCTRIG_*` in `tools/apitest/at_enc.c`), CBR 640x360 at 3 kbit/s
  with maximum QP 36 so that the GOP is starved. QPs are read from the stream with `tools/fisheye_mbqp.py`.
  - The picture QP stays at the application maximum unless the scene needs more than `level` times the budget; the lift
    appears with both libraries under the same settings and with the same ceiling (new maximum 45: 45 and 45, 51: 51 and 51,
    49 and 49), the default pair (3.0 / 51) and a pair with `mode` equal to the maximum never lift beyond it.
  - A call during the stream (picture 40): both libraries code an IDR at that picture, restart at QP 15 and the next 30 picture
    QPs are identical (15 ... 23), picture sizes within 10 percent.
  - Streams decode without errors (ffmpeg, strict flags) and show a real picture (mean luma about 120 on the T20 camera).
  The lift depends on the live scene (the same pair lifts in some runs and not in others, on both libraries), so camera runs
  compare ceilings and restart behaviour, not byte counts.
