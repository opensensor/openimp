#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int32_t c_log(int32_t arg1, const char *arg2, ...); /* forward decl */
int32_t fsort64(float *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */

static const float coscoff[64] = {
    0.35355299711227417f, 0.35355299711227417f, 0.35355299711227417f, 0.35355299711227417f,
    0.35355299711227417f, 0.35355299711227417f, 0.35355299711227417f, 0.35355299711227417f,
    0.4903930127620697f,  0.41573500633239746f, 0.2777850031852722f,  0.09754499793052673f,
    -0.09754499793052673f, -0.2777850031852722f, -0.41573500633239746f, -0.4903930127620697f,
    0.4619399905204773f,  0.19134099781513214f, -0.19134099781513214f, -0.4619399905204773f,
    -0.4619399905204773f, -0.19134199619293213f, 0.19134099781513214f, 0.4619399905204773f,
    0.41573500633239746f, -0.09754499793052673f, -0.4903930127620697f, -0.2777850031852722f,
    0.2777850031852722f,  0.4903930127620697f,  0.09754499793052673f, -0.41573500633239746f,
    0.35355401039123535f, -0.35355401039123535f, -0.35355401039123535f, 0.35355401039123535f,
    0.35355401039123535f, -0.35355401039123535f, -0.35355401039123535f, 0.35355401039123535f,
    0.2777850031852722f,  -0.4903930127620697f, 0.09754499793052673f, 0.41573500633239746f,
    -0.41573500633239746f, -0.09754499793052673f, 0.4903930127620697f, -0.2777850031852722f,
    0.19134099781513214f, -0.4619399905204773f, 0.4619399905204773f, -0.19134099781513214f,
    -0.19134199619293213f, 0.4619399905204773f, -0.4619399905204773f, 0.19134099781513214f,
    0.09754499793052673f, -0.2777850031852722f, 0.41573500633239746f, -0.4903930127620697f,
    0.4903930127620697f,  -0.41573500633239746f, 0.2777850031852722f, -0.09754499793052673f,
};

static const float watermarkn[64] = {
    126.0f, 24.0f, 24.0f, 24.0f, 24.0f, 24.0f, 24.0f, 126.0f,
    62.0f, 127.0f, 102.0f, 102.0f, 102.0f, 102.0f, 102.0f, 248.0f,
    254.0f, 230.0f, 118.0f, 62.0f, 126.0f, 67.0f, 127.0f, 60.0f,
    24.0f, 126.0f, 102.0f, 102.0f, 127.0f, 6.0f, 102.0f, 126.0f,
    62.0f, 127.0f, 102.0f, 102.0f, 102.0f, 102.0f, 102.0f, 255.0f,
    24.0f, 0.0f, 28.0f, 30.0f, 24.0f, 24.0f, 24.0f, 126.0f,
    24.0f, 126.0f, 102.0f, 103.0f, 3.0f, 71.0f, 102.0f, 126.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
};

static uint32_t float_abs_bits(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof(bits));
    bits &= 0x7fffffffU;
    return bits;
}

/* stock name: watermark_dct8.constprop.3 */
static int32_t watermark_dct8_constprop_3(float *arg1, int32_t arg2, float *arg3)
{
    const float *t2;
    const float *t3;
    float *t0;
    float *v0;
    float *v1;
    float *a0;
    float *a1;
    float *a3;
    const float *a2;
    const float *t1;
    const float *t4;
    float f2;

    (void)arg2;
    memset(arg1, 0, 0x100);
    memset(arg3, 0, 0x100);

    t4 = arg3 + 64;
    t0 = arg3;
    t2 = coscoff;
    t1 = arg1 + 64;
    f2 = 0.5f;
    t3 = t2;

label_34e30:
    a1 = arg1;
    a2 = t3;
    a0 = t0 + 8;
    v1 = a1;
    v0 = t0;

label_34e44:
    {
        float f0 = *v1;
        float f1 = *v0;

        v0 += 1;
        f0 = f2 * f0;
        v1 += 1;
        f0 = f1 + f0;
        *(v0 - 1) = f0;
        if (v0 != a0) {
            goto label_34e44;
        }
    }

    a1 += 8;
    if (t1 == a1) {
        goto label_34e78;
    }
    a2 += 1;
    f2 = *a2;
    goto label_34e44;

label_34e78:
    if (v0 == t4) {
        goto label_34e8c;
    }
    t3 += 8;
    f2 = *t3;
    t0 = v0;
    goto label_34e30;

label_34e8c:
    f2 = 0.5f;
    a3 = arg1;
    t1 = arg1 + 8;
    t0 = arg3 + 8;
    a1 = arg3;
    a2 = coscoff;
    a0 = a3 + 64;

label_34ea8:
    v1 = a1;
    v0 = a3;

label_34eb0:
    {
        float f0 = *v1;
        float f1 = *v0;

        v0 += 8;
        f0 = f2 * f0;
        v1 += 8;
        f0 = f1 + f0;
        *(v0 - 8) = f0;
        if (a0 != v0) {
            goto label_34eb0;
        }
    }

    a1 += 1;
    if (t0 == a1) {
        goto label_34ee4;
    }
    a2 += 1;
    f2 = *a2;
    goto label_34ea8;

label_34ee4:
    a3 += 1;
    if (t1 == a3) {
        return 0;
    }
    a2 += 8;
    f2 = *a2;
    goto label_34ea8;
}

