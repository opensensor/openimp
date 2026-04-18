#include <stddef.h>
#include <stdint.h>

#include "alcodec/BitStreamLite.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function,
                        void *caller);

int32_t AL_RbspEncoding_BeginSEI(AL_BitStreamLite *arg1, uint8_t arg2); /* forward decl, ported by T15 later */
int32_t AL_RbspEncoding_EndSEI(AL_BitStreamLite *arg1, int32_t arg2); /* forward decl, ported by T15 later */
int32_t AL_RbspEncoding_WriteAUD(AL_BitStreamLite *arg1, int32_t arg2); /* forward decl, ported by T15 later */
int32_t AL_RbspEncoding_WriteUserDataUnregistered(AL_BitStreamLite *arg1, char *arg2,
                                                  int8_t arg3); /* forward decl, ported by T15 later */
int32_t AL_RbspEncoding_WriteMasteringDisplayColourVolume(AL_BitStreamLite *arg1,
                                                          int16_t *arg2); /* forward decl, ported by T15 later */

static int32_t writeSeiRecoveryPoint(AL_BitStreamLite *arg1, int32_t arg2);
static int32_t writeScalingMatrix(AL_BitStreamLite *arg1, int32_t arg2, const void *arg3);
static int32_t writePps(AL_BitStreamLite *arg1, const uint8_t *arg2);
void *AL_GetAvcRbspWriter(void);

static inline uint8_t read_u8(const void *ptr, uint32_t offset)
{
    return *(const uint8_t *)((const uint8_t *)ptr + offset);
}

static inline int8_t read_s8(const void *ptr, uint32_t offset)
{
    return *(const int8_t *)((const uint8_t *)ptr + offset);
}

static inline uint16_t read_u16(const void *ptr, uint32_t offset)
{
    return *(const uint16_t *)((const uint8_t *)ptr + offset);
}

static inline uint32_t read_u32(const void *ptr, uint32_t offset)
{
    return *(const uint32_t *)((const uint8_t *)ptr + offset);
}

static inline int32_t read_s32(const void *ptr, uint32_t offset)
{
    return *(const int32_t *)((const uint8_t *)ptr + offset);
}

static inline const void *read_ptr(const void *ptr, uint32_t offset)
{
    return (const void *)(uintptr_t)read_u32(ptr, offset);
}

static const uint32_t PicStructToNumClockTS[9] = {
    1, 1, 1, 2, 2, 3, 3, 2, 3,
};

static const int32_t AL_DecScanBlock8x8[128] = {
    0x00, 0x01, 0x08, 0x10, 0x09, 0x02, 0x03, 0x0a, 0x11, 0x18, 0x20, 0x19, 0x12, 0x0b, 0x04, 0x05,
    0x0c, 0x13, 0x1a, 0x21, 0x28, 0x30, 0x29, 0x22, 0x1b, 0x14, 0x0d, 0x06, 0x07, 0x0e, 0x15, 0x1c,
    0x23, 0x2a, 0x31, 0x38, 0x39, 0x32, 0x2b, 0x24, 0x1d, 0x16, 0x0f, 0x17, 0x1e, 0x25, 0x2c, 0x33,
    0x3a, 0x3b, 0x34, 0x2d, 0x26, 0x1f, 0x27, 0x2e, 0x35, 0x3c, 0x3d, 0x36, 0x2f, 0x37, 0x3e, 0x3f,
    0x00, 0x08, 0x10, 0x01, 0x09, 0x18, 0x20, 0x11, 0x02, 0x19, 0x28, 0x30, 0x38, 0x21, 0x0a, 0x03,
    0x12, 0x29, 0x31, 0x39, 0x1a, 0x0b, 0x04, 0x13, 0x22, 0x2a, 0x32, 0x3a, 0x1b, 0x0c, 0x05, 0x14,
    0x23, 0x2b, 0x33, 0x3b, 0x1c, 0x0d, 0x06, 0x15, 0x24, 0x2c, 0x34, 0x3c, 0x1d, 0x0e, 0x16, 0x25,
    0x2d, 0x35, 0x3d, 0x1e, 0x07, 0x0f, 0x26, 0x2e, 0x36, 0x3e, 0x17, 0x1f, 0x27, 0x2f, 0x37, 0x3f,
};

