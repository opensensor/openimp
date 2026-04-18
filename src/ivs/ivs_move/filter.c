#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
void imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, ...); /* forward decl */
extern char _gp; /* MIPS GOT base symbol */

int32_t MorphRowFilter(void *arg1, char *arg2, int32_t arg3, int32_t arg4, int32_t arg5); /* forward decl, ported by T<N> later */
int32_t MorphColumnFilter(void *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6,
    int32_t *arg7, int32_t arg8); /* forward decl, ported by T<N> later */
int32_t sub_dc144(int32_t arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5, char *arg6, char *arg7,
    int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11, int32_t arg12); /* forward decl, ported by T<N> later */
int32_t sub_dc2d4(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, char *arg5, int32_t arg6, int32_t arg7,
    void *arg8, char *arg9, int32_t arg10, int32_t arg11, int32_t arg12, int32_t arg13, int32_t arg14,
    int32_t *arg15, int32_t arg16, int32_t arg17, int32_t *arg18, int32_t arg19, int32_t arg20, int32_t arg21,
    int32_t arg22, int32_t arg23, int32_t arg24, int32_t arg25, int32_t arg26, int32_t arg27, int32_t arg28,
    int32_t arg29, int32_t arg30); /* forward decl, ported by T<N> later */
int32_t CalcSum(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t sub_dcf40(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4); /* forward decl, ported by T<N> later */
int32_t sub_dcf90(int32_t arg1, int32_t arg2, int32_t *arg3, int32_t arg4, int32_t arg5); /* forward decl, ported by T<N> later */
int32_t sub_dd3a0(int32_t arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t sub_dd420(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6,
    void *arg7); /* forward decl, ported by T<N> later */
int32_t sub_dd530(int32_t arg1, void *arg2, int32_t arg3, int32_t arg4, void *arg5, int32_t arg6); /* forward decl, ported by T<N> later */
int32_t sub_ddce4(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6,
    int32_t arg7, void *arg8); /* forward decl, ported by T<N> later */
int32_t sub_dde00(int32_t arg1, int32_t arg2, int32_t *arg3, int32_t arg4, uint32_t arg5, uint32_t arg6,
    char *arg7, uint32_t arg8, uint32_t arg9, void *arg10, int32_t *arg11, int32_t arg12, void *arg13); /* forward decl, ported by T<N> later */
int32_t remainingInputRows(void *arg1); /* forward decl */
int32_t remainingOutputRows(void *arg1); /* forward decl */
int32_t sub_dc604(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, void *arg5, int32_t arg6, int32_t arg7,
    int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11, int32_t arg12, int32_t arg13, int32_t arg14,
    int32_t arg15, int32_t arg16, int32_t arg17, int32_t arg18, int32_t arg19, int32_t arg20, int32_t arg21,
    int32_t arg22, int32_t arg23, int32_t arg24, int32_t arg25, int32_t arg26, int32_t arg27, int32_t arg28,
    int32_t arg29, int32_t arg30, int32_t arg31); /* forward decl */
void freeFilterEngine(void *arg1); /* forward decl */

#define PTR_AT(type, base, off) (*(type *)((uint8_t *)(base) + (off)))

typedef struct Image {
    int32_t data;
    uint8_t channel;
    uint8_t pad05[3];
    int32_t width;
    int32_t height;
    int32_t step;
    int32_t pad14;
    int32_t refcount;
} Image;

int32_t *CreateImage(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4, char arg5)
{
    *arg1 = arg2;
    ((uint8_t *)&arg1[1])[0] = (uint8_t)arg5;
    arg1[5] = 0;
    arg1[6] = 0;
    arg1[7] = 1;
    arg1[2] = arg4;
    arg1[3] = arg4;
    arg1[4] = arg3;
    arg1[5] = 0;
    return arg1;
}

int32_t MinuOp(uint32_t arg1, uint8_t arg2)
{
    return (int32_t)(arg1 & (uint32_t)arg2);
}

int32_t MaxuOp(uint8_t arg1, uint8_t arg2)
{
    uint32_t a1 = (uint32_t)arg2;
    uint32_t a0 = (uint32_t)arg1;
    uint8_t v0 = (uint8_t)a1;

    if (a1 < a0) {
        v0 = (uint8_t)a0;
    }

    return (int32_t)(uint32_t)v0;
}

int32_t MinsOp(int32_t arg1, int32_t arg2)
{
    if (arg1 >= arg2) {
        return arg2;
    }

    return arg1;
}

int32_t MaxsOp(int32_t arg1, int32_t arg2)
{
    if (arg2 >= arg1) {
        return arg2;
    }

    return arg1;
}

int32_t borderInterpolate(int32_t arg1, int32_t arg2)
{
    if ((uint32_t)arg1 < (uint32_t)arg2) {
        return arg1;
    }

    if (arg1 < 0) {
        return 0;
    }

    return arg2 - 1;
}

int32_t Start(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7,
    void *arg8)
{
    int32_t s1 = arg7;
    int32_t t1_1;
    int32_t t2_1 = 0;
    int32_t var_30;
    int32_t var_34;
    int32_t var_38;
    int32_t var_3c;
    int32_t var_40;

    if (arg8 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "MOVEFILTER",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0x92, "Start",
            "null pointer filter\n");
        return -1;
    }

    PTR_AT(int32_t, arg8, 0x3c) = arg1;
    PTR_AT(int32_t, arg8, 0x40) = arg2;
    PTR_AT(int32_t, arg8, 0x44) = arg3;
    PTR_AT(int32_t, arg8, 0x48) = arg4;
    PTR_AT(int32_t, arg8, 0x4c) = arg5;
    PTR_AT(int32_t, arg8, 0x50) = arg6;

    t1_1 = arg3;
    if (arg3 >= 0) {
        t2_1 = arg4;
    }

    if (arg3 >= 0 && arg4 >= 0 && arg5 >= 0 && arg6 >= 0 && arg1 >= arg3 + arg5) {
        if (arg2 >= arg4 + arg6) {
            goto label_dba38;
        }

        var_30 = arg2;
        goto label_db9f0;
    }

    var_30 = arg2;

label_db9f0:
    var_34 = arg1;
    var_40 = arg4;
    var_38 = arg6;
    var_3c = arg5;
    printf("%s(%d):roi.x=%d, roi.y=%d, roi.width=%d, roi.height=%d, wholeSize.width=%d, wholeSize.height=%d\n",
        "Start", 0x9b, arg3, var_40, var_3c, var_38, var_34, var_30);
    t1_1 = PTR_AT(int32_t, arg8, 0x44);
    if (t1_1 >= 0) {
        t2_1 = PTR_AT(int32_t, arg8, 0x48);
        if (t2_1 >= 0) {
            goto label_dba38;
        }
    }

    return remainingInputRows((void *)(intptr_t)__assert(
        "filter->roi.x >= 0 && filter->roi.y >= 0 && filter->roi.width >= 0 && filter->roi.height >= 0 && filter->roi.x + filter->roi.width <= filter->wholeSize.width &&filter->roi.y + filter->roi.height <= filter->wholeSize.height",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0x9e, "Start", var_40, var_3c, var_38,
        var_34, var_30));

label_dba38:
    {
        int32_t v1_4 = PTR_AT(int32_t, arg8, 0x4c);

        if (v1_4 < 0) {
            return remainingInputRows((void *)(intptr_t)__assert(
                "filter->roi.x >= 0 && filter->roi.y >= 0 && filter->roi.width >= 0 && filter->roi.height >= 0 && filter->roi.x + filter->roi.width <= filter->wholeSize.width &&filter->roi.y + filter->roi.height <= filter->wholeSize.height",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0x9e, "Start", var_40, var_3c,
                var_38, var_34, var_30));
        }

        {
            int32_t v0_1 = PTR_AT(int32_t, arg8, 0x50);

            if (v0_1 >= 0 && PTR_AT(int32_t, arg8, 0x3c) >= v1_4 + t1_1 &&
                PTR_AT(int32_t, arg8, 0x40) >= v0_1 + t2_1) {
                int32_t a1 = PTR_AT(int32_t, arg8, 0x2c);
                int32_t a0_1;
                int32_t v0_6;
                void *v0_7;
                int32_t v0_8;
                int32_t a0_5;
                void *v0_9;
                void *v0_10;
                int32_t a0_13;
                int32_t v0_14;
                int32_t a0_16;
                int32_t v0_15;
                int32_t v1_12;
                int32_t a0_17;

                if (s1 < 0) {
                    s1 = a1 + 3;
                }

                a0_1 = PTR_AT(int32_t, arg8, 0x34);
                v0_6 = MaxsOp(s1, (MaxsOp(a0_1, a1 - a0_1 - 1) << 1) + 1);
                v0_7 = malloc((size_t)(v0_6 << 2));
                PTR_AT(void *, arg8, 0x88) = v0_7;
                if (v0_7 == NULL) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "MOVEFILTER",
                        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0xaa, "Start",
                        "%s(%d):malloc rows failed\n", "Start", 0xaa, &_gp);
                    return -1;
                }

                v0_8 = MaxsOp(PTR_AT(int32_t, arg8, 0x38), PTR_AT(int32_t, arg8, 0x4c));
                a0_5 = PTR_AT(int32_t, arg8, 0x28);
                PTR_AT(int32_t, arg8, 0x38) = v0_8;
                v0_9 = malloc((size_t)(v0_8 + a0_5 - 1));
                PTR_AT(void *, arg8, 0x84) = v0_9;
                if (v0_9 == NULL) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "MOVEFILTER",
                        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0xb2, "Start",
                        "%s(%d):malloc srcRow failed\n", "Start", 0xb2, &_gp);
                    free(PTR_AT(void *, arg8, 0x88));
                    return -1;
                }

                v0_10 = malloc((size_t)((((v0_8 + 0xf) & 0xfffffff0) * v0_6) + 0x10));
                PTR_AT(void *, arg8, 0x80) = v0_10;
                if (v0_10 == NULL) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "MOVEFILTER",
                        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0xb8, "Start",
                        "%s(%d):malloc ringBuf failed\n", "Start", 0xb8, &_gp);
                    free(PTR_AT(void *, arg8, 0x84));
                    free(PTR_AT(void *, arg8, 0x88));
                    return -1;
                }

                a0_13 = PTR_AT(int32_t, arg8, 0x30) - PTR_AT(int32_t, arg8, 0x44);
                PTR_AT(int32_t, arg8, 0x60) = (PTR_AT(int32_t, arg8, 0x4c) + 0xf) & 0xfffffff0;
                v0_14 = MaxsOp(a0_13, 0);
                a0_16 = PTR_AT(int32_t, arg8, 0x28) - PTR_AT(int32_t, arg8, 0x30) - 1 +
                    PTR_AT(int32_t, arg8, 0x44) + PTR_AT(int32_t, arg8, 0x4c) - PTR_AT(int32_t, arg8, 0x3c);
                PTR_AT(int32_t, arg8, 0x54) = v0_14;
                v0_15 = MaxsOp(a0_16, 0);
                v1_12 = PTR_AT(int32_t, arg8, 0x54);
                PTR_AT(int32_t, arg8, 0x58) = v0_15;

                if (v1_12 > 0 || v0_15 > 0) {
                    int32_t *s5_1 = (int32_t *)((uint8_t *)arg8 + 0x78);
                    int32_t v0_20 = MinsOp(PTR_AT(int32_t, arg8, 0x44), PTR_AT(int32_t, arg8, 0x30));
                    int32_t a0_23 = PTR_AT(int32_t, arg8, 0x54);
                    int32_t s2_2 = PTR_AT(int32_t, arg8, 0x3c);
                    int32_t s3_2 = v0_20 - PTR_AT(int32_t, arg8, 0x44);
                    int32_t s1_2 = 0;
                    int32_t i = 0;

                    if (a0_23 > 0) {
                        do {
                            *s5_1 = s3_2 + borderInterpolate(s1_2 - a0_23, s2_2);
                            a0_23 = PTR_AT(int32_t, arg8, 0x54);
                            s1_2 += 1;
                            s5_1 = &s5_1[1];
                        } while (s1_2 < a0_23);
                    }

                    if (!((v1_12 <= 0 && v0_15 <= 0) || PTR_AT(int32_t, arg8, 0x58) <= 0)) {
                        do {
                            ((int32_t *)((uint8_t *)arg8 + 0x78))[i + PTR_AT(int32_t, arg8, 0x54)] =
                                s3_2 + borderInterpolate(s2_2 + i, s2_2);
                            i += 1;
                        } while (i < PTR_AT(int32_t, arg8, 0x58));
                    }
                }

                a0_17 = PTR_AT(int32_t, arg8, 0x48);
                {
                    int32_t a0_18 = a0_17 - PTR_AT(int32_t, arg8, 0x34);
                    int32_t v0_17 = MaxsOp(a0_18, 0);
                    int32_t a1_10 = PTR_AT(int32_t, arg8, 0x40);
                    int32_t a0_20 = PTR_AT(int32_t, arg8, 0x48) + PTR_AT(int32_t, arg8, 0x50) +
                        PTR_AT(int32_t, arg8, 0x2c) - PTR_AT(int32_t, arg8, 0x34);

                    PTR_AT(int32_t, arg8, 0x74) = 0;
                    PTR_AT(int32_t, arg8, 0x70) = 0;
                    PTR_AT(int32_t, arg8, 0x68) = v0_17;
                    PTR_AT(int32_t, arg8, 0x64) = v0_17;
                    PTR_AT(int32_t, arg8, 0x6c) = MinsOp(a0_20 - 1, a1_10);
                    return PTR_AT(int32_t, arg8, 0x64);
                }
            }
        }
    }

    return remainingInputRows((void *)(intptr_t)__assert(
        "filter->roi.x >= 0 && filter->roi.y >= 0 && filter->roi.width >= 0 && filter->roi.height >= 0 && filter->roi.x + filter->roi.width <= filter->wholeSize.width &&filter->roi.y + filter->roi.height <= filter->wholeSize.height",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0x9e, "Start", var_40, var_3c, var_38,
        var_34, var_30));
}

