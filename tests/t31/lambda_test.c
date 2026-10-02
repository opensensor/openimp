/* The default AVC/HEVC lambda tables are generated from the HM/JM lambda
 * formula (src/t40/t40_lambda.h, docs/T31_LAMBDA.md).  They must stay
 * bit-identical to the tables the AVPU was validated with; only their
 * hashes are kept here as the golden reference. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "t40/t40_lambda.h"

/* FNV-1a 64 of the validated 208-byte tables
 * (sha256 avc  7958426309f35b6b248ae8deea07d0744c8f41b7c5a113398989199ae022c188,
 *  sha256 hevc f776b1d19410a265dc4935115e4e76618c166baf67f559f1b2db3c64ec2ac06e) */
#define GOLDEN_AVC_FNV1A64  0x3c1bd4391e99d4d6ULL
#define GOLDEN_HEVC_FNV1A64 0x1bb4bfd8cbe90e96ULL

static uint64_t fnv1a64(const uint8_t *p, unsigned int n)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    unsigned int i;

    for (i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

static int failures;

#define CHECK(c, ...) do { if (!(c)) { fprintf(stderr, __VA_ARGS__); \
    fputc('\n', stderr); ++failures; } } while (0)

int main(void)
{
    uint8_t tab[T40_LAMBDA_TABLE_SIZE], pure[T40_LAMBDA_TABLE_SIZE];
    int hevc;
    unsigned int i, diffs = 0;
    uint64_t want[2] = { GOLDEN_AVC_FNV1A64, GOLDEN_HEVC_FNV1A64 };

    for (hevc = 0; hevc < 2; ++hevc) {
        uint64_t h;

        t40_lambda_build(tab, hevc, 1);
        h = fnv1a64(tab, sizeof(tab));
        CHECK(h == want[hevc], "%s table hash %016llx != golden %016llx",
              hevc ? "HEVC" : "AVC", (unsigned long long)h,
              (unsigned long long)want[hevc]);
        t40_lambda_default_table(pure, hevc);
        for (i = 0; i < sizeof(tab); ++i)
            CHECK(pure[i] == tab[i], "default_table differs at %u", i);

        /* The pure formula may differ only at the documented overrides,
         * and only by one step. */
        t40_lambda_build(pure, hevc, 0);
        for (i = 0; i < sizeof(tab); ++i) {
            int d = (int)tab[i] - (int)pure[i];

            if (!d)
                continue;
            ++diffs;
            CHECK(d == 1 || d == -1, "%s qp %u lane %u differs by %d",
                  hevc ? "HEVC" : "AVC", i / 4u, i % 4u, d);
        }
        /* monotonic per lane */
        for (i = 4; i < sizeof(tab); ++i)
            CHECK(tab[i] >= tab[i - 4], "not monotonic at %u", i);
    }
    CHECK(diffs == T40_LAMBDA_OVERRIDE_COUNT, "%u formula deviations, %u "
          "overrides", diffs, (unsigned int)T40_LAMBDA_OVERRIDE_COUNT);
    if (failures) {
        fprintf(stderr, "lambda_test: %d failures\n", failures);
        return 1;
    }
    printf("lambda_test: AVC/HEVC default lambda tables bit-identical "
           "(%u formula overrides)\n", diffs);
    return 0;
}
