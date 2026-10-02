/* CPU drawing for OSD_REG_LINE, OSD_REG_RECT and OSD_REG_BITMAP on NV12
 * frames (T31, T20, T21, T30 share this).  Header-only and free of hardware
 * dependencies so it can be unit tested on the host (tests/t31/osd_draw_test.c).
 *
 * Semantics follow the stock libimp osd_draw_line / OSD bitmap path
 * (libimp.so_hlil.txt, docs/T31_OSD_RE.md section 3.6):
 *  - opaque writes: Y byte per pixel, U,V pair at (x & ~1) of chroma row y/2
 *  - colour is the stock word 0xAAYYUUVV (alpha ignored when drawing)
 *  - line width lw: a pixel position p covers [p - lw/2, p - lw/2 + lw - 1]
 *  - endpoints (plus the group offPos) are clamped to the frame
 *  - near-vertical lines (|dx| < 2*lw) are one filled box centred on the
 *    midpoint x; other lines are drawn by stepping the dominant axis and
 *    stamping lw x lw squares, the end position itself excluded
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
};

/* Line widths above this are clamped (stock has no limit; this only bounds
 * the per-frame cost of a bogus value). */
#define OSD_DRAW_MAX_LW 256

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
}

static inline int osd_canvas_valid(const struct osd_canvas *c)
{
    return c->base && c->width >= 2 && c->height >= 1 && !(c->width & 1u) &&
           c->width <= 8192 && c->height <= 8192 &&
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

/* lw x lw stamp whose top-left is (x - lw/2, y - lw/2); a start before the
 * frame is moved to 0 (the stamp keeps its width), as in stock. */
static inline void osd_stamp(struct osd_canvas *c, int64_t x, int64_t y, int lw,
                             uint32_t word)
{
    int w1 = (int)c->width - 1, h1 = (int)c->height - 1;
    int xs = osd_clampi(x - lw / 2, 0, w1), ys = osd_clampi(y - lw / 2, 0, h1);
    int xe = osd_clampi((int64_t)xs + lw - 1, 0, w1);
    int ye = osd_clampi((int64_t)ys + lw - 1, 0, h1);

    osd_fill_box(c, xs, ys, xe, ye, word);
}

static inline void osd_draw_line(struct osd_canvas *c, int x0, int y0, int x1,
                                 int y1, uint32_t linewidth, uint32_t word,
                                 int offx, int offy)
{
    int w1 = (int)c->width - 1, h1 = (int)c->height - 1;
    int lw = linewidth > OSD_DRAW_MAX_LW ? OSD_DRAW_MAX_LW : (int)linewidth;
    int ax, ay, bx, by, dx, dy, adx, ady, k;

    if (lw <= 0 || !osd_canvas_valid(c))
        return;
    ax = osd_clampi((int64_t)x0 + offx, 0, w1);
    ay = osd_clampi((int64_t)y0 + offy, 0, h1);
    bx = osd_clampi((int64_t)x1 + offx, 0, w1);
    by = osd_clampi((int64_t)y1 + offy, 0, h1);
    dx = bx - ax;
    dy = by - ay;
    adx = dx < 0 ? -dx : dx;
    ady = dy < 0 ? -dy : dy;

    if (adx < 2 * lw) {
        /* near vertical: one box, columns centred on the midpoint (start
         * clamped first, so the box keeps its width at the border), rows
         * clipped */
        int mid = (ax + bx) / 2;
        int xs = osd_clampi(mid - lw / 2, 0, w1);
        int ylo = ay < by ? ay : by, yhi = ay < by ? by : ay;

        osd_box_clip(c, xs, (int64_t)ylo - lw / 2, (int64_t)xs + lw - 1,
                     (int64_t)yhi - lw / 2 + lw - 1, word);
        return;
    }
    if (ady == 0) {
        /* horizontal: the union of the stamps along the line */
        int pmin = ax < bx ? ax : bx;
        int pmax = (ax < bx ? bx : ax) - 1;     /* end position excluded */
        int xs = osd_clampi((int64_t)pmin - lw / 2, 0, w1);
        int xe = osd_clampi((int64_t)osd_clampi((int64_t)pmax - lw / 2, 0, w1) +
                            lw - 1, 0, w1);
        int ys = osd_clampi((int64_t)ay - lw / 2, 0, h1);
        int ye = osd_clampi((int64_t)ys + lw - 1, 0, h1);

        osd_fill_box(c, xs, ys, xe, ye, word);
        return;
    }
    if (ady < adx) {
        int dir = dx > 0 ? 1 : -1;

        for (k = 0; k < adx; k++)
            osd_stamp(c, ax + k * dir, ay + (int64_t)dy * (k * dir) / dx, lw, word);
    } else {
        int dir = dy > 0 ? 1 : -1;

        for (k = 0; k < ady; k++)
            osd_stamp(c, ax + (int64_t)dx * (k * dir) / dy, ay + k * dir, lw, word);
    }
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
 * byte writes Y = byte and chroma 0x80 (stock). Clipped to the frame; the
 * bitmap keeps its own row stride bw. */
static inline void osd_draw_bitmap(struct osd_canvas *c, const uint8_t *bmp,
                                   int x0, int y0, uint32_t bw, uint32_t bh,
                                   int offx, int offy)
{
    int64_t ox = (int64_t)x0 + offx, oy = (int64_t)y0 + offy;
    int64_t row, col, r0, r1, c0, c1;

    if (!bmp || !bw || !bh || bw > 8192 || bh > 8192 || !osd_canvas_valid(c))
        return;
    r0 = oy < 0 ? -oy : 0;
    r1 = oy + (int64_t)bh > (int64_t)c->height ? (int64_t)c->height - oy : (int64_t)bh;
    c0 = ox < 0 ? -ox : 0;
    c1 = ox + (int64_t)bw > (int64_t)c->width ? (int64_t)c->width - ox : (int64_t)bw;
    if (r0 >= r1 || c0 >= c1)
        return;
    for (row = r0; row < r1; row++) {
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
