#include <stdint.h>

#include "alcodec/BitStreamLite.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, void *caller);

/* forward decl, ported by T14/T15 already */
int32_t AL_RbspEncoding_WriteAUD(AL_BitStreamLite *arg1, int32_t arg2);
int32_t AL_RbspEncoding_BeginSEI(AL_BitStreamLite *arg1, uint8_t arg2);
int32_t AL_RbspEncoding_EndSEI(AL_BitStreamLite *arg1, int32_t arg2);
int32_t AL_RbspEncoding_CloseSEI(AL_BitStreamLite *arg1);
int32_t AL_RbspEncoding_WriteMasteringDisplayColourVolume(AL_BitStreamLite *arg1, int16_t *arg2);
int32_t AL_RbspEncoding_WriteUserDataUnregistered(AL_BitStreamLite *arg1, char *arg2, int8_t arg3);

int32_t getCodec(void);
int32_t writeContentLightLevel(AL_BitStreamLite *arg1, int16_t *arg2);
int32_t writeSeiPictureTiming(AL_BitStreamLite *arg1, char *arg2, int32_t arg3, int32_t arg4, int32_t arg5);
int32_t writeSeiBufferingPeriod(AL_BitStreamLite *arg1, char *arg2, int32_t arg3, int32_t arg4);
int32_t writeSeiRecoveryPoint(AL_BitStreamLite *arg1, int32_t arg2);
int32_t writeSeiActiveParameterSets(AL_BitStreamLite *arg1, void *arg2, char *arg3);
int32_t writePps(AL_BitStreamLite *arg1, char *arg2);
int32_t writeSps(AL_BitStreamLite *arg1, char *arg2, int32_t arg3);
int32_t writeVps(AL_BitStreamLite *arg1, char *arg2);

static void writeSublayer(AL_BitStreamLite *arg1, char *arg2, int32_t *arg3, int32_t arg4);
static int32_t writeHrdParam(AL_BitStreamLite *arg1, char *arg2, int32_t arg3, int32_t arg4);
static int32_t writeProfileTierLevel(AL_BitStreamLite *arg1, char *arg2, int32_t arg3);

static const uint8_t AL_HEVC_ScanOrder8x8[64] = {
    0x00, 0x08, 0x01, 0x10, 0x09, 0x02, 0x18, 0x11, 0x0a, 0x03, 0x20, 0x19, 0x12, 0x0b, 0x04, 0x28,
    0x21, 0x1a, 0x13, 0x0c, 0x05, 0x30, 0x29, 0x22, 0x1b, 0x14, 0x0d, 0x06, 0x38, 0x31, 0x2a, 0x23,
    0x1c, 0x15, 0x0e, 0x07, 0x39, 0x32, 0x2b, 0x24, 0x1d, 0x16, 0x0f, 0x3a, 0x33, 0x2c, 0x25, 0x1e,
    0x17, 0x3b, 0x34, 0x2d, 0x26, 0x1f, 0x3c, 0x35, 0x2e, 0x27, 0x3d, 0x36, 0x2f, 0x3e, 0x37, 0x3f,
};

static const uint8_t AL_HEVC_ScanOrder4x4[16] = {
    0x00, 0x04, 0x01, 0x08, 0x05, 0x02, 0x0c, 0x09, 0x06, 0x03, 0x0d, 0x0a, 0x07, 0x0e, 0x0b, 0x0f,
};

static void *const writer[] = {
    (void *)getCodec,
    (void *)AL_RbspEncoding_WriteAUD,
    (void *)writeVps,
    (void *)writeSps,
    (void *)writePps,
    (void *)writeSeiActiveParameterSets,
    (void *)writeSeiBufferingPeriod,
    (void *)writeSeiRecoveryPoint,
    (void *)writeSeiPictureTiming,
    (void *)AL_RbspEncoding_WriteMasteringDisplayColourVolume,
    (void *)writeContentLightLevel,
    (void *)AL_RbspEncoding_WriteUserDataUnregistered,
};

int32_t getCodec(void)
{
    return 1;
}

int32_t writeContentLightLevel(AL_BitStreamLite *arg1, int16_t *arg2)
{
    int32_t v0;

    v0 = AL_RbspEncoding_BeginSEI(arg1, 0x90);
    AL_BitStreamLite_PutU(arg1, 0x10, (uint32_t)(uint16_t)arg2[0]);
    AL_BitStreamLite_PutU(arg1, 0x10, (uint32_t)(uint16_t)arg2[1]);
    AL_BitStreamLite_EndOfSEIPayload(arg1);
    return AL_RbspEncoding_EndSEI(arg1, v0);
}

int32_t writeSeiPictureTiming(AL_BitStreamLite *arg1, char *arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    int32_t v0;

    v0 = AL_RbspEncoding_BeginSEI(arg1, 1);
    if ((uint8_t)arg2[0x2b80] != 0) {
        AL_BitStreamLite_PutU(arg1, 4, (uint32_t)arg5);
        AL_BitStreamLite_PutU(arg1, 2, 0);
        AL_BitStreamLite_PutU(arg1, 1, 0);
    }
    if ((uint8_t)arg2[0x2bac] != 0) {
        AL_BitStreamLite_PutU(arg1, 0x1f, (uint32_t)(((uint32_t)arg3 >> 0x1f) + arg3) >> 1);
        AL_BitStreamLite_PutU(arg1, 0x1f, (uint32_t)(((uint32_t)arg4 >> 0x1f) + arg4) >> 1);
        if ((uint8_t)arg2[0x2bae] != 0) {
            AL_BitStreamLite_PutU(arg1, (uint8_t)(arg2[0x2bb8] + 1), 0);
            if ((uint8_t)arg2[0x2bb1] != 0) {
                AL_BitStreamLite_PutUE(arg1, 0);
                AL_BitStreamLite_PutU(arg1, 1, 0);
                AL_BitStreamLite_PutUE(arg1, 0);
            }
        }
    }
    AL_BitStreamLite_EndOfSEIPayload(arg1);
    return AL_RbspEncoding_EndSEI(arg1, v0);
}

