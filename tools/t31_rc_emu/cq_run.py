#!/usr/bin/env python3
"""Mode 8 (CappedVBR) vs 9 (CappedQuality) with identical inputs; dump per-picture state."""
import sys, math, random
import cq_emu
from cq_emu import T31, RC, rc_param, gop_param, status, sse_for_psnr

OOLO = 0x569f8

def run(mode, frames, seed, psnr_cap=4200, target=2_000_000, maxbr=4_000_000, scene=1.0, log=False,
        psnr_fn=None, L=None, patch=None):
    L = L or T31()
    rc = RC(L, mode, rc_param(mode, target=target, maxbr=maxbr, psnr=psnr_cap), gop_param(length=25))
    if patch: patch(L, rc)
    rnd = random.Random(seed)
    rows = []
    for n in range(frames):
        ptype = 2 if n % 25 == 0 else 1
        qp = rc.qp()
        # picture QP as the encoder would use it: I = qp (st+30 not modelled), P = qp
        sc = scene(n) if callable(scene) else scene
        base = (320_000 if ptype == 2 else 40_000) * sc
        size = int(base * 2 ** ((30 - qp) / 6.0) * rnd.uniform(0.9, 1.1)) & ~7
        psnr_db = psnr_fn(qp, n) if psnr_fn else 50 - qp * 0.4
        st = status(size, sse_for_psnr(psnr_db, 1920 * 1080))
        st[60] = (qp - rc.st32(164)) if ptype == 2 else qp   # status QP = used QP (I = base - IP delta)
        flags = 3 if ptype == 2 else 2
        pcs = rc.picture(ptype, flags, st, size)
        hrd = rc.st + 72
        level = L.callv(OOLO, hrd)
        if level & 0x80000000: level -= 1 << 32
        row = dict(n=n, t=ptype, qp_in=qp, size=size, psnr=round(psnr_db, 1), qp=rc.qp(),
                   idle=rc.hrd(60), pics=rc.hrd(56), arr=rc.hrd(24), rem=rc.hrd(32), level=level,
                   gate=0x55218 in pcs, down=0x54d7c in pcs, up=0x54f94 in pcs, skip264=(0x55224 in pcs),
                   stepup=0x55184 in pcs, ioli=0x5535c in pcs, clamp=0x552b8 in pcs, emerg=0x55374 in pcs, delta=L.delta, capped=L.capped)
        rows.append(row)
        if log:
            print('%(n)3d t%(t)d qp %(qp_in)2d->%(qp)2d size %(size)7d psnr %(psnr)5.1f idle %(idle)7d pics %(pics)3d '
                  'arr %(arr)8d rem %(rem)8d level %(level)9d gate %(gate)d down %(down)d up %(up)d skip %(skip264)d d=%(delta)d cap=%(capped)d su=%(stepup)d io=%(ioli)d cl=%(clamp)d em=%(emerg)d' % row)
    return rows

if __name__ == '__main__':
    scen = sys.argv[1] if len(sys.argv) > 1 else 'static'
    frames = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    scenes = {'static': 1.0, 'busy': 4.0, 'mixed': (lambda n: 1.0 if (n // 50) % 2 == 0 else 6.0)}
    def summary(rows):
        return dict(qp=[r['qp'] for r in rows], gate=sum(r['gate'] for r in rows), skip=sum(r['skip264'] for r in rows),
                    down=sum(r['down'] for r in rows), up=sum(r['up'] for r in rows), idle=rows[-1]['idle'],
                    minlevel=min(r['level'] for r in rows), bits=sum(r['size'] for r in rows))
    res = {}
    for mode in (8, 9):
        rows = run(mode, frames, 1, scene=scenes[scen], log='-v' in sys.argv)
        res[mode] = summary(rows)
        print('mode %d: gate %d skip %d down %d up %d idle %d minlevel %d kbit/s %.0f' % (mode, res[mode]['gate'], res[mode]['skip'],
              res[mode]['down'], res[mode]['up'], res[mode]['idle'], res[mode]['minlevel'], res[mode]['bits'] / frames * 25 / 1000))
        print('  qp:', ' '.join('%d' % q for q in res[mode]['qp']))
    # patch experiment: force the two bytes
    def p8(L, rc):  # make mode 8 look like 9
        L.wr(rc.st + 303, b'\0'); L.wr(rc.st + 72 + 21, b'\0')
    def p9(L, rc):
        L.wr(rc.st + 303, b'\1'); L.wr(rc.st + 72 + 21, b'\1')
    r8 = summary(run(8, frames, 1, scene=scenes[scen], patch=p8))
    r9 = summary(run(9, frames, 1, scene=scenes[scen], patch=p9))
    print('mode 8 with st[303]=0,hrd[21]=0 == mode 9:', r8 == res[9], ' mode 9 with 1,1 == mode 8:', r9 == res[8])
