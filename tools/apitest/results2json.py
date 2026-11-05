#!/usr/bin/env python3
"""Turn the logs of one device run into per-function results for docs/api_evidence.json.

Inputs (any combination):
  --report FILE    apitest --json report (one JSON per run: function -> PASS|FAIL|RT|NA|SKIP + detail)
  --apitest FILE   tools/apitest output:   [A] IMP_x_y ret=N PASS|FAIL|N/A|SKIP <detail>
  --devtest FILE   tests/t40/devtest:      RES <Area_Func> PASS|PART|FAIL <detail>   (T40)
  --imgfx FILE     tools/imgfx (IMGFX_STATS=1): [R] <case> set=N get=<readback> | ST Y=.. U=.. V=.. H=.. G=..
Output (--out): {"soc","ip","date","image","funcs":{key:{"v":"PASS|FAIL|PART|SKIP|NA","detail":..,"src":[..]}},"unmapped":[..]}
Keys are the function keys of docs/api_evidence.json (IMP_ prefix removed, Get/Set pairs joined).  A key with
several member functions (Get/Set) is PASS only when every member reported PASS; a FAIL of any member is FAIL;
PASS + (N/A|SKIP) members is PART.  imgfx cases are mapped through IMGFX_CASES below.
Usage: results2json.py --soc T41 --ip <cam-ip> --image agg47 [--date D] --out result.json [inputs]
"""
import argparse, json, math, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
EVID = os.path.join(HERE, "..", "..", "docs", "api_evidence.json")

# ----------------------------------------------------------------------------------------------------------------
# imgfx cases.  Every case line is  [R] <case> set=<ret> get=<readback text> | ST Y=.. U=.. V=.. H=.. G=<8x8 luma grid>
# (ST = picture statistics after the change; the "base" case is the same picture before any change).  A case is
# judged by (1) the set returning 0, (2) the readback text agreeing with what was set, (3) the picture moving the way
# the function is documented to move it, relative to base or to the opposite case of the pair.  Conservative: whatever
# is not clearly right is PART (never merged into the evidence file); FAIL is reserved for nothing here (a failing
# set/dead picture is PART with the reason, the apitest decides FAIL).  A rule returning None ignores the case.
# ----------------------------------------------------------------------------------------------------------------
def _f(rx, s, cast=float):
    m = re.search(rx, s)
    return cast(m.group(1)) if m else None


def _arrow(info):
    m = re.match(r"\s*(-?\d+)->(-?\d+)", info)
    return (int(m.group(1)), int(m.group(2))) if m else None


def _setkey(k):
    return k.replace("Get/Set", "Set")


def _gh(G):       # mirror the 8x8 grid left-right
    return [G[r * 8 + 7 - c] for r in range(8) for c in range(8)]


def _gv(G):       # top-bottom
    return [G[(7 - r) * 8 + c] for r in range(8) for c in range(8)]


def _mad(a, c):
    return sum(abs(x - y) for x, y in zip(a, c)) / float(len(a))


def _chroma(st, n):
    return math.hypot(st["U"] - n, st["V"] - n)


class Ctx:
    def __init__(self, name, ret, info, st, rows, base):
        self.name, self.ret, self.info, self.st, self.rows, self.b = name, ret, info, st, rows, base

    def row(self, name):
        r = self.rows.get(name)
        return r if r and r[0] == 0 and r[2] else None

    def stat(self, name):
        r = self.row(name)
        return r[2] if r else None

    def readback_info(self, name):
        r = self.rows.get(name)
        return r[1] if r else ""


def _pair(c, other):
    o = c.stat(other)
    return o


def _dead(c):
    return c.st["Y"] < 1.0 and c.b["Y"] >= 10


# --- readback helpers (None = ok, string = reason) ----------------------------------------------------------------
def rb_arrow(c, want):
    a = _arrow(c.info)
    if not a:
        return "no readback"
    return None if a[1] == want else "readback %d != set %d" % (a[1], want)


def changed_vs(c, ref, dy=5.0, dh=0.15, duv=0.0):
    d = abs(c.st["Y"] - ref["Y"]) > dy or abs(c.st["H"] - ref["H"]) > dh * max(ref["H"], 0.01) or \
        (duv and abs(c.st["U"] - ref["U"]) + abs(c.st["V"] - ref["V"]) > duv)
    return d


def RTV(note=""):
    return ("RT", note)


def visible(c, ref):
    """does the picture differ from ref by more than the scene drift?"""
    return (abs(c.st["Y"] - ref["Y"]) > 4 or abs(c.st["U"] - ref["U"]) + abs(c.st["V"] - ref["V"]) > 3 or
            abs(c.st["H"] - ref["H"]) > 0.15 * max(ref["H"], 0.01) or _mad(c.st["G"], ref["G"]) > 5)


def P(note=""):
    return ("PASS", note)


def Q(note):
    return ("PART", note)


# --- rules --------------------------------------------------------------------------------------------------------
def rule_bright(c, want, lo):
    bad = rb_arrow(c, want)
    if bad:
        return Q(bad)
    y, by = c.st["Y"], c.b["Y"]
    if lo:
        return P("Y %.1f vs base %.1f" % (y, by)) if y < by - 10 else Q("Y %.1f not darker than base %.1f" % (y, by))
    return P("Y %.1f vs base %.1f" % (y, by)) if y > by + 10 else Q("Y %.1f not brighter than base %.1f" % (y, by))


def rule_contrast(c, want, lo):
    bad = rb_arrow(c, want)
    if bad:
        return Q(bad)
    h, bh = c.st["H"], c.b["H"]
    o = c.stat("contrast-high" if lo else "contrast-low")
    if lo:
        ok = h < bh * 0.85 and (o is None or o["H"] > 1.5 * h)
    else:
        ok = h > bh * 1.15 and (o is None or h > 1.5 * o["H"])
    return P("H %.2f vs base %.2f" % (h, bh)) if ok else Q("H %.2f vs base %.2f: contrast not moving the detail energy" % (h, bh))


def rule_sharp(c, want, lo):
    bad = rb_arrow(c, want)
    if bad:
        return Q(bad)
    h, bh = c.st["H"], c.b["H"]
    ok = h < bh * 0.85 if lo else h > bh * 1.15
    return P("H %.2f vs base %.2f" % (h, bh)) if ok else Q("H %.2f vs base %.2f: no sharpness effect" % (h, bh))


def rule_sat(c, want, lo):
    bad = rb_arrow(c, want)
    if bad:
        return Q(bad)
    if lo:
        dd, db = abs(c.st["U"] - c.st["V"]), abs(c.b["U"] - c.b["V"])
        ok = dd <= 0.5 and db - dd >= 0.25
        return P("U %.2f V %.2f neutral" % (c.st["U"], c.st["V"])) if ok else Q("U %.2f V %.2f not neutral (base %.2f/%.2f)" % (c.st["U"], c.st["V"], c.b["U"], c.b["V"]))
    z = c.stat("saturation-0")
    if z is None:
        return Q("no saturation-0 reference")
    n = (z["U"] + z["V"]) / 2.0
    db, dh = _chroma(c.b, n), _chroma(c.st, n)
    if db < 3.0:
        if dh >= 1.15 * db + 0.3:
            return P("chroma %.1f vs base %.1f" % (dh, db))
        return RTV("readback ok; scene too grey to show the increase (chroma %.1f vs base %.1f)" % (dh, db))
    return P("chroma %.1f vs base %.1f" % (dh, db)) if dh >= 1.15 * db else Q("chroma %.1f vs base %.1f: no increase" % (dh, db))


