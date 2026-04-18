#include <stdint.h>
#include <string.h>

#include "alcodec/al_buffer.h"
#include "alcodec/al_rtos.h"
#include "alcodec/al_types.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function,
                        void *caller);

typedef struct StaticFifoCompat {
    int32_t *elems;
    int32_t read_idx;
    int32_t write_idx;
    int32_t capacity;
} StaticFifoCompat;

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
            Rtos_GetMutex((void *)(uintptr_t)a0_2);
            ((void (*)(void *, void *, void *))(intptr_t)arg1[0x41])(&arg1[0x3d], &arg3[8],
                                                                      (uint8_t *)arg2 + 0x28);
            Rtos_ReleaseMutex((void *)(uintptr_t)arg1[0x48]);
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
    AL_RefMngr_GetFrmBufAddrs((uint8_t *)arg1 + 0x22c8, (char)s5, (int32_t *)((uint8_t *)arg6 + 0x48),
                              (int32_t *)((uint8_t *)arg6 + 0x4c), (uint8_t *)arg6 + 0x74);

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
        AL_RefMngr_GetRefInfo((int32_t)(intptr_t)((uint8_t *)arg1 + 0x22c8), a1_2, (uint8_t *)arg4 + 0x20,
                              (uint8_t *)arg4 + 0x54, (int32_t *)(intptr_t)v1);
        v0_2 = AL_RefMngr_GetRefBufferFromPOC((uint8_t *)arg1 + 0x22c8, READ_S32(arg4, 0x5c));
        AL_RefMngr_GetFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)v0_2);
        WRITE_U8(arg2, 0x291, (uint8_t)v0_2);
        AL_RefMngr_GetFrmBufAddrs((uint8_t *)arg1 + 0x22c8, (char)v0_2, (int32_t *)((uint8_t *)arg6 + 0x28),
                                  (int32_t *)((uint8_t *)arg6 + 0x2c), 0);
        WRITE_U8(arg2, 0x290, (uint8_t)s5);
        a1_6 = READ_S32(arg4, 0x70);
        WRITE_S32(arg6, 0x50, 0);
        WRITE_S32(arg6, 0x54, 0);
        WRITE_S32(arg6, 0x30, 0);
        WRITE_S32(arg6, 0x34, 0);
        WRITE_S32(arg6, 0x40, 0);
        WRITE_S32(arg6, 0x44, 0);
        v0_3 = AL_RefMngr_GetRefBufferFromPOC((uint8_t *)arg1 + 0x22c8, a1_6);
        AL_RefMngr_GetFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)v0_3);
        WRITE_U8(arg2, 0x292, (uint8_t)v0_3);
        AL_RefMngr_GetFrmBufAddrs((uint8_t *)arg1 + 0x22c8, (char)v0_3, (int32_t *)((uint8_t *)arg6 + 0x38),
                                  (int32_t *)((uint8_t *)arg6 + 0x3c), 0);
        v0_4 = AL_RefMngr_GetRefBufferFromPOC((uint8_t *)arg1 + 0x22c8, READ_S32(arg4, 0x84));
        AL_RefMngr_GetFrmBuffer((uint8_t *)arg1 + 0x22c8, (char)v0_4);
        WRITE_U8(arg2, 0x293, (uint8_t)v0_4);
        v0_5 = AL_RefMngr_GetMvBufAddr((uint8_t *)arg1 + 0x22c8, (char)v0_4, (int32_t *)&var_28);
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
    v0 = (int32_t)(intptr_t)RequestsBuffer_Pop((void *)(intptr_t)(arg1 + 0x3de8));
    Rtos_Memset((void *)(intptr_t)v0, 0, 0xc58);
    PopCommandListAddresses((void *)(intptr_t)(arg1 + 0x2c20), (void *)(intptr_t)(v0 + 0xa78));
    return StaticFifo_Queue((StaticFifoCompat *)(intptr_t)(arg1 + READ_S32((void *)(intptr_t)v0, 0xa70) * 0x5c + 0x129b4),
                            v0);
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
int32_t AL_RefMngr_PushBuffer(void *arg1); /* forward decl, ported by T<N> later */
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
            int32_t var_70[7];
            int32_t var_50;
            int32_t var_4c;
            int32_t var_48;
            int32_t var_44;
            int32_t var_40;
            int32_t var_3c;
            int32_t var_38;
            uint32_t shift = (uint32_t)READ_U16(arg3, 0x4e);
            uint32_t max_size = (uint32_t)READ_U16(arg3, 0x40);
            int32_t part_size;
            int32_t result_1;

            result_1 = AL_StreamMngr_GetBuffer(arg1, &var_50);
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

                        var_70[0] = s5_1[-1];
                        var_70[1] = s5_1[-2];
                        var_70[2] = part_size;
                        var_70[3] = s5_1[1];
                        var_70[4] = s5_1[-4];
                        var_70[5] = s5_1[-3];
                        var_70[6] = s5_1[4];
                        s5_1 -= 10;
                        s7_2 -= 1;
                        AL_StreamMngr_AddBufferBack(arg1, var_70);
                        s5_1[8] = 0;
                        if (s7_2 == -1) {
                            return result;
                        }
                    }
                }

                return result;
            }

            s7_1[-4] = var_50;
            s7_1[-2] = var_4c;
            s7_1[-4 + 2] = var_40;
            s7_1[-3 + 2] = var_3c;
            s7_1[4] = var_38;
            s7_1[0] = var_48;
            s7_1[1] = var_44;
            s7_1[3] = var_44;

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
            if (part_size >= var_48) {
                __assert("pStreamInfo->iMaxSize > iStreamPartSize",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c", 0x5ed,
                         "ReserveSpaceForStreamPart", &_gp);
            }

            {
                int32_t v0_4 = var_48 - part_size;

                s7_1[0] = v0_4;
                s7_1[2] = v0_4;
                if ((v0_4 & 3) != 0) {
                    __assert("pStreamInfo->iStreamPartOffset % 4 == 0",
                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c", 0x5f2,
                             "ReserveSpaceForStreamPart", &_gp);
                }

                if (var_44 >= v0_4) {
                    __assert("pStreamInfo->iOffset < pStreamInfo->iStreamPartOffset",
                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c", 0x5f3,
                             "ReserveSpaceForStreamPart", &_gp);
                }
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
    int32_t v0 = StaticFifo_Dequeue((uint8_t *)arg1 + 0x1748);
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

    StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + READ_S32((void *)(intptr_t)v0, 0xa70) * 0x5c + 0x17a8), v0);
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
        StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + 0x18b0), v0_10);
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
    return (uint32_t)READ_U16(arg1, 0x2e);
}

