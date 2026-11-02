#!/usr/bin/env python3
"""Issues #3/#5 (device evidence helper): did a camera run of the apitest case `demask` step the ISP sharpness as the
vendor strategy says?

    tools/demask_check.py RUN.log RUN.h264 RUN.sctrl [--frm-used] [--sim build/tests/demask-sim] [--qp N]

--qp N: a FIXQP run without a stream dump (QEMU): every picture has QP N (RUN.h264 is then ignored, give -).

RUN.log is the apitest output ("demask: SetChnDemask ... before pic N", "pic N bytes"), RUN.h264 the stream dump (the per-picture
QP comes from the slice headers, tools/hskip_trace.py --qp), RUN.sctrl the VIDIOC_S_CTRL log of tools/apitest/sctrl_trace.c.
The expected per-picture ISP calls come from src/demask (tests/demask/demask_sim.c, the C the library runs, checked against
the vendor code by tests/demask) fed with the QP sequence and the SetChnDemask events of the log; they are compared with the
sharpness controls (id 0x98091b) the library really issued, in order.  Prints the two sequences side by side and a verdict.
"""
import os
import re
import subprocess
import sys

SHARP = 0x98091b

def main():
    a = sys.argv[1:]
    frm = '--frm-used' in a
    sim = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'tests', 'demask-sim')
    if '--sim' in a:
        sim = a[a.index('--sim') + 1]
    const_qp = int(a[a.index('--qp') + 1]) if '--qp' in a else None
    skip = {sim, str(const_qp)}
    a = [x for x in a if not x.startswith('--') and x not in skip]
    log, h264, sctrl = a[:3]
    here = os.path.dirname(os.path.abspath(__file__))
    events = []           # (before picture, en, cnt, thr)
    npics = 0
    user = 0x80           # the application's own sharpness (IMP_ISP_Tuning_SetSharpness), what mode 0 sends
    t0 = 0                # CLOCK_MONOTONIC us of CreateChn: earlier controls (EnableTuning defaults) are not the channel's
    for line in open(log):
        m = re.match(r'demask: SetSharpness (\d+) -> 0', line)
        if m:
            user = int(m.group(1))
        m = re.match(r'demask: SetChnDemask en (\d+) cnt (-?\d+) thr (-?\d+) before pic (\d+) -> (-?\d+)', line)
        if m:
            events.append((int(m.group(4)), int(m.group(1)), int(m.group(2)), int(m.group(3))))
        m = re.match(r'demask: CreateChn 0 \(cnt (-?\d+) thr (-?\d+) rt (\d+) fixqp (\d+).*t=(\d+)', line)
        if m:
            t0 = int(m.group(5))
            fixqp = int(m.group(4))
            # the create attribute (no enable transition) and the measure channel_i264e_encoder_init starts with
            events.append((-1, 1 if m.group(3) == '0' else 0, int(m.group(1)), int(m.group(2)), fixqp if fixqp > 0 else 30))
        if re.match(r'pic \d+ \d+', line):
            npics += 1
    if const_qp is not None:
        qps = [const_qp] * npics
    else:
        qps = [int(x) for x in subprocess.run([sys.executable, os.path.join(here, 'hskip_trace.py'), h264, '--qp'],
                                              capture_output=True, text=True).stdout.split()]
    if len(qps) != npics:
        print('warning: %d pictures in the log, %d slice QPs in the stream' % (npics, len(qps)))
    # the create attribute is copied into a zeroed record: the simulator sees it as a set on a disabled record, which
    # would set the sharpness word to 0x80; model the copy by a set with enable but no transition = 'C' line
    feed = []
    for ev in [e for e in events if e[0] < 0]:
        feed.append('C %d %d %d %d %d' % (ev[1], 0, ev[2], ev[3], ev[4]))
    for i, qp in enumerate(qps):
        for ev in events:
            if ev[0] == i:
                feed.append('A %d %d %d %d' % (ev[1], 0, ev[2], ev[3]))
        feed.append('P %d' % qp)
    out = subprocess.run([sim] + (['1'] if frm else ['0']), input='\n'.join(feed) + '\n', capture_output=True, text=True).stdout.split('\n')
    expected = []         # (mode, value) per call, in order
    for line in out:
        if line.startswith('A '):
            if line.split()[1] == '1':
                expected.append(('restore', user))
        elif line.startswith('P '):
            _, mode, value = line.split()
            if mode == '1':
                expected.append((1, int(value)))
            elif mode == '0':
                expected.append((0, user))
    actual = []
    for line in open(sctrl):
        f = line.split()
        if len(f) >= 5 and f[1] == 'S' and int(f[2], 16) == SHARP and int(f[0]) >= t0:
            actual.append((int(f[3]), int(f[4])))
    print('pictures %d, SetChnDemask events %d, expected sharpness controls %d, issued %d' % (len(qps), len(events), len(expected), len(actual)))
    bad = 0
    for i in range(max(len(expected), len(actual))):
        e = expected[i] if i < len(expected) else None
        v = actual[i] if i < len(actual) else None
        ok = e is not None and v is not None and e[1] == v[0] and (v[1] >= 0)
        if not ok:
            bad += 1
        if i < 12 or not ok:
            print('%3d expected %-16s issued %-16s %s' % (i, e, v, '' if ok else 'DIFF'))
    print('qp sequence:', ' '.join(str(q) for q in qps[:60]), '...' if len(qps) > 60 else '')
    print('VERDICT %s (%d of %d controls differ)' % ('EQUAL' if bad == 0 else 'DIFFERENT', bad, max(len(expected), len(actual))))
    return 0 if bad == 0 else 1

if __name__ == '__main__':
    sys.exit(main())
