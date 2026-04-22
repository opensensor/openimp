#include <stdint.h>

#include "alcodec/al_rtos.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line,
                        const char *function, void *caller);
extern const uint8_t PicStructToFieldNumber[];

/* Placement:
 * - o0i/OII/l0I/Iol/I0l/ioI/oiI/o0I/I0I/OiI/llI @ RateCtrl_3.c
 */

int32_t AL_DPBConstraint_GetMaxRef_GopMngrCustom(void *arg1);
int32_t rc_loo(int32_t *arg1, int32_t arg2);
int32_t rc_Iio(char arg1, void *arg2);

/* stock name: o0i */
int32_t rc_o0i(void);
/* stock name: OII */
static char rc_OII(void *arg1);
/* stock name: l0I */
int32_t rc_l0I(void *arg1, int32_t arg2);
/* stock name: Iol */
int32_t rc_Iol(void);
/* stock name: I0l */
int32_t rc_I0l(void *arg1);
/* stock name: ioI */
int32_t *rc_ioI(void *arg1, int32_t *arg2, int32_t arg3);
/* stock name: oiI */
int32_t rc_oiI(void);
/* stock name: o0I */
int32_t rc_o0I(void);
/* stock name: I0I */
int32_t rc_I0I(void);
/* stock name: OiI */
int32_t rc_OiI(void *arg1);
/* stock name: o1I */
int32_t rc_o1I(void *arg1, int32_t *arg2);
/* stock name: llI */
int32_t rc_llI(void);
/* stock name: IIl */
int32_t rc_IIl(void *arg1, void *arg2, int32_t *arg3, void *arg4, int32_t *arg5, char arg6,
               char arg7, char *arg8);
/* stock name: Iil.isra.4 */
int32_t rc_Iil_isra_4(int32_t *arg1, int32_t *arg2, int32_t arg3, int32_t *arg4, int32_t *arg5);

static const int32_t rc_iIi_5070[17] = {0, 0, 8, 4, 2, 1, 3, 6, 5,
                                        7, 12, 10, 9, 11, 14, 13, 15};

/* stock name: o0i */
int32_t rc_o0i(void)
{
    return 0;
}

/* stock name: OII */
static char rc_OII(void *arg1)
{
    int32_t base = *(int32_t *)((char *)arg1 + 0x28);
    int32_t used = *(int32_t *)((char *)arg1 + 0x3c);
    int32_t next = (int32_t)*(uint8_t *)((char *)arg1 + 6) + 1;
    char result = 0;
    char *slot;

    *(int32_t *)((char *)arg1 + 0x30) = next;
    while ((((1U << ((next - base) & 0x1f)) - 1U) & (uint32_t)used) != 0) {
        if (next == 8) {
            next = 6;
        } else if (next == 0x10) {
            next = 8;
        } else if (next == 6) {
            next = 4;
        } else {
            --next;
        }
        *(int32_t *)((char *)arg1 + 0x30) = next;
    }

    slot = (char *)arg1 + next * 6;
    *(uint8_t *)((char *)arg1 + 0xa9) = (uint8_t)next;
    slot[0x44 - 0x00] = (char)((next << 1) - 1);
    slot[0x43 - 0x00] = 0;
    slot[0x45 - 0x00] = 0;
    if (*(uint8_t *)((char *)arg1 + 0x41) != 0) {
        result = *(uint8_t *)((char *)arg1 + 0x42) == 0 ? (char)0xff : 0;
    }
    slot[0x46 - 0x00] = result;
    slot[0x47 - 0x00] = (char)(next >= 2);
    slot[0x48 - 0x00] = 0;
    return result;
}

/* stock name: l0I */
int32_t rc_l0I(void *arg1, int32_t arg2)
{
    char *ctx = *(char **)((char *)arg1 + 0x4c);
    int32_t result = *(int32_t *)(ctx + 0x3c);

    *(int32_t *)(ctx + 0x3c) = result | (1 << (arg2 & 0x1f));
    if (*(int32_t *)(ctx + 0x28) == 0) {
        return (int32_t)rc_OII(ctx);
    }
    return result;
}

/* stock name: Iol */
int32_t rc_Iol(void)
{
    return 0;
}

/* stock name: I0l */
int32_t rc_I0l(void *arg1)
{
    void *allocator = *(void **)((char *)arg1 + 0x40);
    void (*free_fn)(void *allocator, void *buffer) =
        ((void (*)(void *, void *))(*(intptr_t *)allocator + 8));

    free_fn(allocator, *(void **)((char *)arg1 + 0x44));
    return 0;
}

