/*
 * Host test of t30_radix_cabac_init(): the states of every slice type
 * (B, P, I) and QP -10..60 must hash like the states of the vendor libimp
 * 1.0.5 Entropy::resetEntropy (hash recorded with tests/t30/radix/hevc_diff
 * under qemu-mipsel, 2026-11-05, 213 cases, 0 differ).
 */
#include <stdio.h>

#include "t30/radix/t30_radix_cabac.h"

int main(void)
{
    uint32_t hash = 2166136261u;
    uint8_t st[T30_RADIX_CABAC_CONTEXTS];

    for (int t = 0; t < 3; t++)
        for (int qp = -10; qp <= 60; qp++) {
            t30_radix_cabac_init(st, t, qp);
            for (int i = 0; i < T30_RADIX_CABAC_CONTEXTS; i++)
                hash = (hash ^ st[i]) * 16777619u;
        }
    printf("radix_cabac_test: %s (hash 0x%08x)\n",
           hash == 0x96773d18u ? "PASS" : "FAIL", hash);
    return hash != 0x96773d18u;
}
