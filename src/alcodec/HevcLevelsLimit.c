#include <stdint.h>

/* forward decl, ported by T<N> later */
int32_t AL_GetRequiredLevel(int32_t arg1, const uint32_t* arg2, int32_t arg3);

static const uint16_t CSWTCH_11[44] = {
    16, 600, 600, 600, 600, 600, 600, 600, 600, 600, 16, 20, 600, 600, 600,
    600, 600, 600, 600, 600, 30, 40, 600, 600, 600, 600, 600, 600, 600, 600,
    75, 75, 600, 600, 600, 600, 600, 600, 600, 600, 200, 200, 200, 0
};

static const uint32_t CSWTCH_14[52] = {
    350, 240000, 240000, 240000, 240000, 240000, 240000, 240000, 240000,
    240000, 1500, 3000, 240000, 240000, 240000, 240000, 240000, 240000,
    240000, 240000, 6000, 10000, 240000, 240000, 240000, 240000, 240000,
    240000, 240000, 240000, 12000, 20000, 240000, 240000, 240000, 240000,
    240000, 240000, 240000, 240000, 25000, 40000, 60000, 240000, 240000,
    240000, 240000, 240000, 240000, 240000, 60000, 120000
};

static const uint32_t CSWTCH_13[52] = {
    350, 800000, 800000, 800000, 800000, 800000, 800000, 800000, 800000,
    800000, 1500, 3000, 800000, 800000, 800000, 800000, 800000, 800000,
    800000, 800000, 6000, 10000, 800000, 800000, 800000, 800000, 800000,
    800000, 800000, 800000, 30000, 50000, 800000, 800000, 800000, 800000,
    800000, 800000, 800000, 800000, 100000, 160000, 240000, 800000, 800000,
    800000, 800000, 800000, 800000, 800000, 240000, 480000
};

static const uint32_t CSWTCH_7[53] = {
    36864, 8912896, 8912896, 8912896, 8912896, 8912896, 8912896, 8912896,
    8912896, 8912896, 122880, 245760, 8912896, 8912896, 8912896, 8912896,
    8912896, 8912896, 8912896, 8912896, 552960, 983040, 8912896, 8912896,
    8912896, 8912896, 8912896, 8912896, 8912896, 8912896, 2228224, 2228224,
    8912896, 8912896, 8912896, 8912896, 8912896, 8912896, 8912896, 8912896,
    8912896, 8912896, 8912896, 8912896, 8912896, 8912896, 8912896, 8912896,
    8912896, 8912896, 35651584, 35651584, 35651584
};

static const uint32_t HEVC_MAX_TILE_COLS[12] = {
    1, 10, 2, 30, 3, 31, 5, 40, 10, 50, 20, 60
};

static const uint32_t HEVC_MAX_VIDEO_BITRATE_MAIN[24] = {
    128, 10, 1500, 20, 3000, 21, 6000, 30, 10000, 31, 12000, 40, 20000, 41,
    25000, 50, 40000, 51, 60000, 52, 120000, 61, 240000, 62
};

static const uint32_t HEVC_MAX_VIDEO_BITRATE_HIGH[24] = {
    128, 10, 1500, 20, 3000, 21, 6000, 30, 10000, 31, 30000, 40, 50000, 41,
    100000, 50, 160000, 51, 240000, 52, 480000, 61, 800000, 62
};

static const uint32_t HEVC_MAX_PIX_RATE[24] = {
    552960, 10, 3686400, 20, 7372800, 21, 16588800, 30, 33177600, 31,
    66846720, 40, 133693440, 41, 267386880, 50, 534773760, 51, 1069547520, 52,
    2139095040, 61, 4278190080U, 62
};

static const uint32_t AL_HEVC_MAX_PIX_PER_FRAME[17] = {
    36864, 10, 122880, 20, 245760, 21, 552960, 30, 983040,
    31, 2228224, 40, 8912896, 50, 35651584, 60, 0
};

int32_t AL_HEVC_CheckLevel(int32_t arg1)
{
    if (arg1 == 0xa)
        return 1;

    if (((uint32_t)(arg1 - 0x14) >= 0x16 ||
         ((0x300c03U >> ((uint32_t)(arg1 - 0x14) & 0x1f)) & 1) == 0) &&
        (uint32_t)(arg1 - 0x32) >= 3)
        return (uint32_t)(arg1 - 0x3c) < 3 ? 1 : 0;

    return 1;
}

uint32_t AL_HEVC_GetMaxNumberOfSlices(int32_t arg1)
{
    if ((uint32_t)(arg1 - 0xa) >= 0x2b)
        return 0x258;

    return CSWTCH_11[arg1 - 0xa];
}

