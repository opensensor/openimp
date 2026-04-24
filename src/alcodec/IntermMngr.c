#include <stdint.h>

#include "alcodec/al_rtos.h"
#include "imp_log_int.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, void *caller);

int32_t AL_GetAllocSize_CompData(int32_t arg1, int32_t arg2, char arg3, char arg4, int32_t arg5,
                                 char arg6); /* forward decl, ported by T20 later */
int32_t AL_GetAllocSize_EncCompMap(int32_t arg1, int32_t arg2, char arg3,
                                   char arg4, char arg5); /* forward decl, ported by T20 later */
int32_t AL_GetAllocSize_WPP(int32_t arg1, int32_t arg2, char arg3); /* forward decl, ported by T20 later */
int32_t AL_GetAllocSize_SliceSize(int32_t arg1, int32_t arg2,
                                  int32_t arg3, int32_t arg4); /* forward decl, ported by T20 later */
int32_t AL_GetAllocSizeEP2(int32_t arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T20 later */
int32_t AL_GetAllocSizeEP1(void); /* forward decl, ported by T20 later */
uint32_t AL_CleanupMemory(int32_t arg1, int32_t arg2); /* forward decl, ported by T1 later */
void deregister_tm_clones(void); /* forward decl, ported by T<N> later */

/* AL_CLEAN_BUFFERS is defined once in src/core/globals.c (T0 foundation)
 * and consumed here + by AL_CleanupMemory in AllocatorDefault.c. */
extern uint32_t AL_CLEAN_BUFFERS;

typedef struct AL_TIntermBuffer {
    int32_t addr;
    int32_t location;
} AL_TIntermBuffer;

typedef struct AL_TIntermMngr {
    AL_TIntermBuffer buffers[18];   /* 0x00 */
    AL_TIntermBuffer *queue[18];    /* 0x90 */
    int32_t head;                   /* 0xd8 */
    int32_t size;                   /* 0xdc */
    int32_t capacity;               /* 0xe0 */
    int32_t data_size;              /* 0xe4 */
    int32_t map_size;               /* 0xe8 */
    int32_t wpp_size;               /* 0xec */
    int32_t ep1_size;               /* 0xf0 */
    int32_t ep2_size;               /* 0xf4 */
    void *mutex;                    /* 0xf8 */
} AL_TIntermMngr;

#define INTM_MIN_VALID_PTR 0x10000U
#define INTM_KMSG(fmt, ...) IMP_LOG_INFO("INTM", fmt, ##__VA_ARGS__)

static void *AL_IntermMngr_EnsureMutex(AL_TIntermMngr *arg1, const char *site)
{
    void *mutex = arg1->mutex;

    if ((uintptr_t)mutex > INTM_MIN_VALID_PTR) {
        return mutex;
    }

    INTM_KMSG("%s repair-mutex ctx=%p old=%p head=%d size=%d cap=%d ep1=%d wpp=%d ep2=%d",
              site, arg1, mutex, arg1->head, arg1->size, arg1->capacity,
              arg1->ep1_size, arg1->wpp_size, arg1->ep2_size);
    mutex = Rtos_CreateMutex();
    if ((uintptr_t)mutex <= INTM_MIN_VALID_PTR) {
        INTM_KMSG("%s repair-mutex failed ctx=%p new=%p", site, arg1, mutex);
        return NULL;
    }

    arg1->mutex = mutex;
    INTM_KMSG("%s repair-mutex new=%p ctx=%p", site, mutex, arg1);
    return mutex;
}

static void *AL_IntermMngr_Lock(AL_TIntermMngr *arg1, const char *site)
{
    void *mutex = AL_IntermMngr_EnsureMutex(arg1, site);

    if (mutex != NULL) {
        Rtos_GetMutex(mutex);
    }

    return mutex;
}

static int32_t AL_IntermMngr_Unlock(void *mutex)
{
    if (mutex == NULL) {
        return 0;
    }

    return (int32_t)Rtos_ReleaseMutex(mutex);
}

int32_t AL_IntermMngr_ReleaseBufferBack(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2);
int32_t AL_IntermMngr_GetEp1Addr(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2, int32_t *arg3);
int32_t AL_IntermMngr_GetEp1Location(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2);
int32_t AL_IntermMngr_GetWppAddr(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2, int32_t *arg3);
int32_t AL_IntermMngr_GetEp2Addr(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2, int32_t *arg3);
int32_t AL_IntermMngr_GetMapAddr(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2, int32_t *arg3);
int32_t AL_IntermMngr_GetDataAddr(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2, int32_t *arg3);
int32_t AL_IntermMngr_GetBuffer(AL_TIntermMngr *arg1);

static int32_t setting_read_s32(const void *base, uint32_t offset)
{
    return *(const int32_t *)((const uint8_t *)base + offset);
}

static uint8_t setting_read_u8(const void *base, uint32_t offset)
{
    return *(const uint8_t *)((const uint8_t *)base + offset);
}

static int32_t AL_GetIntIdx_part_30(void)
{
    __assert("uIntMask == (1u << iIntIdx)",
             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/include/lib_ip_ctrl/RegistersEnc.h",
             0x14, "AL_GetIntIdx", &_gp);
    deregister_tm_clones();
    return 0;
}

static int32_t GetSubBufLocation_part_0(void)
{
    __assert("(pAddr % 32) == 0",
             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/IntermMngr.c",
             0xb0, "GetSubBufLocation", &_gp);
    return AL_GetIntIdx_part_30();
}

int32_t AL_IntermMngr_Init(AL_TIntermMngr *arg1, const void *arg2)
{
    uint32_t v1 = (uint32_t)(uint8_t)setting_read_u8(arg2, 4);
    uint32_t v0 = (uint32_t)(uint8_t)setting_read_u8(arg2, 0x1f);
    uint32_t a1 = (uint32_t)(uint8_t)setting_read_u8(arg2, 6);
    uint32_t var_1c = a1;
    int32_t var_30 = 0;
    int32_t var_2c = 0;

    arg1->data_size = 0;
    arg1->map_size = 0;

    if (v0 == 0 && (uint32_t)setting_read_u8(arg2, 0x3c) >= 2U) {
        int32_t v0_3 = setting_read_s32(arg2, 0x10);
        int32_t t0_1 = v0_3 & 0xf;
        int32_t a3_1 = (int32_t)((uint32_t)v0_3 >> 4) & 0xf;
        uint32_t s2_3 = (uint32_t)(uint8_t)(1U << (setting_read_u8(arg2, 0x4e) & 0x1f));

        if (a3_1 < t0_1) {
            a3_1 = t0_1;
        }

        var_2c = 1;
        arg1->data_size =
            AL_GetAllocSize_CompData((int32_t)v1, (int32_t)a1, (char)s2_3, (char)a3_1,
                                     (int32_t)((uint32_t)v0_3 >> 8) & 0xf, 1);
        var_30 = 1;
        arg1->map_size = AL_GetAllocSize_EncCompMap((int32_t)v1, (int32_t)var_1c, (char)s2_3,
                                                    setting_read_u8(arg2, 0x3c), 1);
        a1 = (uint32_t)(uint8_t)setting_read_u8(arg2, 6);
    }

    arg1->capacity = 0;
    arg1->head = 0;
    arg1->size = 0;

    if (setting_read_u8(arg2, 0x3e) != 0) {
        arg1->wpp_size = AL_GetAllocSize_SliceSize((int32_t)(uint32_t)(uint8_t)setting_read_u8(arg2, 4),
                                                   (int32_t)a1, setting_read_s32(arg2, 0x40),
                                                   (int32_t)(uint32_t)(uint8_t)setting_read_u8(arg2, 0x4e));
    } else {
        uint32_t a0_2 = (uint32_t)(uint8_t)setting_read_u8(arg2, 0x4e);

        arg1->wpp_size = AL_GetAllocSize_WPP(
            (int32_t)(((1U << (a0_2 & 0x1f)) + a1 - 1U) >> (a0_2 & 0x1f)),
            (int32_t)(uint32_t)(uint8_t)setting_read_u8(arg2, 0x40),
            setting_read_u8(arg2, 0x3c));
    }

    arg1->ep2_size = AL_GetAllocSizeEP2((int32_t)v1, (int32_t)var_1c, (int32_t)v0);
    arg1->ep1_size = AL_GetAllocSizeEP1();
    arg1->mutex = Rtos_CreateMutex();
    INTM_KMSG("Init ctx=%p mutex=%p ep1=%d wpp=%d ep2=%d data=%d map=%d",
              arg1, arg1->mutex, arg1->ep1_size, arg1->wpp_size, arg1->ep2_size,
              arg1->data_size, arg1->map_size);
    (void)var_30;
    (void)var_2c;
    return ((uintptr_t)arg1->mutex > INTM_MIN_VALID_PTR) ? 1 : 0;
}

int32_t AL_IntermMngr_Deinit(AL_TIntermMngr *arg1)
{
    arg1->capacity = 0;
    if ((uintptr_t)arg1->mutex > INTM_MIN_VALID_PTR) {
        Rtos_DeleteMutex(arg1->mutex);
    } else if (arg1->mutex != NULL) {
        INTM_KMSG("Deinit skip-delete invalid mutex=%p ctx=%p", arg1->mutex, arg1);
    }
    arg1->mutex = NULL;
    return 0;
}

int32_t AL_IntermMngr_GetBufferSize(AL_TIntermMngr *arg1)
{
    return arg1->ep1_size + arg1->wpp_size + arg1->ep2_size + arg1->data_size + arg1->map_size;
}

int32_t AL_IntermMngr_AddBuffer(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2)
{
    void *mutex = AL_IntermMngr_Lock(arg1, "AddBuffer");

    if (mutex == NULL) {
        return 0;
    }

    {
        int32_t a0_1 = arg1->capacity;
        int32_t result = 0;

        if (a0_1 < 0x12) {
            int32_t v0_1 = arg2->location;
            AL_TIntermBuffer *a1_1 = &arg1->buffers[a0_1];
            int32_t a2_1;
            int32_t v0_3;

            a1_1->addr = arg2->addr;
            a1_1->location = v0_1;
            a2_1 = arg1->size;
            v0_3 = a2_1 + arg1->head;
            arg1->capacity = a0_1 + 1;
            result = 1;
            arg1->queue[v0_3 % 0x12] = a1_1;
            arg1->size = a2_1 + 1;
        }

        AL_IntermMngr_Unlock(mutex);
        return result;
    }
}

int32_t AL_IntermMngr_ReleaseBuffer(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2)
{
    void *mutex = AL_IntermMngr_Lock(arg1, "ReleaseBuffer");

    if (mutex == NULL) {
        return 0;
    }

    {
        int32_t a1 = arg1->size;

        if (a1 >= arg1->capacity) {
            __assert("pCtx->iSize < pCtx->iCapacity",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/IntermMngr.c",
                     0x92, "AL_IntermMngr_ReleaseBuffer", &_gp);
            return AL_IntermMngr_ReleaseBufferBack(arg1, arg2);
        }

        arg1->queue[(a1 + arg1->head) % 0x12] = arg2;
        arg1->size = a1 + 1;
        return AL_IntermMngr_Unlock(mutex);
    }
}

int32_t AL_IntermMngr_ReleaseBufferBack(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2)
{
    void *mutex = AL_IntermMngr_Lock(arg1, "ReleaseBufferBack");

    if (mutex == NULL) {
        return 0;
    }

    {
        int32_t a2 = arg1->size;

        if (a2 >= arg1->capacity) {
            __assert("pCtx->iSize < pCtx->iCapacity",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/IntermMngr.c",
                     0x9e, "AL_IntermMngr_ReleaseBufferBack", &_gp);
            return AL_IntermMngr_GetEp1Addr(arg1, arg2, 0);
        }

        {
            int32_t v0_2 = arg1->head;
            int32_t v1 = 0x11;

            if (v0_2 != 0) {
                v1 = v0_2 - 1;
            }

            arg1->head = v1;
            arg1->queue[v1] = arg2;
            arg1->size = a2 + 1;
            return AL_IntermMngr_Unlock(mutex);
        }
    }
}

int32_t AL_IntermMngr_GetEp1Addr(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2, int32_t *arg3)
{
    int32_t result;

    (void)arg1;
    result = arg2->addr;
    if (arg3 != 0) {
        *arg3 = arg2->location;
    }
    if ((result & 0x1f) == 0) {
        return result;
    }
    GetSubBufLocation_part_0();
    return AL_IntermMngr_GetEp1Location(arg1, arg2);
}

int32_t AL_IntermMngr_GetEp1Location(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2)
{
    (void)arg1;

    if ((arg2->addr & 0x1f) == 0) {
        return arg2->location;
    }

    GetSubBufLocation_part_0();
    return AL_IntermMngr_GetWppAddr(arg1, arg2, 0);
}

int32_t AL_IntermMngr_GetWppAddr(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2, int32_t *arg3)
{
    int32_t a0 = arg1->ep1_size;
    int32_t result = a0 + arg2->addr;

    if (arg3 != 0) {
        *arg3 = arg2->location + a0;
    }
    if ((result & 0x1f) == 0) {
        return result;
    }
    GetSubBufLocation_part_0();
    return AL_IntermMngr_GetEp2Addr(arg1, arg2, arg3);
}

int32_t AL_IntermMngr_GetEp2Addr(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2, int32_t *arg3)
{
    int32_t v1_1 = arg1->ep1_size + arg1->wpp_size;
    int32_t result = v1_1 + arg2->addr;

    if (arg3 != 0) {
        *arg3 = arg2->location + v1_1;
    }
    if ((result & 0x1f) == 0) {
        return result;
    }
    GetSubBufLocation_part_0();
    return AL_IntermMngr_GetMapAddr(arg1, arg2, arg3);
}

int32_t AL_IntermMngr_GetMapAddr(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2, int32_t *arg3)
{
    int32_t v1_1 = arg1->ep1_size + arg1->wpp_size + arg1->ep2_size;
    int32_t result = v1_1 + arg2->addr;

    if (arg3 != 0) {
        *arg3 = arg2->location + v1_1;
    }
    if ((result & 0x1f) == 0) {
        return result;
    }
    GetSubBufLocation_part_0();
    return AL_IntermMngr_GetDataAddr(arg1, arg2, arg3);
}

int32_t AL_IntermMngr_GetDataAddr(AL_TIntermMngr *arg1, AL_TIntermBuffer *arg2, int32_t *arg3)
{
    int32_t v1_2 = arg1->ep1_size + arg1->wpp_size + arg1->ep2_size + arg1->map_size;
    int32_t result = v1_2 + arg2->addr;

    if (arg3 != 0) {
        *arg3 = arg2->location + v1_2;
    }
    if ((result & 0x1f) == 0) {
        return result;
    }
    GetSubBufLocation_part_0();
    return AL_IntermMngr_GetBuffer(arg1);
}

int32_t AL_IntermMngr_GetBuffer(AL_TIntermMngr *arg1)
{
    AL_TIntermBuffer *result;
    void *mutex = AL_IntermMngr_Lock(arg1, "GetBuffer");

    if (mutex == NULL) {
        return 0;
    }
    {
        int32_t a2 = arg1->size;

        if (a2 == 0) {
            result = 0;
        } else {
            int32_t a0_1 = arg1->head;
            uint32_t a3_1 = AL_CLEAN_BUFFERS;

            result = arg1->queue[a0_1];
            arg1->size = a2 - 1;
            if (a3_1 != 0) {
                int32_t var_14;
                int32_t var_18;
                int32_t var_1c;
                int32_t var_20;

                AL_IntermMngr_GetWppAddr(arg1, result, &var_14);
                AL_IntermMngr_GetEp2Addr(arg1, result, &var_18);
                AL_IntermMngr_GetMapAddr(arg1, result, &var_1c);
                AL_IntermMngr_GetDataAddr(arg1, result, &var_20);
                AL_CleanupMemory(var_14, arg1->wpp_size);
                AL_CleanupMemory(var_18, arg1->ep2_size);
                AL_CleanupMemory(var_1c, arg1->map_size);
                AL_CleanupMemory(var_20, arg1->data_size);
            }
            arg1->head = (a0_1 + 1) % 0x12;
        }
        AL_IntermMngr_Unlock(mutex);
    }
    return (int32_t)(intptr_t)result;
}
