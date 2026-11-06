#!/usr/bin/env python3
"""Reference vectors for src/eprc/eprc_t30.c from the OEM T30 rate controller
(development aid).

Runs the rate-control entry points of a vendor T30 1.0.5 libimp.so
(i264e_ratecontrol_init / _start / _is_reenc with i264e parameter [0] = 3,
which drive eprc_default_set_T30 / JZ_VPU_RC_VIDEO_CFG_T30 /
FRAME_START_T30 / FRAME_REPEATE_JUDGE_T30 / FRAME_END_T30) under the unicorn
CPU emulator on synthetic picture sizes and statistics, and prints the
picture type and QP sequence that tests/eprc/eprc_test.c checks
EPRC30_* against.  Nothing from the vendor library is copied into OpenIMP;
it is read from the path you pass.  The emulator, the scenarios and the
picture-size and statistics models are those of tools/eprc_oracle.py.

    tools/eprc_t30_oracle.py T30/lib/1.0.5/uclibc/5.4.0/libimp.so > tests/eprc/eprc_t30_vectors.txt
    tools/eprc_t30_oracle.py LIB --random SEED N [FRAMES]

The T30 i264e glue (i264e parameters in the T20 layout, controller block E
at i264e parameters + 0x9044c):
  i264e_ratecontrol_init  [40] gop, [44]/[48] size, [188] mode, [192] FIXQP
                          QP, [196]/[200] min/max QP, [216] bit rate,
                          [228] I bias, [232] frame / [236] GOP QP step,
                          [244] static time, [248] max bit rate, [252]
                          change pos, [256] quality, [280]/[284] super-frame
                          sizes (i264e_validate_parameters); a1 > 0: init QP
  i264e_ratecontrol_start handle [0] pictures since IDR, +0x1050 parameters;
                          QP at E+0x228, its check word at E+0x220
  i264e_ratecontrol_is_reenc  handle +0xcf8 coded bytes, +0x2e0 frame type
                          (1 IDR), +0x2e4 QP, +0xb50 cmpx, +0x38c statistics
                          block, +0xba8 the (register, value) table of the 25
                          Helix statistics registers (EPRC30_StatRegs)
  both                    handle +0xbb0 the slice-parameter block
                          (H264E_T30_SliceInit input, E+0x234), +0xba4 the
                          macroblock input block (E+0x8c)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from eprc_oracle import (Lib, HEAP, SCENARIOS, picture_bytes, random_scenarios,
                         statistics)

# the 25 registers the OEM T30 encoder reads after each picture (soc_vpu
# ioctl 0xc0386307 at 0x13200000 + register), in the order of its table
STAT_REGS = (0x800e4, 0x800e8, 0x800ec, 0x80034, 0x80088, 0x8008c, 0x40094,
             0x40098, 0x4009c, 0x400a0, 0x400b8, 0x400bc, 0x400c0,
             0x80120, 0x80124, 0x80128, 0x8012c, 0x80130, 0x80134, 0x80138,
             0x8013c, 0x80140, 0x80144, 0x80148, 0x8014c)
E_OFF = 0x9044c


class SimT30:
    def __init__(self, lib, mode=3, w=1920, h=1080, gop=25, fps=(25, 1), minqp=15, maxqp=45,
                 bitrate=1000, maxbitrate=2000, frm_step=3, gop_step=15, bias=0,
                 static_time=2, change_pos=80, quality=4, initqp=-1, cqp=0, cmpx=0):
        L = self.L = Lib(lib)
        if L.lay['name'] != 'T30':
            raise RuntimeError('not a T30 library')
        self.H = HEAP + 0x10000             # i264e handle (0x1060 bytes)
        self.D = HEAP + 0x14000             # statistics block (handle +0x38c)
        self.TB = HEAP + 0x15000            # register table (handle +0xba8)
        self.R = HEAP + 0x100000            # i264e parameters (E at +0x9044c)
        self.SL = HEAP + 0x30000            # slice parameters (handle +0xbb0 -> E+0x234)
        self.MI = HEAP + 0x40000            # handle +0xba4 -> E+0x8c (macroblock input)
        self.cmpx = cmpx
        L.wr(HEAP, bytes(0x50000))
        L.wr(self.H, bytes(0x8000))
        L.wr(self.R, bytes(0x92000))
        R = self.R
        sizes = validated_sizes(L, w, h)
        L.call('i264e_param_default', R)
        L.w32(R, 3)                         # the T30 controller
        for o, v in ((40, gop), (44, w), (48, h), (188, mode), (192, cqp), (196, minqp),
                     (200, maxqp), (216, bitrate), (228, bias), (232, frm_step),
                     (236, gop_step), (244, static_time), (248, maxbitrate),
                     (252, change_pos), (256, quality), (396, fps[0]), (400, fps[1]),
                     (280, sizes[0]), (284, sizes[1])):
            L.w32(R + o, v)
        L.call('i264e_ratecontrol_init', R, initqp & 0xffffffff)
        H = self.H
        L.w32(H + 0x1050, R)
        L.w32(H + 0x38c, self.D)
        L.w32(H + 0xba8, self.TB)
        L.w32(H + 0xbb0, self.SL)
        L.w32(H + 0xba4, self.MI)
        for i, o in enumerate(STAT_REGS):
            L.w32(self.TB + 8 * i, o)
        self.E = R + E_OFF

    def qp(self):
        return self.L.rd(self.E + 0x228, 1)[0]

    def start(self, since):
        L = self.L
        L.w32(self.H, since)
        L.call('i264e_ratecontrol_start', self.H)
        return self.qp()

    def end(self, nbytes, regs, idr, qp):
        L = self.L
        for i, v in enumerate(regs):
            L.w32(self.TB + 8 * i + 4, v)
        L.w32(self.H + 0xcf8, nbytes)
        L.w32(self.H + 0x2e0, 1 if idr else 3)
        L.w32(self.H + 0x2e4, qp)
        L.w32(self.H + 0xb50, self.cmpx)
        return L.call('i264e_ratecontrol_is_reenc', self.H)


def validated_sizes(L, w, h):
    """param[280], param[284] after i264e_validate_parameters."""
    R = HEAP + 0x200000
    L.wr(R, bytes(0x1000))
    L.call('i264e_param_default', R)
    L.w32(R + 44, w)
    L.w32(R + 48, h)
    L.call('i264e_validate_parameters', R)
    return L.r32(R + 280), L.r32(R + 284)


def main():
    lib = sys.argv[1]
    frames = 120
    scenarios = SCENARIOS
    if len(sys.argv) > 4 and sys.argv[2] == '--random':
        scenarios = random_scenarios(int(sys.argv[3]), int(sys.argv[4]))
        frames = int(sys.argv[5]) if len(sys.argv) > 5 else 150
    for n, sc in enumerate(scenarios):
        (mode, w, h, gop, fps, minqp, maxqp, br, mbr, fstep, gstep, bias, static,
         cpos, qual, bgmul, initqp, idr) = sc[:18]
        s = SimT30(lib, mode=mode, w=w, h=h, gop=gop, fps=(fps, 1), minqp=minqp,
                   maxqp=maxqp, bitrate=br, maxbitrate=mbr, frm_step=fstep, gop_step=gstep,
                   bias=bias, static_time=static, change_pos=cpos, quality=qual,
                   initqp=initqp)
        print('S', ' '.join(str(v) for v in sc))
        for f in range(frames):
            since = f % idr
            qp = s.start(since)
            out = [qp]
            size = picture_bytes(f, qp, since == 0, w * h, n % 2)
            regs = statistics(f)
            while True:
                r = s.end(size, regs, since == 0, qp)
                if r != 1:
                    break
                qp = s.qp()
                out.append(qp)
                size = max(size // 2, 20)
            print('F', since, ' '.join(str(v) for v in out))


if __name__ == '__main__':
    main()
