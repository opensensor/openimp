#!/usr/bin/env python3
"""Compare the OEM T21 and T23 Helix H.264 slice builders (development aid).

Runs H264E_T21_SliceInit from a vendor T21 and a vendor T23 libimp.so
under the unicorn CPU emulator on identical inputs and prints how the T23
command list differs from the T21 one, plus the register-order digest that
tests/t23/helix_descriptor_test.c checks OpenIMP's T23 list against.

Nothing from the vendor libraries is copied into OpenIMP; the libraries are
read from paths you pass, and only register offsets and constants written
by the builders are reported.

    pip install unicorn pyelftools
    tools/helix_oem_descriptor_compare.py --readelf mipsel-linux-readelf \\
        T21/lib/1.0.33/uclibc/5.4.0/libimp.so \\
        T23/lib/1.3.0/uclibc/5.4.0/libimp.so

The slice structure is the vendor's private one.  The script fills only
what the builders need to run (picture size in macroblocks, the
descriptor pointer at +672, the CABAC state pointer at +440, flat 8x8
scaling lists at +132, buffer sharing off at +1116); everything else is
zero, so the output describes register order and the constants that do not
depend on the vendor's per-picture state.
"""
import argparse
import difflib
import re
import struct
import subprocess

from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_MIPS, UC_MODE_MIPS32, UC_MODE_LITTLE_ENDIAN
from unicorn import UC_HOOK_CODE
from unicorn.mips_const import (UC_MIPS_REG_A0, UC_MIPS_REG_PC,
                                UC_MIPS_REG_RA, UC_MIPS_REG_SP,
                                UC_MIPS_REG_T9, UC_MIPS_REG_V0)

STUB, STACK, END, HEAP = 0x70000000, 0x60000000, 0x7ff00000, 0x50000000
SLICE, DESCRIPTOR, TABLE = HEAP, HEAP + 0x100000, HEAP + 0x200000


class Library:
    def __init__(self, path, readelf):
        self.uc = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 + UC_MODE_LITTLE_ENDIAN)
        self.symbols = {}
        with open(path, 'rb') as f:
            elf = ELFFile(f)
            loads = [s for s in elf.iter_segments()
                     if s['p_type'] == 'PT_LOAD']
            top = max(s['p_vaddr'] + s['p_memsz'] for s in loads)
            self.uc.mem_map(0, (top + 0xffff) & ~0xffff)
            for s in loads:
                self.uc.mem_write(s['p_vaddr'], s.data())
            for section in elf.iter_sections():
                if section.name in ('.dynsym', '.symtab'):
                    for sym in section.iter_symbols():
                        if sym['st_value']:
                            self.symbols.setdefault(sym.name,
                                                    sym['st_value'])
        for base, size in ((STUB, 0x100000), (STACK - 0x100000, 0x200000),
                           (END, 0x1000), (HEAP, 0x400000)):
            self.uc.mem_map(base, size)
        # undefined imports return 0
        got = subprocess.run([readelf, '-A', path], capture_output=True,
                             text=True, check=True).stdout
        count = 0
        for line in got.splitlines():
            m = re.match(r'\s+([0-9a-f]{8})\s+-?\d+\(gp\)\s+[0-9a-f]{8}\s+'
                         r'[0-9a-f]{8}\s+\w+\s+UND\s', line)
            if m:
                self.uc.mem_write(int(m.group(1), 16),
                                  struct.pack('<I', STUB + 8 * count))
                count += 1
        self.uc.mem_write(STUB, b'\x08\x00\xe0\x03\x00\x00\x00\x00' * count)
        self.uc.hook_add(UC_HOOK_CODE, self._stub, begin=STUB,
                         end=STUB + 8 * count)

    @staticmethod
    def _stub(uc, address, size, user):
        uc.reg_write(UC_MIPS_REG_V0, 0)
        uc.reg_write(UC_MIPS_REG_PC, uc.reg_read(UC_MIPS_REG_RA))

    def slice_init(self, slice_type, mb_width, mb_height):
        blob = bytearray(0x1200)
        blob[0], blob[1], blob[2], blob[4] = (slice_type, mb_width,
                                              mb_height, mb_height - 1)
        blob[8:12] = struct.pack('<I', mb_width * 16)
        blob[12:16] = struct.pack('<I', mb_height * 16)
        blob[132:260] = bytes([16]) * 128
        blob[440:444] = struct.pack('<I', TABLE)
        blob[672:676] = struct.pack('<I', DESCRIPTOR)
        self.uc.mem_write(SLICE, bytes(blob))
        self.uc.mem_write(DESCRIPTOR, bytes(0x10000))
        self.uc.mem_write(TABLE, bytes((i * 37) % 126 + 1
                                       for i in range(1024)))
        function = self.symbols['H264E_T21_SliceInit']
        self.uc.reg_write(UC_MIPS_REG_SP, STACK)
        self.uc.reg_write(UC_MIPS_REG_T9, function)
        self.uc.reg_write(UC_MIPS_REG_RA, END)
        self.uc.reg_write(UC_MIPS_REG_A0, SLICE)
        self.uc.emu_start(function, END, count=20000000)
        pairs = []
        while True:
            value, command = struct.unpack(
                '<II', self.uc.mem_read(DESCRIPTOR + 8 * len(pairs), 8))
            pairs.append((command, value))
            if not command & 0x80000000 or command & 0x40000000:
                return pairs


def digest(pairs):
    h = 0x811c9dc5
    for command, _ in pairs:
        for b in struct.pack('<I', (command & 0xffffc) |
                             (command & 0x40000000)):
            h = ((h ^ b) * 0x01000193) & 0xffffffff
    return h


def line(pair, rebase):
    command, value = pair
    # CABAC range tables (0x92000..) are data, not addresses
    if rebase and value >> 20 == 0x131 and \
            not 0x92000 <= (command & 0xffffc) < 0x92800:
        value += 0x100000
    return '%05x%s=%08x' % (command & 0xffffc,
                            'T' if command & 0x40000000 else '', value)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--readelf', default='mipsel-linux-readelf')
    parser.add_argument('--mb', default='120x68',
                        help='picture size in macroblocks')
    parser.add_argument('t21_libimp')
    parser.add_argument('t23_libimp')
    args = parser.parse_args()
    mb_width, mb_height = (int(v) for v in args.mb.split('x'))
    t21 = Library(args.t21_libimp, args.readelf)
    t23 = Library(args.t23_libimp, args.readelf)
    for slice_type, name in ((0, 'I'), (1, 'P')):
        a = t21.slice_init(slice_type, mb_width, mb_height)
        b = t23.slice_init(slice_type, mb_width, mb_height)
        print('%s: T21 %d writes digest %08x, T23 %d writes digest %08x' %
              (name, len(a), digest(a), len(b), digest(b)))
        print('  T23 vs T21 (T23 Helix addresses moved +0x100000):')
        for d in difflib.unified_diff([line(p, False) for p in a],
                                      [line(p, True) for p in b],
                                      n=0, lineterm=''):
            if d[:1] in '+-' and d[:3] not in ('---', '+++'):
                print('   ', d)


if __name__ == '__main__':
    main()
