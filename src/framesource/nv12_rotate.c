/*
 * NV12 rotation for the T31 FrameSource, plain C (no MXU/SIMD).
 *
 * 90/270: the source is walked in 32x32 tiles (the vendor block size), so a
 * tile's 32 source and 32 destination lines stay in the 32 KB L1 D-cache.
 * Inside a tile, 4x4 luma blocks (2x2 for CbCr pairs) are loaded as one
 * 32-bit word per row, transposed in registers and stored as one word per
 * destination row: one load and one store per 4 luma samples.
 * 180: each row is copied to the mirrored row with the words reversed.
 * Edges that do not fill a word, and unaligned buffers, take the scalar
 * path.
 */
#include "nv12_rotate.h"

#include <string.h>

typedef uint32_t nv12_rot_u32 __attribute__((may_alias));

#define NV12_ROT_TILE 32u

#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define NV12_ROT_WORDS 1
#else
#define NV12_ROT_WORDS 0
#endif

int nv12_rotate_out_dims(int mode, uint32_t width, uint32_t height,
                         uint32_t *out_width, uint32_t *out_height)
{
    switch (mode) {
    case NV12_ROT_NONE:
    case NV12_ROT_180:
        *out_width = width;
        *out_height = height;
        return 0;
    case NV12_ROT_90_CCW:
    case NV12_ROT_90_CW:
        *out_width = height;
        *out_height = width;
        return 0;
    default:
        return -1;
    }
}

/* Destination coordinates of source sample (x, y). */
static inline void rot_map(int mode, uint32_t w, uint32_t h, uint32_t x,
                           uint32_t y, uint32_t *dx, uint32_t *dy)
{
    switch (mode) {
    case NV12_ROT_90_CW:
        *dx = h - 1u - y;
        *dy = x;
        break;
    case NV12_ROT_90_CCW:
        *dx = y;
        *dy = w - 1u - x;
        break;
    case NV12_ROT_180:
        *dx = w - 1u - x;
        *dy = h - 1u - y;
        break;
    default:
        *dx = x;
        *dy = y;
        break;
    }
}

/* Scalar rotation of the rectangle [x0,x1) x [y0,y1) of the plane. */
static void rot_scalar(const uint8_t *src, uint32_t ss, uint8_t *dst,
                       uint32_t ds, uint32_t w, uint32_t h, int bpp,
                       int mode, uint32_t x0, uint32_t x1, uint32_t y0,
                       uint32_t y1)
{
    uint32_t x, y, dx, dy;

    for (y = y0; y < y1; y++) {
        const uint8_t *s = src + (size_t)y * ss;

        for (x = x0; x < x1; x++) {
            rot_map(mode, w, h, x, y, &dx, &dy);
            if (bpp == 1) {
                dst[(size_t)dy * ds + dx] = s[x];
            } else {
                uint8_t *d = dst + (size_t)dy * ds + (size_t)dx * 2u;

                d[0] = s[x * 2u];
                d[1] = s[x * 2u + 1u];
            }
        }
    }
}

#if NV12_ROT_WORDS
#define LD32(p)     (*(const nv12_rot_u32 *)(const void *)(p))
#define ST32(p, v)  (*(nv12_rot_u32 *)(void *)(p) = (v))

/* 4x4 byte transpose: a..d are four rows (byte k = column k); on return
 * c0..c3 are the four columns (byte k = row k: a, b, c, d). */
#define TRANSPOSE4(a, b, c, d, c0, c1, c2, c3) do {                     \
        uint32_t t0_ = ((a) & 0x00ff00ffu) | (((b) & 0x00ff00ffu) << 8); \
        uint32_t t1_ = (((a) >> 8) & 0x00ff00ffu) | ((b) & 0xff00ff00u); \
        uint32_t t2_ = ((c) & 0x00ff00ffu) | (((d) & 0x00ff00ffu) << 8); \
        uint32_t t3_ = (((c) >> 8) & 0x00ff00ffu) | ((d) & 0xff00ff00u); \
        (c0) = (t0_ & 0xffffu) | (t2_ << 16);                           \
        (c1) = (t1_ & 0xffffu) | (t3_ << 16);                           \
        (c2) = (t0_ >> 16) | (t2_ & 0xffff0000u);                       \
        (c3) = (t1_ >> 16) | (t3_ & 0xffff0000u);                       \
    } while (0)

