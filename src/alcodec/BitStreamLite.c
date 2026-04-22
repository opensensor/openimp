#include <stdint.h>

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, void *caller);

typedef struct AL_BitStreamLite {
    uint8_t *buffer;
    int32_t bit_count;
    int32_t size_bits;
    uint8_t reset_flag;
    uint8_t reserved_0d;
    uint8_t reserved_0e;
    uint8_t reserved_0f;
} AL_BitStreamLite;

static int32_t writeData(AL_BitStreamLite *arg1, uint8_t arg2, uint32_t arg3);
int32_t AL_BitStreamLite_PutBits(AL_BitStreamLite *arg1, uint8_t arg2, uint32_t arg3);
int32_t AL_BitStreamLite_PutBit(AL_BitStreamLite *arg1, uint8_t arg2);
int32_t AL_BitStreamLite_AlignWithBits(void *arg1, uint8_t arg2);

int32_t AL_BitStreamLite_Init(AL_BitStreamLite *arg1, uint8_t *arg2, int32_t arg3)
{
    arg1->buffer = arg2;
    arg1->bit_count = 0;
    arg1->size_bits = arg3 << 3;
    arg1->reset_flag = 0;
    return 0;
}

int32_t AL_BitStreamLite_Deinit(AL_BitStreamLite *arg1)
{
    arg1->buffer = 0;
    arg1->bit_count = 0;
    return 0;
}

int32_t AL_BitStreamLite_Reset(void *arg1)
{
    *(int32_t *)((char *)arg1 + 4) = 0;
    return 0;
}

uint8_t *AL_BitStreamLite_GetData(AL_BitStreamLite *arg1)
{
    return arg1->buffer;
}

uint8_t *AL_BitStreamLite_GetCurData(AL_BitStreamLite *arg1)
{
    int32_t a1;
    int32_t v0;

    a1 = arg1->bit_count;
    v0 = a1 + 7;
    if (a1 >= 0)
        v0 = a1;
    return arg1->buffer + (v0 >> 3);
}

int32_t AL_BitStreamLite_GetBitsCount(void *arg1)
{
    return *(int32_t *)((char *)arg1 + 4);
}