int32_t writeSeiBufferingPeriod(AL_BitStreamLite *arg1, char *arg2, int32_t arg3, int32_t arg4)
{
    int32_t v0;
    int32_t s5;

    v0 = AL_RbspEncoding_BeginSEI(arg1, 0);
    /* offset 0x1d0 */
    AL_BitStreamLite_PutUE(arg1, (int32_t)(uint8_t)*(uint32_t *)(arg2 + 0x1d0));
    if ((uint8_t)arg2[0x2bae] == 0) {
        AL_BitStreamLite_PutU(arg1, 1, 0);
    }
    s5 = 0x18;
    if ((uint8_t)arg2[0x2ba8] != 0) {
        s5 = (uint8_t)arg2[0x2bb7] + 1;
    }
    AL_BitStreamLite_PutU(arg1, 1, 0);
    AL_BitStreamLite_PutU(arg1, (uint8_t)s5, 0);
    if ((uint8_t)arg2[0x2bac] == 0) {
        goto label_7508c;
    } else if (*(int32_t *)(arg2 + 0x2bf4) >= 0) {
        int32_t s5_2;

        s5_2 = 0;
        while (1) {
            AL_BitStreamLite_PutU(arg1, (uint8_t)(arg2[0x2bb6] + 1), (uint32_t)arg3);
            AL_BitStreamLite_PutU(arg1, (uint8_t)(arg2[0x2bb6] + 1), (uint32_t)arg4);
            s5_2 += 1;
            if ((uint8_t)arg2[0x2bae] == 0) {
                if (*(int32_t *)(arg2 + 0x2bf4) < s5_2) {
                    break;
                }
            } else {
                AL_BitStreamLite_PutU(arg1, (uint8_t)(arg2[0x2bb6] + 1), 0);
                AL_BitStreamLite_PutU(arg1, (uint8_t)(arg2[0x2bb6] + 1), 0);
                if (*(int32_t *)(arg2 + 0x2bf4) < s5_2) {
                    break;
                }
            }
        }
        goto label_7508c;
    }

label_7508c:
    if ((uint8_t)arg2[0x2bad] != 0 && *(int32_t *)(arg2 + 0x2bf4) >= 0) {
        int32_t s5_3;

        s5_3 = 0;
        while (1) {
            AL_BitStreamLite_PutU(arg1, (uint8_t)(arg2[0x2bb6] + 1), (uint32_t)arg3);
            AL_BitStreamLite_PutU(arg1, (uint8_t)(arg2[0x2bb6] + 1), (uint32_t)arg4);
            s5_3 += 1;
            if ((uint8_t)arg2[0x2bae] == 0) {
                if (*(int32_t *)(arg2 + 0x2bf4) < s5_3) {
                    break;
                }
            } else {
                AL_BitStreamLite_PutU(arg1, (uint8_t)(arg2[0x2bb6] + 1), 0);
                AL_BitStreamLite_PutU(arg1, (uint8_t)(arg2[0x2bb6] + 1), 0);
                if (*(int32_t *)(arg2 + 0x2bf4) < s5_3) {
                    break;
                }
            }
        }
    }
    AL_BitStreamLite_EndOfSEIPayload(arg1);
    return AL_RbspEncoding_EndSEI(arg1, v0);
}

int32_t writeSeiRecoveryPoint(AL_BitStreamLite *arg1, int32_t arg2)
{
    int32_t v0;

    v0 = AL_RbspEncoding_BeginSEI(arg1, 6);
    AL_BitStreamLite_PutSE(arg1, arg2);
    AL_BitStreamLite_PutBit(arg1, 1);
    AL_BitStreamLite_PutBit(arg1, 0);
    AL_BitStreamLite_EndOfSEIPayload(arg1);
    return AL_RbspEncoding_EndSEI(arg1, v0);
}

int32_t writeSeiActiveParameterSets(AL_BitStreamLite *arg1, void *arg2, char *arg3)
{
    int32_t v0;
    uint32_t i;

    v0 = AL_RbspEncoding_BeginSEI(arg1, 0x81);
    AL_BitStreamLite_PutU(arg1, 4, (uint32_t)(uint8_t)arg3[0]);
    AL_BitStreamLite_PutU(arg1, 1, 0);
    AL_BitStreamLite_PutU(arg1, 1, 1);
    AL_BitStreamLite_PutUE(arg1, 0);
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg3[0x1d0]);
    i = (uint32_t)(uint8_t)*(char *)((char *)arg2 + 1);
    while (*(int8_t *)((char *)arg2 + 3) >= (int32_t)i) {
        AL_BitStreamLite_PutUE(arg1, 0);
        i += 1;
    }
    AL_BitStreamLite_EndOfSEIPayload(arg1);
    AL_RbspEncoding_EndSEI(arg1, v0);
    return AL_RbspEncoding_CloseSEI(arg1);
}

