/*
 * Compact NV12/NV21 copy for the frames IMP_FrameSource_GetFrame hands out.
 *
 * The capture buffer of the T-series frame channel has pitch = width and
 * holds the interleaved chroma plane after ALIGN16(height) luma lines. When
 * the height is not a multiple of 16 (360, 1080) that leaves a gap of
 * (ALIGN16(height) - height) * width bytes between the planes. The vendor
 * libimp copies every frame of a depth channel into a compact NV12 buffer
 * (luma w*h, chroma right behind it, size w*h*3/2; T21 libimp 1.0.33,
 * on_framesource_group_data_update 0x5bd68..0x5bde8), so applications read
 * the chroma at width*height. Pure functions, no state (host tested in
 * tests/t30/nv12_compact_test.c).
 */
#ifndef OPENIMP_NV12_COMPACT_H
#define OPENIMP_NV12_COMPACT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline uint32_t nv12_compact_aligned_height(uint32_t height)
{
    return (height + 15u) & ~15u;
}

/* The compact copy differs from the capture layout only for a height that
 * is not a multiple of 16. */
static inline int nv12_compact_needed(uint32_t width, uint32_t height)
{
    return width != 0 && height != 0 && (height & 15u) != 0;
}

/* Size of the capture buffer and of the compact frame. */
static inline size_t nv12_compact_src_size(uint32_t width, uint32_t height)
{
    return (size_t)width * nv12_compact_aligned_height(height) * 3u / 2u;
}

static inline size_t nv12_compact_size(uint32_t width, uint32_t height)
{
    return (size_t)width * height * 3u / 2u;
}

/* Copies luma (w*h) and chroma (w*h/2, taken from w*ALIGN16(h)) into dst
 * (w*h*3/2 bytes). Returns the compact size. */
static inline size_t nv12_compact_copy(uint8_t *dst, const uint8_t *src,
                                       uint32_t width, uint32_t height)
{
    size_t luma = (size_t)width * height;
    size_t gap = (size_t)width * nv12_compact_aligned_height(height);

    memcpy(dst, src, luma);
    memcpy(dst + luma, src + gap, luma / 2u);
    return luma + luma / 2u;
}

#endif
