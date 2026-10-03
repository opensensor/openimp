"""Opt-in "beyond vendor" variants for rc_sim.py.

Each variant only acts through what an OpenIMP wrapper around the
vendor-identical port could do: change controller inputs/fields between
pictures (OEM-layout offsets, see docs/T20_RC.md, docs/T23_EPRC.md), clamp
the picture QP and write it back where the controller reads the coded QP,
or decline a re-encode.  The ports themselves stay bit-exact.
"""
import statistics as st

from rc_sim import Variant, run, fmt, header, SCENES

E, PB, SB = 0, 1, 2      # OEM blocks: E, rcPara / eprc state, rcSt / eprc S


def write_qp(c, qp):
    """Make the controller believe it coded qp (where FrameEnd reads it)."""
    if c.soc in ('T23', 'T21'):
        c.wr(E, 1600, qp, '<B')
    elif c.soc == 'T20':
        c.wr(SB, 32, qp)
        c.wr(E, 217, qp, '<B')
    elif c.soc == 'T10':
        c.wr(SB, 40, qp)
        c.wr(E, 177, qp, '<B')


# ---------------------------------------------------------------- T10 VBR
class T10SuperFix(Variant):
    """T10 VBR: compare the super-frame thresholds in bits (as the T20
    controller does) instead of bits/1024 -> no re-encode storm.
    OpenIMP: after RCT10_Init write E+88/E+92 = superI/superP bits."""
    name = 't10_superfix'

    def __init__(self, i_bits=19660800, p_bits=14043429):
        self.i, self.p = i_bits, p_bits

    def setup(self, c, cfg):
        if c.soc == 'T10' and c.mode == 'VBR':
            c.wr(E, 88, self.i, '<I')
            c.wr(E, 92, self.p, '<I')


class T10SuperBudget(T10SuperFix):
    """T10 VBR: super-frame limits relative to the bit rate (I > 1 s of
    target, P > 0.25 s): keeps a re-encode as an emergency brake only."""
    name = 't10_superbudget'

    def setup(self, c, cfg):
        if c.soc == 'T10' and c.mode == 'VBR':
            rate = cfg['kbps'] * 1000
            c.wr(E, 88, int(rate), '<I')
            c.wr(E, 92, int(rate / 4), '<I')


# ------------------------------------------------- T20/T10 I-aware budget
class IAware(Variant):
    """T20/T10: the per-picture nominal budget of the P-picture QP (T20
    rcSt+128, T10 rcSt+116) is bit rate / fps (VBR/SMART: GOP target /
    GOP).  The vendor P target is a mix of it and the remaining GOP budget,
    and falls back to it alone once the GOP budget is spent - always after
    a large I picture in a static scene - so the I picture is never paid
    back.  I-aware: nominal = (nominal x GOP - bits of the last I) /
    (GOP - 1), floor nominal / 8 (one hook in calcPFrameQp)."""
    name = 'iaware'

    def __init__(self, floor=8):
        self.floor = floor

    def setup(self, c, cfg):
        if c.soc in ('T20', 'T10'):
            c.set_opt(1, 1)
            c.set_opt(2, self.floor)


# --------------------------------------------- T23 eprc: noise-cliff guard
class EprcCliff(Variant):
    """T21/T23 eprc, static scenes: the controller walks the P QP down until
    the sensor noise is coded (picture size x3..x50 for one QP step) and then
    jumps back (sawtooth, recordings and simulation).  Guard: when a P
    picture coded 1-3 QP below the previous one grew by more than `ratio`
    while it is still small against the per-picture budget is irrelevant -
    the QP it was coded at becomes a floor (+1) for the rest of the GOP;
    floors are dropped at the IDR and when motion is detected (picture size
    above the per-picture budget)."""
    name = 'eprc_cliff'

    def __init__(self, ratio=3.0, down1=False):
        self.ratio = ratio
        self.down1 = down1

    def setup(self, c, cfg):
        self.floor = 0
        self.per = cfg['kbps'] * 1000 / cfg['fps'] / 8   # bytes per picture

    def qp(self, c, ctx, qp, ptype):
        if c.soc not in ('T23', 'T21'):
            return qp
        rows = ctx['rows']
        if ptype == 1:
            self.floor = 0
            return qp
        if len(rows) >= 2 and not rows[-1]['idr'] and not rows[-2]['idr']:
            a, b = rows[-2], rows[-1]
            if b['qp'] < a['qp'] and b['bytes'] > self.ratio * a['bytes'] and \
                    b['bytes'] < 2 * self.per:
                self.floor = max(self.floor, b['qp'] + 1)
            if b['bytes'] > 2 * self.per and b['qp'] >= a['qp']:
                self.floor = 0           # real content change: let it go
        q = qp
        if self.down1 and rows and not rows[-1]['idr'] and q < rows[-1]['qp'] - 1:
            q = rows[-1]['qp'] - 1
        if q < self.floor:
            q = self.floor
        if q != qp:
            write_qp(c, q)
        return q


