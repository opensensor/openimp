/*
 * Picture rate control of the Ingenic T10 H.264 encoder, reimplemented from
 * the OEM T20 libimp 3.12.0, which runs it when get_cpu_id reports a T10:
 * JZ_VPU_RC_VIDEO_CFG / JZ_VPU_RC_FRAME_RC / JZ_VPU_RC_FRAME_REPEATE_JUDGE
 * and the exported RC_H264_* helpers (0x9eec0..0xa2b2c).  See rc_t10.h and
 * docs/T20_RC.md ("T10").
 *
 * The three OEM blocks keep their layout and are addressed by OEM byte
 * offset: E ("h->common.eprc", i264e parameter + 476), P (rcPara, 88 bytes)
 * and S (rcSt, 808 bytes).  Comments give the OEM code address.
 */
#include "rc_t10.h"
#include "../rc_t20/rc_t20_internal.h"

#include <math.h>
#include <string.h>

#define T10_TRUNC_D rct20_trunc_d
#define T10_TRUNC_F rct20_trunc_f
#define ADD rct20_add
#define SUB rct20_sub
#define MUL rct20_mul
#define DIV rct20_div

/* ---------------------------------------------------------------- tables */

/* RC_H264_updateQp.isra.0 (0x9eec0) steps, 0xcf240 */
static const float rct10_ratio[13] = {
    1.0f, 1.125f, 1.25f, 1.375f, 1.625f, 1.75f, 2.0f,
    2.25f, 2.5f, 2.75f, 3.25f, 3.5f, 4.0f,
};

/* qualLvlBrThr2 (0xec110) followed by SmartQualLvl2 (0xec150): the OEM
 * indexes each with the quality level and no bound check. */
static const double rct10_qual[16] = {
    0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1,
    0.8, 0.6, 0.4, 0.3, 0.25, 0.2, 0.15, 0.1,
};

/* RC_H264_calcVBRGopComplexity QP classes (0xcf274, 7 x 6) and steps */
static const int32_t rct10_vbr_class[7][6] = {
    { 20, 23, 26, 29, 32, 35 }, { 22, 25, 28, 31, 34, 37 },
    { 24, 27, 30, 33, 36, 39 }, { 26, 29, 31, 34, 37, 40 },
    { 28, 31, 34, 37, 40, 43 }, { 30, 33, 36, 39, 42, 45 },
    { 32, 35, 38, 41, 44, 47 },
};

static const int32_t rct10_vbr_pct[6] = { -25, -18, -10, 0, 10, 18 };

/* ----------------------------------------------------------- QP helpers */

/* RC_H264_updateQp.isra.0 (0x9eec0): index of the first step above x;
 * 13 above the last step, 0 when x equals it. */
static int32_t rct10_update_qp(double x)
{
    int i;

    for (i = 0; i < 13; i++) {
        if ((double)rct10_ratio[12] < x)
            return 13;
        if (x < (double)rct10_ratio[i])
            return i;
    }
    return 0;
}

static int32_t rct10_clip51(int32_t q)
{
    if (!(q < 52))
        return 51;
    return q < 0 ? 0 : q;
}

/* RC_H264_updateQp_expand.isra.1 (0x9ef70): QP distance for the ratio a/b
 * in 12-QP octaves up to 16, clipped to 0..51. */
static int32_t rct10_update_qp_expand(int32_t a, int32_t b)
{
    double x;

    if (!(a < b)) {
        x = (double)a / (double)b;
        if (x < 4.0)
            return rct10_clip51(rct10_update_qp(x));
        if (x < 8.0)
            return rct10_clip51(rct10_update_qp(x - 4.0) + 12);
        if (x < 12.0)
            return rct10_clip51(rct10_update_qp(x - 8.0) + 24);
        if (x < 16.0)
            return rct10_clip51(rct10_update_qp(x - 12.0) + 36);
        return rct10_clip51(rct10_update_qp(x - 16.0) + 48);
    }
    x = (double)b / (double)a;
    if (x < 4.0)
        return rct10_clip51(-rct10_update_qp(x));
    if (x < 8.0)
        return rct10_clip51(-12 - rct10_update_qp(x - 4.0));
    if (x < 12.0)
        return rct10_clip51(-24 - rct10_update_qp(x - 8.0));
    if (x < 16.0)
        return rct10_clip51(-36 - rct10_update_qp(x - 12.0));
    return rct10_clip51(-48 - rct10_update_qp(x - 16.0));
}

/* eprc_qp2qscale (0x9f254) */
static float rct10_qp2qscale(float qp)
{
    return (float)(pow(2.0, ((double)qp - 12.0) / 6.0) * 0.85);
}

/* eprc_qscale2qp (0x9f2bc) */
static float rct10_qscale2qp(float qs)
{
    float q = (float)(log((double)qs / 0.85) * 6.0 / 0.6931471805599453);

    return (q < 0.0f) ? 12.0f : q + 12.0f;
}

/* eprc_clip(eprc_min(20, n), 1, 20) */
static int32_t rct10_window(int32_t n)
{
    if (!(n < 20))
        n = 20;
    if (n < 1)
        n = 1;
    return n;
}

/* ------------------------------------------------------- model estimate */

/* estimate_qp (0x9f384): JM quadratic R-Q model, MAD from the picture
 * complexity S+724 with the MAD model C1 S+448, C2 S+452. */
static int32_t rct10_estimate_qp(uint8_t *S, int32_t bits)
{
    float mad = (float)RI32(S, 724) * RF32(S, 448) + RF32(S, 452);
    float x1 = RF32(S, 712), x2 = RF32(S, 716);
    float ftgt = (float)bits;
    float f6 = x1 * mad;
    float qs;

    if ((double)x2 < 1e-6) {
        qs = f6 / ftgt;
    } else {
        float f1 = f6 * mad;
        float f0 = x2 * 4.0f * mad * ftgt;
        float disc = x1 * f1 + f0;

        if (disc < 0.0f) {
            qs = f6 / ftgt;
        } else {
            double root = sqrt((double)disc);
            float lin = mad * x1;

            if (root - (double)lin < 1e-6) {
                qs = lin / ftgt;
            } else {
                float num = (x2 + x2) * mad;

                qs = (float)((double)num / (root - (double)(mad * x1)));
            }
        }
    }
    return T10_TRUNC_F(rct10_qscale2qp(qs));
}

/* RDModelEstimator (0x9f69c): window of n (Qstep S+628, bits/MAD S+544),
 * rejected flags S+460, X1 S+712, X2 S+716. */
