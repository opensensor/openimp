#include <stdint.h>
#include <stddef.h>

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, void *caller);

static const int16_t quant_coef4_2643[0x60] = {
    13107, 8066, 13107, 8066, 8066, 5243, 8066, 5243, 13107, 8066, 13107, 8066, 8066, 5243, 8066, 5243,
    11916, 7490, 11916, 7490, 7490, 4660, 7490, 4660, 11916, 7490, 11916, 7490, 7490, 4660, 7490, 4660,
    10082, 6554, 10082, 6554, 6554, 4194, 6554, 4194, 10082, 6554, 10082, 6554, 6554, 4194, 6554, 4194,
    9362, 5825, 9362, 5825, 5825, 3647, 5825, 3647, 9362, 5825, 9362, 5825, 5825, 3647, 5825, 3647,
    8192, 5243, 8192, 5243, 5243, 3355, 5243, 3355, 8192, 5243, 8192, 5243, 5243, 3355, 5243, 3355,
    7282, 4559, 7282, 4559, 4559, 2893, 4559, 2893, 7282, 4559, 7282, 4559, 4559, 2893, 4559, 2893,
};

static const int16_t quant_coef8_2653[0x180] = {
    13107, 12222, 16777, 12222, 13107, 12222, 16777, 12222, 12222, 11428, 15481, 11428, 12222, 11428, 15481, 11428,
    16777, 15481, 20972, 15481, 16777, 15481, 20972, 15481, 12222, 11428, 15481, 11428, 12222, 11428, 15481, 11428,
    13107, 12222, 16777, 12222, 13107, 12222, 16777, 12222, 12222, 11428, 15481, 11428, 12222, 11428, 15481, 11428,
    16777, 15481, 20972, 15481, 16777, 15481, 20972, 15481, 12222, 11428, 15481, 11428, 12222, 11428, 15481, 11428,
    11916, 11058, 14980, 11058, 11916, 11058, 14980, 11058, 11058, 10826, 14290, 10826, 11058, 10826, 14290, 10826,
    14980, 14290, 19174, 14290, 14980, 14290, 19174, 14290, 11058, 10826, 14290, 10826, 11058, 10826, 14290, 10826,
    11916, 11058, 14980, 11058, 11916, 11058, 14980, 11058, 11058, 10826, 14290, 10826, 11058, 10826, 14290, 10826,
    14980, 14290, 19174, 14290, 14980, 14290, 19174, 14290, 11058, 10826, 14290, 10826, 11058, 10826, 14290, 10826,
    10082, 9675, 12710, 9675, 10082, 9675, 12710, 9675, 9675, 8943, 11985, 8943, 9675, 8943, 11985, 8943,
    12710, 11985, 15978, 11985, 12710, 11985, 15978, 11985, 9675, 8943, 11985, 8943, 9675, 8943, 11985, 8943,
    10082, 9675, 12710, 9675, 10082, 9675, 12710, 9675, 9675, 8943, 11985, 8943, 9675, 8943, 11985, 8943,
    12710, 11985, 15978, 11985, 12710, 11985, 15978, 11985, 9675, 8943, 11985, 8943, 9675, 8943, 11985, 8943,
    9362, 8931, 11984, 8931, 9362, 8931, 11984, 8931, 8931, 8228, 11259, 8228, 8931, 8228, 11259, 8228,
    11984, 11259, 14913, 11259, 11984, 11259, 14913, 11259, 8931, 8228, 11259, 8228, 8931, 8228, 11259, 8228,
    9362, 8931, 11984, 8931, 9362, 8931, 11984, 8931, 8931, 8228, 11259, 8228, 8931, 8228, 11259, 8228,
    11984, 11259, 14913, 11259, 11984, 11259, 14913, 11259, 8931, 8228, 11259, 8228, 8931, 8228, 11259, 8228,
    8192, 7740, 10486, 7740, 8192, 7740, 10486, 7740, 7740, 7346, 9777, 7346, 7740, 7346, 9777, 7346,
    10486, 9777, 13159, 9777, 10486, 9777, 13159, 9777, 7740, 7346, 9777, 7346, 7740, 7346, 9777, 7346,
    8192, 7740, 10486, 7740, 8192, 7740, 10486, 7740, 7740, 7346, 9777, 7346, 7740, 7346, 9777, 7346,
    10486, 9777, 13159, 9777, 10486, 9777, 13159, 9777, 7740, 7346, 9777, 7346, 7740, 7346, 9777, 7346,
    7282, 6830, 9118, 6830, 7282, 6830, 9118, 6830, 6830, 6428, 8640, 6428, 6830, 6428, 8640, 6428,
    9118, 8640, 11570, 8640, 9118, 8640, 11570, 8640, 6830, 6428, 8640, 6428, 6830, 6428, 8640, 6428,
    7282, 6830, 9118, 6830, 7282, 6830, 9118, 6830, 6830, 6428, 8640, 6428, 6830, 6428, 8640, 6428,
    9118, 8640, 11570, 8640, 9118, 8640, 11570, 8640, 6830, 6428, 8640, 6428, 6830, 6428, 8640, 6428,
};

static const int32_t g_quantScales[8] = {
    419430, 372827, 328965, 294337, 262144, 233016, 0, 0,
};

static const uint32_t data_66666 = 0x66666;

static const int32_t AL_AVC_ENC_SCL_ORDER_4x4[0x10] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 12, 13, 14, 15,
};

static const int32_t AL_HEVC_ENC_SCL_ORDER_4x4[0x10] = {
    0, 4, 8, 12, 1, 5, 9, 13,
    2, 6, 10, 14, 3, 7, 11, 15,
};