static int32_t writeData(AL_BitStreamLite *arg1, uint8_t arg2, uint32_t arg3)
{
    int32_t v1_1;
    uint8_t a3_1;
    uint32_t v0_2;
    int32_t t2_1;

    v1_1 = arg1->bit_count;
    a3_1 = (uint8_t)(v1_1 & 7);
    v0_2 = (uint32_t)(8 - a3_1);
    if (v0_2 < (uint32_t)arg2)
        goto label_72d1c;
    t2_1 = arg1->size_bits;
    goto label_72de0;

label_72d1c:
    t2_1 = arg1->size_bits;

    while (1) {
        int32_t t0_4;
        uint32_t a3_2;
        uint32_t a1_1;

        t0_4 = (int32_t)v0_2 + v1_1;
        a3_2 = (uint32_t)a3_1;
        if (t2_1 < t0_4) {
            arg1->reset_flag = 1;
            v1_1 = t0_4;
            a1_1 = (uint32_t)arg2 - v0_2;
        } else {
            int32_t v1_2;

            v1_2 = v1_1 >> 3;
            if ((int32_t)(a3_2 + v0_2) >= 9) {
                __assert("byteOffset + iNumBits <= 8",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/BitStreamLite.c",
                         0x4d, "writeData", &_gp);
            }
            a1_1 = (uint32_t)arg2 - v0_2;
            {
                uint32_t t0_8;
                uint8_t *t1_3;

                t0_8 = (uint32_t)(uint8_t)(arg3 >> (a1_1 & 0x1f));
                t1_3 = arg1->buffer;
                if (a3_2 == 0) {
                    *(t1_3 + v1_2) = (uint8_t)(t0_8 << ((8 - v0_2) & 0x1f));
                    t2_1 = arg1->size_bits;
                    v1_1 = (int32_t)v0_2 + arg1->bit_count;
                } else {
                    uint8_t *v1_3;
                    uint32_t t1_5;

                    v1_3 = t1_3 + v1_2;
                    *v1_3 = (uint8_t)(*v1_3 + (uint8_t)(t0_8 << ((8 - a3_2 - v0_2) & 0x1f)));
                    t1_5 = 0xffffffffU >> (v0_2 & 0x1f);
                    arg2 = (uint8_t)a1_1;
                    v1_1 = (int32_t)v0_2 + arg1->bit_count;
                    a3_1 = (uint8_t)(v1_1 & 7);
                    v0_2 = (uint32_t)(8 - a3_1);
                    t2_1 = arg1->size_bits;
                    arg1->bit_count = v1_1;
                    arg3 &= t1_5;
                    if (v0_2 >= (uint32_t)arg2)
                        goto label_72de0;
                    continue;
                }
            }
        }
        a3_1 = (uint8_t)(v1_1 & 7);
        {
            uint32_t t1_1;

            t1_1 = 0xffffffffU >> (v0_2 & 0x1f);
            arg2 = (uint8_t)a1_1;
            v0_2 = (uint32_t)(8 - a3_1);
            arg1->bit_count = v1_1;
            arg3 &= t1_1;
            if (v0_2 >= (uint32_t)arg2)
                goto label_72de0;
        }
    }

label_72de0:
    {
        int32_t v0_3;

        v0_3 = v1_1 + (int32_t)(uint32_t)arg2;
        if (t2_1 < v0_3)
            arg1->reset_flag = 1;
        else {
            uint32_t a3_6;
            int32_t v1_7;

            a3_6 = (uint32_t)a3_1;
            v1_7 = v1_1 >> 3;
            if ((int32_t)(a3_6 + (uint32_t)arg2) < 9) {
                if (a3_6 == 0) {
                    *(arg1->buffer + v1_7) = (uint8_t)(arg3 << ((8 - (uint32_t)arg2) & 0x1f));
                    v0_3 = (int32_t)(uint32_t)arg2 + arg1->bit_count;
                    arg1->bit_count = v0_3;
                    return v0_3;
                }
                {
                    uint8_t *v1_8;
                    int32_t v0_10;

                    v1_8 = arg1->buffer + v1_7;
                    *v1_8 = (uint8_t)(*v1_8 + (uint8_t)(arg3 << ((8 - a3_6 - (uint32_t)arg2) & 0x1f)));
                    v0_10 = (int32_t)(uint32_t)arg2 + arg1->bit_count;
                    arg1->bit_count = v0_10;
                    return v0_10;
                }
            }
            __assert("byteOffset + iNumBits <= 8",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/BitStreamLite.c",
                     0x4d, "writeData", &_gp);
        }
        arg1->bit_count = v0_3;
        return v0_3;
    }
}

int32_t AL_BitStreamLite_PutBits(AL_BitStreamLite *arg1, uint8_t arg2, uint32_t arg3)
{
    if ((uint32_t)arg2 == 0x20U || (arg3 >> ((uint32_t)arg2 & 0x1f)) == 0)
        return writeData(arg1, arg2, arg3);

    {
        int32_t a0;
        int32_t a1_2 = 0;

        a0 = __assert("iNumBits == 32 || (uValue >> iNumBits) == 0",
                      "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/BitStreamLite.c",
                      0x67, "AL_BitStreamLite_PutBits", &_gp);
        return AL_BitStreamLite_PutBit((AL_BitStreamLite *)(intptr_t)a0, (uint8_t)a1_2);
    }
}

int32_t AL_BitStreamLite_PutBit(AL_BitStreamLite *arg1, uint8_t arg2)
{
    if ((uint32_t)arg2 < 2U)
        return AL_BitStreamLite_PutBits(arg1, 1, arg2);

    {
        int32_t a0_1;
        uint8_t a1_1 = 0;

        a0_1 = __assert("(iBit == 0) || (iBit == 1)",
                        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/BitStreamLite.c",
                        0x31, "AL_BitStreamLite_PutBit", &_gp);
        return AL_BitStreamLite_AlignWithBits((void *)(intptr_t)a0_1, a1_1);
    }
}