static const int32_t AL_DecScanBlock4x4[32] = {
    0x00, 0x01, 0x04, 0x08, 0x05, 0x02, 0x03, 0x06, 0x09, 0x0c, 0x0d, 0x0a, 0x07, 0x0b, 0x0e, 0x0f,
    0x00, 0x04, 0x01, 0x08, 0x0c, 0x05, 0x09, 0x0d, 0x02, 0x06, 0x0a, 0x0e, 0x03, 0x07, 0x0b, 0x0f,
};

static int32_t getCodec(void)
{
    return 0;
}

static int32_t writeSeiBufferingPeriod(AL_BitStreamLite *arg1, const void *arg2, uint32_t arg3)
{
    int32_t v0;
    int32_t s1_2;

    v0 = AL_RbspEncoding_BeginSEI(arg1, 0);
    AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 0x10));
    s1_2 = (int32_t)read_u8(arg2, 0xbce) + 1;
    AL_BitStreamLite_PutU(arg1, (uint8_t)s1_2, arg3);
    AL_BitStreamLite_PutU(arg1, (uint8_t)s1_2, 0);
    AL_BitStreamLite_EndOfSEIPayload(arg1);
    return AL_RbspEncoding_EndSEI(arg1, v0);
}

static int32_t writeHrdParam(AL_BitStreamLite *arg1, const void *arg2, int32_t *arg3)
{
    int32_t *s0;
    const uint8_t *s3;
    int32_t i;

    if (read_u8(arg2, 0x48) >= 0x20U) {
        int32_t a0_10;
        int32_t a1_4;

        a0_10 = __assert("pHrd->cpb_cnt_minus1[0] < 32",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/AVC_RbspEncod.c",
                         0x5f, "writeHrdParam", &_gp);
        return writeSeiRecoveryPoint((AL_BitStreamLite *)(intptr_t)a0_10, a1_4);
    }

    s0 = arg3;
    AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 0x48));
    AL_BitStreamLite_PutU(arg1, 4, (uint32_t)read_u8(arg2, 7));
    s3 = (const uint8_t *)&s0[0x80];
    AL_BitStreamLite_PutU(arg1, 4, (uint32_t)read_u8(arg2, 8));
    i = 0;

    do {
        int32_t a1_2;

        i += 1;
        AL_BitStreamLite_PutUE(arg1, *s0);
        a1_2 = s0[0x20];
        s0 = &s0[1];
        AL_BitStreamLite_PutUE(arg1, a1_2);
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(*s3));
        s3 = &s3[1];
    } while (read_u8(arg2, 0x48) >= (uint32_t)i);

    AL_BitStreamLite_PutU(arg1, 5, (uint32_t)read_u8(arg2, 0xa));
    AL_BitStreamLite_PutU(arg1, 5, (uint32_t)read_u8(arg2, 0xb));
    AL_BitStreamLite_PutU(arg1, 5, (uint32_t)read_u8(arg2, 0xc));
    return AL_BitStreamLite_PutU(arg1, 5, (uint32_t)read_u8(arg2, 0xd));
}

static int32_t writeSeiRecoveryPoint(AL_BitStreamLite *arg1, int32_t arg2)
{
    int32_t v0;

    v0 = AL_RbspEncoding_BeginSEI(arg1, 6);
    AL_BitStreamLite_PutUE(arg1, arg2);
    AL_BitStreamLite_PutBit(arg1, 1);
    AL_BitStreamLite_PutBit(arg1, 0);
    AL_BitStreamLite_PutBits(arg1, 2, 0);
    AL_BitStreamLite_EndOfSEIPayload(arg1);
    return AL_RbspEncoding_EndSEI(arg1, v0);
}

static int32_t writeSeiPictureTiming(AL_BitStreamLite *arg1, const void *arg2, uint32_t arg3, uint32_t arg4,
                                     uint32_t arg5)
{
    int32_t v0;

    v0 = AL_RbspEncoding_BeginSEI(arg1, 1);
    if (read_u8(arg2, 0xbc4) != 0) {
        AL_BitStreamLite_PutU(arg1, 0x20, arg3);
        AL_BitStreamLite_PutU(arg1, 0x20, arg4);
    }
    if (read_u8(arg2, 0x106f) != 0) {
        AL_BitStreamLite_PutU(arg1, 4, arg5);
        if (arg5 >= 9U) {
            int32_t a0_7;
            int32_t a1_3;
            void *a2_4;

            a0_7 = __assert("iPicStruct <= 8 && iPicStruct >= 0",
                            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/AVC_RbspEncod.c",
                            0x169, "writeSeiPictureTiming", &_gp);
            return writeScalingMatrix((AL_BitStreamLite *)(intptr_t)a0_7, a1_3, a2_4);
        }
        AL_BitStreamLite_PutBits(arg1, (uint8_t)PicStructToNumClockTS[arg5], 0);
    }
    AL_BitStreamLite_EndOfSEIPayload(arg1);
    return AL_RbspEncoding_EndSEI(arg1, v0);
}

