#include <stdint.h>
#include <string.h>

#include "alcodec/al_allocator.h"
#include "alcodec/al_rtos.h"

typedef struct AL_TAllocatorVtableCompat {
    void *reserved0;
    void *(*Alloc)(AL_TAllocator *allocator, int32_t size);
    void (*Free)(AL_TAllocator *allocator, void *buffer);
    intptr_t (*GetVirtualAddr)(AL_TAllocator *allocator, void *buffer);
    intptr_t (*GetPhysicalAddr)(AL_TAllocator *allocator, void *buffer);
    void *(*AllocNamed)(AL_TAllocator *allocator, int32_t size, char *name);
    void *reserved6;
    void *reserved7;
    void *reserved8;
} AL_TAllocatorVtableCompat;

typedef struct AL_TMemDesc {
    intptr_t pBufVAddr;
    intptr_t uPhysicalAddr;
    int32_t zSize;
    AL_TAllocator *pAllocator;
    void *pBuf;
} AL_TMemDesc;

int32_t AL_CoreConstraint_Init(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6)
{
    int32_t result;

    *arg1 = arg5;
    arg1[1] = arg6;
    *((uint8_t *)arg1 + 0xc) = 1;
    result = arg2 / 0x64;
    if (arg4 == 0) {
        __builtin_trap();
    }
    arg1[2] = (arg2 - result * arg3) / arg4;
    return result;
}

uint32_t AL_CoreConstraint_GetMinCoresCount(void *arg1, int32_t arg2)
{
    int32_t a2;
    int32_t a3;
    int32_t v1;
    int32_t a0;
    uint64_t dividend;
    uint64_t divisor;

    a2 = *(int32_t *)((char *)arg1 + 4);
    a3 = a2 >> 0x1f;
    v1 = (a2 - 1U < (uint32_t)a2) ? 1 : 0;
    a0 = a2 - 1 + arg2;
    dividend = ((uint64_t)(uint32_t)((a0 < a2 - 1) ? 1 : 0) + (uint64_t)(uint32_t)v1 +
        (uint64_t)(uint32_t)a3 - 1ULL + (uint64_t)(uint32_t)(arg2 >> 0x1f))
        << 32 | (uint32_t)a0;
    divisor = ((uint64_t)(uint32_t)a3 << 32) | (uint32_t)a2;
    return (uint32_t)(dividend / divisor);
}

uint32_t AL_GetResources(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    int32_t v0_3;
    int32_t a3_1;
    int32_t v1_3;
    uint64_t dividend;
    uint64_t divisor;
    int64_t product;

    if (arg4 == 0) {
        return 0;
    }

    v0_3 = ((((int32_t)(((arg1 + 0x1fU) < (uint32_t)arg1) ? 1 : 0) + (arg1 >> 0x1f)) << 0x1b) |
        ((uint32_t)(arg1 + 0x1f) >> 5)) *
        ((((int32_t)(((arg2 + 0x1fU) < (uint32_t)arg2) ? 1 : 0) + (arg2 >> 0x1f)) << 0x1b) |
            ((uint32_t)(arg2 + 0x1f) >> 5));
    a3_1 = arg4 >> 0x1f;
    v1_3 = (arg4 - 1U < (uint32_t)arg4) ? 1 : 0;
    product = (int64_t)v0_3 * (int64_t)arg3;
    dividend = (uint64_t)product +
        ((((uint64_t)(uint32_t)v1_3 + (uint64_t)(uint32_t)a3_1 - 1ULL) << 32) | (uint32_t)(arg4 - 1));
    divisor = ((uint64_t)(uint32_t)a3_1 << 32) | (uint32_t)arg4;
    return (uint32_t)(dividend / divisor);
}

uint32_t AL_CoreConstraint_GetExpectedNumberOfCores(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    int32_t s2;
    int32_t s1;
    int32_t v0_1;
    int32_t a3_1;
    int32_t a0_1;
    int32_t v1_2;
    uint32_t v0_2;
    int32_t s2_1;
    int32_t a0_2;
    int32_t v1_5;
    uint32_t v0_3;
    uint64_t dividend;
    uint64_t divisor;

    s2 = *(int32_t *)((char *)arg1 + 4);
    s1 = *(int32_t *)((char *)arg1 + 8);
    v0_1 = (int32_t)AL_GetResources(arg2, arg3, arg4, arg5);
    a3_1 = s2 >> 0x1f;
    a0_1 = s2 - 1 + arg2;
    v1_2 = arg2 >> 0x1f;
    dividend = ((uint64_t)(uint32_t)((a0_1 < s2 - 1) ? 1 : 0) +
        (uint64_t)(uint32_t)((s2 - 1U < (uint32_t)s2) ? 1 : 0) +
        (uint64_t)(uint32_t)a3_1 - 1ULL + (uint64_t)(uint32_t)v1_2) << 32 | (uint32_t)a0_1;
    divisor = ((uint64_t)(uint32_t)a3_1 << 32) | (uint32_t)s2;
    v0_2 = (uint32_t)(dividend / divisor);
    s2_1 = s1 >> 0x1f;
    a0_2 = s1 - 1 + v0_1;
    v1_5 = ((s1 - 1U < (uint32_t)s1) ? 1 : 0) + s2_1 - 1 + (v0_1 >> 0x1f);
    dividend = ((uint64_t)(uint32_t)((a0_2 < s1 - 1) ? 1 : 0) + (uint64_t)(uint32_t)v1_5) << 32 | (uint32_t)a0_2;
    divisor = ((uint64_t)(uint32_t)s2_1 << 32) | (uint32_t)s1;
    v0_3 = (uint32_t)(dividend / divisor);
    if ((int32_t)v0_2 >= (int32_t)v0_3) {
        return v0_2;
    }
    return v0_3;
}