int32_t writePps(AL_BitStreamLite *arg1, char *arg2)
{
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0]);
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[1]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[2]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x11]);
    AL_BitStreamLite_PutU(arg1, 3, 0);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[3]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[4]);
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[5]);
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[6]);
    AL_BitStreamLite_PutSE(arg1, (int32_t)(int8_t)arg2[7]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[8]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[9]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0xa]);
    if ((uint8_t)arg2[0xa] != 0) {
        AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0xb]);
    }
    AL_BitStreamLite_PutSE(arg1, (int32_t)(int8_t)arg2[0xc]);
    AL_BitStreamLite_PutSE(arg1, (int32_t)(int8_t)arg2[0xd]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0xe]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0xf]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x10]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x12]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x13]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x14]);
    if ((uint8_t)arg2[0x13] != 0) {
        int16_t *s3_1;
        int32_t i;
        int16_t *s3_2;
        int32_t i_1;

        /* offset 0x16 */
        AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint16_t *)(arg2 + 0x16));
        s3_1 = (int16_t *)(arg2 + 0x1c);
        i = 0;
        /* offset 0x18 */
        AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint16_t *)(arg2 + 0x18));
        AL_BitStreamLite_PutBit(arg1, 0);
        if ((uint16_t)*(uint16_t *)(arg2 + 0x16) != 0) {
            do {
                AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint16_t)(*s3_1) - 1);
                i += 1;
                s3_1 = &s3_1[1];
            } while (i < (int32_t)(uint16_t)*(uint16_t *)(arg2 + 0x16));
        }
        s3_2 = (int16_t *)(arg2 + 0x44);
        i_1 = 0;
        if ((uint16_t)*(uint16_t *)(arg2 + 0x18) != 0) {
            do {
                AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint16_t)(*s3_2) - 1);
                i_1 += 1;
                s3_2 = &s3_2[1];
            } while (i_1 < (int32_t)(uint16_t)*(uint16_t *)(arg2 + 0x18));
        }
        AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x750]);
    }
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x751]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x752]);
    if ((uint8_t)arg2[0x752] != 0) {
        AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x753]);
        AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x754]);
        if ((uint8_t)arg2[0x754] == 0) {
            AL_BitStreamLite_PutSE(arg1, (int32_t)(int8_t)arg2[0x755]);
            AL_BitStreamLite_PutSE(arg1, (int32_t)(int8_t)arg2[0x756]);
        }
    }
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x757]);
    if ((uint8_t)arg2[0x757] != 0) {
        int32_t a0_41;
        char *a1_38;
        int32_t *a2_2;
        int32_t a3_1;

        a0_41 = __assert(
            "pPps->pps_scaling_list_data_present_flag == 0",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/HEVC_RbspEncod.c",
            0x25e, "writePpsData", &_gp);
        writeSublayer((AL_BitStreamLite *)(intptr_t)a0_41, a1_38, a2_2, a3_1);
        return 0;
    }
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0xda8]);
    /* offset 0xda9 */
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint16_t *)(arg2 + 0xda9));
    AL_BitStreamLite_PutBit(arg1, 0);
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0xdac]);
    AL_BitStreamLite_PutU(arg1, 1, 1);
    return AL_BitStreamLite_AlignWithBits(arg1, 0);
}

static void writeSublayer(AL_BitStreamLite *arg1, char *arg2, int32_t *arg3, int32_t arg4)
{
    int32_t *s0_1;
    char *s2_1;
    int32_t s3_1;

    if (arg4 < 0) {
        return;
    }
    s0_1 = arg3;
    s2_1 = (char *)&arg3[0x80];
    s3_1 = 0;
    do {
        AL_BitStreamLite_PutBit(arg1, (uint8_t)*s2_1);
        AL_BitStreamLite_PutUE(arg1, (uint32_t)*s0_1);
        AL_BitStreamLite_PutUE(arg1, (uint32_t)s0_1[0x20]);
        if ((uint8_t)*arg2 != 0) {
            AL_BitStreamLite_PutUE(arg1, (uint32_t)s0_1[0x40]);
            AL_BitStreamLite_PutUE(arg1, (uint32_t)s0_1[0x60]);
        }
        s0_1 = &s0_1[1];
        s2_1 = &s2_1[1];
        s3_1 += 1;
    } while (arg4 >= s3_1);
}

