#include <errno.h>
#include <stdint.h>

#include "alcodec/al_rtos.h"
#include "alcodec/al_utils.h"

/* forward decl, ported by T4 later */
int32_t AL_AVC_GetMaxNumberOfSlices(void);
/* forward decl, ported by T4 later */
int32_t AL_HEVC_GetMaxNumberOfSlices(void);

static const uint8_t g_NalSizeTable[4][6] = {
    { 4, 0, 0, 0, 0, 0 },
    { 6, 0, 0, 0, 0, 0 },
    { 8, 0, 0, 0, 0, 0 },
    { 12, 0, 0, 0, 0, 0 },
};

static int32_t PostMessage(int32_t arg1, int32_t arg2, int32_t arg3, int32_t *arg4);
static int32_t Close(int32_t arg1, int32_t arg2);
static int32_t Open(int32_t arg1, int32_t arg2);

struct AL_THardwareDriverVtable {
    int32_t (*Open)(int32_t arg1, int32_t arg2);
    int32_t (*Close)(int32_t arg1, int32_t arg2);
    int32_t (*PostMessage)(int32_t arg1, int32_t arg2, int32_t arg3, int32_t *arg4);
};

static const AL_THardwareDriverVtable hardwareDriverVtable = {
    Open,
    Close,
    PostMessage,
};

static AL_THardwareDriver hardwareDriver = {
    &hardwareDriverVtable,
};

int32_t AL_Fifo_Init(int32_t *arg1, int32_t arg2)
{
    int32_t s1;
    int32_t v0_1;
    int32_t v0_2;
    int32_t v0_4;
    int32_t v1_1;

    s1 = (arg2 + 1) << 2;
    *arg1 = arg2 + 1;
    arg1[1] = 0;
    arg1[2] = 0;
    v0_1 = (int32_t)(intptr_t)Rtos_Malloc((size_t)s1);
    arg1[3] = v0_1;
    if (v0_1 == 0)
        return 0;
    Rtos_Memset((void *)(intptr_t)v0_1, 0xcd, (size_t)s1);
    v0_2 = (int32_t)(intptr_t)Rtos_CreateSemaphore(0);
    arg1[5] = v0_2;
    if (v0_2 == 0) {
        Rtos_Free((void *)(intptr_t)arg1[3]);
        return 0;
    }
    arg1[6] = (int32_t)(intptr_t)Rtos_CreateSemaphore(arg2);
    v0_4 = (int32_t)(intptr_t)Rtos_CreateMutex();
    v1_1 = arg1[6];
    arg1[4] = v0_4;
    if (v1_1 != 0)
        return 1;
    Rtos_DeleteSemaphore((void *)(intptr_t)arg1[5]);
    Rtos_Free((void *)(intptr_t)arg1[3]);
    return 0;
}

void AL_Fifo_Deinit(int32_t *arg1)
{
    Rtos_Free((void *)(intptr_t)*(arg1 + 0xc / 4));
    Rtos_DeleteSemaphore((void *)(intptr_t)*(arg1 + 0x14 / 4));
    Rtos_DeleteSemaphore((void *)(intptr_t)*(arg1 + 0x18 / 4));
    Rtos_DeleteMutex((void *)(intptr_t)*(arg1 + 0x10 / 4));
}

int32_t AL_Fifo_Queue(int32_t *arg1, void *arg2, int32_t arg3)
{
    int32_t result;
    int32_t a0_2;
    int32_t v0;
    int32_t a0_5;

    result = Rtos_GetSemaphore((void *)(intptr_t)arg1[6], arg3);
    if (result != 0) {
        Rtos_GetMutex((void *)(intptr_t)arg1[4]);
        a0_2 = arg1[1];
        v0 = *arg1;
        if (v0 == 0)
            __builtin_trap();
        *(void **)((char *)(intptr_t)arg1[3] + ((uint32_t)a0_2 << 2)) = arg2;
        a0_5 = arg1[4];
        arg1[1] = (int32_t)(((uint32_t)a0_2 + 1U) % (uint32_t)v0);
        Rtos_ReleaseMutex((void *)(intptr_t)a0_5);
        Rtos_ReleaseSemaphore((void *)(intptr_t)arg1[5]);
    }
    return result;
}

void *AL_Fifo_Dequeue(int32_t *arg1, int32_t arg2)
{
    int32_t a0_2;
    int32_t v0_1;
    int32_t a0_3;
    void *result;

    (void)arg2;

    if (((int32_t (*)(void *))Rtos_GetSemaphore)((void *)(intptr_t)arg1[5]) == 0)
        return 0;
    Rtos_GetMutex((void *)(intptr_t)arg1[4]);
    a0_2 = arg1[2];
    v0_1 = *arg1;
    if (v0_1 == 0)
        __builtin_trap();
    a0_3 = arg1[4];
    result = *(void **)((char *)(intptr_t)arg1[3] + ((uint32_t)a0_2 << 2));
    arg1[2] = (int32_t)(((uint32_t)a0_2 + 1U) % (uint32_t)v0_1);
    Rtos_ReleaseMutex((void *)(intptr_t)a0_3);
    Rtos_ReleaseSemaphore((void *)(intptr_t)arg1[6]);
    return result;
}