/* Luma 90/270, x and y ranges multiples of 4. */
static void rot90_y_words(const uint8_t *src, uint32_t ss, uint8_t *dst,
                          uint32_t ds, uint32_t w, uint32_t h, int mode,
                          uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1)
{
    uint32_t x, y;

    for (y = y0; y < y1; y += 4u) {
        const uint8_t *s0 = src + (size_t)y * ss;
        const uint8_t *s1 = s0 + ss;
        const uint8_t *s2 = s1 + ss;
        const uint8_t *s3 = s2 + ss;

        for (x = x0; x < x1; x += 4u) {
            uint32_t r0 = LD32(s0 + x), r1 = LD32(s1 + x);
            uint32_t r2 = LD32(s2 + x), r3 = LD32(s3 + x);
            uint32_t c0, c1, c2, c3;
            uint8_t *d;

            if (mode == NV12_ROT_90_CW) {
                /* dst row x+j, columns h-4-y .. h-1-y hold rows y+3 .. y */
                TRANSPOSE4(r3, r2, r1, r0, c0, c1, c2, c3);
                d = dst + (size_t)x * ds + (h - 4u - y);
                ST32(d, c0);
                ST32(d + ds, c1);
                ST32(d + 2u * ds, c2);
                ST32(d + 3u * ds, c3);
            } else {
                /* dst row w-1-x-j, columns y .. y+3 hold rows y .. y+3 */
                TRANSPOSE4(r0, r1, r2, r3, c0, c1, c2, c3);
                d = dst + (size_t)(w - 1u - x) * ds + y;
                ST32(d, c0);
                ST32(d - ds, c1);
                ST32(d - 2u * ds, c2);
                ST32(d - 3u * ds, c3);
            }
        }
    }
}

/* CbCr pairs 90/270, x and y ranges multiples of 2 (in pairs). */
static void rot90_uv_words(const uint8_t *src, uint32_t ss, uint8_t *dst,
                           uint32_t ds, uint32_t w, uint32_t h, int mode,
                           uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1)
{
    uint32_t x, y;

    for (y = y0; y < y1; y += 2u) {
        const uint8_t *s0 = src + (size_t)y * ss;
        const uint8_t *s1 = s0 + ss;

        for (x = x0; x < x1; x += 2u) {
            uint32_t a = LD32(s0 + x * 2u), b = LD32(s1 + x * 2u);
            uint8_t *d;

            if (mode == NV12_ROT_90_CW) {
                /* dst row x+j, pair columns h-2-y, h-1-y = rows y+1, y */
                d = dst + (size_t)x * ds + (size_t)(h - 2u - y) * 2u;
                ST32(d, (b & 0xffffu) | (a << 16));
                ST32(d + ds, (b >> 16) | (a & 0xffff0000u));
            } else {
                /* dst row w-1-x-j, pair columns y, y+1 = rows y, y+1 */
                d = dst + (size_t)(w - 1u - x) * ds + (size_t)y * 2u;
                ST32(d, (a & 0xffffu) | (b << 16));
                ST32(d - ds, (a >> 16) | (b & 0xffff0000u));
            }
        }
    }
}
#endif /* NV12_ROT_WORDS */

