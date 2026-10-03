/* Host test for src/t31/openimp_t31_osd_draw.h: stock geometry of RECT/LINE,
 * NV12 Y+UV writes, clipping and guard-byte checks (no write outside the
 * visible area, whatever the coordinates). */
#include "t31/openimp_t31_osd_draw.h"
#include "osd_draw_old.h"       /* frozen stock drawer, used as the reference */

#include <limits.h>
#include <math.h>               /* cos/sin/lround: test only, never the drawer */
#include <stdio.h>
#include <stdlib.h>

static int failures;

#define CHECK(cond, ...) do {                                   \
        if (!(cond)) {                                          \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                       \
            fputc('\n', stderr);                                \
            failures++;                                         \
        }                                                       \
    } while (0)

#define W 64u
#define H 48u
#define STRIDE 80u          /* > W: stride padding must stay untouched */
#define GUARD 256u
#define WORD 0xff102030u    /* Y=0x10 U=0x20 V=0x30 */

struct frame {
    uint8_t *mem;
    uint8_t *base;
    size_t size;
};

static void frame_new(struct frame *f)
{
    size_t body = (size_t)STRIDE * H * 3 / 2;

    f->size = body + 2 * GUARD;
    f->mem = malloc(f->size);
    memset(f->mem, 0xA5, f->size);
    f->base = f->mem + GUARD;
    memset(f->base, 0, body);
}

static void canvas(struct frame *f, struct osd_canvas *c)
{
    osd_canvas_init(c, f->base, W, H, STRIDE, (size_t)STRIDE * H);
}

/* Guard bytes, row padding and (for the Y plane) the UV plane's padding. */
static void check_untouched(const struct frame *f, const char *what)
{
    size_t i, body = (size_t)STRIDE * H * 3 / 2;
    uint32_t y, x;

    for (i = 0; i < GUARD; i++) {
        CHECK(f->mem[i] == 0xA5, "%s: guard before touched", what);
        CHECK(f->mem[GUARD + body + i] == 0xA5, "%s: guard after touched", what);
    }
    for (y = 0; y < H * 3 / 2; y++)
        for (x = W; x < STRIDE; x++)
            CHECK(f->base[(size_t)y * STRIDE + x] == 0, "%s: padding (%u,%u)", what, x, y);
}

static int ypix(const struct frame *f, uint32_t x, uint32_t y)
{
    return f->base[(size_t)y * STRIDE + x];
}

static int count_y(const struct frame *f, int v)
{
    int n = 0;
    uint32_t x, y;

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++)
            n += ypix(f, x, y) == v;
    return n;
}

static void test_rect(void)
{
    struct frame f;
    struct osd_canvas c;
    uint32_t x, y;

    frame_new(&f);
    canvas(&f, &c);
    /* lw 3: each edge covers [p-1, p+1] */
    osd_draw_rect(&c, 10, 8, 40, 30, 3, WORD, 0, 0);
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) {
            int on_h = (y >= 7 && y <= 9) || (y >= 29 && y <= 31);
            int on_v = (x >= 9 && x <= 11) || (x >= 39 && x <= 41);
            int in_y = y >= 7 && y <= 31, in_x = x >= 9 && x <= 41;
            int want = (in_x && in_y && (on_h || on_v));

            CHECK(ypix(&f, x, y) == (want ? 0x10 : 0), "rect Y (%u,%u)=%d want %d",
                  x, y, ypix(&f, x, y), want);
        }
    }
    /* chroma: U,V pair at (x & ~1) of row y/2 */
    CHECK(f.base[(size_t)STRIDE * H + (size_t)(8 / 2) * STRIDE + 10] == 0x20, "U");
    CHECK(f.base[(size_t)STRIDE * H + (size_t)(8 / 2) * STRIDE + 11] == 0x30, "V");
    CHECK(f.base[(size_t)STRIDE * H + (size_t)(18 / 2) * STRIDE + 40] == 0x20, "U edge");
    CHECK(f.base[(size_t)STRIDE * H + (size_t)(18 / 2) * STRIDE + 20] == 0, "inside untouched");
    check_untouched(&f, "rect");
    CHECK(c.ymin == 7 && c.ymax == 31, "band %d..%d", c.ymin, c.ymax);
    free(f.mem);
}