int32_t endOfInput(void *arg1)
{
    if (READ_S32(arg1, 0x2c) == 0) {
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

int32_t AL_EncChannel_PushRefBuffer(void *arg1)
{
    return AL_RefMngr_PushBuffer((uint8_t *)arg1 + 0x22c8);
}

int32_t AL_EncChannel_PushStreamBuffer(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                       int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9)
{
    void *var_30 = &_gp;
    int32_t v0_1 = arg5 + arg6;
    int32_t var_28 = arg2;
    int32_t var_24 = arg3;
    int32_t var_20;
    int32_t var_1c = arg5;
    int32_t var_18 = arg7;
    int32_t var_14 = arg8;
    int32_t var_10 = arg9;
    int32_t a0_1;

    (void)var_30;
    if (arg4 < v0_1) {
        v0_1 = arg4;
    }

    var_20 = v0_1;
    if ((arg4 & 3) != 0) {
        return 0;
    }

    if ((uint32_t)READ_U8(arg1, 0x1f) == 4U) {
        a0_1 = READ_S32(arg1, 0x1aa4);
        if (a0_1 == 0) {
            a0_1 = READ_S32(arg1, 0x2a50);
        }
    } else {
        a0_1 = READ_S32(arg1, 0x2a50);
    }

    return AL_StreamMngr_AddBuffer((void *)(intptr_t)a0_1, &var_28);
}

int32_t AL_EncChannel_PushIntermBuffer(void *arg1, int32_t arg2, int32_t arg3)
{
    void *var_28 = &_gp;
    int32_t var_20 = arg2;
    int32_t var_1c = arg3;
    int32_t v0;
    int32_t s2_1;

    (void)var_28;
    v0 = AL_IntermMngr_GetEp1Location((uint8_t *)arg1 + 0x2a54, &var_20);
    s2_1 = READ_S32(arg1, 0x35c0);
    if (s2_1 == 0) {
        Rtos_Memset((void *)(intptr_t)v0, 0, AL_GetAllocSizeEP1());
    } else {
        Rtos_Memcpy((void *)(intptr_t)v0, (void *)(intptr_t)s2_1, AL_GetAllocSizeEP1());
    }

    return AL_IntermMngr_AddBuffer((uint8_t *)arg1 + 0x2a54, &var_20);
}

int32_t AL_EncChannel_PushNewFrame(void *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4)
{
    void *var_e8 = &_gp;
    int32_t *i;
    int32_t *s0_1;

    (void)var_e8;
    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    if (READ_S32(arg1, 0x2c) != 0) {
        return Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    }

    if (arg2 != 0 && arg3 != 0) {
        i = arg3;
    } else {
        i = arg3;
    }

    if (arg2 == 0 || arg3 == 0 || arg4 == 0) {
        s0_1 = 0;
    } else {
        int32_t var_e0 = arg4[0];
        int32_t var_dc_1 = arg4[1];
        int32_t var_d8_1 = arg4[2];
        int32_t var_d4_1 = arg4[3];
        int32_t var_d0_1 = arg4[4];
        int32_t var_cc_1 = arg4[5];
        int32_t var_c8_1 = arg4[6];
        int32_t var_c4_1 = arg4[7];
        int32_t var_c0_1 = arg4[8];
        int32_t var_b8_1 = arg2[0];
        int32_t var_b4_1 = arg2[1];
        int32_t var_b0_1 = arg2[2];
        int32_t var_ac_1 = arg2[3];
        int32_t var_a8_1 = arg2[4];
        int32_t var_a4_1 = arg2[5];
        int32_t var_a0_1 = arg2[6];
        int32_t var_9c_1 = arg2[7];
        uint8_t var_98[0x74];
        int32_t *v0_3 = (int32_t *)var_98;

        do {
            v0_3[0] = i[0];
            v0_3[1] = i[1];
            v0_3[2] = i[2];
            v0_3[3] = i[3];
            i += 4;
            v0_3 += 4;
        } while (i != &arg3[0x1c]);
        v0_3[0] = i[0];
        v0_3[1] = i[1];
        v0_3[2] = i[2];
        s0_1 = &var_e0;
        AddNewRequest((int32_t)(intptr_t)arg1);
    }

    AL_SrcReorder_AddSrcBuffer((uint8_t *)arg1 + 0x178, s0_1);
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

int32_t GetWPPOrSliceSizeOffset(void *arg1, void *arg2, int32_t arg3, int32_t arg4,
                                void *arg5); /* forward decl, ported by T<N> later */
int32_t GetSliceSizeOffset(void *arg1, void *arg2, int32_t arg3, int32_t arg4,
                           void *arg5, int32_t arg6, int32_t arg7); /* forward decl */
int32_t GetWPPOffset(void *arg1, void *arg2, int32_t arg3, int32_t arg4,
                     void *arg5, int32_t arg6, int32_t arg7); /* forward decl */
int32_t UpdateCommand(void *arg1, void *arg2, void *arg3, int32_t arg4); /* forward decl */
int32_t AL_GetCompLcuSize(uint32_t arg1, uint32_t arg2, int32_t arg3, int32_t arg4); /* forward decl */
int32_t AL_EncCore_Reset(void *arg1); /* forward decl */
void AL_EncCore_EnableInterrupts(void *arg1, int32_t arg2, int32_t arg3); /* forward decl */
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
void AL_ApplyPictCommands(void *arg1, void *arg2, int32_t arg3); /* forward decl */
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
    int32_t target_bits = ((arg3[1] + 7) >> 3) << 3;
    int32_t status = 0;
    int32_t max_bits = arg3[0x11] << 3;
    int32_t *rc = &arg1[0x3d];
    int32_t *frm = &arg2[8];

    if (arg4 == 0) {
        int32_t used_bits = arg3[0x11] << 3;
        int32_t produced = arg1[3] << 3;

        if (produced > target_bits) {
            target_bits = produced;
        }
        max_bits = target_bits;
        if (arg2[0xc] != 7) {
            Rtos_GetMutex((void *)(uintptr_t)arg1[0x48]);
            ((void (*)(void *, void *, void *, int32_t, int32_t *))(intptr_t)arg1[0x3f])(rc, frm, arg3, max_bits, &status);
            Rtos_ReleaseMutex((void *)(uintptr_t)arg1[0x48]);
        } else {
            Rtos_GetMutex((void *)(uintptr_t)arg1[0x48]);
            ((void (*)(void *, void *, void *, int32_t, int32_t *))(intptr_t)arg1[0x3f])(rc, frm, arg3, used_bits, &status);
            Rtos_ReleaseMutex((void *)(uintptr_t)arg1[0x48]);
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
    }

    if (status <= 0) {
        int32_t clamp = 8;

        if (status >= 8) {
            clamp = status;
        }
        arg2[0xb2] = clamp;
    }

    Rtos_GetMutex((void *)(uintptr_t)arg1[0x48]);
    WRITE_U8(arg1, 0x3de0, 0);
    ((void (*)(void *, void *, void *, int32_t, uint32_t, int32_t, void *))(intptr_t)arg1[0x40])(
        rc, frm, arg3, max_bits, (uint32_t)READ_U8(arg2, 0xb08), arg2[0xb2] << 3, &_gp);
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

        Rtos_GetMutex(READ_PTR(arg1, 0x168));
        ((void (*)(void *, uint32_t, void *, void *, int32_t))(intptr_t)READ_S32(arg1, 0x14c))
            ((uint8_t *)arg1 + 0x128, skip, &arg2[8], arg3, stats[0]);
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x168));
        Rtos_GetMutex(READ_PTR(arg1, 0x168));
        ((void (*)(void *, void *, int32_t))(intptr_t)READ_S32(arg1, 0x144))((uint8_t *)arg1 + 0x128, &arg2[8], 0);
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x168));
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

    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    if (StaticFifo_Empty((uint8_t *)arg1 + 0x162c) == 0) {
        entry = (int32_t *)(intptr_t)StaticFifo_Dequeue((uint8_t *)arg1 + 0x162c);
        memset(tmp, 0, sizeof(tmp));
        memcpy(tmp, entry + 8, 0xe0);
        memset(tmp2, 0, sizeof(tmp2));
        memcpy(tmp2, tmp, 0xe0);
        memcpy(arg2, tmp2, 0xe0);
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
        return 1;
    }

    if (READ_U8(arg1, 0x2c) != 0U && READ_S32(arg1, 0x35b0) == READ_S32(arg1, 0x35b4) && READ_U8(arg1, 0x2d) == 0U) {
        WRITE_U8(arg1, 0x2d, 1);
        AL_RefMngr_Flush((uint8_t *)arg1 + 0x22c8);
        memset(end_evt, 0, sizeof(end_evt));
        WRITE_S32(end_evt, 0, READ_S32(arg1, 0x44));
        WRITE_S32(end_evt, 4, READ_S32(arg1, 0x48));
        WRITE_U32(end_evt, 8, READ_U8(arg1, 0x3c));
        WRITE_U8(end_evt, 0x100 - 0xf0, 1);
        memcpy(arg2, end_evt, 0xe0);
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
        return 1;
    }

    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
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
        StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + 0x162c), (int32_t)(intptr_t)evt);
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

    if ((flags & 0xd) == 8) {
        return 0x90;
    }
    if ((ch_flags & 1) != 0 && READ_S32(arg1, 0xc4) != 0) {
        return 0x90;
    }
    if (((flags >> 4) & 0xd) == 8) {
        return 0x90;
    }
    if (((uint32_t)flags >> 8 & 0xfU) < 4U) {
        return 0x90;
    }
    if (lcu < 7U) {
        return 0x90;
    }
    if (lcu < READ_U8(arg1, 0x4f) || READ_U8(arg1, 0x4f) < 3U) {
        return 0x90;
    }
    if (codec == 4U && READ_U16(arg1, 4) < 0x4001U && READ_S32(arg1, 0xc4) == 0) {
        return 0x90;
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

int32_t AL_EncChannel_Init(int32_t *arg1, int32_t *arg2, void *arg3, uint8_t arg4, uint8_t arg5, uint8_t arg6,
                           void *arg7, void *arg8, int32_t *arg9, int32_t arg10, int32_t arg11, uint8_t arg12)
{
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

    if ((READ_U16(arg2, 0x76) != 0U) && (READ_U16(arg2, 0x74) != 0U) && (READ_U8(arg2, 0x78) != 0U) &&
        READ_U8(arg2, 0x1f) != 0U) {
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
        result = AL_RateCtrl_Init(&arg1[0x3d], arg7, rc_mode, 0);
        if (result == 0) {
            AL_IntermMngr_Deinit(&arg1[0xa95]);
            AL_RefMngr_Deinit(&arg1[0x8b2]);
            AL_StreamMngr_Deinit((void *)(intptr_t)arg1[0xa94]);
            AL_SrcReorder_Deinit(&arg1[0x5e]);
            AL_GopMngr_Deinit(&arg1[0x4a]);
            return 0;
        }
        ((void (*)(void *, uint32_t, uint32_t))(intptr_t)arg1[0x3d])(&arg1[0x3d], width, height);
        Rtos_GetMutex(READ_PTR(arg1, 0x120));
        ((void (*)(void *, void *, void *))(intptr_t)arg1[0x3e])(&arg1[0x3d], &arg2[0x1a], &arg2[0x2a]);
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x120));
    }

    WRITE_S32(arg1, 0x164, (int32_t)(intptr_t)arg3);
    ResetChannelParam(arg1);
    SetTileOffsets(arg1);
    WRITE_U16(arg1, 0x2f, 0);
    WRITE_U8(arg1, 0x3c, arg5);
    WRITE_U8(arg1, 0x3d, arg4);
    WRITE_U8(arg1, 0x2c, arg6);
    WRITE_U8(arg1, 0x3f, READ_U8(arg2, 0xf));
    WRITE_U8(arg1, 0x2e, arg12);
    WRITE_U8(arg1, 0x44, READ_U8(arg2, 0));
    WRITE_S32(arg1, 0x48, arg9[0]);
    WRITE_S32(arg1, 0x4c, arg9[1]);
    WRITE_S32(arg1, 0x50, arg9[2]);
    WRITE_S32(arg1, 0x54, arg9[3]);
    WRITE_U8(arg1, 0x58, 0);
    WRITE_U8(arg1, 0x59, 0);
    InitMERange((int32_t)(intptr_t)arg1, arg2);
    WRITE_S32(arg1, 0x35cc, 0);
    arg1[0xd71] = 0;
    arg1[0xd72] = 0;
    arg1[0xd73] = 0;
    return result;
}

