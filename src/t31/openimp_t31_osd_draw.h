/* CPU drawing for OSD_REG_LINE, OSD_REG_RECT and OSD_REG_BITMAP on NV12
 * frames (T31, T20, T21, T30 share this).  Header-only and free of hardware
 * dependencies so it can be unit tested on the host (tests/t31/osd_draw_test.c).
 *
 * Semantics follow the stock libimp osd_draw_line / OSD bitmap path
 * (libimp.so_hlil.txt, docs/T31_OSD_RE.md section 3.6):
 *  - opaque writes: Y byte per pixel, U,V pair at (x & ~1) of chroma row y/2
 *  - colour is the stock word 0xAAYYUUVV (alpha ignored when drawing)
 *  - line width lw: a pixel position p covers [p - lw/2, p - lw/2 + lw - 1]
 *  - a line whose two endpoints are equal draws nothing
 *  - endpoints are clamped to the frame, the group offPos is added, and the
 *    result is clamped again (stock clamps negative raw coordinates to w-1
 *    through an unsigned compare; here they go to 0)
 *  - exact horizontal (dy == 0) and exact vertical (dx == 0) lines keep the
 *    stock geometry byte for byte: a horizontal line is the union of the
 *    lw x lw stamps at p0 .. p1 (p1 excluded), a vertical one (and a
 *    horizontal one shorter than 2*lw, which the stock dispatch also sends
 *    there) is one filled box centred on x. Diagonals (dx and dy both
 *    non-zero) use osd_draw_diagonal below instead of the stock lw x lw
 *    stamp sequence. That sequence advances one stamp per step of the
 *    dominant axis, so with x dominant it covers lw + (lw - 1) * tan(angle)
 *    rows per step, i.e. lw * cos + (lw - 1) * sin across the line (and the
 *    mirror image with y dominant): 2*lw - 1 rows, (2*lw - 1) / sqrt(2) =
 *    1.41*lw, at 45 degrees. The stroke is therefore always thicker than
 *    requested, up to a factor 1.41 for a large lw halfway through the
 *    quadrant. The replacement steps the dominant axis and draws a run along
 *    the minor axis whose length is round(lw * hypot / major), so the stroke
 *    is a band of perpendicular width lw everywhere - within half a pixel,
 *    which is the best an integer run can do. It costs one run per step and
 *    writes every pixel of the band once. The band is extended by lw/2 at both
 *    ends, like the stamps: its ends are parallelogram corners rather than
 *    perpendicular caps and stick out at most lw/2 + 1/2 pixels along the line
 *    normal. A one-pixel line (lw == 1) keeps the stock routine completely - a
 *    single pixel has no width to distribute - so only lw >= 2 diagonals are
 *    drawn as a band.
 *  - RECT is the four edges p0.x/p1.x/p0.y/p1.y
 * Every write is clipped to the frame; nothing outside [0,w) x [0,h) of the
 * Y plane or the matching chroma bytes is touched. */
#ifndef OPENIMP_T31_OSD_DRAW_H
#define OPENIMP_T31_OSD_DRAW_H

#include <stdint.h>
#include <string.h>

struct osd_canvas {
    uint8_t *base;      /* Y plane; UV plane at base + uv_off */
    uint32_t width;     /* visible size, width must be even */
    uint32_t height;
    uint32_t stride;    /* >= width, even */
    size_t uv_off;      /* byte offset of the UV plane */
    int ymin, ymax;     /* touched Y rows (ymin > ymax: nothing) */
    int dry;            /* 1: only compute ymin/ymax, write nothing */
};

/* Canvas limit, and the line width cap. With lw >= 2 * max(w, h) every
 * stamp and box already covers the whole frame axis, so capping there
 * changes nothing and keeps 2 * lw and the int math in range. */
#define OSD_DRAW_MAX_DIM 8192
#define OSD_DRAW_MAX_LW (2 * OSD_DRAW_MAX_DIM)

static inline void osd_canvas_init(struct osd_canvas *c, uint8_t *base,
                                   uint32_t width, uint32_t height,
                                   uint32_t stride, size_t uv_off)
{
    c->base = base;
    c->width = width;
    c->height = height;
    c->stride = stride;
    c->uv_off = uv_off;
    c->ymin = (int)height;
    c->ymax = -1;
    c->dry = 0;
}

static inline int osd_canvas_valid(const struct osd_canvas *c)
{
    return c->base && c->width >= 2 && c->height >= 1 && !(c->width & 1u) &&
           c->width <= OSD_DRAW_MAX_DIM && c->height <= OSD_DRAW_MAX_DIM &&
           c->stride >= c->width && !(c->stride & 1u);
}

static inline int osd_clampi(int64_t v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : (int)v;
}