class IBudget(Variant):
    """T20/T10: I picture QP never below last I QP + 7 log2(last I bits /
    (share x GOP budget)) (I pictures halve every ~7 QP), i.e. the vendor's
    I QP (P-QP average minus 0..2) is only raised when the last I picture
    took more than `share` of its GOP budget."""
    name = 'ibudget'

    def __init__(self, share=0.5):
        self.share = share

    def setup(self, c, cfg):
        self.gop_bits = cfg['kbps'] * 1000 * cfg['gop'] / cfg['fps']
        if c.mode != 'CBR':
            self.gop_bits *= 0.8         # VBR/SMART target: changePos 80 %

    def qp(self, c, ctx, qp, ptype):
        import math
        rows = ctx['rows']
        if ptype != 1:
            return qp
        last = next((r for r in reversed(rows) if r['idr']), None)
        if not last:
            return qp
        q = int(math.ceil(last['qp'] + 7 * math.log2(last['bytes'] * 8 / (self.share * self.gop_bits))))
        q = min(ctx['max_qp'], q)
        if q > qp:
            write_qp(c, q)
            return q
        return qp


# ------------------------------------------- fast attack on bursts / cuts
class Attack(Variant):
    """All SoCs: when a P picture is > `k` x the per-picture budget and
    > 2.5 x the median of the 5 P pictures before it (motion burst, scene
    cut, IR switch), the next QP is at least last QP + min(6, 3 log2(size /
    1.5 budget)) instead of the vendor's +1..+3 per picture."""
    name = 'attack'

    def __init__(self, k=3.0, cap=6):
        self.k, self.cap = k, cap

    def setup(self, c, cfg):
        self.per = cfg['kbps'] * 1000 / cfg['fps'] / 8

    def qp(self, c, ctx, qp, ptype):
        import math
        rows = ctx['rows']
        if ptype == 1 or len(rows) < 7 or rows[-1]['idr']:
            return qp
        last = rows[-1]
        prev = [r['bytes'] for r in rows[-6:-1] if not r['idr']]
        if not prev or last['bytes'] < self.k * self.per or last['bytes'] < 2.5 * st.median(prev):
            return qp
        q = last['qp'] + min(self.cap, int(round(3 * math.log2(last['bytes'] / (1.5 * self.per)))))
        q = min(ctx['max_qp'], q)
        if q > qp:
            write_qp(c, q)
            return q
        return qp


# ------------------------------------------------- I picture QP smoothing
class ISmooth(Variant):
    """All SoCs: I QP within [mean P QP of the last GOP - lo, + hi]."""
    name = 'ismooth'

    def __init__(self, lo=3, hi=1):
        self.lo, self.hi = lo, hi

    def qp(self, c, ctx, qp, ptype):
        rows = ctx['rows']
        if ptype != 1 or not rows:
            return qp
        g = ctx['gop']
        ps = [r['qp'] for r in rows[-g:] if not r['idr']]
        if not ps:
            return qp
        m = st.mean(ps)
        q = int(round(min(max(qp, m - self.lo), m + self.hi)))
        q = max(ctx['min_qp'], min(ctx['max_qp'], q))
        if q != qp:
            write_qp(c, q)
        return q


class Combo(Variant):
    def __init__(self, name, *vs):
        self.name = name
        self.vs = vs

    def setup(self, c, cfg):
        for v in self.vs:
            v.setup(c, cfg)

    def qp(self, c, ctx, qp, ptype):
        for v in self.vs:
            qp = v.qp(c, ctx, qp, ptype)
        return qp

    def reenc(self, c, ctx, a, b, n):
        return all(v.reenc(c, ctx, a, b, n) for v in self.vs)


REGISTRY = {
    't10_superfix': T10SuperFix,
    't10_superbudget': T10SuperBudget,
    'iaware': IAware,
    'eprc_cliff': EprcCliff,
    'eprc_cliff_down1': lambda: EprcCliff(down1=True),
    'ismooth': ISmooth,
    'attack': Attack,
    'eprc_down1': lambda: EprcCliff(ratio=1e9, down1=True),
    'eprc_stable': lambda: Combo('eprc_stable', EprcCliff(down1=True), Attack()),
}


def get(name):
    return REGISTRY[name]()


# which variants apply where
PLAN = [
    ('t10_superfix', [('T10', 'VBR')]),
    ('t10_superbudget', [('T10', 'VBR')]),
    ('iaware', [('T20', 'CBR'), ('T20', 'VBR'), ('T20', 'SMART'), ('T10', 'CBR'), ('T10', 'VBR'), ('T10', 'SMART')]),
    ('eprc_cliff', [('T23', 'CBR'), ('T23', 'VBR')]),
    ('eprc_cliff_down1', [('T23', 'CBR'), ('T23', 'VBR')]),
    ('ismooth', [('T23', 'CBR'), ('T23', 'VBR'), ('T20', 'CBR'), ('T10', 'CBR'), ('T10', 'SMART')]),
]


