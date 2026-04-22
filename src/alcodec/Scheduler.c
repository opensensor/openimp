#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "alcodec/al_allocator.h"
#include "alcodec/al_buffer.h"
#include "alcodec/al_rtos.h"
#include "alcodec/al_types.h"
#include "imp_log_int.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function,
                        void *caller);

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

int32_t AL_GetAllocSizeEP1(void); /* forward decl, ported by T<N> later */

#define ENC_KMSG(fmt, ...) IMP_LOG_INFO("ENC", fmt, ##__VA_ARGS__)

static void PrepareSourceConfigBeforeLaunch(AL_EncCoreCtxCompat *core, void *ch, void *req, uint32_t *cmd_regs)
{
    AL_IpCtrl *ip;
    uint32_t width;
    uint32_t height;
    uint32_t src_y;
    uint32_t src_uv;
    uint32_t ep1;
    uint32_t wpp;
    uint32_t stream_part;
    uint32_t hdr_off;
    uint32_t stream_budget;
    uint32_t core_base;
    uint32_t reg_85f4;
    uint32_t reg_85f0;
    uint32_t reg_83f4;
    uint32_t reg_8400;
    uint32_t reg_8420;
    uint32_t reg_8424;
    uint32_t reg_8428;
    uint32_t reg_85e4;

    if (core == NULL || ch == NULL || req == NULL || cmd_regs == NULL) {
        return;
    }

    ip = core->ip_ctrl;
    if (ip == NULL || ip->vtable == NULL || ip->vtable->WriteRegister == NULL) {
        return;
    }

    width = READ_U16(ch, 4);
    height = READ_U16(ch, 6);
    src_y = READ_U32(req, 0x298);
    src_uv = READ_U32(req, 0x29c);
    ep1 = READ_U32(req, 0x2fc);
    wpp = READ_U32(req, 0x2f8);
    stream_part = cmd_regs[0x31];
    hdr_off = cmd_regs[0x32];
    stream_budget = cmd_regs[0x33];
    core_base = ((uint32_t)core->core_id) << 9;

    if (width == 0 || height == 0 || src_y == 0 || src_uv == 0 || ep1 == 0) {
        ENC_KMSG("encode1 source-config skip core=%u w=%u h=%u srcY=0x%x srcUV=0x%x ep1=0x%x",
                 (unsigned)core->core_id, width, height, src_y, src_uv, ep1);
        return;
    }

    if (wpp == 0) {
        wpp = ep1 + (uint32_t)AL_GetAllocSizeEP1();
    }

    ip->vtable->WriteRegister(ip, 0x85f4, 1);
    ip->vtable->WriteRegister(ip, 0x85f0, 1);
    ip->vtable->WriteRegister(ip, core_base + 0x83f4, 1);
    ip->vtable->WriteRegister(ip, 0x8400, 0x00000131U);
    ip->vtable->WriteRegister(ip, 0x8404, (((width - 1U) & 0xffffU) << 16) | ((height - 1U) & 0xffffU));
    ip->vtable->WriteRegister(ip, 0x8408, 0x00010001U);
    ip->vtable->WriteRegister(ip, 0x840c, width);
    ip->vtable->WriteRegister(ip, 0x8410, src_y);
    ip->vtable->WriteRegister(ip, 0x8414, src_uv);
    ip->vtable->WriteRegister(ip, 0x8418, wpp);
    ip->vtable->WriteRegister(ip, 0x841c, ep1);
    ip->vtable->WriteRegister(ip, 0x8420, stream_part);
    ip->vtable->WriteRegister(ip, 0x8424, hdr_off);
    ip->vtable->WriteRegister(ip, 0x8428, stream_budget);
    ip->vtable->WriteRegister(ip, 0x85e4, 1);

    reg_85f4 = (uint32_t)ip->vtable->ReadRegister(ip, 0x85f4);
    reg_85f0 = (uint32_t)ip->vtable->ReadRegister(ip, 0x85f0);
    reg_83f4 = (uint32_t)ip->vtable->ReadRegister(ip, core_base + 0x83f4);
    reg_8400 = (uint32_t)ip->vtable->ReadRegister(ip, 0x8400);
    reg_8420 = (uint32_t)ip->vtable->ReadRegister(ip, 0x8420);
    reg_8424 = (uint32_t)ip->vtable->ReadRegister(ip, 0x8424);
    reg_8428 = (uint32_t)ip->vtable->ReadRegister(ip, 0x8428);
    reg_85e4 = (uint32_t)ip->vtable->ReadRegister(ip, 0x85e4);
    ENC_KMSG("encode1 source-config core=%u srcY=0x%x srcUV=0x%x ep1=0x%x wpp=0x%x part=0x%x hdr=0x%x budget=0x%x"
             " rb85f4=0x%x rb85f0=0x%x rb83f4=0x%x rb8400=0x%x rb8420=0x%x rb8424=0x%x rb8428=0x%x rb85e4=0x%x",
             (unsigned)core->core_id, src_y, src_uv, ep1, wpp, stream_part, hdr_off, stream_budget,
             reg_85f4, reg_85f0, reg_83f4, reg_8400, reg_8420, reg_8424, reg_8428, reg_85e4);
}

void StaticFifo_Init(StaticFifoCompat *arg1, int32_t *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t StaticFifo_Queue(StaticFifoCompat *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t StaticFifo_Front(StaticFifoCompat *arg1); /* forward decl, ported by T<N> later */
int32_t PopCommandListAddresses(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
void *RequestsBuffer_Init(void *arg1); /* forward decl, ported by T<N> later */
void *RequestsBuffer_Pop(void *arg1); /* forward decl, ported by T<N> later */
void EndRequestsBuffer_Init(void *arg1); /* forward decl, ported by T<N> later */
int32_t PreprocessHwRateCtrl(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4,
                             void *arg5); /* forward decl, ported by T<N> later */
int32_t InitHwRateCtrl(void *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6,
                       int32_t arg7, int32_t arg8,
                       void *arg9); /* forward decl, ported by T<N> later */
int32_t ResetChannelParam(void *arg1); /* forward decl, ported in this unit */
int32_t AL_RefMngr_GetAvailRef(void *arg1, void *arg2, int32_t *arg3); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_UpdateDPB(void *arg1, int32_t *arg2); /* forward decl, ported by T<N> later */
int32_t AL_DPB_AVC_CheckMMCORequired(void *arg1, void *arg2,
                                     int32_t *arg3); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_ReleaseFrmBuffer(void *arg1, char arg2); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetFrmBufAddrs(void *arg1, char arg2, int32_t *arg3, int32_t *arg4,
                                  void *arg5); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetMvBufAddr(void *arg1, char arg2, int32_t *arg3); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetRefInfo(int32_t arg1, int32_t arg2, void *arg3, void *arg4,
                              int32_t *arg5); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetRefBufferFromPOC(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetFrmBuffer(void *arg1, char arg2); /* forward decl, ported by T<N> later */
int32_t AL_GetEncoderFbcMapSize(int32_t arg1, int32_t arg2, int32_t arg3,
                                int32_t arg4); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetMapBufAddr(void *arg1, char arg2); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_MarkAsReadyForOutput(void *arg1, char arg2); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_StorePicture(void *arg1, void *arg2, char arg3); /* forward decl, ported by T<N> later */
static int32_t SetSourceBuffer_isra_74(void *arg1, int32_t *arg2, int32_t arg3,
                                       int32_t *arg4); /* forward decl */
int32_t AL_SrcReorder_MarkSrcBufferAsUsed(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_SrcReorder_EndSrcBuffer(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
extern void AL_SrcReorder_Cancel(void *arg1, int32_t arg2);
int32_t AL_IntermMngr_ReleaseBuffer(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
extern int32_t AL_EncCore_SetJpegInterrupt(void *arg1);
extern int32_t rc_Iol(void);

typedef struct AL_TIntermBufferCompat {
    int32_t addr;
    int32_t location;
} AL_TIntermBufferCompat;

uint32_t InitMERange(int32_t arg1, void *arg2)
{
    int32_t v0_3;

    if ((uint32_t)READ_U8(arg2, 0x1f) != 0U) {
        int16_t v1_5 = READ_S16(arg2, 6);
        int16_t a2_2 = READ_S16(arg2, 4);

        WRITE_U16((void *)(intptr_t)arg1, 0x48, (uint16_t)a2_2);
        WRITE_U16((void *)(intptr_t)arg1, 0x4a, (uint16_t)a2_2);
        WRITE_U16((void *)(intptr_t)arg1, 0x4c, (uint16_t)v1_5);
        WRITE_U16((void *)(intptr_t)arg1, 0x4e, (uint16_t)v1_5);
        v0_3 = READ_S32(arg2, 0x2c);

        if ((v0_3 & 0x20) != 0) {
            goto label_63fa4;
        }
    } else {
        uint32_t v0_1 = (uint32_t)READ_U8(arg2, 0x20);
        int16_t v0_2;
        int16_t a2;

        WRITE_U16((void *)(intptr_t)arg1, 0x4a, 0x780);
        WRITE_U16((void *)(intptr_t)arg1, 0x48, 0x780);
        if (v0_1 >= 0xbU) {
            if (v0_1 >= 0x15U) {
                int32_t v1_9 = (v0_1 < 0x1fU) ? 1 : 0;

                a2 = 0x1ef;
                if (v1_9 != 0) {
                    a2 = 0xef;
                }

                v0_2 = 0x1f7;
                if (v1_9 != 0) {
                    v0_2 = 0xf7;
                }
            } else {
                a2 = 0x6f;
                v0_2 = 0x77;
            }
        } else {
            a2 = 0x2f;
            v0_2 = 0x37;
        }

        WRITE_U16((void *)(intptr_t)arg1, 0x4c, (uint16_t)v0_2);
        v0_3 = READ_S32(arg2, 0x2c);
        WRITE_U16((void *)(intptr_t)arg1, 0x4e, (uint16_t)a2);
        if ((v0_3 & 0x20) != 0) {
label_63fa4:
            if ((uint32_t)READ_U16((void *)(intptr_t)arg1, 0x4c) >= 0xe1U) {
                WRITE_U16((void *)(intptr_t)arg1, 0x4c, 0xe0);
            }

            if ((uint32_t)READ_U16((void *)(intptr_t)arg1, 0x4e) >= 0xe1U) {
                WRITE_U16((void *)(intptr_t)arg1, 0x4e, 0xe0);
            }
        }
    }

    {
        uint32_t result = 0;

        if (((uint32_t)v0_3 >> 0x11 & 1U) != 0U) {
            uint32_t v1_4 = (uint32_t)READ_U16(arg2, 0x42);
            uint32_t t4_1 = (uint32_t)READ_U16((void *)(intptr_t)arg1, 0x48);
            uint32_t t3_1 = (uint32_t)READ_U16((void *)(intptr_t)arg1, 0x4a);
            uint32_t result_2 = (uint32_t)READ_U16((void *)(intptr_t)arg1, 0x4c);
            uint32_t result_1 = (uint32_t)READ_U16((void *)(intptr_t)arg1, 0x4e);

            result = (uint32_t)READ_U16(arg2, 0x44);
            if ((int32_t)v1_4 < (int32_t)t4_1) {
                t4_1 = v1_4;
            }
            if ((int32_t)result < (int32_t)result_2) {
                result_2 = result;
            }
            if ((int32_t)v1_4 >= (int32_t)t3_1) {
                v1_4 = t3_1;
            }
            if ((int32_t)result >= (int32_t)result_1) {
                result = result_1;
            }

            WRITE_U16((void *)(intptr_t)arg1, 0x48, (uint16_t)t4_1);
            WRITE_U16((void *)(intptr_t)arg1, 0x4a, (uint16_t)v1_4);
            WRITE_U16((void *)(intptr_t)arg1, 0x4c, (uint16_t)result_2);
            WRITE_U16((void *)(intptr_t)arg1, 0x4e, (uint16_t)result);
        }

        return result;
    }
}

int32_t FillSliceParamFromPicParam(int32_t *arg1, void *arg2, int32_t *arg3)
{
    void *var_20 = &_gp;
    int32_t s2 = 0;
    uint32_t a1 = (uint32_t)arg3[0x14];
    int32_t v0 = (int32_t)READ_S8(arg1, 0xe4);
    int32_t v0_1;
    int32_t t2;
    int32_t result;

    (void)var_20;
    if ((uint32_t)READ_U8(arg1, 0x7c) == 1U) {
        s2 = ((arg3[0xc] ^ 7) < 1) ? 1 : 0;
    }

    if (a1 != 0U) {
        s2 = 1;
    }

    if (v0 == -1) {
        char v0_16 = 2;

        if (arg3[0xc] != 0) {
            v0_16 = 3;
        }

        WRITE_U8(arg2, 0xf, (uint8_t)v0_16);
    } else {
        WRITE_U8(arg2, 0xf, (uint8_t)v0);
    }

    v0_1 = arg3[0xc];
    if (v0_1 == 2) {
        char v1_11 = READ_S8(arg3, 0x364);

        WRITE_U8(arg2, 0x21, 0);
        WRITE_U8(arg2, 0x20, (uint8_t)v1_11);
label_641f0:
        {
            int32_t a0_2 = arg1[0x48];

            WRITE_S32(arg2, 0x30, v0_1);
            ENC_KMSG("FillSliceParam rc_pre ch=%p rc_cb=%p rc_mutex=%p pic=%p slice=%p pict_type=%d scene=%u",
                     arg1, (void *)(intptr_t)arg1[0x41], (void *)(uintptr_t)a0_2, &arg3[8], arg2,
                     v0_1, (unsigned)READ_U8(arg3, 0x364));
            Rtos_GetMutex((void *)(uintptr_t)a0_2);
            ((void (*)(void *, void *, void *))(intptr_t)arg1[0x41])(&arg1[0x3d], &arg3[8],
                                                                      (uint8_t *)arg2 + 0x28);
            Rtos_ReleaseMutex((void *)(uintptr_t)arg1[0x48]);
            ENC_KMSG("FillSliceParam rc_post ch=%p pic=%p slice=%p rc_type=%d qp=%d",
                     arg1, &arg3[8], arg2, READ_S32(arg2, 0x30), READ_S16((uint8_t *)arg2 + 0x28, 4));
            t2 = READ_S32(arg2, 0x30);
        }
    } else {
        a1 = (uint32_t)READ_U8(arg3, 0x364);
        WRITE_U8(arg2, 0x21, (uint8_t)((READ_U32(arg3, 0) >> 3) & 1U));
        WRITE_U8(arg2, 0x20, (uint8_t)a1);
        if (v0_1 != 7) {
            goto label_641f0;
        }

        WRITE_S32(arg2, 0x30, 1);
        WRITE_S32(arg2, 0x28, 0x1a);
        t2 = 1;
    }

    if (s2 != 0) {
        WRITE_S32(arg2, 0x38, 1);
    }

    {
        uint32_t v0_2 = (uint32_t)READ_U8(arg3, 0xcc);
        uint32_t v0_4 = (uint32_t)READ_U8(arg3, 0xcd);
        char v0_8;
        char a1_1;
        char a0_1;
        char v1_7;
        int32_t v0_13;

        WRITE_S32(arg2, 0x3c, 0);
        if (v0_2 != 0U) {
            v0_2 = (uint32_t)((uint8_t)v0_2 - 1U);
        }
        WRITE_U8(arg2, 0x41, (uint8_t)v0_2);
        if (v0_4 != 0U) {
            v0_4 = (uint32_t)((uint8_t)v0_4 - 1U);
        }
        WRITE_U8(arg2, 0x40, (uint8_t)v0_4);
        v0_8 = (((uint32_t)arg3[0x1b] ^ 1U) < 1U) ? 1 : 0;
        WRITE_U8(arg2, 0x8e, (((uint32_t)arg3[0x16] ^ 1U) < 1U) ? 1 : 0);
        WRITE_U8(arg2, 0x96, (uint8_t)v0_8);
        WRITE_U8(arg2, 0x8c, READ_U8(arg3, 0xce));
        WRITE_U8(arg2, 0x94, READ_U8(arg3, 0xcf));
        a1_1 = READ_S8(arg3, 0xdb);
        a0_1 = READ_S8(arg3, 0xdc);
        v1_7 = READ_S8(arg3, 0xdd);
        WRITE_U8(arg2, 0x5f, READ_U8(arg3, 0xda));
        WRITE_U8(arg2, 0x60, (uint8_t)a1_1);
        WRITE_U8(arg2, 0x61, (uint8_t)a0_1);
        WRITE_U8(arg2, 0x62, (uint8_t)v1_7);
        if ((arg3[9] & 2) != 0) {
            WRITE_U8(arg2, 0x69, 0);
            WRITE_U8(arg2, 0x6a, 0);
        }

        v0_13 = arg3[0x21];
        if (~(uint32_t)v0_13 == 0U) {
            v0_13 = 0;
        }

        {
            int32_t a0_2 = arg3[0x17];
            int32_t v1_8 = arg3[0x1c];
            int32_t t1 = arg3[0x22];
            int32_t t0 = arg3[0x23];
            int32_t a3 = arg3[0x14];
            int16_t a2_1 = READ_S16(arg3, 0x138);
            uint32_t word9 = (uint32_t)arg3[9];
            uint32_t word11 = (uint32_t)arg3[0xb];
            uint32_t word12 = (uint32_t)arg3[0x12];

            if (~(uint32_t)a0_2 == 0U) {
                a0_2 = 0;
            }
            if (~(uint32_t)v1_8 == 0U) {
                v1_8 = 0;
            }

            WRITE_S32(arg2, 0x84, arg3[0xa]);
            WRITE_S32(arg2, 0x88, a0_2);
            WRITE_S32(arg2, 0x90, v1_8);
            WRITE_S32(arg2, 0x98, v0_13);
            WRITE_S32(arg2, 0xa0, t1);
            WRITE_S32(arg2, 0xa4, t0);
            WRITE_S32(arg2, 0xf0, a3);
            WRITE_U16(arg2, 0xf4, (uint16_t)a2_1);

            if (a3 == 0) {
                WRITE_U8(arg2, 0x60, (uint8_t)((word9 >> 0xf) & 1U));
                WRITE_U8(arg2, 0x61, (uint8_t)((word9 >> 0x12) & 1U));
                WRITE_U8(arg2, 0x62, (uint8_t)((word9 >> 0xb) & 1U));
            } else if (a3 == 1) {
                WRITE_U8(arg2, 0x5f, (uint8_t)((word9 >> 0xf) & 1U));
                WRITE_U8(arg2, 0x65, (uint8_t)((word9 >> 0x12) & 1U));
            }

            WRITE_U8(arg2, 0x63, (uint8_t)((word9 >> 0x10) & 1U));
            WRITE_U8(arg2, 0x64, (uint8_t)((word9 >> 0x11) & 1U));
            WRITE_U8(arg2, 0x66, (uint8_t)((word9 >> 0x13) & 1U));
            WRITE_U8(arg2, 0x67, (uint8_t)((word9 >> 0x14) & 1U));
            WRITE_U8(arg2, 0x69, (uint8_t)((word9 >> 0x16) & 1U));
            WRITE_U8(arg2, 0x6a, (uint8_t)((word9 >> 0x17) & 1U));
            WRITE_U8(arg2, 0x6b, (uint8_t)((word9 >> 0x18) & 1U));
            WRITE_U8(arg2, 0x6c, (uint8_t)((word9 >> 0x19) & 1U));
            WRITE_U8(arg2, 0x6d, (uint8_t)((word9 >> 0x1a) & 1U));
            WRITE_U8(arg2, 0x6e, (uint8_t)((word9 >> 0x1b) & 1U));
            WRITE_U8(arg2, 0x6f, (uint8_t)((word9 >> 0x1c) & 1U));
            WRITE_U8(arg2, 0x70, (uint8_t)((word9 >> 0x1d) & 1U));
            WRITE_U8(arg2, 0x71, (uint8_t)((word9 >> 0x1e) & 1U));
            WRITE_U8(arg2, 0x72, (uint8_t)((word9 >> 0x1f) & 1U));
            WRITE_U16(arg2, 0x74, (uint16_t)arg3[0xa]);
            WRITE_U8(arg2, 0x7e, (uint8_t)(((word11 >> 0x18) & 3U) + 1U));
            WRITE_U8(arg2, 0x7f, (uint8_t)((word11 >> 0x1e) & 1U));
            WRITE_U8(arg2, 0x80, (uint8_t)((word11 >> 0x1f) & 1U));
            WRITE_U16(arg2, 0x7a, (uint16_t)(word11 & 0x3ffU));
            WRITE_U16(arg2, 0x7c, (uint16_t)(((word11 >> 0xc) & 0x3ffU) + 1U));
            WRITE_U16(arg2, 0xa8, (uint16_t)((((word12 & 0x3ffU) + 1U) << 6) & 0xffffU));
            WRITE_U16(arg2, 0xaa, (uint16_t)((((((word12 >> 0xc) & 0x3ffU) + 1U) << 3) & 0xffffU)));
            WRITE_U8(arg2, 0xac, (uint8_t)((word12 >> 0x1e) & 1U));
            ENC_KMSG("FillSliceParam slice_tail pic=%p slice=%p pic9=%08x picb=%08x pic12=%08x s63=%u 7a=%u 7c=%u a8=%u aa=%u ac=%u",
                     arg3, arg2, (unsigned)word9, (unsigned)word11, (unsigned)word12,
                     (unsigned)READ_U8(arg2, 0x63), (unsigned)READ_U16(arg2, 0x7a),
                     (unsigned)READ_U16(arg2, 0x7c), (unsigned)READ_U16(arg2, 0xa8),
                     (unsigned)READ_U16(arg2, 0xaa), (unsigned)READ_U8(arg2, 0xac));
        }
    }

    if (t2 == 2) {
        result = *arg1;
        if (t2 == 2 && result <= 0) {
            return result;
        }
    }

    result = ((uint32_t)(arg3[0x1b] ^ 2) > 0U) ? 1 : 0;
    WRITE_U8(arg2, 0x8f, ((uint32_t)(arg3[0x16] ^ 2) > 0U) ? 1 : 0);
    WRITE_U8(arg2, 0x97, (uint8_t)result);
    return result;
}

uint32_t SetTileOffsets(void *arg1)
{
    uint32_t t4 = (uint32_t)READ_U16(arg1, 0x4e);
    uint32_t t1 = (uint32_t)READ_U8(arg1, 0x3c);
    int32_t t5 = 1 << (t4 & 0x1f);
    int32_t a3 = 1 << ((6 - t4) & 0x1f);
    int32_t t2_3 = (READ_S32(arg1, 4) + t5 - 1) >> (t4 & 0x1f);

    if (READ_S32(arg1, 0x35b8) < (int32_t)t1 - 1) {
        return ResetChannelParam((void *)(intptr_t)__assert(
            "(pCtx->ChanParam.uNumCore - 1) <= pCtx->iIpCoresCount",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c", 0x851,
            "SetTileOffsets", &_gp));
    }

    {
        int32_t a1 = 0;

        if ((int32_t)t1 - 1 > 0) {
            int32_t *t0_1 = (int32_t *)((uint8_t *)arg1 + 0x12f54);
            int16_t *v1_1 = (int16_t *)((uint8_t *)arg1 + 0x12d7c);
            int32_t a2_2 = t2_3 + (int32_t)t1 - 1;
            int16_t *t3_3 = (int16_t *)((uint8_t *)arg1 + (((int32_t)t1 + 0x96bd) << 1));

            while (1) {
                int32_t v0_7;

                if (t1 == 0) {
                    __builtin_trap();
                }

                *t0_1 = 0;
                v0_7 = a2_2 / (int32_t)t1 - a1;
                *v1_1 = (int16_t)a1;
                if (v0_7 >= 0) {
                    int32_t v0_5;

                    if (a3 == 0) {
                        __builtin_trap();
                    }

                    v1_1 = &v1_1[1];
                    t0_1 = &t0_1[1];
                    a2_2 += t2_3;
                    v0_5 = ((a3 + v0_7) - 1) / a3 * a3;
                    *(v1_1 - 0x22) = (int16_t)v0_5;
                    a1 += v0_5;
                    if (v1_1 == t3_3) {
                        break;
                    }
                } else {
                    int32_t v0_9;

                    if (a3 == 0) {
                        __builtin_trap();
                    }

                    v1_1 = &v1_1[1];
                    t0_1 = &t0_1[1];
                    a2_2 += t2_3;
                    v0_9 = v0_7 / a3 * a3;
                    *(v1_1 - 0x22) = (int16_t)v0_9;
                    a1 += v0_9;
                    if (v1_1 == t3_3) {
                        break;
                    }
                }
            }

            a1 &= 0xffff;
        }

        WRITE_S32(arg1, (((int32_t)t1 + 0x4bd3) << 2) + 4, 0);
        WRITE_U16(arg1, ((int32_t)t1 << 1) + 0x12d7a, (uint16_t)a1);
        WRITE_U16(arg1, ((int32_t)t1 << 1) + 0x12d5a, (uint16_t)(t2_3 - a1));

        {
            uint32_t v0_13 = (uint32_t)READ_U16(arg1, 6);
            int32_t t4_1 = (v0_13 + t5 - 1) >> (t4 & 0x1f);

            if (t1 == 1U) {
                WRITE_U16(arg1, 0x12d9c, (uint16_t)t4_1);
                return v0_13;
            }

            {
                uint32_t a1_1 = (uint32_t)READ_U8(arg1, 0x40);
                int32_t v0_14 = (int32_t)a1_1 - 1;

                if (a1_1 != 0U) {
                    int16_t *v1_2 = (int16_t *)((uint8_t *)arg1 + 0x12d9c);
                    int16_t *a0 = (int16_t *)((uint8_t *)arg1 + (((v0_14 & 0xffff) + 0x96cf) << 1));

                    v0_14 = 0;
                    do {
                        int16_t lo_4 = (int16_t)(v0_14 / (int32_t)a1_1);

                        if (a1_1 == 0U) {
                            __builtin_trap();
                        }

                        v0_14 += t4_1;
                        v1_2 = &v1_2[1];
                        if (a1_1 == 0U) {
                            __builtin_trap();
                        }

                        *(v1_2 - 1) = (int16_t)(v0_14 / (int32_t)a1_1) - lo_4;
                    } while (a0 != v1_2);
                }

                return (uint32_t)v0_14;
            }
        }
    }
}

int32_t ResetChannelParam(void *arg1)
{
    void *var_18 = &_gp;

    (void)var_18;
    StaticFifo_Init((StaticFifoCompat *)((uint8_t *)arg1 + 0x129b4), (int32_t *)((uint8_t *)arg1 + 0x12968), 0x13);
    StaticFifo_Init((StaticFifoCompat *)((uint8_t *)arg1 + 0x12a6c), (int32_t *)((uint8_t *)arg1 + 0x12a20), 0x13);
    StaticFifo_Init((StaticFifoCompat *)((uint8_t *)arg1 + 0x12a10), (int32_t *)((uint8_t *)arg1 + 0x129c4), 0x13);
    StaticFifo_Init((StaticFifoCompat *)((uint8_t *)arg1 + 0x12ac8), (int32_t *)((uint8_t *)arg1 + 0x12a7c), 0x13);
    StaticFifo_Init((StaticFifoCompat *)((uint8_t *)arg1 + 0x12b24), (int32_t *)((uint8_t *)arg1 + 0x12ad8), 0x13);
    WRITE_S32(arg1, 0x35bc, 0);
    WRITE_S32(arg1, 0x12960, 0);
    WRITE_S32(arg1, 0x35b0, 0);
    WRITE_S32(arg1, 0x35b4, 0);
    WRITE_S32(arg1, 0x3de0, 1);
    RequestsBuffer_Init((uint8_t *)arg1 + 0x3de8);
    EndRequestsBuffer_Init((uint8_t *)arg1 + 0x12878);
    return 0;
}

uint32_t InitHwRC_Content(void *arg1, void *arg2)
{
    int32_t *s4 = (int32_t *)((uint8_t *)arg1 + 0x35d0);
    int32_t i;
    uint32_t i_1;
    int32_t v1;
    uint32_t s0;

    for (i = 0; i != 3; ) {
        int32_t i_2 = i;

        i += 1;
        PreprocessHwRateCtrl((int32_t *)((uint8_t *)arg2 + 0x68), READ_S32(arg2, 0xa8), (int32_t)READ_U8(arg2, 0x3c),
                             i_2, (void *)(intptr_t)(*s4));
        s4 = &s4[1];
    }

    i_1 = (uint32_t)READ_U8(arg2, 0x3c);
    v1 = 0xd48;
    if ((uint32_t)READ_U8(arg2, 0x1f) == 0U) {
        v1 = 0x352;
    }

    s0 = 0;
    if (i_1 != 0U) {
        do {
            InitHwRateCtrl((uint8_t *)arg1 + s0 * 0x78 + 0x35e4, (uint8_t *)arg2 + 0x68, READ_S32(arg2, 0xa8),
                           (int32_t)READ_U16(arg2, 4), (int32_t)READ_U16(arg2, 6), (int32_t)READ_U8(arg2, 0x4e), v1,
                           (int32_t)READ_U16(arg1, ((int32_t)(s0 + 0x96a8) << 1) + 0xc), &_gp);
            i_1 = (uint32_t)READ_U8(arg2, 0x3c);
            s0 = (uint32_t)((uint8_t)s0 + 1U);
        } while (s0 < i_1);
    }

    if (i_1 != 0U) {
        i_1 = 0;

        {
            int32_t t0_2 = (READ_S32(arg1, 0x35b8) < 2) ? 1 : 0;

            do {
                uint32_t t2_1 = i_1 << 3;

                while (1) {
                    uint32_t v1_10 = i_1 << 7;
                    uint8_t *a1_6 = (uint8_t *)arg1 + v1_10 - t2_1;

                    if (t0_2 == 0) {
                        break;
                    }

                    {
                        int32_t a3_2 = (int32_t)i_1 + 1;
                        char t1_2 = 0;
                        char a1_4 = 0;
                        uint8_t *v1_7 = (uint8_t *)arg1 + v1_10 - t2_1;

                        if (i_1 != 0U) {
                            t1_2 = ((uint32_t)READ_U8(arg1, (int32_t)(i_1 - 1U) * 0x78 + 0x35fa) < 2U) ? 1 : 0;
                        }

                        *(v1_7 + 0x3654) = (uint8_t)t1_2;
                        *(v1_7 + 0x3618) = (uint8_t)t1_2;
                        if ((int32_t)i_1 < (int32_t)READ_U8(arg1, 0x3c) - 1) {
                            a1_4 = ((uint32_t)READ_U8(arg1, a3_2 * 0x78 + 0x35fa) < 2U) ? 1 : 0;
                        }

                        *(v1_7 + 0x3656) = (uint8_t)i_1;
                        *(v1_7 + 0x361a) = (uint8_t)i_1;
                        *(v1_7 + 0x3655) = (uint8_t)a1_4;
                        *(v1_7 + 0x3619) = (uint8_t)a1_4;
                        i_1 = (uint32_t)(a3_2 & 0xff);
                        if (i_1 >= (uint32_t)READ_U8(arg2, 0x3c)) {
                            return i_1;
                        }
                    }
                }

                {
                    uint8_t *v1_13 = (uint8_t *)arg1 + i_1 * 0x78;

                    *(v1_13 + 0x3654) = 0;
                    *(v1_13 + 0x3618) = 0;
                    *(v1_13 + 0x3655) = 0;
                    *(v1_13 + 0x3619) = 0;
                    *(v1_13 + 0x3656) = 0;
                    *(v1_13 + 0x361a) = 0;
                    i_1 = (uint32_t)((uint8_t)i_1 + 1U);
                    if (i_1 >= (uint32_t)READ_U8(arg2, 0x3c)) {
                        return i_1;
                    }
                }
            } while (1);
        }
    }

    return i_1;
}

void *getFifoRunning(void *arg1, int32_t arg2)
{
    void *s0 = (uint8_t *)arg1 + 0x12a6c;
    void *var_18 = &_gp;
    int32_t v0 = StaticFifo_Front((StaticFifoCompat *)s0);
    void *v0_3;
    int32_t v1_1;

    (void)var_18;
    if (v0 != 0) {
        v1_1 = READ_S32((void *)(intptr_t)v0, 0xa70);
        v0_3 = s0;
        if (v0 != 0 && READ_S32((void *)(intptr_t)v0, (((v1_1 * 0x44) + arg2 + 0x230) << 2) + 0x10) > 0) {
            return v0_3;
        }
    }

    {
        void *s0_1 = (uint8_t *)arg1 + 0x12ac8;
        int32_t v0_4 = StaticFifo_Front((StaticFifoCompat *)s0_1);

        if (v0_4 == 0) {
            return 0;
        }

        v0_3 = 0;
        if (READ_S32((void *)(intptr_t)v0_4,
                     (((READ_S32((void *)(intptr_t)v0_4, 0xa70) * 0x44) + arg2 + 0x230) << 2) + 0x10) > 0) {
            return s0_1;
        }

        return v0_3;
    }
}

uint32_t SetPictureReferences(void *arg1, void *arg2)
{
    void *var_280 = &_gp;
    int32_t var_278;
    int32_t str[0x33];

    (void)var_280;
    AL_RefMngr_GetAvailRef((uint8_t *)arg1 + 0x22c8, (uint8_t *)arg2 + 0x20, &var_278);
    memset(&str, 0, 0xcc);
    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    ((void (*)(void *, void *, void *, void *, void *))(intptr_t)READ_S32(arg1, 0x13c))((uint8_t *)arg1 + 0x128,
                                                                                        (uint8_t *)arg2 + 0x20,
                                                                                        &var_278,
                                                                                        (uint8_t *)arg2 + 0x54, &str);
    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    WRITE_U8(arg2, 0x164, (uint8_t)str[0] + READ_U8(&str, 0x60));

    {
        uint32_t result = (uint32_t)READ_U8(arg1, 0x1f);

        if (result == 1U) {
            return (uint32_t)AL_RefMngr_UpdateDPB((uint8_t *)arg1 + 0x22c8, &str[0]);
        }

        if (result == 0U) {
            result = (uint32_t)AL_DPB_AVC_CheckMMCORequired((uint8_t *)arg1 + 0x22c8, (uint8_t *)arg2 + 0x20, &str[0]);
            WRITE_S32(arg2, 0xd4, (int32_t)result);
        }

        return result;
    }
}

int32_t ReleaseRefBuffers(void *arg1, void *arg2)
{
    void *var_18 = &_gp;
    int32_t result;

    (void)var_18;
    if (READ_S32(arg2, 0x2c0) != 0) {
        AL_RefMngr_ReleaseFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x291));
    }

    if (READ_S32(arg2, 0x2d0) != 0) {
        AL_RefMngr_ReleaseFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x292));
    }

    result = READ_S32(arg2, 0x2f0);
    if (result != 0) {
        return AL_RefMngr_ReleaseFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x293));
    }

    return result;
}

void *SetPictureRefBuffers(void *arg1, void *arg2, void *arg3, void *arg4, char arg5, void *arg6)
{
    uint32_t s5 = (uint32_t)(uint8_t)arg5;
    void *var_30 = &_gp;
    uint32_t var_28;

    (void)var_30;
    ENC_KMSG("SetPictureRefBuffers entry chctx=%p req=%p cur=%u info=%p work=%p rec=%u",
             arg1, arg2, s5, arg4, arg6, (unsigned)READ_U8(arg2, 0x290));
    AL_RefMngr_GetFrmBufAddrs((uint8_t *)arg1 + 0x22c8, (char)s5, (int32_t *)((uint8_t *)arg6 + 0x48),
                              (int32_t *)((uint8_t *)arg6 + 0x4c), (uint8_t *)arg6 + 0x74);
    ENC_KMSG("SetPictureRefBuffers cur-addrs rec=%u y=0x%x uv=0x%x trace=0x%x/0x%x/%u",
             s5, READ_S32(arg6, 0x48), READ_S32(arg6, 0x4c),
             READ_S32(arg6, 0x74), READ_S32(arg6, 0x78), (unsigned)READ_U8(arg6, 0x7c));

    {
        int32_t v0_1 = AL_RefMngr_GetMvBufAddr((uint8_t *)arg1 + 0x22c8, (char)s5, (int32_t *)((uint8_t *)arg6 + 0x84));
        int32_t v1 = READ_S32(arg6, 0x84);
        int32_t a1_2 = READ_S32(arg3, 0x1c);
        int32_t v0_2;
        int32_t a1_6;
        int32_t v0_3;
        int32_t v0_4;
        int32_t v0_5;
        int32_t v1_2;

        WRITE_S32(arg6, 0x5c, v0_1);
        ENC_KMSG("SetPictureRefBuffers cur-mv rec=%u mv=0x%x coloc=%p",
                 s5, v0_1, (void *)(intptr_t)v1);
        ENC_KMSG("SetPictureRefBuffers before-ref-info mode=0x%x", READ_S32(arg3, 0x1c));
        AL_RefMngr_GetRefInfo((int32_t)(intptr_t)((uint8_t *)arg1 + 0x22c8), a1_2, (uint8_t *)arg4 + 0x20,
                              (uint8_t *)arg4 + 0x54, (int32_t *)(intptr_t)v1);
        ENC_KMSG("SetPictureRefBuffers after-ref-info pocL0=%d pocL1=%d coloc0=%d coloc1=%d",
                 READ_S32(arg4, 0x5c), READ_S32(arg4, 0x70),
                 READ_S32(arg4, 0x84), READ_S32(arg4, 0x88));
        ENC_KMSG("SetPictureRefBuffers before-ref0-from-poc poc=%d", READ_S32(arg4, 0x5c));
        v0_2 = AL_RefMngr_GetRefBufferFromPOC((uint8_t *)arg1 + 0x22c8, READ_S32(arg4, 0x5c));
        ENC_KMSG("SetPictureRefBuffers after-ref0-from-poc buf=%d", v0_2);
        AL_RefMngr_GetFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)v0_2);
        WRITE_U8(arg2, 0x291, (uint8_t)v0_2);
        AL_RefMngr_GetFrmBufAddrs((uint8_t *)arg1 + 0x22c8, (char)v0_2, (int32_t *)((uint8_t *)arg6 + 0x28),
                                  (int32_t *)((uint8_t *)arg6 + 0x2c), 0);
        ENC_KMSG("SetPictureRefBuffers ref0 buf=%d y=0x%x uv=0x%x",
                 v0_2, READ_S32(arg6, 0x28), READ_S32(arg6, 0x2c));
        WRITE_U8(arg2, 0x290, (uint8_t)s5);
        a1_6 = READ_S32(arg4, 0x70);
        WRITE_S32(arg6, 0x50, 0);
        WRITE_S32(arg6, 0x54, 0);
        WRITE_S32(arg6, 0x30, 0);
        WRITE_S32(arg6, 0x34, 0);
        WRITE_S32(arg6, 0x40, 0);
        WRITE_S32(arg6, 0x44, 0);
        ENC_KMSG("SetPictureRefBuffers before-ref1-from-poc poc=%d", a1_6);
        v0_3 = AL_RefMngr_GetRefBufferFromPOC((uint8_t *)arg1 + 0x22c8, a1_6);
        ENC_KMSG("SetPictureRefBuffers after-ref1-from-poc buf=%d", v0_3);
        AL_RefMngr_GetFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)v0_3);
        WRITE_U8(arg2, 0x292, (uint8_t)v0_3);
        AL_RefMngr_GetFrmBufAddrs((uint8_t *)arg1 + 0x22c8, (char)v0_3, (int32_t *)((uint8_t *)arg6 + 0x38),
                                  (int32_t *)((uint8_t *)arg6 + 0x3c), 0);
        ENC_KMSG("SetPictureRefBuffers ref1 buf=%d y=0x%x uv=0x%x",
                 v0_3, READ_S32(arg6, 0x38), READ_S32(arg6, 0x3c));
        ENC_KMSG("SetPictureRefBuffers before-coloc-from-poc poc=%d", READ_S32(arg4, 0x84));
        v0_4 = AL_RefMngr_GetRefBufferFromPOC((uint8_t *)arg1 + 0x22c8, READ_S32(arg4, 0x84));
        ENC_KMSG("SetPictureRefBuffers after-coloc-from-poc buf=%d", v0_4);
        AL_RefMngr_GetFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)v0_4);
        WRITE_U8(arg2, 0x293, (uint8_t)v0_4);
        v0_5 = AL_RefMngr_GetMvBufAddr((uint8_t *)arg1 + 0x22c8, (char)v0_4, (int32_t *)&var_28);
        ENC_KMSG("SetPictureRefBuffers coloc buf=%d mv=0x%x trace=0x%x",
                 v0_4, v0_5, var_28);
        v1_2 = READ_S32(arg3, 0x2c) & 0x20;
        WRITE_S32(arg6, 0x58, v0_5);
        if (v1_2 != 0) {
            uint32_t v0_6 = (uint32_t)READ_U16(arg3, 4);
            uint32_t a3_4 = (uint32_t)READ_U8(arg3, 0x1f);
            uint32_t a2_6 = (uint32_t)READ_U16(arg3, 6);
            int32_t v0_8;
            int32_t v0_9;

            var_28 = v0_6;
            v0_8 = 0x10;
            if (a3_4 != 0U) {
                v0_8 = 8;
            }

            {
                int32_t v0_7 = AL_GetEncoderFbcMapSize(0, (int32_t)v0_6, (int32_t)a2_6, v0_8);

                ENC_KMSG("SetPictureRefBuffers fbc map-size=%d bitdepth=%d rec=%u ref0=%u ref1=%u",
                         v0_7, v0_8, (unsigned)READ_U8(arg2, 0x290),
                         (unsigned)READ_U8(arg2, 0x291), (unsigned)READ_U8(arg2, 0x292));
                v0_9 = AL_RefMngr_GetMapBufAddr((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x290));
                WRITE_S32(arg6, 0x50, v0_9);
                WRITE_S32(arg6, 0x54, v0_7 + v0_9);
                if (READ_S32(arg6, 0x30) == 0) {
                    int32_t v0_12 = AL_RefMngr_GetMapBufAddr((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x291));

                    WRITE_S32(arg6, 0x30, v0_12);
                    WRITE_S32(arg6, 0x34, v0_7 + v0_12);
                    if (READ_S32(arg6, 0x40) == 0) {
                        int32_t v0_11 = AL_RefMngr_GetMapBufAddr((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x292));

                        WRITE_S32(arg6, 0x40, v0_11);
                        WRITE_S32(arg6, 0x44, v0_7 + v0_11);
                    }
                } else if (READ_S32(arg6, 0x40) == 0) {
                    int32_t v0_11 = AL_RefMngr_GetMapBufAddr((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x292));

                    WRITE_S32(arg6, 0x40, v0_11);
                    WRITE_S32(arg6, 0x44, v0_7 + v0_11);
                }
            }
        }

        {
            void *result = READ_PTR(arg6, 0x84);

            ENC_KMSG("SetPictureRefBuffers done rec=%u ref0=%u ref1=%u coloc=%u result=%p map0=0x%x map1=0x%x map2=0x%x",
                     (unsigned)READ_U8(arg2, 0x290), (unsigned)READ_U8(arg2, 0x291),
                     (unsigned)READ_U8(arg2, 0x292), (unsigned)READ_U8(arg2, 0x293),
                     result, READ_S32(arg6, 0x50), READ_S32(arg6, 0x30), READ_S32(arg6, 0x40));
            if (result != 0) {
                WRITE_S32(result, 0x84, READ_S32(arg4, 0x5c));
                WRITE_S32(result, 0x88, READ_S32(arg4, 0x70));
            }

            return result;
        }
    }
}