static void test_offpos_and_clip(void)
{
    struct frame f;
    struct osd_canvas c;

    /* offPos moves the rect */
    frame_new(&f);
    canvas(&f, &c);
    osd_draw_rect(&c, 4, 4, 20, 20, 1, WORD, 10, 5);
    CHECK(ypix(&f, 14, 9) == 0x10 && ypix(&f, 30, 25) == 0x10, "offPos corner");
    CHECK(ypix(&f, 4, 4) == 0, "offPos old place empty");
    check_untouched(&f, "offpos");
    free(f.mem);

    /* partly / completely outside, extreme values: never out of bounds */
    {
        static const int vals[] = {INT_MIN, -100000, -65, -1, 0, 1, 31, 62, 63,
                                   64, 65, 100000, INT_MAX};
        unsigned a, b, d, e;

        for (a = 0; a < sizeof(vals) / sizeof(vals[0]); a++)
            for (b = 0; b < sizeof(vals) / sizeof(vals[0]); b++)
                for (d = 0; d < sizeof(vals) / sizeof(vals[0]); d += 2)
                    for (e = 0; e < sizeof(vals) / sizeof(vals[0]); e += 3) {
                        uint32_t lws[] = {0, 1, 2, 5, 255, 256, 1000, 0xffffffffu};
                        unsigned l;

                        frame_new(&f);
                        canvas(&f, &c);
                        for (l = 0; l < 8; l++) {
                            osd_draw_rect(&c, vals[a], vals[b], vals[d], vals[e],
                                          lws[l], WORD, vals[e], vals[a]);
                            osd_draw_line(&c, vals[a], vals[b], vals[d], vals[e],
                                          lws[l], WORD, vals[d], vals[b]);
                        }
                        check_untouched(&f, "extreme");
                        free(f.mem);
                    }
    }
}

static void test_line(void)
{
    struct frame f;
    struct osd_canvas c;
    uint32_t x;
    int n;

    /* horizontal, lw 2: rows [y-1, y], columns up to the end excluded + lw-1 */
    frame_new(&f);
    canvas(&f, &c);
    osd_draw_line(&c, 5, 20, 30, 20, 2, WORD, 0, 0);
    for (x = 0; x < W; x++)
        CHECK(ypix(&f, x, 19) == (x >= 4 && x <= 29 ? 0x10 : 0), "hline x=%u", x);
    CHECK(count_y(&f, 0x10) == 2 * 26, "hline count %d", count_y(&f, 0x10));
    check_untouched(&f, "hline");
    free(f.mem);

    /* exactly vertical (dy != 0, dx == 0): stock box centred on the mid x */
    frame_new(&f);
    canvas(&f, &c);
    osd_draw_line(&c, 11, 5, 11, 15, 4, WORD, 0, 0);
    CHECK(ypix(&f, 9, 4) == 0x10 && ypix(&f, 12, 4) == 0x10 && ypix(&f, 13, 4) == 0 &&
          ypix(&f, 8, 4) == 0, "vbox columns");
    CHECK(ypix(&f, 10, 2) == 0 && ypix(&f, 10, 3) == 0x10 &&
          ypix(&f, 10, 16) == 0x10 && ypix(&f, 10, 17) == 0,
          "vbox rows");
    check_untouched(&f, "vline");
    free(f.mem);

    /* diagonal: something is drawn, bounded to the endpoints' box + lw */
    frame_new(&f);
    canvas(&f, &c);
    osd_draw_line(&c, 5, 5, 45, 35, 2, WORD, 0, 0);
    n = count_y(&f, 0x10);
    CHECK(n > 60 && n < 400, "diagonal pixels %d", n);
    CHECK(ypix(&f, 25, 20) == 0x10, "diagonal midpoint");
    check_untouched(&f, "diag");
    free(f.mem);

    /* lw 0 draws nothing */
    frame_new(&f);
    canvas(&f, &c);
    osd_draw_line(&c, 5, 5, 45, 5, 0, WORD, 0, 0);
    CHECK(count_y(&f, 0x10) == 0 && c.ymax < c.ymin, "lw 0");
    free(f.mem);
}

static void test_border(void)
{
    struct frame f;
    struct osd_canvas c;

    /* a rect on the frame edge keeps its full width inside the frame */
    frame_new(&f);
    canvas(&f, &c);
    osd_draw_rect(&c, 0, 0, W - 1, H - 1, 4, WORD, 0, 0);
    CHECK(ypix(&f, 0, 0) == 0x10 && ypix(&f, 3, 20) == 0x10 && ypix(&f, 4, 20) == 0,
          "left border width");
    CHECK(ypix(&f, W - 1, H - 1) == 0x10 && ypix(&f, 20, H - 1) == 0x10, "far corner");
    check_untouched(&f, "border");
    free(f.mem);
}