/* Fill the inclusive box [xa,xb] x [ya,yb], already inside the frame. */
static inline void osd_fill_box(struct osd_canvas *c, int xa, int ya, int xb,
                                int yb, uint32_t word)
{
    uint8_t y = (uint8_t)(word >> 16), u = (uint8_t)(word >> 8),
            v = (uint8_t)word;
    int cx0, cx1, row;

    if (xa > xb || ya > yb || xa < 0 || ya < 0 ||
        xb >= (int)c->width || yb >= (int)c->height)
        return;
    if (c->dry)
        goto band;
    cx0 = xa & ~1;
    cx1 = (xb & ~1) + 1;            /* width is even: always < width */
    for (row = ya; row <= yb; row++) {
        memset(c->base + (size_t)row * c->stride + xa, y, (size_t)(xb - xa + 1));
        /* both rows of a chroma pair write the same bytes: do it once */
        if (!(row & 1) || row == ya) {
            uint8_t *uv = c->base + c->uv_off + (size_t)(row >> 1) * c->stride;
            int x;

            for (x = cx0; x <= cx1; x += 2) {
                uv[x] = u;
                uv[x + 1] = v;
            }
        }
    }
band:
    if (ya < c->ymin)
        c->ymin = ya;
    if (yb > c->ymax)
        c->ymax = yb;
}

/* Range clip (the near-vertical box rows) */
static inline void osd_box_clip(struct osd_canvas *c, int64_t xa, int64_t ya,
                                int64_t xb, int64_t yb, uint32_t word)
{
    int w1 = (int)c->width - 1, h1 = (int)c->height - 1;

    if (xb < 0 || yb < 0 || xa > w1 || ya > h1)
        return;
    osd_fill_box(c, osd_clampi(xa, 0, w1), osd_clampi(ya, 0, h1),
                 osd_clampi(xb, 0, w1), osd_clampi(yb, 0, h1), word);
}

struct osd_box {
    int xa, ya, xb, yb;     /* inclusive, inside the frame */
};

/* lw x lw stamp whose top-left is (x - lw/2, y - lw/2); a start before the
 * frame is moved to 0 (the stamp keeps its width), as in stock. */
static inline struct osd_box osd_stamp_box(const struct osd_canvas *c, int64_t x,
                                           int64_t y, int lw)
{
    int w1 = (int)c->width - 1, h1 = (int)c->height - 1;
    struct osd_box b;

    b.xa = osd_clampi(x - lw / 2, 0, w1);
    b.ya = osd_clampi(y - lw / 2, 0, h1);
    b.xb = osd_clampi((int64_t)b.xa + lw - 1, 0, w1);
    b.yb = osd_clampi((int64_t)b.ya + lw - 1, 0, h1);
    return b;
}

static inline int osd_maxi(int a, int b) { return a > b ? a : b; }
static inline int osd_mini(int a, int b) { return a < b ? a : b; }

/* floor(n / d), d != 0 */
static inline int64_t osd_floordiv(int64_t n, int64_t d)
{
    int64_t q = n / d;

    if ((n % d) != 0 && ((n < 0) != (d < 0)))
        q--;
    return q;
}

/* Integer square root (floor) of a 64-bit value. Called once per diagonal
 * line (not per pixel) to scale the line direction. */
