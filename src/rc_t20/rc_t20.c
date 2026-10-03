/*
 * Picture rate control of the Ingenic T20 H.264 encoder, reimplemented from
 * the OEM T20 libimp 3.12.0 (JZ_VPU_RC_VIDEO_CFG_T20, JZ_VPU_RC_FRAME_RC_T20,
 * JZ_VPU_RC_FRAME_REPEATE_JUDGE_T20 and the static RC_H264_* helpers at
 * 0xa2cb0..0xab660).  See rc_t20.h and docs/T20_RC.md.
 *
 * The three OEM blocks keep their layout and are addressed by OEM byte
 * offset: E (the "eprc_t20" block at i264e param + 704), P (rcPara, 128
 * bytes) and S (rcSt, 872 bytes).  Comments give the OEM code address.
 */
#include "rc_t20.h"
#include "rc_t20_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- tables */

static const float rct20_qp2qstep[6] = {          /* QP2QSTEP 0xd1974 */
    0.625f, 0.6875f, 0.8125f, 0.875f, 1.0f, 1.125f,
};

/* qualLvlBrThr 0xec1d0 and SmartQualLvl 0xec190 (identical values) */
static const double rct20_qual_lvl[8] = {
    0.8, 0.7, 0.6, 0.5, 0.4, 0.3, 0.2, 0.1,
};

/* VBR long-term QP classes by quality level (0xd151c, 7 x 6) */
static const int32_t rct20_vbr_qp_class[7][6] = {
    { 20, 23, 26, 29, 32, 35 }, { 22, 25, 28, 31, 34, 37 },
    { 24, 27, 30, 33, 36, 39 }, { 26, 29, 31, 34, 37, 40 },
    { 28, 31, 34, 37, 40, 43 }, { 30, 33, 36, 39, 42, 45 },
    { 32, 35, 38, 41, 44, 47 },
};

static const int32_t rct20_vbr_pct[6] = { -25, -18, -10, 0, 10, 18 };

/* ----------------------------------------------------------- QP helpers */

/* RC_H264_updateQp.isra.0 (0xa4ec4): first QP whose step exceeds x,
 * minus 3; 48 when none of 0..50 does. */
int32_t RCT20_UpdateQp(double x)
{
    int q;

    for (q = 0; q < 51; q++) {
        float step;

        if (q == 0) {
            step = (float)0.625;
        } else {
            double d = rct20_qp2qstep[q % 6];
            int k;

            for (k = q / 6; k > 0; k--)
                d = d + d;
            step = (float)d;
        }
        if (x < (double)step)
            return q - 3;
    }
    return 48;
}

/* RC_H264_CheckScene (0xa3bb8): scene class S+176 from the hardware motion
 * statistics; it also overwrites the frame QP step P+20. */
void RCT20_CheckScene(uint8_t *P, uint8_t *S)
{
    uint32_t lvl = RU32(S, 176);
    uint32_t moving = RU32(S, 188);
    uint32_t big;
    int32_t ratio;

    RU32(S, 184) = lvl;
    if (moving == 0) {
        switch (lvl) {
        case 1:
        case 2:
            RI32(P, 20) = 5;
            break;
        case 3:
            RI32(P, 20) = 2;
            break;
        case 4:
            RI32(P, 20) = 3;
            break;
        default:
            RI32(P, 20) = 1;
            break;
        }
        return;
    }
    big = (uint32_t)rct20_mul(RI32(P, 44), RI32(P, 48)) * 5u / 6u;
    ratio = rct20_div(RI32(S, 192), (int32_t)moving);
    if (big < moving) {
        if (!(RU32(S, 196) < (uint32_t)ratio)) {
            if (lvl < 2) {
                RU32(S, 176) = 0;
                RI32(P, 20) = 1;
            } else {
                RU32(S, 176) = 1;
                RI32(P, 20) = 5;
            }
            return;
        }
    }
    if (RU32(S, 200) < (uint32_t)ratio) {
        RU32(S, 176) = 4;
        RI32(P, 20) = 3;
    } else if (lvl < 2) {
        RU32(S, 176) = 2;
        RI32(P, 20) = 5;
    } else {
        RU32(S, 176) = 3;
        RI32(P, 20) = 2;
    }
}

/* RC_H264_QpLimit (0xa3cc4) */
void RCT20_QpLimit(uint8_t *P, uint8_t *S)
{
    int32_t type = RI32(S, 24);
    int32_t qp = RI32(S, 32);
    int32_t mx;

    if (type == 1) {
        mx = (RI32(S, 0) != 0 && RI32(S, 80) != 0) ? RI32(S, 76) : RI32(P, 28);
    } else if (type == 3) {
        if (RI32(S, 64) < qp)
            qp = RI32(S, 64);
        if (qp < RI32(S, 60))
            qp = RI32(S, 60);
        if (RI32(S, 72) < qp)
            qp = RI32(S, 72);
        if (qp < RI32(S, 68))
            qp = RI32(S, 68);
        mx = (RI32(S, 4) != 0 && RI32(S, 80) != 0) ? RI32(S, 76) : RI32(P, 28);
    } else {
        return;
    }
    if (mx < qp)
        qp = mx;
    if (qp < RI32(P, 32))
        qp = RI32(P, 32);
    RI32(S, 32) = qp;
}

/* RC_H264_UpdateMaxQp (0xa4fc8): a raised QP cap S+76 (NewMaxQp) while the
 * bit rate is above P+72, for at most P+68 pictures. */
void RCT20_UpdateMaxQp(uint8_t *P, uint8_t *S, int32_t bitrate)
{
    double rate = (double)RI32(S, 132) * (double)RI32(P, 0);
    int32_t qp, n;

    rate = rate / (double)RI32(S, 12);
    qp = rct20_add(RCT20_UpdateQp(rate / (double)bitrate), RI32(S, 48));
    n = RI32(S, 84);
    if (!((double)RI32(P, 72) < rate && RI32(P, 28) < qp)) {
        if (n <= 0 || !(n < RI32(P, 68))) {
            RI32(S, 80) = 0;
            RI32(S, 84) = 0;
            return;
        }
    }
    RI32(S, 80) = 1;
    RI32(S, 76) = qp;
    if (RI32(P, 60) < qp) {
        RI32(S, 76) = RI32(P, 60);
        qp = RI32(P, 60);
    }
    if (qp < RI32(P, 32))
        RI32(S, 76) = RI32(P, 32);
    RI32(S, 84) = rct20_add(n, 1);
}

/* RC_H264_calcOtherIQp.isra.1 (0xa62b0): I QP near the last P QP. */
int32_t RCT20_CalcOtherIQp(uint8_t *S)
{
    int32_t p = RI32(S, 36), i = RI32(S, 48);
    int32_t a = RI32(S, 56), b = RI32(S, 44);
    int32_t r;

    if (i < p - 2)
        r = (i < a - 1) ? i : a - 2;
    else if (p + 2 < i)
        r = (i < a - 2) ? a - 2 : i;
    else if (p + 1 < i)
        r = p + 2;
    else
        r = (i < p - 1) ? p - 2 : i;
    if (!(r < b - 1))
        r = b - 2;
    return r;
}

/* RC_H264_calcIFrameQp (0xa639c) */
void RCT20_CalcIFrameQp(uint8_t *P, uint8_t *S)
{
    int32_t qp;

    if (RI32(S, 20) < RI32(P, 4) && RI32(S, 28) == 1)
        qp = RI32(S, 36);
    else
        qp = RCT20_CalcOtherIQp(S);
    RI32(S, 168) = RI32(P, 8);
    RI32(S, 32) = rct20_add(qp, RI32(P, 8));
}

/* RC_H264_calcFirstIQp (0xa543c): first I picture from bits per picture
 * and picture size. */
void RCT20_CalcFirstIQp(uint8_t *P, uint8_t *S, int32_t bits)
{
    int32_t rate = rct20_mul(bits, RI32(P, 0)) / 25600;
    int32_t pix = rct20_mul(RI32(P, 44) << 4, RI32(P, 48) << 4);
    int32_t qp, bias;

    if (pix < 921601)
        qp = -RCT20_UpdateQp((double)(921600.0f / (float)pix));
    else
        qp = RCT20_UpdateQp((double)((float)pix / 921600.0f));
    if (rate < 1025)
        qp += RCT20_UpdateQp((double)(float)(1024.0 / (double)rate));
    else
        qp -= RCT20_UpdateQp((double)((float)rate * 0.0009765625f));
    qp += 37;
    bias = RI32(P, 8);
    if (bias == 0) {
        uint32_t scene = RU32(S, 176);

        if (scene < 2)
            bias = -1;
        else if (scene == 4)
            bias = 2;
    }
    RI32(S, 168) = bias;
    RI32(S, 32) = qp + bias;
}

