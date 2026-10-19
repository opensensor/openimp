/*
 * Synthetic NV12 test pattern (grey ramp, checkerboard, colour bars).
 *
 * Shared by tools/t31_hwjpeg_probe.c and the FrameSource test hook
 * (OPENIMP_TEST_PATTERN=1, T40/T41): orientation, chroma order, range and
 * block alignment errors are visible at a glance, and a host script can
 * regenerate the exact source picture to compare a decoded stream with.
 *
 * Luma: top third a horizontal ramp 0..255, middle third a 32 px
 * checkerboard 16/235, bottom third a vertical ramp 0..255.
 * Chroma: the top sixth neutral, below it eight vertical bars
 * (U,V) = white, yellow, cyan, green, magenta, red, blue, white.
 *
 * OPENIMP_TEST_PATTERN=2 adds a moving element on top of the same pattern
 * (openimp_testpattern_overlay): a black square (Y 16, U=V=84, a chroma pair
 * not used by the bars) in the bottom third, bouncing horizontally by
 * OPENIMP_TEST_PATTERN_STEP px per frame (default 8, made even), and the
 * frame counter n (per channel, counts the frames handed out) as 16 binary
 * cells of 16x16 px in the top left corner, MSB first, 1 = Y 235, 0 = Y 16.
 * OPENIMP_TEST_PATTERN=3 is the motion detection test: a flat grey picture (Y 110, light per-frame
 * noise OPENIMP_TEST_PATTERN_NOISE, default 1 LSB peak, 0 = none), the centred logo and the frame
 * counter, plus script-controlled objects from the file OPENIMP_TEST_PATTERN_EVENTS (default
 * /tmp/openimp_tp_events), one per line:
 *     t=<s> cell=<cx>,<cy> size=<px> contrast=<dY> [dur=<s>] [speed=<px/s>]
 * t from the first frame of the process; the grid is 8x6 cells; size is in pixels of a 640 wide
 * frame (scaled with the width); contrast is added to Y in the object's rectangle, centred in the
 * cell; speed moves it horizontally inside the cell.  The hook logs "[TP] ev <i> start|stop <ms>"
 * (CLOCK_MONOTONIC ms, same clock as tools/motiontest) to stderr.
 * The host recomputes the expected picture from the counter it reads out of
 * the decoded frame (tools/testpattern/tbmotion.c).
 */
#ifndef OPENIMP_TESTPATTERN_GEN_H
#define OPENIMP_TESTPATTERN_GEN_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "openimp_testpattern_logo.h"
#include "../include/openimp_testpattern.h"


/* ---- centred logo (OpenIMP banner) on a white quiet zone ----
 * Logo height Lh = 15 % of the picture height (even), width from the banner aspect (at most 60 % of the
 * width), quiet zone 18 % of Lh on every side, box centred.  The box lies inside the middle (checkerboard)
 * third, so the ramps and the colour bars stay measurable; the host evaluation uses the same generator
 * (ideal picture contains the logo) and can mask the box with this function.  Returns 0 when the picture
 * is too small.  *bw, *bh include the quiet zone, *q is its width. */
static inline int openimp_testpattern_logo_box(uint32_t w, uint32_t h, uint32_t *x0, uint32_t *y0,
                                               uint32_t *bw, uint32_t *bh, uint32_t *q)
{
    uint32_t lh = (h * 15u / 100u) & ~1u, lw, qq;

    if (h < 96 || w < 128)
        return 0;
    if (lh < 48)
        lh = 48;
    lw = (lh * OPENIMP_LOGO_W / OPENIMP_LOGO_H) & ~1u;
    if (lw > w * 60u / 100u) {
        lw = (w * 60u / 100u) & ~1u;
        lh = (lw * OPENIMP_LOGO_H / OPENIMP_LOGO_W) & ~1u;
    }
    qq = (lh * 18u / 100u) & ~1u;
    *bw = lw + 2u * qq;
    *bh = lh + 2u * qq;
    if (lh < 24 || *bh > h * 2u / 3u || *bw > w)
        return 0;
    *q = qq;
    *x0 = ((w - *bw) / 2u) & ~1u;
    *y0 = ((h - *bh) / 2u) & ~1u;
    return 1;
}