float corcoeff8(float *arg1, float *arg2, int32_t arg3)
{
    double f0;
    double f24;
    double f20;
    float fsum;
    float f12;
    float f22;

    if (arg3 <= 0) {
        f12 = 0.0f;
        f22 = 0.0f;
        f20 = f12;
        goto label_34fac;
    }

    {
        float *v0 = arg1;
        float *v1 = arg1 + arg3;

        fsum = 0.0f;
        do {
            float f1 = *v0;

            v0 += 1;
            fsum = fsum + f1;
        } while (v0 != v1);

        fsum = fsum / (float)arg3;
        f12 = 0.0f;
        f20 = 0.0f;
        f22 = 0.0f;

        do {
            float f2 = *arg1;
            float f1 = *arg2;
            float f3;

            arg1 += 1;
            f2 = f2 - fsum;
            f1 = f1 - fsum;
            arg2 += 1;
            f3 = f2 * f2;
            f2 = f2 * f1;
            f1 = f1 * f1;
            f12 = f12 + f3;
            f20 = f20 + f2;
            f22 = f22 + f1;
        } while (arg1 != v1);

        f20 = (double)f20;
    }

label_34fac:
    f24 = sqrt((double)f12);
    if (f24 != f24) {
        f24 = sqrt((double)f12);
    }

    f0 = sqrt((double)f22);
    if (f0 != f0) {
        f0 = sqrt((double)f22);
    }

    f0 = f24 * f0;
    f0 = f20 / f0;

    {
        float result = (float)f0;
        uint32_t bits = float_abs_bits(result);

        memcpy(&result, &bits, sizeof(result));
        return result;
    }
}

void *watermark_init(void)
{
    void *v0;

    v0 = malloc(0x424);
    if (v0 == 0) {
        c_log(0, "calloc wm failed\n");
        return 0;
    }

    *(uint32_t *)((char *)v0 + 0x0) = 0x21;
    *(uint32_t *)((char *)v0 + 0x4) = 0xc;
    *(uint32_t *)((char *)v0 + 0x8) = 0x10;
    *(float *)((char *)v0 + 0xc) = 0.35355299711227417f;
    *(float *)((char *)v0 + 0x10) = 0.35355299711227417f;
    *(uint32_t *)((char *)v0 + 0x14) = (uint32_t)(uintptr_t)((char *)v0 + 0x24);
    *(uint32_t *)((char *)v0 + 0x18) = (uint32_t)(uintptr_t)((char *)v0 + 0x124);
    *(uint32_t *)((char *)v0 + 0x1c) = (uint32_t)(uintptr_t)((char *)v0 + 0x224);
    *(uint32_t *)((char *)v0 + 0x20) = (uint32_t)(uintptr_t)((char *)v0 + 0x324);
    return v0;
}

void watermark_deinit(void *arg1)
{
    if (arg1 == 0) {
        return;
    }

    free(arg1);
}

