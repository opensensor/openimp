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
    tools/eprc_oracle.py T21/lib/1.0.33/uclibc/5.4.0/libimp.so > tests/eprc/eprc_t21_vectors.txt

The T21 1.0.33 library (the older controller revision, src/eprc/eprc_t21.c)
is run with its own i264e glue layout (LAYOUT_T21 below).

Macroblock rate control (src/eprc/eprc_mbrc.c):

    tools/eprc_oracle.py LIB --mbrc SEED N [FRAMES]   random scenes, 'M' lines
    tools/eprc_oracle.py LIB --mbrc-calls SEED N      single h264_get_mb_qp calls

'--mbrc' runs random scenarios with the Helix activity-class histogram
(registers 0x40094..0x400a0, sas_histogram) and prints after each picture
the macroblock rate-control registers that the OEM H264E_T21_SliceInit
makes of the slice block (and 0x400c0/0x400c4), and FNV-1a hashes of the slice fields and the
controller state h264_get_mb_qp writes.  '--mbrc-calls' calls the OEM
h264_get_mb_qp on random state ('C' lines: inputs, the SAS offsets, the
slice hash).

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
        # T23 1.3.0 has h264_api_enc as a function, T21 1.0.33 inlines it
        self.lay = LAYOUT_T23 if 'h264_api_enc' in self.syms else LAYOUT_T21
        lo, hi = self.lay['code']
        self.code = {a: self.r32(a) for a in range(lo, hi, 4)}
        uc.hook_add(UC_HOOK_CODE, self._track, begin=lo, end=hi)
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
        for i, v in enumerate(args[4:]):        # o32: stack arguments from sp+16
            self.w32(STACK + 16 + 4 * i, v)
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
# The i264e glue around the (shared) eprc controller differs between the
# T23 1.3.0 and T21 1.0.33 libraries: T21 lacks the i264e parameter word at
# +4 (every parameter offset 4 lower, the "rc enabled" test is p[0] == 4
# instead of p[4] in {4, 5}), its controller block E starts at rc + 484
# (T23 rc + 496) and the picture outputs lie 4 bytes lower in E.
LAYOUT_T23 = dict(name='T23', code=(0xbd600, 0xcce90), table=0xd7a40, pshift=0,
                  enable=(4, 4), rc=4896, slice=3880, qp=2096)
LAYOUT_T21 = dict(name='T21', code=(0x8edc0, 0x9d630), table=0xa7aa0, pshift=-4,
                  enable=(0, 4), rc=4784, slice=3884, qp=2080)

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
        lay = L.lay
        def p(o, v): L.w32(R + o + (lay['pshift'] if o != 2756 else 0), v)
        L.w32(R + lay['enable'][0], lay['enable'][1])
        p(44, gop); p(56, w); p(60, h); p(408, fps[0]); p(412, fps[1])
        p(200, mode); p(208, minqp); p(212, maxqp); p(228, bitrate); p(240, bias & 0xffffffff)
        p(244, frm_step); p(248, gop_step); p(256, static_time); p(260, maxbitrate)
        p(264, change_pos); p(268, quality)
        # skip header (i264e_init_skip_header at rc+2752 / +2700) word 1:
        # hSkipAttr.maxSameSceneCnt
        L.w32(R + (2756 if lay['name'] == 'T23' else 2704), bgmul)
        for o, v in (extra or {}).items(): p(o, v)
        L.call('i264e_ratecontrol_init', R, rclevel)
        H = self.H
        L.w32(H + lay['rc'], R); L.w32(H + 896, self.F); L.w32(H + 912, self.ST)
        L.w32(H + 2876, self.TB); L.w32(H + 2872, 312); L.w32(H + lay['slice'], self.SL)
        tab = struct.unpack('<39I', L.rd(lay['table'], 156))
        for i, o in enumerate(tab): L.w32(self.TB + 8 * i, o)
        self.n = 0

    def start(self):
        L = self.L
        L.w32(self.H, self.n)
        L.call('i264e_ratecontrol_start', self.H)
        return L.r32(self.R + L.lay['qp'] - 8), L.rd(self.R + L.lay['qp'], 1)[0]

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
    sh = L.lay['pshift']
    L.w32(R + 56 + sh, w)
    L.w32(R + 60 + sh, h)
    L.w32(R + L.lay['enable'][0], L.lay['enable'][1])
    L.call('i264e_validate_parameters', R)
    return {292: L.r32(R + 292 + sh), 296: L.r32(R + 296 + sh)}


