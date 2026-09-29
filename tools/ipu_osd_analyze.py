#!/usr/bin/env python3
"""Analyse the output of t31_ipu_osd_probe on a PC.

usage: ipu_osd_analyze.py DIR [WIDTH HEIGHT]
Reads DIR/ipu-before.nv12, ipu-after.nv12, ipu-bitmap.bgra (defaults
640x368, 128x64 bitmap). Needs numpy; Pillow for the PNG previews.

Reports, per probe layer, where the frame changed, and which colour
conversion and alpha rounding best explain the blended luma/chroma, so a
CPU fallback and the OpenIMP IPU backend can match the hardware.
"""
import sys
import numpy as np

LAYERS = [  # name, x, y, w, h, kind
    ("pic pixel-alpha", 32, 32, 128, 64, "pic"),
    ("pic pixel*global(128)", 32, 128, 128, 64, "pic_g128"),
    ("cover red a=255", 224, 32, 96, 64, "cover_red"),
    ("cover blue a=128", 224, 128, 96, 64, "cover_blue"),
]


def load(d, w, h):
    def nv12(name):
        raw = np.fromfile(f"{d}/{name}", dtype=np.uint8)
        y = raw[: w * h].reshape(h, w).astype(np.int32)
        uv = raw[w * h: w * h * 3 // 2].reshape(h // 2, w // 2, 2).astype(np.int32)
        return y, uv[..., 0], uv[..., 1]

    bmp = np.fromfile(f"{d}/ipu-bitmap.bgra", dtype=np.uint8).reshape(64, 128, 4).astype(np.int32)
    return nv12("ipu-before.nv12"), nv12("ipu-after.nv12"), bmp


def rgb_to_yuv(r, g, b, variant):
    if variant == "bt601-studio":      # Y 16..235, UV 16..240
        y = 16 + (65.738 * r + 129.057 * g + 25.064 * b) / 256
        u = 128 + (-37.945 * r - 74.494 * g + 112.439 * b) / 256
        v = 128 + (112.439 * r - 94.154 * g - 18.285 * b) / 256
    elif variant == "kernel-analog+16":  # jz_ipu coefficients as read from the driver
        y = 16 + 0.299 * r + 0.587 * g + 0.114 * b
        u = 128 - 0.147 * r - 0.289 * g + 0.436 * b
        v = 128 + 0.615 * r - 0.515 * g - 0.100 * b
    elif variant == "full-range":
        y = 0.299 * r + 0.587 * g + 0.114 * b
        u = 128 - 0.1687 * r - 0.3313 * g + 0.5 * b
        v = 128 + 0.5 * r - 0.4187 * g - 0.0813 * b
    else:
        raise ValueError(variant)
    return np.clip(y, 0, 255), np.clip(u, 0, 255), np.clip(v, 0, 255)


BLENDS = {
    "a/255": lambda s, d, a: (s * a + d * (255 - a)) / 255.0,
    ">>8": lambda s, d, a: np.floor((s * a + d * (256 - a)) / 256.0),
    "(a+1)>>8": lambda s, d, a: np.floor((s * (a + 1) + d * (255 - a)) / 256.0),
    "+128>>8": lambda s, d, a: np.floor((s * a + d * (255 - a) + 128) / 256.0),
}


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "."
    w = int(sys.argv[2]) if len(sys.argv) > 2 else 640
    h = int(sys.argv[3]) if len(sys.argv) > 3 else 368
    (y0, u0, v0), (y1, u1, v1), bmp = load(d, w, h)

    changed = y0 != y1
    print(f"frame {w}x{h}: {changed.sum()} luma pixels changed")
    inside = np.zeros_like(changed)
    for name, x, y, lw, lh, kind in LAYERS:
        box = changed[y:y + lh, x:x + lw]
        inside[y:y + lh, x:x + lw] = True
        ys, xs = np.nonzero(box)
        extent = f"x {xs.min()+x}..{xs.max()+x}, y {ys.min()+y}..{ys.max()+y}" if len(xs) else "unchanged"
        print(f"- {name}: {box.sum()} of {lw*lh} changed ({extent})")
    stray = changed & ~inside
    print(f"- outside the layer boxes: {stray.sum()} changed" + (" <-- unexpected" if stray.any() else ""))

    # pixel-alpha bitmap: fit conversion x rounding on luma
    x0, yb = 32, 32
    a = bmp[..., 3]
    r, g, b = bmp[..., 2], bmp[..., 1], bmp[..., 0]
    bg = y0[yb:yb + 64, x0:x0 + 128]
    got = y1[yb:yb + 64, x0:x0 + 128]
    print("\npic pixel-alpha, luma fit (mean |error| over the region):")
    best = None
    for conv in ("bt601-studio", "kernel-analog+16", "full-range"):
        sy, _, _ = rgb_to_yuv(r, g, b, conv)
        for bname, fn in BLENDS.items():
            err = np.abs(np.round(fn(sy, bg, a)) - got).mean()
            print(f"  {conv:18s} {bname:9s} {err:6.2f}")
            if best is None or err < best[0]:
                best = (err, conv, bname)
    print(f"  best: {best[1]} with {best[2]} (mean |err| {best[0]:.2f})")

    g128 = y1[128:192, 32:160]
    sy, _, _ = rgb_to_yuv(r, g, b, best[1])
    eff = a * 128 // 255
    err = np.abs(np.round(BLENDS[best[2]](sy, y0[128:192, 32:160], eff)) - g128).mean()
    print(f"pic pixel*global(128): mean |err| with alpha*128/255 = {err:.2f}")

    for name, x, y, lw, lh, kind in LAYERS[2:]:
        cy = y1[y + 8:y + lh - 8, x + 8:x + lw - 8]
        cu = u1[(y + 8) // 2:(y + lh - 8) // 2, (x + 8) // 2:(x + lw - 8) // 2]
        cv = v1[(y + 8) // 2:(y + lh - 8) // 2, (x + 8) // 2:(x + lw - 8) // 2]
        by = y0[y + 8:y + lh - 8, x + 8:x + lw - 8]
        print(f"{name}: Y mean {cy.mean():.1f} (bg {by.mean():.1f}), U {cu.mean():.1f}, V {cv.mean():.1f}")
    print("  expected stock colour words: red Y=82 U=90 V=240 (no +16), blue Y=41 U=240 V=110")

    try:
        from PIL import Image
        for tag, (yy, uu, vv) in (("before", (y0, u0, v0)), ("after", (y1, u1, v1))):
            U = uu.repeat(2, 0).repeat(2, 1) - 128
            V = vv.repeat(2, 0).repeat(2, 1) - 128
            Y = (yy - 16) * 1.164
            rgb = np.stack([Y + 1.596 * V, Y - 0.392 * U - 0.813 * V, Y + 2.017 * U], -1)
            Image.fromarray(np.clip(rgb, 0, 255).astype(np.uint8)).save(f"{d}/ipu-{tag}.png")
        print(f"\npreviews: {d}/ipu-before.png, {d}/ipu-after.png")
    except ImportError:
        print("\n(Pillow not installed: no PNG previews)")


if __name__ == "__main__":
    main()