static void test_bitmap(void)
{
    struct frame f;
    struct osd_canvas c;
    uint8_t bmp[8 * 4];
    unsigned i;

    for (i = 0; i < sizeof(bmp); i++)
        bmp[i] = (uint8_t)(i % 3 ? 0x40 + i : 0);
    frame_new(&f);
    canvas(&f, &c);
    osd_draw_bitmap(&c, bmp, 10, 6, 8, 4, 0, 0);
    CHECK(ypix(&f, 11, 6) == bmp[1] && ypix(&f, 10, 6) == 0 && ypix(&f, 17, 9) == bmp[31],
          "bitmap px %d %d", ypix(&f, 11, 6), ypix(&f, 17, 9));
    CHECK(f.base[(size_t)STRIDE * H + 3 * STRIDE + 11] == 0x80, "bitmap chroma");
    check_untouched(&f, "bitmap");
    free(f.mem);

    /* clipped on every side, including far outside */
    {
        static const int pos[] = {INT_MIN, -9, -4, 0, 60, 63, 64, 1000, INT_MAX};
        unsigned a, b;

        for (a = 0; a < sizeof(pos) / sizeof(pos[0]); a++)
            for (b = 0; b < sizeof(pos) / sizeof(pos[0]); b++) {
                frame_new(&f);
                canvas(&f, &c);
                osd_draw_bitmap(&c, bmp, pos[a], pos[b], 8, 4, pos[b], pos[a]);
                check_untouched(&f, "bitmap clip");
                free(f.mem);
            }
    }
    frame_new(&f);
    canvas(&f, &c);
    osd_draw_bitmap(&c, bmp, -3, -1, 8, 4, 0, 0);       /* clipped: bmp[1*8+3 ..] */
    CHECK(ypix(&f, 0, 0) == bmp[1 * 8 + 3] && ypix(&f, 1, 0) == bmp[1 * 8 + 4],
          "clipped bitmap origin");
    free(f.mem);
}

/* ------------------------------------------------------------------------- *
 * Axis-aligned geometry is unchanged: exact horizontal and vertical lines and
 * the four edges of a rect must still be byte identical to the frozen stock
 * drawer in osd_draw_old.h. Only the diagonals are allowed to differ.
 * ------------------------------------------------------------------------- */
static void test_axis_identity(void)
{
    static const uint32_t lws[] = {1, 2, 3, 4, 7, 9, 100, 0xffffffffu};
    unsigned seed = 20240607u, i;

    for (i = 0; i < 600; i++) {
        struct frame f, g;
        struct osd_canvas c, d;
        uint32_t lw;
        int x0, y0, x1, y1, offx, offy;

        seed = seed * 1103515245u + 12345u; x0 = (int)((seed >> 9) % 96u) - 16;
        seed = seed * 1103515245u + 12345u; y0 = (int)((seed >> 9) % 80u) - 16;
        seed = seed * 1103515245u + 12345u; x1 = (int)((seed >> 9) % 96u) - 16;
        seed = seed * 1103515245u + 12345u; y1 = (int)((seed >> 9) % 80u) - 16;
        seed = seed * 1103515245u + 12345u; offx = (int)((seed >> 9) % 9u) - 4;
        seed = seed * 1103515245u + 12345u; offy = (int)((seed >> 9) % 9u) - 4;
        seed = seed * 1103515245u + 12345u;
        lw = lws[(seed >> 9) % (sizeof(lws) / sizeof(lws[0]))];

        /* a rect is four axis-aligned edges in both implementations */
        frame_new(&f);
        frame_new(&g);
        canvas(&f, &c);
        canvas(&g, &d);
        osd_draw_rect(&c, x0, y0, x1, y1, lw, WORD, offx, offy);
        osd_draw_rect_old(&d, x0, y0, x1, y1, lw, WORD, offx, offy);
        CHECK(memcmp(f.mem, g.mem, f.size) == 0,
              "rect (%d,%d)-(%d,%d) lw %u off %d,%d changed", x0, y0, x1, y1,
              lw, offx, offy);
        CHECK(c.ymin == d.ymin && c.ymax == d.ymax, "rect band %d..%d vs %d..%d",
              c.ymin, c.ymax, d.ymin, d.ymax);
        free(f.mem);
        free(g.mem);

        /* half of the cases horizontal, half vertical: the clamp and offPos
         * cannot turn them into a diagonal */
        if (i & 1u)
            x1 = x0;
        else
            y1 = y0;
        frame_new(&f);
        frame_new(&g);
        canvas(&f, &c);
        canvas(&g, &d);
        osd_draw_line(&c, x0, y0, x1, y1, lw, WORD, offx, offy);
        osd_draw_line_old(&d, x0, y0, x1, y1, lw, WORD, offx, offy);
        CHECK(memcmp(f.mem, g.mem, f.size) == 0,
              "%s line (%d,%d)-(%d,%d) lw %u off %d,%d changed",
              (i & 1u) ? "vertical" : "horizontal", x0, y0, x1, y1, lw, offx, offy);
        CHECK(c.ymin == d.ymin && c.ymax == d.ymax, "line band %d..%d vs %d..%d",
              c.ymin, c.ymax, d.ymin, d.ymax);
        free(f.mem);
        free(g.mem);
    }
}

