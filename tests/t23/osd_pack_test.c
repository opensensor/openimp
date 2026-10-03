/* Host test of the T23 OSD picture packing (src/t23/openimp_t23_osd_pack.h):
 * pictures of every width 1..80 (odd ones included) in BGRA, ARGB, 1555 and
 * 1-bpp are packed at the 16-pixel pitch and blended by a model of the T23
 * IPU fetch (lines at src_w rounded up to 16 pixels) into a frame; the
 * frame must show the picture unsheared, the padding transparent.  The
 * unpadded layout (pitch = w) is checked to shear, as on cam-B. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t23/openimp_t23_osd_pack.h"

#define FW 192u
#define FH 40u
#define MAXW 80u

/* T23 IPU model: BGRA source, per-pixel alpha (0 or 255 here), line pitch
 * align16(src_w) pixels; frame of 32-bit "pixels" for exact comparison. */
static void ipu_blend(uint32_t *frame, const uint8_t *pic, uint32_t src_w,
                      uint32_t src_h, uint32_t x0, uint32_t y0)
{
    uint32_t pitch = (src_w + 15u) & ~15u, x, y;

    for (y = 0; y < src_h; y++)
        for (x = 0; x < src_w; x++) {
            const uint8_t *p = pic + (y * pitch + x) * 4u;

            if (p[3])
                frame[(y0 + y) * FW + x0 + x] =
                    (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
        }
}

/* Source pixel (x, y) of the test picture: colour and opacity. */
static uint32_t want_rgb(uint32_t x, uint32_t y, int *on)
{
    *on = ((x * 7u + y * 3u) % 5u) != 0u;     /* holes are transparent */
    return *on ? 0xf8f8f8u & ((x * 0x10305u + y * 0x51307u) | 0x080808u)
               : 0u;
}

static void make_src(uint8_t *src, uint32_t w, uint32_t h, int fmt)
{
    uint32_t x, y;

    memset(src, 0, MAXW * 4u * FH);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int on;
            uint32_t c = want_rgb(x, y, &on);
            uint8_t r = (uint8_t)(c >> 16), g = (uint8_t)(c >> 8),
                    b = (uint8_t)c, a = on ? 0xff : 0;

            switch (fmt) {
            case T23_OSD_PIX_BGRA: {
                uint8_t *p = src + (y * w + x) * 4u;
                p[0] = b; p[1] = g; p[2] = r; p[3] = a;
                break;
            }
            case T23_OSD_PIX_ARGB: {
                uint8_t *p = src + (y * w + x) * 4u;
                p[0] = a; p[1] = r; p[2] = g; p[3] = b;
                break;
            }
            case T23_OSD_PIX_RGB555LE: {
                uint32_t v = (on ? 0x8000u : 0u) | (uint32_t)(r >> 3) << 10 |
                             (uint32_t)(g >> 3) << 5 | (uint32_t)(b >> 3);
                src[(y * w + x) * 2u] = (uint8_t)v;
                src[(y * w + x) * 2u + 1u] = (uint8_t)(v >> 8);
                break;
            }
            default: {              /* 1 bpp, continuous bit stream */
                uint32_t bit = y * w + x;
                if (on)
                    src[bit >> 3] |= (uint8_t)(0x80u >> (bit & 7u));
                break;
            }
            }
        }
}

static uint32_t expect(uint32_t x, uint32_t y, int fmt, int *on)
{
    uint32_t c = want_rgb(x, y, on);

    if (fmt == T23_OSD_PIX_MONOWHITE)
        return 0xffffffu;
    if (fmt == T23_OSD_PIX_RGB555LE) {
        uint32_t r = (c >> 19) & 31u, g = (c >> 11) & 31u, b = (c >> 3) & 31u;
        return (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 |
               (b << 3 | b >> 2);
    }
    return c;
}

/* Returns 1 when the blended frame shows exactly the picture. */
static int run(uint32_t w, uint32_t h, int fmt, int padded)
{
    static uint8_t src[MAXW * 4u * FH];
    static uint8_t pic[MAXW * 4u * FH + 64u * 4u * FH];
    static uint32_t frame[FW * FH];
    uint32_t pitch = padded ? t23_osd_pic_pitch(w) : w;
    uint32_t src_w = padded ? pitch : w;
    const uint32_t x0 = 17u, y0 = 3u, bg = 0x123456u;
    uint32_t x, y;

    make_src(src, w, h, fmt);
    memset(pic, 0xa5, sizeof(pic));             /* garbage past the end */
    t23_osd_pack(pic, pitch, src, w, h, fmt);
    for (x = 0; x < FW * FH; x++)
        frame[x] = bg;
    ipu_blend(frame, pic, src_w, h, x0, y0);
    for (y = 0; y < FH; y++)
        for (x = 0; x < FW; x++) {
            uint32_t want = bg;
            int on = 0;

            if (x >= x0 && x < x0 + w && y >= y0 && y < y0 + h) {
                uint32_t c = expect(x - x0, y - y0, fmt, &on);
                if (on)
                    want = c;
            }
            if (frame[y * FW + x] != want)
                return 0;
        }
    return 1;
}

int main(void)
{
    static const int fmts[] = { T23_OSD_PIX_BGRA, T23_OSD_PIX_ARGB,
                                T23_OSD_PIX_RGB555LE,
                                T23_OSD_PIX_MONOWHITE };
    uint32_t w, i, cases = 0;

    assert(t23_osd_pic_pitch(1) == 16u && t23_osd_pic_pitch(16) == 16u &&
           t23_osd_pic_pitch(17) == 32u && t23_osd_pic_pitch(332) == 336u);
    for (i = 0; i < sizeof(fmts) / sizeof(fmts[0]); i++)
        for (w = 1; w <= MAXW; w++) {
            assert(run(w, 30u, fmts[i], 1));
            /* the old layout: correct only at a 16-pixel width */
            assert(run(w, 30u, fmts[i], 0) == ((w & 15u) == 0u));
            cases++;
        }
    printf("t23 osd pack: %u width/format cases ok\n", cases);
    return 0;
}
