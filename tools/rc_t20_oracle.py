#!/usr/bin/env python3
"""Reference vectors for src/rc_t20 from the OEM T20 rate controller.

Runs the rate-control entry points of a vendor T20 3.12.0 libimp.so
(i264e_ratecontrol_init / _start / _is_reenc, T20 branch, which drive
JZ_VPU_RC_VIDEO_CFG_T20 / JZ_VPU_RC_FRAME_RC_T20 /
JZ_VPU_RC_FRAME_REPEATE_JUDGE_T20) under the unicorn CPU emulator on
synthetic picture sizes and statistics, and prints the picture type / QP /
re-encode sequence that tests/rc_t20/rc_t20_test.c checks OpenIMP's
src/rc_t20 against.  Nothing from the vendor library is copied into
OpenIMP; it is read from the path you pass.

    pip install unicorn pyelftools
    tools/rc_t20_oracle.py T20/lib/3.12.0/uclibc/4.7.2/libimp.so > tests/rc_t20/rc_t20_vectors.txt
    tools/rc_t20_oracle.py --t10 T20/lib/3.12.0/uclibc/4.7.2/libimp.so 16 120 > tests/rc_t10/rc_t10_vectors.txt

--t10 runs the T10 branch of the same library (param[0] = 1, what
get_cpu_id selects on a T10: JZ_VPU_RC_VIDEO_CFG / JZ_VPU_RC_FRAME_RC /
JZ_VPU_RC_FRAME_REPEATE_JUDGE) for tests/rc_t10; no macroblock scenarios.

Scenarios 12 and up run the macroblock rate control (param[268], the OEM
default) on synthetic luma pictures (rc_t20_test.c draws the same ones):
H264_SMA_CalMBFlag uses Ingenic MXU2 SIMD instructions, which the code
hook below executes.  libm calls are answered by the host libm; uClibc's
logf/log2f are (float)log((double)x) / (float)log2((double)x) and are
answered so.
"""
import math
import os
import random
import re
import struct
import subprocess
import sys

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_MIPS, UC_MODE_MIPS32, UC_MODE_LITTLE_ENDIAN, UC_HOOK_CODE
from unicorn.mips_const import *

STUB, STACK, END, HEAP = 0x70000000, 0x60000000, 0x7ff00000, 0x50000000
A = [UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3]
RX = 0x90000


