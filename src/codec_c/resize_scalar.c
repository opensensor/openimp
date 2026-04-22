#include <math.h>
#include <stdint.h>
#include <string.h>

static inline uint8_t *u8_ptr(void *p)
{
    return (uint8_t *)p;
}

static inline const uint8_t *u8_cptr(const void *p)
{
    return (const uint8_t *)p;
}

static inline int16_t trunc_w_s(float value)
{
    return (int16_t)((uint32_t)(int32_t)truncf(value) & 0xffffU);
}

static inline int16_t trunc_w_d(double value)
{
    return (int16_t)((uint32_t)(int32_t)trunc(value) & 0xffffU);
}

const char *c_resize_c(char *arg1, uint32_t *arg2, char *arg3, int32_t arg4, double arg5,
                       float arg6, double arg7, double arg8, double arg9, float arg10,
                       uint32_t arg11, int32_t arg12, char arg13, int32_t arg14, char *arg15,
                       int32_t arg16, int16_t *arg17)
{
    (void)arg5;
    (void)arg6;
    (void)arg7;
    (void)arg8;

    char *arg_0 = arg1;
    int32_t arg_c = arg4;
    int32_t i_6 = (int32_t)(intptr_t)arg3;
    int32_t t5 = i_6 << 1;
    int32_t a0_2;
    int16_t *v0_7;
    int16_t *v1_4;
    int16_t *v1_5;
    int16_t *s1_1;
    int16_t *s7;
    int16_t *v1_6;
    int16_t *v0_10;
    float f3_1;
    float f2_1;
    const char *i_3 = NULL;

    if (t5 == 0) {
        __builtin_trap();
    }

    a0_2 = arg4 - (arg14 / t5);
    if ((uint8_t)arg13 != 0) {
        arg_0 = &arg_0[arg14];
        arg_c = arg16;
        i_6 = (int32_t)(intptr_t)arg15;
    }

    if (i_6 == (int32_t)arg11 && arg_c == arg12) {
        int32_t v0_80 = arg_c;
        uint8_t *a3_20 = (uint8_t *)arg2;

        if (v0_80 > 0) {
            uint8_t *t0_14 = (uint8_t *)arg_0;
            uint32_t v0_82 = (uint32_t)(i_6 - 5) >> 2;
            int32_t t1_15 = 0;

            do {
                int32_t v1_48;
                uint8_t *i = a3_20 + 1;

                if (i_6 - 4 <= 0) {
                    v1_48 = 0;
                } else {
                    uint8_t *v1_47 = t0_14;

                    do {
                        uint8_t a0_37 = *v1_47;

                        i += 4;
                        v1_47 += 4;
                        *(i - 5) = a0_37;
                        *(i - 4) = *(v1_47 - 3);
                        *(i - 3) = *(v1_47 - 2);
                        *(i - 2) = *(v1_47 - 1);
                    } while (i != a3_20 + (v0_82 << 2) + 5);

                    v1_48 = (int32_t)((v0_82 + 1) << 2);
                }

                if (v1_48 < i_6) {
                    uint8_t *i_1 = t0_14 + v1_48;
                    uint8_t *v1_49 = a3_20 + v1_48;

                    do {
                        uint8_t a0_41 = *i_1;

                        v1_49 += 1;
                        i_1 += 1;
                        *(v1_49 - 1) = a0_41;
                    } while (t0_14 + i_6 != i_1);
                }

                t1_15 += 1;
                t0_14 += (int32_t)(intptr_t)arg3;
                a3_20 += i_6;
            } while (arg_c != t1_15);

            v0_80 = arg_c;
        }

        {
            int32_t t2_12 = v0_80 >> 1;

            i_3 = (const char *)(intptr_t)i_6;
            if (t2_12 > 0) {
                int32_t t0_15 = 0;
                uint8_t *a3_22 = (uint8_t *)&arg_0[(int32_t)(intptr_t)arg3 * a0_2];
                uint32_t v0_87 = (uint32_t)(i_6 - 5) >> 2;
                uint8_t *s7_3 = (uint8_t *)arg2 + ((size_t)i_6 * (size_t)arg_c);

                do {
                    int32_t v1_54;
                    uint8_t *i_2 = s7_3 + 1;

                    if (i_6 - 4 <= 0) {
                        v1_54 = 0;
                    } else {
                        uint8_t *v1_53 = a3_22;

                        do {
                            uint8_t a0_42 = *v1_53;

                            i_2 += 4;
                            v1_53 += 4;
                            *(i_2 - 5) = a0_42;
                            *(i_2 - 4) = *(v1_53 - 3);
                            *(i_2 - 3) = *(v1_53 - 2);
                            *(i_2 - 2) = *(v1_53 - 1);
                        } while (i_2 != s7_3 + (v0_87 << 2) + 5);

                        v1_54 = (int32_t)((v0_87 + 1) << 2);
                    }

                    if (v1_54 < i_6) {
                        uint8_t *i_uv = a3_22 + v1_54;
                        uint8_t *v1_55 = s7_3 + v1_54;

                        do {
                            uint8_t a0_46 = *i_uv;

                            v1_55 += 1;
                            i_uv += 1;
                            *(v1_55 - 1) = a0_46;
                        } while (a3_22 + i_6 != i_uv);
                    }

                    t0_15 += 1;
                    a3_22 += (int32_t)(intptr_t)arg3;
                    s7_3 += i_6;
                } while (t2_12 != t0_15);
            }
        }

        return i_3;
    }

    if (i_6 == 0x500 && (int32_t)arg11 == i_6 && arg_c == 0x2d0 && arg12 == 0x3c0) {
        uint8_t *t1_14 = (uint8_t *)arg2;
        uint32_t i_4 = 0;

        do {
            int32_t row = (int32_t)truncf((float)i_4 * 0.75f);
            const uint8_t *j = u8_cptr(arg_0) + ((size_t)row * 0x500);

            memcpy(t1_14, j, 0x500);
            i_4 += 1;
            t1_14 += 0x500;
        } while (i_4 != 0x3c0);

        {
            uint8_t *s7_1 = (uint8_t *)arg2 + 0x4b000;
            uint32_t i_5 = 0;

            while (i_5 != 0x1e0) {
                int32_t row = (int32_t)truncf((float)i_5 * 0.75f);
                const uint8_t *src = u8_cptr(arg_0) + 0xe1000 + ((size_t)row * 0x500);

                memcpy(s7_1, src, 0x500);
                i_5 += 1;
                s7_1 += 0x500;
            }
        }

        return i_3;
    }

    {
        double f0 = (double)(int32_t)arg11;
        double f4_1 = (double)i_6 / f0;
        double f0_2 = (double)arg_c / (double)arg12;

        v0_7 = &arg17[arg11];
        v1_4 = &v0_7[arg11];
        v1_5 = &v1_4[arg12];
        s1_1 = &v1_5[arg12];
        s7 = &s1_1[arg11];
        v1_6 = &s7[arg11];
        a0_2 = arg12 >> 1;
        v0_10 = &v1_6[arg12];
        f3_1 = (float)f4_1;
        f2_1 = (float)f0_2;

        if ((int32_t)arg11 > 0) {
            int16_t *t2_1 = v0_7;
            int16_t *t1_1 = arg17;
            int16_t *t0_1 = s1_1;
            int16_t *a3 = s7;
            uint32_t a0_3 = 0;

            while (1) {
                float f0_3 = (float)a0_3 * f3_1;
                int32_t v1_9 = (int32_t)truncf(f0_3);
                int16_t a2 = (int16_t)(v1_9 & 0xffff);
                double f0_4 = (double)((f0_3 - (float)v1_9) * arg10) + arg9;
                int16_t t4_1;

                if (!(f4_1 <= f0_4)) {
                    int16_t v0_12 = trunc_w_d(f0_4);

                    *t2_1 = v0_12;
                    t4_1 = (int16_t)(a2 + 1);
                    *t1_1 = (int16_t)(0x10 - v0_12);
                    *t0_1 = a2;
                    if (v1_9 < i_6 - 1) {
                        a0_3 += 1;
                        *a3 = t4_1;
                        t2_1 += 1;
                        t1_1 += 1;
                        t0_1 += 1;
                        if (arg11 == a0_3) {
                            break;
                        }
                        a3 += 1;
                        continue;
                    }

                    a0_3 += 1;
                    *a3 = a2;
                    t2_1 += 1;
                    t1_1 += 1;
                    t0_1 += 1;
                    if (arg11 == a0_3) {
                        break;
                    }
                    a3 += 1;
                    continue;
                }

                f0_4 -= f4_1;

                {
                    int16_t v0_15 = trunc_w_d(f0_4);

                    t4_1 = (int16_t)(a2 + 1);
                    *t2_1 = v0_15;
                    *t1_1 = (int16_t)(0x10 - v0_15);
                    *t0_1 = a2;
                }

                if (v1_9 < i_6 - 1) {
                    a0_3 += 1;
                    *a3 = t4_1;
                    t2_1 += 1;
                    t1_1 += 1;
                    t0_1 += 1;
                    if (arg11 == a0_3) {
                        break;
                    }
                    a3 += 1;
                    continue;
                }

                a0_3 += 1;
                *a3 = a2;
                t2_1 += 1;
                t1_1 += 1;
                t0_1 += 1;
                if (arg11 == a0_3) {
                    break;
                }
                a3 += 1;
            }
        }

        if (arg12 > 0) {
            int16_t *a3_1 = v1_5;
            int16_t *t0_2 = v1_4;
            int16_t *t1_2 = v1_6;
            int16_t *t2_2 = v0_10;
            int16_t *s0_1 = a3_1;
            int16_t *s2_1 = t0_2;
            int16_t *s3_1 = t1_2;
            int16_t *s6_2 = t2_2;
            uint32_t a0_4 = 0;

            while (1) {
                float f0_11 = (float)a0_4 * f2_1;
                int32_t v1_12 = (int32_t)truncf(f0_11);
                int16_t a2_1 = (int16_t)(v1_12 & 0xffff);
                double f0_13 = (double)((f0_11 - (float)v1_12) * arg10) + arg9;

                if (!(f4_1 <= f0_13)) {
                    int16_t v0_21 = trunc_w_d(f0_13);

                    *a3_1 = v0_21;
                    *t0_2 = (int16_t)(0x10 - v0_21);
                    *t1_2 = a2_1;
                    if (v1_12 < arg_c - 1) {
                        a0_4 += 1;
                        *t2_2 = (int16_t)(a2_1 + 1);
                        a3_1 += 1;
                        t0_2 += 1;
                        t1_2 += 1;
                        if (arg12 == (int32_t)a0_4) {
                            break;
                        }
                        t2_2 += 1;
                        continue;
                    }

                    a0_4 += 1;
                    *t2_2 = a2_1;
                    a3_1 += 1;
                    t0_2 += 1;
                    t1_2 += 1;
                    if (arg12 == (int32_t)a0_4) {
                        break;
                    }
                    t2_2 += 1;
                    continue;
                }

                f0_13 -= f4_1;

                {
                    int16_t v0_18 = trunc_w_d(f0_13);

                    *a3_1 = v0_18;
                    *t0_2 = (int16_t)(0x10 - v0_18);
                    *t1_2 = a2_1;
                }

                if (v1_12 < arg_c - 1) {
                    a0_4 += 1;
                    *t2_2 = (int16_t)(a2_1 + 1);
                    a3_1 += 1;
                    t0_2 += 1;
                    t1_2 += 1;
                    if (arg12 == (int32_t)a0_4) {
                        break;
                    }
                    t2_2 += 1;
                    continue;
                }

                a0_4 += 1;
                *t2_2 = a2_1;
                a3_1 += 1;
                t0_2 += 1;
                t1_2 += 1;
                if (arg12 == (int32_t)a0_4) {
                    break;
                }
                t2_2 += 1;
            }

            {
                uint8_t *s4_1 = (uint8_t *)arg2;
                uint8_t *t7_1 = s4_1 + arg11;

                do {
                    uint32_t v0_23 = (uint16_t)(*s0_1);

                    if (v0_23 == 0) {
                        uint8_t *t4_10 = u8_ptr(arg_0) + ((size_t)(uint16_t)(*s3_1) * (size_t)(int32_t)(intptr_t)arg3);

                        if ((int32_t)arg11 > 0) {
                            int16_t *t3_10 = arg17;
                            int16_t *t1_13 = v0_7;
                            int16_t *t2_9 = s7;
                            uint8_t *a3_15 = s4_1;
                            int16_t *t0_9 = s1_1;

                            do {
                                uint32_t v0_70 = (uint16_t)(*t3_10);
                                int32_t lo_22 = (int32_t)(uint8_t)*(t4_10 + (uint16_t)(*t2_9)) * (uint16_t)(*t1_13);
                                uint32_t v1_41 = (uint8_t)*(t4_10 + (uint16_t)(*t0_9));
                                int32_t lo_23 = lo_22 + (int32_t)v1_41 * (int32_t)v0_70;

                                a3_15 += 1;
                                t0_9 += 1;
                                t3_10 += 1;
                                t2_9 += 1;
                                t1_13 += 1;
                                *(a3_15 - 1) = (uint8_t)(lo_23 >> 4);
                            } while (t7_1 != a3_15);
                        }

                        s0_1 += 1;
                    } else {
                        uint8_t *t8_3 = u8_ptr(arg_0) + ((size_t)(uint16_t)(*s3_1) * (size_t)(int32_t)(intptr_t)arg3);
                        uint8_t *t9_2 = u8_ptr(arg_0) + ((size_t)(uint16_t)(*s6_2) * (size_t)(int32_t)(intptr_t)arg3);

                        if ((int32_t)arg11 > 0) {
                            int16_t *t6_3 = arg17;
                            int16_t *t5_2 = s7;
                            int16_t *t4_3 = v0_7;
                            uint8_t *t2_3 = s4_1;
                            int16_t *t3_4 = s1_1;
                            uint32_t t0_3 = v0_23;

                            while (1) {
                                uint32_t a0_5 = (uint16_t)(*t5_2);
                                uint32_t v1_16 = (uint16_t)(*t4_3);
                                uint32_t t1_3 = (uint16_t)(*t3_4);
                                uint32_t v0_24 = (uint16_t)(*t6_3);
                                int32_t lo_2 = (int32_t)(uint8_t)*(t9_2 + a0_5) * (int32_t)v1_16;
                                int32_t lo_5 =
                                    ((int32_t)(uint8_t)*(t8_3 + a0_5) * (int32_t)v1_16) +
                                    ((int32_t)(uint8_t)*(t8_3 + t1_3) * (int32_t)v0_24) +
                                    (lo_2 * (int32_t)(uint16_t)(*s2_1)) +
                                    (((int32_t)(uint8_t)*(t9_2 + a0_5) * (int32_t)v1_16) * (int32_t)t0_3);

                                t2_3 += 1;
                                t3_4 += 1;
                                t6_3 += 1;
                                t5_2 += 1;
                                t4_3 += 1;
                                *(t2_3 - 1) = (uint8_t)(lo_5 >> 8);
                                if (t7_1 == t2_3) {
                                    break;
                                }

                                t0_3 = (uint16_t)(*s0_1);
                            }
                        }

                        s0_1 += 1;
                    }

                    s3_1 += 1;
                    s6_2 += 1;
                    s4_1 += arg11;
                    t7_1 += arg11;
                    s2_1 += 1;
                } while (s1_1 != s0_1);
            }
        }

        {
            int32_t t2_4 = (int32_t)arg11 >> 1;

            if (t2_4 > 0) {
                int16_t *t1_6 = s1_1;
                int16_t *t0_4 = s7;
                uint32_t a2_6 = 0;

                while (1) {
                    float f0_17 = (float)a2_6 * f3_1;
                    int32_t a0_10 = (int32_t)truncf(f0_17);
                    double f0_19 = (double)((f0_17 - (float)a0_10) * arg10) + arg9;
                    int16_t *t8_5 = &v0_7[a2_6 * 2];

                    if (!(f4_1 <= f0_19)) {
                        int16_t v0_31 = (int16_t)((uint32_t)trunc_w_d(f0_19) & 0xffffU);
                        int16_t t8_4 = (int16_t)(a0_10 & 0xffff);
                        int16_t v0_33;

                        *t8_5 = v0_31;
                        *(v0_7 + (a2_6 << 1) + 1) = v0_31;
                        v0_33 = (int16_t)(0x10 - *t8_5);
                        arg17[a2_6 * 2] = v0_33;
                        *(arg17 + (a2_6 << 1) + 1) = v0_33;
                        *t1_6 = t8_4;
                        if (a0_10 >= (i_6 >> 1) - 1) {
                            a2_6 += 1;
                            *t0_4 = t8_4 + 1;
                            t1_6 += 1;
                            if (a2_6 == (uint32_t)t2_4) {
                                break;
                            }
                            t0_4 += 1;
                            continue;
                        }

                        a2_6 += 1;
                        *t0_4 = t8_4;
                        t1_6 += 1;
                        if (a2_6 == (uint32_t)t2_4) {
                            break;
                        }
                        t0_4 += 1;
                        continue;
                    }

                    f0_19 -= f4_1;

                    {
                        int16_t v0_31 = (int16_t)((uint32_t)trunc_w_d(f0_19) & 0xffffU);
                        int16_t t8_4 = (int16_t)(a0_10 & 0xffff);
                        int16_t v0_33;

                        *t8_5 = v0_31;
                        *(v0_7 + (a2_6 << 1) + 1) = v0_31;
                        v0_33 = (int16_t)(0x10 - *t8_5);
                        arg17[a2_6 * 2] = v0_33;
                        *(arg17 + (a2_6 << 1) + 1) = v0_33;
                        *t1_6 = t8_4;
                        if (a0_10 >= (i_6 >> 1) - 1) {
                            a2_6 += 1;
                            *t0_4 = t8_4 + 1;
                            t1_6 += 1;
                            if (a2_6 == (uint32_t)t2_4) {
                                break;
                            }
                            t0_4 += 1;
                            continue;
                        }

                        a2_6 += 1;
                        *t0_4 = t8_4;
                        t1_6 += 1;
                        if (a2_6 == (uint32_t)t2_4) {
                            break;
                        }
                        t0_4 += 1;
                        continue;
                    }
                }
            }

            i_3 = "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/FbcMapSize.c";
            if (a0_2 > 0) {
                int16_t *s2_2 = v1_4;
                int16_t *s6_3 = v1_6;
                int16_t *a3_7 = v1_5;
                int16_t *var_48 = v0_10;
                int16_t *s0_2 = a3_7;
                int16_t *t2_5 = v0_10;
                int16_t *t1_7 = s6_3;
                int16_t *t0_5 = s2_2;
                uint32_t a2_7 = 0;

                while (1) {
                    float f0_23 = (float)a2_7 * f2_1;
                    int32_t v1_23 = (int32_t)truncf(f0_23);
                    int16_t a0_12 = (int16_t)(a0_2 + v1_23);
                    double f0_25 = (double)((f0_23 - (float)v1_23) * f3_1) + arg9;

                    if (!(f4_1 <= f0_25)) {
                        int16_t v0_41 = trunc_w_d(f0_25);

                        *a3_7 = v0_41;
                        *t0_5 = (int16_t)(0x10 - v0_41);
                        *t1_7 = a0_12;
                        if (v1_23 < (arg_c >> 1) - 1) {
                            a2_7 += 1;
                            *t2_5 = (int16_t)(a0_12 + 1);
                            a3_7 += 1;
                            t0_5 += 1;
                            t1_7 += 1;
                            if ((uint32_t)a0_2 == a2_7) {
                                break;
                            }
                            t2_5 += 1;
                            continue;
                        }

                        a2_7 += 1;
                        *t2_5 = a0_12;
                        a3_7 += 1;
                        t0_5 += 1;
                        t1_7 += 1;
                        if ((uint32_t)a0_2 == a2_7) {
                            break;
                        }
                        t2_5 += 1;
                        continue;
                    }

                    f0_25 -= f4_1;

                    {
                        int16_t v0_41 = trunc_w_d(f0_25);

                        *a3_7 = v0_41;
                        *t0_5 = (int16_t)(0x10 - v0_41);
                        *t1_7 = a0_12;
                    }

                    if (v1_23 < (arg_c >> 1) - 1) {
                        a2_7 += 1;
                        *t2_5 = (int16_t)(a0_12 + 1);
                        a3_7 += 1;
                        t0_5 += 1;
                        t1_7 += 1;
                        if ((uint32_t)a0_2 == a2_7) {
                            break;
                        }
                        t2_5 += 1;
                        continue;
                    }

                    a2_7 += 1;
                    *t2_5 = a0_12;
                    a3_7 += 1;
                    t0_5 += 1;
                    t1_7 += 1;
                    if ((uint32_t)a0_2 == a2_7) {
                        break;
                    }
                    t2_5 += 1;
                }

                {
                    uint8_t *fp_1 = (uint8_t *)arg2 + ((size_t)arg11 * (size_t)arg12);
                    int16_t *v0_58;

                    do {
                        uint32_t v0_48 = (uint16_t)(*s0_2);

                        if (v0_48 == 0) {
                            uint8_t *t3_9 = u8_ptr(arg_0) + ((size_t)(uint16_t)(*s6_3) * (size_t)(int32_t)(intptr_t)arg3);

                            if ((int32_t)arg11 > 0) {
                                int16_t *t2_8 = arg17;
                                int16_t *t1_12 = v0_7;
                                uint8_t *t0_8 = fp_1;
                                int32_t a3_14 = 0;

                                do {
                                    uint8_t *v1_39 = t3_9 + (((size_t)(uint16_t)s7[a3_14 >> 1]) << 1);
                                    uint32_t a0_22 = (uint16_t)(*t2_8);
                                    int32_t lo_18 = (int32_t)(uint8_t)*v1_39 * (int32_t)(uint16_t)(*t1_12);
                                    uint8_t *v0_66 = t3_9 + (((size_t)(uint16_t)s1_1[a3_14 >> 1]) << 1);
                                    int32_t lo_19 = lo_18 + ((int32_t)(uint8_t)*v0_66 * (int32_t)a0_22);

                                    t0_8 += 2;
                                    t1_12 += 2;
                                    t2_8 += 2;
                                    a3_14 += 2;
                                    *(t0_8 - 2) = (uint8_t)(lo_19 >> 4);

                                    {
                                        int32_t lo_21 =
                                            ((int32_t)(uint8_t)v1_39[1] * (int32_t)(uint16_t)*(t1_12 - 2)) +
                                            ((int32_t)(uint8_t)v0_66[1] * (int32_t)(uint16_t)*(t2_8 - 2));

                                        *(t0_8 - 1) = (uint8_t)(lo_21 >> 4);
                                    }
                                } while (a3_14 < (int32_t)arg11);
                            }

                            v0_58 = var_48;
                        } else {
                            uint8_t *t4_6 = u8_ptr(arg_0) + ((size_t)(uint16_t)(*s6_3) * (size_t)(int32_t)(intptr_t)arg3);
                            uint8_t *t5_6 = u8_ptr(arg_0) + ((size_t)(uint16_t)(*var_48) * (size_t)(int32_t)(intptr_t)arg3);

                            if ((int32_t)arg11 > 0) {
                                int16_t *t9_6 = arg17;
                                int16_t *t8_6 = v0_7;
                                uint8_t *t7_3 = fp_1;
                                int32_t t6_7 = 0;
                                uint32_t t2_6 = v0_48;

                                while (1) {
                                    uint32_t a2_10 = (uint16_t)(*t8_6);
                                    uint32_t v1_32 = ((uint16_t)s7[t6_7 >> 1]) << 1;
                                    uint32_t a0_17 = ((uint16_t)s1_1[t6_7 >> 1]) << 1;
                                    uint32_t v0_50 = (uint16_t)(*t9_6);
                                    int32_t lo_9 = (int32_t)(uint8_t)*(t5_6 + v1_32) * (int32_t)a2_10;
                                    int32_t lo_12 =
                                        ((int32_t)(uint8_t)*(t4_6 + v1_32) * (int32_t)a2_10) +
                                        ((int32_t)(uint8_t)*(t4_6 + a0_17) * (int32_t)v0_50) +
                                        (lo_9 * (int32_t)(uint16_t)(*s2_2)) +
                                        (((int32_t)(uint8_t)*(t5_6 + v1_32) * (int32_t)a2_10) * (int32_t)t2_6);

                                    t7_3 += 2;
                                    t8_6 += 2;
                                    t9_6 += 2;
                                    t6_7 += 2;
                                    *(t7_3 - 2) = (uint8_t)(lo_12 >> 8);

                                    {
                                        uint32_t t2_7 = (uint16_t)*(t8_6 - 1);
                                        int32_t lo_14 = (int32_t)(uint8_t)*(t5_6 + v1_32 + 1) * (int32_t)t2_7;
                                        int32_t lo_17 =
                                            ((int32_t)(uint8_t)*(t4_6 + v1_32 + 1) * (int32_t)t2_7) +
                                            ((int32_t)(uint8_t)*(t4_6 + a0_17 + 1) * (int32_t)(uint16_t)*(t9_6 - 1)) +
                                            (lo_14 * (int32_t)(uint16_t)(*s2_2)) +
                                            (((int32_t)(uint8_t)*(t5_6 + v1_32 + 1) * (int32_t)t2_7) * (int32_t)(uint16_t)(*s0_2));

                                        *(t7_3 - 1) = (uint8_t)(lo_17 >> 8);
                                    }

                                    if (t6_7 >= (int32_t)arg11) {
                                        break;
                                    }

                                    t2_6 = (uint16_t)(*s0_2);
                                }
                            }

                            v0_58 = var_48;
                        }

                        s0_2 += 1;
                        s6_3 += 1;
                        var_48 = v0_58 + 1;
                        i_3 = (const char *)&v1_5[a0_2];
                        fp_1 += arg11;
                        s2_2 += 1;
                    } while (s0_2 != (int16_t *)i_3);
                }
            }
        }
    }

    return i_3;
}
