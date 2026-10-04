#!/usr/bin/env python3
"""Drive the vendor T31 libimp 1.1.6 Allegro rate controller (modes 8 CappedVBR / 9 CappedQuality)
under unicorn and dump the state after each picture.  RE aid only."""
import sys, struct, random
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import emu
from unicorn import UC_HOOK_CODE
from unicorn.mips_const import *

LIB = '<thingino>/dl/ingenic-lib/git/T31/lib/1.1.6/uclibc/5.4.0/libimp.so'
emu.LIB = LIB

class T31(emu.Lib):
    def __init__(self):
        # code tracking range of the parent is the T23 eprc; it is harmless here
        super().__init__(LIB)
        self.code = {a: self.r32(a) for a in range(0x50000, 0x57000, 4)}
        self.uc.hook_add(UC_HOOK_CODE, self._track, begin=0x50000, end=0x57000)
        # allocator object: vtable {0: ?, 4: alloc(self,size), 8: free, 12: map(handle)}
        self.ALLOC = 0x70080000
        self.uc.mem_write(self.ALLOC, b'\x08\x00\xe0\x03\x00\x00\x00\x00' * 4)   # 4 jr-ra stubs
        self.uc.hook_add(UC_HOOK_CODE, self._alloc_stub, begin=self.ALLOC, end=self.ALLOC + 32)
        self.ALLOCOBJ = self.ALLOC + 0x100
        self.w32(self.ALLOCOBJ, self.ALLOCOBJ + 0x10)          # vtable ptr
        for i in range(4):
            self.w32(self.ALLOCOBJ + 0x10 + 4 * i, self.ALLOC + 8 * i)
        self.pcs = set()
        self.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: self.pcs.add(a), begin=0x54820, end=0x55718)
        self.delta = 0; self.capped = 0
        def at_cap(uc, a, s, _):
            # 0x556b4: beqz v0 (psnr <= cap) ; v0 = (psnr > cap); s5 = delta before the cap
            v = uc.reg_read(UC_MIPS_REG_S5); v = v - (1 << 32) if v & 0x80000000 else v
            self.delta = v; self.capped = uc.reg_read(UC_MIPS_REG_V0)
        self.uc.hook_add(UC_HOOK_CODE, at_cap, begin=0x556b4, end=0x556b4)

    def _alloc_stub(self, uc, addr, size, _):
        i = (addr - self.ALLOC) // 8
        a = [uc.reg_read(r) for r in emu.A]
        if i == 1:      # alloc(allocator, size) -> handle == pointer
            n = a[1]; ret = self.brk; self.brk += (n + 15) & ~15
            uc.mem_write(ret, bytes(n))
        elif i == 3:    # map(handle) -> pointer
            ret = a[1]
        else:
            ret = 0
        uc.reg_write(UC_MIPS_REG_V0, ret)
        uc.reg_write(UC_MIPS_REG_PC, uc.reg_read(UC_MIPS_REG_RA))

    def _stub(self, uc, addr, size, _):
        name = self.stubs.get(addr, '?')
        if name == '__assert':
            raise RuntimeError('vendor assert line %d' % uc.reg_read(UC_MIPS_REG_A2))
        if name == 'log10':
            import math
            x = self.fd(12); r = math.log10(x) if x > 0 else -math.inf
            self.setd(0, r); self.log.append((name, x, r))
            uc.reg_write(UC_MIPS_REG_PC, uc.reg_read(UC_MIPS_REG_RA)); return
        return super()._stub(uc, addr, size, _)

    def callv(self, fn, *args):
        """o32 call with up to 10 args (stack args at 16(sp)...)."""
        uc = self.uc
        sp = emu.STACK - 0x100
        for i, v in enumerate(args[4:]):
            self.w32(sp + 16 + 4 * i, v)
        uc.reg_write(UC_MIPS_REG_SP, sp)
        uc.reg_write(UC_MIPS_REG_T9, fn)
        uc.reg_write(UC_MIPS_REG_RA, emu.END)
        for r, v in zip(emu.A, args[:4]):
            uc.reg_write(r, v & 0xffffffff)
        uc.emu_start(fn, emu.END, count=50_000_000)
        if uc.reg_read(UC_MIPS_REG_PC) != emu.END:
            raise RuntimeError('no return pc=%x' % uc.reg_read(UC_MIPS_REG_PC))
        return uc.reg_read(UC_MIPS_REG_V0)

    def s32(self, a):
        v = self.r32(a); return v - (1 << 32) if v & 0x80000000 else v
    def s16(self, a):
        v = struct.unpack('<h', self.rd(a, 2))[0]; return v