/* RC_H264_calcPFrameQp (0xa4120): picture budget, then the quadratic
 * R-Q model (JM style: X1, X2 at S+740/744, MAD model C1/C2 at S+476/480). */
void RCT20_CalcPFrameQp(uint8_t *P, uint8_t *S)
{
    int32_t n = RI32(S, 12);
    int32_t gop = RI32(P, 4);
    int32_t base, cur, used, tgt;
    int lo;
    double w;
    float mad, f27, x1, x2, ftgt, f6, qs, q;

    if (n == 1 && RI32(S, 768) == 0) {                  /* 0xa43d4 */
        RI32(S, 100) = RI32(P, 12);
        RI32(S, 32) = rct20_add(RI32(S, 36), RI32(P, 12));
        return;
    }
    lo = RI32(S, 176) < 2;
    w = lo ? 0.5 : 0.25;
    base = RI32(S, 208);
    if (n == 2) {                                       /* 0xa4404 */
        cur = base;
        RI32(S, 220) = rct20_div(base, gop - 2);
    } else if (n == 1 && RI32(S, 768) == 1) {           /* 0xa4434 */
        cur = base;
        RI32(S, 220) = rct20_div(base, gop - 1);
    } else {
        cur = RI32(S, 212);
    }
    used = rct20_sub(cur, RI32(S, 220));
    RI32(S, 212) = used;
    tgt = RI32(S, 128);
    if (RI32(S, 216) >= 0) {
        int32_t per = rct20_div(RI32(S, 216), rct20_sub(gop, n));
        int32_t dev = rct20_trunc_d(w * (double)rct20_sub(used, base));
        double half = (double)rct20_add(tgt, dev) * 0.5;
        int32_t est = rct20_trunc_d((double)per * 0.5 + half);

        if (!(est < rct20_div(rct20_mul(tgt, 3), 5))) {   /* 0xa4358 */
            int32_t last = RI32(S, 124);

            if (!(tgt < last)) {
                tgt = est;
            } else if ((double)tgt * 1.5 < (double)est) {
            } else if (rct20_mul(tgt, 2) < last) {
            } else {
                int32_t floor_ = rct20_div(rct20_mul(last, 2), 3);

                if (tgt < floor_)
                    tgt = floor_;
                else
                    tgt = (last < est) ? last : est;
            }
            goto model;
        }
    }
    if (lo)                                             /* 0xa445c */
        tgt = rct20_div(rct20_mul(tgt, 3), 5);
model:                                                  /* 0xa4238 */
    mad = (float)RI32(S, 172);
    f27 = mad * RF32(S, 476) + RF32(S, 480);
    x2 = RF32(S, 744);
    x1 = RF32(S, 740);
    ftgt = (float)tgt;
    f6 = f27 * x1;
    if ((double)x2 <= 1e-6) {
        qs = f6 / ftgt;
    } else {
        float f23 = f27 * f6;
        float f1 = f27 * (x2 * 4.0f) * ftgt;
        float disc = x1 * f23 + f1;

        if (disc < 0.0f) {
            qs = f6 / ftgt;
        } else {
            double root = sqrt((double)disc);
            float lin = f27 * x1;

            if (root - (double)lin <= 1e-6) {
                qs = lin / ftgt;
            } else {
                float num = f27 * (x2 + x2);

                qs = (float)((double)num / (root - (double)(f27 * x1)));
            }
        }
    }
    q = (float)(log((double)qs / 0.85) * 6.0 / 0.6931471805599453);
    RI32(S, 32) = (q < 0.0f) ? 12 : rct20_trunc_f(q + 12.0f);
}

/* RC_H264_CBR_frames / RC_H264_VBR_frames (0xa6474, 0xa6538, identical) */
void RCT20_Frames(uint8_t *P, uint8_t *S)
{
    if (RI32(S, 24) == 1) {
        if (RI32(S, 4) == 0)
            RCT20_CalcFirstIQp(P, S, RI32(P, 108));
        else
            RCT20_CalcIFrameQp(P, S);
    } else {
        RCT20_CalcPFrameQp(P, S);
    }
    RCT20_QpLimit(P, S);
    RI32(S, 0) = rct20_add(RI32(S, 0), 1);
    RI32(S, 12) = rct20_add(RI32(S, 12), 1);
}

/* --------------------------------------------------------- model update */

/* RDModelEstimator (0xa2cb0): JM RCModelEstimator on the window of n
 * (Qstep, bits/MAD) pairs at S+656 / S+572, rejected flags S+488. */
void RCT20_RDModel(int32_t n, uint8_t *S)
{
    int32_t real = n, i;
    int est = 0;
    double one = 0.0;
    double a00 = 0.0, a01 = 0.0, a11 = 0.0, b0 = 0.0, b1 = 0.0, mv;

    RF32(S, 744) = 0.0f;
    RF32(S, 740) = 0.0f;
    if (n <= 0)
        return;
    for (i = 0; i < n; i++)
        if (RI32(S, 488 + 4 * i))
            real--;
    for (i = 0; i < n; i++)
        if (!RI32(S, 488 + 4 * i))
            one = (double)RF32(S, 656 + 4 * i);
    for (i = 0; i < n; i++) {
        float q = RF32(S, 656 + 4 * i);

        if (RI32(S, 488 + 4 * i))
            continue;
        if ((double)q != one)
            est = 1;
        RF32(S, 740) = RF32(S, 740) + (q * RF32(S, 572 + 4 * i)) / (float)real;
    }
    if (real <= 0 || !est)
        return;
    for (i = 0; i < n; i++) {
        float q = RF32(S, 656 + 4 * i), r = RF32(S, 572 + 4 * i);

        if (RI32(S, 488 + 4 * i))
            continue;
        a00 = a00 + 1.0;
        a01 = a01 + 1.0 / (double)q;
        a11 = a11 + 1.0 / (double)(q * q);
        b0 = b0 + (double)(q * r);
        b1 = b1 + (double)r;
    }
    mv = a00 * a11 - a01 * a01;
    if (fabs(mv) > 1e-6) {
        RF32(S, 740) = (float)((a11 * b0 - a01 * b1) / mv);
        RF32(S, 744) = (float)((a00 * b1 - a01 * b0) / mv);
    } else {
        RF32(S, 740) = (float)(b0 / a00);
    }
}

/* MADModelEstimator (0xa34f0): JM MADModelEstimator on S+308 (picture MAD)
 * against S+392 (reference MAD), rejected flags S+224. */
void RCT20_MADModel(int32_t n, uint8_t *S)
{
    int32_t real = n, i;
    int est = 0;
    float one = 0.0f;
    float a00 = 0.0f, a01 = 0.0f, a11 = 0.0f, b0 = 0.0f, b1 = 0.0f, mv;

    RF32(S, 480) = 0.0f;
    RF32(S, 476) = 0.0f;
    if (n <= 0)
        return;
    for (i = 0; i < n; i++)
        if (RI32(S, 224 + 4 * i))
            real--;
    for (i = 0; i < n; i++)
        if (!RI32(S, 224 + 4 * i))
            one = RF32(S, 308 + 4 * i);
    for (i = 0; i < n; i++) {
        float pic = RF32(S, 308 + 4 * i);

        if (RI32(S, 224 + 4 * i))
            continue;
        if (pic != one)
            est = 1;
        RF32(S, 476) = RF32(S, 476) +
                       pic / ((float)real * RF32(S, 392 + 4 * i));
    }
    if (real <= 0 || !est)
        return;
    for (i = 0; i < n; i++) {
        float ref = RF32(S, 392 + 4 * i), pic = RF32(S, 308 + 4 * i);

        if (RI32(S, 224 + 4 * i))
            continue;
        a00 = a00 + 1.0f;
        a01 = a01 + ref;
        a11 = ref * ref + a11;
        b0 = b0 + pic;
        b1 = ref * pic + b1;
    }
    mv = a00 * a11 - a01 * a01;
    if (1e-6 < (double)fabsf(mv)) {
        RF32(S, 480) = (a11 * b0 - a01 * b1) / mv;
        RF32(S, 476) = (a00 * b1 - a01 * b0) / mv;
    } else {
        RF32(S, 476) = b0 / a01;
    }
}

/* Outlier rejection of updateModelCoeff: flags where |err| > threshold. */
static void rct20_reject(const float *err, int32_t n, float sum, uint8_t *S,
                         int flags)
{
    float thr = 0.0f;
    int32_t i;

    if (!(n < 3))
        thr = (float)sqrt((double)(sum / (float)(n - 2)));
    for (i = 0; i < n; i++)
        if (thr < fabsf(err[i]))
            RI32(S, flags + 4 * i) = 1;
}