def rule_hue(c, want, lo):
    bad = rb_arrow(c, want)
    if bad:
        return Q(bad)
    o = c.stat("hue-high" if lo else "hue-low")
    if o is None:
        return Q("no opposite hue case")
    s = abs(c.st["U"] - o["U"]) + abs(c.st["V"] - o["V"])
    return P("U/V shift low-high %.1f" % s) if s >= 1.5 else Q("U/V shift low-high only %.1f" % s)


def rule_gamma(c, which):
    g = c.info
    if "type " in g:               # T41 GammaAttr: g[64]
        k, want = 64, (3326 if which == "steep" else 2047)
        keys = ["ISP_Tuning_Get/SetGammaAttr"]
    else:
        k, want = 32, (2702 if which == "steep" else 1023)
        keys = ["ISP_Tuning_Get/SetGamma"]
    cur = _f(r"g\[%d\]=(\d+) " % k, g + " ", int)
    zero = all(int(x) == 0 for x in re.findall(r"g\[\d+\]=(\d+)", g)[:3])
    o = c.stat("gamma-linear" if which == "steep" else "gamma-steep")
    by = c.b["Y"]
    y = c.st["Y"]
    pic = (y > by + 5) if which == "steep" else (y < by - 5)
    if o is not None:
        pic = pic and ((y - o["Y"] > 15) if which == "steep" else (o["Y"] - y > 15))
    if not pic:
        return Q("Y %.1f vs base %.1f: gamma curve has no picture effect" % (y, by))
    if zero:
        return ("PASS", "Y %.1f vs base %.1f; Get returns zeros" % (y, by), [_setkey(keys[0])])
    if cur != want:
        return Q("readback %s != set %d" % (cur, want))
    return ("PASS", "Y %.1f vs base %.1f, curve read back" % (y, by), keys)


def rule_aecomp(c, want, lo):
    if "comp en" in c.info:        # T41 AeScenceAttr
        cur = _f(r"comp en\d+=(\d+)", c.info, int)
        keys = ["ISP_Tuning_Get/SetAeScenceAttr"]
        bad = None if cur == want else "readback %s != set %d" % (cur, want)
    else:
        keys = ["ISP_Tuning_Get/SetAeComp"]
        bad = rb_arrow(c, want)
    if bad:
        return Q(bad)
    o = c.stat("aecomp-high" if lo else "aecomp-low")
    if o is None:
        return Q("no opposite case")
    d = (o["Y"] - c.st["Y"]) if lo else (c.st["Y"] - o["Y"])
    return ("PASS", "Y low->high %+.0f" % d, keys) if d > 15 else Q("Y low/high differ by only %.1f: no picture effect" % d)


def rule_exp(c, lo):
    it = _f(r"\bit=(\d+)", c.info, int)
    oi = _f(r"\bit=(\d+)", c.readback_info("exp-long" if lo else "exp-short"), int)
    o = c.stat("exp-long" if lo else "exp-short")
    keys = ["ISP_Tuning_SetExpr", "ISP_Tuning_GetExpr"]
    if "again=" in c.info:
        keys = ["ISP_Tuning_Get/SetAeExprInfo"]
    if it is None or oi is None or o is None:
        return Q("no readback / opposite case")
    if not ((it < oi) if lo else (it > oi)):
        return Q("integration time readback %d vs %d not ordered" % (it, oi))
    d = (o["Y"] - c.st["Y"]) if lo else (c.st["Y"] - o["Y"])
    if d > 15:
        return ("PASS", "it %d lines, Y long-short %+.0f" % (it, d), keys)
    # the AE keeps the brightness with the gain (manual time only): the short exposure then shows as a much softer, noisier picture
    hl, hs = (o["H"], c.st["H"]) if lo else (c.st["H"], o["H"])
    if hs > 0 and hl / hs > 1.5 and "again=" not in c.info:
        return ("PASS", "it %d lines, Y held by the AE gain (long-short %+.1f), detail long/short %.2f" % (it, d, hl / hs), keys)
    return Q("Y long-short only %+.1f: no picture effect" % d)


def rule_again(c, lo):
    want = 1024 if lo else 16384
    cur = _f(r"\bagain=(\d+)", c.info, int)
    o = c.stat("again-high" if lo else "again-low")
    if cur != want or o is None:
        return Q("again readback %s != %d" % (cur, want))
    d = (o["Y"] - c.st["Y"]) if lo else (c.st["Y"] - o["Y"])
    return ("PASS", "again %d, Y high-low %+.0f" % (cur, d), ["ISP_Tuning_Get/SetAeExprInfo"]) if d > 10 else Q("Y high-low only %+.1f" % d)


def rule_maxgain(c, digital):
    if "maxA=" in c.info:          # T41 AeExprInfo
        f = "maxD" if digital else "maxA"
        cur = _f(r"%s=(\d+)" % f, c.info, int)
        ref = _f(r"%s=(\d+)" % f, c.readback_info("exp-short"), int)
        if cur == 1024 and ref is not None and ref != 1024:
            return ("RT", "%s %d (was %d), cap stored" % (f, cur, ref), ["ISP_Tuning_Get/SetAeExprInfo"])
        return Q("%s readback %s, reference %s: not distinguishable" % (f, cur, ref))
    m = re.search(r"again (\d+)->(\d+) dgain (\d+)->(\d+)", c.info)
    if not m:
        return Q("no readback")
    new = int(m.group(4) if digital else m.group(2))
    k = "ISP_Tuning_Get/SetMaxDgain" if digital else "ISP_Tuning_Get/SetMaxAgain"
    if new == 32:
        return ("RT", "cap %s read back 32 (picture effect scene dependent)" % ("dgain" if digital else "again"), [k])
    return Q("cap readback %d != 32" % new)


def rule_aeattr(c, kind, lo):
    flag = {"it": r"\bit en(\d)=", "again": r"\bagain en(\d)=", "dgain": r"\bdgain en(\d)="}[kind]
    if _f(flag, c.info, int) != 1:
        return Q("manual flag not read back")
    keys = ["ISP_Tuning_Get/SetAeAttr"]
    if kind == "dgain":
        d = c.st["Y"] - c.b["Y"]
        return ("PASS", "Y %+.0f vs base" % d, keys) if d > 10 else Q("Y %+.1f vs base: no picture effect" % d)
    oname = {("it", 1): "aeattr-it-long", ("it", 0): "aeattr-it-short", ("again", 1): "aeattr-again-high", ("again", 0): "aeattr-again-low"}[(kind, 0 if lo else 1)]
    o = c.stat(oname)
    if o is None:
        return Q("no opposite case")
    d = (o["Y"] - c.st["Y"]) if lo else (c.st["Y"] - o["Y"])
    return ("PASS", "Y high-low %+.0f" % d, keys) if d > 15 else Q("Y high-low only %+.1f: no picture effect" % d)


WBMODE = {"auto": 0, "daylight": 2, "cloudy": 3, "incandescent": 4, "fluorescent": 5, "twilight": 6, "shade": 7, "warm-fluorescent": 8}


def rule_awb(c, what):
    m = re.search(r"mode (\d+) r=(\d+) b=(\d+)", c.info)
    if not m:
        return Q("no readback")
    mode, r, b = int(m.group(1)), int(m.group(2)), int(m.group(3))
    sh = abs(c.st["U"] - c.b["U"]) + abs(c.st["V"] - c.b["V"])
    if what == "auto":
        return RTV("mode 0 read back; mode select, no picture effect expected") if mode == 0 else Q("mode %d" % mode)
    if what in ("rhigh", "bhigh"):
        s = re.search(r"set r=(\d+) b=(\d+)", c.info)
        w = re.search(r"\(was mode \d+ r=(\d+) b=(\d+)", c.info)
        if s:
            exp = (int(s.group(1)), int(s.group(2)))
        elif w and int(w.group(1)) and int(w.group(2)):       # T41 line has no "set" part: the case doubles one gain
            exp = (2 * int(w.group(1)), int(w.group(2))) if what == "rhigh" else (int(w.group(1)), 2 * int(w.group(2)))
        else:
            exp = None
        if mode != 1 or exp is None or (r, b) != exp:
            return Q("manual gains read back r=%d b=%d, expected %s" % (r, b, exp))
        return P("manual r=%d b=%d, U/V shift %.0f" % (r, b, sh)) if sh >= 3 else Q("U/V shift only %.1f" % sh)
    if mode != WBMODE[what]:
        return Q("mode %d != %d" % (mode, WBMODE[what]))
    return P("mode %d, U/V shift %.0f" % (mode, sh)) if sh >= 1.5 else Q("U/V shift only %.1f vs base" % sh)