int32_t AddNewRequest(int32_t arg1)
{
    void *var_18 = &_gp;
    int32_t v0;

    (void)var_18;
    ENC_KMSG("AddNewRequest entry chctx=%p reqbuf=%p endbuf=%p active=%d eos=%d lane0_r=%d lane0_w=%d lane1_r=%d lane1_w=%d",
             (void *)(intptr_t)arg1,
             (void *)(intptr_t)(arg1 + 0x3de8),
             (void *)(intptr_t)(arg1 + 0x12878),
             READ_S32((void *)(intptr_t)arg1, 0x2c),
             READ_S32((void *)(intptr_t)arg1, 0x30),
             READ_S32((void *)(intptr_t)arg1, 0x3d8),
             READ_S32((void *)(intptr_t)arg1, 0x3dc),
             READ_S32((void *)(intptr_t)arg1, 0x434),
             READ_S32((void *)(intptr_t)arg1, 0x438));
    v0 = (int32_t)(intptr_t)RequestsBuffer_Pop((void *)(intptr_t)(arg1 + 0x3de8));
    ENC_KMSG("AddNewRequest pop chctx=%p req=%p", (void *)(intptr_t)arg1, (void *)(intptr_t)v0);
    Rtos_Memset((void *)(intptr_t)v0, 0, 0xc58);
    /*
     * Request-local module tables mirror the channel step template exactly:
     * channel 0x18c0..0x1adf -> request 0x84c..0xa6b and channel 0x1ae0 -> request 0xa6c.
     * Without this copy, freshly queued requests never advertise any modules to
     * CheckAndEncode, so they are prepared and then dropped before AVPU launch.
     */
    Rtos_Memcpy((void *)(intptr_t)(v0 + 0x84c), (void *)(intptr_t)(arg1 + 0x18c0), 0x220);
    WRITE_S32((void *)(intptr_t)v0, 0xa6c, READ_S32((void *)(intptr_t)arg1, 0x1ae0));
    ENC_KMSG("AddNewRequest seeded req=%p grp_cnt=%d lane0_mods=%d lane0_slices=%d lane1_mods=%d lane1_slices=%d",
             (void *)(intptr_t)v0, READ_S32((void *)(intptr_t)v0, 0xa6c),
             READ_S32((void *)(intptr_t)v0, 0x8cc), READ_S32((void *)(intptr_t)v0, 0x958),
             READ_S32((void *)(intptr_t)v0, 0x9dc), READ_S32((void *)(intptr_t)v0, 0xa68));
    PopCommandListAddresses((void *)(intptr_t)(arg1 + 0x2c20), (void *)(intptr_t)(v0 + 0xa78));
    ENC_KMSG("AddNewRequest ready req=%p lane=%d cmdsrc_count=%d cmdsrc_next=%d cmdlist=%p"
             " cmd1=%08x/%08x/%08x/%08x/%08x cmd2=%08x/%08x/%08x/%08x/%08x",
             (void *)(intptr_t)v0, READ_S32((void *)(intptr_t)v0, 0xa70),
             READ_S32((void *)(intptr_t)(arg1 + 0x2c20), 0x980), READ_S32((void *)(intptr_t)(arg1 + 0x2c20), 0x984),
             (void *)(intptr_t)(v0 + 0xa78),
             READ_S32((void *)(intptr_t)v0, 0xa78), READ_S32((void *)(intptr_t)v0, 0xa7c),
             READ_S32((void *)(intptr_t)v0, 0xa80), READ_S32((void *)(intptr_t)v0, 0xa84),
             READ_S32((void *)(intptr_t)v0, 0xa88),
             READ_S32((void *)(intptr_t)v0, 0xab8), READ_S32((void *)(intptr_t)v0, 0xabc),
             READ_S32((void *)(intptr_t)v0, 0xac0), READ_S32((void *)(intptr_t)v0, 0xac4),
             READ_S32((void *)(intptr_t)v0, 0xac8));
    {
        StaticFifoCompat *fifo =
            (StaticFifoCompat *)(intptr_t)(arg1 + READ_S32((void *)(intptr_t)v0, 0xa70) * 0x5c + 0x129b4);
        int32_t qret =
            StaticFifo_Queue(fifo, v0);
        ENC_KMSG("AddNewRequest queued req=%p lane=%d qret=%d fifo=%p read=%d write=%d cap=%d",
                 (void *)(intptr_t)v0, READ_S32((void *)(intptr_t)v0, 0xa70), qret,
                 fifo, fifo->read_idx, fifo->write_idx, fifo->capacity);
        return qret;
    }
}

int32_t StorePicture(int32_t arg1, void *arg2)
{
    void *var_18 = &_gp;

    (void)var_18;
    if (READ_S32(arg2, 0x2e0) != 0) {
        AL_RefMngr_MarkAsReadyForOutput((void *)(intptr_t)arg1, (char)READ_U8(arg2, 0x290));
    }

    return AL_RefMngr_StorePicture((void *)(intptr_t)arg1, (uint8_t *)arg2 + 0x20, (char)READ_U8(arg2, 0x290));
}

int32_t ReleaseWorkBuffers(void *arg1, void *arg2)
{
    void *var_18 = &_gp;

    (void)var_18;
    if ((uint32_t)READ_U8(arg1, 0x1f) != 4U) {
        ReleaseRefBuffers(arg1, arg2);
    }

    if (READ_S32(arg2, 0xa70) == 1) {
        StorePicture((int32_t)(intptr_t)((uint8_t *)arg1 + 0x22c8), arg2);
    }

    if (READ_S32(arg2, 0x2e0) != 0) {
        AL_RefMngr_ReleaseFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg2, 0x290));
    }

    if (READ_S32(arg2, 0x298) != 0) {
        if (READ_S32(arg2, 0x30) == 8) {
            AL_SrcReorder_MarkSrcBufferAsUsed((uint8_t *)arg1 + 0x178, READ_S32(arg2, 0x20));
        }

        if (READ_S32(arg2, 0x24) >= 0) {
            AL_SrcReorder_EndSrcBuffer((uint8_t *)arg1 + 0x178, READ_S32(arg2, 0x20));
        }
    }

    return AL_IntermMngr_ReleaseBuffer((uint8_t *)arg1 + 0x2a54, READ_PTR(arg2, 0x838));
}

int32_t GenerateAvcSliceHeader(void *arg1, void *arg2, void *arg3, void *arg4, int32_t arg5,
                               int32_t arg6); /* forward decl, ported by T<N> later */
int32_t GenerateHevcSliceHeader(void *arg1, void *arg2, void *arg3, void *arg4, void *arg5, int32_t arg6,
                                int32_t arg7); /* forward decl, ported by T<N> later */