/* RC_H264_updateModelCoeff (0xa65fc): JM updateRCModel after a P picture. */
void RCT20_UpdateModelCoeff(uint8_t *P, uint8_t *S)
{
    float qstep, rp, mad, x1, x2, c1, c2, sum;
    float err[4];
    int32_t n, i;
    int fill;

    if (RI32(S, 28) != 3)
        return;
    qstep = (float)(pow(2.0, ((double)(float)RI32(S, 32) - 12.0) / 6.0) * 0.85);
    RF32(S, 668) = RF32(S, 664);
    RF32(S, 664) = RF32(S, 660);
    RF32(S, 660) = RF32(S, 656);
    RF32(S, 584) = RF32(S, 580);
    RF32(S, 580) = RF32(S, 576);
    RF32(S, 576) = RF32(S, 572);
    rp = (float)RI32(S, 124) / (float)RI32(S, 172);
    RF32(S, 656) = qstep;
    RF32(S, 572) = rp;
    fill = (uint32_t)(RI32(S, 176) - 1) < 2u;
    if (fill) {                                         /* 0xa6e04 */
        RF32(S, 668) = RF32(S, 664) = RF32(S, 660) = qstep;
        RF32(S, 584) = RF32(S, 580) = RF32(S, 576) = rp;
    }
    if (!(RI32(S, 12) < (RU8(P, 56) == 2 ? 3 : 2))) {
        n = rct20_add(RI32(S, 748), 1);
        n = (n < 5) ? ((n > 0) ? n : 1) : 4;
        RI32(S, 748) = n;
        for (i = 0; i < 4; i++)
            RI32(S, 488 + 4 * i) = 0;
        RCT20_RDModel(n, S);
        x1 = RF32(S, 740);
        x2 = RF32(S, 744);
        sum = 0.0f;
        for (i = 0; i < n; i++) {
            float q = RF32(S, 656 + 4 * i);

            err[i] = (x1 / q + x2 / (q * q)) - RF32(S, 572 + 4 * i);
            sum = err[i] * err[i] + sum;
        }
        rct20_reject(err, n, sum, S, 488);
        RI32(S, 488) = 0;
        RCT20_RDModel(n, S);
        fill = (uint32_t)(RI32(S, 176) - 1) < 2u;
    }
    /* 0xa6a78: MAD window */
    mad = (float)RI32(S, 172);
    {
        float prev = RF32(S, 308);

        RF32(S, 320) = RF32(S, 316);
        RF32(S, 404) = RF32(S, 400);
        RF32(S, 316) = RF32(S, 312);
        RF32(S, 400) = RF32(S, 396);
        RF32(S, 312) = prev;
        RF32(S, 396) = RF32(S, 392);
        RF32(S, 308) = mad;
        RF32(S, 392) = prev;
        if (fill) {
            RF32(S, 320) = RF32(S, 316) = RF32(S, 312) = mad;
            RF32(S, 404) = RF32(S, 400) = RF32(S, 396) = prev;
        }
    }
    if (RI32(S, 12) < 2)
        return;
    n = rct20_add(RI32(S, 484), 1);
    n = (n < 5) ? ((n > 0) ? n : 1) : 4;
    RI32(S, 484) = n;
    for (i = 0; i < 4; i++)
        RI32(S, 224 + 4 * i) = 0;
    RCT20_MADModel(n, S);
    c1 = RF32(S, 476);
    c2 = RF32(S, 480);
    sum = 0.0f;
    for (i = 0; i < n; i++) {
        err[i] = (c1 * RF32(S, 392 + 4 * i) + c2) - RF32(S, 308 + 4 * i);
        sum = err[i] * err[i] + sum;
    }
    rct20_reject(err, n, sum, S, 224);
    RI32(S, 224) = 0;
    RCT20_MADModel(n, S);
}

/* ------------------------------------------------------------- GOP info */

/* Average of the P QP history S+104.. into S+56 (S+92 accumulates). */
static void rct20_avg_qp(uint8_t *S, int32_t n)
{
    int32_t sum, i;

    if (n <= 0) {
        sum = RI32(S, 92);
    } else {
        sum = rct20_add(RI32(S, 92), RI32(S, 104));
        for (i = 1; i < n; i++)
            sum = rct20_add(sum, RI32(S, 104 + 4 * i));
        RI32(S, 92) = sum;
    }
    RI32(S, 56) = rct20_trunc_d((double)sum / (double)n + 0.5);
}

/* Common head of the GOP bookkeeping (CBR 0xa50fc / VBR 0xa5694).
 * Returns the previous picture type, or -1 when nothing more is done. */
static int32_t rct20_gop_head(uint8_t *P, uint8_t *S)
{
    int32_t prev = RI32(S, 24);
    int32_t bits, qp;

    RI32(S, 28) = prev;
    RI32(S, 24) = (RI32(P, 36) == 1) ? 1 : 3;
    if (RI32(S, 0) == 0x7fffffff)
        RI32(S, 0) = 1;
    else if (RI32(S, 0) == 0)
        return -1;
    bits = RI32(S, 124);
    qp = RI32(S, 32);
    RI32(S, 64) = rct20_add(qp, RI32(P, 20));
    RI32(S, 60) = rct20_sub(qp, RI32(P, 20));
    RI32(S, 132) = rct20_add(RI32(S, 132), bits);
    RI32(S, 216) = rct20_sub(RI32(S, 216), bits);
    RI32(S, 208) = rct20_add(RI32(S, 208), rct20_sub(bits, RI32(S, 128)));
    return prev;
}

static void rct20_push_p_qp(uint8_t *S)
{
    int32_t qp = RI32(S, 32), old = RI32(S, 104);

    RI32(S, 44) = qp;
    RI32(S, 88) = rct20_add(RI32(S, 88), qp);
    RI32(S, 108) = old;
    RI32(S, 112) = old;
    RI32(S, 116) = old;
    RI32(S, 120) = old;
    RI32(S, 104) = qp;
}

static void rct20_gop_reset(uint8_t *S)
{
    RI32(S, 88) = 0;
    RI32(S, 132) = 0;
    RI32(S, 12) = 0;
    RI32(S, 760) = 0;
    RI32(S, 92) = 0;
    memset(S + 104, 0, 20);
}

/* RC_H264_CBR_updateGopInfo (0xa50fc) */
void RCT20_CbrUpdateGopInfo(uint8_t *P, uint8_t *S)
{
    int32_t prev = rct20_gop_head(P, S);
    int32_t n;

    if (prev < 0)
        return;
    if (prev == 1) {
        int32_t qp = RI32(S, 32);

        RI32(S, 36) = qp;
        RI32(S, 68) = rct20_sub(qp, RI32(P, 24));
        RI32(S, 72) = rct20_add(qp, RI32(P, 24));
    } else if (prev == 3) {
        RCT20_CheckScene(P, S);
        rct20_push_p_qp(S);
    }
    RCT20_UpdateMaxQp(P, S, RI32(P, 108));
    if (RI32(P, 36) != 1)
        return;
    n = RI32(S, 12);
    RI32(S, 20) = n;
    if (rct20_sub(n, RI32(S, 760)) >= 2) {
        int32_t d = rct20_sub(rct20_sub(n, 1), RI32(S, 760));

        RI32(S, 48) = rct20_trunc_d(
            (double)((float)RI32(S, 88) / (float)d) + 0.5);
    }
    if (RI32(S, 28) == 3)
        rct20_avg_qp(S, n < 6 ? n - 1 : 5);
    if (RI32(P, 16) != 0) {
        RI32(S, 216) = rct20_add(rct20_mul(RI32(S, 128), RI32(P, 4)),
                                 RI32(S, 216));
    } else {
        RI32(S, 208) = 0;
        RI32(S, 216) = rct20_mul(RI32(S, 128), RI32(P, 4));
    }
    RI32(S, 4) = rct20_add(RI32(S, 4), 1);
    rct20_gop_reset(S);
}

/* VBR long-term GOP target (0xa5b1c): GOP history of the average P QP
 * (S+788), bits per second (S+808) and MAD (S+828) over up to 5 GOPs. */
