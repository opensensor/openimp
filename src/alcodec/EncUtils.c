#include <stdint.h>
#include <stdio.h>

#include "alcodec/al_buffer.h"
#include "alcodec/al_rtos.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, void *caller);

int32_t AL_UpdateAspectRatio(uint8_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4);
int32_t FromHex(int32_t arg1, int32_t arg2);

static const int32_t Prime_4989[9] = {
    2, 3, 5, 7, 0xb, 0xd, 0x11, 0x13, 0x17,
};

static const uint8_t CUSTOM_LDA_TABLE[0xd0] = {
    0x29, 0xd6, 0x54, 0xe5, 0x3e, 0x65, 0xc1, 0x47, 0x7b, 0x45, 0xcb, 0xe8, 0x28, 0x86, 0x8b, 0xac,
    0xd1, 0xb8, 0x32, 0x11, 0xa9, 0x81, 0xad, 0xe6, 0xe1, 0xda, 0x3c, 0x73, 0xff, 0xc2, 0x50, 0xf9,
    0x50, 0x4b, 0x80, 0xf8, 0xb9, 0x6f, 0x48, 0x66, 0xfe, 0xc2, 0xc6, 0xf3, 0xea, 0x96, 0x71, 0xf1,
    0xc7, 0x46, 0x9e, 0xe1, 0xfc, 0x76, 0xa6, 0x01, 0xfc, 0xf4, 0xd4, 0x58, 0x0c, 0x1f, 0xd1, 0x01,
    0xcc, 0xb5, 0x75, 0xc7, 0x3b, 0x3c, 0x4d, 0x03, 0xee, 0xfb, 0x7b, 0xfa, 0xa7, 0x7f, 0x9a, 0x95,
    0xb0, 0x39, 0xfa, 0xed, 0x26, 0xa9, 0x83, 0x59, 0xf2, 0x53, 0xed, 0xc7, 0x41, 0x17, 0x96, 0xb8,
    0xd0, 0xd8, 0x8d, 0xe7, 0xf4, 0x18, 0xd5, 0x00, 0x8e, 0x88, 0x7c, 0xa9, 0x18, 0x51, 0x70, 0x81,
    0x25, 0xb2, 0x43, 0x6b, 0x6a, 0xff, 0x0f, 0xcc, 0x14, 0x79, 0xf9, 0x09, 0xf5, 0x4b, 0x41, 0x95,
    0xed, 0xe2, 0x06, 0x48, 0x2f, 0x73, 0x5b, 0x96, 0x1d, 0xb2, 0xfc, 0x25, 0x3f, 0x9e, 0xc3, 0x73,
    0x18, 0xa5, 0x29, 0x5b, 0xec, 0xec, 0xde, 0x34, 0x8e, 0x0f, 0xd9, 0x26, 0x06, 0xe1, 0x52, 0xfa,
    0xde, 0x98, 0x29, 0x06, 0x2c, 0xd0, 0xd8, 0xbd, 0x87, 0x61, 0x8d, 0xd8, 0xaf, 0x19, 0x26, 0xb2,
    0x38, 0x8d, 0x34, 0x02, 0x57, 0x45, 0x49, 0xf3, 0x1c, 0x4d, 0x4a, 0x6b, 0xe8, 0xd2, 0x99, 0x00,
    0xb3, 0xfb, 0xc2, 0x13, 0x52, 0x11, 0x2c, 0x5a, 0x6c, 0xc0, 0xcb, 0x97, 0x78, 0xee, 0x86, 0x1e,
};