def rule_awbct(c, hi):
    o = c.stat("awbct-2800" if hi else "awbct-7500")
    if o is None:
        return Q("no opposite case")
    s = abs(c.st["U"] - o["U"]) + abs(c.st["V"] - o["V"])
    return ("PASS", "U/V shift 2800/7500 %.1f" % s, ["ISP_Tuning_SetAwbCt"]) if s >= 2 else Q("U/V shift 2800/7500 only %.1f: no effect" % s)


def rule_wbalgo(c, other):
    o = c.stat(other)
    if o is None:
        return Q("no opposite case")
    s = abs(c.st["U"] - o["U"]) + abs(c.st["V"] - o["V"])
    return ("PASS", "U/V shift between algos %.1f" % s, ["ISP_Tuning_SetWB_ALGO"]) if s >= 2 else Q("U/V shift between algos only %.1f" % s)


def rule_ccm(c, mono):
    if "manual=1" not in c.info:
        return Q("no readback")
    if mono:
        ok = abs(c.st["U"] - c.st["V"]) <= 0.5 and "m0=0.33 m4=0.33 m8=0.33" in c.info
        return P("U %.2f V %.2f" % (c.st["U"], c.st["V"])) if ok else Q("U %.2f V %.2f not grey" % (c.st["U"], c.st["V"]))
    sh = abs(c.st["U"] - c.b["U"]) + abs(c.st["V"] - c.b["V"])
    ok = "m0=0.00 m4=1.00 m8=0.00" in c.info and sh >= 2
    return P("U/V shift %.1f" % sh) if ok else Q("U/V shift only %.1f" % sh)


def rule_csc(c, want):
    m = re.search(r"(?:mode|gamut) (\d+) \(was \d+\) coef0 ([\d.]+)", c.info)
    if not m:
        return Q("no readback")
    if int(m.group(1)) != want:
        return Q("mode readback %s != set %d" % (m.group(1), want))
    if want < 4:
        coefs = {re.search(r"coef0 ([\d.]+)", c.readback_info("csc-%d" % i)).group(1) for i in range(4) if re.search(r"coef0 ([\d.]+)", c.readback_info("csc-%d" % i))}
        if len(coefs) < 4:
            return Q("coefficients of modes 0-3 not distinct")
    elif m.group(2) == re.search(r"coef0 ([\d.]+)", c.readback_info("csc-0")).group(1):
        return Q("user matrix mode read back, coefficient unchanged")
    keys = ["ISP_Tuning_Get/SetISPCSCAttr"] if "gamut" in c.info else ["ISP_Tuning_Get/SetCsc_Attr"]
    return ("RT", "mode %d coef0 %s read back; mode select, no picture effect expected" % (want, m.group(2)), keys)


def _noise_pair(c, w, base):        # strength 0 shows more detail/noise than 255
    z, f = c.stat(base + "-0"), c.stat(base + "-255")
    return z, f


def eff_noise(z, f):
    return z["H"] > 1.15 * f["H"], "H(0) %.2f vs H(255) %.2f" % (z["H"], f["H"])


def eff_dpc(z, f):
    return f["H"] < 0.92 * z["H"], "H(0) %.2f vs H(255) %.2f" % (z["H"], f["H"])


def eff_drc(z, f):
    return abs(f["Y"] - z["Y"]) > 5 or abs(f["H"] - z["H"]) > 0.1 * max(z["H"], 0.01), "Y(0) %.1f Y(255) %.1f" % (z["Y"], f["Y"])


def eff_hld(z, f):
    if f["Y"] < 1.0:
        return False, "picture black at 255 (Y %.2f)" % f["Y"]
    return f["Y"] < z["Y"] - 5, "Y(0) %.1f Y(255) %.1f" % (z["Y"], f["Y"])


def eff_blc(z, f):
    return f["Y"] > z["Y"] + 5, "Y(0) %.1f Y(255) %.1f" % (z["Y"], f["Y"])


def rule_strength(c, want, kind, key, prefix, eff, ratio=None):
    a = _arrow(c.info)
    if ratio is not None:
        key, has_rb = "ISP_Tuning_Get/SetModule_Ratio", True
    else:
        has_rb = a is not None
    keys = [key if has_rb else _setkey(key)]
    z, f = c.stat(prefix + "-0"), c.stat(prefix + "-255")
    if ratio is not None:
        m = re.search(r"\[(\d+)\] en(\d) ratio (\d+)", c.info)
        if not m or int(m.group(2)) != 1 or int(m.group(3)) != want:
            return ("PART", "ratio readback %s != %d" % (m.groups() if m else "?", want), keys)
    elif has_rb and a[1] != want:
        return ("PART", "readback %d != set %d" % (a[1], want), keys)
    if z is None or f is None:
        return ("PART", "no opposite case", keys)
    ok, note = eff(z, f)
    if not ok:
        return ("PART", note + ": no expected picture effect", keys)
    return ("PASS", note + ("" if has_rb else "; no getter"), keys)


def rule_defogstr(c):
    a = _arrow(c.info)
    if not a or a[1] != 255:
        return Q("readback %s != 255" % (a,))
    o = c.stat("defog-on")
    if o is None:
        return Q("no defog-on reference")
    ok = abs(c.st["Y"] - o["Y"]) > 4 or abs(c.st["H"] - o["H"]) > 0.1 * max(o["H"], 0.01)
    return ("PASS", "Y %.1f H %.2f vs defog-on %.1f/%.2f" % (c.st["Y"], c.st["H"], o["Y"], o["H"]), ["ISP_Tuning_Get/SetDefog_Strength", "ISP_Tuning_EnableDefog"]) if ok else Q("no picture effect vs defog-on")


def rule_drcen(c, on):
    o = c.stat("drc-off" if on else "drc-on")
    if o is None:
        return Q("no opposite case")
    d = abs(c.st["Y"] - o["Y"])
    return ("PASS", "Y on/off differ %.1f" % d, ["ISP_Tuning_EnableDRC"]) if d > 5 else Q("Y on/off differ only %.1f" % d)


def rule_rawdrc(c):
    m = re.search(r"mode (\d+) str (\d+)", c.info)
    if not m or int(m.group(2)) != 255:
        return Q("strength readback %s != 255" % (m.group(2) if m else "?"))
    if not changed_vs(c, c.b, 5, 0.1):
        return Q("no picture effect")
    return ("PASS", "str 255 read back, Y %.1f H %.2f vs base %.1f/%.2f" % (c.st["Y"], c.st["H"], c.b["Y"], c.b["H"]), ["ISP_Tuning_GetRawDRC", "ISP_Tuning_SetRawDRC"])


def rule_antifog(c):
    return P("Y %.1f H %.2f vs base %.1f/%.2f; no getter" % (c.st["Y"], c.st["H"], c.b["Y"], c.b["H"])) if changed_vs(c, c.b, 5, 0.15) else Q("no picture effect")