static void rct10_rd_model(int32_t n, uint8_t *S)
{
    int32_t real = n, i;
    int est = 0;
    double one = 0.0;
    double a00 = 0.0, a01 = 0.0, a11 = 0.0, b0 = 0.0, b1 = 0.0, mv;

    RF32(S, 716) = 0.0f;
    RF32(S, 712) = 0.0f;
    if (n <= 0)
        return;
    for (i = 0; i < n; i++)
        if (RI32(S, 460 + 4 * i))
            real--;
    for (i = 0; i < n; i++)
        if (!RI32(S, 460 + 4 * i))
            one = (double)RF32(S, 628 + 4 * i);
    for (i = 0; i < n; i++) {
        float q = RF32(S, 628 + 4 * i);

        if (RI32(S, 460 + 4 * i))
            continue;
        if ((double)q != one)
            est = 1;
        RF32(S, 712) = RF32(S, 712) + (q * RF32(S, 544 + 4 * i)) / (float)real;
    }
    if (real <= 0 || !est)
        return;
    for (i = 0; i < n; i++) {
        float q = RF32(S, 628 + 4 * i), r = RF32(S, 544 + 4 * i);

        if (RI32(S, 460 + 4 * i))
            continue;
        a00 = a00 + 1.0;
        a01 = a01 + 1.0 / (double)q;
        a11 = a11 + 1.0 / (double)(q * q);
        b0 = b0 + (double)(q * r);
        b1 = b1 + (double)r;
    }
    mv = a00 * a11 - a01 * a01;
    if (1e-6 < fabs(mv)) {
        RF32(S, 712) = (float)((a11 * b0 - a01 * b1) / mv);
        RF32(S, 716) = (float)((a00 * b1 - a01 * b0) / mv);
    } else {
        RF32(S, 712) = (float)(b0 / a00);
    }
}

/* MADModelEstimator (0x9f50c): picture MAD S+280 against the reference
 * MAD S+364, rejected flags S+196, C1 S+448, C2 S+452. */
static void rct10_mad_model(int32_t n, uint8_t *S)
{
    int32_t real = n, i;
    int est = 0;
    float one = 0.0f;
    float a00 = 0.0f, a01 = 0.0f, a11 = 0.0f, b0 = 0.0f, b1 = 0.0f, mv;

    RF32(S, 452) = 0.0f;
    RF32(S, 448) = 0.0f;
    if (n <= 0)
        return;
    for (i = 0; i < n; i++)
        if (RI32(S, 196 + 4 * i))
            real--;
    for (i = 0; i < n; i++)
        if (!RI32(S, 196 + 4 * i))
            one = RF32(S, 280 + 4 * i);
    for (i = 0; i < n; i++) {
        float pic = RF32(S, 280 + 4 * i);

        if (RI32(S, 196 + 4 * i))
            continue;
        if (pic != one)
            est = 1;
        RF32(S, 448) = RF32(S, 448) +
                       pic / ((float)real * RF32(S, 364 + 4 * i));
    }
    if (real <= 0 || !est)
        return;
    for (i = 0; i < n; i++) {
        float ref = RF32(S, 364 + 4 * i), pic = RF32(S, 280 + 4 * i);

        if (RI32(S, 196 + 4 * i))
            continue;
        a00 = a00 + 1.0f;
        a11 = ref * ref + a11;
        b1 = ref * pic + b1;
        a01 = a01 + ref;
        b0 = b0 + pic;
    }
    mv = a00 * a11 - a01 * a01;
    if (1e-6 < (double)fabsf(mv)) {
        RF32(S, 452) = (a11 * b0 - a01 * b1) / mv;
        RF32(S, 448) = (a00 * b1 - a01 * b0) / mv;
    } else {
        RF32(S, 448) = b0 / a01;
    }
}

/* Outlier flags of update_RDModel / update_MADModel: |err| above the RMS
 * error (the OEM divides by n here, unlike JM). */
static void rct10_reject(const float *err, int32_t n, float sum, uint8_t *S,
                         int flags)
{
    float thr = 0.0f;
    int32_t i;

    if (!(n < 3))
        thr = (float)sqrt((double)(sum / (float)n));
    for (i = 0; i < n; i++)
        if (thr < fabsf(err[i]))
            RI32(S, flags + 4 * i) = 1;
}

/* update_RDModel (0x9f860) */
static void rct10_update_rd(uint8_t *S)
{
    float qp = (float)RI32(S, 40);
    float cmpx = (float)RI32(S, 724);
    int32_t bits = RI32(S, 120);
    float err[20], sum, x1, x2;
    int32_t n, i;

    for (i = 19; i > 0; i--) {
        RF32(S, 628 + 4 * i) = RF32(S, 628 + 4 * (i - 1));
        RF32(S, 544 + 4 * i) = RF32(S, 544 + 4 * (i - 1));
    }
    RF32(S, 628) = rct10_qp2qscale(qp);
    RF32(S, 544) = (float)bits / cmpx;
    if (RI32(S, 24) < 2)
        return;
    n = rct10_window(ADD(RI32(S, 720), 1));
    RI32(S, 720) = n;
    for (i = 0; i < 20; i++)
        RI32(S, 460 + 4 * i) = 0;
    rct10_rd_model(n, S);
    x1 = RF32(S, 712);
    x2 = RF32(S, 716);
    sum = 0.0f;
    for (i = 0; i < n; i++) {
        float q = RF32(S, 628 + 4 * i);

        err[i] = (x1 / q + x2 / (q * q)) - RF32(S, 544 + 4 * i);
        sum = err[i] * err[i] + sum;
    }
    rct10_reject(err, n, sum, S, 460);
    RI32(S, 460) = 0;
    rct10_rd_model(n, S);
}

/* update_MADModel (0x9fa68) */
static void rct10_update_mad(uint8_t *S)
{
    float cmpx = (float)RI32(S, 724);
    float err[20], sum, c1, c2;
    int32_t n, i;

    for (i = 19; i > 0; i--) {
        RF32(S, 280 + 4 * i) = RF32(S, 280 + 4 * (i - 1));
        RF32(S, 364 + 4 * i) = RF32(S, 364 + 4 * (i - 1));
    }
    RF32(S, 364) = RF32(S, 284);
    RF32(S, 280) = cmpx;
    if (RI32(S, 24) < 2)
        return;
    n = rct10_window(ADD(RI32(S, 456), 1));
    RI32(S, 456) = n;
    for (i = 0; i < 20; i++)
        RI32(S, 196 + 4 * i) = 0;
    rct10_mad_model(n, S);
    c1 = RF32(S, 448);
    c2 = RF32(S, 452);
    sum = 0.0f;
    for (i = 0; i < n; i++) {
        err[i] = (c1 * RF32(S, 364 + 4 * i) + c2) - RF32(S, 280 + 4 * i);
        sum = err[i] * err[i] + sum;
    }
    rct10_reject(err, n, sum, S, 196);
    RI32(S, 196) = 0;
    rct10_mad_model(n, S);
}

/* RC_H264_updateModelCoeff (0x9feb0): after a P picture. */
static void rct10_update_model_coeff(uint8_t *P, uint8_t *S)
{
    (void)P;
    if (RI32(S, 36) != 3)
        return;
    rct10_update_rd(S);
    rct10_update_mad(S);
}

/* ------------------------------------------------------- picture QP */

/* RC_H264_frameType (0x9fe60) */
static void rct10_frame_type(uint8_t *P, uint8_t *S)
{
    if (!(RI32(S, 0) < 0x989681))
        RI32(S, 0) = 1;
    RI32(S, 36) = RI32(S, 32);
    RI32(S, 32) = (RI32(P, 40) == 1) ? 1 : 3;
}

