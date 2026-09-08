/* Portable AAN fixed-point regression against the previous float transform.
 * Inputs are synthetic, never sensor calibration or captured ISP tables. */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../src/jpeg_dct.h"

static void jpeg_dct_reference(float *d0p, float *d1p, float *d2p, float *d3p,
                     float *d4p, float *d5p, float *d6p, float *d7p)
{
    float d0 = *d0p, d1 = *d1p, d2 = *d2p, d3 = *d3p;
    float d4 = *d4p, d5 = *d5p, d6 = *d6p, d7 = *d7p;
    float tmp0 = d0 + d7, tmp7 = d0 - d7;
    float tmp1 = d1 + d6, tmp6 = d1 - d6;
    float tmp2 = d2 + d5, tmp5 = d2 - d5;
    float tmp3 = d3 + d4, tmp4 = d3 - d4;
    float tmp10 = tmp0 + tmp3, tmp13 = tmp0 - tmp3;
    float tmp11 = tmp1 + tmp2, tmp12 = tmp1 - tmp2;
    float z1, z2, z3, z4, z5, z11, z13;

    d0 = tmp10 + tmp11;
    d4 = tmp10 - tmp11;
    z1 = (tmp12 + tmp13) * 0.707106781f;
    d2 = tmp13 + z1;
    d6 = tmp13 - z1;
    tmp10 = tmp4 + tmp5;
    tmp11 = tmp5 + tmp6;
    tmp12 = tmp6 + tmp7;
    z5 = (tmp10 - tmp12) * 0.382683433f;
    z2 = tmp10 * 0.541196100f + z5;
    z4 = tmp12 * 1.306562965f + z5;
    z3 = tmp11 * 0.707106781f;
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

static const float aasf[8] = {
    2.828427125f, 3.923141121f, 3.695518130f, 3.325878449f,
    2.828427125f, 2.222280933f, 1.530733729f, 0.780361288f
};

int main(void)
{
    uint32_t random = 0x19860920;
    unsigned int iteration, i, q;
    static const unsigned int quantizers[] = {1, 2, 3, 7, 15, 16, 64, 128, 255};
    double worst = 0;
    unsigned int mismatches = 0;
    for (iteration = 0; iteration < 10000; ++iteration) {
        int32_t fixed[64];
        float reference[64];
        for (i = 0; i < 64; ++i) {
            int sample;
            random = random * 1664525u + 1013904223u;
            sample = (int)(random >> 24) - 128;
            if (iteration < 256)
                sample = (int)iteration - 128;
            else if (iteration < 512)
                sample = ((i + i / 8 + iteration) & 1) ? 127 : -128;
            else if (iteration < 768)
                sample = ((iteration >> (i % 8)) & 1) ? 127 : -128;
            else if (iteration < 1024)
                sample = ((iteration >> (i / 8)) & 1) ? 127 : -128;
            fixed[i] = sample * 16;
            reference[i] = sample;
        }
        for (i = 0; i < 64; i += 8) {
            jpeg_dct(&fixed[i], &fixed[i+1], &fixed[i+2], &fixed[i+3],
                     &fixed[i+4], &fixed[i+5], &fixed[i+6], &fixed[i+7]);
            jpeg_dct_reference(&reference[i], &reference[i+1], &reference[i+2],
                &reference[i+3], &reference[i+4], &reference[i+5],
                &reference[i+6], &reference[i+7]);
        }
        for (i = 0; i < 8; ++i) {
            jpeg_dct(&fixed[i], &fixed[i+8], &fixed[i+16], &fixed[i+24],
                     &fixed[i+32], &fixed[i+40], &fixed[i+48], &fixed[i+56]);
            jpeg_dct_reference(&reference[i], &reference[i+8], &reference[i+16],
                &reference[i+24], &reference[i+32], &reference[i+40],
                &reference[i+48], &reference[i+56]);
        }
        assert(fixed[0] == (int32_t)(reference[0] * 16));
        for (i = 0; i < 64; ++i) {
            float normalization = aasf[i / 8] * aasf[i % 8];
            double error = fabs((fixed[i] / 16.0 - reference[i]) / normalization);
            if (error > worst) worst = error;
            for (q = 0; q < sizeof(quantizers) / sizeof(quantizers[0]); ++q) {
                float denominator = quantizers[q] * normalization;
                uint32_t reciprocal = (uint32_t)(65536.0f / denominator + 0.5f);
                float value = reference[i] / denominator;
                int expected = (int)(value < 0 ? value - 0.5f : value + 0.5f);
                int actual = jpeg_dct_quantize(fixed[i], reciprocal);
                if (actual != expected) ++mismatches;
                assert(abs(actual - expected) <= 1);
                assert(i == 0 || abs(actual) <= 1023);
            }
        }
    }
    printf("JPEG fixed AAN: 10000 blocks, worst normalized error %.6f; "
           "%u quantized differences, all <= 1\n", worst, mismatches);
    assert(worst < 0.6);
    return 0;
}
