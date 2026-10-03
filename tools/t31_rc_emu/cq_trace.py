#!/usr/bin/env python3
"""Record vendor rate-controller traces (inputs + full state after every call)
for tests/t31/al_rc_trace_test.c.

    python3 cq_trace.py <outdir>

Line format (all numbers decimal unless noted):
  I mode rcp_hex gop_hex state_hex          init (AL_RateCtrl_Init + lI1i)
  S rcp_hex gop_hex state_hex                parameter update (lI1i)
  R state_hex                                reset (il0i)
  Q type flags qpoff fixed forced -> qp      picture QP query (Il1i)
  P type flags qpoff fixed forced size ovf extra s20 s24 s28 s32 s36 s40 s44 s48 qp sse_lo sse_hi -> filler state_hex
                                             o11i + update (OOoI/Ooii)
"""
import os, random, struct, sys
import cq_emu
from cq_emu import T31, RC, rc_param, gop_param, sse_for_psnr

MB = 1920 * 1080 // 256

class TraceRC(RC):
    def __init__(self, L, mode, rcp, gop, out):
        super().__init__(L, mode, rcp, gop)
        self.out_ptr = self.out
        self.out = out
        self.rcp_bytes, self.gop_bytes = rcp, gop
        out.write('I %d %s %s %s\n' % (mode, rcp.hex(), gop.hex(), self.state_hex()))

    def state_hex(self):
        return self.L.rd(self.st, 328).hex()

    def set_pic(self, ptype, flags, qpoff, fixed, forced):
        L = self.L
        L.wr(self.pic, bytes(0x80))
        L.w32(self.pic + 4, flags); L.w32(self.pic + 16, ptype)
        L.wr(self.pic + 37, struct.pack('<b', qpoff))
        L.wr(self.pic + 44, bytes([fixed])); L.wr(self.pic + 45, struct.pack('<b', forced))

    def query_qp(self, ptype, flags, qpoff=0, fixed=0, forced=0):
        L = self.L
        self.set_pic(ptype, flags, qpoff, fixed, forced)
        L.w16(self.out_ptr, 0)
        L.callv(L.r32(self.ctx + 16), self.ctx, self.pic, self.out_ptr)
        qp = struct.unpack('<h', L.rd(self.out_ptr, 2))[0]
        self.out.write('Q %d %d %d %d %d -> %d\n' % (ptype, flags, qpoff, fixed, forced, qp))
        return qp

    def picture2(self, ptype, flags, qpoff, fixed, forced, size, ovf, extra, stat):
        L = self.L
        self.set_pic(ptype, flags, qpoff, fixed, forced)
        L.wr(self.stat, bytes(0x80))
        keys = (20, 24, 28, 32, 36, 40, 44, 48)
        for k in keys:
            L.w32(self.stat + k, stat[k])
        L.w16(self.stat + 60, stat['qp'] & 0xffff)
        L.w32(self.stat + 96, stat['sse_lo']); L.w32(self.stat + 100, stat['sse_hi'])
        L.w32(self.out_ptr, 0)
        L.callv(L.r32(self.ctx + 8), self.ctx, self.pic, self.stat, size, self.out_ptr)
        filler = L.s32(self.out_ptr)
        L.callv(L.r32(self.ctx + 12), self.ctx, self.pic, self.stat, size, ovf, extra)
        self.out.write('P %d %d %d %d %d %d %d %d %s %d %d %d -> %d %s\n' % (
            ptype, flags, qpoff, fixed, forced, size, ovf, extra,
            ' '.join(str(stat[k]) for k in keys), stat['qp'], stat['sse_lo'], stat['sse_hi'],
            filler, self.state_hex()))
        return filler

    def set_params(self, rcp, gop):
        L = self.L
        L.wr(self.rcp, rcp); L.wr(self.gop, gop)
        L.callv(L.r32(self.ctx + 4), self.ctx, self.rcp, self.gop)
        self.out.write('S %s %s %s\n' % (rcp.hex(), gop.hex(), self.state_hex()))

    def reset(self):
        L = self.L
        L.callv(L.r32(self.ctx + 0), self.ctx)
        self.out.write('R %s\n' % self.state_hex())