/* RC_H264_qpLimit (0x9fd04) */
static void rct10_qp_limit(uint8_t *P, uint8_t *S)
{
    int32_t type = RI32(S, 32);
    int32_t qp = RI32(S, 40);

    if (type == 3) {
        if (RI32(S, 44) < qp)
            qp = RI32(S, 40) = RI32(S, 44);
        if (qp < RI32(S, 48))
            qp = RI32(S, 40) = RI32(S, 48);
        if (RI32(S, 52) < qp)
            qp = RI32(S, 40) = RI32(S, 52);
        if (qp < RI32(S, 56))
            qp = RI32(S, 40) = RI32(S, 56);
    } else if (type != 1) {
        return;
    }
    if (RI32(S, 788) == 1) {
        int32_t lim = ADD(RI32(P, 72), RI32(P, 16));

        if (!(lim < 52))
            lim = 51;
        if (lim < qp)
            qp = RI32(S, 40) = lim;
    } else if (RI32(P, 16) < qp) {
        qp = RI32(S, 40) = RI32(P, 16);
    }
    if (qp < RI32(P, 20))
        RI32(S, 40) = RI32(P, 20);
}

/* RC_H264_updateMaxQp (0x9fc5c): NewMaxQp trigger S+788. */
static void rct10_update_max_qp(uint8_t *P, uint8_t *S, int32_t bits)
{
    double ratio = (double)RI32(S, 128) / (double)bits;
    int32_t q = rct10_update_qp(ratio);

    RI32(S, 788) = ((double)RF32(P, 76) < ratio ||
                    RI32(P, 16) < ADD(RI32(S, 76), q)) ? 1 : 0;
}

/* RC_H264_calcFirstIQp (0x9ff28) */
static void rct10_calc_first_iqp(uint8_t *P, uint8_t *S, int32_t bits)
{
    int32_t rate = MUL(bits, RI32(P, 8)) / 25600;
    int32_t pix = (int32_t)((uint32_t)RI32(P, 44) << 8);
    int32_t qp;

    if (pix < 921601)
        qp = -rct10_update_qp((double)(921600.0f / (float)pix));
    else
        qp = rct10_update_qp((double)((float)pix / 921600.0f));
    if (rate < 1025)
        qp += rct10_update_qp((double)(float)(1024.0 / (double)rate));
    else
        qp -= rct10_update_qp((double)((float)rate * 0.0009765625f));
    RI32(S, 40) = ADD(RI32(P, 32), qp + 37);
}

/* RC_H264_calcIFrameQp (0xa0098) */
static void rct10_calc_iframe_qp(uint8_t *P, uint8_t *S)
{
    int32_t last_i, avg_p, d, a, v, lim;

    if (RI32(S, 36) == 1) {
        RI32(S, 40) = RI32(S, 64);
        return;
    }
    last_i = RI32(S, 64);
    avg_p = RI32(S, 76);
    d = RI32(P, 12) / 15;
    if (!(d < 3))
        d = 2;
    a = SUB(avg_p, d);
    if (a < last_i - 2) {
        v = a - 1;
        goto bias;
    }
    if (last_i + 1 < a)
        v = (last_i + 2 < last_i - 1) ? last_i - 2 : last_i + 2;
    else
        v = (a < last_i - 1) ? last_i - 2 : a;
    lim = RI32(S, 68) - 1;
    if (!(v < lim))
        v--;
bias:
    if (RI32(P, 4) < SUB(avg_p, v))
        v = SUB(avg_p, RI32(P, 4));
    RI32(S, 40) = ADD(RI32(P, 32), v);
}

/* RC_H264_calcPFrameQp (0xa0170): picture budget, then the R-Q model. */
static void rct10_calc_pframe_qp(uint8_t *P, uint8_t *S)
{
    int32_t gop = RI32(P, 12);
    int32_t n = RI32(S, 24);
    int32_t used, avg, tgt;

    if (RI32(S, 36) == 1) {
        RI32(S, 40) = RI32(S, 64);
        return;
    }
    if (n == 2) {
        RI32(S, 192) = DIV(RI32(S, 180), gop - 2);
        used = RI32(S, 180);
    } else {
        used = RI32(S, 184);
    }
    used = SUB(used, RI32(S, 192));
    RI32(S, 184) = used;
    avg = RI32(S, 116);
    if (RI32(S, 188) >= 0) {
        int32_t q = DIV(RI32(S, 188), SUB(gop, n));
        int32_t dev = T10_TRUNC_D((double)SUB(used, RI32(S, 180)) * 0.5);
        int32_t per = ADD(avg, dev);
        int32_t est = T10_TRUNC_D((double)q * 0.5 + (double)per * 0.5);

        if (!(est < DIV(MUL(avg, 3), 5))) {             /* 0xa0288 */
            int32_t last = RI32(S, 120);

            tgt = est;
            if (avg < est && avg < last) {
                if (MUL(avg, 3) < last) {
                    tgt = MUL(avg, 2);
                } else {
                    int32_t v = DIV(MUL(last, 2), 3);

                    if (avg < v)
                        tgt = v;
                    else
                        tgt = (last < est) ? last : est;
                }
            }
            goto model;
        }
    }
    tgt = (RI32(S, 780) <= 0) ? DIV(MUL(avg, 3), 5) : avg;   /* 0xa0258 */
model:
    RI32(S, 40) = rct10_estimate_qp(S, tgt);
}

/* RC_H264_CBR_frames / RC_H264_VBR_frames (0xa0580, 0xa0f40) */
static void rct10_frames(uint8_t *P, uint8_t *S)
{
    int32_t type = RI32(S, 32);

    if (type == 1) {
        if (RI32(S, 0) == 0)
            rct10_calc_first_iqp(P, S, RI32(P, 56));
        else
            rct10_calc_iframe_qp(P, S);
    } else if (type == 3) {
        rct10_calc_pframe_qp(P, S);
    }
    rct10_qp_limit(P, S);
    RI32(S, 0) = ADD(RI32(S, 0), 1);
    RI32(S, 24) = ADD(RI32(S, 24), 1);
}

/* RC_H264_SceneJudge (0x9f150): motion class S+780 from the complexity
 * ratio of the last P to the last I (SMART: the GOP head) picture. */
static void rct10_scene_judge(uint8_t *P, uint8_t *S)
{
    double ref, p;
    int32_t v;

    if (RI32(S, 32) == 1) {
        RI32(S, 800) = 100;
        return;
    }
    ref = (RI32(P, 80) == 3) ? *(double *)(S + 744) : *(double *)(S + 728);
    p = *(double *)(S + 736);
    if (ref < p) {
        RI32(S, 800) = 100;
        RI32(S, 780) = 2;
        return;
    }
    if (0.0 < ref) {
        v = T10_TRUNC_D(p * 100.0 / ref);
        RI32(S, 800) = v;
    } else {
        v = RI32(S, 800);
    }
    if (!(v < 41))
        RI32(S, 780) = 2;
    else
        RI32(S, 780) = (v < 26) ? 0 : 1;
}

static double rct10_cplx(uint8_t *P, uint8_t *S)
{
    return (double)RI32(S, 724) / (double)RI32(P, 44);
}

/* ------------------------------------------------------------------ CBR */

