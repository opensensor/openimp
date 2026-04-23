#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line,
                        const char *function, void *caller);

/* Placement:
 * - UpdateHwRateCtrlParam @ RateCtrl_6.c
 * - InitHwRateCtrl @ RateCtrl_6.c
 * - findContainer @ RateCtrl_6.c
 */

/* forward decl, ported by T<N> later */
uint32_t SourceVector_IsIn(void *arg1, int32_t arg2);
/* forward decl, ported by T<N> later */
uint32_t GetTargetSize(void *arg1, int32_t *arg2, int32_t arg3);

int32_t UpdateHwRateCtrlParam(void *arg1, void *arg2, int32_t *arg3);
int32_t InitHwRateCtrl(void *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                       char arg6, int32_t arg7, int32_t arg8);
int32_t findContainer(int32_t arg1, int32_t arg2);

#define READ_U8(base, off) (*(uint8_t *)((uint8_t *)(base) + (off)))
#define READ_U16(base, off) (*(uint16_t *)((uint8_t *)(base) + (off)))
#define READ_S32(base, off) (*(int32_t *)((uint8_t *)(base) + (off)))
#define WRITE_U8(base, off, val) (*(uint8_t *)((uint8_t *)(base) + (off)) = (uint8_t)(val))
#define WRITE_U16(base, off, val) (*(uint16_t *)((uint8_t *)(base) + (off)) = (uint16_t)(val))
#define WRITE_S32(base, off, val) (*(int32_t *)((uint8_t *)(base) + (off)) = (int32_t)(val))