int32_t remainingInputRows(void *arg1)
{
    if (arg1 != NULL) {
        return PTR_AT(int32_t, arg1, 0x6c) - PTR_AT(int32_t, arg1, 0x64) - PTR_AT(int32_t, arg1, 0x70);
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "MOVEFILTER",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0xee, "remainingInputRows",
        "null pointer filter\n", &_gp);
    return -1;
}

int32_t remainingOutputRows(void *arg1)
{
    if (arg1 != NULL) {
        return PTR_AT(int32_t, arg1, 0x50) - PTR_AT(int32_t, arg1, 0x74);
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "MOVEFILTER",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0xf7, "remainingOutputRows",
        "null pointer filter\n", &_gp);
    return -1;
}

int32_t Proceed(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, void *arg5, int32_t arg6)
{
    void *var_98 = &_gp;
    int32_t arg_4 = arg2;
    int32_t a3 = 0;
    int32_t arg_c = a3;
    int32_t v0_2;
    int32_t var_78_1;
    int32_t var_74_1;
    int32_t var_48_1;
    int32_t var_3c_1;
    int32_t var_38_1;
    int32_t var_34_1;
    int32_t var_80_1;
    int32_t v0_7;
    int32_t v1_8;
    int32_t v0_8;
    int32_t v0_10;
    void *var_7c_1;
    int32_t var_4c_1;

    (void)var_98;
    (void)arg_4;
    (void)arg_c;

    if (PTR_AT(int32_t, arg5, 0x3c) <= 0 || PTR_AT(int32_t, arg5, 0x40) <= 0) {
        __assert("filter->wholeSize.width > 0 && filter->wholeSize.height > 0",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0x108, "Proceed");
    } else {
        v0_2 = PTR_AT(int32_t, arg5, 0x4c);
        var_78_1 = v0_2;
        var_74_1 = v0_2 + PTR_AT(int32_t, arg5, 0x28) - 1;
        var_48_1 = PTR_AT(int32_t, arg5, 0x88);
        var_3c_1 = PTR_AT(int32_t, arg5, 0x2c);
        var_38_1 = PTR_AT(int32_t, arg5, 0x34);
        var_34_1 = PTR_AT(int32_t, arg5, 0x54);
        var_80_1 = PTR_AT(int32_t, arg5, 0x58);
        v0_7 = -MinsOp(PTR_AT(int32_t, arg5, 0x44), PTR_AT(int32_t, arg5, 0x30));
        v1_8 = arg1 + v0_7;
        v0_8 = arg3 + v0_7;
        v0_10 = MinsOp(PTR_AT(int32_t, arg5, 0x6c) - PTR_AT(int32_t, arg5, 0x64), remainingInputRows(arg5));
        var_7c_1 = (uint8_t *)arg5 + 0x78;
        var_4c_1 = v0_10;

        (void)var_78_1;
        (void)var_74_1;
        (void)var_48_1;
        (void)var_3c_1;
        (void)var_38_1;
        (void)var_34_1;
        (void)var_80_1;
        (void)var_7c_1;
        (void)var_4c_1;

        if (v1_8 != 0 && v0_8 != 0 && arg4 != 0 && v0_10 > 0) {
            if (arg6 == 0) {
                return sub_dc144(arg6, arg5, 0, 0, 0, NULL, NULL, 0, 0, 0, 0, 0);
            }
        }
    }

    freeFilterEngine((void *)(intptr_t)__assert("src1 && src2 && dst && filter && count > 0",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0x11d, "Proceed"));
    return 0;
}