static const uint8_t AL_AVC_DefaultScalingLists8x8[0x80] = {
    0x06, 0x0a, 0x0d, 0x10, 0x12, 0x17, 0x19, 0x1b, 0x0a, 0x0b, 0x10, 0x12, 0x17, 0x19, 0x1b, 0x1d,
    0x0d, 0x10, 0x12, 0x17, 0x19, 0x1b, 0x1d, 0x1f, 0x10, 0x12, 0x17, 0x19, 0x1b, 0x1d, 0x1f, 0x21,
    0x12, 0x17, 0x19, 0x1b, 0x1d, 0x1f, 0x21, 0x24, 0x17, 0x19, 0x1b, 0x1d, 0x1f, 0x21, 0x24, 0x26,
    0x19, 0x1b, 0x1d, 0x1f, 0x21, 0x24, 0x26, 0x28, 0x1b, 0x1d, 0x1f, 0x21, 0x24, 0x26, 0x28, 0x2a,
    0x09, 0x0d, 0x0f, 0x11, 0x13, 0x15, 0x16, 0x18, 0x0d, 0x0d, 0x11, 0x13, 0x15, 0x16, 0x18, 0x19,
    0x0f, 0x11, 0x13, 0x15, 0x16, 0x18, 0x19, 0x1b, 0x11, 0x13, 0x15, 0x16, 0x18, 0x19, 0x1b, 0x1c,
    0x13, 0x15, 0x16, 0x18, 0x19, 0x1b, 0x1c, 0x1e, 0x15, 0x16, 0x18, 0x19, 0x1b, 0x1c, 0x1e, 0x20,
    0x16, 0x18, 0x19, 0x1b, 0x1c, 0x1e, 0x20, 0x21, 0x18, 0x19, 0x1b, 0x1c, 0x1e, 0x20, 0x21, 0x23,
};

static const uint8_t AL_AVC_DefaultScalingLists4x4[0x20] = {
    0x06, 0x0d, 0x14, 0x1c, 0x0d, 0x14, 0x1c, 0x20, 0x14, 0x1c, 0x20, 0x25, 0x1c, 0x20, 0x25, 0x2a,
    0x0a, 0x0e, 0x14, 0x18, 0x0e, 0x14, 0x18, 0x1b, 0x14, 0x18, 0x1b, 0x1e, 0x18, 0x1b, 0x1e, 0x22,
};

int32_t AL_AVC_GenerateSPS_Resolution(uint8_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6)
{
    int32_t t0 = 1;
    int32_t v1 = 1 << (arg4 & 0x1f);
    int32_t t2_2 = (arg2 + v1 - 1) >> (arg4 & 0x1f);
    int32_t t1_1 = ((uint32_t)arg5 >> 8) & 0xf;
    int32_t t3_1 = 2;
    int32_t lo;
    int32_t v1_3;
    int32_t lo_1;
    uint8_t t4_1;

    if ((uint32_t)(t1_1 - 1) >= 2U)
        t3_1 = 1;
    lo = ((t2_2 << (arg4 & 0x1f)) - arg2) / t3_1;
    if (t3_1 == 0)
        __builtin_trap();
    v1_3 = (v1 + arg3 - 1) >> (arg4 & 0x1f);
    if (t1_1 == 1)
        t0 = 2;
    /* +0xb68 */
    *(uint16_t *)(arg1 + 0xb68) = (uint16_t)(v1_3 - 1);
    /* +0xb66 */
    *(uint16_t *)(arg1 + 0xb66) = (uint16_t)(t2_2 - 1);
    /* +0xb70 */
    *(int32_t *)(arg1 + 0xb70) = 0;
    /* +0xb78 */
    *(int32_t *)(arg1 + 0xb78) = 0;
    /* +0xb74 */
    *(int32_t *)(arg1 + 0xb74) = lo;
    lo_1 = ((v1_3 << (arg4 & 0x1f)) - arg3) / t0;
    if (t0 == 0)
        __builtin_trap();
    t4_1 = (uint8_t)((0 < lo_1) ? 1 : 0);
    if (lo >= 1)
        t4_1 = 1;
    /* +0xb7c */
    *(int32_t *)(arg1 + 0xb7c) = lo_1;
    arg1[0xb6d] = t4_1;
    return AL_UpdateAspectRatio(arg1 + 0xb84, arg2, arg3, arg6);
}

void AL_Decomposition(int32_t *arg1, uint8_t *arg2)
{
    int32_t v1 = *arg1;
    int32_t v1_1;

    *arg2 = 0;
    if (v1 == 0)
        goto label_57794;
    if ((v1 & 1) == 0) {
        while (1) {
            uint32_t v0_3;

            *arg1 = (int32_t)((uint32_t)v1 >> 1);
            v0_3 = (uint32_t)(*arg2 + 1);
            *arg2 = (uint8_t)v0_3;
            v1 = *arg1;
            if (v1 == 0)
                goto label_57794;
            if (v0_3 < 0xfU && (v1 & 1) == 0)
                continue;
            v1_1 = v1 - 1;
            break;
        }
    } else {
        v1_1 = v1 - 1;
    }
    *arg1 = v1_1;
    return;

label_57794:
    *arg1 = -1;
}