static int32_t writeScalingMatrix(AL_BitStreamLite *arg1, int32_t arg2, const void *arg3)
{
    int32_t result;

    result = (int32_t)(intptr_t)"/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/FbcMapSize.c";
    if (arg2 != 0) {
        const uint8_t *s0_1;
        int32_t i;

        s0_1 = (const uint8_t *)arg3 + 0x17;
        i = 0;
        while (i != 8) {
            AL_BitStreamLite_PutU(arg1, 1, (uint32_t)(*s0_1));
            if (*s0_1 != 0) {
                int32_t v1_1;
                int32_t v0_1;
                const int32_t *v0_3;
                uint32_t a1;
                const uint8_t *s6_1;
                const uint8_t *fp_6;
                uint32_t a3_1;
                const int32_t *s7_1;
                const int32_t *s5_1;
                uint32_t v1_2;

                v1_1 = i < 6 ? 1 : 0;
                v0_1 = v1_1 ^ 1;
                if (v1_1 == 0) {
                    int32_t a0_3;

                    a0_3 = 3;
                    if (i != 7)
                        a0_3 = 0;
                    fp_6 = (const uint8_t *)arg3 + ((((v0_1 * 6) + a0_3) << 6) + 0x13b);
                    a3_1 = 0x40;
                    v0_3 = AL_DecScanBlock8x8;
                } else {
                    fp_6 = (const uint8_t *)arg3 + ((((v0_1 * 6) + i) << 6) + 0x13b);
                    a3_1 = 0x10;
                    v0_3 = AL_DecScanBlock4x4;
                }
                s7_1 = v0_3 + 1;
                s5_1 = v0_3 + a3_1;
                a1 = 8;
                s6_1 = fp_6;
                while (1) {
                    while (1) {
                        v1_2 = (uint32_t)(*s6_1);
                        if (s7_1 == s5_1)
                            goto label_73f8c;
                        s6_1 = fp_6 + *s7_1;
                        s7_1 = s7_1 + 1;
                        if (v1_2 != 0)
                            break;
                        a1 = (uint32_t)(*s6_1);
                    }
                    {
                        int32_t a1_1;

                        a1_1 = (int32_t)v1_2 - (int32_t)a1;
                        if ((uint32_t)a1_1 >= 0x80U)
                            a1_1 -= 0x100;
                        if (a1_1 < -0x80)
                            a1_1 += 0x100;
                        AL_BitStreamLite_PutSE(arg1, a1_1);
                        a1 = v1_2;
                        if (s7_1 == s5_1)
                            break;
                    }
                }
            }
label_73f8c:
            i += 1;
            result = 8;
            s0_1 = s0_1 + 1;
        }
    }
    return result;
}

