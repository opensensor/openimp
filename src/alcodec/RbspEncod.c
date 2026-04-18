#include <stdint.h>

#include "alcodec/BitStreamLite.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, void *caller);

typedef struct AL_CabacCtx {
    int32_t ivlOffset;
    int32_t ivlCurrRange;
    int32_t bitsOutstanding;
    int32_t firstBitFlag;
} AL_CabacCtx;

static const uint32_t SliceTypeToPrimaryPicType[12] = {
    2, 1, 0, 4, 3, 7, 1, 7, 7, 0, 0, 0,
};

static const uint8_t AL_transIdxLPS[64] = {
    0x00, 0x00, 0x01, 0x02, 0x02, 0x04, 0x04, 0x05,
    0x06, 0x07, 0x08, 0x09, 0x09, 0x0b, 0x0b, 0x0c,
    0x0d, 0x0d, 0x0f, 0x0f, 0x10, 0x10, 0x12, 0x12,
    0x13, 0x13, 0x15, 0x15, 0x16, 0x16, 0x17, 0x18,
    0x18, 0x19, 0x1a, 0x1a, 0x1b, 0x1b, 0x1c, 0x1d,
    0x1d, 0x1e, 0x1e, 0x1e, 0x1f, 0x20, 0x20, 0x21,
    0x21, 0x21, 0x22, 0x22, 0x23, 0x23, 0x23, 0x24,
    0x24, 0x24, 0x25, 0x25, 0x25, 0x26, 0x26, 0x3f,
};

static const uint8_t AL_transIdxMPS[64] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
    0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
    0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3e, 0x3f,
};

static const uint8_t AL_TabRangeLPS[64][4] = {
    {0x80, 0xb0, 0xd0, 0xf0}, {0x80, 0xa7, 0xc5, 0xe3}, {0x80, 0x9e, 0xbb, 0xd8}, {0x7b, 0x96, 0xb2, 0xcd},
    {0x74, 0x8e, 0xa9, 0xc3}, {0x6f, 0x87, 0xa0, 0xb9}, {0x69, 0x80, 0x98, 0xaf}, {0x64, 0x7a, 0x90, 0xa6},
    {0x5f, 0x74, 0x89, 0x9e}, {0x5a, 0x6e, 0x82, 0x96}, {0x55, 0x68, 0x7b, 0x8e}, {0x51, 0x63, 0x75, 0x87},
    {0x4d, 0x5e, 0x6f, 0x80}, {0x49, 0x59, 0x69, 0x7a}, {0x45, 0x55, 0x64, 0x74}, {0x42, 0x50, 0x5f, 0x6e},
    {0x3e, 0x4c, 0x5a, 0x68}, {0x3b, 0x48, 0x56, 0x63}, {0x38, 0x45, 0x51, 0x5e}, {0x35, 0x41, 0x4d, 0x59},
    {0x33, 0x3e, 0x49, 0x55}, {0x30, 0x3b, 0x45, 0x50}, {0x2e, 0x38, 0x42, 0x4c}, {0x2b, 0x35, 0x3f, 0x48},
    {0x29, 0x32, 0x3b, 0x45}, {0x27, 0x30, 0x38, 0x41}, {0x25, 0x2d, 0x36, 0x3e}, {0x23, 0x2b, 0x33, 0x3b},
    {0x21, 0x29, 0x30, 0x38}, {0x20, 0x27, 0x2e, 0x35}, {0x1e, 0x25, 0x2b, 0x32}, {0x1d, 0x23, 0x29, 0x30},
    {0x1b, 0x21, 0x27, 0x2d}, {0x1a, 0x1f, 0x25, 0x2b}, {0x18, 0x1e, 0x23, 0x29}, {0x17, 0x1c, 0x21, 0x27},
    {0x16, 0x1b, 0x20, 0x25}, {0x15, 0x1a, 0x1e, 0x23}, {0x14, 0x18, 0x1d, 0x21}, {0x13, 0x17, 0x1b, 0x1f},
    {0x12, 0x16, 0x1a, 0x1e}, {0x11, 0x15, 0x19, 0x1c}, {0x10, 0x14, 0x17, 0x1b}, {0x0f, 0x13, 0x16, 0x19},
    {0x0e, 0x12, 0x15, 0x18}, {0x0e, 0x11, 0x14, 0x17}, {0x0d, 0x10, 0x13, 0x16}, {0x0c, 0x0f, 0x12, 0x15},
    {0x0c, 0x0e, 0x11, 0x14}, {0x0b, 0x0e, 0x10, 0x13}, {0x0b, 0x0d, 0x0f, 0x12}, {0x0a, 0x0c, 0x0f, 0x11},
    {0x0a, 0x0c, 0x0e, 0x10}, {0x09, 0x0b, 0x0d, 0x0f}, {0x09, 0x0b, 0x0c, 0x0e}, {0x08, 0x0a, 0x0c, 0x0e},
    {0x08, 0x09, 0x0b, 0x0d}, {0x07, 0x09, 0x0b, 0x0c}, {0x07, 0x09, 0x0a, 0x0c}, {0x07, 0x08, 0x0a, 0x0b},
    {0x06, 0x08, 0x09, 0x0b}, {0x06, 0x07, 0x09, 0x0a}, {0x06, 0x07, 0x08, 0x09}, {0x02, 0x02, 0x02, 0x02},
};