/* RC_H264_CBR_init (0xa0540) */
static void rct10_cbr_init(uint8_t *P, uint8_t *S)
{
    int32_t rate = RI32(P, 56), gop = RI32(P, 12), fps = RI32(P, 8);

    RI32(S, 128) = 0;
    RI32(S, 84) = 0;
    RI32(S, 24) = 0;
    RI32(S, 0) = 0;
    RI32(S, 4) = 0;
    RF32(S, 712) = 1.0f;
    RF32(S, 448) = 1.0f;
    RI32(S, 788) = 0;
    RI32(S, 780) = 0;
    RI32(S, 180) = 0;
    RI32(S, 124) = DIV(MUL(rate, gop), fps);
    RI32(S, 188) = RI32(S, 124);
    RI32(S, 116) = DIV(rate, fps);
}

/* Common head of the *_updateGopInfo functions after a coded picture. */
static void rct10_account(uint8_t *P, uint8_t *S)
{
    int32_t bits = RI32(S, 120), qp = RI32(S, 40);

    RI32(S, 48) = SUB(qp, RI32(P, 24));
    RI32(S, 188) = SUB(RI32(S, 188), bits);
    RI32(S, 128) = ADD(RI32(S, 128), bits);
    RI32(S, 44) = ADD(qp, RI32(P, 24));
    RI32(S, 180) = ADD(RI32(S, 180), SUB(bits, RI32(S, 116)));
}

/* RC_H264_CBR_updateGopInfo (0xa03a8) */
static void rct10_cbr_update_gop(uint8_t *P, uint8_t *S)
{
    int32_t qp;

    rct10_frame_type(P, S);
    if (RI32(S, 0) == 0)
        return;
    rct10_account(P, S);
    qp = RI32(S, 40);
    if (RI32(S, 36) == 1) {
        RI32(S, 56) = SUB(qp, RI32(P, 28));
        RI32(S, 52) = ADD(qp, RI32(P, 28));
        RI32(S, 64) = qp;
        *(double *)(S + 728) = rct10_cplx(P, S);
    } else if (RI32(S, 36) == 3) {
        RI32(S, 84) = ADD(RI32(S, 84), qp);
        RI32(S, 68) = qp;
        *(double *)(S + 736) = rct10_cplx(P, S);
    }
    rct10_scene_judge(P, S);
    if (RI32(P, 40) != 1)
        return;
    if (!(RI32(S, 24) < 2))
        RI32(S, 76) = T10_TRUNC_D((double)RI32(S, 84) /
                                  (double)(RI32(S, 24) - 1) + 0.5);
    if (RI32(S, 24) == RI32(P, 12))
        rct10_update_max_qp(P, S, RI32(S, 124));
    if (RI32(P, 36) != 0) {
        RI32(S, 188) = ADD(RI32(S, 188), RI32(S, 124));
    } else {
        RI32(S, 180) = 0;
        RI32(S, 188) = RI32(S, 124);
    }
    RI32(S, 24) = 0;
    RI32(S, 128) = 0;
    RI32(S, 4) = ADD(RI32(S, 4), 1);
    RI32(S, 84) = 0;
}

/* ------------------------------------------------------------------ VBR */

/* RC_H264_VBR_init (0xa0b1c) */
static void rct10_vbr_init(uint8_t *P, uint8_t *S)
{
    int32_t gop = RI32(P, 12), fps = RI32(P, 8);
    int32_t lo = T10_TRUNC_D((double)RI32(P, 64) * rct10_qual[RU32(P, 68) & 15]);
    int32_t floor_ = RI32(P, 60);

    if (!(lo < floor_))
        floor_ = lo;
    RI32(S, 804) = floor_;
    RI32(S, 752) = 0;
    RI32(S, 756) = 0;
    RF32(S, 712) = 1.0f;
    RF32(S, 448) = 1.0f;
    RI32(S, 128) = 0;
    RI32(S, 84) = 0;
    RI32(S, 24) = 0;
    RI32(S, 0) = 0;
    RI32(S, 4) = 0;
    RI32(S, 796) = 0;
    RI32(S, 788) = 0;
    RI32(S, 780) = 0;
    RI32(S, 180) = 0;
    RI32(S, 140) = DIV(MUL(gop, RI32(P, 56)), fps);
    RI32(S, 124) = RI32(S, 140);
    RI32(S, 188) = RI32(S, 140);
    RI32(S, 144) = DIV(MUL(gop, RI32(P, 64)), fps);
    RI32(S, 136) = DIV(MUL(gop, floor_), fps);
    memset(S + 92, 0, 20);
    memset(S + 760, 0, 20);
    memset(S + 160, 0, 20);
}

/* RC_H264_updateVBRGopReAllocBits (0xa0a58): a GOP running above twice its
 * share switches to the maximum budget once. */
static void rct10_vbr_realloc(uint8_t *P, uint8_t *S)
{
    int32_t n = RI32(S, 24), gop = RI32(P, 12), budget, per, left;

    if (n < 3 || RI32(S, 796) != 0 || !(n < gop))
        return;
    if (!(MUL(RI32(S, 116), 2) < RI32(S, 120)))
        return;
    budget = RI32(S, 140);
    if (!(RI32(S, 124) < budget))
        budget = DIV(MUL(gop, RI32(P, 64)), RI32(P, 8));
    RI32(S, 124) = budget;
    RI32(S, 188) = SUB(budget, RI32(S, 128));
    RI32(S, 796) = 1;
    per = DIV(budget, gop);
    RI32(S, 116) = per;
    left = SUB(RI32(S, 128), MUL(n, per));
    RI32(S, 180) = left;
    RI32(S, 184) = left;
    RI32(S, 192) = DIV(left, gop - n);
}

/* RC_H264_calcVBRGopComplexity (0xa0650) */
static double rct10_vbr_complexity(uint8_t *P, uint8_t *S)
{
    int32_t rate = DIV(MUL(RI32(S, 128), RI32(P, 8)), RI32(P, 12));
    int32_t q = rct10_update_qp((double)RI32(P, 64) / (double)rate);
    int32_t v = SUB(RI32(S, 72), q);
    int32_t lvl = RI32(P, 68);
    const int32_t *row = rct10_vbr_class[(lvl >= 0 && lvl < 7) ? lvl : 0];
    int i;

    if (row[5] < v)
        return 1.25;
    for (i = 0; i < 6; i++)
        if (v < row[i])
            return ((double)rct10_vbr_pct[i] + 100.0) / 100.0;
    return 1.0;
}

static int64_t rct10_s64(const uint8_t *S, int o)
{
    return (int64_t)(((uint64_t)RU32(S, o + 4) << 32) | RU32(S, o));
}

/* RC_H264_calcVBRGopAllocBits (0xa0810): next GOP budget from the
 * complexity history (5 GOPs). */
