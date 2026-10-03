/*
 * Macroblock rate control of the OEM T20 libimp 3.12.0: H264_SMA_CalMBFlag
 * (0xa4530), H264_SMA_CalMBQP (0xa6eb0), JZM_QPTabConv (0xa3dec) and the
 * macroblock part of JZ_VPU_RC_FRAME_REPEATE_JUDGE_T20 (0xab31c).  See
 * docs/T20_RC.md.
 */
#include "rc_t20.h"
#include "rc_t20_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* E offsets of the macroblock maps: rc_t20_internal.h (MB_*) */

static inline uint32_t absdiff(uint8_t a, uint8_t b)
{
    return a > b ? (uint32_t)(a - b) : (uint32_t)(b - a);
}

/* Luma activity of one 16x8 half macroblock (rows 0, 3, 6 of the half):
 * vertical |r0 - r3| + |r3 - r6|, horizontal |p - right| on rows 0, 3, 6
 * (the column right of the macroblock included).  The OEM sums byte pairs
 * into 8 halfword lanes (MXU2 subuab/dotpuh/adduuh); the totals cannot
 * saturate. */
static void rct20_half_activity(const uint8_t *p, uint32_t stride,
                                uint32_t *v, uint32_t *h)
{
    const uint8_t *r0 = p, *r3 = p + 3u * stride, *r6 = p + 6u * stride;
    uint32_t sv = 0, sh = 0;
    int x;

    for (x = 0; x < 16; x++) {
        sv += absdiff(r0[x], r3[x]) + absdiff(r3[x], r6[x]);
        sh += absdiff(r0[x], r0[x + 1]) + absdiff(r3[x], r3[x + 1]) +
              absdiff(r6[x], r6[x + 1]);
    }
    *v += sv;
    *h += sh;
}

/* H264_SMA_CalMBFlag (0xa4530): macroblock activity class 1..7 into
 * E+336 (thresholds E+272.. vertical, E+296.. horizontal), the centre luma
 * sample into E+0x40150, smoothing by the 8 neighbours, class counts into
 * E+324 (classes 0..3), E+328 (4..5), E+332 (other).  luma: the picture's
 * luma plane with E+224 stride and at least 16 x rows lines (+1 byte). */