int32_t AL_EncChannel_DeInit(void *arg1)
{
    int32_t *alloc = (int32_t *)READ_PTR(arg1, 0x35ac);
    uint32_t core_count = READ_U8(arg1, 0x3c);
    int32_t i;
    int32_t cb = READ_S32(arg1, 0x58);

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
    StaticFifoCompat *fifo;

    if (READ_U16(arg1, 0x2f) != 0U) {
        return (int32_t)(intptr_t)arg1;
    }

    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    fifo = (StaticFifoCompat *)((uint8_t *)arg1 + 0x15d0);
    for (lane = 0; lane != 2; ++lane) {
        int32_t *req = (int32_t *)(intptr_t)StaticFifo_Front(fifo);

        if (req != 0) {
            if (READ_U8(req, 0xa74) != 0U) {
                int32_t *grp = (int32_t *)((uint8_t *)req + lane * 0x110 + 0x84c);
                int32_t i;

                for (i = 0; i < READ_S32(req, lane * 0x110 + 0x8cc); ++i) {
                    AL_ModuleArray_AddModule(arg2, READ_U8(arg1, 0x3d) + grp[0], grp[1]);
                    grp += 2;
                }
            } else {
                int32_t pict_id = READ_S32(req, 0x20);

                if (READ_U8(req, 0xa75) == 0U) {
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

                        if (ready != 0 && (READ_S32(ready, 0) & 0x200) != 0) {
                            WRITE_U16(arg1, 4, (uint16_t)READ_S32(ready, 0x68));
                            WRITE_U16(arg1, 6, (uint16_t)READ_S32(ready, 0x6c));
                            WRITE_U8(arg1, 0x44, READ_U8(ready, 0x70));
                            AL_EncChannel_SetNumberOfCores(arg1);
                            SetChannelSteps(arg1, arg1);
                            if ((uint32_t)AL_EncChannel_CheckAndAdjustParam(arg1) >= 0x80U) {
                                __assert("!AL_IS_ERROR_CODE(eErrorCode)",
                                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChannelMngr.c",
                                         0xb62, "SetInputResolution", &_gp);
                            }
                            SetTileOffsets(arg1);
                            InitMERange((int32_t)(intptr_t)arg1, arg1);
                            AL_RefMngr_SetRecResolution((uint8_t *)arg1 + 0x22c8, READ_U16(arg1, 4), READ_U16(arg1, 6));
                            ((void (*)(void *, uint32_t, uint32_t))(intptr_t)READ_S32(arg1, 0xf4))
                                ((uint8_t *)arg1 + 0xf4, READ_U16(arg1, 4), READ_U16(arg1, 6));
                            InitHwRC_Content(arg1, arg1);
                        }
                    }
                    WRITE_U8(arg1, 0x2f, 0);
                    AL_SrcReorder_MarkSrcBufferAsUsed((uint8_t *)arg1 + 0x178, pict_id);
                    if (READ_S32(req, 0x30) == 2 && READ_U8(arg1, 0x2d4) != 0U) {
                        Rtos_GetMutex(READ_PTR(arg1, 0x170));
                        ((void (*)(void *))(intptr_t)READ_S32(arg1, 0x158))((uint8_t *)arg1 + 0x128);
                        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                    }
                    AL_ApplyPictCommands(arg1, AL_SrcReorder_GetReadyCommand((uint8_t *)arg1 + 0x178, pict_id), pict_id);
                    Rtos_GetMutex(READ_PTR(arg1, 0x170));
                    ((void (*)(void *, void *))(intptr_t)READ_S32(arg1, 0x138))((uint8_t *)arg1 + 0x128,
                                                                                 (uint8_t *)req + 0x20);
                    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                    if (READ_U8(arg1, 0x1f) != 4U) {
                        SetPictureReferences(arg1, req);
                    }
                    WRITE_U8(req, 0xa75, 1);
                }
            }
        }
        fifo = (StaticFifoCompat *)((uint8_t *)fifo + 0x5c);
    }

    Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
    return 0;
}

