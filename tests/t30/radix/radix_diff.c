/*
 * Differential test: OpenIMP t30_radix_build_cmdlist() against the vendor
 * libimp 1.0.5 H265E_T30_SliceInit (0x3ad5c) for random parameter blocks.
 * Desk tool, not part of libimp; see docs/T30_HEVC.md:
 *   radix_diff <vendor libimp.so> [cases] [seed]     compare
 *   radix_diff <vendor libimp.so> -golden <n>        print vendor list hashes
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "t30/radix/t30_radix_cmdlist.h"
#include "radix_case.h"

typedef void (*vendor_fn)(uint8_t *si);

static size_t list_words(const uint32_t *l, size_t max)
{
    for (size_t i = 1; i < max; i += 2)
        if (l[i] & 0x40000000u)
            return i + 1;
    return 0;
}

int main(int argc, char **argv)
{
    const char *lib = argc > 1 ? argv[1] : "libimp.so";
    int golden = argc > 3 && !strcmp(argv[2], "-golden");
    int cases = golden ? atoi(argv[3]) : argc > 2 ? atoi(argv[2]) : 2000;
    void *h;
    vendor_fn vendor;
    int bad = 0;

    if (!golden && argc > 3)
        radix_rng = (uint32_t)strtoul(argv[3], NULL, 0);
    h = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        fprintf(stderr, "dlopen %s: %s\n", lib, dlerror());
        return 2;
    }
    vendor = (vendor_fn)dlsym(h, "H265E_T30_SliceInit");
    if (!vendor) {
        fprintf(stderr, "no H265E_T30_SliceInit\n");
        return 2;
    }
    for (int c = 0; c < cases; c++) {
        static uint8_t si_v[T30_RADIX_SI_SIZE], si_o[T30_RADIX_SI_SIZE];
        static uint8_t state[256];
        static uint32_t qpt[256];
        static uint32_t lv[8192], lo[8192];
        size_t nv, no;

        radix_make_case(c, si_v, state, qpt);
        rsi_set_u32(si_v, RSI_QP_TABLE, (uint32_t)(uintptr_t)qpt);
        rsi_set_u32(si_v, RSI_STATE, (uint32_t)(uintptr_t)state);
        rsi_set_u32(si_v, RSI_DES_VA, (uint32_t)(uintptr_t)lv);
        memcpy(si_o, si_v, sizeof(si_o));
        memset(lv, 0xa5, sizeof(lv));
        memset(lo, 0x5a, sizeof(lo));
        vendor(si_v);
        nv = list_words(lv, 8192);
        if (golden) {
            printf("    { %4zu, 0x%08xu },\n", nv, radix_hash(lv, nv));
            continue;
        }
        no = t30_radix_build_cmdlist(si_o, lo, NULL, NULL);
        if (nv != no || memcmp(lv, lo, no * 4) || memcmp(si_v, si_o, sizeof(si_o))) {
            if (++bad <= 5) {
                printf("case %d (%ux%u type %u qlen %u bu_len %u): vendor %zu words, ours %zu\n",
                       c, rsi_u16(si_v, RSI_FRAME_WIDTH), rsi_u16(si_v, RSI_FRAME_HEIGHT),
                       si_v[RSI_FRAME_TYPE], rsi_u16(si_v, RSI_QP_TABLE_LEN),
                       si_v[RSI_BU_LEN], nv, no);
                for (size_t i = 0; i < (nv > no ? nv : no); i += 2)
                    if (lv[i] != lo[i] || lv[i + 1] != lo[i + 1]) {
                        printf("  pair %zu: vendor %08x @%08x  ours %08x @%08x\n", i / 2,
                               lv[i], lv[i + 1], lo[i], lo[i + 1]);
                        break;
                    }
                if (memcmp(si_v, si_o, sizeof(si_o)))
                    printf("  parameter block differs after the call\n");
            }
        }
    }
    if (!golden)
        printf("radix_diff: %d cases, %d differ\n", cases, bad);
    return bad ? 1 : 0;
}
