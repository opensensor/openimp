#!/usr/bin/env python3
"""Per-picture size, type and slice QP of an H.264 recording (mp4/h264).

    tools/rc_sim/h264_trace.py rec.mp4 > rec.csv     # n,type,bytes,qp

Needs ffmpeg (to get Annex B).  Parses SPS/PPS and the slice headers up to
slice_qp_delta (CAVLC/CABAC, frame pictures, no B-slices needed).  The QP is
the slice QP of the first slice of each picture.
"""
import subprocess
import sys


class Bits:
    def __init__(self, b):
        # remove emulation prevention bytes
        out = bytearray()
        z = 0
        for c in b:
            if z >= 2 and c == 3:
                z = 0
                continue
            out.append(c)
            z = z + 1 if c == 0 else 0
        self.b, self.p = bytes(out), 0

    def u(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.b[self.p >> 3] >> (7 - (self.p & 7))) & 1)
            self.p += 1
        return v

    def ue(self):
        z = 0
        while self.u(1) == 0:
            z += 1
        return (1 << z) - 1 + self.u(z)

    def se(self):
        k = self.ue()
        return (k + 1) // 2 if k & 1 else -(k // 2)


def skip_scaling(r, n):
    last = nxt = 8
    for _ in range(n):
        if nxt:
            nxt = (last + r.se() + 256) % 256
        last = nxt or last


def parse_sps(r):
    sps = {}
    prof = r.u(8); r.u(16)
    sps['id'] = r.ue()
    if prof in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135):
        cf = r.ue()
        if cf == 3:
            r.u(1)
        r.ue(); r.ue(); r.u(1)
        if r.u(1):
            for i in range(8 if cf != 3 else 12):
                if r.u(1):
                    skip_scaling(r, 16 if i < 6 else 64)
    sps['fn'] = r.ue() + 4
    sps['poc'] = r.ue()
    if sps['poc'] == 0:
        sps['pocl'] = r.ue() + 4
    elif sps['poc'] == 1:
        sps['dz'] = r.u(1); r.se(); r.se()
        for _ in range(r.ue()):
            r.se()
    r.ue(); r.u(1)
    r.ue(); r.ue()
    sps['fmo'] = r.u(1)
    return sps


def parse_pps(r):
    pps = {'id': r.ue(), 'sps': r.ue(), 'cabac': r.u(1), 'popp': r.u(1)}
    if r.ue() != 0:
        raise ValueError('slice groups')
    pps['l0'] = r.ue() + 1; pps['l1'] = r.ue() + 1
    pps['wp'] = r.u(1); pps['wb'] = r.u(2)
    pps['qp0'] = 26 + r.se()
    r.se(); r.se()
    r.u(1); r.u(1); pps['rpc'] = r.u(1)
    return pps


def slice_qp(r, nal_type, ref_idc, spss, ppss):
    first_mb = r.ue()
    st = r.ue() % 5
    pps = ppss[r.ue()]
    sps = spss[pps['sps']]
    r.u(sps['fn'])
    if not sps['fmo']:
        if r.u(1):
            r.u(1)
    if nal_type == 5:
        r.ue()
    if sps['poc'] == 0:
        r.u(sps['pocl'])
        if pps['popp']:
            r.se()
    elif sps['poc'] == 1 and not sps['dz']:
        r.se()
        if pps['popp']:
            r.se()
    if pps['rpc']:
        r.ue()
    if st == 1:
        r.u(1)
    if st in (0, 1, 3):
        if r.u(1):
            r.ue()
            if st == 1:
                r.ue()
    if st not in (2, 4):  # ref_pic_list_modification
        for _ in range(2 if st == 1 else 1):
            if r.u(1):
                while True:
                    m = r.ue()
                    if m == 3:
                        break
                    r.ue()
    if (pps['wp'] and st in (0, 3)) or (pps['wb'] == 1 and st == 1):
        raise ValueError('weighted prediction not handled')
    if ref_idc:
        if nal_type == 5:
            r.u(1); r.u(1)
        elif r.u(1):
            while True:
                m = r.ue()
                if m == 0:
                    break
                if m in (1, 3):
                    r.ue()
                if m in (2, 3, 6):
                    r.ue()
                if m == 4:
                    r.ue()
    if pps['cabac'] and st not in (2, 4):
        r.ue()
    return first_mb, st, pps['qp0'] + r.se()


def trace(path):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-c:v', 'copy', '-an',
                          '-bsf:v', 'h264_mp4toannexb', '-f', 'h264', '-'],
                         capture_output=True, check=True).stdout
    nals, i = [], 0
    starts = []
    while True:
        j = raw.find(b'\x00\x00\x01', i)
        if j < 0:
            break
        starts.append(j + 3)
        i = j + 3
    for k, s in enumerate(starts):
        e = starts[k + 1] - 3 if k + 1 < len(starts) else len(raw)
        nal = raw[s:e].rstrip(b'\x00')
        nals.append(nal)
    spss, ppss, pics = {}, {}, []
    for nal in nals:
        if not nal:
            continue
        t, ref = nal[0] & 31, (nal[0] >> 5) & 3
        if t == 7:
            s = parse_sps(Bits(nal[1:])); spss[s['id']] = s
        elif t == 8:
            p = parse_pps(Bits(nal[1:])); ppss[p['id']] = p
        elif t in (1, 5):
            fm, st, qp = slice_qp(Bits(nal[1:]), t, ref, spss, ppss)
            if fm == 0:
                pics.append(['I' if t == 5 or st == 2 else 'P', 0, qp])
            pics[-1][1] += len(nal) + 4
    return pics


if __name__ == '__main__':
    print('n,type,bytes,qp')
    for n, (t, b, q) in enumerate(trace(sys.argv[1])):
        print('%d,%s,%d,%d' % (n, t, b, q))