static int32_t rct10_vbr_alloc(uint8_t *P, uint8_t *S)
{
    double c = rct10_vbr_complexity(P, S);
    int32_t gop = RI32(P, 12), fps = RI32(P, 8);
    int32_t n = (RI32(S, 4) < 6) ? RI32(S, 4) : 5;
    int64_t sum_c = 0, sum_b = 0;
    int32_t sum_q = 0, cur, avg_c, avg_q, avg_b, t;
    double d4, d2, d20;
    int i;

    for (i = 4; i > 0; i--) {
        RI32(S, 92 + 4 * i) = RI32(S, 88 + 4 * i);
        RI32(S, 760 + 4 * i) = RI32(S, 756 + 4 * i);
        RI32(S, 160 + 4 * i) = RI32(S, 156 + 4 * i);
    }
    RI32(S, 92) = RI32(S, 76);
    cur = (int32_t)(rct10_s64(S, 752) / (int64_t)gop);
    RI32(S, 760) = cur;
    RI32(S, 160) = DIV(MUL(fps, RI32(S, 128)), gop);
    for (i = 4; i >= 0; i--) {
        sum_c += (int64_t)RI32(S, 760 + 4 * i);
        sum_b += (int64_t)RI32(S, 160 + 4 * i);
        sum_q = ADD(sum_q, RI32(S, 92 + 4 * i));
    }
    avg_c = (int32_t)(sum_c / (int64_t)n);
    avg_q = DIV(sum_q, n);
    d4 = (double)cur / (double)avg_c;
    d2 = (double)RI32(S, 76) / (double)avg_q;
    t = T10_TRUNC_D((double)cur * d4);
    d20 = (double)t / (double)avg_c * d2 * c;
    avg_b = (int32_t)(sum_b / (int64_t)n);
    return T10_TRUNC_D(d20 * (double)DIV(MUL(gop, avg_b), fps));
}

/* RC_H264_VBR_updateGopInfo (0xa0c58) */
static void rct10_vbr_update_gop(uint8_t *P, uint8_t *S)
{
    int32_t qp, budget, v, gop;
    uint64_t acc;

    rct10_frame_type(P, S);
    if (RI32(S, 0) == 0)
        return;
    acc = (uint64_t)rct10_s64(S, 752) + (uint64_t)(int64_t)RI32(S, 724);
    RU32(S, 752) = (uint32_t)acc;
    RU32(S, 756) = (uint32_t)(acc >> 32);
    rct10_account(P, S);
    rct10_vbr_realloc(P, S);
    qp = RI32(S, 40);
    if (RI32(S, 36) == 1) {
        RI32(S, 56) = SUB(qp, RI32(P, 28));
        RI32(S, 52) = ADD(qp, RI32(P, 28));
        RI32(S, 64) = qp;
        RI32(S, 152) = RI32(S, 120);
        *(double *)(S + 728) = rct10_cplx(P, S);
    } else if (RI32(S, 36) == 3) {
        RI32(S, 68) = qp;
        RI32(S, 84) = ADD(RI32(S, 84), qp);
        *(double *)(S + 736) = rct10_cplx(P, S);
    }
    rct10_scene_judge(P, S);
    if (RI32(P, 40) != 1)
        return;
    if (!(RI32(S, 24) < 2)) {
        float avg_p = (float)RI32(S, 84) / (float)(RI32(S, 24) - 1);
        float avg = (float)ADD(RI32(S, 84), RI32(S, 64)) / (float)RI32(S, 24);

        RI32(S, 76) = T10_TRUNC_D((double)avg_p + 0.5);
        RI32(S, 72) = T10_TRUNC_D((double)avg + 0.5);
    }
    gop = RI32(P, 12);
    if (RI32(S, 24) < gop) {
        budget = RI32(S, 124);
    } else {
        RI32(S, 4) = ADD(RI32(S, 4), 1);
        budget = rct10_vbr_alloc(P, S);
        rct10_update_max_qp(P, S, RI32(S, 144));
        gop = RI32(P, 12);
    }
    v = (RI32(S, 144) < budget) ? RI32(S, 144) : budget;
    if (v < RI32(S, 136))
        v = RI32(S, 136);
    if (RI32(P, 36) == 0) {
        RI32(S, 188) = v;
        RI32(S, 180) = 0;
    } else {
        RI32(S, 188) = ADD(RI32(S, 188), v);
    }
    RI32(S, 124) = v;
    RI32(S, 24) = 0;
    RI32(S, 128) = 0;
    RI32(S, 84) = 0;
    RI32(S, 796) = 0;
    RI32(S, 756) = 0;
    RI32(S, 752) = 0;
    RI32(S, 116) = DIV(v, gop);
}

/* ---------------------------------------------------------------- SMART */

/* RC_H264_SMART_init (0xa0ff8) */
static void rct10_smart_init(uint8_t *P, uint8_t *S)
{
    int32_t gop = RI32(P, 12), fps = RI32(P, 8);
    int32_t lo, floor_;

    RI32(S, 0) = 0;
    RI32(S, 4) = 0;
    RI32(S, 180) = 0;
    RI32(S, 24) = 0;
    RF32(S, 712) = 1.0f;
    RF32(S, 448) = 1.0f;
    RI32(S, 128) = 0;
    RI32(S, 84) = 0;
    RI32(S, 788) = 0;
    RI32(S, 780) = 0;
    RI32(S, 88) = 0;
    RI32(S, 8) = 0;
    RI32(S, 12) = 0;
    RI32(S, 144) = DIV(MUL(gop, RI32(P, 64)), fps);
    lo = T10_TRUNC_D((double)RI32(S, 144) * rct10_qual[8 + (RU32(P, 68) & 7)]);
    RI32(S, 136) = lo;
    floor_ = DIV(MUL(gop, RI32(P, 60)), fps);
    RI32(S, 140) = DIV(MUL(gop, RI32(P, 56)), fps);
    RI32(S, 124) = RI32(S, 140);
    RI32(S, 188) = RI32(S, 140);
    if (lo < floor_)
        RI32(S, 136) = floor_;
}

/* RC_H264_SMART_reAllocGopBs (0xa10d0): budget switch by motion class. */
static void rct10_smart_realloc(uint8_t *P, uint8_t *S)
{
    int32_t n = RI32(S, 24), cur = RI32(S, 124), gop, budget, per, t0, t1;
    int32_t used, left_n;

    if (n < 3 || RI32(S, 144) < cur)
        return;
    if (cur == RI32(S, 140) && RI32(S, 780) == 2) {
        RI32(S, 124) = RI32(S, 144);
    } else if (cur != RI32(S, 136)) {
        return;
    } else if (RI32(S, 780) == 2) {
        RI32(S, 124) = RI32(S, 144);
    } else if (RI32(S, 780) == 1) {
        RI32(S, 124) = RI32(S, 140);
    } else {
        return;
    }
    gop = RI32(P, 12);
    if (!(n < gop))
        return;
    budget = RI32(S, 124);
    used = RI32(S, 128);
    per = DIV(budget, gop);
    t0 = MUL(n, per);
    left_n = SUB(gop, n);
    if (t0 < used)
        t1 = SUB(budget, used);
    else
        t1 = MUL(left_n, per);
    RI32(S, 188) = t1;
    RI32(S, 116) = per;
    RI32(S, 180) = SUB(used, t0);
    RI32(S, 184) = SUB(used, t0);
    RI32(S, 192) = DIV(SUB(used, t0), left_n);
    if (RI32(S, 8) == 0)
        RI32(S, 132) = budget;
}