/* ------------------------------------------------------------------------- *
 * lw == 1 is one pixel per step: there is no width to distribute, and the
 * overhang of the band would add a step at the end point, so the stock routine
 * is kept for it - the stock |dx| < 2*lw dispatch (|dx| <= 1) and the stock
 * step range from p0 towards p1 with p1 excluded. Diagonal lines with lw == 1
 * must therefore still be byte identical to the frozen stock drawer, band
 * included.
 * ------------------------------------------------------------------------- */
static void check_lw1(int x0, int y0, int x1, int y1, int offx, int offy)
{
    struct frame f, g;
    struct osd_canvas c, d;

    frame_new(&f);
    frame_new(&g);
    canvas(&f, &c);
    canvas(&g, &d);
    osd_draw_line(&c, x0, y0, x1, y1, 1, WORD, offx, offy);
    osd_draw_line_old(&d, x0, y0, x1, y1, 1, WORD, offx, offy);
    CHECK(memcmp(f.mem, g.mem, f.size) == 0,
          "lw 1 line (%d,%d)-(%d,%d) off %d,%d changed", x0, y0, x1, y1, offx,
          offy);
    CHECK(c.ymin == d.ymin && c.ymax == d.ymax,
          "lw 1 line (%d,%d)-(%d,%d) off %d,%d band %d..%d vs %d..%d", x0, y0,
          x1, y1, offx, offy, c.ymin, c.ymax, d.ymin, d.ymax);
    free(f.mem);
    free(g.mem);
}

static void test_lw1_identity(void)
{
    /* corners, edges, one- and two-pixel dx (the stock box dispatch), 45
     * degrees, coordinates far outside and the int extremes */
    static const int fixed[][4] = {
        {0, 0, 63, 47}, {63, 47, 0, 0}, {-40, 10, 90, 60}, {70, -30, -20, 55},
        {0, 0, 1, 5}, {5, 1, 0, 0}, {0, 0, 2, 9}, {9, 2, 0, 0},
        {5, 5, 6, 6}, {31, 20, 32, 21}, {-1, -1, 65, 50},
        {-1000, -1000, 1000, 1000}, {INT_MIN, 3, INT_MAX, 44},
        {3, INT_MIN, 44, INT_MAX}, {INT_MIN, INT_MIN, INT_MAX, INT_MAX},
    };
    static const int offs[][2] = {{0, 0}, {1, -1}, {-3, 2}};
    unsigned seed = 20261003u, i, o;
    size_t k;

    for (k = 0; k < sizeof(fixed) / sizeof(fixed[0]); k++)
        for (o = 0; o < sizeof(offs) / sizeof(offs[0]); o++)
            check_lw1(fixed[k][0], fixed[k][1], fixed[k][2], fixed[k][3],
                      offs[o][0], offs[o][1]);

    /* 400 random diagonals inside the frame, at and over its edges */
    for (i = 0; i < 400; i++) {
        int x0, y0, x1, y1, offx, offy;

        seed = seed * 1103515245u + 12345u; x0 = (int)((seed >> 9) % 96u) - 16;
        seed = seed * 1103515245u + 12345u; y0 = (int)((seed >> 9) % 80u) - 16;
        seed = seed * 1103515245u + 12345u; x1 = (int)((seed >> 9) % 96u) - 16;
        seed = seed * 1103515245u + 12345u; y1 = (int)((seed >> 9) % 80u) - 16;
        seed = seed * 1103515245u + 12345u; offx = (int)((seed >> 9) % 9u) - 4;
        seed = seed * 1103515245u + 12345u; offy = (int)((seed >> 9) % 9u) - 4;
        if (x1 == x0)               /* keep both deltas non-zero: no axis case */
            x1 += 1;
        if (y1 == y0)
            y1 += 1;
        check_lw1(x0, y0, x1, y1, offx, offy);
    }
}

/* ------------------------------------------------------------------------- *
 * Uniform stroke thickness of the diagonals. The stock drawer advances one
 * lw x lw stamp per step of the dominant axis, so its cross section along the
 * minor axis is more than lw pixels wide: 2*lw-1 at 45 degrees (i.e.
 * (2*lw-1)/sqrt(2) = 1.41*lw across the line instead of lw) and still wider at
 * shallow/steep angles (10.4 px for lw 8 at 30 and 60 degrees). The band
 * drawer must stay within one pixel of lw everywhere; both are printed below.
 *
 * Measured on a 512 x 512 frame, far away from the clipping: a band of
 * perpendicular width lw crosses every dominant-axis step in a single run of
 * n pixels along the minor axis, and a run of n pixels projects onto the line
 * normal as n * major / hypot. The interior steps (one line width away from
 * the ends, where the run is clamped) are checked one by one: there the new
 * and the stock drawer step the same dominant axis, so the interior step
 * counts match. The ends do not: the band is extended by lw/2 at either end
 * like the stamps, so it can add up to lw + 1 steps, and those end rows form
 * parallelogram corners rather than perpendicular caps, up to lw/2 + 1/2
 * pixels past the normal.
 * ------------------------------------------------------------------------- */
