#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "alcodec/al_buffer.h"
#include "alcodec/al_fourcc.h"
#include "alcodec/al_metadata.h"
#include "alcodec/al_rtos.h"
#include "alcodec/al_types.h"
#include "alcodec/al_utils.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function,
                        void *caller);

#define BPF_KMSG(fmt, ...) do { \
    int _kfd = open("/dev/kmsg", O_WRONLY); \
    if (_kfd >= 0) { \
        char _b[192]; \
        int _n = snprintf(_b, sizeof(_b), "libimp/BUF: " fmt "\n", ##__VA_ARGS__); \
        if (_n > 0) write(_kfd, _b, _n > (int)sizeof(_b) ? (int)sizeof(_b) : _n); \
        close(_kfd); \
    } \
} while (0)

AL_TBuffer *AL_Buffer_Create_And_AllocateNamed(AL_TAllocator *arg1, int32_t arg2, char arg3, uint32_t arg4,
                                               void *arg5,
                                               int32_t arg6); /* forward decl, ported by T16 later */
int32_t AL_Buffer_AddMetaData(AL_TBuffer *arg1, void *arg2); /* forward decl, ported by T16 later */
int32_t AL_Buffer_Ref(AL_TBuffer *arg1); /* forward decl, ported by T16 later */
AL_TPicFormat *AL_EncGetSrcPicFormat(AL_TPicFormat *pPicFormat, int32_t eChromaMode, uint8_t uBitDepth,
                                     int32_t eStorageMode,
                                     uint8_t bCompressed); /* forward decl, ported by T20 earlier */