static const int32_t AL_SLOW_HEVC_ENC_SCL_ORDER_8x8[0x40] = {
    0, 8, 16, 24, 32, 40, 48, 56, 1, 9, 17, 25, 33, 41, 49, 57,
    2, 10, 18, 26, 34, 42, 50, 58, 3, 11, 19, 27, 35, 43, 51, 59,
    4, 12, 20, 28, 36, 44, 52, 60, 5, 13, 21, 29, 37, 45, 53, 61,
    6, 14, 22, 30, 38, 46, 54, 62, 7, 15, 23, 31, 39, 47, 55, 63,
};

static const int32_t AL_FAST_HEVC_ENC_SCL_ORDER_8x8[0x40] = {
    0, 8, 16, 24, 1, 9, 17, 25, 32, 40, 48, 56, 33, 41, 49, 57,
    2, 10, 18, 26, 3, 11, 19, 27, 34, 42, 50, 58, 35, 43, 51, 59,
    4, 12, 20, 28, 5, 13, 21, 29, 36, 44, 52, 60, 37, 45, 53, 61,
    6, 14, 22, 30, 7, 15, 23, 31, 38, 46, 54, 62, 39, 47, 55, 63,
};

static const int32_t AL_FAST_AVC_ENC_SCL_ORDER_8x8[0x40] = {
    0, 1, 2, 3, 8, 9, 10, 11, 4, 5, 6, 7, 12, 13, 14, 15,
    16, 17, 18, 19, 24, 25, 26, 27, 20, 21, 22, 23, 28, 29, 30, 31,
    32, 33, 34, 35, 40, 41, 42, 43, 36, 37, 38, 39, 44, 45, 46, 47,
    48, 49, 50, 51, 56, 57, 58, 59, 52, 53, 54, 55, 60, 61, 62, 63,
};

void AL_sWriteFwdCoeffs(uint32_t **arg1, void *arg2, int32_t arg3, void *arg4);
void AL_sWriteInvCoeff(void *arg1, void *arg2, int32_t arg3, uint32_t **arg4);
int32_t AL_HEVC_WriteEncHwScalingList(void *arg1, void *arg2, uint32_t *arg3);

