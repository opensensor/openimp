#!/usr/bin/env python3
"""Closed-loop host simulation of the vendor rate controllers (T23/T21 eprc,
T20, T10) against synthetic scenes, plus opt-in "beyond vendor" variants.

    make -C tools/rc_sim            # builds build/librcsim.so from the ports
    tools/rc_sim/rc_sim.py baseline # metrics of the vendor controllers
    tools/rc_sim/rc_sim.py study    # baseline + every variant (markdown)
    tools/rc_sim/rc_sim.py trace T23 SMART day_static [variant]   # per frame

Encoder model (fitted to 1080p recordings of the OpenIMP T21/T23 stack, see
docs/RC_BEYOND_VENDOR_STUDY.md):
  I bytes = hdr + Mpx * ci * 2^((30-QP)/7)
  P bytes = hdr + Mpx * (cm * 2^((30-QP)/6)
                         + cn / (1 + 2^((QP-qn)/sn)) * 1.7^clip(QPref-QP, -4, 4))
  P bytes <= 0.9 * I bytes (the encoder falls back to intra)
ci/cm/cn in bytes per Mpixel; cn/qn/sn: sensor noise and the skip "cliff";
the 1.7^dq factor: a P picture coded finer than its reference re-codes the
reference's noise (sawtooth seen in the recordings).  Per picture a
lognormal jitter (sigma 0.12) that is the same for re-encodes.
"""
import math
import random
import statistics as st
import sys

import rcsim

# --------------------------------------------------------------------------
# scenes: per-frame complexity
# --------------------------------------------------------------------------
# keys: ci, cm, cn, qn, sn, mv (mean |mv| per moving MB), mov (moving
# MB share), intra (intra MB share), act (SAD per pixel, P), cut (1 on a
# scene change picture)
DAY = dict(ci=250e3, cm=0.0, cn=14e3, qn=28.0, sn=1.0, mv=3.0, mov=0.02, intra=0.005, act=1.0)
NIGHT = dict(ci=150e3, cm=0.0, cn=30e3, qn=33.0, sn=1.5, mv=4.0, mov=0.05, intra=0.01, act=3.0)
MOTION = dict(cm=18e3, mv=25.0, mov=0.35, intra=0.04, act=6.0)
BURST = dict(cm=45e3, mv=40.0, mov=0.55, intra=0.08, act=10.0)


def _mix(base, over, w=1.0):
    d = dict(base)
    for k, v in over.items():
        d[k] = base.get(k, 0) * (1 - w) + v * w
    return d


def scene(name, n, fps, seed=1):
    rnd = random.Random(seed * 7919 + sum(map(ord, name)))
    out = []
    for i in range(n):
        t = i / fps
        if name == 'day_static':
            f = dict(DAY)
        elif name == 'night_static':
            f = dict(NIGHT)
        elif name == 'day_motion':
            # continuous traffic, slowly varying
            w = 0.6 + 0.4 * math.sin(t / 7.0)
            f = _mix(DAY, MOTION, w)
        elif name == 'bursts':
            # a person passes for 3 s every 15 s
            ph = t % 15.0
            w = 0.0 if ph < 10 else (1.0 if ph < 13 else 0.0)
            f = _mix(DAY, BURST, w)
        elif name == 'scene_cut':
            # static -> different, busier scene at 20 s -> back at 40 s
            if 20 <= t < 40:
                f = _mix(dict(DAY, ci=400e3, cn=20e3, qn=30.0), MOTION, 0.5)
            else:
                f = dict(DAY)
            f['cut'] = 1 if i in (int(20 * fps), int(40 * fps)) else 0
        elif name == 'ir_switch':
            # day -> IR night at 30 s (one cut picture, then noisy mono)
            f = dict(DAY) if t < 30 else dict(NIGHT)
            f['cut'] = 1 if i == int(30 * fps) else 0
        else:
            raise KeyError(name)
        f.setdefault('cut', 0)
        f['jit'] = math.exp(rnd.gauss(0, 0.12))
        out.append(f)
    return out


SCENES = ['day_static', 'night_static', 'day_motion', 'bursts', 'scene_cut', 'ir_switch']
EVENTS = {'bursts': [10, 13, 25, 28, 40, 43, 55, 58], 'scene_cut': [20, 40], 'ir_switch': [30]}


# --------------------------------------------------------------------------
# encoder model
# --------------------------------------------------------------------------
HDR = 60


def frame_bytes(f, idr, qp, qp_ref, mpx):
    ib = HDR + mpx * f['ci'] * 2 ** ((30 - qp) / 7.0)
    if idr:
        return ib * f['jit']
    if f.get('cut'):
        return 0.9 * ib * f['jit']
    dq = max(-4, min(4, qp_ref - qp))
    noise = f['cn'] / (1 + 2 ** ((qp - f['qn']) / f['sn'])) * 1.7 ** dq
    pb = HDR + mpx * (f['cm'] * 2 ** ((30 - qp) / 6.0) + noise)
    return min(pb, 0.9 * ib) * f['jit']