int32_t AL_Reduction(int32_t *arg1, int32_t *arg2)
{
    int32_t a2 = *arg1;
    const int32_t *t0 = Prime_4989;
    int32_t result = 2;

    while (1) {
        if (result == 0)
            __builtin_trap();
        if ((uint32_t)a2 % (uint32_t)result == 0U) {
            uint32_t v1_2 = (uint32_t)*arg2;

            if (result == 0)
                __builtin_trap();
            if (v1_2 % (uint32_t)result != 0U)
                break;
            do {
                if (result == 0)
                    __builtin_trap();
                *arg1 = (int32_t)((uint32_t)a2 / (uint32_t)result);
                if (result == 0)
                    __builtin_trap();
                v1_2 = (uint32_t)*arg2 / (uint32_t)result;
                *arg2 = (int32_t)v1_2;
                a2 = *arg1;
                if (result == 0)
                    __builtin_trap();
            } while ((uint32_t)a2 % (uint32_t)result == 0U);
        }
        t0 = &t0[1];
        if (t0 == &Prime_4989[9])
            break;
        result = *t0;
    }
    return result;
}

int32_t AL_UpdateAspectRatio(uint8_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    int32_t result = ((arg3 + 0xf) >> 4) << 4;
    int32_t var_10;
    int32_t var_c;

    *arg1 = 0;
    arg1[1] = 0;
    *(uint16_t *)(arg1 + 2) = 0;
    *(uint16_t *)(arg1 + 4) = 0;
    if (arg4 != 4) {
        if (arg4 != 0) {
            if (arg4 == 2)
                goto label_57998;
            if (arg4 == 3) {
                if (arg2 != 0x160)
                    goto label_5791c;
                result = 0x240;
                if (arg3 == 0x1e0) {
                    arg1[1] = 8;
                    result = 1;
                    *arg1 = 1;
                    return result;
                }
                if (arg3 != 0x240)
                    goto label_57958;
                arg1[1] = 9;
                result = 1;
                *arg1 = 1;
                return result;
            }
            if (arg4 == 1)
                goto label_57990;
            var_c = arg2;
            var_10 = arg3;
label_578c0:
            if (arg3 == arg2)
                goto label_57990;
label_578d0:
            AL_Reduction(&var_c, &var_10);
            *(uint16_t *)(arg1 + 2) = (uint16_t)var_10;
            *(uint16_t *)(arg1 + 4) = (uint16_t)var_c;
            arg1[1] = 0xff;
            result = 1;
            *arg1 = 1;
            return result;
        }

label_5791c:
        if (arg2 == 0x1e0) {
            result = 0x240;
            if (arg3 == arg2)
                goto label_57b00;
            if (arg3 != 0x240)
                goto label_57958;
            arg1[1] = 6;
            result = 1;
            *arg1 = 1;
            return result;
        }
        if (arg2 == 0x210) {
            result = 0x240;
            if (arg3 == 0x1e0)
                goto label_57b58;
            if (arg3 != 0x240)
                goto label_57958;
            arg1[1] = 0xc;
            result = 1;
            *arg1 = 1;
            return result;
        }
        if (arg2 == 0x2d0) {
            result = 0x240;
            if (arg3 == 0x1e0)
                goto label_57a60;
            if (arg3 != 0x240)
                goto label_57958;
            arg1[1] = 4;
            result = 1;
            *arg1 = 1;
            return result;
        }
        if (arg2 == 0x3c0) {
            if (result != 0x440)
                goto label_57958;
            arg1[1] = 0x10;
            result = 1;
            *arg1 = 1;
            return result;
        }
        if (arg2 == 0x500) {
            if (arg3 == 0x2d0)
                goto label_57990;
            if (result != 0x440)
                goto label_57958;
            arg1[1] = 0xf;
            result = 1;
            *arg1 = 1;
            return result;
        }
        if (arg2 != 0x5a0) {
            if (arg2 == 0x780 && result == 0x440)
                goto label_57990;
            goto label_57958;
        }
        if (result != 0x440)
            goto label_57958;
        arg1[1] = 0xe;
        result = 1;
        *arg1 = 1;
        return result;
    }

    return result;

label_57990:
    arg1[1] = 1;
    result = 1;
    *arg1 = 1;
    return result;

label_57998:
    if (arg2 == 0x160) {
        if (arg3 == 0xf0)
            goto label_57a40;
        if (arg3 == 0x120)
            goto label_57a5c;
        result = 0x240;
        if (arg3 == 0x1e0)
            goto label_57b00;
        if (arg3 != 0x240)
            goto label_579cc;
        arg1[1] = 6;
        result = 1;
        *arg1 = 1;
        return result;
    } else if (arg2 == 0x1e0) {
        result = 0x240;
        if (arg3 == arg2) {
label_57b18:
            arg1[1] = 0xb;
            result = 1;
            *arg1 = 1;
            return result;
        }
        if (arg3 != 0x240)
            goto label_579cc;
        arg1[1] = 0xa;
        result = 1;
        *arg1 = 1;
        return result;
    } else if (arg2 == 0x210) {
        result = 0x240;
        if (arg3 == 0x1e0) {
label_57b0c:
            arg1[1] = 5;
            result = 1;
            *arg1 = 1;
            return result;
        }
        if (arg3 != 0x240)
            goto label_579cc;
        arg1[1] = 4;
        result = 1;
        *arg1 = 1;
        return result;
    } else if (arg2 == 0x280) {
        if (arg3 != 0x1e0)
            goto label_579cc;
        result = 1;
        arg1[1] = 1;
        *arg1 = 1;
        return result;
    } else if (arg2 == 0x2d0) {
        result = 0x240;
        if (arg3 == 0x1e0)
            goto label_57a40;
        if (arg3 != 0x240)
            goto label_579cc;
        goto label_57a5c;
    } else if (arg2 == 0x5a0 && result == 0x440) {
        result = 1;
        arg1[1] = 1;
        *arg1 = 1;
        return result;
    }
    goto label_579cc;

label_57b00:
    arg1[1] = 7;
    result = 1;
    *arg1 = 1;
    return result;

label_579cc:
    if (arg4 != 0) {
        arg2 *= 3;
        arg3 <<= 2;
        var_c = arg2;
        var_10 = arg3;
        goto label_578c0;
    }

label_57958:
    if (arg4 != 0)
        goto label_57964;
    {
        int32_t a1_1 = arg2 * 9;

        arg3 <<= 4;
        var_c = a1_1;
        var_10 = arg3;
        if (arg3 != a1_1)
            goto label_578d0;
        goto label_57990;
    }

label_57964:
    var_c = arg2 * 3;
    var_10 = arg3 << 2;
    if (var_10 != var_c)
        goto label_578d0;
    goto label_57990;

label_57a40:
    arg1[1] = 3;
    result = 1;
    *arg1 = 1;
    return result;

label_57a5c:
    arg1[1] = 2;
    result = 1;
    *arg1 = 1;
    return result;

label_57b58:
    arg1[1] = 0xd;
    result = 1;
    *arg1 = 1;
    return result;

label_57a60:
    arg1[1] = 5;
    result = 1;
    *arg1 = 1;
    return result;
}