static int32_t writeHrdParam(AL_BitStreamLite *arg1, char *arg2, int32_t arg3, int32_t arg4)
{
    char *s7;
    int32_t *s0;
    int32_t s2;
    int32_t result;

    if (arg3 != 0) {
        AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0]);
        AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[1]);
        if ((uint8_t)arg2[0] != 0) {
            AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[2]);
            if ((uint8_t)arg2[2] != 0) {
                AL_BitStreamLite_PutU(arg1, 8, (uint32_t)(uint8_t)arg2[3]);
                AL_BitStreamLite_PutU(arg1, 5, (uint32_t)(uint8_t)arg2[4]);
                AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[5]);
                AL_BitStreamLite_PutU(arg1, 5, (uint32_t)(uint8_t)arg2[0xc]);
            }
            AL_BitStreamLite_PutU(arg1, 4, (uint32_t)(uint8_t)arg2[7]);
            AL_BitStreamLite_PutU(arg1, 4, (uint32_t)(uint8_t)arg2[8]);
            if ((uint8_t)arg2[2] != 0) {
                AL_BitStreamLite_PutU(arg1, 4, (uint32_t)(uint8_t)arg2[9]);
            }
            AL_BitStreamLite_PutU(arg1, 5, (uint32_t)(uint8_t)arg2[0xa]);
            AL_BitStreamLite_PutU(arg1, 5, (uint32_t)(uint8_t)arg2[0xb]);
            AL_BitStreamLite_PutU(arg1, 5, (uint32_t)(uint8_t)arg2[0xc]);
        }
    }
    s7 = &arg2[0xe];
    s0 = (int32_t *)&arg2[0x48];
    s2 = 0;
    while (1) {
        AL_BitStreamLite_PutBit(arg1, (uint8_t)*s7);
        if ((uint8_t)*s7 != 0) {
            result = AL_BitStreamLite_PutUE(arg1, (uint32_t)*(s0 - 0x28 / 4));
            if ((uint8_t)s7[0x32] != 0) {
                goto label_7592c;
            }
            goto label_759a0;
        }
        AL_BitStreamLite_PutBit(arg1, (uint8_t)s7[8]);
        if ((uint8_t)s7[8] == 0) {
            result = AL_BitStreamLite_PutBit(arg1, (uint8_t)s7[0x32]);
            if ((uint8_t)s7[0x32] != 0) {
                goto label_7592c;
            }
            goto label_759a0;
        }

label_7592c:
        if ((uint8_t)arg2[0] != 0) {
            writeSublayer(arg1, &arg2[2], (int32_t *)&arg2[0x68], *s0);
            result = 0;
            if ((uint8_t)arg2[1] == 0) {
                goto label_75940;
            }
            s2 += 1;
            writeSublayer(arg1, &arg2[2], (int32_t *)&arg2[0x288], *s0);
            result = 0;
            s7 = &s7[1];
            s0 = &s0[1];
            if (arg4 < s2) {
                break;
            }
            continue;
        }

label_75938:
        if ((uint8_t)arg2[1] == 0) {
label_75940:
            s2 += 1;
            s7 = &s7[1];
            s0 = &s0[1];
            if (arg4 < s2) {
                break;
            }
            continue;
        }
        s2 += 1;
        writeSublayer(arg1, &arg2[2], (int32_t *)&arg2[0x288], *s0);
        result = 0;
        s7 = &s7[1];
        s0 = &s0[1];
        if (arg4 < s2) {
            break;
        }
        continue;

label_759a0:
        result = AL_BitStreamLite_PutUE(arg1, (uint32_t)*s0);
        if ((uint8_t)arg2[0] == 0) {
            goto label_75938;
        }
        writeSublayer(arg1, &arg2[2], (int32_t *)&arg2[0x68], *s0);
        result = 0;
        if ((uint8_t)arg2[1] == 0) {
            goto label_75940;
        }
        s2 += 1;
        writeSublayer(arg1, &arg2[2], (int32_t *)&arg2[0x288], *s0);
        result = 0;
        s7 = &s7[1];
        s0 = &s0[1];
        if (arg4 < s2) {
            break;
        }
    }
    return result;
}