static int32_t rct20_vbr_gop_target(uint8_t *P, uint8_t *S, int32_t n)
{
    int32_t cnt, last, i, sum_a, avg_a, avg_b, avg_c, row, delta, t0, v;
    int64_t sum_b, sum_c, mad;
    double c0, fc, ratio, adj;

    cnt = rct20_add(RI32(S, 4), 1);
    RI32(S, 4) = cnt;
    if (!(cnt < 6))
        cnt = 5;
    last = cnt - 1;
    for (i = last; i > 0; i--) {
        RI32(S, 788 + 4 * i) = RI32(S, 788 + 4 * (i - 1));
        RI32(S, 808 + 4 * i) = RI32(S, 808 + 4 * (i - 1));
        RI32(S, 828 + 4 * i) = RI32(S, 828 + 4 * (i - 1));
    }
    RI32(S, 788) = RI32(S, 48);
    mad = (int64_t)((uint64_t)RU32(S, 860) << 32 | RU32(S, 856));
    {
        int64_t d = (int64_t)rct20_sub(n, 1);

        RI32(S, 828) = (int32_t)(d ? mad / d : 0);
    }
    RI32(S, 808) = rct20_div(rct20_mul(RI32(P, 0), RI32(S, 132)), n);
    sum_a = 0;
    sum_b = 0;
    sum_c = 0;
    for (i = last; i >= 0; i--) {
        sum_a = rct20_add(sum_a, RI32(S, 788 + 4 * i));
        sum_b += RI32(S, 808 + 4 * i);
        sum_c += RI32(S, 828 + 4 * i);
    }
    avg_b = (int32_t)(sum_b / cnt);
    avg_c = (int32_t)(sum_c / cnt);
    avg_a = rct20_div(sum_a, cnt);
    c0 = (double)RI32(S, 828);
    fc = (double)avg_c;
    t0 = rct20_trunc_d(c0 * (c0 / fc));
    ratio = ((double)t0 / fc) * ((double)RI32(S, 788) / (double)avg_a);
    delta = rct20_sub(RI32(S, 848),
                      RCT20_UpdateQp((double)RI32(P, 116) /
                                     (double)RI32(S, 808)));
    row = RI32(P, 124);
    if ((uint32_t)row > 6u)
        row = 0;
    if (rct20_vbr_qp_class[row][5] < delta) {
        adj = 1.25;
    } else {
        int k;

        for (k = 0; k < 6; k++)
            if (delta < rct20_vbr_qp_class[row][k])
                break;
        adj = (k < 6) ? ((double)rct20_vbr_pct[k] + 100.0) / 100.0 : 1.0;
    }
    t0 = rct20_trunc_d(ratio * adj *
                       (double)rct20_div(rct20_mul(n, avg_b), RI32(P, 0)));
    switch (RU32(S, 176)) {
    case 0:
    case 1:
        v = (RI32(S, 160) < t0) ? RI32(S, 160) : t0;
        if (v < RI32(S, 156))
            v = RI32(S, 156);
        break;
    case 4:
        v = (RI32(S, 152) < t0) ? RI32(S, 152) : t0;
        if (v < RI32(S, 160))
            v = RI32(S, 160);
        break;
    default:
        v = RI32(S, 160);
        break;
    }
    if (RI32(S, 152) < v)
        v = RI32(S, 152);
    if (v < RI32(S, 156))
        v = RI32(S, 156);
    RI32(S, 780) = v;
    return v;
}

/* RC_H264_VBR_updateGopInfo (0xa5694) */
void RCT20_VbrUpdateGopInfo(uint8_t *P, uint8_t *S)
{
    int32_t prev = rct20_gop_head(P, S);
    int32_t gop, fps, n, target, per;

    if (prev < 0)
        return;
    if (prev == 1) {
        int32_t qp = RI32(S, 32);

        RI32(S, 36) = qp;
        RI32(S, 140) = RI32(S, 124);
        RI32(S, 68) = rct20_sub(qp, RI32(P, 24));
        RI32(S, 72) = rct20_add(qp, RI32(P, 24));
    } else if (prev == 3) {
        uint64_t mad;

        RCT20_CheckScene(P, S);
        mad = (uint64_t)RU32(S, 860) << 32 | RU32(S, 856);
        mad += (uint64_t)(int64_t)RI32(S, 172);
        RU32(S, 856) = (uint32_t)mad;
        RU32(S, 860) = (uint32_t)(mad >> 32);
        rct20_push_p_qp(S);
    }
    RCT20_UpdateMaxQp(P, S, RI32(P, 116));
    gop = RI32(P, 4);
    if (RI32(P, 36) == 1) {                             /* 0xa58bc */
        n = RI32(S, 12);
        RI32(S, 20) = n;
        if (rct20_sub(n, RI32(S, 760)) >= 2) {
            int32_t s6 = rct20_sub(n, RI32(S, 760));
            int32_t d = rct20_sub(rct20_sub(n, 1), RI32(S, 760));
            int32_t sum = rct20_add(RI32(S, 88), RI32(S, 36));
            float avg = (float)RI32(S, 88) / (float)d;
            float avg2 = (float)sum / (float)s6;

            RI32(S, 96) = sum;
            RI32(S, 48) = rct20_trunc_d((double)avg + 0.5);
            RI32(S, 848) = rct20_trunc_d((double)avg2 + 0.5);
            if (RI32(S, 28) == 3)
                rct20_avg_qp(S, n < 6 ? n - 1 : 5);
        }
        if (n == gop)
            target = rct20_vbr_gop_target(P, S, n);
        else
            target = RI32(S, 780);
        RI32(S, 128) = rct20_div(target, gop);          /* 0xa5958 */
        if (RI32(P, 16) == 0) {
            RI32(S, 216) = target;
            RI32(S, 208) = 0;
        } else {
            RI32(S, 216) = rct20_add(RI32(S, 216), target);
        }
        RU32(S, 856) = 0;
        RU32(S, 860) = 0;
        rct20_gop_reset(S);
        return;
    }
    /* 0xa57d4 */
    if (RI32(S, 28) != 3 || RI32(S, 12) < 3)
        return;
    fps = RI32(P, 0);
    {
        int32_t cap = rct20_div(rct20_mul(RI32(P, 116), gop), fps);

        if (!(RI32(S, 780) < cap))
            return;
        target = RI32(S, 780);
        if (!(RI32(S, 44) < 39)) {
            RI32(S, 780) = cap;
            target = cap;
        } else if (RI32(S, 780) <
                       rct20_div(rct20_mul(gop, RI32(P, 108)), fps) &&
                   RI32(S, 176) == 2) {                 /* 0xa61a4 */
            target = rct20_trunc_d((double)RI32(P, 116) * rct20_qual_lvl[1] *
                                   (double)gop / (double)fps);
            RI32(S, 780) = target;
        }
    }
    per = rct20_div(target, gop);                       /* 0xa5850 */
    RI32(S, 216) = rct20_sub(target, RI32(S, 132));
    RI32(S, 128) = per;
    {
        int32_t t4 = rct20_add(rct20_mul(per, rct20_sub(RI32(S, 760),
                                                        RI32(S, 12))),
                               RI32(S, 132));

        RI32(S, 208) = t4;
        RI32(S, 212) = t4;
        RI32(S, 220) = rct20_div(t4, rct20_sub(gop, RI32(S, 12)));
    }
}

/* ------------------------------------------------------- configuration */

/* NewMaxQp trigger bit rate P+72 (bits/s): from the picture size and the
 * fNewMaxQpTrigLvl factor, at least twice the bit rate. */
static int32_t rct20_new_max_qp_rate(uint8_t *E, float trig, int32_t kbps)
{
    int32_t mbs = rct20_mul(RI32(E, 228), RI32(E, 232));
    /* uClibc logf: (float)log((double)x) */
    float l = (float)log((double)((float)mbs / 3600.0f));
    int32_t lvl = rct20_mul(rct20_trunc_f(trig), 2000);
    int32_t r = rct20_trunc_f((float)lvl * (l + 1.0f));
    double d;

    if (r < kbps)
        r = rct20_mul(kbps, 2);
    if (!(r < lvl))
        lvl = r;
    d = (double)lvl * 0.8;
    d = (double)kbps * 0.2 + d;
    return rct20_trunc_d(d * 1024.0);
}

/* The VBR/SMART target bit rate P+108: maxBitRate x changePos %. */
static int32_t rct20_change_pos_rate(uint32_t kbps, uint8_t change_pos)
{
    float f = (float)(double)kbps * (float)change_pos;

    return rct20_trunc_f(f / 100.0f * 1024.0f);
}

/* The state common to CBR, VBR and SMART (0xa94a8, 0xa9ea0, 0xa9920). */
static void rct20_state_init(uint8_t *P, uint8_t *S)
{
    RF32(S, 740) = 1.0f;
    RF32(S, 476) = 1.0f;
    RI32(S, 44) = 51;
    RI32(S, 76) = RI32(P, 60);
    RI32(S, 24) = 1;
    RI32(S, 132) = 0;
    RI32(S, 88) = 0;
    RI32(S, 0) = 0;
    RI32(S, 12) = 0;
    RI32(S, 4) = 0;
    RI32(S, 80) = 0;
    RI32(S, 760) = 0;
    RI32(S, 752) = 0;
    RI32(S, 176) = 0;
    RI32(S, 484) = 0;
    RI32(S, 748) = 0;
    RI32(S, 92) = 0;
}