def rule_colorfx(c, mode):
    bad = rb_arrow(c, mode)
    if bad:
        return Q(bad)
    bw = c.stat("colorfx-bw")
    if mode == 1:
        return P("U %.2f V %.2f" % (c.st["U"], c.st["V"])) if abs(c.st["U"] - c.st["V"]) <= 0.5 and abs(c.b["U"] - c.b["V"]) - abs(c.st["U"] - c.st["V"]) >= 0.0 else Q("not monochrome")
    if mode == 2:
        return P("U %.1f V %.1f" % (c.st["U"], c.st["V"])) if c.st["V"] - c.st["U"] > 15 else Q("no sepia cast")
    if mode == 3:
        return P("Y %.1f vs inverted base %.1f" % (c.st["Y"], 255 - c.b["Y"])) if abs(c.st["Y"] - (255 - c.b["Y"])) < 25 and abs(c.st["Y"] - c.b["Y"]) > 5 else Q("Y %.1f not inverted (base %.1f)" % (c.st["Y"], c.b["Y"]))
    if bw is None:
        return Q("no bw reference")
    n = (bw["U"] + bw["V"]) / 2.0
    db, dh = _chroma(c.b, n), _chroma(c.st, n)
    return P("chroma %.1f vs base %.1f" % (dh, db)) if dh > 1.5 * db + 1.0 else Q("chroma %.1f vs base %.1f: no vivid increase" % (dh, db))


def rule_scene(c, want):
    bad = rb_arrow(c, want)
    if bad:
        return Q(bad)
    return P("Y %.1f" % c.st["Y"]) if changed_vs(c, c.b, 5, 0.2, 3.0) else Q("no picture effect")


def rule_mesh(c):
    o = c.stat("shading-ultra" if c.name == "shading-low" else "shading-low")
    if o is None:
        return Q("no opposite case")
    if min(c.st["Y"], o["Y"]) < 20:
        return Q("scene too dark for a lens shading measurement (Y %.1f / %.1f); needs a bright scene" % (c.st["Y"], o["Y"]))

    def vig(st):            # corner / centre luma of the 8x8 grid: lens shading compensation lifts the corners
        g = st["G"]
        return (g[0] + g[7] + g[56] + g[63]) / 4.0 / max((g[27] + g[28] + g[35] + g[36]) / 4.0, 1.0)
    d, dv = abs(c.st["Y"] - o["Y"]), abs(vig(c.st) - vig(o))
    return P("corner/centre %.3f vs %.3f (Y differ %.1f); no getter" % (vig(c.st), vig(o), d)) if (d > 5 or dv > 0.03) else Q("low/ultra differ only Y %.1f, corner/centre %.3f" % (d, dv))


def rule_aflick(c, want):
    if "mode " in c.info and "/" in c.info:             # T41 "mode M/F"
        m = re.match(r"mode (\d+)/(\d+)", c.info)
        ok = m and ((want == 0 and m.group(1) == "0") or (want == 1 and m.group(1) != "0" and m.group(2) == "50") or (want == 2 and m.group(1) != "0" and m.group(2) == "60"))
        return RTV(c.info) if ok else Q("readback " + c.info)
    bad = rb_arrow(c, want)
    return Q(bad) if bad else RTV("readback ok; no picture effect expected")


def rule_runmode(c, night):
    _rm = _runmode(c, night)
    if _rm[0] == "PASS" and not night:
        return RTV("mode read back; no picture effect expected")
    return _rm


def _runmode(c, night):
    m = re.search(r"mode now (\d+)", c.info)
    if not m or int(m.group(1)) != night:
        return Q("readback %s" % (m.group(1) if m else "?"))
    if night and abs(c.st["U"] - c.st["V"]) > 0.6:
        return Q("night mode not monochrome (U %.2f V %.2f)" % (c.st["U"], c.st["V"]))
    return P("U %.2f V %.2f" % (c.st["U"], c.st["V"]))


def rule_wdr(c, want):
    bad = rb_arrow(c, want)
    return Q(bad) if bad else RTV("readback ok; WDR flag, no picture effect expected on a linear sensor")


