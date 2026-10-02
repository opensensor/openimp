#ifndef OPENIMP_T40_LAMBDA_H
#define OPENIMP_T40_LAMBDA_H

/*
 * Default AVPU lambda tables (EP1 offset 0, 52 QPs x 4 one-byte lanes),
 * computed from the HM/JM rate-distortion lambda model instead of being
 * copied out of a vendor binary.  See docs/T31_LAMBDA.md for the
 * derivation.
 *
 *   lambda(QP) = f * 2^((QP - 12) / 3)
 *   entry      = R(sqrt(lambda(QP))) = R(sqrt(f) * 2^((QP - 12) / 6)),
 *                clamped to >= 1
 *
 * Lane order follows AL_GetLambda's slice-type index (B=0, P=1, I=2);
 * lane 3 is the second hardware lane and uses the P-slice factor.
 *
 *   lane   AVC               HEVC
 *   0      f=1.00 round      f=1.34 floor
 *   1      f=0.42 ceil       f=0.42 ceil
 *   2      f=0.54 ceil       f=0.54 ceil
 *   3      f=0.42 ceil       f=0.42 ceil
 *
 * The formula reproduces 404 of the 416 bytes.  The 12 remaining entries
 * deviate by exactly +-1 from every single-parameter fit (they are tuning
 * irregularities, not rounding noise) and are listed in
 * t40_lambda_overrides below so the shipped tables stay bit-identical to
 * the ones the AVPU has been validated with.  Build with
 * -DOPENIMP_LDA_FORMULA_ONLY to drop the overrides (device testing only).
 *
 * The arithmetic uses only IEEE double multiplies of the constants below
 * (no libm), so host and MIPS soft-float produce the same bytes; the
 * closest any entry gets to a rounding boundary is 0.0013.
 */

#include <stdint.h>

#define T40_LAMBDA_QP_COUNT 52u
#define T40_LAMBDA_TABLE_SIZE (T40_LAMBDA_QP_COUNT * 4u)

enum t40_lambda_round {
    T40_LDA_FLOOR,
    T40_LDA_ROUND,
    T40_LDA_CEIL
};

typedef struct {
    double sqrt_f; /* sqrt of the lambda factor f */
    enum t40_lambda_round mode;
} t40_lambda_lane;

/* 2^(k/6), k = 0..5 */
static const double t40_lambda_pow2_sixth[6] = {
    1.0, 1.122462048309373, 1.259921049894873,
    1.414213562373095, 1.587401051968199, 1.781797436280679,
};

#define T40_LDA_SQRT_1_00 1.0
#define T40_LDA_SQRT_0_42 0.648074069840786
#define T40_LDA_SQRT_0_54 0.734846922834953
#define T40_LDA_SQRT_1_34 1.157583690279023

static const t40_lambda_lane t40_lambda_lanes[2][4] = {
    { /* AVC */
        { T40_LDA_SQRT_1_00, T40_LDA_ROUND },
        { T40_LDA_SQRT_0_42, T40_LDA_CEIL },
        { T40_LDA_SQRT_0_54, T40_LDA_CEIL },
        { T40_LDA_SQRT_0_42, T40_LDA_CEIL },
    },
    { /* HEVC */
        { T40_LDA_SQRT_1_34, T40_LDA_FLOOR },
        { T40_LDA_SQRT_0_42, T40_LDA_CEIL },
        { T40_LDA_SQRT_0_54, T40_LDA_CEIL },
        { T40_LDA_SQRT_0_42, T40_LDA_CEIL },
    },
};

typedef struct {
    uint8_t hevc;
    uint8_t qp;
    uint8_t lane;
    uint8_t value;
} t40_lambda_override;

/* Entries where the validated table deviates from the formula (+-1). */
static const t40_lambda_override t40_lambda_overrides[] = {
    { 0, 37, 1, 13 }, { 0, 37, 3, 13 },
    { 1, 16, 0, 2 },  { 1, 25, 0, 4 },  { 1, 29, 0, 7 },  { 1, 34, 0, 15 },
    { 1, 35, 0, 17 }, { 1, 36, 0, 19 }, { 1, 37, 0, 21 }, { 1, 38, 0, 24 },
    { 1, 43, 0, 42 }, { 1, 44, 0, 47 },
};

#define T40_LAMBDA_OVERRIDE_COUNT \
    (sizeof(t40_lambda_overrides) / sizeof(t40_lambda_overrides[0]))

static inline uint8_t t40_lambda_formula(int hevc, unsigned int qp,
                                         unsigned int lane)
{
    const t40_lambda_lane *l = &t40_lambda_lanes[hevc ? 1 : 0][lane & 3u];
    /* (qp - 12) / 6 with floor semantics for qp < 12 */
    unsigned int e = qp + 48u; /* (qp - 12) + 60 */
    int octave = (int)(e / 6u) - 10;
    double x = l->sqrt_f * t40_lambda_pow2_sixth[e % 6u];
    unsigned int v;

    for (; octave > 0; --octave)
        x *= 2.0;
    for (; octave < 0; ++octave)
        x *= 0.5;
    switch (l->mode) {
    case T40_LDA_ROUND:
        v = (unsigned int)(x + 0.5);
        break;
    case T40_LDA_CEIL:
        v = (unsigned int)x;
        if ((double)v < x)
            ++v;
        break;
    default:
        v = (unsigned int)x;
        break;
    }
    if (v < 1u)
        v = 1u;
    if (v > 255u)
        v = 255u;
    return (uint8_t)v;
}

/* Fill out[0..207]; apply_overrides=0 gives the pure formula table. */
static inline void t40_lambda_build(uint8_t *out, int hevc,
                                    int apply_overrides)
{
    unsigned int qp, lane, i;

    for (qp = 0u; qp < T40_LAMBDA_QP_COUNT; ++qp)
        for (lane = 0u; lane < 4u; ++lane)
            out[qp * 4u + lane] = t40_lambda_formula(hevc, qp, lane);
    if (!apply_overrides)
        return;
    for (i = 0u; i < T40_LAMBDA_OVERRIDE_COUNT; ++i) {
        const t40_lambda_override *o = &t40_lambda_overrides[i];

        if ((o->hevc != 0) == (hevc != 0))
            out[o->qp * 4u + o->lane] = o->value;
    }
}

static inline void t40_lambda_default_table(uint8_t *out, int hevc)
{
#ifdef OPENIMP_LDA_FORMULA_ONLY
    t40_lambda_build(out, hevc, 0);
#else
    t40_lambda_build(out, hevc, 1);
#endif
}

#endif