/* JZ_VPU_RC_VIDEO_CFG_T20 (0xa8fb4).  P and S are the OEM rcPara (128 bytes)
 * and rcSt (872 bytes); the OEM allocates them once and clears them on each
 * call. */
void RCT20_VideoCfg(uint8_t *E, uint8_t *P, uint8_t *S)
{
    int32_t mode = RI32(E, 0);

    memset(P, 0, RCT20_P_SIZE);
    RI32(E, RX(348)) = 0;
    memset(S, 0, RCT20_S_SIZE);
    RI32(P, 100) = mode;
    RU8(P, 56) = RU8(E, 212);
    RI32(P, 36) = RI8(E, 213);
    if (mode == 0) {                                    /* CBR 0xa915c */
        RI32(P, 0) = RU8(E, 12);
        RI32(P, 4) = RI32(E, 16);
        RI32(P, 28) = RU8(E, 56);
        RI32(P, 32) = RU8(E, 57);
        RI32(P, 60) = RU8(E, 62);
        RF32(P, 64) = RF32(E, 64);
        RI32(P, 68) = RI32(E, 68);
        RI32(P, 72) = rct20_new_max_qp_rate(E, RF32(E, 64), RI32(E, 20));
        RI32(P, 20) = RU8(E, 59);
        RI32(P, 24) = RU8(E, 58);
        RI32(P, 16) = RI8(E, 61);
        RI32(P, 92) = RI32(E, 96);
        RI32(P, 76) = RI32(E, 92);
        RI32(P, 8) = RI8(E, 60);
        RI32(P, 12) = RI8(E, 82);
        RU8(P, 80) = RU8(E, 84);
        RU8(P, 96) = RU8(E, 85);
        RI32(P, 108) = (int32_t)(RU32(E, 20) << 10);
        RI32(P, 84) = (int32_t)(RU32(E, 72) << 10);
        RI32(P, 88) = (int32_t)(RU32(E, 76) << 10);
        RI32(P, 120) = RU8(E, 83);
        rct20_state_init(P, S);
        RI32(S, 128) = rct20_div(RI32(P, 108), RI32(P, 0));
        RI32(S, 216) = rct20_mul(RI32(S, 128), RI32(P, 4));
        memset(S + 104, 0, 20);
    } else if (mode == 1 || mode == 3) {                /* VBR 0xa9b00, SMART 0xa9564 */
        int vbr = mode == 1;
        int o = vbr ? 0 : 16;                           /* attribute offset */
        int q = vbr ? 0 : 56;                           /* parameter offset */
        int32_t gop, fps, kbps, a, half, mn;
        uint32_t qual;

        if (!vbr)
            RU8(E, RX(385)) = 0;
        RI32(P, 0) = RU8(E, 24 + o);
        RI32(P, 4) = RI32(E, 28 + o);
        RI32(P, 8) = RI8(E, 120 + q);
        RI32(P, 60) = RU8(E, 110 + q);
        RI32(P, 68) = RI32(E, 116 + q);
        RI32(P, 20) = RU8(E, 121 + q);
        RI32(P, 24) = RU8(E, 122 + q);
        RI32(P, 28) = RU8(E, 108 + q);
        RI32(P, 32) = RU8(E, 109 + q);
        RF32(P, 64) = RF32(E, 112 + q);
        RI32(P, 16) = RI8(E, 124 + q);
        kbps = RI32(E, 36 + o);
        RI32(P, 72) = rct20_new_max_qp_rate(E, RF32(E, 112 + q), kbps);
        if (vbr)
            RI32(P, 92) = RI32(E, 152);
        RI32(P, 76) = RI32(E, 148 + q);
        RU8(P, 80) = RU8(E, 125 + q);
        RU8(P, 96) = RU8(E, 126 + q);
        RI32(P, 84) = (int32_t)(RU32(E, 136 + q) << 10);
        RI32(P, 88) = (int32_t)(RU32(E, 140 + q) << 10);
        RI32(P, 12) = RI8(E, 146 + q);
        RI32(P, 112) = (int32_t)(RU32(E, 104 + q) << 10);
        RI32(P, 116) = (int32_t)((uint32_t)kbps << 10);
        RI32(P, 108) = rct20_change_pos_rate((uint32_t)kbps, RU8(E, 123 + q));
        qual = RU8(E, 147 + q);
        RI32(P, 124) = qual < 8 ? (int32_t)qual : 7;
        RI32(P, 120) = RU8(E, 134 + q);

        gop = RI32(P, 4);
        fps = RI32(P, 0);
        rct20_state_init(P, S);
        if (vbr) {                                      /* 0xa9ea0 */
            RI32(S, 16) = 0;                            /* (written by SMART only) */
        }
        mn = rct20_trunc_d((double)RI32(P, 116) *
                           rct20_qual_lvl[RI32(P, 124)]);
        RI32(S, 776) = mn;
        a = rct20_div(rct20_mul(RI32(P, 116), gop), fps);
        RI32(S, 152) = a;
        if (vbr) {
            half = rct20_div(rct20_mul(gop, RI32(P, 108)), fps);
            RI32(S, 160) = half;
            RI32(S, 780) = half;
            RI32(S, 216) = half;
        } else {
            half = (int32_t)((uint32_t)a + ((uint32_t)a >> 31)) >> 1;
            RI32(S, 780) = half;
            RI32(S, 216) = half;
            RI32(S, 160) = rct20_div(rct20_mul(gop, RI32(P, 108)), fps);
            RI32(S, 16) = 0;
            RI32(S, 8) = 0;
            RI32(S, 756) = 0;
        }
        if (RI32(P, 112) != 0 && !(RI32(P, 112) < mn)) {
            mn = RI32(P, 112);
            RI32(S, 776) = mn;
        }
        if (!vbr && !(mn < 0x1e8481)) {
            mn = 2000000;
            RI32(S, 776) = mn;
        }
        RI32(S, 156) = rct20_div(rct20_mul(gop, mn), fps);
        RI32(S, 128) = rct20_div(RI32(S, 780), gop);
        if (vbr) {
            RU32(S, 856) = 0;
            RU32(S, 860) = 0;
            memset(S + 788, 0, 20);
            memset(S + 828, 0, 20);
            memset(S + 808, 0, 20);
        }
        memset(S + 104, 0, 20);
    } else if (mode == 2) {                             /* FIXQP 0xaa0c0 */
        RI32(P, 0) = RU8(E, 8);
        RI32(P, 4) = RI32(E, 4);
        RU8(P, 104) = RU8(E, 9);
        RU8(P, 105) = RU8(E, 10);
        RI32(S, 0) = 0;
        RI32(S, 12) = 0;
        RI32(S, 4) = 0;
    }
}

/* ------------------------------------------------------- per picture */

static void rct20_publish(uint8_t *E, uint8_t *S, int set_scene)
{
    if (set_scene) {                                    /* 0xaa384 */
        RI32(E, RX(360)) = RI32(S, 176);
        RI32(E, RX(364)) = RI32(S, 184);
    }
    RU8(E, 236) = RU8(S, 24);                           /* 0xaa39c */
    RU8(E, 217) = RU8(S, 32);
    RI32(E, 248) = RI32(S, 0);
}

/* bitRateUpMode frame skipping of CBR (E+96..) and VBR (E+152..): the OEM
 * marks pictures to drop (mode 1) or to code as P skip (mode 2). */
static void rct20_skip_frames(uint8_t *E, uint8_t *P, uint8_t *S, int mode,
                              int gaps, int cnt, int flag)
{
    int32_t left = rct20_sub(RI32(P, 4), RI32(S, 12));
    int32_t n = RU8(E, gaps);

    if (mode == 1) {
        RU8(E, flag - 1) = 1;
        if (left < n)
            n = left & 0xff;
        RU8(E, cnt) = (uint8_t)n;
        RI32(S, 0) = rct20_add(RI32(S, 0), n);
        RI32(S, 12) = rct20_add(RI32(S, 12), RU8(E, cnt));
        RI32(S, 760) = rct20_add(RI32(S, 760), RU8(E, cnt));
    } else {
        RU8(E, cnt + 1) = (uint8_t)((left < n) ? left : n);
        RU8(E, flag) = 1;
    }
}

