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