void RCT20_CalMBFlag(uint8_t *E, const uint8_t *luma)
{
    uint32_t stride = RU32(E, 224);
    int32_t rows = RI32(E, 232), cols = RI32(E, 228);
    uint32_t n = rct20_mbs(E);
    int32_t r, c, i;

    if (luma && rows > 0 && cols > 0) {
        for (r = 0; r < rows; r++) {
            const uint8_t *top = luma + (size_t)(16u * (uint32_t)r) * stride;
            const uint8_t *bottom = top + 8u * (size_t)stride;

            for (c = 0; c < cols; c++) {
                uint32_t v = 0, h = 0, cls;
                int k;

                rct20_half_activity(top + 16 * c, stride, &v, &h);
                rct20_half_activity(bottom + 16 * c, stride, &v, &h);
                cls = 0;
                for (k = 0; k < 5; k++)
                    if (v < RU32(E, 272 + 4 * k) && h < RU32(E, 296 + 4 * k)) {
                        cls = (uint32_t)k + 1u;
                        break;
                    }
                if (!cls)
                    cls = (v < RU32(E, 292) && h < RU32(E, 316)) ? 6u : 7u;
                RU32(E, MB_CLASS + 4 * (r * cols + c)) = cls;
                RU8(E, MB_LUMA(n) + r * cols + c) = bottom[16 * c + 7];
            }
        }
    }
    /* 0xa4900: smoothing of the inner macroblocks, in place */
    if (rows - 1 >= 2 && cols >= 3) {
        for (r = 1; r < rows - 1; r++) {
            for (c = 1; c < cols - 1; c++) {
                static const int dr[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
                static const int dc[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
                uint32_t *cls = &RU32(E, MB_CLASS + 4 * (r * cols + c));
                int na = 0, nb = 0, nc = 0, k;

                for (k = 0; k < 8; k++) {
                    uint32_t n = RU32(E, MB_CLASS +
                                      4 * ((r + dr[k]) * cols + c + dc[k]));

                    na += n - 1u < 3u;
                    nb += n - 4u < 2u;
                    nc += n - 6u < 2u;
                }
                if (*cls - 1u < 3u) {
                    if (!(nb < 6))
                        *cls = 4;
                    if (!(nc < 6)) {
                        *cls = 6;
                        goto final;
                    }
                }
                if (*cls - 4u < 2u) {
                    if (!(na < 6))
                        *cls = 3;
                    if (!(nc < 6)) {
                        *cls = 6;
                        goto final;
                    }
                }
                if (!(*cls - 6u < 2u))
                    continue;
final:
                if (!(na < 6))
                    *cls = 3;
                if (!(nb < 6))
                    *cls = 5;
            }
        }
    }
    /* 0xa4b94: class counts */
    RI32(E, 324) = 0;
    RI32(E, 328) = 0;
    RI32(E, 332) = 0;
    for (i = 0; rows > 0 && cols > 0 && i < rows * cols; i++) {
        uint32_t cls = RU32(E, MB_CLASS + 4 * i);

        if (cls < 4u)
            RI32(E, 324)++;
        else if (cls - 4u < 2u)
            RI32(E, 328)++;
        else
            RI32(E, 332)++;
    }
}

/* JZM_QPTabConv (0xa3dec): run-length coding of the macroblock QPs into the
 * VPU table (zeroed by the caller): a byte qp | 0x80 starts a value, a
 * byte without bit 7 repeats it (count - 1, up to 127); bytes are packed
 * from the top of each 32-bit word.  Returns the table length in words. */
static void rct20_tab_put(uint8_t *tab, int32_t pos, uint32_t byte)
{
    uint32_t *w = (uint32_t *)(void *)(tab + 4 * (pos >> 2));

    *w |= (byte & 0xffu) << (8 * (3 - (pos & 3)));
}

int32_t RCT20_QPTabConv(const uint8_t *qp, int32_t n, uint8_t *tab)
{
    int32_t out = 1, run = 0, i;

    if (n <= 0)
        return 0;
    *(uint32_t *)(void *)tab = (uint32_t)(qp[0] | 0x80u) << 24;
    for (i = 1; i < n; i++) {
        if (qp[i] == qp[i - 1]) {
            if (i == n - 1) {
                rct20_tab_put(tab, out++, (uint32_t)run);
                if (!(run + 1 < 128))
                    rct20_tab_put(tab, out++, (uint32_t)run); /* OEM: twice */
                run++;
            } else if (run + 1 < 128) {
                run++;
            } else {
                rct20_tab_put(tab, out++, (uint32_t)run);
                run = 0;
            }
        } else {
            if (run)
                rct20_tab_put(tab, out++, (uint32_t)(run - 1));
            rct20_tab_put(tab, out++, qp[i] | 0x80u);
            run = 0;
        }
    }
    return (out + 3) >> 2;
}

/* MB QP from the class offsets around the picture QP. */
static uint8_t rct20_class_qp(const uint8_t *E, uint32_t qp, uint32_t cls)
{
    uint32_t q = (qp + RU8(E, MB_DELTA + cls)) & 0xffu;

    if (!(q < 52u))
        return 51;
    return q ? (uint8_t)q : 1;
}

/* Common tail: VPU table, its length E+0x90158 and the mean QP E+0x90160. */
static void rct20_publish_table(uint8_t *E, const uint8_t *qp, int32_t n,
                                uint64_t sum)
{
    float mean;

    uint32_t m = rct20_mbs(E);      /* == n when n > 0 */

    memset(E + MB_QPTAB(m), 0, MB_QPTAB_SIZE(m));
    RI32(E, RX(344)) = RCT20_QPTabConv(qp, n, E + MB_QPTAB(m));
    mean = (float)(int64_t)sum / (float)n;
    RU32(E, RX(352)) = (mean >= 2147483648.0f)
        ? ((uint32_t)rct20_trunc_f(mean - 2147483648.0f) | 0x80000000u)
        : (uint32_t)rct20_trunc_f(mean);
}

/* Macroblock part of JZ_VPU_RC_FRAME_REPEATE_JUDGE_T20 (0xab31c): the QP
 * map for the re-encode from the classes and the new picture QP E+217. */
void RCT20_MBQpReencode(uint8_t *E)
{
    int32_t cols = RI32(E, 228), rows = RI32(E, 232);
    int32_t n = rct20_mul(cols, rows), r, c;
    uint32_t qp = RU8(E, 217);
    uint64_t sum = 0;
    uint8_t *map = E + MB_MAP(rct20_mbs(E));

    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            int32_t mb = r * cols + c;
            uint32_t cls = RU32(E, MB_CLASS + 4 * mb);
            uint8_t q;

            if (!(qp < 41u) && cls < 4u)
                q = 35;
            else
                q = rct20_class_qp(E, qp, cls);
            map[mb] = q;
            sum += q;
        }
    }
    rct20_publish_table(E, map, n, sum);
}

/* Class QP offsets by scene (0xd15ec, copied to the stack by CalMBQP):
 * row x 7 classes; only the last offset differs between rows. */
static const int8_t rct20_mb_offsets[32][7] = {
#define ROW6 { -1, -1, -1, -1, 0, 3, 6 }
#define ROW7 { -1, -1, -1, -1, 0, 3, 7 }
    ROW6, ROW7, ROW7, ROW7, ROW7, ROW7, ROW7, ROW7,
    ROW6, ROW7, ROW7, ROW7, ROW7, ROW7, ROW7, ROW7,
    ROW6, ROW7, ROW7, ROW7, ROW7, ROW7, ROW7, ROW7,
    ROW6, ROW7, ROW7, ROW7, ROW7, ROW7, ROW7, ROW7,
#undef ROW6
#undef ROW7
};

