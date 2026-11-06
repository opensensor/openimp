#ifndef OPENIMP_T30_BLACK_START_H
#define OPENIMP_T30_BLACK_START_H

#include <stdint.h>
#include <string.h>

/*
 * OPENIMP_T30_BLACK_START (#216, default on; "0" switches it off): the T30
 * H.264 channel codes black pictures instead of the live picture until the
 * ISP picture has settled, then starts the live stream with an IDR and a
 * fresh rate controller.
 *
 * Why (vendor behaviour, T30X SC4236, audit/t30/b43/vendor/cbr-25.h264): the
 * vendor stack (stock ko fe105b88 + libimp abd61e9a) starts every stream
 * dark: decoded mean luma of pictures 0..12 is 0.0 0.0 0.0 0.0 0.0 0.1 0.2
 * 0.5 1.2 2.6 5.0 8.8 14.7 (the vendor AE ramps up from the minimum
 * exposure, about x2 per picture), first IDR 2491 bytes, P 67 bytes.  The
 * vendor encoder codes every picture; the dark start is the vendor ISP's.
 * The open ISP delivers a real picture from picture 0 while AE/AWB still
 * move (luma 63 -> 83 -> 74 at 2048x1536), so the first P pictures are
 * nearly IDR-sized (b44 2048x1536 15 fps: first 0.5 s 5426 kbit/s at a
 * 2000 kbit/s CBR).  OpenIMP mirrors the vendor's visible result (a dark
 * start, no live picture until it is usable) in the encoder, so the open
 * ISP's start-up is not changed.
 *
 * Settle criterion: a 16 x 16 grid of 4 x 4 luma block means plus an 8 x 8
 * grid of 2 x 2 chroma (U and V) block means, compared with the previous
 * picture; the picture is settled when the mean absolute difference is at
 * most 1.5 levels (calibrated on b43/b44 open streams: 2048x1536 15/25 fps
 * and 1920x1080 15/25 fps all reach it at picture 6, after 14..16, 9..11,
 * 4..5 levels at pictures 1..3).  Bound: at most 8 black pictures and at
 * most 0.5 s (fps / 2 pictures), whichever is fewer, so a noisy night
 * picture starts live after the bound.  The first picture of a stream is
 * always black (no previous picture to compare with).
 */
#define T30_BLACK_START_GRID 16u
#define T30_BLACK_START_SAMPLES                                             \
    (T30_BLACK_START_GRID * T30_BLACK_START_GRID +                          \
     2u * (T30_BLACK_START_GRID / 2u) * (T30_BLACK_START_GRID / 2u))
#define T30_BLACK_START_MAX_PICTURES 8u
/* 1.5 levels, in 1/16 levels */
#define T30_BLACK_START_THRESHOLD_16 24u

typedef struct {
    int active;                 /* black pictures may still be coded */
    int have_prev;
    uint32_t pictures;          /* black pictures coded so far */
    uint8_t prev[T30_BLACK_START_SAMPLES];
} T30BlackStart;

static inline int t30_black_start_wanted(const char *env)
{
    return !(env && env[0] == '0');
}

static inline void t30_black_start_reset(T30BlackStart *s, int on)
{
    memset(s, 0, sizeof(*s));
    s->active = on;
}

/* The pictures the bound allows at this frame rate: min(8, fps / 2), >= 1. */
static inline uint32_t t30_black_start_limit(uint32_t fps_num,
                                             uint32_t fps_den)
{
    uint32_t n = fps_den ? fps_num / (2u * fps_den) : 0u;

    if (n > T30_BLACK_START_MAX_PICTURES)
        n = T30_BLACK_START_MAX_PICTURES;
    return n ? n : 1u;
}

/* Grid samples of an NV12 picture: luma rows of stride bytes, chroma
 * (interleaved U V) at chroma with the same stride.  width >= 64 and
 * height >= 64 keep every block inside the picture. */
static inline void t30_black_start_sample(const uint8_t *luma,
                                          const uint8_t *chroma,
                                          uint32_t stride, uint32_t width,
                                          uint32_t height,
                                          uint8_t out[T30_BLACK_START_SAMPLES])
{
    const uint32_t g = T30_BLACK_START_GRID;
    uint32_t r, c, i, j, n = 0;

    for (r = 0; r < g; r++) {
        uint32_t y0 = height * (2u * r + 1u) / (2u * g) - 2u;

        for (c = 0; c < g; c++) {
            uint32_t x0 = width * (2u * c + 1u) / (2u * g) - 2u;
            uint32_t sum = 0;

            for (i = 0; i < 4u; i++)
                for (j = 0; j < 4u; j++)
                    sum += luma[(y0 + i) * stride + x0 + j];
            out[n++] = (uint8_t)((sum + 8u) / 16u);
        }
    }
    for (r = 0; r < g / 2u; r++) {
        uint32_t y0 = (height / 2u) * (2u * r + 1u) / g - 1u;

        for (c = 0; c < g / 2u; c++) {
            uint32_t x0 = (width * (2u * c + 1u) / g - 2u) & ~1u;
            uint32_t su = 0, sv = 0;

            for (i = 0; i < 2u; i++)
                for (j = 0; j < 2u; j++) {
                    const uint8_t *p = chroma + (y0 + i) * stride + x0 + 2u * j;

                    su += p[0];
                    sv += p[1];
                }
            out[n++] = (uint8_t)((su + 2u) / 4u);
            out[n++] = (uint8_t)((sv + 2u) / 4u);
        }
    }
}

/* Mean absolute difference of two sample sets, in 1/16 levels. */
static inline uint32_t t30_black_start_diff16(const uint8_t *a,
                                              const uint8_t *b)
{
    uint32_t i, sum = 0;

    for (i = 0; i < T30_BLACK_START_SAMPLES; i++)
        sum += a[i] > b[i] ? (uint32_t)(a[i] - b[i]) : (uint32_t)(b[i] - a[i]);
    return (sum * 16u + T30_BLACK_START_SAMPLES / 2u) /
           T30_BLACK_START_SAMPLES;
}

/* One picture with its samples: 1 = code it black, 0 = code it live (the
 * start is over: settled, bound reached, or not active). */
static inline int t30_black_start_step(T30BlackStart *s,
                                       const uint8_t samples[T30_BLACK_START_SAMPLES],
                                       uint32_t fps_num, uint32_t fps_den)
{
    if (!s->active)
        return 0;
    if ((s->have_prev &&
         t30_black_start_diff16(samples, s->prev) <=
             T30_BLACK_START_THRESHOLD_16) ||
        s->pictures >= t30_black_start_limit(fps_num, fps_den)) {
        s->active = 0;
        return 0;
    }
    memcpy(s->prev, samples, T30_BLACK_START_SAMPLES);
    s->have_prev = 1;
    s->pictures++;
    return 1;
}

#endif