#define BIGW 512u
#define BIGH 512u
#define BIGSTRIDE 512u
#define BIGBODY ((size_t)BIGSTRIDE * BIGH * 3 / 2)

struct bigframe {
    uint8_t *mem;
};

static void big_new(struct bigframe *b)
{
    b->mem = malloc(BIGBODY);
    memset(b->mem, 0, BIGBODY);
}

static void big_canvas(const struct bigframe *b, struct osd_canvas *c)
{
    osd_canvas_init(c, b->mem, BIGW, BIGH, BIGSTRIDE, (size_t)BIGSTRIDE * BIGH);
}

static size_t big_at(int x, int y) { return (size_t)y * BIGSTRIDE + x; }

static double seg_len(int adx, int ady)
{
    return (double)osd_isqrt64(((uint64_t)adx * adx + (uint64_t)ady * ady) << 20) /
           1024.0;
}

static double perp_width(int n, int adx, int ady)
{
    int major = adx > ady ? adx : ady;

    return (double)n * (double)major / seg_len(adx, ady);
}

/* Pixels of the single run in column x: -1 if the column holds more than one
 * run (a hole in the band), 0 if it holds none. */
static int col_run(const struct bigframe *b, int x, int *ya, int *yb)
{
    int y, n = 0, runs = 0, prev = -2;

    for (y = 0; y < (int)BIGH; y++) {
        if (b->mem[big_at(x, y)] != 0x10)
            continue;
        if (!n) {
            *ya = y;
            *yb = y;
            runs = 1;
        } else if (y == prev + 1) {
            *yb = y;
        } else {
            runs++;
        }
        prev = y;
        n++;
    }
    return runs > 1 ? -1 : n;
}

static int row_run(const struct bigframe *b, int y, int *xa, int *xb)
{
    int x, n = 0, runs = 0, prev = -2;

    for (x = 0; x < (int)BIGW; x++) {
        if (b->mem[big_at(x, y)] != 0x10)
            continue;
        if (!n) {
            *xa = x;
            *xb = x;
            runs = 1;
        } else if (x == prev + 1) {
            *xb = x;
        } else {
            runs++;
        }
        prev = x;
        n++;
    }
    return runs > 1 ? -1 : n;
}

static int big_count(const struct bigframe *b)
{
    int x, y, n = 0;

    for (y = 0; y < (int)BIGH; y++)
        for (x = 0; x < (int)BIGW; x++)
            n += b->mem[big_at(x, y)] == 0x10;
    return n;
}

static void measure(const struct bigframe *b, int x0, int y0, int x1, int y1,
                    int lw, double *pmin, double *pmax, double *pmean,
                    int *steps, int *holes)
{
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int lo, hi, i, m = lw + 4;

    *pmin = 1e30;
    *pmax = 0;
    *pmean = 0;
    *steps = 0;
    *holes = 0;
    if (adx >= ady) {
        lo = x0 < x1 ? x0 : x1;
        hi = x0 < x1 ? x1 : x0;
        for (i = lo + m; i <= hi - m; i++) {
            int ya, yb, n = col_run(b, i, &ya, &yb);
            double w;

            if (!n)
                continue;
            if (n < 0) {
                (*holes)++;
                continue;
            }
            w = perp_width(n, adx, ady);
            if (w < *pmin)
                *pmin = w;
            if (w > *pmax)
                *pmax = w;
            *pmean += w;
            (*steps)++;
        }
    } else {
        lo = y0 < y1 ? y0 : y1;
        hi = y0 < y1 ? y1 : y0;
        for (i = lo + m; i <= hi - m; i++) {
            int xa, xb, n = row_run(b, i, &xa, &xb);
            double w;

            if (!n)
                continue;
            if (n < 0) {
                (*holes)++;
                continue;
            }
            w = perp_width(n, adx, ady);
            if (w < *pmin)
                *pmin = w;
            if (w > *pmax)
                *pmax = w;
            *pmean += w;
            (*steps)++;
        }
    }
    if (*steps)
        *pmean /= *steps;
}

/* Outside the two straight caps nothing may be drawn further away from the
 * line than half a line width plus the half pixel the integer rasterisation
 * can add: 4 * cross^2 <= (lw + 3)^2 * hypot^2 with
 * cross = dy*(x-x0) - dx*(y-y0) tests exactly that without floats. The caps
 * are left out: there the run is clamped to the endpoint and the intended
 * half-line-width overhang reaches a little past the band around the
 * infinite line. */
