#include <stdint.h>

int32_t nv12_scaler_16(
    void *arg1,
    void *arg2,
    void *arg3,
    void *arg4,
    int16_t arg5,
    int16_t arg6,
    int16_t arg7,
    char arg8,
    char arg9,
    char arg10)
{
    uint32_t a1 = (uint16_t)arg9;
    uint32_t v0 = (uint16_t)arg7;
    uint32_t s0 = (uint16_t)arg6;
    uint32_t fp = (uint16_t)arg5;
    uint32_t t8 = (uint8_t)arg8;
    char *src_y = (char *)arg1;
    char *dst_y = (char *)arg2;
    char *src_uv = (char *)arg3;
    char *dst_uv = (char *)arg4;

    if (v0 != 0) {
        int32_t s6_1 = 0;
        int32_t var_40_1 = 0;
        int32_t s7_1 = 0;
        char *arg_8 = src_uv;

        while (1) {
            int32_t t7_3 = ((s6_1 >> 4) + 1) * (int32_t)fp;

            if (s0 != 0) {
                int32_t t2_1 = 0;
                int32_t t1_1 = 0;
                char *t3_1 = dst_y + var_40_1;

                while (1) {
                    if ((((int32_t)(v0 - 0xf)) & -16) >= s7_1) {
                        char *a2_1 = src_y + t7_3 - (int32_t)fp + (t2_1 >> 4) + 1;
                        char *a1_3 = src_y + t7_3 + (t2_1 >> 4) + 1;
                        char *t4_2 = src_y + t7_3 + (int32_t)fp + (t2_1 >> 4) + 1;
                        char *v0_8 = src_y + (t2_1 >> 4) + 1 + ((int32_t)fp << 1) + t7_3;

                        if ((((int32_t)(s0 - 0xf)) & -16) >= t1_1) {
                            uint32_t v1_8 = (uint8_t)*a2_1 + (uint8_t)*a1_3 + (uint8_t)*t4_2 +
                                (uint8_t)*v0_8 + (uint8_t)*(a2_1 - 1) + (uint8_t)a2_1[1] +
                                (uint8_t)a2_1[2] + (uint8_t)*(a1_3 - 1) + (uint8_t)a1_3[1] +
                                (uint8_t)a1_3[2] + (uint8_t)*(t4_2 - 1) + (uint8_t)t4_2[1] +
                                (uint8_t)t4_2[2];
                            t1_1 += 1;
                            *t3_1 = (char)(((v1_8 + (uint8_t)*(v0_8 - 1) + (uint8_t)v0_8[1] +
                                (uint8_t)v0_8[2]) >> 4) & 0xff);
                            t2_1 += (int32_t)t8;
                            t3_1 = &t3_1[1];
                            if ((uint32_t)t1_1 == s0) {
                                break;
                            }
                            continue;
                        }
                    }

                    {
                        char v0_14 = *(src_y + t7_3 + (t2_1 >> 4) + 1);
                        t1_1 += 1;
                        t2_1 += (int32_t)t8;
                        *t3_1 = v0_14;
                        t3_1 = &t3_1[1];
                        if ((uint32_t)t1_1 == s0) {
                            break;
                        }
                    }
                }

                var_40_1 += (int32_t)s0;
            }

            s7_1 += 1;
            s6_1 += (int32_t)a1;
            src_uv = arg_8;
            if ((uint32_t)s7_1 == v0) {
                break;
            }
        }
    }

    {
        uint32_t t3_2 = v0 >> 1;

        if (t3_2 != 0) {
            uint32_t t4_3 = s0 >> 1;
            uint32_t t6_2 = t4_3 << 1;
            int32_t t2_2 = 0;
            int32_t t0_6 = 0;
            int32_t t1_2 = 0;

            do {
                if (t4_3 != 0) {
                    char *i = dst_uv + t0_6;
                    int32_t a0 = 0;

                    do {
                        char *v0_24 = src_uv + (((a0 >> 4) + 1) << 1) + (((t2_2 >> 4) + 1) * (int32_t)fp);
                        i += 2;
                        a0 += (int32_t)t8;
                        *(i - 2) = *v0_24;
                        *(i - 1) = v0_24[1];
                    } while (i != dst_uv + t6_2 + t0_6);

                    t0_6 += (int32_t)t6_2;
                }

                t1_2 += 1;
                t2_2 += (int32_t)a1;
            } while ((uint32_t)t1_2 != t3_2);
        }

        if ((uint8_t)arg10 != 1) {
            return 0;
        }

        {
            int32_t i_1 = 8;
            int32_t t0_8 = ((int32_t)v0 - 1) * (int32_t)s0;
            char *a2_6 = dst_y + (s0 * v0);

            do {
                char *j = dst_y + t0_8;
                char *v1_13 = a2_6;

                if (s0 != 0) {
                    do {
                        char a0_2 = *j;
                        v1_13 += 1;
                        j = &j[1];
                        *(v1_13 - 1) = a0_2;
                    } while (dst_y + s0 + t0_8 != j);
                }

                i_1 -= 1;
                a2_6 += (int32_t)s0;
            } while (i_1 != 0);
        }

        {
            int32_t a3_10 = ((int32_t)t3_2 - 1) * (int32_t)s0;
            int32_t i_2 = 4;
            char *t3_3 = dst_uv + (t3_2 * s0);

            do {
                char *j_1 = dst_uv + a3_10;
                char *v1_15 = t3_3;

                if (s0 != 0) {
                    do {
                        char a0_3 = *j_1;
                        v1_15 += 1;
                        j_1 = &j_1[1];
                        *(v1_15 - 1) = a0_3;
                    } while (dst_uv + s0 + a3_10 != j_1);
                }

                i_2 -= 1;
                t3_3 += (int32_t)s0;
            } while (i_2 != 0);
        }
    }

    return 0;
}

int32_t nv12_scaler_8(
    void *arg1,
    void *arg2,
    void *arg3,
    void *arg4,
    int16_t arg5,
    int16_t arg6,
    int16_t arg7,
    char arg8,
    char arg9,
    char arg10)
{
    uint32_t a1 = (uint16_t)arg9;
    uint32_t v0 = (uint16_t)arg7;
    uint32_t t8 = (uint16_t)arg6;
    uint32_t s6 = (uint16_t)arg5;
    uint32_t t7 = (uint8_t)arg8;
    char *src_y = (char *)arg1;
    char *dst_y = (char *)arg2;
    char *src_uv = (char *)arg3;
    char *dst_uv = (char *)arg4;

    if (v0 != 0) {
        int32_t s7_1 = 0;
        int32_t fp_1 = 0;
        int32_t s5_1 = 0;

        while (1) {
            int32_t t5_3 = ((s7_1 >> 4) + 1) * (int32_t)s6;

            if (t8 != 0) {
                int32_t t2_1 = 0;
                int32_t t1_1 = 0;
                char *t3_1 = dst_y + fp_1;

                while (1) {
                    if ((((int32_t)(v0 - 0xf)) & -16) >= s5_1) {
                        char *a3_1 = src_y + t5_3 - (int32_t)s6 + (t2_1 >> 4) + 1;
                        char *a2_1 = src_y + t5_3 + (int32_t)s6 + (t2_1 >> 4) + 1;
                        char *v0_6 = src_y + t5_3 + (t2_1 >> 4) + 1;

                        if ((((int32_t)(t8 - 0xf)) & -16) >= t1_1) {
                            uint32_t v0_8 = (uint8_t)*a3_1 + (uint8_t)*a2_1 + (uint8_t)*(a3_1 - 1) +
                                (uint8_t)a3_1[1] + (uint8_t)*(v0_6 - 1) + (uint8_t)*(v0_6 + 1) +
                                (uint8_t)*(a2_1 - 1);
                            t1_1 += 1;
                            *t3_1 = (char)(((v0_8 + (uint8_t)a2_1[1]) >> 3) & 0xff);
                            t2_1 += (int32_t)t7;
                            t3_1 = &t3_1[1];
                            if ((uint32_t)t1_1 == t8) {
                                break;
                            }
                            continue;
                        }
                    }

                    {
                        char v0_13 = *(src_y + t5_3 + (t2_1 >> 4) + 1);
                        t1_1 += 1;
                        t2_1 += (int32_t)t7;
                        *t3_1 = v0_13;
                        t3_1 = &t3_1[1];
                        if ((uint32_t)t1_1 == t8) {
                            break;
                        }
                    }
                }

                fp_1 += (int32_t)t8;
            }

            s5_1 += 1;
            s7_1 += (int32_t)a1;
            if ((uint32_t)s5_1 == v0) {
                break;
            }
        }
    }

    {
        uint32_t t4_3 = v0 >> 1;

        if (t4_3 != 0) {
            uint32_t t5_4 = t8 >> 1;
            uint32_t t9_2 = t5_4 << 1;
            int32_t t3_2 = 0;
            int32_t t1_2 = 0;
            int32_t t2_2 = 0;

            do {
                if (t5_4 != 0) {
                    char *i = dst_uv + t1_2;
                    int32_t a0 = 0;

                    do {
                        char *v0_19 = src_uv + (((a0 >> 4) + 1) << 1) + (((t3_2 >> 4) + 1) * (int32_t)s6);
                        i += 2;
                        a0 += (int32_t)t7;
                        *(i - 2) = *v0_19;
                        *(i - 1) = v0_19[1];
                    } while (i != dst_uv + t1_2 + t9_2);

                    t1_2 += (int32_t)t9_2;
                }

                t2_2 += 1;
                t3_2 += (int32_t)a1;
            } while ((uint32_t)t2_2 != t4_3);
        }

        if ((uint8_t)arg10 != 1) {
            return 0;
        }

        {
            int32_t i_1 = 8;
            int32_t t1_4 = ((int32_t)v0 - 1) * (int32_t)t8;
            char *a2_3 = dst_y + (t8 * v0);

            do {
                char *j = dst_y + t1_4;
                char *v1_8 = a2_3;

                if (t8 != 0) {
                    do {
                        char a0_2 = *j;
                        v1_8 += 1;
                        j = &j[1];
                        *(v1_8 - 1) = a0_2;
                    } while (dst_y + t8 + t1_4 != j);
                }

                i_1 -= 1;
                a2_3 += (int32_t)t8;
            } while (i_1 != 0);
        }

        {
            int32_t t0_7 = ((int32_t)t4_3 - 1) * (int32_t)t8;
            int32_t i_2 = 4;
            char *t4_4 = dst_uv + (t4_3 * t8);

            do {
                char *j_1 = dst_uv + t0_7;
                char *v1_9 = t4_4;

                if (t8 != 0) {
                    do {
                        char a0_3 = *j_1;
                        v1_9 += 1;
                        j_1 = &j_1[1];
                        *(v1_9 - 1) = a0_3;
                    } while (dst_uv + t8 + t0_7 != j_1);
                }

                i_2 -= 1;
                t4_4 += (int32_t)t8;
            } while (i_2 != 0);
        }
    }

    return 0;
}

