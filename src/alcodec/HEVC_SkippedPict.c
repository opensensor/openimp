#include <stdint.h>

#include "alcodec/BitStreamLite.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, ...);

typedef struct AL_CabacCtx {
    int32_t ivlOffset;
    int32_t ivlCurrRange;
    int32_t bitsOutstanding;
    int32_t firstBitFlag;
} AL_CabacCtx;

static const uint8_t AL_TabValMPS[3] = {0x01, 0x00, 0x00};
static const uint8_t AL_TabState[3] = {0x06, 0x03, 0x00};
static const uint8_t AL_HEVC_SkippedPictureState[5] = {0x10, 0x0f, 0x08, 0x10, 0x10};
static const uint8_t AL_HEVC_SkippedPictureMPS[5] = {0x00, 0x00, 0x01, 0x01, 0x00};

int32_t AL_Cabac_Init(AL_CabacCtx *arg1);
int32_t AL_Cabac_WriteBin(AL_BitStreamLite *arg1, AL_CabacCtx *arg2, uint8_t *arg3, uint8_t *arg4, uint8_t arg5);
int32_t AL_Cabac_Terminate(AL_BitStreamLite *arg1, AL_CabacCtx *arg2, int32_t arg3);
int32_t AL_Cabac_Finish(AL_BitStreamLite *arg1, AL_CabacCtx *arg2);

/* forward decl, ported by T<N> later */
int32_t AL_Codec_Encode_ValidateGopParam(void *arg1, int32_t *arg2);

static int32_t AL_AVC_GenerateSkippedPictureCavlc(AL_BitStreamLite *arg1, int32_t arg2)
{
    void *var_10 = &_gp;

    (void)var_10;
    AL_BitStreamLite_PutUE(arg1, arg2);
    AL_BitStreamLite_PutU(arg1, 1, 1);
    return AL_BitStreamLite_AlignWithBits(arg1, 0);
}

static int32_t AL_AVC_GenerateSkippedPictureCabac(AL_BitStreamLite *arg1, int32_t arg2, int32_t arg3)
{
    char v0_1;
    void *var_48 = &_gp;
    char var_2f;
    char var_30;
    AL_CabacCtx var_40;
    int32_t result;

    (void)var_48;
    v0_1 = (char)AL_TabValMPS[arg2];
    var_2f = (char)AL_TabState[arg2];
    var_30 = v0_1;
    AL_Cabac_Init(&var_40);

    if (arg3 <= 0) {
        result = 0;
    } else {
        int32_t s0_1 = 1;

        do {
            AL_Cabac_WriteBin(arg1, &var_40, (uint8_t *)&var_2f, (uint8_t *)&var_30, 1);
            AL_Cabac_Terminate(arg1, &var_40, ((uint32_t)(arg3 ^ s0_1) < 1U) ? 1 : 0);
            result = s0_1 << 1;
            s0_1 += 1;
        } while (arg3 >= s0_1);
    }

    AL_Cabac_Finish(arg1, &var_40);
    return result;
}

int32_t AL_AVC_GenerateSkippedPicture(void *arg1, int32_t arg2, char arg3, int32_t arg4)
{
    if (arg1 != 0) {
        /* offset 0x4 */
        int32_t v0_1 = *(int32_t *)((char *)arg1 + 0x4);

        if (v0_1 != 0) {
            void *var_38 = &_gp;
            int32_t s5 = 0;
            AL_BitStreamLite var_30;

            (void)var_38;
            /* offset 0x8 */
            AL_BitStreamLite_Init(&var_30, (uint8_t *)(intptr_t)v0_1, *(int32_t *)((char *)arg1 + 0x8));

            if ((uint8_t)arg3 != 0)
                s5 = AL_AVC_GenerateSkippedPictureCabac(&var_30, arg4, arg2);
            else
                AL_AVC_GenerateSkippedPictureCavlc(&var_30, arg2);

            /* offset 0xc */
            *(int32_t *)((char *)arg1 + 0xc) = AL_BitStreamLite_GetBitsCount(&var_30);
            /* offset 0x10 */
            *(int32_t *)((char *)arg1 + 0x10) = s5;
            /* offset 0x14 */
            *(int32_t *)((char *)arg1 + 0x14) = 0;
            AL_BitStreamLite_Deinit(&var_30);
            return 1;
        }
    }

    return 0;
}