static void check_no_stray(const struct bigframe *b, int x0, int y0, int x1, int y1,
                           int lw)
{
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int64_t h2 = (int64_t)adx * adx + (int64_t)ady * ady;
    int64_t lim = (int64_t)(lw + 3) * (lw + 3) * h2;
    int m = lw + 4, lo, hi, x, y, bad = 0;

    if (adx >= ady) {
        lo = (x0 < x1 ? x0 : x1) + m;
        hi = (x0 < x1 ? x1 : x0) - m;
    } else {
        lo = (y0 < y1 ? y0 : y1) + m;
        hi = (y0 < y1 ? y1 : y0) - m;
    }
    for (y = 0; y < (int)BIGH; y++)
        for (x = 0; x < (int)BIGW; x++) {
            int64_t cross;
            int c = adx >= ady ? x : y;

            if (c < lo || c > hi)
                continue;
            if (b->mem[big_at(x, y)] != 0x10)
                continue;
            cross = (int64_t)dy * (x - x0) - (int64_t)dx * (y - y0);
            if (4 * cross * cross > lim)
                bad++;
        }
    CHECK(bad == 0, "line (%d,%d)-(%d,%d) lw %d: %d pixel(s) outside the band",
          x0, y0, x1, y1, lw, bad);
}

static void test_diagonal_thickness(void)
{
    static const int seg[3][4] = {
        {120, 120, 420, 420},   /* 45 degrees */
        {120, 160, 420, 333},   /* 30 degrees, dy/dx = 173/300 */
        {160, 120, 260, 293},   /* 60 degrees, dy/dx = 173/100 */
    };
    static const char *deg[3] = {"45", "30", "60"};
    static const int lws[] = {1, 2, 4, 8};
    unsigned s, l;

    puts("diagonal stroke thickness (perpendicular, interior steps):");
    for (s = 0; s < 3; s++)
        for (l = 0; l < sizeof(lws) / sizeof(lws[0]); l++) {
            struct bigframe nf, of;
            struct osd_canvas c;
            double nmin, nmax, nmean, omin, omax, omean;
            int nsteps, nholes, osteps, oholes, lw = lws[l];
            int x0 = seg[s][0], y0 = seg[s][1], x1 = seg[s][2], y1 = seg[s][3];
            int adx = x1 - x0, ady = y1 - y0, area, use;

            big_new(&nf);
            big_new(&of);
            big_canvas(&nf, &c);
            osd_draw_line(&c, x0, y0, x1, y1, (uint32_t)lw, WORD, 0, 0);
            big_canvas(&of, &c);
            osd_draw_line_old(&c, x0, y0, x1, y1, (uint32_t)lw, WORD, 0, 0);

            measure(&nf, x0, y0, x1, y1, lw, &nmin, &nmax, &nmean, &nsteps, &nholes);
            measure(&of, x0, y0, x1, y1, lw, &omin, &omax, &omean, &osteps, &oholes);
            printf("  %s deg lw %d over %d steps: new %.2f..%.2f (mean %.2f), "
                   "old %.2f..%.2f (mean %.2f)\n", deg[s], lw, nsteps, nmin, nmax,
                   nmean, omin, omax, omean);

            CHECK(nsteps > 100, "%s deg lw %d: only %d step(s) measured", deg[s], lw,
                  nsteps);
            CHECK(nholes == 0, "%s deg lw %d: %d step(s) not one contiguous run",
                  deg[s], lw, nholes);
            /* the requested tolerance */
            CHECK(nmin >= lw - 1.0 && nmax <= lw + 1.0,
                  "%s deg lw %d: perpendicular width %.2f..%.2f outside lw +- 1",
                  deg[s], lw, nmin, nmax);
            /* the same stroke on every step: no taper along the line */
            CHECK(nmax - nmin <= 1.0, "%s deg lw %d: width varies %.2f..%.2f",
                  deg[s], lw, nmin, nmax);
            CHECK(osteps == nsteps,
                  "%s deg lw %d: interior steps %d vs old %d", deg[s], lw,
                  nsteps, osteps);
            check_no_stray(&nf, x0, y0, x1, y1, lw);

            /* covered area / length is the mean width including the caps */
            area = big_count(&nf);
            use = (int)((double)lw * seg_len(adx, ady));
            CHECK(area >= use - (int)seg_len(adx, ady) &&
                  area <= use + (int)seg_len(adx, ady),
                  "%s deg lw %d: %d pixels, expected about %d", deg[s], lw, area, use);
            free(nf.mem);
            free(of.mem);
        }
}

/* ------------------------------------------------------------------------- *
 * Fixed sweep over the whole angle range: 0..180 degrees in one-degree steps
 * for lw 2..8, the segment 200 px long in the middle of the 512 x 512 frame so
 * that neither the ends nor the clipping enter the measurement. For every
 * angle and width the perpendicular width of the band must stay within half a
 * pixel of lw (the run length is an integer rounded to the nearest pixel, so
 * half a pixel is the best that can be achieved) and every interior step must
 * be covered by exactly one run - no gaps, no split runs.
 * ------------------------------------------------------------------------- */