/* RC_H264_SMART_updateGopInfo (0xa11ac) */
static void rct10_smart_update_gop(uint8_t *P, uint8_t *S)
{
    int32_t bits, qp, rem, budget, gop, n;

    rct10_frame_type(P, S);
    if (RI32(S, 0) == 0)
        return;
    bits = RI32(S, 120);
    qp = RI32(S, 40);
    rct10_account(P, S);
    rem = RI32(S, 188);
    if (RI32(S, 36) == 1) {
        RI32(S, 64) = qp;
        RI32(S, 56) = SUB(qp, RI32(P, 28));
        RI32(S, 52) = ADD(qp, RI32(P, 28));
        RI32(S, 152) = bits;
        if (RI32(S, 792) == 0) {
            RI32(S, 156) = bits;
            RI32(S, 60) = qp;
            RI32(S, 112) = bits;
            RI32(S, 784) = RI32(S, 780);
            RI32(S, 132) = RI32(S, 124);
            *(double *)(S + 744) = rct10_cplx(P, S);
        } else if (RI32(S, 792) == 1 && RI32(S, 780) == 0) {
            int32_t d = SUB(RI32(S, 156), bits);

            rem = SUB(rem, d < 0 ? SUB(0, d) : d);
            RI32(S, 188) = rem;
        }
        if (rem < 0) {
            rem = ADD(bits, RI32(S, 148));
            RI32(S, 188) = rem;
        }
        RI32(S, 116) = DIV(rem, RI32(P, 12));
    } else if (RI32(S, 36) == 3) {
        RI32(S, 68) = qp;
        RI32(S, 84) = ADD(RI32(S, 84), qp);
        *(double *)(S + 736) = rct10_cplx(P, S);
        if (SUB(RI32(P, 12), RI32(S, 24)) < 5)
            RI32(S, 88) = ADD(RI32(S, 88), qp);
    }
    rct10_scene_judge(P, S);
    rct10_smart_realloc(P, S);
    if (RI32(P, 40) != 1)
        return;
    n = RI32(S, 24);
    RI32(S, 28) = n;
    gop = RI32(P, 12);
    if (!(n < 2)) {
        float avg_p = (float)RI32(S, 84) / (float)(n - 1);

        RI32(S, 76) = T10_TRUNC_D((double)avg_p + 0.5);
        if (!(gop < 6))
            RI32(S, 80) = T10_TRUNC_D((double)RI32(S, 88) / 5.0 + 0.5);
    }
    if (n < gop) {
        budget = RI32(S, 124);
    } else {
        int32_t cls = RI32(S, 780);

        RI32(S, 148) = SUB(RI32(S, 128), RI32(S, 152));
        RI32(S, 4) = ADD(RI32(S, 4), 1);
        if (cls == 2)
            budget = RI32(S, 144);
        else if (cls == 0)
            budget = RI32(S, 136);
        else
            budget = RI32(S, 140);
        rct10_update_max_qp(P, S, RI32(S, 144));
        gop = RI32(P, 12);
    }
    if (RI32(P, 36) != 0) {
        RI32(S, 188) = ADD(RI32(S, 188), budget);
    } else {
        RI32(S, 188) = budget;
        RI32(S, 180) = 0;
    }
    if (RI32(S, 792) == 2) {
        RI32(S, 8) = ADD(RI32(S, 8), 1);
    } else {
        int32_t k = ADD(RI32(S, 12), 1);

        RI32(S, 8) = 0;
        RI32(S, 12) = (k < 0x989681) ? k : 3;
    }
    RI32(S, 24) = 0;
    RI32(S, 128) = 0;
    RI32(S, 84) = 0;
    RI32(S, 752) = 0;
    RI32(S, 756) = 0;
    RI32(S, 796) = 0;
    RI32(S, 88) = 0;
    RI32(S, 124) = budget;
    RI32(S, 116) = DIV(budget, gop);
}

/* RC_H264_SMART_frames (0xa1570) */
static void rct10_smart_frames(uint8_t *P, uint8_t *S)
{
    int32_t type = RI32(S, 32);

    if (type == 1) {
        int32_t gop = RI32(P, 12);

        if (RI32(S, 0) == 0) {
            rct10_calc_first_iqp(P, S, RI32(P, 56));
        } else if (RI32(S, 4) > 0 &&
                   (RI32(S, 28) < gop || RI32(S, 36) == 1)) {
            RI32(S, 40) = RI32(S, 64);
        } else {
            int32_t last_i = RI32(S, 64), v = RI32(S, 76);

            if (v < last_i - 2) {
                if (!(gop < 6)) {
                    int32_t g = RI32(S, 80);

                    if (!(v < g - 1))
                        v = g - 2;
                    else if (last_i + 2 < v && v < g - 2)
                        v = g - 2;
                }
            } else if (last_i + 2 < v) {
                if (!(gop < 6)) {
                    int32_t g = RI32(S, 80) - 2;

                    if (v < g)
                        v = g;
                }
            } else if (last_i + 1 < v) {
                v = (last_i + 2 < last_i - 1) ? last_i - 2 : last_i + 2;
            } else if (v < last_i - 1) {
                v = last_i - 2;
            }
            if (!(RI32(S, 12) < 3) && RI32(S, 792) == 0 &&
                RI32(S, 784) == 0 && RI32(S, 780) == 0) {
                int32_t s4 = rct10_update_qp(1.67);
                int32_t a = rct10_update_qp_expand(RI32(S, 132), RI32(S, 156));
                int32_t b = rct10_update_qp_expand(RI32(S, 132), RI32(S, 124));

                v = ADD(s4, ADD(b, SUB(RI32(S, 60), a)));
            }
            RI32(S, 40) = v;
        }
    } else if (type == 3) {
        rct10_calc_pframe_qp(P, S);
        if (RI32(S, 24) == 1 && RI32(S, 780) == 0 && RI32(S, 792) == 0)
            RI32(S, 40) = RI32(S, 76);
    }
    rct10_qp_limit(P, S);
    RI32(S, 24) = ADD(RI32(S, 24), 1);
    RI32(S, 0) = ADD(RI32(S, 0), 1);
}

/* ------------------------------------------------------- entry points */

