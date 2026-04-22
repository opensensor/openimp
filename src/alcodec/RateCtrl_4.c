#include <stdint.h>

#include "alcodec/al_rtos.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line,
                        const char *function, void *caller);
extern const uint8_t PicStructToFieldNumber[];

int32_t rc_Il0(void *arg1);
void *rc_OI1(void *arg1, int32_t arg2);
uint32_t AL_DPBConstraint_GetMaxRef_DefaultGopMngr(int32_t *arg1, int32_t arg2);
static int32_t rc_o11_isra_4(int32_t *arg1, int32_t arg2, int32_t *arg3);
int32_t rc_ooo(int32_t *arg1, int32_t arg2, int32_t arg3);

/* Placement:
 * - OO1/O11/l11/oI1/II/Il/l1/lOo/loo/Oio/Iio/OIo/lIo/Olo @ RateCtrl_4.c
 * - IO1/I10/ol1/iOOo helper cluster pending continuation
 */

static const uint8_t rc_Il1_5053[5] = {0, 2, 3, 13, 18};
static const uint8_t rc_ll1_5054[5] = {1, 4, 5, 14, 19};

/* stock name: II */
int32_t rc_II(int32_t *arg1)
{
    arg1[1] = 2;
    arg1[2] = -1;
    return -1;
}

/* stock name: Il */
int32_t rc_Il(int32_t *arg1, int32_t *arg2)
{
    arg2[0x22] = 0;
    arg2[0] = 0;
    arg2[0x11] = 0;
    rc_II(arg1);
    rc_II(arg1 + 5);
    rc_II(arg1 + 0x0f);
    rc_II(arg1 + 0x14);
    return rc_II(arg1 + 0x19);
}

/* stock name: l1 */
int32_t rc_l1(void *arg1, int32_t *arg2)
{
    if (*(int32_t *)((char *)arg1 + 0x10) == 0) {
        if (arg2[1] == 2) {
            return 2;
        }

        arg2[5] = arg2[0];
        arg2[6] = arg2[1];
        arg2[7] = arg2[2];
        arg2[8] = arg2[3];
        arg2[9] = arg2[4];
        return arg2[4];
    }

    if (arg2[1] != 2) {
        return arg2[9];
    }

    arg2[0] = arg2[5];
    arg2[1] = arg2[6];
    arg2[2] = arg2[7];
    arg2[3] = arg2[8];
    arg2[4] = arg2[9];
    return arg2[9];
}

/* stock name: lOo */
int32_t rc_lOo(int32_t *arg1)
{
    int32_t count = arg1[0] + arg1[1];

    if (count <= 0) {
        return -1;
    }
    if (arg1[0x22] == 1) {
        return arg1[2];
    }

    for (int32_t i = 0; i < count; ++i) {
        if (arg1[0x23 + i] == 1) {
            return arg1[2 + i];
        }
    }
    return -1;
}

/* stock name: loo */
int32_t rc_loo(int32_t *arg1, int32_t arg2)
{
    int32_t count = arg1[0] + arg1[1];

    if (count <= 0) {
        return 2;
    }
    if (arg2 == arg1[2]) {
        return arg1[0x22];
    }

    for (int32_t i = 0; i < count; ++i) {
        if (arg1[2 + i] == arg2) {
            return arg1[0x22 + i];
        }
    }
    return 2;
}

/* stock name: Oio */
int32_t rc_Oio(int32_t *arg1, int32_t arg2)
{
    int32_t count = arg1[0] + arg1[1];

    if (count <= 0) {
        return 7;
    }
    if (arg2 == arg1[2]) {
        return arg1[0x42];
    }

    for (int32_t i = 0; i < count; ++i) {
        if (arg1[2 + i] == arg2) {
            return arg1[0x42 + i];
        }
    }
    return 7;
}

/* stock name: Iio */
int32_t rc_Iio(char arg1, void *arg2)
{
    *(int32_t *)((char *)arg2 + 0x18) = (arg1 != 0) ? 1 : 0;
    return 1;
}

/* stock name: OIo */
int32_t rc_OIo(int32_t *arg1, int32_t arg2)
{
    int32_t count = arg1[0] + arg1[1];
    int32_t best = -1;
    int32_t best_delta = -1;

    for (int32_t i = 0; i < count; ++i) {
        int32_t value = arg1[2 + i];
        int32_t delta = value - arg2;

        if (delta < 0) {
            delta = -delta;
        }
        if ((uint32_t)delta < (uint32_t)best_delta) {
            best_delta = delta;
            best = value;
        }
    }
    return best;
}

/* stock name: lIo */
int32_t rc_lIo(int32_t *arg1, int32_t arg2)
{
    int32_t count = arg1[0] + arg1[1];
    int32_t result = -1;

    if (count <= 0) {
        return -1;
    }

    result = arg1[2];
    if (result < arg2) {
        return result;
    }

    for (int32_t i = 1; i < count; ++i) {
        result = arg1[2 + i];
        if (result < arg2) {
            return result;
        }
    }
    return -1;
}