static inline int32_t rd32(const void *base, int32_t off)
{
    return *(const int32_t *)((const uint8_t *)base + off);
}

static inline void wr32(void *base, int32_t off, int32_t value)
{
    *(int32_t *)((uint8_t *)base + off) = value;
}

static inline int16_t rd16(const void *base, int32_t off)
{
    return *(const int16_t *)((const uint8_t *)base + off);
}

static inline void wr16(void *base, int32_t off, int16_t value)
{
    *(int16_t *)((uint8_t *)base + off) = value;
}

static inline uint8_t rd8(const void *base, int32_t off)
{
    return *(const uint8_t *)((const uint8_t *)base + off);
}

static inline void wr8(void *base, int32_t off, uint8_t value)
{
    *(uint8_t *)((uint8_t *)base + off) = value;
}

static inline void *rdptr(const void *base, int32_t off)
{
    return *(void * const *)((const uint8_t *)base + off);
}

static inline void wrptr(void *base, int32_t off, void *value)
{
    *(void **)((uint8_t *)base + off) = value;
}

static inline int32_t trunc_s32(double x)
{
    return (int32_t)x;
}

static int32_t AL_Unimplemented_SIMD128(void)
{
    static int warned;
    warned = 1;
    (void)warned;
    return 0;
}

static int32_t c_resize_simd(void) { return AL_Unimplemented_SIMD128(); }
static int32_t c_resize_simd_nv12_up(void) { return AL_Unimplemented_SIMD128(); }
static int32_t c_resize_simd_nv12_down(void) { return AL_Unimplemented_SIMD128(); }

static int32_t sub_30fbc(int32_t arg1, int32_t arg2, void *arg3, int16_t *arg4, void *arg5, int16_t arg6);
static int32_t sub_30e38(
    int32_t arg1, int16_t arg2, int16_t *arg3, int16_t *arg4, int32_t arg5,
    int16_t *arg6, int16_t *arg7, int16_t arg8, int32_t arg9, int32_t arg10,
    int16_t *arg11, void *arg12, double arg13, double arg14, double arg15,
    double arg16, double arg17, double arg18);
static int32_t sub_30e7c(
    int32_t arg1, int16_t arg2, int16_t *arg3, int16_t *arg4, int16_t *arg5,
    int16_t *arg6, int16_t arg7, int32_t arg8, int32_t arg9, int32_t arg10,
    int16_t *arg11, void *arg12, double arg13, double arg14, double arg15,
    double arg16, double arg17, double arg18);
static int32_t sub_310d4(
    int16_t *arg1, int16_t *arg2, int32_t arg3, uint8_t *arg4, int32_t arg5,
    int16_t *arg6, void *arg7);
static int32_t sub_31180(void *arg1);
static int32_t sub_3123c(
    int32_t arg1, int32_t arg2, int16_t *arg3, int16_t *arg4, int32_t arg5,
    int16_t *arg6, int16_t *arg7, int32_t arg8, int32_t arg9, int32_t arg10,
    int16_t arg11, int32_t arg12, int16_t *arg13, uint8_t *arg14, void *arg15,
    int16_t *arg16, void *arg17, double arg18, double arg19, double arg20,
    double arg21, double arg22, double arg23, double arg24);
static int32_t sub_318a8(
    int16_t *arg1, int16_t *arg2, uint8_t *arg3, int16_t *arg4, void *arg5);
static int32_t sub_319a0(void *arg1);
static int32_t sub_31b28(
    uint8_t *arg1, int16_t *arg2, uint8_t *arg3, uint8_t *arg4, int32_t arg5,
    int16_t *arg6, int16_t *arg7, int16_t *arg8, int16_t *arg9, int16_t *arg10,
    int32_t arg11, void *arg12, int32_t arg13);
static int32_t sub_32298(
    int32_t arg1, int32_t arg2, int32_t arg3, int16_t *arg4, int32_t arg5,
    int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10,
    void *arg11, int16_t *arg12, int32_t arg13);
static int32_t sub_323d0(
    uint8_t *arg1, int16_t *arg2, int16_t *arg3, int16_t *arg4, int16_t *arg5,
    uint8_t *arg6, int16_t *arg7, int32_t arg8, int16_t *arg9, void *arg10);
static int32_t sub_32458(void *arg1);
static int32_t sub_3296c(int32_t arg1, int16_t *arg2, char *arg3, int16_t *arg4, void *arg5);

static int32_t sub_30cbc(
    int16_t *arg1, int16_t *arg2, int16_t *arg3, void *arg4, double arg5,
    double arg6, double arg7, double arg8, double arg9, double arg10)
{
    int32_t width = rd32(arg4, 0x308);
    int16_t *w0 = (int16_t *)rdptr(arg4, 0x248);
    int16_t *w1 = (int16_t *)rdptr(arg4, 0x320);
    int16_t *dst0 = arg1;
    int16_t *dst1 = arg2;
    uint32_t i = 0;
    int32_t count = rd32(arg4, 0x310);

    while (i != (uint32_t)count) {
        uint32_t x = (uint32_t)trunc_s32((float)i);
        double pos = ((((double)((float)arg5)) * arg10) - (double)((float)arg6)) * arg9 + arg8;
        int16_t frac;
        int16_t base;
        int16_t next;
        int16_t *hits;

        if (arg7 <= pos) {
            pos -= arg7;
            frac = (int16_t)trunc_s32(pos);
            base = (int16_t)x;
            hits = &arg3[x];
            *w0 = frac;
            *w1 = (int16_t)(0x10 - frac);
            *hits = (int16_t)(*hits + 1);
            next = (int16_t)(base + ((int32_t)x < (width - 1) ? 1 : 0));
        } else {
            frac = (int16_t)trunc_s32(pos);
            base = (int16_t)x;
            hits = &arg3[x];
            *w0 = frac;
            *w1 = (int16_t)(0x10 - frac);
            *hits = (int16_t)(*hits + 1);
            next = (int16_t)(base + ((int32_t)x < (width - 1) ? 1 : 0));
        }

        *dst0++ = base;
        *dst1++ = next;
        ++w0;
        ++w1;
        ++i;
    }

    if (rd32(arg4, 0x314) > 0) {
        return sub_30e7c(
            0, dst1[-1], (int16_t *)rdptr(arg4, 0x29c), (int16_t *)rdptr(arg4, 0x298),
            (int16_t *)rdptr(arg4, 0x290), (int16_t *)rdptr(arg4, 0x294), 0x10,
            rd32(arg4, 0x30c) - 1, (int32_t)0x80000000u, rd32(arg4, 0x314), arg3,
            arg4, arg5, arg6, arg7, arg8, arg9, arg10);
    }

    wr32(arg4, 0x238, 0);
    wrptr(arg4, 0x270, (uint8_t *)arg4 + 0x18);
    wr32(arg4, 0x23c, 0);
    wr32(arg4, 0x240, 0);
    wr32(arg4, 0x244, 0);
    *(int16_t *)((uint8_t *)arg4 + 0x18) = 0;
    if ((uint16_t)*arg3 != 0) {
        wr32(arg4, 0x238, 0);
    }

    {
        int32_t v1 = rd32(arg4, 0x310);
        int32_t chunks = (v1 - 1) >> 4;
        wr32(arg4, 0x250, chunks);
        wr32(arg4, 0x260, chunks << 4);
        wr32(arg4, 0x280, v1 - (chunks << 4));
    }

    if (rd32(arg4, 0x314) > 0) {
        return sub_30fbc(rd32(arg4, 0x250), 0, (uint8_t *)arg4 + 0x238, arg3, arg4, dst1[-1]);
    }

    return 0;
}