/* Offset row from the class shares pa (classes 0..3), pb (4..5),
 * pc (6..7) in percent and the picture QP (0xa703c..0xa72f4, 0xa7d60..,
 * 0xa8da0..).  *skip: keep all offsets 0. */
static int rct20_mb_row(float pa, float pb, float pc, uint32_t qp, int *skip)
{
    uint32_t qr = (qp - 25u) & 0xffu;
    int near = qr < 11u;
    int in_a = 11.0f < pa && pa < 22.0f;
    int in_b = 21.0f < pa && pa < 52.0f;
    int in_c = 51.0f < pa && pa < 82.0f;
    int in_d = 81.0f < pa && pa < 92.0f;

    *skip = (pa < 12.0f || 91.0f < pa) ? 1 : qp < 25u;
    if (in_c && !in_d) {
        int c7 = 40.0f < pc, c5 = 20.0f < pb && 20.0f < pc, c3 = 40.0f < pb;

        if (near)
            return c7 ? 8 : c5 ? 9 : c3 ? 10 : 11;
        return c7 ? 12 : c5 ? 13 : c3 ? 14 : 15;
    }
    if (in_d) {
        int d8 = 16.0f < pc, d4 = 7.0f < pb && 7.0f < pc, d1 = 16.0f < pb;

        if (qr < 8u)
            return d8 ? 0 : d4 ? 1 : d1 ? 2 : 3;
        return d8 ? 4 : d4 ? 5 : d1 ? 6 : 7;
    }
    if (in_b) {
        int b1 = 70.0f < pc, b4 = 35.0f < pb && 35.0f < pc, b5 = 70.0f < pb;

        if (near)
            return b1 ? 16 : b4 ? 17 : b5 ? 18 : 19;
        return b1 ? 20 : b4 ? 21 : b5 ? 22 : 23;
    }
    if (in_a) {
        int a2 = 80.0f < pc, a5 = 40.0f < pb && 40.0f < pc, a6 = 80.0f < pb;

        if (near)
            return a2 ? 24 : a5 ? 25 : a6 ? 26 : 27;
        return a2 ? 28 : a5 ? 29 : a6 ? 30 : 31;
    }
    return 32;
}

static uint32_t rct20_f2u(float f)
{
    return (f >= 2147483648.0f)
        ? ((uint32_t)rct20_trunc_f(f - 2147483648.0f) | 0x80000000u)
        : (uint32_t)rct20_trunc_f(f);
}

/* H264_SMA_CalMBQP (0xa6eb0): the macroblock QP map for the picture: the
 * class offsets chosen from the class shares, dark flat macroblocks
 * (centre luma < 128) a further log2(luma + 1) - 8, then the VPU table.
 * ROI regions (E+0x90170, IMP ROI) and the /tmp/smad, /tmp/roic debug
 * dumps are not reproduced: OpenIMP has no ROI on the T20. */
void RCT20_CalMBQP(uint8_t *E)
{
    int32_t cols = RI32(E, 228), rows = RI32(E, 232);
    int32_t n = rct20_mul(cols, rows), r, c, row, i;
    float fn = (float)n;
    float pa = (float)(double)RU32(E, 324) * 100.0f / fn;
    float pb = (float)(double)RU32(E, 328) * 100.0f / fn;
    float pc = (float)(double)RU32(E, 332) * 100.0f / fn;
    uint32_t qp = RU8(E, 217), q0 = qp;
    uint64_t sum = 0;
    uint8_t *map;
    int skip;

    RU32(E, RX(356)) = rct20_f2u(pa * 100.0f);
    row = rct20_mb_row(pa, pb, pc, qp, &skip);
    memset(E + MB_DELTA + 1, 0, 7);
    if (!skip && row < 32)
        for (i = 0; i < 7; i++)
            RU8(E, MB_DELTA + 1 + i) = (uint8_t)rct20_mb_offsets[row][i];
    map = E + MB_MAP(rct20_mbs(E));
    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            int32_t mb = r * cols + c;
            uint32_t cls = RU32(E, MB_CLASS + 4 * mb);
            uint32_t d = RU8(E, MB_DELTA + cls), q;

            if (cls < 4u) {
                uint8_t y = RU8(E, MB_LUMA(n) + mb);

                if (!(q0 < 41u))
                    q0 = 40;            /* stays for the rest of the picture */
                if (y < 128u) {
                    float l = (float)log2((double)(float)(y + 1u));

                    q = d + (q0 - 8u) + (uint32_t)rct20_trunc_f(l);
                } else {
                    q = q0 + d;
                }
            } else {
                q = q0 + d;
            }
            q &= 0xffu;
            if (!(q < 52u))
                q = 51;
            else if (!q)
                q = 1;
            map[mb] = (uint8_t)q;
            sum += q;
        }
    }
    rct20_publish_table(E, map, n, sum);
}