int32_t AL_AVC_GenerateHwScalingList(void *arg1, int32_t arg2)
{
    const int16_t *s2 = quant_coef4_2643;
    const int16_t *s3 = quant_coef8_2653;
    int32_t s1 = 0;
    uint32_t v0_3 = 0x3333;
    uint32_t t0 = 0x3333;

    while (1) {
        const int16_t *t2_1 = &s3[1];
        uint8_t *t1_1 = (uint8_t *)arg1 + 0x1bc;
        uint8_t *a3_2 = (uint8_t *)(intptr_t)arg2 + s1 + 0x400;
        const int16_t *t3_1 = t2_1;
        uint32_t v1_1 = v0_3;

        while (1) {
            uint32_t a2_1 = (uint32_t)*t1_1;

            a3_2 += 4;
            if (a2_1 == 0U)
                __builtin_trap();
            t1_1 = &t1_1[1];
            *(int32_t *)(a3_2 - 4) = (int32_t)(((v1_1 << 4) + (a2_1 >> 1)) / a2_1);
            if (a3_2 == (uint8_t *)(intptr_t)arg2 + s1 + 0x500)
                break;
            v1_1 = (uint32_t)*t3_1;
            t3_1 = &t3_1[1];
        }

        {
            uint8_t *a3_3 = (uint8_t *)arg1 + 0x3c;
            uint8_t *t1_3 = (uint8_t *)(intptr_t)arg2 + s1 + 0x700;
            const int16_t *t3_2 = s2;
            uint32_t a0_1 = t0;

            while (1) {
                uint32_t a2_2 = (uint32_t)*a3_3;

                t1_3 += 4;
                if (a2_2 == 0U)
                    __builtin_trap();
                a3_3 = &a3_3[1];
                t3_2 = &t3_2[1];
                *(int32_t *)(t1_3 - 4) = (int32_t)(((a0_1 << 4) + (a2_2 >> 1)) / a2_2);
                if (a3_3 == (uint8_t *)arg1 + 0x4c)
                    break;
                a0_1 = (uint32_t)*t3_2;
            }
        }

        {
            uint8_t *a3_4 = (uint8_t *)arg1 + 0x7c;
            uint8_t *t1_5 = (uint8_t *)(intptr_t)arg2 + s1 + 0x740;
            const int16_t *t3_3 = s2;
            uint32_t a0_3 = t0;

            while (1) {
                uint32_t a2_3 = (uint32_t)*a3_4;

                t1_5 += 4;
                if (a2_3 == 0U)
                    __builtin_trap();
                a3_4 = &a3_4[1];
                t3_3 = &t3_3[1];
                *(int32_t *)(t1_5 - 4) = (int32_t)(((a0_3 << 4) + (a2_3 >> 1)) / a2_3);
                if (a3_4 == (uint8_t *)arg1 + 0x8c)
                    break;
                a0_3 = (uint32_t)*t3_3;
            }
        }

        {
            uint8_t *a3_5 = (uint8_t *)arg1 + 0xbc;
            uint8_t *t1_7 = (uint8_t *)(intptr_t)arg2 + s1 + 0x780;
            const int16_t *t3_4 = s2;
            uint32_t a0_5 = t0;

            while (1) {
                uint32_t a2_4 = (uint32_t)*a3_5;

                t1_7 += 4;
                if (a2_4 == 0U)
                    __builtin_trap();
                a3_5 = &a3_5[1];
                t3_4 = &t3_4[1];
                *(int32_t *)(t1_7 - 4) = (int32_t)(((a0_5 << 4) + (a2_4 >> 1)) / a2_4);
                if (a3_5 == (uint8_t *)arg1 + 0xcc)
                    break;
                a0_5 = (uint32_t)*t3_4;
            }
        }

        {
            uint8_t *a3_6 = (uint8_t *)arg1 + 0x27c;
            uint8_t *a2_6 = (uint8_t *)(intptr_t)arg2 + s1 + 0x32e0;
            uint32_t v1_14 = v0_3;

            while (1) {
                uint32_t a0_7 = (uint32_t)*a3_6;

                a2_6 += 4;
                if (a0_7 == 0U)
                    __builtin_trap();
                a3_6 = &a3_6[1];
                *(int32_t *)(a2_6 - 4) = (int32_t)(((v1_14 << 4) + (a0_7 >> 1)) / a0_7);
                if (a2_6 == (uint8_t *)(intptr_t)arg2 + s1 + 0x33e0)
                    break;
                v1_14 = (uint32_t)*t2_1;
                t2_1 = &t2_1[1];
            }
        }

        {
            uint8_t *a0_8 = (uint8_t *)arg1 + 0xfc;
            uint8_t *a2_8 = (uint8_t *)(intptr_t)arg2 + s1 + 0x35e0;
            const int16_t *a3_7 = s2;
            uint32_t v1_16 = t0;

            while (1) {
                uint32_t t1_10 = (uint32_t)*a0_8;

                a2_8 += 4;
                if (t1_10 == 0U)
                    __builtin_trap();
                a0_8 = &a0_8[1];
                a3_7 = &a3_7[1];
                *(int32_t *)(a2_8 - 4) = (int32_t)(((v1_16 << 4) + (t1_10 >> 1)) / t1_10);
                if (a0_8 == (uint8_t *)arg1 + 0x10c)
                    break;
                v1_16 = (uint32_t)*a3_7;
            }
        }

        {
            uint8_t *a0_9 = (uint8_t *)arg1 + 0x13c;
            uint8_t *a2_10 = (uint8_t *)(intptr_t)arg2 + s1 + 0x3620;
            const int16_t *a3_8 = s2;
            uint32_t v1_18 = t0;

            while (1) {
                uint32_t t1_11 = (uint32_t)*a0_9;

                a2_10 += 4;
                if (t1_11 == 0U)
                    __builtin_trap();
                a0_9 = &a0_9[1];
                a3_8 = &a3_8[1];
                *(int32_t *)(a2_10 - 4) = (int32_t)(((v1_18 << 4) + (t1_11 >> 1)) / t1_11);
                if (a0_9 == (uint8_t *)arg1 + 0x14c)
                    break;
                v1_18 = (uint32_t)*a3_8;
            }
        }

        {
            uint8_t *a0_10 = (uint8_t *)arg1 + 0x17c;
            uint8_t *a2_12 = (uint8_t *)(intptr_t)arg2 + s1 + 0x3660;
            const int16_t *a3_9 = s2;
            uint32_t v1_20 = t0;

            while (1) {
                uint32_t t0_1 = (uint32_t)*a0_10;

                a2_12 += 4;
                if (t0_1 == 0U)
                    __builtin_trap();
                a0_10 = &a0_10[1];
                a3_9 = &a3_9[1];
                *(int32_t *)(a2_12 - 4) = (int32_t)(((v1_20 << 4) + (t0_1 >> 1)) / t0_1);
                if (a0_10 == (uint8_t *)arg1 + 0x18c)
                    break;
                v1_20 = (uint32_t)*a3_9;
            }
        }

        s1 += 0x7d0;
        s2 = &s2[0x10];
        s3 = &s3[0x40];
        if (s1 == 0x2ee0)
            break;
        t0 = (uint32_t)*s2;
        v0_3 = (uint32_t)*s3;
    }

    return 0x2ee0;
}

