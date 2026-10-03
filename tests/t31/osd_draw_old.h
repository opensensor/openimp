/* Frozen copy of the line/rect drawer of src/t31/openimp_t31_osd_draw.h as of
 * commit 4c1ffe1, i.e. the stock lw x lw stamp staircase including the
 * incremental-union helper osd_fill_new that the uniform-thickness change
 * made dead in the real header. Kept out of the production header and used by
 * the host tests only:
 *  - reference for the byte-identity checks of the axis-aligned lines and of
 *    the rectangles (those keep the stock geometry),
 *  - reference for the before/after thickness table (osd_draw_test.c) and for
 *    the before/after benchmark (osd_thick_bench.c).
 * It shares the unchanged primitives with the current header, so it is the old
 * code and not a re-implementation. */
#ifndef OPENIMP_T31_OSD_DRAW_OLD_H
#define OPENIMP_T31_OSD_DRAW_OLD_H

#include "t31/openimp_t31_osd_draw.h"

/* Fill n minus o (o == NULL: all of n). The union of all stamps is the same
 * as stamping each one fully, since every pixel of n not written here is in
 * o, which was written before. */
static inline void osd_fill_new_old(struct osd_canvas *c, const struct osd_box *n,
                                    const struct osd_box *o, uint32_t word)
{
    int ox0, ox1;

    if (!o || n->xa > o->xb || n->xb < o->xa || n->ya > o->yb || n->yb < o->ya) {
        osd_fill_box(c, n->xa, n->ya, n->xb, n->yb, word);
        return;
    }
    /* columns of n outside o, all rows of n */
    osd_fill_box(c, n->xa, n->ya, osd_mini(n->xb, o->xa - 1), n->yb, word);
    osd_fill_box(c, osd_maxi(n->xa, o->xb + 1), n->ya, n->xb, n->yb, word);
    /* columns shared with o, rows of n outside o */
    ox0 = osd_maxi(n->xa, o->xa);
    ox1 = osd_mini(n->xb, o->xb);
    osd_fill_box(c, ox0, n->ya, ox1, osd_mini(n->yb, o->ya - 1), word);
    osd_fill_box(c, ox0, osd_maxi(n->ya, o->yb + 1), ox1, n->yb, word);
}

static inline void osd_draw_line_old(struct osd_canvas *c, int x0, int y0, int x1,
                                     int y1, uint32_t linewidth, uint32_t word,
                                     int offx, int offy)
{
    int w1 = (int)c->width - 1, h1 = (int)c->height - 1;
    int lw = linewidth > OSD_DRAW_MAX_LW ? OSD_DRAW_MAX_LW : (int)linewidth;
    int ax, ay, bx, by, dx, dy, adx, ady, k;
    struct osd_box prev = {0, 0, -1, -1}, cur;

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

    if (adx < 2 * lw) {
        /* near vertical: one box, columns centred on the midpoint (start
         * clamped first, so the box keeps its width at the border), rows
         * clipped */
        int mid = (ax + bx) / 2;
        int xs = osd_clampi((int64_t)mid - lw / 2, 0, w1);
        int ylo = ay < by ? ay : by, yhi = ay < by ? by : ay;

        osd_box_clip(c, xs, (int64_t)ylo - lw / 2, (int64_t)xs + lw - 1,
                     (int64_t)yhi - lw / 2 + lw - 1, word);
        return;
    }
    /* here adx >= 2 * lw > 0 */
    if (ady == 0) {
        /* horizontal: the union of the stamps at p0 .. p1 (p1 excluded),
         * i.e. positions [pmin, pmax] */
        int pmin = ax < bx ? ax : bx + 1;
        int pmax = ax < bx ? bx - 1 : ax;
        struct osd_box lo = osd_stamp_box(c, pmin, ay, lw);
        struct osd_box hi = osd_stamp_box(c, pmax, ay, lw);

        osd_fill_box(c, lo.xa, lo.ya, hi.xb, lo.yb, word);
        return;
    }
    if (ady < adx) {
        int dir = dx > 0 ? 1 : -1;

        for (k = 0; k < adx; k++) {
            int s = k * dir;

            cur = osd_stamp_box(c, (int64_t)ax + s,
                                ay + osd_floordiv((int64_t)dy * s, dx), lw);
            osd_fill_new_old(c, &cur, k ? &prev : NULL, word);
            prev = cur;
        }
    } else {
        int dir = dy > 0 ? 1 : -1;

        for (k = 0; k < ady; k++) {
            int s = k * dir;

            cur = osd_stamp_box(c, ax + osd_floordiv((int64_t)dx * s, dy),
                                (int64_t)ay + s, lw);
            osd_fill_new_old(c, &cur, k ? &prev : NULL, word);
            prev = cur;
        }
    }
}

static inline void osd_draw_rect_old(struct osd_canvas *c, int x0, int y0, int x1,
                                     int y1, uint32_t linewidth, uint32_t word,
                                     int offx, int offy)
{
    osd_draw_line_old(c, x0, y0, x1, y0, linewidth, word, offx, offy);
    osd_draw_line_old(c, x0, y0, x0, y1, linewidth, word, offx, offy);
    osd_draw_line_old(c, x0, y1, x1, y1, linewidth, word, offx, offy);
    osd_draw_line_old(c, x1, y0, x1, y1, linewidth, word, offx, offy);
}

#endif
