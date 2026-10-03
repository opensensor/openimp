#!/usr/bin/env python3
"""Reference vectors for src/eprc from the OEM rate controller (development aid).

Runs the rate-control entry points of a vendor T23 1.3.0 libimp.so
(i264e_ratecontrol_init / _start / _is_reenc, which drive
JZ_VPU_RC_VIDEO_CFG_T21 / FRAME_START_T21 / FRAME_REPEATE_JUDGE_T21 /
FRAME_END_T21) under the unicorn CPU emulator on synthetic picture sizes
and statistics, and prints the picture type and QP sequence that
tests/eprc/eprc_test.c checks OpenIMP's src/eprc against.  Nothing from the
vendor library is copied into OpenIMP; it is read from the path you pass.

    pip install unicorn pyelftools
    tools/eprc_oracle.py T23/lib/1.3.0/uclibc/5.4.0/libimp.so > tests/eprc/eprc_vectors.txt

The OEM keeps int arrays at odd addresses (the Linux kernel fixes up the
unaligned accesses on the camera); the emulator does the same in a code
hook.
"""
import math
import os
import re
import struct
import subprocess
import sys

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_MIPS, UC_MODE_MIPS32, UC_MODE_LITTLE_ENDIAN, UC_HOOK_CODE
from unicorn.mips_const import *
READELF = 'readelf'
STUB, STACK, END, HEAP = 0x70000000, 0x60000000, 0x7ff00000, 0x50000000
A = [UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3]

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
        got = subprocess.run([READELF, '-A', path], capture_output=True, text=True,
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
        self.brk = HEAP + 0x1000000
        self.lastpc = 0
        self.unaligned_count = 0
        self.code = {a: self.r32(a) for a in range(0xbd600, 0xcce90, 4)}
        uc.hook_add(UC_HOOK_CODE, self._track, begin=0xbd600, end=0xcce90)
        # enable FPU (CP0 Status.CU1); FR stays 0 (o32 fp32)
        st = uc.reg_read(UC_MIPS_REG_CP0_STATUS)
        uc.reg_write(UC_MIPS_REG_CP0_STATUS, st | (1 << 29))

    # o32 FR=0: double in even/odd pair
    def fd(self, r):
        lo = self.uc.reg_read(UC_MIPS_REG_F0 + r) & 0xffffffff
        hi = self.uc.reg_read(UC_MIPS_REG_F0 + r + 1) & 0xffffffff
        return struct.unpack('<d', struct.pack('<II', lo, hi))[0]

    def setd(self, r, v):
        lo, hi = struct.unpack('<II', struct.pack('<d', v))
        self.uc.reg_write(UC_MIPS_REG_F0 + r, lo)
        self.uc.reg_write(UC_MIPS_REG_F0 + r + 1, hi)

    MEMOPS = {0x23: 4, 0x2b: 4, 0x21: 2, 0x25: 2, 0x29: 2, 0x31: 4, 0x39: 4, 0x35: 8, 0x3d: 8}

    def _ea(self, ins):
        uc = self.uc
        base = uc.reg_read(UC_MIPS_REG_ZERO + ((ins >> 21) & 31))
        off = ins & 0xffff
        return (base + off - (0x10000 if off & 0x8000 else 0)) & 0xffffffff

    def _misaligned(self, ins):
        n = self.MEMOPS.get(ins >> 26)
        if not n:
            return False
        a = self._ea(ins)
        return a % min(n, 4) != 0 or (n == 8 and a % 4 != 0)

    def _domem(self, ins):
        uc = self.uc
        op = ins >> 26; rt = (ins >> 16) & 31; a = self._ea(ins)
        G = UC_MIPS_REG_ZERO + rt
        if op == 0x23: v = self.r32(a)
        elif op == 0x21: v = struct.unpack('<h', self.rd(a, 2))[0] & 0xffffffff
        elif op == 0x25: v = struct.unpack('<H', self.rd(a, 2))[0]
        elif op == 0x2b: self.w32(a, uc.reg_read(G)); return
        elif op == 0x29: self.w16(a, uc.reg_read(G)); return
        elif op == 0x31: uc.reg_write(UC_MIPS_REG_F0 + rt, self.r32(a)); return
        elif op == 0x39: self.w32(a, uc.reg_read(UC_MIPS_REG_F0 + rt)); return
        elif op == 0x35:
            uc.reg_write(UC_MIPS_REG_F0 + rt, self.r32(a)); uc.reg_write(UC_MIPS_REG_F0 + rt + 1, self.r32(a + 4)); return
        elif op == 0x3d:
            self.w32(a, uc.reg_read(UC_MIPS_REG_F0 + rt)); self.w32(a + 4, uc.reg_read(UC_MIPS_REG_F0 + rt + 1)); return
        if rt: uc.reg_write(G, v)

    def _branch(self, pc, ins):
        """None if not a branch, else (taken, target, likely)."""
        uc = self.uc
        op = ins >> 26
        def gr(r):
            v = uc.reg_read(UC_MIPS_REG_ZERO + r) & 0xffffffff
            return v - 0x100000000 if v & 0x80000000 else v
        tgt = (pc + 4 + ((ins & 0xffff) - (0x10000 if ins & 0x8000 else 0)) * 4) & 0xffffffff
        if op in (4, 5, 6, 7, 20, 21, 22, 23):
            rs, rt = gr((ins >> 21) & 31), gr((ins >> 16) & 31)
            return ([rs == rt, rs != rt, rs <= 0, rs > 0][op & 3], tgt, op >= 20)
        if op == 1:
            c = (ins >> 16) & 31
            if c in (0, 1, 2, 3):
                rs = gr((ins >> 21) & 31)
                return ((rs < 0) if c in (0, 2) else (rs >= 0), tgt, c >= 2)
            return 'other'
        if op == 2:
            return (True, ((pc + 4) & 0xf0000000) | ((ins & 0x3ffffff) << 2), False)
        if op == 0 and (ins & 0x3f) == 8:
            return (True, uc.reg_read(UC_MIPS_REG_ZERO + ((ins >> 21) & 31)), False)
        if op in (3,) or (op == 0 and (ins & 0x3f) == 9) or (op == 17 and ((ins >> 21) & 31) == 8):
            return 'other'
        return None

    def _track(self, uc, addr, size, _):
        self.lastpc = addr
        ins = self.code.get(addr)
        if ins is None:
            return
        if self._misaligned(ins):
            self._domem(ins)
            self.unaligned_count += 1
            uc.reg_write(UC_MIPS_REG_PC, addr + 4)
            return
        br = self._branch(addr, ins)
        if br is None:
            return
        ds = self.code.get(addr + 4)
        if ds is None or not self._misaligned(ds):
            return
        if br == 'other':
            raise RuntimeError('misaligned access in an unsupported delay slot at %x' % (addr + 4))
        taken, tgt, likely = br
        if taken or not likely:
            self._domem(ds)
            self.unaligned_count += 1
        uc.reg_write(UC_MIPS_REG_PC, tgt if taken else addr + 8)

    def _stub(self, uc, addr, size, _):
        name = self.stubs.get(addr, '?')
        a = [uc.reg_read(r) for r in A]
        ret = 0
        if name == 'memset':
            uc.mem_write(a[0], bytes([a[1] & 0xff]) * a[2]); ret = a[0]
        elif name == 'memcpy':
            uc.mem_write(a[0], bytes(uc.mem_read(a[1], a[2]))); ret = a[0]
        elif name in ('malloc', 'calloc', 'realloc'):
            n = a[0] if name == 'malloc' else (a[0] * a[1] if name == 'calloc' else a[1])
            ret = self.brk; self.brk += (n + 15) & ~15
            uc.mem_write(ret, bytes(n))
            if name == 'realloc' and a[0]:
                uc.mem_write(ret, bytes(uc.mem_read(a[0], n)))
        elif name in ('pow', 'log', 'sqrt', 'floor'):
            x = self.fd(12)
            if name == 'pow':
                r = math.pow(x, self.fd(14))
            elif name == 'log':
                r = math.log(x) if x > 0 else (-math.inf if x == 0 else math.nan)
            elif name == 'sqrt':
                r = math.sqrt(x) if x >= 0 else math.nan
            else:
                r = math.floor(x)
            self.setd(0, r)
        elif name in ('free', 'i264e_log', 'puts', 'fwrite', 'vfprintf', 'printf', 'fprintf'):
            pass
        elif name == 'access':
            ret = 0xffffffff
        elif name == 'exit':
            raise RuntimeError('exit(%d) called' % a[0])
        uc.reg_write(UC_MIPS_REG_V0, ret)
        uc.reg_write(UC_MIPS_REG_PC, uc.reg_read(UC_MIPS_REG_RA))

    def call(self, fn, *args, limit=50_000_000):
        fn = self.syms[fn] if isinstance(fn, str) else fn
        uc = self.uc
        uc.reg_write(UC_MIPS_REG_SP, STACK)
        uc.reg_write(UC_MIPS_REG_T9, fn)
        uc.reg_write(UC_MIPS_REG_RA, END)
        for r, v in zip(A, args):
            uc.reg_write(r, v)
        uc.emu_start(fn, END, count=limit)
        if uc.reg_read(UC_MIPS_REG_PC) != END:
            raise RuntimeError('no return pc=%x' % uc.reg_read(UC_MIPS_REG_PC))
        return uc.reg_read(UC_MIPS_REG_V0)

    def rd(self, a, n): return bytes(self.uc.mem_read(a, n))
    def wr(self, a, b): self.uc.mem_write(a, bytes(b))
    def r32(self, a): return struct.unpack('<I', self.rd(a, 4))[0]
    def w32(self, a, v): self.wr(a, struct.pack('<I', v & 0xffffffff))
    def w16(self, a, v): self.wr(a, struct.pack('<H', v & 0xffff))
    def w8(self, a, v): self.wr(a, bytes([v & 0xff]))
TABLE_ADDR = 0xd7a40

class Sim:
    def __init__(self, lib, mode=3, w=1920, h=1080, gop=25, fps=(25, 1), minqp=15, maxqp=45,
                 bitrate=1000, maxbitrate=2000, frm_step=3, gop_step=15, bias=0,
                 static_time=2, change_pos=80, quality=4, bgmul=2, rclevel=0xffffffff,
                 extra=None):
        L = self.L = Lib(lib)
        self.H = HEAP; self.R = HEAP + 0x10000; self.F = HEAP + 0x20000; self.ST = HEAP + 0x21000
        self.TB = HEAP + 0x22000; self.SL = HEAP + 0x30000
        L.wr(HEAP, bytes(0x40000))
        R = self.R
        L.call('i264e_param_default', R)
        def p(o, v): L.w32(R + o, v)
        p(4, 4); p(44, gop); p(56, w); p(60, h); p(408, fps[0]); p(412, fps[1])
        p(200, mode); p(208, minqp); p(212, maxqp); p(228, bitrate); p(240, bias & 0xffffffff)
        p(244, frm_step); p(248, gop_step); p(256, static_time); p(260, maxbitrate)
        p(264, change_pos); p(268, quality); p(2756, bgmul)
        for o, v in (extra or {}).items(): p(o, v)
        L.call('i264e_ratecontrol_init', R, rclevel)
        H = self.H
        L.w32(H + 4896, R); L.w32(H + 896, self.F); L.w32(H + 912, self.ST)
        L.w32(H + 2876, self.TB); L.w32(H + 2872, 312); L.w32(H + 3880, self.SL)
        tab = struct.unpack('<39I', L.rd(TABLE_ADDR, 156))
        for i, o in enumerate(tab): L.w32(self.TB + 8 * i, o)
        self.n = 0

    def start(self):
        L = self.L
        L.w32(self.H, self.n)
        L.call('i264e_ratecontrol_start', self.H)
        return L.r32(self.R + 2088), L.rd(self.R + 2096, 1)[0]

    def end(self, nbytes, regs):
        L = self.L
        for i, v in enumerate(regs): L.w32(self.TB + 8 * i + 4, v)
        L.w32(self.H + 3888, nbytes); L.w32(self.H + 736, 1)
        r = L.call('i264e_ratecontrol_is_reenc', self.H)
        self.n += 1
        return r


def xorshift(state):
    state ^= (state << 13) & 0xffffffff
    state ^= state >> 17
    state ^= (state << 5) & 0xffffffff
    return state & 0xffffffff


def statistics(frame):
    """The 25 Helix statistics words of picture `frame` (as the test)."""
    state = 0x9e3779b9 ^ (frame * 0x85ebca6b & 0xffffffff)
    regs = []
    for _ in range(25):
        state = xorshift(state)
        regs.append(state >> (state & 15))
    return regs


SCENARIOS = [
    # mode w h gop fps minqp maxqp bitrate maxbitrate frmstep gopstep bias static changepos quality bgmul initqp idr
    (3, 1920, 1080, 25, 25, 15, 45, 1000, 2000, 3, 15, 0, 2, 80, 4, 0, -1, 25),
    (3, 1920, 1080, 25, 25, 15, 45, 1000, 2000, 3, 15, 0, 2, 80, 4, 2, -1, 50),
    (2, 1280, 720, 50, 25, 20, 48, 500, 1000, 2, 15, -1, 2, 80, 2, 0, -1, 50),
    (1, 1920, 1080, 30, 15, 22, 51, 1500, 3000, 3, 30, 2, 6, 80, 4, 0, 30, 30),
    (1, 640, 360, 10, 25, 10, 38, 256, 512, 8, 2, -3, 1, 50, 0, 0, -1, 10),
    (2, 2304, 1296, 51, 20, 15, 45, 3000, 6000, 1, 15, 3, 2, 100, 6, 0, 40, 51),
    (3, 640, 360, 60, 30, 15, 51, 128, 256, 2, 2, 0, 5, 50, 1, 3, -1, 180),
]


def picture_bytes(frame, qp, idr, pixels, pattern):
    """Integer picture-size model, identical in the test."""
    k = (frame // 40) % 2
    cplx = (1, 5)[k] if pattern else 1 + (frame % 7)
    size = pixels * cplx // 40
    for _ in range(qp):
        size = size * 89 // 100        # about 6 QP per halving
    if idr:
        size *= 4
    if frame % 37 == 36:
        size *= 120                    # an oversized picture (re-encode)
    return max(size, 20)


def validated_vbv(lib, w, h):
    """param[292], param[296] after i264e_validate_parameters."""
    L = Lib(lib)
    R = HEAP + 0x10000
    L.wr(HEAP, bytes(0x40000))
    L.call('i264e_param_default', R)
    L.w32(R + 56, w)
    L.w32(R + 60, h)
    L.w32(R + 4, 4)
    L.call('i264e_validate_parameters', R)
    return {292: L.r32(R + 292), 296: L.r32(R + 296)}


def main():
    lib = sys.argv[1]
    frames = 120
    for n, sc in enumerate(SCENARIOS):
        (mode, w, h, gop, fps, minqp, maxqp, br, mbr, fstep, gstep, bias, static,
         cpos, qual, bgmul, initqp, idr) = sc
        s = Sim(lib, mode=mode, w=w, h=h, gop=gop, fps=(fps, 1), minqp=minqp, maxqp=maxqp,
                bitrate=br, maxbitrate=mbr, frm_step=fstep, gop_step=gstep, bias=bias,
                static_time=static, change_pos=cpos, quality=qual, bgmul=bgmul,
                rclevel=initqp & 0xffffffff, extra=validated_vbv(lib, w, h))
        print('S', ' '.join(str(v) for v in sc))
        for f in range(frames):
            since = f % idr
            s.L.w32(s.H, since)
            s.L.call('i264e_ratecontrol_start', s.H)
            t = s.L.rd(s.SL, 1)[0]        # picture type (2 IDR, 6 GOP start, 0 P)
            qp = s.L.rd(s.R + 2096, 1)[0]
            out = [t, qp]
            size = picture_bytes(f, qp, since == 0, w * h, n % 2)
            regs = statistics(f)
            while True:
                for i, v in enumerate(regs):
                    s.L.w32(s.TB + 8 * i + 4, v)
                s.L.w32(s.H + 3888, size)
                s.L.w32(s.H + 736, 1)
                r = s.L.call('i264e_ratecontrol_is_reenc', s.H)
                if r != 1:
                    break
                qp = s.L.rd(s.R + 2096, 1)[0]
                out.append(qp)
                size = max(size // 2, 20)
            print('F', since, ' '.join(str(v) for v in out))


if __name__ == '__main__':
    main()