/* stock name: ioI */
int32_t *rc_ioI(void *arg1, int32_t *arg2, int32_t arg3)
{
    int32_t *result = *(int32_t **)((char *)arg1 + 0x4c);
    int32_t flags;

    if (*(uint8_t *)(result + 7) != 0) {
        return result;
    }

    flags = arg2[0];
    if ((flags & 4) == 0) {
        return (int32_t *)(intptr_t)__assert(
            "loI -> l & 0x04",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_3.c",
            0x257, "ioI", &_gp);
    }

    result[0] = flags;
    result[1] = arg2[1];
    result[2] = arg2[2];
    result[3] = arg2[3];
    result[4] = arg2[4];
    result[5] = arg2[5];
    result[6] = arg2[6];
    *((uint8_t *)result + 0x40) = (uint8_t)(arg3 < 1);
    result[8] = -1;
    result[9] = 0;
    result[0x0c] = (int32_t)*(uint8_t *)((char *)arg2 + 6) + 1;
    result[0x0e] = 0;
    result[0x0b] = -1;
    result[0x0a] = (int32_t)*(uint8_t *)((char *)arg2 + 6);
    *((uint8_t *)result + 0x1d) = 0;
    result[0x0f] = 1;
    *(int32_t *)((char *)result + 0x34) =
        (int32_t)*(uint8_t *)((char *)result + 4) / ((int32_t)*(uint8_t *)((char *)result + 6) + 1);
    *((uint8_t *)result + 0xaa) = 0;
    *((uint8_t *)result + 0x42) = 0;
    *((uint8_t *)result + 7) = 0;
    *((uint8_t *)result + 0x41) = (uint8_t)(flags & 1);
    return result;
}

/* stock name: oiI */
int32_t rc_oiI(void)
{
    __assert("0", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_3.c",
             0x315, "oiI", &_gp);
    return rc_o0I();
}

/* stock name: o0I */
int32_t rc_o0I(void)
{
    __assert("0", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_3.c",
             0x6b4, "o0I", &_gp);
    return rc_I0I();
}

/* stock name: I0I */
int32_t rc_I0I(void)
{
    return rc_OiI((void *)(intptr_t)__assert(
        "0", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_3.c",
        0x6c7, "I0I", &_gp));
}

/* stock name: OiI */
int32_t rc_OiI(void *arg1)
{
    return AL_DPBConstraint_GetMaxRef_GopMngrCustom(*(void **)((char *)arg1 + 0x4c));
}

/* stock name: o1I */
int32_t rc_o1I(void *arg1, int32_t *arg2)
{
    char *ctx = *(char **)((char *)arg1 + 0x4c);
    int32_t index;
    uint8_t chroma_mode;
    int32_t flags;

    Rtos_Memset(arg2, 0, 0x34);
    arg2[5] = 0;
    arg2[1] = 0x38;
    *((uint8_t *)arg2 + 0x2c) = 0;
    arg2[0x0c] = 0;
    *((uint8_t *)arg2 + 0x2e) = 0;

    if ((*(int32_t *)(ctx + 0x3c) & 1) == 0) {
        rc_Iio(0, arg2);
        *(int32_t *)arg2 = *(int32_t *)(ctx + 0x2c) + 1;
        arg2[1] = 0x3b;
        arg2[2] = 0;
        arg2[3] = 0;
        arg2[4] = 2;
        arg2[8] = (rc_iIi_5070[*(int32_t *)(ctx + 0x30) + 1] + 1) << 1;
        *((uint8_t *)arg2 + 0x25) = 0;
        *((uint8_t *)arg2 + 0x27) = 0xff;
        return -1;
    }

    index = (int32_t)*(int8_t *)(ctx + 0xa9);
    chroma_mode = (uint8_t)ctx[index * 6 + 0x44];
    flags = arg2[1];

    if (chroma_mode != 1) {
        if ((chroma_mode & 1) != 0) {
            flags |= 2;
            arg2[1] = flags;
            if (*(uint8_t *)(ctx + 0x1d) != 0 && *(int32_t *)(ctx + 0x28) == 0) {
                int32_t next = *(int32_t *)(ctx + 0x20) + 1;

                if ((next < 0) || ((uint32_t)next >= (uint32_t)*(int32_t *)(ctx + 8))) {
                    arg2[1] = flags | 3;
                    *(int32_t *)arg2 = *(int32_t *)(ctx + 0x2c) + 1;
                    arg2[2] = 0;
                    arg2[4] = 2;
                    arg2[8] =
                        (rc_iIi_5070[*(int32_t *)(ctx + 0x30) + 1] + 1) *
                        (int32_t)PicStructToFieldNumber[(uint8_t)arg2[5]];
                    *((uint8_t *)arg2 + 0x25) = 0;
                    if ((arg2[1] & 1) == 0) {
                        arg2[3] = *(int32_t *)(ctx + 0x24);
                    }
                    *((uint8_t *)arg2 + 0x27) = 0xff;
                    return -1;
                }
            }
        }
    }

    arg2[2] = *(int32_t *)(ctx + 0x20) + index;
    if (*(uint8_t *)(ctx + 0x1d) != 0 && *(int32_t *)(ctx + 0x28) == 0 &&
        chroma_mode == 1) {
        int32_t next = *(int32_t *)(ctx + 0x20) + 1;

        if (next >= 0 && (uint32_t)next < (uint32_t)*(int32_t *)(ctx + 8)) {
            arg2[4] = 2;
        }
    } else {
        char *slot = ctx + index * 6;
        int32_t left = (int32_t)*(int8_t *)(slot + 0x45);
        int32_t right = (int32_t)*(int8_t *)(slot + 0x46);

        arg2[4] = (left == right) ? 1 : 0;
        if ((flags & 2) != 0 && left != right) {
            arg2[1] = flags | 0x80;
        }
    }

    *(int32_t *)arg2 = *(int32_t *)(ctx + 0x2c) + index;
    {
        char *slot = ctx + index * 6;
        uint8_t ref = (uint8_t)slot[0x43];

        if (*(uint8_t *)(ctx + 0x41) != 0 && ref != 0) {
            --ref;
        }
        *((uint8_t *)arg2 + 0x1c) = ref;
        *((uint8_t *)arg2 + 0x25) = (ref != 0) ? (uint8_t)ctx[ref + 0x17] : 0;
    }

    arg2[8] = ((int32_t)*(int8_t *)(ctx + 0xa9) - *(int32_t *)(ctx + 0x28) +
               rc_iIi_5070[*(int32_t *)(ctx + 0x30) + 1]) *
              (int32_t)PicStructToFieldNumber[(uint8_t)arg2[5]];
    if (arg2[8] < 0) {
        __assert("oOo -> O0i >= 0",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_3.c",
                 0x884, "o1I", &_gp);
    }
    if ((arg2[1] & 1) == 0) {
        arg2[3] = *(int32_t *)(ctx + 0x24);
    }
    *((uint8_t *)arg2 + 0x27) = 0xff;
    return -1;
}

