#include <stdint.h>

#include "alcodec/al_allocator.h"
#include "alcodec/al_rtos.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, void *caller);

static intptr_t AL_sDefaultAllocator_GetVirtualAddr(AL_TAllocator *arg1, void *arg2);
static intptr_t AL_sDefaultAllocator_GetPhysicalAddr(AL_TAllocator *arg1, void *arg2);
static void AL_sDefaultAllocator_Free(AL_TAllocator *arg1, void *arg2);
static void *AL_sDefaultAllocator_Alloc(AL_TAllocator *arg1, int32_t arg2);
static intptr_t AL_sWrapperAllocator_GetVirtualAddr(AL_TAllocator *arg1, void *arg2);
static intptr_t AL_sWrapperAllocator_GetPhysicalAddr(AL_TAllocator *arg1, void *arg2);
static void AL_sWrapperAllocator_Free(AL_TAllocator *arg1, void *arg2);
static void *AL_sWrapperAllocator_Alloc(AL_TAllocator *arg1, int32_t arg2);
static void WrapperFree(int32_t arg1, int32_t arg2);

typedef struct AL_TAllocatorVtableCompat {
    void *reserved0;
    void *(*Alloc)(AL_TAllocator *allocator, int32_t size);
    void (*Free)(AL_TAllocator *allocator, void *buffer);
    intptr_t (*GetVirtualAddr)(AL_TAllocator *allocator, void *buffer);
    intptr_t (*GetPhysicalAddr)(AL_TAllocator *allocator, void *buffer);
    void *(*AllocNamed)(AL_TAllocator *allocator, int32_t size, int32_t name);
    void *(*AllocNamedEmpty)(AL_TAllocator *allocator, int32_t size, char *name);
    int32_t (*FreeEmpty)(AL_TAllocator *allocator, int32_t handle);
    int32_t (*SetExtraMemory)(AL_TAllocator *allocator, void *buffer, int32_t a3, int32_t a4, int32_t a5);
} AL_TAllocatorVtableCompat;

static const AL_TAllocatorVtableCompat s_WrapperAllocatorVtable = {
    NULL,
    AL_sWrapperAllocator_Alloc,
    AL_sWrapperAllocator_Free,
    AL_sWrapperAllocator_GetVirtualAddr,
    AL_sWrapperAllocator_GetPhysicalAddr,
    NULL,
    NULL,
    NULL,
    NULL,
};

static const AL_TAllocatorVtableCompat s_DefaultAllocatorVtable = {
    NULL,
    AL_sDefaultAllocator_Alloc,
    AL_sDefaultAllocator_Free,
    AL_sDefaultAllocator_GetVirtualAddr,
    AL_sDefaultAllocator_GetPhysicalAddr,
    NULL,
    NULL,
    NULL,
    NULL,
};

static AL_TAllocator s_DefaultAllocator = {
    (const AL_TAllocatorVtable *)&s_DefaultAllocatorVtable,
};

static AL_TAllocator s_WrapperAllocator = {
    (const AL_TAllocatorVtable *)&s_WrapperAllocatorVtable,
};

static uint32_t AL_CLEAN_BUFFERS = 0;

uint32_t AL_CleanupMemory(int32_t arg1, int32_t arg2)
{
    uint32_t result;

    result = AL_CLEAN_BUFFERS;
    if (result != 0) {
        return (uint32_t)(intptr_t)Rtos_Memset((void *)(intptr_t)arg1, 0, (size_t)arg2);
    }
    return result;
}

static intptr_t AL_sDefaultAllocator_GetVirtualAddr(AL_TAllocator *arg1, void *arg2)
{
    (void)arg1;

    return (intptr_t)arg2;
}

static intptr_t AL_sDefaultAllocator_GetPhysicalAddr(AL_TAllocator *arg1, void *arg2)
{
    (void)arg1;
    (void)arg2;

    return 0x20;
}

static intptr_t AL_sWrapperAllocator_GetVirtualAddr(AL_TAllocator *arg1, void *arg2)
{
    (void)arg1;

    return (intptr_t)*(int32_t *)arg2;
}

static intptr_t AL_sWrapperAllocator_GetPhysicalAddr(AL_TAllocator *arg1, void *arg2)
{
    int32_t a0;
    int32_t *a1;

    a0 = __assert("0",
        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/AllocatorDefault.c",
        0x69, "AL_sWrapperAllocator_GetPhysicalAddr", &_gp);
    a1 = (int32_t *)arg2;
    return (intptr_t)(uint32_t)(AL_sWrapperAllocator_Free((AL_TAllocator *)(intptr_t)a0, a1), 1U);
}

static void AL_sWrapperAllocator_Free(AL_TAllocator *arg1, void *arg2)
{
    int32_t t9;

    (void)arg1;

    t9 = ((int32_t *)arg2)[2];
    if (t9 != 0) {
        ((void (*)(int32_t, int32_t))t9)(((int32_t *)arg2)[1], *(int32_t *)arg2);
    }
    Rtos_Free(arg2);
}

static void WrapperFree(int32_t arg1, int32_t arg2)
{
    (void)arg1;

    Rtos_Free((void *)(intptr_t)arg2);
}

static void AL_sDefaultAllocator_Free(AL_TAllocator *arg1, void *arg2)
{
    (void)arg1;

    Rtos_Free(arg2);
}

static void *AL_sWrapperAllocator_Alloc(AL_TAllocator *arg1, int32_t arg2)
{
    int32_t v0;

    (void)arg1;

    v0 = (int32_t)(intptr_t)Rtos_Malloc((size_t)arg2);
    if (v0 != 0) {
        int32_t *result;

        result = (int32_t *)Rtos_Malloc(0xc);
        if (result != 0) {
            *result = v0;
            result[1] = 0;
            result[2] = (int32_t)(intptr_t)WrapperFree;
            return result;
        }
    }
    return 0;
}

static void *AL_sDefaultAllocator_Alloc(AL_TAllocator *arg1, int32_t arg2)
{
    (void)arg1;

    return Rtos_Malloc((size_t)arg2);
}

AL_TAllocator *AL_GetWrapperAllocator(void)
{
    return &s_WrapperAllocator;
}

AL_TAllocator *AL_GetDefaultAllocator(void)
{
    return &s_DefaultAllocator;
}

void *AL_WrapperAllocator_WrapData(int32_t arg1, int32_t arg2, int32_t arg3)
{
    int32_t result;

    AL_GetWrapperAllocator();
    result = (int32_t)(intptr_t)Rtos_Malloc(0xc);
    if (result != 0) {
        *(int32_t *)(intptr_t)result = arg1;
        *(int32_t *)(intptr_t)(result + 4) = arg3;
        *(int32_t *)(intptr_t)(result + 8) = arg2;
    }
    return (void *)(intptr_t)result;
}
