#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include "alcodec/BitStreamLite.h"
#include "alcodec/al_buffer.h"
#include "alcodec/al_rtos.h"
#include "alcodec/al_types.h"

extern char _gp;
extern void *__assert(const char *expression, const char *file, int32_t line, const char *function, ...);
int IMP_Log_Get_Option(void); /* forward decl */
void imp_log_fun(int level, int option, int type, ...); /* forward decl */

#define CMG_KMSG(fmt, ...) do { \
    int _kfd = open("/dev/kmsg", O_WRONLY); \
    if (_kfd >= 0) { \
        char _b[192]; \
        int _n = snprintf(_b, sizeof(_b), "libimp/SCH: " fmt "\n", ##__VA_ARGS__); \
        if (_n > 0) { write(_kfd, _b, _n > (int)sizeof(_b) ? (int)sizeof(_b) : _n); } \
        close(_kfd); \
    } \
} while (0)

#define LIVE_T31_NORMALIZE_REENTRY_BEFORE_LAUNCH 0
#define LIVE_T31_EARLY_SINGLE_CORE_AVC 0
#define LIVE_T31_DEFER_UNLOCKED_OUTPUT_CB 1

#define READ_U8(base, off) (*(uint8_t *)((uint8_t *)(base) + (off)))
#define READ_S8(base, off) (*(int8_t *)((uint8_t *)(base) + (off)))
#define READ_U16(base, off) (*(uint16_t *)((uint8_t *)(base) + (off)))
#define READ_S16(base, off) (*(int16_t *)((uint8_t *)(base) + (off)))
#define READ_U32(base, off) (*(uint32_t *)((uint8_t *)(base) + (off)))
#define READ_S32(base, off) (*(int32_t *)((uint8_t *)(base) + (off)))
#define READ_PTR(base, off) (*(void **)((uint8_t *)(base) + (off)))
#define WRITE_U8(base, off, val) (*(uint8_t *)((uint8_t *)(base) + (off)) = (uint8_t)(val))
#define WRITE_U16(base, off, val) (*(uint16_t *)((uint8_t *)(base) + (off)) = (uint16_t)(val))
#define WRITE_U32(base, off, val) (*(uint32_t *)((uint8_t *)(base) + (off)) = (uint32_t)(val))
#define WRITE_S32(base, off, val) (*(int32_t *)((uint8_t *)(base) + (off)) = (int32_t)(val))
#define WRITE_PTR(base, off, val) (*(void **)((uint8_t *)(base) + (off)) = (void *)(val))

#define CH_GATE_OFF 0x1aee
#define CH_CORE_COUNT(ctx) READ_U8((ctx), 0x3c)
#define CH_CORE_BASE(ctx) READ_U8((ctx), 0x3d)
#define CH_RES_BUDGET(ctx) READ_S32((ctx), 0x2c)

typedef struct AL_ClockCompat {
    int32_t field_00;
    int32_t core_0;
    int32_t core_1;
    int32_t core_2;
    int32_t core_3;
    int32_t count;
} AL_ClockCompat;

typedef struct AL_CoreStateCompat {
    uint8_t running_channel_0;
    uint8_t running_channel_1;
    uint8_t run_state_0;
    uint8_t run_state_1;
    int32_t budget;
    int32_t weight;
    int32_t channels;
    int32_t clocks[0x21];
} AL_CoreStateCompat;

typedef struct StaticFifoCompat {
    int32_t *elems;
    int32_t read_idx;
    int32_t write_idx;
    int32_t capacity;
} StaticFifoCompat;

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
} AL_EncCoreCtxCompat;

typedef struct ChMngrFactoryVtable {
    int32_t (*destroy)(void *self);
    void *(*create)(void *self, void *arg);
    int32_t (*reserve_08)(void *self);
    void *(*get)(void *self, uint32_t idx);
} ChMngrFactoryVtable;

typedef struct ChMngrFactoryCompat {
    const ChMngrFactoryVtable *vtable;
} ChMngrFactoryCompat;