static int32_t writeSps(AL_BitStreamLite *arg1, const uint8_t *arg2)
{
    uint32_t i;

    AL_BitStreamLite_PutU(arg1, 8, (uint32_t)read_u32(arg2, 0));
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 4));
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 5));
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 6));
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 7));
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 8));
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 9));
    AL_BitStreamLite_PutU(arg1, 2, 0);
    AL_BitStreamLite_PutU(arg1, 8, (uint32_t)read_u32(arg2, 0xc));
    AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 0x10));

    {
        int32_t v0;

        v0 = read_s32(arg2, 0);
        if (v0 == 0x7a || v0 == 0xf4 || v0 == 0x2c ||
            ((uint32_t)(v0 - 0x53) < 0x1cU && ((0x8020001U >> ((uint32_t)(v0 - 0x53) & 0x1f)) & 1U) != 0U) ||
            (v0 & 0xffffffdf) == 0x56 || v0 == 0x80) {
            AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 0x11));
            if (read_u8(arg2, 0x11) != 3) {
                AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 0x13));
                AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 0x14));
                AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0x15));
                AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0x16));
                writeScalingMatrix(arg1, (int32_t)read_u8(arg2, 0x16), arg2);
            } else {
                int32_t a0_62;
                char *a1_20;

                a0_62 = __assert("pSps->chroma_format_idc != 3",
                                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/AVC_RbspEncod.c",
                                 0x8d, "writeSpsData", &_gp);
                return writePps((AL_BitStreamLite *)(intptr_t)a0_62, (uint8_t *)a1_20);
            }
        }
    }

    AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 0x74f));
    AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 0x750));
    i = (uint32_t)read_u8(arg2, 0x750);
    if (i == 0) {
        AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 0x751));
    } else if (i != 1) {
        __assert("pSps->pic_order_cnt_type != 1",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/AVC_RbspEncod.c", 0x9c,
                 "writeSpsData", &_gp);
    }

    AL_BitStreamLite_PutUE(arg1, read_s32(arg2, 0xb60));
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xb64));
    AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 0xb66));
    AL_BitStreamLite_PutUE(arg1, (int32_t)read_u16(arg2, 0xb68));
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xb6a));
    if (read_u8(arg2, 0xb6a) == 0) {
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xb6b));
    }
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xb6c));
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xb6d));
    if (read_u8(arg2, 0xb6d) != 0) {
        AL_BitStreamLite_PutUE(arg1, read_s32(arg2, 0xb70));
        AL_BitStreamLite_PutUE(arg1, read_s32(arg2, 0xb74));
        AL_BitStreamLite_PutUE(arg1, read_s32(arg2, 0xb78));
        AL_BitStreamLite_PutUE(arg1, read_s32(arg2, 0xb7c));
    }
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xb80));
    if (read_u8(arg2, 0xb80) != 0) {
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xb81));
        if (read_u8(arg2, 0xb81) != 0) {
            AL_BitStreamLite_PutU(arg1, 8, (uint32_t)read_u8(arg2, 0xb85));
            if (read_u8(arg2, 0xb85) == 0xff) {
                AL_BitStreamLite_PutU(arg1, 0x10, (uint32_t)read_u16(arg2, 0xb86));
                AL_BitStreamLite_PutU(arg1, 0x10, (uint32_t)read_u16(arg2, 0xb88));
            }
        }
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xb8a));
        if (read_u8(arg2, 0xb8a) != 0) {
            AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xb8b));
        }
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xb8c));
        if (read_u8(arg2, 0xb8c) != 0) {
            AL_BitStreamLite_PutU(arg1, 3, (uint32_t)read_u8(arg2, 0xb8d));
            AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xb8e));
            AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xb8f));
            if (read_u8(arg2, 0xb8f) != 0) {
                AL_BitStreamLite_PutU(arg1, 8, (uint32_t)read_u8(arg2, 0xb90));
                AL_BitStreamLite_PutU(arg1, 8, (uint32_t)read_u8(arg2, 0xb91));
                AL_BitStreamLite_PutU(arg1, 8, (uint32_t)read_u8(arg2, 0xb92));
            }
        }
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xb93));
        if (read_u8(arg2, 0xb93) != 0) {
            AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 0xb94));
            AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 0xb95));
        }
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xbac));
        if (read_u8(arg2, 0xbac) != 0) {
            AL_BitStreamLite_PutU(arg1, 0x20, read_u32(arg2, 0xbb0));
            AL_BitStreamLite_PutU(arg1, 0x20, read_u32(arg2, 0xbb4));
            AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0x106d));
        }
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xbc4));
        if (read_u8(arg2, 0xbc4) != 0) {
            writeHrdParam(arg1, arg2 + 0xbc4, (int32_t *)(void *)(arg2 + 0xc2c));
        }
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xbc5));
        if (read_u8(arg2, 0xbc5) != 0) {
            writeHrdParam(arg1, arg2 + 0xbc4, (int32_t *)(void *)(arg2 + 0xe4c));
        }
        if (read_u16(arg2, 0xbc4) != 0) {
            AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0xc04));
        }
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0x106f));
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0x1078));
        if (read_u8(arg2, 0xb80) != 0 && read_u8(arg2, 0x1078) != 0) {
            __assert("pSps->vui_param.bitstream_restriction_flag == 0",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/AVC_RbspEncod.c", 0xf5,
                     "writeSpsData", &_gp);
        }
    }
    AL_BitStreamLite_PutU(arg1, 1, 1);
    return AL_BitStreamLite_AlignWithBits(arg1, 0);
}