/* stock name: llI */
int32_t rc_llI(void)
{
    return 0;
}

/* stock name: IIl */
int32_t rc_IIl(void *arg1, void *arg2, int32_t *arg3, void *arg4, int32_t *arg5, char arg6,
               char arg7, char *arg8)
{
    int32_t value = (int32_t)(int8_t)arg6 + *(int32_t *)((char *)arg2 + 8) -
                    (int32_t)*(uint8_t *)((char *)arg1 + 0xa9);
    int32_t result = rc_loo(arg3, value);

    if (result != 2) {
        if (arg7 == 0) {
            int32_t index = arg5[0x11];
            char *slot = (char *)arg4 + index * 0x14;

            *(int32_t *)(slot + 0x40) = 0;
            *(int32_t *)(slot + 0x44) = value;
            arg5[index + 0x12] = value;
            arg5[0x11] = index + 1;
        } else if (result != 0) {
            int32_t index = arg5[0x22];

            arg5[0x22] = index + 1;
            arg5[index + 0x23] = value;
        } else {
            int32_t index = arg5[0];

            arg5[0] = index + 1;
            arg5[index + 1] = value;
        }

        result = (uint8_t)*arg8;
        if (result == 0) {
            result = ((uint8_t)*(char *)((char *)arg1 + 0x42) ^ (uint8_t)arg6) == 0;
        }
        *arg8 = (char)result;
    }
    return result;
}

/* stock name: Iil.isra.4 */
int32_t rc_Iil_isra_4(int32_t *arg1, int32_t *arg2, int32_t arg3, int32_t *arg4, int32_t *arg5)
{
    int32_t result = 2;

    if (*arg4 != 2) {
        int32_t count = arg2[0] + arg2[1];
        int32_t target = *arg5;

        if (count > 0) {
            if (target == arg2[2]) {
                return 0;
            }
            for (int32_t i = 1; i < count; ++i) {
                if (arg2[2 + i] == target) {
                    return i;
                }
            }
        }
    }

    *arg5 = arg3;
    if (rc_loo(arg2, arg3) != 0) {
        if (rc_loo(arg2, *arg5) == 1) {
            result = arg1[0x22];
            arg1[0x22] = result + 1;
            arg1[result + 0x23] = *arg5;
        } else {
            *arg4 = 2;
        }
    } else {
        result = arg1[0];
        arg1[0] = result + 1;
        arg1[result + 1] = *arg5;
    }
    return result;
}
