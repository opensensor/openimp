#include <stdint.h>
#include <stddef.h>

#include "alcodec/al_buffer.h"
#include "alcodec/al_rtos.h"
#include "imp_log_int.h"

typedef struct AL_IpCtrl AL_IpCtrl;
typedef struct AL_IpCtrlVtable {
    void *Destroy;
    int32_t (*ReadRegister)(AL_IpCtrl *self, uint32_t reg);
    int32_t (*WriteRegister)(AL_IpCtrl *self, uint32_t reg, uint32_t value);
    int32_t (*RegisterCallBack)(AL_IpCtrl *self, void (*cb)(void *), void *user_data, uint32_t irq_idx);
} AL_IpCtrlVtable;

struct AL_IpCtrl {
    const AL_IpCtrlVtable *vtable;
};

typedef struct AL_EncCoreCtxCompat {
    AL_IpCtrl *ip_ctrl;   /* +0x00 */
    int32_t cmd_regs_1;   /* +0x04 */
    int32_t cmd_regs_2;   /* +0x08 */
    uint8_t core_id;      /* +0x0c */
    uint8_t pad_0d[3];
    int32_t core_num;     /* +0x10 */
    int32_t clock_entry0; /* +0x14 */
    int32_t clock_entry1; /* +0x18 */
    void *lock_ref;       /* +0x1c */
    int32_t state_20;     /* +0x20 */
    int32_t state_24;     /* +0x24 */
    int32_t state_28;     /* +0x28 */
    int32_t state_2c;     /* +0x2c */
    int32_t state_30;     /* +0x30 */
    int32_t state_34;     /* +0x34 */
    int32_t state_38;     /* +0x38 */
    int32_t state_3c;     /* +0x3c */
    int32_t state_40;     /* +0x40 */
} AL_EncCoreCtxCompat;

typedef struct AL_ClockCompat {
    int32_t field_00;
    int32_t core_0;
    int32_t core_1;
    int32_t core_2;
    int32_t core_3;
    int32_t count;
} AL_ClockCompat;

typedef struct AL_CoreStateCompat {
    uint8_t running_channel_0; /* +0x00 */
    uint8_t running_channel_1; /* +0x01 */
    uint8_t run_state_0;       /* +0x02 */
    uint8_t run_state_1;       /* +0x03 */
    int32_t budget;            /* +0x04 */
    int32_t weight;            /* +0x08 */
    int32_t channels;          /* +0x0c */
    int32_t clocks[0x21];      /* +0x10 */
} AL_CoreStateCompat;

typedef struct StaticFifoCompat {
    int32_t *elems;
    int32_t read_idx;
    int32_t write_idx;
    int32_t capacity;
} StaticFifoCompat;

typedef struct ZoneDescCompat {
    uint32_t reg_base;
    uint16_t reg_offset;
    uint16_t reg_count;
} ZoneDescCompat;

extern void *__assert(const char *expression, const char *file, int32_t line, const char *function, ...);