static int32_t AL_sHEVC_WriteSkippedCU(AL_BitStreamLite *arg1, AL_CabacCtx *arg2, uint8_t *arg3, uint8_t *arg4,
                                       int32_t arg5, int32_t arg6, int32_t arg7)
{
    void *var_20 = &_gp;
    int32_t result = 2;

    (void)var_20;

    if (arg7 == 0) {
        int32_t var_28_1 = 0;

        (void)var_28_1;
        result = 3;
        AL_Cabac_WriteBin(arg1, arg2, arg3, arg4, 0);
    }

    {
        int32_t a2 = 2;
        int32_t a2_1;

        if (arg5 == 0)
            a2 = 1;

        a2_1 = a2 + (arg6 > 0 ? 1 : 0);
        AL_Cabac_WriteBin(arg1, arg2, arg3 + a2_1, arg4 + a2_1, 1);
        AL_Cabac_WriteBin(arg1, arg2, arg3 + 4, arg4 + 4, 0);
    }

    return result;
}

static int32_t AL_sHEVC_WriteSkippedCuRight(AL_BitStreamLite *arg1, AL_CabacCtx *arg2, uint8_t *arg3, uint8_t *arg4,
                                            int32_t arg5, int32_t arg6, int32_t arg7)
{
    int32_t i = arg7;
    void *var_48 = &_gp;
    int32_t t0 = arg5;
    int32_t s6 = 0;

    (void)var_48;

    while (i != 1) {
        int32_t v0_2 = (i - 1) & arg6;
        int32_t v0_10;

        if ((arg6 & i) != 0) {
            int32_t v0_4 = AL_sHEVC_WriteSkippedCU(arg1, arg2, arg3, arg4, 1, t0, 0);

            if (((i - 1) & arg6) == 0)
                return v0_4 + AL_sHEVC_WriteSkippedCU(arg1, arg2, arg3, arg4, 1, 1, 0) + s6;

            {
                int32_t i_2 = i >> 1;

                v0_10 = v0_4 + AL_sHEVC_WriteSkippedCuRight(arg1, arg2, arg3, arg4, t0, arg6, i_2)
                      + AL_sHEVC_WriteSkippedCU(arg1, arg2, arg3, arg4, 1, 1, 0);
                i = i_2;
            }
        } else {
            int32_t i_1 = i >> 1;

            if (v0_2 == 0)
                return v0_2 + s6;

            v0_10 = AL_sHEVC_WriteSkippedCuRight(arg1, arg2, arg3, arg4, t0, arg6, i_1);
            i = i_1;
        }

        s6 += v0_10;
        t0 = 1;
    }

    return AL_sHEVC_WriteSkippedCU(arg1, arg2, arg3, arg4, 1, t0, 1)
         + AL_sHEVC_WriteSkippedCU(arg1, arg2, arg3, arg4, 1, 1, 1) + s6;
}