int32_t sub_dc5a8(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t *arg5, void *arg6, int32_t arg7,
    int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11, int32_t arg12)
{
    int32_t a0_5 = 0;
    int32_t hi = 0;

    do {
        int32_t v0_3 = CalcSum(arg8 + PTR_AT(int32_t, arg6, 0x74) + arg4 + PTR_AT(int32_t, arg6, 0x48) - arg1);
        int32_t v1_1 = PTR_AT(int32_t, arg6, 0x64);

        if (v0_3 < v1_1) {
            __assert("sumRows >= filter->startY", "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c",
                0x16f, "sub_dc5a8");
        }

        a0_5 = PTR_AT(int32_t, arg6, 0x70);
        arg5 += 1;
        if (v0_3 >= v1_1 + a0_5) {
            break;
        }

        {
            int32_t v0_7 = v0_3 - PTR_AT(int32_t, arg6, 0x68);
            hi = v0_7 / 6;
            a0_5 = (((intptr_t)PTR_AT(void *, arg6, 0x80)) + 0xf) & arg7;
            arg4 += 1;
            arg5[-1] = (v0_7 - hi * 6) * PTR_AT(int32_t, arg6, 0x60) + a0_5;
        }
    } while (arg3 != arg4);

    return sub_dc604(arg9, arg2, arg12, arg11, arg6, arg4, hi, a0_5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0);
}