int32_t AL_ModuleArray_IsEmpty(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t EncodingStatusRegsToSliceStatus(const void *status_regs, void *slice_status); /* forward decl, ported by T<N> later */
int32_t MergeEncodingStatus(void *merged_status, const void *slice_status); /* forward decl, ported by T<N> later */
int32_t IntVector_Add(int32_t *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
void IntVector_Init(int32_t *arg1); /* forward decl, ported by T<N> later */
int32_t IntVector_Remove(int32_t *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_Buffer_Unref(AL_TBuffer *arg1); /* forward decl, ported by T<N> later */

static const uint64_t AL_ENCJPEG_CMD = 0x0b000000000000ULL;

static int32_t StartEnc1WithCommandList_isra_25(AL_EncCoreCtxCompat *arg1, uint8_t *arg2, int32_t arg3, int32_t arg4)
{
    AL_IpCtrl *a0 = arg1->ip_ctrl;
    uint32_t core = (uint32_t)(*arg2);
    uint32_t base = core << 9;
    uint32_t status;

    IMP_LOG_INFO("AVPU", "startenc core=%u write 0x%04x=0x%08x 0x%04x=0x%08x",
                 core, base + 0x83e0, (uint32_t)arg3, base + 0x83e4, (uint32_t)arg4);
    a0->vtable->WriteRegister(a0, base + 0x83e0, (uint32_t)arg3);

    {
        AL_IpCtrl *a0_1 = arg1->ip_ctrl;

        int32_t rc = a0_1->vtable->WriteRegister(a0_1, base + 0x83e4, (uint32_t)arg4);

        status = (uint32_t)a0_1->vtable->ReadRegister(a0_1, base + 0x83f8);
        IMP_LOG_INFO("AVPU", "startenc core=%u post reg83f8=0x%08x rc=%d", core, status, rc);
        return rc;
    }
}

static int32_t ResetCore_isra_27(AL_IpCtrl *arg1, uint8_t *arg2)
{
    arg1->vtable->WriteRegister(arg1, ((uint32_t)(*arg2) << 9) + 0x83f0, 1);
    arg1->vtable->WriteRegister(arg1, ((uint32_t)(*arg2) << 9) + 0x83f0, 2);
    return arg1->vtable->WriteRegister(arg1, ((uint32_t)(*arg2) << 9) + 0x83f0, 4);
}

int32_t SetClockCommand(AL_EncCoreCtxCompat *arg1, int32_t arg2, int32_t arg3)
{
    if (arg2 != 0 && arg2 != 1)
        return SetClockCommand((AL_EncCoreCtxCompat *)(intptr_t)__assert("0",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/CoreManager.c",
            0x23, "SetClockCommand"), arg2, arg3);

    {
        AL_IpCtrl *s2 = arg1->ip_ctrl;
        uint32_t s1_1 = ((uint32_t)arg1->core_id << 9) + 0x83f4;
        int32_t v0_1 = s2->vtable->ReadRegister(s2, s1_1);

        return s2->vtable->WriteRegister(s2, s1_1, (uint32_t)((((uint32_t)v0_1 ^ (uint32_t)arg3) & 3U) ^ (uint32_t)v0_1));
    }
}

static int32_t TurnOffGC_constprop_35(AL_EncCoreCtxCompat *arg1)
{
    Rtos_GetMutex(*(void **)arg1->lock_ref);
    SetClockCommand(arg1, 0, 0);
    return Rtos_ReleaseMutex(*(void **)arg1->lock_ref);
}

static int32_t TurnOnGC_constprop_36(AL_EncCoreCtxCompat *arg1)
{
    Rtos_GetMutex(*(void **)arg1->lock_ref);
    SetClockCommand(arg1, 0, 1);
    return Rtos_ReleaseMutex(*(void **)arg1->lock_ref);
}

void EndEncoding(void *arg1)
{
    int32_t t9 = *(int32_t *)((char *)arg1 + 0x14);

    IMP_LOG_INFO("AVPU", "core EndEncoding ctx=%p fn=0x%x user=%p payload=%p mode=0",
                 arg1, t9, *(void **)((char *)arg1 + 0x18), *(void **)((char *)arg1 + 0x10));
    if (t9 != 0)
        ((void (*)(void *, void *, int32_t))(intptr_t)t9)(*(void **)((char *)arg1 + 0x18), *(void **)((char *)arg1 + 0x10), 0);
}

void EndAvcEntropy(void *arg1)
{
    int32_t t9 = *(int32_t *)((char *)arg1 + 0x14);

    if (t9 != 0)
        ((void (*)(void *, void *, int32_t))(intptr_t)t9)(*(void **)((char *)arg1 + 0x18), *(void **)((char *)arg1 + 0x10), 1);
}

int32_t AL_EncCore_TurnOnGC(AL_EncCoreCtxCompat *arg1, int32_t arg2)
{
    int32_t result = AL_ModuleArray_IsEmpty(arg2);

    if (result == 0)
        return TurnOnGC_constprop_36(arg1);

    return result;
}

int32_t AL_EncCore_TurnOffGC(AL_EncCoreCtxCompat *arg1, int32_t arg2)
{
    int32_t result = AL_ModuleArray_IsEmpty(arg2);

    if (result == 0)
        return TurnOffGC_constprop_35(arg1);

    return result;
}

int32_t IsEnc1AlreadyRunning(AL_IpCtrl *arg1, int32_t arg2)
{
    uint32_t reg = (uint32_t)arg1->vtable->ReadRegister(arg1, (arg2 << 9) + 0x83f8);

    IMP_LOG_INFO("AVPU", "enc1-busy core=%d reg83f8=0x%08x busy=%u",
                 arg2, reg, (unsigned)((reg & 2U) != 0U));
    if ((reg & 2U) == 0)
        return 0;

    return IsEnc1AlreadyRunning((AL_IpCtrl *)(intptr_t)__assert("0",
        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/CoreManager.c",
        0x4b, "IsEnc1AlreadyRunning"), arg2);
}

int32_t IsEnc2AlreadyRunning(AL_IpCtrl *arg1, int32_t arg2)
{
    uint32_t reg = (uint32_t)arg1->vtable->ReadRegister(arg1, (arg2 << 9) + 0x83f8);

    IMP_LOG_INFO("AVPU", "enc2-busy core=%d reg83f8=0x%08x busy=%u",
                 arg2, reg, (unsigned)((reg & 0x10U) != 0U));
    if ((reg & 0x10U) == 0)
        return 0;

    return IsEnc2AlreadyRunning((AL_IpCtrl *)(intptr_t)__assert("0",
        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/CoreManager.c",
        0x57, "IsEnc2AlreadyRunning"), arg2);
}

static void WriteZoneRegisters(AL_IpCtrl *arg1, int32_t arg2, int32_t arg3, const ZoneDescCompat *arg4)
{
    uint32_t v1 = arg4->reg_count;
    int32_t s0_1 = arg2 + (int32_t)arg4->reg_base;
    int32_t *s2_2 = (int32_t *)((char *)(intptr_t)arg3 + ((uint32_t)arg4->reg_offset << 2));

    if (v1 == 0)
        return;

    {
        uint32_t s1_1 = 0;

        do {
            s1_1 += 1;
            if ((uint32_t)s0_1 != 0xffffffffU)
                arg1->vtable->WriteRegister(arg1, (uint32_t)s0_1, (uint32_t)*s2_2);
            v1 = arg4->reg_count;
            s0_1 += 4;
            s2_2 = &s2_2[1];
        } while (s1_1 < v1);
    }
}

int32_t AL_EncJpegCore_Init(AL_EncCoreCtxCompat *arg1, int32_t *arg2, AL_IpCtrl *arg3, char arg4, char arg5, int32_t arg6)
{
    uint32_t v0 = (uint32_t)(uint8_t)arg5;
    uint32_t v0_1;
    int32_t a1_1;
    int32_t v0_2;
    uint8_t a3_2;

    arg1->core_id = (uint8_t)v0;
    arg1->lock_ref = (void *)(intptr_t)arg6;
    arg1->ip_ctrl = arg3;
    arg1->core_num = (uint32_t)(uint8_t)arg4;
    arg1->clock_entry0 = arg2[0];
    arg1->clock_entry1 = arg2[1];
    v0_1 = v0 << 2;
    a1_1 = 1 << (v0_1 & 0x1f);

    if (v0_1 == 0) {
        v0_2 = 1;
        a3_2 = 0;
    } else {
        int32_t a3_1 = 1;

        do {
            v0_2 = 1 << (a3_1 & 0x1f);
            a3_1 += 1;
        } while ((v0_2 & a1_1) == 0);

        a3_2 = (uint8_t)(a3_1 - 1);
    }

    if (a1_1 != v0_2)
        return AL_EncJpegCore_Init((AL_EncCoreCtxCompat *)(intptr_t)__assert("0",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/CoreManager.c",
            0x6a, "AL_EncJpegCore_Init"), arg2, arg3, arg4, arg5, arg6);

    arg3->vtable->RegisterCallBack(arg3, EndEncoding, arg1, a3_2);
    arg1->state_20 = 4;
    arg1->state_40 = 1;
    return 1;
}

int32_t AL_EncCore_Init(AL_EncCoreCtxCompat *arg1, int32_t *arg2, AL_IpCtrl *arg3, char arg4, int32_t arg5)
{
    uint32_t a3 = (uint32_t)(uint8_t)arg4;
    uint32_t v0;
    int32_t a0_1;
    int32_t v0_1;
    uint8_t a3_2;

    arg1->core_id = (uint8_t)a3;
    arg1->clock_entry0 = arg2[0];
    arg1->ip_ctrl = arg3;
    arg1->core_num = a3;
    arg1->lock_ref = (void *)(intptr_t)arg5;
    arg1->clock_entry1 = arg2[1];
    v0 = a3 << 2;
    a0_1 = 1 << (v0 & 0x1f);

    if (v0 == 0) {
        v0_1 = 1;
        a3_2 = 0;
    } else {
        int32_t a3_1 = 1;

        do {
            v0_1 = 1 << (a3_1 & 0x1f);
            a3_1 += 1;
        } while ((a0_1 & v0_1) == 0);

        a3_2 = (uint8_t)(a3_1 - 1);
    }

    if (a0_1 == v0_1) {
        int32_t a1_4;
        int32_t a3_4 = 1;
        int32_t v0_3;
        AL_IpCtrl *a0_3;

        arg3->vtable->RegisterCallBack(arg3, EndEncoding, arg1, a3_2);
        a0_3 = arg1->ip_ctrl;
        a1_4 = 1 << ((((uint32_t)arg1->core_id << 2) + 2) & 0x1f);
        do {
            v0_3 = 1 << (a3_4 & 0x1f);
            a3_4 += 1;
        } while ((a1_4 & v0_3) == 0);

        if (a1_4 == v0_3)
            a0_3->vtable->RegisterCallBack(a0_3, EndAvcEntropy, arg1, (uint8_t)(a3_4 - 1));

        ResetCore_isra_27(arg1->ip_ctrl, &arg1->core_id);
        arg1->ip_ctrl->vtable->WriteRegister(arg1->ip_ctrl, 0x8018, 0xffffff);
        arg3->vtable->WriteRegister(arg3, 0x8054, 0x80);
        arg1->state_20 = 1;
        arg1->state_24 = 0;
        arg1->state_40 = 2;
        return 2;
    }

    return AL_EncCore_Init((AL_EncCoreCtxCompat *)(intptr_t)__assert("0",
        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/CoreManager.c",
        0x88, "AL_EncCore_Init"), arg2, arg3, arg4, arg5);
}

void AL_EncCore_Deinit(AL_EncCoreCtxCompat *arg1)
{
    /* Guard: rvd-style cleanup paths call Deinit even when Init was
     * never reached (e.g. when IMP_ISP_EnableTuning failed upstream).
     * The stock binary appears to be called only after Init so its
     * decomp doesn't show this check — but the defensive guard matches
     * how all other Deinit paths in libimp handle NULL state. */
    if (arg1 == NULL || arg1->ip_ctrl == NULL) return;

    uint32_t v0_1 = (uint32_t)arg1->core_id << 2;
    AL_IpCtrl *a0 = arg1->ip_ctrl;
    int32_t a1 = 1 << (v0_1 & 0x1f);
    int32_t v0_2;
    uint8_t a3_1;

    if (v0_1 == 0) {
        v0_2 = 1;
        a3_1 = 0;
    } else {
        int32_t a3 = 1;

        do {
            v0_2 = 1 << (a3 & 0x1f);
            a3 += 1;
        } while ((v0_2 & a1) == 0);

        a3_1 = (uint8_t)(a3 - 1);
    }

    if (a1 == v0_2)
        a0->vtable->RegisterCallBack(a0, NULL, NULL, a3_1);

    {
        AL_IpCtrl *a0_1 = arg1->ip_ctrl;
        int32_t a1_4 = 1 << ((((uint32_t)arg1->core_id << 2) + 2) & 0x1f);
        int32_t a3_3 = 1;
        int32_t v0_4;

        do {
            v0_4 = 1 << (a3_3 & 0x1f);
            a3_3 += 1;
        } while ((v0_4 & a1_4) == 0);

        if (v0_4 == a1_4)
            a0_1->vtable->RegisterCallBack(a0_1, NULL, NULL, (uint8_t)(a3_3 - 1));
    }

    {
        uint32_t v0_7 = (uint32_t)arg1->core_id << 2;
        AL_IpCtrl *a0_2 = arg1->ip_ctrl;
        int32_t a1_5 = 1 << (v0_7 & 0x1f);
        uint8_t a3_6 = 0;
        int32_t v0_8;

        if (v0_7 != 0) {
            int32_t a3_7 = 1;

            do {
                v0_8 = 1 << (a3_7 & 0x1f);
                a3_7 += 1;
            } while ((v0_8 & a1_5) == 0);

            a3_6 = (uint8_t)(a3_7 - 1);
        } else {
            v0_8 = 1;
        }

        if (a1_5 == v0_8)
            a0_2->vtable->RegisterCallBack(a0_2, NULL, NULL, a3_6);
    }
}

void AL_EncCore_TurnOnRAM(AL_EncCoreCtxCompat *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    int32_t i;
    AL_EncCoreCtxCompat *core = arg1;

    (void)arg2;
    (void)arg4;
    (void)arg5;

    if (core == NULL)
        return;

    if (arg3 <= 0)
        arg3 = 1;

    for (i = 0; i < arg3; ++i) {
        TurnOnGC_constprop_36(core);
        core = (AL_EncCoreCtxCompat *)((uint8_t *)core + 0x44);
    }
}

void AL_EncCore_TurnOffRAM(AL_EncCoreCtxCompat *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    int32_t i;
    AL_EncCoreCtxCompat *core = arg1;

    (void)arg2;
    (void)arg4;
    (void)arg5;

    if (core == NULL)
        return;

    if (arg3 <= 0)
        arg3 = 1;

    for (i = 0; i < arg3; ++i) {
        TurnOffGC_constprop_35(core);
        core = (AL_EncCoreCtxCompat *)((uint8_t *)core + 0x44);
    }
}

int32_t AL_EncCore_Reset(AL_EncCoreCtxCompat *arg1)
{
    return ResetCore_isra_27(arg1->ip_ctrl, &arg1->core_id);
}

int32_t AL_EncCore_Encode1(AL_EncCoreCtxCompat *arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    int32_t result;
    uint32_t *cmd_regs_virt = (uint32_t *)(intptr_t)arg3;

    if (arg4 != 0)
        result = IsEnc2AlreadyRunning(arg1->ip_ctrl, (uint32_t)arg1->core_id);

    if (arg4 == 0 || result == 0) {
        uint32_t a1_1 = (uint32_t)arg1->core_id;
        AL_IpCtrl *a0_1 = arg1->ip_ctrl;

        arg1->cmd_regs_1 = arg3;
        result = IsEnc1AlreadyRunning(a0_1, a1_1);
        if (result == 0) {
            if (cmd_regs_virt != NULL) {
                uint32_t cfg_8400 = (uint32_t)arg1->ip_ctrl->vtable->ReadRegister(arg1->ip_ctrl, 0x8400);
                uint32_t cfg_8404 = (uint32_t)arg1->ip_ctrl->vtable->ReadRegister(arg1->ip_ctrl, 0x8404);
                uint32_t cfg_8410 = (uint32_t)arg1->ip_ctrl->vtable->ReadRegister(arg1->ip_ctrl, 0x8410);
                uint32_t cfg_8414 = (uint32_t)arg1->ip_ctrl->vtable->ReadRegister(arg1->ip_ctrl, 0x8414);
                uint32_t cfg_8420 = (uint32_t)arg1->ip_ctrl->vtable->ReadRegister(arg1->ip_ctrl, 0x8420);
                uint32_t cfg_8424 = (uint32_t)arg1->ip_ctrl->vtable->ReadRegister(arg1->ip_ctrl, 0x8424);
                uint32_t cfg_8428 = (uint32_t)arg1->ip_ctrl->vtable->ReadRegister(arg1->ip_ctrl, 0x8428);
                uint32_t cfg_85e4 = (uint32_t)arg1->ip_ctrl->vtable->ReadRegister(arg1->ip_ctrl, 0x85e4);

                IMP_LOG_INFO("AVPU",
                             "enc1-prelaunch core=%u cfg8400=%08x cfg8404=%08x cfg8410=%08x cfg8414=%08x cfg8420=%08x cfg8424=%08x cfg8428=%08x cfg85e4=%08x",
                             (unsigned)arg1->core_id, cfg_8400, cfg_8404, cfg_8410, cfg_8414,
                             cfg_8420, cfg_8424, cfg_8428, cfg_85e4);
                IMP_LOG_INFO("AVPU",
                             "enc1-cmd core=%u cl_phys=0x%08x cl_virt=%p w31=0x%08x w32=0x%08x w33=0x%08x "
                             "w3d=0x%08x w3e=0x%08x w3f=0x%08x w64=0x%08x w65=0x%08x w67=0x%08x w68=0x%08x w69=0x%08x",
                             (unsigned)arg1->core_id, (unsigned)arg2, cmd_regs_virt,
                             cmd_regs_virt[0x31], cmd_regs_virt[0x32], cmd_regs_virt[0x33],
                             cmd_regs_virt[0x3d], cmd_regs_virt[0x3e], cmd_regs_virt[0x3f],
                             cmd_regs_virt[0x64], cmd_regs_virt[0x65], cmd_regs_virt[0x67],
                             cmd_regs_virt[0x68], cmd_regs_virt[0x69]);
            }
            Rtos_FlushCacheMemory(arg3, 0x100000);
            if (arg2 == 0)
                return AL_EncCore_Encode1((AL_EncCoreCtxCompat *)(intptr_t)__assert("CmdRegs1_p",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/CoreManager.c",
                    0x76, "StartEncodeWithCommandList"), arg2, arg3, arg4);

            if (arg4 == 0)
                return StartEnc1WithCommandList_isra_25(arg1, &arg1->core_id, arg2, 2);

            StartEnc1WithCommandList_isra_25(arg1, &arg1->core_id, arg2, 2);
            return StartEnc1WithCommandList_isra_25(arg1, &arg1->core_id, arg4, 8);
        }
    } else {
        return result;
    }

    return result;
}

int32_t AL_EncCore_Encode2(AL_EncCoreCtxCompat *arg1, int32_t arg2, int32_t arg3)
{
    uint32_t a1 = (uint32_t)arg1->core_id;
    AL_IpCtrl *a0 = arg1->ip_ctrl;
    int32_t result;

    arg1->cmd_regs_2 = arg3;
    result = IsEnc2AlreadyRunning(a0, a1);
    if (result == 0)
        return StartEnc1WithCommandList_isra_25(arg1, &arg1->core_id, arg2, 8);

    return result;
}

int32_t AL_EncCore_EncodeJpeg(AL_EncCoreCtxCompat *arg1, int32_t arg2)
{
    Rtos_FlushCacheMemory(arg2, 0x100000);
    Rtos_GetMutex(*(void **)arg1->lock_ref);
    arg1->ip_ctrl->vtable->WriteRegister(arg1->ip_ctrl, ((uint32_t)arg1->core_id << 9) + 0x83f0, 1);
    WriteZoneRegisters(arg1->ip_ctrl, ((uint32_t)arg1->core_id << 9) + 0x8200, arg2, (const ZoneDescCompat *)&AL_ENCJPEG_CMD);
    arg1->ip_ctrl->vtable->WriteRegister(arg1->ip_ctrl, ((uint32_t)arg1->core_id << 9) + 0x83e4, 1);
    return Rtos_ReleaseMutex(*(void **)arg1->lock_ref);
}

int32_t AL_EncCore_DisableEnc1Interrupt(AL_EncCoreCtxCompat *arg1)
{
    AL_IpCtrl *s1 = arg1->ip_ctrl;

    return s1->vtable->WriteRegister(s1, 0x8014, (uint32_t)(~(1 << ((((uint32_t)arg1->core_id << 2) & 0x1f))) & s1->vtable->ReadRegister(s1, 0x8014)));
}

int32_t AL_EncCore_DisableEnc2Interrupt(AL_EncCoreCtxCompat *arg1)
{
    AL_IpCtrl *s1 = arg1->ip_ctrl;

    return s1->vtable->WriteRegister(s1, 0x8014, (uint32_t)(~(1 << ((((uint32_t)arg1->core_id << 2) + 2) & 0x1f)) & s1->vtable->ReadRegister(s1, 0x8014)));
}

int32_t AL_EncCore_EnableInterrupts(AL_EncCoreCtxCompat *arg1, uint8_t *arg2, char arg3, char arg4, char arg5)
{
    uint32_t s0 = (uint32_t)arg1->core_id;
    uint32_t s5 = (uint32_t)(uint8_t)arg4;
    uint32_t s3 = (uint32_t)arg2[s0];
    int32_t irq_mask_before = arg1->ip_ctrl->vtable->ReadRegister(arg1->ip_ctrl, 0x8014);

    if (s5 != 0 && s0 < s3) {
        uint32_t s1_1 = s0;
        AL_IpCtrl *fp_1 = arg1->ip_ctrl;

        do {
            int32_t s7_1 = 1 << ((s1_1 << 2) & 0x1f);
            int32_t v0_3 = fp_1->vtable->ReadRegister(fp_1, 0x8014);

            s1_1 = (uint32_t)(uint8_t)(s1_1 + 1);
            fp_1->vtable->WriteRegister(fp_1, 0x8014, (uint32_t)(((s7_1 ^ v0_3) & s7_1) ^ v0_3));
        } while (s1_1 < s3);
    }

    {
        uint32_t i_1 = (uint32_t)(uint8_t)arg3;

        if ((uint32_t)(uint8_t)arg5 != 0)
            i_1 = s0 < s3 ? 1U : 0U;

        if (i_1 == 0) {
            if (s5 != 0)
                s3 = (uint32_t)(uint8_t)(s0 + 1);

            if (s0 < s3) {
                int32_t i;

                do {
                    AL_IpCtrl *s4_3 = arg1->ip_ctrl;
                    int32_t s1_8 = 1 << ((((int32_t)s0 << 2) + 2) & 0x1f);
                    int32_t v0_10 = s4_3->vtable->ReadRegister(s4_3, 0x8014);

                    s0 = (uint32_t)(uint8_t)(s0 + 1);
                    s4_3->vtable->WriteRegister(s4_3, 0x8014, (uint32_t)(((s1_8 ^ v0_10) & s1_8) ^ v0_10));
                    i = s0 < s3 ? 1 : 0;
                } while (i != 0);

                return i;
            }

            return 0;
        }

        while (s0 < s3) {
            AL_IpCtrl *s4_1 = arg1->ip_ctrl;
            uint32_t s1_3 = s0 << 2;

            s0 = (uint32_t)(uint8_t)(s0 + 1);
            s4_1->vtable->WriteRegister(s4_1, 0x8014, (uint32_t)(~(1 << (s1_3 & 0x1f)) & s4_1->vtable->ReadRegister(s4_1, 0x8014)));

            {
                AL_IpCtrl *s4_2 = arg1->ip_ctrl;
                int32_t s1_5 = 1 << ((s1_3 + 2) & 0x1f);
                int32_t v0_8 = s4_2->vtable->ReadRegister(s4_2, 0x8014);

                s4_2->vtable->WriteRegister(s4_2, 0x8014, (uint32_t)(((s1_5 ^ v0_8) & s1_5) ^ v0_8));
            }
        }
    }

    {
        int32_t irq_mask_after = arg1->ip_ctrl->vtable->ReadRegister(arg1->ip_ctrl, 0x8014);
        IMP_LOG_INFO("ENC", "EnableInterrupts core=%u span=%u arg3=%u arg4=%u arg5=%u mask=0x%08x->0x%08x",
                     (unsigned)arg1->core_id, (unsigned)s3, (unsigned)(uint8_t)arg3, (unsigned)(uint8_t)arg4,
                     (unsigned)(uint8_t)arg5, (unsigned)irq_mask_before, (unsigned)irq_mask_after);
    }

    return 0;
}

int32_t AL_EncCore_EnableEnc2Interrupt(AL_EncCoreCtxCompat *arg1)
{
    AL_IpCtrl *s0 = arg1->ip_ctrl;
    int32_t s1 = 1 << ((((uint32_t)arg1->core_id << 2) + 2) & 0x1f);
    int32_t v0_3 = s0->vtable->ReadRegister(s0, 0x8014);

    return s0->vtable->WriteRegister(s0, 0x8014, (uint32_t)(((s1 ^ v0_3) & s1) ^ v0_3));
}

int32_t AL_EncCore_ReadStatusRegsEnc(void *arg1, void *arg2)
{
    int32_t s0 = *(int32_t *)((char *)arg1 + 4);
    int32_t i;
    uint8_t var_88[0x88];

    do {
        EncodingStatusRegsToSliceStatus((void *)(intptr_t)s0, var_88);
        s0 += 0x200;
        MergeEncodingStatus(arg2, var_88);
        {
            int32_t v0_1 = *(int32_t *)(intptr_t)(s0 - 0x1e4);

            *(int32_t *)((char *)arg2 + 4) += ((v0_1 >> 0xc) & 0x3ff) * (v0_1 & 0x3ff);
        }
        i = *(int32_t *)(intptr_t)(s0 - 0x200);
        IMP_LOG_INFO("AVPU", "ReadStatusRegsEnc regs=0x%x state=0x%x bytes=%d done=%d",
                     s0 - 0x200, *(int32_t *)(intptr_t)(s0 - 0x200),
                     *(int32_t *)((char *)arg2 + 4), i);
    } while (i >= 0);

    return i;
}

void AL_EncCore_ResetWPPCore0(AL_EncCoreCtxCompat *arg1)
{
    if ((uint32_t)arg1->core_id != 0)
        return;

    TurnOnGC_constprop_36(arg1);
    arg1->ip_ctrl->vtable->WriteRegister(arg1->ip_ctrl, 0x820c, 0);
    TurnOffGC_constprop_35(arg1);
}

int32_t AL_EncCore_ReadStatusRegsJpeg(AL_EncCoreCtxCompat *arg1, void *arg2)
{
    Rtos_GetMutex(*(void **)arg1->lock_ref);
    *(int32_t *)((char *)arg2 + 0x70) = arg1->ip_ctrl->vtable->ReadRegister(arg1->ip_ctrl, ((uint32_t)arg1->core_id << 9) + 0x8230);
    *(int32_t *)((char *)arg2 + 8) = arg1->ip_ctrl->vtable->ReadRegister(arg1->ip_ctrl, ((uint32_t)arg1->core_id << 9) + 0x8234);
    *(uint8_t *)((char *)arg2 + 1) = (uint8_t)((arg1->ip_ctrl->vtable->ReadRegister(arg1->ip_ctrl, ((uint32_t)arg1->core_id << 9) + 0x8238) >> 1) & 1);
    return Rtos_ReleaseMutex(*(void **)arg1->lock_ref);
}

int32_t AL_EncCore_SetJpegInterrupt(AL_EncCoreCtxCompat *arg1)
{
    AL_IpCtrl *s0 = arg1->ip_ctrl;
    int32_t s1_2 = 1 << ((((uint32_t)arg1->core_id << 2) & 0x1f));
    int32_t v0_1 = s0->vtable->ReadRegister(s0, 0x8014);

    return s0->vtable->WriteRegister(s0, 0x8014, (uint32_t)(((s1_2 ^ v0_1) & s1_2) ^ v0_1));
}

int32_t AL_Clock_Init(AL_ClockCompat *arg1, int32_t arg2)
{
    arg1->field_00 = 0;
    arg1->core_0 = arg2;
    arg1->count = 1;
    return 1;
}

int32_t AL_Clock_AddCore(AL_ClockCompat *arg1, int32_t arg2)
{
    int32_t v0_1 = arg1->count;

    *(int32_t *)((char *)arg1 + (v0_1 << 2) + 4) = arg2;
    arg1->count = v0_1 + 1;
    return v0_1 + 1;
}

int32_t AL_CoreState_Init(AL_CoreStateCompat *arg1, int32_t arg2, int32_t arg3)
{
    arg1->budget = arg2;
    arg1->weight = arg3;
    arg1->running_channel_0 = 0xff;
    arg1->run_state_0 = 0;
    arg1->running_channel_1 = 0xff;
    arg1->run_state_1 = 0;
    Rtos_Memset(&arg1->clocks[0x20], 0, 8);
    IntVector_Init(&arg1->channels);
    return 0;
}

void AL_CoreState_AddClock(AL_CoreStateCompat *arg1, int32_t arg2, int32_t arg3)
{
    *(int32_t *)((char *)arg1 + ((arg2 + 0x24) << 2)) = arg3;
}

int32_t AL_CoreState_RemoveChannel(AL_CoreStateCompat *arg1, int32_t arg2, int32_t arg3)
{
    (void)arg2;
    arg1->budget += arg3;
    return IntVector_Remove(&arg1->channels, arg2);
}

int32_t AL_CoreState_AddChannel(AL_CoreStateCompat *arg1, int32_t arg2, int32_t arg3)
{
    arg1->budget -= arg3;
    return IntVector_Add(&arg1->channels, arg2);
}

uint32_t AL_CoreState_IsRunning(AL_CoreStateCompat *arg1)
{
    uint32_t result = arg1->run_state_0;

    if (result != 0)
        return result;

    return arg1->run_state_1;
}

uint32_t AL_CoreState_IsChannelRunning(AL_CoreStateCompat *arg1, int32_t arg2)
{
    uint32_t result = arg1->run_state_0;

    if (result == 0 || arg2 != arg1->running_channel_0) {
        result = arg1->run_state_1;
        if (result != 0)
            return ((uint32_t)(arg1->running_channel_1 ^ arg2) < 1U) ? 1U : 0U;
    }

    return result;
}

void AL_CoreState_SetChannelRunState(uint8_t *arg1, uint8_t arg2, int32_t arg3, uint8_t arg4)
{
    uint8_t *a0 = arg1 + arg3;

    a0[2] = arg4;
    a0[0] = arg2;
}

void StaticFifo_Init(StaticFifoCompat *arg1, int32_t *arg2, int32_t arg3)
{
    arg1->read_idx = 0;
    arg1->write_idx = 0;
    arg1->capacity = arg3;
    arg1->elems = arg2;
    if (arg3 <= 0)
        return;

    {
        int32_t v0 = 0;

        while (1) {
            int32_t *a1 = (int32_t *)((char *)arg2 + (v0 << 2));

            v0 += 1;
            *a1 = 0;
            if (arg3 == v0)
                break;
            arg2 = arg1->elems;
        }
    }
}

uint32_t StaticFifo_Empty(StaticFifoCompat *arg1)
{
    return ((uint32_t)(arg1->read_idx ^ arg1->write_idx) < 1U) ? 1U : 0U;
}

int32_t StaticFifo_Queue(StaticFifoCompat *arg1, int32_t arg2)
{
    int32_t a2_2 = arg1->write_idx;
    int32_t result = arg1->capacity;

    if (result == 0)
        __builtin_trap();

    *(int32_t *)((char *)arg1->elems + (a2_2 << 2)) = arg2;
    arg1->write_idx = (a2_2 + 1) % result;
    return result;
}

int32_t StaticFifo_Dequeue(StaticFifoCompat *arg1)
{
    if (StaticFifo_Empty(arg1) != 0)
        return 0;

    {
        int32_t a0 = arg1->read_idx;
        int32_t v0_1 = arg1->capacity;
        int32_t result;

        if (v0_1 == 0)
            __builtin_trap();

        result = *(int32_t *)((char *)arg1->elems + (a0 << 2));
        arg1->read_idx = (a0 + 1) % v0_1;
        return result;
    }
}

int32_t StaticFifo_Front(StaticFifoCompat *arg1)
{
    if (StaticFifo_Empty(arg1) != 0)
        return 0;

    return *(int32_t *)((char *)arg1->elems + (arg1->read_idx << 2));
}

int32_t StaticFifo_GetElem(StaticFifoCompat *arg1, int32_t arg2)
{
    int32_t v0 = arg1->capacity;

    if (v0 == 0)
        __builtin_trap();

    return *(int32_t *)((char *)arg1->elems + ((((arg2 + arg1->read_idx) % v0) << 2)));
}