int32_t AL_RbspEncoding_BeginSEI2(AL_BitStreamLite *arg1, int32_t arg2, int32_t arg3);
int32_t AL_RbspEncoding_CloseSEI(AL_BitStreamLite *arg1);
int32_t PutBit(AL_BitStreamLite *arg1, AL_CabacCtx *arg2, uint8_t arg3);
int32_t RenormE(AL_BitStreamLite *arg1, AL_CabacCtx *arg2);

int32_t AL_RbspEncoding_WriteAUD(AL_BitStreamLite *arg1, int32_t arg2)
{
    AL_BitStreamLite_PutU(arg1, 3, SliceTypeToPrimaryPicType[arg2]);
    AL_BitStreamLite_PutU(arg1, 1, 1);
    return AL_BitStreamLite_AlignWithBits(arg1, 0);
}

int32_t AL_RbspEncoding_BeginSEI(AL_BitStreamLite *arg1, uint8_t arg2)
{
    int32_t result;

    AL_BitStreamLite_PutBits(arg1, 8, (uint32_t)arg2);
    result = AL_BitStreamLite_GetBitsCount(arg1);
    if ((result & 7) != 0) {
        int32_t a0_2;
        int32_t a1;
        int32_t a2_1;

        a0_2 = __assert("bookmarkSEI % 8 == 0",
                        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/RbspEncod.c",
                        0x1f, "AL_RbspEncoding_BeginSEI", &_gp);
        return AL_RbspEncoding_BeginSEI2((AL_BitStreamLite *)(intptr_t)a0_2, a1, a2_1);
    }
    AL_BitStreamLite_PutBits(arg1, 8, 0xff);
    return result;
}

int32_t AL_RbspEncoding_BeginSEI2(AL_BitStreamLite *arg1, int32_t arg2, int32_t arg3)
{
    PutUV(arg1, arg2);
    return PutUV(arg1, arg3);
}

