#include <stdint.h>

#include "alcodec/al_utils.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function,
                        void *caller);

int32_t AL_GetNumLinesInPitch(int32_t arg1); /* forward decl, ported by T5 later */
int32_t ComputeRndPitch(int32_t arg1, uint16_t arg2, int32_t arg3, int32_t arg4); /* forward decl, ported by T5 later */
int32_t GetFbcMapPitch(int32_t arg1, int32_t arg2); /* forward decl, ported by T7 later */
int32_t GetFbcMapSize(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4); /* forward decl, ported by T7 later */
int32_t AL_GetCompLcuSize(char arg1, char arg2, int32_t arg3, char arg4);

int32_t GetMaxLCU(int32_t arg1, int32_t arg2, char arg3)
{
    uint32_t a2_1 = (uint32_t)(uint8_t)arg3;
    int32_t v1 = 1 << (a2_1 & 0x1f);

    return ((arg1 + v1 - 1) >> (a2_1 & 0x1f)) * ((arg2 + v1 - 1) >> (a2_1 & 0x1f));
}

int32_t AL_GetAllocSizeEP1(void)
{
    return 0x6400;
}

int32_t AL_GetAllocSizeEP2(int32_t arg1, int32_t arg2, int32_t arg3)
{
    if (arg3 != 1) {
        if (arg3 == 0 || arg3 == 4) {
            return ((GetBlk16x16(arg1, arg2) + 0x7f) >> 7 << 7) + 0x40;
        }

        __assert("0",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/EncBuffers.c",
                 0x43, "AL_GetAllocSizeEP2", &_gp);
    }

    return (((GetBlk32x32(arg1, arg2) << 3) + 0x7f) >> 7 << 7) + 0x40;
}

int32_t AL_GetAllocSizeEP3PerCore(void)
{
    return 0x1420;
}

int32_t AL_GetAllocSizeEP3(void)
{
    return (AL_GetAllocSizeEP3PerCore() + 0x7f) >> 7 << 7;
}

int32_t AL_GetAllocSizeSRD(int32_t arg1, int16_t arg2, char arg3)
{
    return (1 << ((((uint32_t)(uint8_t)arg3) - 2) << 1 & 0x1f)) *
           GetMaxLCU((arg1 + 0x1f) >> 5 << 5, (uint32_t)(uint16_t)arg2, arg3);
}

int32_t AL_EncGetMinPitch(int32_t arg1, char arg2, int32_t arg3)
{
    return ComputeRndPitch(arg1, (uint32_t)(uint8_t)arg2, arg3, 0x10);
}

int32_t AL_CalculatePitchValue(int32_t arg1, char arg2)
{
    return AL_EncGetMinPitch(arg1, (uint32_t)(uint8_t)arg2, 0);
}

int32_t AL_GetSrcStorageMode(int32_t arg1)
{
    if ((uint32_t)arg1 >= 4U) {
        if ((uint32_t)arg1 < 6U) {
            return 3;
        }

        if ((uint32_t)arg1 < 8U) {
            return 2;
        }
    }

    return 0;
}

int32_t AL_IsSrcCompressed(int32_t arg1)
{
    return ((((uint32_t)arg1 & 0xfffffffdU) ^ 5U) < 1U) ? 1 : 0;
}

int32_t AL_GetAllocSizeSrc_Y(int32_t arg1, int32_t arg2, int32_t arg3)
{
    int32_t v0_1 = AL_GetNumLinesInPitch(AL_GetSrcStorageMode(arg1));

    if (v0_1 == 0) {
        __builtin_trap();
    }

    return arg3 * arg2 / v0_1;
}

uint32_t AL_GetAllocSizeSrc_UV(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    int32_t result = AL_GetAllocSizeSrc_Y(arg1, arg2, arg3);

    if (arg4 == 1) {
        return (uint32_t)result >> 1;
    }

    if (arg4 != 0) {
        if (arg4 == 2) {
            return (uint32_t)result;
        }

        __assert("0",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/EncBuffers.c",
                 0x6b, "GetChromaAllocSize", &_gp);
    }

    return 0;
}

int32_t AL_GetAllocSizeSrc_MapY(int32_t arg1, int32_t arg2, int32_t arg3)
{
    if ((arg3 & 1) != 0) {
        return GetFbcMapSize(0, arg1, arg2, AL_GetSrcStorageMode(arg3));
    }

    return 0;
}

int32_t AL_GetAllocSizeSrc_MapUV(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    int32_t result = arg3 & 1;

    if (result != 0) {
        result = AL_GetAllocSizeSrc_MapY(arg1, arg2, arg3);

        if (arg4 == 1) {
            return (uint32_t)result >> 1;
        }

        if (arg4 == 0) {
            return 0;
        }

        if (arg4 != 2) {
            __assert("0",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/EncBuffers.c",
                     0x6b, "GetChromaAllocSize", &_gp);
            return 0;
        }
    }

    return result;
}