int32_t AL_GetRequiredLevel(uint32_t arg1, const uint32_t *arg2, int32_t arg3)
{
    int32_t v1_1;
    const uint32_t *a1;
    const uint32_t *a3_1;

    if (arg3 <= 0)
        return 0xff;
    v1_1 = 0;
    if (*arg2 >= arg1)
        return (int32_t)*((const uint8_t *)&arg2[1]);
    a1 = &arg2[2];
    while (1) {
        v1_1 += 1;
        a3_1 = a1;
        a1 = &a1[2];
        if (arg3 == v1_1)
            return 0xff;
        if (*(a1 - 2) >= arg1)
            return (int32_t)*((const uint8_t *)&a3_1[1]);
    }
}

int32_t GetBlk64x64(int32_t arg1, int32_t arg2)
{
    int32_t v0;
    int32_t a3;

    v0 = arg2 + 0x3f;
    a3 = arg1 + 0x7e;
    if (v0 < 0)
        v0 = arg2 + 0x7e;
    if (arg1 + 0x3f >= 0)
        a3 = arg1 + 0x3f;
    return (a3 >> 6) * (v0 >> 6);
}

int32_t GetBlk32x32(int32_t arg1, int32_t arg2)
{
    int32_t v0;
    int32_t a3;

    v0 = arg2 + 0x1f;
    a3 = arg1 + 0x3e;
    if (v0 < 0)
        v0 = arg2 + 0x3e;
    if (arg1 + 0x1f >= 0)
        a3 = arg1 + 0x1f;
    return (a3 >> 5) * (v0 >> 5);
}

int32_t GetBlk16x16(int32_t arg1, int32_t arg2)
{
    int32_t v0;
    int32_t a3;

    v0 = arg2 + 0xf;
    a3 = arg1 + 0x1e;
    if (v0 < 0)
        v0 = arg2 + 0x1e;
    if (arg1 + 0xf >= 0)
        a3 = arg1 + 0xf;
    return (a3 >> 4) * (v0 >> 4);
}

int32_t GetPcmVclNalSize(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    int32_t a3;

    a3 = (int32_t)g_NalSizeTable[arg3][0] * arg4;
    return ((a3 * 7 + 0x2f) / 0x30) * GetBlk64x64(arg1, arg2);
}

int32_t Hevc_GetMaxVclNalSize(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    int32_t a3;

    a3 = (int32_t)g_NalSizeTable[arg3][0] * arg4;
    return ((a3 * 5 + 0x17) / 0x18) * GetBlk64x64(arg1, arg2);
}

int32_t AL_GetMaxNalSize(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                         int32_t arg6, int32_t arg7)
{
    int32_t v0;

    if (arg1 == 1)
        return (Hevc_GetMaxVclNalSize(arg2, arg3, arg4, arg5) + 0x200 +
                (((int32_t (*)(int32_t))AL_HEVC_GetMaxNumberOfSlices)(arg6) << 9) + 0x1f) >> 5
               << 5;
    v0 = GetPcmVclNalSize(arg2, arg3, arg4, arg5);
    if (arg1 == 0)
        return (v0 + 0x200 +
                (((int32_t (*)(int32_t, int32_t, int32_t, int32_t, int32_t))
                      AL_AVC_GetMaxNumberOfSlices)(arg7, arg6, 1, 0x3c, 0x7fffffff)
                 << 9) +
                0x1f) >> 5 << 5;
    return (v0 + 0x41f) >> 5 << 5;
}

int32_t AL_GetMitigatedMaxNalSize(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    int32_t v0;
    int32_t v1;

    v0 = GetPcmVclNalSize(arg1, arg2, arg3, arg4);
    v1 = arg2 + 0x1e;
    if (arg2 + 0xf >= 0)
        v1 = arg2 + 0xf;
    return ((((v1 >> 4) + 1) << 9) + v0 + 0x1f) >> 5 << 5;
}

AL_THardwareDriver *AL_GetHardwareDriver(void)
{
    return &hardwareDriver;
}

void AL_HDRSEIs_Reset(uint8_t *arg1)
{
    if (arg1 != 0) {
        *arg1 = 0;
        arg1[0x1c] = 0;
    }
}

static int32_t PostMessage(int32_t arg1, int32_t arg2, int32_t arg3, int32_t *arg4)
{
    int32_t v0_3;

    (void)arg1;

    while (1) {
        int32_t s1_1;

        if (arg3 == -4) {
            int32_t v0_5 = Rtos_DriverPoll(arg2, *arg4, 1);
            s1_1 = v0_5;
            if (v0_5 == 0)
                return 4;
        } else {
            s1_1 = ((int32_t (*)(int32_t, int32_t, int32_t *))Rtos_DriverIoctl)(arg2, arg3,
                                                                                 arg4);
        }
        v0_3 = errno;
        if (s1_1 >= 0)
            return 0;
        if (v0_3 != 0xb) {
            if (v0_3 != 4)
                break;
        }
    }
    if (v0_3 == 0xc)
        return 2;
    if (v0_3 == 0x16)
        return 3;
    if (v0_3 == 1)
        return 3;
    return 1;
}

static int32_t Close(int32_t arg1, int32_t arg2)
{
    (void)arg1;

    return ((int32_t (*)(int32_t))Rtos_DriverClose)(arg2);
}

static int32_t Open(int32_t arg1, int32_t arg2)
{
    int32_t result;

    (void)arg1;

    result = Rtos_DriverOpen(arg2);
    if (result == 0)
        return -1;
    return result;
}