static int32_t writeProfileTierLevel(AL_BitStreamLite *arg1, char *arg2, int32_t arg3)
{
    char *i;
    uint32_t v0;
    uint32_t v0_2;
    int32_t result;

    AL_BitStreamLite_PutU(arg1, 2, (uint32_t)(uint8_t)arg2[0]);
    i = &arg2[3];
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[1]);
    AL_BitStreamLite_PutU(arg1, 5, (uint32_t)(uint8_t)arg2[2]);
    do {
        uint32_t a1_1;

        a1_1 = (uint32_t)(uint8_t)*i;
        i = &i[1];
        AL_BitStreamLite_PutBit(arg1, (uint8_t)a1_1);
    } while (i != &arg2[0x23]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x23]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x24]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x25]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x26]);
    v0 = (uint32_t)(uint8_t)arg2[2];
    if (v0 == 4 || (uint8_t)arg2[7] != 0 || v0 == 5 || (uint8_t)arg2[8] != 0 || v0 == 6 || (uint8_t)arg2[9] != 0 ||
        v0 == 7 || (uint8_t)arg2[0xa] != 0) {
        int32_t a0_18;

        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x2a]);
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x2b]);
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x2c]);
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x2d]);
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x2e]);
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x2f]);
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x30]);
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x31]);
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x32]);
        v0_2 = (uint32_t)(uint8_t)arg2[2];
        if (v0_2 == 5 || (uint8_t)arg2[8] != 0 || v0_2 == 9) {
            a0_18 = (int32_t)(intptr_t)arg1;
label_75e08:
            AL_BitStreamLite_PutU((AL_BitStreamLite *)(intptr_t)a0_18, 1, 0);
            AL_BitStreamLite_PutU(arg1, 0x10, 0);
            AL_BitStreamLite_PutU(arg1, 0x11, 0);
        } else {
            a0_18 = (int32_t)(intptr_t)arg1;
            if ((uint8_t)arg2[0xc] != 0 || v0_2 == 0xa || (uint8_t)arg2[0xd] != 0) {
                goto label_75e08;
            }
            AL_BitStreamLite_PutU((AL_BitStreamLite *)(intptr_t)a0_18, 0x10, 0);
            AL_BitStreamLite_PutU(arg1, 0x12, 0);
        }
    } else {
        AL_BitStreamLite_PutU(arg1, 0x10, 0);
        AL_BitStreamLite_PutU(arg1, 0x1b, 0);
    }
    AL_BitStreamLite_PutU(arg1, 1, 0);
    result = AL_BitStreamLite_PutU(arg1, 8, (uint32_t)(uint8_t)arg2[0x33]);
    if (arg3 != 0) {
        char *i_3;
        char *i_1;

        i_3 = &arg2[0x34];
        i_1 = i_3;
        do {
            uint32_t a1_6;

            a1_6 = (uint32_t)(uint8_t)*i_1;
            i_1 = &i_1[1];
            AL_BitStreamLite_PutBit(arg1, (uint8_t)a1_6);
            AL_BitStreamLite_PutBit(arg1, (uint8_t)i_1[7]);
        } while (i_1 != &arg2[arg3 + 0x34]);
        if (arg3 == 0) {
label_75ef4:
            char *s0_1;
            int32_t s5_3;

            s0_1 = &arg2[0x5c];
            s5_3 = 0;
            while (1) {
                char *s2;

                s2 = &s0_1[0x20];
                if ((uint8_t)*i_3 != 0) {
                    AL_BitStreamLite_PutU(arg1, 2, (uint32_t)(uint8_t)i_3[0x10]);
                    AL_BitStreamLite_PutBit(arg1, (uint8_t)i_3[0x18]);
                    AL_BitStreamLite_PutU(arg1, 5, (uint32_t)(uint8_t)i_3[0x20]);
                    do {
                        uint32_t a1_9;

                        a1_9 = (uint32_t)(uint8_t)*s0_1;
                        s0_1 = &s0_1[1];
                        AL_BitStreamLite_PutBit(arg1, (uint8_t)a1_9);
                    } while (s0_1 != s2);
                    AL_BitStreamLite_PutBit(arg1, (uint8_t)i_3[0x128]);
                    AL_BitStreamLite_PutBit(arg1, (uint8_t)i_3[0x130]);
                    AL_BitStreamLite_PutBit(arg1, (uint8_t)i_3[0x138]);
                    AL_BitStreamLite_PutBit(arg1, (uint8_t)i_3[0x140]);
                    AL_BitStreamLite_PutU(arg1, 0x20, 0);
                    AL_BitStreamLite_PutU(arg1, 0xc, 0);
                    if ((uint8_t)i_3[8] == 0) {
                        goto label_75f0c;
                    }
                } else if ((uint8_t)i_3[8] == 0) {
label_75f0c:
                    s5_3 += 1;
                    result = s5_3 < arg3 ? 1 : 0;
                    i_3 = &i_3[1];
                    s0_1 = s2;
                    if (result == 0) {
                        break;
                    }
                    continue;
                }
                s5_3 += 1;
                AL_BitStreamLite_PutU(arg1, 8, (uint32_t)(uint8_t)i_3[0x190]);
                result = s5_3 < arg3 ? 1 : 0;
                i_3 = &i_3[1];
                s0_1 = s2;
                if (result == 0) {
                    break;
                }
            }
        } else {
            int32_t i_2;

            i_2 = arg3;
            if (arg3 != 8) {
                do {
                    i_2 += 1;
                    result = AL_BitStreamLite_PutU(arg1, 2, 0);
                } while (i_2 != 8);
            }
            if (arg3 != 0) {
                goto label_75ef4;
            }
        }
    }
    return result;
}

int32_t writeSps(AL_BitStreamLite *arg1, char *arg2, int32_t arg3)
{
    int32_t s2_2;

    AL_BitStreamLite_PutU(arg1, 4, (uint32_t)(uint8_t)arg2[0]);
    if (arg3 != 0) {
        AL_BitStreamLite_PutU(arg1, 3, (uint32_t)(uint8_t)arg2[2]);
        if ((uint8_t)arg2[2] != 7) {
            goto label_76138;
        }
        /* offset 0x1d0 */
        AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)(arg2 + 0x1d0));
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x1d1]);
        if ((uint8_t)arg2[0x1d1] != 0) {
            AL_BitStreamLite_PutU(arg1, 8, (uint32_t)(uint8_t)arg2[0x1d2]);
        }
        /* offset 0x1e6 */
        AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)(arg2 + 0x1e6));
        s2_2 = 1;
        goto label_76314;
    }
    AL_BitStreamLite_PutU(arg1, 3, (uint32_t)(uint8_t)arg2[1]);

