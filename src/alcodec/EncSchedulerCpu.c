#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "alcodec/al_allocator.h"
#include "alcodec/al_buffer.h"
#include "alcodec/al_fourcc.h"
#include "alcodec/al_rtos.h"
#include "alcodec/al_types.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, ...);

#define SCH_KMSG(fmt, ...) do { \
    int _kfd = open("/dev/kmsg", O_WRONLY); \
    if (_kfd >= 0) { \
        char _b[192]; \
        int _n = snprintf(_b, sizeof(_b), "libimp/SCH: " fmt "\n", ##__VA_ARGS__); \
        if (_n > 0) { write(_kfd, _b, _n > (int)sizeof(_b) ? (int)sizeof(_b) : _n); } \
        close(_kfd); \
    } \
} while (0)

#define READ_U8(base, off) (*(uint8_t *)((uint8_t *)(base) + (off)))
#define READ_S32(base, off) (*(int32_t *)((uint8_t *)(base) + (off)))
#define READ_PTR(base, off) (*(void **)((uint8_t *)(base) + (off)))
#define WRITE_S32(base, off, val) (*(int32_t *)((uint8_t *)(base) + (off)) = (int32_t)(val))
#define WRITE_PTR(base, off, val) (*(void **)((uint8_t *)(base) + (off)) = (void *)(val))

AL_TBuffer *AL_PixMapBuffer_Create(AL_TAllocator *arg1, void *arg2, int32_t arg3, int32_t arg4,
                                   uint32_t arg5); /* forward decl, ported by T<N> later */
int32_t AL_PixMapBuffer_AddPlanes(AL_TBuffer *arg1, void *arg2, int32_t arg3, char arg4, uint32_t arg5,
                                  int32_t *arg6, int32_t arg7); /* forward decl, ported by T<N> later */
uint32_t AL_Buffer_GetPhysicalAddress(AL_TBuffer *buffer); /* forward decl, ported by T<N> later */
int32_t AL_EncRecBuffer_FillPlaneDesc(int32_t *arg1, int32_t arg2, int32_t arg3, char arg4,
                                      char arg5); /* forward decl, ported by T<N> later */
uint32_t AL_GetAllocSize_EncReference(int32_t arg1, int32_t arg2, char arg3, int32_t arg4,
                                      char arg5); /* forward decl, ported by T<N> later */
AL_TPicFormat *AL_EncGetRecPicFormat(AL_TPicFormat *pPicFormat, int32_t eChromaMode, uint8_t uBitDepth,
                                     uint8_t bCompressed); /* forward decl, ported by T<N> later */