int32_t AL_HEVC_GenerateHwScalingList(void *arg1, void *arg2)
{
    const int32_t *t8 = g_quantScales;
    uint8_t *t1 = (uint8_t *)arg2 + 0x35e0;
    uint32_t v0_11 = data_66666;
    int32_t t9 = 0;

    while (1) {
        uint8_t *t0_1 = (uint8_t *)arg1 + 0x4bc;
        uint8_t *a3_1 = (uint8_t *)arg2 + 0x100;
        uint8_t *a2_1 = (uint8_t *)arg2;

        do {
            uint32_t v1_1 = (uint32_t)*t0_1;

            a2_1 += 4;
            t0_1 = &t0_1[1];
            if (v1_1 == 0U)
                __builtin_trap();
            *(int32_t *)(a2_1 - 4) = (int32_t)(v0_11 / v1_1);
        } while (a2_1 != a3_1);

        {
            uint8_t *t0_2 = (uint8_t *)arg1 + 0x33c;
            uint8_t *a2_2 = (uint8_t *)arg2 + 0x200;

            do {
                uint32_t v1_3 = (uint32_t)*t0_2;

                a3_1 += 4;
                t0_2 = &t0_2[1];
                if (v1_3 == 0U)
                    __builtin_trap();
                *(int32_t *)(a3_1 - 4) = (int32_t)(v0_11 / v1_3);
            } while (a3_1 != a2_2);

            {
                uint8_t *t0_3 = (uint8_t *)arg1 + 0x37c;
                uint8_t *a3_2 = (uint8_t *)arg2 + 0x300;

                do {
                    uint32_t v1_5 = (uint32_t)*t0_3;

                    a2_2 += 4;
                    t0_3 = &t0_3[1];
                    if (v1_5 == 0U)
                        __builtin_trap();
                    *(int32_t *)(a2_2 - 4) = (int32_t)(v0_11 / v1_5);
                } while (a2_2 != a3_2);

                {
                    uint8_t *t0_4 = (uint8_t *)arg1 + 0x3bc;
                    uint8_t *a2_3 = (uint8_t *)arg2 + 0x400;

                    do {
                        uint32_t v1_7 = (uint32_t)*t0_4;

                        a3_2 += 4;
                        t0_4 = &t0_4[1];
                        if (v1_7 == 0U)
                            __builtin_trap();
                        *(int32_t *)(a3_2 - 4) = (int32_t)(v0_11 / v1_7);
                    } while (a3_2 != a2_3);

                    {
                        uint8_t *t0_5 = (uint8_t *)arg1 + 0x1bc;
                        uint8_t *a3_3 = (uint8_t *)arg2 + 0x500;

                        do {
                            uint32_t v1_9 = (uint32_t)*t0_5;

                            a2_3 += 4;
                            t0_5 = &t0_5[1];
                            if (v1_9 == 0U)
                                __builtin_trap();
                            *(int32_t *)(a2_3 - 4) = (int32_t)(v0_11 / v1_9);
                        } while (a2_3 != a3_3);

                        {
                            uint8_t *t0_6 = (uint8_t *)arg1 + 0x1fc;
                            uint8_t *a2_4 = (uint8_t *)arg2 + 0x600;

                            do {
                                uint32_t v1_11 = (uint32_t)*t0_6;

                                a3_3 += 4;
                                t0_6 = &t0_6[1];
                                if (v1_11 == 0U)
                                    __builtin_trap();
                                *(int32_t *)(a3_3 - 4) = (int32_t)(v0_11 / v1_11);
                            } while (a3_3 != a2_4);

                            {
                                uint8_t *a3_4 = (uint8_t *)arg1 + 0x23c;
                                uint8_t *t0_7 = (uint8_t *)arg2 + 0x700;

                                do {
                                    uint32_t v1_13 = (uint32_t)*a3_4;

                                    a2_4 += 4;
                                    a3_4 = &a3_4[1];
                                    if (v1_13 == 0U)
                                        __builtin_trap();
                                    *(int32_t *)(a2_4 - 4) = (int32_t)(v0_11 / v1_13);
                                } while (a2_4 != t0_7);

                                {
                                    uint8_t *i = (uint8_t *)arg1 + 0x3c;

                                    do {
                                        uint32_t v1_15 = (uint32_t)*i;

                                        t0_7 += 4;
                                        i = &i[1];
                                        if (v1_15 == 0U)
                                            __builtin_trap();
                                        *(int32_t *)(t0_7 - 4) = (int32_t)(v0_11 / v1_15);
                                    } while (i != (uint8_t *)arg1 + 0x4c);
                                }

                                {
                                    uint8_t *i_1 = (uint8_t *)arg1 + 0x7c;
                                    uint8_t *a3_5 = (uint8_t *)arg2 + 0x740;

                                    do {
                                        uint32_t v1_17 = (uint32_t)*i_1;

                                        a3_5 += 4;
                                        i_1 = &i_1[1];
                                        if (v1_17 == 0U)
                                            __builtin_trap();
                                        *(int32_t *)(a3_5 - 4) = (int32_t)(v0_11 / v1_17);
                                    } while (i_1 != (uint8_t *)arg1 + 0x8c);
                                }

                                {
                                    uint8_t *i_2 = (uint8_t *)arg1 + 0xbc;
                                    uint8_t *a3_6 = (uint8_t *)arg2 + 0x780;

                                    do {
                                        uint32_t v1_19 = (uint32_t)*i_2;

                                        a3_6 += 4;
                                        i_2 = &i_2[1];
                                        if (v1_19 == 0U)
                                            __builtin_trap();
                                        *(int32_t *)(a3_6 - 4) = (int32_t)(v0_11 / v1_19);
                                    } while (i_2 != (uint8_t *)arg1 + 0xcc);
                                }
                            }
                        }
                    }
                }
            }
        }

        {
            uint32_t v1_21 = (uint32_t)*((uint8_t *)arg1 + 0x30);
            uint8_t *t0_8 = (uint8_t *)arg1 + 0x57c;
            uint8_t *a2_5 = (uint8_t *)arg2 + 0x2ee0;
            uint8_t *a3_7;

            if (v1_21 == 0U)
                __builtin_trap();
            a3_7 = (uint8_t *)arg2 + 0x2fe0;
            *(int32_t *)((uint8_t *)arg2 + 0x7c0) = (int32_t)(v0_11 / v1_21);

            {
                uint32_t v1_23 = (uint32_t)*((uint8_t *)arg1 + 0x31);

                if (v1_23 == 0U)
                    __builtin_trap();
                *(int32_t *)((uint8_t *)arg2 + 0x7c4) = (int32_t)(v0_11 / v1_23);
            }

            {
                uint32_t v1_25 = (uint32_t)*((uint8_t *)arg1 + 0x32);

                if (v1_25 == 0U)
                    __builtin_trap();
                *(int32_t *)((uint8_t *)arg2 + 0x7c8) = (int32_t)(v0_11 / v1_25);
            }

            {
                uint32_t v1_27 = (uint32_t)*((uint8_t *)arg1 + 0x36);

                if (v1_27 == 0U)
                    __builtin_trap();
                *(int32_t *)((uint8_t *)arg2 + 0x7cc) = (int32_t)(v0_11 / v1_27);
            }

            do {
                uint32_t v1_29 = (uint32_t)*t0_8;

                a2_5 += 4;
                t0_8 = &t0_8[1];
                if (v1_29 == 0U)
                    __builtin_trap();
                *(int32_t *)(a2_5 - 4) = (int32_t)(v0_11 / v1_29);
            } while (a2_5 != a3_7);

            {
                uint8_t *t0_9 = (uint8_t *)arg1 + 0x3fc;
                uint8_t *a2_6 = (uint8_t *)arg2 + 0x30e0;

                do {
                    uint32_t v1_31 = (uint32_t)*t0_9;

                    a3_7 += 4;
                    t0_9 = &t0_9[1];
                    if (v1_31 == 0U)
                        __builtin_trap();
                    *(int32_t *)(a3_7 - 4) = (int32_t)(v0_11 / v1_31);
                } while (a3_7 != a2_6);

                {
                    uint8_t *t0_10 = (uint8_t *)arg1 + 0x43c;
                    uint8_t *a3_8 = (uint8_t *)arg2 + 0x31e0;

                    do {
                        uint32_t v1_33 = (uint32_t)*t0_10;

                        a2_6 += 4;
                        t0_10 = &t0_10[1];
                        if (v1_33 == 0U)
                            __builtin_trap();
                        *(int32_t *)(a2_6 - 4) = (int32_t)(v0_11 / v1_33);
                    } while (a2_6 != a3_8);

                    {
                        uint8_t *t0_11 = (uint8_t *)arg1 + 0x47c;
                        uint8_t *a2_7 = (uint8_t *)arg2 + 0x32e0;

                        do {
                            uint32_t v1_35 = (uint32_t)*t0_11;

                            a3_8 += 4;
                            t0_11 = &t0_11[1];
                            if (v1_35 == 0U)
                                __builtin_trap();
                            *(int32_t *)(a3_8 - 4) = (int32_t)(v0_11 / v1_35);
                        } while (a2_7 != a3_8);

                        {
                            uint8_t *t0_12 = (uint8_t *)arg1 + 0x27c;
                            uint8_t *a3_9 = (uint8_t *)arg2 + 0x33e0;

                            do {
                                uint32_t v1_37 = (uint32_t)*t0_12;

                                a2_7 += 4;
                                t0_12 = &t0_12[1];
                                if (v1_37 == 0U)
                                    __builtin_trap();
                                *(int32_t *)(a2_7 - 4) = (int32_t)(v0_11 / v1_37);
                            } while (a3_9 != a2_7);

                            {
                                uint8_t *t0_13 = (uint8_t *)arg1 + 0x2bc;
                                uint8_t *a2_8 = (uint8_t *)arg2 + 0x34e0;

                                do {
                                    uint32_t v1_39 = (uint32_t)*t0_13;

                                    a3_9 += 4;
                                    t0_13 = &t0_13[1];
                                    if (v1_39 == 0U)
                                        __builtin_trap();
                                    *(int32_t *)(a3_9 - 4) = (int32_t)(v0_11 / v1_39);
                                } while (a2_8 != a3_9);

                                {
                                    uint8_t *a3_10 = (uint8_t *)arg1 + 0x2fc;

                                    do {
                                        uint32_t v1_41 = (uint32_t)*a3_10;

                                        a2_8 += 4;
                                        a3_10 = &a3_10[1];
                                        if (v1_41 == 0U)
                                            __builtin_trap();
                                        *(int32_t *)(a2_8 - 4) = (int32_t)(v0_11 / v1_41);
                                    } while (a2_8 != t1);
                                }
                            }
                        }
                    }
                }
            }

            {
                uint8_t *i_3 = (uint8_t *)arg1 + 0xfc;
                uint8_t *a3_11 = t1;

                do {
                    uint32_t v1_43 = (uint32_t)*i_3;

                    a3_11 += 4;
                    i_3 = &i_3[1];
                    if (v1_43 == 0U)
                        __builtin_trap();
                    *(int32_t *)(a3_11 - 4) = (int32_t)(v0_11 / v1_43);
                } while (i_3 != (uint8_t *)arg1 + 0x10c);
            }

            {
                uint8_t *i_4 = (uint8_t *)arg1 + 0x13c;
                uint8_t *a3_12 = (uint8_t *)arg2 + 0x3620;

                do {
                    uint32_t v1_45 = (uint32_t)*i_4;

                    a3_12 += 4;
                    i_4 = &i_4[1];
                    if (v1_45 == 0U)
                        __builtin_trap();
                    *(int32_t *)(a3_12 - 4) = (int32_t)(v0_11 / v1_45);
                } while (i_4 != (uint8_t *)arg1 + 0x14c);
            }

            {
                uint8_t *i_5 = (uint8_t *)arg1 + 0x17c;
                uint8_t *a3_13 = (uint8_t *)arg2 + 0x3660;

                do {
                    uint32_t v1_47 = (uint32_t)*i_5;

                    a3_13 += 4;
                    i_5 = &i_5[1];
                    if (v1_47 == 0U)
                        __builtin_trap();
                    *(int32_t *)(a3_13 - 4) = (int32_t)(v0_11 / v1_47);
                } while (i_5 != (uint8_t *)arg1 + 0x18c);
            }
        }

        {
            uint32_t v1_49 = (uint32_t)*((uint8_t *)arg1 + 0x33);

            arg2 = (uint8_t *)arg2 + 0x7d0;
            t9 += 1;
            if (v1_49 == 0U)
                __builtin_trap();
            t8 = &t8[1];
            *(int32_t *)((uint8_t *)arg2 + 0x2ed0) = (int32_t)(v0_11 / v1_49);
        }

        {
            uint32_t v1_51 = (uint32_t)*((uint8_t *)arg1 + 0x34);

            if (v1_51 == 0U)
                __builtin_trap();
            *(int32_t *)((uint8_t *)arg2 + 0x2ed4) = (int32_t)(v0_11 / v1_51);
        }

        {
            uint32_t v1_53 = (uint32_t)*((uint8_t *)arg1 + 0x35);

            if (v1_53 == 0U)
                __builtin_trap();
            *(int32_t *)((uint8_t *)arg2 + 0x2ed8) = (int32_t)(v0_11 / v1_53);
        }

        {
            uint32_t v1_55 = (uint32_t)*((uint8_t *)arg1 + 0x39);

            if (v1_55 == 0U)
                __builtin_trap();
            *(int32_t *)((uint8_t *)arg2 + 0x2edc) = (int32_t)(v0_11 / v1_55);
        }

        t1 += 0x7d0;
        if (t9 == 6)
            break;
        v0_11 = (uint32_t)*t8;
    }

    return 6;
}