static int32_t sub_30e38(
    int32_t arg1, int16_t arg2, int16_t *arg3, int16_t *arg4, int32_t arg5,
    int16_t *arg6, int16_t *arg7, int16_t arg8, int32_t arg9, int32_t arg10,
    int16_t *arg11, void *arg12, double arg13, double arg14, double arg15,
    double arg16, double arg17, double arg18)
{
    int16_t frac = (int16_t)trunc_s32(arg13);
    *arg7 = frac;
    *arg6 = (int16_t)(arg8 - frac);
    *arg4 = arg2;
    *arg3 = (arg5 >= arg9) ? arg2 : (int16_t)(arg2 + 1);
    if (arg10 != arg1 + 1) {
        return sub_30e7c(arg1 + 1, arg2, arg3 + 1, arg4 + 1, arg6 + 1, arg7 + 1, arg8, arg9,
            (int32_t)0x80000000u, arg10, arg11, arg12, arg13, arg14, arg15, arg16, arg17, arg18);
    }

    wr32(arg12, 0x238, 0);
    wrptr(arg12, 0x270, (uint8_t *)arg12 + 0x18);
    wr32(arg12, 0x23c, 0);
    wr32(arg12, 0x240, 0);
    wr32(arg12, 0x244, 0);
    *(int16_t *)((uint8_t *)arg12 + 0x18) = 0;
    if ((uint16_t)*arg11 != 0) {
        wr32(arg12, 0x238, 0);
    }

    {
        int32_t v1 = rd32(arg12, 0x310);
        int32_t chunks = (v1 - 1) >> 4;
        wr32(arg12, 0x250, chunks);
        wr32(arg12, 0x260, chunks << 4);
        wr32(arg12, 0x280, v1 - (chunks << 4));
    }

    if (rd32(arg12, 0x314) > 0) {
        return sub_30fbc(rd32(arg12, 0x250), 0, (uint8_t *)arg12 + 0x238, arg11, arg12, arg2);
    }

    return 0;
}

static int32_t sub_30e7c(
    int32_t arg1, int16_t arg2, int16_t *arg3, int16_t *arg4, int16_t *arg5,
    int16_t *arg6, int16_t arg7, int32_t arg8, int32_t arg9, int32_t arg10,
    int16_t *arg11, void *arg12, double arg13, double arg14, double arg15,
    double arg16, double arg17, double arg18)
{
    while (1) {
        uint32_t x = (uint32_t)trunc_s32((float)arg1);
        int16_t base = (int16_t)x;
        double pos = ((((double)((float)arg13)) * arg18) - (double)((float)arg14)) * arg17 + arg16;

        if (!(arg15 <= pos)) {
            return sub_30e38(arg1, base, arg3, arg4, x, arg5, arg6, arg7, arg8, arg10, arg11, arg12,
                pos, arg14, arg15, arg16, arg17, arg18);
        }

        pos -= arg15;
        {
            int16_t frac = (int16_t)(trunc_s32(pos) | arg9);
            *arg6 = frac;
            *arg5 = (int16_t)(arg7 - frac);
            *arg4 = base;
            *arg3 = (int32_t)x < arg8 ? (int16_t)(base + 1) : base;
        }

        ++arg1;
        ++arg6;
        ++arg5;
        ++arg4;
        ++arg3;
        if (arg10 == arg1) {
            break;
        }
    }

    wr32(arg12, 0x238, 0);
    wrptr(arg12, 0x270, (uint8_t *)arg12 + 0x18);
    wr32(arg12, 0x23c, 0);
    wr32(arg12, 0x240, 0);
    wr32(arg12, 0x244, 0);
    *(int16_t *)((uint8_t *)arg12 + 0x18) = 0;
    if ((uint16_t)*arg11 != 0) {
        wr32(arg12, 0x238, 0);
    }

    {
        int32_t v1 = rd32(arg12, 0x310);
        int32_t chunks = (v1 - 1) >> 4;
        wr32(arg12, 0x250, chunks);
        wr32(arg12, 0x260, chunks << 4);
        wr32(arg12, 0x280, v1 - (chunks << 4));
    }

    if (rd32(arg12, 0x314) > 0) {
        return sub_30fbc(rd32(arg12, 0x250), 0, (uint8_t *)arg12 + 0x238, arg11, arg12, arg2);
    }

    return 0;
}

static int32_t sub_30fbc(int32_t arg1, int32_t arg2, void *arg3, int16_t *arg4, void *arg5, int16_t arg6)
{
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    (void)arg6;
    return 0;
}

static int32_t sub_310bc(
    int16_t *arg1, int16_t *arg2, uint8_t *arg3, uint8_t *arg4, int16_t *arg5,
    int16_t *arg6, void *arg7)
{
    return sub_310d4(arg1 + 0x10, arg2 + 0x10, 2, arg4 + 0x10, 0x20, arg6 + 1, arg7);
}

static int32_t sub_310d4(
    int16_t *arg1, int16_t *arg2, int32_t arg3, uint8_t *arg4, int32_t arg5,
    int16_t *arg6, void *arg7)
{
    uint8_t *src = (uint8_t *)(intptr_t)((uint16_t)(*arg6) * arg5 + arg3);
    int32_t remain = rd32(arg7, 0x280);

    if (remain > 0) {
        int32_t off = rd32(arg7, 0x260) << 1;
        int16_t *idx0 = arg1 + (off >> 1);
        int16_t *w0 = (int16_t *)((uint8_t *)rdptr(arg7, 0x320) + off);
        int16_t *idx1 = arg2 + (off >> 1);
        int16_t *w1 = (int16_t *)((uint8_t *)rdptr(arg7, 0x248) + off);
        uint8_t *out = arg4;
        uint8_t *out_end = out + remain;

        while (out != out_end) {
            uint32_t a = (uint16_t)(*w0);
            uint32_t lo = (uint8_t)src[(uint16_t)(*idx1)] * (uint16_t)(*w1);
            uint32_t hi = (uint8_t)src[(uint16_t)(*idx0)] * a;
            *out++ = (uint8_t)((int32_t)(lo + hi) >> 4);
            ++idx0;
            ++w0;
            ++idx1;
            ++w1;
        }
    }

    if (arg6 + 1 == (int16_t *)rdptr(arg7, 0x2a0)) {
        return sub_31180(arg7);
    }

    return 0;
}

static int32_t sub_31180(void *arg1)
{
    (void)rd32(arg1, 0x2c8);
    return 0;
}

static int32_t sub_311f8(
    int32_t arg1, int16_t *arg2, int16_t *arg3, uint8_t *arg4, void *arg5,
    int16_t *arg6, void *arg7, double arg8, double arg9, double arg10,
    double arg11, double arg12, double arg13, double arg14)
{
    int32_t width = rd32(arg7, 0x308);
    int16_t *dst0 = arg2;
    int16_t *dst1 = arg3;
    uint32_t i = 0;
    int16_t *wlo = (int16_t *)rdptr(arg7, 0x248);
    int16_t *whi = (int16_t *)rdptr(arg7, 0x320);

    while (i != (uint32_t)arg1) {
        uint32_t x = (uint32_t)trunc_s32((float)i);
        double pos = ((((double)((float)arg8)) * arg14) - (double)((float)arg9)) * arg12 + arg11;
        int16_t frac = (int16_t)trunc_s32((arg10 <= pos) ? (pos - arg10) : pos);
        int32_t off = (int32_t)(i << 2);
        int16_t *cur = (int16_t *)((uint8_t *)wlo + off);
        int16_t *cur_hi = (int16_t *)((uint8_t *)whi + off);
        int16_t base = (int16_t)x;
        int16_t next = (int32_t)x < ((width >> 1) - 1) ? (int16_t)(base + 1) : base;

        cur[0] = frac;
        cur[1] = frac;
        cur_hi[0] = (int16_t)(0x10 - frac);
        cur_hi[1] = (int16_t)(0x10 - frac);
        arg6[x] = (int16_t)(arg6[x] + 1);
        *dst0++ = base;
        *dst1++ = next;
        ++i;
    }

    return 0;
}