int32_t embed_watermark(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    int32_t s7;
    int32_t s4;
    int32_t s5;
    int32_t s3;
    int32_t row_offset;
    double f28;
    double f26;
    double f24;
    double f22;
    double f20;

    (void)arg2;
    s7 = arg3;
    if (arg3 < 0x40 || arg4 < *(int32_t *)((char *)arg1 + 0x8)) {
        c_log(0, "wxh=(%d,%d) is too small\n", arg3, arg4);
        return -1;
    }

    if (*(int32_t *)((char *)arg1 + 0x8) <= 0) {
        return 0;
    }

    row_offset = 0;
    f26 = -1.0;
    f28 = 1.0;
    f20 = 5e-06;
    f24 = 5e-06;
    f22 = 5e-06;
    s4 = 0;

label_3522c:
    {
        int32_t s0 = s4 + 7;
        int32_t s1_base;

        if (!(s4 < 0)) {
            s0 = s4;
        }
        s0 = (s0 >> 3) << 3;
        s1_base = s0;
        s5 = 0;
        s3 = 0x100;

label_35250:
        {
            int32_t a0 = 0;
            int32_t s1 = s5 + row_offset;
            float *a2 = (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x14);

            s1 += arg2;
            do {
                int32_t v0 = a0 >> 3;
                int32_t v1 = v0 * s7;

                a2 += 1;
                v0 = v1 + s1;
                v1 = a0 & 7;
                v0 = v0 + v1;
                {
                    uint8_t pixel = *(uint8_t *)(uintptr_t)v0;

                    a0 += 1;
                    *(a2 - 1) = (float)(int32_t)pixel;
                }
            } while (a0 != 0x40);
        }

        watermark_dct8_constprop_3((float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x18), 0,
                                   (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x1c));

        {
            float *v0 = (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x18);
            uint32_t v1 = float_abs_bits(v0[0]);

            if ((float)v1 < 0.35355299711227417f) {
                memcpy(v0, watermarkn, 0x100);
                goto label_352d0;
            }
        }

label_352d0:
        {
            float *v0 = (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x18);
            int32_t *a0 = (int32_t *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x1c);
            float *v1 = (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x20);
            float *t0 = (float *)((char *)a0 + 0x100);

            do {
                *a0 = (int32_t)float_abs_bits(*v0);
                a0 += 1;
                v0 += 1;
            } while ((float *)a0 != t0);

            memcpy(v1, (void *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x1c), 0x100);
        }

        fsort64((float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x1c), 0, 0x3f);

        {
            float *s4p = (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x1c);
            float *a1 = (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x20);
            int32_t a0 = 0;
            int32_t v1;
            int32_t a4;
            int32_t a3;
            int32_t a6;
            float f2 = s4p[0x1e];

            v1 = 0;
label_35388:
            {
                float f0 = *(float *)((char *)a1 + v1);

                f0 = f2 - f0;
                if ((double)(float)(int32_t)float_abs_bits(f0) < f20) {
                    goto label_353ac;
                }
            }
            v1 += 4;
            a0 += 4;
            if (a0 != s3) {
                goto label_35388;
            }
            a4 = -4;
            goto label_353ac;

label_353ac:
            {
                int32_t v3 = 0;
                float f2_2 = s4p[0x1f];

                a6 = (int32_t)(uintptr_t)a1;
label_353c8:
                {
                    float f0 = *(float *)(uintptr_t)a6;

                    f0 = f2_2 - f0;
                    if ((double)(float)(int32_t)float_abs_bits(f0) < f24) {
                        goto label_353ec;
                    }
                }
                a6 += 4;
                v3 += 4;
                if (v3 != s3) {
                    goto label_353c8;
                }
                a3 = -4;
                goto label_353ec;
            }

label_353ec:
            {
                int32_t v6 = 0;
                float f2_3 = s4p[0x20];

label_35404:
                {
                    float f0 = *(float *)((char *)a1 + v6);

                    f0 = f2_3 - f0;
                    if ((double)(float)(int32_t)float_abs_bits(f0) < f22) {
                        goto label_35428;
                    }
                }
                v6 += 4;
                if (v6 != s3) {
                    goto label_35404;
                }
                a6 = -4;
            }

label_35428:
            {
                float *s2 = (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x18);
                int32_t t1 = *(int32_t *)((char *)arg1 + 0x0);
                int32_t t0 = *(int32_t *)((char *)arg1 + 0x4);
                double f10 = f26;
                double f2d;
                double f8d;
                double f0;
                int32_t *v1p;
                int32_t a1v;
                uint32_t a2v;

                t1 = (t1 << 2);
                t0 = (t0 << 2);
                t1 = (int32_t)(uintptr_t)s2 + t1;
                t0 = (int32_t)(uintptr_t)s2 + t0;
                if (0.0f < *(float *)(uintptr_t)t1) {
                    f10 = f28;
                }
                if (0.0f < *(float *)(uintptr_t)t0) {
                    f2d = 1.0;
                } else {
                    f2d = -1.0;
                }

                {
                    float f1 = *(float *)((char *)s2 + a0);

                    if (0.0f < f1) {
                        f8d = 1.0;
                    } else {
                        f8d = -1.0;
                    }
                }

                v1p = (int32_t *)((char *)s2 + a4);
                a1v = *v1p;
                {
                    float f1 = *(float *)((char *)s2 + a6);
                    int32_t a7 = -1;

                    if (0.0f < f1) {
                        a7 = 1;
                    } else {
                        f0 = -1.0;
                        goto label_354c0;
                    }

                    f0 = 1.0;
label_354c0:
                    a2v = (uint32_t)a1v & 0x7fffffffU;
                    if (80.0f < (float)a2v) {
                        float f4 = (float)a2v;

                        *(double *)&f0 = f0;
                        f4 = (float)(((double)(float)a7) * 5e-06);
                        a1v = (int32_t)float_abs_bits(f4);
                    }
                }

                *v1p = a1v;
                {
                    float f4 = (float)(int32_t)((uint32_t)a1v & 0x7fffffffU);

                    *(float *)(uintptr_t)t1 = (float)((double)f4 * f8d);
                }

                {
                    int32_t a5 = (s5 >> 3) + s0;
                    int32_t t3 = a5 << 3;
                    float *s6 = (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x14);
                    int32_t a7 = t3 + 4;
                    float f8 = *(float *)(uintptr_t)t1;
                    int32_t v3 = float_abs_bits(*v1p);

                    *(float *)((char *)s2 + a0) = (float)(((double)(float)v3) * *(double *)&f0);
                    *(float *)(uintptr_t)t1 =
                        (float)(((double)(*(float *)((char *)arg1 + 0xc) * watermarkn[t3] + 0.10000000149011612f)) *
                                ((double)(*(float *)(uintptr_t)t1) * f10));
                    *(float *)(uintptr_t)t0 =
                        (float)(((double)(*(float *)((char *)arg1 + 0xc) * watermarkn[a7] + 0.10000000149011612f)) *
                                ((double)(*(float *)(uintptr_t)t0) * f2d));

                    memset(s6, 0, 0x100);
                    memset(s4p, 0, 0x100);

                    {
                        const float *t4 = coscoff;
                        const float *t5 = coscoff + 64;
                        float *t0p = s2;
                        float *a3p = s4p;
                        const float *t3p;
                        float f2 = 0.5f;

label_3560c:
                        {
                            float *a1p = a3p;
                            const float *a2p = t4;
                            float *a4p = a1p + 64;
                            float *v0p = a1p;
                            const float *v1q = t0p;

label_35618:
                            {
                                float f0v = *v1q;
                                float f1v = *v0p;

                                v0p += 8;
                                f0v = f2 * f0v;
                                v1q += 8;
                                f0v = f1v + f0v;
                                *(v0p - 8) = f0v;
                                if (a4p != v0p) {
                                    goto label_35618;
                                }
                            }

                            a1p += 1;
                            if (t0p + 8 == a1p) {
                                goto label_3564c;
                            }
                            a2p += 1;
                            f2 = *a2p;
                            goto label_35618;
                        }

label_3564c:
                        t4 += 8;
                        if (t5 == t4) {
                            goto label_35660;
                        }
                        t0p += 1;
                        f2 = *t4;
                        goto label_3560c;
                    }

label_35660:
                    {
                        const float *t5 = coscoff + 64;
                        const float *t3p = coscoff;
                        float *v0p = s6;
                        const float *a5p = t3p;
                        float f2 = 0.5f;

label_35674:
                        {
                            float *a4p = v0p + 8;
                            const float *v1q = s4p;
                            float *v2p = v0p;

label_3567c:
                            {
                                float f0v = *v1q;
                                float f1v = *v2p;

                                v2p += 1;
                                f0v = f2 * f0v;
                                v1q += 1;
                                f0v = f1v + f0v;
                                *(v2p - 1) = f0v;
                                if (v2p != a4p) {
                                    goto label_3567c;
                                }
                            }

                            a5p += 1;
                            if (s6 + 64 == v2p) {
                                goto label_356ac;
                            }
                            f2 = *a5p;
                            goto label_3567c;
                        }

label_356ac:
                        t3p += 8;
                        if (t5 == t3p) {
                            goto label_356c8;
                        }
                        v0p += 8;
                        a5p = t3p;
                        f2 = *t3p;
                        goto label_35674;
                    }

label_356c8:
                    {
                        float *a5 = (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x14);
                        int32_t a0v = 0;
                        int32_t a3v = 0x80000000;

                        do {
                            int32_t v0 = a0v >> 3;
                            int32_t t0v = v0 * s7;
                            double fd = (double)*a5 + 0.41999998688697815;
                            int32_t v1x;

                            v1x = t0v + s1_base + (a0v & 7);
                            if (0.5398999452590942 <= fd) {
                                fd = fd - 0.5398999452590942;
                                a0v += 1;
                                a5 += 1;
                                *(uint8_t *)(uintptr_t)v1x = (uint8_t)((int32_t)fd | a3v);
                            } else {
                                a0v += 1;
                                *(uint8_t *)(uintptr_t)v1x = (uint8_t)(int32_t)fd;
                                a5 += 1;
                            }
                        } while (a0v != 0x40);
                    }
                }
            }
        }

        s5 += 8;
        if (s5 != *(int32_t *)((char *)arg1 + 0x0)) {
            goto label_35250;
        }
    }

    s4 += 8;
    row_offset += arg3 << 3;
    if (s4 < *(int32_t *)((char *)arg1 + 0x8)) {
        goto label_3522c;
    }
    return 0;
}