void AL_sWriteFwdCoeffs(uint32_t **arg1, void *arg2, int32_t arg3, void *arg4)
{
    uint32_t *t2 = *arg1;

    if (arg3 > 0) {
        int32_t v1_1 = (int32_t)(intptr_t)t2 + 0x10;
        int32_t t1_1 = 0;

        if (arg4 != NULL) {
            while (1) {
                int32_t *v0_5 = (int32_t *)((uint8_t *)arg4 + (t1_1 << 4));

                v1_1 += 0x10;
                t1_1 += 1;
                *(int32_t *)(intptr_t)(v1_1 - 0x20) = *(int32_t *)((uint8_t *)arg2 + (*v0_5 << 2));
                *(int32_t *)(intptr_t)(v1_1 - 0x1c) = *(int32_t *)((uint8_t *)arg2 + (v0_5[1] << 2));
                *(int32_t *)(intptr_t)(v1_1 - 0x18) = *(int32_t *)((uint8_t *)arg2 + (v0_5[2] << 2));
                *(int32_t *)(intptr_t)(v1_1 - 0x14) = *(int32_t *)((uint8_t *)arg2 + (v0_5[3] << 2));
                if (arg3 == t1_1)
                    break;
            }
        } else {
            while (1) {
                int32_t v0_6 = t1_1 << 4;
                int32_t *t0_13 = (int32_t *)((uint8_t *)arg2 + v0_6);

                *(int32_t *)(intptr_t)(v1_1 - 0x10) = *t0_13;
                v1_1 += 0x10;
                t1_1 += 1;
                *(int32_t *)(intptr_t)(v1_1 - 0x1c) = t0_13[1];
                *(int32_t *)(intptr_t)(v1_1 - 0x18) = t0_13[2];
                *(int32_t *)(intptr_t)(v1_1 - 0x14) = *(int32_t *)((uint8_t *)arg2 + v0_6 + 0xc);
                if (arg3 == t1_1)
                    break;
            }
        }

        t2 = (uint32_t *)((uint8_t *)t2 + (arg3 << 4));
        *arg1 = t2;
    }
}