int32_t CmdRegsEnc1ToSliceParam(void *arg1, void *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
void InitSliceStatus(void *arg1); /* forward decl, ported by T<N> later */
void EncodingStatusRegsToSliceStatus(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
void EntropyStatusRegsToSliceStatus(void *arg1, void *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t MergeEncodingStatus(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t MergeEntropyStatus(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int16_t GetSliceEnc2CmdOffset(uint32_t arg1, uint32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
void getAsyncEntropyChannel(int32_t *arg1); /* forward decl, ported by T<N> later */
void getNoEntropyChannel(int32_t *arg1); /* forward decl, ported by T<N> later */
int32_t AL_StreamMngr_GetBuffer(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_StreamMngr_AddBufferBack(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_SrcReorder_GetSrcBuffer(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetFrmBufTraceAddrs(void *arg1, char arg2); /* forward decl, ported by T<N> later */
int32_t AL_IntermMngr_GetMapAddr(void *arg1, void *arg2, int32_t *arg3); /* forward decl, ported by T<N> later */
int32_t AL_IntermMngr_GetDataAddr(void *arg1, void *arg2, int32_t *arg3); /* forward decl, ported by T<N> later */
int32_t StaticFifo_Empty(void *arg1); /* forward decl, ported by T<N> later */
int32_t StaticFifo_Dequeue(void *arg1); /* forward decl, ported by T<N> later */
int32_t GetCoreFirstEnc2CmdOffset(void); /* forward decl, ported by T<N> later */
void AL_EncCore_EnableEnc2Interrupt(void *arg1); /* forward decl, ported by T<N> later */
void AL_EncCore_Encode2(void *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
void AL_EncCore_TurnOffRAM(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5); /* forward decl */
void AL_EncCore_ReadStatusRegsJpeg(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
void JpegStatusToStatusRegs(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
void *EndRequestsBuffer_Pop(void *arg1); /* forward decl, ported by T<N> later */
int32_t Rtos_GetTime(void); /* forward decl, ported by T<N> later */
void AL_CoreConstraintEnc_Init(void *arg1, int32_t arg2, uint32_t arg3); /* forward decl */
uint32_t AL_CoreConstraintEnc_GetExpectedNumberOfCores(void *arg1, void *arg2); /* forward decl */
int32_t AL_CoreConstraintEnc_GetResources(void *arg1, int32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5,
                                          uint32_t arg6, uint32_t arg7); /* forward decl */
int32_t AL_SrcReorder_GetWaitingSrcBufferCount(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_PushBuffer(void *arg1, int32_t arg2, int32_t arg3, uint32_t arg4, int32_t arg5,
                              int32_t arg6); /* forward decl, ported by T<N> later */
int32_t AL_StreamMngr_AddBuffer(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_IntermMngr_GetEp1Location(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_GetAllocSizeEP1(void); /* forward decl, ported by T<N> later */
int32_t AL_IntermMngr_AddBuffer(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_DPBConstraint_GetMaxDPBSize(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_RefMngr_GetBufferSize(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_IntermMngr_GetBufferSize(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_SrcReorder_AddSrcBuffer(void *arg1, void *arg2); /* forward decl, ported by T<N> later */

int32_t OutputSkippedPicture(void *arg1, void *arg2, void *arg3)
{
    void *s3 = READ_PTR(arg2, 0x318);
    void *var_98 = &_gp;
    uint32_t byte_count = (uint32_t)(READ_S32(arg1, 0x3d7c) + 7) >> 3;
    uint8_t *dst = (uint8_t *)READ_PTR(s3, 8) + READ_S32(s3, 0x14);
    uint8_t *src = (uint8_t *)READ_PTR(arg1, 0x3d74);
    int32_t escaped = 0;

    (void)var_98;
    if (byte_count != 0U) {
        uint32_t cur = (uint32_t)*src;
        uint8_t *src_end = src + byte_count;
        uint8_t *dst_next = dst + 1;
        int32_t zero_run = 1;
        uint8_t *src_cur = src + 1;

        for (;;) {
            if (cur == 0U) {
                *dst = 0;
                if (src_cur == src_end) {
                    break;
                }

                if (zero_run == 2 && (((uint32_t)*src_cur & 0xfffffffcU) == 0U)) {
                    dst_next = dst + 2;
                    dst[1] = 3;
                    escaped += 1;
                    zero_run = 0;
                    dst = dst_next;
                }

                do {
                    cur = (uint32_t)*src_cur;
                    dst_next = dst + 1;
                    zero_run += 1;
                    src_cur += 1;
                } while (cur == 0U);
            }

            *dst = (uint8_t)cur;
            if (src_cur == src_end) {
                break;
            }

            zero_run = 0;
            dst = dst_next;
            cur = (uint32_t)*src_cur;
            dst_next = dst + 1;
            zero_run += 1;
            src_cur += 1;
            if (cur == 0U) {
                continue;
            }
        }
    }

    {
        uint32_t codec = (uint32_t)READ_U8(arg1, 0x1f);
        int32_t skipped_bytes = (int32_t)byte_count + escaped;
        int32_t header_bytes = 0;

        WRITE_S32(arg2, 0xb30, 1);
        WRITE_U16(arg2, 0xb24, 1);
        WRITE_U16(arg2, 0xb26, 1);
        WRITE_S32(arg2, 0xb98, 1);
        if (codec == 0U) {
            header_bytes = GenerateAvcSliceHeader(arg1, arg2, (uint8_t *)arg2 + 0x170, (uint8_t *)arg2 + 0x298,
                                                  READ_S32(s3, 0x14), 0);
            codec = (uint32_t)READ_U8(arg1, 0x1f);
        }

        if (codec == 1U) {
            int32_t slice_size = READ_S32(s3, 0x14);
            int16_t num_core = READ_S16(arg1, 0x40);
            uint8_t temp[0x10];

            WRITE_U16(arg2, 0xb24, READ_U16(arg1, 0x3c));
            WRITE_U16(arg2, 0xb26, (uint16_t)num_core);
            memset(temp, 0, sizeof(temp));
            WRITE_S32(temp, 8, skipped_bytes);
            header_bytes = GenerateHevcSliceHeader(arg1, arg2, (uint8_t *)arg2 + 0x170, (uint8_t *)arg2 + 0x298, temp,
                                                   slice_size, 0);
            codec = (uint32_t)READ_U8(arg1, 0x1f);
        }

        {
            int32_t tile_mb = READ_U16(arg2, 0x278) * READ_U16(arg2, 0x27a);
            int32_t qp = READ_S32(arg2, 0x28c);
            int32_t slice_type = READ_S32(arg2, 0x1a0);
            int32_t stream_size = READ_S32(arg1, 0x3d80);
            int32_t *desc = (int32_t *)((uint8_t *)READ_PTR(s3, 8) + READ_S32(s3, 0x18));
            int32_t result = header_bytes + skipped_bytes;

            desc[0] = READ_S32(s3, 0x14) - header_bytes;
            desc[1] = result;
            desc[2] = qp;
            desc[3] = slice_type;
            WRITE_S32(arg3, 8, skipped_bytes);
            WRITE_S32(arg3, 0xc, stream_size);
            WRITE_S32(arg3, 0x14, 0);
            WRITE_S32(arg3, 0x18, 0);
            WRITE_S32(arg3, 0x1c, 0);
            WRITE_S32(arg3, 4, tile_mb);
            WRITE_S32(arg3, 0x20, tile_mb);
            WRITE_S32(arg3, 0x24, 0);
            if (codec == 0U) {
                WRITE_S32(arg3, 0x28, tile_mb);
                WRITE_S32(arg3, 0x2c, 0);
            } else {
                WRITE_S32(arg3, 0x28, 0);
                WRITE_S32(arg3, 0x2c, 0);
            }
            return result;
        }
    }
}

int32_t CmdList_MergeMultiSliceEntropyStatus(void *arg1, void *arg2, void *arg3, void *arg4, uint8_t arg5, uint8_t arg6)
{
    int32_t t8 = (int32_t)(int8_t)arg5;
    int32_t t7 = READ_S32(arg2, 0xa6c);
    void *var_18 = &_gp;
    void *a0_1 = (uint8_t *)READ_PTR(arg2, ((t8 + 0x29c) << 2) + 8) + ((uint32_t)arg6 << 9);
    int32_t v1_3;
    uint8_t *t4_1;
    int32_t t6_1;
    int32_t t5_1;

    (void)arg1;
    (void)var_18;
    if (t7 <= 0) {
        v1_3 = READ_S32(a0_1, 0xcc);
        goto done;
    }

    t4_1 = (uint8_t *)arg2 + 0x84c;
    t6_1 = 0;
    t5_1 = 0;
    while (1) {
        int32_t t3_1 = READ_S32(t4_1, 0x80);
        uint8_t *v1_1 = t4_1;

        if (t3_1 > 0) {
            int32_t t0_1 = 0;
            int32_t t1_1 = 0;

            do {
                t0_1 += 1;
                t1_1 += (((uint32_t)READ_S32(v1_1, 4) ^ 1U) < 1U) ? 1 : 0;
                v1_1 = (uint8_t *)arg2 + ((intptr_t)v1_1 + t6_1 + 0x854 - (intptr_t)t4_1);
            } while (t0_1 != t3_1);

            if (t1_1 != 0) {
                v1_3 = READ_S32(a0_1, 0xfc);
                break;
            }
        }

        t5_1 += 1;
        t6_1 += 0x110;
        t4_1 += 0x110;
        if (t5_1 == t7) {
            v1_3 = READ_S32(a0_1, 0xcc);
            break;
        }
    }

done:
    {
        void *s0 = (uint8_t *)arg3 + t8 * 0x78;

        EntropyStatusRegsToSliceStatus(a0_1, s0, v1_3);
        return MergeEntropyStatus(arg4, s0);
    }
}

int32_t UpdateStatus(void *arg1, int32_t *arg2)
{
    if (READ_U8(arg1, 0x1de) != 0U) {
        WRITE_S32(arg1, 0xb34, arg2[0xd]);
    }

    {
        int32_t result = READ_S32(arg1, 0xb94);

        if (result != 0) {
            return result;
        }

        if ((uint32_t)arg2[2] != 0U) {
            WRITE_S32(arg1, 0xb94, 0x93);
            return 0x93;
        }

        if ((uint32_t)arg2[1] != 0U) {
            WRITE_S32(arg1, 0xb94, 0x88);
            return 0x88;
        }

        if ((uint32_t)arg2[0] != 0U) {
            WRITE_S32(arg1, 0xb94, 2);
            return 2;
        }

        return result;
    }
}

int32_t SetTileInfoIfNeeded(void *arg1, void *arg2, void *arg3, int32_t arg4)
{
    int32_t v0 = 1;

    if ((uint32_t)READ_U8(arg3, 0x1f) == 1U) {
        int32_t v0_2 = (READ_S32(arg3, 0x3c) < 2) ? 1 : 0;

        if (v0_2 == 0 && arg4 != 0) {
            if (READ_S32(arg2, 0x2c) != 0) {
                WRITE_S32(arg2, 0x40, READ_S32(arg1, 0x1ae8));
            }

            {
                uint32_t count = (uint32_t)READ_U16(arg2, 0x2e);

                v0 = (int32_t)(count + 0x11U);
                if (count == 0U) {
                    v0 = 1;
                }

                {
                    uint16_t *a0 = (uint16_t *)((uint8_t *)arg1 + 0x1b28);
                    uint32_t *i = (uint32_t *)((uint8_t *)arg2 + 0x44);

                    while ((uint8_t *)i != (uint8_t *)arg2 + ((uint32_t)v0 << 2)) {
                        *i++ = (uint32_t)*a0++;
                    }

                    return (int32_t)(intptr_t)i;
                }
            }
        }

        return v0_2;
    }

    return v0;
}

int32_t SetChannelSteps(void *arg1, void *arg2)
{
    void *var_28 = &_gp;
    int32_t v0_3;
    int32_t var_20;
    void *s1;
    int32_t result;

    (void)var_28;
    if ((uint32_t)READ_U8(arg2, 0x1f) != 0U || READ_S32(arg2, 0x3c) == 1) {
        getNoEntropyChannel(&var_20);
        v0_3 = var_20;
    } else {
        getAsyncEntropyChannel(&var_20);
        v0_3 = var_20;
    }

    s1 = (uint8_t *)arg1 + 0x18c0;
    WRITE_S32(arg1, 0xf0, v0_3);
    Rtos_Memset(s1, 0, 0x220);
    result = ((int32_t (*)(void *, void *))(intptr_t)READ_S32(arg1, 0xf0))(arg2, s1);
    WRITE_S32(arg1, 0x1ae0, result);
    return result;
}

static int32_t GetStreamBuffers_part_72(void *arg1, void *arg2, void *arg3)
{
    uint32_t s2 = 1;
    int32_t result;
    int32_t *s7_1;
    int32_t fp_1;

    if (READ_S32(arg3, 0xc4) != 0) {
        s2 = (uint32_t)READ_U16(arg3, 0x40);
    }

    result = 1;
    if (READ_S32(arg3, 0xc4) == 0 || s2 != 0U) {
        s7_1 = (int32_t *)((uint8_t *)arg2 + 0x348);
        fp_1 = 0;
        while (1) {
            int32_t stream_entry[8];
            int32_t push_back_entry[8];
            uint32_t shift = (uint32_t)READ_U16(arg3, 0x4e);
            uint32_t max_size = (uint32_t)READ_U16(arg3, 0x40);
            int32_t part_size;
            int32_t result_1;

            result_1 = AL_StreamMngr_GetBuffer(arg1, stream_entry);
            result = result_1;
            if (result_1 == 0) {
                int32_t s7_2 = fp_1 - 1;

                if (fp_1 == 0) {
                    break;
                }

                {
                    int32_t *s5_1 = (int32_t *)((uint8_t *)arg2 + fp_1 * 0x28 + 0x320);

                    while (1) {
                        if (READ_U16(arg3, 0x3e) != 0U) {
                            max_size = 0xc8;
                        }

                        part_size = (((((((1 << (shift & 0x1fU)) - 1) + (uint32_t)READ_U16(arg3, 6)) >>
                                         (shift & 0x1fU)) < max_size)
                                          ? max_size
                                          : ((((1 << (shift & 0x1fU)) - 1) + (uint32_t)READ_U16(arg3, 6)) >>
                                             (shift & 0x1fU))) *
                                         (uint32_t)READ_U16(arg3, 0x3c) +
                                     0x10)
                                    << 4;
                        part_size = ((part_size + 0x7f) >> 7) << 7;
                        part_size += s5_1[0];
                        s5_1[0] = part_size;
                        s5_1[2] = part_size;
                        if ((part_size & 3) != 0) {
                            __assert("pStreamInfo->iStreamPartOffset % 4 == 0",
                                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c",
                                     0x5e5, "FreeReservedSpaceForStreamPart", &_gp);
                        }

                        if (s5_1[1] >= part_size) {
                            break;
                        }

                        push_back_entry[0] = s5_1[-1];
                        push_back_entry[1] = s5_1[-2];
                        push_back_entry[2] = part_size;
                        push_back_entry[3] = s5_1[1];
                        push_back_entry[4] = s5_1[-4];
                        push_back_entry[5] = s5_1[-3];
                        push_back_entry[6] = s5_1[4];
                        push_back_entry[7] = 0;
                        s5_1 -= 10;
                        s7_2 -= 1;
                        AL_StreamMngr_AddBufferBack(arg1, push_back_entry);
                        s5_1[8] = 0;
                        if (s7_2 == -1) {
                            return result;
                        }
                    }
                }

                return result;
            }

            ENC_KMSG("GetStreamBuffers raw lane=%d entry=[%08x %08x %08x %08x %08x %08x %08x %08x] cfg6=%u cfg3c=%u cfg3e=%u cfg40=%u cfg4e=%u cfg4f=%u",
                     fp_1,
                     stream_entry[0], stream_entry[1], stream_entry[2], stream_entry[3],
                     stream_entry[4], stream_entry[5], stream_entry[6], stream_entry[7],
                     (unsigned)READ_U16(arg3, 6), (unsigned)READ_U16(arg3, 0x3c),
                     (unsigned)READ_U16(arg3, 0x3e), (unsigned)READ_U16(arg3, 0x40),
                     (unsigned)READ_U16(arg3, 0x4e), (unsigned)READ_U16(arg3, 0x4f));
            s7_1[-1] = stream_entry[0];
            s7_1[-2] = stream_entry[1];
            s7_1[-4] = stream_entry[4];
            s7_1[-3] = stream_entry[5];
            s7_1[4] = stream_entry[6];
            s7_1[0] = stream_entry[2];
            s7_1[1] = stream_entry[3];
            s7_1[3] = stream_entry[3];

            if (READ_U16(arg3, 0x3e) != 0U) {
                max_size = 0xc8;
            }

            part_size = (((((((1 << (shift & 0x1fU)) - 1) + (uint32_t)READ_U16(arg3, 6)) >> (shift & 0x1fU)) <
                           max_size)
                              ? max_size
                              : ((((1 << (shift & 0x1fU)) - 1) + (uint32_t)READ_U16(arg3, 6)) >> (shift & 0x1fU))) *
                             (uint32_t)READ_U16(arg3, 0x3c) +
                         0x10)
                        << 4;
            part_size = ((part_size + 0x7f) >> 7) << 7;
            if (part_size >= stream_entry[2]) {
                __assert("pStreamInfo->iMaxSize > iStreamPartSize",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c", 0x5ed,
                         "ReserveSpaceForStreamPart", &_gp);
            }

            {
                int32_t v0_4 = stream_entry[2] - part_size;

                s7_1[0] = v0_4;
                s7_1[2] = v0_4;
                if ((v0_4 & 3) != 0) {
                    __assert("pStreamInfo->iStreamPartOffset % 4 == 0",
                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c", 0x5f2,
                             "ReserveSpaceForStreamPart", &_gp);
                }

                if (stream_entry[3] >= v0_4) {
                    __assert("pStreamInfo->iOffset < pStreamInfo->iStreamPartOffset",
                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c", 0x5f3,
                             "ReserveSpaceForStreamPart", &_gp);
                }

                ENC_KMSG("GetStreamBuffers lane_entry phys=0x%x virt=%p max=%d off=%d side0=%d side1=%d side2=%p part=%d part_off=%d",
                         stream_entry[0], (void *)(intptr_t)stream_entry[1], stream_entry[2], stream_entry[3],
                         stream_entry[4], stream_entry[5], (void *)(intptr_t)stream_entry[6],
                         part_size, v0_4);
                ENC_KMSG("GetStreamBuffers reserve lane=%d shift=%u min_lcu=%u max_cfg=%u rows=%u forced=%u part=%d part_off=%d limit=%d",
                         fp_1, shift,
                         (unsigned)((((1U << (shift & 0x1fU)) - 1U) + (uint32_t)READ_U16(arg3, 6)) >> (shift & 0x1fU)),
                         max_size, (unsigned)READ_U16(arg3, 0x3c), (unsigned)READ_U16(arg3, 0x3e),
                         part_size, v0_4, stream_entry[2]);
            }

            fp_1 += 1;
            s7_1 += 10;
            if (fp_1 >= (int32_t)s2) {
                return 1;
            }
        }
    }

    return result;
}

int32_t InitTracedBuffers(void *arg1, int32_t *arg2, void *arg3)
{
    int32_t *v1 = arg2;
    void *var_20 = &_gp;
    int32_t *i = (int32_t *)((uint8_t *)arg3 + 0x298);
    int32_t result;

    (void)var_20;
    do {
        v1[0] = i[0];
        v1[1] = i[1];
        v1[2] = i[2];
        v1[3] = i[3];
        i += 4;
        v1 += 4;
    } while ((uint8_t *)i != (uint8_t *)arg3 + 0x338);

    arg2[0x28] = READ_S32(arg3, 0x18);
    arg2[0x29] = READ_S32(arg3, 0x1c);
    arg2[0x2a] = AL_RefMngr_GetFrmBufTraceAddrs((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg3, 0x291));
    arg2[0x2b] = AL_RefMngr_GetFrmBufTraceAddrs((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg3, 0x292));
    arg2[0x2c] = AL_RefMngr_GetFrmBufTraceAddrs((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg3, 0x290));
    AL_RefMngr_GetMvBufAddr((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg3, 0x290), &arg2[0x2e]);
    AL_RefMngr_GetMvBufAddr((uint8_t *)arg1 + 0x22c8, (char)READ_U8(arg3, 0x293), &arg2[0x2d]);
    AL_IntermMngr_GetMapAddr((uint8_t *)arg1 + 0x2a54, READ_PTR(arg3, 0x838), &arg2[0x2f]);
    AL_IntermMngr_GetDataAddr((uint8_t *)arg1 + 0x2a54, READ_PTR(arg3, 0x838), &arg2[0x30]);

    {
        void *v0_4 = READ_PTR(arg3, 0x318);
        int32_t a0_7 = READ_S32(v0_4, 8);
        int32_t v1_2 = READ_S32(v0_4, 0x1c);

        result = READ_S32(v0_4, 0x10);
        arg2[0x34] = a0_7;
        arg2[0x35] = READ_S32(v0_4, 0xc);
        arg2[0x36] = result;
        arg2[0x37] = v1_2;
        return result;
    }
}

int32_t FillEncTrace(int32_t *arg1, void *arg2, void *arg3)
{
    int32_t t0 = READ_S32(arg3, 0x10);

    arg1[0] = READ_S32(arg2, 0);
    arg1[1] = t0;
    WRITE_U8(arg1, 0x10, READ_U8(arg2, 0x1ae5));
    WRITE_U8(arg1, 0x11, 0);
    WRITE_U8(arg1, 0x13, READ_U8(arg2, 0x3c));
    WRITE_U16(arg1, 0x14, READ_U16(arg2, 0x40));
    arg1[7] = READ_S32(arg3, 0xc50);
    return InitTracedBuffers(arg2, &arg1[8], arg3);
}

int32_t handleInputTraces(void *arg1, void *arg2, void *arg3, uint8_t arg4)
{
    int32_t cb = READ_S32(arg1, 0x1a9c);
    void *var_138 = &_gp;

    (void)var_138;
    if (cb != 0) {
        uint8_t trace[0x130];

        memset(trace, 0, sizeof(trace));
        FillEncTrace((int32_t *)trace, arg1, arg2);
        WRITE_S32(trace, 8, READ_S32(arg3, 8));
        WRITE_S32(trace, 0x0c, (int32_t)(intptr_t)((uint8_t *)arg2 + 0xa78));
        WRITE_S32(trace, 0x10, (int32_t)(intptr_t)((uint8_t *)arg2 + 0xab8));
        WRITE_U8(trace, 0x11, READ_U8(arg3, 0));
        WRITE_U8(trace, 0x12, arg4);
        WRITE_S32(trace, 0x18, 0);
        return ((int32_t (*)(void *, void *))(intptr_t)cb)(trace, READ_PTR(arg1, 0));
    }

    return (uint32_t)cb;
}

int32_t encode2(void *arg1)
{
    void *var_28 = &_gp;
    int32_t v0 = StaticFifo_Dequeue((uint8_t *)arg1 + 0x12a10);
    int32_t t0 = READ_S32((void *)(intptr_t)v0, 0xa68);
    int32_t *v0_1 = (int32_t *)(intptr_t)(v0 + 0x9e8);
    uint32_t a1 = (uint32_t)READ_U8(arg1, 0x1ae5);
    intptr_t a3 = 0x9f0 - (intptr_t)v0_1;
    int32_t a0_1 = 0;
    uint32_t a0_6;
    uint32_t v1_7;
    uint32_t s1;
    uint32_t s0_1;
    int32_t i;

    (void)var_28;
    if (t0 > 0) {
        do {
            a0_1 += 1;
            *v0_1 += (int32_t)a1;
            v0_1 = (int32_t *)(intptr_t)(v0 + (intptr_t)v0_1 + a3);
        } while (a0_1 != t0);
    }

    {
        int32_t t0_1 = READ_S32((void *)(intptr_t)v0, 0x9dc);
        int32_t *v0_2 = (int32_t *)(intptr_t)(v0 + 0x95c);
        int32_t a0_2 = 0;
        intptr_t a3_1 = 0x964 - (intptr_t)v0_2;

        if (t0_1 > 0) {
            do {
                a0_2 += 1;
                *v0_2 += (int32_t)a1;
                v0_2 = (int32_t *)(intptr_t)(v0 + (intptr_t)v0_2 + a3_1);
            } while (a0_2 != t0_1);
        }
    }

    StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + READ_S32((void *)(intptr_t)v0, 0xa70) * 0x5c + 0x12a6c), v0);
    a0_6 = (uint32_t)READ_U16(arg1, 0x3c);
    v1_7 = (uint32_t)READ_U8((void *)(intptr_t)(v0 + READ_S32((void *)(intptr_t)v0, 0xa70) * 0x110 + 0x8d4), 0);
    s1 = 0;
    s0_1 = 0;
    i = ((int32_t)a0_6 < (int32_t)v1_7) ? 1 : 0;
    if (i != 0) {
        v1_7 = a0_6;
    }

    if ((int32_t)v1_7 > 0) {
        do {
            int32_t s1_2 = v0 + ((int32_t)s1 << 2);
            int32_t s1_4 = (int32_t)s0_1 * 0x44;
            uint32_t v0_13 = (uint32_t)(uint16_t)GetCoreFirstEnc2CmdOffset() << 9;
            int32_t s5_2 = (int32_t)v0_13 + READ_S32((void *)(intptr_t)s1_2, 0xab8);
            int32_t s6_2 = READ_S32((void *)(intptr_t)s1_2, 0xa78) + (int32_t)v0_13;
            void *core = (uint8_t *)READ_PTR(arg1, 0x168) + s1_4;

            AL_EncCore_EnableEnc2Interrupt(core);
            AL_EncCore_Encode2(core, s5_2, s6_2);

            a0_6 = (uint32_t)READ_U16(arg1, 0x3c);
            v1_7 = (uint32_t)READ_U8((void *)(intptr_t)(v0 + READ_S32((void *)(intptr_t)v0, 0xa70) * 0x110 + 0x8d4), 0);
            s0_1 = (uint32_t)((uint8_t)s0_1 + 1U);
            if ((int32_t)a0_6 < (int32_t)v1_7) {
                v1_7 = a0_6;
            }

            i = ((int32_t)s0_1 < (int32_t)v1_7) ? 1 : 0;
            s1 = s0_1;
        } while (i != 0);
    }

    return i;
}

int32_t handleJpegInputTrace(void *arg1, void *arg2)
{
    int32_t result = READ_S32(arg1, 0x1a9c);
    void *var_130 = &_gp;

    (void)var_130;
    if (result == 0) {
        return result;
    }

    {
        uint8_t trace[0x128];

        memset(trace, 0, sizeof(trace));
        FillEncTrace((int32_t *)trace, arg1, arg2);
        WRITE_S32(trace, 0x10, (int32_t)(intptr_t)((uint8_t *)arg2 + 0xa78));
        WRITE_S32(trace, 0x14, (int32_t)(intptr_t)((uint8_t *)arg2 + 0xab8));
        WRITE_S32(trace, 0x18, 8);
        return ((int32_t (*)(void *, void *))(intptr_t)result)(trace, READ_PTR(arg1, 0));
    }
}

int32_t EndJpegEncoding(void *arg1)
{
    void *var_1a8 = &_gp;
    int32_t v0 = StaticFifo_Dequeue((uint8_t *)arg1 + 0x1780);
    void *core = READ_PTR(arg1, 0x168);
    uint8_t t0 = READ_U8((void *)(intptr_t)v0, 0x24);
    int32_t t1 = READ_S32(READ_PTR((void *)(intptr_t)v0, 0x318), 0x18);
    int32_t a2 = READ_S32((void *)(intptr_t)v0, 0x10);
    int32_t a3 = READ_S32((void *)(intptr_t)v0, 0x14);
    int32_t v0_4 = READ_S32((void *)(intptr_t)v0, 0x18);
    int32_t v1_1 = READ_S32((void *)(intptr_t)v0, 0x1c);
    int32_t t6 = READ_S32((void *)(intptr_t)v0, 0x40);
    int32_t t5 = READ_S32((void *)(intptr_t)v0, 0x30);
    int32_t t4 = READ_S32((void *)(intptr_t)v0, 0x34);
    int32_t t3 = READ_S32((void *)(intptr_t)v0, 0x48);
    int16_t t2 = READ_S16((void *)(intptr_t)v0, 4);
    uint8_t status[0x90];
    void *a1_1;
    uint32_t a1_2;
    int32_t *v0_7;
    int32_t cb;
    int32_t t9_3;
    void *s0_1;

    (void)var_1a8;
    WRITE_S32((void *)(intptr_t)v0, 0xa70, READ_S32((void *)(intptr_t)v0, 0xa70) + 1);
    AL_EncCore_TurnOffRAM(core, 1, 1, 0, 0);
    WRITE_U8((void *)(intptr_t)v0, 0xbad, READ_U8((void *)(intptr_t)v0, 0x3c));
    WRITE_S32((void *)(intptr_t)v0, 0xaf8, a2);
    WRITE_S32((void *)(intptr_t)v0, 0xafc, a3);
    WRITE_S32((void *)(intptr_t)v0, 0xb30, 1);
    WRITE_U8((void *)(intptr_t)v0, 0xba2, 1);
    WRITE_U8((void *)(intptr_t)v0, 0xba1, 1);
    WRITE_S32((void *)(intptr_t)v0, 0xb00, v0_4);
    WRITE_S32((void *)(intptr_t)v0, 0xb04, v1_1);
    WRITE_S32((void *)(intptr_t)v0, 0xb94, 0);
    WRITE_S32((void *)(intptr_t)v0, 0xb10, t6);
    WRITE_S32((void *)(intptr_t)v0, 0xb98, t5);
    WRITE_S32((void *)(intptr_t)v0, 0xb9c, t4);
    WRITE_U8((void *)(intptr_t)v0, 0xba0, t0 & 1U);
    WRITE_S32((void *)(intptr_t)v0, 0xba8, t3);
    WRITE_U16((void *)(intptr_t)v0, 0xba4, (uint16_t)t2);
    WRITE_S32((void *)(intptr_t)v0, 0xb2c, t1);
    memset(status, 0, sizeof(status));
    AL_EncCore_ReadStatusRegsJpeg(core, status);
    a1_1 = READ_PTR((void *)(intptr_t)v0, 0x318);
    a1_2 = (uint32_t)READ_U8(status, 1);
    v0_7 = (int32_t *)((uint8_t *)READ_PTR(a1_1, 8) + (READ_S32((void *)(intptr_t)v0, 0xb30) << 4) - 0x10 +
                       READ_S32(a1_1, 0x18));
    v0_7[0] = READ_S32(a1_1, 0x14);
    v0_7[1] = READ_S32(status, 8);
    v0_7[2] = -1;
    v0_7[3] = -1;
    if (a1_2 != 0U) {
        WRITE_S32((void *)(intptr_t)v0, 0xb94, 0x88);
    }

    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    ((int32_t (*)(void *, void *, int32_t))(intptr_t)READ_S32(arg1, 0x144))((uint8_t *)arg1 + 0x128,
                                                                             (uint8_t *)(intptr_t)v0 + 0x20, 0);
    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));

    cb = READ_S32(arg1, 0x1a9c);
    if (cb != 0) {
        uint8_t trace[0x1a0];

        JpegStatusToStatusRegs(status, (void *)(intptr_t)READ_S32((void *)(intptr_t)v0, 0xa78));
        memset(trace, 0, sizeof(trace));
        FillEncTrace((int32_t *)trace, arg1, (void *)(intptr_t)v0);
        WRITE_S32(trace, 0x10, READ_S32((void *)(intptr_t)v0, 0xab8));
        WRITE_S32(trace, 0x14, READ_S32((void *)(intptr_t)v0, 0xa78));
        WRITE_S32(trace, 0x18, 9);
        ((int32_t (*)(void *, void *))(intptr_t)cb)(trace, READ_PTR(arg1, 0));
    }

    ReleaseWorkBuffers(arg1, (void *)(intptr_t)v0);

    {
        int32_t v0_10 = (int32_t)(intptr_t)EndRequestsBuffer_Pop((uint8_t *)arg1 + 0x1604);
        int32_t *i = (int32_t *)(intptr_t)(v0 + 0xaf8);
        int32_t *a0_13;

        (*(void **)(void *)(intptr_t)v0_10) = READ_PTR((void *)(intptr_t)v0, 0x318);
        a0_13 = (int32_t *)(intptr_t)(v0_10 + 8);
        do {
            a0_13[0] = i[0];
            a0_13[1] = i[1];
            a0_13[2] = i[2];
            a0_13[3] = i[3];
            i += 4;
            a0_13 += 4;
        } while ((intptr_t)i != v0 + 0xbd8);
        StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + 0x12b24), v0_10);
    }

    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    WRITE_S32(arg1, 0x35b4, READ_S32(arg1, 0x35b4) + 1);
    t9_3 = READ_S32(arg1, 0x1aa8);
    if (t9_3 != 0) {
        ((void (*)(void *, void *))(intptr_t)t9_3)(READ_PTR(arg1, 0x1aac), (uint8_t *)(intptr_t)v0 + 0x84c);
    }

    s0_1 = READ_PTR(READ_PTR((void *)(intptr_t)v0, 0x18), 0x24);
    if (s0_1 != 0) {
        int32_t t9_4 = READ_S32(s0_1, 0x434);

        if (t9_4 != 0 && READ_S32(s0_1, 0x438) != 0) {
            ((void (*)(void *, int32_t, int32_t))(intptr_t)t9_4)(READ_PTR(s0_1, 0x43c), READ_S32(s0_1, 0x438), 1);
        }

        {
            int32_t v0_14;
            int32_t v1_6;
            void *a0_19 = READ_PTR(s0_1, 0x428);

            v0_14 = Rtos_GetTime();
            v1_6 = Rtos_GetTime();
            WRITE_S32(s0_1, 0x410, v0_14);
            WRITE_S32(s0_1, 0x414, v1_6);
            WRITE_S32(a0_19, 0x160, READ_S32(status, 8));
            WRITE_U16(a0_19, 0x164, READ_U16(arg1, 0x80));
        }
    }

    return 1;
}

static void *SetChannelTraceCallBack_1a9c(void *arg1, void *arg2, void *arg3)
{
    (*(void **)((uint8_t *)arg1 + 0x1a9c)) = arg2;
    (*(void **)((uint8_t *)arg1 + 0x1aa0)) = arg3;
    return (void *)&_gp;
}

int32_t AL_EncChannel_ScheduleDestruction(void *arg1, void *arg2, void *arg3)
{
    void *var_10 = &_gp;

    (void)var_10;
    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    WRITE_U16(arg1, 0x1ae6, 1);
    (*(void **)((uint8_t *)arg1 + 0x1a0c)) = arg2;
    (*(void **)((uint8_t *)arg1 + 0x1aa8)) = arg3;
    return Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
}

uint32_t AL_EncChannel_SetNumberOfCores(void *arg1)
{
    void *var_38 = &_gp;
    uint8_t var_30[0x30];
    uint32_t s2;
    int32_t v0_1;
    uint32_t result_1;
    uint32_t lo;
    uint32_t v0_3;
    uint32_t result;

    (void)var_38;
    memset(var_30, 0, sizeof(var_30));
    AL_CoreConstraintEnc_Init(var_30, READ_S32(arg1, 0x2c), (uint32_t)READ_U8(arg1, 0x1f));
    s2 = AL_CoreConstraintEnc_GetExpectedNumberOfCores(var_30, arg1);
    v0_1 = AL_CoreConstraintEnc_GetResources(var_30, READ_S32(arg1, 0x2c), (uint32_t)READ_U16(arg1, 4),
                                             (uint32_t)READ_U16(arg1, 6), (uint32_t)READ_U16(arg1, 0x74),
                                             (uint32_t)READ_U16(arg1, 0x76), s2);
    result_1 = (uint32_t)READ_U16(arg1, 0x1a88);
    lo = (uint32_t)READ_S32(arg1, 0x1a84) / result_1;
    if (result_1 == 0U || lo == 0U) {
        __assert("0", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c", 0, "AL_EncChannel_SetNumberOfCores",
                 &_gp);
    }

    v0_3 = (uint32_t)((lo - 1 + (uint32_t)v0_1) / lo);
    if ((int32_t)v0_3 < (int32_t)s2) {
        v0_3 = s2;
    }

    result = v0_3 & 0xffU;
    if ((int32_t)result_1 < (int32_t)result) {
        result = result_1;
    }

    WRITE_U8(arg1, 0x3c, (uint8_t)result);
    return result;
}

uint32_t AL_EncChannel_ChannelCanBeLaunched(void *arg1)
{
    uint32_t result = (uint32_t)READ_U16(arg1, 0x176);
    ENC_KMSG("ChannelCanBeLaunched chctx=%p gate=0x%x active=%d need=%d flags40=%d lane0_r=%d lane0_w=%d lane1_r=%d lane1_w=%d",
             arg1, result,
             READ_S32(arg1, 0x2c),
             READ_S32(arg1, 0x30),
             READ_S32(arg1, 0x40),
             READ_S32(arg1, 0x3d8),
             READ_S32(arg1, 0x3dc),
             READ_S32(arg1, 0x434),
             READ_S32(arg1, 0x438));
    return result;
}

int32_t endOfInput(void *arg1)
{
    if (READ_U8(arg1, 0x174) == 0U) {
        return 1;
    }

    {
        void *var_10 = &_gp;

        (void)var_10;
        return (AL_SrcReorder_GetWaitingSrcBufferCount((uint8_t *)arg1 + 0x178) > 0) ? 1 : 0;
    }
}

int32_t AL_EncChannel_GetBufResources(int32_t *arg1, void *arg2)
{
    void *var_20 = &_gp;
    int32_t v0 = AL_DPBConstraint_GetMaxDPBSize(arg2);
    int32_t s0 = v0 + 1;
    int32_t v0_2;
    int32_t s0_2;
    int32_t s3_1;

    (void)var_20;
    if ((uint32_t)READ_U8(arg2, 0x1f) == 1U) {
        s0 = v0;
    }

    v0_2 = AL_RefMngr_GetBufferSize((uint8_t *)arg2 + 0x22c8);
    s0_2 = s0 + ((READ_S32(arg2, 0x1adc) < 2) ? 0 : 1) + ((READ_S32(arg2, 0x2c) >> 6) & 1);
    if ((uint32_t)READ_U8(arg2, 0x1f) == 4U) {
        s0_2 = 0;
    }

    s3_1 = READ_S32(arg2, 0x1adc);
    arg1[3] = AL_IntermMngr_GetBufferSize((uint8_t *)arg2 + 0x2a54);
    arg1[0] = s0_2;
    arg1[1] = v0_2;
    arg1[2] = s3_1;
    return (int32_t)(intptr_t)arg1;
}

int32_t AL_EncChannel_PushRefBuffer(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                    int32_t arg6, void *arg7)
{
    (void)arg7;
    return AL_RefMngr_PushBuffer((uint8_t *)arg1 + 0x22c8, arg2, arg3, (uint32_t)arg4, arg5, arg6);
}

int32_t AL_EncChannel_PushStreamBuffer(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                       int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9)
{
    void *var_30 = &_gp;
    int32_t v0_1 = arg5 + arg6;
    int32_t stream_entry[8];
    int32_t a0_1;

    (void)var_30;
    if (arg4 < v0_1) {
        v0_1 = arg4;
    }

    if ((arg4 & 3) != 0) {
        return 0;
    }

    /* OEM passes a contiguous stack record into AL_StreamMngr_AddBuffer.
     * Recreate that record explicitly instead of relying on local-variable
     * layout, which is not a valid C ABI contract. */
    stream_entry[0] = arg2;
    stream_entry[1] = arg3;
    stream_entry[2] = v0_1;
    stream_entry[3] = arg5;
    stream_entry[4] = arg7;
    stream_entry[5] = arg8;
    stream_entry[6] = arg9;
    stream_entry[7] = 0;

    if ((uint32_t)READ_U8(arg1, 0x1f) == 4U) {
        a0_1 = READ_S32(arg1, 0x1aa4);
        if (a0_1 == 0) {
            a0_1 = READ_S32(arg1, 0x2a50);
        }
    } else {
        a0_1 = READ_S32(arg1, 0x2a50);
    }

    ENC_KMSG("PushStreamBuffer queue chctx=%p mgr=%p phys=0x%x virt=%p limit=%d size=%d side0=%d side1=%d side2=%p",
             arg1, (void *)(intptr_t)a0_1, (unsigned)stream_entry[0], (void *)(intptr_t)stream_entry[1],
             stream_entry[2], stream_entry[3], stream_entry[4], stream_entry[5],
             (void *)(intptr_t)stream_entry[6]);
    return AL_StreamMngr_AddBuffer((void *)(intptr_t)a0_1, stream_entry);
}

int32_t AL_EncChannel_PushIntermBuffer(void *arg1, int32_t arg2, int32_t arg3)
{
    void *var_28 = &_gp;
    AL_TIntermBufferCompat interm = { arg2, arg3 };
    int32_t v0;
    int32_t s2_1;
    int32_t ep1_size;

    (void)var_28;
    v0 = AL_IntermMngr_GetEp1Location((uint8_t *)arg1 + 0x2a54, &interm);
    s2_1 = READ_S32(arg1, 0x35c0);
    ep1_size = AL_GetAllocSizeEP1();
    ENC_KMSG("PushInterm chctx=%p in_phys=0x%x in_virt=0x%x ep1_dst=0x%x tpl=0x%x ep1_size=%d",
             arg1, arg2, arg3, v0, s2_1, ep1_size);
    if (s2_1 == 0) {
        ENC_KMSG("PushInterm memset dst=0x%x size=%d", v0, ep1_size);
        Rtos_Memset((void *)(intptr_t)v0, 0, ep1_size);
    } else {
        ENC_KMSG("PushInterm memcpy dst=0x%x src=0x%x size=%d", v0, s2_1, ep1_size);
        Rtos_Memcpy((void *)(intptr_t)v0, (void *)(intptr_t)s2_1, ep1_size);
    }

    ENC_KMSG("PushInterm post-copy addbuf phys=0x%x virt=0x%x", interm.addr, interm.location);
    return AL_IntermMngr_AddBuffer((uint8_t *)arg1 + 0x2a54, &interm);
}

int32_t AL_EncChannel_PushNewFrame(void *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4)
{
    void *var_e8 = &_gp;
    int32_t *i;
    uint8_t *s0_1;

    (void)var_e8;
    ENC_KMSG("PushNewFrame entry chctx=%p src=%p stream=%p meta=%p flags2c=0x%x eos=%u gate=0x%x freeze=%u",
             arg1, arg2, arg3, arg4, READ_S32(arg1, 0x2c), (unsigned)READ_U8(arg1, 0x174),
             (unsigned)READ_U16(arg1, 0x176), (unsigned)READ_U8(arg1, 0x177));
    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    if (READ_U8(arg1, 0x174) != 0U) {
        ENC_KMSG("PushNewFrame reject-eos chctx=%p eos=%u flags2c=0x%x",
                 arg1, (unsigned)READ_U8(arg1, 0x174), READ_S32(arg1, 0x2c));
        return Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    }

    if (arg2 != 0 && arg3 != 0) {
        i = arg3;
    } else {
        i = arg3;
    }

    if (arg2 == 0 || arg3 == 0 || arg4 == 0) {
        s0_1 = 0;
        ENC_KMSG("PushNewFrame arm-only chctx=%p src=%p stream=%p meta=%p", arg1, arg2, arg3, arg4);
    } else {
        uint8_t src_buf[0xc8];

        /*
         * SrcReorder copies a fixed 0xc8-byte payload and later exposes the
         * command block at +0x48. Build that blob explicitly instead of
         * relying on decompiler-derived stack layout.
         */
        Rtos_Memset(src_buf, 0, sizeof(src_buf));
        Rtos_Memcpy(src_buf, arg4, 9 * sizeof(int32_t));
        Rtos_Memcpy(src_buf + 0x28, arg2, 8 * sizeof(int32_t));
        Rtos_Memcpy(src_buf + 0x48, arg3, 31 * sizeof(int32_t));
        s0_1 = src_buf;
        ENC_KMSG("PushNewFrame prepared chctx=%p pict=%d src0=0x%x src1=0x%x",
                 arg1, arg4[0], arg2[0], arg2[1]);
        ENC_KMSG("PushNewFrame packed meta0=%08x meta1=%08x meta2=%08x meta3=%08x src8=%08x src9=%08x src10=%08x src11=%08x",
                 READ_S32(src_buf, 0x00), READ_S32(src_buf, 0x04), READ_S32(src_buf, 0x08), READ_S32(src_buf, 0x0c),
                 READ_S32(src_buf, 0x48), READ_S32(src_buf, 0x4c), READ_S32(src_buf, 0x50), READ_S32(src_buf, 0x54));
        ENC_KMSG("PushNewFrame packed streammeta cmd=%08x meta0c=%08x meta10=%08x meta14=%08x meta18=%08x meta1c=%08x tailc0=%08x tailc4=%08x",
                 READ_S32(src_buf, 0x48), READ_S32(src_buf, 0x54), READ_S32(src_buf, 0x58), READ_S32(src_buf, 0x5c),
                 READ_S32(src_buf, 0x60), READ_S32(src_buf, 0x64), READ_S32(src_buf, 0xc0), READ_S32(src_buf, 0xc4));
    }

    {
        int32_t req = AddNewRequest((int32_t)(intptr_t)arg1);
        ENC_KMSG("PushNewFrame post-AddNewRequest chctx=%p req=%p srcbuf=%p",
                 arg1, (void *)(intptr_t)req, s0_1);
    }
    AL_SrcReorder_AddSrcBuffer((uint8_t *)arg1 + 0x178, s0_1);
    ENC_KMSG("PushNewFrame exit chctx=%p srcbuf=%p", arg1, s0_1);
    return Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
}

int32_t findCurCoreSlice(void *arg1, uint8_t arg2, uint8_t arg3)
{
    uint32_t i = (uint32_t)arg2;
    uint32_t a2 = (uint32_t)arg3;

    do {
        int32_t v1_2 = 1 << (i & 0x1fU);

        i += a2;
        if ((((uint32_t)v1_2 & (uint32_t)READ_S32(arg1, 0x840)) |
             ((uint32_t)(v1_2 >> 31) & (uint32_t)READ_S32(arg1, 0x844))) == 0U) {
            return (int32_t)(i - a2);
        }
    } while (i < 0x40U);

    __assert("curSlice <= 63", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c",
             0xe83, "findCurCoreSlice", &_gp);
    return 0;
}

int32_t GetWPPOrSliceSizeOffset(void *arg1, void *arg2, int32_t arg3, int32_t arg4); /* forward decl, ported by T<N> later */
int32_t GetSliceSizeOffset(void *arg1, void *arg2, int32_t arg3, int32_t arg4,
                           void *arg5, int32_t arg6, int32_t arg7); /* forward decl */
int32_t GetWPPOffset(void *arg1, void *arg2, int32_t arg3, int32_t arg4,
                     void *arg5, int32_t arg6, int32_t arg7); /* forward decl */
int32_t UpdateCommand(void *arg1, void *arg2, void *arg3, int32_t arg4); /* forward decl */
int32_t AL_GetCompLcuSize(uint32_t arg1, uint32_t arg2, int32_t arg3, int32_t arg4); /* forward decl */
int32_t AL_EncCore_Reset(void *arg1); /* forward decl */
int32_t AL_EncCore_EnableInterrupts(void *arg1, uint8_t *arg2, char arg3, char arg4, char arg5); /* forward decl */
void AL_EncCore_TurnOnRAM(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5); /* forward decl */
void AL_EncCore_Encode1(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5); /* forward decl */
void AL_EncCore_ReadStatusRegsEnc(void *arg1, void *arg2); /* forward decl */
void AL_EncCore_DisableEnc1Interrupt(void *arg1, int32_t arg2); /* forward decl */
void AL_EncCore_DisableEnc2Interrupt(void *arg1, int32_t arg2); /* forward decl */
void AL_EncCore_ResetWPPCore0(void *arg1); /* forward decl */
int32_t AL_RefMngr_Flush(void *arg1); /* forward decl */
int32_t AL_RefMngr_GetMVBufferSize(void *arg1); /* forward decl */
void AL_RateCtrl_ExtractStatistics(void *arg1, void *arg2); /* forward decl */
int32_t AL_ModuleArray_AddModule(void *arg1, int32_t arg2, int32_t arg3); /* forward decl */
int32_t AL_SrcReorder_IsEosNext(void *arg1); /* forward decl */
int32_t AL_SrcReorder_IsAvailable(void *arg1, int32_t arg2); /* forward decl */
int32_t AL_SrcReorder_GetCommandAndMoveNext(void *arg1); /* forward decl */
void AL_ApplyNewGOPAndRCParams(void *arg1, void *arg2); /* forward decl */
void AL_ApplyGopCommands(void *arg1, void *arg2, int32_t arg3); /* forward decl */
void AL_ApplyGmvCommands(void *arg1, void *arg2); /* forward decl */
void AL_ApplyPictCommands(void *arg1, void *arg2, void *arg3); /* forward decl */
void *AL_SrcReorder_GetReadyCommand(void *arg1, int32_t arg2); /* forward decl */
void AL_RefMngr_SetRecResolution(void *arg1, int32_t arg2, int32_t arg3); /* forward decl */
int32_t AL_RefMngr_GetNewFrmBuffer(void *arg1); /* forward decl */
int32_t AL_IntermMngr_GetBuffer(void *arg1); /* forward decl */
void AL_IntermMngr_ReleaseBufferBack(void *arg1, void *arg2); /* forward decl */
int32_t AL_IntermMngr_GetEp1Addr(void *arg1, void *arg2, int32_t *arg3); /* forward decl */
void AL_GetLambda(int32_t arg1, int32_t arg2, int32_t arg3, uint32_t arg4, void *arg5, void *arg6,
                  uint32_t arg7); /* forward decl */
int32_t AL_IntermMngr_GetEp2Addr(void *arg1, void *arg2, int32_t *arg3); /* forward decl */
int32_t AL_IntermMngr_GetWppAddr(void *arg1, void *arg2, void *arg3); /* forward decl */
int32_t AL_StreamMngr_Init(void *arg1); /* forward decl */
void AL_StreamMngr_Deinit(void *arg1); /* forward decl */
int32_t AL_RefMngr_Init(void *arg1, void *arg2, int32_t arg3, int32_t arg4); /* forward decl */
void AL_RefMngr_Deinit(void *arg1); /* forward decl */
int32_t AL_IntermMngr_Init(void *arg1, void *arg2); /* forward decl */
void AL_IntermMngr_Deinit(void *arg1); /* forward decl */
void AL_SrcReorder_Init(void *arg1); /* forward decl */
void AL_SrcReorder_Deinit(void *arg1); /* forward decl */
int32_t AL_GopMngr_Init(void *arg1, void *arg2, int32_t arg3, int32_t arg4); /* forward decl */
void AL_GopMngr_Deinit(void *arg1); /* forward decl */
int32_t AL_RateCtrl_Init(void *arg1, void *arg2, int32_t arg3, int32_t arg4); /* forward decl */
void AL_RateCtrl_Deinit(void *arg1); /* forward decl */
void AL_GmvMngr_Init(void *arg1); /* forward decl */
void AL_RefMngr_EnableRecOut(void *arg1); /* forward decl */
int32_t AL_GetAllocSizeEP3(void); /* forward decl */
int32_t AL_GetAllocSizeSRD(uint32_t arg1, uint32_t arg2, uint32_t arg3); /* forward decl */
int32_t AlignedAlloc(void *arg1, const void *arg2, int32_t arg3, int32_t arg4, int32_t *arg5,
                     uint32_t *arg6); /* forward decl */
int32_t AL_HEVC_GenerateSkippedPicture(void); /* forward decl */
int32_t AL_AVC_GenerateSkippedPicture(void *arg1, int32_t arg2, int32_t arg3, uint32_t arg4); /* forward decl */
void SetCommandListBuffer(void *arg1, uint32_t arg2, uint32_t arg3, int32_t arg4, int32_t arg5); /* forward decl */
void JpegParamToCtrlRegs(void *arg1, void *arg2); /* forward decl */
void AL_EncCore_EncodeJpeg(void *arg1, void *arg2); /* forward decl */
int32_t embed_watermark(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                        void *arg6, int32_t arg7, int32_t arg8, int32_t arg9); /* forward decl */
void AL_UpdateAutoQpCtrl(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                         int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10); /* forward decl */
void AL_GmvMngr_UpdateGMVPoc(void *arg1, int32_t arg2, int32_t arg3); /* forward decl */
void AL_GmvMngr_GetGMV(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, void *arg6,
                       int32_t arg7, int32_t arg8, int32_t arg9); /* forward decl */
void SliceParamToCmdRegsEnc1(void *arg1, void *arg2, void *arg3, int32_t arg4); /* forward decl */
void SliceParamToCmdRegsEnc2(void *arg1, void *arg2, int32_t arg3); /* forward decl */

static int32_t SetSourceBuffer_isra_74(void *arg1, int32_t *arg2, int32_t arg3, int32_t *arg4)
{
    int32_t *src = (int32_t *)(intptr_t)AL_SrcReorder_GetSrcBuffer((uint8_t *)arg1 + 0x178, arg3);

    arg4[0] = src[0];
    arg4[1] = src[1];
    arg4[2] = src[2];
    arg4[3] = src[3];
    arg4[4] = src[4];
    arg4[5] = src[5];
    arg4[6] = src[6];

    arg2[0] = src[10];
    arg2[1] = src[11];
    arg2[2] = src[12];
    arg2[3] = src[13];
    arg2[4] = src[14];
    arg2[5] = src[15];
    arg2[6] = src[16];
    arg2[7] = src[17];
    return src[17];
}

static int32_t UpdateRateCtrl_constprop_83(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t arg4, uint8_t arg5)
{
    int32_t rounded_bits = ((arg3[1] + 7) >> 3) << 3;
    int32_t status = 0;
    int32_t max_bits;
    int32_t produced;
    int32_t slice_budget;
    int32_t *rc = &arg1[0x3d];
    int32_t *frm = &arg2[8];

    if (arg4 == 0) {
        max_bits = arg1[3] << 3;
        if (arg2[0xc] != 7) {
            Rtos_GetMutex((void *)(uintptr_t)arg1[0x48]);
            ((void (*)(void *, void *, void *, int32_t, int32_t *))(intptr_t)arg1[0x3f])(rc, frm, arg3, max_bits, &status);
            Rtos_ReleaseMutex((void *)(uintptr_t)arg1[0x48]);
        } else {
            slice_budget = arg2[0xb2];
            goto final_update;
        }

        if ((uint32_t)arg5 != 0U && status < 0) {
            if ((arg1[0x24] & 8) != 0 && READ_U8(arg1, 0x3de0) == 0 && READ_U8(arg1, 0xc4) == 0 &&
                (arg2[9] & 1) == 0) {
                WRITE_U8(arg2, 0xb08, 1);
                AL_SrcReorder_Cancel(&arg1[0x5e], arg2[8]);
                ReleaseRefBuffers(arg1, arg2);
                Rtos_GetMutex((void *)(uintptr_t)arg1[0x5c]);
                ((void (*)(void *, void *))(intptr_t)arg1[0x50])(&arg1[0x4a], frm);
                Rtos_ReleaseMutex((void *)(uintptr_t)arg1[0x5c]);
                AL_SrcReorder_MarkSrcBufferAsUsed(&arg1[0x5e], arg2[8]);
                FillSliceParamFromPicParam(arg1, &arg2[0x5c], arg2);
                SetSourceBuffer_isra_74(arg1, arg2, arg2[8], &arg2[0xa6]);
                SetPictureReferences(arg1, arg2);
                SetPictureRefBuffers(arg1, arg2, arg1, arg2, (char)READ_U8(arg2, 0x290), &arg2[0xa6]);

                {
                    uint8_t old = READ_U8(arg2, 0x290);
                    uint8_t cur = (uint8_t)AL_RefMngr_GetFrmBuffer(&arg1[0x8b2], READ_U8(arg2, 0x291));
                    WRITE_U8(arg2, 0x290, cur);
                    if (old != 0xffU) {
                        AL_RefMngr_ReleaseFrmBuffer(&arg1[0x8b2], (char)old);
                    }
                }

                max_bits = OutputSkippedPicture(arg1, arg2, arg3) << 3;
                Rtos_GetMutex((void *)(uintptr_t)arg1[0x48]);
                ((void (*)(void *, void *, void *, int32_t, int32_t *))(intptr_t)arg1[0x3f])(rc, frm, arg3, max_bits, &status);
                Rtos_ReleaseMutex((void *)(uintptr_t)arg1[0x48]);
            }
        }
    } else {
        produced = READ_S32(arg3, 0x44) << 3;
        ENC_KMSG("UpdateRateCtrl arg4=1 frm=%p status=%p produced=%d rounded=%d type=%d skip=%u",
                 frm, arg3, produced, rounded_bits, arg2[0xc], (unsigned)READ_U8(arg2, 0xb08));

        if (produced >= rounded_bits) {
            rounded_bits = produced;
        }
        max_bits = rounded_bits;
        if (arg2[0xc] != 7) {
            ENC_KMSG("UpdateRateCtrl pre-lock rc=%p lock=%p cb3f=%p max_bits=%d",
                     rc, (void *)(uintptr_t)arg1[0x48], (void *)(intptr_t)arg1[0x3f], max_bits);
            Rtos_GetMutex((void *)(uintptr_t)arg1[0x48]);
            ENC_KMSG("UpdateRateCtrl post-lock rc=%p lock=%p", rc, (void *)(uintptr_t)arg1[0x48]);
            ((void (*)(void *, void *, void *, int32_t, int32_t *))(intptr_t)arg1[0x3f])(rc, frm, arg3, max_bits, &status);
            ENC_KMSG("UpdateRateCtrl post-cb3f rc=%p status=%d", rc, status);
            Rtos_ReleaseMutex((void *)(uintptr_t)arg1[0x48]);
            ENC_KMSG("UpdateRateCtrl post-unlock rc=%p", rc);
        } else {
            slice_budget = arg2[0xb2];
            goto final_update;
        }
    }

    if (status <= 0) {
        slice_budget = arg2[0xb2];
    } else {
        slice_budget = 8;
        if (status >= 8) {
            slice_budget = status;
        }
        arg2[0xb2] = slice_budget;
    }

final_update:
    Rtos_GetMutex((void *)(uintptr_t)arg1[0x48]);
    WRITE_U8(arg1, 0x3de0, 0);
    ((void (*)(void *, void *, void *, int32_t, uint32_t, int32_t, void *))(intptr_t)arg1[0x40])(
        rc, frm, arg3, max_bits, (uint32_t)READ_U8(arg2, 0xb08), slice_budget << 3, &_gp);
    return Rtos_ReleaseMutex((void *)(uintptr_t)arg1[0x48]);
}

void OutputSlice(void *arg1, void *arg2, int32_t arg3, void *arg4)
{
    uint8_t enc_stat_buf[0x78 * 2];
    uint8_t merged[0x78];
    uint8_t *tmp;
    int32_t i;
    uint32_t num_core;

    memset(enc_stat_buf, 0, sizeof(enc_stat_buf));
    InitSliceStatus(merged);
    tmp = enc_stat_buf;
    do {
        InitSliceStatus(tmp);
        tmp += 0x78;
    } while (tmp != merged);

    num_core = READ_U8(arg2, 0x1ee);
    {
        void *cmd_regs;
        int32_t cmd_index = arg3 << 9;

        if (READ_S32(arg2, 0x174) == 0) {
            cmd_regs = (uint8_t *)READ_PTR(arg2, 0xa78) + cmd_index;
        } else {
            uint32_t core_count = READ_U8(arg1, 0x3c);

            if (core_count == 0U) {
                __builtin_trap();
            }
            cmd_regs = (uint8_t *)READ_PTR(arg2, ((arg3 % (int32_t)core_count) + 0x29c) * 4 + 8) +
                       ((uint32_t)GetSliceEnc2CmdOffset(core_count, READ_U16(arg1, 0x40), arg3) << 9);
        }

        CmdRegsEnc1ToSliceParam(cmd_regs, (uint8_t *)arg2 + 0x170, READ_S32(arg1, 0x4c));
        if (READ_S32(arg2, 0xa6c) <= 0) {
            READ_S32(cmd_regs, 0xc8);
        } else {
            int32_t grp;

            for (grp = 0; grp < READ_S32(arg2, 0xa6c); ++grp) {
                uint8_t *base = (uint8_t *)arg2 + 0x84c + grp * 0x110;
                int32_t slice_cnt = READ_S32(base, 0x80);
                int32_t j;
                int32_t disabled = 0;

                for (j = 0; j < slice_cnt; ++j) {
                    disabled += (READ_S32(base, 4 + j * 8) ^ 1) == 0;
                }
                if (disabled != 0) {
                    READ_S32(cmd_regs, 0xf8);
                    break;
                }
            }
        }
        WRITE_S32(READ_PTR(arg2, 0x318), 0x14, READ_S32(cmd_regs, 0xc8));
    }

    if (num_core == 0U) {
        __assert("pReq->tSliceParam.NumCore > 0",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c",
                 0x1a4, "CmdList_MergeMultiSliceStatus", &_gp);
    }

    for (i = 0; i < (int32_t)num_core; ++i) {
        EncodingStatusRegsToSliceStatus((uint8_t *)READ_PTR(arg2, 0xa78 + i * 4) + (arg3 << 9), enc_stat_buf + i * 0x78);
        MergeEncodingStatus(merged, enc_stat_buf + i * 0x78);
    }

    if (READ_S32(arg2, 0xa6c) > 0) {
        int32_t grp;
        int32_t merged_any = 0;

        for (grp = 0; grp < READ_S32(arg2, 0xa6c); ++grp) {
            uint8_t *base = (uint8_t *)arg2 + 0x84c + grp * 0x110;
            int32_t slice_cnt = READ_S32(base, 0x80);
            int32_t j;
            int32_t disabled = 0;

            for (j = 0; j < slice_cnt; ++j) {
                disabled += (READ_S32(base, 4 + j * 8) ^ 1) == 0;
            }
            if (disabled != 0) {
                CmdList_MergeMultiSliceEntropyStatus(arg1, arg2, enc_stat_buf, merged,
                                                     (uint8_t)(arg3 % (int32_t)READ_U8(arg1, 0x3c)),
                                                     (uint8_t)GetSliceEnc2CmdOffset(READ_U8(arg1, 0x3c),
                                                                                    READ_U16(arg1, 0x40), arg3));
                merged_any = 1;
                break;
            }
        }

        if (merged_any == 0 && num_core != 0U) {
            for (i = 0; i < (int32_t)num_core; ++i) {
                CmdList_MergeMultiSliceEntropyStatus(arg1, arg2, enc_stat_buf, merged, (uint8_t)i, (uint8_t)arg3);
            }
        }
    }

    MergeEncodingStatus(arg4, merged);
    MergeEntropyStatus(arg4, merged);
}

int32_t TerminateRequest(void *arg1, int32_t *arg2, int32_t *arg3)
{
    int32_t i;
    int32_t mv_dst;
    int32_t mv_src;
    int32_t do_cb = 1;
    int32_t use_rc = 1;
    int32_t stats[4];

    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    if (READ_U8(arg1, 0x3c) != 0U) {
        for (i = 0; i < (int32_t)READ_U8(arg1, 0x3c); ++i) {
            WRITE_S32(arg1, 0x30 + i * 4, 0);
        }
    }

    WRITE_S32(arg1, 0x35bc, READ_S32(arg1, 0x35bc) + 1);
    SetTileInfoIfNeeded(arg1, &arg2[0x2be], arg1, READ_U8(arg2, 0x19d));
    WRITE_U16(arg2, 0xb28, READ_U16(arg2, 0x198));
    WRITE_U16(arg3, 0x3c, READ_U16(arg2, 0x198));
    WRITE_U8(arg2, 0xbad, READ_U8(arg2, 0x17e));

    if (READ_U8(arg1, 0x1f) != 0U || READ_U8(arg1, 0x3c) == 1U) {
        stats[0] = 1;
        UpdateRateCtrl_constprop_83(arg1, arg2, arg3, 0, 1);
        mv_dst = arg2[0xd6];
        do_cb = 1;
    } else {
        stats[0] = 1;
        mv_dst = arg2[0xd6];
        do_cb = 0;
    }

    if (mv_dst != 0) {
        mv_src = arg2[0xc7];
        if (mv_src != 0) {
            Rtos_Memcpy((void *)(intptr_t)mv_dst, (void *)(intptr_t)(mv_src + 0x100),
                        AL_RefMngr_GetMVBufferSize((uint8_t *)arg1 + 0x22c8) - 0x100);
        }
    }

    AL_RateCtrl_ExtractStatistics(arg3, &arg2[0x2ec]);
    if (do_cb != 0) {
        uint32_t skip = READ_U8(arg2, 0xb08);

        Rtos_GetMutex(READ_PTR(arg1, 0x170));
        ((void (*)(void *, uint32_t, void *, void *, int32_t))(intptr_t)READ_S32(arg1, 0x14c))
            ((uint8_t *)arg1 + 0x128, skip, &arg2[8], arg3, stats[0]);
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
        Rtos_GetMutex(READ_PTR(arg1, 0x170));
        ((void (*)(void *, void *, int32_t))(intptr_t)READ_S32(arg1, 0x144))((uint8_t *)arg1 + 0x128, &arg2[8], 0);
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    }

    if (READ_U8(arg2, 0x1d3) != 0U) {
        if (READ_U8(arg2, 0x1a0) == 2U) {
            WRITE_U8(arg1, 0x35e0, 0);
        } else {
            uint64_t weighted = (uint64_t)((READ_S32(arg3, 0x2c) << 4) + (READ_S32(arg3, 0x30) << 6) +
                                           READ_S32(arg3, 0x24) + (READ_S32(arg3, 0x28) << 2));
            WRITE_U8(arg1, 0x35e0, ((weighted / 3U) < (uint64_t)READ_S32(arg3, 0x1c)) ? 1 : 0);
        }
    }

    return Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
}

int32_t AL_EncChannel_GetNextFrameToOutput(void *arg1, int32_t *arg2)
{
    uint8_t tmp[0x100];
    uint8_t tmp2[0x100];
    int32_t *entry;
    uint8_t end_evt[0x100];
    StaticFifoCompat *fifo = (StaticFifoCompat *)((uint8_t *)arg1 + 0x12b24);

    ENC_KMSG("GetNextFrameToOutput entry chctx=%p fifo=%p elems=%p r=%d w=%d cap=%d eos_field=%u done=%d flushed=%u",
             arg1, fifo, fifo->elems, fifo->read_idx, fifo->write_idx, fifo->capacity,
             (unsigned)READ_U8(arg1, 0x174), READ_S32(arg1, 0x35b0), (unsigned)READ_U8(arg1, 0x175));
    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    ENC_KMSG("GetNextFrameToOutput post-lock fifo r=%d w=%d cap=%d empty=%d",
             fifo->read_idx, fifo->write_idx, fifo->capacity, (int)StaticFifo_Empty(fifo));
    if (fifo->elems == NULL || fifo->capacity == 0) {
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
        ENC_KMSG("GetNextFrameToOutput skip uninit fifo");
        return 0;
    }
    if (StaticFifo_Empty((uint8_t *)arg1 + 0x12b24) == 0) {
        entry = (int32_t *)(intptr_t)StaticFifo_Dequeue((uint8_t *)arg1 + 0x12b24);
        ENC_KMSG("GetNextFrameToOutput dequeued entry=%p", entry);
        memset(tmp, 0, sizeof(tmp));
        ENC_KMSG("GetNextFrameToOutput pre-copy1 src=%p", entry ? (void *)(entry + 8) : NULL);
        memcpy(tmp, entry + 8, 0xe0);
        memset(tmp2, 0, sizeof(tmp2));
        ENC_KMSG("GetNextFrameToOutput pre-copy2");
        memcpy(tmp2, tmp, 0xe0);
        ENC_KMSG("GetNextFrameToOutput pre-copy3 dst=%p", arg2);
        memcpy(arg2, tmp2, 0xe0);
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
        ENC_KMSG("GetNextFrameToOutput return queued");
        return 1;
    }

    if (0 && READ_U8(arg1, 0x174) != 0U && READ_S32(arg1, 0x35b0) == READ_S32(arg1, 0x35b4) && READ_U8(arg1, 0x175) == 0U) {
        WRITE_U8(arg1, 0x175, 1);
        AL_RefMngr_Flush((uint8_t *)arg1 + 0x22c8);
        memset(end_evt, 0, sizeof(end_evt));
        WRITE_S32(end_evt, 0, READ_S32(arg1, 0x44));
        WRITE_S32(end_evt, 4, READ_S32(arg1, 0x48));
        WRITE_U32(end_evt, 8, READ_U8(arg1, 0x3c));
        WRITE_U8(end_evt, 0x100 - 0xf0, 1);
        memcpy(arg2, end_evt, 0xe0);
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
        ENC_KMSG("GetNextFrameToOutput return eos");
        return 1;
    }

    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    ENC_KMSG("GetNextFrameToOutput return empty");
    return 0;
}

int32_t handleOutputTraces(void *arg1, void *arg2, uint8_t arg3, char arg4)
{
    uint8_t trace[0x128];
    int32_t cb;

    FillEncTrace((int32_t *)trace, arg1, arg2);
    WRITE_U8(trace, 0x116, arg3);
    WRITE_U8(trace, 0x117, (uint8_t)arg4);
    cb = READ_S32(arg1, 0x4c);
    if (cb == 0) {
        return (int32_t)(intptr_t)((uint8_t *)arg2 + 0xa78);
    }
    return ((int32_t (*)(void *, int32_t))(intptr_t)cb)(trace, READ_S32(arg1, 0));
}

int32_t CommitSlice(void *arg1, void *arg2, int32_t *arg3, int32_t arg4, int32_t arg5, int32_t arg6)
{
    int32_t *slice_status = &arg3[0x2f6 / 4];
    int32_t cur;

    if (READ_U8(arg3, 0x174) == 0U) {
        cur = READ_U16(arg3, 0x848);
        {
            int32_t bit = 1 << (cur & 0x1f);

            if ((cur & 0x20) != 0) {
                arg3[0x211] |= bit;
            } else {
                arg3[0x210] |= bit;
            }
        }
        WRITE_U16(arg3, 0x848, (uint16_t)(cur + 1));
        if (cur >= READ_U16(arg1, 0x40)) {
            __assert("iCurSliceIndex >= 0 && iCurSliceIndex < pChParam->uNumSlices",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c",
                     0xf04, "CommitSlice", &_gp);
        }
    } else {
        cur = READ_U8(arg3, arg4 + 0x84a) - 1;
        {
            int32_t bit = 1 << (cur & 0x1f);

            if ((cur & 0x20) != 0) {
                arg3[0x211] |= bit;
            } else {
                arg3[0x210] |= bit;
            }
        }
        WRITE_U16(arg3, 0x848, (uint16_t)(READ_U16(arg3, 0x848) + 1));
    }

    arg3[0xc6] = READ_S32(arg3, 0x338 + cur * 0x28);
    arg3[0x2ea] = arg3[0x12];
    arg3[0x2e5] = 0;
    arg3[0x2c4] = arg3[0x10];
    arg3[0x2cb] = arg3[0xd4 + cur * 0xa];
    arg3[0x2e6] = arg3[0xc];
    arg3[0x2e7] = arg3[0xd];
    WRITE_U8(arg3, 0xba0, READ_U8(arg3, 0x24) & 1U);
    WRITE_U16(arg3, 0xba4, READ_U16(arg3, 2));
    WRITE_U8(arg3, 0xbac, READ_U8(arg3, 0x3c));
    WRITE_U8(arg3, 0xba1, 0);
    WRITE_U8(arg3, 0xba2, 0);

    InitSliceStatus(slice_status);
    WRITE_U8(arg3, 0xba1, 1);
    OutputSlice(arg1, arg3, cur, slice_status);
    UpdateStatus(arg3, slice_status);
    SetTileInfoIfNeeded(arg1, &arg3[0x2be], arg1, READ_U8(arg3, 0x19d));
    WRITE_U8(arg3, 0xbad, READ_U8(arg3, 0x17e));
    WRITE_U16(arg3, 0xb28, READ_U16(arg3, 0x198));
    arg3[0x2c0] = arg3[6];
    arg3[0x2c1] = arg3[7];

    if (cur == READ_U16(arg1, 0x40) - 1) {
        StaticFifo_Dequeue(arg2);
        arg3[0x29c] += 1;
        WRITE_U8(arg3, 0xba2, 1);
        TerminateRequest(arg1, arg3, slice_status);
        ReleaseWorkBuffers(arg1, arg3);
    }

    {
        int32_t *evt = (int32_t *)(intptr_t)EndRequestsBuffer_Pop((uint8_t *)arg1 + 0x1604);

        evt[0] = arg3[0xc6];
        memcpy(evt + 2, &arg3[0x2be], (uint8_t *)slice_status - (uint8_t *)&arg3[0x2be]);
        StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + 0x12b24), (int32_t)(intptr_t)evt);
    }

    handleOutputTraces(arg1, arg3, (uint8_t)cur, 7);
    if (cur == READ_U16(arg1, 0x40) - 1) {
        WRITE_S32(arg1, 0x35b4, READ_S32(arg1, 0x35b4) + 1);
    }

    if (READ_U8(arg3, 0x174) == 0U) {
        int32_t next = cur + 1;
        int32_t bit = 1 << (next & 0x1f);

        if (((cur + 1) & 0x20) != 0) {
            if ((arg3[0x211] & bit) != 0) {
                return ((READ_U16(arg1, 0x40) - 1) == cur) ? 1 : 0;
            }
        } else if ((arg3[0x210] & bit) != 0) {
            return ((READ_U16(arg1, 0x40) - 1) == cur) ? 1 : 0;
        }
    }

    return ((READ_U16(arg1, 0x40) - 1) == cur) ? 1 : 0;
}

uint32_t adjustSubframeNumSlices(void *arg1)
{
    uint32_t num_core;
    uint32_t num_slices;

    if (READ_S32(arg1, 0xc4) == 0) {
        __assert("pChParam->bSubframeLatency",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c",
                 0x8e9, "adjustSubframeNumSlices", &_gp);
    }

    if (READ_U8(arg1, 0x1f) != 0U) {
        num_slices = READ_U16(arg1, 0x40);
        if (num_slices > 0x20U) {
            num_slices = 0x20U;
        }
        WRITE_U16(arg1, 0x40, (uint16_t)num_slices);
        return num_slices;
    }

    num_core = READ_U8(arg1, 0x3c);
    if (num_core == 0U) {
        __builtin_trap();
    }
    num_slices = ((READ_U16(arg1, 0x40) + num_core - 1U) / num_core) * num_core;
    {
        uint32_t max_slices = (0x20U / num_core) * num_core;

        if (num_slices > max_slices) {
            num_slices = max_slices;
        }
    }
    WRITE_U16(arg1, 0x40, (uint16_t)num_slices);
    return num_slices;
}

int32_t AL_EncChannel_CheckAndAdjustParam(void *arg1)
{
    int32_t flags = READ_S32(arg1, 0x10);
    int32_t ch_flags = READ_S32(arg1, 0x30);
    uint32_t lcu = READ_U8(arg1, 0x4e);
    uint32_t codec = READ_U8(arg1, 0x1f);
    uint32_t slice_per_core;
    uint32_t num_core;
    int32_t width = READ_U16(arg1, 6);

    if ((flags & 0xd) != 8) {
        return 0x90;
    }
    if ((ch_flags & 1) != 0 && READ_U8(arg1, 0xc4) != 0) {
        return 0x90;
    }
    if (((flags >> 4) & 0xd) != 8) {
        return 0x90;
    }
    if (((uint32_t)flags >> 8 & 0xfU) >= 4U) {
        return 0x90;
    }
    if (lcu >= 7U) {
        return 0x90;
    }
    if (lcu < READ_U8(arg1, 0x4f) || READ_U8(arg1, 0x4f) < 3U) {
        return 0x90;
    }
    if (codec == 4U) {
        if (READ_U16(arg1, 4) >= 0x4001U) {
            return 0x90;
        }
        if (READ_U8(arg1, 0xc4) != 0) {
            return 0x90;
        }
    } else if (codec == 0U) {
        /* AVC-specific: stock reads num_core + *(arg1+0x3e). If num_core >= 2
         * AND *(arg1+0x3e) != 0, FAIL. Stock path via 0x695b8. */
        if ((int32_t)READ_U8(arg1, 0x3c) >= 2 && READ_U16(arg1, 0x3e) != 0U) {
            return 0x90;
        }
    }
    if (READ_U32(arg1, 0xc8) >= 4U && READ_U32(arg1, 0xc8) != 0x80U) {
        return 0x90;
    }

    num_core = READ_U8(arg1, 0x3c);
    if (num_core == 1U) {
        ch_flags &= ~2;
        WRITE_S32(arg1, 0x30, ch_flags);
    } else if (READ_S32(arg1, 0xc4) == 0) {
        WRITE_S32(arg1, 0x30, ch_flags | 2);
    }

    slice_per_core = READ_U16(arg1, 0x40);
    if ((READ_S32(arg1, 0x30) & 1) != 0) {
        int32_t need_lcus = (int32_t)(slice_per_core * num_core);
        int32_t lcu_px = 1 << (lcu & 0x1f);

        if (width < need_lcus * lcu_px) {
            int32_t total = lcu_px * (int32_t)num_core;
            uint32_t new_slices;

            if (total == 0) {
                __builtin_trap();
            }
            new_slices = (uint32_t)(width / total);
            WRITE_U16(arg1, 0x40, (uint16_t)new_slices);
            if (new_slices != slice_per_core) {
                adjustSubframeNumSlices(arg1);
                return 3;
            }
        }
    }

    return 0;
}

int32_t AL_EncChannel_Init(int32_t *arg1, int32_t *arg2, void *arg3, uint8_t arg4, uint8_t arg5, int32_t arg6,
                           void *arg7, void *arg8, int32_t *arg9, int32_t arg10, int32_t arg11, int32_t arg12,
                           uint8_t arg13)
{
    AL_TAllocator *dma_alloc = (AL_TAllocator *)arg8;
    void *cmdlist_handle = NULL;
    void *cmdlist_mutex = NULL;
    intptr_t cmdlist_vaddr;
    intptr_t cmdlist_paddr;
    int32_t cmdlist_alloc_size;
    uint32_t cmdlist_align;
    uint32_t cmdlist_size;
    int32_t result;
    uint32_t width = READ_U16(arg2, 4);
    uint32_t lcu = READ_U8(arg2, 0x4e);
    uint32_t height = READ_U16(arg2, 6);
    int32_t dpb_size;
    int32_t max_ref;
    int32_t rc_mode = -1;

    SetChannelSteps(arg1, arg2);
    arg1[0xd6e] = arg11;
    arg1[0xd6b] = (int32_t)(intptr_t)arg8;
    arg1[0xd70] = arg10;
    AL_SrcReorder_Init(&arg1[0x5e]);
    result = AL_GopMngr_Init(&arg1[0x4a], arg7, arg2[0x2a], 0);
    if (result == 0) {
        AL_SrcReorder_Deinit(&arg1[0x5e]);
        AL_GopMngr_Deinit(&arg1[0x4a]);
        return 0;
    }

    ((void (*)(void *, uint32_t, int32_t, uint32_t, uint32_t))(intptr_t)arg1[0x4a])(
        &arg1[0x4a], READ_U8(arg2, 0x1f), arg2[3], (width + (1U << (lcu & 0x1fU)) - 1U) >> (lcu & 0x1fU),
        (height + (1U << (lcu & 0x1fU)) - 1U) >> (lcu & 0x1fU));
    ((void (*)(void *, void *, uint32_t))(intptr_t)arg1[0x4b])(&arg1[0x4a], &arg2[0x2a], READ_U8(arg2, 0x1f));

    if (AL_StreamMngr_Init(&arg1[0xa94]) == 0) {
        AL_SrcReorder_Deinit(&arg1[0x5e]);
        AL_GopMngr_Deinit(&arg1[0x4a]);
        return 0;
    }

    Rtos_GetMutex(READ_PTR(arg1, 0x168));
    max_ref = ((int32_t (*)(void *, uint32_t))(intptr_t)arg1[0x4c])(&arg1[0x4a], READ_U8(arg2, 0x1f));
    Rtos_ReleaseMutex(READ_PTR(arg1, 0x168));
    if (READ_U8(arg2, 0x1f) == 4U) {
        max_ref = 0;
    }

    dpb_size = AL_DPBConstraint_GetMaxDPBSize(arg2);
    {
        int32_t ref_count = dpb_size + 1;

        if (READ_U8(arg2, 0x1f) == 1U) {
            ref_count = dpb_size;
        }
        if (AL_RefMngr_Init(&arg1[0x8b2], arg2, max_ref, ref_count) == 0) {
            AL_StreamMngr_Deinit((void *)(intptr_t)arg1[0xa94]);
            AL_SrcReorder_Deinit(&arg1[0x5e]);
            AL_GopMngr_Deinit(&arg1[0x4a]);
            return 0;
        }
    }

    AL_GmvMngr_Init(&arg1[0xad4]);
    arg2[0xa] = (((arg2[0xa] & 0xffffff0f) | (max_ref << 4)) & 0xfffff0ff) | (max_ref << 8);
    memcpy(arg1, arg2, 0xf0);
    if ((arg1[0xb] & 0x40) != 0) {
        AL_RefMngr_EnableRecOut(&arg1[0x8b2]);
    }
    if (AL_IntermMngr_Init(&arg1[0xa95], arg2) == 0) {
        AL_RefMngr_Deinit(&arg1[0x8b2]);
        AL_StreamMngr_Deinit((void *)(intptr_t)arg1[0xa94]);
        AL_SrcReorder_Deinit(&arg1[0x5e]);
        AL_GopMngr_Deinit(&arg1[0x4a]);
        return 0;
    }

    if ((READ_U16(arg2, 0x76) != 0U) && (READ_U16(arg2, 0x74) != 0U) && (READ_U8(arg2, 0x78) != 0U)) {
        switch (arg2[0x1a]) {
            case 0: rc_mode = 2; break;
            case 1: rc_mode = 0; break;
            case 2: rc_mode = 1; break;
            case 3: rc_mode = 4; break;
            case 4: rc_mode = 8; break;
            case 8: rc_mode = 9; break;
            case 0x3f: rc_mode = 5; break;
            default: rc_mode = -1; break;
        }
        ENC_KMSG("AL_EncChannel_Init rc_gate w=%u h=%u codec_mode=%u rc_sel=%d rc74=0x%x rc76=0x%x rc78=0x%x",
                 width, height, (unsigned)READ_U8(arg2, 0x1f), rc_mode,
                 (unsigned)READ_U16(arg2, 0x74), (unsigned)READ_U16(arg2, 0x76),
                 (unsigned)READ_U8(arg2, 0x78));
        result = AL_RateCtrl_Init(&arg1[0x3d], arg7, rc_mode, 0);
        if (result == 0) {
            AL_IntermMngr_Deinit(&arg1[0xa95]);
            AL_RefMngr_Deinit(&arg1[0x8b2]);
            AL_StreamMngr_Deinit((void *)(intptr_t)arg1[0xa94]);
            AL_SrcReorder_Deinit(&arg1[0x5e]);
            AL_GopMngr_Deinit(&arg1[0x4a]);
            return 0;
        }
        ENC_KMSG("AL_EncChannel_Init rc_ready init=%p cfg=%p mutex=%p state=%p",
                 (void *)(intptr_t)arg1[0x3d], (void *)(intptr_t)arg1[0x3e], READ_PTR(arg1, 0x120),
                 READ_PTR(arg1, 0x11c));
        ENC_KMSG("AL_EncChannel_Init rc_init_call rc=%p width=%u height=%u",
                 &arg1[0x3d], width, height);
        ((void (*)(void *, uint32_t, uint32_t))(intptr_t)arg1[0x3d])(&arg1[0x3d], width, height);
        ENC_KMSG("AL_EncChannel_Init rc_init_done rc=%p", &arg1[0x3d]);
        Rtos_GetMutex(READ_PTR(arg1, 0x120));
        void *rc_attr = &arg2[0x1a];
        void *gop_attr = &arg2[0x2a];
        ENC_KMSG("AL_EncChannel_Init rc_cfg_call rc=%p rcAttr=%p gop=%p",
                 &arg1[0x3d], rc_attr, gop_attr);
        ((void (*)(void *, void *, void *))(intptr_t)arg1[0x3e])(&arg1[0x3d], rc_attr, gop_attr);
        ENC_KMSG("AL_EncChannel_Init rc_cfg_done rc=%p", &arg1[0x3d]);
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x120));
    }

    WRITE_S32(arg1, 0x164, (int32_t)(intptr_t)arg3);
    ResetChannelParam(arg1);
    SetTileOffsets(arg1);
    WRITE_U8(arg1, 0x3c, arg5);
    WRITE_U8(arg1, 0x3d, arg4);
    WRITE_S32(arg1, 0x2c, arg6);
    WRITE_U8(arg1, 0x3f, READ_U8(arg2, 0xf));
    WRITE_S32(arg1, 0x1a80, arg12);
    WRITE_S32(arg1, 0x1a84, arg6);
    WRITE_U16(arg1, 0x1a88, (uint16_t)READ_U8(arg2, 0xf));
    WRITE_U8(arg1, 0x44, READ_U8(arg2, 0));
    WRITE_S32(arg1, 0x48, arg9[0]);
    WRITE_S32(arg1, 0x4c, arg9[1]);
    WRITE_S32(arg1, 0x50, arg9[2]);
    WRITE_S32(arg1, 0x54, arg9[3]);
    WRITE_U8(arg1, 0x174, 0);
    WRITE_U8(arg1, 0x175, 0);
    WRITE_U16(arg1, 0x176, 1);
    WRITE_U8(arg1, 0x177, 0);
    WRITE_U8(arg1, 0x58, 0);
    WRITE_U8(arg1, 0x59, 0);
    InitMERange((int32_t)(intptr_t)arg1, arg2);
    cmdlist_mutex = Rtos_CreateMutex();
    if (cmdlist_mutex == NULL) {
        AL_RateCtrl_Deinit((uint8_t *)arg1 + 0xf4);
        AL_IntermMngr_Deinit(&arg1[0xa95]);
        AL_RefMngr_Deinit(&arg1[0x8b2]);
        AL_StreamMngr_Deinit((void *)(intptr_t)arg1[0xa94]);
        AL_SrcReorder_Deinit(&arg1[0x5e]);
        AL_GopMngr_Deinit(&arg1[0x4a]);
        return 0;
    }
    WRITE_PTR(arg1, 0x170, cmdlist_mutex);

    cmdlist_size = (uint32_t)READ_U16(arg2, 0x40) * (uint32_t)READ_U8(arg2, 0x3c) * 0x2600U;
    if (cmdlist_size != 0U) {
        cmdlist_handle = (void *)(intptr_t)AlignedAlloc(dma_alloc, "cmdlist", (int32_t)cmdlist_size, 0x20,
                                                        &cmdlist_alloc_size, &cmdlist_align);
        if (cmdlist_handle == NULL) {
            ENC_KMSG("AL_EncChannel_Init cmdlist alloc failed size=%u cores=%u blocks=%u",
                     (unsigned)cmdlist_size, (unsigned)READ_U8(arg2, 0x3c), (unsigned)READ_U16(arg2, 0x40));
            Rtos_DeleteMutex(cmdlist_mutex);
            WRITE_PTR(arg1, 0x170, NULL);
            AL_RateCtrl_Deinit((uint8_t *)arg1 + 0xf4);
            AL_IntermMngr_Deinit(&arg1[0xa95]);
            AL_RefMngr_Deinit(&arg1[0x8b2]);
            AL_StreamMngr_Deinit((void *)(intptr_t)arg1[0xa94]);
            AL_SrcReorder_Deinit(&arg1[0x5e]);
            AL_GopMngr_Deinit(&arg1[0x4a]);
            return 0;
        }

        cmdlist_vaddr = dma_alloc->vtable->GetVirtualAddr(dma_alloc, cmdlist_handle);
        cmdlist_paddr = dma_alloc->vtable->GetPhysicalAddr(dma_alloc, cmdlist_handle);
        if (cmdlist_vaddr == 0 || cmdlist_paddr == 0) {
            ENC_KMSG("AL_EncChannel_Init cmdlist map failed handle=%p v=0x%lx p=0x%lx align=%d",
                     cmdlist_handle, (unsigned long)cmdlist_vaddr, (unsigned long)cmdlist_paddr,
                     (unsigned)cmdlist_align);
            dma_alloc->vtable->Free(dma_alloc, cmdlist_handle);
            Rtos_DeleteMutex(cmdlist_mutex);
            WRITE_PTR(arg1, 0x170, NULL);
            AL_RateCtrl_Deinit((uint8_t *)arg1 + 0xf4);
            AL_IntermMngr_Deinit(&arg1[0xa95]);
            AL_RefMngr_Deinit(&arg1[0x8b2]);
            AL_StreamMngr_Deinit((void *)(intptr_t)arg1[0xa94]);
            AL_SrcReorder_Deinit(&arg1[0x5e]);
            AL_GopMngr_Deinit(&arg1[0x4a]);
            return 0;
        }

        Rtos_Memset((void *)(cmdlist_vaddr + cmdlist_align), 0, (size_t)cmdlist_alloc_size);
        SetCommandListBuffer((void *)(intptr_t)(arg1 + 0xb08), (uint32_t)READ_U8(arg2, 0x3c),
                             (uint32_t)READ_U16(arg2, 0x40), (int32_t)(cmdlist_vaddr + cmdlist_align),
                             (int32_t)(cmdlist_paddr + cmdlist_align));
        WRITE_PTR(arg1, 0x35a8, cmdlist_handle);
        ENC_KMSG("AL_EncChannel_Init cmdlist ready handle=%p v=0x%lx p=0x%lx align=%u count=%d next=%d cores=%u blocks=%u size=%u",
                 cmdlist_handle, (unsigned long)(cmdlist_vaddr + cmdlist_align),
                 (unsigned long)(cmdlist_paddr + cmdlist_align), (unsigned)cmdlist_align,
                 READ_S32((uint8_t *)arg1 + 0x2c20, 0x980), READ_S32((uint8_t *)arg1 + 0x2c20, 0x984),
                 (unsigned)READ_U8(arg2, 0x3c), (unsigned)READ_U16(arg2, 0x40), (unsigned)cmdlist_size);
    }
    WRITE_S32(arg1, 0x35cc, 0);
    arg1[0xd71] = 0;
    arg1[0xd72] = 0;
    arg1[0xd73] = 0;
    ENC_KMSG("EncChannel_Init final chctx=%p 0x2c=%d 0x3c=%u 0x3d=%u 0x40=%u legacy78=%u legacy79=%u legacy7c=%u",
             arg1, READ_S32(arg1, 0x2c), (unsigned)READ_U8(arg1, 0x3c), (unsigned)READ_U8(arg1, 0x3d),
             (unsigned)READ_U16(arg1, 0x40), (unsigned)READ_U8(arg1, 0x78), (unsigned)READ_U8(arg1, 0x79),
             (unsigned)READ_U16(arg1, 0x7c));
    (void)arg12;
    return result;
}

int32_t AL_EncChannel_DeInit(void *arg1)
{
    int32_t *alloc = (int32_t *)READ_PTR(arg1, 0x35ac);
    uint32_t core_count = READ_U8(arg1, 0x3c);
    int32_t i;
    int32_t cb = READ_S32(arg1, 0x58);

    ENC_KMSG("EncChannel_DeInit chctx=%p alloc=%p core_count=%u rc_fn=%p rc_alloc=%p rc_state=%p rc_mutex=%p stream=%p destroy_flag=0x%x",
             arg1, alloc, (unsigned)core_count,
             READ_PTR((uint8_t *)arg1 + 0xf4, 0x20),
             READ_PTR((uint8_t *)arg1 + 0xf4, 0x24),
             READ_PTR((uint8_t *)arg1 + 0xf4, 0x28),
             READ_PTR((uint8_t *)arg1 + 0xf4, 0x2c),
             READ_PTR(arg1, 0x2a50), READ_U16(arg1, 0x1ae6));

    AL_RateCtrl_Deinit((uint8_t *)arg1 + 0xf4);
    AL_GopMngr_Deinit((uint8_t *)arg1 + 0x128);
    AL_IntermMngr_Deinit((uint8_t *)arg1 + 0x2a54);
    AL_RefMngr_Deinit((uint8_t *)arg1 + 0x22c8);
    AL_StreamMngr_Deinit(READ_PTR(arg1, 0x2a50));
    AL_SrcReorder_Deinit((uint8_t *)arg1 + 0x178);
    WRITE_S32(arg1, 0x55, 0);
    WRITE_U16(arg1, 0x50, 0);
    WRITE_U8(arg1, 0x3c, 0xff);
    WRITE_S32(arg1, 0x48, 0);
    WRITE_S32(arg1, 0x4c, 0);
    ResetChannelParam(arg1);
    (*((void (**)(void *, int32_t))(*alloc + 8)))(alloc, READ_S32(arg1, 0x35a8));
    (*((void (**)(void *, int32_t))(*alloc + 8)))(alloc, READ_S32(arg1, 0x3d70));
    for (i = 0; i < 3; ++i) {
        (*((void (**)(void *, int32_t))(*alloc + 8)))(alloc, READ_S32(arg1, 0x35c4 + i * 4));
    }
    Rtos_DeleteMutex(READ_PTR(arg1, 0x170));
    if (READ_S32(arg1, 0x3d64) != 0) {
        (*((void (**)(void *, int32_t))(*alloc + 8)))(alloc, READ_S32(arg1, 0x3d64));
        WRITE_S32(arg1, 0x3d64, 0);
        WRITE_S32(arg1, 0x3d68, 0);
        WRITE_S32(arg1, 0x3d6c, 0);
    }
    if (cb != 0) {
        return ((int32_t (*)(int32_t, uint32_t))(intptr_t)cb)(READ_S32(arg1, 0x54), core_count);
    }
    return 0;
}

int32_t AL_EncChannel_ListModulesNeeded(void *arg1, void *arg2)
{
    int32_t lane;
    StaticFifoCompat *fifo_base;

    ENC_KMSG("ListModulesNeeded entry chctx=%p freeze=%u out_count=%d",
             arg1, (unsigned)READ_U8(arg1, 0x177), READ_S32(arg2, 0x80));
    if (READ_U8(arg1, 0x177) != 0U) {
        ENC_KMSG("ListModulesNeeded early-freeze chctx=%p", arg1);
        return (int32_t)(intptr_t)arg1;
    }

    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    fifo_base = (StaticFifoCompat *)((uint8_t *)arg1 + 0x129b4);
    for (lane = 0; lane != 2; ++lane) {
        StaticFifoCompat *fifo = (StaticFifoCompat *)((uint8_t *)fifo_base + lane * 0x5c);
        int32_t *req = (int32_t *)(intptr_t)StaticFifo_Front(fifo);

        ENC_KMSG("ListModulesNeeded lane=%d fifo=%p front=%p read=%d write=%d cap=%d",
                 lane, fifo, req, fifo->read_idx, fifo->write_idx, fifo->capacity);

        if (req != 0) {
            ENC_KMSG("ListModulesNeeded lane=%d req=%p prep=%u armed=%u pict=%d type=%d lane_mods=%d lane_slices=%d",
                     lane, req, (unsigned)READ_U8(req, 0xa74), (unsigned)READ_U8(req, 0xa75),
                     READ_S32(req, 0x20), READ_S32(req, 0x30),
                     READ_S32(req, lane * 0x110 + 0x8cc), READ_S32(req, lane * 0x110 + 0x958));
            if (READ_U8(req, 0xa74) != 0U) {
                int32_t *grp = (int32_t *)((uint8_t *)req + lane * 0x110 + 0x84c);
                int32_t i;

                for (i = 0; i < READ_S32(req, lane * 0x110 + 0x8cc); ++i) {
                    ENC_KMSG("ListModulesNeeded lane=%d add core=%d mod=%d idx=%d",
                             lane, READ_U8(arg1, 0x3d) + grp[0], grp[1], i);
                    AL_ModuleArray_AddModule(arg2, READ_U8(arg1, 0x3d) + grp[0], grp[1]);
                    grp += 2;
                }
                ENC_KMSG("ListModulesNeeded lane=%d prepared-count=%d", lane, READ_S32(arg2, 0x80));
            } else {
                int32_t pict_id = READ_S32(req, 0x20);

                if (READ_U8(req, 0xa75) == 0U) {
                    ENC_KMSG("ListModulesNeeded lane=%d prepare req=%p pict=%d", lane, req, pict_id);
                    if (AL_SrcReorder_IsEosNext((uint8_t *)arg1 + 0x178) != 0 &&
                        AL_SrcReorder_GetWaitingSrcBufferCount((uint8_t *)arg1 + 0x178) == 0) {
                        Rtos_GetMutex(READ_PTR(arg1, 0x170));
                        ((void (*)(void *, void *))(intptr_t)READ_S32(arg1, 0x134))((uint8_t *)arg1 + 0x128,
                                                                                     (uint8_t *)req + 0x20);
                        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                    }
                    if (READ_S32(arg1, 0x22b8) != READ_S32(arg1, 0x22bc)) {
                        while (AL_SrcReorder_IsAvailable((uint8_t *)arg1 + 0x178, pict_id) == 0) {
                            int32_t wait = AL_SrcReorder_GetWaitingSrcBufferCount((uint8_t *)arg1 + 0x178);
                            int32_t eos = AL_SrcReorder_IsEosNext((uint8_t *)arg1 + 0x178);
                            void *cmd = (void *)(intptr_t)AL_SrcReorder_GetCommandAndMoveNext((uint8_t *)arg1 + 0x178);

                            if (cmd != 0) {
                                AL_ApplyNewGOPAndRCParams(arg1, cmd);
                                AL_ApplyGopCommands((uint8_t *)arg1 + 0x128, cmd, wait);
                                AL_ApplyGmvCommands((uint8_t *)arg1 + 0x2b50, cmd);
                                if (eos != 0) {
                                    Rtos_GetMutex(READ_PTR(arg1, 0x170));
                                    ((void (*)(void *, int32_t))(intptr_t)READ_S32(arg1, 0x150))
                                        ((uint8_t *)arg1 + 0x128, wait);
                                    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                                }
                            }
                            Rtos_GetMutex(READ_PTR(arg1, 0x170));
                            ((void (*)(void *, void *))(intptr_t)READ_S32(arg1, 0x134))((uint8_t *)arg1 + 0x128,
                                                                                         (uint8_t *)req + 0x20);
                            Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                        }
                    }
                    if (READ_S32(req, 0x24) >= 0 && READ_S32(req, 0x30) != 8) {
                        void *ready = AL_SrcReorder_GetReadyCommand((uint8_t *)arg1 + 0x178, pict_id);

                        ENC_KMSG("ListModulesNeeded lane=%d ready=%p pict=%d", lane, ready, pict_id);
                        if (ready != 0 && (READ_S32(ready, 0) & 0x200) != 0) {
                            int32_t check_rc;

                            ENC_KMSG("ListModulesNeeded lane=%d ready-dynres req=%p w=%d h=%d core_hint=%u flags=0x%x",
                                     lane, req, READ_S32(ready, 0x68), READ_S32(ready, 0x6c),
                                     (unsigned)READ_U8(ready, 0x70), READ_S32(ready, 0));
                            WRITE_U16(arg1, 4, (uint16_t)READ_S32(ready, 0x68));
                            WRITE_U16(arg1, 6, (uint16_t)READ_S32(ready, 0x6c));
                            WRITE_U8(arg1, 0x44, READ_U8(ready, 0x70));
                            ENC_KMSG("ListModulesNeeded lane=%d before-set-cores chctx=%p size=%ux%u core_hint=%u",
                                     lane, arg1, (unsigned)READ_U16(arg1, 4), (unsigned)READ_U16(arg1, 6),
                                     (unsigned)READ_U8(arg1, 0x44));
                            AL_EncChannel_SetNumberOfCores(arg1);
                            ENC_KMSG("ListModulesNeeded lane=%d after-set-cores cores=%u core_base=%u active=%d need=%d",
                                     lane, (unsigned)READ_U8(arg1, 0x3c), (unsigned)READ_U8(arg1, 0x3d),
                                     READ_S32(arg1, 0x2c), READ_S32(arg1, 0x30));
                            SetChannelSteps(arg1, arg1);
                            ENC_KMSG("ListModulesNeeded lane=%d after-set-steps step4e=%u step4f=%u fmt=%u",
                                     lane, (unsigned)READ_U8(arg1, 0x4e), (unsigned)READ_U8(arg1, 0x4f),
                                     (unsigned)READ_U8(arg1, 0x1f));
                            check_rc = AL_EncChannel_CheckAndAdjustParam(arg1);
                            ENC_KMSG("ListModulesNeeded lane=%d check-adjust rc=0x%x", lane, check_rc);
                            if ((uint32_t)check_rc >= 0x80U) {
                                __assert("!AL_IS_ERROR_CODE(eErrorCode)",
                                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c",
                                         0xb62, "SetInputResolution", &_gp);
                            }
                            SetTileOffsets(arg1);
                            ENC_KMSG("ListModulesNeeded lane=%d after-tile-offsets", lane);
                            InitMERange((int32_t)(intptr_t)arg1, arg1);
                            ENC_KMSG("ListModulesNeeded lane=%d after-init-me-range", lane);
                            AL_RefMngr_SetRecResolution((uint8_t *)arg1 + 0x22c8, READ_U16(arg1, 4), READ_U16(arg1, 6));
                            ENC_KMSG("ListModulesNeeded lane=%d after-set-rec-resolution", lane);
                            ((void (*)(void *, uint32_t, uint32_t))(intptr_t)READ_S32(arg1, 0xf4))
                                ((uint8_t *)arg1 + 0xf4, READ_U16(arg1, 4), READ_U16(arg1, 6));
                            ENC_KMSG("ListModulesNeeded lane=%d after-hw-fn", lane);
                            InitHwRC_Content(arg1, arg1);
                            ENC_KMSG("ListModulesNeeded lane=%d after-init-hwrc", lane);
                        }
                    }
                    WRITE_U8(arg1, 0x177, 0);
                    ENC_KMSG("ListModulesNeeded lane=%d before-mark-used pict=%d", lane, pict_id);
                    AL_SrcReorder_MarkSrcBufferAsUsed((uint8_t *)arg1 + 0x178, pict_id);
                    ENC_KMSG("ListModulesNeeded lane=%d after-mark-used pict=%d", lane, pict_id);
                    if (READ_U8(arg1, 0x1f) != 4U) {
                        int32_t rec = AL_RefMngr_GetNewFrmBuffer((uint8_t *)arg1 + 0x22c8);

                        if (rec == 0xff) {
                            ENC_KMSG("ListModulesNeeded lane=%d no-rec-buffer pict=%d", lane, pict_id);
                            req = 0;
                            continue;
                        }

                        WRITE_S32(req, 0x838,
                                  AL_IntermMngr_GetBuffer((uint8_t *)arg1 + 0x2a54));
                        if (READ_PTR(req, 0x838) == NULL) {
                            ENC_KMSG("ListModulesNeeded lane=%d no-interm-buffer pict=%d rec=%d",
                                     lane, pict_id, rec);
                            AL_RefMngr_ReleaseFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)rec);
                            req = 0;
                            continue;
                        }

                        WRITE_S32(req, 0x318, (int32_t)(intptr_t)((uint8_t *)req + 0x338));
                        {
                            int32_t stream_ok = GetStreamBuffers_part_72(READ_PTR(arg1, 0x2a50), req, arg1);

                            ENC_KMSG("ListModulesNeeded lane=%d post-get-stream-buffers pict=%d req=%p ok=%d stream=%p",
                                     lane, pict_id, req, stream_ok, READ_PTR(req, 0x318));
                            if (stream_ok == 0) {
                            ENC_KMSG("ListModulesNeeded lane=%d no-stream-buffers pict=%d rec=%d interm=%p",
                                     lane, pict_id, rec, READ_PTR(req, 0x838));
                            AL_IntermMngr_ReleaseBufferBack((uint8_t *)arg1 + 0x2a54, READ_PTR(req, 0x838));
                            WRITE_S32(req, 0x838, 0);
                            AL_RefMngr_ReleaseFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)rec);
                            req = 0;
                            continue;
                            }
                        }

                        ENC_KMSG("ListModulesNeeded lane=%d before-set-source-buffer pict=%d req=%p srcslot=%p",
                                 lane, pict_id, req, &req[0xa6]);
                        SetSourceBuffer_isra_74(arg1, req, pict_id, &req[0xa6]);
                        ENC_KMSG("ListModulesNeeded lane=%d after-set-source-buffer pict=%d req=%p srcY=0x%x srcUV=0x%x",
                                 lane, pict_id, req, READ_S32(req, 0x298), READ_S32(req, 0x29c));
                        {
                            ENC_KMSG("ListModulesNeeded lane=%d before-get-src-buffer pict=%d reorder=%p",
                                     lane, pict_id, (uint8_t *)arg1 + 0x178);
                            int32_t *src = (int32_t *)(intptr_t)AL_SrcReorder_GetSrcBuffer((uint8_t *)arg1 + 0x178,
                                                                                            pict_id);

                            ENC_KMSG("ListModulesNeeded lane=%d after-get-src-buffer pict=%d src=%p",
                                     lane, pict_id, src);
                            if (src != NULL) {
                                WRITE_S32(req, 0x2b4, src[7]);
                                WRITE_S32(req, 0x2b8, src[8]);
                                WRITE_S32(req, 0x2bc, 0);
                                ENC_KMSG("ListModulesNeeded lane=%d copied-src-stream pict=%d ep2phys=0x%x ep2virt=0x%x",
                                         lane, pict_id, READ_S32(req, 0x2b4), READ_S32(req, 0x2b8));
                            }
                        }
                        ENC_KMSG("ListModulesNeeded lane=%d before-get-ep1 pict=%d interm=%p",
                                 lane, pict_id, READ_PTR(req, 0x838));
                        WRITE_S32(req, 0x2fc,
                                  AL_IntermMngr_GetEp1Addr((uint8_t *)arg1 + 0x2a54,
                                                           READ_PTR(req, 0x838), &req[0xca]));
                        ENC_KMSG("ListModulesNeeded lane=%d after-get-ep1 pict=%d ep1phys=0x%x ep1virt=0x%x",
                                 lane, pict_id, READ_S32(req, 0x2fc), READ_S32(req, 0x328));
                        if ((READ_S32(req, 0x2b8) | READ_S32(req, 0x2bc)) == 0 ||
                            READ_S32(req, 0x2b4) == 0) {
                            int32_t ep2_virt = 0;

                            ENC_KMSG("ListModulesNeeded lane=%d before-fallback-ep2 pict=%d interm=%p",
                                     lane, pict_id, READ_PTR(req, 0x838));
                            WRITE_S32(req, 0x2b4,
                                      AL_IntermMngr_GetEp2Addr((uint8_t *)arg1 + 0x2a54,
                                                               READ_PTR(req, 0x838), &ep2_virt));
                            WRITE_S32(req, 0x2b8, ep2_virt);
                            WRITE_S32(req, 0x2bc, 0);
                            ENC_KMSG("ListModulesNeeded lane=%d after-fallback-ep2 pict=%d ep2phys=0x%x ep2virt=0x%x",
                                     lane, pict_id, READ_S32(req, 0x2b4), READ_S32(req, 0x2b8));
                        }
                        WRITE_S32(req, 0x300, 0);
                        WRITE_S32(req, 0x324, 0);
                        WRITE_S32(req, 0x304, 0);
                        WRITE_S32(req, 0x308, 0);
                        if ((READ_U32(arg1, 0x1c) >> 0x18) == 0U && READ_U8(arg1, 0x3c) >= 2U) {
                            ENC_KMSG("ListModulesNeeded lane=%d before-get-map-data pict=%d interm=%p",
                                     lane, pict_id, READ_PTR(req, 0x838));
                            WRITE_S32(req, 0x304,
                                      AL_IntermMngr_GetMapAddr((uint8_t *)arg1 + 0x2a54,
                                                               READ_PTR(req, 0x838), 0));
                            WRITE_S32(req, 0x308,
                                      AL_IntermMngr_GetDataAddr((uint8_t *)arg1 + 0x2a54,
                                                                READ_PTR(req, 0x838), 0));
                            WRITE_S32(req, 0x2f8,
                                      AL_IntermMngr_GetWppAddr((uint8_t *)arg1 + 0x2a54,
                                                               READ_PTR(req, 0x838), &req[0xc8]));
                            ENC_KMSG("ListModulesNeeded lane=%d after-get-map-data pict=%d map=0x%x data=0x%x wpp=0x%x wppvirt=0x%x",
                                     lane, pict_id, READ_S32(req, 0x304), READ_S32(req, 0x308),
                                     READ_S32(req, 0x2f8), READ_S32(req, 0x320));
                        }
                        ENC_KMSG("ListModulesNeeded lane=%d before-set-picture-ref-bufs pict=%d req=%p rec=%d",
                                 lane, pict_id, req, rec);
                        SetPictureRefBuffers(arg1, req, arg1, req, (char)rec, &req[0xa6]);
                        ENC_KMSG("ListModulesNeeded lane=%d after-set-picture-ref-bufs pict=%d req=%p",
                                 lane, pict_id, req);
                        ENC_KMSG("ListModulesNeeded lane=%d prepared-bufs pict=%d rec=%d interm=%p stream=%p srcY=0x%x srcUV=0x%x ep1=0x%x ep2=0x%x stream_off=%d stream_part=%d",
                                 lane, pict_id, rec, READ_PTR(req, 0x838), READ_PTR(req, 0x318),
                                 READ_S32(req, 0x298), READ_S32(req, 0x29c),
                                 READ_S32(req, 0x2fc), READ_S32(req, 0x2b4),
                                 READ_PTR(req, 0x318) ? READ_S32(READ_PTR(req, 0x318), 0x0c) : 0,
                                 READ_PTR(req, 0x318) ? READ_S32(READ_PTR(req, 0x318), 0x14) : 0);
                    }
                    if (READ_S32(req, 0x30) == 2 && READ_U8(arg1, 0x2d4) != 0U) {
                        Rtos_GetMutex(READ_PTR(arg1, 0x170));
                        ((void (*)(void *))(intptr_t)READ_S32(arg1, 0x158))((uint8_t *)arg1 + 0x128);
                        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                    }
                    {
                        int32_t *pict_src = (int32_t *)(intptr_t)AL_SrcReorder_GetSrcBuffer((uint8_t *)arg1 + 0x178, pict_id);
                        int32_t *pict_cmd = pict_src ? pict_src + 0x12 : NULL;

                        ENC_KMSG("ListModulesNeeded lane=%d before-apply-pict pict=%d src=%p cmd=%p",
                                 lane, pict_id, pict_src, pict_cmd);
                        AL_ApplyPictCommands(arg1, pict_cmd, (void *)(intptr_t)pict_id);
                    }
                    ENC_KMSG("ListModulesNeeded lane=%d after-apply-pict pict=%d", lane, pict_id);
                    if (READ_U8(arg1, 0x1f) == 0U && READ_U8(arg1, 0x3c) > 1U &&
                        READ_PTR(arg1, 0x138) != (void *)(intptr_t)rc_Iol) {
                        ENC_KMSG("ListModulesNeeded lane=%d repair-update-fn old=%p new=%p",
                                 lane, READ_PTR(arg1, 0x138), (void *)(intptr_t)rc_Iol);
                        WRITE_S32(arg1, 0x138, (int32_t)(intptr_t)rc_Iol);
                    }
                    ENC_KMSG("ListModulesNeeded lane=%d before-update-fn req=%p fn=%p req20=%p w0=0x%x w1=0x%x w2=0x%x w3=0x%x",
                             lane, req, READ_PTR(arg1, 0x138), (uint8_t *)req + 0x20,
                             READ_S32(req, 0x20), READ_S32(req, 0x24), READ_S32(req, 0x28), READ_S32(req, 0x2c));
                    Rtos_GetMutex(READ_PTR(arg1, 0x170));
                    ((void (*)(void *, void *))(intptr_t)READ_S32(arg1, 0x138))((uint8_t *)arg1 + 0x128,
                                                                                 (uint8_t *)req + 0x20);
                    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                    ENC_KMSG("ListModulesNeeded lane=%d after-update-fn pict=%d w8=%08x w9=%08x wa=%08x wb=%08x wc=%08x wd=%08x w12=%08x",
                             lane, pict_id,
                             READ_S32(req, 0x20), READ_S32(req, 0x24), READ_S32(req, 0x28),
                             READ_S32(req, 0x2c), READ_S32(req, 0x30), READ_S32(req, 0x34),
                             READ_S32(req, 0x48));
                    if (READ_U8(arg1, 0x1f) != 4U) {
                        ENC_KMSG("ListModulesNeeded lane=%d before-set-picture-refs req=%p", lane, req);
                        SetPictureReferences(arg1, req);
                        ENC_KMSG("ListModulesNeeded lane=%d after-set-picture-refs req=%p", lane, req);
                    }
                    WRITE_U8(req, 0xa74, 1);
                    WRITE_U8(req, 0xa75, 1);
                    {
                        int32_t *grp = (int32_t *)((uint8_t *)req + lane * 0x110 + 0x84c);
                        int32_t i;
                        int32_t mod_count = READ_S32(req, lane * 0x110 + 0x8cc);

                        for (i = 0; i < mod_count; ++i) {
                            ENC_KMSG("ListModulesNeeded lane=%d add-prepared core=%d mod=%d idx=%d",
                                     lane, READ_U8(arg1, 0x3d) + grp[0], grp[1], i);
                            AL_ModuleArray_AddModule(arg2, READ_U8(arg1, 0x3d) + grp[0], grp[1]);
                            grp += 2;
                        }
                    }
                    ENC_KMSG("ListModulesNeeded lane=%d prepared req=%p a74=%u a75=%u lane_mods=%d lane_slices=%d out_count=%d",
                             lane, req, (unsigned)READ_U8(req, 0xa74), (unsigned)READ_U8(req, 0xa75),
                             READ_S32(req, lane * 0x110 + 0x8cc), READ_S32(req, lane * 0x110 + 0x958),
                             READ_S32(arg2, 0x80));
                }
            }
        } else {
            ENC_KMSG("ListModulesNeeded lane=%d empty", lane);
        }

        if (req != 0) {
            ENC_KMSG("ListModulesNeeded lane=%d done out_count=%d req_a74=%u req_a75=%u lane_mods=%d lane_slices=%d",
                     lane, READ_S32(arg2, 0x80), (unsigned)READ_U8(req, 0xa74), (unsigned)READ_U8(req, 0xa75),
                     READ_S32(req, lane * 0x110 + 0x8cc), READ_S32(req, lane * 0x110 + 0x958));
        } else {
            ENC_KMSG("ListModulesNeeded lane=%d done out_count=%d", lane, READ_S32(arg2, 0x80));
        }
    }

    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    ENC_KMSG("ListModulesNeeded exit chctx=%p out_count=%d", arg1, READ_S32(arg2, 0x80));
    return 0;
}

int32_t encode1(void *arg1)
{
    int32_t *ch = arg1;
    int32_t *req;
    int32_t core;
    int32_t pict_id;
    uint32_t core_offset = READ_U8(ch, 0x3d);
    int released_for_launch = 0;

    Rtos_GetMutex(READ_PTR(ch, 0x170));
    req = (int32_t *)(intptr_t)StaticFifo_Dequeue((uint8_t *)ch + 0x129b4);
    for (core = 0; core < READ_S32(req, 0x958); ++core) {
        WRITE_S32(req, 0x8d8 + core * 8, READ_S32(req, 0x8d8 + core * 8) + core_offset);
    }
    for (core = 0; core < READ_S32(req, 0x8cc); ++core) {
        WRITE_S32(req, 0x84c + core * 8, READ_S32(req, 0x84c + core * 8) + core_offset);
    }
    StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)ch + 0x12a6c + READ_S32(req, 0xa70) * 0x5c),
                     (int32_t)(intptr_t)req);
    ENC_KMSG("encode1 dequeued req=%p grp=%d slice_count=%d mod_count=%d core_off=%u",
             req, READ_S32(req, 0xa70), READ_S32(req, 0x958), READ_S32(req, 0x8cc),
             (unsigned)core_offset);
    WRITE_U8(req, 0x182, (READ_U8(ch, 0x1f) == 0U) ? (READ_U8(ch, 0x3c) < 2U) : 1U);
    WRITE_S32(ch, 0x30, 0);
    WRITE_U8(req, 0x170, 2);
    WRITE_U8(req, 0x171, READ_U8(ch, 0x50));
    WRITE_U8(req, 0x172, READ_U8(ch, 0x13c));
    WRITE_U8(req, 0x174, READ_U8(ch, 0x1f));
    WRITE_U8(req, 0x173, READ_U8(ch, 0x4e));
    WRITE_U16(req, 0x17a, (READ_U16(ch, 4) + 7U) >> 3);
    WRITE_U16(req, 0x17c, (READ_U16(ch, 6) + 7U) >> 3);
    WRITE_U16(req, 0x278, (READ_U16(ch, 4) + (1U << (READ_U8(ch, 0x4e) & 0x1fU)) - 1U) >> (READ_U8(ch, 0x4e) & 0x1fU));
    WRITE_U16(req, 0x27a, (READ_U16(ch, 6) + (1U << (READ_U8(ch, 0x4e) & 0x1fU)) - 1U) >> (READ_U8(ch, 0x4e) & 0x1fU));
    pict_id = READ_S32(req, 0x20);
    if (READ_PTR(req, 0x318) == NULL) {
        int32_t *src = (int32_t *)(intptr_t)AL_SrcReorder_GetSrcBuffer((uint8_t *)ch + 0x178, pict_id);
        if (src != NULL) {
            WRITE_S32(req, 0x318, (int32_t)(intptr_t)((uint8_t *)src + 0x48));
        }
        ENC_KMSG("encode1 req source pict=%d src=%p cmd=%p", pict_id, src, READ_PTR(req, 0x318));
    }
    if (READ_U8(ch, 0x1f) != 4U) {
        WRITE_S32(req, 0x318, (int32_t)(intptr_t)((uint8_t *)req + 0x338));
    }
    ENC_KMSG("encode1 pre-FillSliceParam req=%p mode=%u chroma=%u cores=%u dual=%u ch4e=%u ch4f=%u pre170=%u pre171=%u pre172=%u pre173=%u pre174=%u w8=%08x w9=%08x wa=%08x wb=%08x wc=%08x wd=%08x w12=%08x",
             req, (unsigned)READ_U8(ch, 0x1f), (unsigned)READ_U8(ch, 0x4), (unsigned)READ_U8(ch, 0x3c),
             (unsigned)READ_U8(req, 0x182),
             (unsigned)READ_U8(ch, 0x4e), (unsigned)READ_U8(ch, 0x4f),
             (unsigned)READ_U8(req, 0x170), (unsigned)READ_U8(req, 0x171), (unsigned)READ_U8(req, 0x172),
             (unsigned)READ_U8(req, 0x173), (unsigned)READ_U8(req, 0x174),
             READ_S32(req, 0x20), READ_S32(req, 0x24), READ_S32(req, 0x28),
             READ_S32(req, 0x2c), READ_S32(req, 0x30), READ_S32(req, 0x34),
             READ_S32(req, 0x48));
    FillSliceParamFromPicParam(ch, (uint8_t *)req + 0x170, req);
    ENC_KMSG("encode1 post-FillSliceParam req=%p slice_type=%u pic_order=%d cmd318=%p s170=%u s171=%u s172=%u s173=%u s174=%u",
             req, (unsigned)READ_U8(req, 0x170), READ_S32(req, 0x184), READ_PTR(req, 0x318),
             (unsigned)READ_U8(req, 0x170), (unsigned)READ_U8(req, 0x171), (unsigned)READ_U8(req, 0x172),
             (unsigned)READ_U8(req, 0x173), (unsigned)READ_U8(req, 0x174));
    ENC_KMSG("encode1 req-win req=%p 298=%08x 29c=%08x 2a0=%08x 2a4=%08x 2a8=%08x 2ac=%08x 2b0=%08x 2b4=%08x",
             req,
             READ_S32(req, 0x298), READ_S32(req, 0x29c), READ_S32(req, 0x2a0), READ_S32(req, 0x2a4),
             READ_S32(req, 0x2a8), READ_S32(req, 0x2ac), READ_S32(req, 0x2b0), READ_S32(req, 0x2b4));
    ENC_KMSG("encode1 req-win2 req=%p 2c0=%08x 2c4=%08x 2c8=%08x 2d0=%08x 2d4=%08x 2d8=%08x 2e0=%08x 2e4=%08x 2e8=%08x",
             req,
             READ_S32(req, 0x2c0), READ_S32(req, 0x2c4), READ_S32(req, 0x2c8), READ_S32(req, 0x2d0),
             READ_S32(req, 0x2d4), READ_S32(req, 0x2d8), READ_S32(req, 0x2e0), READ_S32(req, 0x2e4),
             READ_S32(req, 0x2e8));
    ENC_KMSG("encode1 req-win3 req=%p 2f0=%08x 2f4=%08x 2f8=%08x 2fc=%08x 300=%08x 304=%08x 308=%08x 30c=%08x 310=%08x 314=%08x",
             req,
             READ_S32(req, 0x2f0), READ_S32(req, 0x2f4), READ_S32(req, 0x2f8), READ_S32(req, 0x2fc),
             READ_S32(req, 0x300), READ_S32(req, 0x304), READ_S32(req, 0x308), READ_S32(req, 0x30c),
             READ_S32(req, 0x310), READ_S32(req, 0x314));
    ENC_KMSG("encode1 req-win4 req=%p 315=%02x 318=%p meta0c=%08x meta10=%08x meta14=%08x meta18=%08x meta1c=%08x",
             req, (unsigned)READ_U8(req, 0x315), READ_PTR(req, 0x318),
             READ_PTR(req, 0x318) ? READ_S32(READ_PTR(req, 0x318), 0x0c) : 0,
             READ_PTR(req, 0x318) ? READ_S32(READ_PTR(req, 0x318), 0x10) : 0,
             READ_PTR(req, 0x318) ? READ_S32(READ_PTR(req, 0x318), 0x14) : 0,
             READ_PTR(req, 0x318) ? READ_S32(READ_PTR(req, 0x318), 0x18) : 0,
             READ_PTR(req, 0x318) ? READ_S32(READ_PTR(req, 0x318), 0x1c) : 0);
    ENC_KMSG("encode1 pre-UpdateCommand req=%p slice=%p cmd1[0]=0x%x cmd1[1]=0x%x cmd2[0]=0x%x cmd2[1]=0x%x",
             req, (uint8_t *)req + 0x170,
             READ_S32(req, 0xa78), READ_S32(req, 0xa7c),
             READ_S32(req, 0xab8), READ_S32(req, 0xabc));
    UpdateCommand(ch, req, (uint8_t *)req + 0x170, 0);
    ENC_KMSG("encode1 post-UpdateCommand req=%p cmd318=%p cmd1[0]=0x%x",
             req, READ_PTR(req, 0x318), READ_S32(req, 0xa78));
    {
        uint8_t *slice = (uint8_t *)req + 0x170;
        void *src_meta = READ_PTR(req, 0x318);
        int32_t slice_idx = 0;

        ENC_KMSG("encode1 materialize-entry req=%p slice=%p meta=%p cores=%u",
                 req, slice, src_meta, (unsigned)READ_U8(ch, 0x3c));
        for (core = 0; core < (int32_t)READ_U8(ch, 0x3c); ++core) {
            uint8_t *cmd_regs = (uint8_t *)(intptr_t)READ_S32(req, 0xa78 + core * 4);
            int32_t stream_off = 0;
            int32_t stream_avail = 0;
            int32_t slice_off = 0;

            if (cmd_regs == NULL) {
                continue;
            }

            memset(cmd_regs, 0, 0x200);

            if (src_meta != NULL) {
                stream_off = READ_S32(src_meta, 0x14);
                stream_avail = READ_S32(src_meta, 0x10) - stream_off;
                if (stream_avail < 0) {
                    stream_avail = 0;
                }
                stream_avail &= ~0x1f;
            }

            ENC_KMSG("encode1 materialize-core core=%d cmd=%p meta10=%08x meta14=%08x",
                     core, cmd_regs,
                     src_meta ? READ_S32(src_meta, 0x10) : 0,
                     src_meta ? READ_S32(src_meta, 0x14) : 0);
            slice_off = GetWPPOrSliceSizeOffset((uint8_t *)ch, slice, core, (uint8_t)slice_idx);
            ENC_KMSG("encode1 materialize-off core=%d cmd=%p slice_off=%d", core, cmd_regs, slice_off);

            WRITE_S32(cmd_regs, 0x80, READ_S32(req, 0x298));
            WRITE_S32(cmd_regs, 0x84, READ_S32(req, 0x29c));
            WRITE_S32(cmd_regs, 0x88, (READ_S32(cmd_regs, 0x88) & 0xfffc0000) | (READ_S32(req, 0x2a4) & 0x3ffff));
            WRITE_S32(cmd_regs, 0x88, (READ_S32(cmd_regs, 0x88) & 0x7fffffff) | ((READ_S32(req, 0x2a0) & 1) << 31));
            WRITE_S32(cmd_regs, 0x88, (READ_S32(cmd_regs, 0x88) & 0x87ffffff) | ((READ_U8(req, 0x2a8) & 0x7) << 27));
            WRITE_S32(cmd_regs, 0x88, (READ_S32(cmd_regs, 0x88) & 0xf807ffff) | (READ_U8(req, 0x2a9) << 19));
            WRITE_S32(cmd_regs, 0x8c, READ_S32(req, 0x2b4));
            WRITE_S32(cmd_regs, 0x90, READ_S32(req, 0x2e0));
            WRITE_S32(cmd_regs, 0x94, READ_S32(req, 0x2e4));
            WRITE_S32(cmd_regs, 0x98, (READ_S32(cmd_regs, 0x98) & 0xfffc0000) | (READ_S32(req, 0x310) & 0x3ffff));
            WRITE_S32(cmd_regs, 0x98, (READ_S32(cmd_regs, 0x98) & 0x7fffffff) | ((READ_U8(req, 0x30c) & 1) << 31));
            WRITE_S32(cmd_regs, 0x98, (READ_S32(cmd_regs, 0x98) & 0xf807ffff) | (READ_U8(req, 0x315) << 19));
            WRITE_S32(cmd_regs, 0x98, (READ_S32(cmd_regs, 0x98) & 0x8fffffff) | ((READ_U8(req, 0x314) & 0x7) << 28));
            WRITE_S32(cmd_regs, 0x9c, READ_S32(req, 0x2fc));
            WRITE_S32(cmd_regs, 0xa0, READ_S32(req, 0x2c0));
            WRITE_S32(cmd_regs, 0xa4, READ_S32(req, 0x2c4));
            WRITE_S32(cmd_regs, 0xa8, READ_S32(req, 0x2d0));
            WRITE_S32(cmd_regs, 0xac, READ_S32(req, 0x2d4));
            if (READ_S32(req, 0x2f0) != 0) {
                WRITE_S32(cmd_regs, 0xb0, READ_S32(req, 0x2f0) + 0x100);
            }
            WRITE_S32(cmd_regs, 0xb4, READ_S32(req, 0x300));
            WRITE_S32(cmd_regs, 0xb8, READ_S32(req, 0x2f4) + 0x100);
            WRITE_S32(cmd_regs, 0xbc, READ_S32(req, 0x2f8) + slice_off);
            WRITE_S32(cmd_regs, 0xc0, src_meta ? READ_S32(src_meta, 0x0c) : 0);
            WRITE_S32(cmd_regs, 0xc4, src_meta ? READ_S32(src_meta, 0x10) : 0);
            WRITE_S32(cmd_regs, 0xc8, src_meta ? stream_off : 0);
            WRITE_S32(cmd_regs, 0xcc, stream_avail);
            WRITE_S32(cmd_regs, 0xd0, READ_S32(req, 0x304));
            WRITE_S32(cmd_regs, 0xd4, READ_S32(req, 0x308));
            WRITE_S32(cmd_regs, 0xd8, 0);
            WRITE_S32(cmd_regs, 0xdc, READ_S32(req, 0x2e8));
            WRITE_S32(cmd_regs, 0xe0, READ_S32(req, 0x2c8));
            WRITE_S32(cmd_regs, 0xe4, READ_S32(req, 0x2d8));

            ENC_KMSG("encode1 materialize-prepack core=%d cmd=%p c0=%08x c4=%08x c8=%08x cc=%08x",
                     core, cmd_regs,
                     READ_S32(cmd_regs, 0xc0), READ_S32(cmd_regs, 0xc4),
                     READ_S32(cmd_regs, 0xc8), READ_S32(cmd_regs, 0xcc));
            SliceParamToCmdRegsEnc1(slice, cmd_regs, (uint8_t *)req + 0x298, READ_U8(ch, 0x4f));
            ENC_KMSG("encode1 materialized core=%d cmd=%p slice_off=%d stream_off=%d stream_avail=%d w0=%08x w1=%08x w2=%08x w3=%08x",
                     core, cmd_regs, slice_off, stream_off, stream_avail,
                     READ_S32(cmd_regs, 0x00), READ_S32(cmd_regs, 0x04),
                     READ_S32(cmd_regs, 0x08), READ_S32(cmd_regs, 0x0c));
        }
    }
    ENC_KMSG("encode1 pre-handleInputTraces req=%p", req);
    handleInputTraces(ch, req, (uint8_t *)req + 0x170, 0);
    ENC_KMSG("encode1 post-handleInputTraces req=%p", req);
    ENC_KMSG("encode1 pre-TurnOnRAM cores=%u", (unsigned)READ_U8(ch, 0x3c));
    AL_EncCore_TurnOnRAM(READ_PTR(ch, 0x164), READ_U8(ch, 0x1f), READ_U8(ch, 0x3c), 0, 0);
    ENC_KMSG("encode1 post-TurnOnRAM");
    ENC_KMSG("encode1 pre-EnableInterrupts cores=%u core_tbl=%p",
             (unsigned)READ_U8(ch, 0x3c), (uint8_t *)ch + 0x3c);
    AL_EncCore_EnableInterrupts(READ_PTR(ch, 0x164), (uint8_t *)ch + 0x3c, 0, 1, 0);
    ENC_KMSG("encode1 post-EnableInterrupts");
    /* The first AVPU IRQ can arrive before all cores are launched. Holding the
     * channel mutex across AL_EncCore_Encode1 then blocks EndEncoding on the
     * callback thread, which stalls the pipeline after the first interrupt. */
    Rtos_ReleaseMutex(READ_PTR(ch, 0x170));
    released_for_launch = 1;
    ENC_KMSG("encode1 released mutex before launch req=%p", req);
    for (core = 0; core < (int32_t)READ_U8(ch, 0x3c); ++core) {
        void *enc1 = (uint8_t *)READ_PTR(ch, 0x164) + core * 0x44;
        int32_t cmd1 = READ_S32(req, 0xa78 + core * 4);
        int32_t cmd2 = READ_S32(req, 0xab8 + core * 4);
        void *req18 = READ_PTR(req, 0x18);
        void *req18_24 = req18 ? READ_PTR(req18, 0x24) : NULL;

        ENC_KMSG("encode1 launch core=%d enc=%p cmd1=0x%x cmd2=0x%x dual=%u",
                 core, enc1, cmd1, cmd2, (unsigned)READ_U8(req, 0x182));
        ENC_KMSG("encode1 launch-ctx core=%d req18=%p req18+24=%p req18_24_428=%08x req18_24_42c=%08x req18_24_430=%08x req18_24_434=%08x req18_24_43c=%p",
                 core, req18, req18_24,
                 req18_24 ? READ_S32(req18_24, 0x428) : 0,
                 req18_24 ? READ_S32(req18_24, 0x42c) : 0,
                 req18_24 ? READ_S32(req18_24, 0x430) : 0,
                 req18_24 ? READ_S32(req18_24, 0x434) : 0,
                 req18_24 ? READ_PTR(req18_24, 0x43c) : NULL);
        PrepareSourceConfigBeforeLaunch(enc1, ch, req, (uint32_t *)(uintptr_t)cmd1);

        AL_EncCore_Encode1(enc1, cmd2, cmd1, (READ_U8(req, 0x182) != 0U) ? cmd2 : 0,
                           (READ_U8(req, 0x182) != 0U) ? cmd1 : 0);
    }
    if (!released_for_launch)
        return Rtos_ReleaseMutex(READ_PTR(ch, 0x170));
    return 0;
}

int32_t AL_EncChannel_Encode(void *arg1, void *arg2)
{
    int32_t (*workers[2])(void *) = { encode1, encode2 };
    StaticFifoCompat *running = (StaticFifoCompat *)((uint8_t *)arg1 + 0x129b4);
    int lane;
    int32_t result = 0;

    ENC_KMSG("EncChannel_Encode entry chctx=%p modules=%p codec=%u cores=%u core_base=%u",
             arg1, arg2, (unsigned)READ_U8(arg1, 0x1f), (unsigned)READ_U8(arg1, 0x3c), (unsigned)READ_U8(arg1, 0x3d));
    if (READ_U8(arg1, 0x1f) != 4U) {
        int i;
        uint8_t *core = (uint8_t *)READ_PTR(arg1, 0x164);

        for (i = 0; i < (int)READ_U8(arg1, 0x3c); ++i) {
            AL_EncCore_Reset(core + i * 0x44);
        }

        for (lane = 0; lane != 2; ++lane) {
            int32_t active = 0;
            int32_t *front = (int32_t *)(intptr_t)StaticFifo_Front(running);

            if (READ_S32(arg2, 0x80) > 0) {
                int32_t *slot = (int32_t *)arg2;
                int i;

                for (i = 0; i < READ_S32(arg2, 0x80); ++i) {
                    active += (lane == READ_S32(slot, 4));
                    slot += 2;
                }
            }
            if (active != 0 && front != 0) {
                int32_t ofs = READ_S32(front, 0xa70) * 0x110;
                int32_t slice_count = READ_S32(front, ofs + 0x958);
                int32_t i;
                int32_t match = 0;

                for (i = 0; i < slice_count; ++i) {
                    match += (lane == READ_S32(front, ofs + 0x8dc + i * 8));
                }
                if (match != 0) {
                    ENC_KMSG("EncChannel_Encode lane=%d active=%d slice_match=%d front=%p", lane, active, match, front);
                    result = workers[lane](arg1);
                    ENC_KMSG("EncChannel_Encode worker lane=%d result=%d", lane, result);
                }
            }
            running = (StaticFifoCompat *)((uint8_t *)running + 0x5c);
        }
        ENC_KMSG("EncChannel_Encode exit result=%d", result);
        return result;
    }

    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    {
        int32_t *req = (int32_t *)(intptr_t)StaticFifo_Dequeue((uint8_t *)arg1 + 0x129b4);
        int i;

        for (i = 0; i < READ_S32(req, 0x958); ++i) {
            WRITE_S32(req, 0x8d8 + i * 8, READ_S32(req, 0x8d8 + i * 8) + READ_U8(arg1, 0x3d));
        }
        for (i = 0; i < READ_S32(req, 0x8cc); ++i) {
            WRITE_S32(req, 0x84c + i * 8, READ_S32(req, 0x84c + i * 8) + READ_U8(arg1, 0x3d));
        }
        StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + 0x12a6c), (int32_t)(intptr_t)req);
        WRITE_S32(req, 0x318, (int32_t)(intptr_t)((uint8_t *)req + 0x338));
        memset((void *)(intptr_t)READ_S32(req, 0xa78), 0, 0x200);
        JpegParamToCtrlRegs(arg1, (void *)(intptr_t)READ_S32(req, 0xa78));
        WRITE_S32((void *)(intptr_t)READ_S32(req, 0xa78), 0x10, READ_S32(req, 0x298));
        WRITE_S32((void *)(intptr_t)READ_S32(req, 0xa78), 0x14, READ_S32(req, 0x29c));
        WRITE_S32((void *)(intptr_t)READ_S32(req, 0xa78), 0x18, READ_S32(req, 0x2fc));
        WRITE_S32((void *)(intptr_t)READ_S32(req, 0xa78), 0x1c, READ_S32(READ_PTR(req, 0x318), 0xc));
        WRITE_S32((void *)(intptr_t)READ_S32(req, 0xa78), 0x20, READ_S32(READ_PTR(req, 0x318), 0x10));
        WRITE_S32((void *)(intptr_t)READ_S32(req, 0xa78), 0x24, READ_S32(READ_PTR(req, 0x318), 0x14));
        WRITE_S32((void *)(intptr_t)READ_S32(req, 0xa78), 0x28,
                  READ_S32(READ_PTR(req, 0x318), 0x10) - READ_S32(READ_PTR(req, 0x318), 0x14));
        handleJpegInputTrace(arg1, req);
        AL_EncCore_TurnOnRAM(READ_PTR(arg1, 0x164), 4, 1, 0, 0);
        AL_EncCore_SetJpegInterrupt(READ_PTR(arg1, 0x164));
        AL_EncCore_EncodeJpeg(READ_PTR(arg1, 0x164), (void *)(intptr_t)READ_S32(req, 0xa78));
    }
    return Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
}

