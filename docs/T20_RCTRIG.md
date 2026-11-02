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
- Cameras, corrected 2026-11-03: the camera runs of 2026-11-02 that this section described as "vendor library and OpenIMP"
  used the installed libimp, which is OpenIMP on both test cameras: they compared OpenIMP with itself and are withdrawn.
  Real A/B (vendor 3.12.0 and OpenIMP next via `LD_LIBRARY_PATH`, the library of every run checked by its log lines:
  OpenIMP `[KernelIF]` / `[VBM]`, vendor `i264e`; `apitest` case `rctrig`, CBR 640x360, T20 17 and T10 8 scenarios):
  - A call during the stream (picture 40): the vendor codes on without an IDR and without a size change; OpenIMP restarts its
    controller with an IDR at QP 15 and then codes much larger P pictures (T20: 188 kB against 38 kB for 120 pictures,
    T10: 312 kB against 32 kB for 150 pictures). Not vendor-equal (follow-up issue).
  - Lift ceiling of a starved stream: T10 level 0.5 / 1.5 with new maximum 51: vendor up to 51, OpenIMP 42; T20 level 0.5 /
    51: vendor 37, OpenIMP 48. The T20 run without any call already differs (mean QP 26 vendor, 34 OpenIMP), so not all of
    it is the trigger.
  Vendor equality of the trigger decision itself rests on the host oracle above.