class RC:
    """One vendor controller instance."""
    def __init__(self, L, mode, rcp, gop):
        self.L = L
        self.ctx = L.brk; L.brk += 0x100
        self.rcp = L.brk; L.brk += 0x40
        self.gop = L.brk; L.brk += 0x20
        self.pic = L.brk; L.brk += 0x80
        self.stat = L.brk; L.brk += 0x80
        self.out = L.brk; L.brk += 0x10
        L.wr(self.rcp, rcp); L.wr(self.gop, gop)
        r = L.call('AL_RateCtrl_Init', self.ctx, L.ALLOCOBJ, mode, 0)
        assert r == 1, r
        L.callv(L.r32(self.ctx + 4), self.ctx, self.rcp, self.gop)
        self.st = L.r32(self.ctx + 48)

    def picture(self, ptype, flags, status, size_bits, overflow=0, n8=0):
        L = self.L
        L.wr(self.pic, bytes(0x80)); L.wr(self.stat, bytes(0x80))
        L.w32(self.pic + 4, flags); L.w32(self.pic + 16, ptype)
        for off, v in status.items():
            L.w32(self.stat + off, v)
        L.callv(L.r32(self.ctx + 8), self.ctx, self.pic, self.stat, size_bits, self.out)
        L.pcs.clear()
        L.callv(L.r32(self.ctx + 12), self.ctx, self.pic, self.stat, size_bits, overflow, n8)
        return set(L.pcs)

    def qp(self): return self.L.s16(self.st + 32)
    def st32(self, off): return self.L.s32(self.st + off)
    def hrd(self, off): return self.L.s32(self.st + 72 + off)

def rc_param(mode, fps=25, clk=1000, target=2_000_000, maxbr=4_000_000, qp0=30, qmin=15, qmax=48,
             ipd=-1, pbd=-1, opts=1, pixels=1920*1080, psnr=4200, peak=255, cpb_ticks=90000, rem_ticks=45000):
    b = bytearray(0x40)
    struct.pack_into('<I', b, 0, mode)
    struct.pack_into('<II', b, 4, rem_ticks, cpb_ticks)
    struct.pack_into('<HH', b, 12, fps, clk)
    struct.pack_into('<II', b, 16, target, maxbr)
    struct.pack_into('<hhhhh', b, 24, qp0, qmin, qmax, ipd, pbd)
    struct.pack_into('<I', b, 40, opts)
    struct.pack_into('<I', b, 44, pixels)
    struct.pack_into('<HH', b, 48, psnr, peak)
    return bytes(b)

def gop_param(mode=2, length=25, numb=0):
    b = bytearray(28)
    struct.pack_into('<IHB', b, 0, mode, length, numb)
    return bytes(b)

def sse_for_psnr(psnr_db, pixels):
    mse = 255.0 ** 2 / (10 ** (psnr_db / 10.0))
    return int(mse * pixels)

def status(size_bits, sse, intra_pct=20, skip_pct=30):
    mb = 1920 * 1080 // 256
    return {4: size_bits, 68: size_bits // 8,
            36: mb, 40: 0, 44: 0, 48: 0,          # +36.. block counts (16x16 only -> total = mb)
            28: mb * intra_pct // 100, 32: mb * skip_pct // 100, 20: size_bits * 30 // 100, 24: 0,
            96: sse & 0xffffffff, 100: sse >> 32}

if __name__ == '__main__':
    L = T31()
    for mode in (8, 9):
        rc = RC(L, mode, rc_param(mode), gop_param())
        print('mode %d: st[303]=%d hrd[21]=%d st[34]=%d st[304]=%d target/frame=%d max/frame=%d hrd[0]=%d' % (
            mode, L.rd(rc.st + 303, 1)[0], L.rd(rc.st + 72 + 21, 1)[0], L.s16(rc.st + 34), rc.st32(304),
            rc.st32(140), rc.st32(148), rc.hrd(0)))
