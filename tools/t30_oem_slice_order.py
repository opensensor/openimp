#!/usr/bin/env python3
"""Compare OpenIMP's T30 Helix command-list register order with the OEM one.

Runs H264E_T30_SliceInit from a vendor T30 1.0.5 libimp.so under the unicorn
CPU emulator (the harness of tools/helix_oem_descriptor_compare.py) and
compares the register sequence it writes with tools/t30_descriptor_dump
(OpenIMP's src/t30/t30_h264_descriptor.c) for I and P slices.  Nothing from
the vendor library is copied into OpenIMP; it is read from the path you pass.

    pip install unicorn pyelftools
    tools/t30_oem_slice_order.py --readelf mipsel-linux-readelf \\
        --dump ./t30_descriptor_dump T30/lib/1.0.5/uclibc/5.4.0/libimp.so

Only the T30 slice-structure fields the builder needs to run are filled:
slice type and macroblock geometry at +0..+15 (as on T21) and the
descriptor pointer at +264 (T21: +672).  Addresses, QP, CABAC state and
scaling lists stay zero in the OEM run, so register *values* differ by
construction; the check is the register order and the pair count
(1920x1080: 611 I / 785 P pairs).
"""
import argparse
import importlib.util
import os
import struct
import subprocess
import sys

from unicorn.mips_const import (UC_MIPS_REG_A0, UC_MIPS_REG_RA,
                                UC_MIPS_REG_SP, UC_MIPS_REG_T9)

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    'helix_cmp', os.path.join(HERE, 'helix_oem_descriptor_compare.py'))
helix = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helix)

T30_DESCRIPTOR_POINTER = 264


def oem_slice(lib, slice_type, mb_width, mb_height):
    blob = bytearray(0x1200)
    blob[0], blob[1], blob[2], blob[4] = (slice_type, mb_width, mb_height,
                                          mb_height - 1)
    blob[8:12] = struct.pack('<I', mb_width * 16)
    blob[12:16] = struct.pack('<I', mb_height * 16)
    blob[T30_DESCRIPTOR_POINTER:T30_DESCRIPTOR_POINTER + 4] = \
        struct.pack('<I', helix.DESCRIPTOR)
    lib.uc.mem_write(helix.SLICE, bytes(blob))
    lib.uc.mem_write(helix.DESCRIPTOR, bytes(0x10000))
    function = lib.symbols['H264E_T30_SliceInit']
    lib.uc.reg_write(UC_MIPS_REG_SP, helix.STACK)
    lib.uc.reg_write(UC_MIPS_REG_T9, function)
    lib.uc.reg_write(UC_MIPS_REG_RA, helix.END)
    lib.uc.reg_write(UC_MIPS_REG_A0, helix.SLICE)
    lib.uc.emu_start(function, helix.END, count=20000000)
    pairs = []
    while len(pairs) < 8192:
        value, command = struct.unpack(
            '<II', lib.uc.mem_read(helix.DESCRIPTOR + 8 * len(pairs), 8))
        pairs.append((command, value))
        if not command & 0x80000000 or command & 0x40000000:
            break
    return [helix.line(p, False).split('=')[0] for p in pairs]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--readelf', default='mipsel-linux-readelf')
    parser.add_argument('--dump', default='./t30_descriptor_dump')
    parser.add_argument('--mb', default='120x68')
    parser.add_argument('t30_libimp')
    args = parser.parse_args()
    mb_width, mb_height = (int(v) for v in args.mb.split('x'))
    lib = helix.Library(args.t30_libimp, args.readelf)
    failed = False
    for slice_type, name in ((0, 'I'), (1, 'P')):
        oem = oem_slice(lib, slice_type, mb_width, mb_height)
        ours = [l.split('=')[0] for l in subprocess.run(
            [args.dump, str(slice_type), str(mb_width), str(mb_height)],
            check=True, capture_output=True, text=True).stdout.split()]
        same = oem == ours
        failed |= not same
        print('%s: OEM %d pairs, OpenIMP %d pairs, register order %s' %
              (name, len(oem), len(ours), 'identical' if same else 'DIFFERS'))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