static void rct20_frame_cbr_vbr(uint8_t *E, uint8_t *P, uint8_t *S, int vbr)
{
    int up_mode = RI32(E, vbr ? 152 : 96);
    void (*gop_info)(uint8_t *, uint8_t *) =
        vbr ? RCT20_VbrUpdateGopInfo : RCT20_CbrUpdateGopInfo;

    if (up_mode == 0) {
        gop_info(P, S);
        RCT20_UpdateModelCoeff(P, S);
        RCT20_Frames(P, S);
        return;
    }
    {
        float budget = vbr ? (float)RI32(S, 152) : (float)RI32(P, 108);
        float thr = budget * RF32(E, vbr ? 128 : 88);

        if ((float)RI32(S, 132) < thr ||
            RI32(S, 12) == rct20_sub(RI32(P, 4), 1)) {
            gop_info(P, S);
            RCT20_UpdateModelCoeff(P, S);
            RCT20_Frames(P, S);
            if (up_mode == 1)
                RU8(E, vbr ? 156 : 102) = 0;
            else if (up_mode == 2)
                RU8(E, vbr ? 157 : 103) = 0;
            return;
        }
    }
    gop_info(P, S);
    RCT20_UpdateModelCoeff(P, S);
    RCT20_Frames(P, S);
    if (up_mode == 1)
        rct20_skip_frames(E, P, S, 1, vbr ? 134 : 83, vbr ? 132 : 100,
                          vbr ? 157 : 103);
    else if (up_mode == 2)
        rct20_skip_frames(E, P, S, 2, vbr ? 134 : 83, vbr ? 132 : 100,
                          vbr ? 157 : 157);
}

static void rct20_frame_fixqp(uint8_t *P, uint8_t *S)
{
    int32_t n;

    RI32(S, 28) = RI32(S, 24);
    RI32(S, 24) = (RI32(P, 36) == 1) ? 1 : 3;
    n = RI32(S, 12);
    if (n == RI32(P, 4)) {
        RI32(S, 12) = 0;
        n = 0;
    }
    RI32(S, 32) = RU8(P, RI32(S, 24) == 1 ? 104 : 105);
    RI32(S, 0) = rct20_add(RI32(S, 0), 1);
    RI32(S, 12) = rct20_add(n, 1);
}


/* SMART GOP budget re-targeting before a P picture (0xaa9cc..0xaaa8c). */
static void rct20_smart_retarget(uint8_t *P, uint8_t *S)
{
    int32_t n = RI32(S, 12), scene = RI32(S, 176);
    int32_t target, gop, used, per, left, rest, dev;

    if (n < 3 || scene < 0)
        return;
    if (!(RI32(S, 780) < RI32(S, 152)))
        return;
    if (RI32(S, 780) < RI32(S, 160) && RU32(S, 184) < 2u) {
        if (scene < 2)
            return;
        RI32(S, 780) = RI32(S, 160);
    } else {
        if (!(RU32(S, 184) < 4u) || scene != 4)
            return;
        RI32(S, 780) = RI32(S, 152);
    }
    target = RI32(S, 780);                              /* 0xaaa34 */
    gop = RI32(P, 4);
    used = RI32(S, 132);
    per = rct20_div(target, gop);
    dev = rct20_mul(n, per);
    left = rct20_sub(gop, n);
    rest = (dev < used) ? rct20_sub(target, used) : rct20_mul(per, left);
    RI32(S, 216) = rest;
    RI32(S, 128) = per;
    RI32(S, 208) = rct20_sub(used, dev);
    RI32(S, 212) = rct20_sub(used, dev);
    RI32(S, 220) = rct20_div(rct20_sub(used, dev), left);
    if (RI32(S, 16) == 0)
        RI32(S, 784) = target;
}

/* SMART GOP start (next picture IDR, 0xaacfc..0xaadfc). */
static void rct20_smart_gop_start(uint8_t *P, uint8_t *S)
{
    int32_t n = RI32(S, 12), gop = RI32(P, 4), target, scene;

    RI32(S, 20) = n;
    if (n >= 2) {
        RI32(S, 48) = rct20_trunc_d(
            (double)((float)RI32(S, 88) / (float)(n - 1)) + 0.5);
        if (RI32(S, 28) == 3)
            rct20_avg_qp(S, n < 6 ? n - 1 : 5);
    }
    if (n == gop) {                                     /* 0xaaf94 */
        if (RI32(S, 4) == 0)
            RI32(S, 136) = RI32(S, 132);
        RI32(S, 4) = rct20_add(RI32(S, 4), 1);
        RI32(S, 164) = rct20_sub(RI32(S, 132), RI32(S, 140));
    }
    scene = RI32(S, 176);                               /* 0xaad50 */
    if (scene != 0)
        target = scene < 4 ? RI32(S, 160) : RI32(S, 152);
    else
        target = rct20_trunc_d((double)RI32(S, 152) *
                               rct20_qual_lvl[RI32(P, 124)]);
    RI32(S, 780) = target;
    RI32(S, 128) = rct20_div(target, gop);
    if (RI32(P, 16) == 0) {
        RI32(S, 216) = target;
        RI32(S, 208) = 0;
    } else {
        RI32(S, 216) = rct20_add(RI32(S, 216), target);
    }
    if (RI32(S, 768) == 2) {                            /* 0xaada8 */
        RI32(S, 16) = rct20_add(RI32(S, 16), 1);
    } else {
        int32_t g = rct20_add(RI32(S, 8), 1);

        RI32(S, 16) = 0;
        RI32(S, 8) = (g < 10000001) ? g : 3;
    }
    RI32(S, 88) = 0;                                    /* 0xaadd8 */
    RI32(S, 132) = 0;
    RI32(S, 12) = 0;
    RI32(S, 92) = 0;
    memset(S + 104, 0, 20);
}

/* SMART (JZ_VPU_RC_FRAME_RC_T20 0xaa684..0xab0bc): a VBR variant with its
 * own GOP budget between the scene classes and the min/max GOP budgets. */
static void rct20_frame_smart(uint8_t *E, uint8_t *P, uint8_t *S)
{
    int32_t first = RI32(S, 0);

    RI32(S, 28) = RI32(S, 24);
    RI32(S, 24) = (RI32(P, 36) == 1) ? 1 : 3;
    if (first == 0x7fffffff)
        RI32(S, 0) = 1;
    if (first != 0) {
        int32_t qp, bits, rest, per;

        RCT20_CheckScene(P, S);                         /* 0xaa944 */
        qp = RI32(S, 32);
        bits = RI32(S, 124);
        rest = RI32(S, 216);
        per = RI32(S, 128);
        if (RI32(S, 28) == 1) {                         /* 0xaae04 */
            RI32(S, 36) = qp;
            RI32(S, 68) = rct20_sub(qp, RI32(P, 24));
            RI32(S, 72) = rct20_add(qp, RI32(P, 24));
            RI32(S, 140) = bits;
            if (RI32(S, 768) == 0) {
                RI32(S, 144) = bits;
                RI32(S, 40) = qp;
                RI32(S, 772) = RI32(S, 176);
                RI32(S, 784) = RI32(S, 780);
            } else if (RI32(S, 768) == 1 && RI32(S, 176) == 0) {
                int32_t d = rct20_sub(RI32(S, 144), bits);

                RI32(S, 148) = d;
                rest = rct20_sub(rest, d < 0 ? rct20_sub(0, d) : d);
                RI32(S, 216) = rest;
            }
            if (rest < 0)                               /* 0xaae4c */
                rest = rct20_add(bits, RI32(S, 164));
            per = rct20_div(rest, RI32(P, 4));
            RI32(S, 128) = per;
        } else if (RI32(S, 28) == 3) {                  /* 0xaaec8 */
            int32_t old = RI32(S, 104);

            RI32(S, 44) = qp;
            RI32(S, 88) = rct20_add(RI32(S, 88), qp);
            if (RI32(S, 16) == 0 && RI32(S, 12) == 3 &&
                RI32(S, 772) != RI32(S, 176))
                RI32(S, 772) = RI32(S, 176);
            RI32(S, 108) = old;
            RI32(S, 112) = old;
            RI32(S, 116) = old;
            RI32(S, 120) = old;
            RI32(S, 104) = qp;
        }
        /* 0xaa97c */
        RI32(S, 64) = rct20_add(qp, RI32(P, 20));
        RI32(S, 60) = rct20_sub(qp, RI32(P, 20));
        RI32(S, 132) = rct20_add(RI32(S, 132), bits);
        RI32(S, 216) = rct20_sub(rest, bits);
        RI32(S, 208) = rct20_add(RI32(S, 208), rct20_sub(bits, per));
        if (RI32(S, 0) != 0)
            RCT20_UpdateMaxQp(P, S, RI32(P, 116));
        if (RI32(P, 36) == 1)
            rct20_smart_gop_start(P, S);
        rct20_smart_retarget(P, S);
    }
    RCT20_UpdateModelCoeff(P, S);                       /* 0xaa6c0 */
    if (RI32(S, 24) == 1) {
        if (RI32(S, 4) == 0)
            RCT20_CalcFirstIQp(P, S, RI32(P, 108));
        else
            RI32(S, 32) = RCT20_CalcOtherIQp(S);
    } else {
        RCT20_CalcPFrameQp(P, S);
        if (RI32(S, 12) == 1 && RI32(S, 176) == 0 && RI32(S, 768) == 0 &&
            RI32(S, 32) < RI32(S, 56))
            RI32(S, 32) = RI32(S, 56);
    }
    RCT20_QpLimit(P, S);                                /* 0xaa704 */
    RI32(S, 0) = rct20_add(RI32(S, 0), 1);
    RI32(S, 12) = rct20_add(RI32(S, 12), 1);
    if ((double)RI32(S, 152) * rct20_qual_lvl[RI32(P, 124)] <
        (double)RI32(S, 780)) {
        memset(E + RX(386), 0, 7);
    } else {
        static const uint8_t mb_cfg[7] = { 1, 15, 15, 15, 1, 0, 0 };

        memcpy(E + RX(386), mb_cfg, 7);
    }
}