int32_t AL_GetAllocSizeSrc(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                           int32_t arg6)
{
    int32_t result_1 = AL_GetAllocSizeSrc_Y(arg4, arg5, arg6);
    int32_t result = result_1;

    if (arg3 == 1) {
        result += (uint32_t)result_1 >> 1;

        if ((arg4 & 1) != 0) {
            int32_t v0_6 = AL_GetAllocSizeSrc_MapY(arg1, arg2, arg4);

            return ((uint32_t)v0_6 >> 1) + result + v0_6;
        }
    } else {
        if (arg3 == 0) {
            goto label_4c774;
        }

        if (arg3 != 2) {
            __assert("0",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/EncBuffers.c",
                     0x6b, "GetChromaAllocSize", &_gp);
        } else {
            result <<= 1;

            if ((arg4 & 1) != 0) {
                return AL_GetAllocSizeSrc_MapY(arg1, arg2, arg4) * 2 + result;
            }
        }
    }

    return result;

label_4c774:
    if ((arg4 & 1) != 0) {
        return result + AL_GetAllocSizeSrc_MapY(arg1, arg2, arg4);
    }

    return result;
}

int32_t AL_GetAllocSize_Src(int32_t arg1, int32_t arg2, char arg3, int32_t arg4, int32_t arg5)
{
    return AL_GetAllocSizeSrc(arg1, arg2, arg4, arg5,
                              AL_EncGetMinPitch(arg1, (uint32_t)(uint8_t)arg3,
                                                AL_GetSrcStorageMode(arg5)),
                              arg2);
}

int32_t AL_GetEncoderFbcMapPitch(int32_t arg1)
{
    return GetFbcMapPitch(arg1, 3);
}

int32_t AL_GetEncoderFbcMapSize(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    int32_t v0_3;

    if (arg2 < 0) {
        if (arg4 == 0) {
            __builtin_trap();
        }

        v0_3 = arg2 / arg4 * arg4;
    } else {
        if (arg4 == 0) {
            __builtin_trap();
        }

        v0_3 = (arg2 + arg4 - 1) / arg4 * arg4;
    }

    if (arg3 < 0) {
        if (arg4 == 0) {
            __builtin_trap();
        }

        return GetFbcMapSize(arg1, v0_3, arg3 / arg4 * arg4, 3);
    }

    if (arg4 == 0) {
        __builtin_trap();
    }

    return GetFbcMapSize(arg1, v0_3, (arg4 + arg3 - 1) / arg4 * arg4, 3);
}

uint32_t AL_GetAllocSize_EncReference(int32_t arg1, int32_t arg2, char arg3, int32_t arg4, char arg5)
{
    int32_t v1 = arg1;
    int32_t a0 = arg4;
    uint32_t a2 = (uint32_t)(uint8_t)arg3;
    uint32_t a3 = (uint32_t)(uint8_t)arg5;
    int32_t a1 = (arg2 + 0x3f) >> 6 << 6;
    int32_t v0_5;
    int32_t s0;
    int32_t var_10_1;
    int32_t var_c_1;

    if (v1 < 0) {
        int32_t v0_8 = (v1 + 0x3f) >> 6 << 6;

        var_10_1 = v0_8;
        var_c_1 = a1;
        v0_5 = v0_8 * a1;

        if (a0 == 1) {
            goto label_4ca48;
        }

        if (a0 == 0) {
            s0 = 1;
            goto label_4c9b4;
        }

        if (a0 == 2) {
            v0_5 <<= 1;
            s0 = 1;
            goto label_4c9b4;
        }

        __assert("0",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/EncBuffers.c",
                 0xed, "GetRasterFrameSize", &_gp);
    } else {
        int32_t v0_4 = (v1 + 0x3f) >> 6 << 6;

        var_10_1 = v0_4;
        var_c_1 = a1;
        v0_5 = v0_4 * a1;

        if (a0 != 1) {
            if (a0 == 0) {
                s0 = 1;
                goto label_4c9b4;
            }

            if (a0 == 2) {
                v0_5 <<= 1;
                s0 = 1;
                goto label_4c9b4;
            }

            __assert("0",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/EncBuffers.c",
                     0xed, "GetRasterFrameSize", &_gp);
        }
    }

label_4ca48:
    v0_5 *= 3;
    s0 = 2;

label_4c9b4:
    if (a2 >= 9U) {
        v0_5 *= 0xa;
        s0 <<= 3;
    }

    if (s0 == 0) {
        __builtin_trap();
    }

    {
        uint32_t result = (uint32_t)v0_5 / (uint32_t)s0;

        if (a3 == 0) {
            return result;
        }

        return result + AL_GetEncoderFbcMapSize(0, var_10_1, var_c_1, (a2 >= 9U) ? 0x10 : 8);
    }
}