#define RC6_KMSG(fmt, ...)                                                                        \
    do {                                                                                          \
        int _kfd = open("/dev/kmsg", O_WRONLY);                                                   \
        if (_kfd >= 0) {                                                                          \
            char _buf[256];                                                                       \
            int _n = snprintf(_buf, sizeof(_buf), "libimp/RC6: " fmt "\n", ##__VA_ARGS__);       \
            if (_n > 0) {                                                                         \
                write(_kfd, _buf, (size_t)_n);                                                    \
            }                                                                                     \
            close(_kfd);                                                                          \
        }                                                                                         \
    } while (0)

int32_t UpdateHwRateCtrlParam(void *arg1, void *arg2, int32_t *arg3)
{
    uint32_t s1 = (uint32_t)*(uint16_t *)((char *)arg1 + 0x50);
    uint32_t a0;
    uint32_t s7_1;
    uint32_t s4_1;
    uint32_t s6_1;
    int32_t fp_1;
    int32_t s3;

    RC6_KMSG("Update entry hw=%p stats=%p 14=%u 50=%u 16=%u 52=%u 38=%u 74=%u 3a=%u 76=%u",
             arg1, arg3,
             (unsigned)READ_U16(arg1, 0x14), (unsigned)READ_U16(arg1, 0x50),
             (unsigned)READ_U16(arg1, 0x16), (unsigned)READ_U16(arg1, 0x52),
             (unsigned)READ_U16(arg1, 0x38), (unsigned)READ_U16(arg1, 0x74),
             (unsigned)READ_U16(arg1, 0x3a), (unsigned)READ_U16(arg1, 0x76));

    if (s1 != (uint32_t)*(uint16_t *)((char *)arg1 + 0x14)) {
        RC6_KMSG("Update mismatch iiOo 14=%u 50=%u",
                 (unsigned)READ_U16(arg1, 0x14), (unsigned)READ_U16(arg1, 0x50));
        goto label_6a9d0;
    }
    a0 = (uint32_t)*(uint16_t *)((char *)arg1 + 0x52);
    if (a0 != (uint32_t)*(uint16_t *)((char *)arg1 + 0x16)) {
        RC6_KMSG("Update mismatch IiOo 16=%u 52=%u",
                 (unsigned)READ_U16(arg1, 0x16), (unsigned)READ_U16(arg1, 0x52));
        goto label_6a9ac;
    }
    s7_1 = (uint32_t)*(uint16_t *)((char *)arg1 + 0x38);
    s4_1 = (uint32_t)*(uint16_t *)((char *)arg1 + 0x74);
    if (s4_1 != s7_1) {
        RC6_KMSG("Update mismatch OIOo 38=%u 74=%u",
                 (unsigned)READ_U16(arg1, 0x38), (unsigned)READ_U16(arg1, 0x74));
        goto label_6a970;
    }
    s6_1 = (uint32_t)*(uint16_t *)((char *)arg1 + 0x76);
    fp_1 = (int32_t)(s1 * a0);
    if (s6_1 != (uint32_t)*(uint16_t *)((char *)arg1 + 0x3a)) {
        RC6_KMSG("Update mismatch liOo 3a=%u 76=%u",
                 (unsigned)READ_U16(arg1, 0x3a), (unsigned)READ_U16(arg1, 0x76));
        goto label_6a98c;
    }

    s3 = fp_1 * (int32_t)s6_1;
    if (*arg3 == 8 || (uint32_t)(uint16_t)arg3[1] < 2) {
        uint32_t v0_5 = GetTargetSize(arg2, arg3, 2);
        uint32_t v0_6 = GetTargetSize(arg2, arg3, 1);
        uint32_t v0_8 = (uint32_t)(((uint64_t)v0_5 * (uint64_t)(uint32_t)fp_1) / (uint64_t)s4_1);
        uint32_t lo_3;

        if (s3 == 0) {
            __builtin_trap();
        }
        *(int32_t *)((char *)arg1 + 0x10) = (int32_t)((uint32_t)s1 * v0_8 / (uint32_t)s3);
        lo_3 = (uint32_t)(((uint64_t)v0_6 * (uint64_t)(uint32_t)fp_1) / (uint64_t)s4_1);
        if (s3 == 0) {
            __builtin_trap();
        }
        *(int32_t *)((char *)arg1 + 0x4c) = (int32_t)((uint32_t)s1 * lo_3 / (uint32_t)s3);
    } else {
        uint32_t v0_15 = GetTargetSize(arg2, arg3, 2);
        uint32_t lo_5 = (uint32_t)(((uint64_t)(uint32_t)fp_1 * (uint64_t)v0_15) / (uint64_t)s7_1);

        if (s3 == 0) {
            __builtin_trap();
        }
        lo_5 = (uint32_t)s1 * lo_5 / (uint32_t)s3;
        *(int32_t *)((char *)arg1 + 0x10) = (int32_t)lo_5;
        *(int32_t *)((char *)arg1 + 0x4c) = (int32_t)lo_5;
    }

    {
        int32_t a1_8 = *(int32_t *)((char *)arg2 + 0x14);
        uint32_t v0_19 = (uint32_t)*(uint16_t *)((char *)arg2 + 0x0c);
        uint32_t v1_5 = (uint32_t)*(int32_t *)((char *)arg1 + 0x4c) >> 4;
        int32_t result = (int32_t)(s4_1 * s6_1 * v0_19);
        uint32_t lo_6;

        *(int32_t *)((char *)arg1 + 0x20) = *(int32_t *)((char *)arg1 + 0x10) >> 4;
        *(int32_t *)((char *)arg1 + 0x5c) = (int32_t)v1_5;
        if (result == 0) {
            __builtin_trap();
        }
        lo_6 = (uint32_t)s1 * (uint32_t)a1_8 / (uint32_t)result;
        *(int32_t *)((char *)arg1 + 0x0c) = (int32_t)lo_6;
        *(int32_t *)((char *)arg1 + 0x48) = (int32_t)lo_6;
        RC6_KMSG("Update exit hw=%p result=%d target0=%d target1=%d",
                 arg1, result, READ_S32(arg1, 0x10), READ_S32(arg1, 0x4c));
        return result;
    }

label_6a970:
    __assert("oiOo [ 0 ] . OIOo == oiOo [ 01 ] . OIOo",
             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_6.c",
             0x69, "UpdateHwRateCtrlParam", &_gp);
label_6a98c:
    __assert("oiOo [ 0 ] . liOo == oiOo [ 01 ] . liOo",
             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_6.c",
             0x58, "UpdateHwRateCtrlParam", &_gp);
label_6a9ac:
    __assert("oiOo [ 0 ] . IiOo == oiOo [ 01 ] . IiOo",
             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_6.c",
             0x47, "UpdateHwRateCtrlParam", &_gp);
label_6a9d0:
    return InitHwRateCtrl((void *)(intptr_t)__assert(
                              "oiOo [ 0 ] . iiOo == oiOo [ 01 ] . iiOo",
                              "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_6.c",
                              0x36, "UpdateHwRateCtrlParam", &_gp),
                          arg2, (int32_t)(intptr_t)arg3, 0, 0, 0, 0, 0);
}

int32_t InitHwRateCtrl(void *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                       char arg6, int32_t arg7, int32_t arg8)
{
    uint32_t t8 = (uint32_t)(uint8_t)arg6;
    int32_t t2 = 1 << (t8 & 0x1f);
    int32_t t6_3 = (t2 + arg5 - 1) >> (t8 & 0x1f);
    int32_t i_2;
    int32_t i;

    (void)arg3;

    if ((uint32_t)arg8 >= 0x21) {
        i = 0x20;
label_6aa2c:
        i_2 = arg8;
        do {
            if (i == 0) {
                __builtin_trap();
            }
            if ((uint32_t)arg8 % (uint32_t)i == 0) {
                if (i == 0) {
                    __builtin_trap();
                }
                if ((uint32_t)(t6_3 * arg8 / i) < 0x400 && (uint32_t)(arg8 / i) < 0x41) {
                    i_2 = i;
                }
            }
            i -= 1;
        } while (i != 4);
    } else {
        i = arg8 - 1;
        i_2 = arg8;
        if (i >= 5) {
            goto label_6aa2c;
        }
    }

    {
        uint8_t lo_3 = (uint8_t)((uint32_t)arg8 / (uint32_t)i_2);
        int32_t a3_2;
        uint8_t *i_1;

        if (i_2 == 0) {
            __builtin_trap();
        }
        a3_2 = (t2 + arg4 - 1) >> (t8 & 0x1f);
        i_1 = (uint8_t *)arg1;
        if (i_2 == 0) {
            __builtin_trap();
        }
        RC6_KMSG("Init entry hw=%p rc=%p stats=%p cols=%d split=%d lo=%u a3=%d t6=%d",
                 arg1, arg2, (void *)(intptr_t)arg3, arg8, i_2, (unsigned)lo_3, a3_2, t6_3);
        do {
            WRITE_U8(i_1, 0x03, 1);
            WRITE_U8(i_1, 0x04, 0);
            WRITE_S32(i_1, 0x08, 0x200000);
            WRITE_U16(i_1, 0x14, (uint16_t)i_2);
            WRITE_U16(i_1, 0x16, (uint16_t)lo_3);
            WRITE_U16(i_1, 0x24, (uint16_t)(((uint32_t)(a3_2 << 2) / (uint32_t)i_2) + 1));
            WRITE_U8(i_1, 0x18, READ_U8(arg2, 0x1a));
            WRITE_S32(i_1, 0x2c, (int32_t)((uint32_t)lo_3 * (uint32_t)t6_3));
            WRITE_U8(i_1, 0x19, READ_U8(arg2, 0x1c));
            {
                int16_t t3_3 = (int16_t)READ_U16(arg2, 0x1a);

                if (t3_3 < 0x10) {
                    t3_3 = 0x10;
                }
                WRITE_U8(i_1, 0x1a, (uint8_t)t3_3);
            }
            WRITE_U8(i_1, 0x1b, READ_U8(arg2, 0x18));
            WRITE_U8(i_1, 0x1d, 1);
            WRITE_U16(i_1, 0x26, (uint16_t)(((uint32_t)((arg7 << 1) - 0x190)) / 0xf));
            WRITE_U16(i_1, 0x38, (uint16_t)a3_2);
            WRITE_U16(i_1, 0x3a, (uint16_t)t6_3);
            i_1 += 0x3c;
        } while ((uint8_t *)arg1 + 0x78 != i_1);
    }

    RC6_KMSG("Init seeded hw=%p slot0[14/16/38/3a]=[%u %u %u %u] slot1[14/16/38/3a]=[%u %u %u %u]",
             arg1,
             (unsigned)READ_U16(arg1, 0x14), (unsigned)READ_U16(arg1, 0x16),
             (unsigned)READ_U16(arg1, 0x38), (unsigned)READ_U16(arg1, 0x3a),
             (unsigned)READ_U16(arg1, 0x50), (unsigned)READ_U16(arg1, 0x52),
             (unsigned)READ_U16(arg1, 0x74), (unsigned)READ_U16(arg1, 0x76));
    return UpdateHwRateCtrlParam(arg1, arg2, (int32_t *)(intptr_t)arg3);
}

int32_t findContainer(int32_t arg1, int32_t arg2)
{
    int32_t s0 = arg1 + 0x1db0;

    if (SourceVector_IsIn((void *)(intptr_t)s0, arg2) == 0) {
        s0 = arg1 + 0x1ee0;
        if (SourceVector_IsIn((void *)(intptr_t)s0, arg2) == 0) {
            int32_t s0_1 = arg1 + 0x2010;

            if (SourceVector_IsIn((void *)(intptr_t)s0_1, arg2) == 0) {
                return 0;
            }
            return s0_1;
        }
    }
    return s0;
}