def random_scenarios(seed, count):
    """Random CBR/VBR/SMART scenarios (tools/eprc_oracle.py LIB --random SEED N)."""
    import random
    rnd = random.Random(seed)
    sizes = [(320, 240), (640, 360), (1280, 720), (1920, 1080), (2304, 1296), (2560, 1440),
             (2336, 1296), (2352, 1296), (176, 144), (801, 600), (1920, 1088)]
    out = []
    for _ in range(count):
        mode = rnd.choice((1, 2, 3))
        w, h = rnd.choice(sizes)
        gop = rnd.randint(1, 120)
        fps = rnd.randint(5, 30)
        minqp = rnd.randint(5, 30)
        maxqp = rnd.randint(max(minqp, 20), 51)
        br = rnd.choice((128, 256, 512, 1000, 2000, 4000))
        mbr = br * rnd.choice((1, 2, 3))
        bgmul = rnd.choice((0, 0, 1, 2, 3))
        idr = gop * (bgmul if bgmul and mode == 3 else 1)
        if bgmul and mode != 3 and rnd.random() < 0.5:
            idr = gop * bgmul
        out.append((mode, w, h, gop, fps, minqp, maxqp, br, mbr, rnd.randint(1, 10),
                    rnd.randint(1, 30), rnd.randint(-5, 5), rnd.randint(1, 8),
                    rnd.randint(50, 100), rnd.randint(0, 6), bgmul,
                    rnd.choice((-1, -1, rnd.randint(10, 45))), idr))
    return out


def ae_zones(frame):
    """The 225 AE zone words of picture `frame` (as the test)."""
    state = 0x2545f491 ^ (frame * 0x9e3779b1 & 0xffffffff)
    out = []
    for _ in range(225):
        state = xorshift(state)
        out.append(state & 0xfffff)
    return out


def fnv1a(data):
    h = 0x811c9dc5
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xffffffff
    return h


# Extended scenario fields (after the 18 of SCENARIOS): param[52] (T23:
# re-encode IDR pictures only, set by IMP_Encoder_SetRdBufShare, the OEM
# default 1), the FIXQP QP param[204], and flags: 1 = AE zones as the OEM
# frame source channel 0 attaches them (a 'Z' line: FNV-1a of the
# controller's zone buffer after FRAME_START), 2 = picture types from the
# OEM i264e_decide_slice_type_and_rd (skip type N1X, maxSameSceneCnt =
# bgmul: IDR every bgmul GOPs or at a GOP boundary on a scene cut).
EXTENDED = [
    # T23 default: IDR-only re-encode (ring), SMART / CBR / VBR
    (3, 1920, 1080, 25, 25, 15, 45, 1000, 2000, 3, 15, 0, 2, 80, 4, 0, -1, 25, 1, 0, 0),
    (1, 1280, 720, 30, 15, 22, 51, 512, 1024, 3, 15, 0, 2, 80, 4, 0, -1, 30, 1, 0, 1),
    # FIXQP
    (0, 1920, 1080, 25, 25, 15, 45, 1000, 2000, 3, 15, 0, 2, 80, 4, 0, -1, 25, 0, 30, 0),
    (0, 640, 360, 10, 25, 22, 40, 256, 512, 3, 15, 0, 2, 80, 4, 0, -1, 10, 0, 20, 1),
    (0, 1280, 720, 15, 30, 10, 40, 256, 512, 3, 15, 0, 2, 80, 4, 0, -1, 15, 1, 2, 0),
    (0, 1280, 720, 15, 30, 10, 40, 256, 512, 3, 15, 0, 2, 80, 4, 0, -1, 15, 0, 1, 0),
    (0, 640, 360, 12, 30, 10, 40, 256, 512, 3, 15, 0, 2, 80, 4, 0, -1, 12, 0, 60, 0),
    # AE zones
    (3, 1920, 1080, 20, 25, 15, 45, 1000, 2000, 3, 15, 0, 2, 80, 4, 2, -1, 40, 1, 0, 1),
    (2, 1920, 1080, 20, 25, 15, 45, 1000, 2000, 3, 15, 0, 2, 80, 4, 2, -1, 40, 0, 0, 1),
    # scene-cut IDR (vendor picture type decision)
    (3, 1280, 720, 5, 25, 15, 45, 1000, 2000, 3, 15, 0, 2, 80, 4, 4, -1, 0, 1, 0, 2),
    (1, 1280, 720, 5, 25, 15, 45, 800, 1600, 3, 15, 0, 2, 80, 4, 3, -1, 0, 1, 0, 3),
    (2, 640, 360, 4, 15, 10, 51, 256, 512, 2, 15, 0, 2, 80, 4, 5, -1, 0, 0, 0, 2),
]


