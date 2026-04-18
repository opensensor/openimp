#include <stdint.h>

extern uint32_t AL_GetRequiredLevel(int32_t arg1, int32_t* arg2, int32_t arg3); /* forward decl, ported by T<N> later */
extern void __assert(const char* assertion, const char* file, int line, const char* function);

static const int32_t CSWTCH_20[53] = {
    350, 175, 500, 1000, 2000, 800000, 800000, 800000,
    800000, 800000, 800000, 2000, 4000, 4000, 800000,
    800000, 800000, 800000, 800000, 800000, 800000,
    10000, 14000, 20000, 800000, 800000, 800000, 800000,
    800000, 800000, 800000, 25000, 62500, 62500, 800000,
    800000, 800000, 800000, 800000, 800000, 800000,
    135000, 240000, 240000, 800000, 800000, 800000, 800000,
    800000, 800000, 800000, 240000, 480000,
};

static const int32_t CSWTCH_16[44] = {
    396, 396, 900, 2376, 2376, 696320, 696320, 696320,
    696320, 696320, 696320, 2376, 4752, 8100, 696320,
    696320, 696320, 696320, 696320, 696320, 696320,
    8100, 18000, 20480, 696320, 696320, 696320, 696320,
    696320, 696320, 696320, 32768, 32768, 34816, 696320,
    696320, 696320, 696320, 696320, 696320, 696320,
    110400, 184320, 184320,
};

static const uint32_t CSWTCH_13[53] = {
    1485U, 1485U, 3000U, 6000U, 11880U, 16711680U, 16711680U,
    16711680U, 16711680U, 16711680U, 16711680U, 11880U, 19800U,
    20250U, 16711680U, 16711680U, 16711680U, 16711680U, 16711680U,
    16711680U, 16711680U, 40500U, 108000U, 216000U, 16711680U,
    16711680U, 16711680U, 16711680U, 16711680U, 16711680U,
    16711680U, 245760U, 245760U, 522240U, 16711680U, 16711680U,
    16711680U, 16711680U, 16711680U, 16711680U, 16711680U,
    589824U, 983040U, 2073600U, 16711680U, 16711680U, 16711680U,
    16711680U, 16711680U, 16711680U, 16711680U, 4177920U,
    8355840U,
};

static const int32_t AVC_MAX_VIDEO_DPB_SIZE[24] = {
    396, 10, 900, 11, 2376, 12, 4752, 21, 8100, 22, 18000, 31,
    20480, 32, 32768, 40, 34816, 42, 110400, 50, 184320, 51, 696320, 60,
};

static const int32_t AVC_MAX_VIDEO_BITRATE[30] = {
    64, 10, 128, 9, 192, 11, 384, 12, 768, 13, 2000, 20,
    4000, 21, 10000, 30, 14000, 31, 20000, 32, 50000, 41, 135000, 50,
    240000, 51, 480000, 61, 800000, 62,
};

static const int32_t AVC_MAX_MB_RATE[18] = {
    1485, 10, 245760, 40, 522240, 42, 589824, 50, 983040, 51, 2073600, 52,
    4177920, 60, 8355840, 61, 16711680, 62,
};

static const int32_t AVC_MAX_FRAME_MB[22] = {
    99, 10, 396, 11, 792, 21, 1620, 22, 3600, 31, 5120, 32,
    8192, 40, 8704, 42, 22080, 50, 36864, 51, 139264, 60,
};

int32_t AL_AVC_CheckLevel(int32_t arg1)
{
    int32_t result;

    if ((uint32_t)(arg1 - 0xa) < 0x17U) {
        result = 1;

        if ((0x701c0fU >> ((arg1 - 0xa) & 0x1f) & 1U) == 0U) {
            result = 0;
        }
    } else {
        result = 0;

        if ((uint32_t)(arg1 - 0x28) < 0x17U) {
            return (int32_t)(0x701c07U >> ((arg1 - 0x28) & 0x1f) & 1U);
        }
    }

    return result;
}

