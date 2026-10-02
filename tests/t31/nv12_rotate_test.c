/* Host test for src/framesource/nv12_rotate.c: every mode against a naive
 * per-sample reference, over aligned, unaligned (360/1080-style) and odd
 * geometries, plus round trips. Padding outside the picture must stay
 * untouched. */
#include "framesource/nv12_rotate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...) do {                                   \
        if (!(cond)) {                                          \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__);                       \
            fputc('\n', stderr);                                \
            failures++;                                         \
        }                                                       \
    } while (0)

/* Reference: dst sample for each source sample, straight from the
 * definitions (CW: (x,y) -> (h-1-y, x); CCW: (x,y) -> (y, w-1-x)). */
static void ref_rotate(const uint8_t *src, uint8_t *dst, uint32_t w,
                       uint32_t h, int mode)
{
    uint32_t ow, oh, sp, dp, x, y, p;

    nv12_rotate_out_dims(mode, w, h, &ow, &oh);
    sp = nv12_rotate_pitch(w);
    dp = nv12_rotate_pitch(ow);
    for (p = 0; p < 2; p++) {
        uint32_t pw = p ? w / 2 : w, ph = p ? h / 2 : h, bpp = p ? 2 : 1;
        const uint8_t *s = src + (p ? (size_t)sp * nv12_rotate_lines(h) : 0);
        uint8_t *d = dst + (p ? (size_t)dp * nv12_rotate_lines(oh) : 0);

        for (y = 0; y < ph; y++) {
            for (x = 0; x < pw; x++) {
                uint32_t dx, dy, b;

                switch (mode) {
                case NV12_ROT_90_CW:  dx = ph - 1 - y; dy = x; break;
                case NV12_ROT_90_CCW: dx = y; dy = pw - 1 - x; break;
                case NV12_ROT_180:    dx = pw - 1 - x; dy = ph - 1 - y; break;
                default:              dx = x; dy = y; break;
                }
                for (b = 0; b < bpp; b++)
                    d[(size_t)dy * dp + dx * bpp + b] =
                        s[(size_t)y * sp + x * bpp + b];
            }
        }
    }
}

static void fill(uint8_t *buf, size_t n, uint32_t seed)
{
    size_t i;

    for (i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        buf[i] = (uint8_t)(seed >> 16);
    }
}

static void test_case(uint32_t w, uint32_t h, int mode, size_t misalign)
{
    uint32_t ow, oh;
    size_t sn = nv12_rotate_frame_size(w, h), dn;
    uint8_t *sbuf, *a, *b, *src, *dst;

    nv12_rotate_out_dims(mode, w, h, &ow, &oh);
    dn = nv12_rotate_frame_size(ow, oh);
    CHECK(sn == dn, "frame size differs after rotation %ux%u", w, h);
    sbuf = malloc(sn + 8);
    a = malloc(dn + 8);
    b = malloc(dn + 8);
    src = sbuf + misalign;
    fill(src, sn, w * 31u + h);
    memset(a, 0xa5, dn + 8);
    memset(b, 0xa5, dn + 8);
    dst = a + misalign;
    CHECK(nv12_rotate(src, dst, w, h, mode) == 0, "rotate %ux%u mode %d", w, h, mode);
    ref_rotate(src, b + misalign, w, h, mode);
    if (memcmp(a, b, dn + 8) != 0) {
        size_t i;

        for (i = 0; i < dn + 8 && a[i] == b[i]; i++)
            ;
        CHECK(0, "%ux%u mode %d misalign %zu: first diff at byte %zu",
              w, h, mode, misalign, i);
    }
    free(sbuf);
    free(a);
    free(b);
}

/* rotate(CW) then rotate(CCW) is the identity on the picture area; CW
 * twice equals 180. */
static void test_round_trip(uint32_t w, uint32_t h)
{
    size_t n = nv12_rotate_frame_size(w, h);
    uint8_t *s = calloc(1, n), *t = calloc(1, n), *u = calloc(1, n);
    uint8_t *v = calloc(1, n);
    uint32_t y, p = nv12_rotate_pitch(w), l = nv12_rotate_lines(h);

    fill(s, n, 7);
    nv12_rotate(s, t, w, h, NV12_ROT_90_CW);
    nv12_rotate(t, u, h, w, NV12_ROT_90_CCW);
    for (y = 0; y < h; y++)
        CHECK(!memcmp(s + y * p, u + y * p, w), "CW/CCW luma row %u %ux%u", y, w, h);
    for (y = 0; y < h / 2; y++)
        CHECK(!memcmp(s + (l + y) * p, u + (l + y) * p, w), "CW/CCW uv row %u", y);
    memset(u, 0, n);
    nv12_rotate(t, u, h, w, NV12_ROT_90_CW);
    nv12_rotate(s, v, w, h, NV12_ROT_180);
    for (y = 0; y < h; y++)
        CHECK(!memcmp(u + y * p, v + y * p, w), "CW*2 != 180 row %u", y);
    free(s); free(t); free(u); free(v);
}

int main(void)
{
    static const uint32_t dims[][2] = {
        { 64, 64 }, { 1280, 704 }, { 640, 360 }, { 1920, 1080 },
        { 2560, 1440 }, { 96, 36 }, { 36, 96 }, { 48, 30 }, { 6, 10 },
        { 2, 2 }, { 34, 18 },
    };
    static const int modes[] = { NV12_ROT_NONE, NV12_ROT_90_CCW,
                                 NV12_ROT_90_CW, NV12_ROT_180 };
    size_t i, m;
    uint32_t ow, oh;

    for (i = 0; i < sizeof(dims) / sizeof(dims[0]); i++)
        for (m = 0; m < 4; m++)
            test_case(dims[i][0], dims[i][1], modes[m], 0);
    /* unaligned buffers take the scalar path */
    test_case(64, 36, NV12_ROT_90_CW, 1);
    test_case(64, 36, NV12_ROT_180, 2);
    test_round_trip(640, 360);
    test_round_trip(96, 64);

    CHECK(nv12_rotate_out_dims(NV12_ROT_90_CW, 1920, 1080, &ow, &oh) == 0 &&
          ow == 1080 && oh == 1920, "CW dims");
    CHECK(nv12_rotate_out_dims(4, 16, 16, &ow, &oh) == -1, "mode 4 rejected");
    {
        uint8_t x[64];

        CHECK(nv12_rotate(x, x + 32, 5, 4, NV12_ROT_180) == -1, "odd width rejected");
    }
    /* 1920x1080: 1088 luma lines landscape, pitch 1088 portrait: equal size */
    CHECK(nv12_rotate_frame_size(1920, 1080) == nv12_rotate_frame_size(1080, 1920),
          "1080p rotated frame size");

    if (failures) {
        fprintf(stderr, "nv12_rotate_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("nv12_rotate_test: ok\n");
    return 0;
}