int32_t AL_Constraint_NumCoreIsSane(int32_t arg1, int32_t arg2, int32_t arg3, int32_t *arg4)
{
    int32_t lo;
    int32_t s2;
    int32_t t1;
    int32_t v1_1;
    int32_t a0_1;
    uint32_t v0_4;
    int32_t v1_2;
    int32_t s3_1;
    uint64_t dividend;
    uint64_t divisor;

    lo = arg1 / arg2;
    if (arg2 == 0) {
        __builtin_trap();
    }

    s2 = 1 << (arg3 & 0x1f);
    t1 = s2 >> 0x1f;
    v1_1 = (lo - 1U < (uint32_t)lo) ? 1 : 0;
    a0_1 = lo - 1 + s2;
    dividend = ((uint64_t)(uint32_t)((a0_1 < lo - 1) ? 1 : 0) + (uint64_t)(uint32_t)v1_1 +
        (uint64_t)(uint32_t)(lo >> 0x1f) - 1ULL + (uint64_t)(uint32_t)t1) << 32 | (uint32_t)a0_1;
    divisor = ((uint64_t)(uint32_t)t1 << 32) | (uint32_t)s2;
    v0_4 = (uint32_t)(dividend / divisor);

    if (arg4 == 0) {
        v1_2 = 0;
        if (arg2 > 0) {
            goto label_47d18;
        }
        s3_1 = (int32_t)v0_4 < 9 - arg3 ? 1 : 0;
    } else {
        Rtos_Memset(arg4, 0, 8);
        v1_2 = 0;
        if (arg2 > 0) {
            goto label_47d18;
        }
label_47d64:
        *arg4 = 9 - arg3;
        arg4[1] = (int32_t)v0_4;
        s3_1 = (int32_t)v0_4 < 9 - arg3 ? 1 : 0;
    }

    if (s3_1 != 0) {
        return 0;
    }
    if (arg1 < 0) {
        if (s2 == 0) {
            __builtin_trap();
        }
        return (((arg1 / s2) * s2) < v1_2 ? 1 : 0) ^ 1;
    }
    if (s2 == 0) {
        __builtin_trap();
    }
    return ((((s2 + arg1 - 1) / s2) * s2) < v1_2 ? 1 : 0) ^ 1;

label_47d18:
    {
        int32_t a0_3;
        int32_t v0_5;

        a0_3 = 0;
        v0_5 = 0;
        while (1) {
            v1_2 = s2 * (9 - arg3) + v0_5;
            if (v1_2 >= 0) {
                a0_3 += 1;
                v0_5 = ((v1_2 + 0x3f) >> 6) << 6;
                if (a0_3 >= arg2) {
                    break;
                }
            } else {
                a0_3 += 1;
                v0_5 = ((v1_2 + 0x3f) >> 6) << 6;
                if (a0_3 >= arg2) {
                    break;
                }
            }
        }
    }

    if (arg4 != 0) {
        goto label_47d64;
    }
    s3_1 = (int32_t)v0_4 < 9 - arg3 ? 1 : 0;
    if (s3_1 != 0) {
        return 0;
    }
    if (arg1 < 0) {
        if (s2 == 0) {
            __builtin_trap();
        }
        return (((arg1 / s2) * s2) < v1_2 ? 1 : 0) ^ 1;
    }
    if (s2 == 0) {
        __builtin_trap();
    }
    return ((((s2 + arg1 - 1) / s2) * s2) < v1_2 ? 1 : 0) ^ 1;
}

void MemDesc_Init(AL_TMemDesc *arg1)
{
    if (arg1 != 0) {
        memset(arg1, 0, 0x14);
    }
}

int32_t MemDesc_AllocNamed(AL_TMemDesc *arg1, AL_TAllocator *arg2, int32_t arg3, char *arg4)
{
    const AL_TAllocatorVtableCompat *vtable;
    void *s3;

    if (arg1 == 0 || arg2 == 0) {
        return 0;
    }

    vtable = (const AL_TAllocatorVtableCompat *)arg2->vtable;
    if (vtable->AllocNamed == 0) {
        s3 = vtable->Alloc(arg2, arg3);
    } else {
        s3 = vtable->AllocNamed(arg2, arg3, arg4);
    }

    if (s3 == 0) {
        return 0;
    }

    arg1->pAllocator = arg2;
    arg1->pBuf = s3;
    arg1->zSize = arg3;
    arg1->pBufVAddr = vtable->GetVirtualAddr(arg2, s3);
    arg1->uPhysicalAddr = vtable->GetPhysicalAddr(arg2, s3);
    return 1;
}

int32_t MemDesc_Alloc(AL_TMemDesc *arg1, AL_TAllocator *arg2, int32_t arg3)
{
    return MemDesc_AllocNamed(arg1, arg2, arg3, "unknown");
}

int32_t MemDesc_Free(AL_TMemDesc *arg1)
{
    AL_TAllocator *v0_1;
    void *a1_1;

    if (arg1 != 0) {
        v0_1 = *(AL_TAllocator **)((char *)arg1 + 0xc);
        if (v0_1 != 0) {
            a1_1 = *(void **)((char *)arg1 + 0x10);
            if (a1_1 != 0) {
                ((const AL_TAllocatorVtableCompat *)v0_1->vtable)->Free(v0_1, a1_1);
                MemDesc_Init(arg1);
                return 1;
            }
        }
    }
    return 0;
}