int32_t AL_EncChannel_EndEncoding(void *arg1, uint8_t arg2, int32_t arg3)
{
    ENC_KMSG("EndEncoding entry chctx=%p core=%u lane=%d jpeg=%u gate=%u",
             arg1, (unsigned)arg2, arg3, (unsigned)READ_U8(arg1, 0x1f), (unsigned)READ_U8(arg1, 0xc4));
    if (READ_U8(arg1, 0x1f) == 4U) {
        EndJpegEncoding(arg1);
        return 1;
    }

    if (READ_U8(arg1, 0xc4) != 0U) {
        Rtos_GetMutex(READ_PTR(arg1, 0x170));
        {
            void *fifo = getFifoRunning(arg1, arg3);

            if (fifo == 0) {
                ENC_KMSG("EndEncoding no-fifo chctx=%p core=%u lane=%d", arg1, (unsigned)arg2, arg3);
                Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                return 0;
            }

            {
                int32_t *req = (int32_t *)(intptr_t)StaticFifo_Front(fifo);
                int32_t *core = (int32_t *)((uint8_t *)req + arg2);
                uint32_t done = READ_U8(core, 0x84a) + 1U;

                ENC_KMSG("EndEncoding req=%p core_slot=%p done=%u req174=%u req848=%u core_base=%u core_count=%u",
                         req, core, (unsigned)done, (unsigned)READ_U8(req, 0x174),
                         (unsigned)READ_U16(req, 0x848), (unsigned)READ_U8(arg1, 0x3d),
                         (unsigned)READ_U8(arg1, 0x3c));

                WRITE_U8(core, 0x84a, (uint8_t)done);
                WRITE_S32(req, req[0x29c] * 0x11 + arg3 + 0x234, READ_S32(req, req[0x29c] * 0x11 + arg3 + 0x234) - 1);
                if (READ_U8(req, 0x174) == 0U) {
                    uint32_t num_core = READ_U8(arg1, 0x3c);
                    uint8_t start = READ_U8(arg1, 0x3d);

                    if (num_core == 0U) {
                        __builtin_trap();
                    }
                    if ((uint32_t)arg2 == (uint32_t)(start + (READ_U16(req, 0x848) % num_core))) {
                        int32_t committed = CommitSlice(arg1, fifo, req, arg2, 0, arg3);

                        ENC_KMSG("EndEncoding commit-single req=%p core=%u lane=%d committed=%d",
                                 req, (unsigned)arg2, arg3, committed);

                        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                        if (committed == 0) {
                            return 0;
                        }
                        if (READ_S32(arg1, 0x50) != 0) {
                            ((void (*)(int32_t, void *))(intptr_t)READ_S32(arg1, 0x50))
                                (READ_S32(arg1, 0x54), (uint8_t *)req + (req[0x29c] - 1) * 0x110 + 0x84c);
                        }
                        return 1;
                    }
                } else {
                    int32_t ready = 0;
                    int32_t *base = (int32_t *)((uint8_t *)req + READ_U8(arg1, 0x3d) + 0x84a);

                    if (READ_S32(base, 0) >= (int32_t)done &&
                        (READ_U8(arg1, 0x3c) == 1U || READ_S32(base, 1) >= (int32_t)done)) {
                        ready = 1;
                    }
                    if (ready != 0) {
                        int32_t committed = CommitSlice(arg1, fifo, req, arg2, 0, arg3);

                        ENC_KMSG("EndEncoding commit-ready req=%p core=%u lane=%d committed=%d done=%u",
                                 req, (unsigned)arg2, arg3, committed, (unsigned)done);

                        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                        if (committed == 0) {
                            return 0;
                        }
                        if (READ_S32(arg1, 0x50) != 0) {
                            ((void (*)(int32_t, void *))(intptr_t)READ_S32(arg1, 0x50))
                                (READ_S32(arg1, 0x54), (uint8_t *)req + (req[0x29c] - 1) * 0x110 + 0x84c);
                        }
                        return 1;
                    }
                }

                if (READ_U8(req, 0x174) == 0U) {
                    int32_t bit = 1 << (findCurCoreSlice(req, (uint8_t)(arg2 - READ_U8(arg1, 0x3d)),
                                                         (uint8_t)READ_U8(arg1, 0x3c)) & 0x1f);
                    req[0x210] |= bit;
                    req[0x211] |= bit >> 31;
                    ENC_KMSG("EndEncoding mark-pending req=%p bit=0x%x mask0=0x%x mask1=0x%x",
                             req, bit, req[0x210], req[0x211]);
                }
            }
        }
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
        return 0;
    }

    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    {
        void *fifo = getFifoRunning(arg1, arg3);

        if (fifo != 0) {
            int32_t *req = (int32_t *)(intptr_t)StaticFifo_Front(fifo);
            int32_t req_phase = READ_S32(req, 0xa70);
            int32_t pending_off = 0x8d0 + ((req_phase * 0x44 + arg3) << 2);
            int32_t pending = READ_S32(req, pending_off) - 1;

            WRITE_S32(req, pending_off, pending);
            ENC_KMSG("EndEncoding non-gate req=%p lane=%d phase=%d pending=%d a6c=%d",
                     req, arg3, req_phase, pending, READ_S32(req, 0xa6c));

            if (pending <= 0) {
                void *core_arr = READ_PTR(arg1, 0x164);
                int32_t req_phase_next;
                int32_t done_cb;
                int32_t force_status;
                int32_t active_cores;
                int32_t core_idx;
                uint8_t slice_status[0x78];
                int32_t *evt;
                int32_t *i;
                int32_t *dst;
                int32_t slice_count;
                int32_t slice_idx;

                StaticFifo_Dequeue(fifo);
                req_phase_next = req_phase + 1;
                WRITE_S32(req, 0xa70, req_phase_next);

                AL_EncCore_TurnOffRAM(READ_PTR(arg1, 0x164), 0, READ_U8(arg1, 0x3c), 0, 0);

                WRITE_S32(req, 0xaf8, READ_S32(req, 0x10));
                WRITE_S32(req, 0xafc, READ_S32(req, 0x14));
                WRITE_S32(req, 0xb00, READ_S32(req, 0x18));
                WRITE_S32(req, 0xb04, READ_S32(req, 0x1c));
                WRITE_S32(req, 0xb94, 0);
                WRITE_S32(req, 0xb10, READ_S32(req, 0x40));
                WRITE_S32(req, 0xb98, READ_S32(req, 0x30));
                WRITE_S32(req, 0xb9c, READ_S32(req, 0x34));
                WRITE_U8(req, 0xba0, READ_U8(req, 0x24) & 1U);
                WRITE_U16(req, 0xba4, READ_U16(req, 4));
                WRITE_S32(req, 0xba8, READ_S32(req, 0x48));
                WRITE_U8(req, 0xbac, READ_U8(req, 0x3c));
                WRITE_U8(req, 0xba1, 1);
                WRITE_U8(req, 0xba2, 1);
                WRITE_S32(req, 0xb2c, READ_S32(READ_PTR(req, 0x318), 0x18));

                if (req_phase_next < READ_S32(req, 0xa6c)) {
                    void *core_arr_164 = READ_PTR(arg1, 0x164);
                    void *core_arr_168 = READ_PTR(arg1, 0x168);

                    ENC_KMSG("EndEncoding non-gate phase-advance req=%p next=%d total=%d core164=%p core168=%p active=%u",
                             req, req_phase_next, READ_S32(req, 0xa6c), core_arr_164, core_arr_168,
                             (unsigned)READ_U8(req, 0x1ee));
                    StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + req_phase_next * 0x5c + 0x12a6c),
                                     (int32_t)(intptr_t)req);
                    ENC_KMSG("EndEncoding non-gate queued req=%p fifo_off=0x%x",
                             req, req_phase_next * 0x5c + 0x12a6c);
                    InitSliceStatus(slice_status);
                    active_cores = (int32_t)READ_U8(req, 0x1ee);
                    core_arr = core_arr_164;
                    for (core_idx = 0; core_idx < active_cores; ++core_idx) {
                        ENC_KMSG("EndEncoding non-gate read-status req=%p core_idx=%d core=%p",
                                 req, core_idx, (uint8_t *)core_arr + core_idx * 0x44);
                        AL_EncCore_ReadStatusRegsEnc((uint8_t *)core_arr + core_idx * 0x44, slice_status);
                    }
                    ENC_KMSG("EndEncoding non-gate read-status-done req=%p rc_bytes=%d status1=%d status2=%d",
                             req, READ_S32(slice_status, 4), READ_S32(slice_status, 0), READ_S32(slice_status, 8));
                    force_status = 0;
                    if ((READ_S32(arg1, 0x90) & 8) != 0 &&
                        READ_U8(arg1, 0x3de0) == 0U &&
                        READ_U8(arg1, 0xc4) == 0U) {
                        force_status = 1;
                    }
                    ENC_KMSG("EndEncoding non-gate pre-rc req=%p force_status=%d skip=%u",
                             req, force_status, (unsigned)READ_U8(req, 0xb08));
                    UpdateRateCtrl_constprop_83(arg1, req, (int32_t *)slice_status, 1, 1);
                    ENC_KMSG("EndEncoding non-gate post-rc req=%p rc_bytes=%d slice_budget=%d skip=%u",
                             req, READ_S32(slice_status, 4), READ_S32(req, 0x2c8 * 4), (unsigned)READ_U8(req, 0xb08));
                    ENC_KMSG("EndEncoding non-gate pre-cb14c req=%p cb=%p",
                             req, (void *)(intptr_t)READ_S32(arg1, 0x14c));
                    ((void (*)(void *, uint32_t, void *, void *, int32_t))(intptr_t)READ_S32(arg1, 0x14c))
                        ((uint8_t *)arg1 + 0x128, READ_U8(req, 0xb08), (uint8_t *)req + 0x20, slice_status,
                         force_status);
                    ENC_KMSG("EndEncoding non-gate post-cb14c req=%p", req);
                    ENC_KMSG("EndEncoding non-gate pre-cb144 req=%p cb=%p",
                             req, (void *)(intptr_t)READ_S32(arg1, 0x144));
                    ((void (*)(void *, void *, int32_t))(intptr_t)READ_S32(arg1, 0x144))
                        ((uint8_t *)arg1 + 0x128, (uint8_t *)req + 0x20, 0);
                    ENC_KMSG("EndEncoding non-gate post-cb144 req=%p", req);
                    ENC_KMSG("EndEncoding non-gate pre-store req=%p", req);
                    StorePicture((int32_t)(intptr_t)((uint8_t *)arg1 + 0x22c8), req);
                    ENC_KMSG("EndEncoding non-gate post-store req=%p", req);
                    ENC_KMSG("EndEncoding non-gate pre-output-trace req=%p", req);
                    handleOutputTraces(arg1, req, (uint8_t)(READ_U8(arg1, 0x3c) - 1U), 3);
                    ENC_KMSG("EndEncoding non-gate post-output-trace req=%p", req);
                    done_cb = READ_S32(arg1, 0x1aa8);
                    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                    ENC_KMSG("EndEncoding non-gate queued-next-phase req=%p next=%d total=%d rc_bytes=%d skip=%u",
                             req, req_phase_next, READ_S32(req, 0xa6c), READ_S32(slice_status, 4),
                             (unsigned)READ_U8(req, 0xb08));
                    if (done_cb != 0) {
                        ((void (*)(void *, void *))(intptr_t)done_cb)
                            (READ_PTR(arg1, 0x1aac),
                             (uint8_t *)req + (READ_S32(req, 0xa70) - 1) * 0x110 + 0x84c);
                    }
                    return 1;
                }

                InitSliceStatus(slice_status);
                if (READ_S32(req, 0x30) == 7) {
                    OutputSkippedPicture(arg1, req, slice_status);
                } else {
                    slice_count = READ_U16(arg1, 0x40);
                    for (slice_idx = 0; slice_idx < slice_count; ++slice_idx) {
                        OutputSlice(arg1, req, slice_idx, slice_status);
                    }
                }
                UpdateStatus(req, (int32_t *)slice_status);
                handleOutputTraces(arg1, req, (uint8_t)(READ_U8(arg1, 0x3c) - 1U),
                                   (READ_S32(req, 0xa6c) >= 2) ? 5 : 7);
                TerminateRequest(arg1, req, (int32_t *)slice_status);
                ReleaseWorkBuffers(arg1, req);

                evt = (int32_t *)(intptr_t)EndRequestsBuffer_Pop((uint8_t *)arg1 + 0x1604);
                evt[0] = (int32_t)(intptr_t)READ_PTR(req, 0x318);
                i = &req[0xaf8 / 4];
                dst = evt + 2;
                while ((uint8_t *)i != (uint8_t *)req + 0xbd8) {
                    dst[0] = i[0];
                    dst[1] = i[1];
                    dst[2] = i[2];
                    dst[3] = i[3];
                    i += 4;
                    dst += 4;
                }
                StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + 0x12b24), (int32_t)(intptr_t)evt);
                WRITE_S32(arg1, 0x35b4, READ_S32(arg1, 0x35b4) + 1);
                done_cb = READ_S32(arg1, 0x1aa8);
                Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                ENC_KMSG("EndEncoding non-gate queued-output req=%p evt=%p phase=%d", req, evt, req_phase_next);
                if (done_cb != 0) {
                    ((void (*)(void *, void *))(intptr_t)done_cb)(READ_PTR(arg1, 0x1aac), (uint8_t *)req + 0x84c);
                }
                return 1;
            }
        }
    }
    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    return 0;
}

int32_t AL_EncChannel_SetTraceCallBack(void *arg1, int32_t arg2, int32_t arg3)
{
    WRITE_S32(arg1, 0x4c, arg2);
    WRITE_S32(arg1, 0x54, arg3);
    return 0;
}