/* JZ_VPU_RC_FRAME_RC_T20 (0xaa20c): consumes the statistics of the last
 * picture (E+240..268) and decides the next one: type E+236 (1 I, 3 P),
 * QP E+217. */
void RCT20_FrameRc(uint8_t *E, uint8_t *P, uint8_t *S, const uint8_t *luma)
{
    uint32_t cmpx = RU32(E, 240);
    int32_t mode = RI32(E, 0);

    RI32(P, 52) = RI32(E, 220);
    RI32(P, 40) = RI32(E, 224);
    RI32(P, 44) = RI32(E, 228);
    RI32(P, 48) = RI32(E, 232);
    RI32(P, 36) = RI8(E, 213);
    RU32(S, 172) = cmpx < 256 ? 1u : cmpx >> 8;
    RI32(S, 124) = RI32(E, 244);
    RI32(S, 188) = RI32(E, 252);
    RI32(S, 192) = RI32(E, 256);
    RI32(S, 196) = RI32(E, 260);
    RI32(S, 200) = RI32(E, 264);
    RI32(S, 204) = RI32(E, 268);
    RI32(S, 752) = 0;
    RI32(S, 756) = 0;
    RI32(S, 32) = RU8(E, 217);
    RI32(S, 768) = RI8(E, RX(384));
    RI32(S, 864) = RU8(E, 215);
    RI32(E, 320) = 0;
    RI32(E, RX(348)) = 0;
    RI32(E, RX(344)) = 0;
    /* The OEM also enables macroblock rate control when /tmp/smac0 or
     * /tmp/smac1 exists (debug switch, not reproduced). */
    switch (mode) {
    case 0:
    case 1:
        rct20_frame_cbr_vbr(E, P, S, mode);
        rct20_publish(E, S, 1);
        break;
    case 2:
        rct20_frame_fixqp(P, S);
        rct20_publish(E, S, 0);
        break;
    case 3:
        rct20_frame_smart(E, P, S);
        RI32(E, RX(360)) = RI32(S, 176);
        RI32(E, RX(364)) = RI32(S, 184);
        rct20_publish(E, S, 0);
        break;
    default:
        rct20_publish(E, S, 0);
        break;
    }
    if (RI32(S, 864) != 0 && mode != 2) {
        RCT20_CalMBFlag(E, luma);
        RI32(E, RX(348)) = 1;
        RCT20_CalMBQP(E);
    }
}

/* JZ_VPU_RC_FRAME_REPEATE_JUDGE_T20 (0xab0c0): after coding, E+244 holds
 * the picture size in bits.  Returns 1 when the picture is to be coded
 * again with the raised QP E+217 (super frame), else whether the SMART
 * re-encode flag E+0x90000+385 is set. */
int RCT20_RepeatJudge(uint8_t *E, uint8_t *P, uint8_t *S)
{
    int32_t mode = RI32(E, 0);
    uint32_t bits, thr;
    int32_t qp, delta;

    if (mode == 3 && RI8(E, RX(384)) == 1 && RU8(E, 216) != 0 &&
        RI32(S, 756) < (int32_t)RU8(E, 182)) {
        RU8(E, RX(385)) = 1;
        RI32(S, 756) = rct20_add(RI32(S, 756), 1);
    } else {
        RU8(E, RX(385)) = 0;
        RI32(S, 756) = 0;
        if (!(mode == 3 || (uint32_t)mode < 2u))
            return RU8(E, RX(385)) == 1;
    }
    if (RI32(P, 76) != 2)                               /* superFrmMode */
        return RU8(E, RX(385)) == 1;
    thr = RU32(P, RU8(E, 236) == 1 ? 84 : 88);
    if (!(RU32(S, 752) < RU8(P, 80))) {
        RI32(S, 752) = 0;
        return RU8(E, RX(385)) == 1;
    }
    bits = RU32(E, 244);
    qp = RU8(E, 217);
    if (!(thr < bits) || !(qp < 49)) {
        if (qp < 49 || RI32(E, RX(348)) == 0) {
            RI32(S, 752) = 0;
            return RU8(E, RX(385)) == 1;
        }
        RI32(E, RX(348)) = 0;
        RI32(E, RX(344)) = 0;
    }
    delta = RCT20_UpdateQp((double)bits / (double)(int32_t)thr);
    qp = (qp + delta) & 0xff;
    if (!(qp < 52))
        qp = 51;
    if (qp < RI32(P, 32))
        qp = RI32(P, 32);
    RU8(E, 217) = (uint8_t)qp;
    RI32(S, 752) = rct20_add(RI32(S, 752), 1);
    if (RI32(S, 864) == 1 && RI32(E, RX(348)) != 0 && delta != 0)
        RCT20_MBQpReencode(E);
    return 1;
}

/* ------------------------------------------------------- i264e glue */

const uint32_t RCT20_StatRegs[RCT20_STAT_REG_COUNT] = {
    0x800e4, 0x800e8, 0x800ec,
};

/* Macroblock activity class thresholds (E+272..316): eprc_default_set_t20,
 * and i264e_ratecontrol_start sets them again before each picture unless a
 * file "smooth" in the working directory overrides them. */
static const int32_t rct20_mb_thr[12] = {
    200, 350, 500, 1500, 2500, 4500, 180, 310, 450, 1200, 2200, 4200,
};

/* i264e_param_default (0x2ff10) values of the fields the controller reads. */
void RCT20_DefaultParams(RcT20Params *p)
{
    memset(p, 0, sizeof(*p));
    p->method = 0;
    p->width = 1280;
    p->height = 720;
    p->gop = 0;
    p->fps_num = 25;
    p->fps_den = 1;
    p->qp = 30;
    p->min_qp = 0;
    p->max_qp = 51;
    p->bitrate = 2000;
    p->frm_qp_step = 3;
    p->gop_qp_step = 15;
    p->change_pos = 80;
    p->quality = 4;
    p->new_max_qp_trig = 3.0f;
    p->new_max_qp = 51;
    p->mb_rc = 1;
    p->mb_rc2 = 1;
    p->super_i_bits = 0x12c0000;
    p->super_p_bits = 0xd64925;
}

/* eprc_default_set_t20 (0x3a9e4) */
void RCT20_DefaultSet(uint8_t *E)
{
    int i;

    RU8(E, 212) = 1;
    RU8(E, 214) = 1;
    RI32(E, 260) = 6;
    RI32(E, 264) = 32;
    RI32(E, 268) = 64;
    for (i = 0; i < 12; i++)
        RI32(E, 272 + 4 * i) = rct20_mb_thr[i];
    RI32(E, 320) = 4;
    RI32(E, 16) = 50;
    RU8(E, 12) = 25;
    RU8(E, 56) = 38;
    RU8(E, 62) = 48;
    RU8(E, 57) = 15;
    RF32(E, 64) = 4.0f;
    RI32(E, 68) = 100;
    RU8(E, 59) = 3;
    RU8(E, 58) = 15;
    RI32(E, 0) = 0;
    RU8(E, 213) = 0;
    RU8(E, 215) = 0;
    RU8(E, 216) = 0;
    RI32(E, 20) = 0;
    RU8(E, 60) = 0;
    RU8(E, 84) = 3;
    RI32(E, 72) = 2000;
    RI32(E, 76) = 2000;
    RU8(E, 82) = 0;
    RU8(E, 61) = 0;
    RU8(E, 85) = 2;
    RF32(E, 88) = 1.2f;
    RU8(E, 83) = 2;
    RI32(E, 96) = 0;
    RI32(E, 92) = 2;
    RI32(E, 224) = 0;
    RI32(E, 220) = 0;
    RI32(E, 228) = 0;
    RI32(E, 232) = 0;
    RU8(E, 217) = 0;
    RI32(E, 240) = 0;
    RI32(E, 244) = 0;
}

