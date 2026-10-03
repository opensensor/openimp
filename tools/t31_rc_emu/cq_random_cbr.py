#!/usr/bin/env python3
"""Random CBR traces (AL_RateCtrl mode 0) for tests/t31/al_rc_trace_test.c, including
the GOP / rc-param variants the T31 IMP never sets (rc param +34 special-P flag, GOP
modes 3/8/9/0x12, B pictures, eRcOptions variants), and the IIii instruction coverage.

    python3 cq_random_cbr.py <outdir> <seed_from> <seed_to> <frames>

P34=<probability> and GM='[gop modes]' bias the variants."""
import os, random, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cq_trace
from cq_trace import TraceRC, MB
from cq_emu import T31, rc_param, gop_param, sse_for_psnr
from unicorn import UC_HOOK_CODE

COV = set()

def rcp_x(rnd, al_mode, **kw):
    b = bytearray(rc_param(al_mode, **kw))
    if rnd.random() < float(os.environ.get('P34', '0.3')):
        b[34] = 1
        struct.pack_into('<hH', b, 36, rnd.choice([1, 2, 3, 5]), rnd.choice([1, 2, 3, 4]))
    return bytes(b)

def run(seed, frames, out):
    rnd = random.Random(seed)
    L = T31()
    L.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: COV.add(a), begin=0x53360, end=0x5468c)
    gop_mode = rnd.choice(eval(os.environ.get('GM', '[2, 2, 2, 8, 9, 3, 0x10 | 2]')))
    gop_len = rnd.choice([10, 25, 30, 50, 1, 2, 3, 4])
    numb = rnd.choice([0, 0, 1, 2]) if gop_len > 3 else 0
    fps = rnd.choice([(25, 1), (30, 1), (15, 1), (30000, 1001), (20, 1)])
    target = rnd.choice([256_000, 512_000, 1_200_000, 3_000_000, 4_000_000])
    qmin, qmax = rnd.choice([(15, 48), (10, 51), (20, 40), (0, 51)])
    opts = rnd.choice([1, 17, 0, 3, 5, 16])
    fr, clk = fps[0], (fps[1] * 1000) & 0xffff
    kw = dict(fps=fr, clk=clk, target=target, maxbr=target, qp0=rnd.choice([25, 30, 34]),
              qmin=qmin, qmax=qmax, ipd=rnd.choice([-1, -1, 1, 3]), pbd=rnd.choice([-1, 2]),
              opts=opts, cpb_ticks=rnd.choice([270000, 90000]), rem_ticks=rnd.choice([216000, 45000]))
    if kw['rem_ticks'] > kw['cpb_ticks']:
        kw['rem_ticks'] = kw['cpb_ticks']
    rcp = rcp_x(rnd, 1, **kw)
    gop = gop_param(mode=gop_mode, length=gop_len, numb=numb)
    try:
        rc = TraceRC(L, 0, rcp, gop, out)
    except Exception as e:
        out.write('E\n'); return 'init ' + str(e)
    scene = rnd.choice([0.3, 1.0, 2.5])
    sticky = 0
    for n in range(frames):
        k = n % gop_len
        if k == 0:
            ptype, flags = 2, 3
        elif numb and k % (numb + 1) != 0:
            ptype, flags = 0, 2
        else:
            ptype, flags = 1, 2
        if rnd.random() < 0.05: flags &= ~2
        if rnd.random() < 0.02: flags |= 4
        if rnd.random() < 0.05: flags |= 0x80
        qpoff = rnd.choice([0, 0, 0, -2, 1])
        fixed = 1 if rnd.random() < 0.02 else 0
        forced = rnd.randint(0, 51)
        try:
            qp = rc.query_qp(ptype, flags, qpoff, fixed, forced)
        except RuntimeError as e:
            out.write('E\n'); return str(e)
        if rnd.random() < 0.05:
            scene = rnd.choice([0.1, 0.3, 1.0, 2.5, 6.0, 15.0])
        base = (320_000 if ptype == 2 else 40_000) * scene * (target / 2_000_000)
        size = max(int(base * 2 ** ((30 - qp) / 6.0) * rnd.uniform(0.7, 1.3)) & ~7, 64)
        intra = rnd.randint(0, 100) if ptype != 2 else rnd.randint(80, 100)
        stat = {36: MB, 40: rnd.randint(0, 50), 44: rnd.randint(0, 20), 48: rnd.randint(0, 5),
                28: MB * intra // 100, 32: MB * rnd.randint(0, 100) // 100,
                20: size * rnd.randint(0, 100) // 100, 24: rnd.randint(0, MB), 'qp': qp}
        sse = sse_for_psnr(52 - qp * 0.45 + rnd.uniform(-2, 2), 1920 * 1080)
        stat['sse_lo'], stat['sse_hi'] = sse & 0xffffffff, sse >> 32
        ovf = 1 if rnd.random() < 0.02 else 0
        try:
            filler = rc.picture2(ptype, flags, qpoff, fixed, forced, size, ovf, sticky, stat)
        except RuntimeError as e:
            out.write('E\n'); return str(e)
        sticky = max(filler, 8) * 8 if filler > 0 else (sticky if rnd.random() < 0.5 else 0)
        if rnd.random() < 0.01:
            kw['target'] = kw['maxbr'] = int(kw['target'] * rnd.choice([0.5, 1.5]))
            kw['fps'] = rnd.choice([fr, 25, 15]); kw['qp0'] = 30
            rc.set_params(rcp_x(rnd, 1, **kw), gop)
        if rnd.random() < 0.005:
            rc.reset()
    out.write('E\n')
    return None

if __name__ == '__main__':
    outdir, n0, n1, frames = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
    os.makedirs(outdir, exist_ok=True)
    for s in range(n0, n1):
        with open(os.path.join(outdir, 'rcbr_%d.trc' % s), 'w') as f:
            r = run(s, frames, f)
        if r: print('seed', s, r)
    total = (0x5468c - 0x53360) // 4
    print('IIii coverage %d/%d instructions' % (len(COV), total))