/* 8.8 fixed point bilinear sample of an nw x nh plane at (fx, fy) in 1/256 source pixels (pixel centres) */
static inline int openimp_tp_bil(const uint8_t *p, int nw, int nh, int fx, int fy)
{
    int x0, y0, x1, y1, wx, wy, a, b;

    if (fx < 0) fx = 0;
    if (fy < 0) fy = 0;
    if (fx > (nw - 1) * 256) fx = (nw - 1) * 256;
    if (fy > (nh - 1) * 256) fy = (nh - 1) * 256;
    x0 = fx >> 8; wx = fx & 255; y0 = fy >> 8; wy = fy & 255;
    x1 = x0 + 1 < nw ? x0 + 1 : x0;
    y1 = y0 + 1 < nh ? y0 + 1 : y0;
    a = p[y0 * nw + x0] * (256 - wx) + p[y0 * nw + x1] * wx;
    b = p[y1 * nw + x0] * (256 - wx) + p[y1 * nw + x1] * wx;
    return (a * (256 - wy) + b * wy + 32768) >> 16;
}

struct openimp_tp_tile {
    int state;                        /* 0 empty, 1 building, 2 ready */
    uint32_t w, h, logo;
    uint32_t x0, y0, bw, bh;
    uint8_t *y, *uv;                  /* bw*bh, bw*bh/2 (interleaved U V) */
};

/* unpack the run-length coded master logo into palette indices (W*H) */
static inline void openimp_tp_unpack(const uint8_t *rl, size_t len, uint8_t *idx)
{
    size_t o = 0, i;
    const size_t n = OPENIMP_LOGO_W, total = n * OPENIMP_LOGO_H;

    for (i = 0; i < len && o < total; i++) {
        uint8_t t = rl[i];
        size_t r;

        if (t & 0x80) {
            idx[o++] = t & 0x1f;
        } else if (t & 0x40) {
            for (r = (size_t)(t & 63) + 1; r && o < total; r--, o++)
                idx[o] = o >= n ? idx[o - n] : 0;
        } else {
            for (r = (size_t)(t & 63) + 1; r && o < total; r--, o++)
                idx[o] = o ? idx[o - 1] : 0;
        }
    }
}

/* Builds the tile once per (size, logo): unpacks the master into temporary planes and scales it
 * (2x2 sub-samples of a bilinear lookup = area average when shrinking) into the quiet-zone box. */