/* JZ_VPU_RC_VIDEO_CFG (0xa2484); P and S are cleared here. */
void RCT10_VideoCfg(uint8_t *E, uint8_t *P, uint8_t *S)
{
    uint32_t mode = RU32(E, 0);

    memset(P, 0, RCT10_P_SIZE);
    memset(S, 0, RCT10_S_SIZE);
    if (mode == 0) {
        RI32(P, 8) = RU8(E, 12);
        RI32(P, 36) = RI8(E, 60);
        RI32(P, 16) = RU8(E, 48);
        RI32(P, 20) = RU8(E, 49);
        RI32(P, 32) = RI8(E, 59);
        RI32(P, 24) = RU8(E, 57);
        RU32(P, 12) = RU32(E, 16);
        RU32(P, 56) = RU32(E, 20) << 10;
        RI32(P, 28) = RU8(E, 58);
        RI32(P, 4) = RI8(E, 56);
        RF32(P, 76) = RF32(E, 52);
        RI32(P, 72) = RU8(E, 50);
        rct10_cbr_init(P, S);
    } else if (mode == 1) {
        uint32_t fps48 = (uint32_t)RU8(E, 24) * 48u;
        uint32_t peak = RU32(E, 80);
        uint32_t maxr = RU32(E, 28);

        RI32(P, 8) = RU8(E, 24);
        RI32(P, 36) = RI8(E, 108);
        RI32(P, 16) = RU8(E, 64);
        RI32(P, 20) = RU8(E, 65);
        RI32(P, 32) = RI8(E, 66);
        RI32(P, 24) = RU8(E, 72);
        RU32(P, 12) = RU32(E, 32);
        RI32(P, 28) = RU8(E, 73);
        RI32(P, 4) = RI8(E, 77);
        RF32(P, 76) = RF32(E, 68);
        RI32(P, 72) = RU8(E, 67);
        if (peak < fps48) {
            peak = (uint32_t)T10_TRUNC_D((double)(int32_t)fps48 *
                                         ((double)maxr / 2000.0));
            RU32(E, 80) = peak;
        }
        RU32(P, 60) = peak << 10;
        RU32(P, 64) = maxr << 10;
        RU32(P, 84) = RU32(E, 112);
        RU32(P, 56) = (uint32_t)(((uint64_t)((maxr << 10) * RU8(E, 74)) *
                                  0x51eb851full) >> 37);
        RI32(P, 68) = RU8(E, 75);
        rct10_vbr_init(P, S);
    } else if (mode == 2) {
        RI32(P, 0) = RU8(E, 9);
        RI32(P, 4) = RI8(E, 10);
    } else if (mode == 3) {
        uint32_t maxr = RU32(E, 40);

        RI32(P, 8) = RU8(E, 36);
        RI32(P, 36) = RI8(E, 164);
        RI32(P, 16) = RU8(E, 120);
        RI32(P, 20) = RU8(E, 121);
        RI32(P, 32) = RI8(E, 122);
        RI32(P, 24) = RU8(E, 128);
        RU32(P, 12) = RU32(E, 44);
        RI32(P, 28) = RU8(E, 129);
        RI32(P, 4) = RI8(E, 133);
        RI32(P, 72) = RU8(E, 123);
        RF32(P, 76) = RF32(E, 124);
        RU32(P, 60) = RU32(E, 136) << 10;
        RU32(P, 64) = maxr << 10;
        RU32(P, 56) = (uint32_t)(((uint64_t)((maxr << 10) * RU8(E, 130)) *
                                  0x51eb851full) >> 37);
        RU32(P, 84) = RU32(E, 168);
        RI32(P, 68) = RU8(E, 131);
        rct10_smart_init(P, S);
    }
    RU32(P, 48) = RU32(E, 200);
    RU32(P, 52) = RU32(E, 204);
    RU32(P, 44) = (RU32(E, 200) * RU32(E, 204)) << 8;
    RU32(P, 80) = mode;
}

/* JZ_VPU_RC_FRAME_RC (0xa2930) */
void RCT10_FrameRc(uint8_t *E, uint8_t *P, uint8_t *S)
{
    uint32_t mode = RU32(E, 0);

    RI32(P, 40) = RI8(E, 212);
    RU32(S, 120) = RU32(E, 192);
    RU32(S, 724) = RU32(E, 188);
    RI32(S, 32) = RU8(E, 184);
    RI32(S, 16) = 0;
    RI32(S, 20) = 0;
    RI32(S, 792) = RI8(E, 224);
    switch (mode) {
    case 0:
        rct10_cbr_update_gop(P, S);
        rct10_update_model_coeff(P, S);
        rct10_frames(P, S);
        break;
    case 1:
        rct10_vbr_update_gop(P, S);
        rct10_update_model_coeff(P, S);
        rct10_frames(P, S);
        break;
    case 3:
        rct10_smart_update_gop(P, S);
        rct10_update_model_coeff(P, S);
        rct10_smart_frames(P, S);
        break;
    case 2:
        rct10_frame_type(P, S);
        if (RI32(S, 32) == 1)
            RI32(S, 40) = SUB(RI32(P, 0), RI32(P, 4));
        else if (RI32(S, 32) == 3)
            RI32(S, 40) = RI32(P, 0);
        RI32(S, 0) = ADD(RI32(S, 0), 1);
        break;
    default:
        break;
    }
    RU8(E, 184) = (uint8_t)RI32(S, 32);
    RU8(E, 177) = (uint8_t)RI32(S, 40);
}

/* JZ_VPU_RC_FRAME_REPEATE_JUDGE (0xa2b28): re-encode an over-size picture
 * with a higher QP.  The SMART branch reads the VBR mode word E+112 (OEM). */
int RCT10_RepeatJudge(uint8_t *E, uint8_t *P, uint8_t *S)
{
    uint32_t mode = RU32(E, 0);
    uint32_t thr_i, thr_p, times, step, mx, mn, type, bits;
    int r = 0;

    (void)P;
    if (mode == 3 && RI8(E, 224) == 1 && RU8(E, 225) != 0 &&
        RI32(S, 20) < (int32_t)RU8(E, 156)) {
        RU8(E, 226) = 1;
        RI32(S, 20) = ADD(RI32(S, 20), 1);
    } else {
        RU8(E, 226) = 0;
        RI32(S, 20) = 0;
        if (mode != 1 && mode != 3)
            goto out;
    }
    if (RU32(E, 112) != 2)
        goto out;
    if (mode == 1) {
        thr_i = RU32(E, 88);
        thr_p = RU32(E, 92);
        times = RU8(E, 100);
        step = RU8(E, 101);
        mx = RU8(E, 64);
        mn = RU8(E, 65);
    } else {
        thr_i = RU32(E, 144);
        thr_p = RU32(E, 148);
        times = RU8(E, 156);
        step = RU8(E, 157);
        mx = RU8(E, 120);
        mn = RU8(E, 121);
    }
    type = RU8(E, 184);
    bits = RU32(E, 192);
    if (!((type == 1 && thr_i < bits) || (type == 3 && thr_p < bits)) ||
        !(RI32(S, 16) < (int32_t)times)) {
        RI32(S, 16) = 0;
        goto out;
    }
    {
        uint32_t q = (step + RU8(E, 177)) & 0xff;

        if (mx < q)
            q = mx;
        RU8(E, 177) = (uint8_t)q;
        if (q < mn)
            RU8(E, 177) = (uint8_t)mn;
    }
    RI32(S, 16) = ADD(RI32(S, 16), 1);
    r = 1;
out:
    return (RU8(E, 226) == 1) ? 1 : r;
}

/* ------------------------------------------------------------ the glue */

void RCT10_DefaultParams(RcT10Params *p)
{
    memset(p, 0, sizeof(*p));
    p->method = 1;
    p->gop = 25;
    p->fps_num = 25;
    p->fps_den = 1;
    p->qp = 30;
    p->min_qp = 15;
    p->max_qp = 48;
    p->bitrate = 1000;
    p->frm_qp_step = 3;
    p->gop_qp_step = 15;
    p->max_bitrate = 1000;
    p->change_pos = 80;
    p->quality = 2;
    p->new_max_qp_trig = 3.0f;
    p->new_max_qp = 51;
    p->mb_rc2 = 1;
    p->super_i_bits = 19660800;
    p->super_p_bits = 14043429;
    p->ip_factor = 1.4f;
}

/* log2 of the I/P factor rounded (uClibc logf = (float)log(double)) */
static int8_t rct10_ip_shift(float ipf)
{
    float l = (float)log((double)ipf) / 0.6931471824645996f;

    return (int8_t)T10_TRUNC_D((double)l + 0.5);
}