label_76138:
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[3]);
    writeProfileTierLevel(arg1, &arg2[4], (uint32_t)(uint8_t)arg2[1]);
    /* offset 0x1d0 */
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)(arg2 + 0x1d0));
    /* offset 0x1d3 */
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)(arg2 + 0x1d3));
    if ((uint8_t)arg2[0x1d3] == 3) {
        __assert("pSps->chroma_format_idc != 3",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/HEVC_RbspEncod.c",
                 0x1b1, "writeSpsData", &_gp);
    }
    /* offset 0x1d6 */
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)(arg2 + 0x1d6));
    /* offset 0x1d8 */
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)(arg2 + 0x1d8));
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x1da]);
    if ((uint8_t)arg2[0x1da] != 0) {
        /* offset 0x1dc */
        AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)(arg2 + 0x1dc));
        /* offset 0x1de */
        AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)(arg2 + 0x1de));
        /* offset 0x1e0 */
        AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)(arg2 + 0x1e0));
        /* offset 0x1e2 */
        AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)(arg2 + 0x1e2));
    }
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x1e4]);
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x1e5]);
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x1e6]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x1e7]);
    if ((uint8_t)arg2[0x1e7] == 0) {
        uint32_t i_3;
        void *s2_1;
        void *s3_3;

        i_3 = (uint32_t)(uint8_t)arg2[1];
        s2_1 = &arg2[i_3];
        s3_3 = &arg2[(i_3 + 0x7e) << 2];
        do {
            i_3 += 1;
            AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)((char *)s2_1 + 0x1e8));
            AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)((char *)s2_1 + 0x1f0));
            s2_1 = (char *)s2_1 + 1;
            s3_3 = (char *)s3_3 + 4;
            AL_BitStreamLite_PutUE(arg1, *(uint32_t *)((char *)s3_3 - 4));
        } while ((int32_t)(uint8_t)arg2[1] >= (int32_t)i_3);
        s2_2 = 0;
        goto label_76314;
    }
    s2_2 = 1;

label_76314:
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x218]);
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x219]);
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x21a]);
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x21b]);
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x21d]);
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x21c]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x21e]);
    if ((uint8_t)arg2[0x21e] != 0) {
        if (s2_2 != 0) {
            AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x21f]);
            if ((uint8_t)arg2[0x21f] == 0) {
                goto label_7663c;
            }
            AL_BitStreamLite_PutU(arg1, 6, (uint32_t)(uint8_t)arg2[0x220]);
        } else {
            if ((uint8_t)arg2[0x21f] == 0) {
                goto label_7663c;
            }
            AL_BitStreamLite_PutU(arg1, 6, (uint32_t)(uint8_t)arg2[0x220]);
        }
    }
    goto label_763d4;

label_7663c:
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x221]);
    if ((uint8_t)arg2[0x221] != 0) {
        int32_t s5_3;

        s5_3 = 0;
        while (1) {
            int32_t fp_1;
            int32_t s2_4;
            const uint8_t *a0_49;
            int32_t v1_6;
            void *v1_7;
            int32_t s4_2;
            void *v0_29;

            fp_1 = s5_3 << 1;
            s2_4 = 1 << ((fp_1 + 4) & 0x1f);
            if (s2_4 >= 0x41) {
                s2_4 = 0x40;
            }
            a0_49 = AL_HEVC_ScanOrder4x4;
            v1_6 = (s5_3 << 3) - fp_1;
            v1_7 = &arg2[v1_6];
            if (s2_4 == 0x40) {
                a0_49 = AL_HEVC_ScanOrder8x8;
            }
            s4_2 = 0;
            v0_29 = v1_7;
            while (1) {
                char *s6_3;

                s6_3 = (char *)v0_29 + s4_2;
                AL_BitStreamLite_PutBit(arg1, (uint8_t)s6_3[0x222]);
                if ((uint8_t)s6_3[0x222] == 0) {
                    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)s6_3[0x23a]);
                } else {
                    uint32_t a1_40;
                    uint32_t i;
                    char *fp_2;

                    a1_40 = 8;
                    if (s5_3 >= 2) {
                        char *s6_4;

                        s6_4 = &arg2[(s5_3 - 2) * 6 + s4_2];
                        AL_BitStreamLite_PutSE(arg1, (uint32_t)(uint8_t)s6_4[0x252] - 8);
                        a1_40 = (uint32_t)(uint8_t)s6_4[0x252];
                    }
                    i = 0;
                    fp_2 = &arg2[(v1_6 + s4_2) << 6];
                    do {
                        const uint8_t *s7_1;
                        uint32_t a1_43;

                        s7_1 = a0_49 + i;
                        a1_43 = (uint32_t)(uint8_t)fp_2[*s7_1 + 0x25e] - a1_40;
                        if ((int32_t)a1_43 >= 0x80) {
                            a1_43 -= 0x100;
                        } else if ((int32_t)a1_43 < -0x7f) {
                            a1_43 += 0x100;
                        }
                        AL_BitStreamLite_PutSE(arg1, (int32_t)a1_43);
                        i = (uint8_t)(i + 1);
                        a1_40 = (uint32_t)(uint8_t)fp_2[*s7_1 + 0x25e];
                    } while (i < (uint32_t)(s2_4 & 0x3f));
                }
                if (s5_3 == 3) {
                    s4_2 += 3;
                    v0_29 = v1_7;
                    if (s4_2 >= 6) {
                        goto label_763d4;
                    }
                } else {
                    s4_2 += 1;
                    v0_29 = v1_7;
                    if (s4_2 >= 6) {
                        s5_3 += 1;
                        break;
                    }
                }
            }
        }
    }
    AL_BitStreamLite_PutU(arg1, 6, (uint32_t)(uint8_t)arg2[0x220]);