def rule_bypass(c):
    m = re.search(r"key 0x([0-9a-f]+) -> 0x([0-9a-f]+) \(was 0x([0-9a-f]+)\)", c.info)
    if not m:
        return Q("no readback")
    a, b, w = m.groups()
    if not (a == b and b != w):
        return Q("key %s -> %s (was %s): bit not changed" % (a, b, w))
    # AE drifts during a run, so base is no reference: use the median of all bypass cases (+ the antiflicker-off case) as "neutral picture"
    names = [n for n in c.rows if n.startswith("bypass-")] + ["aflicker-off"]
    sts = [c.stat(n) for n in names if c.stat(n)]
    if len(sts) < 4:
        # a filtered colour pass has no median of other bypass cases: the reference is base and base-end (their difference is the drift)
        be = c.stat("base-end")
        if not be:
            return Q("readback only, no reference picture")
        drift = abs(c.b["U"] - be["U"]) + abs(c.b["V"] - be["V"])
        ref = {k: (c.b[k] + be[k]) / 2 for k in ("Y", "U", "V", "H")}
        ref["G"] = [(x + y) / 2 for x, y in zip(c.b["G"], be["G"])]
        sh = abs(c.st["U"] - ref["U"]) + abs(c.st["V"] - ref["V"])
        if sh > max(3.0, 3 * drift) or abs(c.st["Y"] - ref["Y"]) > 6 or abs(c.st["H"] - ref["H"]) > 0.2 * max(ref["H"], 0.01):
            return P("key %s (was %s), picture differs from base/base-end (U/V shift %.1f, drift %.1f, Y %.1f vs %.1f)" % (b, w, sh, drift, c.st["Y"], ref["Y"]))
        return Q("readback only, no visible effect (U/V shift %.1f, drift %.1f)" % (sh, drift))

    def med(f):
        v = sorted(f(x) for x in sts)
        return v[len(v) // 2]
    ref = {k: med(lambda x, k=k: x[k]) for k in ("Y", "U", "V", "H")}
    ref["G"] = [med(lambda x, i=i: x["G"][i]) for i in range(64)]
    if visible(c, ref):
        return P("key %s (was %s), picture differs from the neutral median (Y %.1f vs %.1f, H %.2f vs %.2f)" % (b, w, c.st["Y"], ref["Y"], c.st["H"], ref["H"]))
    return Q("readback only, no visible effect (Y %.1f H %.2f vs neutral %.1f/%.2f)" % (c.st["Y"], c.st["H"], ref["Y"], ref["H"]))


def rule_mask(c):
    if "chn0[" in c.info:
        if "en=1" not in c.info:
            return Q("not enabled on readback")
        key = "ISP_Tuning_Get/SetMask"
    else:
        key = "ISP_Tuning_SetMaskBlock"
    sh = abs(c.st["U"] - c.b["U"]) + abs(c.st["V"] - c.b["V"])
    g = _mad(c.st["G"], c.b["G"])
    return ("PASS", "U/V shift %.0f grid %.0f" % (sh, g), [key]) if sh >= 8 or g >= 15 else Q("masked picture not changed")


def rule_fcrop(c):
    en = re.search(r"en=1 (\d+),(\d+) (\d+)x(\d+)", c.info)
    if not en:
        return Q("crop not read back")
    g = _mad(c.st["G"], c.b["G"])
    return P("grid change %.0f, Y %.1f vs %.1f" % (g, c.st["Y"], c.b["Y"])) if g >= 6 or abs(c.st["Y"] - c.b["Y"]) > 5 else Q("no picture change")


def rule_flip(c, hh, vv, style):
    """style: isp (h%u v%u), hv2, enum (a->b), t41isp, t41sens, sens (no readback)"""
    keys = []
    want_enum = hh | (vv << 1)
    if style in ("isp", "hv2"):
        m = re.match(r"h(\d) v(\d)", c.info)
        if not m or (int(m.group(1)), int(m.group(2))) != (hh, vv):
            return Q("readback %s" % c.info.split(" (")[0])
        if style == "hv2":
            keys = ["ISP_Tuning_Get/SetISPHVflip"]
        else:
            keys = (["ISP_Tuning_Get/SetISPHflip"] if hh else []) + (["ISP_Tuning_Get/SetISPVflip"] if vv else [])
    elif style == "enum":
        bad = rb_arrow(c, want_enum)
        if bad:
            return Q(bad)
        keys = ["ISP_Tuning_SetHVFLIP", "ISP_Tuning_GetHVFlip"]
    elif style == "t41isp":
        if not re.search(r"isp %d/%d/%d" % ((want_enum,) * 3), c.info):
            return Q("readback " + c.info.split(" (")[0])
        keys = ["ISP_Tuning_Get/SetHVFLIP"]
    elif style == "t41sens":
        if not re.search(r"sensor %d " % want_enum, c.info):
            return Q("readback " + c.info.split(" (")[0])
        keys = ["ISP_Tuning_Get/SetHVFLIP"]
    else:
        keys = (["ISP_Tuning_SetSensorHflip"] if hh else []) + (["ISP_Tuning_SetSensorVflip"] if vv else [])
    # which mirror of the base picture is the new picture?  (th, tv) = toggles relative to base
    G0 = c.b["G"]
    cand = {(0, 0): G0, (1, 0): _gh(G0), (0, 1): _gv(G0), (1, 1): _gh(_gv(G0))}
    err = sorted((_mad(c.st["G"], g), t) for t, g in cand.items())
    if _mad(G0, cand[(1, 0)]) < 3.0 and _mad(G0, cand[(0, 1)]) < 3.0:
        return Q("picture symmetric, flip not visible")
    if err[0][0] > 6.0 or err[1][0] < 3 * err[0][0]:
        return Q("grid matches no mirror of base (best err %.1f, next %.1f)" % (err[0][0], err[1][0]))
    th, tv = err[0][1]
    base_state = (hh ^ th, vv ^ tv)           # flip state the base picture was taken in
    if style in ("isp", "hv2"):
        m = re.search(r"\(was (\d) (\d)\)", c.info)
        was = (int(m.group(1)), int(m.group(2))) if m else None
    elif style == "enum":
        a_ = _arrow(c.info)
        was = (a_[0] & 1, (a_[0] >> 1) & 1) if a_ else None
    elif style in ("t41isp", "t41sens"):
        m = re.search(r"\(was (\d+) (\d+)\)", c.info)
        w = int(m.group(2 if style == "t41isp" else 1)) if m else None
        was = (w & 1, (w >> 1) & 1) if w is not None else None
    else:
        was = (0, 0)
    if was is not None and was != base_state:
        return ("PART", "picture shows %s%s flip (err %.1f), implying initial state h%d v%d, but Get reported h%d v%d" % (
            "H" if th else "-", "V" if tv else "-", err[0][0], base_state[0], base_state[1], was[0], was[1]), keys)
    return ("PASS", "picture mirrored %s%s vs base as expected (err %.1f, next %.1f)" % ("H" if th else "-", "V" if tv else "-", err[0][0], err[1][0]), keys)


def _flip_rule(hh, vv, style):
    return lambda c: rule_flip(c, hh, vv, style)


def _T41(c):
    return "sensor " in c.info and "isp " in c.info


def guess_keys(case, info):
    t41 = "sensor " in info and "isp " in info
    g = {
        "isp-hflip": ["ISP_Tuning_Get/SetHVFLIP"] if t41 else ["ISP_Tuning_Get/SetISPHflip"],
        "isp-vflip": ["ISP_Tuning_Get/SetHVFLIP"] if t41 else ["ISP_Tuning_Get/SetISPVflip"],
        "isp-hvflip": ["ISP_Tuning_Get/SetHVFLIP"] if t41 else ["ISP_Tuning_Get/SetISPHflip", "ISP_Tuning_Get/SetISPVflip"],
        "isp-hvflip2-api": ["ISP_Tuning_Get/SetISPHVflip"],
        "sensor-hflip": ["ISP_Tuning_Get/SetHVFLIP"] if t41 else ["ISP_Tuning_SetSensorHflip"],
        "sensor-vflip": ["ISP_Tuning_Get/SetHVFLIP"] if t41 else ["ISP_Tuning_SetSensorVflip"],
        "sensor-hvflip": ["ISP_Tuning_Get/SetHVFLIP"] if t41 else ["ISP_Tuning_SetSensorHflip", "ISP_Tuning_SetSensorVflip"],
        "exp": ["ISP_Tuning_Get/SetAeExprInfo"] if "again=" in info else ["ISP_Tuning_SetExpr", "ISP_Tuning_GetExpr"],
        "again": ["ISP_Tuning_Get/SetAeExprInfo"],
        "gamma": ["ISP_Tuning_Get/SetGammaAttr"] if "type " in info else ["ISP_Tuning_Get/SetGamma"],
        "aecomp": ["ISP_Tuning_Get/SetAeScenceAttr"] if "comp en" in info else ["ISP_Tuning_Get/SetAeComp"],
        "maxagain": ["ISP_Tuning_Get/SetAeExprInfo"] if "maxA=" in info else ["ISP_Tuning_Get/SetMaxAgain"],
        "maxdgain": ["ISP_Tuning_Get/SetAeExprInfo"] if "maxA=" in info else ["ISP_Tuning_Get/SetMaxDgain"],
        "aeattr": ["ISP_Tuning_Get/SetAeAttr"], "awbct": ["ISP_Tuning_SetAwbCt"], "wbalgo": ["ISP_Tuning_SetWB_ALGO"],
        "csc": ["ISP_Tuning_Get/SetISPCSCAttr"] if "gamut" in info else ["ISP_Tuning_Get/SetCsc_Attr"],
        "defog": ["ISP_Tuning_Get/SetDefog_Strength", "ISP_Tuning_EnableDefog"], "drc-on": ["ISP_Tuning_EnableDRC"],
        "drc-off": ["ISP_Tuning_EnableDRC"], "rawdrc": ["ISP_Tuning_GetRawDRC", "ISP_Tuning_SetRawDRC"],
        "mask": ["ISP_Tuning_Get/SetMask"] if "chn0[" in info else ["ISP_Tuning_SetMaskBlock"],
    }
    if case in g:
        return g[case]
    if case.startswith("hvflip-enum"):
        return ["ISP_Tuning_SetHVFLIP", "ISP_Tuning_GetHVFlip"]
    for pre in ("exp", "again", "gamma", "aecomp", "maxagain", "maxdgain", "aeattr", "awbct", "wbalgo", "csc", "defog", "rawdrc", "mask"):
        if case.startswith(pre):
            return g[pre]
    return []


# case name -> (default keys, rule(c)).  rule returns (verdict, note[, keys]) or None (case ignored)
IMGFX_CASES = {
    "isp-hflip": ([], lambda c: rule_flip(c, 1, 0, "t41isp" if _T41(c) else "isp")),
    "isp-vflip": ([], lambda c: rule_flip(c, 0, 1, "t41isp" if _T41(c) else "isp")),
    "isp-hvflip": ([], lambda c: rule_flip(c, 1, 1, "t41isp" if _T41(c) else "isp")),
    "isp-hvflip2-api": ([], _flip_rule(1, 1, "hv2")),
    "hvflip-enum-h": ([], _flip_rule(1, 0, "enum")), "hvflip-enum-v": ([], _flip_rule(0, 1, "enum")),
    "hvflip-enum-hv": ([], _flip_rule(1, 1, "enum")),
    "sensor-hflip": ([], lambda c: rule_flip(c, 1, 0, "t41sens" if _T41(c) else "sens")),
    "sensor-vflip": ([], lambda c: rule_flip(c, 0, 1, "t41sens" if _T41(c) else "sens")),
    "sensor-hvflip": ([], lambda c: rule_flip(c, 1, 1, "t41sens" if _T41(c) else "sens")),
    "fcrop-mid50": (["ISP_Tuning_Get/SetFrontCrop"], rule_fcrop), "fcrop-topleft50": (["ISP_Tuning_Get/SetFrontCrop"], rule_fcrop),
    "brightness-low": (["ISP_Tuning_SetBrightness", "ISP_Tuning_GetBrightness"], lambda c: rule_bright(c, 30, True)),
    "brightness-high": (["ISP_Tuning_SetBrightness", "ISP_Tuning_GetBrightness"], lambda c: rule_bright(c, 225, False)),
    "contrast-low": (["ISP_Tuning_Get/SetContrast"], lambda c: rule_contrast(c, 30, True)),
    "contrast-high": (["ISP_Tuning_Get/SetContrast"], lambda c: rule_contrast(c, 225, False)),
    "saturation-0": (["ISP_Tuning_Get/SetSaturation"], lambda c: rule_sat(c, 0, True)),
    "saturation-high": (["ISP_Tuning_Get/SetSaturation"], lambda c: rule_sat(c, 230, False)),
    "sharpness-0": (["ISP_Tuning_SetSharpness", "ISP_Tuning_GetSharpness"], lambda c: rule_sharp(c, 0, True)),
    "sharpness-high": (["ISP_Tuning_SetSharpness", "ISP_Tuning_GetSharpness"], lambda c: rule_sharp(c, 255, False)),
    "hue-low": (["ISP_Tuning_Get/SetBcshHue"], lambda c: rule_hue(c, 40, True)),
    "hue-high": (["ISP_Tuning_Get/SetBcshHue"], lambda c: rule_hue(c, 215, False)),
    "gamma-steep": ([], lambda c: rule_gamma(c, "steep")), "gamma-linear": ([], lambda c: rule_gamma(c, "linear")),
    "aecomp-low": ([], lambda c: rule_aecomp(c, 60, True)), "aecomp-high": ([], lambda c: rule_aecomp(c, 200, False)),
    "exp-short": ([], lambda c: rule_exp(c, True)), "exp-long": ([], lambda c: rule_exp(c, False)),
    "again-low": ([], lambda c: rule_again(c, True)), "again-high": ([], lambda c: rule_again(c, False)),
    "maxagain-low": ([], lambda c: rule_maxgain(c, False)), "maxdgain-low": ([], lambda c: rule_maxgain(c, True)),
    "aeattr-it-short": ([], lambda c: rule_aeattr(c, "it", True)), "aeattr-it-long": ([], lambda c: rule_aeattr(c, "it", False)),
    "aeattr-again-low": ([], lambda c: rule_aeattr(c, "again", True)), "aeattr-again-high": ([], lambda c: rule_aeattr(c, "again", False)),
    "aeattr-dgain-high": ([], lambda c: rule_aeattr(c, "dgain", False)),
    "awbct-2800": ([], lambda c: rule_awbct(c, False)), "awbct-7500": ([], lambda c: rule_awbct(c, True)),
    "wbalgo-grayworld": ([], lambda c: rule_wbalgo(c, "wbalgo-reweight")), "wbalgo-reweight": ([], lambda c: rule_wbalgo(c, "wbalgo-grayworld")),
    "ccm-swap-rb": (["ISP_Tuning_Get/SetCCMAttr"], lambda c: rule_ccm(c, False)), "ccm-mono": (["ISP_Tuning_Get/SetCCMAttr"], lambda c: rule_ccm(c, True)),
    "defog-on": ([], lambda c: None), "defog-strength-255": ([], rule_defogstr),
    "drc-off": ([], lambda c: rule_drcen(c, False)), "drc-on": ([], lambda c: rule_drcen(c, True)),
    "rawdrc-off": ([], lambda c: None), "rawdrc-unlimit": ([], lambda c: None), "rawdrc-manual255": ([], rule_rawdrc),
    "antifog-strong": (["ISP_Tuning_SetAntiFogAttr"], rule_antifog), "antifog-weak": (["ISP_Tuning_SetAntiFogAttr"], rule_antifog),
    "mask-block": ([], rule_mask),
    "colorfx-bw": (["ISP_Tuning_Get/SetColorfxMode"], lambda c: rule_colorfx(c, 1)),
    "colorfx-sepia": (["ISP_Tuning_Get/SetColorfxMode"], lambda c: rule_colorfx(c, 2)),
    "colorfx-negative": (["ISP_Tuning_Get/SetColorfxMode"], lambda c: rule_colorfx(c, 3)),
    "colorfx-vivid": (["ISP_Tuning_Get/SetColorfxMode"], lambda c: rule_colorfx(c, 9)),
    "scene-night": (["ISP_Tuning_SetSceneMode", "ISP_Tuning_GetSceneMode"], lambda c: rule_scene(c, 8)),
    "scene-landscape": (["ISP_Tuning_SetSceneMode", "ISP_Tuning_GetSceneMode"], lambda c: rule_scene(c, 7)),
    "shading-low": (["ISP_Tuning_SetMeshShadingScale"], rule_mesh), "shading-ultra": (["ISP_Tuning_SetMeshShadingScale"], rule_mesh),
    "aflicker-off": (["ISP_Tuning_Get/SetAntiFlickerAttr"], lambda c: rule_aflick(c, 0)),
    "aflicker-50hz": (["ISP_Tuning_Get/SetAntiFlickerAttr"], lambda c: rule_aflick(c, 1)),
    "aflicker-60hz": (["ISP_Tuning_Get/SetAntiFlickerAttr"], lambda c: rule_aflick(c, 2)),
    "mode-day": (["ISP_Tuning_Get/SetISPRunningMode"], lambda c: rule_runmode(c, 0)),
    "mode-night": (["ISP_Tuning_Get/SetISPRunningMode"], lambda c: rule_runmode(c, 1)),
    "wdr-off": (["ISP_Tuning_Get/SetWDRAttr"], lambda c: rule_wdr(c, 0)), "wdr-on": (["ISP_Tuning_Get/SetWDRAttr"], lambda c: rule_wdr(c, 1)),
}
for _n in ("auto", "manual-rhigh", "manual-bhigh", "daylight", "cloudy", "incandescent", "fluorescent", "twilight", "shade", "warm-fluorescent"):
    _w = {"manual-rhigh": "rhigh", "manual-bhigh": "bhigh"}.get(_n, _n)
    IMGFX_CASES["awb-" + _n] = (["ISP_Tuning_Get/SetWB"], (lambda w: lambda c: rule_awb(c, w))(_w))
for _i in range(5):
    IMGFX_CASES["csc-%d" % _i if _i < 4 else "csc-4-user"] = ([], (lambda i: lambda c: rule_csc(c, i))(_i))
for _b in ("ccm", "gamma", "defog", "lsc", "dpc", "sdns", "mdns", "sharpen", "adr", "ydns", "bcsh"):
    IMGFX_CASES["bypass-" + _b] = (["ISP_Tuning_Get/SetModuleControl"], lambda c: rule_bypass(c))
# strength pairs 0/255 (classic arrow readback) and the T41 Module_Ratio variants
for _p, _key, _eff in (("sinter", "ISP_Tuning_SetSinterStrength", eff_noise), ("temper", "ISP_Tuning_SetTemperStrength", eff_noise),
                       ("drc-strength", "ISP_Tuning_Get/SetDRC_Strength", eff_drc), ("hilight-depress", "ISP_Tuning_Get/SetHiLightDepress", eff_hld),
                       ("backlight-comp", "ISP_Tuning_Get/SetBacklightComp", eff_blc)):
    for _v in (0, 255):
        IMGFX_CASES["%s-%d" % (_p, _v)] = ([], (lambda v, p, k, e: lambda c: rule_strength(c, v, None, k, p, e))(_v, _p, _key, _eff))
for _v in (0, 255):
    IMGFX_CASES["dpc-%d" % _v] = ([], (lambda v: lambda c: rule_strength(c, v, None, "ISP_Tuning_Get/SetDPC_Strength" if _arrow(c.info) else "ISP_Tuning_SetDPStrength", "dpc", eff_dpc))(_v))
for _p, _e in (("sinter", eff_noise), ("temper", eff_noise), ("drc", eff_drc), ("dpc", eff_dpc)):
    for _v in (0, 255):
        IMGFX_CASES["%s-ratio-%d" % (_p, _v)] = ([], (lambda v, p, e: lambda c: rule_strength(c, v, None, "ISP_Tuning_Get/SetModule_Ratio", p + "-ratio", e, ratio=True))(_v, _p, _e))
# not mapped on purpose (no function-specific, checkable effect): bcsh-combo, scaler-lv-*, ae-hlc, ae-blc, defog-ratio-255,
# fs1-crop-* / autozoom-ch1-* (act on channel 1, the statistics are taken from channel 0), isp-bypass, fps-*


def load_keys():
    ev = json.load(open(EVID, encoding="utf-8"))
    return list(ev["functions"])


T30PASS_ALIAS = {
    "t30pass_inttime": ["ISP_Tuning_Get/SetIntegrationTime"],
    "t30pass_waitframe": ["ISP_Tuning_WaitFrame"], "t30pass_waitframe_timeout": ["ISP_Tuning_WaitFrame"],
    "t30pass_aecomp": ["ISP_Tuning_Get/SetAeComp"], "t30pass_antifog": ["ISP_Tuning_SetAntiFogAttr"],
    "t30pass_rawdrc": ["ISP_Tuning_GetRawDRC", "ISP_Tuning_SetRawDRC"],
    "t30pass_rawdrc_strength": ["ISP_Tuning_GetRawDRC", "ISP_Tuning_SetRawDRC"],
    "t30pass_daynight": ["ISP_Tuning_Get/SetISPRunningMode"], "t30pass_csc": ["ISP_Tuning_Get/SetCsc_Attr"],
    "t30pass_wb": ["ISP_Tuning_Get/SetWB"], "t30pass_temperstrength": ["ISP_Tuning_SetTemperStrength"],
    "t30pass_aehist": ["ISP_Tuning_Get/SetAeHist"], "t30pass_aeweight": ["ISP_Tuning_Get/SetAeWeight"],
    "t30pass_awbhist": ["ISP_Tuning_Get/SetAwbHist"], "t30pass_awbweight": ["ISP_Tuning_Get/SetAwbWeight"],
    "t30pass_afhist": ["ISP_Tuning_Get/SetAfHist"], "t30pass_rgbcoefft": ["ISP_Tuning_Awb_Get/SetRgbCoefft"],
    "t30pass_sinterdns": ["ISP_Tuning_Get/SetSinterDnsAttr"], "t30pass_temperdns": ["ISP_Tuning_Get/SetTemperDnsAttr"],
    "t30pass_aeroi": ["ISP_Tuning_AE_Get/SetROI"], "t30pass_sensorreg": ["ISP_Get/SetSensorRegister"],
    "t30pass_movestate": ["ISP_Tuning_EnableMovestate", "ISP_Tuning_DisableMovestate"],
    "t30pass_colorfx": ["ISP_Tuning_Get/SetColorfxMode"],
    "t30pass_scene": ["ISP_Tuning_GetSceneMode", "ISP_Tuning_SetSceneMode"],
    "t30pass_videodrop": ["ISP_Tuning_SetVideoDrop"], "t30pass_fwfreeze": ["ISP_Tuning_SetFWFreeze"],
}

PAIR_ALIAS = {"ISP_Tuning_Get/SetHVFLIP": ["ISP_Tuning_GetHVFlip", "ISP_Tuning_SetHVFLIP"]}      # the vendor header spells the pair differently


def members(key):
    if key in PAIR_ALIAS:
        return PAIR_ALIAS[key]
    m = re.match(r"^(.*?)Get/Set(.*)$", key)
    return [m.group(1) + "Get" + m.group(2), m.group(1) + "Set" + m.group(2)] if m else [key]


class Acc:
    def __init__(self, keys):
        self.keys = keys
        self.m2k = {}
        for k in keys:
            if "*" in k or " " in k:
                continue
            for n in members(k):
                self.m2k.setdefault(n, k)
        self.seen = {}      # key -> {member: (verdict, detail, src)}
        self.override = False
        self.unmapped = []

    def key_of(self, name):
        if name in T30PASS_ALIAS:                       # t30pass_<item> are labels, not function names (at_cases.c)
            out = []
            for x in T30PASS_ALIAS[name]:
                if x in self.keys and "Get/Set" in x:
                    out += [(x, m) for m in members(x)]
                elif x in self.m2k:
                    out.append((self.m2k[x], x))
            return out, None
        n = re.sub(r"^IMP_", "", name)
        n = re.sub(r"\([^)]*\)$", "", n)               # "Encoder_Get/SetX(CBR)" -> variant of the key
        if n in self.keys and "Get/Set" in n:           # a whole pair reported at once
            return [(n, x) for x in members(n)], None
        if n in self.m2k:
            return self.m2k[n], n
        low = {x.lower(): x for x in self.m2k}          # vendor spelling differs in case (GetHVFlip / SetHVFLIP)
        if n.lower() in low:
            return self.m2k[low[n.lower()]], low[n.lower()]
        if "/" in n and n not in self.keys:         # composite "Encoder_A/B/C" -> Encoder_A, Encoder_B, Encoder_C
            parts = n.split("/")
            pre = parts[0][:parts[0].rindex("_") + 1] if "_" in parts[0] else ""
            names = [parts[0]] + [pre + p for p in parts[1:]]
            hit = [x for x in names if x in self.m2k]
            if hit:
                return [(self.m2k[x], x) for x in hit], None
        return None, n

    def add(self, name, verdict, detail, src, whole=False):
        k, n = self.key_of(name)
        if k is None:
            self.unmapped.append("%s %s %s" % (src, name, verdict))
            return
        for kk, nn in (k if isinstance(k, list) else [(k, n)]):
            d = self.seen.setdefault(kk, {})
            old = d.get(nn)
            # keep the worst verdict of repeated reports of the same member
            order = {"FAIL": 0, "PART": 1, "RT": 2, "PASS": 3, "SKIP": 4, "NA": 5}
            # either: the function is proven by ONE case with a visible effect (sharpness: low or high direction; module control: any block whose
            # bypass changes the picture, every bit flip is read back by its own case; module ratio: any module that has an effect on the scene; SetWB: any manual gain or preset that shifts U/V)
            either = nn in ("ISP_Tuning_SetSharpness", "ISP_Tuning_GetSharpness", "ISP_Tuning_SetModuleControl", "ISP_Tuning_GetModuleControl",
                            "ISP_Tuning_SetModule_Ratio", "ISP_Tuning_GetModule_Ratio",
                            "ISP_Tuning_SetWB")                    # white balance: the auto case is a mode read back (RT), the manual gains / presets show the colour shift
            if self.override and src == "imgfx" and (verdict == "PASS" or old is None or old[0] != "PASS"):       # a later colour pass (--imgfx twice) replaces what the first pass said about the same member
                d[nn] = (verdict, detail, src); continue
            if src == "imgfx" and verdict in ("PART", "RT") and old is not None and old[0] == "PASS" and (old[2] != "imgfx" or either):
                continue                            # an unclear picture case never takes back an apitest PASS
            if either and src == "imgfx" and verdict == "PASS" and old is not None and old[0] in ("PART", "RT") and old[2] == "imgfx":
                d[nn] = (verdict, detail, src); continue
            if old is None or order[verdict] < order[old[0]]:
                d[nn] = (verdict, detail, src)
            elif src == "imgfx" and verdict in ("PASS", "RT") and old[0] in ("PART", "SKIP", "NA") and old[2] != "imgfx":
                d[nn] = (verdict, detail, src)      # a picture-proven case upgrades a bare apitest PART
            elif old[0] == verdict == "PASS" and detail and not old[1]:
                d[nn] = (verdict, detail, src)

    def result(self):
        out = {}
        for k, d in self.seen.items():
            mem = members(k)
            vs = [d[x][0] for x in mem if x in d]
            if "FAIL" in vs:
                v = "FAIL"
            elif vs and all(x == "PASS" for x in vs) and len(vs) == len(mem):
                v = "PASS"
            elif vs and all(x in ("PASS", "RT") for x in vs) and len(vs) == len(mem):
                v = "RT"
            elif "PASS" in vs or "PART" in vs or "RT" in vs:
                v = "PART"
            elif "SKIP" in vs:
                v = "SKIP"
            else:
                v = "NA"
            detail = "; ".join("%s: %s" % (x, d[x][1]) if len(mem) > 1 else d[x][1] for x in mem if x in d and (d[x][0] in ("FAIL", "PART") or len(d) == 1 or True))[:400]
            out[k] = {"v": v, "detail": detail, "src": sorted({d[x][2] for x in d})}
        return out


def parse_apitest(path, acc):
    for l in open(path, errors="replace"):
        m = re.match(r"\[A\] (\S+) ret=(-?\d+|\S+) (PASS|FAIL|N/A|SKIP)(?: (.*))?$", l.rstrip("\n"))
        if not m:
            continue
        fn, ret, v, det = m.group(1), m.group(2), m.group(3), (m.group(4) or "")
        v = {"N/A": "NA"}.get(v, v)
        if v == "PASS" and re.match(r"IMP_(AO_|AI_\w*Aec|AI_GetFrameAndRef)", fn):
            v = "RT"                  # silence-only AO/AEC runs (user decision 2026-10-26): return codes and round trips, no audible effect
        if v == "PASS" and re.search(r"(^|\s)rt: ", det):
            v = "RT"                  # at_cases.c: call + readback ok, no picture/state effect expected or measurable
        acc.add(fn, v, ("ret=%s %s" % (ret, det)).strip(), "apitest")


def parse_report(path, acc):
    """apitest --json report: {"funcs":[{"fn","area","ret","v":"PASS|FAIL|RT|NA|SKIP","detail"}]} (same verdicts as the [A] lines)"""
    try:
        rep = json.load(open(path))
    except ValueError:
        return                          # truncated by a watchdog kill: the .log fallback covers it
    for e in rep.get("funcs", []):
        if e["v"] == "PASS" and re.match(r"IMP_(AO_|AI_\w*Aec|AI_GetFrameAndRef)", e["fn"]):
            e["v"] = "RT"             # silence-only AO/AEC (see parse_apitest)
        acc.add(e["fn"], e["v"], ("ret=%s %s" % (e["ret"], e["detail"])).strip(), "apitest")


def parse_devtest(path, acc):
    for l in open(path, errors="replace"):
        m = re.match(r"RES (\S+) (PASS|PART|FAIL)(?: (.*))?$", l.rstrip("\n"))
        if m:
            acc.add(m.group(1), m.group(2), m.group(3) or "", "devtest")


def stats(line):
    m = re.search(r"ST Y=([\d.]+) U=([\d.]+) V=([\d.]+) H=([\d.]+) G=([\d.,]+)", line)
    if not m:
        return None
    return dict(Y=float(m.group(1)), U=float(m.group(2)), V=float(m.group(3)), H=float(m.group(4)), G=[float(x) for x in m.group(5).split(",")])


def parse_imgfx(path, acc, cases_out=None):
    rows = {}          # case -> (ret|None, readback text, stats|None)
    order = []
    for l in open(path, errors="replace"):
        l = l.rstrip("\n")
        m = re.match(r"\[R\] (\S+) (N/A|set=(-?\d+) get=(.*))$", l)
        if not m:
            continue
        if m.group(2) == "N/A":
            rows[m.group(1)] = (None, "", None)
            order.append(m.group(1))
            continue
        rest = m.group(4)
        i = rest.find("ST Y=")
        st = stats(rest[i:]) if i >= 0 else None
        info = rest[:i] if i >= 0 else rest
        info = re.sub(r"\s*\|\s*$", "", info).strip()
        rows[m.group(1)] = (int(m.group(3)), info, st)
        order.append(m.group(1))
    if "base" not in rows or not rows["base"][2]:
        return
    base = rows["base"][2]
    for case in order:
        ret, info, st = rows[case]
        if case.startswith("base") or ret is None:
            continue                      # base reference / N/A: not exported
        spec = IMGFX_CASES.get(case)
        if spec is None:
            acc.unmapped.append("imgfx case %s (no rule)" % case)
            continue
        keys, rule = spec
        c = Ctx(case, ret, info, st, rows, base)
        if ret != 0:
            res = Q("set returned %d (%s)" % (ret, info[:60]))
        elif st is None:
            res = Q("no picture")
        elif _dead(c) and case not in ("brightness-low", "hilight-depress-255"):
            res = Q("picture lost (Y %.2f, base %.1f)" % (st["Y"], base["Y"]))
        else:
            try:
                res = rule(c)
            except Exception as ex:      # a rule must never take the whole run down
                res = Q("rule error %s" % ex)
        if res is None:
            if cases_out is not None:
                cases_out[case] = ["IGNORED", "effect judged by a sibling case"]
            continue
        v, note = res[0], res[1]
        if len(res) > 2:
            keys = res[2]
        elif not keys:
            keys = guess_keys(case, info)
        if not keys:
            acc.unmapped.append("imgfx case %s (no function for this readback)" % case)
            continue
        if cases_out is not None:
            cases_out[case] = [v, note]
        det = "imgfx %s: %s" % (case, note)
        for k in keys:
            acc.add(k, v, det, "imgfx")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--soc", required=True)
    ap.add_argument("--ip", default="")
    ap.add_argument("--image", default="")
    ap.add_argument("--date", default="")
    ap.add_argument("--apitest")
    ap.add_argument("--report", help="apitest --json report (instead of --apitest)")
    ap.add_argument("--devtest")
    ap.add_argument("--imgfx", action="append", help="imgfx log; a second file (colour pass, IMGFX_SAT=230) overrides the first for the members it covers")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    acc = Acc(load_keys())
    cases = {}
    for f, fn in ((a.report, parse_report), (a.apitest, parse_apitest), (a.devtest, parse_devtest)):
        if f and os.path.exists(f):
            fn(f, acc)
    for i, f in enumerate(a.imgfx or []):
        if os.path.exists(f):
            acc.override = i > 0
            parse_imgfx(f, acc, cases)
    acc.override = False
    res = acc.result()
    cnt = {}
    for r in res.values():
        cnt[r["v"]] = cnt.get(r["v"], 0) + 1
    json.dump({"soc": a.soc, "ip": a.ip, "image": a.image, "date": a.date, "counts": cnt, "funcs": res, "imgfx_cases": cases,
               "unmapped": sorted(set(acc.unmapped))}, open(a.out, "w"), indent=1, sort_keys=True)
    print("%s: %d functions %s, %d unmapped" % (a.soc, len(res), cnt, len(set(acc.unmapped))))


if __name__ == "__main__":
    main()
