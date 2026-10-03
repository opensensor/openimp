# T31 libimp 1.1.6 rate-controller emulation

Reverse-engineering aid, host only.  Loads the OEM T31 `libimp.so` into
unicorn and drives the Allegro rate controller (`AL_RateCtrl_Init` and its
vtable) with synthetic pictures, so the CappedVBR (mode 8) and CappedQuality
(mode 9) behaviour can be compared picture by picture.  Documented in
`docs/RC_MODES.md`.

Needs: python3, unicorn, pyelftools (the paths at the top of `emu.py` and
`cq_emu.py` point at the local toolchain checkout and the OEM library; adjust
them).

    python3 cq_run.py static 150      # also: busy, mixed; add -v for every picture

Prints the QP trace per mode, counts of the decision branches (`gate` =
0x55218 reached, `skip` = CappedVBR skipped the QP-lowering search, `down`/
`up` = search ran) and the check that mode 8 with `state+0x12f = 0` and
`hrd+0x15 = 0` equals mode 9 and vice versa.
