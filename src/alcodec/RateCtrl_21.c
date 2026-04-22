#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "alcodec/al_rtos.h"

/* Placement:
 * - PreprocessHwRateCtrl @ RateCtrl_21.c
 * - l0io/iiIo/IIIo/i0Io/I0Io/o1Io/i1Io/l1Io/OOlo @ RateCtrl_21.c
 */

uint32_t GetTargetSize(void *arg1, int32_t *arg2, int32_t arg3);
void *Ooio(void *arg1, int32_t arg2, int32_t arg3);

/* stock name: l0io */
int32_t rc_l0io(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                int32_t arg6, char arg7, char arg8);
/* stock name: iiIo */
int32_t rc_iiIo(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4);
/* stock name: IIIo */
uint32_t rc_IIIo(int32_t *arg1, int32_t arg2);
/* stock name: i0Io */
uint32_t rc_i0Io(void *arg1, int32_t arg2);
/* stock name: I0Io */
int32_t rc_I0Io(void *arg1);
/* stock name: o1Io */
int32_t rc_o1Io(void *arg1);
/* stock name: i1Io */
int32_t rc_i1Io(void *arg1);
/* stock name: l1Io */
int32_t rc_l1Io(int32_t *arg1);
/* stock name: OOlo */
int32_t rc_OOlo(void *arg1);

static const int32_t rc_PreprocessHwRateCtrlDefaults[12] = {
    0x00300000, 0x002ccccc, 0x00299999, 0x00266666, 0x00233333, 0x00200000,
    0x001ccccc, 0x001e0000, 0x001b9999, 0x001a6666, 0x00193333, 0x00180000,
};

