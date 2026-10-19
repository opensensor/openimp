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
 */
#ifndef OPENIMP_TESTPATTERN_H
#define OPENIMP_TESTPATTERN_H

#include <stddef.h>
#include <stdint.h>

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

#endif