static inline uint64_t osd_isqrt64(uint64_t v)
{
    uint64_t res = 0, bit = 1ULL << 62;

    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= res + bit) {
            v -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

/* Length of the perpendicular stroke run along the minor axis when the major
 * axis advances by one pixel: t = round(lw * hypot(adx, ady) / max). A run of
 * t pixels projects onto the line normal as t * major / hypot, which is the
 * requested width lw (within half a pixel, the best an integer run can do).
 * hypot is evaluated in 1/1024 pixel units so that even a one-pixel 45 degree
 * step keeps the rounding right. */
static inline int osd_thick_span(int lw, int adx, int ady)
{
    int major = adx > ady ? adx : ady;
    uint64_t hq = osd_isqrt64(((uint64_t)adx * adx + (uint64_t)ady * ady) << 20);
    int64_t t = ((int64_t)lw * (int64_t)hq + ((int64_t)major << 9)) /
                ((int64_t)major << 10);

    if (t < 1)
        t = 1;
    if (t > OSD_DRAW_MAX_LW)
        t = OSD_DRAW_MAX_LW;
    return (int)t;
}

/* One column, rows [ya, yb] already inside the frame: the Y bytes plus the
 * chroma pair of every row, exactly as osd_fill_box does for a one-pixel-wide
 * box (so both rows of a chroma pair still write the same bytes). */
static inline void osd_fill_col(struct osd_canvas *c, int x, int ya, int yb,
                                uint32_t word)
{
    uint8_t y = (uint8_t)(word >> 16), u = (uint8_t)(word >> 8),
            v = (uint8_t)word;
    int row;

    if (c->dry)
        goto band;
    for (row = ya; row <= yb; row++) {
        c->base[(size_t)row * c->stride + x] = y;
        if (!(row & 1) || row == ya) {
            uint8_t *uv = c->base + c->uv_off +
                          (size_t)(row >> 1) * c->stride + (x & ~1);

            uv[0] = u;
            uv[1] = v;
        }
    }
band:
    if (ya < c->ymin)
        c->ymin = ya;
    if (yb > c->ymax)
        c->ymax = yb;
}

/* Diagonal stroke of uniform perpendicular width lw (lw >= 2; lw == 1 keeps
 * the stock path). The dominant axis is stepped from half a line width before
 * one end to half a line width past the other, the same end overhang the
 * lw x lw stamps give. The minor coordinate follows floor(slope * s) and is
 * clamped to the endpoints, so the overhang runs straight along the dominant
 * axis: the two ends are parallelogram corners, not perpendicular caps, and
 * stick out at most lw/2 + 1/2 pixels measured along the line normal. Every
 * step draws one run of osd_thick_span() pixels across the line, which makes
 * the drawn set a band of perpendicular width lw. The overhang also means that
 * only the interior steps keep the stock step count: the two ends add lw/2
 * each, i.e. up to lw + 1 steps in total.
 *
 * At lw == 1 the run is a single pixel and the overhang would add one step at
 * the end point, so p1 is excluded there exactly like in the stock loop (which
 * runs from p0 towards p1, p1 itself excluded) and the result stays byte
 * identical to the stock drawer. */
static inline void osd_draw_diagonal(struct osd_canvas *c, int ax, int ay,
                                     int bx, int by, int lw, uint32_t word)
{
    int w1 = (int)c->width - 1, h1 = (int)c->height - 1;
    int dx = bx - ax, dy = by - ay;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int lo = ax < bx ? ax : bx, hi = ax < bx ? bx : ax;
    int ylo = ay < by ? ay : by, yhi = ay < by ? by : ay;
    int i, t;

    if (adx >= ady) {
        /* shallow: a vertical run of t pixels per column */
        int start = lo - lw / 2, stop = hi + lw / 2;

        if (lw == 1) {          /* stock: p0 included, p1 excluded */
            if (dx > 0)
                stop = hi - 1;
            else
                start = lo + 1;
        }
        t = osd_thick_span(lw, adx, ady);
        for (i = start; i <= stop; i++) {
            int64_t yy, r0, r1;

            if (i < 0)
                continue;
            if (i > w1)
                break;
            yy = osd_clampi((int64_t)ay +
                                osd_floordiv((int64_t)dy * ((int64_t)i - ax), dx),
                            ylo, yhi);
            r0 = yy - t / 2;
            r1 = r0 + t - 1;
            if (r1 < 0 || r0 > h1)
                continue;
            osd_fill_col(c, i, r0 < 0 ? 0 : (int)r0,
                         r1 > h1 ? h1 : (int)r1, word);
        }
    } else {
        /* steep: a horizontal run of t pixels per row */
        int start = ylo - lw / 2, stop = yhi + lw / 2;

        if (lw == 1) {          /* stock: p0 included, p1 excluded */
            if (dy > 0)
                stop = yhi - 1;
            else
                start = ylo + 1;
        }
        t = osd_thick_span(lw, adx, ady);
        for (i = start; i <= stop; i++) {
            int64_t xx, c0, c1;

            if (i < 0)
                continue;
            if (i > h1)
                break;
            xx = osd_clampi((int64_t)ax +
                                osd_floordiv((int64_t)dx * ((int64_t)i - ay), dy),
                            lo, hi);
            c0 = xx - t / 2;
            c1 = c0 + t - 1;
            if (c1 < 0 || c0 > w1)
                continue;
            osd_fill_box(c, c0 < 0 ? 0 : (int)c0, i,
                         c1 > w1 ? w1 : (int)c1, i, word);
        }
    }
}

static inline void osd_draw_line(struct osd_canvas *c, int x0, int y0, int x1,
                                 int y1, uint32_t linewidth, uint32_t word,
                                 int offx, int offy)
{
    int w1 = (int)c->width - 1, h1 = (int)c->height - 1;
    int lw = linewidth > OSD_DRAW_MAX_LW ? OSD_DRAW_MAX_LW : (int)linewidth;
    int ax, ay, bx, by, dx, dy, adx, ady;

    /* stock osd_draw_line returns at once for p0 == p1 */
    if (lw <= 0 || (x0 == x1 && y0 == y1) || !osd_canvas_valid(c))
        return;
    /* clamp, add offPos, clamp again (stock order) */
    ax = osd_clampi((int64_t)osd_clampi(x0, 0, w1) + offx, 0, w1);
    ay = osd_clampi((int64_t)osd_clampi(y0, 0, h1) + offy, 0, h1);
    bx = osd_clampi((int64_t)osd_clampi(x1, 0, w1) + offx, 0, w1);
    by = osd_clampi((int64_t)osd_clampi(y1, 0, h1) + offy, 0, h1);
    dx = bx - ax;
    dy = by - ay;
    adx = dx < 0 ? -dx : dx;
    ady = dy < 0 ? -dy : dy;

    /* The axis-aligned cases keep the stock geometry byte for byte, and so
     * does any lw == 1 line: a single pixel has no width to distribute, and
     * the stock |dx| < 2*lw dispatch (|dx| <= 1 at lw == 1) plus the stock
     * step range that osd_draw_diagonal keeps for lw == 1 reproduce it
     * exactly. Every other line has a non-zero dx and dy and gets the uniform
     * band. */
    if (adx == 0 || ady == 0 || (lw == 1 && adx < 2 * lw)) {
        if (adx < 2 * lw) {
            /* near vertical: one box, columns centred on the midpoint (start
             * clamped first, so the box keeps its width at the border), rows
             * clipped */
            int mid = (ax + bx) / 2;
            int xs = osd_clampi((int64_t)mid - lw / 2, 0, w1);
            int ylo = ay < by ? ay : by, yhi = ay < by ? by : ay;

            osd_box_clip(c, xs, (int64_t)ylo - lw / 2, (int64_t)xs + lw - 1,
                         (int64_t)yhi - lw / 2 + lw - 1, word);
        } else {
            /* horizontal: the union of the stamps at p0 .. p1 (p1 excluded),
             * i.e. positions [pmin, pmax] */
            int pmin = ax < bx ? ax : bx + 1;
            int pmax = ax < bx ? bx - 1 : ax;
            struct osd_box lo = osd_stamp_box(c, pmin, ay, lw);
            struct osd_box hi = osd_stamp_box(c, pmax, ay, lw);

            osd_fill_box(c, lo.xa, lo.ya, hi.xb, lo.yb, word);
        }
        return;
    }
    osd_draw_diagonal(c, ax, ay, bx, by, lw, word);
}

static inline void osd_draw_rect(struct osd_canvas *c, int x0, int y0, int x1,
                                 int y1, uint32_t linewidth, uint32_t word,
                                 int offx, int offy)
{
    osd_draw_line(c, x0, y0, x1, y0, linewidth, word, offx, offy);
    osd_draw_line(c, x0, y0, x0, y1, linewidth, word, offx, offy);
    osd_draw_line(c, x0, y1, x1, y1, linewidth, word, offx, offy);
    osd_draw_line(c, x1, y0, x1, y1, linewidth, word, offx, offy);
}

/* 8-bit mask bitmap of bw x bh at frame position (x0,y0)+off: each non-zero
 * byte writes Y = byte and chroma 0x80 at byte x of chroma row y/2 (stock).
 * Clipped to the frame; the bitmap keeps its own row stride bw. (Stock
 * clamps the rect first and then reads the bitmap packed with the clipped
 * width from its first byte, which shears a bitmap that hangs over the
 * frame edge; for a bitmap inside the frame both are the same.) */
static inline void osd_draw_bitmap(struct osd_canvas *c, const uint8_t *bmp,
                                   int x0, int y0, uint32_t bw, uint32_t bh,
                                   int offx, int offy)
{
    int64_t ox = (int64_t)x0 + offx, oy = (int64_t)y0 + offy;
    int64_t row, col, r0, r1, c0, c1;

    if (!bmp || !bw || !bh || bw > OSD_DRAW_MAX_DIM || bh > OSD_DRAW_MAX_DIM ||
        !osd_canvas_valid(c))
        return;
    r0 = oy < 0 ? -oy : 0;
    r1 = oy + (int64_t)bh > (int64_t)c->height ? (int64_t)c->height - oy : (int64_t)bh;
    c0 = ox < 0 ? -ox : 0;
    c1 = ox + (int64_t)bw > (int64_t)c->width ? (int64_t)c->width - ox : (int64_t)bw;
    if (r0 >= r1 || c0 >= c1)
        return;
    for (row = c->dry ? r1 : r0; row < r1; row++) {
        uint8_t *yp = c->base + (size_t)(oy + row) * c->stride + ox;
        uint8_t *uv = c->base + c->uv_off + (size_t)((oy + row) >> 1) * c->stride + ox;
        const uint8_t *src = bmp + (size_t)row * bw;

        for (col = c0; col < c1; col++) {
            if (src[col]) {
                yp[col] = src[col];
                uv[col] = 0x80;
            }
        }
    }
    if ((int)(oy + r0) < c->ymin)
        c->ymin = (int)(oy + r0);
    if ((int)(oy + r1 - 1) > c->ymax)
        c->ymax = (int)(oy + r1 - 1);
}

#endif