static int32_t sub_3123c(
    int32_t arg1, int32_t arg2, int16_t *arg3, int16_t *arg4, int32_t arg5,
    int16_t *arg6, int16_t *arg7, int32_t arg8, int32_t arg9, int32_t arg10,
    int16_t arg11, int32_t arg12, int16_t *arg13, uint8_t *arg14, void *arg15,
    int16_t *arg16, void *arg17, double arg18, double arg19, double arg20,
    double arg21, double arg22, double arg23, double arg24)
{
    (void)arg15;
    while (arg2 != arg9) {
        int16_t frac = (int16_t)(trunc_s32(arg18) | arg12);
        int16_t v = (int16_t)arg1;
        int16_t *hits = &arg16[arg1];
        *arg13 = frac;
        *(int16_t *)((uint8_t *)arg3 + arg5) = frac;
        {
            int16_t inv = (int16_t)(arg11 - *arg13);
            *(int16_t *)((uint8_t *)arg7 + arg8) = inv;
            *(int16_t *)((uint8_t *)arg7 + arg5) = inv;
        }
        *hits = (int16_t)(*hits + 1);
        *arg6 = v;
        *arg4 = (arg1 >= arg10) ? v : (int16_t)(v + 1);

        ++arg2;
        ++arg6;
        ++arg4;
        arg5 = (arg2 << 2) + 2;
        arg8 = arg5 - 2;
        arg1 = trunc_s32((float)arg2);
        arg18 = ((((double)((float)arg18)) * arg24) - (double)((float)arg19)) * arg22 + arg21;
        arg13 = (int16_t *)((uint8_t *)arg3 + arg8);
        if (arg20 <= arg18) {
            arg18 -= arg20;
        }
    }

    return 0;
}

static int32_t sub_31890(
    int16_t *arg1, int16_t *arg2, uint8_t *arg3, int16_t *arg4, void *arg5,
    int16_t *arg6)
{
    return sub_318a8(arg1 + 0x10, arg2 + 0x10, arg3 + 2, arg4 + 1, arg5);
}

static int32_t sub_318a8(
    int16_t *arg1, int16_t *arg2, uint8_t *arg3, int16_t *arg4, void *arg5)
{
    int32_t t2 = rd32(arg5, 0x2b0);
    uint8_t *base = arg3 + ((uint16_t)*arg4) * rd32(arg5, 0x318) + rd32(arg5, 0x300);

    if (rd32(arg5, 0x2a0) > 0) {
        int16_t *w0 = (int16_t *)((uint8_t *)rdptr(arg5, 0x320) + (t2 << 1));
        int16_t *w1 = (int16_t *)((uint8_t *)rdptr(arg5, 0x248) + (t2 << 1));
        int32_t end = (int32_t)(intptr_t)rdptr(arg5, 0x298) + t2;

        while (t2 != end) {
            int32_t even = (t2 >> 1) << 1;
            uint8_t *p1 = base + (((uint16_t)*(arg2 + even)) << 1);
            uint8_t *p0 = base + (((uint16_t)*(arg1 + even)) << 1);
            uint32_t a = (uint16_t)(*w0);
            uint32_t b = (uint16_t)(*w1);
            *arg3++ = (uint8_t)(((uint8_t)*p1 * b + (uint8_t)*p0 * a) >> 4);
            *arg3++ = (uint8_t)((((uint8_t)p1[1] * b) + ((uint8_t)p0[1] * a)) >> 4);
            w1 += 2;
            w0 += 2;
            t2 += 2;
        }
    }

    wr32(arg5, 0x250, rd32(arg5, 0x250) + rd32(arg5, 0x310));
    wr32(arg5, 0x260, rd32(arg5, 0x260) + 2);
    if (arg4 + 1 == (int16_t *)rdptr(arg5, 0x290)) {
        return sub_319a0(arg5);
    }

    return 0;
}

static int32_t sub_319a0(void *arg1)
{
    (void)rd32(arg1, 0x2f0);
    (void)rd32(arg1, 0x2ec);
    (void)rd32(arg1, 0x2e8);
    (void)rd32(arg1, 0x2e4);
    (void)rd32(arg1, 0x2e0);
    (void)rd32(arg1, 0x2dc);
    (void)rd32(arg1, 0x2d8);
    (void)rd32(arg1, 0x2d4);
    (void)rd32(arg1, 0x2d0);
    return 0;
}

static int32_t sub_31b28(
    uint8_t *arg1, int16_t *arg2, uint8_t *arg3, uint8_t *arg4, int32_t arg5,
    int16_t *arg6, int16_t *arg7, int16_t *arg8, int16_t *arg9, int16_t *arg10,
    int32_t arg11, void *arg12, int32_t arg13)
{
    int32_t end = (int32_t)(intptr_t)rdptr(arg12, 0x298) + arg11;
    uint8_t *out = arg4 + 1;

    while (arg11 != end) {
        int32_t even = (arg11 >> 1) << 1;
        uint32_t w0 = (uint16_t)arg7[even];
        uint32_t w1 = (uint16_t)arg6[even];
        uint32_t s0 = ((uint16_t)arg9[even]) << 1;
        uint32_t s1 = ((uint16_t)arg8[even]) << 1;
        *out++ = (uint8_t)((arg3[s0] * w0 + arg3[s1] * w1 + arg1[s0] * (uint16_t)(*arg8) + arg13) >> 8);
        *out++ = (uint8_t)((arg3[s0 + 1] * w0 + arg3[s1 + 1] * w1 + arg1[s0 + 1] * (uint16_t)(*arg8) + arg13) >> 8);
        arg11 += 2;
    }

    wr32(arg12, 0x250, rd32(arg12, 0x250) + rd32(arg12, 0x310));
    wr32(arg12, 0x260, rd32(arg12, 0x260) + 2);
    if (arg10 + 2 == (int16_t *)rdptr(arg12, 0x290)) {
        return sub_319a0(arg12);
    }

    return 0;
}

static int32_t sub_31ec0(
    int16_t arg1, int16_t *arg2, int16_t *arg3, int16_t *arg4, void *arg5,
    double arg6, double arg7, double arg8, double arg9, double arg10,
    double arg11, double arg12)
{
    int32_t width = rd32(arg5, 0x268);
    int16_t *dst0 = (int16_t *)rdptr(arg5, 0x1d0);
    int16_t *dst1 = (int16_t *)rdptr(arg5, 0x1d4);
    uint32_t i = 0;
    int32_t count = rd32(arg5, 0x270);

    while (i != (uint32_t)count) {
        uint32_t x = (uint32_t)trunc_s32((float)i);
        double pos = ((((double)((float)(arg6 + arg8))) * (double)(float)arg12) - arg8) - (double)((float)arg7);
        pos = pos * arg10 + arg8;
        {
            int16_t frac = (int16_t)trunc_s32((arg9 <= pos) ? (pos - arg9) : pos);
            uint32_t off = x << 1;
            arg4[off / 2] = frac;
            arg3[off / 2] = (int16_t)(0x10 - frac);
            *dst0 = (int16_t)x;
            arg2[x] = 1;
            *dst1 = (int32_t)x < (width - 1) ? (int16_t)(x + 1) : (int16_t)x;
        }
        ++i;
        ++dst0;
        ++dst1;
    }

    return 0;
}

static int32_t sub_3221c(int16_t *arg1, int16_t *arg2, void *arg3)
{
    int32_t s7 = rd32(arg3, 0x268) >> 4;
    int32_t a1 = rd32(arg3, 0x270);
    int32_t accum = 0;
    int32_t last_idx = 0;
    int32_t last_sum = 0;
    int16_t *p = arg2;

    if (s7 > 0) {
        int32_t v = 1;
        while (v < s7) {
            uint32_t t = (uint16_t)*p++;
            a1 -= (int32_t)t;
            accum += (int32_t)t;
            if (a1 >= 0x10) {
                last_idx = v;
                last_sum = accum;
            }
            ++v;
        }
    }

    if ((rd32(arg3, 0x260) ^ rd32(arg3, 0x264)) != 0) {
        last_idx = s7;
    } else {
        accum = last_sum;
    }

    if (rd32(arg3, 0x274) > 0) {
        return sub_32298(accum, rd32(arg3, 0x270), last_idx, arg1, 0, 0, 0, 0, 0, s7, arg3, p, 0);
    }

    return 0;
}

static int32_t sub_32298(
    int32_t arg1, int32_t arg2, int32_t arg3, int16_t *arg4, int32_t arg5,
    int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10,
    void *arg11, int16_t *arg12, int32_t arg13)
{
    int32_t off = arg1 << 1;
    int16_t *a1 = (int16_t *)rdptr(arg11, 0x208);
    int16_t *a3 = (int16_t *)rdptr(arg11, 0x20c);
    wrptr(arg11, 0x1c0, a1 + rd32(arg11, 0x274));
    wrptr(arg11, 0x200, (uint8_t *)rdptr(arg11, 0x1d0) + off);
    wrptr(arg11, 0x204, (uint8_t *)rdptr(arg11, 0x1d4) + off);
    wr32(arg11, 0x218, arg10);
    wr32(arg11, 0x1b0, arg9);
    wr32(arg11, 0x228, arg5);
    wr32(arg11, 0x21c, arg6);
    wr32(arg11, 0x220, arg7);
    wr32(arg11, 0x1e0, arg1 < rd32(arg11, 0x270));
    wr32(arg11, 0x1f0, arg3);
    wr32(arg11, 0x224, arg8);
    (void)arg2;
    (void)arg4;
    (void)a3;
    (void)arg12;
    (void)arg13;
    return 0;
}

static int32_t sub_323b0(
    uint8_t *arg1, int16_t *arg2, int16_t *arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, int16_t *arg7)
{
    return sub_323d0(arg1 + 0x20, arg2 + 0x20, arg3 + 1, arg2, arg3, (uint8_t *)(intptr_t)(arg6 + (uint16_t)*arg3),
        arg7, arg4, arg3 + 1, arg7);
}