uint32_t AL_AVC_GetMaxNumberOfSlices(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, uint32_t arg5)
{
    int32_t v0_2;
    uint32_t t1;

    if ((uint32_t)(arg2 - 9) >= 0x35U) {
        t1 = 0xff0000;
label_3bb10:
        v0_2 = 0x18;
label_3bb18:
        if ((uint32_t)(arg1 - 0x42) >= 2U && arg1 != 0x58) {
            uint32_t lo_1;

            if (arg4 == 0) {
                __builtin_trap();
            }

            lo_1 = (uint32_t)((arg3 << 1) * t1) / (uint32_t)arg4 / (uint32_t)v0_2;

            if (v0_2 == 0) {
                __builtin_trap();
            }

            if ((int32_t)arg5 >= (int32_t)lo_1) {
                return lo_1;
            }
        }

        return arg5;
    }

    t1 = CSWTCH_13[arg2 - 9];

    if ((uint32_t)(arg2 - 9) >= 0x20U) {
        goto label_3bb10;
    }

    {
        int32_t a1_1 = 1 << ((arg2 - 9) & 0x1f);

        if ((a1_1 & 0x381f) == 0) {
            v0_2 = 0x3c;

            if ((a1_1 & (int32_t)0x80c00000U) != 0) {
                goto label_3bb18;
            }

            v0_2 = 0x16;

            if (((uint32_t)a1_1 >> 0x15 & 1U) != 0U) {
                goto label_3bb18;
            }

            goto label_3bb10;
        }
    }

    v0_2 = 0x18;
    goto label_3bb18;
}

int32_t AL_AVC_GetMaxCPBSize(int32_t arg1)
{
    if ((uint32_t)(arg1 - 9) >= 0x35U) {
        return 0xc3500;
    }

    return CSWTCH_20[arg1 - 9];
}

int32_t AL_AVC_GetLevelFromFrameSize(int32_t arg1)
{
    return (int32_t)AL_GetRequiredLevel(arg1, (int32_t*)&AVC_MAX_FRAME_MB[0], 0xb);
}

int32_t AL_AVC_GetMaxDPBSize(int32_t arg1, int32_t arg2, int32_t arg3)
{
    if (arg2 == 0) {
        __assert("iWidth", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/AvcLevelsLimit.c", 0xaa, "AL_AVC_GetMaxDPBSize");
    }

    if (arg3 != 0) {
        int32_t v1 = arg2 + 0xf;
        int32_t result;

        if ((uint32_t)(arg1 - 9) >= 0x2cU) {
            int32_t a1_3 = arg3;
            int32_t a1_5;

            if (arg2 >= 0) {
                v1 = arg2;
            }

            if (arg3 < 0) {
                a1_3 = arg3 + 0xf;
            }

            a1_5 = (v1 >> 4) * (a1_3 >> 4);

            if (a1_5 == 0) {
                __builtin_trap();
            }

            result = 696320 / a1_5;

            if (result < 2) {
                return 2;
            }
        } else {
            int32_t a1 = arg3;
            int32_t a1_2;

            if (arg2 >= 0) {
                v1 = arg2;
            }

            if (arg3 < 0) {
                a1 = arg3 + 0xf;
            }

            a1_2 = (v1 >> 4) * (a1 >> 4);

            if (a1_2 == 0) {
                __builtin_trap();
            }

            result = CSWTCH_16[arg1 - 9] / a1_2;

            if (result < 2) {
                return 2;
            }
        }

        if (result < 0x11) {
            return result;
        }

        return 0x10;
    }

    __assert("iHeight", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/AvcLevelsLimit.c", 0xab, "AL_AVC_GetMaxDPBSize");
}

int32_t AL_AVC_GetLevelFromMBRate(int32_t arg1)
{
    return (int32_t)AL_GetRequiredLevel(arg1, (int32_t*)&AVC_MAX_MB_RATE[0], 0x11);
}

int32_t AL_AVC_GetLevelFromBitrate(int32_t arg1)
{
    return (int32_t)AL_GetRequiredLevel(arg1, (int32_t*)&AVC_MAX_VIDEO_BITRATE[0], 0xf);
}

int32_t AL_AVC_GetLevelFromDPBSize(int32_t arg1)
{
    return (int32_t)AL_GetRequiredLevel(arg1, (int32_t*)&AVC_MAX_VIDEO_DPB_SIZE[0], 0xc);
}

int32_t AL_AVC_IsIDR(int32_t arg1)
{
    return (uint32_t)(arg1 ^ 5) < 1U ? 1 : 0;
}

int32_t AL_AVC_IsVcl(int32_t arg1)
{
    return (uint32_t)(((uint32_t)arg1 & 0xfffffffbU) ^ 1U) < 1U ? 1 : 0;
}