int32_t check_watermark(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    float var_148[64];
    int32_t s8;
    int32_t s4;
    int32_t block_offset;
    double f22;
    float f24;
    double f20;

    (void)arg2;
    s8 = arg3;
    if (arg3 < 0x40 || arg4 < *(int32_t *)((char *)arg1 + 0x8)) {
        c_log(0, "wxh=(%d,%d) is too small\n", arg3, arg4);
        return -1;
    }

    memset(var_148, 0, sizeof(var_148));
    if (*(int32_t *)((char *)arg1 + 0x8) <= 0) {
        goto label_35a9c;
    }

    f22 = 4.999999873689376e-05;
    f24 = 4.999999873689376e-05f;
    f20 = 1.0;
    block_offset = 0;
    s4 = 0;

label_35924:
    {
        int32_t v1 = block_offset;
        int32_t s6 = 0;
        int32_t s1 = v1 + 7;

        if (!(v1 < 0)) {
            s1 = v1;
        }
        s1 = (s1 >> 3) << 3;

label_35940:
        {
            int32_t v3 = 0;
            int32_t t3 = s6 + s4;
            float *t1 = (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x14);

            t3 += arg2;
            do {
                int32_t v0 = v3 >> 3;
                int32_t a7 = v0 * s8;
                int32_t t2 = v3 & 7;
                uint8_t pixel;

                t1 += 1;
                v3 += 1;
                v0 = a7 + t3;
                v0 += t2;
                pixel = *(uint8_t *)(uintptr_t)v0;
                *(t1 - 1) = (float)(int32_t)pixel;
            } while (v3 != 0x40);
        }

        watermark_dct8_constprop_3((float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x18), 0,
                                   (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x1c));

        {
            float *a0 = (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x1c);
            float *v0 = (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x18);
            float *a1 = a0;
            float *a2 = v0 + 64;

            do {
                *a1 = (float)float_abs_bits(*v0);
                a1 += 1;
                v0 += 1;
            } while (v0 != a2);
        }

        fsort64((float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x1c), 0, 0x3f);

        {
            float *a2 = (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x1c);
            float f0 = a2[0x20];
            double f4;
            int32_t v0;
            float *a0;
            int32_t v3;
            int32_t t1;

            if (f0 == 0.0f) {
                f4 = f22;
                f0 = f24;
                goto label_359e8;
            }

            f4 = (double)f0;
            a2[0x20] = f0;

label_359e8:
            v0 = *(int32_t *)((char *)arg1 + 0x0);
            a0 = (float *)(uintptr_t)*(uint32_t *)((char *)arg1 + 0x18);
            v0 = *(int32_t *)((char *)a0 + (v0 << 2));
            v3 = *(int32_t *)((char *)arg1 + 0x4);
            t1 = ((s6 >> 3) + s1) << 3;
            var_148[t1 >> 2] = (float)((((double)(float)((uint32_t)v0 & 0x7fffffffU) / f4) - f20) /
                                       (double)*(float *)((char *)arg1 + 0xc));
            v3 = *(int32_t *)((char *)a0 + (v3 << 2));
            var_148[(t1 >> 2) + 1] =
                (float)((((double)(float)((uint32_t)v3 & 0x7fffffffU) / (double)a2[0x20]) - f20) /
                        (double)*(float *)((char *)arg1 + 0xc));
        }

        s6 += 8;
        if (s6 != 0x40) {
            goto label_35940;
        }
    }

    block_offset += 8;
    if (block_offset < *(int32_t *)((char *)arg1 + 0x8)) {
        s4 += arg3 << 3;
        goto label_35924;
    }

label_35a9c:
    {
        float f0 = corcoeff8((float *)watermarkn, var_148, *(int32_t *)((char *)arg1 + 0x8) << 1);

        if (*(float *)((char *)arg1 + 0x10) <= f0) {
            return 1;
        }
        return 0;
    }
}