static inline void openimp_tp_tile_build(struct openimp_tp_tile *t, uint32_t w, uint32_t h)
{
    uint8_t *idx, *ly, *lu, *lv;
    uint32_t q, bw, bh, x0, y0, lw, lh, i, j, k;
    const int NW = OPENIMP_LOGO_W, NH = OPENIMP_LOGO_H;

    if (!openimp_testpattern_logo_box(w, h, &x0, &y0, &bw, &bh, &q))
        return;
    lw = bw - 2u * q;
    lh = bh - 2u * q;
    idx = (uint8_t *)malloc((size_t)NW * NH * 2u + (size_t)NW * NH / 2u);
    t->y = (uint8_t *)malloc((size_t)bw * bh);
    t->uv = (uint8_t *)malloc((size_t)bw * bh / 2u);
    if (!idx || !t->y || !t->uv) {
        free(idx); free(t->y); free(t->uv); t->y = t->uv = NULL;
        return;
    }
    ly = idx + (size_t)NW * NH;
    lu = ly + (size_t)NW * NH;
    lv = lu + (size_t)NW * NH / 4u;
    openimp_tp_unpack(openimp_logo_rle, OPENIMP_LOGO_RLE_LEN, idx);
    for (j = 0; j < (uint32_t)NW * NH; j++)
        ly[j] = openimp_logo_pal[3 * (idx[j] & 31u)];
    for (j = 0; j < (uint32_t)NH / 2; j++)
        for (i = 0; i < (uint32_t)NW / 2; i++) {
            int su = 0, sv = 0;
            for (k = 0; k < 4; k++) {
                const uint8_t *e = openimp_logo_pal + 3 * (idx[(2u * j + (k >> 1)) * NW + 2u * i + (k & 1)] & 31u);
                su += e[1]; sv += e[2];
            }
            lu[j * (NW / 2) + i] = (uint8_t)((su + 2) / 4);
            lv[j * (NW / 2) + i] = (uint8_t)((sv + 2) / 4);
        }
    memset(t->y, 235, (size_t)bw * bh);
    memset(t->uv, 128, (size_t)bw * bh / 2u);
    for (j = 0; j < lh; j++)
        for (i = 0; i < lw; i++) {
            int sum = 0;
            for (k = 0; k < 4; k++) {
                int fx = (int)((4u * i + 2u * (k & 1) + 1u) * NW * 64u / lw) - 128;
                int fy = (int)((4u * j + 2u * (k >> 1) + 1u) * NH * 64u / lh) - 128;
                sum += openimp_tp_bil(ly, NW, NH, fx, fy);
            }
            t->y[(size_t)(q + j) * bw + q + i] = (uint8_t)((sum + 2) / 4);
        }
    for (j = 0; j < lh / 2; j++)
        for (i = 0; i < lw / 2; i++) {
            int fx = (int)((2u * i + 1u) * (NW / 2) * 128u / (lw / 2u)) - 128;
            int fy = (int)((2u * j + 1u) * (NH / 2) * 128u / (lh / 2u)) - 128;
            t->uv[(size_t)(q / 2 + j) * bw + q + 2 * i] = (uint8_t)openimp_tp_bil(lu, NW / 2, NH / 2, fx, fy);
            t->uv[(size_t)(q / 2 + j) * bw + q + 2 * i + 1] = (uint8_t)openimp_tp_bil(lv, NW / 2, NH / 2, fx, fy);
        }
    free(idx);
    t->x0 = x0; t->y0 = y0; t->bw = bw; t->bh = bh;
}