int32_t AL_RbspEncoding_EndSEI(AL_BitStreamLite *arg1, int32_t arg2)
{
    int32_t s1;
    uint8_t *v0;
    uint8_t *s1_2;
    int32_t v0_2;

    s1 = arg2 + 7;
    v0 = AL_BitStreamLite_GetData(arg1);
    if (arg2 >= 0)
        s1 = arg2;
    s1_2 = v0 + (s1 >> 3);
    if ((uint32_t)(*s1_2) != 0xffU) {
        __assert("*pSize == 0xFF",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/RbspEncod.c",
                 0x3c, "AL_RbspEncoding_EndSEI", &_gp);
    }
    v0_2 = AL_BitStreamLite_GetBitsCount(arg1) - arg2;
    if ((v0_2 & 7) == 0) {
        if (v0_2 < 0)
            v0_2 += 7;
        *s1_2 = (uint8_t)((v0_2 >> 3) - 1);
        return (uint8_t)((v0_2 >> 3) - 1);
    }
    return AL_RbspEncoding_CloseSEI((AL_BitStreamLite *)(intptr_t)__assert(
        "bits % 8 == 0",
        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/RbspEncod.c",
        0x3e, "AL_RbspEncoding_EndSEI", &_gp));
}

int32_t AL_RbspEncoding_CloseSEI(AL_BitStreamLite *arg1)
{
    AL_BitStreamLite_PutU(arg1, 1, 1);
    return AL_BitStreamLite_AlignWithBits(arg1, 0);
}

int32_t AL_RbspEncoding_WriteUserDataUnregistered(AL_BitStreamLite *arg1, char *arg2, int8_t arg3)
{
    char *i;
    int32_t v0;

    i = arg2;
    v0 = AL_RbspEncoding_BeginSEI(arg1, 5);
    do {
        uint32_t a2;

        a2 = (uint32_t)(uint8_t)(*i);
        i = &i[1];
        AL_BitStreamLite_PutU(arg1, 8, a2);
    } while (i != &i[0x10]);
    AL_BitStreamLite_PutU(arg1, 8, (uint32_t)(int32_t)arg3);
    AL_RbspEncoding_EndSEI(arg1, v0);
    return AL_RbspEncoding_CloseSEI(arg1);
}

int32_t AL_RbspEncoding_WriteMasteringDisplayColourVolume(AL_BitStreamLite *arg1, int16_t *arg2)
{
    int32_t v0;
    int16_t *i;

    v0 = AL_RbspEncoding_BeginSEI(arg1, 0x89);
    i = arg2;
    do {
        AL_BitStreamLite_PutU(arg1, 0x10, (uint32_t)(uint16_t)(*i));
        AL_BitStreamLite_PutU(arg1, 0x10, (uint32_t)(uint16_t)i[1]);
        i = &i[2];
    } while (i != &arg2[6]);
    AL_BitStreamLite_PutU(arg1, 0x10, (uint32_t)(uint16_t)arg2[6]);
    AL_BitStreamLite_PutU(arg1, 0x10, (uint32_t)(uint16_t)arg2[7]);
    /* offset 0x10 */
    AL_BitStreamLite_PutU(arg1, 0x20, *(uint32_t *)((char *)arg2 + 0x10));
    /* offset 0x14 */
    AL_BitStreamLite_PutU(arg1, 0x20, *(uint32_t *)((char *)arg2 + 0x14));
    AL_BitStreamLite_EndOfSEIPayload(arg1);
    return AL_RbspEncoding_EndSEI(arg1, v0);
}

int32_t PutBit(AL_BitStreamLite *arg1, AL_CabacCtx *arg2, uint8_t arg3)
{
    int32_t i_1;
    int32_t result;

    if (arg2->firstBitFlag == 0) {
        return AL_BitStreamLite_PutBit(arg1, arg3);
    } else {
        arg2->firstBitFlag = 0;
        i_1 = arg2->bitsOutstanding;
        result = i_1 < 0x21 ? 1 : 0;
        if (i_1 != 0) {
            int32_t i_2;
            int32_t i;

            i_2 = i_1;
            if (result == 0) {
                i = i_1;
                do {
                    i -= 0x20;
                    AL_BitStreamLite_PutBits(arg1, 0x20, (uint32_t)(arg3 - 1));
                } while (i >= 0x21);
                i_2 = i_1 - 0x20 - ((i_1 - 0x21) & 0xffffffe0);
            }
            result = AL_BitStreamLite_PutBits(arg1, (uint8_t)(i_2 & 0xff),
                                              (uint32_t)(arg3 - 1) >> ((uint32_t)(-i_2) & 0x1f));
            arg2->bitsOutstanding = 0;
            return result;
        }
    }
    return 0;
}

