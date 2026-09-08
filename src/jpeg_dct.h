/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef OPENIMP_JPEG_DCT_H
#define OPENIMP_JPEG_DCT_H

#include <stdint.h>

/* AAN forward DCT. Samples retain four fractional bits through both passes;
 * the four cosine constants are rounded Q13 values. For centered 8-bit
 * input, even the conservative second-pass products fit signed 32 bits.
 * Quantization removes the factor of sixteen along with the AAN scale. */
static inline int32_t jpeg_dct_multiply(int32_t value, int32_t cosine)
{
    return (value * cosine + 4096) >> 13;
}

static inline void jpeg_dct(int32_t *d0p, int32_t *d1p, int32_t *d2p, int32_t *d3p,
                            int32_t *d4p, int32_t *d5p, int32_t *d6p, int32_t *d7p)
{
    int32_t d0 = *d0p, d1 = *d1p, d2 = *d2p, d3 = *d3p;
    int32_t d4 = *d4p, d5 = *d5p, d6 = *d6p, d7 = *d7p;
    int32_t tmp0 = d0 + d7, tmp7 = d0 - d7;
    int32_t tmp1 = d1 + d6, tmp6 = d1 - d6;
    int32_t tmp2 = d2 + d5, tmp5 = d2 - d5;
    int32_t tmp3 = d3 + d4, tmp4 = d3 - d4;
    int32_t tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3;
    int32_t tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;
    int32_t z1, z2, z3, z4, z5, z11, z13;

    d0 = tmp10 + tmp11;
    d4 = tmp10 - tmp11;
    z1 = jpeg_dct_multiply(tmp12 + tmp13, 5793);
    d2 = tmp13 + z1;
    d6 = tmp13 - z1;
    tmp10 = tmp4 + tmp5;
    tmp11 = tmp5 + tmp6;
    tmp12 = tmp6 + tmp7;
    z5 = jpeg_dct_multiply(tmp10 - tmp12, 3135);
    z2 = jpeg_dct_multiply(tmp10, 4433) + z5;
    z4 = jpeg_dct_multiply(tmp12, 10703) + z5;
    z3 = jpeg_dct_multiply(tmp11, 5793);
    z11 = tmp7 + z3;
    z13 = tmp7 - z3;
    *d5p = z13 + z2;
    *d3p = z13 - z2;
    *d1p = z11 + z4;
    *d7p = z11 - z4;
    *d0p = d0;
    *d2p = d2;
    *d4p = d4;
    *d6p = d6;
}

static inline int jpeg_dct_quantize(int32_t coefficient, uint32_t reciprocal)
{
    int64_t value = (int64_t)coefficient * reciprocal;

    return value < 0 ? -(int)((-value + 524288) >> 20)
                     : (int)((value + 524288) >> 20);
}

#endif