void AL_sWriteInvCoeff(void *arg1, void *arg2, int32_t arg3, uint32_t **arg4)
{
    uint32_t a2_2 = (uint32_t)arg3 >> 2;
    uint32_t *t4 = *arg4;

    if (a2_2 != 0U) {
        int32_t t2_1 = 0;
        uint32_t *t3_1 = t4;

        if (arg2 != NULL) {
            while (1) {
                int32_t *t0_2 = (int32_t *)((uint8_t *)arg2 + (t2_1 << 4));

                t3_1 = (uint32_t *)((uint8_t *)t3_1 + 4);
                t2_1 += 1;
                *(t3_1 - 1) = (uint32_t)((uint32_t)*((uint8_t *)arg1 + t0_2[3]) << 24 |
                                          (uint32_t)*((uint8_t *)arg1 + t0_2[1]) << 8 |
                                          (uint32_t)*((uint8_t *)arg1 + t0_2[2]) << 16 |
                                          (uint32_t)*((uint8_t *)arg1 + *t0_2));
                if (a2_2 == (uint32_t)t2_1)
                    break;
            }
        } else {
            while (1) {
                int32_t t1_4 = t2_1 << 2;
                uint8_t *t0_3 = (uint8_t *)arg1 + t1_4;

                t3_1 = (uint32_t *)((uint8_t *)t3_1 + 4);
                t2_1 += 1;
                *(t3_1 - 1) = (uint32_t)((uint32_t)*((uint8_t *)arg1 + t1_4 + 3) << 24 |
                                          (uint32_t)t0_3[1] << 8 |
                                          (uint32_t)t0_3[2] << 16 |
                                          (uint32_t)*t0_3);
                if (a2_2 == (uint32_t)t2_1)
                    break;
            }
        }

        *arg4 = (uint32_t *)((uint8_t *)t4 + (a2_2 << 2));
    }
}

