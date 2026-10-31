#!/usr/bin/env python3
"""Host test of src/t23/openimp_t23_jpegdec.c against Pillow (libjpeg): baseline JPEGs in several samplings, with restart
intervals, 16 bit quantisation and odd sizes decode to the same picture (luma and chroma within the libjpeg rounding),
progressive / truncated / garbage input is refused.  Usage: jpegdec_check.py <jpegdec_cli>"""
import io, os, subprocess, sys, tempfile, random
from PIL import Image, ImageDraw

cli = sys.argv[1]
random.seed(7)


def picture(w, h, gray=False):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        for x in range(w):
            px[x, y] = ((x * 255) // max(w - 1, 1), (y * 255) // max(h - 1, 1), ((x + y) * 255) // max(w + h - 2, 1))
    d = ImageDraw.Draw(im)
    if w >= 8 and h >= 8:       # muted colours: no RGB clipping in the libjpeg reference
        d.ellipse((w // 4, h // 4, 3 * w // 4, 3 * h // 4), fill=(170, 90, 100))
        if w // 3 > 2 and h // 5 > 2:
            d.rectangle((2, 2, w // 3, h // 5), fill=(60, 150, 90))
    return im.convert("L") if gray else im


def planes(im, w, h):
    """Y, Cb, Cr (4:2:0 box averages) from the libjpeg-decoded picture"""
    ycc = im.convert("YCbCr")
    y, cb, cr = [p.tobytes() for p in ycc.split()]
    cw, ch = (w + 1) // 2, (h + 1) // 2
    out = []
    for pl in (cb, cr):
        c = bytearray(cw * ch)
        for cy in range(ch):
            for cx in range(cw):
                s = 0
                for dy in (0, 1):
                    for dx in (0, 1):
                        s += pl[min(2 * cy + dy, h - 1) * w + min(2 * cx + dx, w - 1)]
                c[cy * cw + cx] = (s + 2) // 4
        out.append(bytes(c))
    return y, out[0], out[1]


def mae(a, b):
    return sum(abs(x - y) for x, y in zip(a, b)) / max(len(a), 1)


def run(data, fmt):
    with tempfile.TemporaryDirectory() as d:
        i, o = os.path.join(d, "a.jpg"), os.path.join(d, "a.raw")
        open(i, "wb").write(data)
        r = subprocess.run([cli, i, fmt, o], capture_output=True, text=True)
        if r.returncode != 0:
            return None, r.stdout
        return open(o, "rb").read(), r.stdout


fails = 0
cases = []
for (w, h) in ((64, 48), (37, 29), (16, 16), (1, 1), (130, 7)):
    for sub in (0, 1, 2):                      # 4:4:4, 4:2:2, 4:2:0
        cases.append((w, h, False, dict(subsampling=sub, quality=90), "sub%d" % sub))
    cases.append((w, h, True, dict(quality=85), "gray"))
cases.append((64, 48, False, dict(subsampling=2, quality=90, restart_marker_blocks=3), "restart3"))
cases.append((64, 48, False, dict(subsampling=0, quality=75, restart_marker_rows=1), "restart-rows"))
cases.append((64, 48, False, dict(subsampling=2, quality=95, qtables=[[1] * 64, [1] * 64]), "q1"))
cases.append((40, 40, False, dict(subsampling=2, quality=60, optimize=True), "optimized"))
for (w, h, gray, kw, name) in cases:
    im = picture(w, h, gray)
    b = io.BytesIO()
    im.save(b, "JPEG", **kw)
    ref = Image.open(io.BytesIO(b.getvalue()))
    ref.load()
    exact = None
    if (kw.get("subsampling") == 2 or gray) and w > 1:      # 4:2:0 / gray: the file's own planes, via ffmpeg
        with tempfile.TemporaryDirectory() as d:
            open(os.path.join(d, "a.jpg"), "wb").write(b.getvalue())
            r = subprocess.run(["ffmpeg", "-v", "error", "-i", os.path.join(d, "a.jpg"), "-pix_fmt", "yuvj420p" if not gray else "gray",
                                "-f", "rawvideo", os.path.join(d, "a.raw")], capture_output=True)
            if r.returncode == 0:
                exact = open(os.path.join(d, "a.raw"), "rb").read()
    if gray:
        ry = ref.convert("L").tobytes()
        rcb = rcr = bytes([128]) * (((w + 1) // 2) * ((h + 1) // 2))
    else:
        ry, rcb, rcr = planes(ref.convert("RGB"), w, h)
    if exact is not None and not gray and len(exact) == w * h + 2 * ((w + 1) // 2) * ((h + 1) // 2):
        ry, rcb, rcr = exact[:w * h], exact[w * h:w * h + ((w + 1) // 2) * ((h + 1) // 2)], exact[w * h + ((w + 1) // 2) * ((h + 1) // 2):]
    for fmt in ("yuv420p", "nv12", "nv21"):
        raw, msg = run(b.getvalue(), fmt)
        tag = "%dx%d %s %s" % (w, h, name, fmt)
        if raw is None:
            print("FAIL", tag, "refused", msg); fails += 1; continue
        cw, ch = (w + 1) // 2, (h + 1) // 2
        if len(raw) != w * h + 2 * cw * ch or msg.split() != [str(w), str(h)]:
            print("FAIL", tag, "size", len(raw), msg); fails += 1; continue
        y = raw[:w * h]
        if fmt == "yuv420p":
            cb, cr = raw[w * h:w * h + cw * ch], raw[w * h + cw * ch:]
        else:
            ch_ = raw[w * h:]
            u, v = ch_[0::2], ch_[1::2]
            cb, cr = (u, v) if fmt == "nv12" else (v, u)
        ey, ecb, ecr = mae(y, ry), mae(cb, rcb), mae(cr, rcr)
        lim = 0.6 if exact is not None else 3.0       # exact planes: IDCT rounding only; else libjpeg's own upsampling / RGB round trip
        ok = ey < (0.6 if exact is not None else 2.0) and ecb < lim and ecr < lim
        if not ok:
            fails += 1
        print("PASS" if ok else "FAIL", tag, "mae Y %.2f Cb %.2f Cr %.2f" % (ey, ecb, ecr))

# refusals
prog = io.BytesIO()
picture(32, 32).save(prog, "JPEG", progressive=True)
good = io.BytesIO()
picture(64, 48).save(good, "JPEG", quality=90)
for name, data in (("progressive", prog.getvalue()), ("truncated", good.getvalue()[:len(good.getvalue()) // 2]),
                   ("garbage", bytes(random.randrange(256) for _ in range(500))), ("empty", b"\xff\xd8"),
                   ("headeronly", good.getvalue()[:200])):
    raw, msg = run(data, "nv12")
    ok = raw is None
    print("PASS" if ok else "FAIL", "refused:", name)
    fails += 0 if ok else 1
sys.exit(1 if fails else 0)
