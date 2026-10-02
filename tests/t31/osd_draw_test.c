/* Host test for src/t31/openimp_t31_osd_draw.h: stock geometry of RECT/LINE,
 * NV12 Y+UV writes, clipping and guard-byte checks (no write outside the
 * visible area, whatever the coordinates). */
#include "t31/openimp_t31_osd_draw.h"

#include <limits.h>
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

    /* near vertical (dx < 2*lw) is a box centred on the mid x */
    frame_new(&f);
    canvas(&f, &c);
    osd_draw_line(&c, 10, 5, 12, 15, 4, WORD, 0, 0);
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

/* Reference: stock-style full lw x lw stamps at every step (no incremental
 * union), same floor rounding. The fast drawer must give identical frames. */
static void ref_line(struct frame *f, int x0, int y0, int x1, int y1, int lw)
{
    int w1 = (int)W - 1, h1 = (int)H - 1;
    int ax = x0 < 0 ? 0 : x0 > w1 ? w1 : x0, ay = y0 < 0 ? 0 : y0 > h1 ? h1 : y0;
    int bx = x1 < 0 ? 0 : x1 > w1 ? w1 : x1, by = y1 < 0 ? 0 : y1 > h1 ? h1 : y1;
    int dx = bx - ax, dy = by - ay, adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    int n = adx > ady ? adx : ady, k;

    for (k = 0; k < n; k++) {
        int px, py, xs, ys, xe, ye, x, y;

        if (ady < adx) {
            int s = dx > 0 ? k : -k;

            px = ax + s;
            py = ay + (int)osd_floordiv((int64_t)dy * s, dx);
        } else {
            int s = dy > 0 ? k : -k;

            py = ay + s;
            px = ax + (int)osd_floordiv((int64_t)dx * s, dy);
        }
        xs = osd_clampi((int64_t)px - lw / 2, 0, w1);
        ys = osd_clampi((int64_t)py - lw / 2, 0, h1);
        xe = osd_clampi((int64_t)xs + lw - 1, 0, w1);
        ye = osd_clampi((int64_t)ys + lw - 1, 0, h1);
        for (y = ys; y <= ye; y++)
            for (x = xs; x <= xe; x++) {
                f->base[(size_t)y * STRIDE + x] = 0x10;
                f->base[(size_t)STRIDE * H + (size_t)(y / 2) * STRIDE + (x & ~1)] = 0x20;
                f->base[(size_t)STRIDE * H + (size_t)(y / 2) * STRIDE + (x & ~1) + 1] = 0x30;
            }
    }
}

static void test_line_reference(void)
{
    static const int lws[] = {1, 2, 3, 4, 7};
    unsigned seed = 12345, i, l;

    for (i = 0; i < 400; i++) {
        for (l = 0; l < sizeof(lws) / sizeof(lws[0]); l++) {
            struct frame f, g;
            struct osd_canvas c;
            int x0, y0, x1, y1, dx;

            seed = seed * 1103515245u + 12345u;
            x0 = (int)(seed >> 8) % 80 - 8;
            seed = seed * 1103515245u + 12345u;
            y0 = (int)(seed >> 8) % 64 - 8;
            seed = seed * 1103515245u + 12345u;
            x1 = (int)(seed >> 8) % 80 - 8;
            seed = seed * 1103515245u + 12345u;
            y1 = (int)(seed >> 8) % 64 - 8;
            dx = osd_clampi(x1, 0, W - 1) - osd_clampi(x0, 0, W - 1);
            if (dx < 0)
                dx = -dx;
            if (dx < 2 * lws[l] || (x0 == x1 && y0 == y1))
                continue;           /* box path, checked elsewhere */
            frame_new(&f);
            frame_new(&g);
            canvas(&f, &c);
            osd_draw_line(&c, x0, y0, x1, y1, (uint32_t)lws[l], WORD, 0, 0);
            ref_line(&g, x0, y0, x1, y1, lws[l]);
            CHECK(memcmp(f.mem, g.mem, f.size) == 0,
                  "line (%d,%d)-(%d,%d) lw %d differs from full stamps",
                  x0, y0, x1, y1, lws[l]);
            free(f.mem);
            free(g.mem);
        }
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
    test_line_reference();
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