int32_t sub_dc604(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, void *arg5, int32_t arg6, int32_t arg7,
    int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11, int32_t arg12, int32_t arg13, int32_t arg14,
    int32_t arg15, int32_t arg16, int32_t arg17, int32_t arg18, int32_t arg19, int32_t arg20, int32_t arg21,
    int32_t arg22, int32_t arg23, int32_t arg24, int32_t arg25, int32_t arg26, int32_t arg27, int32_t arg28,
    int32_t arg29, int32_t arg30, int32_t arg31)
{
    int32_t v0_17;

    while (1) {
        if (arg6 < arg16) {
            int32_t s7_1 = arg6 - arg15;

            ((int32_t (*)(int32_t, int32_t, int32_t, int32_t))PTR_AT(void *, arg5, 0x90))(arg14, arg30, arg31,
                s7_1);
            arg10 += s7_1;
            arg30 += s7_1 * arg31;
            arg7 = 0;
        }

        {
            int32_t a0_13 = arg19 - PTR_AT(int32_t, arg5, 0x64) - PTR_AT(int32_t, arg5, 0x70) +
                PTR_AT(int32_t, arg5, 0x48);

            if (0 >= a0_13) {
                a0_13 = arg20;
            }

            v0_17 = CalcSum(a0_13);
            arg13 -= v0_17;
            if (v0_17 > 0) {
                break;
            }
        }

        {
            int32_t v0_27 = CalcSum(PTR_AT(int32_t, arg5, 0x50) - (arg10 + PTR_AT(int32_t, arg5, 0x74)) + arg15);

            if (v0_27 > 0) {
                return sub_dc5a8(arg17, arg2, v0_27, 0, (int32_t *)(intptr_t)arg14, arg5, 0xfffffff0, arg10, arg1,
                    arg16, arg17, arg18);
            }
        }

        {
            int32_t t7_3 = PTR_AT(int32_t, arg5, 0x64);
            int32_t a3_9 = PTR_AT(int32_t, arg5, 0x70);
            int32_t hi = (t7_3 - PTR_AT(int32_t, arg5, 0x68) + a3_9) / arg2;
            void *a0_14 = PTR_AT(void *, arg5, 0x84);

            if (a3_9 + 1 >= 7) {
                PTR_AT(int32_t, arg5, 0x64) = t7_3 + 1;
            } else {
                PTR_AT(int32_t, arg5, 0x70) = a3_9 + 1;
            }

            if (arg3 != 0) {
                if (arg1 > 0) {
                    break;
                }
            } else if (arg4 >= 4) {
                return sub_dc2d4(a0_14, arg9, arg4, arg2, (char *)(intptr_t)arg11, arg18, v0_17 - 1, arg5,
                    (char *)(intptr_t)arg12, arg3, arg1, hi, 0, 0, NULL, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                    0, 0);
            }

            break;
        }
    }

    {
        int32_t v0_9 = arg10 + PTR_AT(int32_t, arg5, 0x74);
        int32_t v1_4 = PTR_AT(int32_t, arg5, 0x50) < v0_9;

        PTR_AT(int32_t, arg5, 0x74) = v0_9;
        if (v1_4 == 0) {
            return arg10;
        }
    }

    __assert("filter->columnCount <= filter->roi.height",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0x186, "sub_dc604");
    __assert("filter->wholeSize.width > 0 && filter->wholeSize.height > 0",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0x108, "Proceed");
    freeFilterEngine((void *)(intptr_t)__assert("src1 && src2 && dst && filter && count > 0",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0x11d, "Proceed"));
    return 0;
}

