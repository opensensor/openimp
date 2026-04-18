#include <stdint.h>

#include "alcodec/al_allocator.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, void *caller);
extern uint32_t AL_CleanupMemory(int32_t arg1, int32_t arg2);

static const uint8_t tab_ceil_log2[32] = {
    0x00, 0x00, 0x01, 0x02, 0x02, 0x03, 0x03, 0x03,
    0x03, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
};

int32_t floor_log2(int32_t arg1);

uint32_t ceil_log2(int32_t arg1)
{
    uint32_t n = (uint32_t)arg1;

    if (n == 0) {
        return (uint32_t)floor_log2(__assert(
            "n > 0",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/Utils.c",
            0x1f,
            "ceil_log2",
            &_gp));
    }

    if (n < 0x20U) {
        return (uint32_t)tab_ceil_log2[n];
    }

    {
        uint32_t i = n - 1U;
        int32_t result = 0;

        do {
            i >>= 1;
            result += 1;
        } while (i != 0U);

        return (uint32_t)result;
    }
}

int32_t floor_log2(int32_t arg1)
{
    uint32_t i = (uint32_t)arg1;
    int32_t result = -1;

    while (i != 0U) {
        i >>= 1;
        result += 1;
    }

    return result;
}

void *AlignedAlloc(AL_TAllocator *arg1, char *arg2, int32_t arg3, int32_t arg4, int32_t *arg5, int32_t *arg6)
{
    const void *vtable = *(const void **)arg1;
    void *result;
    int32_t total_size = arg3 + arg4;

    *arg5 = 0;
    *arg6 = 0;

    /* vtable + 0x14 */
    if (*(const void * const *)((const char *)vtable + 0x14) == 0) {
        /* vtable + 0x04 */
        result = ((void *(*)(AL_TAllocator *, int32_t))*(const void * const *)((const char *)vtable + 0x04))(arg1, total_size);
    } else {
        result = ((void *(*)(AL_TAllocator *, int32_t, char *))*(const void * const *)((const char *)vtable + 0x14))(arg1, total_size, arg2);
    }

    if (result != 0) {
        uint32_t vaddr;

        *arg5 = total_size;
        vtable = *(const void **)arg1;

        /* vtable + 0x10 */
        vaddr = (uint32_t)((intptr_t (*)(AL_TAllocator *, void *))*(const void * const *)((const char *)vtable + 0x10))(arg1, result);

        if (arg4 == 0) {
            __builtin_trap();
        }

        *arg6 = (int32_t)((((uint32_t)(arg4 - 1) + vaddr) / (uint32_t)arg4) * (uint32_t)arg4 - vaddr);
    }

    return result;
}

int32_t AL_GetNumLinesInPitch(int32_t arg1)
{
    if (arg1 == 0) {
        return 1;
    }

    if ((uint32_t)(arg1 - 2) >= 2U) {
        __assert(
            "0",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/BufCommon.c",
            0x15,
            "AL_GetNumLinesInPitch",
            &_gp);
    }

    return 4;
}

int32_t ComputeRndPitch(int32_t arg1, uint16_t arg2, int32_t arg3, int32_t arg4)
{
    uint32_t a1 = (uint32_t)arg2;
    int32_t v1_1;
    int32_t s0_2;

    if (arg3 == 2) {
        v1_1 = 0x20;
        goto label_470b0;
    }

    if (arg3 == 3) {
        v1_1 = 0x40;

        if (arg1 >= 0) {
            goto label_470c4;
        }

        goto label_47104;
    }

    if (arg3 == 0) {
        goto label_470ac;
    }

    while (1) {
        __assert(
            "0",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/BufCommon.c",
            0,
            "ComputeRndPitch",
            &_gp);
    }

label_470ac:
    v1_1 = 1;

label_470b0:
    if (arg1 < 0) {
label_47104:
        if (v1_1 == 0) {
            __builtin_trap();
        }

        s0_2 = arg1 / v1_1 * v1_1;

        if (arg3 != 0) {
            goto label_470e0;
        }
    } else {
label_470c4:
        if (v1_1 == 0) {
            __builtin_trap();
        }

        s0_2 = (arg1 + v1_1 - 1) / v1_1 * v1_1;

        if (arg3 != 0) {
            goto label_470e0;
        }

        if (a1 != 8U) {
            s0_2 <<= 1;
        }
    }

    goto label_47120;

label_470e0:
    {
        int32_t a1_1 = (a1 < 9U) ? 1 : 0;

        if ((uint32_t)(arg3 - 2) >= 2U) {
            while (1) {
                __assert(
                    "0",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/BufCommon.c",
                    0,
                    "ComputeRndPitch",
                    &_gp);
            }
        }

        {
            int32_t s1_1 = 0xa;
            int32_t a0_3;
            int32_t s0_4;

            if (a1_1 != 0) {
                s1_1 = 8;
            }

            a0_3 = AL_GetNumLinesInPitch(arg3) * s0_2 * s1_1;
            s0_4 = a0_3 + 7;

            if (a0_3 >= 0) {
                s0_4 = a0_3;
            }

            s0_2 = s0_4 >> 3;
        }
    }

label_47120:
    if (arg4 <= 0 || (arg4 & 0xf) != 0) {
        return (int32_t)AL_CleanupMemory(
            __assert(
                "iBurstAlignment > 0 && (iBurstAlignment % 16) == 0",
                "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/BufCommon.c",
                0x47,
                "ComputeRndPitch",
                &_gp),
            arg4);
    }

    if (s0_2 < 0) {
        if (arg4 == 0) {
            __builtin_trap();
        }

        return s0_2 / arg4 * arg4;
    }

    if (arg4 == 0) {
        __builtin_trap();
    }

    return (s0_2 + arg4 - 1) / arg4 * arg4;
}