static int32_t AL_sHEVC_WriteSkippedCuBottom(AL_BitStreamLite *arg1, AL_CabacCtx *arg2, uint8_t *arg3, uint8_t *arg4,
                                             int32_t arg5, int32_t arg6, int32_t arg7)
{
    int32_t i = arg7;
    void *var_40 = &_gp;
    int32_t t2 = arg5;
    int32_t s1 = 0;

    (void)var_40;

    while (i != 1) {
        int32_t fp_1 = 0;
        AL_BitStreamLite *a0;
        AL_CabacCtx *a1;
        uint8_t *a2;
        uint8_t *a3;

        if ((arg6 & i) == 0) {
            int32_t v1_2 = (i - 1) & arg6;

            a3 = arg4;
            a2 = arg3;
            a1 = arg2;
            a0 = arg1;
            i >>= 1;

            if (v1_2 == 0)
                return fp_1 + s1;
        } else {
            int32_t v1_4 = (i - 1) & arg6;

            fp_1 = AL_sHEVC_WriteSkippedCU(arg1, arg2, arg3, arg4, t2, 1, 0)
                 + AL_sHEVC_WriteSkippedCU(arg1, arg2, arg3, arg4, 1, 1, 0);
            a3 = arg4;
            a2 = arg3;
            a1 = arg2;
            a0 = arg1;
            i >>= 1;

            if (v1_4 == 0)
                return fp_1 + s1;
        }

        s1 += fp_1 + AL_sHEVC_WriteSkippedCuBottom(a0, a1, a2, a3, t2, arg6, i);
        t2 = 1;
    }

    return AL_sHEVC_WriteSkippedCU(arg1, arg2, arg3, arg4, t2, 1, 1)
         + AL_sHEVC_WriteSkippedCU(arg1, arg2, arg3, arg4, 1, 1, 1) + s1;
}