static int32_t sub_323d0(
    uint8_t *arg1, int16_t *arg2, int16_t *arg3, int16_t *arg4, int16_t *arg5,
    uint8_t *arg6, int16_t *arg7, int32_t arg8, int16_t *arg9, void *arg10)
{
    uint8_t *base = arg6 + ((uint16_t)*arg9) * arg8;
    int16_t *idx = (int16_t *)rdptr(arg10, 0x200);
    int16_t *mix = (int16_t *)rdptr(arg10, 0x204);

    if (rd32(arg10, 0x1e0) != 0) {
        while (1) {
            uint32_t x = (uint16_t)*idx;
            uint32_t off = x << 1;
            uint32_t y = (uint16_t)*mix;
            *arg1++ = (uint8_t)(((uint8_t)base[x] * (uint16_t)*(int16_t *)((uint8_t *)arg5 + off) +
                (uint8_t)base[y] * (uint16_t)*(int16_t *)((uint8_t *)arg7 + off)) >> 4);
            ++idx;
            ++mix;
            if (idx == (int16_t *)sub_323d0) {
                break;
            }
        }
    }

    ++arg9;
    arg2 = (int16_t *)((uint8_t *)arg2 + rd32(arg10, 0x270));
    ++arg3;
    if (arg9 == (int16_t *)rdptr(arg10, 0x1c0)) {
        return sub_32458(arg10);
    }

    (void)arg4;
    (void)arg6;
    return 0;
}

static int32_t sub_32458(void *arg1)
{
    (void)rd32(arg1, 0x21c);
    (void)rd32(arg1, 0x220);
    (void)rd32(arg1, 0x1b0);
    (void)rd32(arg1, 0x228);
    (void)rd32(arg1, 0x270);
    wrptr(arg1, 0x200, rdptr(arg1, 0x224));
    return 0;
}

static int32_t sub_3296c(int32_t arg1, int16_t *arg2, char *arg3, int16_t *arg4, void *arg5)
{
    char *src = (char *)rdptr(arg5, 0x214);
    int32_t i = 0;
    wr32(arg5, 0x210, arg1);
    *arg2 = 0x10;
    while (1) {
        if ((uint16_t)arg4[i] == 0) {
            --(*arg2);
        } else {
            *(((char *)arg2 + i) + 0x164) = src[i];
        }
        ++i;
        if (arg3 == &src[i]) {
            break;
        }
    }
    return 0;
}

static int32_t sub_32514(
    int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int16_t *arg6, int16_t *arg7, int16_t *arg8, int16_t *arg9, int16_t *arg10,
    void *arg11, double arg12, double arg13, double arg14, double arg15,
    double arg16, double arg17, double arg18)
{
    int16_t frac = (int16_t)trunc_s32(arg12);

    (void)arg1;
    while (1) {
        int16_t base = (int16_t)(arg2 << 1);
        int32_t v1 = arg5 << 2;
        int32_t has_next = arg2 < rd32(arg11, 0x1c0);

        arg8[arg4 >> 1] = frac;
        arg8[arg3 >> 1] = frac;
        arg9[arg4 >> 1] = (int16_t)(0x10 - frac);
        arg9[arg3 >> 1] = (int16_t)(0x10 - frac);
        arg10[v1 >> 1] = base;
        arg10[(v1 >> 1) + 1] = (int16_t)(base + 1);
        arg7[arg4 >> 1] = 1;
        arg6[v1 >> 1] = (int16_t)(base + (has_next ? 2 : 0));
        arg6[(v1 >> 1) + 1] = (int16_t)(base + (has_next ? 3 : 1));
        arg7[(arg2 << 1)] = 1;

        ++arg5;
        if (arg5 == rd32(arg11, 0x1b0)) {
            break;
        }

        arg2 = trunc_s32((float)arg5);
        arg3 = arg2 << 2;
        arg4 = arg3 + 2;
        arg12 = ((((double)((float)arg13) + arg15) * arg16) - arg15 - (double)((float)arg14)) * arg17 + arg15;
        if (arg16 <= arg12) {
            arg12 -= arg16;
        }
        frac = (int16_t)trunc_s32(arg12);
    }

    if (rd32(arg11, 0x210) > 0) {
        int16_t *idx0 = (int16_t *)rdptr(arg11, 0x208);
        int16_t *idx1 = (int16_t *)rdptr(arg11, 0x20c);
        int16_t *w0 = (int16_t *)(intptr_t)rd32(arg11, 0x1d8);
        int16_t *w1 = (int16_t *)(intptr_t)rd32(arg11, 0x1dc);
        int32_t limit = rd32(arg11, 0x210);
        int32_t i = 0;
        double pos_seed = arg13;

        wr32(arg11, 0x1a0, rd32(arg11, 0x204));
        while (i != limit) {
            int32_t sample = trunc_s32((float)i);
            double pos = ((((double)((float)pos_seed) + arg15) * arg18) - arg15 - (double)((float)arg14)) * arg17 + arg15;
            int16_t frac2;

            if (arg16 <= pos) {
                pos -= arg16;
            }
            frac2 = (int16_t)trunc_s32(pos);
            *w1++ = frac2;
            *w0++ = (int16_t)(0x10 - frac2);
            *idx0++ = (int16_t)sample;
            *idx1++ = (sample < (limit - 1)) ? (int16_t)(sample + 1) : (int16_t)sample;
            ++i;
            pos_seed = pos;
        }

        return sub_3296c(limit - 1, (int16_t *)rdptr(arg11, 0x21c), (char *)rdptr(arg11, 0x218), arg7, arg11);
    }

    wr32(arg11, 0x210, rd32(arg11, 0x210) - 1);
    return 0;
}

static int32_t sub_324c0(
    int16_t *arg1, int16_t *arg2, char *arg3, int16_t *arg4, int32_t arg5,
    int16_t *arg6, int16_t *arg7, int16_t *arg8, int32_t arg9, void *arg10,
    double arg11, double arg12, double arg13, double arg14, double arg15,
    double arg16, double arg17)
{
    int32_t count = rd32(arg10, 0x1b0);
    double scale = arg17;
    int32_t i;

    wr32(arg10, 0x1f0, arg5);
    wrptr(arg10, 0x200, arg7);
    wr32(arg10, 0x1c0, count - 1);
    wr32(arg10, 0x1e0, arg9);
    wrptr(arg10, 0x204, arg1);
    wrptr(arg10, 0x218, arg3);
    wrptr(arg10, 0x21c, arg2);

    for (i = 0; i != count; ++i) {
        int32_t sample = trunc_s32((float)i);
        int32_t off = sample << 2;
        double pos = ((((double)((float)arg11) + arg14) * scale) - arg14 - (double)((float)arg12)) * arg16 + arg14;
        int16_t frac;
        int16_t base = (int16_t)(sample << 1);
        int32_t has_next;

        if (arg15 <= pos) {
            pos -= arg15;
            frac = (int16_t)trunc_s32(pos);
        } else {
            return sub_32514(base, sample, off, off + 2, i, arg4, arg8, arg6, arg7, arg6, arg10,
                pos, arg12, arg14, arg15, scale, arg16, arg17);
        }

        has_next = sample < rd32(arg10, 0x1c0);
        arg6[(off >> 1) + 1] = frac;
        arg6[off >> 1] = frac;
        arg7[(off >> 1) + 1] = (int16_t)(0x10 - frac);
        arg7[off >> 1] = (int16_t)(0x10 - frac);
        arg8[i * 2] = base + (has_next ? 2 : 0);
        arg8[i * 2 + 1] = base + (has_next ? 3 : 1);
        arg4[i * 2] = base;
        arg4[i * 2 + 1] = (int16_t)(base + 1);
        arg6[sample * 2] = 1;
    }

    if (rd32(arg10, 0x210) > 0) {
        int16_t *idx0 = (int16_t *)rdptr(arg10, 0x208);
        int16_t *idx1 = (int16_t *)rdptr(arg10, 0x20c);
        int16_t *w0 = (int16_t *)(intptr_t)rd32(arg10, 0x1d8);
        int16_t *w1 = (int16_t *)(intptr_t)rd32(arg10, 0x1dc);
        int32_t limit = rd32(arg10, 0x210);

        wr32(arg10, 0x1a0, rd32(arg10, 0x204));
        for (i = 0; i != limit; ++i) {
            int32_t sample = trunc_s32((float)i);
            double pos = ((((double)((float)arg11) + arg14) * arg17) - arg14 - (double)((float)arg12)) * arg16 + arg14;
            int16_t frac;

            if (arg15 <= pos) {
                pos -= arg15;
            }
            frac = (int16_t)trunc_s32(pos);
            *w1++ = frac;
            *w0++ = (int16_t)(0x10 - frac);
            *idx0++ = (int16_t)sample;
            *idx1++ = (sample < (limit - 1)) ? (int16_t)(sample + 1) : (int16_t)sample;
        }
        return sub_3296c(limit - 1, (int16_t *)rdptr(arg10, 0x21c), (char *)rdptr(arg10, 0x218), arg6, arg10);
    }

    wr32(arg10, 0x210, rd32(arg10, 0x210) - 1);
    return 0;
}