void freeFilterEngine(void *arg1)
{
    if (arg1 == NULL) {
        return;
    }

    if (PTR_AT(void *, arg1, 9 * sizeof(void *)) != NULL) {
        free(PTR_AT(void *, arg1, 9 * sizeof(void *)));
    }
    PTR_AT(void *, arg1, 9 * sizeof(void *)) = NULL;

    if (PTR_AT(void *, arg1, 0) != NULL) {
        free(PTR_AT(void *, arg1, 0));
    }
    PTR_AT(void *, arg1, 0) = NULL;

    if (PTR_AT(void *, arg1, 0x22 * sizeof(void *)) != NULL) {
        free(PTR_AT(void *, arg1, 0x22 * sizeof(void *)));
    }
    PTR_AT(void *, arg1, 0x22 * sizeof(void *)) = NULL;

    if (PTR_AT(void *, arg1, 0x21 * sizeof(void *)) != NULL) {
        free(PTR_AT(void *, arg1, 0x21 * sizeof(void *)));
    }
    PTR_AT(void *, arg1, 0x21 * sizeof(void *)) = NULL;

    if (PTR_AT(void *, arg1, 0x20 * sizeof(void *)) != NULL) {
        free(PTR_AT(void *, arg1, 0x20 * sizeof(void *)));
    }

    free(arg1);
}

