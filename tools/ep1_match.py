#!/usr/bin/env python3
"""Compare a stock EP1 dump (from oem_trace) with OpenIMP's generator.

usage: ep1_match.py PROBE_BINARY ep1-000.bin [more dumps...]

PROBE_BINARY is tools/t31_hwjpeg_probe built for the PC (gcc, no device
needed); its -G option writes the generated EP1 for a quality. Reports the
quality that matches exactly, or the closest one with the differing byte
ranges, which usually points at the wrong field (quant order, reciprocals,
Huffman words, header words at 0x180/0x184).
"""
import os
import subprocess
import sys
import tempfile

REGIONS = [
    (0x000, 0x040, "luma quant (zigzag)"), (0x040, 0x080, "chroma quant (zigzag)"),
    (0x080, 0x100, "luma reciprocals"), (0x100, 0x180, "chroma reciprocals"),
    (0x180, 0x188, "header words"), (0x188, 0x1c8, "BITS"),
    (0x1c8, 0x450, "AC luma codebook"), (0x450, 0x480, "DC luma codebook"),
    (0x480, 0x708, "AC chroma codebook"), (0x708, 0x738, "DC chroma codebook"),
    (0x738, 0x790, "zero tail"),
]


def generate(probe, q, tmp):
    path = os.path.join(tmp, f"ep1-q{q}.bin")
    subprocess.run([probe, "-q", str(q), "-G", path], check=True)
    return open(path, "rb").read()


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    probe = sys.argv[1]
    with tempfile.TemporaryDirectory() as tmp:
        ours = {q: generate(probe, q, tmp) for q in range(1, 101)}
        for dump in sys.argv[2:]:
            stock = open(dump, "rb").read()[:0x790]
            diffs = {q: sum(a != b for a, b in zip(stock, e)) for q, e in ours.items()}
            q = min(diffs, key=diffs.get)
            if diffs[q] == 0:
                print(f"{dump}: identical to OpenIMP EP1 at quality {q}")
                continue
            print(f"{dump}: no exact match; closest quality {q} with {diffs[q]} differing bytes")
            for lo, hi, name in REGIONS:
                n = sum(stock[i] != ours[q][i] for i in range(lo, min(hi, len(stock))))
                if n:
                    first = next(i for i in range(lo, hi) if stock[i] != ours[q][i])
                    print(f"  {name:22s} 0x{lo:03x}-0x{hi:03x}: {n} bytes differ, first at 0x{first:03x} "
                          f"(stock {stock[first]:02x}, ours {ours[q][first]:02x})")


if __name__ == "__main__":
    main()
