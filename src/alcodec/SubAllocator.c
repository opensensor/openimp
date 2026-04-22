#include <stddef.h>
#include <stdint.h>

#include "alcodec/al_allocator.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, void *caller);

typedef struct SubAllocator {
    const void *vtable;
    int32_t *pBuf;
    int32_t pAddr;
    int32_t zBaseSize;
    int32_t zFirstFreeChunk;
    int32_t zMaxFreeChunk;
} SubAllocator;

typedef struct SubAllocatorVtable {
    void *reserved0;
    void *(*Alloc)(AL_TAllocator *allocator, int32_t size);
    void (*Free)(AL_TAllocator *allocator, void *buffer);
    intptr_t (*GetVirtualAddr)(AL_TAllocator *allocator, void *buffer);
    intptr_t (*GetPhysicalAddr)(AL_TAllocator *allocator, void *buffer);
    void *AllocNamed;
    void *AllocNamedEmpty;
    void *FreeEmpty;
    void *SetExtraMemory;
} SubAllocatorVtable;

static int32_t SubAllocator_RemoveFreeChunk(SubAllocator *arg1, int32_t arg2);
static int32_t SubAllocator_InsertFreeChunk(SubAllocator *arg1, int32_t arg2, int32_t arg3);
void *SubAllocator_Alloc(AL_TAllocator *arg1, int32_t arg2);
int32_t SubAllocator_Free(AL_TAllocator *arg1, void *arg2);
intptr_t SubAllocator_GetVirtualAddr(AL_TAllocator *arg1, void *arg2);
intptr_t SubAllocator_GetPhysicalAddr(AL_TAllocator *arg1, void *arg2);
int32_t SubAllocator_Init(void **arg1, int32_t *arg2, int32_t arg3, int32_t arg4, int32_t arg5);
int32_t SubAllocator_Deinit(SubAllocator *arg1);

static const SubAllocatorVtable vtable_2078 = {
    NULL,
    SubAllocator_Alloc,
    (void (*)(AL_TAllocator *, void *))SubAllocator_Free,
    SubAllocator_GetVirtualAddr,
    SubAllocator_GetPhysicalAddr,
    NULL,
    NULL,
    NULL,
    NULL,
};

static int32_t SubAllocator_RemoveFreeChunk(SubAllocator *arg1, int32_t arg2)
{
    int32_t *v1_1 = arg1->pBuf;
    int32_t a1_1 = (arg2 + 2) << 2;
    int32_t *a2 = (int32_t *)((char *)v1_1 + a1_1);
    int32_t v0 = *a2;
    int32_t *a1_3 = (int32_t *)((char *)v1_1 + a1_1 - 4);

    if (v0 == -1) {
        int32_t a3_1 = *a1_3;

        if (a3_1 == v0) {
            arg1->zMaxFreeChunk = 0;
        } else {
            arg1->zMaxFreeChunk = *(int32_t *)((char *)v1_1 + (a3_1 << 2)) & 0x7fffffff;
        }
    } else {
        *(int32_t *)((char *)v1_1 + ((v0 + 1) << 2)) = *a1_3;
    }

    {
        int32_t v0_4 = *a1_3;

        if (v0_4 == -1) {
            int32_t v0_7 = *a2;

            arg1->zFirstFreeChunk = v0_7;
            return v0_7;
        }

        {
            int32_t v0_6 = (v0_4 + 2) << 2;

            *(int32_t *)((char *)v1_1 + v0_6) = *a2;
            return v0_6;
        }
    }
}

intptr_t SubAllocator_GetVirtualAddr(AL_TAllocator *arg1, void *arg2)
{
    return (intptr_t)(((SubAllocator *)arg1)->pBuf + (((int32_t)(intptr_t)arg2 - 3) << 2) / 4);
}

intptr_t SubAllocator_GetPhysicalAddr(AL_TAllocator *arg1, void *arg2)
{
    return (intptr_t)((((int32_t)(intptr_t)arg2 - 3) << 2) + ((SubAllocator *)arg1)->pAddr);
}

