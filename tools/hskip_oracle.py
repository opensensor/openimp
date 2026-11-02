#!/usr/bin/env python3
"""Issues #3/#5 (development aid): reference vectors for src/hskip from the OEM encoder.

Runs i264e_init_skip_header / i264e_decide_slice_type_and_rd / i264e_dec_ref_pic_remark /
i264e_get_prev_fram_dec of a vendor T10/T20 3.12.0 libimp.so under the unicorn CPU emulator (see eprc_oracle.py for the
loader) on a synthetic encoder state and prints, for each scenario (GOP, skip type, m, n, maxSameSceneCnt) and each of
240 pictures, what the OEM decides: IDR counter, picture class, frame type (0 IDR, 1 LBASE, 2 SBASE, 3 ENHANCE),
reference and reconstruction buffer, reference list modification and MMCO commands, the re-encode variants.  Nothing of
the vendor library is copied into OpenIMP; it is read from the path you pass.

    pip install unicorn pyelftools
    tools/hskip_oracle.py T20/lib/3.12.0/uclibc/4.7.2/libimp.so > tests/hskip/hskip_vectors.txt
    tools/hskip_oracle.py T20/lib/3.12.0/uclibc/4.7.2/libimp.so --black > tests/hskip/black_vectors.txt

Vector lines: 'S gop type m n same : A B buffers' then per picture 'frame_num idr ftype ref recon ltr
reorder reorder(idc*16+value) mmco_count'.
"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import eprc_oracle as O

O.LAYOUT_T21['code'] = (0x2a000, 0x40000)      # i264e text of the 3.12.0 library (unaligned-access fix-up range)
SH = 0x905f4            # skip header in the i264e handle (SH[0] gop, [1] maxSameSceneCnt, [2] type, [3..5] derived)
FR = 0x90640            # three frame records of 0xa0 bytes
CTX = 0x921e8           # pointer to the picture context
SCEN = []
for gop in (30, 8, 2):
    for typ in range(7):
        for (m, n, same) in ((gop - 1, 1, 0), (gop - 1, 1, 3), (4, 2, 0), (3, 3, 5)):
            SCEN.append((gop, typ, m, n, same))

def run(L, gop, typ, m, n, same, frames=240):
    H = O.HEAP
    C = H + 0x200000            # picture context
    P = H + 0x300000            # struct behind ctx[0xe0] (+0x44 = force IDR)
    L.wr(H, bytes(0x400000))
    L.w32(H + 40, gop)
    L.call('i264e_init_skip_header', H + SH, gop, typ, m, n, same, 2, H + 0x3f0000)
    sh = [L.r32(H + SH + 4 * i) for i in range(6)]
    L.w32(H + CTX, C)
    L.w32(C + 4 * 0xe0, P)
    L.w32(C, 0xffffffff)
    out = []
    for f in range(frames):
        L.w32(C + 4 * 0xb8, 0)
        for o in (0x208, 0x209, 0x2a5):
            L.w32(C + 4 * o, 0)
        L.call('i264e_decide_slice_type_and_rd', H, C)
        r = lambda o: L.r32(C + 4 * o)
        ref = r(0xe2)
        ri = -1 if ref == 0 else (ref - (H + FR)) // 0xa0
        ci = (r(0xe3) - (H + FR)) // 0xa0
        nm = r(0x2a5)
        # idr counter(frame_num) class ftype ref recon ltr reorder(idc,val) mmco(count)
        out.append('%d %d %d %d %d %d %d %d %d' % (r(0), 1 if r(0xb8) == 1 else 0, r(0x45e), ri, ci,
                                                   1 if nm == 1 else 0, r(0x208), r(0x20a) * 16 + r(0x20b) if r(0x208) else 0,
                                                   nm if nm == 2 else 0))
    return sh, out

def black(L, mbs):
    """i264e_icodec_black: the slice data of an all-skip picture of mbs macroblocks (>= 135 for a valid string)."""
    H = O.HEAP
    C, B = H + 0x200000, H + 0x300000
    L.wr(H, bytes(0x400000))
    L.w32(C + 0x1240, H)
    L.w32(H + 0x90484, mbs)
    L.w32(H + 0x90488, 1)
    L.w32(C + 0x390, B)
    L.call('i264e_icodec_black', C)
    n = L.r32(C + 0xef4)
    return n, L.rd(B, n).hex()


if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[2] == '--black':
        L = O.Lib(sys.argv[1])
        for mbs in sorted(set(list(range(135, 8300, 13)) + [144, 240, 920, 1350, 3600, 8160, 8299])):
            n, hx = black(L, mbs)
            print('0 0 %d %d %s' % (mbs, n, hx))
        sys.exit(0)
    L = O.Lib(sys.argv[1])
    for (gop, typ, m, n, same) in SCEN:
        try:
            sh, lines = run(L, gop, typ, m, n, same)
        except Exception as e:          # the OEM traps (divide by zero) for a spacing of 0, e.g. HN1 with gop 2
            sys.stderr.write('skipped %s: %s\n' % ((gop, typ, m, n, same), e))
            continue
        print('S %d %d %d %d %d : %d %d %d' % (gop, typ, m, n, same, sh[3], sh[4], sh[5]))
        for line in lines:
            print(line)