int32_t AL_BitStreamLite_AlignWithBits(void *arg1, uint8_t arg2)
{
    int32_t i;

    i = *(int32_t *)((char *)arg1 + 4) & 7;
    if (i != 0) {
        do {
            AL_BitStreamLite_PutBit((AL_BitStreamLite *)arg1, arg2);
            i = *(int32_t *)((char *)arg1 + 4) & 7;
        } while (i != 0);
    }
    return i;
}

uint32_t AL_BitStreamLite_EndOfSEIPayload(void *arg1)
{
    int32_t v0;
    uint8_t v1_1;
    uint32_t result;

    v0 = *(int32_t *)((char *)arg1 + 4);
    v1_1 = (uint8_t)(((uint32_t)(v0 >> 0x1f)) >> 0x1d);
    result = (uint32_t)((((uint8_t)v0 + v1_1) & 7) - v1_1);
    if (result == 0)
        return result;
    AL_BitStreamLite_PutBits((AL_BitStreamLite *)arg1, 1, 1);
    return (uint32_t)AL_BitStreamLite_AlignWithBits(arg1, 0);
}

int32_t AL_BitStreamLite_SkipBits(void *arg1, int32_t arg2)
{
    int32_t result;
    int32_t a1;
    int32_t v1_1;

    result = *(int32_t *)((char *)arg1 + 4);
    a1 = arg2 + result;
    v1_1 = *(int32_t *)((char *)arg1 + 8) < a1 ? 1 : 0;
    *(int32_t *)((char *)arg1 + 4) = a1;
    if (v1_1 != 0) {
        result = 1;
        *(uint8_t *)((char *)arg1 + 0xc) = 1;
    }
    return result;
}

int32_t AL_BitStreamLite_PutU(AL_BitStreamLite *arg1, uint8_t arg2, uint32_t arg3)
{
    return AL_BitStreamLite_PutBits(arg1, (uint8_t)(uint32_t)arg2, arg3);
}

int32_t bit_scan_reverse_soft(int32_t arg1)
{
    int32_t result;

    result = -1;
    while (arg1 != 0) {
        arg1 >>= 1;
        result += 1;
    }
    return result;
}

int32_t AL_BitStreamLite_PutUE(AL_BitStreamLite *arg1, int32_t arg2)
{
    int32_t v0_1;

    v0_1 = bit_scan_reverse_soft(arg2 + 1) << 1;
    if (v0_1 == 0)
        return AL_BitStreamLite_PutU(arg1, 1, 1);
    {
        uint32_t v0_2;
        int32_t s2;

        v0_2 = (uint32_t)v0_1 >> 1;
        s2 = (int32_t)(v0_2 & 0xff);
        AL_BitStreamLite_PutBits(arg1, (uint8_t)s2, 0);
        AL_BitStreamLite_PutBits(arg1, 1, 1);
        return AL_BitStreamLite_PutBits(arg1, (uint8_t)s2,
                                        (uint32_t)(arg2 + 1 - (1 << (v0_2 & 0x1f))));
    }
}

int32_t AL_BitStreamLite_PutSE(AL_BitStreamLite *arg1, int32_t arg2)
{
    int32_t v1;

    v1 = arg2 >> 0x1f;
    return AL_BitStreamLite_PutUE(arg1, ((((v1 ^ arg2) - v1) << 1) - (0 < arg2 ? 1 : 0)));
}

int32_t PutUV(AL_BitStreamLite *arg1, int32_t arg2)
{
    int32_t i;

    i = arg2;
    if (arg2 >= 0x100) {
        do {
            i -= 0xff;
            AL_BitStreamLite_PutU(arg1, 8, 0xff);
        } while (i >= 0x100);
    }
    return AL_BitStreamLite_PutU(arg1, 8, (uint32_t)i);
}