def scenario(name, mode, seed, frames, out, al_mode=None):
    """mode = AL_RateCtrl_Init mode (0 CBR, 1 VBR, 8 CappedVBR, 9 CappedQuality); al_mode = the
    AL eRCMode in the rc param (2 VBR, 4 CappedVBR, 8 CappedQuality on the T31)."""
    if al_mode is None:
        al_mode = {0: 1, 1: 2, 8: 4, 9: 8}[mode]
    rnd = random.Random(seed)
    L = T31()
    gop_len = rnd.choice([10, 25, 30, 50, 1, 2])
    fps = rnd.choice([(25, 1), (30, 1), (15, 1), (30000, 1001), (20, 1)])
    target = rnd.choice([512_000, 1_000_000, 2_000_000, 4_000_000])
    maxbr = target if al_mode == 1 else int(target * rnd.choice([1.0, 4 / 3, 2.0]))
    qmin, qmax = rnd.choice([(15, 48), (10, 51), (20, 40), (0, 51)])
    ipd = rnd.choice([-1, -1, 1, 3])
    pbd = rnd.choice([-1, 2])
    opts = rnd.choice([1, 1, 17, 0, 3])
    psnr_cap = rnd.choice([3000, 4200, 5000])
    fr, clk = fps[0], (fps[1] * 1000) & 0xffff   # the IMP truncates to u16
    g = fps[0] // fps[1] if fps[1] == 1 else 30
    rcp = rc_param(al_mode, fps=fr, clk=clk, target=target, maxbr=maxbr, qp0=rnd.choice([25, 30, 34]),
                   qmin=qmin, qmax=qmax, ipd=ipd, pbd=pbd, opts=opts, psnr=psnr_cap,
                   cpb_ticks=270000, rem_ticks=216000)
    gop = gop_param(mode=2, length=gop_len, numb=0)
    rc = TraceRC(L, mode, rcp, gop, out)
    scene = 1.0
    sticky_extra = 0
    for n in range(frames):
        if n % gop_len == 0:
            ptype, flags = 2, 3
        else:
            ptype, flags = 1, 2
        if rnd.random() < 0.02:
            flags |= 4           # scene change reset
        if rnd.random() < 0.01:
            flags |= 0x80
        qpoff = rnd.choice([0, 0, 0, 0, -2, 1])
        fixed = 1 if rnd.random() < 0.02 else 0
        forced = rnd.randint(0, 51)
        qp = rc.query_qp(ptype, flags, qpoff, fixed, forced)
        if rnd.random() < 0.03:
            scene = rnd.choice([0.3, 1.0, 2.5, 6.0])
        base = (320_000 if ptype == 2 else 40_000) * scene * (target / 2_000_000)
        size = int(base * 2 ** ((30 - qp) / 6.0) * rnd.uniform(0.85, 1.15)) & ~7
        size = max(size, 64)
        intra = rnd.randint(0, 100) if ptype == 1 else rnd.randint(80, 100)
        skip = rnd.randint(0, 100)
        stat = {36: MB, 40: rnd.randint(0, 50), 44: rnd.randint(0, 20), 48: rnd.randint(0, 5),
                28: MB * intra // 100, 32: MB * skip // 100,
                20: size * rnd.randint(0, 100) // 100, 24: rnd.randint(0, MB),
                'qp': qp, }
        psnr_db = 52 - qp * 0.45 + rnd.uniform(-2, 2)
        sse = sse_for_psnr(psnr_db, 1920 * 1080)
        if rnd.random() < 0.01:
            sse = 0
        stat['sse_lo'], stat['sse_hi'] = sse & 0xffffffff, sse >> 32
        ovf = 1 if rnd.random() < 0.02 else 0
        filler = rc.picture2(ptype, flags, qpoff, fixed, forced, size, ovf, sticky_extra, stat)
        if filler > 0:
            sticky_extra = max(filler, 8) * 8
        if rnd.random() < 0.01:
            # parameter change: bitrate and/or frame rate and/or QP bounds
            target = int(target * rnd.choice([0.5, 1.5, 1.0]))
            maxbr = target if al_mode == 1 else max(target, int(maxbr * rnd.choice([1.0, 1.2])))
            fr2 = rnd.choice([fr, 25, 15])
            qmin2 = rnd.choice([qmin, qmin + 5])
            qmax2 = rnd.choice([qmax, 45])
            rcp = rc_param(al_mode, fps=fr2, clk=clk, target=target, maxbr=maxbr, qp0=30,
                           qmin=qmin2, qmax=max(qmax2, qmin2 + 1), ipd=rnd.choice([-1, 2]),
                           pbd=pbd, opts=opts, psnr=psnr_cap, cpb_ticks=270000, rem_ticks=216000)
            rc.set_params(rcp, gop)
        if rnd.random() < 0.005:
            rc.reset()
    out.write('E\n')

if __name__ == '__main__':
    outdir = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(outdir, exist_ok=True)
    # T31 configurations: IMP CBR -> AL 1 / RateCtrl 0, VBR -> AL 2 / RateCtrl 1, CappedVBR -> 4 / 8, CappedQuality -> 8 / 9
    runs = [('vbr', 1, 2), ('cappedvbr', 8, 4), ('cappedquality', 9, 8), ('cbr', 0, 1)]
    for name, mode, al_mode in runs:
        for seed in range(1, 5):
            path = os.path.join(outdir, '%s_%d.trc' % (name, seed))
            with open(path, 'w') as f:
                scenario(name, mode, seed * 7 + mode, 150, f, al_mode)
            print(path)
    # synthetic: rc param mode 1 (CBR HRD, filler) and 9 (removal clock slip) with the VBR update
    for name, mode, al_mode in (('synthetic_cbrhrd', 1, 1), ('synthetic_slip', 9, 9)):
        path = os.path.join(outdir, '%s_1.trc' % name)
        with open(path, 'w') as f:
            scenario(name, mode, 99 + mode, 150, f, al_mode)
        print(path)