def study(argv):
    only = set(argv)
    for vname, cfgs in PLAN:
        if only and vname not in only:
            continue
        print('\n### %s\n' % vname)
        print(header(['SoC', 'mode', 'scene', 'run']))
        for soc, mode in cfgs:
            for s in SCENES:
                for tag, v in (('vendor', None), (vname, get(vname))):
                    m = run(soc, mode, s, v)
                    print('| ' + ' | '.join([soc, mode, s, tag] + fmt(m)) + ' |')


# ---------------------------------------------------------------- robustness
OPS = [
    ('A 1080p15 1.5M g50', dict()),
    ('B 1080p25 2.5M g50', dict(fps=25, kbps=2500)),
    ('C 360p15 384k g50', dict(width=640, height=360, kbps=384)),
    ('D A, low texture', dict(ci_scale=0.3)),
    ('E A, GOP 15', dict(gop=15)),
]
SEEDS = (1, 2, 3)

PROPOSALS = [
    ('t10_superfix', [('T10', 'VBR')]),
    ('iaware', [('T20', 'CBR'), ('T20', 'VBR'), ('T20', 'SMART'), ('T10', 'CBR'), ('T10', 'VBR'),
                ('T10', 'SMART')]),
    ('t10_vbr_fix', [('T10', 'VBR')]),
    ('iaware_ibudget', [('T20', 'CBR'), ('T20', 'VBR'), ('T20', 'SMART'), ('T10', 'CBR')]),
    ('eprc_down1', [('T23', 'CBR'), ('T23', 'VBR')]),
    ('eprc_cliff_down1', [('T23', 'CBR'), ('T23', 'VBR')]),
    ('eprc_stable', [('T23', 'CBR'), ('T23', 'VBR')]),
    ('eprc_cliff', [('T23', 'CBR')]),
    ('attack', [('T23', 'VBR'), ('T20', 'CBR'), ('T10', 'CBR')]),
    ('ismooth', [('T23', 'CBR'), ('T10', 'SMART')]),
]
REGISTRY['ibudget'] = IBudget
REGISTRY['iaware_ibudget'] = lambda: Combo('iaware_ibudget', IAware(), IBudget())
REGISTRY['t10_vbr_fix'] = lambda: Combo('t10_vbr_fix', T10SuperFix(), IAware())


def verdict(mode, b, v):
    """'+' clear win, '-' regression, '=' neutral (see the study doc)."""
    reg = (v['vbv'] > 1.2 * b['vbv'] + 0.5 or
           (v['qp'] > b['qp'] + 0.5 and v['rate'] >= b['rate'] - 0.03) or
           (v['rate'] > b['rate'] + 0.05 and v['qp'] >= b['qp'] - 0.3) or
           v['dqp'] > b['dqp'] + 0.3 or
           (mode == 'CBR' and v['rate'] < 0.9 and v['qp'] > b['qp'] + 0.5 and b['rate'] <= 1.05) or
           (mode == 'CBR' and abs(v['rate'] - 1) > abs(b['rate'] - 1) + 0.05))
    win = (v['reenc'] < b['reenc'] - 0.1 or
           (v['rate'] < b['rate'] - 0.05 and v['qp'] <= b['qp'] + 0.3) or
           (v['qp'] < b['qp'] - 0.3 and v['rate'] <= b['rate'] + 0.03) or
           (b['rate'] > 1.05 and v['rate'] < b['rate'] - 0.1) or
           v['dqp'] < b['dqp'] - 0.3 or v['vbv'] < 0.8 * b['vbv'] - 0.5)
    return '-' if reg else '+' if win else '='


def robust(argv):
    keys = ('rate', 'sec_peak', 'vbv', 'qp', 'dqp', 'pulse', 'reenc', 'ovr')
    only = set(argv)
    for vname, cfgs in PROPOSALS:
        if only and vname not in only:
            continue
        print('\n### %s\n' % vname)
        print('| SoC | mode | op | ' + ' | '.join('%s v->n' % k for k in keys) + ' | + / = / - |')
        print('|---|---|---|' + '---|' * (len(keys) + 1))
        for soc, mode in cfgs:
            for oname, kw in OPS:
                acc = {k: [0.0, 0.0] for k in keys}
                cnt = {'+': 0, '=': 0, '-': 0}
                nn = 0
                for s in SCENES:
                    for seed in SEEDS:
                        b = run(soc, mode, s, None, seed=seed, **kw)
                        v = run(soc, mode, s, get(vname), seed=seed, **kw)
                        for k in keys:
                            acc[k][0] += b[k]
                            acc[k][1] += v[k]
                        cnt[verdict(mode, b, v)] += 1
                        nn += 1
                cells = []
                for k in keys:
                    a, c = acc[k][0] / nn, acc[k][1] / nn
                    cells.append(('%.2f->%.2f' if k not in ('qp', 'pulse', 'ovr') else '%.1f->%.1f') % (a, c))
                print('| %s | %s | %s | %s | %d / %d / %d |' % (soc, mode, oname, ' | '.join(cells),
                                                                cnt['+'], cnt['='], cnt['-']))
