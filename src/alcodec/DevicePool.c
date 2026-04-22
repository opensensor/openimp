#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "alcodec/al_allocator.h"
#include "alcodec/al_rtos.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, void *caller);

/* forward decl, ported by T<N> later */
int32_t IMP_Alloc(void *arg1, int32_t arg2, char *arg3);
/* forward decl, ported by T<N> later */
int32_t IMP_Free(void *arg1, int32_t arg2);
/* forward decl, ported by T<N> later */
int32_t IMP_PoolAlloc(int32_t arg1, void *arg2, int32_t arg3, char *arg4);
/* forward decl, ported by T<N> later */
int32_t IMP_PoolFree(int32_t arg1, void *arg2, int32_t arg3);
/* forward decl, ported by T<N> later */
void *IMP_MemPool_GetById(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t IMP_FlushCache(int32_t arg1, int32_t arg2, int32_t arg3);
/* forward decl, implemented in dma_alloc.c */
int DMA_RmemFlushCache(void *virt_addr, uint32_t size, int dir);

typedef struct DevicePoolEntry {
    char *name;
    int32_t refcount;
    int32_t fd;
} DevicePoolEntry;

typedef struct AL_TAllocatorVtableCompat {
    void *reserved0;
    void *(*Alloc)(AL_TAllocator *allocator, int32_t size);
    int32_t (*Free)(AL_TAllocator *allocator, void *buffer);
    intptr_t (*GetVirtualAddr)(AL_TAllocator *allocator, void *buffer);
    intptr_t (*GetPhysicalAddr)(AL_TAllocator *allocator, void *buffer);
    void *(*AllocNamed)(AL_TAllocator *allocator, int32_t size, int32_t name);
    void *(*AllocNamedEmpty)(AL_TAllocator *allocator, int32_t size, char *name);
    int32_t (*FreeEmpty)(AL_TAllocator *allocator, int32_t handle);
    int32_t (*SetExtraMemory)(AL_TAllocator *allocator, void *buffer, int32_t arg3, int32_t arg4, int32_t arg5);
} AL_TAllocatorVtableCompat;

typedef struct PoolAllocatorHolder {
    int32_t pool_id;
    const AL_TAllocatorVtableCompat *vtable;
} PoolAllocatorHolder;

void *AL_sDmaAllocator_Alloc(AL_TAllocator *arg1, int32_t arg2);
int32_t AL_sDmaAllocator_Free(AL_TAllocator *arg1, void *arg2);
intptr_t AL_sDmaAllocator_GetVirtualAddr(AL_TAllocator *arg1, void *arg2);
intptr_t AL_sDmaAllocator_GetPhysicalAddr(AL_TAllocator *arg1, void *arg2);
void *AL_sDmaAllocator_AllocNamed(AL_TAllocator *arg1, int32_t arg2, int32_t arg3);
void *AL_sDmaAllocator_AllocNamedEmpty(AL_TAllocator *arg1, int32_t arg2, char *arg3);
int32_t AL_sDmaAllocator_FreeEmpty(AL_TAllocator *arg1, int32_t arg2);
int32_t AL_sDmaAllocator_SetExtraMemory(AL_TAllocator *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5);
void *AL_Pool_Alloc(AL_TAllocator *arg1, int32_t arg2);
int32_t AL_Pool_Free(AL_TAllocator *arg1, void *arg2);
intptr_t AL_Pool_GetVirtualAddr(AL_TAllocator *arg1, void *arg2);
intptr_t AL_Pool_GetPhysicalAddr(AL_TAllocator *arg1, void *arg2);
void *AL_Pool_AllocNamed(AL_TAllocator *arg1, int32_t arg2, int32_t arg3);
void *AL_Pool_AllocNamedEmpty(AL_TAllocator *arg1, int32_t arg2, char *arg3);
int32_t AL_Pool_FreeEmpty(AL_TAllocator *arg1, int32_t arg2);
int32_t AL_Pool_SetExtraMemory(AL_TAllocator *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5);
static int32_t AL_Pool_Alloc_part_7(void);

static DevicePoolEntry g_DevicePool[0x20];
static uint8_t g_DevicePoolInit;
static void *data_119220;

static const AL_TAllocatorVtableCompat s_poolVtable = {
    NULL,
    AL_Pool_Alloc,
    AL_Pool_Free,
    AL_Pool_GetVirtualAddr,
    AL_Pool_GetPhysicalAddr,
    AL_Pool_AllocNamed,
    AL_Pool_AllocNamedEmpty,
    AL_Pool_FreeEmpty,
    AL_Pool_SetExtraMemory,
};

static const AL_TAllocatorVtableCompat s_DmaAllocatorVtable = {
    NULL,
    AL_sDmaAllocator_Alloc,
    AL_sDmaAllocator_Free,
    AL_sDmaAllocator_GetVirtualAddr,
    AL_sDmaAllocator_GetPhysicalAddr,
    AL_sDmaAllocator_AllocNamed,
    AL_sDmaAllocator_AllocNamedEmpty,
    AL_sDmaAllocator_FreeEmpty,
    AL_sDmaAllocator_SetExtraMemory,
};

static const AL_TAllocatorVtableCompat *s_poolAllocator = &s_poolVtable;
static PoolAllocatorHolder g_dma_default_allocator = {
    -1,
    &s_DmaAllocatorVtable,
};

void AL_DevicePool_Deinit(void)
{
    void *a0 = data_119220;

    if (a0 != NULL) {
        Rtos_DeleteMutex(a0);
    }

    data_119220 = NULL;
}

int32_t AL_DevicePool_Open(char *arg1)
{
    void *a0;
    int32_t s0;
    int32_t *s5;
    int32_t *s2;
    int32_t result;

    if (g_DevicePoolInit == 0) {
        atexit(AL_DevicePool_Deinit);
        a0 = Rtos_CreateMutex();
        data_119220 = a0;
        g_DevicePoolInit = 1;
    }

    a0 = data_119220;
    s0 = 0;
    Rtos_GetMutex(a0);
    s5 = &g_DevicePool[0].refcount;
    s2 = &g_DevicePool[0].refcount;

    while (1) {
        int32_t *v0_5;

        if (*s2 > 0 && strcmp(arg1, *(char **)(s2 - 1)) == 0) {
            v0_5 = (int32_t *)((char *)g_DevicePool + s0 * 0xc);
label_4638c:
            result = v0_5[2];
            v0_5[1] += 1;
            break;
        }

        s0 += 1;
        s2 = &s2[3];
        if (s0 == 0x20) {
            int32_t i = 0;

            while (i != 0x20) {
                int32_t v0_7 = *s5;

                s5 = &s5[3];
                if (v0_7 == 0) {
                    *(char **)((char *)g_DevicePool + i * 0xc) = strdup(arg1);
                    v0_7 = open(arg1, 2);
                    *(int32_t *)((char *)g_DevicePool + i * 0xc + 8) = v0_7;
                    if (v0_7 < 0) {
                        free(*(void **)((char *)g_DevicePool + i * 0xc));
                        goto label_4648c;
                    }

                    v0_5 = (int32_t *)((char *)g_DevicePool + i * 0xc);
                    goto label_4638c;
                }

                i += 1;
            }

label_4648c:
            result = -1;
            break;
        }
    }

    Rtos_ReleaseMutex(data_119220);
    return result;
}

int32_t AL_DevicePool_Close(int32_t arg1)
{
    int32_t v1;
    int32_t *v0;
    int32_t result;

    Rtos_GetMutex(data_119220);
    v1 = 0;
    v0 = &g_DevicePool[0].refcount;

    while (1) {
        int32_t a0_1 = *v0;

        if (a0_1 != 0 && arg1 == v0[1]) {
            if (a0_1 <= 0) {
                int32_t a0_6;
                void *a1;

                a0_6 = __assert("pEntry->iRefCount > 0",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_fpga/DevicePool.c",
                    0x92, "DevicePool_Close", &_gp);
                return (int32_t)AL_Pool_GetVirtualAddr((AL_TAllocator *)(intptr_t)a0_6, a1);
            }

            *(int32_t *)((char *)g_DevicePool + v1 * 0xc + 4) = a0_1 - 1;
            result = 0;
            if (a0_1 == 1) {
                free(*(void **)((char *)g_DevicePool + v1 * 0xc));
                result = close(*(int32_t *)((char *)g_DevicePool + v1 * 0xc + 8));
            }
            break;
        }

        v1 += 1;
        v0 = &v0[3];
        if (v1 == 0x20) {
            result = -1;
            break;
        }
    }

    Rtos_ReleaseMutex(data_119220);
    return result;
}

intptr_t AL_Pool_GetVirtualAddr(AL_TAllocator *arg1, void *arg2)
{
    (void)arg1;

    if (arg2 == NULL) {
        return 0;
    }

    return *(intptr_t *)((char *)arg2 + 0x80);
}

intptr_t AL_Pool_GetPhysicalAddr(AL_TAllocator *arg1, void *arg2)
{
    (void)arg1;

    if (arg2 == NULL) {
        return 0;
    }

    return *(intptr_t *)((char *)arg2 + 0x84);
}

int32_t AL_Pool_FreeEmpty(AL_TAllocator *arg1, int32_t arg2)
{
    (void)arg1;

    if (arg2 == 0) {
        return 1;
    }

    Rtos_Free((void *)(intptr_t)arg2);
    return 1;
}

int32_t AL_sDmaAllocator_Free(AL_TAllocator *arg1, void *arg2)
{
    (void)arg1;

    if (arg2 == NULL) {
        return 1;
    }

    IMP_Free(arg2, *(int32_t *)((char *)arg2 + 0x80));
    Rtos_Free(arg2);
    return 1;
}

void allocator_release(int32_t *arg1)
{
    if (arg1 != NULL && (uint32_t)*arg1 < 0x20) {
        free(arg1);
    }
}

int32_t AL_Pool_Free(AL_TAllocator *arg1, void *arg2)
{
    if (arg2 == NULL) {
        return 1;
    }

    IMP_PoolFree(*((int32_t *)arg1 - 1), arg2, *(int32_t *)((char *)arg2 + 0x80));
    Rtos_Free(arg2);
    return 1;
}

int32_t AL_sDmaAllocator_SetExtraMemory(AL_TAllocator *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    int32_t v1_1;

    (void)arg1;

    if (arg2 == NULL) {
        return 0;
    }

    v1_1 = (uint32_t)arg5 < *(uint32_t *)((char *)arg2 + 0x88) ? 1 : 0;
    *(int32_t *)((char *)arg2 + 0x80) = arg3;
    *(int32_t *)((char *)arg2 + 0x84) = arg4;
    if (v1_1 == 0) {
        return 1;
    }

    return __assert("pAlloc->info.length <= size",
        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_fpga/BoardMips.c",
        0x78, "AL_sDmaAllocator_SetExtraMemory", &_gp);
}

int32_t AL_Pool_SetExtraMemory(AL_TAllocator *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    int32_t v1_1;

    (void)arg1;

    if (arg2 == NULL) {
        return 0;
    }

    v1_1 = (uint32_t)arg5 < *(uint32_t *)((char *)arg2 + 0x88) ? 1 : 0;
    *(int32_t *)((char *)arg2 + 0x80) = arg3;
    *(int32_t *)((char *)arg2 + 0x84) = arg4;
    if (v1_1 == 0) {
        return 1;
    }

    __assert("pAlloc->info.length <= size",
        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_fpga/BoardMips.c",
        0x158, "AL_Pool_SetExtraMemory", &_gp);
    return AL_Pool_Alloc_part_7();
}

static int32_t AL_Pool_Alloc_part_7(void)
{
    fwrite("malloc the IMP_Alloc varible buf failed\n", 1, 0x28, stderr);
    return 0;
}

void *AL_sDmaAllocator_Alloc(AL_TAllocator *arg1, int32_t arg2)
{
    intptr_t result;

    (void)arg1;

    result = (intptr_t)Rtos_Malloc(0x94);
    if (result != 0) {
        Rtos_Memset((void *)result, 0, 0x94);
        if (IMP_Alloc((void *)result, arg2, "vcodec") >= 0) {
            return (void *)result;
        }

        Rtos_Free((void *)result);
        fprintf(stderr, "IMP_Alloc fails to alloc the %d size dma buf\n", arg2);
    }

    AL_Pool_Alloc_part_7();
    return NULL;
}

void *AL_sDmaAllocator_AllocNamed(AL_TAllocator *arg1, int32_t arg2, int32_t arg3)
{
    intptr_t result;

    (void)arg1;

    result = (intptr_t)Rtos_Malloc(0x94);
    if (result != 0) {
        Rtos_Memset((void *)result, 0, 0x94);
        if (IMP_Alloc((void *)result, arg2, (char *)(intptr_t)arg3) >= 0) {
            return (void *)result;
        }

        Rtos_Free((void *)result);
        fprintf(stderr, "IMP_Alloc fails to alloc the %d size dma buf\n", arg2);
    }

    AL_Pool_Alloc_part_7();
    return NULL;
}

void *AL_Pool_AllocNamedEmpty(AL_TAllocator *arg1, int32_t arg2, char *arg3)
{
    intptr_t result;

    (void)arg1;

    result = (intptr_t)Rtos_Malloc(0x94);
    if (result == 0) {
        AL_Pool_Alloc_part_7();
        return NULL;
    }

    Rtos_Memset((void *)result, 0, 0x94);
    Rtos_Memcpy((void *)(result + 0x60), arg3, strlen(arg3) + 1);
    *(int32_t *)(result + 0x88) = arg2;
    return (void *)result;
}

void *AL_sDmaAllocator_AllocNamedEmpty(AL_TAllocator *arg1, int32_t arg2, char *arg3)
{
    return AL_Pool_AllocNamedEmpty(arg1, arg2, arg3);
}

void *AL_Pool_AllocNamed(AL_TAllocator *arg1, int32_t arg2, int32_t arg3)
{
    intptr_t result;

    result = (intptr_t)Rtos_Malloc(0x94);
    if (result != 0) {
        Rtos_Memset((void *)result, 0, 0x94);
        if (IMP_PoolAlloc(*((int32_t *)arg1 - 1), (void *)result, arg2, (char *)(intptr_t)arg3) >= 0) {
            return (void *)result;
        }

        Rtos_Free((void *)result);
        fprintf(stderr, "IMP_Alloc fails to alloc the %d size dma buf\n", arg2);
    }

    AL_Pool_Alloc_part_7();
    return NULL;
}

void *AL_Pool_Alloc(AL_TAllocator *arg1, int32_t arg2)
{
    intptr_t result;

    result = (intptr_t)Rtos_Malloc(0x94);
    if (result != 0) {
        Rtos_Memset((void *)result, 0, 0x94);
        if (IMP_PoolAlloc(*((int32_t *)arg1 - 1), (void *)result, arg2, "vcodec") >= 0) {
            return (void *)result;
        }

        Rtos_Free((void *)result);
        fprintf(stderr, "IMP_Alloc fails to alloc the %d size dma buf\n", arg2);
    }

    AL_Pool_Alloc_part_7();
    return NULL;
}

intptr_t AL_sDmaAllocator_GetPhysicalAddr(AL_TAllocator *arg1, void *arg2)
{
    (void)arg1;

    if (arg2 == NULL) {
        return 0;
    }

    return *(intptr_t *)((char *)arg2 + 0x84);
}

intptr_t AL_sDmaAllocator_GetVirtualAddr(AL_TAllocator *arg1, void *arg2)
{
    (void)arg1;

    if (arg2 == NULL) {
        return 0;
    }

    return *(intptr_t *)((char *)arg2 + 0x80);
}

int32_t AL_sDmaAllocator_FreeEmpty(AL_TAllocator *arg1, int32_t arg2)
{
    (void)arg1;

    if (arg2 == 0) {
        return 1;
    }

    Rtos_Free((void *)(intptr_t)arg2);
    return 1;
}

AL_TAllocator *AL_DmaAlloc_Create(void)
{
    return (AL_TAllocator *)&g_dma_default_allocator.vtable;
}

void AL_DmaAlloc_Destroy(void)
{
}

int32_t AL_DmaAlloc_FlushCache(int32_t arg1, int32_t arg2, int32_t arg3)
{
    int ret;

    if (arg1 != 0 && arg2 > 0) {
        ret = DMA_RmemFlushCache((void *)(intptr_t)arg1, (uint32_t)arg2, arg3);
        if (ret == 0)
            return 0;
    }

    return IMP_FlushCache(arg1, arg2, arg3);
}

AL_TAllocator *AL_DmaAlloc_GetAllocator(int32_t arg1)
{
    if ((uint32_t)arg1 < 0x20) {
        char *v0_2 = (char *)IMP_MemPool_GetById(arg1);

        if (v0_2 != NULL) {
            PoolAllocatorHolder *v1_1;

            /* +0x14: cached allocator wrapper */
            v1_1 = *(PoolAllocatorHolder **)(void *)(v0_2 + 0x14);
            if (v1_1 != NULL) {
                return (AL_TAllocator *)&v1_1->vtable;
            }

            v1_1 = (PoolAllocatorHolder *)malloc(8);
            if (v1_1 != NULL) {
                v1_1->pool_id = arg1;
                v1_1->vtable = s_poolAllocator;
                /* +0x14: cached allocator wrapper */
                *(PoolAllocatorHolder **)(void *)(v0_2 + 0x14) = v1_1;
                /* +0x18: release callback */
                *(void (**)(int32_t *))(void *)(v0_2 + 0x18) = allocator_release;
                return (AL_TAllocator *)&v1_1->vtable;
            }
        }
    }

    return (AL_TAllocator *)&g_dma_default_allocator.vtable;
}

int32_t AllocatorPoolGetPoolId(AL_TAllocator *arg1)
{
    if (arg1 != AL_DmaAlloc_GetAllocator(*((int32_t *)arg1 - 1))) {
        return -1;
    }

    return *((int32_t *)arg1 - 1);
}