AL_TAllocator *AL_DmaAlloc_GetAllocator(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t AllocatorPoolGetPoolId(AL_TAllocator *arg1); /* forward decl, ported by T<N> later */
int32_t AL_SchedulerEnc_SetTraceCallBack(int32_t *arg1, char arg2, int32_t arg3,
                                         int32_t arg4); /* forward decl, ported by T<N> later */
int32_t AL_SchedulerEnc_ReleaseRecPicture(int32_t *arg1, char arg2, char arg3); /* forward decl */
int32_t AL_SchedulerEnc_GetRecPicture(int32_t *arg1, char arg2, int32_t *arg3); /* forward decl */
int32_t AL_SchedulerEnc_PutStreamBuffer(int32_t *arg1, char arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                        int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9); /* forward decl */
int32_t AL_SchedulerEnc_EncodeOneFrame(int32_t *arg1, char arg2, int32_t *arg3, int32_t *arg4,
                                       int32_t *arg5); /* forward decl */
void AL_SchedulerEnc_DeInit(int32_t *arg1); /* forward decl */
int32_t AL_SchedulerEnc_CreateChannel2(int32_t *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                       int32_t *arg6, int32_t arg7); /* forward decl */
int32_t *AL_SchedulerEnc_GetChannelBufResources(int32_t *arg1, int32_t *arg2, char arg3); /* forward decl */
int32_t AL_SchedulerEnc_PutIntermBuffer(int32_t *arg1, char arg2, int32_t arg3, int32_t arg4); /* forward decl */
int32_t AL_SchedulerEnc_PutRefBuffer(int32_t *arg1, char arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                     int32_t arg6); /* forward decl */
int32_t AL_SchedulerEnc_Init(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4, int32_t arg5,
                             int32_t arg6); /* forward decl */
int32_t AL_SchedulerEnc_DestroyChannel(int32_t *arg1, char arg2, void *arg3, void *arg4); /* forward decl */

int32_t AL_SchedulerCpu_SetTraceCallBack(int32_t arg1, int32_t *arg2);
int32_t AL_SchedulerCpu_ReleaseRecPicture(int32_t arg1, int32_t *arg2, void *arg3);
int32_t AL_SchedulerCpu_GetRecPicture(void *arg1, int32_t *arg2, int32_t *arg3);
int32_t AL_SchedulerCpu_PutStreamBuffer(int32_t arg1, int32_t *arg2, AL_TBuffer *arg3, int32_t arg4, int32_t arg5,
                                        int32_t arg6);
int32_t AL_SchedulerCpu_EncodeOneFrame(int32_t arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4, int32_t *arg5);
int32_t AL_SchedulerCpu_DestroyChannel(void *arg1, int32_t *arg2);
int32_t AL_SchedulerCpu_Deinit(int32_t arg1);
int32_t AL_SchedulerCpu_CreateChannel(int32_t **arg1, int32_t *arg2, void *arg3, int32_t *arg4, int32_t *arg5);
int32_t *AL_SchedulerCpu_Create(int32_t arg1, int32_t *arg2);
int32_t AL_SchedulerCpu_Destroy(int32_t arg1);

static int32_t AL_SchedulerCpu_DestroyChannelCB(void *arg1);

static void *CpuEncSchedulerVtable[] = {
    AL_SchedulerCpu_Deinit,
    AL_SchedulerCpu_CreateChannel,
    AL_SchedulerCpu_DestroyChannel,
    AL_SchedulerCpu_EncodeOneFrame,
    AL_SchedulerCpu_PutStreamBuffer,
    AL_SchedulerCpu_GetRecPicture,
    AL_SchedulerCpu_ReleaseRecPicture,
    AL_SchedulerCpu_SetTraceCallBack,
};

int32_t AL_IEncScheduler_Destroy(int32_t *arg1)
{
    return ((int32_t (*)(int32_t *))(intptr_t)(*(int32_t *)(intptr_t)*arg1))(arg1);
}

int32_t SetChannelInfo(int32_t *arg1, void *arg2)
{
    int32_t v0 = READ_S32(arg2, 0x10);
    int32_t a3 = v0 & 0xf;
    int32_t s1 = (int32_t)((uint32_t)v0 >> 4) & 0xf;
    void *var_40 = &_gp;
    int32_t s0;
    int32_t s2_1;
    AL_TPicFormat var_38;
    uint32_t result;

    (void)var_40;
    if (s1 < a3)
        s1 = a3;
    s0 = (int32_t)((uint32_t)v0 >> 8) & 0xf;
    s2_1 = ((uint32_t)READ_S32(arg2, 0x2c) >> 5) & 1;
    ((uint8_t *)arg1)[2] = (uint8_t)((READ_U8(arg2, 0x1f) < 1U) ? 1 : 0);
    arg1[0] = (int32_t)AL_GetAllocSize_EncReference((int32_t)READ_U8(arg2, 4), (int32_t)READ_U8(arg2, 6),
                                                    (char)s1, s0, (char)s2_1);
    AL_EncGetRecPicFormat(&var_38, s0, (uint8_t)s1, (uint8_t)s2_1);
    result = AL_GetFourCC(var_38);
    arg1[1] = (int32_t)result;
    return (int32_t)result;
}

int32_t SetRecPic(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t *arg4, int32_t *arg5)
{
    void *var_78 = &_gp;
    int32_t v0_1;
    int32_t a0_1;
    int32_t var_70[0x10];
    int32_t *var_30 = var_70;
    int32_t *s1;
    int32_t v0_2;
    int32_t s4;
    int32_t s0;
    int32_t v0_3;
    uint32_t v0_5;
    int32_t a1;
    int32_t a2_1;
    int32_t a2_3;
    int32_t a1_2;
    int32_t a0_6;
    int32_t result;

    (void)var_78;
    v0_1 = (int32_t)(intptr_t)AL_PixMapBuffer_Create((AL_TAllocator *)(intptr_t)arg2, NULL, arg5[3], arg5[4],
                                                     (uint32_t)arg4[1]);
    a0_1 = arg4[1];
    *arg1 = v0_1;
    s1 = var_70;
    v0_2 = AL_GetChromaMode((uint32_t)a0_1);
    s4 = 2;
    s0 = 0;
    v0_3 = (int32_t)AL_GetBitDepth((uint32_t)arg4[1]);
    if (AL_IsCompressed((uint32_t)arg4[1]) != 0)
        s4 = 4;
    v0_5 = (uint32_t)(uint8_t)((uint8_t *)arg4)[8];
    a1 = arg5[3];
    a2_1 = arg5[4];
    do {
        *s1 = s0;
        s0 += 1;
        AL_EncRecBuffer_FillPlaneDesc(s1, a1, a2_1, (char)v0_2, (char)v0_3);
        s1 = &s1[4];
    } while (s4 != s0);
    AL_PixMapBuffer_AddPlanes((AL_TBuffer *)(intptr_t)*arg1, (void *)(intptr_t)arg3, arg4[0], 0, 0, var_30, s4);
    a2_3 = arg5[1];
    a1_2 = arg5[2];
    a0_6 = arg5[3];
    result = arg5[4];
    arg1[1] = arg5[0];
    arg1[2] = a2_3;
    arg1[3] = a1_2;
    arg1[4] = a0_6;
    arg1[5] = result;
    return result;
}

int32_t allocateBuffers(void *arg1, AL_TAllocator *arg2, int32_t arg3, int32_t arg4, char *arg5)
{
    if (arg3 > 0) {
        int32_t s0_1 = 0;

        do {
            const AL_TAllocatorVtable *v0_2 = arg2->vtable;
            void *v0_1;

            SCH_KMSG("allocBuffers[%s] iter=%d/%d size=%d next_slot=%d", arg5, s0_1 + 1, arg3, arg4,
                     READ_S32(arg1, 0x50));

            if (v0_2->AllocNamed != NULL) {
                s0_1 += 1;
                v0_1 = ((void *(*)(AL_TAllocator *, int32_t, char *))(intptr_t)v0_2->AllocNamed)(arg2, arg4, arg5);
                SCH_KMSG("allocBuffers[%s] AllocNamed -> %p", arg5, v0_1);
                if (v0_1 == NULL)
                    return 0;
            } else {
                s0_1 += 1;
                v0_1 = ((void *(*)(AL_TAllocator *, int32_t, char *))(intptr_t)v0_2->Alloc)(arg2, arg4, arg5);
                SCH_KMSG("allocBuffers[%s] Alloc -> %p", arg5, v0_1);
                if (v0_1 == NULL)
                    return 0;
            }

            {
                int32_t v1_1 = READ_S32(arg1, 0x50);

                WRITE_S32(arg1, 0x50, v1_1 + 1);
                *(int32_t *)((uint8_t *)arg1 + ((uint32_t)v1_1 << 2)) = (int32_t)(intptr_t)v0_1;
                SCH_KMSG("allocBuffers[%s] stored slot=%d ptr=%p new_count=%d", arg5, v1_1, v0_1, v1_1 + 1);
            }
        } while (arg3 != s0_1);
    }

    SCH_KMSG("allocBuffers[%s] complete count=%d", arg5, arg3);
    return 1;
}

int32_t AL_SchedulerCpu_SetTraceCallBack(int32_t arg1, int32_t *arg2)
{
    if (arg2 == NULL)
        return 0;

    {
        void *var_10 = &_gp;

        (void)var_10;
        AL_SchedulerEnc_SetTraceCallBack((int32_t *)(intptr_t)(arg1 + 4), (char)arg2[0], arg2[1], arg2[2]);
        return 1;
    }
}

int32_t AL_SchedulerCpu_ReleaseRecPicture(int32_t arg1, int32_t *arg2, void *arg3)
{
    if (arg2 == NULL)
        return 0;

    {
        void *var_10 = &_gp;

        (void)var_10;
        AL_SchedulerEnc_ReleaseRecPicture((int32_t *)(intptr_t)(arg1 + 4), (char)*arg2, (char)READ_S32(arg3, 4));
        return 1;
    }
}

int32_t AL_SchedulerCpu_GetRecPicture(void *arg1, int32_t *arg2, int32_t *arg3)
{
    if (arg2 == NULL)
        return 0;

    {
        void *var_40 = &_gp;
        int32_t var_38[5];
        int32_t result;

        (void)var_40;
        result = AL_SchedulerEnc_GetRecPicture((int32_t *)((uint8_t *)arg1 + 4), (char)*arg2, var_38);
        if (result == 0)
            return 0;

        SetRecPic(arg3, READ_S32(arg1, 0x12f0),
                  *(int32_t *)((uint8_t *)arg1 + (((*arg2 * 0x15) + var_38[0] + 0x4c0) << 2)),
                  &arg2[5], var_38);
        return result;
    }
}

int32_t AL_SchedulerCpu_PutStreamBuffer(int32_t arg1, int32_t *arg2, AL_TBuffer *arg3, int32_t arg4, int32_t arg5,
                                        int32_t arg6)
{
    void *var_20 = &_gp;
    void *v0;
    int32_t s4;
    uint32_t phys;
    void *virt;
    uint32_t size;

    (void)var_20;
    SCH_KMSG("Cpu.PutStreamBuffer entry sched=%p chctx=%p buf=%p arg4=%d arg5=%d arg6=%d ch_id=%u",
             (void *)(intptr_t)arg1, arg2, arg3, arg4, arg5, arg6, arg2 ? (unsigned)(uint8_t)*arg2 : 0);
    v0 = AL_Buffer_GetMetaData(arg3, 7);
    SCH_KMSG("Cpu.PutStreamBuffer meta7=%p", v0);
    if (v0 == NULL) {
        s4 = 0;
    } else {
        s4 = (int32_t)(intptr_t)AL_Buffer_GetData(*(AL_TBuffer **)((uint8_t *)v0 + 0x38));
        SCH_KMSG("Cpu.PutStreamBuffer meta7 payload=%p", (void *)(intptr_t)s4);
    }

    phys = AL_Buffer_GetPhysicalAddress(arg3);
    virt = AL_Buffer_GetData(arg3);
    size = AL_Buffer_GetSize(arg3);
    SCH_KMSG("Cpu.PutStreamBuffer phys=0x%x virt=%p size=%u side=%p",
             (unsigned)phys, virt, (unsigned)size, (void *)(intptr_t)s4);
    return AL_SchedulerEnc_PutStreamBuffer((int32_t *)(intptr_t)(arg1 + 4), (char)*arg2,
                                           (int32_t)phys, (int32_t)(intptr_t)virt, (int32_t)size,
                                           0, 0, 0, s4);
}

int32_t AL_SchedulerCpu_EncodeOneFrame(int32_t arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4, int32_t *arg5)
{
    if (arg2 == NULL)
        return 0;

    {
        void *var_10 = &_gp;

        (void)var_10;
        SCH_KMSG("Cpu.EncodeOneFrame entry cpu=%p chctx=%p ch=%u src=%p opts=%p meta=%p",
                 (void *)(intptr_t)arg1, arg2, (unsigned)(uint8_t)*arg2, arg3, arg4, arg5);
        if (arg3 != NULL || arg4 != NULL) {
            SCH_KMSG("Cpu.EncodeOneFrame payload src[0..3]=0x%x 0x%x 0x%x 0x%x opts[0..3]=0x%x 0x%x 0x%x 0x%x",
                     arg3 ? arg3[0] : 0, arg3 ? arg3[1] : 0, arg3 ? arg3[2] : 0, arg3 ? arg3[3] : 0,
                     arg4 ? arg4[0] : 0, arg4 ? arg4[1] : 0, arg4 ? arg4[2] : 0, arg4 ? arg4[3] : 0);
        } else {
            SCH_KMSG("Cpu.EncodeOneFrame arm-only call ch=%u", (unsigned)(uint8_t)*arg2);
        }
        AL_SchedulerEnc_EncodeOneFrame((int32_t *)(intptr_t)(arg1 + 4), (char)*arg2, arg3, arg4, arg5);
        SCH_KMSG("Cpu.EncodeOneFrame exit meta=%p", arg5);
        return 1;
    }
}

int32_t EndEncodingCallBack(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    (void)arg2;
    SCH_KMSG("EndEncodingCallBack entry ctx=%p fn=0x%x user=0x%x a3=%d a4=%d a5=%d",
             arg1, READ_S32(arg1, 0xc), READ_S32(arg1, 0x10), arg3, arg4, arg5);
    Rtos_GetMutex(READ_PTR(arg1, 8));
    {
        int32_t t9 = READ_S32(arg1, 0xc);

        if (t9 != 0)
            ((void (*)(int32_t, int32_t, int32_t, int32_t, void *))(intptr_t)t9)(READ_S32(arg1, 0x10), arg3, arg4,
                                                                                   arg5, &_gp);
    }
    SCH_KMSG("EndEncodingCallBack exit ctx=%p", arg1);
    return Rtos_ReleaseMutex(READ_PTR(arg1, 8));
}

static int32_t AL_SchedulerCpu_DestroyChannelCB(void *arg1)
{
    return Rtos_SetEvent(arg1);
}

int32_t AL_SchedulerCpu_FreeChannelBuffers2(void *arg1, int32_t arg2, int32_t arg3)
{
    int32_t s4 = arg2 << 4;
    int32_t s3 = arg2 << 2;
    int32_t s0_1 = (s4 - s3) * 7;
    void *s6 = (uint8_t *)arg1 + s0_1;
    void *var_28 = &_gp;

    (void)var_28;
    if (READ_S32(s6, 0x1350) > 0) {
        int32_t *s0_3 = (int32_t *)((uint8_t *)arg1 + s0_1 + 0x1300);
        int32_t i = 0;

        do {
            AL_TAllocator *v0_2 = AL_DmaAlloc_GetAllocator(arg3);
            int32_t a1 = *s0_3;

            i += 1;
            s0_3 = &s0_3[1];
            ((int32_t (*)(AL_TAllocator *, int32_t))(intptr_t)v0_2->vtable->Free)(v0_2, a1);
        } while (i < READ_S32(s6, 0x1350));
    }

    {
        int32_t s3_1 = s4 - s3;
        int32_t result = s3_1 << 3;

        WRITE_S32(arg1, result - s3_1 + 0x1350, 0);
        return result;
    }
}

int32_t AL_SchedulerCpu_DestroyChannel(void *arg1, int32_t *arg2)
{
    int32_t result;

    if (arg2 == NULL)
        return 0;

    AL_SchedulerEnc_DestroyChannel((int32_t *)((uint8_t *)arg1 + 4), (char)*arg2, AL_SchedulerCpu_DestroyChannelCB,
                                   (void *)(intptr_t)arg2[1]);
    result = Rtos_WaitEvent((void *)(intptr_t)arg2[1], 0xffffffff);
    Rtos_GetMutex((void *)(intptr_t)arg2[2]);
    Rtos_ReleaseMutex((void *)(intptr_t)arg2[2]);
    arg2[3] = 0;
    Rtos_DeleteEvent((void *)(intptr_t)arg2[1]);
    Rtos_DeleteMutex((void *)(intptr_t)arg2[2]);
    AL_SchedulerCpu_FreeChannelBuffers2(arg1, *arg2, arg2[8]);
    Rtos_Free(arg2);
    return result;
}

int32_t AL_SchedulerCpu_Deinit(int32_t arg1)
{
    void *var_10 = &_gp;

    (void)var_10;
    AL_SchedulerEnc_DeInit((int32_t *)(intptr_t)(arg1 + 4));
    Rtos_Free((void *)(intptr_t)arg1);
    return 0;
}

int32_t AL_SchedulerCpu_CreateChannel(int32_t **arg1, int32_t *arg2, void *arg3, int32_t *arg4, int32_t *arg5)
{
    void *var_60 = &_gp;
    int32_t s4;
    int32_t *v0;
    int32_t *s6_1;
    int32_t a2;
    int32_t var_40;

    (void)var_60;
    var_40 = 0;
    SCH_KMSG("SchCpu.CC ENTRY arg2=%p arg3=%p arg4=%p arg5=%p", (void*)arg2, (void*)arg3, (void*)arg4, (void*)arg5);
    s4 = *(int32_t *)arg3;
    v0 = (int32_t *)Rtos_Malloc(0x24);
    SCH_KMSG("SchCpu.CC s4=0x%x v0(obj)=%p pool_arg=%p", s4, (void*)v0, (void*)((int32_t *)arg3)[3]);
    v0[8] = AllocatorPoolGetPoolId((AL_TAllocator *)((int32_t *)arg3)[3]);
    v0[1] = (int32_t)(intptr_t)Rtos_CreateEvent(0);
    if (v0[1] == 0) {
        SCH_KMSG("SchCpu.CC Rtos_CreateEvent=0 -> fail_free_obj");
        var_40 = 0x87;
        goto fail_free_obj;
    }

    v0[2] = (int32_t)(intptr_t)Rtos_CreateMutex();
    if (v0[2] == 0) {
        SCH_KMSG("SchCpu.CC Rtos_CreateMutex=0 -> fail_delete_event");
        var_40 = 0x87;
        goto fail_delete_event;
    }

    s6_1 = ((int32_t **)arg3)[3];
    a2 = *arg4;
    v0[4] = arg5[1];
    v0[3] = *arg5;
    {
        int32_t var_44_1 = (int32_t)(intptr_t)v0;
        int32_t (*var_48_1)(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5) =
            EndEncodingCallBack;
        int32_t v0_4;

        (void)var_44_1;
        (void)var_48_1;
        SCH_KMSG("SchCpu.CC pre-SchEnc.CC2 schEnc=%p s4=0x%x a2=0x%x s6=%p v0=%p",
                 (void*)(arg2 + 1), s4, a2, (void*)s6_1, (void*)v0);
        v0_4 = AL_SchedulerEnc_CreateChannel2(arg2 + 1, (void *)(intptr_t)s4, a2, (int32_t)EndEncodingCallBack,
                                              (int32_t)(intptr_t)v0, &var_40, (int32_t)(intptr_t)s6_1);
        SCH_KMSG("SchCpu.CC post-SchEnc.CC2 v0_4=0x%x var_40=0x%x", v0_4, var_40);
        v0[0] = v0_4;
        if (v0_4 != 0xff) {
            int32_t var_58[4];
            int32_t s7_1;
            int32_t v0_7;
            int32_t *s3_2;
            int32_t var_54;
            int32_t (*var_38_1)(void *arg1, AL_TAllocator *arg2, int32_t arg3, int32_t arg4, char *arg5) =
                allocateBuffers;
            char *var_70_2;
            int32_t var_50;
            int32_t var_4c;

            SCH_KMSG("SchCpu.CC pre-GetBufRes chan=%d", v0_4);
            AL_SchedulerEnc_GetChannelBufResources(var_58, arg2 + 1, (char)v0_4);
            s7_1 = var_58[0];
            var_54 = var_58[1];
            var_50 = var_58[2];
            var_4c = var_58[3];
            SCH_KMSG("SchCpu.CC bufRes s7=%d var54=%d var50=%d var4c=%d", s7_1, var_54, var_50, var_4c);
            v0_7 = v0[0] * 0x54;
            WRITE_S32(arg2, v0_7 + 0x1350, 0);
            s3_2 = arg2 + v0_7 + 0x4c0;
            SCH_KMSG("SchCpu.CC pre-alloc(ref+rec+mv) s3=%p", (void*)s3_2);
            if (allocateBuffers(s3_2, (AL_TAllocator *)s6_1, s7_1, var_54, "ref + rec + mv") != 0) {
                var_70_2 = "interm";
                SCH_KMSG("SchCpu.CC pre-alloc(interm)");
                if (var_38_1(s3_2, (AL_TAllocator *)s6_1, var_50, var_4c, "interm") != 0) {
                    SCH_KMSG("SchCpu.CC post-alloc(interm)");
                    int32_t v1_6 = v0[0];
                    int32_t v0_14 = var_50 + s7_1;
                    AL_TAllocator *s6_2 = (AL_TAllocator *)READ_S32(arg2, 0x12f0);
                    int32_t s2_1 = 0;

                    if (v0_14 > 0) {
                        do {
                            int32_t fp_2 = *s3_2;
                            SCH_KMSG("SchCpu.CC buf-loop idx=%d/%d raw=%p", s2_1, v0_14, (void *)(intptr_t)fp_2);
                            int32_t v0_16 =
                                (int32_t)s6_2->vtable->GetPhysicalAddr(s6_2, (void *)(intptr_t)fp_2);
                            int32_t v0_17 = (int32_t)s6_2->vtable->GetVirtualAddr(s6_2, (void *)(intptr_t)fp_2);
                            SCH_KMSG("SchCpu.CC buf-loop idx=%d phys=0x%x virt=0x%x", s2_1, v0_16, v0_17);

                            if (s2_1 >= s7_1) {
                                SCH_KMSG("SchCpu.CC pre-PutInterm idx=%d chan=%d", s2_1, v1_6);
                                AL_SchedulerEnc_PutIntermBuffer(arg2 + 1, (char)v1_6, v0_16, v0_17);
                                SCH_KMSG("SchCpu.CC post-PutInterm idx=%d chan=%d", s2_1, v1_6);
                            } else {
                                int32_t var_68_2 = fp_2;
                                int32_t var_64_1 = 0;

                                (void)var_68_2;
                                (void)var_64_1;
                                var_70_2 = (char *)(intptr_t)var_54;
                                SCH_KMSG("SchCpu.CC pre-PutRef idx=%d chan=%d stride=%d", s2_1, v1_6, var_54);
                                AL_SchedulerEnc_PutRefBuffer(arg2 + 1, (char)v1_6, v0_16, v0_17, (int32_t)var_70_2,
                                                             0);
                                SCH_KMSG("SchCpu.CC post-PutRef idx=%d chan=%d", s2_1, v1_6);
                            }

                            s2_1 += 1;
                            s3_2 = &s3_2[1];
                        } while (s2_1 != v0_14);
                    }

                    SCH_KMSG("SchCpu.CC pre-SetChannelInfo v0[5]=%p s4=0x%x", (void*)&v0[5], s4);
                    SetChannelInfo(&v0[5], (void *)(intptr_t)s4);
                    *arg1 = v0;
                    SCH_KMSG("SchCpu.CC SUCCESS return var_40=0x%x", var_40);
                    return var_40;
                }
            }

            SCH_KMSG("SchCpu.CC alloc failed -> FreeChannelBuffers2");
            {
                int32_t a1_3 = v0[0];
                int32_t a2_3 = v0[8];

                AL_SchedulerCpu_FreeChannelBuffers2(arg2, a1_3, a2_3);
                var_40 = 0x87;
            }
        } else if (var_40 == 0) {
            SCH_KMSG("SchCpu.CC CC2 returned 0xff and var_40=0 -> __assert");
            __assert("!AL_IS_SUCCESS_CODE(errorCode)",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/EncSchedulerCpu.c",
                     0x9f, "AL_SchedulerCpu_CreateChannel");
            var_40 = 0x87;
        } else {
            SCH_KMSG("SchCpu.CC CC2 returned 0xff var_40=0x%x -> error path", var_40);
        }
    }

    Rtos_DeleteMutex((void *)(intptr_t)v0[2]);
fail_delete_event:
    Rtos_DeleteEvent((void *)(intptr_t)v0[1]);
fail_free_obj:
    Rtos_Free(v0);
    *arg1 = NULL;
    return var_40;
}

int32_t *AL_SchedulerCpu_Create(int32_t arg1, int32_t *arg2)
{
    int32_t *result = (int32_t *)Rtos_Malloc(0x1d80);

    if (result == NULL)
        return NULL;

    result[0] = (int32_t)(intptr_t)CpuEncSchedulerVtable;
    Rtos_Memset(result + 0x4c0, 0, 0xa80);
    /* The decompiler shows a literal `1` here, but the live scheduler state
     * demonstrates that a single instantiated core cannot satisfy the stock
     * resource model for 1080p25 AVC on this AVPU: expected cores=5,
     * per-core budget=211764, requested budget=1000000. The board is also
     * created with `5` AVPU IRQ slots. Use 5 scheduler cores so the compact
     * core table and budget accounting reflect the hardware topology. */
    if (AL_SchedulerEnc_Init(result + 1, (int32_t *)AL_GetDefaultAllocator(), arg2, (int32_t *)(intptr_t)arg1, 5,
                             0x2faf0800) >= 0) {
        return result;
    }

    Rtos_Free(result);
    return NULL;
}

int32_t AL_SchedulerCpu_Destroy(int32_t arg1)
{
    void *var_10 = &_gp;

    (void)var_10;
    AL_SchedulerEnc_DeInit((int32_t *)(intptr_t)(arg1 + 4));
    Rtos_Free((void *)(intptr_t)arg1);
    return 0;
}