int32_t AL_HEVC_GenerateSkippedPicture(void *arg1, uint32_t arg2, uint32_t arg3, char arg4, char arg5, int32_t arg6,
                                       int32_t arg7, int16_t *arg8, int16_t *arg9)
{
    uint32_t v0 = (uint8_t)arg4;
    uint32_t v0_1 = (uint8_t)arg5;
    void *var_e8 = &_gp;
    uint32_t arg_8 = arg3;

    (void)var_e8;

    if (arg1 != 0) {
        /* offset 0x4 */
        int32_t a1 = *(int32_t *)((char *)arg1 + 0x4);

        if (a1 != 0) {
            AL_BitStreamLite var_d0;
            int32_t v0_7;
            int32_t s1_2;

            /* offset 0x8 */
            AL_BitStreamLite_Init(&var_d0, (uint8_t *)(intptr_t)a1, *(int32_t *)((char *)arg1 + 0x8));

            if (arg7 <= 0) {
                s1_2 = 0;
                v0_7 = 0;
            } else {
                int32_t v1_1 = (int32_t)(1U << (v0 & 0x1f));
                int32_t v0_5 = ((int32_t)(1U << ((v0 - v0_1) & 0x1f))) >> 1;
                int32_t var_30_1 = 0;
                int16_t *var_70_1 = arg9;
                int32_t var_84_1 = 0;
                int32_t var_78_1 = 0;
                int32_t v1_2 = arg6;

                s1_2 = 0;
                v0_7 = 0;

                while (1) {
                    if (v1_2 > 0) {
                        /* offset 0x18 from arg1 base (6 * 4) plus var_84_1 * 4 */
                        int32_t *var_7c_1 = (int32_t *)((char *)arg1 + ((var_84_1 + 6) << 2));
                        int32_t var_90_1 = 0;
                        int16_t *var_8c_1 = arg8;
                        uint8_t var_c0[5];
                        uint8_t *var_a4_1 = var_c0;
                        uint8_t var_b8[5];
                        uint8_t *var_a8_1 = var_b8;
                        uint32_t var_94_1 = arg2;
                        int32_t var_f4 = 0;
                        uint32_t var_f8 = 0;
                        int32_t var_f0 = 0;

                        while (1) {
                            uint32_t a1_1 = var_94_1;
                            uint32_t s2_1 = (uint16_t)(*var_8c_1);
                            uint32_t a0_5 = s2_1 << (v0 & 0x1f);
                            uint32_t v1_6 = (uint16_t)(*var_70_1);
                            int32_t fp_1;

                            if ((int32_t)a1_1 >= (int32_t)a0_5)
                                a1_1 = a0_5;

                            {
                                uint32_t v0_21 = v1_6 << (v0 & 0x1f);
                                uint32_t a0_6 = arg_8;
                                int32_t s6_1 = ((uint32_t)((arg7 - 1) ^ var_30_1) < 1U) ? 1 : 0;
                                int32_t s2_2 = (int32_t)(s2_1 * v1_6);
                                AL_CabacCtx var_e0;

                                if (arg6 - 1 != var_90_1)
                                    s6_1 = 0;

                                if ((int32_t)arg_8 >= (int32_t)v0_21)
                                    a0_6 = v0_21;

                                AL_Cabac_Init(&var_e0);
                                var_c0[0] = AL_HEVC_SkippedPictureMPS[0];
                                var_c0[1] = AL_HEVC_SkippedPictureMPS[1];
                                var_c0[2] = AL_HEVC_SkippedPictureMPS[2];
                                var_c0[3] = AL_HEVC_SkippedPictureMPS[3];
                                var_b8[0] = AL_HEVC_SkippedPictureState[0];
                                var_b8[1] = AL_HEVC_SkippedPictureState[1];
                                var_b8[2] = AL_HEVC_SkippedPictureState[2];
                                var_b8[3] = AL_HEVC_SkippedPictureState[3];
                                var_b8[4] = AL_HEVC_SkippedPictureState[4];
                                var_c0[4] = AL_HEVC_SkippedPictureMPS[4];

                                if (v1_1 == 0)
                                    __builtin_trap();

                                {
                                    int32_t s0_3 = (int32_t)(a1_1 + (uint32_t)v1_1 - 1) >> (v0 & 0x1f);
                                    int32_t s5_2 = (int32_t)a0_6 >> (v0 & 0x1f);
                                    int32_t v1_9 = ((int32_t)a1_1 % v1_1) >> (v0_1 & 0x1f);

                                    if (v1_1 == 0)
                                        __builtin_trap();

                                    {
                                        int32_t v0_27 = ((int32_t)a0_6 % v1_1) >> (v0_1 & 0x1f);

                                        if (s2_2 == 0) {
                                            s1_2 = 0;
                                        } else {
                                            int32_t v0_29 = s2_2 - 1;
                                            int32_t s4_4 = 0;

                                            s1_2 = 0;

                                            while (1) {
                                                uint32_t lo_3;
                                                uint32_t hi_3;

                                                if (s0_3 == 0)
                                                    __builtin_trap();

                                                lo_3 = (uint32_t)s4_4 / (uint32_t)s0_3;
                                                hi_3 = (uint32_t)s4_4 % (uint32_t)s0_3;

                                                if ((int32_t)hi_3 == s0_3 - 1 && v1_9 != 0) {
                                                    int32_t v1_28 = v0_5;

                                                    if (s5_2 == (int32_t)lo_3) {
                                                        if (v0_27 == 0)
                                                            goto label_87b28;

                                                        {
                                                            int32_t fp_2 = 0;

                                                            if (v1_28 != 1) {
                                                                int32_t s4_5 = v1_28;
                                                                int32_t s5_3 = 0;

                                                                while (1) {
                                                                    int32_t s0_4 = s4_5 - 1;
                                                                    int32_t t0_1;
                                                                    int32_t v0_49;
                                                                    int32_t fp_3;

                                                                    if ((v0_27 & s4_5) == 0) {
                                                                        fp_3 = 0;
                                                                        v0_49 = v0_27 & s0_4;
label_87c30:
                                                                        t0_1 = fp_3;
                                                                        if (v0_49 != 0) {
label_87c38:
                                                                            if ((v1_9 & s4_5) != 0) {
                                                                                var_f0 = s4_5 >> 1;
                                                                                var_f8 = 1;
                                                                                var_f4 = v0_27;
                                                                                fp_3 += AL_sHEVC_WriteSkippedCuBottom(
                                                                                    &var_d0, &var_e0,
                                                                                    (uint8_t *)var_a8_1,
                                                                                    (uint8_t *)var_a4_1, 1, var_f4,
                                                                                    var_f0);
                                                                            }

                                                                            if ((v1_9 & s0_4) == 0)
                                                                                t0_1 = fp_3;
                                                                        }
                                                                    } else {
                                                                        fp_3 = 0;

                                                                        if ((v1_9 & s4_5) != 0) {
                                                                            var_f4 = 1;
                                                                            var_f8 = 1;
                                                                            var_f0 = 0;
                                                                            fp_3 = AL_sHEVC_WriteSkippedCU(
                                                                                &var_d0, &var_e0,
                                                                                (uint8_t *)var_a8_1,
                                                                                (uint8_t *)var_a4_1, 1, 1, 0);
                                                                        }

                                                                        s0_4 = s4_5 - 1;
                                                                        v0_49 = v0_27 & s0_4;

                                                                        if ((v1_9 & s0_4) == 0)
                                                                            goto label_87c30;

                                                                        var_f0 = s4_5 >> 1;
                                                                        var_f8 = 1;
                                                                        var_f4 = v1_9;
                                                                        fp_3 += AL_sHEVC_WriteSkippedCuRight(
                                                                            &var_d0, &var_e0, (uint8_t *)var_a8_1,
                                                                            (uint8_t *)var_a4_1, 1, var_f4,
                                                                            var_f0);

                                                                        if ((v0_27 & s0_4) != 0)
                                                                            goto label_87c38;

                                                                        t0_1 = fp_3;
                                                                    }

                                                                    fp_1 = t0_1 + s5_3 + s1_2;
                                                                    s4_5 >>= 1;
                                                                    s5_3 += fp_3;
                                                                    if (s4_5 != 1)
                                                                        continue;

                                                                    fp_2 = s5_3;
                                                                    break;
                                                                }
                                                            }

                                                            var_f0 = 1;
                                                            var_f4 = 1;
                                                            var_f8 = 1;
                                                            fp_1 = AL_sHEVC_WriteSkippedCU(
                                                                       &var_d0, &var_e0, (uint8_t *)var_a8_1,
                                                                       (uint8_t *)var_a4_1, 1, 1, 1)
                                                                 + fp_2 + s1_2;
                                                        }
                                                    } else {
label_87b28:
                                                        var_f0 = v1_28;
                                                        var_f8 = (((uint32_t)s4_4 < (uint32_t)s0_3) ? 1U : 0U) ^ 1U;
                                                        var_f4 = v1_9;
                                                        fp_1 = AL_sHEVC_WriteSkippedCuRight(
                                                                   &var_d0, &var_e0, (uint8_t *)var_a8_1,
                                                                   (uint8_t *)var_a4_1, (int32_t)var_f8, var_f4,
                                                                   var_f0)
                                                             + s1_2;
                                                    }

                                                    if (s6_1 != 0) {
                                                        AL_Cabac_Terminate(&var_d0, &var_e0,
                                                                           ((uint32_t)(s4_4 ^ v0_29) < 1U) ? 1 : 0);
                                                        s1_2 = fp_1 + 1;
                                                    } else {
label_87b6c:
                                                        AL_Cabac_Terminate(&var_d0, &var_e0, 0);
                                                        s1_2 = fp_1 + 1;
                                                        if (s4_4 == v0_29) {
                                                            AL_Cabac_Terminate(&var_d0, &var_e0, 1);
                                                            s1_2 = fp_1 + 2;
                                                        }
                                                    }
                                                } else {
                                                    if (s5_2 == (int32_t)lo_3 && v0_27 != 0) {
                                                        var_f0 = v0_5;
                                                        var_f8 = hi_3;
                                                        var_f4 = v0_27;
                                                        fp_1 = AL_sHEVC_WriteSkippedCuBottom(&var_d0, &var_e0,
                                                                                            (uint8_t *)var_a8_1,
                                                                                            (uint8_t *)var_a4_1,
                                                                                            (int32_t)var_f8, var_f4,
                                                                                            var_f0)
                                                             + s1_2;
                                                    } else {
                                                        int32_t v0_31 = ((uint32_t)s4_4 < (uint32_t)s0_3) ? 1 : 0;

                                                        if (s5_2 == (int32_t)lo_3)
                                                            v0_31 = ((uint32_t)s4_4 < (uint32_t)s0_3) ? 1 : 0;

                                                        var_f8 = (uint32_t)(v0_31 ^ 1);
                                                        var_f4 = (int32_t)hi_3;
                                                        var_f0 = 0;
                                                        fp_1 = AL_sHEVC_WriteSkippedCU(&var_d0, &var_e0,
                                                                                      (uint8_t *)var_a8_1,
                                                                                      (uint8_t *)var_a4_1,
                                                                                      (int32_t)var_f8, var_f4, 0)
                                                             + s1_2;
                                                    }

                                                    if (s6_1 == 0)
                                                        goto label_87b6c;

                                                    AL_Cabac_Terminate(&var_d0, &var_e0,
                                                                       ((uint32_t)(s4_4 ^ v0_29) < 1U) ? 1 : 0);
                                                    s1_2 = fp_1 + 1;
                                                }

                                                s4_4 += 1;
                                                if (s2_2 == s4_4)
                                                    break;
                                            }
                                        }
                                    }
                                }

                                AL_Cabac_Finish(&var_d0, &var_e0);
                            }

                            v0_7 = AL_BitStreamLite_GetBitsCount(&var_d0);

                            {
                                int32_t v1_12 = v0_7 - var_78_1;

                                if ((v1_12 & 7) != 0) {
                                    void *a0_35;
                                    int32_t *a1_20;

                                    a0_35 = (void *)(intptr_t)__assert(
                                        "((iBitsCount - iPrevBitsCount) % 8) == 0",
                                        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/HEVC_SkippedPict.c",
                                        0xd6, "AL_HEVC_GenerateSkippedPicture", var_f8, var_f4, var_f0);
                                    return AL_Codec_Encode_ValidateGopParam(a0_35, a1_20);
                                }

                                if (v1_12 < 0)
                                    v1_12 += 7;

                                {
                                    uint32_t a1_7 = (uint16_t)(*var_8c_1);

                                    *var_7c_1 = v1_12 >> 3;
                                    var_78_1 = v0_7;
                                    var_84_1 += 1;
                                    var_94_1 -= a1_7 << (v0 & 0x1f);
                                    var_7c_1 = &var_7c_1[1];
                                    var_8c_1 = &var_8c_1[1];

                                    {
                                        int32_t a1_9 = var_90_1 + 1;

                                        var_90_1 = a1_9;
                                        if (arg6 == a1_9)
                                            break;
                                    }
                                }
                            }
                        }
                    }

                    {
                        uint32_t v1_21 = (uint16_t)(*var_70_1);
                        int32_t a1_11 = var_30_1 + 1;

                        var_70_1 = &var_70_1[1];
                        var_30_1 = a1_11;
                        arg_8 -= v1_21 << (v0 & 0x1f);
                        v1_2 = arg6;
                        if (arg7 == a1_11)
                            break;
                    }
                }
            }

            /* offset 0xc */
            *(int32_t *)((char *)arg1 + 0xc) = v0_7;
            /* offset 0x10 */
            *(int32_t *)((char *)arg1 + 0x10) = s1_2;
            /* offset 0x14 */
            *(int32_t *)((char *)arg1 + 0x14) = arg7 * arg6;
            AL_BitStreamLite_Deinit(&var_d0);
            return 1;
        }
    }

    return 0;
}