/* i264e_ratecontrol_init (0x3ab38), T10 branch: the E block. */
void RCT10_SetupE(uint8_t *E, const RcT10Params *p)
{
    /* OEM: the super-frame thresholds are the parameter / 1024 compared
     * with the picture's bits (every T10 VBR picture above 13714 bits is
     * coded twice).  OpenIMP extra superfrm_bits = 1: compare the parameter
     * in bits, as the T20 controller does (docs/T20_RC.md, "T10"). */
    const int32_t sf_div = p->superfrm_bits ? 1 : 1024;
    uint8_t fps = (uint8_t)(p->fps_den ? p->fps_num / p->fps_den : 0);
    uint8_t maxq = (uint8_t)p->max_qp;
    float trig = p->new_max_qp_trig;

    memset(E, 0, RCT10_E_SIZE);
    RU8(E, 58) = 6;
    RU8(E, 50) = 6;
    RU32(E, 16) = 25;
    RU8(E, 12) = 25;
    RU8(E, 48) = 38;
    RU8(E, 176) = 1;
    RU8(E, 49) = 15;
    RU8(E, 57) = 2;
    RU8(E, 56) = 2;
    RU8(E, 59) = 2;
    RF32(E, 52) = 3.0f;
    RU8(E, 225) = (uint8_t)p->mb_rc2;
    RU32(E, 200) = (p->width + 15) >> 4;
    RU32(E, 204) = (p->height + 15) >> 4;
    switch (p->method) {
    case 0:
        RU32(E, 0) = 2;
        RU32(E, 4) = p->gop;
        RU8(E, 8) = fps;
        RU8(E, 9) = (uint8_t)p->qp;
        RI8(E, 10) = rct10_ip_shift(p->ip_factor);
        break;
    case 1:
        RU32(E, 16) = p->gop;
        RU8(E, 48) = maxq;
        RU8(E, 49) = (uint8_t)p->min_qp;
        RU8(E, 50) = (uint8_t)(p->new_max_qp - maxq);
        RF32(E, 52) = trig;
        RU8(E, 12) = fps;
        RU32(E, 20) = p->bitrate;
        RU8(E, 57) = (uint8_t)p->frm_qp_step;
        RU8(E, 58) = (uint8_t)p->gop_qp_step;
        RU8(E, 60) = (uint8_t)p->gop_relation;
        RU8(E, 59) = (uint8_t)p->i_bias;
        RI8(E, 56) = rct10_ip_shift(p->ip_factor);
        break;
    case 2:
        RU8(E, 72) = (uint8_t)p->frm_qp_step;
        RU32(E, 28) = p->max_bitrate;
        RU8(E, 64) = maxq;
        RU8(E, 65) = (uint8_t)p->min_qp;
        RU8(E, 66) = (uint8_t)p->i_bias;
        RU8(E, 67) = (uint8_t)(p->new_max_qp - maxq);
        RU8(E, 74) = (uint8_t)p->change_pos;
        RU8(E, 75) = (uint8_t)p->quality;
        RU32(E, 0) = 1;
        RU32(E, 32) = p->gop;
        RU8(E, 25) = (uint8_t)p->static_time;
        RF32(E, 68) = trig;
        RU8(E, 24) = fps;
        RU8(E, 73) = (uint8_t)p->gop_qp_step;
        RU32(E, 80) = 200;
        RU8(E, 101) = 3;
        RU8(E, 96) = (uint8_t)p->gop;
        RI32(E, 92) = p->super_p_bits / sf_div;
        RU8(E, 97) = 1;
        RU8(E, 100) = 1;
        RU8(E, 108) = (uint8_t)p->gop_relation;
        RU32(E, 112) = 2;
        RF32(E, 104) = 1.2f;
        RI8(E, 77) = rct10_ip_shift(p->ip_factor);
        RU32(E, 84) = fps ? 200u / fps : 0;
        RI32(E, 88) = p->super_i_bits / sf_div;
        break;
    case 3:
        RU8(E, 128) = (uint8_t)p->frm_qp_step;
        RU32(E, 40) = p->max_bitrate;
        RU8(E, 120) = maxq;
        RU8(E, 121) = (uint8_t)p->min_qp;
        RU8(E, 122) = (uint8_t)p->i_bias;
        RU8(E, 123) = (uint8_t)(p->new_max_qp - maxq);
        RU8(E, 130) = (uint8_t)p->change_pos;
        RU8(E, 131) = (uint8_t)p->quality;
        RU32(E, 0) = 3;
        RU32(E, 44) = p->gop;
        RU8(E, 37) = (uint8_t)p->static_time;
        RF32(E, 124) = trig;
        RU8(E, 36) = fps;
        RU8(E, 129) = (uint8_t)p->gop_qp_step;
        RU32(E, 136) = 200;
        RU8(E, 152) = (uint8_t)p->gop;
        RI32(E, 148) = p->super_p_bits / sf_div;
        RU8(E, 153) = 1;
        RU8(E, 156) = 1;
        RU8(E, 157) = 3;
        RU8(E, 164) = (uint8_t)p->gop_relation;
        RU32(E, 168) = 2;
        RF32(E, 160) = 1.2f;
        RU32(E, 140) = fps ? 200u / fps : 0;
        RI32(E, 144) = p->super_i_bits / sf_div;
        RI8(E, 133) = rct10_ip_shift(p->ip_factor);
        break;
    default:
        break;
    }
}

int RCT10_Init(RcT10 *rc, const RcT10Params *params)
{
    if (params->method > 3 || params->fps_den == 0 ||
        params->fps_num / params->fps_den == 0 ||
        (params->fps_num / params->fps_den) > 255 || params->gop < 3)
        return -1;
    memset(rc, 0, sizeof(*rc));
    rc->params = *params;
    RCT10_SetupE(rc->e, params);
    RCT10_VideoCfg(rc->e, rc->p, rc->s);
    return 0;
}

/* i264e_ratecontrol_start (0x3caa8), T10 path. */
void RCT10_Start(RcT10 *rc, int idr, RcT10Picture *pic)
{
    uint8_t *E = rc->e;

    pic->idr = idr ? 1 : 0;
    if (rc->params.method == 0) {
        if (rc->frames == 0 || idr) {
            float l = (float)log((double)rc->params.ip_factor) /
                      0.6931471824645996f;
            int32_t q = T10_TRUNC_D((double)(int32_t)rc->params.qp -
                                    (double)l * 6.0 + 0.5);

            pic->qp = (uint8_t)(q < 0 ? 0 : (q > 51 ? 51 : q));
        } else {
            pic->qp = (uint8_t)rc->params.qp;
        }
        rc->frames++;
        return;
    }
    RU8(E, 212) = (uint8_t)pic->idr;
    if (!idr) {
        RU32(E, 188) = rc->last.cmpx;
        RU32(E, 192) = rc->last.bits;
    }
    RCT10_FrameRc(E, rc->p, rc->s);
    RU8(E, 224) = 0;
    pic->qp = RU8(E, 177);
    rc->frames++;
}

/* i264e_ratecontrol_end + i264e_ratecontrol_is_reenc (0x3d1c0, 0x3d300) */
int RCT10_End(RcT10 *rc, const RcT10Stats *stats, RcT10Picture *pic)
{
    rc->last = *stats;
    if (rc->params.method < 1 || rc->params.method > 3)
        return 0;
    RU32(rc->e, 192) = stats->bits;
    if (RCT10_RepeatJudge(rc->e, rc->p, rc->s) <= 0)
        return 0;
    pic->qp = RU8(rc->e, 177);
    return 1;
}