void IntVector_Init(int32_t *arg1); /* forward decl, ported by T<N> later */
int32_t IntVector_Add(int32_t *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
void IntVector_MoveBack(int32_t *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t IntVector_Remove(int32_t *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
void IntVector_Revert(int32_t *arg1); /* forward decl, ported by T<N> later */
int32_t IntVector_Copy(int32_t *arg1, int32_t *arg2); /* forward decl, ported by T<N> later */
int32_t AL_ModuleArray_AddModule(void *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_ModuleArray_IsEmpty(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_Clock_Init(AL_ClockCompat *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
void AL_CoreState_AddClock(AL_CoreStateCompat *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_CoreState_Init(AL_CoreStateCompat *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
uint32_t AL_CoreState_IsRunning(AL_CoreStateCompat *arg1); /* forward decl, ported by T<N> later */
uint32_t AL_CoreState_IsChannelRunning(AL_CoreStateCompat *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
void AL_CoreState_SetChannelRunState(uint8_t *arg1, uint8_t arg2, int32_t arg3, uint8_t arg4); /* forward decl, ported by T<N> later */
int32_t AL_CoreState_RemoveChannel(AL_CoreStateCompat *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_CoreState_AddChannel(AL_CoreStateCompat *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_EncCore_Init(void *arg1, void *arg2, void *arg3, char arg4, int32_t arg5); /* forward decl, ported by T<N> later */
int32_t AL_EncJpegCore_Init(void *arg1, void *arg2, void *arg3, char arg4, char arg5, int32_t arg6); /* forward decl, ported by T<N> later */
int32_t AL_EncCore_Deinit(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_EncCore_TurnOnGC(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_EncCore_TurnOffGC(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
void AL_CoreConstraintEnc_Init(void *arg1, int32_t arg2, uint32_t arg3); /* forward decl, ported by T<N> later */
uint32_t AL_CoreConstraintEnc_GetExpectedNumberOfCores(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_CoreConstraintEnc_GetResources(void *arg1, int32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7); /* forward decl, ported by T<N> later */
int32_t AL_EncChannel_ChannelCanBeLaunched(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_EncChannel_ListModulesNeeded(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_EncChannel_Encode(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_EncChannel_EndEncoding(void *arg1, uint8_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_EncChannel_DeInit(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_EncChannel_Init(int32_t *arg1, int32_t *arg2, void *arg3, uint8_t arg4, uint8_t arg5, int32_t arg6,
                           void *arg7, void *arg8, int32_t *arg9, int32_t arg10, int32_t arg11, int32_t arg12,
                           uint8_t arg13); /* forward decl, ported by T<N> later */
int32_t AL_EncChannel_CheckAndAdjustParam(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_EncChannel_ScheduleDestruction(void *arg1, void *arg2, void *arg3); /* forward decl, ported by T<N> later */
int32_t AL_EncChannel_GetBufResources(int32_t *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_EncChannel_PushRefBuffer(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                    int32_t arg6, void *arg7); /* forward decl, ported by T<N> later */
int32_t AL_EncChannel_PushStreamBuffer(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                       int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9); /* forward decl, ported by T<N> later */
int32_t AL_EncChannel_PushIntermBuffer(void *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_EncChannel_PushNewFrame(void *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4); /* forward decl, ported by T<N> later */
int32_t AL_EncChannel_GetNextFrameToOutput(void *arg1, int32_t *arg2); /* forward decl, ported by T<N> later */
int32_t AL_EncChannel_SetTraceCallBack(void *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetNextRecPicture(void *arg1, int32_t *arg2); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_ReleaseRecPicture(void *arg1, char arg2); /* forward decl, ported by T<N> later */
int32_t UpdateCommand(void *arg1, void *arg2, void *arg3, int32_t arg4); /* forward decl, ported by T<N> later */
int32_t ChannelResourcesAreAvailable(int32_t *arg1, void *arg2, int32_t arg3); /* forward decl */
void *GetChMngrCtx(int32_t *arg1, char arg2); /* forward decl */
static int32_t OutputFrames(int32_t *arg1); /* forward decl */
static int32_t EndFrameEncoding(int32_t *arg1, int32_t *arg2); /* forward decl */
void AL_SchedulerEnc_DeInit(int32_t *arg1); /* forward decl */
int32_t AL_SchedulerEnc_CreateChannel(int32_t *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                      int32_t *arg6); /* forward decl */
int32_t AL_SchedulerEnc_DestroyChannel(int32_t *arg1, char arg2, void *arg3, void *arg4); /* forward decl */
int32_t AL_SchedulerEnc_PutStreamBuffer(int32_t *arg1, char arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                        int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9); /* forward decl */

static int t31_allow_small_avc_overcommit(const uint8_t *ch_param, int32_t resources, int32_t core_count)
{
    uint32_t width = READ_U16(ch_param, 4);
    uint32_t height = READ_U16(ch_param, 6);

#if LIVE_T31_EARLY_SINGLE_CORE_AVC
    if (READ_U8(ch_param, 0x1f) == 0
        && core_count == 1
        && width <= 1920U
        && height <= 1080U
        && resources <= 1000000) {
        return 1;
    }
#endif

    return READ_U8(ch_param, 0x1f) == 0
        && core_count == 1
        && width <= 640U
        && height <= 480U
        && resources <= 160000;
}

static int32_t t31_pick_overcommit_core(int32_t *scheduler, const uint8_t *ch_param)
{
    int32_t best = -1;
    int32_t best_budget = INT32_MIN;

    if (scheduler[0] > 0
        && READ_U8(ch_param, 0x1f) == 0
        && READ_U16(ch_param, 4) >= 1280U) {
        return 0;
    }

    for (int32_t core = 0; core < scheduler[0]; ++core) {
        int32_t budget = scheduler[core * 0x26 + 0x176];

        if (budget > best_budget) {
            best_budget = budget;
            best = core;
        }
    }

    return best;
}

static int t31_read_live_core_status(int32_t *scheduler, int32_t core_idx, uint32_t *status_out)
{
    AL_EncCoreCtxCompat *core;

    if (scheduler == NULL || status_out == NULL || core_idx < 0 || core_idx >= scheduler[0]) {
        return 0;
    }

    core = (AL_EncCoreCtxCompat *)((uint8_t *)scheduler + 0x8c + core_idx * 0x44);
    if (core == NULL || core->ip_ctrl == NULL || core->ip_ctrl->vtable == NULL ||
        core->ip_ctrl->vtable->ReadRegister == NULL) {
        return 0;
    }

    *status_out = (uint32_t)core->ip_ctrl->vtable->ReadRegister(core->ip_ctrl,
                                                                ((uint32_t)core->core_id << 9) + 0x83f8);
    return 1;
}

static uint32_t t31_heal_stale_live_run_state(int32_t *scheduler, const uint8_t *ch_ctx, int channel, int idle,
                                              int32_t core_idx, AL_CoreStateCompat *core_state)
{
    uint32_t status;

    if (idle == 0 || scheduler == NULL || ch_ctx == NULL || core_state == NULL || READ_U8(ch_ctx, 0x1f) != 0U) {
        return AL_CoreState_IsRunning(core_state);
    }

    if (!t31_read_live_core_status(scheduler, core_idx, &status)) {
        return AL_CoreState_IsRunning(core_state);
    }

    if (core_state->run_state_0 != 0U && (status & 0x2U) == 0U) {
        CMG_KMSG("CheckAndEncode heal-stale ch=%d core=%d mod=0 hw=0x%08x run_ch=%u",
                 channel, core_idx, status, (unsigned)core_state->running_channel_0);
        AL_CoreState_SetChannelRunState((uint8_t *)core_state, 0xff, 0, 0);
    }
    if (core_state->run_state_1 != 0U && (status & 0x10U) == 0U) {
        CMG_KMSG("CheckAndEncode heal-stale ch=%d core=%d mod=1 hw=0x%08x run_ch=%u",
                 channel, core_idx, status, (unsigned)core_state->running_channel_1);
        AL_CoreState_SetChannelRunState((uint8_t *)core_state, 0xff, 1, 0);
    }

    return AL_CoreState_IsRunning(core_state);
}

static __attribute__((noinline)) void Debug_ListModulesNeeded_Call(void *ctx, int32_t *out)
{
    CMG_KMSG("CheckAndEncode wrapper-entry ctx=%p out=%p slot0=%d slot1=%d count=%d",
             ctx, out, READ_S32(out, 0), READ_S32(out, 4), READ_S32(out, 0x80));
    AL_EncChannel_ListModulesNeeded(ctx, out);
    CMG_KMSG("CheckAndEncode wrapper-return ctx=%p out=%p slot0=%d slot1=%d count=%d",
             ctx, out, READ_S32(out, 0), READ_S32(out, 4), READ_S32(out, 0x80));
}

static void getCompatibleCores(int32_t *arg1, int32_t *arg2, int32_t arg3)
{
    int32_t var_a8[0x21];
    int32_t *i_1 = var_a8;
    void *var_b0 = &_gp;
    int32_t *v0_4;
    uint32_t i;
    uint8_t *s4_1;
    int32_t s5_1;

    (void)var_b0;
    IntVector_Init(var_a8);
    if (*arg2 < 0) {
        v0_4 = arg1;
        goto copy_out;
    }

    i = (uint32_t)arg3 >> 0x18;
    s4_1 = (uint8_t *)&arg2[0x2c];
    s5_1 = 0;

    while (1) {
        int32_t a1 = READ_S32(s4_1, 0x1c);

        if (a1 > 0) {
            uint8_t *v1_1 = s4_1;
            int32_t v0_2 = 0;

            if (i != READ_U32(s4_1, -4)) {
                do {
                    v0_2 += 1;
                    v1_1 += 4;
                    if (v0_2 == a1) {
                        goto label_712cc;
                    }
                } while (READ_U32(v1_1, -4) != i);
            }

            IntVector_Add(var_a8, s5_1);
        }

label_712cc:
        s5_1 += 1;
        s4_1 += 0x44;
        if (*arg2 < s5_1) {
            v0_4 = arg1;
            break;
        }
    }

copy_out:
    /* Stock unrolls an 8-iteration copy of 4 int32s each (32 int32s), then
     * one final int32 — total 33 int32s (the full var_a8[0x21] contents).
     * Stock's termination was i_1 != &var_28 which only worked because the
     * compiler placed var_28 exactly at &var_a8[32] on its stack. gcc makes
     * no such guarantee, so use a bounded counter. */
    {
        int iter;

        for (iter = 0; iter < 8; iter++) {
            int32_t a3_1 = i_1[0];
            int32_t a2_1 = i_1[1];
            int32_t a1_1 = i_1[2];
            int32_t a0_2 = i_1[3];

            i_1 += 4;
            v0_4[0] = a3_1;
            v0_4[1] = a2_1;
            v0_4[2] = a1_1;
            v0_4[3] = a0_2;
            v0_4 += 4;
        }

        *v0_4 = *i_1;
    }
}

int32_t IsChannelIdle(int32_t *arg1, uint8_t *arg2, uint8_t ch_id)
{
    void *var_18 = &_gp;
    uint32_t i = (uint32_t)CH_CORE_BASE(arg2);
    uint32_t count = (uint32_t)CH_CORE_COUNT(arg2);

    (void)var_18;
    if ((int32_t)i < (int32_t)(count + i)) {
        int32_t s2_5 = (int32_t)(intptr_t)((uint8_t *)arg1 + i * 0x98 + 0x5d4);

        do {
            i += 1;
            if (AL_CoreState_IsChannelRunning((AL_CoreStateCompat *)(intptr_t)s2_5, (int32_t)ch_id) != 0) {
                return 0;
            }
            s2_5 += 0x98;
        } while ((int32_t)i < (int32_t)(CH_CORE_BASE(arg2) + CH_CORE_COUNT(arg2)));
    }

    return 1;
}

static int32_t addClock(int32_t *arg1, int32_t arg2)
{
    int32_t v0 = arg1[0x10d4 / 4];
    void *var_20 = &_gp;
    AL_ClockCompat *clk;
    int32_t result;

    (void)var_20;
    clk = (AL_ClockCompat *)((uint8_t *)arg1 + v0 * 0x18 + 0xf54);
    AL_Clock_Init(clk, 0);
    AL_CoreState_AddClock((AL_CoreStateCompat *)((uint8_t *)arg1 + arg2 * 0x98 + 0x5d4), 0, (int32_t)(intptr_t)clk);

    v0 = arg1[0x10d4 / 4];
    clk = (AL_ClockCompat *)((uint8_t *)arg1 + v0 * 0x18 + 0xf54);
    AL_Clock_Init(clk, arg2);
    AL_CoreState_AddClock((AL_CoreStateCompat *)((uint8_t *)arg1 + arg2 * 0x98 + 0x5d4), 1, (int32_t)(intptr_t)clk);

    result = arg1[0x10d4 / 4] + 1;
    arg1[0x10d4 / 4] = result;
    return result;
}

static int32_t NotEnoughResources(int32_t *arg1, int32_t *arg2, int32_t arg3)
{
    int32_t var_240[0x21];
    int32_t var_1bc[0x21];
    int32_t s4;
    int32_t s7;
    int32_t s3;

    getCompatibleCores(var_240, arg1, *arg2);
    if (var_240[0] >= 0x21) {
        return ChannelResourcesAreAvailable((int32_t *)(intptr_t)__assert(
            "(getCompatibleCores(pCtx, pChParam->eProfile)).count <= 32",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/Scheduler.c", 0x13f,
            "NotEnoughResources", &_gp),
            (void *)arg2, arg3);
    }

    /* Stock HLIL reads var_1b8 (frame offset 0x1b8) which in stock's stack
     * layout is var_1bc[1] — the first compatible core index after the
     * IntVector count at [0]. The port author had this as a memcpy from
     * +0x80 which read var_1bc[32] (last slot). */
    getCompatibleCores(var_1bc, arg1, *arg2);
    CMG_KMSG("NotEnoughRes: var_240[0]=%d var_1bc[0]=%d var_1bc[1]=%d arg3=%d",
             var_240[0], var_1bc[0], var_1bc[1], arg3);
    s4 = var_1bc[1];
    s7 = 0;
    s3 = 0;

    while (1) {
        int32_t var_b4[0x21];
        int32_t var_138[0x21];
        int32_t v1_2;

        getCompatibleCores(var_b4, arg1, *arg2);
        if (s7 >= var_b4[0]) {
            break;
        }

        s7 += 1;
        {
            int32_t bump = arg1[s4 * 0x26 + 0x176];
            CMG_KMSG("NotEnoughRes iter: s7=%d s4=%d budget=%d s3_before=%d",
                     s7, s4, bump, s3);
            s3 += bump;
        }

        /* Stock HLIL `$s4 = (*var_30)[v1_2 + 0x43]` is OOB for var_240
         * (sized 0x21) — stock's stack layout placed var_138 immediately
         * after var_1bc, which itself followed var_240. Frame offset
         * arithmetic lands at var_138[v1_2 + 1]. That is the real
         * semantic: pick the (v1_2+1)-th compatible core from this
         * iteration's freshly-filled core list. */
        getCompatibleCores(var_138, arg1, *arg2);
        v1_2 = 0x1f;
        if (s7 < 0x20) {
            v1_2 = s7;
        }
        s4 = var_138[v1_2 + 1];
    }

    CMG_KMSG("NotEnoughRes return: s3=%d arg3=%d -> %s", s3, arg3,
             ((uint32_t)s3 < (uint32_t)arg3) ? "NOT_ENOUGH" : "ENOUGH");
    return ((uint32_t)s3 < (uint32_t)arg3) ? 1 : 0;
}

int32_t ChannelResourcesAreAvailable(int32_t *arg1, void *arg2, int32_t arg3)
{
    int32_t result = NotEnoughResources(arg1, (int32_t *)((uint8_t *)arg2 + 0x1c), arg3);

    if (result != 0) {
        if (t31_allow_small_avc_overcommit((const uint8_t *)arg2, arg3, READ_U8(arg2, 0x3c))) {
            CMG_KMSG("ChRscAvail overcommit-small-avc size=%ux%u cores=%u need=%d",
                     (unsigned)READ_U16(arg2, 4), (unsigned)READ_U16(arg2, 6),
                     (unsigned)READ_U8(arg2, 0x3c), arg3);
            return 1;
        }
        return 0;
    }

    if ((READ_S32(arg2, 0x30) & 1) == 0 || READ_U8(arg2, 4) < 0xa21U) {
        return 1;
    }

    {
        int32_t var_a0[0x21];
        int32_t t1_1;
        uint32_t t3_1;
        uint32_t v1_5;

        getCompatibleCores(var_a0, arg1, READ_S32(arg2, 0x1c));
        t1_1 = var_a0[0];
        t3_1 = (uint32_t)READ_U8(arg2, 0x3c);
        if (t1_1 >= (int32_t)t3_1) {
            return 1;
        }

        v1_5 = (uint32_t)READ_U8(arg2, 4);
        if (t1_1 >= 0x21) {
            return (int32_t)(intptr_t)GetChMngrCtx((int32_t *)(intptr_t)__assert("(cores).count <= 32",
                                      "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/Scheduler.c",
                                      0x12e, "NumCoreIsSane", &_gp),
                                      0);
        }

        if (t1_1 != 0) {
            int32_t t0_2 = (int32_t)v1_5 - arg1[var_a0[1] * 0x26 + 0x177];
            int32_t a2_2 = 0;
            uint32_t a1_3 = 1;

            if (t0_2 <= 0) {
                return (((t3_1 < 1U) ? 1U : 0U) ^ 1U);
            }

            while (1) {
                a2_2 += 1;
                {
                    int32_t a0_8 = 0x1f;

                    if (a2_2 < 0x20) {
                        a0_8 = a2_2;
                    }
                    if ((uint32_t)(t1_1 & 0xff) == a1_3) {
                        return result;
                    }
                    t0_2 -= arg1[var_a0[a0_8 + 1] * 0x26 + 0x177];
                }
                a1_3 = (uint32_t)((uint8_t)a1_3 + 1U);
                if (t0_2 <= 0) {
                    return (((t3_1 < a1_3) ? 1U : 0U) ^ 1U);
                }
            }
        }

        if (v1_5 == 0) {
            return (0U < t3_1) ? 1 : 0;
        }
    }

    return result;
}

void *GetChMngrCtx(int32_t *arg1, char arg2)
{
    uint32_t a1_3 = (uint32_t)(uint8_t)arg2;
    int32_t slot;

    if (a1_3 < 0x20 && (slot = arg1[(a1_3 << 2 >> 2) + 3]) != 0) {
        ChMngrFactoryCompat *factory = (ChMngrFactoryCompat *)(intptr_t)arg1[0x12e8 / 4];

        if (factory != NULL && factory->vtable != NULL && factory->vtable->get != NULL) {
            void *ctx = factory->vtable->get(factory, (uint32_t)slot);

            if ((uintptr_t)ctx > 0x10000U) {
                return ctx;
            }
            if (ctx != NULL) {
                CMG_KMSG("GetChMngrCtx factory get invalid ch=%u ctx=%p; using slot=%p",
                         a1_3, ctx, (void *)(intptr_t)slot);
            }
        }

        /* Fallback for the partially-ported scheduler: the stock path goes
         * through a channel-manager factory, but the translated tree does not
         * currently provide a working implementation. The scheduler also keeps
         * a per-channel slot array at arg1[3..], so if the factory is absent
         * or returns NULL, treat that slot as the direct channel context. */
        return (void *)(intptr_t)slot;
    }

    return 0;
}

static int32_t findCoresAvailable(int32_t *arg1, uint8_t *arg2, int32_t arg3, int32_t arg4)
{
    int32_t var_a8[0x21];
    int32_t t2_1;
    int32_t v0_1;
    int32_t s4;

    getCompatibleCores(var_a8, arg1, READ_S32(arg2, 0x1c));
    t2_1 = var_a8[0] - (arg4 - 1);
    v0_1 = READ_S32(arg2, 0x30) & 1;
    var_a8[0] = t2_1;
    s4 = arg4 & 0xff;

    if (v0_1 == 0 || (uint32_t)s4 < 2U) {
        IntVector_Revert(var_a8);
        t2_1 = var_a8[0];
    }

    {
        uint8_t result_1 = 0;
        uint8_t result = result_1;

        if (t2_1 > 0) {
            uint32_t result_2 = (uint32_t)result;

            if (READ_U8(arg2, 0x3c) == 0) {
                __assert("pChParam->uNumCore",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/Scheduler.c",
                         0xdd, "CheckCoreAvailability", &_gp);
                return addClock(0, 0);
            }

            {
                int32_t t0_1 = arg4 & 0xff;
                int32_t *t1_1 = &var_a8[1];
                int32_t a3 = 0;
                int32_t t4_1 = ((uint32_t)s4 < 2U) ? 1 : 0;

                while (1) {
                    if ((READ_S32(arg2, 0x30) & 1) != 0) {
                        uint32_t v1_2 = READ_U8(arg2, 0x1f);
                        int32_t a1_1 = 0x10;

                        if (v1_2 != 0) {
                            a1_1 = 0x20;
                            if (v1_2 != 1) {
                                a1_1 = 0x40;
                            }
                        }

                        if (((t4_1 != 0 || result_2 == 0) &&
                             ((t0_1 << 7) + 7) < (int32_t)READ_U8(arg2, 4) &&
                             READ_U8(arg2, 6) >= READ_U8(arg2, 0x40) * t0_1 * a1_1)) {
                            goto label_71568;
                        }
                    } else {
label_71568:
                        if (READ_U8(arg2, 0x3e) == 0 || READ_U8(arg2, 0x1f) != 0 || t4_1 != 0) {
                            uint32_t i = (uint32_t)arg3 / (uint32_t)t0_1;
                            uint32_t result_3 = (uint32_t)result;
                            int32_t a1_2;

                            if (t0_1 == 0) {
                                __builtin_trap();
                            }

                            a1_2 = (int32_t)result_3 + t0_1;
                            if ((int32_t)result_3 >= a1_2) {
                                return result;
                            }

                            if (arg1[result_3 * 0x26 + 0x176] >= (int32_t)i) {
                                uint8_t *v1_14 = (uint8_t *)arg1 + result_3 * 0x98 + 0x670;
                                uint8_t *a1_6 = (uint8_t *)arg1 + a1_2 * 0x98 + 0x5d8;

                                if (a1_6 == v1_14) {
                                    return result;
                                }

                                {
                                    uint8_t *v1_15 = v1_14 + 0x98;

                                    while (READ_U32(v1_15, -0x98) >= i) {
                                        uint8_t *temp1_2 = v1_15;

                                        v1_15 += 0x98;
                                        if (a1_6 == temp1_2) {
                                            return result;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    a3 += 1;
                    if (a3 == t2_1) {
                        break;
                    }
                    result = (uint8_t)(*t1_1);
                    t1_1 = &t1_1[1];
                    result_2 = (uint32_t)result;
                }
            }
        }

        if (t31_allow_small_avc_overcommit(arg2, arg3, arg4)) {
            int32_t best = -1;
            int32_t best_budget = INT32_MIN;

            if (READ_U8(arg2, 0x1f) == 0 && READ_U16(arg2, 4) >= 1280U) {
                for (int32_t i = 1; i <= var_a8[0] && i < 0x21; ++i) {
                    if (var_a8[i] == 0) {
                        CMG_KMSG("findCoresAvailable overcommit-main-avc-core0 need=%d size=%ux%u",
                                 arg3, (unsigned)READ_U16(arg2, 4), (unsigned)READ_U16(arg2, 6));
                        return 0;
                    }
                }
            }

            for (int32_t i = 1; i <= var_a8[0] && i < 0x21; ++i) {
                int32_t core = var_a8[i];
                int32_t budget = arg1[core * 0x26 + 0x176];

                if (budget > best_budget) {
                    best_budget = budget;
                    best = core;
                }
            }

            if (best >= 0) {
                CMG_KMSG("findCoresAvailable overcommit-small-avc core=%d budget=%d need=%d size=%ux%u",
                         best, best_budget, arg3, (unsigned)READ_U16(arg2, 4), (unsigned)READ_U16(arg2, 6));
                return best;
            }
        }

        return -1;
    }
}

static int32_t CheckAndEncode(int32_t *arg1)
{
    uint8_t str[0x88];
    int32_t i_1[0x21];
    int32_t var_40 = 0x70000;
    int32_t var_254[0x21];
    int32_t *var_3c = var_254;
    uint8_t var_14c[0x88];
    int32_t *j_1 = (int32_t *)var_14c;
    int32_t var_150[0x21];
    int32_t *var_34 = var_150;

    memset(str, 0, sizeof(str));
    IntVector_Copy(&arg1[0x550 / 4], i_1);
    CMG_KMSG("CheckAndEncode entry pending=%d", i_1[0]);

    while (i_1[0] > 0) {
        int32_t str_3 = i_1[1];

        while (1) {
            *(int32_t *)&str[0] = str_3;
            int32_t s2_1 = str_3 & 0xff;
            int idle = 0;
            int can_launch = 0;
            uint8_t *v0_3 = GetChMngrCtx(arg1, (char)s2_1);

            CMG_KMSG("CheckAndEncode scan-token ch=%d token=0x%x ctx=%p", s2_1, str_3, v0_3);
            if (v0_3 != 0) {
                CMG_KMSG("CheckAndEncode scan-ctx ch=%d base=%u count=%u mode=%u res=%d",
                         s2_1, CH_CORE_BASE(v0_3), CH_CORE_COUNT(v0_3), READ_U8(v0_3, 0x1f),
                         CH_RES_BUDGET(v0_3));
                CMG_KMSG("CheckAndEncode pre-idle ch=%d", s2_1);
                idle = IsChannelIdle(arg1, v0_3, (uint8_t)s2_1);
                CMG_KMSG("CheckAndEncode post-idle ch=%d idle=%d", s2_1, idle);
                CMG_KMSG("CheckAndEncode pre-can-launch ch=%d", s2_1);
                can_launch = (int32_t)(uint32_t)READ_U16(v0_3, CH_GATE_OFF);
                CMG_KMSG("CheckAndEncode post-can-launch ch=%d can=%d", s2_1, can_launch);
            }

            CMG_KMSG("CheckAndEncode scan ch=%d ctx=%p idle=%d can_launch=%d", s2_1, v0_3, idle, can_launch);

            if (idle != 0 && can_launch != 0) {
                int32_t var_1d4;

                Rtos_Memset(str, 0, 0x88);
                Rtos_Memset(var_3c, 0, sizeof(var_254));
                *(int32_t *)&str[0] = str_3;
                CMG_KMSG("CheckAndEncode pre-modules-call ch=%d ctx=%p out=%p slot0=%d slot1=%d count=%d",
                         s2_1, v0_3, var_3c, READ_S32(var_3c, 0), READ_S32(var_3c, 4), READ_S32(var_3c, 0x80));
                Debug_ListModulesNeeded_Call(v0_3, var_3c);
                var_1d4 = READ_S32(var_3c, 0x80);
                CMG_KMSG("CheckAndEncode post-modules-call ch=%d ctx=%p out=%p needed=%d first_core=%d first_mod=%d",
                         s2_1, v0_3, var_3c, var_1d4, READ_S32(var_3c, 0), READ_S32(var_3c, 4));
                CMG_KMSG("CheckAndEncode modules ch=%d needed=%d", s2_1, var_1d4);

                if (var_1d4 > 0) {
                    int32_t s3_2 = 0;

                    if (var_1d4 > 0) {
                        int32_t *s1_1 = var_3c;

                        while (1) {
                            AL_CoreStateCompat *core_state =
                                (AL_CoreStateCompat *)((uint8_t *)arg1 + *s1_1 * 0x98 + 0x5d4);
                            uint32_t running = t31_heal_stale_live_run_state(arg1, v0_3, s2_1, idle, *s1_1, core_state);

                            CMG_KMSG("CheckAndEncode run-scan ch=%d idx=%d core=%d mod=%d state=%p run=%u"
                                     " ch0=%u st0=%u ch1=%u st1=%u",
                                     s2_1, s3_2, *s1_1, s1_1[1], core_state, running,
                                     (unsigned)core_state->running_channel_0, (unsigned)core_state->run_state_0,
                                     (unsigned)core_state->running_channel_1, (unsigned)core_state->run_state_1);

                            if (running != 0U) {
                                CMG_KMSG("CheckAndEncode running-block ch=%d idx=%d core=%d mod=%d",
                                         s2_1, s3_2, *s1_1, s1_1[1]);
                                break;
                            }
                            s3_2 += 1;
                            s1_1 = &s1_1[2];
                            if (s3_2 >= var_1d4) {
                                CMG_KMSG("CheckAndEncode all-modules-idle ch=%d needed=%d", s2_1, var_1d4);
                                goto label_71e98;
                            }
                        }
                    }

                    {
                        int32_t str_1[0x21];
                        int32_t i_2 = i_1[0];

                        IntVector_Init(str_1);
                        if (i_2 >= 0x21) {
                            return OutputFrames((int32_t *)(intptr_t)__assert(
                                "(*channels).count <= 32",
                                "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/Scheduler.c",
                                0x90, "getNonConcurrents", &_gp));
                        }

                        {
                            int32_t str_6 = *(int32_t *)&str[0];
                            int32_t s7_1 = 0;

                            if (i_2 > 0) {
                                do {
                                    uint8_t *v0_13 = GetChMngrCtx(arg1, (char)(str_6 & 0xff));
                                    uint8_t *v0_14 = GetChMngrCtx(arg1, (char)s2_1);
                                    uint32_t v1_3 = READ_U8(v0_13, 0x79);
                                    uint32_t t1_3 = (uint32_t)READ_U8(v0_13, 0x78) + v1_3;

                                    if ((int32_t)v1_3 < (int32_t)t1_3) {
                                        uint32_t a1_8 = READ_U8(v0_14, 0x79);
                                        uint32_t a0_19 = (uint32_t)READ_U8(v0_14, 0x78) + a1_8;

                                        while (1) {
                                            if ((int32_t)a1_8 < (int32_t)a0_19) {
                                                if (a1_8 == v1_3) {
                                                    break;
                                                }

                                                {
                                                    uint32_t v0_17 = a1_8 + 1;

                                                    while (v0_17 != a0_19) {
                                                        uint32_t temp1_1 = v0_17;

                                                        v0_17 += 1;
                                                        if (temp1_1 == v1_3) {
                                                            goto label_71da0;
                                                        }
                                                    }
                                                }
                                            }

                                            v1_3 += 1;
                                            if (v1_3 == t1_3) {
                                                break;
                                            }
                                        }

                                        IntVector_Add(str_1, str_6);
                                    }

label_71da0:
                                    s7_1 += 1;
                                    {
                                        int32_t v1_4 = 0x1f;

                                        if (s7_1 < 0x20) {
                                            v1_4 = s7_1;
                                        }
                                        str_6 = i_1[v1_4 + 1];
                                    }
                                } while (s7_1 < i_1[0]);
                            }
                        }

                        memcpy(i_1, str_1, sizeof(str_1));

                        /* `getNonConcurrents` keeps the blocked channel in the
                         * filtered set. Rotate it to the back so retry scans
                         * the next conflicting channel instead of spinning on
                         * the same blocked head forever. */
                        if (i_1[0] > 1 && i_1[1] == s2_1) {
                            IntVector_MoveBack(i_1, s2_1);
                        }

                        /* Stock reloads the retry token through a stack alias
                         * (`str_5`) that no longer maps correctly once the
                         * function is expressed in C. After filtering the
                         * pending vector, resume from its new head rather than
                         * reusing the stale pre-filter token. */
                        str_3 = i_1[1];
                        if (i_1[0] <= 0) {
                            CMG_KMSG("CheckAndEncode concurrent-filter exhausted");
                            return (int32_t)(intptr_t)Rtos_Memset(str, 0, 0x88);
                        }
                        CMG_KMSG("CheckAndEncode concurrent-filter retry pending=%d next=0x%x head=0x%x tail=0x%x",
                                 i_1[0], str_3, i_1[1], i_1[2]);
                        continue;
                    }
                }
                CMG_KMSG("CheckAndEncode no-modules ch=%d", s2_1);
            }

            CMG_KMSG("CheckAndEncode remove ch=%d token=0x%x", s2_1, str_3);
            IntVector_Remove(i_1, str_3);
            break;
        }
    }

    CMG_KMSG("CheckAndEncode exit no-launch");
    return (int32_t)(intptr_t)Rtos_Memset(str, 0, 0x88);

label_71e98:
    {
        int32_t str_2 = *(int32_t *)&str[0];

        if (str_2 == 0xff) {
            CMG_KMSG("CheckAndEncode label_71e98 invalid-token");
            return 0xff;
        }

        IntVector_MoveBack(&arg1[0x550 / 4], str_2);
        CMG_KMSG("CheckAndEncode moveback token=0x%x", str_2);

        {
            uint32_t v0_25 = (uint32_t)str[0];
            uint8_t *launch_ctx = GetChMngrCtx(arg1, (char)v0_25);
            uint32_t s1_2 = (uint32_t)CH_CORE_BASE(launch_ctx);
            uint8_t *s2_5 = (uint8_t *)arg1 + s1_2 * 0x44 + 0x8c;

            while (1) {
                uint32_t s7_2 = (uint32_t)CH_CORE_BASE(launch_ctx);
                int32_t var_1d4;
                uint8_t str_1[0x84];

                if ((int32_t)s1_2 >= (int32_t)((uint32_t)CH_CORE_COUNT(launch_ctx) + s7_2)) {
                    break;
                }

                memset(str_1, 0, sizeof(str_1));
                var_1d4 = READ_S32(var_3c, 0x80);
                if (var_1d4 > 0) {
                    int32_t fp_1 = 0;
                    int32_t *s7_3 = var_3c;

                    while (1) {
                        uint8_t *s4_2 = READ_PTR(arg1, (((s1_2 * 0x26 + s7_3[1] + 0x198) << 2) + 4));

                        CMG_KMSG("CheckAndEncode gc-scan core=%u mod=%d flag_ptr=%p flag=%u",
                                 s1_2, s7_3[1], s4_2, (unsigned)*s4_2);

                        /* Process() clears this shared per-core GC flag to 0 when it
                         * turns the core clock off. Launch prep must flip it back to 1
                         * and request TurnOnGC before the next submit. */
                        if ((uint32_t)(*s4_2) == 0U) {
                            AL_ModuleArray_AddModule(str_1, (int32_t)s1_2, 0);
                            *s4_2 = 1;
                        }

                        fp_1 += 1;
                        s7_3 = &s7_3[2];
                        if (fp_1 >= var_1d4) {
                            break;
                        }
                    }
                }

                AL_EncCore_TurnOnGC(s2_5, str_1);
                s1_2 += 1;
                s2_5 += 0x44;
            }
        }

        {
            int32_t s3_4 = 0;
            int32_t str_4;
            int32_t *s1_3;
            int32_t var_1d4;

            Rtos_GetMutex((void *)(intptr_t)arg1[0x12f4 / 4]);
            str_4 = *(int32_t *)&str[0];
            s1_3 = var_3c;
            var_1d4 = READ_S32(var_3c, 0x80);
            if (var_1d4 > 0) {
                do {
                    AL_CoreState_SetChannelRunState((uint8_t *)arg1 + *s1_3 * 0x98 + 0x5d4, (uint8_t)str_4, s1_3[1], 1);
                    s3_4 += 1;
                    s1_3 = &s1_3[2];
                } while (s3_4 < var_1d4);
            }
            Rtos_ReleaseMutex((void *)(intptr_t)arg1[0x12f4 / 4]);
        }

        {
            uint8_t *launch_ctx = GetChMngrCtx(arg1, (char)str[0]);
            void *sched_mutex = (void *)(intptr_t)arg1[0x4bd];
            int live_avc = (launch_ctx != NULL && READ_U8(launch_ctx, 0x1f) == 0U);
            int live_single_avc = (live_avc && CH_CORE_COUNT(launch_ctx) == 1U);
            CMG_KMSG("CheckAndEncode launch ch=%u ctx=%p status=%d mutex=%p",
                     (unsigned)(uint8_t)str[0], launch_ctx, arg1[0x4bc], sched_mutex);
            int32_t encode_rc;

            if (live_single_avc && arg1[0x4bc] == 1) {
                arg1[0x4bc] = 2;
                CMG_KMSG("CheckAndEncode launch-guard status=1->2 ch=%u ctx=%p",
                         (unsigned)(uint8_t)str[0], launch_ctx);
            }
            if (live_avc) {
                CMG_KMSG("CheckAndEncode launch-with-scheduler-held ch=%u status=%d",
                         (unsigned)(uint8_t)str[0], arg1[0x4bc]);
                encode_rc = AL_EncChannel_Encode(launch_ctx, var_3c);
                CMG_KMSG("CheckAndEncode launch-held-return ch=%u ctx=%p rc=%d status=%d",
                         (unsigned)(uint8_t)str[0], launch_ctx, encode_rc, arg1[0x4bc]);
                return encode_rc;
            }
            CMG_KMSG("CheckAndEncode pre-release scheduler mutex ch=%u status=%d mutex=%p",
                     (unsigned)(uint8_t)str[0], arg1[0x4bc], sched_mutex);
            Rtos_ReleaseMutex(sched_mutex);
            CMG_KMSG("CheckAndEncode released scheduler mutex before launch ch=%u status=%d",
                     (unsigned)(uint8_t)str[0], arg1[0x4bc]);
            encode_rc = AL_EncChannel_Encode(launch_ctx, var_3c);
            Rtos_GetMutex(sched_mutex);
            CMG_KMSG("CheckAndEncode reacquired scheduler mutex after launch ch=%u rc=%d",
                     (unsigned)(uint8_t)str[0], encode_rc);
            CMG_KMSG("CheckAndEncode launch-return ch=%u ctx=%p rc=%d", (unsigned)(uint8_t)str[0], launch_ctx,
                     encode_rc);
            return encode_rc;
        }
    }
}

static int32_t OutputFramesWithLockState(int32_t *arg1, int caller_holds_sched_mutex)
{
    void *var_1a8 = &_gp;
    int32_t var_a0[0x21];
    int32_t s1_1;
    void *sched_mutex = (void *)(intptr_t)arg1[0x12f4 / 4];

    (void)var_1a8;
    CMG_KMSG("OutputFrames entry sch=%p count=%d caller_locked=%d", arg1, *arg1,
             caller_holds_sched_mutex);
    if (!caller_holds_sched_mutex) {
        Rtos_GetMutex(sched_mutex);
    }
    IntVector_Copy(&arg1[0x550 / 4], var_a0);
    CMG_KMSG("OutputFrames copied pending=%d", var_a0[0]);
    if (var_a0[0] > 0) {
        s1_1 = 0;

        while (1) {
            int32_t var_1a0[0x40];
            uint8_t raw_ch = (uint8_t)var_a0[s1_1 + 1];
            uint8_t *v0_7 = GetChMngrCtx(arg1, (char)raw_ch);
            StaticFifoCompat *out_fifo = v0_7 ? (StaticFifoCompat *)(v0_7 + 0x12b24) : NULL;
            int out_ready = (out_fifo != NULL
                             && out_fifo->elems != NULL
                             && out_fifo->capacity != 0
                             && out_fifo->read_idx != out_fifo->write_idx);

            CMG_KMSG("OutputFrames loop idx=%d raw_ch=%u ctx=%p out_ready=%d r=%d w=%d cap=%d",
                     s1_1, (unsigned)raw_ch, v0_7, out_ready,
                     out_fifo ? out_fifo->read_idx : -1,
                     out_fifo ? out_fifo->write_idx : -1,
                     out_fifo ? out_fifo->capacity : -1);

            if (v0_7 != 0 && out_ready && AL_EncChannel_GetNextFrameToOutput(v0_7, var_1a0) != 0) {
                int32_t (*cb)(int32_t, int32_t, void *, int32_t, int32_t);
                void *v0_1 = (uint8_t *)var_1a0 + 0x10;

                CMG_KMSG("OutputFrames ready ch=%u ctx=%p cb=%p payload=%p flag=%u",
                         (unsigned)raw_ch, v0_7, (void *)(intptr_t)var_1a0[0],
                         (uint8_t *)var_1a0 + 0x10, (unsigned)((uint8_t *)var_1a0)[0xf8]);
                CMG_KMSG("OutputFrames pre-release ch=%u sched_mutex=%p caller_locked=%d",
                         (unsigned)raw_ch, sched_mutex, caller_holds_sched_mutex);
                Rtos_ReleaseMutex(sched_mutex);
                CMG_KMSG("OutputFrames post-release ch=%u sched_mutex=%p",
                         (unsigned)raw_ch, sched_mutex);
                if ((uint32_t)((uint8_t *)var_1a0)[0xf8] != 0U) {
                    v0_1 = 0;
                }
                cb = (int32_t (*)(int32_t, int32_t, void *, int32_t, int32_t))(intptr_t)var_1a0[0];
                if (cb == NULL) {
                    CMG_KMSG("OutputFrames null-cb ch=%u ctx=%p arg1=0x%x arg2=0x%x tail0=0x%x tail1=0x%x",
                             (unsigned)raw_ch, v0_7, var_1a0[1], var_1a0[2],
                             var_1a0[0xf0 / 4], var_1a0[0xf4 / 4]);
                } else {
                    CMG_KMSG("OutputFrames pre-cb ch=%u cb=%p a0=%p a1=0x%x payload=%p a3=0x%x a4=0x%x flag=%u",
                             (unsigned)raw_ch, (void *)(intptr_t)var_1a0[0],
                             (void *)(intptr_t)var_1a0[1], (unsigned)var_1a0[2], v0_1,
                             var_1a0[0xf0 / 4], var_1a0[0xf4 / 4],
                             (unsigned)((uint8_t *)var_1a0)[0xf8]);
                    cb(var_1a0[1], var_1a0[2], v0_1, var_1a0[0xf0 / 4], var_1a0[0xf4 / 4]);
                }
                CMG_KMSG("OutputFrames cb-return ch=%u caller_locked=%d status=%d",
                         (unsigned)raw_ch, caller_holds_sched_mutex, arg1[0x4bc]);
#if LIVE_T31_DEFER_UNLOCKED_OUTPUT_CB
                if (caller_holds_sched_mutex) {
                    CMG_KMSG("OutputFrames cb-exit-unlocked-return ch=%u idx=%d status=%d",
                             (unsigned)raw_ch, s1_1, arg1[0x4bc]);
                    return 2;
                }
#endif
                Rtos_GetMutex(sched_mutex);
                CMG_KMSG("OutputFrames cb-relock ch=%u caller_locked=%d status=%d idx=%d pending=%d",
                         (unsigned)raw_ch, caller_holds_sched_mutex, arg1[0x4bc], s1_1, var_a0[0]);
                if (s1_1 >= var_a0[0]) {
                    break;
                }
                continue;
            }

            s1_1 += 1;
            if (!caller_holds_sched_mutex) {
                Rtos_ReleaseMutex(sched_mutex);
                Rtos_GetMutex(sched_mutex);
            }
            if (s1_1 >= var_a0[0]) {
                break;
            }
        }
    }

    CMG_KMSG("OutputFrames exit sch=%p", arg1);
    if (!caller_holds_sched_mutex) {
        return (int32_t)(intptr_t)Rtos_ReleaseMutex(sched_mutex);
    }
    return 1;
}

static int32_t OutputFrames(int32_t *arg1)
{
    return OutputFramesWithLockState(arg1, 0);
}

static int32_t Process(int32_t *arg1)
{
    CMG_KMSG("Process entry sch=%p status=%d count=%d", arg1, arg1[0x4bc], *arg1);
    Rtos_GetMutex((void *)(intptr_t)arg1[0x4bd]);
    CMG_KMSG("Process post-lock status=%d count=%d", arg1[0x4bc], *arg1);
    if (arg1[0x4bc] != 0) {
        arg1[0x4bc] = 2;
        CMG_KMSG("Process reentry set status=2");
    } else {
        int32_t var_44_1 = 0x70000;
        int32_t var_3c_1 = 0x70000;
        int32_t var_2c_1 = 0x70000;
        int32_t (*var_34_1)(int32_t *arg1) = OutputFrames;

        (void)var_44_1;
        (void)var_3c_1;
        (void)var_2c_1;
        arg1[0x4bc] = 1;
        CMG_KMSG("Process set status=1");

        while (1) {
            CMG_KMSG("Process pre-OutputFrames");
            {
                int32_t out_rc;

                CMG_KMSG("Process pre-release-for-OutputFrames status=%d count=%d",
                         arg1[0x4bc], *arg1);
                Rtos_ReleaseMutex((void *)(intptr_t)arg1[0x4bd]);
                out_rc = var_34_1(arg1);
                Rtos_GetMutex((void *)(intptr_t)arg1[0x4bd]);

                CMG_KMSG("Process post-OutputFrames rc=%d", out_rc);
            }
            CMG_KMSG("Process relock after OutputFrames status=%d count=%d", arg1[0x4bc], *arg1);

            if (arg1[0x4bc] == 1 && arg1[0x550 / 4] > 0) {
                CMG_KMSG("Process skip-maintenance-live status=%d count=%d pending=%d",
                         arg1[0x4bc], *arg1, arg1[0x550 / 4]);
                CMG_KMSG("Process skip-shift-live status=%d count=%d pending=%d",
                         arg1[0x4bc], *arg1, arg1[0x550 / 4]);
                goto process_pre_check_and_encode;
            }

            if (*arg1 > 0) {
                uint8_t *s6_1 = (uint8_t *)&arg1[0x23];
                int32_t *var_48_1 = &arg1[0x199];
                int32_t i = 0;

                do {
                    uint8_t str[0x84];
                    int32_t *s2_1 = var_48_1;
                    int32_t j = 0;

                    CMG_KMSG("Process turnoff-scan core=%d active=%d", i, *arg1);
                    memset(str, 0, sizeof(str));
                    while (j != 2) {
                        uint8_t *fp_1 = (uint8_t *)(intptr_t)(*s2_1);

                        CMG_KMSG("Process turnoff-scan core=%d slot=%d fp=%p enabled=%u jobs=%d",
                                 i, j, fp_1, fp_1 ? (unsigned)(*fp_1) : 0U,
                                 fp_1 ? READ_S32(fp_1, 0x14) : -1);
                        if ((uint32_t)(*fp_1) != 0U) {
                            if (READ_S32(fp_1, 0x14) > 0) {
                                int32_t *s7_1 = (int32_t *)&fp_1[4];
                                int32_t k = 0;

                                do {
                                    CMG_KMSG("Process turnoff-check core=%d slot=%d dep_idx=%d dep_core=%d",
                                             i, j, k, *s7_1);
                                    if (AL_CoreState_IsRunning((AL_CoreStateCompat *)&arg1[*s7_1 * 0x26 + 0x175]) != 0) {
                                        goto label_723c4;
                                    }
                                    k += 1;
                                    s7_1 = &s7_1[1];
                                } while (k < READ_S32(fp_1, 0x14));
                            }

                            AL_ModuleArray_AddModule(str, i, j);
                            *fp_1 = 0;
                        }

label_723c4:
                        j += 1;
                        s2_1 = &s2_1[1];
                    }

                    CMG_KMSG("Process pre-TurnOffGC core=%d", i);
                    AL_EncCore_TurnOffGC(s6_1, str);
                    CMG_KMSG("Process post-TurnOffGC core=%d", i);
                    i += 1;
                    s6_1 += 0x44;
                    var_48_1 += 0x26;
                } while (i < *arg1);
            }

            {
                int32_t *s1_1 = &arg1[3];
                int32_t s6_2 = 0;

                CMG_KMSG("Process pre-deinit-scan");

                while (1) {
                    uint8_t *v0_14 = GetChMngrCtx(arg1, (char)s6_2);

                    if ((s6_2 & 7) == 0) {
                        CMG_KMSG("Process deinit-scan ch=%d ctx=%p", s6_2, v0_14);
                    }

                    if (v0_14 != 0 && READ_U16(v0_14, 0x1ae6) != 0 && IsChannelIdle(arg1, v0_14, (uint8_t)s6_2) != 0) {
                        CMG_KMSG("Process deinit-path ch=%d ctx=%p destroy_flag=0x%x core_base=%u core_count=%u stream=%p",
                                 s6_2, v0_14, READ_U16(v0_14, 0x1ae6), CH_CORE_BASE(v0_14),
                                 CH_CORE_COUNT(v0_14), READ_PTR(v0_14, 0x2a50));
                        uint8_t *v0_17 = GetChMngrCtx(arg1, (char)s6_2);

                        if (v0_17 != 0) {
                            uint32_t i_1 = CH_CORE_BASE(v0_17);
                            uint32_t a1_6 = CH_CORE_COUNT(v0_17);

                            if ((int32_t)i_1 < (int32_t)(a1_6 + i_1)) {
                                uint8_t *s7_7 = (uint8_t *)arg1 + i_1 * 0x98 + 0x5d4;

                                do {
                                    if (a1_6 == 0) {
                                        __builtin_trap();
                                    }
                                    i_1 += 1;
                                    AL_CoreState_RemoveChannel((AL_CoreStateCompat *)s7_7, s6_2,
                                                               CH_RES_BUDGET(v0_17) / (int32_t)a1_6);
                                    a1_6 = CH_CORE_COUNT(v0_17);
                                    s7_7 += 0x98;
                                } while ((int32_t)i_1 < (int32_t)(CH_CORE_BASE(v0_17) + a1_6));
                            }
                        }

                        s1_1 += 1;
                        IntVector_Remove(&arg1[0x154], s6_2);
                        AL_EncChannel_DeInit(v0_17);
                        {
                            int32_t *a0_20 = (int32_t *)(intptr_t)arg1[0x4ba];

                            ((void (*)(void *, int32_t))(intptr_t)(*(int32_t *)(intptr_t)(*a0_20 + 8)))(a0_20, *(s1_1 - 1));
                        }
                        *(s1_1 - 1) = 0;
                        IntVector_Add(&arg1[0x133], s6_2);
                        s6_2 += 1;
                        if (s6_2 == 0x20) {
                            break;
                        }
                        continue;
                    }

                    s6_2 += 1;
                    s1_1 += 1;
                    if (s6_2 == 0x20) {
                        break;
                    }
                }

                CMG_KMSG("Process post-deinit-scan");
            }

            {
                int32_t fp_2 = 0;
                int32_t v0_43;

                CMG_KMSG("Process pre-shift-scan");

                while (1) {
                    uint8_t *v0_25 = GetChMngrCtx(arg1, (char)fp_2);

                    if ((fp_2 & 7) == 0) {
                        CMG_KMSG("Process shift-scan ch=%d ctx=%p", fp_2, v0_25);
                    }

                    if (v0_25 != 0) {
                        {
                            int idle = IsChannelIdle(arg1, v0_25, (uint8_t)fp_2);
                            CMG_KMSG("Process shift-scan ch=%d idle=%d core_count=%u core_base=%u res=%d legacy78=%u legacy79=%u legacy7c=%u",
                                     fp_2, idle, CH_CORE_COUNT(v0_25), CH_CORE_BASE(v0_25), CH_RES_BUDGET(v0_25),
                                     READ_U8(v0_25, 0x78), READ_U8(v0_25, 0x79), READ_U16(v0_25, 0x7c));
                            if (idle != 0) {
                            uint32_t a3_2 = CH_CORE_COUNT(v0_25);
                            int32_t a2_3 = CH_RES_BUDGET(v0_25);
                            uint32_t s7_8 = CH_CORE_BASE(v0_25);
                            uint32_t s3_1 = (uint32_t)fp_2;

                            if (a3_2 == 0) {
                                __builtin_trap();
                            }

                            {
                                uint32_t s1_2 = s7_8 + a3_2;

                                if ((int32_t)s7_8 < (int32_t)s1_2) {
                                    uint8_t *s6_8 = (uint8_t *)arg1 + s7_8 * 0x98 + 0x5d4;

                                    do {
                                        CMG_KMSG("Process shift-remove ch=%d core_idx=%u run_ch=%u per_core=%d",
                                                 fp_2, s7_8, s3_1, a2_3 / a3_2);
                                        s7_8 += 1;
                                        AL_CoreState_RemoveChannel((AL_CoreStateCompat *)s6_8, s3_1, a2_3 / a3_2);
                                        s6_8 += 0x98;
                                    } while (s7_8 != s1_2);
                                }
                            }

                            a2_3 = CH_RES_BUDGET(v0_25);
                            a3_2 = CH_CORE_COUNT(v0_25);
                            CMG_KMSG("Process shift-find ch=%d need=%d cores=%u", fp_2, a2_3, a3_2);

                            {
                                int32_t v0_30 = findCoresAvailable(arg1, v0_25, a2_3, a3_2);
                                CMG_KMSG("Process shift-found ch=%d newCore=%d", fp_2, v0_30);

                                if (v0_30 < 0 || v0_30 + (int32_t)CH_CORE_COUNT(v0_25) > *arg1) {
                                    int32_t bad_core = v0_30;
                                    int32_t picked = t31_pick_overcommit_core(arg1, v0_25);
                                    int32_t current = (int32_t)CH_CORE_BASE(v0_25);

                                    if (picked >= 0 && picked + (int32_t)CH_CORE_COUNT(v0_25) <= *arg1) {
                                        CMG_KMSG("Process shift-fallback-picked ch=%d bad=%d picked=%d cores=%u need=%d",
                                                 fp_2, bad_core, picked, CH_CORE_COUNT(v0_25), CH_RES_BUDGET(v0_25));
                                        v0_30 = picked;
                                    } else if (current >= 0 && current + (int32_t)CH_CORE_COUNT(v0_25) <= *arg1) {
                                        CMG_KMSG("Process shift-fallback-current ch=%d bad=%d current=%d cores=%u need=%d",
                                                 fp_2, bad_core, current, CH_CORE_COUNT(v0_25), CH_RES_BUDGET(v0_25));
                                        v0_30 = current;
                                    } else {
                                        CMG_KMSG("Process shift-fallback-zero ch=%d bad=%d picked=%d current=%d cores=%u active=%d need=%d",
                                                 fp_2, bad_core, picked, current, CH_CORE_COUNT(v0_25), *arg1,
                                                 CH_RES_BUDGET(v0_25));
                                        v0_30 = 0;
                                    }
                                }

                                if (v0_30 != -1) {
                                    uint32_t a0_29 = CH_CORE_COUNT(v0_25);
                                    int32_t s4_3 = v0_30 & 0xff;
                                    int32_t s1_3 = a0_29 + s4_3;
                                    uint32_t lo_3;
                                    uint32_t s6_9 = (uint32_t)fp_2;
                                    int32_t v0_32 = s4_3 << 2;
                                    int32_t var_40_2;

                                    if (a0_29 == 0) {
                                        __builtin_trap();
                                    }
                                    lo_3 = (uint32_t)(CH_RES_BUDGET(v0_25) / (int32_t)a0_29);
                                    if (s4_3 >= s1_3) {
                                        var_40_2 = v0_32;
                                    } else {
                                        uint8_t *i_2;
                                        uint8_t *i_3;

                                        var_40_2 = v0_32;
                                        i_2 = (uint8_t *)arg1 + ((s4_3 * 0xf + v0_32) << 3) + 0x5d4;
                                        i_3 = i_2;
                                        do {
                                            CMG_KMSG("Process shift-add ch=%d dst_core=%d per_core=%u", fp_2, s4_3, lo_3);
                                            i_2 += 0x98;
                                            AL_CoreState_AddChannel((AL_CoreStateCompat *)i_3, s6_9, lo_3);
                                            i_3 = i_2;
                                        } while ((uint8_t *)arg1 + s1_3 * 0x98 + 0x5d4 != i_2);
                                    }

                                    WRITE_U8(v0_25, 0x3d, (uint8_t)(v0_30 & 0xff));
                                    /* +0x7c stores the per-core state base pointer */
                                    WRITE_PTR(v0_25, 0x80, (uint8_t *)arg1 + var_40_2 + (s4_3 << 6) + 0x8c);
                                    CMG_KMSG("Process shift-commit ch=%d core_base=%d core_ptr=%p",
                                             fp_2, v0_30 & 0xff, READ_PTR(v0_25, 0x80));
                                } else {
                                    __assert("newCore != -1",
                                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/Scheduler.c",
                                             0x1b2, "TryToShiftChannels", &_gp);
                                    return EndFrameEncoding((int32_t *)(intptr_t)arg1, &arg1[0x4bd]);
                                }
                            }
                            }
                        }
                    }

                    fp_2 += 1;
                    if (fp_2 == 0x20) {
                        break;
                    }
                }

                CMG_KMSG("Process post-shift-scan");

process_pre_check_and_encode:
                CMG_KMSG("Process pre-CheckAndEncode status=%d count=%d", arg1[0x4bc], *arg1);
#if LIVE_T31_NORMALIZE_REENTRY_BEFORE_LAUNCH
                if (arg1[0x4bc] == 2) {
                    CMG_KMSG("Process normalize-reentry-before-launch status=2->1 count=%d", *arg1);
                    arg1[0x4bc] = 1;
                }
#endif
                CheckAndEncode(arg1);
                CMG_KMSG("Process post-CheckAndEncode status=%d count=%d", arg1[0x4bc], *arg1);
                /*
                 * T31 live AVC can complete during CheckAndEncode while this
                 * thread owns the scheduler mutex. Drain the completed event
                 * before letting queued frame producers run, but drop the
                 * mutex around the output callback so ReleaseStream can return
                 * the stream buffer.
                 */
                CMG_KMSG("Process post-CheckAndEncode pre-OutputFrames");
                {
                    int32_t out_rc;

                    CMG_KMSG("Process post-CheckAndEncode pre-release-for-OutputFrames status=%d count=%d",
                             arg1[0x4bc], *arg1);
                    Rtos_ReleaseMutex((void *)(intptr_t)arg1[0x4bd]);
                    out_rc = var_34_1(arg1);
                    Rtos_GetMutex((void *)(intptr_t)arg1[0x4bd]);

                    CMG_KMSG("Process post-CheckAndEncode post-OutputFrames rc=%d", out_rc);
                }
                v0_43 = arg1[0x4bc];
                CMG_KMSG("Process loop-post status=%d count=%d", v0_43, *arg1);
                if (v0_43 != 0) {
                    if (v0_43 == 2) {
                        /*
                         * Live T31 AVC deliberately marks status=2 while
                         * launching. Reentrant frame/stream-buffer callbacks
                         * use the same marker to request another scheduler
                         * pass, so keep the owner loop alive instead of
                         * dropping queued requests until a later external kick.
                         */
                        arg1[0x4bc] = 1;
                        CMG_KMSG("Process reentry-drain continue status=2->1 count=%d", *arg1);
                        continue;
                    }
                    if (v0_43 != 1) {
                        __assert("pCtx->eProcessStatus != AL_SCHEDULER_NO_PROCESS",
                                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/Scheduler.c",
                                 0x24e, "Process", &_gp);
                    }
                    arg1[0x4bc] = 0;
                    CMG_KMSG("Process idle-reset status=%d count=%d", arg1[0x4bc], *arg1);
                    return (int32_t)(intptr_t)Rtos_ReleaseMutex((void *)(intptr_t)arg1[0x4bd]);
                }
                arg1[0x4bc] = 1;
            }
        }
    }

    CMG_KMSG("Process exit status=%d count=%d", arg1[0x4bc], *arg1);
    return (int32_t)(intptr_t)Rtos_ReleaseMutex((void *)(intptr_t)arg1[0x4bd]);
}

static int32_t EndFrameEncoding(int32_t *arg1, int32_t *arg2)
{
    int32_t *s0 = arg2;
    int32_t i = 0;

    Rtos_GetMutex((void *)(intptr_t)arg1[0x4bd]);
    if (arg2[0x20] > 0) {
        do {
            AL_CoreState_SetChannelRunState((uint8_t *)&arg1[*s0 * 0x26 + 0x175], 0xff, s0[1], 0);
            i += 1;
            s0 = (int32_t *)((uint8_t *)arg2 + ((uint8_t *)s0 + 8 - (uint8_t *)arg2));
        } while (i < arg2[0x20]);
    }
    Rtos_ReleaseMutex((void *)(intptr_t)arg1[0x4bd]);
    return Process(arg1);
}

static int32_t HandleCoreInterrupt(int32_t *arg1, int32_t arg2, int32_t arg3)
{
    void *var_20 = &_gp;
    int32_t arg_8 = arg3;
    int32_t arg_4 = arg2;
    uint8_t *v0_6;
    int32_t result;

    (void)var_20;
    (void)arg_8;
    (void)arg_4;
    CMG_KMSG("HandleCoreInterrupt entry sch=%p core=%d lane=%d mutex=%p", arg1, arg2, arg3,
             (void *)(intptr_t)arg1[0x4bd]);
    /*
     * T31 live AVC completion can arrive while Process()->CheckAndEncode()
     * still owns the non-recursive scheduler mutex. The core-state channel id
     * is already latched before launch, so read it locklessly and let the
     * channel-level mutex serialize EndEncoding.
     */
    CMG_KMSG("HandleCoreInterrupt lockless-read sch=%p core=%d lane=%d", arg1, arg2, arg3);
    v0_6 = GetChMngrCtx(arg1, (char)READ_U8(&arg1[arg2 * 0x26], arg3 + 0x5d4));
    CMG_KMSG("HandleCoreInterrupt ctx=%p raw_ch=%u", v0_6,
             (unsigned)READ_U8(&arg1[arg2 * 0x26], arg3 + 0x5d4));
    result = 1;
    if (v0_6 != 0) {
        CMG_KMSG("HandleCoreInterrupt pre-EndEncoding ctx=%p core=%d lane=%d", v0_6, arg2, arg3);
        result = AL_EncChannel_EndEncoding(v0_6, (uint8_t)arg2, arg3);
        CMG_KMSG("HandleCoreInterrupt post-EndEncoding result=%d", result);
        if (result == 0) {
            /*
             * Live T31 multi-core AVC delivers repeated IRQ4 completions while
             * a single frame is still draining. Re-entering Process() on each
             * partial completion lets the scheduler launch another channel and
             * remap IRQ4 before the last core reports, which strands the frame
             * at pending=1 and never reaches queued-output.
             */
            if (READ_U8(v0_6, 0x1f) == 0U && READ_U8(v0_6, 0x3c) > 1U) {
                CMG_KMSG("HandleCoreInterrupt defer-process live-multicore ctx=%p core_count=%u core_base=%u",
                         v0_6, (unsigned)READ_U8(v0_6, 0x3c), (unsigned)READ_U8(v0_6, 0x3d));
                return 1;
            }
            CMG_KMSG("HandleCoreInterrupt fallback-Process");
            return Process(arg1);
        }
    }
    CMG_KMSG("HandleCoreInterrupt exit result=%d", result);
    return result;
}

static void *CreateChMngrCtx(int32_t *arg1, int32_t arg2)
{
    ChMngrFactoryCompat *a0 = (ChMngrFactoryCompat *)(intptr_t)arg1[0x12e8 / 4];
    void *var_18 = &_gp;
    void *t9 = NULL;

    (void)var_18;
    if (a0 != NULL && a0->vtable != NULL) {
        t9 = (void *)a0->vtable->create;
    }

    if (t9 != 0) {
        void *created = a0->vtable->create(a0, (void *)0x12fd8);
        void *ctx = NULL;

        if (created != NULL) {
            arg1[(arg2 << 2 >> 2) + 3] = (int32_t)(intptr_t)created;
            ctx = GetChMngrCtx(arg1, (char)arg2);
            if ((uintptr_t)ctx > 0x10000U) {
                return ctx;
            }

            CMG_KMSG("CreateChMngrCtx factory get invalid ch=%d created=%p ctx=%p; fallback malloc",
                     arg2, created, ctx);
        } else {
            CMG_KMSG("CreateChMngrCtx factory create failed ch=%d; fallback malloc", arg2);
        }

        arg1[(arg2 << 2 >> 2) + 3] = 0;
    }

    {
        void *ctx = Rtos_Malloc(0x12fd8);

        arg1[(arg2 << 2 >> 2) + 3] = (int32_t)(intptr_t)ctx;
        if (ctx != NULL) {
            Rtos_Memset(ctx, 0, 0x12fd8);
        }
        return ctx;
    }
}

int32_t AL_SchedulerEnc_Init(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4, int32_t arg5, int32_t arg6)
{
    void *var_48 = &_gp;
    int32_t i;
    uint8_t var_40[0x30];
    int32_t (*var_30[2])(int32_t *, int32_t, int32_t);
    int32_t *var_2c_1 = arg1;
    void *s2_2;

    /* Stock HLIL references `var_3c` and `var_38` as scalar reads — but in
     * stock's stack frame they alias `var_40[1]` (byte 4) and `var_40[2]`
     * (byte 8) respectively, both populated by AL_CoreConstraint_Init's
     * `arg1[1]=arg6` and `arg1[2]=computed`. Reading them through separate
     * C locals leaves them uninitialized; the resulting garbage budget
     * propagates into CoreState and fails NotEnoughResources downstream. */
#   define VAR_38 (((int32_t *)var_40)[2])  /* budget: (arg2 - result*0xa)/arg4 */
#   define VAR_3C (((int32_t *)var_40)[1])  /* weight: v0 from CoreConstraintEnc */

    (void)var_48;
    (void)var_2c_1;
    if (arg5 >= 0x11) {
        return -1;
    }
    *arg1 = arg5;
    if (arg4 == 0 || arg2 == 0 || arg3 == 0) {
        __assert("pIpCtrl && pAllocator && pDmaAllocator",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/Scheduler.c",
                 0x29d, "AL_SchedulerEnc_Init");
        AL_SchedulerEnc_DeInit(0);
        return 0;
    }

    arg1[2] = (int32_t)(intptr_t)arg4;
    arg1[0x4ba] = (int32_t)(intptr_t)arg2;
    arg1[0x4bb] = (int32_t)(intptr_t)arg3;
    IntVector_Init(&arg1[0x133]);
    IntVector_Init(&arg1[0x154]);
    arg1[0x4bd] = (int32_t)(intptr_t)Rtos_CreateMutex();
    arg1[0x4be] = (int32_t)(intptr_t)Rtos_CreateMutex();
    arg1[1] = arg6;

    i = 0;
    do {
        int32_t i_2 = i;

        IntVector_Add(&arg1[0x133], i_2);
        arg1[i + 3] = 0;
        i += 1;
    } while (i != 0x20);

    Rtos_Memset(&arg1[0x3d5], 0, 0x180);
    AL_BitStreamLite_Init((AL_BitStreamLite *)&arg1[0x4b6], (uint8_t *)&arg1[0x436], 0x200);

    {
        int32_t *a0_5 = (int32_t *)(intptr_t)arg1[2];
        int32_t t9_1 = *(int32_t *)(intptr_t)(*a0_5 + 8);

        arg1[0x435] = 0;
        var_30[0] = HandleCoreInterrupt;
        var_30[1] = (int32_t (*)(int32_t *, int32_t, int32_t))arg1;
        ((int32_t (*)(int32_t *, int32_t, int32_t))(intptr_t)t9_1)(a0_5, 0x8010, 0x1000);
    }

    AL_CoreConstraintEnc_Init(var_40, arg1[1], 0);
    s2_2 = &arg1[0x4be];

    if (*arg1 > 0) {
        uint32_t i_3 = 0;
        uint32_t i_1 = 0;

        do {
            int32_t s6_1 = (int32_t)i_3 << 2;

            AL_CoreState_Init((AL_CoreStateCompat *)&arg1[(i_3 * 0xf + s6_1) * 2 + 0x175], VAR_38, VAR_3C);
            /* Core-manager records live in the compact 0x44-byte table at
             * scheduler+0x8c. Most runtime paths (getCompatibleCores,
             * AL_EncChannel_Init, Process, DeInit) index that region with
             * byte stride 0x44. The earlier port mirrored HLIL's typed
             * `arg1 + ... + 0x8c` expression literally as int32_t indexing,
             * which shifted init to 0x230 and left the real core table zero. */
            AL_EncCore_Init((uint8_t *)arg1 + 0x8c + i_3 * 0x44, var_30, (void *)(intptr_t)arg1[2], (char)i_3,
                            (int32_t)(intptr_t)s2_2);
            addClock(arg1, (int32_t)i_3);
            i_1 = (uint32_t)((uint8_t)i_1 + 1U);
            i_3 = i_1;
        } while ((int32_t)i_1 < *arg1);
    }

    AL_CoreConstraintEnc_Init(var_40, arg1[1], 4);

    {
        int32_t s4_1 = *arg1;
        int32_t s5_1 = s4_1 << 2;
        int32_t var_50_2 = s4_1 & 0xff;
        void *var_4c_1 = s2_2;

        (void)var_50_2;
        (void)var_4c_1;
        AL_CoreState_Init((AL_CoreStateCompat *)&arg1[(s4_1 * 0xf + s5_1) * 2 + 0x175], VAR_38, VAR_3C);
        AL_EncJpegCore_Init((uint8_t *)arg1 + 0x8c + s4_1 * 0x44, var_30, (void *)(intptr_t)arg1[2], 0, (char)s4_1,
            (int32_t)(intptr_t)s2_2);
        addClock(arg1, s4_1);
    }

    arg1[0x4bc] = 0;
    return 0;
}

void AL_SchedulerEnc_DeInit(int32_t *arg1)
{
    void *var_18 = &_gp;

    (void)var_18;
    if (arg1 == 0) {
        return;
    }

    AL_BitStreamLite_Deinit((AL_BitStreamLite *)&arg1[0x4b6]);

    {
        uint32_t i_1 = 0;
        uint32_t i = 0;

        if (*arg1 > 0) {
            do {
                AL_EncCore_Deinit(&arg1[i_1 * 0x11 + 0x23]);
                i = (uint32_t)((uint8_t)i + 1U);
                i_1 = i;
            } while ((int32_t)i < *arg1);
        }
    }

    Rtos_DeleteMutex((void *)(intptr_t)arg1[0x4be]);
    Rtos_DeleteMutex((void *)(intptr_t)arg1[0x4bd]);
}

static uint32_t filterCoresCount(void *arg1, void *arg2)
{
    uint32_t result = (uint32_t)READ_U8(arg2, 0x3c);

    if (result == 0U) {
        return AL_CoreConstraintEnc_GetExpectedNumberOfCores(arg1, arg2);
    }

    return result;
}

int32_t AL_SchedulerEnc_CreateChannel2(int32_t *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                       int32_t *arg6, int32_t arg7)
{
    int32_t *s1 = arg1;
    uint8_t var_48[0x30];
    int32_t v0;
    uint32_t v1_1;
    uint32_t v0_3;
    uint32_t var_38_1;
    uint32_t var_34_1;
    int32_t v0_4;
    int32_t v0_5;
    void *s0_1;
    uint32_t s2_1;
    int32_t v0_10;
    int32_t a3_3;
    int32_t result;

    CMG_KMSG("SchEnc.CC2 ENTRY s1=%p arg2=%p prof=%u s1[0x4bd]=%p s1[1]=0x%x",
             (void*)s1, arg2, (uint32_t)READ_U8(arg2, 0x1f),
             (void*)arg1[0x4bd], s1[1]);
    CMG_KMSG("SchEnc.CC2 pre-Rtos_GetMutex");
    Rtos_GetMutex((void *)(intptr_t)arg1[0x4bd]);
    /* Dump scheduler's core-capability region [s1+0xac..s1+0xdc] to see
     * whether profile table + count fields are populated. getCompatibleCores
     * reads s4_1 = &s1[0x2c] (byte 0xb0) with stride 0x44, profile[0] at
     * s4_1-4 (byte 0xac), count at s4_1+0x1c (byte 0xcc). */
    {
        uint32_t *sb = (uint32_t *)((uint8_t *)s1 + 0xac);
        CMG_KMSG("SchEnc.CC2 CoreTbl@s1+0xac: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
                 sb[0], sb[1], sb[2], sb[3], sb[4], sb[5], sb[6], sb[7],
                 sb[8], sb[9], sb[10], sb[11]);
    }
    CMG_KMSG("SchEnc.CC2 pre-CoreConstraintEnc_Init");
    AL_CoreConstraintEnc_Init(var_48, s1[1], (uint32_t)READ_U8(arg2, 0x1f));
    CMG_KMSG("SchEnc.CC2 pre-filterCoresCount");
    v0 = (int32_t)filterCoresCount(var_48, arg2);
    CMG_KMSG("SchEnc.CC2 filterCores=%d", v0);
#if LIVE_T31_EARLY_SINGLE_CORE_AVC
    if (READ_U8(arg2, 0x1f) == 0U && READ_U16(arg2, 4) >= 1280U && v0 > 1) {
        CMG_KMSG("SchEnc.CC2 force-early-single-core-avc old=%d size=%ux%u",
                 v0, (unsigned)READ_U16(arg2, 4), (unsigned)READ_U16(arg2, 6));
        v0 = 1;
    }
#endif
    WRITE_U8(arg2, 0x3c, (uint8_t)v0);
    *arg6 = AL_EncChannel_CheckAndAdjustParam(arg2);
    CMG_KMSG("SchEnc.CC2 CheckAndAdjustParam=0x%x", *arg6);
    if ((uint32_t)*arg6 >= 0x80U) {
        CMG_KMSG("SchEnc.CC2 CheckAndAdjustParam fail -> 0xff");
        Rtos_ReleaseMutex((void *)(intptr_t)s1[0x4bd]);
        return 0xff;
    }

    /* Stock uses lhu (U16) for width/height/fps_n/fps_d — not lbu (U8).
     * The port's U8 reads collapsed width 0x780 -> 0x80 and produced a
     * bogus GetResources ~= 0xf52 instead of the true value (~1M). */
    v1_1 = (uint32_t)READ_U16(arg2, 4);
    v0_3 = (uint32_t)READ_U16(arg2, 6);
    var_38_1 = v1_1;
    var_34_1 = v0_3;
    v0_4 = AL_CoreConstraintEnc_GetResources(var_48, READ_S32(arg2, 0x2c), v1_1, v0_3,
                                             (uint32_t)READ_U16(arg2, 0x74),
                                             (uint32_t)READ_U16(arg2, 0x76),
                                             (uint32_t)READ_U8(arg2, 0x3c));
    CMG_KMSG("SchEnc.CC2 GetResources=0x%x", v0_4);
    v0_5 = ChannelResourcesAreAvailable(s1, arg2, v0_4);
    CMG_KMSG("SchEnc.CC2 ChRscAvail=%d", v0_5);
    s0_1 = arg2;
    if (v0_5 == 0) {
        *arg6 = 0x8e;
        CMG_KMSG("SchEnc.CC2 no resources -> 0xff err=0x8e");
        Rtos_ReleaseMutex((void *)(intptr_t)s1[0x4bd]);
        return 0xff;
    }

    s2_1 = (uint32_t)READ_U8(s0_1, 0x3c);
    if (*s1 < (int32_t)s2_1) {
        CMG_KMSG("SchEnc.CC2 s1[0]=%d < s2_1=%u -> 73210", *s1, s2_1);
        goto label_73210;
    }

    while (1) {
        v0_10 = findCoresAvailable(s1, s0_1, v0_4, (int32_t)s2_1);
        a3_3 = v0_10;
        if (v0_10 >= *s1 && t31_allow_small_avc_overcommit((const uint8_t *)s0_1, v0_4, (int32_t)s2_1)) {
            int32_t forced_core = t31_pick_overcommit_core(s1, (const uint8_t *)s0_1);

            if (forced_core >= 0) {
                CMG_KMSG("SchEnc.CC2 force-valid-core old=%d new=%d cores=%d need=0x%x",
                         v0_10, forced_core, *s1, v0_4);
                v0_10 = forced_core;
                a3_3 = forced_core;
            }
        }
        if (v0_10 != -1) {
            break;
        }
        s2_1 += 1;
        if (*s1 < (int32_t)s2_1) {
            CMG_KMSG("SchEnc.CC2 no core found -> 73210_1");
            goto label_73210_1;
        }
    }
    CMG_KMSG("SchEnc.CC2 findCoresAvailable=%d s2_1=%u", v0_10, s2_1);

    WRITE_U8(s0_1, 0x3c, (uint8_t)s2_1);
    if (v0_10 < 0) {
        CMG_KMSG("SchEnc.CC2 v0_10 s< 0 -> 73210");
        goto label_73210;
    }

    if (v0 != (int32_t)(s2_1 & 0xffU)) {
        *arg6 = AL_EncChannel_CheckAndAdjustParam(s0_1);
        a3_3 = v0_10;
        if ((uint32_t)*arg6 >= 0x80U) {
            CMG_KMSG("SchEnc.CC2 2nd CheckAndAdjust fail=0x%x -> 73210", *arg6);
            goto label_73210;
        }
    }

    if (s1[0x133] == 0) {
        *arg6 = 0x8d;
        CMG_KMSG("SchEnc.CC2 s1[0x133]==0 -> 0xff err=0x8d");
        goto label_fail_release;
    }

    result = s1[0x134];
    IntVector_Remove(&s1[0x133], result);
    if (s1[0x133] == 0 || (uint32_t)result >= 0x20U) {
        *arg6 = 0x8d;
    }
    CMG_KMSG("SchEnc.CC2 alloc chan_id=%d a3_3=%d v0_4=0x%x", result, a3_3, v0_4);

    {
        int32_t *v0_14 = CreateChMngrCtx(s1, result);

        CMG_KMSG("SchEnc.CC2 CreateChMngrCtx=%p", (void*)v0_14);
        if (v0_14 == 0) {
            *arg6 = 0x87;
            CMG_KMSG("SchEnc.CC2 CreateChMngrCtx=NULL -> err=0x87 return_channel");
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/Scheduler.c",
                        0x34f, "AL_SchedulerEnc_CreateChannel2",
                        "SchEnc CreateChannel2: CreateChMngrCtx failed chan=%d need=0x%x cores=%u\n",
                        result, v0_4, s2_1);
            goto label_return_channel;
        }

        {
            int32_t a1_7 = *s1;
            int32_t a0_11 = s1[0x4ba];
            int32_t t0_2 = s1[1];
            int32_t cb_bundle[4];
            int32_t _init_ok;

            cb_bundle[0] = arg4;
            cb_bundle[1] = arg5;
            cb_bundle[2] = (int32_t)(intptr_t)EndFrameEncoding;
            cb_bundle[3] = (int32_t)(intptr_t)s1;
            WRITE_PTR(v0_14, 0x40, &s1[0x4b6]);
            CMG_KMSG("SchEnc.CC2 pre-EncChn_Init ctx=%p s0_1=%p core=%p a3_3=%d result=%d",
                     (void*)v0_14, s0_1, (void*)&s1[a3_3 * 0x11 + 0x23], a3_3, result);
            _init_ok = AL_EncChannel_Init(v0_14, s0_1, &s1[a3_3 * 0x11 + 0x23], (uint8_t)a3_3, (uint8_t)s2_1, v0_4,
                                   (void *)(intptr_t)a0_11, (void *)(intptr_t)arg7, cb_bundle, arg3, a1_7, t0_2, 1);
            CMG_KMSG("SchEnc.CC2 EncChn_Init=%d *arg6=0x%x", _init_ok, *arg6);
            if (_init_ok != 0) {
                uint32_t i = CH_CORE_BASE(v0_14);
                uint32_t v1_8 = CH_CORE_COUNT(v0_14);

                if ((int32_t)i < (int32_t)(v1_8 + i)) {
                    uint8_t *s4_2 = (uint8_t *)s1 + i * 0x98 + 0x5d4;

                    do {
                        if (v1_8 == 0) {
                            __builtin_trap();
                        }
                        i += 1;
                        AL_CoreState_AddChannel((AL_CoreStateCompat *)s4_2, result, CH_RES_BUDGET(v0_14) / (int32_t)v1_8);
                        v1_8 = CH_CORE_COUNT(v0_14);
                        s4_2 += 0x98;
                    } while ((int32_t)i < (int32_t)(CH_CORE_BASE(v0_14) + v1_8));
                }

                IntVector_Add(&s1[0x154], result);
                if ((uint32_t)*arg6 >= 0x80U) {
                    CMG_KMSG("SchEnc.CC2 post-EncChn_Init *arg6=0x%x -> assert", *arg6);
                    return AL_SchedulerEnc_CreateChannel((int32_t *)(intptr_t)__assert(
                        "!AL_IS_ERROR_CODE(*eErrorCode)",
                        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/Scheduler.c", 0x355,
                        "AL_SchedulerEnc_CreateChannel2"), 0, 0, 0, 0, 0);
                }

                CMG_KMSG("SchEnc.CC2 SUCCESS return chan=%d", result);
                Rtos_ReleaseMutex((void *)(intptr_t)s1[0x4bd]);
                return result;
            }
        }

        *arg6 = 0x87;
        CMG_KMSG("SchEnc.CC2 EncChn_Init failed -> err=0x87 return_channel");
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Encoder",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/Scheduler.c",
                    0x372, "AL_SchedulerEnc_CreateChannel2",
                    "SchEnc CreateChannel2: EncChn_Init failed chan=%d core=%d need=0x%x size=%ux%u\n",
                    result, a3_3, v0_4, (unsigned)READ_U16(s0_1, 4), (unsigned)READ_U16(s0_1, 6));
label_return_channel:
        IntVector_Add(&s1[0x133], result);
        {
            int32_t *a0_18 = (int32_t *)(intptr_t)s1[0x4ba];
            uint8_t *s0_4 = (uint8_t *)&s1[result];

            *arg6 = 0x80;
            ((void (*)(void *, int32_t))(intptr_t)(*(int32_t *)(intptr_t)(*a0_18 + 8)))(a0_18, READ_S32(s0_4, 0xc));
            WRITE_S32(s0_4, 0xc, 0);
        }
    }

label_fail_release:
    CMG_KMSG("SchEnc.CC2 fail_release -> 0xff *arg6=0x%x", *arg6);
    Rtos_ReleaseMutex((void *)(intptr_t)s1[0x4bd]);
    return 0xff;

label_73210_1:
label_73210:
    *arg6 = 0x8f;
    CMG_KMSG("SchEnc.CC2 73210 -> err=0x8f");
    goto label_fail_release;
}

int32_t AL_SchedulerEnc_CreateChannel(int32_t *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                      int32_t *arg6)
{
    int32_t *s1 = arg1;
    uint8_t var_48[0x30];
    int32_t v0;
    uint32_t v1_1;
    uint32_t v0_3;
    uint32_t var_38_1;
    uint32_t var_34_1;
    int32_t v0_4;
    int32_t v0_5;
    void *s0_1;
    uint32_t s2_1;
    int32_t v0_10;
    int32_t a3_3;
    int32_t result;

    Rtos_GetMutex((void *)(intptr_t)arg1[0x4bd]);
    AL_CoreConstraintEnc_Init(var_48, s1[1], (uint32_t)READ_U8(arg2, 0x1f));
    v0 = (int32_t)filterCoresCount(var_48, arg2);
    WRITE_U8(arg2, 0x3c, (uint8_t)v0);
    *arg6 = AL_EncChannel_CheckAndAdjustParam(arg2);
    if ((uint32_t)*arg6 >= 0x80U) {
        Rtos_ReleaseMutex((void *)(intptr_t)s1[0x4bd]);
        return 0xff;
    }

    /* Stock uses lhu (U16) for width/height/fps_n/fps_d — not lbu (U8).
     * The port's U8 reads collapsed width 0x780 -> 0x80 and produced a
     * bogus GetResources ~= 0xf52 instead of the true value (~1M). */
    v1_1 = (uint32_t)READ_U16(arg2, 4);
    v0_3 = (uint32_t)READ_U16(arg2, 6);
    var_38_1 = v1_1;
    var_34_1 = v0_3;
    v0_4 = AL_CoreConstraintEnc_GetResources(var_48, READ_S32(arg2, 0x2c), v1_1, v0_3,
                                             (uint32_t)READ_U16(arg2, 0x74),
                                             (uint32_t)READ_U16(arg2, 0x76),
                                             (uint32_t)READ_U8(arg2, 0x3c));
    v0_5 = ChannelResourcesAreAvailable(s1, arg2, v0_4);
    s0_1 = arg2;
    if (v0_5 == 0) {
        *arg6 = 0x8e;
        Rtos_ReleaseMutex((void *)(intptr_t)s1[0x4bd]);
        return 0xff;
    }

    s2_1 = (uint32_t)READ_U8(s0_1, 0x3c);
    if (*s1 < (int32_t)s2_1) {
        goto label_73610;
    }

    while (1) {
        v0_10 = findCoresAvailable(s1, s0_1, v0_4, (int32_t)s2_1);
        a3_3 = v0_10;
        if (v0_10 >= *s1 && t31_allow_small_avc_overcommit((const uint8_t *)s0_1, v0_4, (int32_t)s2_1)) {
            int32_t forced_core = t31_pick_overcommit_core(s1, (const uint8_t *)s0_1);

            if (forced_core >= 0) {
                CMG_KMSG("SchEnc.CC force-valid-core old=%d new=%d cores=%d need=0x%x",
                         v0_10, forced_core, *s1, v0_4);
                v0_10 = forced_core;
                a3_3 = forced_core;
            }
        }
        if (v0_10 != -1) {
            break;
        }
        s2_1 += 1;
        if (*s1 < (int32_t)s2_1) {
            goto label_73610_1;
        }
    }

    WRITE_U8(s0_1, 0x3c, (uint8_t)s2_1);
    if (v0_10 < 0) {
        goto label_73610;
    }

    if (v0 != (int32_t)(s2_1 & 0xffU)) {
        *arg6 = AL_EncChannel_CheckAndAdjustParam(s0_1);
        a3_3 = v0_10;
        if ((uint32_t)*arg6 >= 0x80U) {
            goto label_73610;
        }
    }

    if (s1[0x133] == 0) {
        *arg6 = 0x8d;
        goto label_fail_release;
    }

    result = s1[0x134];
    IntVector_Remove(&s1[0x133], result);
    if (s1[0x133] == 0 || (uint32_t)result >= 0x20U) {
        *arg6 = 0x8d;
    }

    {
        int32_t *v0_14 = CreateChMngrCtx(s1, result);

        if (v0_14 == 0) {
            *arg6 = 0x87;
            goto label_return_channel;
        }

        {
            int32_t a1_7 = s1[0x4bb];
            int32_t a0_11 = s1[0x4ba];
            int32_t t1_2 = s1[1];
            int32_t t0_2 = *s1;
            int32_t cb_bundle[4];

            cb_bundle[0] = arg4;
            cb_bundle[1] = arg5;
            cb_bundle[2] = (int32_t)(intptr_t)EndFrameEncoding;
            cb_bundle[3] = (int32_t)(intptr_t)s1;
            WRITE_PTR(v0_14, 0x40, &s1[0x4b6]);
            if (AL_EncChannel_Init(v0_14, s0_1, &s1[a3_3 * 0x11 + 0x23], (uint8_t)a3_3, (uint8_t)s2_1, v0_4,
                                   (void *)(intptr_t)a0_11, (void *)(intptr_t)a1_7, cb_bundle, arg3, t0_2, t1_2, 1) != 0) {
                uint32_t i = CH_CORE_BASE(v0_14);
                uint32_t v1_7 = CH_CORE_COUNT(v0_14);

                if ((int32_t)i < (int32_t)(v1_7 + i)) {
                    uint8_t *s4_2 = (uint8_t *)s1 + i * 0x98 + 0x5d4;

                    do {
                        if (v1_7 == 0) {
                            __builtin_trap();
                        }
                        i += 1;
                        AL_CoreState_AddChannel((AL_CoreStateCompat *)s4_2, result, CH_RES_BUDGET(v0_14) / (int32_t)v1_7);
                        v1_7 = CH_CORE_COUNT(v0_14);
                        s4_2 += 0x98;
                    } while ((int32_t)i < (int32_t)(CH_CORE_BASE(v0_14) + v1_7));
                }

                IntVector_Add(&s1[0x154], result);
                if ((uint32_t)*arg6 >= 0x80U) {
                    return AL_SchedulerEnc_DestroyChannel((int32_t *)(intptr_t)__assert(
                        "!AL_IS_ERROR_CODE(*eErrorCode)",
                        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/Scheduler.c", 0x3bc,
                        "AL_SchedulerEnc_CreateChannel"), 0, 0, 0);
                }

                Rtos_ReleaseMutex((void *)(intptr_t)s1[0x4bd]);
                return result;
            }
        }

        *arg6 = 0x87;
label_return_channel:
        IntVector_Add(&s1[0x133], result);
        {
            int32_t *a0_18 = (int32_t *)(intptr_t)s1[0x4ba];
            uint8_t *s0_4 = (uint8_t *)&s1[result];

            *arg6 = 0x80;
            ((void (*)(void *, int32_t))(intptr_t)(*(int32_t *)(intptr_t)(*a0_18 + 8)))(a0_18, READ_S32(s0_4, 0xc));
            WRITE_S32(s0_4, 0xc, 0);
        }
    }

label_fail_release:
    Rtos_ReleaseMutex((void *)(intptr_t)s1[0x4bd]);
    return 0xff;

label_73610_1:
label_73610:
    *arg6 = 0x8f;
    goto label_fail_release;
}

int32_t AL_SchedulerEnc_DestroyChannel(int32_t *arg1, char arg2, void *arg3, void *arg4)
{
    void *var_18 = &_gp;
    uint8_t *v0;

    (void)var_18;
    Rtos_GetMutex((void *)(intptr_t)arg1[0x4bd]);
    v0 = GetChMngrCtx(arg1, arg2);
    if (v0 == 0) {
        return (int32_t)(intptr_t)Rtos_ReleaseMutex((void *)(intptr_t)arg1[0x4bd]);
    }
    AL_EncChannel_ScheduleDestruction(v0, arg3, arg4);
    Rtos_ReleaseMutex((void *)(intptr_t)arg1[0x4bd]);
    return Process(arg1);
}

int32_t *AL_SchedulerEnc_GetChannelBufResources(int32_t *arg1, int32_t *arg2, char arg3)
{
    void *var_10 = &_gp;

    (void)var_10;
    AL_EncChannel_GetBufResources(arg1, GetChMngrCtx(arg2, arg3));
    return arg1;
}

int32_t AL_SchedulerEnc_PutRefBuffer(int32_t *arg1, char arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6)
{
    void *ctx = GetChMngrCtx(arg1, arg2);
    void *ref_ctx = (ctx != NULL) ? (uint8_t *)ctx + 0x22c8 : NULL;
    void *ref_mutex = (ctx != NULL) ? READ_PTR(ctx, 0x2a4c) : NULL;
    void *stream_ctx = (ctx != NULL) ? READ_PTR(ctx, 0x2a50) : NULL;
    int32_t result;

    CMG_KMSG("PutRefBuffer entry sch=%p ch=%u ctx=%p ref_ctx=%p ref_mutex=%p phys=0x%x virt=0x%x size=%d aux=%d",
             arg1, (unsigned)(uint8_t)arg2, ctx, ref_ctx, ref_mutex, (unsigned)arg3,
             (unsigned)arg4, arg5, arg6);
    CMG_KMSG("PutRefBuffer stream-state sch=%p ch=%u ctx=%p stream_ctx=%p",
             arg1, (unsigned)(uint8_t)arg2, ctx, stream_ctx);
    result = AL_EncChannel_PushRefBuffer(ctx, arg3, arg4, arg5, arg6, 0, &_gp);
    CMG_KMSG("PutRefBuffer exit sch=%p ch=%u ctx=%p result=%d stream_ctx=%p",
             arg1, (unsigned)(uint8_t)arg2, ctx, result,
             (ctx != NULL) ? READ_PTR(ctx, 0x2a50) : NULL);

    if (result != 0) {
        return result;
    }

    return AL_SchedulerEnc_PutStreamBuffer((int32_t *)(intptr_t)__assert(
        "bRet", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/Scheduler.c", 0x3ec,
        "AL_SchedulerEnc_PutRefBuffer"), 0, 0, 0, 0, 0, 0, 0, 0);
}

int32_t AL_SchedulerEnc_PutStreamBuffer(int32_t *arg1, char arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6,
                                        int32_t arg7, int32_t arg8, int32_t arg9)
{
    void *var_30 = &_gp;
    uint8_t *v0;

    (void)var_30;
    CMG_KMSG("PutStreamBuffer entry sch=%p ch=%u phys=0x%x virt=%p size=%d off=%d arg7=%d arg8=%d arg9=%d mutex=%p",
             arg1, (unsigned)(uint8_t)arg2, (unsigned)arg3, (void *)(intptr_t)arg4, arg5, arg6, arg7, arg8, arg9,
             (void *)(intptr_t)arg1[0x4bd]);
    Rtos_GetMutex((void *)(intptr_t)arg1[0x4bd]);
    v0 = GetChMngrCtx(arg1, arg2);
    CMG_KMSG("PutStreamBuffer ctx=%p", v0);
    if (v0 != 0 && AL_EncChannel_PushStreamBuffer(v0, arg3, arg4, arg5, arg6, arg5 - arg6, arg7, arg8, arg9) != 0) {
        int32_t old_status = arg1[0x4bc];

        CMG_KMSG("PutStreamBuffer push ok ctx=%p status=%d", v0, old_status);
        if (old_status != 0) {
            arg1[0x4bc] = 2;
            CMG_KMSG("PutStreamBuffer queued-while-processing ctx=%p status=%d->2", v0, old_status);
            Rtos_ReleaseMutex((void *)(intptr_t)arg1[0x4bd]);
            return 1;
        }
        Rtos_ReleaseMutex((void *)(intptr_t)arg1[0x4bd]);
        {
            int32_t ret = Process(arg1);
            CMG_KMSG("PutStreamBuffer post-Process ret=%d sch=%p ctx=%p", ret, arg1, v0);
            return ret;
        }
    }

    CMG_KMSG("PutStreamBuffer push failed/null ctx=%p", v0);
    return (int32_t)(intptr_t)Rtos_ReleaseMutex((void *)(intptr_t)arg1[0x4bd]);
}

int32_t AL_SchedulerEnc_PutIntermBuffer(int32_t *arg1, char arg2, int32_t arg3, int32_t arg4)
{
    void *var_18 = &_gp;
    void *ctx = GetChMngrCtx(arg1, arg2);
    void *interm_ctx = (ctx != NULL) ? (uint8_t *)ctx + 0x2a54 : NULL;
    void *interm_mutex = (interm_ctx != NULL) ? READ_PTR(interm_ctx, 0xf8) : NULL;
    void *stream_ctx = (ctx != NULL) ? READ_PTR(ctx, 0x2a50) : NULL;
    int32_t result;

    (void)var_18;
    CMG_KMSG("PutIntermBuffer entry sch=%p ch=%u ctx=%p interm_ctx=%p interm_mutex=%p phys=0x%x virt=0x%x",
             arg1, (unsigned)(uint8_t)arg2, ctx, interm_ctx, interm_mutex, (unsigned)arg3, (unsigned)arg4);
    CMG_KMSG("PutIntermBuffer stream-state sch=%p ch=%u ctx=%p stream_ctx=%p",
             arg1, (unsigned)(uint8_t)arg2, ctx, stream_ctx);
    result = AL_EncChannel_PushIntermBuffer(ctx, arg3, arg4);
    CMG_KMSG("PutIntermBuffer exit sch=%p ch=%u ctx=%p result=%d stream_ctx=%p",
             arg1, (unsigned)(uint8_t)arg2, ctx, result,
             (ctx != NULL) ? READ_PTR(ctx, 0x2a50) : NULL);
    return result;
}

int32_t AL_SchedulerEnc_EncodeOneFrame(int32_t *arg1, char arg2, int32_t *arg3, int32_t *arg4, int32_t *arg5)
{
    uint8_t *v0;

    CMG_KMSG("EncodeOneFrame entry sch=%p ch=%u src=%p stream=%p meta=%p mutex=%p",
             arg1,
             (unsigned)(uint8_t)arg2,
             arg3,
             arg4,
             arg5,
             (void *)(intptr_t)arg1[0x4bd]);
    Rtos_GetMutex((void *)(intptr_t)arg1[0x4bd]);
    v0 = GetChMngrCtx(arg1, arg2);
    CMG_KMSG("EncodeOneFrame ctx=%p", v0);
    if (v0 == 0) {
        CMG_KMSG("EncodeOneFrame null-ctx");
        return (int32_t)(intptr_t)Rtos_ReleaseMutex((void *)(intptr_t)arg1[0x4bd]);
    }
    if (arg3 != NULL && arg1[0x4bc] != 0) {
        StaticFifoCompat *lane0 = (StaticFifoCompat *)(v0 + 0x129b4);
        StaticFifoCompat *lane1 = (StaticFifoCompat *)(v0 + 0x12a10);
        int32_t old_status = arg1[0x4bc];

        CMG_KMSG("EncodeOneFrame queue-while-processing ch=%u ctx=%p status=%d src=%p stream=%p meta=%p",
                 (unsigned)(uint8_t)arg2, v0, old_status, arg3, arg4, arg5);
        AL_EncChannel_PushNewFrame(v0, arg3, arg4, arg5);
        arg1[0x4bc] = 2;
        CMG_KMSG("EncodeOneFrame queued-while-processing ctx=%p status=%d->2 q0=%p r/w/c=%d/%d/%d q1=%p r/w/c=%d/%d/%d",
                 v0, old_status, lane0, lane0->read_idx, lane0->write_idx, lane0->capacity,
                 lane1, lane1->read_idx, lane1->write_idx, lane1->capacity);
        Rtos_ReleaseMutex((void *)(intptr_t)arg1[0x4bd]);
        return 1;
    }

    CMG_KMSG("EncodeOneFrame pre-PushNewFrame ctx=%p src=%p stream=%p meta=%p",
             v0, arg3, arg4, arg5);
    AL_EncChannel_PushNewFrame(v0, arg3, arg4, arg5);
    {
        StaticFifoCompat *lane0 = (StaticFifoCompat *)(v0 + 0x129b4);
        StaticFifoCompat *lane1 = (StaticFifoCompat *)(v0 + 0x12a10);

        CMG_KMSG("EncodeOneFrame post-PushNewFrame ctx=%p q0=%p r/w/c=%d/%d/%d q1=%p r/w/c=%d/%d/%d",
                 v0, lane0, lane0->read_idx, lane0->write_idx, lane0->capacity,
                 lane1, lane1->read_idx, lane1->write_idx, lane1->capacity);
    }
    Rtos_ReleaseMutex((void *)(intptr_t)arg1[0x4bd]);
    CMG_KMSG("EncodeOneFrame pre-Process sch=%p", arg1);
    return Process(arg1);
}

int32_t AL_SchedulerEnc_GetRecPicture(int32_t *arg1, char arg2, int32_t *arg3)
{
    void *var_10 = &_gp;

    (void)var_10;
    return AL_RefMngr_GetNextRecPicture((uint8_t *)GetChMngrCtx(arg1, arg2) + 0x22c8, arg3);
}

int32_t AL_SchedulerEnc_ReleaseRecPicture(int32_t *arg1, char arg2, char arg3)
{
    void *var_18 = &_gp;

    (void)var_18;
    AL_RefMngr_ReleaseRecPicture((uint8_t *)GetChMngrCtx(arg1, arg2) + 0x22c8, arg3);
    Rtos_GetMutex((void *)(intptr_t)READ_S32(arg1, 0x12f4));
    CheckAndEncode(arg1);
    return (int32_t)(intptr_t)Rtos_ReleaseMutex((void *)(intptr_t)READ_S32(arg1, 0x12f4));
}

int32_t AL_SchedulerEnc_SetTraceCallBack(int32_t *arg1, char arg2, int32_t arg3, int32_t arg4)
{
    uint8_t *v0 = GetChMngrCtx(arg1, arg2);

    if (v0 != 0) {
        return AL_EncChannel_SetTraceCallBack(v0, arg3, arg4);
    }

    return UpdateCommand((void *)__assert("pChCtx",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/Scheduler.c", 0x429,
                         "AL_SchedulerEnc_SetTraceCallBack", &_gp),
                         0, 0, 0);
}