label_763d4:
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x872]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x873]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x874]);
    if ((uint8_t)arg2[0x874] != 0) {
        AL_BitStreamLite_PutU(arg1, 4, (uint32_t)(uint8_t)arg2[0x875]);
        AL_BitStreamLite_PutU(arg1, 4, (uint32_t)(uint8_t)arg2[0x876]);
        AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x877]);
        AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x878]);
        AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x879]);
    }
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x87a]);
    if ((uint8_t)arg2[0x87a] != 0) {
        int16_t *s3_4;
        int32_t s4_1;

        s3_4 = (int16_t *)(arg2 + 0x8c4);
        s4_1 = 0;
        while (1) {
            if ((uint8_t)*((char *)s3_4 - 0x48) != 0) {
                __assert("pRefPicSet->inter_ref_pic_set_prediction_flag == 0",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/HEVC_RbspEncod.c",
                         0xfd, "writeStRefPicSet", &_gp);
            }
            AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint16_t)*(s3_4 - 0x22));
            AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint16_t)*(s3_4 - 0x21));
            if ((uint16_t)*(s3_4 - 0x22) != 0) {
                int16_t *s6_1;
                char *s5_1;
                int32_t i_1;

                s6_1 = &s3_4[-0x10];
                s5_1 = (char *)&s3_4[0x10];
                i_1 = 0;
                do {
                    i_1 += 1;
                    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint16_t)*s6_1);
                    AL_BitStreamLite_PutBit(arg1, (uint8_t)*s5_1);
                    s6_1 = &s6_1[1];
                    s5_1 = &s5_1[1];
                } while (i_1 < (int32_t)(uint16_t)*(s3_4 - 0x22));
            }
            if ((uint16_t)*(s3_4 - 0x21) != 0) {
                char *s6_2;
                int16_t *s5_2;
                int32_t i_2;

                s6_2 = (char *)&s3_4[0x18];
                s5_2 = s3_4;
                i_2 = 0;
                do {
                    i_2 += 1;
                    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint16_t)*s5_2);
                    AL_BitStreamLite_PutBit(arg1, (uint8_t)*s6_2);
                    s5_2 = &s5_2[1];
                    s6_2 = &s6_2[1];
                } while (i_2 < (int32_t)(uint16_t)*(s3_4 - 0x21));
            }
            s4_1 += 1;
            if (s4_1 >= (int32_t)(uint8_t)arg2[0x87a]) {
                break;
            }
            AL_BitStreamLite_PutBit(arg1, (uint8_t)((char *)&s3_4[0x20])[0]);
            s3_4 = &s3_4[0x44];
        }
    }
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x2b04]);
    if ((uint8_t)arg2[0x2b04] != 0) {
        AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x2b05]);
    }
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x2b69]);
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x2b6a]);
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x2b6b]);
    if ((uint8_t)arg2[0x2b6b] != 0) {
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x2b6c]);
        if ((uint8_t)arg2[0x2b6c] != 0) {
            AL_BitStreamLite_PutU(arg1, 8, (uint32_t)(uint8_t)arg2[0x2b6d]);
            if ((uint8_t)arg2[0x2b6d] == 0xff) {
                /* offset 0x2b6e */
                AL_BitStreamLite_PutU(arg1, 0x10, (uint32_t)(uint16_t)*(uint32_t *)(arg2 + 0x2b6e));
                /* offset 0x2b70 */
                AL_BitStreamLite_PutU(arg1, 0x10, (uint32_t)(uint16_t)*(uint32_t *)(arg2 + 0x2b70));
            }
        }
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x2b72]);
        if ((uint8_t)arg2[0x2b72] == 0) {
            AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x2b74]);
            if ((uint8_t)arg2[0x2b74] != 0) {
                AL_BitStreamLite_PutU(arg1, 3, (uint32_t)(uint8_t)arg2[0x2b75]);
                AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x2b76]);
                AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x2b77]);
                if ((uint8_t)arg2[0x2b77] != 0) {
                    AL_BitStreamLite_PutU(arg1, 8, (uint32_t)(uint8_t)arg2[0x2b78]);
                    AL_BitStreamLite_PutU(arg1, 8, (uint32_t)(uint8_t)arg2[0x2b79]);
                    AL_BitStreamLite_PutU(arg1, 8, (uint32_t)(uint8_t)arg2[0x2b7a]);
                }
            }
            AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x2b7b]);
            if ((uint8_t)arg2[0x2b7b] != 0) {
                AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x2b7c]);
                AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x2b7d]);
            }
            AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x2b7e]);
            AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x2b7f]);
            AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x2b80]);
            AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x2b81]);
            if ((uint8_t)arg2[0x2b81] != 0) {
                /* offset 0x2b84 */
                AL_BitStreamLite_PutUE(arg1, *(uint32_t *)(arg2 + 0x2b84));
                /* offset 0x2b88 */
                AL_BitStreamLite_PutUE(arg1, *(uint32_t *)(arg2 + 0x2b88));
                /* offset 0x2b8c */
                AL_BitStreamLite_PutUE(arg1, *(uint32_t *)(arg2 + 0x2b8c));
                /* offset 0x2b90 */
                AL_BitStreamLite_PutUE(arg1, *(uint32_t *)(arg2 + 0x2b90));
            }
            AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x2b94]);
            if ((uint8_t)arg2[0x2b94] != 0) {
                /* offset 0x2b98 */
                AL_BitStreamLite_PutU(arg1, 0x20, *(uint32_t *)(arg2 + 0x2b98));
                /* offset 0x2b9c */
                AL_BitStreamLite_PutU(arg1, 0x20, *(uint32_t *)(arg2 + 0x2b9c));
                AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x2ba0]);
                if ((uint8_t)arg2[0x2ba0] != 0) {
                    /* offset 0x2ba4 */
                    AL_BitStreamLite_PutUE(arg1, *(uint32_t *)(arg2 + 0x2ba4));
                }
                AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x2ba8]);
                if ((uint8_t)arg2[0x2ba8] != 0) {
                    writeHrdParam(arg1, &arg2[0x2bac], 1, (uint32_t)(uint8_t)arg2[1]);
                }
            }
            AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x3060]);
            if ((uint8_t)arg2[0x3060] != 0) {
                AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x3061]);
                AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x3062]);
                AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x3063]);
                AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x3064]);
                AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x3065]);
                AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x3066]);
                AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x3067]);
                AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)arg2[0x3068]);
            }
        }
    }
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x306c]);
    if ((uint8_t)arg2[0x306c] != 0) {
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x306d]);
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x306e]);
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x306f]);
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x3070]);
        AL_BitStreamLite_PutU(arg1, 4, (uint32_t)(uint8_t)arg2[0x3071]);
    }
    if ((uint8_t)arg2[0x306e] != 0) {
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0x3073]);
    }
    AL_BitStreamLite_PutU(arg1, 1, 1);
    return AL_BitStreamLite_AlignWithBits(arg1, 0);
}