static void test_angle_sweep(void)
{
    static const int lws[] = {2, 3, 4, 5, 6, 7, 8};
    const double len = 100.0, pi = 3.14159265358979323846;
    const int cx = (int)BIGW / 2, cy = (int)BIGH / 2;
    unsigned l;
    int ang;
    double worst = 0;

    for (l = 0; l < sizeof(lws) / sizeof(lws[0]); l++)
        for (ang = 0; ang <= 180; ang++) {
            struct bigframe nf;
            struct osd_canvas c;
            double a = (double)ang * pi / 180.0;
            double nmin, nmax, nmean;
            int nsteps, nholes, lw = lws[l];
            int x0 = (int)lround((double)cx - len * cos(a));
            int y0 = (int)lround((double)cy - len * sin(a));
            int x1 = (int)lround((double)cx + len * cos(a));
            int y1 = (int)lround((double)cy + len * sin(a));
            int adx = x1 - x0 < 0 ? x0 - x1 : x1 - x0;
            int ady = y1 - y0 < 0 ? y0 - y1 : y1 - y0;
            int major = adx > ady ? adx : ady;
            int m = lw + 4;
            int expect = major - 2 * m + 1;

            big_new(&nf);
            big_canvas(&nf, &c);
            osd_draw_line(&c, x0, y0, x1, y1, (uint32_t)lw, WORD, 0, 0);
            measure(&nf, x0, y0, x1, y1, lw, &nmin, &nmax, &nmean, &nsteps,
                    &nholes);
            free(nf.mem);

            CHECK(expect > 50 && nsteps == expect,
                  "sweep %d deg lw %d: %d of %d interior steps covered, width "
                  "%.2f..%.2f", ang, lw, nsteps, expect, nmin, nmax);
            CHECK(nholes == 0, "sweep %d deg lw %d: %d step(s) not one run",
                  ang, lw, nholes);
            CHECK(nmin >= lw - 0.500001 && nmax <= lw + 0.500001,
                  "sweep %d deg lw %d: perpendicular width %.3f..%.3f outside "
                  "lw +- 0.5", ang, lw, nmin, nmax);
            if (nsteps == expect) {
                double dev = nmax - lw;

                if ((double)lw - nmin > dev)
                    dev = (double)lw - nmin;
                if (dev > worst)
                    worst = dev;
            }
        }
    printf("angle sweep 0..180 deg, lw 2..8: worst |width - lw| = %.3f px\n",
           worst);
}

/* Diagonals that leave the frame on every side, with caps that extend past the
 * edge: nothing outside the visible area, and the rows of a dry pass must
 * cover every row the real pass writes. */
static void test_diagonal_clip_dry(void)
{
    static const int seg[][4] = {
        {-30, -30, 40, 70}, {-30, -30, 90, 40}, {70, -10, -20, 60},
        {70, 60, -20, -10}, {0, 0, (int)W - 1, (int)H - 1},
        {(int)W - 1, 0, 0, (int)H - 1}, {-1000, 5, 1000, 20}, {5, -1000, 20, 1000},
        {-1000, -1000, 1000, 1000}, {INT_MIN, INT_MIN, INT_MAX, INT_MAX},
        {INT_MIN, 20, INT_MAX, 40}, {20, INT_MIN, 40, INT_MAX},
        {30, 10, 31, 11}, {-1, -1, 1, 1},
    };
    static const uint32_t lws[] = {1, 2, 3, 8, 0xffffffffu};
    size_t i;
    unsigned l;

    for (i = 0; i < sizeof(seg) / sizeof(seg[0]); i++)
        for (l = 0; l < sizeof(lws) / sizeof(lws[0]); l++) {
            struct frame f, g;
            struct osd_canvas c, d;
            uint32_t y;

            frame_new(&f);
            frame_new(&g);
            canvas(&f, &c);
            canvas(&g, &d);
            d.dry = 1;
            osd_draw_line(&c, seg[i][0], seg[i][1], seg[i][2], seg[i][3], lws[l],
                          WORD, 1, -1);
            osd_draw_line(&d, seg[i][0], seg[i][1], seg[i][2], seg[i][3], lws[l],
                          WORD, 1, -1);
            for (y = 0; y < H; y++) {
                uint32_t x;
                int any = 0;

                for (x = 0; x < W; x++)
                    any |= ypix(&f, x, y) == 0x10;
                CHECK(!any || ((int)y >= d.ymin && (int)y <= d.ymax),
                      "diagonal (%d,%d)-(%d,%d) lw %u: row %u outside dry band "
                      "%d..%d", seg[i][0], seg[i][1], seg[i][2], seg[i][3], lws[l],
                      y, d.ymin, d.ymax);
            }
            CHECK(count_y(&g, 0x10) == 0, "dry diagonal wrote Y");
            check_untouched(&f, "diagonal clip");
            check_untouched(&g, "diagonal dry");
            free(f.mem);
            free(g.mem);
        }
}