#define RC21_KMSG(fmt, ...)                                                                       \
    do {                                                                                          \
        int _kfd = open("/dev/kmsg", O_WRONLY);                                                   \
        if (_kfd >= 0) {                                                                          \
            char _buf[256];                                                                       \
            int _n = snprintf(_buf, sizeof(_buf), "libimp/RC21: " fmt "\n", ##__VA_ARGS__);      \
            if (_n > 0) {                                                                         \
                write(_kfd, _buf, (size_t)_n);                                                    \
            }                                                                                     \
            close(_kfd);                                                                          \
        }                                                                                         \
    } while (0)

int32_t rc_l0io(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                int32_t arg6, char arg7, char arg8)
{
    int32_t *ctx = (int32_t *)arg1;
    uint8_t *bytes = (uint8_t *)arg1;

    RC21_KMSG("l0io entry ctx=%p a2=%d a3=%d a4=%d a5=%d a6=%d a7=%d a8=%d",
              arg1, arg2, arg3, arg4, arg5, arg6, (int)arg7, (int)arg8);

    ctx[0] = arg2;
    ctx[1] = arg3;
    ctx[2] = arg4;
    ctx[3] = arg5;
    ctx[4] = arg6;
    bytes[0x14] = (uint8_t)arg7;
    bytes[0x15] = (uint8_t)arg8;
    ctx[6] = 0;
    ctx[7] = 0;
    ctx[8] = arg3;
    ctx[9] = 0;
    ctx[10] = 0;
    ctx[12] = 0;
    ctx[13] = 0;
    ctx[14] = 0;
    ctx[15] = 0;
    RC21_KMSG("l0io exit ctx=%p rate=%d floor=%d fps=%d period=%d max=%d flags=%u/%u",
              arg1, ctx[0], ctx[1], ctx[2], ctx[3], ctx[4], (unsigned)bytes[0x14],
              (unsigned)bytes[0x15]);
    return 0;
}

int32_t rc_iiIo(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    int32_t *ctx = (int32_t *)arg1;
    uint32_t s5 = (uint32_t)ctx[8];
    uint32_t s0 = (uint32_t)ctx[4];
    uint64_t prod0 = (uint64_t)s5 * (uint64_t)s0;
    uint32_t rem0;
    uint32_t quo0;
    uint32_t t2_1 = (uint32_t)ctx[6];
    uint64_t prod1;
    uint32_t rem1;
    uint32_t quo1;
    int32_t s3;
    int32_t s6_2;
    uint32_t v0_6;
    uint64_t prod2;
    int32_t v0_11;
    uint32_t hi_4;

    if (arg4 == 0) {
        __builtin_trap();
    }
    rem0 = (uint32_t)(prod0 % (uint32_t)arg4);
    quo0 = (uint32_t)(prod0 / (uint32_t)arg4);
    prod1 = (uint64_t)t2_1 * (uint64_t)s0;
    rem1 = (uint32_t)(prod1 % (uint32_t)arg4);
    quo1 = (uint32_t)(prod1 / (uint32_t)arg4);
    if (s5 + quo1 < quo0) {
        s5 = quo0 - quo1;
    }
    ctx[8] = (int32_t)s5;
    s3 = ctx[9];
    s6_2 = ctx[3];
    if (s6_2 == 0) {
        __builtin_trap();
    }
    v0_6 = (uint32_t)(((uint64_t)s0 * (uint64_t)(uint32_t)s3) / (uint32_t)s6_2);
    prod2 = (uint64_t)(uint32_t)arg4 * (uint64_t)(uint32_t)s3;
    v0_11 =
        ctx[7] - (int32_t)v0_6 + arg4 + (int32_t)rem1 + (int32_t)(prod2 / (uint32_t)s6_2) -
        (int32_t)rem0;
    hi_4 = (uint32_t)v0_11 % (uint32_t)arg4;
    ctx[6] = (int32_t)(quo1 - 1 - quo0 + (uint32_t)v0_11 / (uint32_t)arg4 + s5);
    ctx[7] = (int32_t)hi_4;
    if (s6_2 != arg3) {
        uint64_t prod3 = (uint64_t)(uint32_t)s3 * (uint64_t)(uint32_t)arg3;
        uint32_t v0_14;

        if (arg3 == 0) {
            __builtin_trap();
        }
        v0_14 = (uint32_t)(prod3 / (uint32_t)s6_2);
        ctx[8] = (int32_t)(v0_14 / (uint32_t)arg3 + s5);
        ctx[9] = (int32_t)(v0_14 % (uint32_t)arg3);
    }
    ctx[3] = arg3;
    ctx[4] = arg4;
    ctx[2] = arg2;
    return arg2;
}

uint32_t rc_IIIo(int32_t *arg1, int32_t arg2)
{
    uint32_t fp = (uint32_t)((uint8_t *)arg1)[0x14];
    uint32_t s5 = (uint32_t)arg1[6];
    uint32_t s4 = (uint32_t)arg1[8];
    uint32_t v0_7;
    uint32_t hi_1;

    if (fp == 0) {
        uint32_t v0_2 = s4 - (uint32_t)arg1[1];

        if (s5 < v0_2) {
            s5 = v0_2;
        }
    }

    {
        uint32_t s3 = (uint32_t)arg1[4];
        uint64_t prod = (uint64_t)(uint32_t)arg2 * 90000ULL;
        uint32_t v0_3 = (uint32_t)(prod % s3);
        uint32_t v0_5 = (uint32_t)(prod / s3);
        uint32_t v0_4 = (uint32_t)arg1[7] + v0_3;

        if (s3 == 0) {
            __builtin_trap();
        }
        hi_1 = v0_4 % s3;
        v0_7 = v0_4 / s3 + v0_5 + s5;
    }

    if (s4 >= v0_7) {
        uint32_t s6_1 = (uint32_t)arg1[9];
        uint32_t s5_1 = (uint32_t)arg1[3];

        if (v0_7 != s4 || (uint64_t)(uint32_t)arg1[4] * (uint64_t)s6_1 >=
                            (uint64_t)hi_1 * (uint64_t)s5_1) {
            return 0;
        }
        if (fp != 0) {
            uint64_t prod0 = (uint64_t)(uint32_t)arg1[2] * 90000ULL;
            uint32_t v0_8 = (uint32_t)(prod0 % s5_1);
            uint32_t v0_10 = (uint32_t)(prod0 / s5_1);
            uint32_t v0_9 = s6_1 + v0_8;
            uint64_t prod1;
            uint32_t v0_12;
            uint64_t prod2;
            uint32_t v0_16;
            uint32_t v0_17;

            if (s5_1 == 0) {
                __builtin_trap();
            }
            prod1 = (uint64_t)(v0_9 % s5_1) * (uint64_t)(uint32_t)arg1[4];
            v0_12 = (uint32_t)(prod1 / s5_1);
            prod2 = (uint64_t)(uint32_t)arg1[4] *
                    (uint64_t)(s4 - v0_7 + v0_10 + v0_9 / s5_1);
            v0_16 = (uint32_t)(prod2 / 90000ULL);
            v0_17 = (uint32_t)((v0_12 - hi_1) / 90000U + v0_16);
            if ((uint32_t)arg1[0] < v0_17) {
                return (v0_17 - (uint32_t)arg1[0] + 7) >> 3;
            }
            return 0;
        }
        return 0xffffffffU;
    }

    return 0;
}

uint32_t rc_i0Io(void *arg1, int32_t arg2)
{
    int32_t *ctx = (int32_t *)arg1;
    uint32_t t2 = (uint32_t)ctx[6];
    uint32_t t0 = (uint32_t)ctx[8];
    uint32_t fp = (uint32_t)ctx[1];
    uint64_t prod0;
    uint32_t result_1;
    uint32_t v0_5;
    int32_t s3;
    uint64_t prod1;
    uint32_t hi_2;
    uint32_t s2;
    uint32_t v0_11;
    uint32_t a0_5;
    uint32_t a3;
    uint32_t result;
    uint32_t a2_5;
    uint32_t a1_5;
    uint32_t v1_7;
    uint32_t t0_2;

    if (((uint8_t *)arg1)[0x14] == 0) {
        uint32_t v0_1 = t0 - fp;

        if (t2 < v0_1) {
            uint32_t a0_8 = v0_1 - t2;

            t2 = v0_1;
            ctx[15] += (int32_t)a0_8;
        }
    }

    prod0 = (uint64_t)(uint32_t)arg2 * 90000ULL;
    if ((uint32_t)ctx[4] == 0) {
        __builtin_trap();
    }
    result_1 = (uint32_t)((uint32_t)ctx[7] + (uint32_t)(prod0 % (uint32_t)ctx[4])) %
               (uint32_t)ctx[4];
    v0_5 = (uint32_t)(prod0 / (uint32_t)ctx[4]);
    s3 = ctx[3];
    prod1 = (uint64_t)(uint32_t)ctx[2] * 90000ULL;
    ctx[7] = (int32_t)result_1;
    s2 = (uint32_t)ctx[7] + (uint32_t)(prod0 % (uint32_t)ctx[4]);
    s2 = (uint32_t)((s2 / (uint32_t)ctx[4]) + v0_5 + t2);
    if ((uint32_t)s3 == 0) {
        __builtin_trap();
    }
    hi_2 = (uint32_t)((uint32_t)ctx[9] + (uint32_t)(prod1 % (uint32_t)s3)) % (uint32_t)s3;
    v0_11 = (uint32_t)((uint32_t)ctx[9] + (uint32_t)(prod1 % (uint32_t)s3)) / (uint32_t)s3 +
            (uint32_t)(prod1 / (uint32_t)s3) + t0;
    ctx[9] = (int32_t)hi_2;

    if (fp >= v0_11 || fp >= s2) {
        a0_5 = 0;
        a3 = 0;
    } else {
        uint32_t v1_12 = v0_11;

        if (v0_11 >= s2) {
            v1_12 = s2;
        }
        a0_5 = (v1_12 - fp) / 90000U;
        a3 = a0_5 * 90000U;
    }

    v1_7 = (uint32_t)ctx[12] + (uint32_t)arg2;
    t0_2 = (uint32_t)((uint8_t *)arg1)[0x15];
    result = v0_11 - a3;
    a2_5 = s2 - a3;
    a1_5 = (uint32_t)ctx[14] + 1;
    ctx[10] += (int32_t)a0_5;
    ctx[6] = (int32_t)a2_5;
    ctx[8] = (int32_t)result;
    ctx[14] = (int32_t)a1_5;
    ctx[12] = (int32_t)v1_7;
    if (t0_2 == 0) {
        uint32_t carry = (uint64_t)(uint32_t)ctx[13] + (uint32_t)(v1_7 < (uint32_t)ctx[12] ? 1 : 0);

        ctx[13] = (int32_t)carry;
    } else {
        ctx[13] += (v1_7 < (uint32_t)ctx[12] ? 1 : 0);
    }

    if (result < a2_5) {
        ctx[8] = (int32_t)s2;
        ctx[9] = (int32_t)result_1;
        return result_1;
    }
    if (a2_5 == result ||
        (uint64_t)(uint32_t)ctx[4] * (uint64_t)hi_2 < (uint64_t)result_1 * (uint64_t)(uint32_t)s3) {
        ctx[8] = (int32_t)s2;
        ctx[9] = (int32_t)result_1;
        return result_1;
    }
    return result;
}

int32_t rc_I0Io(void *arg1)
{
    int32_t *ctx = (int32_t *)arg1;
    uint32_t s4 = (uint32_t)ctx[6];
    uint32_t s2 = (uint32_t)ctx[8];
    int32_t result = 0;

    if (s2 < s4) {
        uint32_t s6_1 = (uint32_t)ctx[3];
        uint32_t v0 = (uint32_t)ctx[2];
        uint64_t prod0 = (uint64_t)(s4 - s2) * (uint64_t)s6_1;
        uint32_t s3_4 = (uint32_t)((v0 << 4) + (v0 << 6)) * 0x465U;
        uint32_t v0_5 = (uint32_t)(prod0 / (uint32_t)s3_4);
        uint32_t v0_6 = (uint32_t)(prod0 % (uint32_t)s3_4);
        int32_t result_1 = (int32_t)(v0_5 + (v0_6 != 0 ? 1U : 0U));
        uint64_t prod1 = (uint64_t)(uint32_t)result_1 * (uint64_t)s3_4;
        uint32_t v0_8 = (uint32_t)(prod1 % s6_1);
        uint32_t s3_5 = (uint32_t)ctx[9] + v0_8;
        uint32_t v0_9 = (uint32_t)(prod1 / s6_1);

        if (s6_1 == 0) {
            __builtin_trap();
        }
        result = result_1;
        ctx[8] = (int32_t)(s2 + v0_9 + s3_5 / s6_1);
        ctx[9] = (int32_t)(s3_5 % s6_1);
    }
    return result;
}

int32_t rc_o1Io(void *arg1)
{
    return *(int32_t *)((char *)arg1 + 4);
}

int32_t rc_i1Io(void *arg1)
{
    int32_t *ctx = (int32_t *)arg1;
    uint64_t lhs = (uint64_t)(uint32_t)ctx[9] * (uint64_t)(uint32_t)ctx[4];
    uint64_t rhs = (uint64_t)(uint32_t)ctx[7] * (uint64_t)(uint32_t)ctx[3];

    return ctx[8] - ctx[6] - (lhs < rhs ? 1 : 0);
}

int32_t rc_l1Io(int32_t *arg1)
{
    return arg1[0];
}

int32_t rc_OOlo(void *arg1)
{
    int32_t *ctx = (int32_t *)arg1;
    uint32_t s3 = (uint32_t)ctx[6];
    uint32_t s1 = (uint32_t)ctx[7];
    uint32_t s2 = (uint32_t)ctx[8];
    uint32_t a0 = (uint32_t)ctx[9];
    uint32_t s4 = (uint32_t)ctx[4];
    uint32_t s5 = (uint32_t)ctx[3];

    if (((uint8_t *)arg1)[0x14] == 0) {
        uint32_t s6_2 = s2 - (uint32_t)ctx[1];

        if (s3 < s6_2) {
            uint64_t prod = (uint64_t)s4 * (uint64_t)s2;

            s3 = s6_2;
            s1 = (uint32_t)(prod / s5);
            a0 = (uint32_t)ctx[9];
        }
    }

    if (s2 == s3 && (uint64_t)a0 * (uint64_t)s4 < (uint64_t)s1 * (uint64_t)s5) {
        uint64_t prod0 = (uint64_t)a0 * (uint64_t)s4;
        uint32_t v0_11 = (uint32_t)(prod0 / s5);
        uint32_t v0_12 = s1 - v0_11;
        uint64_t prod1 = (uint64_t)(s3 - s2) * (uint64_t)s4;
        int32_t term0 = (int32_t)((int64_t)v0_12 / 90000LL);
        int32_t term1 = (int32_t)(prod1 / 90000ULL);

        return term0 - term1;
    }

    {
        uint64_t prod0 = (uint64_t)a0 * (uint64_t)s4;
        uint32_t v0_4 = (uint32_t)(prod0 / s5);
        uint64_t prod1 = (uint64_t)(s2 - s3) * (uint64_t)s4;

        return (int32_t)((v0_4 - s1) / 90000U + (uint32_t)(prod1 / 90000ULL));
    }
}

int32_t PreprocessHwRateCtrl(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4, void *arg5)
{
    uint8_t *dst = (uint8_t *)arg5;
    int32_t s1_1 = (int32_t)GetTargetSize(arg1, (int32_t *)(intptr_t)arg2, arg4);
    int32_t a0_3 = arg1[arg4 + 0xd];
    int32_t *cfg = (int32_t *)dst;

    Rtos_Memset(dst + 0x500, 0, 0x20);
    if (*arg1 == 3 || a0_3 == 0) {
        Rtos_Memcpy(dst + 8, rc_PreprocessHwRateCtrlDefaults,
                    sizeof(rc_PreprocessHwRateCtrlDefaults));
    } else {
        Rtos_Memset(dst + 8, 0, sizeof(rc_PreprocessHwRateCtrlDefaults));
        s1_1 = a0_3 * 0x5f / 0x64;
    }

    Rtos_Memset(dst, 0, 0x200);
    cfg[0] = (int32_t)((uint64_t)(uint32_t)(s1_1 * 0x9f) / 100ULL);
    cfg[1] = 6;
    cfg[2] = rc_PreprocessHwRateCtrlDefaults[0];
    cfg[3] = 4;
    cfg[4] = rc_PreprocessHwRateCtrlDefaults[7];
    cfg[5] = 7;
    cfg[6] = 9;
    cfg[7] = 0x0c;
    cfg[8] = (int32_t)((uint64_t)(uint32_t)(s1_1 * 0x82) / 100ULL);
    cfg[9] = 4;
    cfg[10] = rc_PreprocessHwRateCtrlDefaults[1];
    cfg[11] = 3;
    cfg[12] = rc_PreprocessHwRateCtrlDefaults[6];
    cfg[13] = 5;
    cfg[14] = 7;
    cfg[15] = 8;
    cfg[16] = (int32_t)((uint64_t)(uint32_t)(s1_1 * 0x73) / 100ULL);
    cfg[17] = 3;
    cfg[18] = rc_PreprocessHwRateCtrlDefaults[2];
    cfg[19] = 2;
    cfg[20] = rc_PreprocessHwRateCtrlDefaults[8];
    cfg[21] = 4;
    cfg[22] = 5;
    cfg[23] = 6;
    cfg[24] = (int32_t)((uint64_t)(uint32_t)(s1_1 * 0x4b) / 100ULL);
    cfg[25] = 2;
    cfg[26] = rc_PreprocessHwRateCtrlDefaults[3];
    cfg[27] = 1;
    cfg[28] = rc_PreprocessHwRateCtrlDefaults[9];
    cfg[29] = 3;
    cfg[30] = 4;
    cfg[31] = 5;
    cfg[32] = (int32_t)((uint64_t)(uint32_t)(s1_1 * 0x37) / 100ULL);
    cfg[33] = 1;
    cfg[34] = rc_PreprocessHwRateCtrlDefaults[4];
    cfg[35] = 1;
    cfg[36] = rc_PreprocessHwRateCtrlDefaults[10];
    cfg[37] = 2;
    cfg[38] = 3;
    cfg[39] = 4;
    cfg[40] = (int32_t)((uint64_t)(uint32_t)(s1_1 * 0x28) / 100ULL);
    cfg[41] = 1;
    cfg[42] = rc_PreprocessHwRateCtrlDefaults[5];
    cfg[43] = 0;
    cfg[44] = rc_PreprocessHwRateCtrlDefaults[11];
    cfg[45] = 1;
    cfg[46] = 1;
    cfg[47] = 2;
    cfg[48] = (int32_t)((uint64_t)(uint32_t)(s1_1 * 0x18) / 100ULL);
    cfg[49] = 0;
    cfg[50] = 0;
    cfg[51] = 1;
    cfg[52] = 0;
    cfg[53] = 0;
    cfg[54] = 1;
    cfg[55] = 1;
    cfg[56] = (int32_t)((uint64_t)(uint32_t)(s1_1 * 0x50) / 100ULL);
    cfg[57] = -1;
    cfg[58] = 0;
    cfg[59] = 0;
    cfg[60] = 0;
    cfg[61] = 0;
    cfg[62] = 0;
    cfg[63] = 0;
    cfg[64] = (int32_t)((uint64_t)(uint32_t)(s1_1 * 0x78) / 100ULL);
    cfg[65] = -1;
    cfg[66] = 0;
    cfg[67] = 0;
    cfg[68] = 0;
    cfg[69] = 0;
    cfg[70] = 0;
    cfg[71] = 0;
    cfg[72] = (int32_t)((uint64_t)(uint32_t)(s1_1 * 0x96) / 100ULL);
    cfg[73] = -2;
    cfg[74] = 0;
    cfg[75] = 0;
    cfg[76] = 0;
    cfg[77] = 0;
    cfg[78] = 0;
    cfg[79] = 0;
    cfg[80] = (int32_t)((uint64_t)(uint32_t)(s1_1 * 0xbe) / 100ULL);
    cfg[81] = -3;
    cfg[82] = 1;
    cfg[83] = 0;
    cfg[84] = 0;
    cfg[85] = 0;
    cfg[86] = 0;
    cfg[87] = 0;
    cfg[88] = 1;
    cfg[89] = -4;
    cfg[90] = 0;
    cfg[91] = 0;
    cfg[92] = 0;
    cfg[93] = 0;
    cfg[94] = 0;
    cfg[95] = 0;
    Ooio(dst + 0x200, 0xa, 0xe);
    if (arg3 >= 2) {
        uint8_t *copy = dst + 0x508;
        int32_t i = 1;

        do {
            Rtos_Memcpy(copy, copy - 0x1420, 0x200);
            Rtos_Memcpy(copy + 0x200, copy - 0x1220, 0x200);
            Rtos_Memset(copy + 0x1400, 0, 0x20);
            copy += 0x1420;
            ++i;
        } while (i != arg3);
    }
    return 1;
}
