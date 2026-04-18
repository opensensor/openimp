#include <stdint.h>

#include "alcodec/al_rtos.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line,
                        const char *function, void *caller);
extern const uint8_t PicStructToFieldNumber[];

int32_t rc_l0io(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                int32_t arg6, char arg7, char arg8);
int32_t rc_iiIo(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4);
uint32_t rc_IIIo(int32_t *arg1, int32_t arg2);
int32_t rc_OOlo(void *arg1);
int32_t rc_i1Io(void *arg1);
int32_t rc_l1Io(int32_t *arg1);

/* Placement:
 * - Ooio @ RateCtrl_18.c (HLIL neighborhood before GetTargetSize)
 * - GetTargetSize/OiOI/IiOI/OIOI/oiOI/iiOI/oIOI/iIOI/IIOI @ RateCtrl_18.c
 */

static const int32_t O1oo[103] = {
    0x5b, 0x66, 0x72, 0x80, 0x90, 0xa1, 0xb5, 0xcb, 0xe4, 0x100, 0x11f, 0x143, 0x16a,
    0x196, 0x1c8, 0x200, 0x23f, 0x285, 0x2d4, 0x32d, 0x390, 0x400, 0x47d, 0x50a, 0x5a8,
    0x659, 0x721, 0x800, 0x8fb, 0xa14, 0xb50, 0xcb3, 0xe41, 0x1000, 0x11f6, 0x1429,
    0x16a1, 0x1966, 0x1c82, 0x2000, 0x23eb, 0x2851, 0x2d41, 0x32cc, 0x3904, 0x4000,
    0x47d6, 0x50a3, 0x5a82, 0x6598, 0x7209, 0x8000, 0x8fad, 0xa145, 0xb505, 0xcb30,
    0xe412, 0x10000, 0x11f5a, 0x1428a, 0x16a0a, 0x19660, 0x1c824, 0x20000, 0x23eb3,
    0x28514, 0x2d414, 0x32cc0, 0x39048, 0x40000, 0x47d67, 0x50a29, 0x5a828, 0x65980,
    0x72090, 0x80000, 0x8facd, 0xa1451, 0xb504f, 0xcb2ff, 0xe411f, 0x100000, 0x11f59b,
    0x1428a3, 0x16a09e, 0x1965ff, 0x1c823e, 0x200000, 0x23eb36, 0x285146, 0x2d413d,
    0x32cbfd, 0x39047c, 0x400000, 0x47d66b, 0x50a28c, 0x5a827a, 0x6597fb, 0x7208f8,
    0x800000, 0x8facd6, 0xa14518, 0xb504f3
};

void *Ooio(void *arg1, int32_t arg2, int32_t arg3)
{
    const int32_t *i = O1oo;
    char *v1 = (char *)arg1;
    int32_t *i_1;

    (void)arg2;
    (void)arg3;

    do {
        int32_t a1_1 = *i;

        v1 += 4;
        i = &i[1];
        *(int32_t *)(v1 - 4) = a1_1;
    } while (i != O1oo + 0x67);

    i_1 = (int32_t *)((char *)arg1 + 0x19c);
    do {
        *i_1 = 0;
        i_1 = &i_1[1];
    } while (i_1 != (int32_t *)((char *)arg1 + 0x200));
    return i_1;
}