static int32_t writePps(AL_BitStreamLite *arg1, const uint8_t *arg2)
{
    uint32_t a2_7;
    const void *a2_9;

    AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 0));
    AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 1));
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 2));
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 3));
    AL_BitStreamLite_PutUE(arg1, read_s32(arg2, 4));
    if (read_s32(arg2, 4) != 0) {
        __assert("pPps->num_slice_groups_minus1 == 0",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/AVC_RbspEncod.c", 0x110,
                 "writePps", &_gp);
    }
    AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 0x7fc0));
    AL_BitStreamLite_PutUE(arg1, (int32_t)read_u8(arg2, 0x7fc1));
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0x7fc2));
    AL_BitStreamLite_PutU(arg1, 2, (uint32_t)read_u8(arg2, 0x7fc3));
    AL_BitStreamLite_PutSE(arg1, (int32_t)read_s8(arg2, 0x7fc4));
    AL_BitStreamLite_PutSE(arg1, (int32_t)read_s8(arg2, 0x7fc5));
    if ((uint32_t)(read_s8(arg2, 0x7fc6) + 0xc) >= 0x19U) {
        __assert("pPps->chroma_qp_index_offset >= -12 && pPps->chroma_qp_index_offset <= 12",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/AVC_RbspEncod.c", 0x117,
                 "writePps", &_gp);
    }
    AL_BitStreamLite_PutSE(arg1, (int32_t)read_s8(arg2, 0x7fc6));
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0x7fc8));
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0x7fc9));
    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0x7fca));
    a2_7 = (uint32_t)read_u8(arg2, 0x7fcb);
    if (a2_7 != 0 || read_s8(arg2, 0x7fc7) != read_s8(arg2, 0x7fc6)) {
        AL_BitStreamLite_PutU(arg1, 1, a2_7);
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)read_u8(arg2, 0x7fcc));
        a2_9 = read_ptr(arg2, 0x80c0);
        if (a2_9 == NULL) {
            __assert("pPps->pSPS != ((void *)0)",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/AVC_RbspEncod.c", 0x122,
                     "writePps", &_gp);
            return (int32_t)(intptr_t)AL_GetAvcRbspWriter();
        }
        writeScalingMatrix(arg1, (int32_t)read_u8(arg2, 0x7fcc), a2_9);
        if ((uint32_t)(read_s8(arg2, 0x7fc7) + 0xc) >= 0x19U) {
            __assert("pPps->second_chroma_qp_index_offset >= -12 && pPps->second_chroma_qp_index_offset <= 12",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_bitstream/AVC_RbspEncod.c", 0x124,
                     "writePps", &_gp);
        }
        AL_BitStreamLite_PutSE(arg1, (int32_t)read_s8(arg2, 0x7fc7));
    }
    AL_BitStreamLite_PutU(arg1, 1, 1);
    return AL_BitStreamLite_AlignWithBits(arg1, 0);
}

typedef struct AL_AVC_RbspWriter {
    int32_t (*getCodec)(void);
    int32_t (*writeAud)(AL_BitStreamLite *arg1, int32_t arg2);
    void *writeVps;
    int32_t (*writeSps)(AL_BitStreamLite *arg1, const uint8_t *arg2);
    int32_t (*writePps)(AL_BitStreamLite *arg1, const uint8_t *arg2);
    int32_t (*writeSeiBufferingPeriod)(AL_BitStreamLite *arg1, const void *arg2, uint32_t arg3);
    int32_t (*writeSeiRecoveryPoint)(AL_BitStreamLite *arg1, int32_t arg2);
    int32_t (*writeSeiPictureTiming)(AL_BitStreamLite *arg1, const void *arg2, uint32_t arg3, uint32_t arg4,
                                     uint32_t arg5);
    int32_t (*writeMdcv)(AL_BitStreamLite *arg1, int16_t *arg2);
    void *reserved_24;
    int32_t (*writeUdu)(AL_BitStreamLite *arg1, char *arg2, int8_t arg3);
} AL_AVC_RbspWriter;

static const AL_AVC_RbspWriter writer = {
    getCodec,
    AL_RbspEncoding_WriteAUD,
    NULL,
    writeSps,
    writePps,
    writeSeiBufferingPeriod,
    writeSeiRecoveryPoint,
    writeSeiPictureTiming,
    AL_RbspEncoding_WriteMasteringDisplayColourVolume,
    NULL,
    AL_RbspEncoding_WriteUserDataUnregistered,
};

void *AL_GetAvcRbspWriter(void)
{
    return (void *)&writer;
}