static int32_t sub_32750(
    uint8_t *arg1, uint8_t *arg2, int16_t *arg3, int16_t *arg4, uint8_t *arg5,
    int16_t *arg6, int16_t *arg7, int16_t *arg8, int16_t *arg9, int16_t *arg10,
    int32_t arg11, int32_t arg12, int32_t arg13, int32_t arg14, int32_t arg15,
    int16_t *arg16, void *arg17, int32_t arg18)
{
    uint32_t y0 = (uint16_t)*arg7;
    uint32_t y1 = (uint16_t)*arg8;
    uint32_t src_y = (uint16_t)*arg16;
    uint32_t off = src_y << 1;
    uint32_t w0 = (uint16_t)arg10[y0];
    uint32_t w1 = (uint16_t)arg9[y0];
    uint32_t a = arg5[y1];
    uint32_t b = arg5[y0];
    uint32_t c = arg2[src_y];
    uint32_t d = arg1[y1];
    uint32_t e = arg1[y0];

    (void)arg3;
    (void)arg4;
    (void)arg6;
    (void)arg7;
    (void)arg8;
    (void)arg11;
    (void)arg12;
    (void)arg13;
    (void)arg14;
    arg6[0] = (uint8_t)((a * w0 + b * w1 + c * (uint16_t)arg4[off >> 1] + d * (uint16_t)arg3[off >> 1] + arg18) >> 8);
    if (arg16 + 1 == (int16_t *)rdptr(arg17, 0x1c0)) {
        return sub_32458(arg17);
    }
    return 0;
}

static int32_t sub_3271c(
    uint8_t *arg1, int16_t *arg2, uint8_t *arg3, int16_t *arg4, int32_t arg5,
    int16_t *arg6, void *arg7)
{
    uint32_t row = (uint16_t)*arg6;
    uint8_t *src0 = arg3 + ((uint16_t)*arg2) * arg5;
    uint8_t *src1 = arg3 + row * arg5;

    if (rd32(arg7, 0x1e0) != 0) {
        return sub_32750(arg1, src0, arg2, arg4, src1, (int16_t *)rdptr(arg7, 0x200),
            (int16_t *)rdptr(arg7, 0x204), (int16_t *)rdptr(arg7, 0x204), arg1 ? arg4 : arg4,
            arg4, 0, 0, 0, 0, arg5, arg6, arg7, 0);
    }

    ++arg6;
    arg3 += rd32(arg7, 0x270);
    ++arg2;
    if (arg6 == (int16_t *)rdptr(arg7, 0x1c0)) {
        return sub_32458(arg7);
    }
    return 0;
}

static int32_t sub_3274c(int16_t *arg1)
{
    return sub_32750(0, 0, 0, 0, 0, 0, arg1, arg1, 0, 0, 0, 0, 0, 0, 0, arg1, 0, 0);
}

static int32_t sub_32d64(
    uint8_t *arg1, uint8_t *arg2, int16_t *arg3, uint8_t *arg4, int32_t arg5,
    int16_t *arg6, int16_t *arg7, void *arg8)
{
    int32_t base = (((uint16_t)*arg3) + rd32(arg8, 0x27c)) * rd32(arg8, 0x278) + rd32(arg8, 0x260);

    if (arg5 < rd32(arg8, 0x270)) {
        int32_t off = arg5 << 1;
        int16_t *idx0 = (int16_t *)((uint8_t *)rdptr(arg8, 0x1d0) + off);
        int16_t *idx1 = (int16_t *)((uint8_t *)rdptr(arg8, 0x1d4) + off);
        int16_t *idx_end = (int16_t *)((uint8_t *)rdptr(arg8, 0x1d0) + (rd32(arg8, 0x270) << 1));

        while (idx0 != idx_end) {
            uint32_t s0 = (uint16_t)*idx0++;
            uint32_t s1 = (uint16_t)*idx1++;
            uint32_t a = (uint16_t)arg7[s0];
            uint32_t b = (uint16_t)arg6[s0];
            *arg4++ = (uint8_t)(((uint8_t)*(arg2 + s0) * a + (uint8_t)*(arg2 + s1) * b) >> 4);
        }
    }

    (void)arg1;
    return base;
}

static int32_t sub_3316c(
    int32_t arg1, uint8_t *arg2, int16_t *arg3, int32_t arg4, uint8_t *arg5,
    uint8_t *arg6, int32_t arg7, int16_t *arg8, int16_t *arg9, void *arg10,
    int32_t arg11)
{
    int32_t off = arg7 << 1;
    int16_t *idx0 = (int16_t *)((uint8_t *)rdptr(arg10, 0x1d0) + off);
    int16_t *idx1 = (int16_t *)((uint8_t *)rdptr(arg10, 0x1d4) + off);
    int16_t *idx_end = (int16_t *)((uint8_t *)rdptr(arg10, 0x1d0) + (arg4 << 1));
    int16_t *vw0 = (int16_t *)rdptr(arg10, 0x1d8);
    int16_t *vw1 = (int16_t *)rdptr(arg10, 0x1dc);

    while (idx0 != idx_end) {
        uint32_t x0 = (uint16_t)*idx0++;
        uint32_t x1 = (uint16_t)*idx1++;
        uint32_t voff = (uint32_t)arg1 << 1;
        uint32_t hoff = x0 << 1;
        uint32_t hw0 = (uint16_t)arg9[hoff >> 1];
        uint32_t hw1 = (uint16_t)arg8[hoff >> 1];
        uint32_t p00 = (uint8_t)arg5[x0];
        uint32_t p01 = (uint8_t)arg5[x1];
        uint32_t p10 = (uint8_t)arg2[x0];
        uint32_t p11 = (uint8_t)arg2[x1];
        uint32_t vw0v = (uint16_t)*(int16_t *)((uint8_t *)vw0 + voff);
        uint32_t vw1v = (uint16_t)*(int16_t *)((uint8_t *)vw1 + voff);

        *arg6++ = (uint8_t)((p01 * hw0 + p00 * hw1 + p10 * vw0v + p11 * vw1v + arg11) >> 8);
        arg1 = (uint16_t)*arg3;
    }

    return 0;
}

static int32_t sub_33108(
    int32_t arg1, uint8_t *arg2, int16_t *arg3, int32_t arg4, uint8_t *arg5,
    uint8_t *arg6, int32_t arg7, int16_t *arg8, int16_t *arg9, void *arg10,
    int32_t arg11)
{
    (void)*arg9;
    return sub_3316c(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, arg11);
}

static int32_t sub_32c6c(
    int32_t arg1, int32_t arg2, int32_t arg3, int16_t *arg4, void *arg5)
{
    int16_t *idx = (int16_t *)((uint8_t *)rdptr(arg5, 0x208) + arg1);
    uint32_t row = (uint16_t)*idx;

    if ((uint16_t)*(int16_t *)((uint8_t *)rdptr(arg5, 0x1dc) + (row << 1)) == 0) {
        return sub_32d64(0, 0, idx, (uint8_t *)rdptr(arg5, 0x228), arg2, arg4,
            (int16_t *)rdptr(arg5, 0x204), arg5);
    }

    if (arg2 != 0) {
        return sub_33108(
            row,
            (uint8_t *)(intptr_t)((((uint16_t)*(int16_t *)((uint8_t *)rdptr(arg5, 0x20c) + arg1)) + rd32(arg5, 0x27c)) * rd32(arg5, 0x278) + rd32(arg5, 0x260)),
            idx,
            rd32(arg5, 0x270),
            (uint8_t *)(intptr_t)(((row + rd32(arg5, 0x27c)) * rd32(arg5, 0x278)) + rd32(arg5, 0x260)),
            (uint8_t *)(intptr_t)(((rd32(arg5, 0x274) + rd32(arg5, 0x210)) * rd32(arg5, 0x270)) + rd32(arg5, 0x264)),
            arg3,
            (int16_t *)rdptr(arg5, 0x204),
            (int16_t *)rdptr(arg5, 0x1dc),
            arg5,
            0);
    }

    return sub_32d64(0, 0, idx, (uint8_t *)rdptr(arg5, 0x228), 0, arg4, (int16_t *)rdptr(arg5, 0x204), arg5);
}

static int32_t sub_32bc8(
    uint8_t *arg1, int16_t *arg2, int16_t *arg3, int16_t *arg4, int32_t arg5,
    int32_t arg6, int16_t *arg7, int16_t *arg8, int32_t arg9, void *arg10)
{
    int32_t row = (((uint16_t)*arg4) + arg9) * arg6 + arg5;
    int16_t *idx1 = (int16_t *)rdptr(arg10, 0x214);

    if (rd32(arg10, 0x1f0) != 0) {
        int16_t *idx0 = (int16_t *)rdptr(arg10, 0x218);

        while (idx0 != arg3) {
            uint32_t x0 = (uint16_t)*idx0++;
            uint32_t x1 = (uint16_t)*idx1++;
            uint32_t off = x0 << 1;
            uint32_t w0 = (uint16_t)arg7[off >> 1];
            uint32_t w1 = (uint16_t)arg8[off >> 1];
            *arg1++ = (uint8_t)(((uint8_t)*(uint8_t *)(intptr_t)(row + x0) * w0 +
                (uint8_t)*(uint8_t *)(intptr_t)(row + x1) * w1) >> 4);
        }
    }

    ++arg4;
    arg2 = (int16_t *)((uint8_t *)arg2 + rd32(arg10, 0x270));
    wrptr(arg10, 0x1b0, (uint8_t *)rdptr(arg10, 0x1b0) + 2);
    if (arg4 == (int16_t *)rdptr(arg10, 0x1e0)) {
        return sub_32c6c(rd32(arg10, 0x21c), rd32(arg10, 0x220), rd32(arg10, 0x224), arg7, arg10);
    }

    return 0;
}