uint32_t GetTargetSize(void *arg1, int32_t *arg2, int32_t arg3)
{
    if (*arg2 != 8) {
        uint32_t s2_1 = (uint32_t)(uint16_t)arg2[1];
        int32_t s3;
        uint32_t v1_1;
        uint64_t prod;
        uint32_t v0_3;

        if (s2_1 >= 2) {
            s3 = *(int32_t *)((char *)arg1 + 0x10);
            v1_1 = (uint32_t)*(uint16_t *)((char *)arg1 + 0x0c);
            prod = (uint64_t)(uint16_t)*(int16_t *)((char *)arg1 + 0x0e) * (uint64_t)(uint32_t)s3;
            v0_3 = (uint32_t)(prod / ((uint64_t)v1_1 * 1000ULL));
            if (arg3 != 2) {
                uint32_t v0_4 = v0_3 << 2;
                uint32_t a1_1 = v0_4 + v0_3;
                uint32_t s1_2 =
                    (uint32_t)((a1_1 < v0_4 ? 1U : 0U) + (v0_3 >> 30));
                uint32_t v1_3 = a1_1 >> 28;
                uint32_t a0_3 = a1_1 * 0x11;
                uint64_t num = ((uint64_t)(((a0_3 < a1_1 ? 1U : 0U) + s1_2 + (v1_3 | (s1_2 << 4))))
                                << 32) |
                               a0_3;

                return (uint32_t)(num / 100ULL);
            }

            {
                int32_t s2_3 = (int32_t)((s2_1 - 1) * 0xf);
                uint64_t prod_1 = (uint64_t)(uint32_t)s2_3 * (uint64_t)v0_3;
                uint32_t v0_11 = (uint32_t)(prod_1 / 100ULL);
                uint64_t prod_2 = (uint64_t)(uint32_t)s3 *
                                  (uint64_t)(uint32_t)*(int32_t *)((char *)arg1 + 8);
                uint32_t s1_4 = v0_11 + v0_3;
                uint32_t v0_12 = (uint32_t)(prod_2 / 2000ULL);
                uint32_t a1_7;
                uint32_t v0_13;
                uint32_t a2_4;
                uint32_t v0_14;
                uint32_t a0_8;
                uint32_t v1_8;
                uint64_t num;

                if (v0_12 < s1_4) {
                    s1_4 = v0_12;
                }
                a1_7 = s1_4 >> 27;
                v0_13 = s1_4 << 5;
                a2_4 = s1_4 << 7;
                v0_14 = a2_4 - v0_13;
                a0_8 = v0_14 - s1_4;
                v1_8 = (v0_13 >> 30 | (a1_7 << 2)) - a1_7;
                num = ((uint64_t)(v1_8 - (a2_4 < v0_14 ? 1U : 0U) - (v0_14 < a0_8 ? 1U : 0U))
                       << 32) |
                      a0_8;
                return (uint32_t)(num / 100ULL);
            }
        }
    }

    {
        uint32_t a2_6 = (uint32_t)*(uint16_t *)((char *)arg1 + 0x0c);
        uint64_t prod_3 =
            (uint64_t)(uint16_t)*(int16_t *)((char *)arg1 + 0x0e) *
            (uint64_t)(uint32_t)*(int32_t *)((char *)arg1 + 0x10);
        uint32_t v1_9 = a2_6 << 2;
        int32_t a2_8 = (int32_t)((a2_6 * 0x81 - v1_9) << 3);
        uint32_t v0_20 = (uint32_t)(prod_3 / (uint64_t)(uint32_t)a2_8);
        uint32_t a1_11 = v0_20 >> 27;
        uint32_t v1_10 = v0_20 << 5;
        uint32_t a3_2 = v0_20 << 7;
        uint32_t v1_11 = a3_2 - v1_10;
        uint32_t a0_12 = v1_11 - v0_20;
        uint32_t v1_12 = v1_11 < a0_12 ? 1U : 0U;
        uint64_t num =
            ((uint64_t)((v1_10 >> 30 | (a1_11 << 2)) - a1_11 - (a3_2 < v1_11 ? 1U : 0U) - v1_12)
             << 32) |
            a0_12;

        return (uint32_t)(num / 100ULL);
    }
}

/* stock name: OiOI */
int16_t *rc_OiOI(void *arg1)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    int16_t *i = (int16_t *)(ctx + 0x70);

    if ((uint32_t)*ctx == 0) {
        int16_t value = *(int16_t *)(ctx + 0x86);

        do {
            *i = value;
            ++i;
        } while ((char *)i != ctx + 0x82);
        return i;
    }

    *(int16_t *)(ctx + 0x48) = 2;
    return (int16_t *)(intptr_t)2;
}

/* stock name: IiOI */
uint16_t rc_IiOI(void *arg1, void *arg2, void *arg3, int32_t arg4)
{
    int32_t t0 = *(int32_t *)((char *)arg2 + 0x10);
    char *t1 = *(char **)((char *)arg1 + 0x30);
    int32_t v0_4 = (*(int32_t *)((char *)arg3 + 0x28) << 2) +
                   (*(int32_t *)((char *)arg3 + 0x2c) << 4) +
                   *(int32_t *)((char *)arg3 + 0x24) +
                   (*(int32_t *)((char *)arg3 + 0x30) << 6);
    uint32_t a0_3;
    uint32_t result_1;
    uint16_t result;

    if (v0_4 == 0) {
        __builtin_trap();
    }
    a0_3 = ((uint32_t)PicStructToFieldNumber[*(uint8_t *)*(char **)((char *)arg2 + 0x14)] *
            (uint32_t)*(int32_t *)(t1 + ((t0 + 0x10) << 2) + 0x0c)) >>
           1;
    result_1 = (uint32_t)*(int32_t *)((char *)arg3 + 0x38) / (uint32_t)v0_4;
    if ((int32_t)a0_3 < arg4) {
        *(int16_t *)(t1 + 0x48) = (int16_t)t0;
        *(int16_t *)(t1 + ((t0 + 0x38) << 1)) = (int16_t)(result_1 + 1);
        return (uint16_t)(result_1 + 1);
    }

    if (arg4 < (int32_t)((a0_3 << 1) / 3)) {
        --result_1;
    }
    result = (uint16_t)result_1;
    *(int16_t *)(t1 + 0x48) = (int16_t)t0;
    *(int16_t *)(t1 + ((t0 + 0x38) << 1)) = (int16_t)result;
    return result;
}