static void test_stock_details(void)
{
    struct frame f, g;
    struct osd_canvas c;
    uint32_t x;

    /* p0 == p1: stock draws nothing */
    frame_new(&f);
    canvas(&f, &c);
    osd_draw_line(&c, 20, 20, 20, 20, 5, WORD, 0, 0);
    CHECK(count_y(&f, 0x10) == 0 && c.ymax < c.ymin, "point line drawn");
    free(f.mem);

    /* right-to-left horizontal: positions p0 .. p1+1 */
    frame_new(&f);
    canvas(&f, &c);
    osd_draw_line(&c, 30, 20, 5, 20, 2, WORD, 0, 0);
    for (x = 0; x < W; x++)
        CHECK(ypix(&f, x, 19) == (x >= 5 && x <= 30 ? 0x10 : 0), "rtl hline x=%u", x);
    free(f.mem);

    /* zero-width rect: the two point edges vanish, the vertical ones stay */
    frame_new(&f);
    canvas(&f, &c);
    osd_draw_rect(&c, 20, 10, 20, 30, 2, WORD, 0, 0);
    CHECK(ypix(&f, 19, 9) == 0x10 && ypix(&f, 20, 30) == 0x10 &&
          ypix(&f, 19, 31) == 0 && ypix(&f, 21, 20) == 0, "zero-width rect");
    free(f.mem);

    /* raw coordinates are clamped before offPos is added */
    frame_new(&f);
    canvas(&f, &c);
    osd_draw_line(&c, 5, 1000, 40, 1000, 1, WORD, 0, -10);
    CHECK(ypix(&f, 10, H - 11) == 0x10 && count_y(&f, 0x10) == 35, "pre-offset clamp");
    free(f.mem);

    /* any lw >= 2 * frame size gives the same frame as the cap */
    frame_new(&f);
    frame_new(&g);
    canvas(&f, &c);
    osd_draw_line(&c, 3, 4, 50, 40, 0xffffffffu, WORD, 0, 0);
    canvas(&g, &c);
    osd_draw_line(&c, 3, 4, 50, 40, 2 * W, WORD, 0, 0);
    CHECK(memcmp(f.mem, g.mem, f.size) == 0 && count_y(&f, 0x10) == (int)(W * H),
          "huge lw");
    free(f.mem);
    free(g.mem);
}

static void test_dry(void)
{
    struct frame f, g;
    struct osd_canvas c, d;
    uint8_t bmp[16];

    memset(bmp, 0x50, sizeof(bmp));
    frame_new(&f);
    frame_new(&g);
    canvas(&f, &c);
    canvas(&g, &d);
    d.dry = 1;
    osd_draw_rect(&c, 6, 7, 40, 20, 3, WORD, 2, 1);
    osd_draw_line(&c, 1, 40, 60, 30, 2, WORD, 0, 0);
    osd_draw_bitmap(&c, bmp, 50, -2, 4, 4, 0, 0);
    osd_draw_rect(&d, 6, 7, 40, 20, 3, WORD, 2, 1);
    osd_draw_line(&d, 1, 40, 60, 30, 2, WORD, 0, 0);
    osd_draw_bitmap(&d, bmp, 50, -2, 4, 4, 0, 0);
    CHECK(c.ymin == d.ymin && c.ymax == d.ymax, "dry band %d..%d vs %d..%d",
          d.ymin, d.ymax, c.ymin, c.ymax);
    CHECK(count_y(&g, 0x10) == 0 && count_y(&g, 0x50) == 0, "dry run wrote");
    check_untouched(&g, "dry");
    free(f.mem);
    free(g.mem);
}

static void test_invalid_canvas(void)
{
    struct osd_canvas c;
    uint8_t b[64];

    osd_canvas_init(&c, b, 7, 4, 8, 32);       /* odd width */
    osd_draw_line(&c, 0, 0, 5, 0, 1, WORD, 0, 0);
    osd_canvas_init(&c, b, 8, 4, 6, 32);       /* stride < width */
    osd_draw_line(&c, 0, 0, 5, 0, 1, WORD, 0, 0);
    osd_canvas_init(&c, NULL, 8, 4, 8, 32);
    osd_draw_rect(&c, 0, 0, 5, 3, 1, WORD, 0, 0);
}

int main(void)
{
    test_rect();
    test_offpos_and_clip();
    test_line();
    test_border();
    test_bitmap();
    test_axis_identity();
    test_lw1_identity();
    test_diagonal_thickness();
    test_angle_sweep();
    test_diagonal_clip_dry();
    test_stock_details();
    test_dry();
    test_invalid_canvas();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("osd_draw_test: ok");
    return 0;
}