static void rot180_plane(const uint8_t *src, uint32_t ss, uint8_t *dst,
                         uint32_t ds, uint32_t w, uint32_t h, int bpp,
                         int words)
{
    uint32_t y;

    for (y = 0; y < h; y++) {
        const uint8_t *s = src + (size_t)y * ss;
        uint8_t *d = dst + (size_t)(h - 1u - y) * ds;
        uint32_t x = 0;

#if NV12_ROT_WORDS
        if (words) {
            uint32_t step = bpp == 1 ? 4u : 2u;     /* samples per word */
            uint32_t full = w - w % step;

            for (; x < full; x += step) {
                uint32_t v = LD32(s + x * (uint32_t)bpp);

                v = bpp == 1 ? __builtin_bswap32(v) : (v >> 16) | (v << 16);
                ST32(d + (w - step - x) * (uint32_t)bpp, v);
            }
        }
#else
        (void)words;
#endif
        for (; x < w; x++) {
            if (bpp == 1) {
                d[w - 1u - x] = s[x];
            } else {
                d[(w - 1u - x) * 2u] = s[x * 2u];
                d[(w - 1u - x) * 2u + 1u] = s[x * 2u + 1u];
            }
        }
    }
}

void nv12_rotate_plane(const uint8_t *src, uint32_t src_stride,
                       uint8_t *dst, uint32_t dst_stride,
                       uint32_t width, uint32_t height, int bpp, int mode)
{
    uint32_t blk = bpp == 1 ? 4u : 2u;   /* block edge for the word path */
    int words = 0;
    uint32_t tx, ty;

    if (!src || !dst || !width || !height || (bpp != 1 && bpp != 2))
        return;
    if (mode == NV12_ROT_NONE) {
        uint32_t y;

        for (y = 0; y < height; y++)
            memcpy(dst + (size_t)y * dst_stride, src + (size_t)y * src_stride,
                   (size_t)width * (uint32_t)bpp);
        return;
    }
#if NV12_ROT_WORDS
    words = ((uintptr_t)src | (uintptr_t)dst | src_stride | dst_stride) % 4u == 0u;
#endif
    if (mode == NV12_ROT_180) {
        /* Word stores land at w - step - x: aligned when w fills words. */
        rot180_plane(src, src_stride, dst, dst_stride, width, height, bpp,
                     words && width % blk == 0u);
        return;
    }
    if (mode != NV12_ROT_90_CW && mode != NV12_ROT_90_CCW)
        return;

    /* The word path needs both dimensions in whole blocks: destination
     * offsets are h - blk - y (CW) or w - 1 - x (CCW) and must stay
     * word-aligned. Otherwise everything goes scalar (odd geometry only). */
    if (!words || width % blk || height % blk) {
        rot_scalar(src, src_stride, dst, dst_stride, width, height, bpp, mode,
                   0, width, 0, height);
        return;
    }
    for (ty = 0; ty < height; ty += NV12_ROT_TILE) {
        uint32_t ye = ty + NV12_ROT_TILE < height ? ty + NV12_ROT_TILE : height;

        for (tx = 0; tx < width; tx += NV12_ROT_TILE) {
            uint32_t xe = tx + NV12_ROT_TILE < width ? tx + NV12_ROT_TILE : width;

#if NV12_ROT_WORDS
            if (bpp == 1)
                rot90_y_words(src, src_stride, dst, dst_stride, width, height,
                              mode, tx, xe, ty, ye);
            else
                rot90_uv_words(src, src_stride, dst, dst_stride, width,
                               height, mode, tx, xe, ty, ye);
#else
            rot_scalar(src, src_stride, dst, dst_stride, width, height, bpp,
                       mode, tx, xe, ty, ye);
#endif
        }
    }
}

int nv12_rotate(const uint8_t *src, uint8_t *dst, uint32_t width,
                uint32_t height, int mode)
{
    uint32_t ow, oh, sp, dp;

    if (!src || !dst || !width || !height || (width | height) & 1u ||
        nv12_rotate_out_dims(mode, width, height, &ow, &oh) != 0)
        return -1;
    sp = nv12_rotate_pitch(width);
    dp = nv12_rotate_pitch(ow);
    nv12_rotate_plane(src, sp, dst, dp, width, height, 1, mode);
    nv12_rotate_plane(src + (size_t)sp * nv12_rotate_lines(height), sp,
                      dst + (size_t)dp * nv12_rotate_lines(oh), dp,
                      width / 2u, height / 2u, 2, mode);
    return 0;
}