static int32_t SubAllocator_InsertFreeChunk(SubAllocator *arg1, int32_t arg2, int32_t arg3)
{
    int32_t *a3_1 = arg1->pBuf;
    int32_t *t2_1 = (int32_t *)((char *)a3_1 + (arg2 << 2));

    *t2_1 = arg3 | 0x80000000;

    *(int32_t *)((char *)a3_1 + ((arg2 + arg3 + 1) << 2)) = arg2;

    {
        int32_t t0 = arg1->zFirstFreeChunk;

        if (t0 != -1) {
            int32_t *v0_6;

            while (1) {
                int32_t v0_4 = t0 << 2;

                v0_6 = (int32_t *)((char *)a3_1 + v0_4 + 8);
                if ((uint32_t)arg3 < ((uint32_t)(*(int32_t *)((char *)a3_1 + v0_4)) & 0x7fffffffU)) {
                    int32_t v1_6;
                    int32_t v1_7;

                    v0_6 = (int32_t *)((char *)a3_1 + ((t0 + 1) << 2));
                    v1_6 = *v0_6;
                    t2_1[2] = t0;
                    t2_1[1] = v1_6;
                    v1_7 = *v0_6;
                    if (v1_7 == -1) {
                        arg1->zFirstFreeChunk = arg2;
                    } else {
                        *(int32_t *)((char *)a3_1 + ((v1_7 + 2) << 2)) = arg2;
                    }
                    break;
                }

                {
                    int32_t v1_1 = *v0_6;

                    if (v1_1 == -1) {
                        t2_1[1] = t0;
                        t2_1[2] = -1;
                        break;
                    }

                    t0 = v1_1;
                }
            }

            *v0_6 = arg2;
        } else {
            arg1->zFirstFreeChunk = arg2;
            t2_1[1] = t0;
            t2_1[2] = t0;
        }
    }

    {
        int32_t result = ((uint32_t)arg1->zMaxFreeChunk < (uint32_t)arg3) ? 1 : 0;

        if (result != 0) {
            arg1->zMaxFreeChunk = arg3;
        }

        return result;
    }
}

void *SubAllocator_Alloc(AL_TAllocator *arg1, int32_t arg2)
{
    if (arg1 != NULL && arg2 != 0) {
        uint32_t t5_1 = (uint32_t)(arg2 + 3) >> 2;

        if (t5_1 < 2U) {
            t5_1 = 2;
        }

        if ((uint32_t)((SubAllocator *)arg1)->zMaxFreeChunk >= t5_1) {
            int32_t t3 = ((SubAllocator *)arg1)->zFirstFreeChunk;
            int32_t *t6 = ((SubAllocator *)arg1)->pBuf;
            uint32_t v0_3;
            uint32_t t0_1;
            uint32_t *t4_2;

            while (1) {
                t4_2 = (uint32_t *)((char *)t6 + (t3 << 2));
                v0_3 = *t4_2;
                t0_1 = v0_3 & 0x7fffffff;
                if (t0_1 >= t5_1) {
                    break;
                }

                t3 = *(int32_t *)((char *)t6 + ((t3 + 2) << 2));
            }

            if ((int32_t)v0_3 >= 0) {
                __assert(
                    "pBuf[zCur] & ((size_t)((uintptr_t)(1) << (8 * sizeof(size_t) - 1)))",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/SubAllocator.c",
                    0x6f,
                    "SubAllocator_Alloc",
                    &_gp);
                return NULL;
            }

            {
                uint32_t t2 = t0_1 - t5_1;
                int32_t v1_2;

                if (t2 < 4U) {
                    t5_1 = t0_1;
                    SubAllocator_RemoveFreeChunk((SubAllocator *)arg1, t3);
                    v1_2 = (int32_t)(t5_1 + (uint32_t)t3);
                } else {
                    SubAllocator_RemoveFreeChunk((SubAllocator *)arg1, t3);
                    SubAllocator_InsertFreeChunk((SubAllocator *)arg1, (int32_t)(t5_1 + 2U + (uint32_t)t3), (int32_t)(t2 - 2U));
                    v1_2 = (int32_t)(t5_1 + (uint32_t)t3);
                }

                *t4_2 = t5_1;
                *(int32_t *)((char *)t6 + ((v1_2 + 1) << 2)) = t3;
                return (void *)(intptr_t)(t3 + 4);
            }
        }
    }

    return NULL;
}