static int32_t rct20_kbits(int32_t bits)
{
    return (bits < 0 ? bits + 1023 : bits) >> 10;
}

/* i264e_ratecontrol_init, T20 branch (0x3b334..0x3c70c), without the
 * eprc_default_set_t20 call. */
void RCT20_SetupE(uint8_t *E, const RcT20Params *p)
{
    uint8_t fps = (uint8_t)rct20_divu(p->fps_num, p->fps_den);
    int32_t accum = (int32_t)rct20_divu(p->fps_num << 1, p->fps_den);

    RU8(E, 212) = 1;
    RI32(E, 228) = (int32_t)(p->width + 15) >> 4;
    RI32(E, 232) = (int32_t)(p->height + 15) >> 4;
    RU8(E, 215) = (uint8_t)p->mb_rc;
    RU8(E, 216) = (uint8_t)p->mb_rc2;
    switch (p->method) {
    case 0:                                             /* FIXQP */
        RI32(E, 0) = RCT20_MODE_FIXQP;
        RI32(E, 4) = (int32_t)p->gop;
        RU8(E, 9) = (uint8_t)((uint8_t)p->qp - 3);
        RU8(E, 10) = (uint8_t)p->qp;
        RU8(E, 8) = fps;
        break;
    case 1:                                             /* CBR 0x3c61c */
        RI32(E, 72) = rct20_kbits(p->super_i_bits);
        RI32(E, 76) = rct20_kbits(p->super_p_bits);
        RF32(E, 64) = p->new_max_qp_trig;
        RI32(E, 0) = RCT20_MODE_CBR;
        RI32(E, 16) = (int32_t)p->gop;
        RI32(E, 20) = (int32_t)p->bitrate;
        RU8(E, 56) = (uint8_t)p->max_qp;
        RU8(E, 57) = (uint8_t)p->min_qp;
        RU8(E, 58) = (uint8_t)p->gop_qp_step;
        RU8(E, 59) = (uint8_t)p->frm_qp_step;
        RU8(E, 60) = (uint8_t)p->i_bias;
        RU8(E, 61) = (uint8_t)p->gop_relation;
        RU8(E, 82) = 0;
        RU8(E, 83) = 0;
        RU8(E, 84) = 3;
        RU8(E, 62) = (uint8_t)p->new_max_qp;
        RU8(E, 12) = fps;
        RU8(E, 85) = 2;
        RI32(E, 92) = 2;
        RI32(E, 96) = 0;
        RF32(E, 88) = 1.2f;
        RU8(E, 100) = 0;
        RU8(E, 101) = 0;
        RU8(E, 102) = 0;
        RU8(E, 103) = 0;
        RI32(E, 68) = accum;
        break;
    case 2:                                             /* VBR 0x3b8c8 */
    case 3: {                                           /* SMART 0x3b404 */
        int o = p->method == 2 ? 0 : 16;
        int q = p->method == 2 ? 0 : 56;

        RU8(E, 124 + q) = (uint8_t)p->gop_relation;
        RU8(E, 125 + q) = 3;
        RF32(E, 112 + q) = p->new_max_qp_trig;
        RU8(E, 120 + q) = (uint8_t)p->i_bias;
        RU8(E, 121 + q) = (uint8_t)p->frm_qp_step;
        RU8(E, 122 + q) = (uint8_t)p->gop_qp_step;
        RI32(E, 0) = p->method == 2 ? RCT20_MODE_VBR : RCT20_MODE_SMART;
        RI32(E, 28 + o) = (int32_t)p->gop;
        RI32(E, 32 + o) = (int32_t)p->static_time;   /* not read by the controller */
        RI32(E, 36 + o) = (int32_t)p->max_bitrate;
        RI32(E, 104 + q) = 0;
        RU8(E, 108 + q) = (uint8_t)p->max_qp;
        RU8(E, 109 + q) = (uint8_t)p->min_qp;
        RU8(E, 110 + q) = (uint8_t)p->new_max_qp;
        RU8(E, 126 + q) = 2;
        RU8(E, 123 + q) = (uint8_t)p->change_pos;
        RU8(E, 24 + o) = fps;
        RI32(E, 116 + q) = accum;
        RF32(E, 128 + q) = 1.2f;
        RU8(E, 132 + q) = 0;
        RU8(E, 133 + q) = 0;
        RU8(E, 134 + q) = 0;
        RI32(E, 136 + q) = rct20_kbits(p->super_i_bits);
        RI32(E, 140 + q) = rct20_kbits(p->super_p_bits);
        RU8(E, 146 + q) = 0;
        RU8(E, 147 + q) = (uint8_t)p->quality;
        RI32(E, 148 + q) = 2;
        if (p->method == 2)
            RI32(E, 152) = 0;
        RU8(E, 152 + q + (p->method == 2 ? 4 : 0)) = 0;
        RU8(E, 153 + q + (p->method == 2 ? 4 : 0)) = 0;
        break;
    }
    default:
        break;
    }
}

int RCT20_Init(RcT20 *rc, const RcT20Params *params)
{
    uint8_t *e = rc->e;

    if (!e) {
        e = calloc(1, RCT20_E_SIZE);
        if (!e)
            return -1;
    }
    memset(rc, 0, sizeof(*rc));
    memset(e, 0, RCT20_E_SIZE);
    rc->e = e;
    rc->params = *params;
    RCT20_DefaultSet(e);
    RCT20_SetupE(e, params);
    RCT20_VideoCfg(e, rc->p, rc->s);
    return 0;
}

void RCT20_Free(RcT20 *rc)
{
    free(rc->e);
    rc->e = NULL;
}

/* i264e_ratecontrol_start (0x3caa8), T20 */
void RCT20_Start(RcT20 *rc, int idr, const uint8_t *luma, uint32_t stride,
                 RcT20Picture *pic)
{
    uint8_t *E = rc->e;
    int i;

    if (rc->params.method == 0) {                       /* 0x3cb48 */
        int32_t qp = (int32_t)rc->params.qp;

        if (rc->frames == 0 || idr) {                   /* 0x3cf6c */
            float l = (float)log((double)1.4f) / 0.6931471824645996f;
            double d = (double)qp - (double)l * 6.0 + 0.5;

            qp = rct20_trunc_d(d);
            qp = qp < 0 ? 0 : qp > 51 ? 51 : qp;
        }
        pic->idr = idr;
        pic->qp = (uint8_t)qp;
        rc->frames++;
        return;
    }
    for (i = 0; i < 12; i++)
        RI32(E, 272 + 4 * i) = rct20_mb_thr[i];
    RU8(E, 215) = (uint8_t)rc->params.mb_rc;
    RI32(E, 220) = (int32_t)(uint32_t)(uintptr_t)luma;
    RI32(E, 224) = (int32_t)stride;
    RU8(E, 216) = (uint8_t)rc->params.mb_rc2;
    RI8(E, 213) = idr ? 1 : 0;
    if (!idr) {
        uint32_t r0 = rc->last.reg[0], r1 = rc->last.reg[1],
                 r2 = rc->last.reg[2];

        RU32(E, 240) = rc->last.cmpx;
        RU32(E, 244) = rc->last.bits;
        RU32(E, 252) = ((r0 >> 16) & 0x7fff) + (r0 & 0x7fff);
        RU32(E, 256) = (r2 & 0x7ffffff) + (r1 & 0x7ffffff);
    }
    RI8(E, RX(384)) = 0;
    RCT20_FrameRc(E, rc->p, rc->s, luma);
    pic->idr = RU8(E, 236) == 1;
    pic->qp = RU8(E, 217);
    rc->frames++;
}

/* i264e_ratecontrol_is_reenc (0x3d300) */
int RCT20_End(RcT20 *rc, const RcT20Stats *stats, RcT20Picture *pic)
{
    uint8_t *E = rc->e;

    if (rc->params.method < 1 || rc->params.method > 3)
        return 0;
    rc->last = *stats;
    RU32(E, 244) = stats->bits;
    if (RCT20_RepeatJudge(E, rc->p, rc->s) > 0) {
        pic->qp = RU8(E, 217);
        return 1;
    }
    return 0;
}