int32_t updateRoiMask(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t *arg6)
{
    int32_t s4 = PTR_AT(int32_t, arg1, 0x18);

    if (s4 <= 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Outside Error",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0x1ae, "updateRoiMask",
            "Bad paramter of roi!! Please set it bigger than 0!\n");
        return -1;
    }

    if (PTR_AT(void *, arg1, 0x24) != NULL) {
        free(PTR_AT(void *, arg1, 0x24));
    }

    s4 = PTR_AT(int32_t, arg1, 0x18);
    {
        int32_t v1_1 = s4 + 7;
        int32_t v0_3;
        int32_t s5_1;
        int32_t s3_1;
        size_t nitems;
        void *v0_4;

        if (s4 >= 0) {
            v1_1 = s4;
        }

        v0_3 = ((0 < (s4 & 7)) ? 1 : 0) + (v1_1 >> 3);
        s5_1 = arg5 * v0_3;
        s3_1 = arg4 * v0_3 + s5_1;
        nitems = (size_t)((s4 << 2) + s3_1);
        PTR_AT(size_t, arg1, 0x0c) = nitems;
        v0_4 = calloc(nitems, 1);
        PTR_AT(void *, arg1, 0x24) = v0_4;
        if (v0_4 == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "MOVEFILTER",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0x1be, "updateRoiMask",
                "%s(%d):malloc MaskData error\n", "updateRoiMask", 0x1be, &_gp);
            return -1;
        }

        PTR_AT(void *, arg1, 0x04) = v0_4;
        PTR_AT(void *, arg1, 0x08) = (uint8_t *)v0_4 + s5_1;
        PTR_AT(void *, arg1, 0x20) = (uint8_t *)v0_4 + s3_1;
        PTR_AT(int32_t, arg1, 0x14) = arg4;
        PTR_AT(int32_t, arg1, 0x10) = arg5;
        PTR_AT(int32_t, arg1, 0x1c) = 0;

        if (s4 > 0) {
            int32_t *t2_1 = arg6;
            int32_t i = 0;

            do {
                int32_t t6_1 = t2_1[0];
                int32_t a2 = t2_1[1];
                int32_t t0_1 = t2_1[2];
                int32_t a3 = t2_1[3];

                if (arg4 > 0) {
                    uint32_t t7_2 = ((uint32_t)(i >> 31)) >> 29;
                    int32_t v0_7 = arg2;
                    int32_t a0_2 = 0;

                    do {
                        if (v0_7 >= t6_1 && t0_1 >= v0_7) {
                            uint8_t *v1_7 = (uint8_t *)PTR_AT(void *, arg1, 0x08) + ((i >> 3) * PTR_AT(int32_t, arg1, 0x14)) + a0_2;
                            *v1_7 |= (uint8_t)(1U << ((((i + (int32_t)t7_2) & 7) - (int32_t)t7_2) & 0x1f));
                        }
                        a0_2 += 1;
                        v0_7 += 1;
                    } while (arg4 != a0_2);
                }

                if (arg5 > 0) {
                    uint32_t t0_3 = ((uint32_t)(i >> 31)) >> 29;
                    int32_t v0_10 = arg3;
                    int32_t a0_3 = 0;

                    do {
                        if (v0_10 >= a2 && a3 >= v0_10) {
                            uint8_t *v1_11 = (uint8_t *)PTR_AT(void *, arg1, 0x04) + ((i >> 3) * PTR_AT(int32_t, arg1, 0x10)) + a0_3;
                            *v1_11 |= (uint8_t)(1U << ((((i + (int32_t)t0_3) & 7) - (int32_t)t0_3) & 0x1f));
                        }
                        a0_3 += 1;
                        v0_10 += 1;
                    } while (arg5 != a0_3);
                }

                i += 1;
                t2_1 = &t2_1[4];
            } while (i < PTR_AT(int32_t, arg1, 0x18));
        }
    }

    return 0;
}

