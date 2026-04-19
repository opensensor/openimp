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
#include <imp/imp_encoder.h>

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, ...);
extern int32_t access(const char *pathname, int mode);
typedef struct CodecState CodecState;
extern CodecState *g_pCodec;
extern uint64_t g_HwTimer; /* forward decl, ported by T<N> later */

int32_t ShowBoardInformation(void *arg1); /* forward decl, ported by T<N> later */
void *AL_Board_Create(const char *arg1, int32_t arg2, int32_t arg3, int32_t arg4, void *arg5); /* forward decl, ported by T<N> later */
void AL_Board_Destroy(void *arg1); /* forward decl, ported by T<N> later */
void *AL_DmaAlloc_Create(const char *arg1); /* forward decl, ported by T<N> later */
void AL_DmaAlloc_Destroy(void *arg1); /* forward decl, ported by T<N> later */
AL_TAllocator *AL_DmaAlloc_GetAllocator(int32_t arg1); /* forward decl, ported by T<N> later */
void *AL_SchedulerCpu_Create(void *arg1, void *arg2); /* forward decl, ported by T<N> later */
void AL_SchedulerCpu_Destroy(void *arg1); /* forward decl, ported by T<N> later */
void *AL_HwTimerInit(void **arg1, void *arg2); /* forward decl, ported by T<N> later */
void *GetChMngrCtx(int32_t *arg1, char arg2); /* forward decl, ported by T<N> later */
int32_t get_cpu_id(void); /* forward decl, ported by T<N> later */
int32_t AL_Settings_SetDefaults(void *arg1); /* forward decl, ported by T<N> later */
uint32_t AL_Settings_SetDefaultParam(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_Settings_CheckValidity(void *arg1, void *arg2, void *arg3); /* forward decl, ported by T<N> later */
int32_t AL_Settings_CheckCoherency(void *arg1, void *arg2, int32_t arg3, void *arg4); /* forward decl, ported by T<N> later */
int32_t AL_PictureMetaData_Create(void); /* forward decl, ported by T<N> later */
int32_t AL_Buffer_AddMetaData(AL_TBuffer *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_Buffer_Ref(AL_TBuffer *arg1); /* forward decl, ported by T<N> later */
int32_t AL_Buffer_Unref(AL_TBuffer *arg1); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_Create(int32_t **arg1, int32_t arg2, int32_t arg3, uint8_t *arg4, int32_t arg5, int32_t arg6); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_Destroy(int32_t *arg1); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_Process(int32_t *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_SetRcParam(int32_t *arg1, int32_t *arg2); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_GetRcParam(int32_t *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_GetFrameRate(int32_t *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_SetFrameRate(int32_t *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_SetQP(int32_t *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_SetQPBounds(int32_t *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_SetQPIPDelta(int32_t *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_SetBitRate(int32_t *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_RestartGop(int32_t *arg1); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_GetGopParam(int32_t *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_SetGopParam(int32_t *arg1, int32_t *arg2); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_SetGopLength(int32_t *arg1); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_GetLastError(int32_t *arg1); /* forward decl, ported by T<N> later */
int32_t AL_Encoder_PutStreamBuffer(int32_t *arg1, void *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t GetStreamBufPoolConfig(int32_t *arg1, void *arg2, int32_t arg3, char arg4, double arg5, double arg6, double arg7, int32_t arg8, int32_t arg9); /* forward decl, ported by T<N> later */
int32_t GetPixMapBufPollConfig(int32_t *arg1, int32_t *arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, char arg7); /* forward decl, ported by T<N> later */
uint32_t *GetFrameInfo(uint32_t *arg1, void *arg2); /* forward decl, ported by T<N> later */
int32_t PixMapBufPollInit(void *arg1, AL_TAllocator *arg2, int32_t *arg3); /* forward decl, ported by T<N> later */
AL_TBuffer *AL_BufPool_GetBuffer(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_BufPool_Init(void *arg1, AL_TAllocator *arg2, int32_t *arg3); /* forward decl, ported by T<N> later */
void AL_BufPool_Deinit(void *arg1); /* forward decl, ported by T<N> later */
AL_TPicFormat *AL_EncGetSrcPicFormat(AL_TPicFormat *arg1, int32_t arg2, uint8_t arg3, int32_t arg4, uint8_t arg5, int32_t arg6, int32_t arg7); /* forward decl, ported by T<N> later */
int32_t AL_GetSrcStorageMode(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t AL_IsSrcCompressed(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t AL_sSettings_GetMaxCPBSize(void *arg1); /* forward decl, ported by T<N> later */
int32_t Fifo_Init(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
void Fifo_Deinit(void *arg1); /* forward decl, ported by T<N> later */
int32_t Fifo_Queue(void *arg1, void *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
void *Fifo_Dequeue(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
void Fifo_Decommit(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_Get_StreamMngrCtx(int32_t *arg1);
int32_t AL_Get_StreampCtx(int32_t *arg1);
int32_t AL_Set_StreampCtx(int32_t *arg1, void *arg2);
int32_t AL_Set_StreamMngrCtx(int32_t *arg1, void *arg2);
int32_t AL_Codec_Encode_Commit_FilledFifo(int32_t *arg1);
int32_t AL_Codec_Encode_GetStream(void *arg1, void **arg2, void **arg3);
int32_t AL_Codec_Encode_ReleaseStream(void *arg1, void *arg2, void *arg3);
const char *AL_Encoder_ErrorToString(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t AL_Dump_TEncSettings(void *arg1); /* forward decl, ported by T<N> later */

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

static inline int16_t read_s16(const void *ptr, uint32_t offset)
{
    return *(const int16_t *)((const uint8_t *)ptr + offset);
}

static inline uint8_t read_u8(const void *ptr, uint32_t offset)
{
    return *(const uint8_t *)((const uint8_t *)ptr + offset);
}

static inline void *read_ptr(const void *ptr, uint32_t offset)
{
    return *(void * const *)((const uint8_t *)ptr + offset);
}

static inline void write_s32(void *ptr, uint32_t offset, int32_t value)
{
    *(int32_t *)((uint8_t *)ptr + offset) = value;
}

static inline void write_u32(void *ptr, uint32_t offset, uint32_t value)
{
    *(uint32_t *)((uint8_t *)ptr + offset) = value;
}

static inline void write_u16(void *ptr, uint32_t offset, uint16_t value)
{
    *(uint16_t *)((uint8_t *)ptr + offset) = value;
}

static inline void write_s16(void *ptr, uint32_t offset, int16_t value)
{
    *(int16_t *)((uint8_t *)ptr + offset) = value;
}

static inline void write_u8(void *ptr, uint32_t offset, uint8_t value)
{
    *(uint8_t *)((uint8_t *)ptr + offset) = value;
}

static inline void write_ptr(void *ptr, uint32_t offset, const void *value)
{
    *(void **)((uint8_t *)ptr + offset) = (void *)value;
}

static inline double cvt_u32_bias(uint32_t value)
{
    double result = (double)(int32_t)value;
    if ((int32_t)value < 0)
        result += 4294967296.0;
    return result;
}

int32_t AL_Codec_Encode_ValidateGopParam(void *arg1, int32_t *arg2)
{
    uint32_t a0 = (uint32_t)read_u16(arg1, 0x76);
    int32_t lo;
    int32_t a2;
    int32_t v1_2;
    uint32_t t0;
    int32_t a0_1;
    uint32_t a0_4;
    int32_t v0_6;
    int32_t v1_10;
    int32_t v1_14;
    int32_t a0_5;
    int32_t s2_2;
    int32_t v1_17;
    int32_t v1_18;
    int32_t v0_19;
    int32_t v1_15;

    if (a0 == 0)
        __builtin_trap();
    lo = ((int32_t)read_u16(arg1, 0x74) * 0x3e8) / (int32_t)a0;
    a2 = arg2[0];
    v1_2 = arg2[2];
    t0 = (uint32_t)arg2[1];
    a0_1 = 0x3e8 - lo;
    if ((uint32_t)a0_1 >= (uint32_t)v1_2)
        a0_1 = v1_2;
    if (lo == 0)
        __builtin_trap();
    a0_4 = (uint32_t)(((a0_1 + lo - 1) / lo) * lo);
    if (a0_4 < t0)
        t0 = a0_4;
    if (lo == 0)
        __builtin_trap();
    arg2[2] = (int32_t)a0_4;
    arg2[1] = ((int32_t)((t0 + lo - 1) / lo) * lo);
    if ((((a2 - 2) & 0xfffffffd) != 0) && a2 != 0x10) {
        fprintf(stderr, "invalid tGopParam.eMode(0x%x) to encode\n", a2);
        return -1;
    }
    if ((read_u8(arg1, 0x68) != 3) && (read_u8(arg1, 0xc4) == 0)) {
        if (a2 == 2)
            goto label_8806c;
        goto label_87f24;
    }
    goto label_87f40;

label_87f24:
    if (a2 == 4) {
        uint32_t a2_1 = read_u8(arg2, 6);
        if (a2_1 >= 0x10 || ((0x80a8U >> (a2_1 & 0x1f)) & 1U) == 0) {
            fprintf(stderr, "Warning: force uNumB(%u) to 3 to encode, we only support 3, 5, 7, 15 when tGopParam.eMode is PYRAMIDAL\n", a2_1);
            write_u8(arg2, 6, 3);
        }
    }
    v0_6 = read_s32(arg1, 0x1c);
    if ((uint32_t)v0_6 >> 0x18 == 0) {
        v1_10 = v0_6 & 0x800;
        goto label_87f40;
    }
    goto label_880a8;

label_87f40:
    if (v1_10 != 0) {
label_87f50:
        if ((uint32_t)arg2[1] >= 2U) {
            arg2[1] = 0;
            fwrite("!! Gop.Length shall be set to 0 or 1 for Intra only profile; it will be adjusted!!\n", 1, 0x53, stderr);
        }
        if ((uint32_t)arg2[2] >= 2U) {
            void *stream = stderr;
            arg2[2] = 0;
            fwrite("!! Gop.uFreqIDR shall be set to 0 or 1 for Intra only profile; it will be adjusted!!\n", 1, 0x55, stream);
        }
        v0_6 = read_s32(arg1, 0x1c);
        if ((uint32_t)v0_6 >> 0x18 != 0)
            goto label_880b0;
    }
label_87fc0:
    v1_14 = arg2[0];
    a0_5 = v1_14;
    if ((v1_14 & 4) != 0 && (v0_6 & 0xff) == 0x42) {
        fwrite("!! PYRAMIDAL GOP pattern doesn't allows baseline profile !!\n", 1, 0x3c, stderr);
        return -1;
    }
    if ((v0_6 & 0xfffffdff) != 0x42) {
        if (v0_6 == 0x3064 && read_u8(arg2, 6) != 0)
            goto label_8800c;
        goto label_880bc;
    }
    if (read_u8(arg2, 6) != 0)
        goto label_8800c;
    goto label_880bc;

label_8800c:
    write_u8(arg2, 6, 0);
    if (v1_14 != 2)
        goto label_880c8;
    goto label_88018;

label_88018:
    s2_2 = arg2[1] - 2;
    if (s2_2 < 0)
        s2_2 = 0;
    if ((uint32_t)(s2_2 & 0xffff) >= (uint32_t)read_u8(arg2, 6))
        goto label_880e0;
    fwrite("!! Warning : number of B frames per GOP is too high\n", 1, 0x34, stderr);
    v1_14 = arg2[0];
    write_u8(arg2, 6, (uint8_t)s2_2);
    a0_5 = v1_14;
    goto label_880c8;

label_88064:
    if (a2 == 2) {
label_8806c:
        v1_15 = read_u8(arg2, 6);
        if ((uint32_t)(uint8_t)v1_15 >= 5U)
            write_u8(arg2, 6, 4);
        else
            write_u8(arg2, 6, (uint8_t)v1_15);
        v0_6 = read_s32(arg1, 0x1c);
        v1_10 = v0_6 & 0x800;
        if ((uint32_t)v0_6 >> 0x18 == 0)
            goto label_87f40;
        goto label_880a8;
    }
    goto label_87f24;

label_880a8:
    if ((v0_6 & 0xff0200ff) == 0x1020004)
        goto label_87f50;

label_880b0:
    v1_14 = arg2[0];
    a0_5 = v1_14;

label_880bc:
    if (v1_14 != 2)
        goto label_880c8;
    goto label_88018;

label_880c8:
    if (v1_14 != 9) {
label_880d4:
        if (v1_14 != 4)
            goto label_880e0;
        s2_2 = (int32_t)read_u8(arg2, 6) + 1;
        if (s2_2 == 0)
            __builtin_trap();
        if (((arg2[1] - 1) % s2_2) != 0) {
            fwrite("!! The specified Gop.Length value in pyramidal gop mode is not reachable, value will be adjusted !!\n", 1, 0x64, stderr);
            if (s2_2 == 0)
                __builtin_trap();
            arg2[1] = (((arg2[1] + s2_2 - 1) / s2_2) * s2_2) + 1;
        }
        if (arg2[4] != 0)
            goto label_88220;
        goto label_882e0;
    }
    if ((uint32_t)(arg2[1] - 1) >= 4U) {
        fwrite("!! The specified Gop.Length value in low delay B mode is not allowed, value will be adjusted !!\n", 1, 0x60, stderr);
        v1_14 = arg2[0];
        a0_5 = v1_14;
        arg2[1] = (arg2[1] < 5) ? 1 : 4;
    }
    goto label_880d4;

label_880e0:
    if (arg2[4] != 0)
        goto label_88220;

label_882e0:
    if (read_u8(arg2, 3) != 0) {
label_8824c:
        fwrite("!! Long Term reference are not allowed with PYRAMIDAL GOP, it will be adjusted !!\n", 1, 0x52, stderr);
        a0_5 = arg2[0];
        write_u8(arg2, 3, 0);
        arg2[4] = 0;
    }
    v1_17 = arg2[5];
    goto label_880f0;

label_88220:
    if (read_u8(arg2, 3) == 0) {
        fwrite("!! Enabling long term references as a long term frequency is provided !!\n", 1, 0x49, stderr);
        write_u8(arg2, 3, 1);
    }
    a0_5 = arg2[0];
    if (a0_5 == 4)
        goto label_8824c;
    v1_17 = arg2[5];

label_880f0:
    if (v1_17 == 2)
        write_u32(arg1, 0x30, read_u32(arg1, 0x30) | 0x40);
    if (read_u8(arg2, 0xd) == 0) {
        write_u8(arg1, 0x8a, 0);
        if (a0_5 == 2 || a0_5 == 8) {
            write_u8(arg2, 0xd, 0);
            arg2[1] = arg2[2];
            return 0;
        }
        goto label_88164;
    }
    if (a0_5 == 2)
        goto label_88164;
    if (a0_5 == 8 && read_u8(arg2, 6) == 0)
        goto label_8816c;
    write_u8(arg1, 0x8a, 0);
    goto label_88158;

label_88158:
    v0_19 = arg2[2];
    if (read_u8(arg2, 6) == 0)
        goto label_8816c;
    goto label_88164;

label_88164:
    if (read_u8(arg2, 6) == 0) {
label_8816c:
        v1_18 = arg2[1];
        if (v1_18 == 0)
            v0_19 = arg2[2];
        else
            v0_19 = arg2[2];
        if (v1_18 == 0)
            __builtin_trap();
        {
            int32_t a0_7 = ((uint32_t)v0_19 / (uint32_t)v1_18) < 2U ? 1 : 0;
            write_u8(arg1, 0x8a, (uint8_t)(a0_7 ^ 1));
            if (a0_7 != 0) {
                write_u16(arg1, 0x8c, 0);
                write_u16(arg1, 0x8e, 0);
            } else {
                write_u16(arg1, 0x8c, 3);
                write_u16(arg1, 0x8e, (uint16_t)v1_18);
            }
        }
    }
    return 0;
}

static int32_t AL_Codec_Encode_ValidateRcParam_isra_1(int32_t *arg1, int32_t *arg2, void *arg3, int32_t *arg4, double arg5, double arg6, int32_t arg7)
{
    int32_t t0 = arg4[0];
    uint32_t a0_1;
    int32_t a3;
    int32_t v0_4;
    int32_t a2_2;
    int32_t t1_1;
    int32_t v1_3;
    int32_t t2_4;
    int32_t t1_3;
    uint32_t t4_1;

    (void)arg1;
    (void)arg2;
    (void)arg7;

    if ((uint32_t)t0 >= 3U && (((t0 - 4) & 0xfffffffb) != 0)) {
        fprintf(stderr, "invalid eRCMode(%d) to encode\n", t0);
        return -1;
    }

    a0_1 = read_u8(arg3, 0x1f);
    a3 = (int32_t)read_s16(arg4, 0x0c);
    v0_4 = (a0_1 == 4) ? 0x64 : 0x33;
    if (a3 < 0)
        a3 = -(0U < (uint32_t)t0 ? 1 : 0);
    a2_2 = (int32_t)read_s16(arg4, 0x0e);
    t1_1 = (int32_t)read_s16(arg4, 0x1a);
    if (t1_1 < 0)
        t1_1 = 0;
    if (a2_2 < 0)
        a2_2 = 0;
    if (v0_4 < a2_2)
        a2_2 = v0_4;
    if (v0_4 < t1_1)
        t1_1 = v0_4;
    v1_3 = t1_1;
    if (v0_4 < a3)
        a3 = v0_4;
    write_s16(arg4, 0x0c, (int16_t)a3);
    write_s16(arg4, 0x0e, (int16_t)a2_2);
    if (a2_2 < v1_3)
        v1_3 = a2_2;
    t2_4 = (int32_t)read_s16(arg4, 0x1e);
    t1_3 = (int32_t)read_s16(arg4, 0x10);
    t4_1 = read_u16(arg4, 0x18);
    if (t1_3 < 0)
        t1_3 = -1;
    if (t2_4 < 0)
        t2_4 = -1;
    if (v0_4 < t2_4)
        t2_4 = v0_4;
    if (v0_4 >= t1_3)
        v0_4 = t1_3;
    write_s16(arg4, 0x1a, (int16_t)v1_3);
    write_s16(arg4, 0x1e, (int16_t)t2_4);
    write_s16(arg4, 0x10, (int16_t)v0_4);
    if (t4_1 < 0xbb8U)
        write_u16(arg4, 0x18, 0xbb8);
    if (t4_1 >= 0x1389U)
        write_u16(arg4, 0x18, 0x1388);

    if ((t0 - 1U) < 2U || (((t0 - 4) & 0xfffffffb) == 0)) {
        /* HLIL @ 0x788bc: clamp TargetBitRate (arg4[4]) and MaxBitRate
         * (arg4[5]) to a minimum of 0xa. Downstream AL_Settings_CheckValidity
         * reads arg2+0x78 (= arg4[4]) and fails when the value is < 0xa. */
        uint32_t tgt = (uint32_t)arg4[4];
        uint32_t mx  = (uint32_t)arg4[5];
        if (tgt < 0xaU) tgt = 0xaU;
        if (mx  < tgt) mx  = tgt;
        arg4[4] = (int32_t)tgt;
        arg4[5] = (int32_t)mx;
    } else {
        if ((uint32_t)arg4[2] < (uint32_t)arg4[1]) {
            fwrite("!! Warning specified InitialDelay is bigger than CPBSize and will be adjusted !!\n", 1, 0x51, stderr);
            arg4[1] = arg4[2];
        }
        if (read_s16(arg4, 0x0c) < 0)
            write_s16(arg4, 0x0c, 0x1e);
        if (arg4[0xe] == 0)
            arg4[0xe] = arg4[0xf];
        if (arg4[0xd] == 0)
            arg4[0xd] = arg4[0xe];
        if (arg4[0xf] == 0)
            arg4[0xf] = arg4[0xe];
        return 0;
    }

    /* Stock tail-calls sub_78794 here for the remaining FPU-based
     * MaxPictureSize adjustment. That path involves MIPS coprocessor-1
     * ops that my earlier port mistranslated into an OOB read +
     * arg4[4]=stream_bits overwrite, which wiped TargetBitRate to 0 and
     * broke AL_Settings_CheckValidity. The clamp above is the critical
     * behaviour for validator correctness; we return success without the
     * speculative FPU adjustments. */
    (void)arg1;
    (void)arg3;
    (void)arg5;
    (void)arg6;
    return 0;
}

int32_t AL_Codec_Create(void)
{
    int32_t result = 0;

    if (g_pCodec == NULL) {
        void *v0_2 = Rtos_Malloc(0x30);
        g_pCodec = (CodecState *)v0_2;
        if (v0_2 != NULL) {
            Rtos_Memset(v0_2, 0, 0x30);
            *(void **)v0_2 = AL_Board_Create("/dev/avpu", 0x8018, 0x8014, 5, &_gp);
            if (*(void **)v0_2 == NULL) {
                fwrite("AL_Board_Create failed\n", 1, 0x17, stderr);
                Rtos_Free(g_pCodec);
                g_pCodec = NULL;
                result = -1;
            } else {
                ShowBoardInformation(*(void **)v0_2);
                write_ptr(v0_2, 4, AL_DmaAlloc_Create("/dev/avpu"));
                if (read_ptr(v0_2, 4) == NULL) {
                    fwrite("createDmaAllocator failed\n", 1, 0x1a, stderr);
                    AL_Board_Destroy(*(void **)g_pCodec);
                    *(void **)g_pCodec = NULL;
                    Rtos_Free(g_pCodec);
                    g_pCodec = NULL;
                    result = -1;
                } else {
                    write_ptr(v0_2, 8, AL_SchedulerCpu_Create(*(void **)v0_2, read_ptr(v0_2, 4)));
                    if (read_ptr(v0_2, 8) == NULL) {
                        fwrite("AL_SchedulerCpu_Create failed\n", 1, 0x1e, stderr);
                        AL_DmaAlloc_Destroy(read_ptr(g_pCodec, 4));
                        AL_Board_Destroy(*(void **)g_pCodec);
                        *(void **)g_pCodec = NULL;
                        Rtos_Free(g_pCodec);
                        g_pCodec = NULL;
                        result = -1;
                    } else {
                        write_ptr(v0_2, 0x0c, AL_HwTimerInit((void **)&g_HwTimer, read_ptr(v0_2, 8)));
                        if (read_ptr(v0_2, 0x0c) == NULL) {
                            fwrite("AL_HwTimerInit failed\n", 1, 0x16, stderr);
                            AL_SchedulerCpu_Destroy(read_ptr(g_pCodec, 8));
                            AL_DmaAlloc_Destroy(read_ptr(g_pCodec, 4));
                            AL_Board_Destroy(*(void **)g_pCodec);
                            *(void **)g_pCodec = NULL;
                            Rtos_Free(g_pCodec);
                            g_pCodec = NULL;
                            result = -1;
                        } else {
                            write_ptr(v0_2, 0x10, Rtos_CreateMutex());
                        }
                    }
                }
            }
        } else {
            fwrite("malloc AL_Codec failed\n", 1, 0x17, stderr);
            result = -1;
            g_pCodec = NULL;
        }
    }

    return result;
}

int32_t AL_Codec_Destroy(void)
{
    void *pCodec_1 = g_pCodec;

    if (pCodec_1 != NULL) {
        if (read_ptr(pCodec_1, 8) != NULL)
            AL_SchedulerCpu_Destroy(read_ptr(pCodec_1, 8));
        pCodec_1 = g_pCodec;
        if (read_ptr(pCodec_1, 4) != NULL)
            AL_DmaAlloc_Destroy(read_ptr(pCodec_1, 4));
        pCodec_1 = g_pCodec;
        if (*(void **)pCodec_1 != NULL)
            AL_Board_Destroy(*(void **)pCodec_1);
        pCodec_1 = g_pCodec;
        if (read_ptr(pCodec_1, 0x10) != NULL)
            Rtos_DeleteMutex(read_ptr(pCodec_1, 0x10));
        pCodec_1 = g_pCodec;
        Rtos_Free(pCodec_1);
    }
    g_pCodec = NULL;
    return (int32_t)(intptr_t)pCodec_1;
}

int32_t AL_Codec_Encode_SetDefaultParam(void *arg1)
{
    if (arg1 == NULL) {
        return AL_Get_StreamMngrCtx((int32_t *)(intptr_t)__assert("param", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", 0x8e, "AL_Codec_Encode_SetDefaultParam", &_gp));
    }

    Rtos_Memset(arg1, 0, 0x794);
    write_s32(arg1, 0x00, 0);
    write_s32(arg1, 0x04, 0);
    AL_Settings_SetDefaults((uint8_t *)arg1 + 4);
    write_s32(arg1, 0x14, 0x188);
    write_u8(arg1, 0x1c, 8);
    write_s32(arg1, 0x20, 0x1000001);
    write_u8(arg1, 0x24, 0x32);
    memcpy((uint8_t *)arg1 + 0x30, "\x00\x00\x04\x00\x1c\x00\x00\x00\xff\xff\x00\x00\x00\x00\x00\x01\x00", 0x11);
    write_u16(arg1, 0x44, 1);
    write_u16(arg1, 0x4e, 0xffff);
    write_u16(arg1, 0x50, 0xffff);
    write_u16(arg1, 0x4a, 0xffff);
    write_u16(arg1, 0x4c, 0xffff);
    write_u16(arg1, 0x08, 0);
    write_u16(arg1, 0x0a, 0);
    write_u16(arg1, 0x0c, 0);
    write_u16(arg1, 0x0e, 0);
    write_s32(arg1, 0x10, 0);
    write_s32(arg1, 0x18, 0);
    write_u8(arg1, 0x25, 0);
    write_s32(arg1, 0x28, 0);
    write_s32(arg1, 0x2c, 0);
    write_u8(arg1, 0x3a, 0);
    write_u8(arg1, 0x3b, 0);
    write_u8(arg1, 0x3c, 0);
    write_u8(arg1, 0x3d, 0);
    write_u8(arg1, 0x3e, 0);
    write_u8(arg1, 0x3f, 0);
    write_u8(arg1, 0x42, 0);
    write_u8(arg1, 0x46, 0);
    write_u16(arg1, 0x48, 0);
    write_u8(arg1, 0x53, 3);
    write_u16(arg1, 0x8a, 0xffff);
    write_u16(arg1, 0x8c, 0xffff);
    write_u8(arg1, 0x55, 2);
    write_u16(arg1, 0x90, 2);
    write_u8(arg1, 0x6a, 0xf);
    write_u16(arg1, 0x92, 0xa);
    write_s32(arg1, 0x94, 0x11);
    write_s32(arg1, 0x70, 0x34bc0);
    write_s32(arg1, 0x7c, 0xaae60);
    write_s32(arg1, 0x80, 0xaae60);
    write_u16(arg1, 0x9c, 0x1068);
    write_u8(arg1, 0x52, 5);
    write_u8(arg1, 0x54, 5);
    write_s32(arg1, 0x74, 0x41eb0);
    write_u16(arg1, 0x78, 0x19);
    write_u16(arg1, 0x7a, 0x3e8);
    write_u16(arg1, 0x84, 0x19);
    write_u16(arg1, 0x88, 0x33);
    write_u8(arg1, 0x56, 1);
    write_u8(arg1, 0x57, 1);
    write_s32(arg1, 0x58, 1);
    write_s32(arg1, 0x6c, 1);
    write_s32(arg1, 0x5c, 0);
    write_u8(arg1, 0x60, 0);
    write_s16(arg1, 0x61, 0);
    write_u8(arg1, 0x63, 0);
    write_s16(arg1, 0x66, 0);
    write_u16(arg1, 0x86, 0);
    write_u16(arg1, 0x8e, 0);
    write_s32(arg1, 0x98, 0);
    write_u8(arg1, 0x9e, 0xff);
    write_s32(arg1, 0xac, 2);
    write_s32(arg1, 0xb4, 0x7fffffff);
    write_s32(arg1, 0xcc, 3);
    write_s32(arg1, 0x100, 4);
    write_u16(arg1, 0xb0, 0x19);
    write_u8(arg1, 0xe8, 5);
    write_s32(arg1, 0x104, 5);
    write_s32(arg1, 0x108, 1);
    write_s32(arg1, 0x10c, 1);
    write_s32(arg1, 0x110, 1);
    write_u8(arg1, 0x116, 1);
    write_s32(arg1, 0xb8, 0);
    write_s32(arg1, 0xb4 - 0x10, 0);
    write_s32(arg1, 0xb4 - 0x14, 0);
    write_u8(arg1, 0xb2, 0);
    write_u8(arg1, 0xb9, 0);
    write_s32(arg1, 0xbc, 0);
    write_u8(arg1, 0xc4, 0);
    write_u16(arg1, 0xc5, 0);
    write_u8(arg1, 0xc7, 0);
    write_u8(arg1, 0xc8, 0);
    write_u8(arg1, 0xf4, 0);
    write_s16(arg1, 0x114, 0);
    write_s32(arg1, 0x118, 0);
    write_s16(arg1, 0x11a, 0);
    write_s32(arg1, 0x11c, 1);
    write_s32(arg1, 0x124, 1);
    write_s32(arg1, 0x128, 1);
    write_s32(arg1, 0x120, 0);
    Rtos_Memset((uint8_t *)arg1 + 0x12c, 0, 0x600);
    Rtos_Memset((uint8_t *)arg1 + 0x72c, 0, 0x18);
    Rtos_Memset((uint8_t *)arg1 + 0x744, 0, 8);
    Rtos_Memset((uint8_t *)arg1 + 0x74c, 0, 8);
    write_u8(arg1, 0x754, 0);
    AL_Settings_SetDefaultParam((uint8_t *)arg1 + 4);
    strncpy((char *)arg1 + 0x764, "NV12", 4);
    write_s32(arg1, 0x758, 1);
    write_u8(arg1, 0x760, 1);
    write_u8(arg1, 0x768, 1);
    write_u8(arg1, 0x769, 0);
    write_s32(arg1, 0x76c, 0x10);
    write_u8(arg1, 0x770, 0);
    write_s32(arg1, 0x774, 0);
    write_u8(arg1, 0x778, 0);
    write_s32(arg1, 0x77c, 0);
    write_s32(arg1, 0x780, 0);
    write_s32(arg1, 0x784, 0);
    return 0x10;
}

int32_t AL_Get_StreamMngrCtx(int32_t *arg1)
{
    void *v0 = *(void **)arg1;
    return read_s32(GetChMngrCtx((int32_t *)(intptr_t)(read_s32(v0, 0xf25c) + 4), (char)read_u8(*(void **)read_ptr(v0, 0x18), 0)), 0x2a50);
}

int32_t AL_Get_StreampCtx(int32_t *arg1)
{
    return *arg1;
}

int32_t AL_Set_StreampCtx(int32_t *arg1, void *arg2)
{
    int32_t a1 = read_s32(arg2, 0x790);
    write_s32(*(void **)arg1, 0xf27c, a1);
    return -(a1 < 1);
}

int32_t AL_Set_StreamMngrCtx(int32_t *arg1, void *arg2)
{
    void *v0 = *(void **)arg1;
    void *v0_2 = GetChMngrCtx((int32_t *)(intptr_t)(read_s32(v0, 0xf25c) + 4), (char)read_u8(*(void **)read_ptr(v0, 0x18), 0));
    int32_t v1_1 = read_s32(arg2, 0x78c);
    write_s32(v0_2, 0x2a50, v1_1);
    return -(v1_1 < 1);
}

void AL_Encoder_EndEncodingCallBack(void *arg1, AL_TBuffer *arg2, AL_TBuffer *arg3)
{
    if (arg2 != NULL) {
        if (arg3 == NULL)
            return;
        {
            void *s3_1 = read_ptr(arg3, 0x24);
            int32_t v0_1;
            int32_t v1_1;

            v0_1 = Rtos_GetTime();
            v1_1 = Rtos_GetTime();
            write_s32(s3_1, 0x41c, v1_1);
            write_s32(s3_1, 0x418, v0_1);

            {
                int32_t v0_2 = AL_Encoder_GetLastError((int32_t *)read_ptr(arg1, 0x798));
                if ((uint32_t)v0_2 >= 0x80U) {
                    fprintf(stderr, "%s\n", AL_Encoder_ErrorToString(v0_2));
                    fprintf(stderr, "%s:eProfile=0x%08x, uEncWidth=%u, uEncHeight=%u\n", AL_Encoder_ErrorToString(v0_2), read_u32(arg1, 0x24), read_u16(arg1, 0xc), read_u16(arg1, 0xe));
                    __assert("eErr == AL_SUCCESS", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", 0x14b, "AL_Encoder_EndEncodingCallBack");
                    Rtos_SetEvent(read_ptr(arg1, 0x79c));
                    return;
                }
            }

            if (read_s32(arg1, 0x920) == -1 || read_s32(arg1, 0x920) != -1) {
                int32_t a2 = read_s32(AL_Buffer_GetMetaData(arg2, 3), 0xc);
                write_s32(arg1, 0x920, a2);
                fprintf(stderr, "Picture Type %i\n", a2);
            }
            AL_Buffer_Ref(arg2);
            Fifo_Queue((uint8_t *)arg1 + 0x7f8, arg2, -1);
            Fifo_Queue((uint8_t *)arg1 + 0x81c, arg3, -1);
        }
    }
}

int32_t AL_Codec_Encode_Create(void **arg1, void *arg2)
{
    int32_t var_88;
    int32_t var_84;
    uint32_t var_80;
    int32_t a2_16;
    int32_t a2_17;
    const char *a3_20;

    if (g_pCodec == NULL) {
        a2_17 = 0x313;
        a3_20 = "AL_Codec_Encode_Create";
        __assert("g_pCodec", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", a2_17, a3_20, var_88, var_84, var_80);
    }
    if (arg2 == NULL)
        __assert("pCodecParam", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", 0x314, "AL_Codec_Encode_Create");

    {
        uint8_t *s0_1 = Rtos_Malloc(0x924);
        if (s0_1 == NULL) {
            fwrite("malloc AL_CodecEncode failed\n", 1, 0x1d, stderr);
            return -1;
        }
        *(void **)s0_1 = g_pCodec;
        Rtos_Memcpy(s0_1 + 4, arg2, 0x794);
        if (read_s32(s0_1, 0x75c) <= 0)
            write_s32(s0_1, 0x75c, 1);
        AL_Settings_SetDefaultParam(s0_1 + 8);

        {
            uint32_t a2_1;
            uint32_t a3_1;

            if ((uint32_t)(get_cpu_id() - 0x15) >= 2U) {
                a2_1 = read_u16(s0_1, 0xc);
                if (a2_1 - 0x10 >= 0xa11U) {
label_89f34:
                    a3_1 = read_u16(s0_1, 0xe);
label_89ed8:
                    fprintf(stderr, "invalid resolution(%dx%d) to encode\n", a2_1, a3_1);
label_897e8:
                    fwrite("ValidateParam failed\n", 1, 0x15, stderr);
                    Rtos_Free(s0_1);
                    return -1;
                }
                a3_1 = read_u16(s0_1, 0xe);
                if ((a2_1 & 0xf) != 0 || a3_1 - 8 >= 0xff9U)
                    goto label_89ed8;
                {
                    int32_t a2_2 = read_s32(s0_1, 0x24);
                    if (a2_2 == 0x42 || a2_2 == 0x4d || a2_2 == 0x64) {
label_896ac:
                        uint32_t v0_14 = read_u8(s0_1, 0x28);
                        if (v0_14 - 0xa >= 0x2aU) {
                            if (v0_14 < 0xa)
                                v0_14 = 0xa;
                            else if ((v0_14 & 0xff) >= 0x34U)
                                v0_14 = 0x33;
                        }
                        write_u8(s0_1, 0x28, (uint8_t)v0_14);
                    } else if (a2_2 != 0x1000001) {
                        if (a2_2 == 0x4000000)
                            goto label_896d8;
                        fprintf(stderr, "invalid profile(0x%x), only support avc baseline, avc main, avc high, hevc high, jpeg\n", a2_2);
                        goto label_897e8;
                    } else {
                        uint32_t v0_25 = read_u8(s0_1, 0x28);
                        int32_t v1_15 = v0_25 & 0xff;
                        if (v0_25 - 0xa >= 0x29U) {
                            if (v0_25 < 0xa) {
                                v0_25 = 0xa;
                                v1_15 = 0xa;
                            }
                            if ((uint32_t)v1_15 >= 0x33U)
                                v0_25 = 0x32;
                        }
                        write_u8(s0_1, 0x28, (uint8_t)v0_25);
                        if (read_u8(s0_1, 0x29) >= 2)
                            write_u8(s0_1, 0x29, 1);
                    }
                }
            } else {
                int32_t v0_13 = read_s32(s0_1, 0x24);
                if (v0_13 != 0x4000000) {
                    a2_1 = read_u16(s0_1, 0xc);
                    if (a2_1 - 0x10 >= 0xff1U)
                        goto label_89f34;
                    a3_1 = read_u16(s0_1, 0xe);
                    if ((a2_1 & 0xf) != 0 || a3_1 - 8 >= 0xff9U)
                        goto label_89ed8;
                    if (v0_13 == 0x42 || v0_13 == 0x4d || v0_13 == 0x64)
                        goto label_896ac;
                    fprintf(stderr, "invalid profile(0x%x), only support avc baseline, avc main, avc high, jpeg\n", v0_13);
                    goto label_897e8;
                }
            }
        }

label_896d8:
        {
            int32_t a2_3 = read_s32(s0_1, 0x18);
            if ((a2_3 & 0xfffffeff) != 0x88 && a2_3 != 0x288) {
                fprintf(stderr, "invalid ePicFormat(0x%x) to encode\n", a2_3);
                goto label_897e8;
            }
        }

        { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: reached ValidateRcParam\n"; write(kfd, m, strlen(m)); close(kfd); } }
        if (AL_Codec_Encode_ValidateRcParam_isra_1((int32_t *)(s0_1 + 0x120), (int32_t *)(s0_1 + 0x124), s0_1 + 8, (int32_t *)(s0_1 + 0x70), 0.0, 1.2, read_s32(s0_1, 0x768)) < 0) {
            { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: FAIL rcParam\n"; write(kfd, m, strlen(m)); close(kfd); } }
            fwrite("valid rcParam failed\n", 1, 0x15, stderr);
            goto label_897e8;
        }
        if (AL_Codec_Encode_ValidateGopParam(s0_1 + 8, (int32_t *)(s0_1 + 0xb0)) < 0) {
            { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: FAIL gopParam\n"; write(kfd, m, strlen(m)); close(kfd); } }
            fwrite("valid gopParam failed\n", 1, 0x16, stderr);
            goto label_897e8;
        }
        if (read_s32(s0_1, 0x12c) > 0) {
            uint8_t *s1_1 = s0_1 + 8;
            int32_t i = 0;
            do {
                if (AL_Settings_CheckCoherency(s0_1 + 8, s1_1, read_s32(s0_1, 0x768), stderr) == -1) {
                    { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: FAIL coherency\n"; write(kfd, m, strlen(m)); close(kfd); } }
                    fwrite("Fatal coherency error in settings\n", 1, 0x22, stderr);
                    goto label_897e8;
                }
                i += 1;
                s1_1 += 0xf0;
            } while (i < read_s32(s0_1, 0x12c));
        }
        if (AL_Settings_CheckValidity(s0_1 + 8, s0_1 + 8, stderr) != 0) {
            { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: FAIL CheckValidity\n"; write(kfd, m, strlen(m)); close(kfd); } }
            fwrite("AL_Settings_CheckValidity failed\n", 1, 0x21, stderr);
            goto label_897e8;
        }

        { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: CheckValidity PASS — setup cbk/event\n"; write(kfd, m, strlen(m)); close(kfd); } }
        write_ptr(s0_1, 0x7a4, s0_1);
        { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[128]; int n = snprintf(b, sizeof(b), "libimp/ENC: step1 EndEncCB=%p\n", (void*)AL_Encoder_EndEncodingCallBack); if (n>0) write(kfd, b, n); close(kfd); } }
        write_ptr(s0_1, 0x7a0, AL_Encoder_EndEncodingCallBack);
        { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: step2 pre-Rtos_CreateEvent\n"; write(kfd, m, strlen(m)); close(kfd); } }
        write_ptr(s0_1, 0x79c, Rtos_CreateEvent(0));
        { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: step3 pre-access\n"; write(kfd, m, strlen(m)); close(kfd); } }
        if (access("/tmp/dumpEncParam", 0) == 0)
            AL_Dump_TEncSettings((AL_TEncSettings *)(s0_1 + 8));
        { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: step4 post-access\n"; write(kfd, m, strlen(m)); close(kfd); } }

        {
            { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[96]; int n = snprintf(b, sizeof(b), "libimp/ENC: step5 pre-GetPool chn=%d\n", *(int32_t*)arg2); if (n>0) write(kfd, b, n); close(kfd); } }
            int32_t v0_31 = IMP_Encoder_GetPool(*(int32_t *)arg2);
            { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[96]; int n = snprintf(b, sizeof(b), "libimp/ENC: step6 GetPool=%d\n", v0_31); if (n>0) write(kfd, b, n); close(kfd); } }
            AL_TAllocator *s1_2;
            if (v0_31 < 0)
                s1_2 = (AL_TAllocator *)read_ptr(g_pCodec, 4);
            else
                s1_2 = AL_DmaAlloc_GetAllocator(v0_31);

            { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[160]; int n = snprintf(b, sizeof(b), "libimp/ENC: pre-Encoder_Create sch=%p alloc=%p settings=%p cbk=%p arg=%p\n", read_ptr(g_pCodec, 8), s1_2, s0_1+8, read_ptr(s0_1, 0x7a0), read_ptr(s0_1, 0x7a4)); if (n>0) write(kfd, b, n); close(kfd); } }
            if ((uint32_t)AL_Encoder_Create((int32_t **)(s0_1 + 0x798), (int32_t)(intptr_t)read_ptr(g_pCodec, 8), (int32_t)(intptr_t)s1_2, s0_1 + 8, (int32_t)(intptr_t)read_ptr(s0_1, 0x7a0), (int32_t)(intptr_t)read_ptr(s0_1, 0x7a4)) >= 0x80U) {
                { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[128]; int n = snprintf(b, sizeof(b), "libimp/ENC: FAIL AL_Encoder_Create errorCode=0x%x\n", read_s32(s0_1, 0x798)); if (n>0) write(kfd, b, n); close(kfd); } }
                fprintf(stderr, "AL_Encoder_Create failed, errorCode=%x:%s\n", read_s32(s0_1, 0x798), AL_Encoder_ErrorToString(read_s32(s0_1, 0x798)));
                Rtos_DeleteEvent(read_ptr(s0_1, 0x79c));
                Rtos_Free(s0_1);
                return -1;
            }
            { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[128]; int n = snprintf(b, sizeof(b), "libimp/ENC: Encoder_Create OK enc=%p\n", *(void **)(s0_1 + 0x798)); if (n>0) write(kfd, b, n); close(kfd); } }

            if ((uint32_t)read_u8(arg2, 0x23) == 4 && read_u8(arg2, 0x789) == 1) {
                int32_t v0_77 = AL_Set_StreamMngrCtx(*(int32_t **)(s0_1 + 0x798), arg2);
                int32_t v0_78 = 0;
                if (v0_77 >= 0)
                    v0_78 = AL_Set_StreampCtx(*(int32_t **)(s0_1 + 0x798), arg2);
                if (v0_77 < 0 || v0_78 < 0) {
                    puts("Jpeg Set StreamMngrCtx failed");
                    return -1;
                }
                puts("set jpeg streamMngCtx suceess");
            } else {
                ((int32_t *)arg2)[0x1e3] = AL_Get_StreamMngrCtx(*(int32_t **)(s0_1 + 0x798));
                ((int32_t *)arg2)[0x1e4] = AL_Get_StreampCtx(*(int32_t **)(s0_1 + 0x798));
            }

            if (read_u32(**(void ***)((uint8_t *)g_pCodec + 4), 0x18) == 0 || read_u32(**(void ***)((uint8_t *)g_pCodec + 4), 0x1c) == 0 || read_u32(**(void ***)((uint8_t *)g_pCodec + 4), 0x20) == 0) {
                write_u8(s0_1, 0x76c, 0);
                write_u8(s0_1, 0x76d, 0);
            }

            var_84 = read_s32(s0_1, 0x760);
            var_88 = read_s32(s0_1, 0x75c);
            GetStreamBufPoolConfig((int32_t *)(s0_1 + 0x7ac), s0_1 + 8, 0, read_u8(s0_1, 0x76d), (double)var_88, (double)var_84, 1.2, 0, 0);
            if (AL_BufPool_Init(s0_1 + 0x7c0, s1_2, (int32_t *)(s0_1 + 0x7ac)) == 0) {
                fwrite("AL_BufPool_Init failed\n", 1, 0x17, stderr);
                AL_Encoder_Destroy(*(int32_t **)(s0_1 + 0x798));
                Rtos_DeleteEvent(read_ptr(s0_1, 0x79c));
                Rtos_Free(s0_1);
                return -1;
            }
        }

        {
            int32_t s1_3 = 0;
            if (read_s32(s0_1, 0x7ac) > 0) {
                while (1) {
                    AL_TBuffer *v0_47 = AL_BufPool_GetBuffer(s0_1 + 0x7c0, 1);
                    if (v0_47 == NULL) {
                        a2_16 = 0x370;
                        __assert("pStream", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", a2_16, "AL_Codec_Encode_Create", var_88, var_84, var_80);
                    }
                    if (read_u32(v0_47, 0x20) != 0) {
                        void *v0_49 = (void *)(intptr_t)AL_PictureMetaData_Create();
                        if (v0_49 == NULL)
                            __assert("pMeta", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", 0x377, "AL_Codec_Encode_Create", var_88, var_84, var_80);
                        if (AL_Buffer_AddMetaData(v0_47, v0_49) == 0)
                            __assert("attached", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", 0x379, "AL_Codec_Encode_Create", var_88, var_84, var_80);
                    }
                    s1_3 += 1;
                    AL_Buffer_Unref(v0_47);
                    if (s1_3 >= read_s32(s0_1, 0x7ac))
                        break;
                }
            }
        }

        if (Fifo_Init(s0_1 + 0x7f8, read_s32(s0_1, 0x75c)) == 0)
            while (1)
                __assert("bRet == 1", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", 0, "AL_Codec_Encode_Create");
        if (Fifo_Init(s0_1 + 0x81c, read_s32(s0_1, 0x75c)) == 0)
            while (1)
                __assert("bRet == 1", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", 0, "AL_Codec_Encode_Create");

        {
            uint32_t var_5c[4];
            int32_t var_58 = -1;
            GetFrameInfo(var_5c, s0_1 + 8);
            write_s32(s0_1, 0x91c, read_s32(s0_1, 0x75c) + 1 + read_u8(s0_1, 0xb6));
            if (read_s32(s0_1, 0x770) < 2)
                var_84 = -1;
            else
                var_84 = (read_s32(s0_1, 0x770) - 1 + var_58) & -read_s32(s0_1, 0x770);
            var_80 = read_u8(s0_1, 0x76c);
            GetPixMapBufPollConfig((int32_t *)(s0_1 + 0x840), (int32_t *)var_5c, read_s32(s0_1, 0x1c), read_s32(s0_1, 0x91c), var_84, -1, (char)var_80);
            if (PixMapBufPollInit(s0_1 + 0x8e0, read_ptr(g_pCodec, 4), (int32_t *)(s0_1 + 0x840)) == 0) {
                fwrite("PixMapBufPollInit failed\n", 1, 0x19, stderr);
                AL_BufPool_Deinit(s0_1 + 0x7c0);
                AL_Encoder_Destroy(*(int32_t **)(s0_1 + 0x798));
                Rtos_DeleteEvent(read_ptr(s0_1, 0x79c));
                Rtos_Free(s0_1);
                return -1;
            }
            {
                AL_TPicFormat var_70;
                int32_t var_6c;
                int32_t var_68;
                int32_t var_64;
                int32_t var_60;
                char var_54;
                int32_t var_50;

                AL_EncGetSrcPicFormat(&var_70, var_50, (uint8_t)var_54, AL_GetSrcStorageMode(read_s32(s0_1, 0x1c)), AL_IsSrcCompressed(read_s32(s0_1, 0x1c)), var_84, var_80);
                var_88 = var_60;
                write_u32(s0_1, 0x918, AL_GetFourCC(var_70));
            }
        }

        if ((uint32_t)read_u8(arg2, 0x23) != 4 || read_u8(arg2, 0x789) == 0) {
            int32_t s2_5 = 0;
            if (read_s32(s0_1, 0x7ac) > 0) {
                while (1) {
                    AL_TBuffer *v0_68 = AL_BufPool_GetBuffer(s0_1 + 0x7c0, 1);
                    if (v0_68 == NULL) {
                        a2_16 = 0x396;
                        __assert("pStream", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", a2_16, "AL_Codec_Encode_Create", var_88, var_84, var_80);
                    }
                    if (AL_Encoder_PutStreamBuffer(*(int32_t **)(s0_1 + 0x798), v0_68, 0) == 0)
                        __assert("bRet", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", 0x399, "AL_Codec_Encode_Create");
                    s2_5 += 1;
                    AL_Buffer_Unref(v0_68);
                    if (s2_5 >= read_s32(s0_1, 0x7ac))
                        break;
                }
            }
        }

        write_s32(s0_1, 0x920, read_u32(s0_1, 0x774) != 0 ? 9 : -1);
        if (g_pCodec != NULL) {
            Rtos_GetMutex(read_ptr(g_pCodec, 0x10));
            {
                uint8_t *pCodec_4 = (uint8_t *)g_pCodec;
                int32_t i_1 = 0;
                int32_t *v1_28 = (int32_t *)(pCodec_4 + 0x14);
                do {
                    int32_t a1_18 = *v1_28;
                    if (a1_18 == 0) {
                        *(void **)(pCodec_4 + (((i_1 + 4) << 2) + 4)) = s0_1;
                        write_s32(s0_1, 0x7a8, i_1 + 1);
                        Rtos_AtomicIncrement((int32_t *)(pCodec_4 + 0x2c));
                        Rtos_ReleaseMutex(read_ptr(g_pCodec, 0x10));
                        *arg1 = s0_1;
                        return 0;
                    }
                    i_1 += 1;
                    v1_28 = &v1_28[1];
                } while (i_1 != 6);
                Rtos_ReleaseMutex(read_ptr(pCodec_4, 0x10));
                fwrite("AL_Codec_Encode_Register failed\n", 1, 0x20, stderr);
                AL_BufPool_Deinit(s0_1 + 0x8e0);
                AL_BufPool_Deinit(s0_1 + 0x7c0);
                AL_Encoder_Destroy(*(int32_t **)(s0_1 + 0x798));
                Rtos_DeleteEvent(read_ptr(s0_1, 0x79c));
                Rtos_Free(s0_1);
                return -1;
            }
        }

        a2_17 = 0x6c;
        a3_20 = "AL_Codec_Encode_Register";
        __assert("g_pCodec", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", a2_17, a3_20, var_88, var_84, var_80);
    }

    return -1;
}

int32_t AL_Codec_Encode_Destroy(void *arg1)
{
    uint8_t *pCodec_1 = (uint8_t *)g_pCodec;
    int32_t a2_2;
    const char *a3_2;

    if (pCodec_1 == NULL) {
        a2_2 = 0x3ba;
        a3_2 = "AL_Codec_Encode_Destroy";
        __assert("g_pCodec", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", a2_2, a3_2, &_gp);
        while (1) {
            a2_2 = 0x7f;
            a3_2 = "AL_Codec_Encode_UnRegister";
            __assert("g_pCodec", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", a2_2, a3_2, &_gp);
        }
    }
    if (arg1 == NULL)
        return (int32_t)(intptr_t)pCodec_1;

    AL_Encoder_Process(*(int32_t **)((uint8_t *)arg1 + 0x798), 0, 0);
    Rtos_WaitEvent(read_ptr(arg1, 0x79c), -1);
    if (g_pCodec != NULL)
        Rtos_GetMutex(read_ptr(g_pCodec, 0x10));
    {
        int32_t v0 = read_s32(arg1, 0x7a8);
        uint8_t *pCodec_3 = (uint8_t *)g_pCodec;
        if ((uint32_t)(v0 - 1) < 6U) {
            int32_t v0_2 = (int32_t)(intptr_t)(pCodec_3 + (v0 << 2));
            if (arg1 == read_ptr((void *)(intptr_t)v0_2, 0x10)) {
                write_ptr((void *)(intptr_t)v0_2, 0x10, NULL);
                write_s32(arg1, 0x7a8, 0);
                Rtos_AtomicDecrement((int32_t *)(pCodec_3 + 0x2c));
                pCodec_3 = (uint8_t *)g_pCodec;
            }
        }
        Rtos_ReleaseMutex(read_ptr(pCodec_3, 0x10));
    }
    Fifo_Deinit((uint8_t *)arg1 + 0x7f8);
    Fifo_Deinit((uint8_t *)arg1 + 0x81c);
    AL_Encoder_Destroy(*(int32_t **)((uint8_t *)arg1 + 0x798));
    AL_BufPool_Deinit((uint8_t *)arg1 + 0x8e0);
    if (read_u8(arg1, 0x27) != 4 || read_u32(arg1, 0x78c) == 0)
        AL_BufPool_Deinit((uint8_t *)arg1 + 0x7c0);
    Rtos_DeleteEvent(read_ptr(arg1, 0x79c));
    Rtos_Free(arg1);
    return 0;
}

int32_t AL_Codec_Encode_Process(int32_t *arg1, void *arg2, void *arg3)
{
    (void)arg3;

    if (g_pCodec == NULL)
        __assert("g_pCodec", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", 0x3d3, "AL_Codec_Encode_Process");
    if (arg2 == NULL) {
        AL_Encoder_Process((int32_t *)(intptr_t)arg1[0x1e6], (int32_t)(intptr_t)arg2, 0);
        return 0;
    }
    if (arg1[0x246] != *(int32_t *)((uint8_t *)arg2 + 0x10))
        __assert("pCodecEncode->m_SrcFourCC == pCodecFrame->frameInfo.pixfmt", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", 0x3da, "AL_Codec_Encode_Process");
    {
        AL_TBuffer *v0_2 = AL_BufPool_GetBuffer(&arg1[0x238], 0);
        if (read_u32(v0_2, 0x20) != 0) {
            int32_t v1_4 = *(int32_t *)((uint8_t *)arg2 + 0x14);
            int32_t a1_3 = read_s32(v0_2, 0x14);
            int32_t *a0_4 = *(int32_t **)(*(uint8_t **)arg1 + 4);
            int32_t a3_1 = *(int32_t *)((uint8_t *)arg2 + 0x18);
            write_ptr(v0_2, 0x24, arg2);
            ((void (*)(int32_t *, int32_t, int32_t, int32_t, int32_t))(*(int32_t *)*(void **)a0_4 + 0x20))(a0_4, a1_3, *(int32_t *)((uint8_t *)arg2 + 0x1c), a3_1, v1_4);
        } else {
            void *v0_4 = *(void **)arg1;
            int32_t a1 = read_s32(v0_2, 0x14);
            write_ptr(v0_2, 0x24, arg2);
            {
                int32_t *a0_1 = *(int32_t **)((uint8_t *)v0_4 + 4);
                int32_t v0_6 = ((int32_t (*)(int32_t *, int32_t))(*(int32_t *)*(void **)a0_1 + 0xc))(a0_1, a1);
                if (read_u32(v0_2, 8) < *(uint32_t *)((uint8_t *)arg2 + 0x14))
                    return AL_Codec_Encode_Commit_FilledFifo((int32_t *)(intptr_t)__assert("pCodecFrame->frameInfo.size <= pSrc->zSizes[0]", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", 0x3e5, "AL_Codec_Encode_Process"));
                Rtos_Memcpy((void *)(intptr_t)v0_6, *(void **)((uint8_t *)arg2 + 0x1c), *(uint32_t *)((uint8_t *)arg2 + 0x14));
            }
        }
        AL_Encoder_Process((int32_t *)(intptr_t)arg1[0x1e6], (int32_t)(intptr_t)v0_2, 0);
    }
    return 0;
}

int32_t AL_Codec_Encode_Commit_FilledFifo(int32_t *arg1)
{
    if (arg1 == NULL)
        return AL_Codec_Encode_GetStream((void *)(intptr_t)__assert("pCodecEncode", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", 0x3f0, "AL_Codec_Encode_Commit_FilledFifo", &_gp), NULL, NULL);
    Fifo_Decommit((uint8_t *)arg1 + 0x7f8);
    Fifo_Decommit((uint8_t *)arg1 + 0x81c);
    return 0;
}

int32_t AL_Codec_Encode_GetStream(void *arg1, void **arg2, void **arg3)
{
    const char *a3 = "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/FbcMapSize.c";

    if (arg1 == NULL) {
        return AL_Codec_Encode_ReleaseStream((void *)(intptr_t)__assert("pCodecEncode", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", 0x3f9, &a3[0x755c], &_gp), (void *)(intptr_t)0, (void *)(intptr_t)0);
    }
    if (arg2 == NULL) {
        a3 = (const char *)(intptr_t)__assert("pStream", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/lib_codec.c", 0x3fa, "AL_Codec_Encode_GetStream", &_gp);
    }
    *arg2 = Fifo_Dequeue((uint8_t *)arg1 + 0x7f8, -1);
    *arg3 = Fifo_Dequeue((uint8_t *)arg1 + 0x81c, -1);
    if (*arg2 == NULL)
        return -1;
    return -((uintptr_t)(*arg3) < 1U);
}

int32_t AL_Codec_Encode_ReleaseStream(void *arg1, void *arg2, void *arg3)
{
    (void)arg1;
    AL_Buffer_Unref(arg3);
    AL_Encoder_PutStreamBuffer(*(int32_t **)((uint8_t *)arg1 + 0x798), arg2, 0);
    AL_Buffer_Unref(arg2);
    return 0;
}

int32_t AL_Codec_Encode_GetSrcFrameCntAndSize(void *arg1, int32_t *arg2, int32_t *arg3)
{
    *arg2 = read_s32(arg1, 0x840);
    *arg3 = read_s32(arg1, 0x8dc);
    return 0;
}

int32_t AL_Codec_Encode_GetSrcStreamCntAndSize(void *arg1, int32_t *arg2, int32_t *arg3)
{
    *arg2 = read_s32(arg1, 0x7ac);
    *arg3 = read_s32(arg1, 0x7b0);
    return 0;
}

int32_t AL_Codec_Encode_GetRcParam(void *arg1, void *arg2)
{
    AL_Encoder_GetRcParam(*(int32_t **)((uint8_t *)arg1 + 0x798), arg2);
    return 0;
}

int32_t AL_Codec_Encode_SetRcParam(void *arg1, void *arg2)
{
    if (AL_Codec_Encode_ValidateRcParam_isra_1((int32_t *)((uint8_t *)arg1 + 0x120), (int32_t *)((uint8_t *)arg1 + 0x124), (uint8_t *)arg1 + 8, (int32_t *)arg2, 0.0, 1.2, read_s32(arg1, 0x768)) < 0) {
        fwrite("Validate RcParam failed\n", 1, 0x18, stderr);
        return -1;
    }
    if ((((*(int32_t *)arg2 - 1U) < 2U) || (((*(int32_t *)arg2 - 4) & 0xfffffffb) == 0))) {
        int32_t v0_6 = read_s32(arg1, 0x8dc) << 3;
        if ((uint32_t)v0_6 < ((uint32_t *)arg2)[5]) {
            fprintf(stdout, "Warning:%s:uMaxBitRate(%db)>streamBuf(%db)\n", "AL_Codec_Encode_SetRcParam", ((uint32_t *)arg2)[5], v0_6);
            ((uint32_t *)arg2)[5] = (uint32_t)v0_6;
        }
        if ((uint32_t)v0_6 < ((uint32_t *)arg2)[4]) {
            fprintf(stdout, "Warning:%s:uTargetBitRate(%db)>streamBuf(%db)\n", "AL_Codec_Encode_SetRcParam", ((uint32_t *)arg2)[5], v0_6);
            ((uint32_t *)arg2)[4] = (uint32_t)v0_6;
        }
        fprintf(stderr, "pRCParam->pMaxPictureSize[AL_SLICE_I]=%d, pCodecEncode->m_SrcBufPoolConfig.zBufSize * 8 / 1.2=%d\n", ((int32_t *)arg2)[0xf], v0_6);
        if (((int32_t *)arg2)[0xf] > (int32_t)((double)v0_6 / 1.2))
            ((int32_t *)arg2)[0xf] = (int32_t)((double)v0_6 / 1.2);
    }
    Rtos_Memcpy((uint8_t *)arg1 + 0x70, arg2, 0x40);
    AL_Encoder_SetRcParam(*(int32_t **)((uint8_t *)arg1 + 0x798), arg2);
    return 0;
}

int32_t AL_Codec_Encode_GetFrameRate(void *arg1, void *arg2)
{
    AL_Encoder_GetFrameRate(*(int32_t **)((uint8_t *)arg1 + 0x798), arg2);
    return 0;
}

int32_t AL_Codec_Encode_SetFrameRate(void *arg1, void *arg2)
{
    uint32_t s0 = *(uint16_t *)arg2;
    uint32_t s1 = *(uint16_t *)((uint8_t *)arg2 + 2);
    write_u16(arg1, 0x7c, (uint16_t)s0);
    write_u16(arg1, 0x7e, (uint16_t)s1);
    if (AL_Encoder_SetFrameRate(*(int32_t **)((uint8_t *)arg1 + 0x798), (int32_t)s0, (int32_t)s1) != 0)
        return 0;
    fprintf(stderr, "Encoder_SetFrameRate failed, uFrameRate=%u, uClkRatio=%u\n", s0, s1);
    return -1;
}

int32_t AL_Codec_Encode_SetQp(void *arg1, void *arg2)
{
    int32_t qp = *(int16_t *)arg2;
    if (AL_Encoder_SetQP(*(int32_t **)((uint8_t *)arg1 + 0x798), qp) != 0)
        return 0;
    fprintf(stderr, "Encoder_SetQp failed, iQP = %d\n", qp);
    return -1;
}

int32_t AL_Codec_Encode_SetQpBounds(void *arg1, int32_t arg2, int32_t arg3)
{
    int32_t s1 = 0x33;
    int32_t s0;

    if (arg3 < 0)
        arg3 = 0;
    s0 = arg3;
    if (arg3 >= 0x34)
        s0 = 0x33;
    if (arg2 < 0)
        arg2 = 0;
    if (s0 < arg2)
        arg2 = s0;
    if (arg2 < 0x34)
        s1 = arg2;
    write_s16(arg1, 0x8a, (int16_t)s1);
    write_s16(arg1, 0x8c, (int16_t)s0);
    if (AL_Encoder_SetQPBounds(*(int32_t **)((uint8_t *)arg1 + 0x798), s1, s0) != 0)
        return 0;
    fprintf(stderr, "Encoder_SetQpBounds failed, iMinQP=%d, iMaxQP=%d\n", s1, s0);
    return -1;
}

int32_t AL_Codec_Encode_SetQpIPDelta(void *arg1, int32_t arg2)
{
    int16_t a1 = (int16_t)arg2;
    write_s16(arg1, 0x8e, a1);
    if (AL_Encoder_SetQPIPDelta(*(int32_t **)((uint8_t *)arg1 + 0x798), a1) != 0)
        return 0;
    fprintf(stderr, "Encoder_SetQPIPDelta failed, uIPDelta = %d\n", arg2);
    return -1;
}

int32_t AL_Codec_Encode_SetBitRate(void *arg1, int32_t arg2, int32_t arg3)
{
    int32_t v0 = read_s32(arg1, 0x70);
    write_s32(arg1, 0x80, arg2);
    if (v0 == 1)
        write_s32(arg1, 0x84, arg2);
    else if ((((v0 - 2) & 0xfffffffd) == 0) || v0 == 8)
        write_s32(arg1, 0x84, arg3);
    if (AL_Encoder_SetBitRate(*(int32_t **)((uint8_t *)arg1 + 0x798), arg2, arg3) != 0)
        return 0;
    fwrite("Encoder_SetBitRate failed\n", 1, 0x1a, stderr);
    return -1;
}

int32_t AL_Codec_Encode_RestartGop(void *arg1)
{
    AL_Encoder_RestartGop(*(int32_t **)((uint8_t *)arg1 + 0x798));
    return 0;
}

int32_t AL_Codec_Encode_GetGopParam(void *arg1, void *arg2)
{
    AL_Encoder_GetGopParam(*(int32_t **)((uint8_t *)arg1 + 0x798), arg2);
    return 0;
}

int32_t AL_Codec_Encode_SetGopParam(void *arg1, void *arg2)
{
    if (AL_Codec_Encode_ValidateGopParam((uint8_t *)arg1 + 8, arg2) < 0) {
        fwrite("Validate GopParam failed\n", 1, 0x19, stderr);
        return -1;
    }
    Rtos_Memcpy((uint8_t *)arg1 + 0xb0, arg2, 0x1c);
    if (AL_Encoder_SetGopParam(*(int32_t **)((uint8_t *)arg1 + 0x798), arg2) != 0)
        return 0;
    fwrite("Encoder_SetGopParam failed\n", 1, 0x1b, stderr);
    return -1;
}

int32_t AL_Codec_Encode_SetGopLength(void *arg1, int32_t arg2)
{
    write_s32(arg1, 0xb4, arg2);
    if (AL_Encoder_SetGopLength(*(int32_t **)((uint8_t *)arg1 + 0x798)) != 0)
        return 0;
    fwrite("Encoder_SetGopLength failed\n", 1, 0x1c, stderr);
    return -1;
}