/* stock name: OIOI */
int32_t rc_OIOI(void *arg1, void *arg2, int16_t *arg3)
{
    char *ctx = *(char **)((char *)arg1 + 0x30);
    int32_t v1 = (int32_t)*(int16_t *)(ctx + ((*(int32_t *)((char *)arg2 + 0x10) + 0x38) << 1));
    int32_t result = (int32_t)*(int16_t *)(ctx + 0x84);

    if (result < v1) {
        int32_t a0_1 = (int32_t)*(int16_t *)(ctx + 0x82);
        int16_t result_1 = (int16_t)v1;

        if (v1 < a0_1) {
            result_1 = (int16_t)a0_1;
        }
        result = (int32_t)result_1;
    }
    *arg3 = (int16_t)result;
    return result;
}

/* stock name: oiOI */
int16_t *rc_oiOI(void *arg1, void *arg2, int32_t arg3)
{
    int32_t v0 = *(int32_t *)((char *)arg2 + 8);
    int32_t s1 = *(int32_t *)((char *)arg2 + 4);
    char *s2;
    int32_t s0_1;
    int32_t *sizes;
    int16_t value;

    if ((uint32_t)v0 < (uint32_t)s1) {
        return (int16_t *)(intptr_t)__assert(
            "IIoi -> IIio >= IIoi -> oilo",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_18.c",
            0xc1, "oiOI", &_gp);
    }

    s2 = *(char **)((char *)arg1 + 0x30);
    if ((uint32_t)*s2 == 0) {
        s0_1 = *(int32_t *)((char *)arg2 + 0x14) & ~0x3f;
        rc_l0io(s2 + 8,
                (int32_t)(((uint64_t)(uint32_t)v0 * (uint64_t)(uint32_t)s0_1) / 90000ULL), s1,
                (int32_t)(uint32_t)*(uint16_t *)((char *)arg2 + 0x0e),
                *(int32_t *)((char *)arg2 + 0x0c) * 1000, s0_1, 1, 1);
        *s2 = 0;
        *(int16_t *)(s2 + 0x82) = *(int16_t *)((char *)arg2 + 0x1a);
        *(int16_t *)(s2 + 0x84) = *(int16_t *)((char *)arg2 + 0x1c);
        *(int16_t *)(s2 + 0x86) = *(int16_t *)((char *)arg2 + 0x18);
        sizes = (int32_t *)(s2 + 0x4c);
        for (int32_t i = 0; i != 9; ++i) {
            *sizes++ = (int32_t)GetTargetSize(arg2, (int32_t *)(intptr_t)arg3, i);
        }
        value = *(int16_t *)(s2 + 0x86);
        for (int16_t *i = (int16_t *)(s2 + 0x70); (char *)i != s2 + 0x82; ++i) {
            *i = value;
        }
    } else {
        rc_iiIo(s2 + 8, (int32_t)(uint32_t)*(uint16_t *)((char *)arg2 + 0x0e),
                *(int32_t *)((char *)arg2 + 0x0c) * 1000, *(int32_t *)((char *)arg2 + 0x14) & ~0x3f);
    }

    return (int16_t *)(s2 + 0x82);
}

/* stock name: iiOI */
int32_t *rc_iiOI(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t *arg5)
{
    (void)arg2;
    (void)arg3;
    rc_IIIo((int32_t *)(*(char **)((char *)arg1 + 0x30) + 8), arg4);
    *arg5 = 0;
    return arg5;
}

/* stock name: oIOI */
int32_t rc_oIOI(void *arg1, int32_t *arg2)
{
    int32_t result = rc_OOlo(*(void **)((char *)arg1 + 0x30) + 8);

    *arg2 = result;
    return result;
}

/* stock name: iIOI */
int32_t rc_iIOI(void *arg1, int32_t *arg2)
{
    int32_t result = rc_i1Io(*(void **)((char *)arg1 + 0x30) + 8);

    *arg2 = result;
    return result;
}

/* stock name: IIOI */
int32_t rc_IIOI(void *arg1, int32_t *arg2)
{
    int32_t *s0_1 = (int32_t *)(*(char **)((char *)arg1 + 0x30) + 8);
    int32_t result = rc_l1Io(s0_1) - rc_OOlo(s0_1);

    *arg2 = result;
    return result;
}
