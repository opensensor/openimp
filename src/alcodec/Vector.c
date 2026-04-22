#include <stdint.h>

#include "alcodec/al_rtos.h"

void *SourceVector_Init(void *arg1)
{
    return Rtos_Memset(arg1, 0, 0x130);
}

int32_t SourceVector_Add(void *arg1, int32_t arg2, void *arg3)
{
    uint8_t *a0 = (uint8_t *)arg1 + ((arg2 % 0x26) << 3);

    if (*(uint8_t *)(a0 + 4) != 0) {
        return 0;
    }
    *(uint8_t *)(a0 + 4) = 1;
    *(void **)a0 = arg3;
    return 1;
}

int32_t SourceVector_Size(void *arg1)
{
    uint8_t *i = (uint8_t *)arg1 + 4;
    int32_t result = 0;

    do {
        uint32_t a1_1 = (uint32_t)*i;

        i = i + 8;
        result += 0U < a1_1 ? 1 : 0;
    } while (i != (uint8_t *)arg1 + 0x134);

    return result;
}

int32_t SourceVector_Remove(void *arg1, int32_t arg2)
{
    *(uint8_t *)((uint8_t *)arg1 + ((arg2 % 0x26) << 3) + 4) = 0;
    return arg2 / 0x26 * 0x26;
}

void *SourceVector_Get(void *arg1, int32_t arg2)
{
    return *(void **)((uint8_t *)arg1 + ((arg2 % 0x26) << 3));
}

uint32_t SourceVector_IsIn(void *arg1, int32_t arg2)
{
    return (uint32_t)*(uint8_t *)((uint8_t *)arg1 + ((arg2 % 0x26) << 3) + 4);
}

void IntVector_Init(int32_t *arg1)
{
    *arg1 = 0;
}

int32_t IntVector_Add(int32_t *arg1, int32_t arg2)
{
    int32_t v0_1 = *arg1;

    arg1[v0_1 + 1] = arg2;
    *arg1 = v0_1 + 1;
    return v0_1 + 1;
}

void IntVector_MoveBack(int32_t *arg1, int32_t arg2)
{
    int32_t t0_3 = *arg1;
    int32_t v1_1;

    if (t0_3 <= 0) {
        if (t0_3 == 0) {
            v1_1 = -1;
            goto label_6108c;
        }
        return;
    }
    if (arg2 == arg1[1]) {
        v1_1 = 0;
        goto label_6108c;
    } else {
        int32_t *v0_2 = &arg1[2];

        v1_1 = 0;
        do {
            v1_1 += 1;
            v0_2 = v0_2 + 1;
            if (v1_1 == t0_3) {
                v1_1 = -1;
                break;
            }
        } while (arg2 != *(v0_2 - 1));
    }

label_6108c:
    {
        int32_t *v0 = &arg1[v1_1 + 2];

        do {
            v1_1 += 1;
            *(v0 - 1) = *v0;
            v0 = v0 + 1;
        } while (v1_1 < t0_3);
    }
    arg1[t0_3] = arg2;
}

int32_t IntVector_Remove(int32_t *arg1, int32_t arg2)
{
    IntVector_MoveBack(arg1, arg2);

    {
        int32_t result = *arg1 - 1;

        *arg1 = result;
        return result;
    }
}

int32_t IntVector_IsIn(int32_t *arg1, int32_t arg2)
{
    int32_t a2 = *arg1;

    if (a2 > 0) {
        int32_t *a0 = &arg1[2];

        if (arg2 == arg1[1]) {
            return 1;
        }

        {
            int32_t v0_2 = 0;

            while (1) {
                v0_2 += 1;
                a0 = a0 + 1;
                if (v0_2 == a2) {
                    break;
                }
                if (arg2 == *(a0 - 1)) {
                    return 0U < (uint32_t)(v0_2 + 1) ? 1 : 0;
                }
            }
        }
    }

    return 0;
}

void IntVector_Revert(int32_t *arg1)
{
    int32_t s0 = *arg1;
    int32_t i = s0 - 1;

    if (i < 0) {
        return;
    }

    {
        int32_t *s1_1 = &arg1[s0];

        do {
            i -= 1;
            IntVector_MoveBack(arg1, *s1_1);
            s1_1 = s1_1 - 1;
        } while (i != -1);
    }
}

int32_t IntVector_Copy(int32_t *arg1, int32_t *arg2)
{
    int32_t result = *arg1;
    int32_t a2;
    int32_t *a0;

    *arg2 = result;
    a2 = *arg1;
    a0 = &arg1[1];
    if (a2 > 0) {
        int32_t *a1 = &arg2[1];

        result = 0;
        do {
            int32_t v1_1 = *a0;

            result += 1;
            a0 = a0 + 1;
            *a1 = v1_1;
            a1 = a1 + 1;
        } while (result != a2);
    }

    return result;
}