int32_t AL_GetAllocSizeEP2(int32_t arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T20 earlier */
int32_t AL_EncGetMinPitch(int32_t arg1, char arg2, int32_t arg3); /* forward decl, ported by T20 earlier */
int32_t AL_GetSrcStorageMode(int32_t arg1); /* forward decl, ported by T20 earlier */
int32_t AL_IsSrcCompressed(int32_t arg1); /* forward decl, ported by T20 earlier */
int32_t AL_GetAllocSizeSrc_Y(int32_t arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T20 earlier */
uint32_t AL_GetAllocSizeSrc_UV(int32_t arg1, int32_t arg2, int32_t arg3,
                               int32_t arg4); /* forward decl, ported by T20 earlier */
int32_t AL_GetAllocSizeSrc_MapY(int32_t arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T20 earlier */
int32_t AL_GetAllocSizeSrc_MapUV(int32_t arg1, int32_t arg2, int32_t arg3,
                                 int32_t arg4); /* forward decl, ported by T20 earlier */
int32_t GetFbcMapPitch(int32_t arg1, int32_t arg2); /* forward decl, ported by T7 earlier */
AL_TBuffer *AL_PixMapBuffer_Create(AL_TAllocator *arg1, void *arg2, int32_t arg3, int32_t arg4,
                                   uint32_t arg5); /* forward decl, ported by T17 earlier */
int32_t AL_PixMapBuffer_Allocate_And_AddPlanes(AL_TBuffer *arg1, int32_t arg2, char arg3, uint32_t arg4,
                                               int32_t *arg5, int32_t arg6,
                                               int32_t arg7); /* forward decl, ported by T17 earlier */

static inline int32_t read_s32(const void *ptr, uint32_t offset)
{
    return *(const int32_t *)((const uint8_t *)ptr + offset);
}

static inline uint32_t read_u32(const void *ptr, uint32_t offset)
{
    return *(const uint32_t *)((const uint8_t *)ptr + offset);
}

static inline uint16_t read_u16(const void *ptr, uint32_t offset)
{
    return *(const uint16_t *)((const uint8_t *)ptr + offset);
}

static inline uint8_t read_u8(const void *ptr, uint32_t offset)
{
    return *(const uint8_t *)((const uint8_t *)ptr + offset);
}

static inline void *read_ptr(const void *ptr, uint32_t offset)
{
    return (void *)(uintptr_t)read_u32(ptr, offset);
}

static inline void write_u32(void *ptr, uint32_t offset, uint32_t value)
{
    *(uint32_t *)((uint8_t *)ptr + offset) = value;
}

static inline void write_u8(void *ptr, uint32_t offset, uint8_t value)
{
    *(uint8_t *)((uint8_t *)ptr + offset) = value;
}

static inline void write_ptr(void *ptr, uint32_t offset, const void *value)
{
    write_u32(ptr, offset, (uint32_t)(uintptr_t)value);
}

static inline double cvt_u32_with_bias(uint32_t value, double bias)
{
    double result = (double)(int32_t)value;

    if ((int32_t)value < 0)
        result += bias;

    return result;
}

int32_t GetCPlanePitch(int32_t arg1, uint32_t arg2);
AL_TBuffer *AL_BufPool_GetBuffer(void *arg1, int32_t arg2);

int32_t Fifo_Init(void *arg1, int32_t arg2)
{
    void *var_18 = &_gp;
    int32_t s1 = (arg2 + 1) << 2;
    int32_t v0_1;
    int32_t v0_2;
    int32_t v0_4;
    int32_t v1_1;

    (void)var_18;

    write_u32(arg1, 0x00, (uint32_t)(arg2 + 1));
    write_u32(arg1, 0x04, 0);
    write_u32(arg1, 0x08, 0);
    write_u32(arg1, 0x18, 0);
    write_u8(arg1, 0x1c, 0);
    v0_1 = (int32_t)(intptr_t)Rtos_Malloc((size_t)s1);
    write_ptr(arg1, 0x0c, (void *)(intptr_t)v0_1);
    if (v0_1 == 0)
        return 0;

    Rtos_Memset((void *)(intptr_t)v0_1, 0xcd, (size_t)s1);
    v0_2 = (int32_t)(intptr_t)Rtos_CreateEvent(0);
    write_ptr(arg1, 0x14, (void *)(intptr_t)v0_2);
    if (v0_2 == 0) {
        Rtos_Free(read_ptr(arg1, 0x0c));
        return 0;
    }

    write_ptr(arg1, 0x20, Rtos_CreateSemaphore(arg2));
    v0_4 = (int32_t)(intptr_t)Rtos_CreateMutex();
    v1_1 = read_s32(arg1, 0x20);
    write_ptr(arg1, 0x10, (void *)(intptr_t)v0_4);
    if (v1_1 != 0)
        return 1;

    Rtos_DeleteEvent(read_ptr(arg1, 0x14));
    Rtos_Free(read_ptr(arg1, 0x0c));
    return 0;
}

void Fifo_Deinit(void *arg1)
{
    void *var_10 = &_gp;

    (void)var_10;

    Rtos_Free(read_ptr(arg1, 0x0c));
    Rtos_DeleteEvent(read_ptr(arg1, 0x14));
    Rtos_DeleteSemaphore(read_ptr(arg1, 0x20));
    Rtos_DeleteMutex(read_ptr(arg1, 0x10));
}

int32_t Fifo_Queue(void *arg1, AL_TBuffer *arg2, int32_t arg3)
{
    void *var_18 = &_gp;
    int32_t result = Rtos_GetSemaphore(read_ptr(arg1, 0x20), arg3);

    (void)var_18;

    BPF_KMSG("Fifo_Queue entry fifo=%p elem=%p wait=%d sem=%d head=%d tail=%d count=%d",
             arg1, arg2, arg3, result, read_s32(arg1, 0x04), read_s32(arg1, 0x08), read_s32(arg1, 0x18));

    if (result == 0)
        return result;

    Rtos_GetMutex(read_ptr(arg1, 0x10));

    {
        int32_t a0_2 = read_s32(arg1, 0x04);
        int32_t v0 = read_s32(arg1, 0x00);
        int32_t v0_1;
        void *a0_5;

        if (v0 == 0)
            __builtin_trap();

        v0_1 = read_s32(arg1, 0x18);
        *(AL_TBuffer **)((uint8_t *)read_ptr(arg1, 0x0c) + ((uint32_t)a0_2 << 2)) = arg2;
        a0_5 = read_ptr(arg1, 0x14);
        write_u32(arg1, 0x18, (uint32_t)(v0_1 + 1));
        write_u32(arg1, 0x04, (uint32_t)((uint32_t)(a0_2 + 1) % (uint32_t)v0));
        Rtos_SetEvent(a0_5);
    }

    Rtos_ReleaseMutex(read_ptr(arg1, 0x10));
    BPF_KMSG("Fifo_Queue ok fifo=%p elem=%p head=%d tail=%d count=%d",
             arg1, arg2, read_s32(arg1, 0x04), read_s32(arg1, 0x08), read_s32(arg1, 0x18));
    return result;
}

AL_TBuffer *Fifo_Dequeue(void *arg1, int32_t arg2)
{
    void *var_18 = &_gp;
    int32_t v0;
    int32_t a0_1;

    (void)var_18;

    BPF_KMSG("Fifo_Dequeue entry fifo=%p wait=%d head=%d tail=%d count=%d decommit=%d",
             arg1, arg2, read_s32(arg1, 0x04), read_s32(arg1, 0x08), read_s32(arg1, 0x18), read_u8(arg1, 0x1c));
    Rtos_GetMutex(read_ptr(arg1, 0x10));
    v0 = read_s32(arg1, 0x18);
    if (v0 <= 0) {
        while (1) {
            int32_t v0_1;

            if (read_u8(arg1, 0x1c) == 0) {
                Rtos_ReleaseMutex(read_ptr(arg1, 0x10));
                v0_1 = Rtos_WaitEvent(read_ptr(arg1, 0x14), arg2);
                Rtos_GetMutex(read_ptr(arg1, 0x10));
                v0 = read_s32(arg1, 0x18);
                if (v0 > 0) {
                    a0_1 = read_s32(arg1, 0x08);
                    break;
                }

                if (v0_1 != 0)
                    continue;
            }

            Rtos_ReleaseMutex(read_ptr(arg1, 0x10));
            BPF_KMSG("Fifo_Dequeue empty fifo=%p", arg1);
            return NULL;
        }
    } else {
        a0_1 = read_s32(arg1, 0x08);
    }

    {
        int32_t v1 = read_s32(arg1, 0x00);
        uint32_t hi;
        void *a0_6;
        AL_TBuffer *result;

        if (v1 == 0)
            __builtin_trap();

        hi = (uint32_t)(a0_1 + 1) % (uint32_t)v1;
        a0_6 = read_ptr(arg1, 0x10);
        result = *(AL_TBuffer **)((uint8_t *)read_ptr(arg1, 0x0c) + ((uint32_t)a0_1 << 2));
        write_u32(arg1, 0x18, (uint32_t)(v0 - 1));
        write_u32(arg1, 0x08, hi);
        Rtos_ReleaseMutex(a0_6);
        Rtos_ReleaseSemaphore(read_ptr(arg1, 0x20));
        BPF_KMSG("Fifo_Dequeue ok fifo=%p elem=%p head=%d tail=%d count=%d",
                 arg1, result, read_s32(arg1, 0x04), read_s32(arg1, 0x08), read_s32(arg1, 0x18));
        return result;
    }
}

void Fifo_Decommit(void *arg1)
{
    void *var_10 = &_gp;
    void *a0_1;

    (void)var_10;

    Rtos_GetMutex(read_ptr(arg1, 0x10));
    a0_1 = read_ptr(arg1, 0x14);
    write_u32(arg1, 0x1c, 1);
    Rtos_SetEvent(a0_1);
    Rtos_ReleaseMutex(read_ptr(arg1, 0x10));
}

int32_t Fifo_GetMaxElements(void *arg1)
{
    void *var_18 = &_gp;
    int32_t result;

    (void)var_18;

    Rtos_GetMutex(read_ptr(arg1, 0x10));
    result = read_s32(arg1, 0x00) - 1;
    Rtos_ReleaseMutex(read_ptr(arg1, 0x10));
    return result;
}

int32_t AL_BufPool_AddMetaData(void *arg1, void *arg2)
{
    void *var_20 = &_gp;
    int32_t i;

    (void)var_20;

    if (read_u32(arg1, 0x08) == 0)
        return 1;

    i = 0;
    do {
        AL_TBuffer *s3_1 = *(AL_TBuffer **)((uint8_t *)read_ptr(arg1, 0x04) + ((uint32_t)i << 2));
        int32_t result;

        i += 1;
        result = AL_Buffer_AddMetaData(s3_1, (*(void *(**)(void *))((uint8_t *)arg2 + 8))(arg2));
        if (result == 0)
            return result;
    } while ((uint32_t)i < read_u32(arg1, 0x08));

    return 1;
}

void AL_BufPool_Deinit(void *arg1)
{
    void *var_18 = &_gp;
    int32_t s0 = 0;

    (void)var_18;

    if (read_u32(arg1, 0x08) != 0) {
        int32_t i;
        int32_t s2_1;

        do {
            s2_1 = s0 << 2;
            s0 += 1;
            AL_Buffer_Destroy(*(AL_TBuffer **)((uint8_t *)read_ptr(arg1, 0x04) + (uint32_t)s2_1));
            i = ((uint32_t)s0 < read_u32(arg1, 0x08)) ? 1 : 0;
            *(uint32_t *)((uint8_t *)read_ptr(arg1, 0x04) + (uint32_t)s2_1) = 0;
        } while (i != 0);
    }

    {
        void *a0_1 = read_ptr(arg1, 0x10);

        if (a0_1 != NULL)
            ((void (*)(void *))((uint8_t *)a0_1 + 4))(a0_1);
    }

    Fifo_Deinit((uint8_t *)arg1 + 0x14);
    Rtos_Free(read_ptr(arg1, 0x04));
    Rtos_Memset(arg1, 0, 0x38);
}

void AL_BufPool_Decommit(void *arg1)
{
    Fifo_Decommit((uint8_t *)arg1 + 0x14);
}

int32_t AL_sBufPool_InitStructure(void *arg1, AL_TAllocator *arg2, int32_t arg3, void *arg4, char arg5)
{
    void *var_20 = &_gp;
    int32_t result = 0;

    (void)var_20;

    if (arg1 != 0 && arg2 != 0) {
        int32_t result_1;
        int32_t v0_3;

        write_ptr(arg1, 0x00, arg2);
        result_1 = Fifo_Init((uint8_t *)arg1 + 0x14, arg3);
        result = result_1;
        if (result_1 == 0)
            return 0;

        write_ptr(arg1, 0x0c, arg4);
        write_u8(arg1, 0x10, (uint8_t)arg5);
        write_u32(arg1, 0x08, 0);
        v0_3 = (int32_t)(intptr_t)Rtos_Malloc((size_t)(arg3 << 2));
        write_ptr(arg1, 0x04, (void *)(intptr_t)v0_3);
        if (v0_3 == 0) {
            result = 0;
            AL_BufPool_Deinit(arg1);
        }
    }

    return result;
}

int32_t AL_sBufPool_FreeBufInPool(AL_TBuffer *arg1)
{
    void *var_10 = &_gp;

    (void)var_10;

    return Fifo_Queue((uint8_t *)AL_Buffer_GetUserData(arg1) + 0x14, arg1, 0xffffffff);
}

int32_t FreePixMapBufInPool(AL_TBuffer *arg1)
{
    return AL_sBufPool_FreeBufInPool(arg1);
}

int32_t AL_sBufPool_AddBuf(void *arg1, AL_TBuffer *arg2)
{
    if (arg2 == 0)
        return 0;

    if (read_u32(arg1, 0x08) >= (uint32_t)Fifo_GetMaxElements((uint8_t *)arg1 + 0x14)) {
        int32_t *a0_4;
        int32_t a1_2;
        int32_t *a2;

        a0_4 = (int32_t *)__assert("pBufPool->uNumBuf < Fifo_GetMaxElements(&pBufPool->fifo)",
                                   "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/BufPool.c", 0x3a,
                                   "AL_sBufPool_AddBuf", &_gp);
        return AL_sBufPool_InitStructure(a0_4, (AL_TAllocator *)(intptr_t)a1_2, (int32_t)(intptr_t)a2, NULL, 0);
    }

    AL_Buffer_SetUserData(arg2, arg1);

    {
        int32_t v0_2 = read_s32(arg1, 0x08);
        AL_TBuffer **v1_1 = (AL_TBuffer **)((uint8_t *)read_ptr(arg1, 0x04) + ((uint32_t)v0_2 << 2));

        write_u32(arg1, 0x08, (uint32_t)(v0_2 + 1));
        *v1_1 = arg2;
        BPF_KMSG("BufPool_AddBuf pool=%p buf=%p slot=%d pool_count=%d",
                 arg1, arg2, v0_2, read_s32(arg1, 0x08));
    }

    {
        int32_t qret = Fifo_Queue((uint8_t *)arg1 + 0x14, arg2, 0xffffffff);
        BPF_KMSG("BufPool_AddBuf queue_ret=%d pool=%p fifo_count=%d",
                 qret, arg1, read_s32((uint8_t *)arg1 + 0x14, 0x18));
        return qret;
    }
}

int32_t AL_BufPool_Init(void *arg1, AL_TAllocator *arg2, int32_t *arg3)
{
    int32_t result_1 = AL_sBufPool_InitStructure(arg1, arg2, arg3[0], (void *)(intptr_t)arg3[3], (char)read_u8(arg3, 0x10));
    int32_t result = result_1;

    if (result_1 != 0) {
        while (read_u32(arg1, 0x08) < read_u32(arg3, 0x00)) {
            AL_TBuffer *v0_7 = AL_Buffer_Create_And_AllocateNamed(*(AL_TAllocator **)arg1, arg3[1], read_u8(arg3, 0x10), 0,
                                                                  AL_sBufPool_FreeBufInPool, arg3[2]);
            AL_TBuffer *a1_1;

            if (v0_7 == NULL) {
                a1_1 = NULL;
            label_8b720:
                if (AL_sBufPool_AddBuf(arg1, a1_1) != 0)
                    continue;
            } else {
                void *v0_8 = (void *)(intptr_t)arg3[3];

                if (v0_8 != 0) {
                    void *v0_1 = (*(void *(**)(void *))((uint8_t *)v0_8 + 8))(v0_8);

                    if (v0_1 == 0) {
                        AL_Buffer_Destroy(v0_7);
                        a1_1 = NULL;
                    } else {
                        a1_1 = v0_7;
                        if (AL_Buffer_AddMetaData(v0_7, v0_1) == 0) {
                            ((void (*)(void *, AL_TBuffer *))((uint8_t *)v0_1 + 4))(v0_1, a1_1);
                            AL_Buffer_Destroy(v0_7);
                            a1_1 = NULL;
                        }
                    }

                    goto label_8b720;
                }

                if (AL_sBufPool_AddBuf(arg1, v0_7) != 0)
                    continue;
            }

            result = 0;
            AL_BufPool_Deinit(arg1);
            break;
        }
    }

    return result;
}

int32_t AL_GetWaitMode(int32_t arg1)
{
    if (arg1 == 0)
        return 0xffffffff;

    if (arg1 == 1)
        return 0;

    if ((uint32_t)arg1 >= 2U)
        return 0;

    {
        int32_t ra;
        int32_t var_4 = ra;
        int32_t a0_1;
        int32_t a1;

        (void)var_4;

        a0_1 = __assert("eMode >= AL_BUF_MODE_MAX", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/BufPool.c",
                        0x130, "AL_GetWaitMode", &_gp);
        return (int32_t)(intptr_t)AL_BufPool_GetBuffer((void *)(intptr_t)a0_1, a1);
    }
}

AL_TBuffer *AL_BufPool_GetBuffer(void *arg1, int32_t arg2)
{
    void *var_18 = &_gp;
    AL_TBuffer *result = Fifo_Dequeue((uint8_t *)arg1 + 0x14, AL_GetWaitMode(arg2));

    (void)var_18;

    BPF_KMSG("BufPool_GetBuffer pool=%p mode=%d result=%p pool_count=%d",
             arg1, arg2, result, read_s32(arg1, 0x08));
    if (result == 0)
        return result;

    AL_Buffer_Ref(result);
    return result;
}

int32_t *GetBufPoolConfig(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, char arg6)
{
    arg1[1] = arg4;
    arg1[2] = arg2;
    arg1[0] = arg5;
    arg1[3] = arg3;
    *(uint8_t *)((uint8_t *)arg1 + 0x10) = (uint8_t)arg6;
    return arg1;
}

int32_t GetQpBufPoolConfig(void *arg1, void *arg2, void *arg3, int32_t arg4, char arg5)
{
    int32_t v0_2 = (read_u32(arg2, 0x11c) - 1U < 2U) ? 1 : 0;

    memset(arg1, 0, 0x14);
    if (v0_2 == 0)
        return (int32_t)(intptr_t)arg1;

    GetBufPoolConfig((int32_t *)arg1, (int32_t)(intptr_t)"qp-ext", 0,
                     AL_GetAllocSizeEP2((int32_t)read_u8(arg3, 0x04), (int32_t)read_u8(arg3, 0x06),
                                        (int32_t)read_u8(arg3, 0x1f)),
                     arg4, (char)(uint8_t)arg5);
    return (int32_t)(intptr_t)arg1;
}

int32_t ComputeYPitch(int32_t arg1, uint32_t arg2, int32_t arg3)
{
    int32_t v0_2 = AL_EncGetMinPitch(arg1, (char)AL_GetBitDepth(arg2), AL_GetStorageMode(arg2));

    if (arg3 == 0 || arg3 == 0xffffffff)
        return v0_2;

    if (arg3 >= v0_2)
        return arg3;

    {
        int32_t a0_3;
        int32_t a1_1;

        a0_3 = __assert("extStride >= iPitch", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/BufPool.c",
                        0x157, "ComputeYPitch", &_gp);
        return GetCPlanePitch(a0_3, a1_1);
    }
}

int32_t GetCPlanePitch(int32_t arg1, uint32_t arg2)
{
    void *var_10 = &_gp;

    (void)var_10;

    if (AL_IsSemiPlanar(arg2) != 0)
        return arg1;

    return ((arg1 >> 0x1f) + arg1) >> 1;
}

int32_t GetPixMapBufPollConfig(int32_t *arg1, int32_t *arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                               int32_t arg6, char arg7)
{
    void *var_48 = &_gp;
    int32_t s1 = arg6;
    AL_TPicFormat var_40;
    uint32_t v0_3;
    int32_t a0_4;
    int32_t v0_4;
    int32_t v0_5;
    int32_t a0_6;
    int32_t a3_2;
    uint8_t *v1_2;
    int32_t v0_6;
    int32_t v0_7;
    uint8_t *s1_3;
    int32_t a0_9;
    int32_t v0_10;
    int32_t v1_6;
    int32_t result;

    (void)var_48;

    BPF_KMSG("GetPixMapCfg entry out=%p info=%p storage=0x%x count=%d extPitch=%d arg6=%d cache=%d",
             arg1, arg2, arg3, arg4, arg5, arg6, (int)arg7);
    Rtos_Memset(arg1, 0, 0xa0);
    BPF_KMSG("GetPixMapCfg info w=%d h=%d bitdepth=%u chroma=%d",
             arg2[0], arg2[1], read_u8(arg2, 0x08), arg2[3]);
    AL_EncGetSrcPicFormat(&var_40, arg2[3], read_u8(arg2, 0x08), AL_GetSrcStorageMode(arg3), (uint8_t)AL_IsSrcCompressed(arg3));
    BPF_KMSG("GetPixMapCfg post-EncGetSrcPicFormat");
    v0_3 = AL_GetFourCC(var_40);
    BPF_KMSG("GetPixMapCfg fourcc=0x%x", v0_3);
    a0_4 = arg2[0];
    arg1[3] = arg2[1];
    *(uint8_t *)((uint8_t *)arg1 + 0x14) = (uint8_t)arg7;
    arg1[1] = (int32_t)(intptr_t)"PixBuf";
    arg1[4] = (int32_t)v0_3;
    arg1[2] = a0_4;
    arg1[0] = arg4;
    v0_4 = ComputeYPitch(a0_4, v0_3, arg5);
    BPF_KMSG("GetPixMapCfg ypitch=%d", v0_4);
    if (s1 == 0xffffffff)
        s1 = ((arg2[1] + 7) >> 3) << 3;

    v0_5 = AL_GetAllocSizeSrc_Y(arg3, v0_4, s1);
    BPF_KMSG("GetPixMapCfg planeY size=%d lines=%d", v0_5, s1);
    a0_6 = arg1[0x26];
    a3_2 = arg2[3];
    v1_2 = (uint8_t *)&arg1[a0_6 * 4];
    *(int32_t *)(void *)(v1_2 + 0x18) = 0;
    *(int32_t *)(void *)(v1_2 + 0x1c) = 0;
    *(int32_t *)(void *)(v1_2 + 0x20) = v0_4;
    *(int32_t *)(void *)(v1_2 + 0x24) = v0_5;
    v0_6 = arg1[0x27] + v0_5;
    arg1[0x26] = a0_6 + 1;
    arg1[0x27] = v0_6;
    v0_7 = (int32_t)AL_GetAllocSizeSrc_UV(arg3, v0_4, s1, a3_2);
    BPF_KMSG("GetPixMapCfg planeUV size=%d total=%d", v0_7, v0_6);
    s1_3 = (uint8_t *)&arg1[arg1[0x26] * 4];
    *(int32_t *)(void *)(s1_3 + 0x18) = 1;
    *(int32_t *)(void *)(s1_3 + 0x1c) = arg1[0x27];
    *(int32_t *)(void *)(s1_3 + 0x20) = GetCPlanePitch(v0_4, (uint32_t)arg1[4]);
    a0_9 = arg1[4];
    v0_10 = arg1[0x26] + 1;
    v1_6 = arg1[0x27] + v0_7;
    arg1[v0_10 * 4 + 5] = v0_7;
    arg1[0x27] = v1_6;
    arg1[0x26] = v0_10;
    BPF_KMSG("GetPixMapCfg post-planes count=%d bytes=%d compressed=%d",
             arg1[0x26], arg1[0x27], AL_IsCompressed((uint32_t)a0_9));
    result = (int32_t)AL_IsCompressed((uint32_t)a0_9);
    if (result != 0) {
        int32_t v0_12 = GetFbcMapPitch(arg2[0], AL_GetStorageMode((uint32_t)arg1[4]));
        int32_t v0_13 = AL_GetAllocSizeSrc_MapY(arg1[2], arg1[3], arg3);
        int32_t a2_6 = arg1[0x26];
        uint8_t *v1_8 = (uint8_t *)&arg1[a2_6 * 4];
        int32_t t0_1;
        int32_t a3_3;
        int32_t a0_13;
        int32_t a1_11;
        int32_t a0_14;
        uint8_t *v1_10;
        int32_t a1_12;

        *(int32_t *)(void *)(v1_8 + 0x18) = 2;
        t0_1 = arg1[0x27];
        *(int32_t *)(void *)(v1_8 + 0x24) = v0_13;
        *(int32_t *)(void *)(v1_8 + 0x20) = v0_12;
        *(int32_t *)(void *)(v1_8 + 0x1c) = t0_1;
        a3_3 = arg2[3];
        a0_13 = arg1[2];
        a1_11 = arg1[3];
        arg1[0x26] = a2_6 + 1;
        arg1[0x27] = v0_13 + t0_1;
        BPF_KMSG("GetPixMapCfg mapY pitch=%d size=%d total=%d", v0_12, v0_13, arg1[0x27]);
        result = AL_GetAllocSizeSrc_MapUV(a0_13, a1_11, arg3, a3_3);
        a0_14 = arg1[0x26];
        v1_10 = (uint8_t *)&arg1[a0_14 * 4];
        *(int32_t *)(void *)(v1_10 + 0x18) = 3;
        a1_12 = arg1[0x27];
        *(int32_t *)(void *)(v1_10 + 0x20) = v0_12;
        *(int32_t *)(void *)(v1_10 + 0x1c) = a1_12;
        *(int32_t *)(void *)(v1_10 + 0x24) = result;
        arg1[0x27] = result + a1_12;
        arg1[0x26] = a0_14 + 1;
        BPF_KMSG("GetPixMapCfg mapUV size=%d final_count=%d final_bytes=%d", result, arg1[0x26], arg1[0x27]);
    }

    BPF_KMSG("GetPixMapCfg exit count=%d bytes=%d fourcc=0x%x", arg1[0x26], arg1[0x27], arg1[4]);
    return result;
}

int32_t PixMapBufPollInit(void *arg1, AL_TAllocator *arg2, int32_t *arg3)
{
    void *var_28 = &_gp;

    (void)var_28;

    if (AL_sBufPool_InitStructure(arg1, arg2, arg3[0], (void *)(intptr_t)arg3[0x27], 0) == 0) {
        fwrite("AL_sBufPool_InitStructure failed\n", 1, 0x21, stderr);
        return 0;
    }

    while (1) {
        if (read_u32(arg1, 0x08) >= read_u32(arg3, 0x00))
            return 1;

        {
            int32_t var_34;
            const char *var_30;
            AL_TBuffer *v0_2 = AL_PixMapBuffer_Create(arg2, FreePixMapBufInPool, arg3[2], arg3[3], (uint32_t)arg3[4]);

            if (v0_2 == 0) {
                fwrite("AL_PixMapBuffer_Create failed\n", 1, 0x1e, stderr);
                break;
            }

            var_30 = "PixMapBuf";
            var_34 = arg3[0x26];
            if (AL_PixMapBuffer_Allocate_And_AddPlanes(v0_2, arg3[0x27], read_u8(arg3, 0x14), 0, &arg3[6], var_34,
                                                       (int32_t)(intptr_t)var_30) == 0) {
                fwrite("AL_PixMapBuffer_Allocate_And_AddPlanes failed\n", 1, 0x2e, stderr);
                AL_Buffer_Destroy(v0_2);
                break;
            }

            if (AL_sBufPool_AddBuf(arg1, v0_2) == 0) {
                fprintf(stderr, "AL_sBufPool_AddBuf pPixMapBufPool %d failed\n", read_s32(arg1, 0x08));
                break;
            }
        }
    }

    AL_BufPool_Deinit(arg1);
    return 0;
}

int32_t GetStreamBufPoolConfig(int32_t *arg1, void *arg2, int32_t arg3, char arg4, double arg5, double arg6,
                               double arg7, int32_t arg8, int32_t arg9)
{
    void *var_38 = &_gp;
    int32_t stream_count = (int32_t)arg5 + arg8;
    int32_t v1_1;
    uint8_t *v0_4;
    int32_t a0_1;
    uint32_t s6;
    uint32_t s7;
    int32_t v0_5;
    int32_t v0_6;

    (void)var_38;

    if (stream_count <= 0)
        stream_count = (int32_t)read_u8(arg2, 0xae) + arg8;
    if (stream_count <= 0)
        stream_count = 1;

    v1_1 = read_s32(arg2, 0x10);
    v0_4 = (uint8_t *)arg2 + (arg3 * 0xf0);
    a0_1 = (v1_1 >> 4) & 0xf;
    s6 = read_u16(v0_4, 0x06);
    s7 = read_u16(v0_4, 0x04);
    v0_5 = v1_1 & 0xf;
    if (a0_1 >= v0_5)
        v0_5 = a0_1;

    v0_6 = AL_GetMitigatedMaxNalSize((int32_t)s7, (int32_t)s6, (v1_1 >> 8) & 0xf, v0_5);

    {
        uint32_t pixels = s7 * s6;
        int32_t min_size = 0x10000;
        int32_t v1_4;
        int32_t s0_2;
        uint32_t v1_7 = 1;
        int32_t s0_6;

        if (pixels >= 1280U * 720U)
            min_size = 0x20000;
        if (pixels >= 1920U * 1080U)
            min_size = 0x40000;

        v1_4 = (v0_6 >> 3) + 0x200 + ((((int32_t)s6 + 0xf) >> 4) << 9);
        if (v1_4 >= v0_6)
            v0_6 = v1_4;

        s0_2 = v0_6;
        if (read_u8(arg2, 0xc4) != 0) {
            uint32_t a0_9 = read_u8(arg2, 0x40);
            int32_t lo_1 = v0_6 / (int32_t)a0_9;

            if (a0_9 == 0)
                __builtin_trap();

            stream_count *= (int32_t)a0_9;
            if (lo_1 + 0x2000 < 0) {
                s0_2 = ((lo_1 + 0x201f) >> 5) << 5;
            } else {
                int32_t v0_8 = lo_1 + 0x203e;

                if (lo_1 + 0x201f >= 0)
                    v0_8 = lo_1 + 0x201f;

                s0_2 = (v0_8 >> 5) << 5;
            }
        }

        if ((int32_t)pixels >= 0x1fe000)
            v1_7 = (uint32_t)(int32_t)log10((double)pixels);

        s0_6 = ((int32_t)v1_7 * s0_2 + 0x1f) >> 5 << 5;
        if (s0_6 < min_size)
            s0_6 = min_size;
        if ((uint32_t)arg9 >= (uint32_t)s0_6 || arg9 != 0)
            s0_6 = arg9;

        BPF_KMSG("GetStreamCfg count=%d size=%d w=%u h=%u mitigated=%d min=%d slice_split=%u",
                 stream_count, s0_6, s7, s6, v0_6, min_size, read_u8(arg2, 0xc4));
        GetBufPoolConfig(arg1, (int32_t)(intptr_t)"stream",
                         (int32_t)(intptr_t)((void *(*)(int32_t))(void *)AL_StreamMetaData_Create)(0x194), s0_6,
                         stream_count, arg4);
    }

    return (int32_t)(intptr_t)arg1;
}

uint32_t *GetFrameInfo(uint32_t *arg1, void *arg2)
{
    uint32_t a3 = (uint32_t)*(const uint16_t *)((const uint8_t *)arg2 + 0x0a);
    char a2 = *(const char *)((const uint8_t *)arg2 + 0x18);
    int32_t v1 = (read_s32(arg2, 0x10) >> 8) & 0xf;

    arg1[0] = (uint32_t)*(const uint16_t *)((const uint8_t *)arg2 + 0x08);
    arg1[1] = a3;
    *((uint8_t *)arg1 + 8) = (uint8_t)a2;
    arg1[3] = (uint32_t)v1;
    return arg1;
}
