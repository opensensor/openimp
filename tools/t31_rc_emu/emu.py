#!/usr/bin/env python3
"""Load a vendor libimp.so into unicorn (MIPS32 LE) and call its functions (host RE aid; from the T23 eprc work)."""
import math, re, struct, subprocess, sys
sys.path.insert(0, '<path>/t23native/pylib')
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UcError, UC_ARCH_MIPS, UC_MODE_MIPS32, UC_MODE_LITTLE_ENDIAN, UC_HOOK_CODE, UC_ERR_READ_UNALIGNED, UC_ERR_WRITE_UNALIGNED
from unicorn.mips_const import *

LIB = '<thingino>/dl/ingenic-lib/git/T23/lib/1.3.0/uclibc/5.4.0/libimp.so'
STUB, STACK, END, HEAP = 0x70000000, 0x60000000, 0x7ff00000, 0x50000000
A = [UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3]

class Lib:
    def __init__(self, path=LIB):
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
        got = subprocess.run(['readelf', '-A', path], capture_output=True, text=True).stdout
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
        self.log = []
        self.unknown = set()
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
            self.log.append((name, x, r))
        elif name in ('free', 'i264e_log', 'puts', 'fwrite', 'vfprintf', 'printf', 'fprintf'):
            pass
        elif name == 'access':
            ret = 0xffffffff
        elif name == 'exit':
            raise RuntimeError('exit(%d) called' % a[0])
        else:
            self.unknown.add(name)
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