void *Init(int32_t arg1, int32_t arg2, int32_t arg3, int32_t *arg4)
{
    void *var_28 = &_gp;
    void **result = (void **)malloc(0x94);

    (void)var_28;

    if (result == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "MOVEFILTER",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0x4b, "Init", "malloc error\n");
        return NULL;
    }

    result[0xc] = (void *)1;
    result[0xd] = (void *)1;
    result[0x23] = (void *)MorphRowFilter;
    ((uint8_t *)&result[0x17])[0] = 0x14;
    ((uint8_t *)result)[0x5d] = 1;
    result[0xa] = (void *)3;
    result[0xb] = (void *)3;
    result[0xe] = 0;
    result[0x18] = 0;
    result[0x20] = 0;
    result[0x21] = 0;
    result[0x22] = 0;
    result[9] = 0;
    result[8] = 0;
    result[0] = NULL;
    result[0x24] = (void *)MorphColumnFilter;

    {
        int32_t a2 = arg4[0];
        int32_t a3 = arg4[1];
        int32_t a0 = arg4[2];
        int32_t v1 = arg4[3];

        if (arg3 >= 2) {
            int32_t *i = &arg4[4];

            do {
                int32_t t2_1 = i[0];
                int32_t t1_1 = i[1];
                int32_t t0_1 = i[2];
                int32_t a1_1 = i[3];

                i = &i[4];
                if (t2_1 < a2) {
                    a2 = t2_1;
                }
                if (t1_1 < a3) {
                    a3 = t1_1;
                }
                if (a0 < t0_1) {
                    a0 = t0_1;
                }
                if (v1 < a1_1) {
                    v1 = a1_1;
                }
            } while (&arg4[arg3 * 4] != i);
        }

        result[0x11] = (void *)(intptr_t)a2;
        result[0x12] = (void *)(intptr_t)a3;
        result[0x13] = (void *)(intptr_t)(a0 - a2 + 1);
        result[0x14] = (void *)(intptr_t)(v1 - a3 + 1);

        if (Start(arg1, arg2, a2, a3, a0 - a2 + 1, v1 - a3 + 1, -1, result) < 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "MOVEFILTER",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0x71, "Init", "%s:start failed\n",
                "Init");
            free(result);
            return NULL;
        }

        result[0] = malloc((size_t)(arg3 << 2));
        if (result[0] == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "MOVEFILTER",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0x77, "Init",
                "malloc Noise_Thresh failed\n");
            free(result[0x20]);
            free(result[0x21]);
            free(result[0x22]);
            free(result);
            return NULL;
        }

        result[6] = (void *)(intptr_t)arg3;
        if (updateRoiMask(result, a2, a3, a0 - a2 + 1, v1 - a3 + 1, arg4) >= 0) {
            return result;
        }

        imp_log_fun(6, IMP_Log_Get_Option(), 2, "MOVEFILTER",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/filter.c", 0x7c, "Init",
            "updateRoiMask failed\n");
        free(result[0]);
        free(result[0x20]);
        free(result[0x21]);
        free(result[0x22]);
        free(result);
        return NULL;
    }
}