int32_t AL_AVC_WriteEncHwScalingList(void *arg1, void *arg2, uint32_t *arg3)
{
    uint32_t *var_18 = arg3;

    if ((((uintptr_t)arg3) & 1U) != 0U) {
        int32_t a0_13;
        void *a1_4 = NULL;
        uint32_t *a2 = NULL;

        a0_13 = __assert("(1 & (size_t)pBuf) == 0",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/EncHwScalingList.c",
                         0x35, "AL_AVC_WriteEncHwScalingList", &_gp);
        return AL_HEVC_WriteEncHwScalingList((void *)(intptr_t)a0_13, a1_4, a2);
    }

    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x1bc, (void *)AL_FAST_AVC_ENC_SCL_ORDER_8x8, 0x40, &var_18);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x27c, (void *)AL_FAST_AVC_ENC_SCL_ORDER_8x8, 0x40, &var_18);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x3c, (void *)AL_AVC_ENC_SCL_ORDER_4x4, 0x10, &var_18);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x7c, (void *)AL_AVC_ENC_SCL_ORDER_4x4, 0x10, &var_18);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0xbc, (void *)AL_AVC_ENC_SCL_ORDER_4x4, 0x10, &var_18);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0xfc, (void *)AL_AVC_ENC_SCL_ORDER_4x4, 0x10, &var_18);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x13c, (void *)AL_AVC_ENC_SCL_ORDER_4x4, 0x10, &var_18);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x17c, (void *)AL_AVC_ENC_SCL_ORDER_4x4, 0x10, &var_18);

    {
        uint8_t *i = (uint8_t *)arg2 + 0x400;
        uint8_t *i_2 = i;

        do {
            uint8_t *a1 = i + 0x2ee0;

            AL_sWriteFwdCoeffs(&var_18, i_2, 0x10, (void *)AL_FAST_AVC_ENC_SCL_ORDER_8x8);
            i += 0x7d0;
            AL_sWriteFwdCoeffs(&var_18, a1, 0x10, (void *)AL_FAST_AVC_ENC_SCL_ORDER_8x8);
            i_2 = i;
        } while (i != (uint8_t *)arg2 + 0x32e0);
    }

    {
        uint8_t *i_1 = (uint8_t *)arg2 + 0x700;
        int32_t result;

        do {
            uint8_t *i_3 = i_1;
            int32_t j = 0;

            while (j != 2) {
                uint8_t *a1_3;

                AL_sWriteFwdCoeffs(&var_18, i_3, 4, (void *)AL_AVC_ENC_SCL_ORDER_4x4);
                j += 1;
                AL_sWriteFwdCoeffs(&var_18, i_3 + 0x40, 4, (void *)AL_AVC_ENC_SCL_ORDER_4x4);
                a1_3 = i_3 + 0x80;
                i_3 += 0x2ee0;
                result = 0;
                AL_sWriteFwdCoeffs(&var_18, a1_3, 4, (void *)AL_AVC_ENC_SCL_ORDER_4x4);
            }
            i_1 += 0x7d0;
            if (i_1 == (uint8_t *)arg2 + 0x35e0)
                return result;
        } while (1);
    }
}

