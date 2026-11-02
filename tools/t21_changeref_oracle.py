#!/usr/bin/env python3
"""Issue #7 (development aid): IMP_Encoder_SetChangeRef has no effect on the T21 1.0.33 library.

The SetChangeRef second try of T10/T20 (i264e_slice_write codes a picture of the H1M classes again with the third buffer
as reference) is behind the flag word picture+0x1284.  On T21 nothing sets that flag: i264e_ratecontrol_is_reenc writes 0
to it, i264e_ratecontrol_is_reenc_end is only called with it set, i264e_slice_write only clears it.  This tool runs
i264e_ratecontrol_start / _is_reenc of the vendor library under unicorn on the seven eprc oracle scenarios (240 pictures
each, the re-encode judge repeated until it accepts) with the skip header on H1M_FALSE / H1M_TRUE (the classes of the
second try), the ChangeRef parameter (encoder +0x118) on and off, and checks after every judge call that the flag is
0 and that the judge's decisions do not depend on ChangeRef.  Expected: "second-try flag set: 0, ChangeRef dependent: 0".

    tools/t21_changeref_oracle.py T21/lib/1.0.33/uclibc/5.4.0/libimp.so
"""
import os
import struct
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import eprc_oracle as O

lib = sys.argv[1]

def run(sc, chref, hstype, hsft):
    (mode, w, h, gop, fps, minqp, maxqp, br, mbr, fstep, gstep, bias, static, cpos, qual, bgmul, initqp, idr) = sc[:18]
    extra = O.validated_vbv(lib, w, h)
    extra[204] = 0
    s = O.Sim(lib, mode=mode, w=w, h=h, gop=gop, fps=(fps, 1), minqp=minqp, maxqp=maxqp, bitrate=br, maxbitrate=mbr,
              frm_step=fstep, gop_step=gstep, bias=bias, static_time=static, change_pos=cpos, quality=qual, bgmul=bgmul,
              rclevel=initqp & 0xffffffff, extra=extra)
    L = s.L
    out = []
    flagset = 0
    L.w32(s.R + 0x118, chref)                   # encoder +0x118: the ChangeRef parameter
    L.w32(s.R + 0xa9c, hstype)                  # skip header: type (SH[4]) and the frame type of the current picture
    L.w32(s.R + 0xad0, hsft)
    for f in range(240):
        since = f % idr
        L.w32(s.H, since)
        L.call('i264e_ratecontrol_start', s.H)
        t = L.rd(s.SL, 1)[0]
        qp = L.rd(s.R + L.lay['qp'], 1)[0]
        size = O.picture_bytes(f, qp, since == 0, w * h, 1)
        regs = O.statistics(f)
        while True:
            for i, v in enumerate(regs):
                L.w32(s.TB + 8 * i + 4, v)
            L.w32(s.H + 3888, size)
            L.w32(s.H + 736, 1)
            r = L.call('i264e_ratecontrol_is_reenc', s.H)
            flagset += L.r32(s.H + 0x1284) != 0
            out.append((t, qp, size, r))
            if r != 1:
                break
            qp = L.rd(s.R + L.lay['qp'], 1)[0]
            size = O.picture_bytes(f, qp, since == 0, w * h, 1)
    return out, flagset

flags = dep = pics = 0
for n, sc in enumerate(O.SCENARIOS):
    for (ty, ft) in ((5, 2), (6, 1)):
        a, fa = run(sc, 0, ty, ft)
        b, fb = run(sc, 1, ty, ft)
        flags += fa + fb
        dep += a != b
        pics += len(a) + len(b)
        print(n, 'H1M_FALSE SBASE' if ty == 5 else 'H1M_TRUE LBASE', len(a), 'judge calls, ChangeRef 0/1:', 'identical' if a == b else 'DIFF',
              'flag set:', fa + fb)
print('judge calls:', pics, ' second-try flag set: %d, ChangeRef dependent: %d' % (flags, dep))