/* cached tile for (w, h, logo); lock free after the first build, NULL = no logo */
static inline const struct openimp_tp_tile *openimp_tp_tile_get(uint32_t w, uint32_t h, uint32_t logo)
{
    static struct openimp_tp_tile slot[16];
    int i;

    if (logo != 1)
        return NULL;
    for (i = 0; i < 16; i++) {
        struct openimp_tp_tile *t = &slot[i];
        int st = __atomic_load_n(&t->state, __ATOMIC_ACQUIRE);
        int zero = 0;

        if (st == 0 && __atomic_compare_exchange_n(&t->state, &zero, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            t->w = w; t->h = h; t->logo = logo;
            openimp_tp_tile_build(t, w, h);
            __atomic_store_n(&t->state, 2, __ATOMIC_RELEASE);
            return t->y ? t : NULL;
        }
        while ((st = __atomic_load_n(&t->state, __ATOMIC_ACQUIRE)) == 1)
            ;
        if (st == 2 && t->w == w && t->h == h && t->logo == logo)
            return t->y ? t : NULL;
    }
    return NULL;
}

static inline void openimp_testpattern_logo(uint8_t *y, uint8_t *uv, uint32_t w, uint32_t h,
                                            uint32_t pitch, uint32_t logo)
{
    const struct openimp_tp_tile *t = openimp_tp_tile_get(w, h, logo);
    uint32_t r;

    if (!t)
        return;
    for (r = 0; r < t->bh; r++)
        memcpy(y + (size_t)(t->y0 + r) * pitch + t->x0, t->y + (size_t)r * t->bw, t->bw);
    for (r = 0; r < t->bh / 2u; r++)
        memcpy(uv + (size_t)(t->y0 / 2u + r) * pitch + t->x0, t->uv + (size_t)r * t->bw, t->bw);
}

static inline void openimp_testpattern_nv12_logo(uint8_t *y, uint8_t *uv,
                                                 uint32_t w, uint32_t h,
                                                 uint32_t pitch, uint32_t logo)
{
    static const uint8_t bars_u[8] = {128, 16, 166, 54, 202, 90, 240, 128};
    static const uint8_t bars_v[8] = {128, 146, 16, 34, 222, 240, 110, 128};
    uint32_t r, c;

    if (w < 2 || h < 2)
        return;
    for (r = 0; r < h; r++)
        for (c = 0; c < w; c++) {
            uint8_t v;
            if (r < h / 3)
                v = (uint8_t)(c * 255 / (w - 1));
            else if (r < 2 * h / 3)
                v = (uint8_t)(((c / 32) ^ (r / 32)) & 1 ? 235 : 16);
            else
                v = (uint8_t)(r * 255 / (h - 1));
            y[(size_t)r * pitch + c] = v;
        }
    for (r = 0; r < h / 2; r++)
        for (c = 0; c < w / 2; c++) {
            unsigned bar = c * 8 / (w / 2);
            uv[(size_t)r * pitch + 2 * c] = r < h / 6 ? 128 : bars_u[bar];
            uv[(size_t)r * pitch + 2 * c + 1] = r < h / 6 ? 128 : bars_v[bar];
        }
    openimp_testpattern_logo(y, uv, w, h, pitch, logo);
}

/* default logo (OpenIMP banner); host tools may select with OPENIMP_TP_LOGO=0..1 */
static inline void openimp_testpattern_nv12(uint8_t *y, uint8_t *uv,
                                            uint32_t w, uint32_t h,
                                            uint32_t pitch)
{
    const char *e = getenv("OPENIMP_TP_LOGO");

    openimp_testpattern_nv12_logo(y, uv, w, h, pitch, e ? (uint32_t)atoi(e) : 1u);
}

/* square size, top row and left position for frame counter n (bounces) */
static inline void openimp_testpattern_square(uint32_t w, uint32_t h,
                                              uint32_t n, uint32_t step,
                                              uint32_t *size, uint32_t *y0,
                                              uint32_t *x0)
{
    uint32_t s = (h / 8u) & ~15u, span, pos;

    if (s < 32)
        s = 32;
    span = w > s ? w - s : 1u;
    pos = (uint32_t)(((uint64_t)n * step) % (2u * span));
    if (pos > span)
        pos = 2u * span - pos;
    *size = s;
    *y0 = (h * 3u / 4u) & ~1u;
    *x0 = pos & ~1u;
}

/* draw the moving square and the counter cells over a finished pattern */
static inline void openimp_testpattern_overlay(uint8_t *y, uint8_t *uv,
                                               uint32_t w, uint32_t h,
                                               uint32_t pitch, uint32_t n,
                                               uint32_t step)
{
    uint32_t s, y0, x0, r, c, b;

    if (w < 320 || h < 96)
        return;
    openimp_testpattern_square(w, h, n, step, &s, &y0, &x0);
    for (r = 0; r < s && y0 + r < h; r++)
        for (c = 0; c < s; c++)
            y[(size_t)(y0 + r) * pitch + x0 + c] = 16;
    for (r = 0; r < s / 2 && y0 / 2 + r < h / 2; r++)
        for (c = 0; c < s / 2; c++) {
            uv[(size_t)(y0 / 2 + r) * pitch + x0 + 2 * c] = 84;
            uv[(size_t)(y0 / 2 + r) * pitch + x0 + 2 * c + 1] = 84;
        }
    for (r = 0; r < 16; r++)
        for (b = 0; b < 16; b++)
            for (c = 0; c < 16; c++)
                y[(size_t)r * pitch + b * 16u + c] =
                    ((n >> (15u - b)) & 1u) ? 235 : 16;
}


/* ---- mode 3: motion detection test ---- */
#define OPENIMP_TP_GX 8
#define OPENIMP_TP_GY 6
#define OPENIMP_TP_MAXEV 128
struct openimp_tp_event { double t, dur, speed; int cx, cy, size, contrast, state; };

/* counters the caller owns (read by OpenIMP_TestPattern_Get) */
struct openimp_tp_stats { uint64_t objects_shown; uint32_t objects_active; };

static inline int64_t openimp_tp_now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* (re)load the events file when its path or mtime changed; returns the count */
static inline int openimp_tp_events(struct openimp_tp_event *ev, const char *path, int reset)
{
    static int n;
    static time_t mt;
    static int64_t last_check;
    static char loaded[160];
    int64_t now = openimp_tp_now_ms();
    struct stat st;

    if (reset) {
        last_check = 0;
        loaded[0] = 0;
        mt = 0;
        n = 0;
    }
    if (now - last_check < 500)
        return n;
    last_check = now;
    if (!path || !path[0])
        path = getenv("OPENIMP_TEST_PATTERN_EVENTS");
    if (!path || !path[0])
        path = "/tmp/openimp_tp_events";
    if (stat(path, &st) == 0 && (st.st_mtime != mt || strcmp(path, loaded))) {
        FILE *f = fopen(path, "r");
        char line[200];
        mt = st.st_mtime;
        snprintf(loaded, sizeof loaded, "%s", path);
        n = 0;
        while (f && fgets(line, sizeof line, f) && n < OPENIMP_TP_MAXEV) {
            struct openimp_tp_event e = { 0, 3, 0, 0, 0, 32, 40, 0 };
            char *p;
            if (!(p = strstr(line, "t=")))
                continue;
            e.t = atof(p + 2);
            if ((p = strstr(line, "cell=")))
                sscanf(p + 5, "%d,%d", &e.cx, &e.cy);
            if ((p = strstr(line, "size=")))
                e.size = atoi(p + 5);
            if ((p = strstr(line, "contrast=")))
                e.contrast = atoi(p + 9);
            if ((p = strstr(line, "dur=")))
                e.dur = atof(p + 4);
            if ((p = strstr(line, "speed=")))
                e.speed = atof(p + 6);
            ev[n++] = e;
        }
        if (f)
            fclose(f);
    } else if (stat(path, &st) != 0) {
        n = 0;
    }
    return n;
}

/* add contrast to Y in a size x size rectangle in grid cell (cx, cy), bouncing by speed (px/s of 640) */
static inline void openimp_tp_object(uint8_t *y, uint32_t w, uint32_t h, uint32_t pitch,
                                     int cx, int cy, int size640, int contrast, double speed, double ts)
{
    uint32_t cw = w / OPENIMP_TP_GX, ch = h / OPENIMP_TP_GY;
    int size = (int)((uint32_t)size640 * w / 640u);
    int ox = 0, x0, y0, rr, cc;

    if (size < 2 || size > (int)cw || size > (int)ch)
        return;
    if (speed > 0) {
        double range = (double)(cw - (uint32_t)size) / 2.0, ph, d;
        if (range < 1)
            range = 1;
        d = ts * speed * w / 640.0;
        ph = d - 4.0 * range * (double)(long)(d / (4.0 * range));
        ox = (int)(ph < 2 * range ? ph - range : 3 * range - ph);
    }
    x0 = (int)(cx * cw + cw / 2) - size / 2 + ox;
    y0 = (int)(cy * ch + ch / 2) - size / 2;
    for (rr = 0; rr < size; rr++)
        for (cc = 0; cc < size; cc++) {
            int v;
            if (y0 + rr < 0 || y0 + rr >= (int)h || x0 + cc < 0 || x0 + cc >= (int)w)
                continue;
            v = y[(size_t)(y0 + rr) * pitch + x0 + cc] + contrast;
            y[(size_t)(y0 + rr) * pitch + x0 + cc] = (uint8_t)(v < 16 ? 16 : v > 235 ? 235 : v);
        }
}

/* fill y/uv (pitch) with the mode 3 picture for the frame counter n.  epoch changes with every
 * OpenIMP_TestPattern_Set: script and built-in object times restart from that moment. */
static inline void openimp_testpattern_motion3_p(uint8_t *y, uint8_t *uv,
                                                 uint32_t w, uint32_t h,
                                                 uint32_t pitch, uint32_t n,
                                                 const OpenIMPTestPatternParams *p, uint32_t epoch,
                                                 struct openimp_tp_stats *st)
{
    static struct openimp_tp_event ev[OPENIMP_TP_MAXEV];
    static int64_t t0;
    static uint32_t rng = 12345u, last_epoch = ~0u;
    static int bstate;
    int amp = p->noise, count, i, reset = 0, active_n = 0;
    int64_t now = openimp_tp_now_ms();
    uint32_t r, c;

    if (amp > 16)
        amp = 16;
    if (!t0 || epoch != last_epoch) {
        t0 = now;
        last_epoch = epoch;
        bstate = 0;
        reset = 1;
        memset(ev, 0, sizeof ev);
    }
    for (r = 0; r < h; r++) {
        uint8_t *row = y + (size_t)r * pitch;
        if (amp > 0)
            for (c = 0; c < w; c++) {
                rng = rng * 1664525u + 1013904223u;
                row[c] = (uint8_t)(110 + (int)((rng >> 24) % (2u * amp + 1u)) - amp);
            }
        else
            memset(row, 110, w);
    }
    for (r = 0; r < h / 2; r++)
        memset(uv + (size_t)r * pitch, 128, w);
    openimp_testpattern_logo(y, uv, w, h, pitch, p->logo);
    for (r = 0; r < 16 && h >= 96; r++)
        for (c = 0; c < 16; c++) {
            uint32_t b;
            for (b = 0; b < 16 && w >= 320; b++)
                y[(size_t)r * pitch + b * 16u + c] = ((n >> (15u - b)) & 1u) ? 235 : 16;
        }
    count = openimp_tp_events(ev, p->script, reset);
    for (i = 0; i < count; i++) {
        double ts = (double)(now - t0) / 1000.0;
        int active = ts >= ev[i].t && ts < ev[i].t + ev[i].dur;

        if (active != ev[i].state) {
            ev[i].state = active;
            if (active && st)
                st->objects_shown++;
            fprintf(stderr, "[TP] ev %d %s %lld cell=%d,%d size=%d contrast=%d speed=%g\n", i,
                    active ? "start" : "stop", (long long)now, ev[i].cx, ev[i].cy,
                    ev[i].size, ev[i].contrast, ev[i].speed);
        }
        if (!active)
            continue;
        active_n++;
        openimp_tp_object(y, w, h, pitch, ev[i].cx, ev[i].cy, ev[i].size, ev[i].contrast,
                          ev[i].speed, ts - ev[i].t);
    }
    if (p->obj_size) {   /* built-in object: visible obj_on_ms of every obj_period_ms */
        int64_t ms = now - t0;
        int on = p->obj_period_ms == 0 || (uint64_t)(ms % p->obj_period_ms) < p->obj_on_ms;

        if (on != bstate) {
            bstate = on;
            if (on && st)
                st->objects_shown++;
            fprintf(stderr, "[TP] ev obj %s %lld cell=%u,%u size=%u contrast=%d speed=%u\n",
                    on ? "start" : "stop", (long long)now, p->obj_cell_x, p->obj_cell_y,
                    p->obj_size, p->obj_contrast, p->obj_speed);
        }
        if (on) {
            active_n++;
            openimp_tp_object(y, w, h, pitch, (int)p->obj_cell_x, (int)p->obj_cell_y, (int)p->obj_size,
                              p->obj_contrast, p->obj_speed,
                              (double)(p->obj_period_ms ? ms % p->obj_period_ms : ms) / 1000.0);
        }
    }
    if (st)
        st->objects_active = (uint32_t)active_n;
}

/* host tools / default parameters */
static inline void openimp_testpattern_motion3(uint8_t *y, uint8_t *uv,
                                               uint32_t w, uint32_t h,
                                               uint32_t pitch, uint32_t n)
{
    OpenIMPTestPatternParams p;
    const char *ne = getenv("OPENIMP_TEST_PATTERN_NOISE");

    OpenIMP_TestPattern_ParamsInit(&p);
    if (ne)
        p.noise = atoi(ne);
    openimp_testpattern_motion3_p(y, uv, w, h, pitch, n, &p, 0, NULL);
}

#endif