int32_t encode1(void *arg1)
{
    int32_t *ch = arg1;
    int32_t *req;
    int32_t core;
    uint32_t core_offset = READ_U8(ch, 0x3d);

    Rtos_GetMutex(READ_PTR(ch, 0x170));
    req = (int32_t *)(intptr_t)StaticFifo_Dequeue((uint8_t *)ch + 0x15d0);
    for (core = 0; core < READ_S32(req, 0x958); ++core) {
        WRITE_S32(req, 0x8d8 + core * 8, READ_S32(req, 0x8d8 + core * 8) + core_offset);
    }
    for (core = 0; core < READ_S32(req, 0x8cc); ++core) {
        WRITE_S32(req, 0x84c + core * 8, READ_S32(req, 0x84c + core * 8) + core_offset);
    }
    StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)ch + 0x15fb + READ_S32(req, 0xa70) * 0x5c + 0xc),
                     (int32_t)(intptr_t)req);
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
    FillSliceParamFromPicParam(ch, (uint8_t *)req + 0x170, req);
    UpdateCommand(ch, req, (uint8_t *)req + 0x170, 0);
    handleInputTraces(ch, req, (uint8_t *)req + 0x170, 0);
    AL_EncCore_TurnOnRAM(READ_PTR(ch, 0x164), READ_U8(ch, 0x1f), READ_U8(ch, 0x3c), 0, 0);
    AL_EncCore_EnableInterrupts(READ_PTR(ch, 0x164), READ_U8(ch, 0x3c), 0);
    for (core = 0; core < (int32_t)READ_U8(ch, 0x3c); ++core) {
        void *enc1 = (uint8_t *)READ_PTR(ch, 0x164) + core * 0x44;
        int32_t cmd1 = READ_S32(req, 0xa78 + core);
        int32_t cmd2 = READ_S32(req, 0xab8 + core);

        AL_EncCore_Encode1(enc1, cmd2, cmd1, (READ_U8(req, 0x182) != 0U) ? cmd2 : 0,
                           (READ_U8(req, 0x182) != 0U) ? cmd1 : 0);
    }
    return Rtos_ReleaseMutex(READ_PTR(ch, 0x170));
}

