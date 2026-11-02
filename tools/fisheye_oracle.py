#!/usr/bin/env python3
"""Issues #3/#5 (development aid): reference vectors for src/fisheye from the OEM T10/T20 libimp 3.12.0.

IMP_Encoder_SetFisheyeEnableStatus goes to i264e parameter word 0x72.  The only reader the library has is
i264e_icodec_enc (called by i264e_slice_write for every try of every picture): with the flag set it clears the
56-byte ROI table of the picture (8 regions x 7 bytes {enable, relative, qp, x0, x1, y0, y1}, context +0xc) and, for an
I slice (slice type 2) or a picture at a whole second (counter * fps_den % fps_num == 0), sets region 0 to
{1, 0, min(slice qp, 15), 0, 3, 0, 1}: the top-left 64x32 pixels at QP 15 or better.  With the flag clear the table
is left alone.

T21 (issue #7): libimp 1.0.33 (both gcc builds) has the same function; the layout differs only by two words in the
encoder handle (flag at +0x1d0, frame rate numerator +0x194, denominator +0x198; T10/T20: +0x1c8, +0x18c, +0x190).  The
layout is picked from the library.  Both T21 builds give output byte-identical to the T10/T20 vectors
(tests/fisheye/fisheye_vectors.txt; `tools/fisheye_oracle.py <T21 libimp> | cmp - tests/fisheye/fisheye_vectors.txt` after the
2-line comment header).

This script runs the real vendor i264e_icodec_enc under the unicorn CPU emulator on a synthetic picture context and
prints, per case, the ROI table the function hands to hwicodec_process.  tests/fisheye/fisheye_test.c checks
src/fisheye against the output.  Nothing of the vendor library is copied into OpenIMP; it is read from the path you pass.

    pip install unicorn pyelftools
    tools/fisheye_oracle.py T20/lib/3.12.0/uclibc/4.7.2/libimp.so > tests/fisheye/fisheye_vectors.txt
    tools/fisheye_oracle.py T21/lib/1.0.33/uclibc/4.7.2/libimp.so | cmp - <(tail -n +3 tests/fisheye/fisheye_vectors.txt)

Lines: 'C flag stype counter fps_num fps_den qp : table' (table = 56 bytes hex; the context table is pre-filled with
the bytes 0xa5 + i, an "application ROI", before the call).
"""
import os
import random
import subprocess
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import eprc_oracle as O
from unicorn import UC_HOOK_CODE
from unicorn.mips_const import *

O.LAYOUT_T21['code'] = (0x4d000, 0x4e000)       # T10/T20 i264e_icodec_enc (unaligned-access fix-up range)
O.LAYOUT_T21['code21'] = (0x38000, 0x3a000)     # T21 1.0.33, both gcc builds
CTX = O.HEAP + 0x100000
ENC = O.HEAP + 0x200000


class Vendor:
    def __init__(self, path):
        if 'eprc_default_set_T21' in subprocess.run(['strings', '-a', path], capture_output=True, text=True).stdout:
            self.t21 = True
            O.LAYOUT_T21['code'] = O.LAYOUT_T21['code21']
        else:
            self.t21 = False
        self.L = L = O.Lib(path)
        if self.t21:
            # the GOT slot hwicodec_process is called through: the last 'lw t9, N(gp)' before the final 'jr t9'
            a = L.syms['i264e_icodec_enc']
            w = [L.r32(a + 4 * i) for i in range(4)]
            gp = a + ((w[0] & 0xffff) << 16) + ((w[1] & 0xffff) - (0x10000 if w[1] & 0x8000 else 0))
            end = next(a + 4 * i for i in range(2, 600) if L.r32(a + 4 * i) == 0x03200008)
            ins = next(L.r32(end - 4 * k) for k in range(1, 8) if (L.r32(end - 4 * k) >> 16) == 0x8f99)
            off = ins & 0xffff
            got = gp + off - (0x10000 if off & 0x8000 else 0)
        else:
            got = 0xfc9a0                          # PTR_hwicodec_process
        if L.r32(got) == 0:
            L.w32(got, L.syms['hwicodec_process'])
        L.uc.hook_add(UC_HOOK_CODE, self._stop, begin=L.syms['hwicodec_process'], end=L.syms['hwicodec_process'] + 4)

    def _stop(self, uc, addr, size, _):
        uc.reg_write(UC_MIPS_REG_V0, 0)
        uc.reg_write(UC_MIPS_REG_PC, uc.reg_read(UC_MIPS_REG_RA))

    def run(self, flag, stype, counter, fnum, fden, qp):
        L = self.L
        L.wr(CTX, bytes(0x10000))
        L.wr(ENC, bytes(0x100000))
        L.w32(CTX + (0x12b0 if self.t21 else 4 * 0x490), ENC)
        L.w32(CTX, counter)
        L.w32(CTX + 4 * 0x1f6, stype)
        L.w32(CTX + 4 * 0xb9, qp)
        sh = 8 if self.t21 else 0                  # T21: two words further
        L.w32(ENC + 4 * 0x72 + sh, flag)
        L.w32(ENC + 4 * 99 + sh, fnum)
        L.w32(ENC + 4 * 100 + sh, fden)
        L.wr(CTX + 12, bytes((0xa5 + i) & 0xff for i in range(56)))
        L.call('i264e_icodec_enc', CTX)
        return L.rd(CTX + 12, 56)


def cases():
    r = random.Random(7)
    fps = [(25, 1), (15, 1), (10, 1), (30000, 1001), (7, 2), (1, 1)]
    for flag in (0, 1):
        for stype in (0, 1, 2):
            for qp in (0, 14, 15, 16, 51):
                for fnum, fden in fps:
                    for c in list(range(0, 2 * max(fnum // fden, 1) + 2, 1 if fnum <= 30 else 4001)):
                        yield flag, stype, c, fnum, fden, qp
            for _ in range(40):
                fnum, fden = r.choice(fps)
                yield flag, stype, r.getrandbits(32), fnum, fden, r.randint(0, 51)
    for c in (0, 25, 30000, 60000, 4294967295 - 4294967295 % 25):
        yield 1, 0, c, 30000, 1001, 20


def main():
    V = Vendor(sys.argv[1])
    for flag, stype, c, fnum, fden, qp in cases():
        t = V.run(flag, stype, c, fnum, fden, qp)
        print('C %d %d %d %d %d %d : %s' % (flag, stype, c, fnum, fden, qp, t.hex()))


if __name__ == '__main__':
    main()