int32_t AL_IsGdrEnabled(const uint8_t *arg1)
{
    return ((uint32_t)*(const uint32_t *)(arg1 + 0xbc) >> 1) & 1;
}

AL_TBuffer *AL_GetSrcBufferFromStatus(void *arg1)
{
    return *(AL_TBuffer **)((uint8_t *)arg1 + 8);
}

int32_t fillScalingList(uint8_t *arg1, uint8_t *arg2, int32_t arg3, int32_t arg4, int32_t arg5, uint8_t *arg6)
{
    int32_t v0_1 = arg3 * 6;
    uint8_t *a0_1 = arg2 + (((v0_1 + arg4) << 6) + 0x120 + 0x1b);

    if (arg1[v0_1 + arg4 + 0x728] != 0) {
        int32_t v0_5 = 0x40;

        *arg6 = 1;
        if (arg3 == 0)
            v0_5 = 0x10;
        return (int32_t)(intptr_t)Rtos_Memcpy(a0_1, arg1 + (((v0_1 + arg4) << 6) + 0x120 + 8), (size_t)v0_5);
    }
    *arg6 = 0;
    if (arg3 != 0)
        return (int32_t)(intptr_t)Rtos_Memcpy(a0_1, AL_AVC_DefaultScalingLists8x8 + (arg5 << 6), 0x40);
    if (arg4 != 0 && arg4 != 3) {
        if (*(arg2 + arg4 + 0x16) != 0) {
            *arg6 = 1;
            return (int32_t)(intptr_t)Rtos_Memcpy(a0_1, arg2 + ((arg4 - 1) << 6) + 0x13b, 0x10);
        }
    }
    return (int32_t)(intptr_t)Rtos_Memcpy(a0_1, AL_AVC_DefaultScalingLists4x4 + (arg5 << 4), 0x10);
}