int32_t writeVps(AL_BitStreamLite *arg1, char *arg2)
{
    uint32_t i;
    void *s0;
    void *s1_2;
    char *s1_3;
    int32_t i_1;

    AL_BitStreamLite_PutU(arg1, 4, (uint32_t)(uint8_t)arg2[0]);
    i = 0;
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[1]);
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[2]);
    AL_BitStreamLite_PutU(arg1, 6, (uint32_t)(uint8_t)arg2[3]);
    AL_BitStreamLite_PutU(arg1, 3, (uint32_t)(uint8_t)arg2[4]);
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[5]);
    AL_BitStreamLite_PutU(arg1, 0x10, 0xffff);
    writeProfileTierLevel(arg1, &arg2[8], (uint32_t)(uint8_t)arg2[4]);
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[6]);
    if ((uint8_t)arg2[6] == 0) {
        i = (uint32_t)(uint8_t)arg2[4];
    }
    s0 = &arg2[i];
    s1_2 = &arg2[(i + 0x79) << 2];
    do {
        i += 1;
        AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)((char *)s0 + 0x1d4));
        AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)((char *)s0 + 0x1dc));
        s0 = (char *)s0 + 1;
        s1_2 = (char *)s1_2 + 4;
        AL_BitStreamLite_PutUE(arg1, *(uint32_t *)((char *)s1_2 - 4));
    } while ((int32_t)(uint8_t)arg2[4] >= (int32_t)i);
    AL_BitStreamLite_PutU(arg1, 6, (uint32_t)(uint8_t)arg2[0x204]);
    s1_3 = &arg2[0x248];
    i_1 = 0;
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)(arg2 + 0x206));
    if (*(uint32_t *)(arg2 + 0x206) != 0) {
        do {
            i_1 += 1;
            AL_BitStreamLite_PutBit(arg1, (uint8_t)*s1_3);
            s1_3 = &s1_3[1];
        } while ((int32_t)(uint8_t)arg2[0x204] >= i_1);
    }
    AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x288]);
    if ((uint8_t)arg2[0x288] != 0) {
        char *s0_1;
        char *s6_1;
        int16_t *s5_1;
        int32_t s4;
        uint32_t a2_13;

        /* offset 0x28c */
        AL_BitStreamLite_PutU(arg1, 0x20, *(uint32_t *)(arg2 + 0x28c));
        /* offset 0x290 */
        AL_BitStreamLite_PutU(arg1, 0x20, *(uint32_t *)(arg2 + 0x290));
        AL_BitStreamLite_PutBit(arg1, (uint8_t)arg2[0x294]);
        if ((uint8_t)arg2[0x294] != 0) {
            /* offset 0x298 */
            AL_BitStreamLite_PutUE(arg1, *(uint32_t *)(arg2 + 0x298));
        }
        /* offset 0x29c */
        AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)(arg2 + 0x29c));
        if (*(uint32_t *)(arg2 + 0x29c) != 0) {
            /* offset 0x29e */
            AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint8_t)*(uint32_t *)(arg2 + 0x29e));
            s0_1 = &arg2[0x2a2];
            s6_1 = &arg2[0x2a4];
            s5_1 = (int16_t *)&arg2[0x2a0];
            s4 = 0;
            a2_13 = (uint32_t)(uint8_t)*s0_1;
            while (1) {
                writeHrdParam(arg1, s6_1, (int32_t)a2_13, (uint32_t)(uint8_t)arg2[4]);
                s4 += 1;
                if (s4 >= *(int32_t *)(arg2 + 0x29c)) {
                    break;
                }
                s0_1 = &s0_1[1];
                s6_1 = &s6_1[0x4a8];
                AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint16_t)*s5_1);
                s5_1 = &s5_1[1];
                AL_BitStreamLite_PutBit(arg1, (uint8_t)*s0_1);
                a2_13 = (uint32_t)(uint8_t)*s0_1;
            }
        }
    }
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(uint8_t)arg2[0xbf4]);
    AL_BitStreamLite_PutU(arg1, 1, 1);
    return AL_BitStreamLite_AlignWithBits(arg1, 0);
}

void *AL_GetHevcRbspWriter(void)
{
    return (void *)writer;
}