int32_t RenormE(AL_BitStreamLite *arg1, AL_CabacCtx *arg2)
{
    int32_t result;
    int32_t a2_1;

    result = arg2->ivlCurrRange;
    if ((uint32_t)result < 0x100U) {
        a2_1 = arg2->ivlOffset;
        while (1) {
            if ((uint32_t)(a2_1 - 0x100) < 0x100U) {
                a2_1 = (a2_1 - 0x100) << 1;
                arg2->bitsOutstanding += 1;
                arg2->ivlOffset = a2_1;
                result <<= 1;
                arg2->ivlCurrRange = result;
                if ((uint32_t)result >= 0x100U)
                    break;
            } else {
                PutBit(arg1, arg2, (uint8_t)(((uint32_t)a2_1 >> 9) & 0xff));
                result = arg2->ivlCurrRange << 1;
                a2_1 = (arg2->ivlOffset & 0x1ff) << 1;
                arg2->ivlOffset = a2_1;
                arg2->ivlCurrRange = result;
                if ((uint32_t)result >= 0x100U)
                    break;
            }
        }
    }
    return result;
}

int32_t AL_Cabac_Init(AL_CabacCtx *arg1)
{
    arg1->ivlCurrRange = 0x1fe;
    arg1->ivlOffset = 0;
    arg1->bitsOutstanding = 0;
    arg1->firstBitFlag = 1;
    return 1;
}

int32_t AL_Cabac_WriteBin(AL_BitStreamLite *arg1, AL_CabacCtx *arg2, uint8_t *arg3, uint8_t *arg4, uint8_t arg5)
{
    int32_t t0;
    uint32_t v1_1;
    int32_t t0_1;

    t0 = arg2->ivlCurrRange;
    v1_1 = (uint32_t)AL_TabRangeLPS[(uint32_t)(*arg3)][((uint32_t)t0 >> 6) & 3];
    t0_1 = t0 - (int32_t)v1_1;
    arg2->ivlCurrRange = t0_1;
    if ((uint32_t)(*arg4) == (uint32_t)arg5) {
        *arg3 = AL_transIdxMPS[(uint32_t)(*arg3)];
        return RenormE(arg1, arg2);
    }
    arg2->ivlCurrRange = (int32_t)v1_1;
    arg2->ivlOffset += t0_1;
    if ((uint32_t)(*arg3) == 0)
        *arg4 = (uint8_t)(1 - *arg4);
    *arg3 = AL_transIdxLPS[(uint32_t)(*arg3)];
    return RenormE(arg1, arg2);
}

int32_t AL_Cabac_Terminate(AL_BitStreamLite *arg1, AL_CabacCtx *arg2, int32_t arg3)
{
    int32_t v0_1;

    v0_1 = arg2->ivlCurrRange - 2;
    arg2->ivlCurrRange = v0_1;
    if (arg3 != 0) {
        arg2->ivlCurrRange = 2;
        arg2->ivlOffset += v0_1;
    }
    return RenormE(arg1, arg2);
}

int32_t AL_Cabac_Finish(AL_BitStreamLite *arg1, AL_CabacCtx *arg2)
{
    PutBit(arg1, arg2, (uint8_t)(((uint32_t)arg2->ivlOffset >> 9) & 1));
    AL_BitStreamLite_PutBits(arg1, 2, (((uint32_t)arg2->ivlOffset >> 7) & 2) | 1);
    return AL_BitStreamLite_AlignWithBits(arg1, 0);
}