int32_t AL_GetAllocSize_CompData(int32_t arg1, int32_t arg2, char arg3, char arg4, int32_t arg5,
                                 char arg6)
{
    return GetBlk16x16(arg1, arg2) *
           AL_GetCompLcuSize((uint32_t)(uint8_t)arg3, (uint32_t)(uint8_t)arg4, arg5,
                             (uint32_t)(uint8_t)arg6);
}

int32_t AL_GetAllocSize_EncCompMap(int32_t arg1, int32_t arg2, char arg3, char arg4, char arg5)
{
    if ((uint32_t)(uint8_t)arg5 == 0) {
        return ((GetBlk16x16(arg1, arg2) << 4) + 0x1f) >> 5 << 5;
    }

    {
        uint32_t a2 = (uint32_t)(uint8_t)arg3;
        int32_t a1_3;

        if (arg2 < 0) {
            if (a2 == 0) {
                __builtin_trap();
            }

            a1_3 = arg2 / (int32_t)a2 * (int32_t)a2;
        } else {
            if (a2 == 0) {
                __builtin_trap();
            }

            a1_3 = (arg2 + (int32_t)a2 - 1) / (int32_t)a2 * (int32_t)a2;
        }

        if (a2 == 0) {
            __builtin_trap();
        }

        return (a1_3 / (int32_t)a2 * (uint32_t)(uint8_t)arg4) << 5;
    }
}

int32_t AL_GetAllocSize_MV(int32_t arg1, int32_t arg2, char arg3, int32_t arg4)
{
    int32_t s0 = 1;
    uint32_t a2 = (uint32_t)(uint8_t)arg3;

    if (arg4 != 1) {
        s0 = 2;
    }

    if (a2 == 5U) {
        return (s0 * (GetBlk32x32(arg1, arg2) << 2) + 0x10) << 4;
    }

    if (a2 == 6U) {
        return (s0 * (GetBlk64x64(arg1, arg2) << 4) + 0x10) << 4;
    }

    if (a2 != 4U) {
        __assert("0",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/EncBuffers.c",
                 0x145, "AL_GetAllocSize_MV", &_gp);
    }

    return (s0 * GetBlk16x16(arg1, arg2) + 0x10) << 4;
}

int32_t AL_GetAllocSize_WPP(int32_t arg1, int32_t arg2, char arg3)
{
    uint32_t a2;

    if (arg2 == 0) {
        __builtin_trap();
    }

    a2 = (uint32_t)(uint8_t)arg3;

    if (a2 == 0) {
        __builtin_trap();
    }

    return (int32_t)a2 * ((((((arg1 + arg2 - 1) / arg2 + (int32_t)a2 - 1) / (int32_t)a2) << 2) +
                            0x7f) >>
                           7
                           << 7) *
           arg2;
}

int32_t AL_GetAllocSize_SliceSize(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    int32_t v0 = 1 << (arg4 & 0x1f);
    int32_t a3_1 =
        ((uint32_t)(arg1 - 1 + v0) >> (arg4 & 0x1f)) * ((uint32_t)(arg2 - 1 + v0) >> (arg4 & 0x1f));
    int32_t a2_2 = ((arg3 << 5) + a3_1) << 2;
    int32_t a3_2 = a3_1 << 5;

    if (a2_2 >= a3_2) {
        a3_2 = a2_2;
    }

    return (a3_2 + 0x1f) >> 5 << 5;
}

uint32_t AL_GetRecPitch(int32_t arg1, int32_t arg2)
{
    if ((uint32_t)arg1 < 9U) {
        return (uint32_t)(arg2 + 0x3f) >> 6 << 8;
    }

    return ((uint32_t)(arg2 + 0x3f) >> 6) * 0x140U;
}

int32_t AL_GetCompLcuSize(char arg1, char arg2, int32_t arg3, char arg4)
{
    uint32_t a0 = (uint32_t)(uint8_t)arg1;

    if ((uint32_t)(uint8_t)arg4 == 0) {
        return 0x520;
    }

    {
        int32_t a0_2 = (int32_t)(a0 * a0 * (uint32_t)(uint8_t)arg2);
        int32_t v0_1 = a0_2 >> 3;

        if (arg3 == 2) {
            v0_1 <<= 1;
        }

        if (arg3 == 3) {
            v0_1 *= 3;
        }

        if (arg3 == 1) {
            v0_1 += a0_2 >> 4;
        }

        return (v0_1 + 0x33) >> 5 << 5;
    }
}
