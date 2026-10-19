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
 * The host recomputes the expected picture from the counter it reads out of
 * the decoded frame (tools/testpattern/tbmotion.c).
 */
#ifndef OPENIMP_TESTPATTERN_H
#define OPENIMP_TESTPATTERN_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

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
}


/* 0 = off, 1 = static pattern, 2 = static pattern + moving element */
static inline int openimp_testpattern_mode(void)
{
    static int mode = -1;

    if (mode < 0) {
        const char *env = getenv("OPENIMP_TEST_PATTERN");
        mode = (env && env[0] == '1') ? 1 : (env && env[0] == '2') ? 2 : 0;
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

#endif