static int32_t sub_32ba8(
    uint8_t *arg1, int16_t *arg2, int16_t *arg3, int16_t *arg4, int32_t arg5,
    int32_t arg6, int16_t *arg7, int16_t *arg8, int32_t arg9, void *arg10)
{
    return sub_32bc8(arg1 + 0x20, arg2 + 0x10, arg3 + 1, arg4 + 8, arg5, arg6,
        arg7, arg8, arg9, arg10);
}

static int32_t sub_32d44(
    uint8_t *arg1, uint8_t *arg2, int16_t *arg3, uint8_t *arg4, int32_t arg5,
    int16_t *arg6, int16_t *arg7, int16_t *arg8, void *arg9)
{
    return sub_32d64(arg1 + 0x20, arg2 + 0x20, arg3, arg4 + (uint16_t)*arg8, arg5,
        arg6, arg7, arg9);
}

static int32_t sub_32f24(
    uint8_t *arg1, int16_t *arg2, int16_t *arg3, int16_t *arg4, int32_t arg5,
    int32_t arg6, int16_t *arg7, int16_t *arg8, int32_t arg9, void *arg10,
    int32_t arg11)
{
    uint32_t row0 = (uint16_t)*arg4;
    int32_t base0 = (row0 + arg9) * arg6 + arg5;
    int32_t base1 = (((uint16_t)**(int16_t **)rdptr(arg10, 0x1b0)) + arg9) * arg6 + arg5;
    int16_t *idx1 = (int16_t *)rdptr(arg10, 0x214);

    if (rd32(arg10, 0x1f0) != 0) {
        int16_t *idx0 = (int16_t *)rdptr(arg10, 0x218);
        uint32_t src_row = row0;

        while (1) {
            uint32_t x0 = (uint16_t)*idx0++;
            uint32_t x1 = (uint16_t)*idx1++;
            uint32_t soff = src_row << 1;
            uint32_t hoff = x0 << 1;
            uint32_t hw0 = (uint16_t)arg8[hoff >> 1];
            uint32_t hw1 = (uint16_t)arg7[hoff >> 1];
            uint32_t p00 = (uint8_t)*(uint8_t *)(intptr_t)(base0 + x0);
            uint32_t p01 = (uint8_t)*(uint8_t *)(intptr_t)(base0 + x1);
            uint32_t p10 = (uint8_t)*(uint8_t *)(intptr_t)(base1 + x0);
            uint32_t p11 = (uint8_t)*(uint8_t *)(intptr_t)(base1 + x1);
            uint32_t vw0 = (uint16_t)*(int16_t *)((uint8_t *)arg8 + soff);
            uint32_t vw1 = (uint16_t)*(int16_t *)((uint8_t *)arg7 + soff);

            *arg1++ = (uint8_t)((p01 * hw0 + p00 * hw1 + p10 * vw0 + p11 * vw1 + arg11) >> 8);
            if (idx0 == arg3) {
                break;
            }
            src_row = (uint16_t)*arg4;
        }
    }

    ++arg4;
    arg2 = (int16_t *)((uint8_t *)arg2 + rd32(arg10, 0x270));
    wrptr(arg10, 0x1b0, (uint8_t *)rdptr(arg10, 0x1b0) + 2);
    if (arg4 == (int16_t *)rdptr(arg10, 0x1e0)) {
        return sub_32c6c(rd32(arg10, 0x21c), rd32(arg10, 0x220), rd32(arg10, 0x224), arg7, arg10);
    }

    return 0;
}

static int32_t sub_329f4(
    int16_t *arg1, void *arg2, int16_t *arg3, int16_t *arg4, int32_t arg5,
    void *arg6)
{
    int32_t pitch = (rd32(arg6, 0x260) == rd32(arg6, 0x264)) ? rd32(arg6, 0x270) : rd32(arg6, 0x268);
    int32_t last_idx = 0;
    int32_t last_sum = 0;
    int32_t accum = 0;
    int32_t i = 0;
    int16_t *scan = arg1;

    if (arg5 > 0) {
        i = 1;
        while (i < arg5) {
            uint32_t n = (uint16_t)*scan++;
            pitch -= (int32_t)n;
            accum += (int32_t)n;
            if (pitch >= 0x10) {
                last_idx = i;
                last_sum = accum;
            }
            ++i;
        }
    }

    if (rd32(arg6, 0x260) == rd32(arg6, 0x264)) {
        accum = last_sum;
        arg5 = last_idx;
    }

    if (rd32(arg6, 0x210) <= 0) {
        wr32(arg6, 0x210, 0);
        return sub_32c6c(0, last_idx, last_sum, arg3, arg6);
    }

    wr32(arg6, 0x21c, rd32(arg6, 0x210) << 1);
    wrptr(arg6, 0x200, arg3);
    wrptr(arg6, 0x228, arg2);
    wrptr(arg6, 0x1e0, (uint8_t *)rdptr(arg6, 0x208) + rd32(arg6, 0x210));
    wrptr(arg6, 0x1c0, arg4);
    wrptr(arg6, 0x218, (uint8_t *)rdptr(arg6, 0x1d0) + (accum << 1));
    wrptr(arg6, 0x214, (uint8_t *)rdptr(arg6, 0x1d4) + (accum << 1));
    wrptr(arg6, 0x204, arg1);
    wrptr(arg6, 0x1b0, rdptr(arg6, 0x20c));
    wr32(arg6, 0x200, arg5);
    wr32(arg6, 0x1f0, accum < rd32(arg6, 0x270));
    wr32(arg6, 0x220, last_idx);
    wr32(arg6, 0x224, last_sum);
    return 0;
}

static int32_t sub_333e0(int32_t arg1, int32_t arg2, void *arg3, void *arg4)
{
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    return c_resize_simd_nv12_up();
}

static int32_t sub_33444(
    int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    void *arg6, void *arg7)
{
    if (arg4 == 0x5a0 && arg1 == 0x440) {
        wr32(arg7, 0x68, 0xee030);
    }
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    (void)arg6;
    return c_resize_simd_nv12_down();
}

static int32_t sub_351e8(
    int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, char *arg5,
    int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10,
    int32_t arg11, int32_t arg12, int32_t arg13);
static int32_t sub_3529c(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5);
static int32_t sub_352d8(
    int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, char *arg5,
    int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10,
    int32_t arg11, int32_t arg12);
static int32_t sub_35500(
    int32_t arg1, int32_t arg2, void *arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, int32_t arg7);
static int32_t sub_356d0(
    void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int32_t arg6);
static int32_t sub_35768(int32_t arg1, uint8_t *arg2, int16_t *arg3, void *arg4);

static int32_t sub_31474(
    uint8_t *arg1, int16_t *arg2, uint8_t *arg3, uint8_t *arg4, int32_t arg5,
    int32_t arg6, int16_t *arg7, int16_t *arg8, int32_t arg9, void *arg10,
    int16_t *arg11, int16_t *arg12, int16_t *arg13, void *arg14, int32_t arg15)
{
    int32_t off = arg6 << 1;
    int16_t *idx0 = (int16_t *)((uint8_t *)arg7 + off);
    int16_t *idx1 = (int16_t *)((uint8_t *)arg2 + off);
    int16_t *idx2 = (int16_t *)((uint8_t *)arg8 + off);
    int16_t *w = (int16_t *)((uint8_t *)rdptr(arg14, 0x248) + off);
    uint8_t *out = arg4;
    uint8_t *out_end = out + rd32(arg14, 0x280);
    uint32_t vw0 = (uint16_t)arg12[0];
    uint32_t vw1 = (arg13 != 0) ? (uint16_t)arg13[0] : (uint16_t)(0x100 - vw0);

    wr32(arg14, 0x2cc, arg5);
    while (out != out_end) {
        uint32_t s0 = (uint16_t)*idx2++;
        uint32_t hw0 = (uint16_t)*w++;
        uint32_t s1 = (uint16_t)*idx0++;
        uint32_t hw1 = (uint16_t)*idx1++;
        uint32_t p00 = (uint8_t)arg1[s0];
        uint32_t p01 = (uint8_t)arg1[s1];
        uint32_t p10 = (uint8_t)arg3[s0];
        uint32_t p11 = (uint8_t)arg3[s1];
        uint32_t top = p00 * hw0 + p01 * hw1;
        uint32_t bot = p10 * hw0 + p11 * hw1;
        *out++ = (uint8_t)((top * vw0 + bot * vw1 + (uint32_t)arg15) >> 8);
    }

    (void)arg9;
    (void)arg10;
    (void)arg11;
    if (arg13 + 2 == (int16_t *)rdptr(arg14, 0x2a0)) {
        return sub_31180(arg14);
    }

    return 0;
}

