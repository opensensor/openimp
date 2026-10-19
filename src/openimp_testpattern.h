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
 * noise OPENIMP_TEST_PATTERN_NOISE, default 1 LSB peak, 0 = none), the two logos and the frame
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
#ifndef OPENIMP_TESTPATTERN_H
#define OPENIMP_TESTPATTERN_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "openimp_testpattern_logo.h"


/* geometry of the two logos (OpenIMP, open-tx-isp) side by side in the top third; 0 = does not fit */
static inline uint32_t openimp_testpattern_logo_geom(uint32_t w, uint32_t h,
                                                     uint32_t *x0, uint32_t *y0)
{
    uint32_t l = (h / 8u) & ~1u;

    if (l < 64)
        l = 64;
    *x0 = 16;
    *y0 = 24;
    if (*y0 + l > h / 3u || 2u * l + 48u > w)
        return 0;
    return l;
}

/* nearest-neighbour scaled logos; chroma from the mean colour of each 2x2 block (BT.601 limited) */
static inline void openimp_testpattern_logos(uint8_t *y, uint8_t *uv,
                                             uint32_t w, uint32_t h,
                                             uint32_t pitch)
{
    uint32_t x0, y0, l = openimp_testpattern_logo_geom(w, h, &x0, &y0), k, r, c;

    for (k = 0; l && k < 2; k++) {
        const uint8_t *bm = k ? openimp_logo_isp : openimp_logo_imp;
        uint32_t bx = x0 + k * (l + 16u);
        for (r = 0; r < l; r++)
            for (c = 0; c < l; c++) {
                const uint8_t *q = openimp_logo_pal[bm[(r * OPENIMP_LOGO_N / l) * OPENIMP_LOGO_N +
                                                       c * OPENIMP_LOGO_N / l]];
                y[(size_t)(y0 + r) * pitch + bx + c] =
                    (uint8_t)(16 + (65481 * q[0] + 128553 * q[1] + 24966 * q[2]) / 255000);
            }
        for (r = 0; r < l; r += 2)
            for (c = 0; c < l; c += 2) {
                int rs = 0, gs = 0, bs = 0, i;
                for (i = 0; i < 4; i++) {
                    uint32_t rr = r + (i >> 1), cc = c + (i & 1);
                    const uint8_t *q = openimp_logo_pal[bm[(rr * OPENIMP_LOGO_N / l) * OPENIMP_LOGO_N +
                                                           cc * OPENIMP_LOGO_N / l]];
                    rs += q[0]; gs += q[1]; bs += q[2];
                }
                rs /= 4; gs /= 4; bs /= 4;
                uv[(size_t)((y0 + r) / 2) * pitch + bx + c] =
                    (uint8_t)(128 + (-37797 * rs - 74203 * gs + 112000 * bs) / 255000);
                uv[(size_t)((y0 + r) / 2) * pitch + bx + c + 1] =
                    (uint8_t)(128 + (112000 * rs - 93786 * gs - 18214 * bs) / 255000);
            }
    }
}

static inline void openimp_testpattern_nv12(uint8_t *y, uint8_t *uv,
                                            uint32_t w, uint32_t h,
                                            uint32_t pitch)
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
    openimp_testpattern_logos(y, uv, w, h, pitch);
}


/* 0 = off, 1 = static pattern, 2 = + moving element, 3 = motion detection test */
static inline int openimp_testpattern_mode(void)
{
    static int mode = -1;

    if (mode < 0) {
        const char *env = getenv("OPENIMP_TEST_PATTERN");
        mode = (env && env[0] >= '1' && env[0] <= '3') ? env[0] - '0' : 0;
    }
    return mode;
}