def frame_stats(f, idr, mbs, pixels):
    if idr or f.get('cut'):
        act = f['ci'] / 25e3 * (3 if f.get('cut') else 1)
        mv = 0 if idr else 60.0
        mov, intra = (0.0, 1.0) if idr else (0.9, 0.85)
    else:
        act, mv, mov, intra = f['act'], f['mv'], f['mov'], f['intra']
    cmpx = int(act * pixels) & 0xffffffff
    return cmpx, int(mv * mov * mbs), int(mov * mbs), int(intra * mbs)


# --------------------------------------------------------------------------
# run one controller (+ optional variant) over a scene
# --------------------------------------------------------------------------
class Variant:
    """Base: the vendor controller unchanged."""
    name = 'vendor'
    may_repeat = 1

    def setup(self, c, cfg):
        pass

    def qp(self, c, ctx, qp, ptype):
        return qp

    def reenc(self, c, ctx, qp_old, qp_new, nbytes):
        return True


def run(soc, mode, scn, variant=None, fps=15, gop=50, kbps=1500, width=1920, height=1080,
        seconds=60, seed=1, min_qp=15, max_qp=45, frm_step=3, gop_step=15, trace=False,
        ci_scale=1.0, cm_scale=1.0):
    v = variant or Variant()
    c = rcsim.Controller(soc, mode, width, height, gop, fps, kbps, min_qp, max_qp, frm_step,
                         gop_step)
    cfg = dict(soc=soc, mode=mode, fps=fps, gop=gop, kbps=kbps, min_qp=min_qp, max_qp=max_qp,
               mbs=((width + 15) // 16) * ((height + 15) // 16), pixels=width * height)
    v.setup(c, cfg)
    frames = scene(scn, int(seconds * fps), fps, seed)
    for f in frames:
        f['ci'] *= ci_scale
        f['cm'] *= cm_scale
    mpx = width * height / 1e6
    mbs = cfg['mbs']
    rows = []
    qp_ref = 35
    ctx = dict(cfg, rows=rows)
    for i, f in enumerate(frames):
        k = i % gop
        idr = k == 0
        qp, ptype = c.start(idr, k)
        qp = v.qp(c, ctx, qp, ptype)
        ctx['frame'] = f
        nb = frame_bytes(f, idr, qp, qp_ref, mpx)
        cm = frame_stats(f, idr, mbs, cfg['pixels'])
        enc, q0 = 1, qp
        while True:
            q2 = c.end(int(nb * 8), *cm, may_repeat=v.may_repeat)
            if q2 is None:
                break
            if not v.reenc(c, ctx, qp, q2, nb) or enc >= 5:
                # caller cannot / does not want to code again: the
                # controller already took the request; finish as coded
                break
            qp = q2
            nb = frame_bytes(f, idr, qp, qp_ref, mpx)
            enc += 1
        c.note_bits(idr, nb * 8)
        rows.append(dict(i=i, idr=idr, qp=qp, q0=q0, bytes=nb, enc=enc, cut=f.get('cut', 0)))
        qp_ref = qp
    if trace:
        return rows
    return metrics(rows, cfg, scn)


# --------------------------------------------------------------------------
# metrics
# --------------------------------------------------------------------------
def metrics(rows, cfg, scn):
    fps, kbps, gop = cfg['fps'], cfg['kbps'], cfg['gop']
    n = len(rows)
    bits = [r['bytes'] * 8 for r in rows]
    total_kbps = sum(bits) / (n / fps) / 1000
    # per second (sliding 1 s window) and per GOP
    w = fps
    sec = [sum(bits[i:i + w]) / 1000 for i in range(0, n - w + 1)]
    gops = [sum(bits[i:i + gop]) / (gop / fps) / 1000 for i in range(0, n - gop + 1, gop)]
    # leaky bucket, drained at the target rate, size 1 s of target
    fill, peak = 0.0, 0.0
    for b in bits:
        fill = max(0.0, fill + b - kbps * 1000 / fps)
        peak = max(peak, fill)
    P = [r for r in rows if not r['idr']]
    I = [r for r in rows if r['idr']]
    pq = [r['qp'] for r in P]
    dq = [abs(P[j]['qp'] - P[j - 1]['qp']) for j in range(1, len(P)) if P[j]['i'] == P[j - 1]['i'] + 1]
    # I-frame pulsing: |QP(I) - QP(last P before it)|
    pulse = [abs(rows[r['i']]['qp'] - rows[r['i'] - 1]['qp']) for r in I if r['i'] > 0]
    allq = [r['qp'] for r in rows]
    # seconds (non-overlapping, without an IDR) above 1.5 x target
    ovr = 0
    for i in range(0, n - w + 1, w):
        blk = rows[i:i + w]
        if not any(r['idr'] for r in blk) and sum(r['bytes'] * 8 for r in blk) > 1.5 * kbps * 1000:
            ovr += 1
    reenc = sum(r['enc'] - 1 for r in rows)
    m = dict(
        kbps=total_kbps, rate=total_kbps / kbps,
        sec_err=st.mean(abs(s / kbps - 1) for s in sec),
        sec_peak=max(sec) / kbps,
        gop_err=st.mean(abs(g / kbps - 1) for g in gops) if gops else 0,
        vbv=peak / (kbps * 1000),
        qp=st.mean(allq), pqp=st.mean(pq), iqp=st.mean(r['qp'] for r in I),
        qp_sd=st.pstdev(pq), dqp=st.mean(dq) if dq else 0,
        jumps=sum(1 for d in dq if d > 3),
        pulse=st.mean(pulse) if pulse else 0,
        atmax=sum(1 for q in allq if q >= cfg['max_qp']) / n,
        reenc=reenc / n, ovr=ovr,
        recov=recovery(rows, cfg, scn),
    )
    # rate-normalised quality: QP that the same stream would need at the
    # target rate (6 QP per factor 2 of bits); lower is better
    m['qp_eq'] = m['qp'] + 6 * math.log2(max(m['rate'], 1e-3))
    return m


def recovery(rows, cfg, scn):
    """Mean seconds after each event until the 1 s bit rate stays within
    +-25 % of its level 4..8 s after the event (or the QP within +-2 of
    its level then)."""
    ev = EVENTS.get(scn)
    if not ev:
        return None
    fps = cfg['fps']
    out = []
    qps = [r['qp'] for r in rows]
    for t in ev:
        s = int(t * fps)
        ref = qps[s + 4 * fps:s + 8 * fps]
        if len(ref) < fps:
            continue
        lvl = st.median(ref)
        rec = None
        for j in range(s, min(len(qps), s + 4 * fps)):
            if all(abs(q - lvl) <= 2 for q in qps[j:j + fps] if True):
                rec = (j - s) / fps
                break
        out.append(rec if rec is not None else 4.0)
    return st.mean(out) if out else None


COLS = [('kbps', '%.0f'), ('rate', '%.2f'), ('sec_err', '%.2f'), ('sec_peak', '%.2f'),
        ('gop_err', '%.2f'), ('vbv', '%.2f'), ('qp', '%.1f'), ('iqp', '%.1f'), ('pqp', '%.1f'),
        ('qp_sd', '%.1f'), ('dqp', '%.2f'), ('jumps', '%d'), ('pulse', '%.1f'), ('atmax', '%.2f'),
        ('reenc', '%.2f'), ('ovr', '%d'), ('recov', '%s'), ('qp_eq', '%.1f')]


def fmt(m):
    out = []
    for k, f in COLS:
        v = m[k]
        out.append('-' if v is None else (('%.1f' % v) if k == 'recov' else f % v))
    return out


def header(first):
    return '| ' + ' | '.join(first + [k for k, _ in COLS]) + ' |\n|' + '---|' * (len(first) + len(COLS))


CONFIGS = [('T23', 'CBR'), ('T23', 'VBR'), ('T23', 'SMART'), ('T20', 'CBR'), ('T20', 'VBR'),
           ('T20', 'SMART'), ('T10', 'CBR'), ('T10', 'VBR'), ('T10', 'SMART')]


def baseline(configs=CONFIGS, scenes=SCENES, **kw):
    print(header(['SoC', 'mode', 'scene']))
    for soc, mode in configs:
        for s in scenes:
            m = run(soc, mode, s, **kw)
            print('| ' + ' | '.join([soc, mode, s] + fmt(m)) + ' |')


def main(argv):
    if not argv or argv[0] == 'baseline':
        baseline()
    elif argv[0] == 'trace':
        soc, mode, scn = argv[1:4]
        v = None
        if len(argv) > 4:
            import variants
            v = variants.get(argv[4])
        for r in run(soc, mode, scn, v, trace=True):
            print('%4d %s qp %2d%s %8d %s' % (r['i'], 'I' if r['idr'] else 'P', r['qp'],
                  '' if r['q0'] == r['qp'] else '<%d' % r['q0'], r['bytes'],
                  'CUT' if r['cut'] else ''))
    elif argv[0] == 'study':
        import variants
        variants.study(argv[1:])
    elif argv[0] == 'robust':
        import variants
        variants.robust(argv[1:])


if __name__ == '__main__':
    main(sys.argv[1:])