int32_t AL_EncChannel_Encode(void *arg1, void *arg2)
{
    int32_t (*workers[2])(void *) = { encode1, encode2 };
    StaticFifoCompat *running = (StaticFifoCompat *)((uint8_t *)arg1 + 0x15d0);
    int lane;
    int32_t result = 0;

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
                    result = workers[lane](arg1);
                }
            }
            running = (StaticFifoCompat *)((uint8_t *)running + 0x5c);
        }
        return result;
    }

    Rtos_GetMutex(READ_PTR(arg1, 0x170));
    {
        int32_t *req = (int32_t *)(intptr_t)StaticFifo_Dequeue((uint8_t *)arg1 + 0x15d0);
        int i;

        for (i = 0; i < READ_S32(req, 0x958); ++i) {
            WRITE_S32(req, 0x8d8 + i * 8, READ_S32(req, 0x8d8 + i * 8) + READ_U8(arg1, 0x3d));
        }
        for (i = 0; i < READ_S32(req, 0x8cc); ++i) {
            WRITE_S32(req, 0x84c + i * 8, READ_S32(req, 0x84c + i * 8) + READ_U8(arg1, 0x3d));
        }
        StaticFifo_Queue((StaticFifoCompat *)((uint8_t *)arg1 + 0x15fe), (int32_t)(intptr_t)req);
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
    if (READ_U8(arg1, 0x1f) == 4U) {
        EndJpegEncoding(arg1);
        return 1;
    }

    if (READ_U8(arg1, 0xc4) != 0U) {
        Rtos_GetMutex(READ_PTR(arg1, 0x170));
        {
            void *fifo = getFifoRunning(arg1, arg3);

            if (fifo == 0) {
                Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
                return 0;
            }

            {
                int32_t *req = (int32_t *)(intptr_t)StaticFifo_Front(fifo);
                int32_t *core = (int32_t *)((uint8_t *)req + arg2);
                uint32_t done = READ_U8(core, 0x84a) + 1U;

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
                }
            }
        }
        Rtos_ReleaseMutex(READ_PTR(arg1, 0x170));
        return 0;
    }

    return 1;
}

int32_t AL_EncChannel_SetTraceCallBack(void *arg1, int32_t arg2, int32_t arg3)
{
    WRITE_S32(arg1, 0x4c, arg2);
    WRITE_S32(arg1, 0x54, arg3);
    return 0;
}