static inline uint32_t openimp_testpattern_step(void)
{
    static uint32_t step;

    if (!step) {
        const char *env = getenv("OPENIMP_TEST_PATTERN_STEP");
        int v = env ? atoi(env) : 8;
        step = (uint32_t)(v < 2 ? 2 : v) & ~1u;
    }
    return step;
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

static inline int64_t openimp_tp_now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* (re)load the events file when its mtime changed; returns the count */
static inline int openimp_tp_events(struct openimp_tp_event *ev)
{
    static int n;
    static time_t mt;
    static int64_t last_check;
    const char *path = getenv("OPENIMP_TEST_PATTERN_EVENTS");
    int64_t now = openimp_tp_now_ms();
    struct stat st;

    if (now - last_check < 500)
        return n;
    last_check = now;
    if (!path)
        path = "/tmp/openimp_tp_events";
    if (stat(path, &st) == 0 && st.st_mtime != mt) {
        FILE *f = fopen(path, "r");
        char line[200];
        mt = st.st_mtime;
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
    }
    return n;
}

/* fill y/uv (pitch) with the mode 3 picture for the frame counter n */
static inline void openimp_testpattern_motion3(uint8_t *y, uint8_t *uv,
                                               uint32_t w, uint32_t h,
                                               uint32_t pitch, uint32_t n)
{
    static struct openimp_tp_event ev[OPENIMP_TP_MAXEV];
    static int64_t t0;
    static uint32_t rng = 12345u;
    const char *ne = getenv("OPENIMP_TEST_PATTERN_NOISE");
    int amp = ne ? atoi(ne) : 1, count, i;
    int64_t now = openimp_tp_now_ms();
    uint32_t r, c;

    if (!t0)
        t0 = now;
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
    openimp_testpattern_logos(y, uv, w, h, pitch);
    for (r = 0; r < 16 && h >= 96; r++)
        for (c = 0; c < 16; c++) {
            uint32_t b;
            for (b = 0; b < 16 && w >= 320; b++)
                y[(size_t)r * pitch + b * 16u + c] = ((n >> (15u - b)) & 1u) ? 235 : 16;
        }
    count = openimp_tp_events(ev);
    for (i = 0; i < count; i++) {
        double ts = (double)(now - t0) / 1000.0;
        int active = ts >= ev[i].t && ts < ev[i].t + ev[i].dur;
        uint32_t cw = w / OPENIMP_TP_GX, ch = h / OPENIMP_TP_GY;
        int size = (int)((uint32_t)ev[i].size * w / 640u);
        int ox = 0, x0, y0, rr, cc;

        if (active != ev[i].state) {
            ev[i].state = active;
            fprintf(stderr, "[TP] ev %d %s %lld cell=%d,%d size=%d contrast=%d speed=%g\n", i,
                    active ? "start" : "stop", (long long)now, ev[i].cx, ev[i].cy,
                    ev[i].size, ev[i].contrast, ev[i].speed);
        }
        if (!active || size < 2 || size > (int)cw || size > (int)ch)
            continue;
        if (ev[i].speed > 0) {
            double range = (double)(cw - (uint32_t)size) / 2.0, ph, d;
            if (range < 1)
                range = 1;
            d = (ts - ev[i].t) * ev[i].speed * w / 640.0;
            ph = d - 4.0 * range * (double)(long)(d / (4.0 * range));
            ox = (int)(ph < 2 * range ? ph - range : 3 * range - ph);
        }
        x0 = (int)(ev[i].cx * cw + cw / 2) - size / 2 + ox;
        y0 = (int)(ev[i].cy * ch + ch / 2) - size / 2;
        for (rr = 0; rr < size; rr++)
            for (cc = 0; cc < size; cc++) {
                int v;
                if (y0 + rr < 0 || y0 + rr >= (int)h || x0 + cc < 0 || x0 + cc >= (int)w)
                    continue;
                v = y[(size_t)(y0 + rr) * pitch + x0 + cc] + ev[i].contrast;
                y[(size_t)(y0 + rr) * pitch + x0 + cc] = (uint8_t)(v < 16 ? 16 : v > 235 ? 235 : v);
            }
    }
}

#endif