/* stock name: Olo */
int32_t rc_Olo(int32_t *arg1, int32_t arg2)
{
    int32_t count = arg1[0] + arg1[1];

    for (int32_t i = count - 1; i >= 0; --i) {
        int32_t value = arg1[2 + i];

        if (value > arg2) {
            return value;
        }
    }
    return -1;
}

/* stock name: l11 */
void rc_l11(void *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    char *entries = (char *)arg1;
    int32_t *scratch = (int32_t *)arg2;
    int32_t fallback = arg3 == *(int32_t *)(entries + 8) ? arg5 : arg4;

    if (arg3 < 0) {
        return;
    }
    if (arg3 != *(int32_t *)(entries + 8) && arg3 != *(int32_t *)(entries + 0x1c)) {
        int32_t index = scratch[0x11];
        char *slot = entries + index * 0x14;

        scratch[index + 0x12] = arg3;
        *(int32_t *)(slot + 0x40) = 1;
        *(int32_t *)(slot + 0x44) = arg3;
        scratch[0x11] = index + 1;
        return;
    }
    if (arg3 != arg4) {
        int32_t index = scratch[0x11];
        char *slot = entries + index * 0x14;

        scratch[index + 0x12] = arg4;
        *(int32_t *)(slot + 0x40) = 0;
        *(int32_t *)(slot + 0x44) = arg4;
        scratch[0x11] = index + 1;
        if (arg3 != *(int32_t *)(entries + 0x1c)) {
            return;
        }
    }
    if (arg3 == fallback) {
        return;
    }

    {
        int32_t index = scratch[0x11];
        char *slot = entries + index * 0x14;

        scratch[index + 0x12] = fallback;
        *(int32_t *)(slot + 0x40) = 0;
        *(int32_t *)(slot + 0x44) = fallback;
        scratch[0x11] = index + 1;
    }
}

/* stock name: OO1 */
int32_t rc_OO1(void *arg1)
{
    return (int32_t)AL_DPBConstraint_GetMaxRef_DefaultGopMngr(
        *(int32_t **)((char *)arg1 + 0x4c), 0);
}

/* stock name: O11 */
int32_t rc_O11(void *arg1, void *arg2)
{
    int32_t flags = *(int32_t *)((char *)arg2 + 4);
    char *ctx = *(char **)((char *)arg1 + 0x4c);
    char use_repeat = 0;

    if ((flags & 2) != 0) {
        use_repeat = (*(int32_t *)(ctx + 0x2c) <= 0 || *(int32_t *)(ctx + 0x10) == 0) &&
                     ctx[0x54] != 0;
    } else if ((flags & 1) != 0) {
        use_repeat = ctx[0x56];
    } else {
        use_repeat = ctx[0x54] != 0;
    }

    rc_Iio(use_repeat, arg2);
    if (ctx[0x55] != 0 && ctx[0x56] != 0) {
        *(int32_t *)((char *)arg2 + 4) |= 0x40;
    }

    if (ctx[0x57] != 0 && *(int32_t *)((char *)arg2 + 0x10) != 2) {
        *(int32_t *)((char *)arg2 + 4) |= 0x100;
    }
    return *(int32_t *)((char *)arg2 + 4);
}

/* stock name: oI1 */
int32_t rc_oI1(void *arg1, int32_t arg2)
{
    int32_t *ctx = *(int32_t **)((char *)arg1 + 0x4c);

    if ((uint32_t)arg2 >= 0x20U) {
        __assert("i0I >= 0 && i0I <= 037",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_4.c",
                 0x89c, "oI1", &_gp);
    }

    *(int32_t *)((char *)ctx + 0x4c) |= 1 << (arg2 & 0x1f);
    return (int32_t)(intptr_t)rc_OI1(arg1, arg2);
}

/* stock name: o11.isra.4 */
static int32_t rc_o11_isra_4(int32_t *arg1, int32_t arg2, int32_t *arg3)
{
    if (arg2 == 0) {
        int32_t index = arg1[0];

        arg1[0] = index + 1;
        arg1[index + 1] = *arg3;
        return index << 2;
    }
    if (arg2 != 1) {
        return 1;
    }

    {
        int32_t index = arg1[0x22];

        arg1[0x22] = index + 1;
        arg1[index + 0x23] = *arg3;
        return (index + 0x20) << 2;
    }
}

/* stock name: ooo */
int32_t rc_ooo(int32_t *arg1, int32_t arg2, int32_t arg3)
{
    int32_t count = arg1[0] + arg1[1];
    int32_t result = 0;

    for (int32_t i = 0; i < count; ++i) {
        if (arg1[2 + i] == arg2) {
            result = ((arg1[0x22 + i] ^ arg3) == 0);
        }
    }
    return result;
}