static int32_t sub_34228(
    int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10,
    int32_t arg11, int32_t arg12, int32_t arg13, int32_t arg14, int32_t arg15,
    int32_t arg16, int32_t arg17, int32_t arg18, int32_t arg19, int32_t arg20,
    int32_t arg21, void *arg22)
{
    wr32(arg22, 0x6c, arg5);
    wr32(arg22, 0x50, rd32(arg22, 0x50) + 0xff00);
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg6;
    (void)arg7;
    (void)arg8;
    (void)arg9;
    (void)arg10;
    (void)arg11;
    (void)arg12;
    (void)arg13;
    (void)arg14;
    (void)arg15;
    (void)arg16;
    (void)arg17;
    (void)arg18;
    (void)arg19;
    (void)arg20;
    (void)arg21;
    return 0;
}

static int32_t sub_350a4(
    int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10,
    int32_t arg11, int32_t arg12, int32_t arg13, int32_t arg14, int32_t arg15,
    int32_t arg16, int32_t arg17, int32_t arg18, int32_t arg19, int32_t arg20,
    void *arg21)
{
    wr32(arg21, 0x50, rd32(arg21, 0x50) + 0xff00);
    wrptr(arg21, 0x70, (uint8_t *)rdptr(arg21, 0x70) + 2);
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    (void)arg6;
    (void)arg7;
    (void)arg8;
    (void)arg9;
    (void)arg10;
    (void)arg11;
    (void)arg12;
    (void)arg13;
    (void)arg14;
    (void)arg15;
    (void)arg16;
    (void)arg17;
    (void)arg18;
    (void)arg19;
    (void)arg20;
    return 0;
}

static int32_t sub_351ac(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    (void)arg2;
    (void)arg3;
    (void)arg4;
    if (arg5 >= arg1) {
        return 0;
    }
    return 0;
}

static int32_t sub_351e8(
    int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, char *arg5,
    int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10,
    int32_t arg11, int32_t arg12, int32_t arg13)
{
    char *dst = (char *)(intptr_t)(arg1 + arg13);

    for (int32_t i = 0; i < arg7; ++i) {
        dst[i] = arg5[i];
    }

    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg6;
    (void)arg8;
    (void)arg9;
    (void)arg10;
    (void)arg11;
    (void)arg12;
    return 0;
}

static int32_t sub_35288(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    if (arg1 + 4 < arg4) {
        return 0;
    }
    return sub_3529c(arg1, arg2, arg3, arg4, 0);
}

static int32_t sub_3529c(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    (void)arg2;
    (void)arg4;
    if (arg3 >= arg1) {
        return 0;
    }
    return (arg5 != 0) ? 0 : 0;
}

static int32_t sub_352d8(
    int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, char *arg5,
    int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10,
    int32_t arg11, int32_t arg12)
{
    char *dst = (char *)(intptr_t)(arg1 + arg9);

    for (int32_t i = 0; i < arg7; ++i) {
        dst[i] = arg5[i];
    }

    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg6;
    (void)arg8;
    (void)arg10;
    (void)arg11;
    (void)arg12;
    return 0;
}

static int32_t sub_3536c(
    int16_t *arg1, int16_t *arg2, void *arg3, int64_t arg4, double arg5,
    double arg6)
{
    double cur = (double)(float)arg4;
    int16_t *uv = (int16_t *)rdptr(arg3, 0x68);
    uint8_t *lut = (uint8_t *)arg3 + 0x30;
    uint8_t *lut_vals = (uint8_t *)arg3 + 0x40;
    int16_t count = 0;

    for (int32_t i = 0; i < 0x2d0; ++i) {
        cur = (((double)(float)cur) + arg5) * arg6 - arg5;
        int32_t idx = trunc_s32((double)(float)cur);
        arg1[i] = (int16_t)idx;
        arg2[idx] = 1;
    }

    for (int32_t i = 0; i < 0x240; ++i) {
        cur = (((double)(float)cur) + arg5) * arg6 - arg5;
        uv[i] = (int16_t)trunc_s32((double)(float)cur);
    }

    for (int32_t i = 0; i < 0x11; ++i) {
        lut[i] = 0;
    }
    lut_vals[0] = 0;
    for (int32_t i = 1; i < 0x10; ++i) {
        lut_vals[i] = (uint8_t)(i << 1);
    }

    for (int32_t i = 0; i < 0x10; ++i) {
        if (arg2[i] != 0) {
            lut[count++] = lut_vals[i];
        }
    }

    return 0;
}

static int32_t sub_354f0(
    int32_t arg1, int32_t arg2, void *arg3, int32_t arg4, int32_t arg5,
    int16_t *arg6)
{
    if (arg4 + 0x10 != arg2) {
        return 0;
    }

    return sub_35500((uint16_t)*arg6, arg2, arg3, arg4, arg5, 0, 0);
}

static int32_t sub_35500(
    int32_t arg1, int32_t arg2, void *arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, int32_t arg7)
{
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    (void)arg6;
    (void)arg7;
    return 0;
}

static int32_t sub_35540(
    uint16_t *arg1, int16_t *arg2, int16_t *arg3, void *arg4, int64_t arg5,
    double arg6, double arg7)
{
    double cur = (double)(float)arg5;
    int16_t *uv = (int16_t *)rdptr(arg4, 0x6c);
    uint8_t *src_lut = (uint8_t *)rdptr(arg4, 0x68);
    uint8_t *dst_lut = (uint8_t *)rdptr(arg4, 0x54);

    for (int32_t i = 0; i < 0x168; ++i) {
        cur = (((double)(float)cur) + arg6) * arg7 - arg6;
        int32_t idx = trunc_s32((double)(float)cur);
        uint16_t off = (uint16_t)(idx << 1);
        arg1[i << 1] = off;
        arg1[(i << 1) + 1] = (uint16_t)(off + 1);
        arg2[idx << 1] = 1;
        arg2[(idx << 1) + 1] = 1;
    }

    for (int32_t i = 0; i < 0x120; ++i) {
        cur = (((double)(float)cur) + arg6) * arg7 - arg6;
        uv[i] = (int16_t)trunc_s32((double)(float)cur);
    }

    *arg3 = 0;
    for (int32_t i = 0; i < 0x18; ++i) {
        if (arg2[i] != 0) {
            dst_lut[*arg3] = src_lut[i];
            *arg3 = (int16_t)(*arg3 + 1);
        }
    }

    return 0;
}

static int32_t sub_356b8(
    int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int16_t *arg6)
{
    if (arg5 + 0x10 != arg4) {
        return 0;
    }

    return sub_356d0((void *)(intptr_t)(arg1 + 0x10), arg2 + (uint16_t)*arg6, arg3, arg4, arg5, 0);
}

static int32_t sub_356d0(
    void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int32_t arg6)
{
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    (void)arg6;
    return 0;
}

static int32_t sub_3570c(uint8_t *arg1, int32_t arg2, int16_t *arg3)
{
    uint32_t idx = (uint16_t)*arg3;

    if ((int32_t)idx < 0x2c1 || (int32_t)idx >= 0x2d0) {
        return 0;
    }

    (void)arg1;
    (void)arg2;
    return 0;
}

static int32_t sub_35768(int32_t arg1, uint8_t *arg2, int16_t *arg3, void *arg4)
{
    int16_t *scan = arg3 + arg1;
    uint8_t *dst = (uint8_t *)rdptr(arg4, 0xbc) + arg1 + 0x97b30;
    uint8_t result = 0;

    do {
        uint32_t x0 = (uint16_t)scan[0];
        uint32_t x1 = (uint16_t)scan[1];
        dst[0] = arg2[x0];
        result = arg2[x1];
        dst[1] = result;
        dst += 2;
        scan += 2;
        arg1 += 2;
    } while (arg1 < 0x2d0);

    return result;
}

static uint32_t *sub_357c8(uint32_t *arg1, void *arg2, int32_t arg3, float arg4)
{
    uint32_t *dst = (uint32_t *)rdptr(arg2, 0xbc);
    uint32_t *tail = dst;

    (void)arg3;
    for (uint32_t i = 0; i != 0x3c0; ++i) {
        int32_t row = trunc_s32((double)((float)i * arg4));
        uint32_t *src = (uint32_t *)((uint8_t *)arg1 + row * 0x500);

        for (uint32_t j = 0; j != 0x140; ++j) {
            dst[j] = src[j];
        }

        dst += 0x140;
    }

    dst = (uint32_t *)((uint8_t *)rdptr(arg2, 0xbc) + 0x12c000);
    for (uint32_t i = 0; i != 0x1e0; ++i) {
        int32_t row = trunc_s32((double)((float)i * arg4));
        uint32_t *src = (uint32_t *)((uint8_t *)arg1 + row * 0x500 + 0xe1000);

        for (uint32_t j = 0; j != 0x140; ++j) {
            dst[j] = src[j];
        }

        tail = dst + 0x140;
        dst += 0x140;
    }

    return tail;
}