int32_t AL_HEVC_GetMaxCPBSize(int32_t arg1, int32_t arg2)
{
    if (arg2 == 1) {
        if ((uint32_t)(arg1 - 0xa) < 0x34)
            return (int32_t)CSWTCH_13[arg1 - 0xa];

        return 0xc3500;
    }

    if ((uint32_t)(arg1 - 0xa) >= 0x34)
        return 0x3a980;

    return (int32_t)CSWTCH_14[arg1 - 0xa];
}

int32_t AL_HEVC_GetMaxDPBSize(int32_t arg1, int32_t arg2, int32_t arg3)
{
    int32_t v1 = 0x220000;
    int32_t a0_3;

    if ((uint32_t)(arg1 - 0xa) >= 0x35)
        a0_3 = 0x880000;
    else {
        a0_3 = (int32_t)CSWTCH_7[arg1 - 0xa];
        {
            int32_t v0_1 = a0_3 + 3;

            if (a0_3 >= 0)
                v0_1 = a0_3;

            v1 = v0_1 >> 2;
        }
    }

    {
        int32_t a1 = arg2 * arg3;
        int32_t result = 0x10;

        if (v1 < a1) {
            result = 0xc;

            if ((((uint32_t)a0_3 >> 0x1f) + a0_3) >> 1 < a1) {
                int32_t a2 = a0_3 << 1;

                result = 9;
                if (a2 / 3 < a1) {
                    int32_t a0_4 = a2 + a0_3;

                    if (a0_4 < 0)
                        a0_4 += 3;

                    result = 6;
                    if ((a0_4 >> 2) >= a1)
                        return 8;
                }
            }
        }

        return result;
    }
}

int32_t AL_HEVC_GetLevelFromFrameSize(int32_t arg1)
{
    return AL_GetRequiredLevel(arg1, AL_HEVC_MAX_PIX_PER_FRAME, 8);
}

int32_t AL_HEVC_GetLevelFromPixRate(int32_t arg1)
{
    return AL_GetRequiredLevel(arg1, HEVC_MAX_PIX_RATE, 0xc);
}

int32_t AL_HEVC_GetLevelFromBitrate(int32_t arg1, int32_t arg2)
{
    if (arg2 != 0)
        return AL_GetRequiredLevel(arg1, HEVC_MAX_VIDEO_BITRATE_HIGH, 0xc);

    return AL_GetRequiredLevel(arg1, HEVC_MAX_VIDEO_BITRATE_MAIN, 0xc);
}

int32_t AL_HEVC_GetLevelFromTileCols(int32_t arg1)
{
    return AL_GetRequiredLevel(arg1, HEVC_MAX_TILE_COLS, 6);
}

uint32_t AL_HEVC_GetLevelFromDPBSize(int32_t arg1, int32_t arg2)
{
    int32_t a3 = 0x9000;
    int32_t t0 = 0;
    const uint32_t* t1 = AL_HEVC_MAX_PIX_PER_FRAME;

    while (1) {
        int32_t a2_1 = 0x10;

        if ((a3 >> 2) < arg2) {
            a2_1 = 0xc;

            if ((a3 >> 1) < arg2) {
                a2_1 = 8;
                if (((a3 * 3) >> 2) < arg2)
                    a2_1 = 6;
            }
        }

        if (a2_1 >= arg1)
            return t1[1];

        t0 += 1;
        t1 = &t1[2];
        if (t0 == 8)
            break;

        a3 = (int32_t)*t1;
    }

    return 0xff;
}

int32_t AL_HEVC_IsSLNR(int32_t arg1)
{
    if ((uint32_t)arg1 >= 0xf)
        return 0;

    return (0U < ((1U << ((uint32_t)arg1 & 0x1f)) & 0x5555U)) ? 1 : 0;
}

int32_t AL_HEVC_IsRASL_RADL_SLNR(int32_t arg1)
{
    if ((uint32_t)arg1 >= 0xf)
        return 0;

    return (0U < ((1U << ((uint32_t)arg1 & 0x1f)) & 0x57d5U)) ? 1 : 0;
}

int32_t AL_HEVC_IsBLA(int32_t arg1)
{
    return (uint32_t)(arg1 - 0x10) < 3 ? 1 : 0;
}

int32_t AL_HEVC_IsCRA(int32_t arg1)
{
    return (uint32_t)(arg1 ^ 0x15) < 1 ? 1 : 0;
}

int32_t AL_HEVC_IsIDR(int32_t arg1)
{
    return (uint32_t)(arg1 - 0x13) < 2 ? 1 : 0;
}

int32_t AL_HEVC_IsRASL(int32_t arg1)
{
    return (uint32_t)(arg1 - 8) < 2 ? 1 : 0;
}

int32_t AL_HEVC_IsVcl(int32_t arg1)
{
    if ((uint32_t)arg1 < 0xa)
        return 1;

    return (uint32_t)(arg1 - 0x10) < 6 ? 1 : 0;
}