int32_t SubAllocator_Free(AL_TAllocator *arg1, void *arg2)
{
    if (arg1 != NULL) {
        int32_t t2_1;

        if (arg2 != NULL) {
            t2_1 = (int32_t)(intptr_t)arg2 - 4;

            {
                int32_t v0 = ((SubAllocator *)arg1)->zBaseSize;

                if ((uint32_t)v0 < (uint32_t)t2_1) {
                    __assert(
                        "(size_t)hBuf <= pSubAllocator->zBaseSize",
                        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/SubAllocator.c",
                        0x9b,
                        "SubAllocator_Free",
                        &_gp);
                } else {
                    int32_t *t3_1 = ((SubAllocator *)arg1)->pBuf;
                    int32_t *t0_2 = (int32_t *)((char *)t3_1 + (t2_1 << 2));
                    int32_t a2_1 = *t0_2;
                    uint32_t v0_1 = (uint32_t)v0 >> 2;

                    if (a2_1 < 0) {
                        __assert(
                            "(pBuf[zCur] & ((size_t)((uintptr_t)(1) << (8 * sizeof(size_t) - 1)))) == 0",
                            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/SubAllocator.c",
                            0xa4,
                            "SubAllocator_Free",
                            &_gp);
                    } else {
                        int32_t a1_1 = (int32_t)(intptr_t)arg2 - 2 + a2_1;

                        if (v0_1 < (uint32_t)a1_1) {
                            __assert(
                                "zNext <= zMaxSize",
                                "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/SubAllocator.c",
                                0xae,
                                "SubAllocator_Free",
                                &_gp);
                        } else {
                            if ((uint32_t)a1_1 < v0_1) {
                                int32_t v0_10 = *(int32_t *)((char *)t3_1 + (a1_1 << 2));

                                if (v0_10 < 0) {
                                    SubAllocator_RemoveFreeChunk((SubAllocator *)arg1, a1_1);
                                    a2_1 = (v0_10 & 0x7fffffff) + a2_1 + 2;
                                }
                            }

                            if (t2_1 == 0) {
                                goto label_377ec;
                            }

                            {
                                int32_t t0_3 = *((int32_t *)((char *)t0_2 - 4));
                                int32_t v1_2 = *(int32_t *)((char *)t3_1 + (t0_3 << 2));
                                int32_t a3_1 = t2_1 - t0_3;

                                if (a3_1 == ((v1_2 & 0x7fffffff) + 2)) {
                                    if ((uint32_t)t0_3 >= (uint32_t)t2_1) {
                                        goto label_378f8;
                                    }

                                    if (v1_2 < 0) {
                                        SubAllocator_RemoveFreeChunk((SubAllocator *)arg1, t0_3);
                                        t2_1 = t0_3;
                                        a2_1 += a3_1;
                                    }

label_377ec:
                                    SubAllocator_InsertFreeChunk((SubAllocator *)arg1, t2_1, a2_1);
                                    return 1;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;

label_378f8:
    __assert(
        "(zCur - zPrev) == (pBuf[zPrev] & ~((size_t)((uintptr_t)(1) << (8 * sizeof(size_t) - 1)))) + 2",
        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/SubAllocator.c",
        0xbf,
        "SubAllocator_Free",
        &_gp);
    return 0;
}

int32_t SubAllocator_Init(void **arg1, int32_t *arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    if (arg4 == 0) {
        __builtin_trap();
    }

    if ((uint32_t)arg3 % (uint32_t)arg4 != 0 || arg1 == NULL || arg2 == NULL) {
        return 0;
    }

    {
        uint32_t v0_1 = (uint32_t)arg5 >> 2;

        if (arg5 == 0 || (int32_t)(v0_1 - 2) <= 0) {
            return 0;
        }

        if (((uintptr_t)arg2 & 3U) != 0U) {
            __assert(
                "((size_t)VirtualAddr) % sizeof(size_t) == 0",
                "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/SubAllocator.c",
                0xf0,
                "SubAllocator_Init",
                &_gp);
            return 0;
        }

        arg1[3] = (void *)(intptr_t)arg5;
        arg1[1] = arg2;
        arg1[2] = (void *)(intptr_t)arg3;
        arg1[4] = 0;
        arg1[5] = (void *)(intptr_t)(v0_1 - 2);
        *arg2 = (int32_t)((v0_1 - 2) | 0x80000000U);
        arg2[1] = -1;
        arg2[2] = -1;
        arg2[v0_1 - 1] = 0;
        *arg1 = (void *)&vtable_2078;
        return 1;
    }
}

int32_t SubAllocator_Deinit(SubAllocator *arg1)
{
    if (arg1 == NULL) {
        return 0;
    }

    return ((((uint32_t)arg1->zBaseSize >> 2) - 2U) ^ (uint32_t)arg1->zMaxFreeChunk) < 1U ? 1 : 0;
}
