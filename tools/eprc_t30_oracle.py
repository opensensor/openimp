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

    tools/eprc_t30_oracle.py T30/lib/1.0.5/uclibc/4.7.2/libimp.so > tests/eprc/eprc_t30_vectors.txt
    tools/eprc_t30_oracle.py LIB --real [--random SEED N [FRAMES]]

The OEM T30 i264e codes an IDR whenever the picture count since the last
IDR is a multiple of the GOP (i264e_ratecontrol_start: [0] % gop).  '--real'
replaces the synthetic statistics by counts in the ranges of a real picture
(and a non-zero channel cmpx, the MAD input), so that the scene classes and
the MAD model run on finite values; the S line then carries a 19th field 1.

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


def real_statistics(frame, w, h):
    """statistics(frame) scaled into the ranges of a real picture, and the
    channel cmpx (as the test)."""
    regs = list(statistics(frame))
    mbs = ((w + 15) // 16) * ((h + 15) // 16)
    k = (frame // 20) % 5
    regs[0] = ((regs[0] % max(1, mbs // (1 + k))) & 0x7fff) | \
              (((regs[4] % max(1, mbs // 3)) & 0x7fff) << 16)
    regs[1] = (regs[1] % (mbs * (1 + 20 * k))) & 0x7ffffff
    regs[2] = (regs[2] % (mbs * (1 + 10 * k))) & 0x7ffffff
    for j in (10, 11, 12):
        regs[j] = regs[j] % max(1, mbs)
    cmpx = 1 + (regs[3] % 40000) * (1 + (frame // 30) % 3)
    return regs, cmpx


def main():
    lib = sys.argv[1]
    args = sys.argv[2:]
    real = '--real' in args
    if real:
        args.remove('--real')
    frames = 200 if real else 120
    scenarios = SCENARIOS
    if len(args) > 1 and args[0] == '--sc':
        # explicit scenarios: --sc "18 numbers;18 numbers" [FRAMES]
        scenarios = [tuple(int(x) for x in t.split()) for t in args[1].split(';')]
        frames = int(args[2]) if len(args) > 2 else frames
        args = []
    if len(args) > 2 and args[0] == '--random':
        scenarios = random_scenarios(int(args[1]), int(args[2]))
        frames = int(args[3]) if len(args) > 3 else 150
    for n, sc in enumerate(scenarios):
        (mode, w, h, gop, fps, minqp, maxqp, br, mbr, fstep, gstep, bias, static,
         cpos, qual, bgmul, initqp, idr) = sc[:18]
        s = SimT30(lib, mode=mode, w=w, h=h, gop=gop, fps=(fps, 1), minqp=minqp,
                   maxqp=maxqp, bitrate=br, maxbitrate=mbr, frm_step=fstep, gop_step=gstep,
                   bias=bias, static_time=static, change_pos=cpos, quality=qual,
                   initqp=initqp)
        print('S', ' '.join(str(v) for v in sc) + (' 1' if real else ''))
        for f in range(frames):
            since = f % idr
            is_idr = since % gop == 0
            qp = s.start(since)
            out = [qp]
            size = picture_bytes(f, qp, is_idr, w * h, n % 2)
            if real:
                regs, s.cmpx = real_statistics(f, w, h)
            else:
                regs = statistics(f)
            while True:
                r = s.end(size, regs, is_idr, qp)
                if r != 1:
                    break
                qp = s.qp()
                out.append(qp)
                size = max(size // 2, 20)
            print('F', since, ' '.join(str(v) for v in out))


if __name__ == '__main__':
    main()


# ---------------------------------------------------------------- H.265
# The i265e glue (I265eRatectrl, i265e object = i265e_param_t copy at +0):
#   rcInit(this, i265e)     E = this + 0x60; i265e +0x14/+0x18 fps, +0x1c gop,
#                           +0x28/+0x2c size, +0x48 mode, +0x4c QP, +0x50 bit
#                           rate, +0x60 double, +0x70/+0x74 frame/GOP QP step,
#                           +0x9c/+0xa0 max/min QP, +0xa4 I bias, +0xa8 static
#                           time, +0xac, +0xb4 change pos, +0xb8 quality
#   rcStart(this, common)   common +0x28 = frame: +0x778 slice type (2 I),
#                           +0xd54 parameter block (E+0x234), +0xd4c (E+0x8c);
#                           QP at frame +0x77c
#   rcReEncode / rcEnd(this, task)  task +0x28 = frame: +0xea0 bytes, +0xd44
#                           the (register, value) list of the 19 Radix
#                           statistics registers
STAT_REGS_265 = (0x40088, 0x4008c, 0x400a0, 0x400a4, 0x400b0, 0x400b8, 0x40028,
                 0x4002c, 0x200a0, 0x200a4, 0x200a8, 0x200ac, 0x200b0, 0x200b4,
                 0x200b8, 0x200bc, 0x20128, 0x2012c, 0x20130)
RC265 = dict(init='_ZN8I265E_NS13I265eRatectrl6rcInitEP5i265e',
             start='_ZN8I265E_NS13I265eRatectrl7rcStartERNS_15I265eEncodeTaskERNS_17I265eEncodeCommonE',
             end='_ZN8I265E_NS13I265eRatectrl5rcEndEPNS_15I265eEncodeTaskE',
             reenc='_ZN8I265E_NS13I265eRatectrl10rcReEncodeEPNS_15I265eEncodeTaskE')


class SimT30H265:
    def __init__(self, lib, mode=1, w=1920, h=1080, gop=50, fps=(25, 1), minqp=15, maxqp=45,
                 bitrate=1000, frm_step=3, gop_step=15, bias=0, static_time=2, change_pos=80,
                 quality=4, qp=42, fluct=2):
        L = self.L = Lib(lib)
        self.TH = HEAP + 0x10000            # I265eRatectrl
        self.P = HEAP + 0x11000             # i265e (param copy)
        self.F = HEAP + 0x20000             # frame (0x2100)
        self.C = HEAP + 0x13000             # encode common / task (+0x28 = frame)
        self.IO = HEAP + 0x14000            # statistics list
        self.BLK = HEAP + 0x30000           # parameter block 0x1204
        self.MI = HEAP + 0x40000
        L.wr(HEAP, bytes(0x50000))
        L.call('i265e_param_default', self.P)
        P = self.P
        for o, v in ((0x14, fps[0]), (0x18, fps[1]), (0x1c, gop), (0x28, w), (0x2c, h),
                     (0x48, mode), (0x4c, qp), (0x50, bitrate), (0x70, frm_step),
                     (0x74, gop_step), (0x9c, maxqp), (0xa0, minqp), (0xa4, bias),
                     (0xa8, static_time), (0xac, fluct), (0xb4, change_pos), (0xb8, quality)):
            L.w32(P + o, v)
        L.call(RC265['init'], self.TH, P)
        self.E = self.TH + 0x60
        F = self.F
        L.w32(F + 0xd54, self.BLK)
        L.w32(F + 0xd4c, self.MI)
        L.w32(F + 0xd44, self.IO)
        L.w32(self.C + 0x28, F)
        for i, o in enumerate(STAT_REGS_265):
            L.w32(self.IO + 8 * i, 0x13600000 + o)

    def qp(self):
        return self.L.r32(self.F + 0x77c) & 0xff

    def start(self, idr):
        self.L.w32(self.F + 0x778, 2 if idr else 1)
        self.L.call(RC265['start'], self.TH, self.C)
        return self.qp()

    def end(self, nbytes, regs):
        L = self.L
        for i, v in enumerate(regs):
            L.w32(self.IO + 8 * i + 4, v)
        L.w32(self.F + 0xea0, nbytes)
        if L.call(RC265['reenc'], self.TH, self.C) == 1:
            return 1
        L.call(RC265['end'], self.TH, self.C)
        return 0