int32_t FromHex(int32_t arg1, int32_t arg2)
{
    int32_t v1_5 = arg1 & 0xff;
    int32_t v0_3;
    int32_t v1;

    if ((uint32_t)(v1_5 - 0x61) >= 6U) {
        if ((uint32_t)(v1_5 - 0x41) >= 6U) {
            v0_3 = 0;
            if ((uint32_t)(v1_5 - 0x30) < 0xaU)
                v0_3 = (arg1 - 0x30) << 4;
            goto label_589cc;
        }
        v1 = arg2 & 0xff;
        v0_3 = (arg1 - 0x37) << 4;
        if ((uint32_t)(v1 - 0x61) < 6U)
            return arg2 - 0x57 + v0_3;
    } else {
        v0_3 = (arg1 - 0x57) << 4;
label_589cc:
        v1 = arg2 & 0xff;
        if ((uint32_t)(v1 - 0x61) < 6U)
            return arg2 - 0x57 + v0_3;
    }
    if ((uint32_t)(v1 - 0x41) < 6U)
        return arg2 - 0x37 + v0_3;
    {
        int32_t a1 = arg2 - 0x30;

        if ((uint32_t)(v1 - 0x30) >= 0xaU)
            a1 = 0;
        return a1 + v0_3;
    }
}

int32_t LoadLambdaFromFile(char *arg1, int32_t *arg2)
{
    FILE *stream = fopen(arg1, "r");
    int32_t s0 = *arg2;

    if (stream != 0) {
        int32_t s5_1 = s0 + 0xd0;
        char str[0x100];

        while (fgets(str, 0x100, stream) != 0) {
            s0 += 4;
            *(uint8_t *)(intptr_t)(s0 - 4) = (uint8_t)FromHex((int32_t)str[6], (int32_t)str[7]);
            *(uint8_t *)(intptr_t)(s0 - 3) = (uint8_t)FromHex((int32_t)str[4], (int32_t)str[5]);
            *(uint8_t *)(intptr_t)(s0 - 2) = (uint8_t)FromHex((int32_t)str[2], (int32_t)str[3]);
            *(uint8_t *)(intptr_t)(s0 - 1) = (uint8_t)FromHex((int32_t)str[0], (int32_t)str[1]);
            if (s0 == s5_1) {
                arg2[5] |= 1;
                fclose(stream);
                return 1;
            }
        }
    }
    return 0;
}

int32_t LoadCustomLda(int32_t *arg1)
{
    return (int32_t)(intptr_t)Rtos_Memcpy((void *)(intptr_t)*arg1, CUSTOM_LDA_TABLE, 0xd0);
}

int32_t AL_H273_ColourDescToColourPrimaries(int32_t arg1)
{
    switch (arg1) {
    case 0:
        return 0;
    case 2:
        return 4;
    case 3:
        return 6;
    case 4:
        return 5;
    case 5:
        return 1;
    case 6:
        return 9;
    case 7:
        return 7;
    case 8:
        return 0xa;
    case 9:
        return 0xb;
    case 0xa:
        return 0xc;
    case 0xb:
        return 0x16;
    case 0xc:
        return 8;
    case 0xd:
        __assert("0",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common_enc/EncUtils.c",
                 0x17,
                 "AL_H273_ColourDescToColourPrimaries",
                 &_gp);
        return 2;
    default:
        return 2;
    }
}
