# T31 libimp 1.1.6 rate-controller emulation

Reverse-engineering and verification aid, host only.  Loads the OEM T31
`libimp.so` into unicorn (MIPS32 LE) and drives the Allegro rate controller
(`AL_RateCtrl_Init` and its vtable) with synthetic pictures.  The port in
`src/t40/t31_al_rc.c` is verified against it (`docs/RC_MODES.md`).

Needs: python3, unicorn, pyelftools.  The paths at the top of `emu.py` and
in `cq_emu.py` point at the local toolchain checkout and the OEM library;
adjust them.

* `cq_run.py static|busy|mixed [frames] [-v]`: CappedVBR (mode 8) against
  CappedQuality (mode 9) with identical inputs, QP trace and decision-branch
  counts, plus the check that mode 8 with `state+0x12f = 0` equals mode 9.
* `cq_trace.py <outdir>`: records the traces for
  `tests/t31/al_rc_trace_test.c` (inputs and the full 328-byte state after
  every init / parameter update / reset / picture): the three T31
  configurations (VBR = AL eRCMode 2 with controller mode 1, CappedVBR 4/8,
  CappedQuality 8/9), four seeds each, and two synthetic ones (rc param
  mode 1 for the CBR HRD and filler path, mode 9 for the removal-clock
  slip).  `scenario(name, mode, seed, frames, out, al_mode)` can be called
  for larger local runs.
* `cq_trace.py` also records `cbr_1..4` (IMP CBR: AL eRCMode 1,
  `AL_RateCtrl_Init` mode 0, update `IIii`).
* `cq_random_cbr.py <outdir> <seed_from> <seed_to> <frames>`: random CBR
  traces, including the GOP / rc-param variants the T31 IMP never sets
  (`P34=1 GM='[8,9,10,12]'` biases towards them), and the `IIii`
  instruction coverage; `synthetic_cbrgop_1/2` in the test come from it
  (seeds 103 with `P34=1 GM='[8,9,8|2,12]'`, and 7, 150 pictures).
* `cq_replay.py <trace> <line>`: replays a trace in the OEM code and prints
  the `Ioii` intermediates (targets, idle budget, search result, remaining
  pictures, size predictions, delta before the clamp, PSNR cap) for the
  picture on that line; compile the port with `-DT31_AL_RC_DEBUG` for the
  same values on the other side.