int32_t sub_dcf90(int32_t arg1, int32_t arg2, int32_t *arg3, int32_t arg4, int32_t arg5)
{
    *arg3 = arg5 + arg4;
    return sub_dcf40(arg1, arg2, (int32_t)(intptr_t)arg3, 0);
}

int32_t sub_dd0f8(char *arg1, char *arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7,
    int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11, int32_t arg12, int32_t arg13, int32_t arg14,
    int32_t arg15)
{
    char result;
    int32_t entry_gp = arg6;

    (void)arg7;
    (void)arg8;
    (void)arg9;
    (void)arg10;
    (void)arg11;
    (void)arg12;
    (void)arg13;
    (void)arg14;
    (void)arg15;

    do {
        int32_t v0_1 = ((int32_t (*)(uint32_t, uint32_t))(intptr_t)(entry_gp - 0x6e80))((uint32_t)(uint8_t)arg1[0],
            (uint32_t)(uint8_t)arg1[1]);
        uint32_t a1_2 = (uint32_t)(uint8_t)arg1[2];

        arg1 = &arg1[arg3];
        result = (char)((int32_t (*)(int32_t, uint32_t))(intptr_t)(arg6 - 0x6e80))(v0_1, a1_2);
        entry_gp = arg6;
        *arg2 = result;
        arg2 = &arg2[arg3];
    } while (arg1 - (char *)(intptr_t)arg5 < arg4);

    return result;
}