class Lib:
    def __init__(self, path):
        uc = self.uc = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 + UC_MODE_LITTLE_ENDIAN)
        self.syms = {}
        with open(path, 'rb') as f:
            elf = ELFFile(f)
            loads = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
            top = max(s['p_vaddr'] + s['p_memsz'] for s in loads)
            uc.mem_map(0, (top + 0xffff) & ~0xffff)
            for s in loads:
                uc.mem_write(s['p_vaddr'], s.data())
            for sec in elf.iter_sections():
                if sec.name in ('.dynsym', '.symtab'):
                    for s in sec.iter_symbols():
                        if s['st_value']:
                            self.syms.setdefault(s.name, s['st_value'])
        for b, n in ((STUB, 0x100000), (STACK - 0x100000, 0x200000), (END, 0x1000), (HEAP, 0x4000000)):
            uc.mem_map(b, n)
        got = subprocess.run(['readelf', '-A', path], capture_output=True, text=True,
                             env=dict(os.environ, LC_ALL='C')).stdout
        self.stubs = {}
        n = 0
        for line in got.splitlines():
            m = re.match(r'\s+([0-9a-f]{8})\s+-?\d+\(gp\)\s+([0-9a-f]{8})\s+([0-9a-f]{8})\s+(\w+)\s+(\w+)\s+(\S+)', line)
            if m and m.group(5) == 'UND':
                a = STUB + n * 8
                uc.mem_write(int(m.group(1), 16), struct.pack('<I', a))
                self.stubs[a] = m.group(6)
                n += 1
        uc.mem_write(STUB, b'\x08\x00\xe0\x03\x00\x00\x00\x00' * n)
        uc.hook_add(UC_HOOK_CODE, self._stub, begin=STUB, end=STUB + n * 8)
        # the OEM debug print "show" returns at once
        uc.hook_add(UC_HOOK_CODE, self._ret, begin=self.syms['show'], end=self.syms['show'])
        self.brk = HEAP + 0x1000000
        # MXU2 (SIMD) instructions of H264_SMA_CalMBFlag
        self.vr = [bytes(16)] * 32
        fn = self.syms['H264_SMA_CalMBFlag']
        uc.hook_add(UC_HOOK_CODE, self._mxu2, begin=fn, end=fn + 0x994)
        st = uc.reg_read(UC_MIPS_REG_CP0_STATUS)
        uc.reg_write(UC_MIPS_REG_CP0_STATUS, st | (1 << 29))  # FPU on, FR=0

    def _mxu2(self, uc, addr, size, _):
        w = struct.unpack('<I', bytes(uc.mem_read(addr, 4)))[0]
        gpr = lambda r: uc.reg_read(UC_MIPS_REG_ZERO + r) & 0xffffffff
        vd, vs, vt = (w >> 6) & 31, (w >> 11) & 31, (w >> 16) & 31
        op, f = w >> 26, w & 63
        if op == 0x12:                                  # COP2: vector ops
            a, b = self.vr[vs], self.vr[vt]
            key = ((w >> 21) & 31, f)
            if key == (17, 12):                         # subua.b
                r = bytes(abs(x - y) for x, y in zip(a, b))
            elif key == (17, 25):                       # adduu.h
                r = struct.pack('<8H', *[min(x + y, 0xffff) for x, y in
                                         zip(struct.unpack('<8H', a), struct.unpack('<8H', b))])
            elif key in ((18, 41), (18, 42), (18, 43)): # dotpu.h/.w/.d
                n = {41: 1, 42: 2, 43: 4}[f]
                fm = {1: 'B', 2: 'H', 4: 'I'}[n]
                x, y = struct.unpack('<%d%s' % (16 // n, fm), a), struct.unpack('<%d%s' % (16 // n, fm), b)
                fo = {1: 'H', 2: 'I', 4: 'Q'}[n]
                mask = (1 << (16 * n)) - 1
                r = struct.pack('<%d%s' % (8 // n, fo), *[(x[2 * i] * y[2 * i] + x[2 * i + 1] * y[2 * i + 1]) & mask
                                                          for i in range(8 // n)])
            else:
                raise RuntimeError('MXU2 %08x at %x' % (w, addr))
            self.vr[vd] = r
        elif op == 0x1c and f in (0x0c, 0x0d, 0x0e):  # li.b/.h/.w
            imm = (w >> 11) & 0x3ff
            self.vr[vd] = (bytes([imm & 0xff]) * 16 if f == 0x0c else
                           struct.pack('<H', imm) * 8 if f == 0x0d else struct.pack('<I', imm) * 4)
        elif op == 0x1c and f in (0x14, 0x1c):          # lu1q / su1q
            off = (w >> 11) & 0x3ff
            ea = (gpr((w >> 21) & 31) + (off - 0x400 if off & 0x200 else off)) & 0xffffffff
            if f == 0x14:
                self.vr[vd] = bytes(uc.mem_read(ea, 16))
            else:
                uc.mem_write(ea, self.vr[vd])
        elif op == 0x1c and f == 0x07 and ((w >> 11) & 31) in (0, 4):   # lu1qx / su1qx
            ea = (gpr((w >> 21) & 31) + gpr((w >> 16) & 31)) & 0xffffffff
            if (w >> 11) & 31 == 0:
                self.vr[vd] = bytes(uc.mem_read(ea, 16))
            else:
                uc.mem_write(ea, self.vr[vd])
        else:
            return
        uc.reg_write(UC_MIPS_REG_PC, addr + 4)

    def _ret(self, uc, addr, size, _):
        uc.reg_write(UC_MIPS_REG_PC, uc.reg_read(UC_MIPS_REG_RA))

    def fd(self, r):
        lo = self.uc.reg_read(UC_MIPS_REG_F0 + r) & 0xffffffff
        hi = self.uc.reg_read(UC_MIPS_REG_F0 + r + 1) & 0xffffffff
        return struct.unpack('<d', struct.pack('<II', lo, hi))[0]

    def setd(self, r, v):
        lo, hi = struct.unpack('<II', struct.pack('<d', v))
        self.uc.reg_write(UC_MIPS_REG_F0 + r, lo)
        self.uc.reg_write(UC_MIPS_REG_F0 + r + 1, hi)

    def _stub(self, uc, addr, size, _):
        name = self.stubs.get(addr, '?')
        a = [uc.reg_read(r) for r in A]
        ret = 0
        if name == 'memset':
            uc.mem_write(a[0], bytes([a[1] & 0xff]) * a[2]); ret = a[0]
        elif name in ('memcpy', 'memmove'):
            uc.mem_write(a[0], bytes(uc.mem_read(a[1], a[2]))); ret = a[0]
        elif name in ('malloc', 'calloc'):
            n = a[0] if name == 'malloc' else a[0] * a[1]
            ret = self.brk; self.brk += (n + 15) & ~15
            uc.mem_write(ret, bytes(n))
        elif name in ('logf', 'log2f'):
            x = struct.unpack('<f', struct.pack('<I', uc.reg_read(UC_MIPS_REG_F12) & 0xffffffff))[0]
            r = (math.log(x) if name == 'logf' else math.log2(x)) if x > 0 else (-math.inf if x == 0 else math.nan)
            uc.reg_write(UC_MIPS_REG_F0, struct.unpack('<I', struct.pack('<f', r))[0])
        elif name in ('pow', 'log', 'sqrt'):
            x = self.fd(12)
            if name == 'pow':
                r = math.pow(x, self.fd(14))
            elif name == 'log':
                r = math.log(x) if x > 0 else (-math.inf if x == 0 else math.nan)
            else:
                r = math.sqrt(x) if x >= 0 else math.nan
            self.setd(0, r)
        elif name == 'access':
            ret = 0xffffffff        # no debug files
        elif name == 'exit':
            raise RuntimeError('exit(%d) called' % a[0])
        # fopen returns NULL: no "smooth" file
        uc.reg_write(UC_MIPS_REG_V0, ret)
        uc.reg_write(UC_MIPS_REG_PC, uc.reg_read(UC_MIPS_REG_RA))

    def call(self, fn, *args):
        uc = self.uc
        fn = self.syms[fn] if isinstance(fn, str) else fn
        uc.reg_write(UC_MIPS_REG_SP, STACK - 64)
        uc.reg_write(UC_MIPS_REG_T9, fn)
        uc.reg_write(UC_MIPS_REG_RA, END)
        for r, v in zip(A, args):
            uc.reg_write(r, v & 0xffffffff)
        uc.emu_start(fn, END, count=200_000_000)
        if uc.reg_read(UC_MIPS_REG_PC) != END:
            raise RuntimeError('no return pc=%x' % uc.reg_read(UC_MIPS_REG_PC))
        return uc.reg_read(UC_MIPS_REG_V0)

    def alloc(self, n):
        a = self.brk; self.brk += (n + 15) & ~15
        self.uc.mem_write(a, bytes(n)); return a

    def r32(self, a): return struct.unpack('<I', bytes(self.uc.mem_read(a, 4)))[0]
    def w32(self, a, v): self.uc.mem_write(a, struct.pack('<I', v & 0xffffffff))
    def w8(self, a, v): self.uc.mem_write(a, bytes([v & 0xff]))


def picture(frame, stride, lines):
    """Synthetic luma, drawn the same way by rc_t20_test.c."""
    out = bytearray(stride * lines)
    for y in range(lines):
        for x in range(stride):
            b = (x >> 4) + (y >> 4) * 3 + frame
            if b % 4 == 0:
                v = 30 + (frame * 7) % 60                       # dark flat
            elif b % 4 == 1:
                v = 140 + ((x + y) & 3)                         # bright flat
            elif b % 4 == 2:
                v = (x * 13 + y * 29 + frame * 5) & 0xff        # texture
            else:
                v = ((x * x + y * 7 + frame) * 2654435761 >> 13) & 0xff
            out[y * stride + x] = v
    return bytes(out)


KEYS = ('method', 'w', 'h', 'gop', 'fpsn', 'fpsd', 'qp', 'min', 'max', 'bitrate', 'ibias', 'frm', 'gopstep',
        'gopRel', 'static', 'maxbr', 'chg', 'qual', 'trig', 'newmax', 'superI', 'superP', 'mbrc')


class Oem:
    """i264e_ratecontrol_* of the T20 branch on a minimal i264e handle."""

    def __init__(self, lib, cfg):
        L = self.L = lib
        self.cfg = cfg
        P = self.param = L.alloc(RX + 0x1000)
        self.h = L.alloc(0x2000)
        self.stats = [L.alloc(256), L.alloc(256)]
        self.regs = L.alloc(32)
        self.fenc = L.alloc(256)
        w32 = L.w32
        fb = lambda x: struct.unpack('<I', struct.pack('<f', x))[0]
        w32(P + 0, 1 if T10 else 2)                    # T10 / T20 rate control
        w32(P + 40, cfg['gop']); w32(P + 44, cfg['w']); w32(P + 48, cfg['h'])
        w32(P + 188, cfg['method']); w32(P + 192, cfg['qp'])
        w32(P + 196, cfg['min']); w32(P + 200, cfg['max']); w32(P + 208, fb(1.4))
        w32(P + 216, cfg['bitrate']); w32(P + 228, cfg['ibias'])
        w32(P + 232, cfg['frm']); w32(P + 236, cfg['gopstep']); L.w8(P + 241, cfg['gopRel'])
        w32(P + 244, cfg['static']); w32(P + 248, cfg['maxbr']); w32(P + 252, cfg['chg'])
        w32(P + 256, cfg['qual']); w32(P + 260, fb(cfg['trig'])); w32(P + 264, cfg['newmax'])
        w32(P + 268, cfg['mbrc']); w32(P + 272, 1)    # macroblock rate control
        w32(P + 280, cfg['superI']); w32(P + 284, cfg['superP'])
        w32(P + 396, cfg['fpsn']); w32(P + 400, cfg['fpsd'])
        w32(P + RX + 1156, (cfg['w'] + 15) >> 4); w32(P + RX + 1160, (cfg['h'] + 15) >> 4)
        w32(P + RX + 1076, L.alloc(64))               # ROI regions: none
        stride = (cfg['w'] + 15) & ~15
        self.stride = stride
        self.luma = L.alloc(stride * (((cfg['h'] + 15) & ~15) + 16) + 64)
        w32(self.fenc + 76, self.luma); w32(self.fenc + 100, stride)
        w32(self.h + 4672, P)
        w32(self.h + 896, self.fenc)
        for i, a in enumerate((0x800e4, 0x800e8, 0x800ec, 0x80034)):
            w32(self.regs + 8 * i, a)
        w32(self.h + 2936, self.regs)
        L.call('i264e_ratecontrol_init', P, 0xffffffff)
        self.frame = 0

    def start(self):
        L, h, f = self.L, self.h, self.frame
        if self.cfg['mbrc']:
            L.uc.mem_write(self.luma, picture(f, self.stride, ((self.cfg['h'] + 15) & ~15) + 16))
        idr = f % self.cfg['gop'] == 0
        L.w32(h + 0, f)
        L.w32(h + 904, self.stats[(f + 1) & 1]); L.w32(h + 908, self.stats[f & 1])
        L.call('i264e_ratecontrol_start', h)
        return idr, L.r32(h + 740)

    def table(self):
        """QP table of the picture: (words, sum of the words, mean QP)."""
        L, E = self.L, self.param + 704
        if not L.r32(E + RX + 348) & 0xff:
            return 0, 0, 0
        n = L.r32(E + RX + 344)
        words = struct.unpack('<%dI' % n, bytes(L.uc.mem_read(E + 0x50158, 4 * n)))
        return n, sum(words) & 0xffffffff, L.r32(E + RX + 352)

    def end(self, idr, cmpx, nbytes, regs):
        L, h = self.L, self.h
        L.w32(h + 2884, cmpx); L.w32(h + 3828, nbytes); L.w32(h + 736, 1 if idr else 0)
        for i in range(3):
            L.w32(self.regs + 8 * i + 4, regs[i])
        r = L.call('i264e_ratecontrol_is_reenc', h)
        return (1 if 0 < r < 0x80000000 else 0), L.r32(h + 740)


def scenario(lib_path, seed, frames, out):
    rnd = random.Random(seed)
    method = (1, 2, 3, 0)[seed % 4]
    mbrc = 1 if seed >= 12 and not T10 else 0
    w, hh = rnd.choice([(320, 240), (256, 160), (176, 144)] if mbrc else
                       [(1920, 1080), (1280, 720), (640, 360), (320, 240)])
    mx = rnd.randint(36, 51); mn = rnd.randint(0, 30)
    cfg = dict(method=method, w=w, h=hh, gop=rnd.choice([20, 25, 30, 50]), fpsn=rnd.choice([25, 15, 20]), fpsd=1,
               qp=rnd.randint(20, 40), min=mn, max=mx, bitrate=rnd.choice([256, 512, 1000, 2000]),
               ibias=rnd.randint(-3, 3), frm=rnd.randint(2, 6), gopstep=rnd.randint(2, 15), gopRel=rnd.randint(0, 1),
               static=rnd.randint(1, 10), maxbr=rnd.choice([512, 1000, 2000, 4000]), chg=rnd.randint(50, 100),
               qual=rnd.randint(0, 6), trig=rnd.choice([3.0, 2.0, 1.5]), newmax=rnd.choice([51, 48]),
               superI=0x12c0000, superP=0xd64925, mbrc=mbrc)
    if seed % 3 == 2:   # small super-frame thresholds: re-encodes
        cfg['superI'] = rnd.randint(200000, 900000); cfg['superP'] = rnd.randint(60000, 300000)
    oem = Oem(Lib(lib_path), cfg)
    out.write('S %s\n' % ' '.join(repr(cfg[k]) if k == 'trig' else str(cfg[k]) for k in KEYS))
    mbs = ((w + 15) >> 4) * ((hh + 15) >> 4)
    cplx = rnd.uniform(0.3, 3.0)
    for f in range(frames):
        if rnd.random() < 0.06:
            cplx = rnd.uniform(0.2, 4.0)
        idr, qp = oem.start()
        out.write('F %d %d %d\n' % (f, int(idr), qp))
        if mbrc:
            out.write('T %d %d %d\n' % oem.table())
        moving = rnd.randint(0, mbs) if rnd.random() < 0.7 else rnd.randint(0, mbs // 10)
        for attempt in range(8):
            size = cplx * mbs * 5 * (2.0 ** ((30 - qp) / 6.0)) * (4.0 if idr else 1.0)
            nbytes = max(4, int(size * rnd.uniform(0.7, 1.3)))
            cmpx = int(cplx * mbs * 256 * rnd.uniform(0.8, 1.2))
            r0 = (rnd.randint(0, min(moving, 0x7fff)) << 16) | rnd.randint(0, min(moving, 0x7fff))
            regs = [r0, rnd.randint(0, moving * 20 + 1), rnd.randint(0, moving * 20 + 1)]
            re_, nq = oem.end(idr, cmpx, nbytes, regs)
            out.write('E %d %d %d %d %d %d %d\n' % (cmpx, nbytes * 8, regs[0], regs[1], regs[2], re_, nq if re_ else 0))
            if mbrc and re_:
                out.write('T %d %d %d\n' % oem.table())
            if not re_:
                break
            qp = nq
        oem.frame += 1


T10 = False


def main():
    global T10
    args = sys.argv[1:]
    if args and args[0] == '--t10':
        T10 = True
        args = args[1:]
    lib_path = args[0]
    scenarios = int(args[1]) if len(args) > 1 else 16
    frames = int(args[2]) if len(args) > 2 else 120
    for seed in range(scenarios):
        scenario(lib_path, seed, frames, sys.stdout)


if __name__ == '__main__':
    main()