int32_t AL_HEVC_WriteEncHwScalingList(void *arg1, void *arg2, uint32_t *arg3)
{
    uint32_t *i_10 = arg3;

    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x4bc, (void *)AL_SLOW_HEVC_ENC_SCL_ORDER_8x8, 0x40, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x57c, (void *)AL_SLOW_HEVC_ENC_SCL_ORDER_8x8, 0x40, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x33c, (void *)AL_SLOW_HEVC_ENC_SCL_ORDER_8x8, 0x40, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x37c, (void *)AL_SLOW_HEVC_ENC_SCL_ORDER_8x8, 0x40, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x3bc, (void *)AL_SLOW_HEVC_ENC_SCL_ORDER_8x8, 0x40, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x3fc, (void *)AL_SLOW_HEVC_ENC_SCL_ORDER_8x8, 0x40, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x43c, (void *)AL_SLOW_HEVC_ENC_SCL_ORDER_8x8, 0x40, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x47c, (void *)AL_SLOW_HEVC_ENC_SCL_ORDER_8x8, 0x40, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x1bc, (void *)AL_FAST_HEVC_ENC_SCL_ORDER_8x8, 0x40, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x1fc, (void *)AL_FAST_HEVC_ENC_SCL_ORDER_8x8, 0x40, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x23c, (void *)AL_FAST_HEVC_ENC_SCL_ORDER_8x8, 0x40, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x27c, (void *)AL_FAST_HEVC_ENC_SCL_ORDER_8x8, 0x40, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x2bc, (void *)AL_FAST_HEVC_ENC_SCL_ORDER_8x8, 0x40, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x2fc, (void *)AL_FAST_HEVC_ENC_SCL_ORDER_8x8, 0x40, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x3c, (void *)AL_HEVC_ENC_SCL_ORDER_4x4, 0x10, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x7c, (void *)AL_HEVC_ENC_SCL_ORDER_4x4, 0x10, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0xbc, (void *)AL_HEVC_ENC_SCL_ORDER_4x4, 0x10, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0xfc, (void *)AL_HEVC_ENC_SCL_ORDER_4x4, 0x10, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x13c, (void *)AL_HEVC_ENC_SCL_ORDER_4x4, 0x10, &i_10);
    AL_sWriteInvCoeff((uint8_t *)arg1 + 0x17c, (void *)AL_HEVC_ENC_SCL_ORDER_4x4, 0x10, &i_10);

    {
        uint32_t *i_13 = i_10;

        *i_13 = (uint32_t)((uint32_t)*((uint8_t *)arg1 + 0x31) << 8 |
                           (uint32_t)*((uint8_t *)arg1 + 0x32) << 16 |
                           (uint32_t)*((uint8_t *)arg1 + 0x30));
        i_13[1] = (uint32_t)((uint32_t)*((uint8_t *)arg1 + 0x34) << 8 |
                             (uint32_t)*((uint8_t *)arg1 + 0x35) << 16 |
                             (uint32_t)*((uint8_t *)arg1 + 0x33));
        __builtin_memset(&i_13[2], 0, 0x18);
        i_13[8] = (uint32_t)((uint32_t)*((uint8_t *)arg1 + 0x39) << 8 |
                             (uint32_t)*((uint8_t *)arg1 + 0x36));
        __builtin_memset(&i_13[9], 0, 0x1c);
        i_10 = &i_13[0x10];
    }

    {
        uint8_t *s2 = (uint8_t *)arg2 + 0x2ee0;
        uint8_t *t4 = (uint8_t *)arg2;
        uint8_t *s3 = s2;

        do {
            AL_sWriteFwdCoeffs(&i_10, t4, 0x10, (void *)AL_SLOW_HEVC_ENC_SCL_ORDER_8x8);
            t4 += 0x7d0;
            AL_sWriteFwdCoeffs(&i_10, s2, 0x10, (void *)AL_SLOW_HEVC_ENC_SCL_ORDER_8x8);
            s2 = t4 + 0x2ee0;
        } while (t4 != s3);
    }

    {
        uint32_t *i_5 = i_10;
        int32_t *v1_11 = (int32_t *)((uint8_t *)arg2 + 0x7cc);
        uint32_t *i = i_5;

        do {
            int32_t a0_28 = *v1_11;

            i = &i[2];
            v1_11 = &v1_11[0x1f4];
            *(i - 2) = (uint32_t)a0_28;
            *(i - 1) = (uint32_t)v1_11[0x9c4];
        } while (i != &i_5[0xc]);
        i_5[0xc] = 0;
        i_5[0xd] = 0;
        i_5[0xe] = 0;
        i_5[0xf] = 0;
        i_10 = &i_5[0x10];
    }

    {
        uint8_t *i_1 = (uint8_t *)arg2 + 0x100;
        uint32_t *i_6;

        do {
            uint8_t *i_7 = i_1;
            int32_t j = 0;
            uint8_t *i_11 = i_7;

            do {
                uint8_t *a1_5;

                AL_sWriteFwdCoeffs(&i_10, i_11, 0x10, (void *)AL_SLOW_HEVC_ENC_SCL_ORDER_8x8);
                AL_sWriteFwdCoeffs(&i_10, i_7 + 0x100, 0x10, (void *)AL_SLOW_HEVC_ENC_SCL_ORDER_8x8);
                a1_5 = i_7 + 0x200;
                j += 1;
                i_7 += 0x2ee0;
                AL_sWriteFwdCoeffs(&i_10, a1_5, 0x10, (void *)AL_SLOW_HEVC_ENC_SCL_ORDER_8x8);
                i_11 = i_7;
            } while (j != 2);
            i_1 += 0x7d0;
            i_6 = i_10;
        } while (i_1 != (uint8_t *)arg2 + 0x2fe0);

        {
            int32_t *v1_12 = (int32_t *)((uint8_t *)arg2 + 0x7c0);
            uint32_t *i_2 = i_6;

            do {
                int32_t a0_33 = *v1_12;

                i_2 = &i_2[6];
                v1_12 = &v1_12[0x1f4];
                *(i_2 - 6) = (uint32_t)a0_33;
                *(i_2 - 5) = (uint32_t)*(v1_12 - 0x1f3);
                *(i_2 - 4) = (uint32_t)*(v1_12 - 0x1f2);
                *(i_2 - 3) = (uint32_t)v1_12[0x9c4];
                *(i_2 - 2) = (uint32_t)v1_12[0x9c5];
                *(i_2 - 1) = (uint32_t)v1_12[0x9c6];
            } while (i_2 != &i_6[0x24]);
            i_6[0x24] = 0;
            i_6[0x25] = 0;
            i_6[0x26] = 0;
            i_6[0x27] = 0;
            i_10 = &i_6[0x28];
        }
    }

    {
        uint8_t *i_3 = (uint8_t *)arg2 + 0x400;

        do {
            uint8_t *i_8 = i_3;
            int32_t j_1 = 0;
            uint8_t *i_12 = i_8;

            do {
                uint8_t *a1_7;

                AL_sWriteFwdCoeffs(&i_10, i_12, 0x10, (void *)AL_FAST_HEVC_ENC_SCL_ORDER_8x8);
                j_1 += 1;
                AL_sWriteFwdCoeffs(&i_10, i_8 + 0x100, 0x10, (void *)AL_FAST_HEVC_ENC_SCL_ORDER_8x8);
                a1_7 = i_8 + 0x200;
                i_8 += 0x2ee0;
                AL_sWriteFwdCoeffs(&i_10, a1_7, 0x10, (void *)AL_FAST_HEVC_ENC_SCL_ORDER_8x8);
                i_12 = i_8;
            } while (j_1 != 2);
            i_3 += 0x7d0;
        } while (i_3 != (uint8_t *)arg2 + 0x32e0);
    }

    {
        uint8_t *i_4 = (uint8_t *)arg2 + 0x700;
        int32_t result;

        do {
            uint8_t *i_9 = i_4;
            int32_t j_2 = 0;

            while (j_2 != 2) {
                uint8_t *a1_10;

                AL_sWriteFwdCoeffs(&i_10, i_9, 4, (void *)AL_HEVC_ENC_SCL_ORDER_4x4);
                j_2 += 1;
                AL_sWriteFwdCoeffs(&i_10, i_9 + 0x40, 4, (void *)AL_HEVC_ENC_SCL_ORDER_4x4);
                a1_10 = i_9 + 0x80;
                i_9 += 0x2ee0;
                result = 0;
                AL_sWriteFwdCoeffs(&i_10, a1_10, 4, (void *)AL_HEVC_ENC_SCL_ORDER_4x4);
            }
            i_4 += 0x7d0;
            if (i_4 == (uint8_t *)arg2 + 0x35e0)
                return result;
        } while (1);
    }
}
