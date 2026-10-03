#!/usr/bin/env python3
"""Replay a trace file in the vendor controller and print Ioii intermediates
for one picture:  python3 cq_replay.py <trace.trc> <line-number-of-P-line>"""
import struct, sys
import cq_emu
from unicorn import UC_HOOK_CODE
from unicorn.mips_const import *

def s32(v): return v - (1 << 32) if v & 0x80000000 else v

def main():
    path, target_line = sys.argv[1], int(sys.argv[2])
    L = cq_emu.T31()
    lines = open(path).read().split('\n')
    I = lines[0].split()
    rc = cq_emu.RC(L, int(I[1]), bytes.fromhex(I[2]), bytes.fromhex(I[3]))
    R = lambda r: L.uc.reg_read(r)
    def sp(off): return L.r32(R(UC_MIPS_REG_SP) + off)
    def st16(off): return struct.unpack('<h', L.rd(rc.st + off, 2))[0]
    def st32(off): return s32(L.r32(rc.st + off))
    active = [False]
    def hook(uc, a, sz, _):
        if not active[0]:
            return
        if a == 0x54820: print('V enter Ioii: qp=%d p_qp=%d opt_bit0=%d' % (st16(32), st16(28), L.rd(rc.st + 280, 1)[0]))
        if a == 0x54aa4: print('V 54aa4: fl=%d static(80)=%d lvl0(84)=%d' % (R(UC_MIPS_REG_S0), sp(80), s32(sp(84))))
        if a == 0x54eec: print('V 54eec: overflow path')
        if a == 0x54b44: print('V 54b44: level(88)=%d qp=%d size=%d' % (s32(sp(88)), st16(32), sp(148)))
        if a == 0x54c98: print('V 54c98: target s0=%d outT=%d idle5=%d idle=%d' % (s32(R(UC_MIPS_REG_S0)), L.r32(sp(164)), L.r32(sp(168)), L.r32(rc.st + 132)))
        if a == 0x54ddc: print('V 54ddc: after search delta=%d' % s32(L.r32(sp(160))))
        if a == 0x54e70: print('V 54e70: remaining s1=%d cpb s0=%d preds b=%d p=%d 3=%d' % (s32(R(UC_MIPS_REG_S1)), R(UC_MIPS_REG_S0), s32(sp(48)), s32(sp(44)), s32(sp(40))))
        if a == 0x55158: print('V 55158: pred_i s2=%d avail-need v0=%d cpb s0=%d' % (s32(R(UC_MIPS_REG_S2)), s32(R(UC_MIPS_REG_V0)), s32(R(UC_MIPS_REG_S0))))
        if a == 0x552f0: print('V 552f0: I/short-gop branch')
        if a == 0x55298: print('V 55298: before clamp v0=%d' % s32(R(UC_MIPS_REG_V0)))
        if a == 0x556b4: print('V Ooii cap: psnr>cap=%d delta=%d' % (R(UC_MIPS_REG_V0), s32(R(UC_MIPS_REG_S5))))
    for pc in (0x54820, 0x54aa4, 0x54eec, 0x54b44, 0x54c98, 0x54ddc, 0x54e70, 0x55158, 0x552f0, 0x55298, 0x556b4):
        L.uc.hook_add(UC_HOOK_CODE, hook, begin=pc, end=pc)
    for ln_no, ln in enumerate(lines[1:], start=2):
        if ln_no == target_line:
            active[0] = True
        if ln.startswith('P'):
            f = ln.split()
            ptype, flags, qpoff, fixed, forced, size, ovf, extra = map(int, f[1:9])
            stat = dict(zip((20, 24, 28, 32, 36, 40, 44, 48), map(int, f[9:17])))
            qp, sse_lo, sse_hi = map(int, f[17:20])
            L.wr(rc.pic, bytes(0x80)); L.w32(rc.pic + 4, flags); L.w32(rc.pic + 16, ptype)
            L.wr(rc.pic + 37, struct.pack('<b', qpoff)); L.wr(rc.pic + 44, bytes([fixed])); L.wr(rc.pic + 45, struct.pack('<b', forced))
            L.wr(rc.stat, bytes(0x80))
            for k, v in stat.items(): L.w32(rc.stat + k, v)
            L.w16(rc.stat + 60, qp & 0xffff); L.w32(rc.stat + 96, sse_lo); L.w32(rc.stat + 100, sse_hi)
            L.callv(L.r32(rc.ctx + 8), rc.ctx, rc.pic, rc.stat, size, rc.out)
            if active[0]:
                print('P line %d: type=%d flags=%#x ovf=%d size=%d stat_qp=%d filler=%d qp_after_o11i=%d' % (ln_no, ptype, flags, ovf, size, qp, L.s32(rc.out), st16(32)))
            L.callv(L.r32(rc.ctx + 12), rc.ctx, rc.pic, rc.stat, size, ovf, extra)
            if active[0]:
                print('  -> qp %d' % st16(32))
                break
        elif ln.startswith('Q'):
            f = ln.split(); ptype, flags, qpoff, fixed, forced = map(int, f[1:6])
            L.wr(rc.pic, bytes(0x80)); L.w32(rc.pic + 4, flags); L.w32(rc.pic + 16, ptype)
            L.wr(rc.pic + 37, struct.pack('<b', qpoff)); L.wr(rc.pic + 44, bytes([fixed])); L.wr(rc.pic + 45, struct.pack('<b', forced))
            L.callv(L.r32(rc.ctx + 16), rc.ctx, rc.pic, rc.out)
        elif ln.startswith('S'):
            f = ln.split(); L.wr(rc.rcp, bytes.fromhex(f[1])); L.wr(rc.gop, bytes.fromhex(f[2]))
            L.callv(L.r32(rc.ctx + 4), rc.ctx, rc.rcp, rc.gop)
        elif ln.startswith('R'):
            L.callv(L.r32(rc.ctx + 0), rc.ctx)

if __name__ == '__main__':
    main()
