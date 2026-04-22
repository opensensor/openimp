#include <stdint.h>
#include <string.h>

#include "alcodec/al_rtos.h"
#include "imp_log_int.h"

extern char _gp;
extern void *__assert(const char *expression, const char *file, int32_t line, const char *function, ...);

uint32_t AL_GetAllocSize_EncReference(int32_t arg1, int32_t arg2, char arg3, int32_t arg4,
                                      char arg5); /* forward decl, ported by T<N> later */
int32_t AL_GetAllocSize_MV(int32_t arg1, int32_t arg2, char arg3,
                           int32_t arg4); /* forward decl, ported by T<N> later */
int32_t AL_EncRecBuffer_FillPlaneDesc(int32_t *arg1, int32_t arg2, int32_t arg3, char arg4,
                                      char arg5); /* forward decl, ported by T<N> later */
int32_t AL_DPB_Init(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6,
                    int32_t arg7, int32_t arg8, int32_t arg9); /* forward decl, ported by T<N> later */
int32_t AL_DPB_Deinit(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_DPB_PushPicture(void *arg1, void *arg2, uint32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_DPB_Flush(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
uint32_t AL_DPB_GetRefMode(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_DPB_AVC_GetRefInfo(char *arg1, void *arg2, void *arg3, int32_t *arg4,
                              int32_t (*arg5)[0x20],
                              int32_t arg6); /* forward decl, ported by T<N> later */
int32_t AL_DPB_HEVC_GetRefInfo(char *arg1, void *arg2, void *arg3, int32_t *arg4,
                               uint8_t arg5); /* forward decl, ported by T<N> later */
uint32_t AL_DPB_GetRefFromPOC(uint8_t *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_DPB_Update(void *arg1, int32_t *arg2, uint32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_DPB_GetAvailRef(void *arg1, void *arg2, int32_t *arg3); /* forward decl, ported by T<N> later */

struct dpb_ref_list_desc {
    uint16_t *kind;
    int32_t *list;
    int32_t count;
};

static int32_t AL_sRefMngr_IncrementBufID_part_0(void)
{
    __assert("iRefCount > 0",
             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/RefManager.c",
             0x3e, "AL_sRefMngr_IncrementBufID", &_gp);
    return 0;
}

uint32_t AL_sRefMngr_OutputBufPtr(void *arg1, char arg2, char arg3, int32_t arg4);
int32_t AL_sRefMngr_ReleaseBufId(void *arg1, int32_t arg2);
int32_t AL_sRefMngr_ReleaseBufPtr(void *arg1, char arg2);
int32_t AL_RefMngr_Init(void *arg1, int32_t *arg2, int32_t arg3, int32_t arg4);
int32_t AL_RefMngr_Deinit(void *arg1);
int32_t AL_RefMngr_MarkAsReadyForOutput(void *arg1, char arg2);
int32_t AL_RefMngr_GetMvBufAddr(void *arg1, char arg2, int32_t *arg3);
int32_t AL_RefMngr_GetColocRefPOC(int32_t arg1, void *arg2, void *arg3);
int32_t AL_RefMngr_GetRefBufferFromPOC(void *arg1, int32_t arg2);
int32_t AL_RefMngr_GetRefInfo(int32_t arg1, int32_t arg2, void *arg3, void *arg4, int32_t *arg5);

#define REFM_KMSG(fmt, ...) IMP_LOG_INFO("REFM", fmt, ##__VA_ARGS__)

int32_t AL_sRefMngr_IncrementBufPtr(void *arg1, char arg2)
{
    uint32_t a1 = (uint32_t)(uint8_t)arg2;
    int32_t result = 0xff;

    if (a1 != 0xffU) {
        result = Rtos_AtomicIncrement((int32_t *)((uint8_t *)arg1 + (((a1 + 0xbeU) << 3) + 8U)));

        if (result <= 0) {
            AL_sRefMngr_IncrementBufID_part_0();
            return AL_sRefMngr_OutputBufPtr(arg1, (char)a1, 0, 0);
        }
    }

    return result;
}

uint32_t AL_sRefMngr_OutputBufPtr(void *arg1, char arg2, char arg3, int32_t arg4)
{
    uint8_t *ctx = (uint8_t *)arg1;
    uint32_t result = (uint32_t)*(int32_t *)(ctx + 0x764U);

    if (result == 0U) {
        return result;
    }

    {
        uint32_t s3 = (uint32_t)(uint8_t)arg2;

        Rtos_GetMutex(*(void **)(ctx + 0x784U));

        if (*(int32_t *)(ctx + 0x760U) >= *(int32_t *)(ctx + 0x698U)) {
            __assert("pRefCtx->iNumRecOut < pRefCtx->iNumFrm",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/RefManager.c",
                     0x68, "AL_sRefMngr_OutputBufPtr", &_gp);
            AL_sRefMngr_IncrementBufID_part_0();
            return (uint32_t)AL_sRefMngr_ReleaseBufId(arg1, (int32_t)s3);
        }

        if (Rtos_AtomicIncrement((int32_t *)(ctx + (((s3 + 0xbeU) << 3) + 8U))) > 0) {
            int32_t a2 = *(int32_t *)(ctx + 0x760U);
            uint8_t *v0_9 = ctx + (((uint32_t)((a2 + *(int32_t *)(ctx + 0x75cU)) % 0x14)) << 3) + 0x6bcU;
            int32_t a0_8;

            v0_9[0] = (uint8_t)s3;
            v0_9[1] = (uint8_t)arg3;
            *(int32_t *)(v0_9 + 4) = arg4;
            a0_8 = *(int32_t *)(ctx + 0x784U);
            *(int32_t *)(ctx + 0x760U) = a2 + 1;
            return Rtos_ReleaseMutex((void *)(uintptr_t)a0_8);
        }

        AL_sRefMngr_IncrementBufID_part_0();
        return (uint32_t)AL_sRefMngr_ReleaseBufId(arg1, (int32_t)s3);
    }
}

int32_t AL_sRefMngr_ReleaseBufId(void *arg1, int32_t arg2)
{
    uint8_t *ctx = (uint8_t *)arg1;

    if (arg2 == 0xff) {
        return 0;
    }

    {
        int32_t v0 = Rtos_AtomicDecrement((int32_t *)(ctx + (((uint32_t)(arg2 + 0xbe) << 3) + 8U)));

        if (v0 < 0) {
            AL_sRefMngr_IncrementBufID_part_0();
            return AL_sRefMngr_ReleaseBufPtr(arg1, (char)arg2);
        }

        if (v0 != 0) {
            return 1;
        }

        Rtos_GetMutex(*(void **)(ctx + 0x784U));

        {
            int32_t a1 = *(int32_t *)(ctx + 0x6b0U);
            int32_t v1 = *(int32_t *)(ctx + 0x6b8U);
            int32_t hi = (*(int32_t *)(ctx + 0x6b4U) + a1) % v1;
            int32_t a0_5;

            if (v1 == 0) {
                __builtin_trap();
            }

            a0_5 = *(int32_t *)(ctx + 0x784U);
            *(int32_t *)(ctx + 0x6b0U) = a1 + 1;
            Rtos_ReleaseMutex((void *)(uintptr_t)a0_5);
            *(ctx + hi + 0x69cU) = (uint8_t)arg2;
            return 1;
        }
    }
}

int32_t AL_sRefMngr_ReleaseBufPtr(void *arg1, char arg2)
{
    return AL_sRefMngr_ReleaseBufId(arg1, (int32_t)(uint8_t)arg2);
}

int32_t AL_RefMngr_GetFrmBuffer(void *arg1, char arg2)
{
    uint32_t a1 = (uint32_t)(uint8_t)arg2;
    int32_t result = 0xff;

    if (a1 != 0xffU) {
        result = Rtos_AtomicIncrement((int32_t *)((uint8_t *)arg1 + (((a1 + 0xbeU) << 3) + 8U)));

        if (result <= 0) {
            AL_sRefMngr_IncrementBufID_part_0();
            return AL_RefMngr_Init(arg1, (int32_t *)(uintptr_t)a1, 0, 0);
        }
    }

    return result;
}

int32_t AL_RefMngr_Init(void *arg1, int32_t *arg2, int32_t arg3, int32_t arg4)
{
    uint8_t *ctx = (uint8_t *)arg1;

    if (arg1 == 0 || arg2 == 0) {
        __assert("pCtx && pChParam",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/RefManager.c",
                 0x7a, "AL_RefMngr_Init", &_gp);
        return AL_RefMngr_Deinit(arg1);
    }

    {
        int32_t v0 = arg2[4];
        int32_t a2 = ((uint32_t)v0 >> 4) & 0xf;
        int32_t v0_1 = v0 & 0xf;
        uint32_t t2;
        uint32_t t1;
        int32_t v1;
        int32_t t0_1;
        int32_t v1_1;
        uint8_t v0_3;
        int32_t v0_5;
        uint32_t a1_2;
        int32_t a0_3;
        int32_t v1_3;

        if (a2 < v0_1) {
            a2 = v0_1;
        }

        t2 = (uint32_t)(uint8_t)arg2[1];
        t1 = (uint32_t)(uint8_t)*(uint8_t *)((uint8_t *)arg2 + 6U);
        v1 = arg2[0xb];
        *(ctx + 0x774U) = (uint8_t)a2;
        t0_1 = ((uint32_t)v0 >> 8) & 0xf;
        v1_1 = ((uint32_t)v1 >> 5) & 1;
        v0_3 = ((uint32_t)(uint8_t)*(uint8_t *)((uint8_t *)arg2 + 0x1fU) < 1U) ? 1U : 0U;
        *(int32_t *)(ctx + 0x768U) = (int32_t)t2;
        *(int32_t *)(ctx + 0x76cU) = (int32_t)t1;
        *(int32_t *)(ctx + 0x770U) = t0_1;
        *(ctx + 0x775U) = (uint8_t)v1_1;
        *(ctx + 0x776U) = v0_3;
        *(int32_t *)(ctx + 0x77cU) =
            (int32_t)AL_GetAllocSize_EncReference((int32_t)t2, (int32_t)t1, (char)a2, t0_1, (char)v1_1);
        v0_5 = AL_GetAllocSize_MV(*(int32_t *)(ctx + 0x768U), *(int32_t *)(ctx + 0x76cU),
                                  *(uint8_t *)((uint8_t *)arg2 + 0x4eU),
                                  *(uint8_t *)((uint8_t *)arg2 + 0x1fU));
        a1_2 = (uint32_t)(uint8_t)*(uint8_t *)((uint8_t *)arg2 + 0x1fU);
        a0_3 = arg2[0];
        v1_3 = *(int32_t *)(ctx + 0x77cU) + v0_5;
        *(int32_t *)(ctx + 0x780U) = v0_5;
        *(int32_t *)(ctx + 0x778U) = v1_3;
        *(int32_t *)(ctx + 0x698U) = 0;
        *(int32_t *)(ctx + 0x6b0U) = 0;
        *(int32_t *)(ctx + 0x6b4U) = 0;
        *(int32_t *)(ctx + 0x6b8U) = 0;
        *(int32_t *)(ctx + 0x75cU) = 0;
        *(int32_t *)(ctx + 0x760U) = 0;
        *(int32_t *)(ctx + 0x764U) = 0;
        AL_DPB_Init(arg1, (int32_t)a1_2, arg4, arg3, a0_3, (int32_t)(uintptr_t)arg1,
                    (int32_t)(uintptr_t)AL_sRefMngr_IncrementBufPtr,
                    (int32_t)(uintptr_t)AL_sRefMngr_ReleaseBufPtr,
                    (int32_t)(uintptr_t)AL_sRefMngr_OutputBufPtr);
        *(void **)(ctx + 0x784U) = Rtos_CreateMutex();
        return 1;
    }
}

int32_t AL_RefMngr_Deinit(void *arg1)
{
    uint8_t *ctx = (uint8_t *)arg1;
    int32_t a0;

    AL_DPB_Deinit(arg1);
    a0 = *(int32_t *)(ctx + 0x784U);
    *(int32_t *)(ctx + 0x698U) = 0;
    *(int32_t *)(ctx + 0x6b0U) = 0;
    *(int32_t *)(ctx + 0x6b4U) = 0;
    *(int32_t *)(ctx + 0x6b8U) = 0;
    Rtos_DeleteMutex((void *)(uintptr_t)a0);
    return 0;
}

int32_t AL_RefMngr_GetBufferSize(void *arg1)
{
    return *(int32_t *)((uint8_t *)arg1 + 0x778U);
}

int32_t AL_RefMngr_GetMVBufferSize(void *arg1)
{
    return *(int32_t *)((uint8_t *)arg1 + 0x780U);
}

int32_t AL_RefMngr_EnableRecOut(void *arg1)
{
    *(int32_t *)((uint8_t *)arg1 + 0x764U) = 1;
    return 1;
}

int32_t AL_RefMngr_PushBuffer(void *arg1, int32_t arg2, int32_t arg3, uint32_t arg4, int32_t arg5,
                              int32_t arg6)
{
    uint8_t *ctx = (uint8_t *)arg1;

    Rtos_GetMutex(*(void **)(ctx + 0x784U));

    {
        int32_t v0 = *(int32_t *)(ctx + 0x698U);
        int32_t result;

        if (v0 == 0x14) {
            result = 0;
        } else if (arg4 < (uint32_t)*(int32_t *)(ctx + 0x778U)) {
            result = 0;
        } else {
            int32_t a0_1 = v0 << 3;
            uint8_t *v1_4 = ctx + ((uint32_t)v0 << 5) - (uint32_t)a0_1;
            uint8_t *a0_2 = ctx + a0_1;
            int32_t v1_5;

            *(int32_t *)(v1_4 + 0x418U) = arg2;
            *(int32_t *)(v1_4 + 0x41cU) = arg3;
            *(int32_t *)(v1_4 + 0x420U) = arg5;
            *(int32_t *)(v1_4 + 0x424U) = arg6;
            *(int32_t *)(a0_2 + 0x5f8U) = 0;
            *(int32_t *)(a0_2 + 0x5fcU) = 0;
            v1_5 = *(int32_t *)(ctx + 0x6b0U);
            *(int32_t *)(ctx + 0x698U) = v0 + 1;
            *(int32_t *)(ctx + 0x6b0U) = v1_5 + 1;
            *(ctx + v1_5 + 0x69cU) = (uint8_t)v0;
            result = 1;
            *(int32_t *)(ctx + 0x6b8U) += 1;
        }

        Rtos_ReleaseMutex(*(void **)(ctx + 0x784U));
        return result;
    }
}

int32_t AL_RefMngr_GetNextRecPicture(void *arg1, int32_t *arg2)
{
    uint8_t *ctx = (uint8_t *)arg1;

    Rtos_GetMutex(*(void **)(ctx + 0x784U));

    {
        int32_t t0 = *(int32_t *)(ctx + 0x760U);
        int32_t result;

        if (t0 <= 0) {
            result = 0;
        } else {
            int32_t v1_1 = *(int32_t *)(ctx + 0x75cU);
            uint8_t *a0_2 = ctx + (v1_1 << 3);
            uint32_t a3_1 = (uint32_t)*(uint8_t *)(a0_2 + 0x6bcU);

            if (a3_1 == 0xffU) {
                __assert("uBufId != uNUL",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/RefManager.c",
                         0xee, "AL_RefMngr_GetNextRecPicture", &_gp);
                return AL_RefMngr_MarkAsReadyForOutput(arg1, (char)a3_1);
            }

            if (*(int32_t *)(ctx + (((a3_1 + 0xbeU) << 3) + 0xcU)) < 0) {
                result = 0;
            } else {
                uint8_t *a1_2 = ctx + a3_1 * 0x18U;
                int32_t t1_2 = *(int32_t *)(a1_2 + 0x428U);
                int32_t a0_3 = *(int32_t *)(a1_2 + 0x42cU);

                arg2[1] = (uint32_t)*(uint8_t *)(a0_2 + 0x6bdU);
                arg2[2] = *(int32_t *)(a0_2 + 0x6c0U);
                arg2[0] = (int32_t)a3_1;
                arg2[3] = t1_2;
                arg2[4] = a0_3;
                result = 1;
                *(int32_t *)(ctx + 0x75cU) = (v1_1 + 1) % 0x14;
                *(int32_t *)(ctx + 0x760U) = t0 - 1;
            }
        }

        Rtos_ReleaseMutex(*(void **)(ctx + 0x784U));
        return result;
    }
}

int32_t AL_RefMngr_MarkAsReadyForOutput(void *arg1, char arg2)
{
    uint8_t *ctx = (uint8_t *)arg1;
    uint32_t s0 = (uint32_t)(uint8_t)arg2;

    Rtos_GetMutex(*(void **)(ctx + 0x784U));

    if (s0 != 0xffU) {
        uint8_t *v0_2 = ctx + (s0 << 3);

        *(int32_t *)(v0_2 + 0x5fcU) |= 0x80;
    }

    Rtos_ReleaseMutex(*(void **)(ctx + 0x784U));
    return (0U < (s0 ^ 0xffU)) ? 1 : 0;
}

uint32_t AL_RefMngr_GetNewFrmBuffer(void *arg1)
{
    uint8_t *ctx = (uint8_t *)arg1;

    if (*(int32_t *)(ctx + 0x6b0U) <= 0) {
        return 0xffU;
    }

    {
        uint32_t result = (uint32_t)*(uint8_t *)(ctx + *(int32_t *)(ctx + 0x6b4U) + 0x69cU);

        Rtos_GetMutex(*(void **)(ctx + 0x784U));

        {
            uint8_t *s3_3 = ctx + (((result + 0xbeU) << 3));
            int32_t v1_1;
            int32_t a0_2;

            Rtos_AtomicDecrement((int32_t *)(ctx + 0x6b0U));
            v1_1 = *(int32_t *)(ctx + 0x6b8U);

            if (v1_1 == 0) {
                __builtin_trap();
            }

            a0_2 = *(int32_t *)(ctx + 0x784U);
            *(int32_t *)(ctx + 0x6b4U) = (*(int32_t *)(ctx + 0x6b4U) + 1) % v1_1;
            Rtos_ReleaseMutex((void *)(uintptr_t)a0_2);
            Rtos_AtomicIncrement((int32_t *)(s3_3 + 8));
            *(int32_t *)(s3_3 + 0xc) = 0;

            if (result != 0xffU) {
                int32_t v1_2 = *(int32_t *)(ctx + 0x76cU);
                uint8_t *s0_2 = ctx + result * 0x18U;

                *(int32_t *)(s0_2 + 0x428U) = *(int32_t *)(ctx + 0x768U);
                *(int32_t *)(s0_2 + 0x42cU) = v1_2;
            }

            return result;
        }
    }
}

uint32_t AL_RefMngr_ReleaseFrmBuffer(void *arg1, char arg2)
{
    uint32_t a1 = (uint32_t)(uint8_t)arg2;

    if (a1 == 0xffU) {
        return 0xffU;
    }

    return (uint32_t)AL_sRefMngr_ReleaseBufId(arg1, (int32_t)a1);
}

int32_t AL_RefMngr_ReleaseRecPicture(void *arg1, char arg2)
{
    return (int32_t)AL_RefMngr_ReleaseFrmBuffer(arg1, (char)(uint8_t)arg2);
}

int32_t AL_RefMngr_GetRefBufferFromPOC(void *arg1, int32_t arg2)
{
    return (int32_t)AL_DPB_GetRefFromPOC((uint8_t *)arg1, arg2);
}

int32_t AL_RefMngr_StorePicture(void *arg1, void *arg2, char arg3)
{
    return AL_DPB_PushPicture(arg1, arg2, (uint32_t)(uint8_t)arg3);
}

int32_t AL_RefMngr_UpdateDPB(void *arg1, int32_t *arg2)
{
    return AL_DPB_Update(arg1, arg2, 0);
}

int32_t AL_RefMngr_Flush(void *arg1)
{
    uint8_t *ctx = (uint8_t *)arg1;
    int32_t result;

    Rtos_GetMutex(*(void **)(ctx + 0x784U));
    AL_DPB_Flush(arg1, 0x14 - *(int32_t *)(ctx + 0x760U));
    result = (0 < *(int32_t *)(ctx + 0x760U)) ? 1 : 0;
    Rtos_ReleaseMutex(*(void **)(ctx + 0x784U));
    return result;
}

int32_t AL_RefMngr_GetFrmBufAddrs(void *arg1, char arg2, int32_t *arg3, int32_t *arg4, void *arg5)
{
    uint8_t *ctx = (uint8_t *)arg1;
    uint32_t a1 = (uint32_t)(uint8_t)arg2;

    REFM_KMSG("GetFrmBufAddrs entry ctx=%p id=%u used=%d bitdepth=%u storage=%u comp=%u outY=%p outUV=%p outTrace=%p",
              arg1, a1, *(int32_t *)(ctx + 0x698U), (unsigned)*(uint8_t *)(ctx + 0x774U),
              (unsigned)*(uint8_t *)(ctx + 0x770U), (unsigned)*(uint8_t *)(ctx + 0x776U),
              arg3, arg4, arg5);

    if (a1 >= (uint32_t)*(int32_t *)(ctx + 0x698U)) {
        if (arg3 != 0) {
            *arg3 = 0;
        }

        if (arg4 != 0) {
            *arg4 = 0;
        }

        if (arg5 != 0) {
            *(int32_t *)((uint8_t *)arg5 + 0U) = 0;
            *(int32_t *)((uint8_t *)arg5 + 4U) = 0;
            *(int32_t *)((uint8_t *)arg5 + 8U) = 0;
        }

        REFM_KMSG("GetFrmBufAddrs invalid id=%u used=%d", a1, *(int32_t *)(ctx + 0x698U));
        return 0;
    }

    {
        uint32_t s6 = a1 << 3;
        uint32_t s2 = a1 << 5;
        uint32_t v0_3 = (uint32_t)*(uint8_t *)(ctx + 0x774U);
        uint8_t *s4_1 = ctx + s2 - s6;
        uint32_t v1 = (uint32_t)*(uint8_t *)(ctx + 0x776U);
        int32_t a1_1 = *(int32_t *)(s4_1 + 0x428U);
        int32_t a3 = *(int32_t *)(ctx + 0x770U);
        int32_t a2 = *(int32_t *)(s4_1 + 0x42cU);
        int32_t var_48 = 1;
        int32_t var_44 = 0;
        int32_t var_40 = 0;
        int32_t var_3c = 0;
        int32_t var_38 = 0;
        int32_t var_34 = 0;
        int32_t var_30 = 0;

        REFM_KMSG("GetFrmBufAddrs slot id=%u phys=0x%x trace=0x%x dims=%dx%d mode=%d comp=%d",
                  a1, *(int32_t *)(s4_1 + 0x418U), *(int32_t *)(s4_1 + 0x41cU), a1_1, a2, a3, v1);
        REFM_KMSG("GetFrmBufAddrs before-fill-main id=%u kind=%d", a1, var_38);
        AL_EncRecBuffer_FillPlaneDesc(&var_38, a1_1, a2, (char)v0_3, (char)v1);
        REFM_KMSG("GetFrmBufAddrs after-fill-main id=%u offY=%d pitch=%d", a1, var_34, var_38);
        REFM_KMSG("GetFrmBufAddrs before-fill-aux id=%u kind=%d", a1, var_48);
        AL_EncRecBuffer_FillPlaneDesc(&var_48, *(int32_t *)(s4_1 + 0x428U), *(int32_t *)(s4_1 + 0x42cU),
                                      *(uint8_t *)(ctx + 0x770U), *(uint8_t *)(ctx + 0x776U));
        REFM_KMSG("GetFrmBufAddrs after-fill-aux id=%u offUV=%d pitch=%d", a1, var_44, var_48);

        if (arg3 != 0) {
            *arg3 = *(int32_t *)(s4_1 + 0x418U) + var_34;
        }

        if (arg4 != 0) {
            *arg4 = *(int32_t *)(ctx + s2 - s6 + 0x418U) + var_44;
        }

        if (arg5 != 0) {
            uint32_t v0_11 = (uint32_t)*(uint8_t *)(ctx + 0x774U);
            uint8_t *s2_2 = ctx + s2 - s6;
            int32_t a1_3 = *(int32_t *)(s2_2 + 0x428U);
            int32_t a2_2 = *(int32_t *)(s2_2 + 0x42cU);
            uint8_t s2_3 = 0;

            if ((uint32_t)*(uint8_t *)(ctx + 0x775U) != 0U) {
                uint32_t v1_5 = (uint32_t)*(uint8_t *)(ctx + 0x776U);
                int32_t a3_2 = *(int32_t *)(ctx + 0x770U);
                int32_t var_58 = 2;
                int32_t var_54_1 = 0;
                uint8_t var_50_1 = 0;
                int32_t var_4c_1 = 0;

                AL_EncRecBuffer_FillPlaneDesc(&var_58, a1_3, a2_2, (char)a3_2, (char)v0_11);

                if ((uint32_t)*(uint8_t *)(ctx + 0x775U) != 0U) {
                    s2_3 = var_50_1;
                }
            }

            *(int32_t *)((uint8_t *)arg5 + 0U) = ((v0_11 < 9U) ? 1 : 0) ^ 1;
            *(int32_t *)((uint8_t *)arg5 + 4U) = var_30;
            *((uint8_t *)arg5 + 8U) = 2;
            *((uint8_t *)arg5 + 9U) = s2_3;
        }

        REFM_KMSG("GetFrmBufAddrs exit id=%u y=0x%x uv=0x%x trace=%p",
                  a1, arg3 ? *arg3 : 0, arg4 ? *arg4 : 0, arg5);
        return 1;
    }
}

int32_t AL_RefMngr_GetFrmBufTraceAddrs(void *arg1, char arg2)
{
    uint8_t *ctx = (uint8_t *)arg1;
    uint32_t a1 = (uint32_t)(uint8_t)arg2;

    if (a1 >= (uint32_t)*(int32_t *)(ctx + 0x698U)) {
        return 0;
    }

    return *(int32_t *)(ctx + a1 * 0x18U + 0x41cU);
}

int32_t AL_RefMngr_GetMvBufAddr(void *arg1, char arg2, int32_t *arg3)
{
    uint8_t *ctx = (uint8_t *)arg1;
    uint32_t a1 = (uint32_t)(uint8_t)arg2;
    int32_t result = (a1 < (uint32_t)*(int32_t *)(ctx + 0x698U)) ? 1 : 0;

    if (result == 0) {
        if (arg3 == 0) {
            return 0;
        }

        *arg3 = 0;
        return result;
    }

    {
        uint32_t a3 = a1 << 3;
        int32_t v0;
        uint32_t a1_1;

        if (arg3 == 0) {
            v0 = *(int32_t *)(ctx + 0x77cU);
            a1_1 = a1 << 5;
        } else {
            a1_1 = a1 << 5;
            v0 = *(int32_t *)(ctx + 0x77cU);
            *arg3 = *(int32_t *)(ctx + a1_1 - a3 + 0x41cU) + v0;
        }

        return v0 + *(int32_t *)(ctx + a1_1 - a3 + 0x418U);
    }
}

int32_t AL_RefMngr_GetMapBufAddr(void *arg1, char arg2)
{
    uint8_t *ctx = (uint8_t *)arg1;
    uint32_t a1 = (uint32_t)(uint8_t)arg2;

    if (a1 >= (uint32_t)*(int32_t *)(ctx + 0x698U)) {
        return 0;
    }

    {
        uint32_t v0_1 = (uint32_t)*(uint8_t *)(ctx + 0x774U);
        uint8_t *s0_2 = ctx + a1 * 0x18U;
        uint32_t v1_2 = (uint32_t)*(uint8_t *)(ctx + 0x776U);
        int32_t a3 = *(int32_t *)(ctx + 0x770U);
        int32_t a1_2 = *(int32_t *)(s0_2 + 0x428U);
        int32_t a2 = *(int32_t *)(s0_2 + 0x42cU);
        int32_t var_18 = 2;
        int32_t var_14 = 0;
        int32_t var_10 = 0;
        int32_t var_c = 0;

        AL_EncRecBuffer_FillPlaneDesc(&var_18, a1_2, a2, (char)a3, (char)v0_1);
        return *(int32_t *)(s0_2 + 0x418U) + var_14;
    }
}

int32_t AL_RefMngr_GetAvailRef(void *arg1, void *arg2, int32_t *arg3)
{
    return AL_DPB_GetAvailRef(arg1, arg2, arg3);
}

int32_t AL_RefMngr_GetColocRefPOC(int32_t arg1, void *arg2, void *arg3)
{
    (void)arg1;

    if (arg2 == 0) {
        *(int32_t *)((uint8_t *)arg3 + 0xcU) = -1;
        *(int32_t *)((uint8_t *)arg3 + 0x10U) = -1;
        return -1;
    }

    *(int32_t *)((uint8_t *)arg3 + 0xcU) = *(int32_t *)((uint8_t *)arg2 + 0x84U);

    {
        int32_t result = *(int32_t *)((uint8_t *)arg2 + 0x88U);

        *(int32_t *)((uint8_t *)arg3 + 0x10U) = result;
        return result;
    }
}

int32_t AL_RefMngr_GetRefInfo(int32_t arg1, int32_t arg2, void *arg3, void *arg4, int32_t *arg5)
{
    int32_t var_24 = 0;
    int32_t var_20 = 0;
    struct dpb_ref_list_desc l0_desc = {
        .kind = (uint16_t *)((uint8_t *)arg4 + 0x8aU),
        .list = (int32_t *)((uint8_t *)arg4 + 0x8cU),
        .count = 0,
    };
    uint8_t var_30 = 0;
    int32_t (*var_34)[0x20] = (int32_t (*)[0x20])((uint8_t *)arg5 + 0x10U);
    struct dpb_ref_list_desc l1_desc = {
        .kind = (uint16_t *)((uint8_t *)arg4 + 0xccU),
        .list = (int32_t *)((uint8_t *)arg4 + 0xd0U),
        .count = 0,
    };
    int32_t var_48 = 0;
    uint8_t var_4c = 0;
    uint8_t var_5c = 0;
    void *var_70[14];
    int32_t result;

    REFM_KMSG("GetRefInfo entry ctx=%p mode=0x%x curInfo=%p out=%p coloc=%p",
              (void *)(intptr_t)arg1, (unsigned)arg2, arg3, arg4, arg5);
    *(uint8_t *)((uint8_t *)arg4 + 0x8aU) = 0;
    *(uint8_t *)((uint8_t *)arg4 + 0xccU) = 0;
    Rtos_Memset(&var_70, 0, 0x38);
    /* DPB expects tiny descriptors shaped like { kind_ptr, list_ptr, count }.
     * The original binary relied on stack-local layout; model it explicitly. */
    var_70[0] = &l0_desc;

    {
        uint32_t a1 = (uint32_t)arg2 >> 0x18;

        var_70[1] = &l1_desc;
        REFM_KMSG("GetRefInfo desc l0 kind=%p list=%p l1 kind=%p list=%p",
                  (void *)l0_desc.kind, (void *)l0_desc.list,
                  (void *)l1_desc.kind, (void *)l1_desc.list);

        if (a1 == 1U) {
            REFM_KMSG("GetRefInfo before-hevc-dpb out=%p coloc=%p", arg4, arg5);
            AL_DPB_HEVC_GetRefInfo((char *)(uintptr_t)arg1, arg3, arg4, (int32_t *)var_70,
                                   (0U < ((uint32_t)(*(int32_t *)((uint8_t *)arg3 + 0x10U) ^ 2))) ? 1 : 0);
            REFM_KMSG("GetRefInfo after-hevc-dpb out84=%d out70=%d", *(int32_t *)((uint8_t *)arg4 + 0x84U),
                      *(int32_t *)((uint8_t *)arg4 + 0x70U));
        }

        if (a1 == 0U) {
            REFM_KMSG("GetRefInfo before-avc-dpb out=%p coloc=%p", arg4, arg5);
            AL_DPB_AVC_GetRefInfo((char *)(uintptr_t)arg1, arg3, arg4, (int32_t *)var_70, var_34, var_24);
            REFM_KMSG("GetRefInfo after-avc-dpb out84=%d out70=%d", *(int32_t *)((uint8_t *)arg4 + 0x84U),
                      *(int32_t *)((uint8_t *)arg4 + 0x70U));
        }
    }

    *(uint8_t *)((uint8_t *)arg4 + 0x78U) = (uint8_t)var_24;
    *(int32_t *)((uint8_t *)arg4 + 0x30U) = var_48;
    *(uint8_t *)((uint8_t *)arg4 + 0x79U) = var_30;
    *(uint8_t *)((uint8_t *)arg4 + 0x7aU) = var_5c;
    *(uint8_t *)((uint8_t *)arg4 + 0x7bU) = var_4c;

    {
        int32_t v0_5 = AL_RefMngr_GetRefBufferFromPOC((void *)(uintptr_t)arg1,
                                                      *(int32_t *)((uint8_t *)arg4 + 0x84U));

        REFM_KMSG("GetRefInfo after-ref-from-poc buf=%d coloc_poc=%d", v0_5,
                  *(int32_t *)((uint8_t *)arg4 + 0x84U));
        if (*(int32_t *)((uint8_t *)arg3 + 0x10U) != 2) {
            AL_RefMngr_GetMvBufAddr((void *)(uintptr_t)arg1, (char)v0_5, &var_20);
            REFM_KMSG("GetRefInfo got-coloc-mv buf=%d mv=0x%x", v0_5, var_20);
        }

        AL_RefMngr_GetColocRefPOC(arg1, (void *)(uintptr_t)var_20, (uint8_t *)arg4 + 0x28U);
        REFM_KMSG("GetRefInfo coloc-ref L0=%d L1=%d",
                  *(int32_t *)((uint8_t *)arg4 + 0x34U), *(int32_t *)((uint8_t *)arg4 + 0x38U));

        {
            int32_t s2_2 = var_20;
            int32_t v0_7 = (int32_t)(AL_DPB_GetRefMode((void *)(uintptr_t)arg1, *(int32_t *)((uint8_t *)arg4 + 0x38U)) ^ 1U);
            int32_t a1_5 = *(int32_t *)((uint8_t *)arg4 + 0x34U);

            *(uint8_t *)((uint8_t *)arg4 + 0x89U) = (v0_7 < 1) ? 1U : 0U;
            result = ((AL_DPB_GetRefMode((void *)(uintptr_t)arg1, a1_5) ^ 1U) < 1U) ? 1 : 0;
            *(uint8_t *)((uint8_t *)arg4 + 0x88U) = (uint8_t)result;
            *(uint8_t *)((uint8_t *)arg4 + 0x87U) = 0;

            if (s2_2 != 0 && var_24 > 0) {
                int32_t *result_1 = arg5;

                {
                    int32_t a1_6 = *(int32_t *)((uint8_t *)arg4 + 0x34U);
                    int32_t *a0_9 = &result_1[var_24];

                    while (1) {
                        int32_t v1_1 = *result_1;

                        result_1 = &result_1[1];

                        if (v1_1 == a1_6) {
                            *(uint8_t *)((uint8_t *)arg4 + 0x87U) = 1;
                        }

                        if (a0_9 == result_1) {
                            break;
                        }
                    }
                }

                result = (int32_t)(uintptr_t)arg5;
            }

            *(int32_t *)((uint8_t *)arg4 + 0x84U) = 0;
            REFM_KMSG("GetRefInfo exit out84=%d out70=%d out34=%d out38=%d",
                      *(int32_t *)((uint8_t *)arg4 + 0x84U), *(int32_t *)((uint8_t *)arg4 + 0x70U),
                      *(int32_t *)((uint8_t *)arg4 + 0x34U), *(int32_t *)((uint8_t *)arg4 + 0x38U));
            return result;
        }
    }
}

int32_t AL_RefMngr_SetRecResolution(void *arg1, int32_t arg2, int32_t arg3)
{
    uint8_t *ctx = (uint8_t *)arg1;
    int32_t a0_1;

    Rtos_GetMutex(*(void **)(ctx + 0x784U));
    a0_1 = *(int32_t *)(ctx + 0x784U);
    *(int32_t *)(ctx + 0x768U) = arg2;
    *(int32_t *)(ctx + 0x76cU) = arg3;
    return Rtos_ReleaseMutex((void *)(uintptr_t)a0_1);
}
