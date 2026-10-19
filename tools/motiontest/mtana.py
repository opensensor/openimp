#!/usr/bin/env python3
"""mtana.py LOG : evaluate a motiontest run on the OPENIMP_TEST_PATTERN=3 picture.
LOG = stderr of motiontest on the camera ([TP] ev ... lines from the hook, [M] <ms> <mask> results)."""
import re, sys
GX = 8
ev, res = {}, []
for ln in open(sys.argv[1], errors='replace'):
    m = re.match(r'\[TP\] ev (\d+) (start|stop) (\d+) cell=(\d+),(\d+) size=(\d+) contrast=(\d+) speed=([\d.]+)', ln)
    if m:
        i = int(m[1]); e = ev.setdefault(i, dict(cx=int(m[4]), cy=int(m[5]), size=int(m[6]), con=int(m[7]), speed=float(m[8])))
        e[m[2]] = int(m[3]); continue
    m = re.match(r'\[M\] (\d+) ([0-9a-f]+)', ln)
    if m: res.append((int(m[1]), int(m[2], 16)))
if not res: sys.exit('no [M] results')
def cells(mask): return [c for c in range(48) if mask >> c & 1]
def near(c, e):
    return abs(c % GX - e['cx']) <= 1 and abs(c // GX - e['cy']) <= 1
rate = (len(res) - 1) * 1000.0 / max(1, res[-1][0] - res[0][0])
print('results %d, %.1f/s, first %d ms' % (len(res), rate, res[0][0]))
rows, stat = [], {}
for i in sorted(ev):
    e = ev[i]
    if 'start' not in e or 'stop' not in e: continue
    hit = lat = None; exact = False
    for t, mk in res:
        if e['start'] <= t <= e['stop'] + 1000:
            cs = cells(mk)
            if any(c == e['cy'] * GX + e['cx'] for c in cs):
                hit = t; exact = True; break
            if hit is None and any(near(c, e) for c in cs): hit = t
    lat = (hit - e['start']) if hit else None
    rows.append((i, e, hit is not None, exact, lat))
    k = (e['size'], e['con'], e['speed'] > 0); s = stat.setdefault(k, [0, 0, 0]); s[1] += 1; s[0] += hit is not None; s[2] += exact
print('ev  cell  size con speed  dur  hit exact latency_ms')
for i, e, h, x, l in rows:
    print('%2d  %d,%d  %4d %3d %5g %5.1f  %s  %s  %s' % (i, e['cx'], e['cy'], e['size'], e['con'], e['speed'], (e['stop'] - e['start']) / 1000.0, 'Y' if h else '-', 'Y' if x else '-', l if l is not None else ''))
lats = sorted(l for _, _, _, _, l in rows if l is not None)
if lats: print('latency ms: min %d median %d max %d' % (lats[0], lats[len(lats) // 2], lats[-1]))
# false events: results with a set cell while no event is active (+-1.5 s) near that cell
fa = 0; fa_bits = 0; static = 0
for t, mk in res:
    act = [e for e in ev.values() if 'start' in e and e['start'] - 500 <= t <= e.get('stop', 1 << 60) + 1500]
    if not act: static += 1
    bad = [c for c in cells(mk) if not any(near(c, e) for e in act)]
    if bad: fa += 1; fa_bits += len(bad)
print('false events: %d of %d results (%d cell hits; %d results had no event active)' % (fa, len(res), fa_bits, static))
print('sensitivity (size px@640, contrast dY): hits/events (exact cell)')
for k in sorted(stat): print('  size %3d contrast %3d%s : %d/%d (%d)' % (k[0], k[1], ' moving' if k[2] else '', stat[k][0], stat[k][1], stat[k][2]))