def sas_histogram(frame, mbs, scene):
    """The Helix activity-class counts (7 x 16 bits in 0x40094..0x400a0) of
    picture `frame` (as the test): a dominant class that moves every 13
    pictures, summing to `mbs`."""
    state = (0x6a09e667 ^ (frame * 0x27d4eb2d) ^ (scene * 0x165667b1)) & 0xffffffff
    dom = (frame // 13 + scene) % 7
    w = []
    for i in range(7):
        state = xorshift(state)
        w.append(1 + (state & 0x3f) + ((0x200 + (state >> 20)) if i == dom else 0))
    total = sum(w)
    c = [mbs * x // total for x in w]
    c[dom] += mbs - sum(c)
    c = [min(v, 0xffff) for v in c]
    return [c[0] | c[1] << 16, c[2] | c[3] << 16, c[4] | c[5] << 16, c[6]]


def mbrc_registers(sim):
    """The OEM command list of the current slice block (H264E_T21_SliceInit
    on a copy): qp flags of 0x40074, 0x40078..0x40090 and the picture
    control registers 0x400c0/0x400c4."""
    L = sim.L
    CL = HEAP + 0x3000000
    keep = L.rd(sim.SL, 0x1200)
    L.w32(sim.SL + 672, CL)
    L.wr(CL, bytes(0x8000))
    L.call('H264E_T21_SliceInit', sim.SL)
    w = struct.unpack('<8192I', L.rd(CL, 0x8000))
    L.wr(sim.SL, keep)
    regs = {}
    for i in range(0, len(w) - 1, 2):
        if w[i] == 0 and w[i + 1] == 0:
            break
        regs.setdefault(w[i + 1] & 0xffffc, w[i])
    out = [regs[0x40074] & 0xc0c0c0ff]
    out += [regs[0x40078 + 4 * i] for i in range(7)]
    return out + [regs[0x400c0], regs[0x400c4]]


def mbrc_hashes(sim):
    """FNV-1a of slice +752..+925 (without the pointers +820, +864, +868:
    h264_api_enc buffer addresses on the first picture) and of the
    state h264_get_mb_qp writes (S+368..375, S+5184..5343; T21 40 lower)."""
    L = sim.L
    t21 = L.lay['name'] == 'T21'
    E = sim.R + (484 if t21 else 496)
    S = L.r32(E + (1616 if t21 else 1620)) + (336 if t21 else 352)
    d = -40 if t21 else 0
    sl = bytearray(L.rd(sim.SL + 752, 174))
    sl[68:72] = bytes(4)
    sl[112:120] = bytes(8)
    st = L.rd(S + 368 + d, 8) + L.rd(S + 5184 + d, 160)
    return fnv1a(sl), fnv1a(st)


def mbrc_scenes(lib, seed, count, frames):
    """--mbrc: random scenarios (flag 4) with the macroblock rate control
    outputs after every FRAME_START."""
    for n, sc in enumerate(random_scenarios(seed, count)):
        (mode, w, h, gop, fps, minqp, maxqp, br, mbr, fstep, gstep, bias, static,
         cpos, qual, bgmul, initqp, idr) = sc
        extra = validated_vbv(lib, w, h)
        extra[204] = 0
        s = Sim(lib, mode=mode, w=w, h=h, gop=gop, fps=(fps, 1), minqp=minqp, maxqp=maxqp,
                bitrate=br, maxbitrate=mbr, frm_step=fstep, gop_step=gstep, bias=bias,
                static_time=static, change_pos=cpos, quality=qual, bgmul=bgmul,
                rclevel=initqp & 0xffffffff, extra=extra)
        L = s.L
        mbs = ((w + 15) // 16) * ((h + 15) // 16)
        print('S', ' '.join(str(v) for v in sc), 0, 0, 4)
        for f in range(frames):
            since = f % idr
            L.w32(s.H, since)
            L.call('i264e_ratecontrol_start', s.H)
            t = L.rd(s.SL, 1)[0]
            qp = L.rd(s.R + L.lay['qp'], 1)[0]
            out = [t, qp]
            regs_cl = mbrc_registers(s)
            mline = regs_cl[:8] + list(mbrc_hashes(s)) + regs_cl[8:]
            size = picture_bytes(f, qp, since == 0, w * h, n % 2)
            regs = statistics(f)
            regs[16:20] = sas_histogram(f, mbs, n)
            while True:
                for i, v in enumerate(regs):
                    L.w32(s.TB + 8 * i + 4, v)
                L.w32(s.H + 3888, size)
                L.w32(s.H + 736, 1)
                if L.call('i264e_ratecontrol_is_reenc', s.H) != 1:
                    break
                out.append(L.rd(s.R + L.lay['qp'], 1)[0])
                size = max(size // 2, 20)
            print('F', since, ' '.join(str(v) for v in out))
            print('M', ' '.join(str(v) for v in mline))


def mbrc_calls(lib, seed, count):
    """--mbrc-calls: h264_get_mb_qp on random controller state.  'C' line:
    mbw mbh mbs(A+52) frame(S+0) type(S+28) qp(S+68) S+88 4 counter words
    slice+859 | the 7 SAS offsets (S+5336) | FNV-1a of slice +752..+925
    (without the pointers; the slice zero but +859 before)."""
    import random
    rnd = random.Random(seed)
    L = Lib(lib)
    t21 = L.lay['name'] == 'T21'
    d = -40 if t21 else 0
    A, S, SL = HEAP, HEAP + 0x1000, HEAP + 0x6000
    marks = [7, 12, 16, 20, 22, 35, 40, 52, 70, 80, 82, 92, 100]
    for _ in range(count):
        mbw, mbh = rnd.randint(1, 160), rnd.randint(1, 90)
        mbs = rnd.choice([mbw * mbh, mbw * mbh, rnd.randint(1, 9000), 0, -5])
        m = max(mbs, 1)

        def pick(k):
            if rnd.random() < 0.5:
                v = int(round(rnd.choice(marks) * m / 100)) + rnd.choice((-1, 0, 1))
            else:
                v = rnd.randint(0, m)
            return [min(0xffff, max(v, 0) // k)] * k
        c = pick(3) + pick(2) + pick(2)
        words = [c[0] | c[1] << 16, c[2] | c[3] << 16, c[4] | c[5] << 16,
                 c[6] | rnd.getrandbits(16) << 16]
        frame = rnd.choice((0, 1, rnd.randint(2, 100000)))
        typ = rnd.choice((0, 2, 6))
        qp = rnd.randint(0, 51)
        s88 = rnd.getrandbits(32)
        s859 = rnd.choice((0, rnd.randint(1, 255)))
        L.wr(HEAP, bytes(0x8000))
        L.w32(A + 40, mbw); L.w32(A + 44, mbh); L.w32(A + 52, mbs)
        L.w32(A + 208, 0x30); L.w32(A + 212, 0x30)
        L.w32(S, frame); L.w32(S + 28, typ); L.w8(S + 68, qp); L.w32(S + 88, s88)
        for i, v in enumerate(words):
            L.w32(S + 4488 + d + 4 * i, v)
        L.w32(S + 6772 + d, SL)
        L.w8(SL + 859, s859)
        L.call('h264_get_mb_qp', A, S, HEAP + 0x4000)
        ofs = struct.unpack('<7b', L.rd(S + 5336 + d, 7))
        sl = bytearray(L.rd(SL + 752, 174))
        sl[68:72] = bytes(4)
        sl[112:120] = bytes(8)
        print('C', mbw, mbh, mbs, frame, typ, qp, s88, ' '.join(str(v) for v in words), s859,
              ' '.join(str(v) for v in ofs), fnv1a(sl))


def main():
    lib = sys.argv[1]
    if len(sys.argv) > 4 and sys.argv[2] == '--mbrc':
        mbrc_scenes(lib, int(sys.argv[3]), int(sys.argv[4]),
                    int(sys.argv[5]) if len(sys.argv) > 5 else 100)
        return
    if len(sys.argv) > 4 and sys.argv[2] == '--mbrc-calls':
        mbrc_calls(lib, int(sys.argv[3]), int(sys.argv[4]))
        return
    frames = 120
    scenarios = SCENARIOS
    if len(sys.argv) > 4 and sys.argv[2] == '--random':
        scenarios = random_scenarios(int(sys.argv[3]), int(sys.argv[4]))
        frames = int(sys.argv[5]) if len(sys.argv) > 5 else 150
    elif len(sys.argv) > 2 and sys.argv[2] == '--extended':
        # T21: no param[52]; the picture type decision is only run on T23
        # (the T21 rule is the same, 0x2bfd4)
        t21 = Lib(lib).lay['name'] == 'T21'
        scenarios = [sc for sc in EXTENDED if not t21 or (sc[18] == 0 and not sc[20] & 2)]
        frames = 200
    for n, sc in enumerate(scenarios):
        (mode, w, h, gop, fps, minqp, maxqp, br, mbr, fstep, gstep, bias, static,
         cpos, qual, bgmul, initqp, idr) = sc[:18]
        f52, cqp, flags = (tuple(sc[18:]) + (0, 0, 0))[:3]
        extra = validated_vbv(lib, w, h)
        sh = 0 if Lib(lib).lay['name'] == 'T23' else -4
        if f52:
            if sh:
                raise RuntimeError('param[52] exists on T23 only')
            extra[52] = f52
        extra[204] = cqp
        s = Sim(lib, mode=mode, w=w, h=h, gop=gop, fps=(fps, 1), minqp=minqp, maxqp=maxqp,
                bitrate=br, maxbitrate=mbr, frm_step=fstep, gop_step=gstep, bias=bias,
                static_time=static, change_pos=cpos, quality=qual, bgmul=bgmul,
                rclevel=initqp & 0xffffffff, extra=extra)
        L = s.L
        E = s.R + (496 if sh == 0 else 484)
        zbuf = L.r32(L.r32(E + (1620 if sh == 0 else 1616)) + (7148 if sh == 0 else 7092))
        ZONES = HEAP + 0x3c000
        if flags & 2:
            if sh:
                raise RuntimeError('vendor picture types: T23 only')
            out2 = HEAP + 0x3e000
            L.call('i264e_init_skip_header', s.R + 0xac0, gop, 0, 0, 0, bgmul, 0,
                   out2, out2 + 4, 0, f52)
            L.w32(s.R + 0x3dd8, s.H)
            L.w32(s.H, 0xffffffff)
        print('S', ' '.join(str(v) for v in sc))
        for f in range(frames):
            if flags & 2:
                L.call('i264e_decide_slice_type_and_rd', s.R, s.H)
                since = L.r32(s.H)
            else:
                since = f % idr
                L.w32(s.H, since)
            if flags & 1:
                L.wr(ZONES, struct.pack('<225I', *ae_zones(f)))
                L.w8(s.F + 0x1e0, 1)
                L.w32(s.F + 0x1e4, ZONES)
            L.call('i264e_ratecontrol_start', s.H)
            t = L.rd(s.SL, 1)[0]          # picture type (2 IDR, 6 GOP start, 0 P)
            qp = L.rd(s.R + L.lay['qp'], 1)[0]
            out = [t, qp]
            zcrc = fnv1a(L.rd(zbuf, 3648)) if flags & 1 else None
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
                qp = s.L.rd(s.R + s.L.lay['qp'], 1)[0]
                out.append(qp)
                size = max(size // 2, 20)
            print('F', since, ' '.join(str(v) for v in out))
            if zcrc is not None:
                print('Z', zcrc)


if __name__ == '__main__':
    main()
