/*
 * Differential test of the T30 H.265 syntax helpers against the vendor
 * libimp 1.0.5 (x265-derived i265e) under qemu-mipsel:
 *   hevc_diff <vendor libimp.so> [cases]
 * cabac: t30_radix_cabac_init vs Entropy::resetEntropy (0x6476c), every slice
 *        type and QP -10..60.
 * Desk tool, not part of libimp (docs/T30_HEVC.md).
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t30/radix/t30_radix_cabac.h"

typedef void (*ctor_fn)(void *self);
typedef void (*reset_fn)(void *entropy, const void *slice);

static void *sym(void *h, const char *name)
{
    void *p = dlsym(h, name);

    if (!p) {
        fprintf(stderr, "missing %s\n", name);
        exit(2);
    }
    return p;
}

static int test_cabac(void *h)
{
    ctor_fn ent_ctor = (ctor_fn)sym(h, "_ZN8I265E_NS7EntropyC1Ev");
    reset_fn reset = (reset_fn)sym(h, "_ZN8I265E_NS7Entropy12resetEntropyERKNS_10I265eSliceE");
    int bad = 0, n = 0;
    uint32_t hash = 2166136261u;

    for (int t = 0; t < 3; t++)
        for (int qp = -10; qp <= 60; qp++) {
            static uint8_t ent[8192], slice[8192], mine[T30_RADIX_CABAC_CONTEXTS];

            memset(ent, 0, sizeof(ent));
            memset(slice, 0, sizeof(slice));
            ent_ctor(ent);
            memcpy(slice + 0x774, &t, 4);
            memcpy(slice + 0x778, &qp, 4);
            reset(ent, slice);
            t30_radix_cabac_init(mine, t, qp);
            for (int i = 0; i < T30_RADIX_CABAC_CONTEXTS; i++)
                hash = (hash ^ ent[0x10 + i]) * 16777619u;
            n++;
            if (memcmp(ent + 0x10, mine, sizeof(mine))) {
                if (++bad <= 3)
                    for (int i = 0; i < T30_RADIX_CABAC_CONTEXTS; i++)
                        if (ent[0x10 + i] != mine[i]) {
                            printf("cabac type %d qp %d ctx 0x%02x: vendor %u ours %u\n",
                                   t, qp, i, ent[0x10 + i], mine[i]);
                            break;
                        }
            }
        }
    printf("cabac: %d cases, %d differ, vendor hash 0x%08x\n", n, bad, hash);
    return bad;
}

int main(int argc, char **argv)
{
    void *h = dlopen(argc > 1 ? argv[1] : "libimp.so", RTLD_NOW | RTLD_LOCAL);
    int bad = 0;

    if (!h) {
        fprintf(stderr, "dlopen: %s\n", dlerror());
        return 2;
    }
    bad += test_cabac(h);
    return bad ? 1 : 0;
}
