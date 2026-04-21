#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "alcodec/al_buffer.h"
#include "alcodec/al_fourcc.h"
#include "alcodec/al_rtos.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, void *caller);
extern uint32_t __udivdi3(uint32_t lo, uint32_t hi, uint32_t div_lo, uint32_t div_hi);
extern const uint8_t PicStructToFieldNumber[];

typedef struct PixMapDimension {
    int32_t iWidth;
    int32_t iHeight;
} PixMapDimension;

int32_t AL_EncGetSrcPicFormat(AL_TPicFormat *pPicFormat, int32_t eChromaMode, int32_t uBitDepth,
                              int32_t eStorageMode, int32_t bIsCompressed); /* forward decl, ported by T<N> later */
int32_t AL_GetSrcStorageMode(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t AL_IsSrcCompressed(int32_t arg1); /* forward decl, ported by T<N> later */
PixMapDimension *AL_PixMapBuffer_GetDimension(PixMapDimension *arg1,
                                              AL_TBuffer *arg2); /* forward decl, ported by T<N> later */
uint32_t AL_PixMapBuffer_GetFourCC(AL_TBuffer *arg1); /* forward decl, ported by T<N> later */
int32_t AL_PixMapBuffer_GetPlanePitch(AL_TBuffer *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_PixMapBuffer_GetPlaneChunkIdx(AL_TBuffer *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
uint32_t AL_Buffer_GetSizeChunk(AL_TBuffer *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_EncGetMinPitch(int32_t arg1, char arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_GetAllocSizeSrc_Y(int32_t arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
uint32_t AL_GetAllocSizeSrc_UV(int32_t arg1, int32_t arg2, int32_t arg3,
                               int32_t arg4); /* forward decl, ported by T<N> later */
int32_t AL_GetAllocSizeSrc_MapY(int32_t arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t AL_GetAllocSizeSrc_MapUV(int32_t arg1, int32_t arg2, int32_t arg3,
                                 int32_t arg4); /* forward decl, ported by T<N> later */
int32_t AL_AVC_GenerateHwScalingList(void *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_AVC_WriteEncHwScalingList(void *arg1, void *arg2, uint32_t *arg3); /* forward decl, ported by T<N> later */
int32_t AL_AVC_GenerateSPS_Resolution(uint8_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4,
                                      int32_t arg5, int32_t arg6); /* forward decl, ported by T<N> later */
int32_t AL_Reduction(int32_t *arg1, int32_t *arg2); /* forward decl, ported by T<N> later */
void AL_Decomposition(int32_t *arg1, uint8_t *arg2); /* forward decl, ported by T<N> later */
int32_t AL_IsGdrEnabled(const uint8_t *arg1); /* forward decl, ported by T<N> later */
uint8_t AL_H273_ColourDescToColourPrimaries(int32_t arg1); /* forward decl, ported by T<N> later */
AL_TBuffer *AL_GetSrcBufferFromStatus(void *arg1); /* forward decl, ported by T<N> later */
uint32_t AL_Common_Encoder_SetHlsParam(void *arg1); /* forward decl, ported by T<N> later */
void *AL_Common_Encoder_SetME(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4,
                              int8_t *arg5); /* forward decl, ported by T<N> later */
int32_t AL_Common_Encoder_ComputeRCParam(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4,
                                         int16_t *arg5); /* forward decl, ported by T<N> later */
uint32_t *AL_ExtractNalsData(uint32_t *arg1, void *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
void *AL_GetAvcRbspWriter(void); /* forward decl, ported by T<N> later */
int32_t GenerateSections(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, uint32_t arg5, int32_t arg6,
                         int32_t arg7, int32_t arg8, void *arg9, int32_t arg10, void *arg11, int32_t arg12,
                         char arg13, char arg14); /* forward decl, ported by T<N> later */
int32_t AL_AVC_PreprocessScalingList(int32_t arg1, int32_t *arg2);
char AL_AVC_GenerateSPS(uint8_t *arg1, uint8_t *arg2, int32_t arg3, int32_t arg4);
int32_t AL_AVC_GeneratePPS(uint8_t *arg1, uint8_t *arg2, char arg3, uint8_t *arg4);
int32_t AL_AVC_UpdateSPS(uint8_t *arg1, uint8_t *arg2, void *arg3, uint8_t *arg4);
int32_t AL_AVC_UpdatePPS(uint8_t *arg1, uint8_t *arg2, uint8_t *arg3);

static int32_t fillScalingList(uint8_t *arg1, uint8_t *arg2, int32_t arg3, int32_t arg4, int32_t arg5, uint8_t *arg6);
static int32_t shouldReleaseSource(void);
static int32_t updateHlsAndWriteSections(uint8_t *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5);
static int32_t generateNals(uint8_t *arg1, int32_t arg2);
static int32_t ConfigureChannel(int32_t arg1, uint8_t *arg2, uint8_t *arg3);
static int32_t preprocessEp1(uint8_t *arg1);
static uint8_t *GetNalHeaderAvc(uint8_t *arg1, char arg2, int32_t arg3);
static uint32_t *CreateAvcNuts(uint32_t *arg1);
static int32_t AVC_GenerateSections(uint8_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4);

static const uint8_t AL_AVC_DefaultScalingLists8x8[0x80] = {
    0x06, 0x0a, 0x0d, 0x10, 0x12, 0x17, 0x19, 0x1b, 0x0a, 0x0b, 0x10, 0x12, 0x17, 0x19, 0x1b, 0x1d,
    0x0d, 0x10, 0x12, 0x17, 0x19, 0x1b, 0x1d, 0x1f, 0x10, 0x12, 0x17, 0x19, 0x1b, 0x1d, 0x1f, 0x21,
    0x12, 0x17, 0x19, 0x1b, 0x1d, 0x1f, 0x21, 0x24, 0x17, 0x19, 0x1b, 0x1d, 0x1f, 0x21, 0x24, 0x26,
    0x19, 0x1b, 0x1d, 0x1f, 0x21, 0x24, 0x26, 0x28, 0x1b, 0x1d, 0x1f, 0x21, 0x24, 0x26, 0x28, 0x2a,
    0x09, 0x0d, 0x0f, 0x11, 0x13, 0x15, 0x16, 0x18, 0x0d, 0x0d, 0x11, 0x13, 0x15, 0x16, 0x18, 0x19,
    0x0f, 0x11, 0x13, 0x15, 0x16, 0x18, 0x19, 0x1b, 0x11, 0x13, 0x15, 0x16, 0x18, 0x19, 0x1b, 0x1c,
    0x13, 0x15, 0x16, 0x18, 0x19, 0x1b, 0x1c, 0x1e, 0x15, 0x16, 0x18, 0x19, 0x1b, 0x1c, 0x1e, 0x20,
    0x16, 0x18, 0x19, 0x1b, 0x1c, 0x1e, 0x20, 0x21, 0x18, 0x19, 0x1b, 0x1c, 0x1e, 0x20, 0x21, 0x23,
};

static const uint8_t AL_AVC_DefaultScalingLists4x4[0x20] = {
    0x06, 0x0d, 0x14, 0x1c, 0x0d, 0x14, 0x1c, 0x20, 0x14, 0x1c, 0x20, 0x25, 0x1c, 0x20, 0x25, 0x2a,
    0x0a, 0x0e, 0x14, 0x18, 0x0e, 0x14, 0x18, 0x1b, 0x14, 0x18, 0x1b, 0x1e, 0x18, 0x1b, 0x1e, 0x22,
};

static void srcchk_kmsg(const char *fmt, ...)
{
    int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return;

    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0)
        dprintf(fd, "libimp/SRCCHK: %s\n", buf);
    close(fd);
}

static void srcchk_dump_layout(AL_TBuffer *buf, uint32_t fourcc)
{
    srcchk_kmsg("layout buf=%p fourcc=0x%x plane_chunk=[%d,%d,%d,%d] plane_pitch=[%d,%d,%d,%d] chunk_size=[%u,%u,%u,%u]",
                buf, fourcc,
                AL_PixMapBuffer_GetPlaneChunkIdx(buf, 0),
                AL_PixMapBuffer_GetPlaneChunkIdx(buf, 1),
                AL_PixMapBuffer_GetPlaneChunkIdx(buf, 2),
                AL_PixMapBuffer_GetPlaneChunkIdx(buf, 3),
                AL_PixMapBuffer_GetPlanePitch(buf, 0),
                AL_PixMapBuffer_GetPlanePitch(buf, 1),
                AL_PixMapBuffer_GetPlanePitch(buf, 2),
                AL_PixMapBuffer_GetPlanePitch(buf, 3),
                AL_Buffer_GetSizeChunk(buf, 0),
                AL_Buffer_GetSizeChunk(buf, 1),
                AL_Buffer_GetSizeChunk(buf, 2),
                AL_Buffer_GetSizeChunk(buf, 3));
}

int32_t AL_SrcBuffersChecker_Init(uint32_t *arg1, uint8_t *arg2)
{
    uint32_t v1 = (uint32_t)*(uint16_t *)(arg2 + 8);
    uint32_t v0 = (uint32_t)*(uint16_t *)(arg2 + 0x0a);
    int32_t a0 = *(int32_t *)(arg2 + 0x14);
    void *var_40 = &_gp;
    int32_t s2 = *(int32_t *)(arg2 + 0x10);
    AL_TPicFormat var_38;
    int32_t result;

    (void)var_40;
    arg1[2] = v1;
    arg1[0] = v1;
    arg1[3] = v0;
    arg1[1] = v0;
    AL_EncGetSrcPicFormat(&var_38, (s2 >> 8) & 0xf, (uint32_t)*(uint16_t *)(arg2 + 0x18),
                          AL_GetSrcStorageMode(a0), AL_IsSrcCompressed(*(int32_t *)(arg2 + 0x14)));
    arg1[4] = *(uint32_t *)((uint8_t *)&var_38 + 0);
    arg1[5] = *(uint32_t *)((uint8_t *)&var_38 + 4);
    arg1[6] = *(uint32_t *)((uint8_t *)&var_38 + 8);
    arg1[7] = *(uint32_t *)((uint8_t *)&var_38 + 0x0c);
    arg1[8] = *(uint32_t *)((uint8_t *)&var_38 + 0x10);
    result = (int32_t)AL_GetFourCC(var_38);
    arg1[0x0a] = (uint32_t)result;
    arg1[9] = *(uint32_t *)(arg2 + 0x14);
    return result;
}

int32_t AL_SrcBuffersChecker_UpdateResolution(int32_t *arg1, int32_t arg2, int32_t arg3)
{
    int32_t arg_4 = arg2;
    int32_t arg_8 = arg3;

    (void)arg_4;
    (void)arg_8;
    if (arg1[2] < arg2 || arg1[3] < arg3)
        return 0;
    arg1[0] = arg2;
    arg1[1] = arg3;
    return 1;
}

int32_t AL_SrcBuffersChecker_CanBeUsed(int32_t *arg1, AL_TBuffer *arg2)
{
    PixMapDimension actual_dim;
    PixMapDimension alloc_dim;
    uint32_t fourcc;
    int32_t chroma_mode;
    int32_t pitch_y;
    int32_t pitch_uv = 0;
    int32_t min_pitch;
    int32_t alloc_pitch_y;
    int32_t alloc_chroma_mode;
    int32_t chunk_need[3];
    int32_t plane;
    int32_t chunk_idx;

    if (arg2 == NULL)
        return 0;

    if (AL_Buffer_GetMetaData(arg2, 0) == NULL)
        return 0;

    AL_PixMapBuffer_GetDimension(&actual_dim, arg2);
    fourcc = AL_PixMapBuffer_GetFourCC(arg2);
    chroma_mode = AL_GetChromaMode(fourcc);
    pitch_y = AL_PixMapBuffer_GetPlanePitch(arg2, 0);
    min_pitch = AL_EncGetMinPitch(actual_dim.iWidth, (char)AL_GetBitDepth(fourcc), AL_GetStorageMode(fourcc));

    if (actual_dim.iWidth != arg1[0] || actual_dim.iHeight != arg1[1] || (int32_t)fourcc != arg1[0x0a] ||
        pitch_y < min_pitch || (pitch_y & 0xf) != 0) {
        srcchk_dump_layout(arg2, fourcc);
        srcchk_kmsg("reject basic buf=%p fourcc=0x%x exp_fourcc=0x%x pitchY=%d min_pitch=%d dim=%dx%d exp=%dx%d",
                    arg2, fourcc, arg1[0x0a], pitch_y, min_pitch,
                    actual_dim.iWidth, actual_dim.iHeight, arg1[0], arg1[1]);
        return 0;
    }

    if (chroma_mode != 0)
        pitch_uv = AL_PixMapBuffer_GetPlanePitch(arg2, 1);

    if (chroma_mode != 0 && pitch_y != pitch_uv) {
        srcchk_dump_layout(arg2, fourcc);
        srcchk_kmsg("reject uv-pitch buf=%p fourcc=0x%x pitchY=%d pitchUV=%d chroma=%d dim=%dx%d exp=%dx%d",
                    arg2, fourcc, pitch_y, pitch_uv, chroma_mode,
                    actual_dim.iWidth, actual_dim.iHeight, arg1[0], arg1[1]);
        return 0;
    }

    AL_PixMapBuffer_GetDimension(&alloc_dim, arg2);
    alloc_pitch_y = AL_PixMapBuffer_GetPlanePitch(arg2, 0);
    alloc_chroma_mode = AL_GetChromaMode(AL_PixMapBuffer_GetFourCC(arg2));
    chunk_need[0] = 0;
    chunk_need[1] = 0;
    chunk_need[2] = 0;

    for (plane = 0; plane != 4; ++plane) {
        int32_t plane_need;

        chunk_idx = AL_PixMapBuffer_GetPlaneChunkIdx(arg2, plane);
        if (chunk_idx == -1)
            continue;

        if (chunk_idx < 0 || chunk_idx >= 3) {
            srcchk_dump_layout(arg2, fourcc);
            srcchk_kmsg("reject chunk-idx buf=%p plane=%d chunk=%d fourcc=0x%x dim=%dx%d",
                        arg2, plane, chunk_idx, fourcc, alloc_dim.iWidth, alloc_dim.iHeight);
            return 0;
        }

        if (plane == 2) {
            plane_need = AL_GetAllocSizeSrc_MapY(alloc_dim.iWidth, alloc_dim.iHeight, arg1[9]);
        } else if (plane == 3) {
            plane_need = AL_GetAllocSizeSrc_MapUV(alloc_dim.iWidth, alloc_dim.iHeight, arg1[9], alloc_chroma_mode);
        } else if (plane == 1) {
            plane_need = (int32_t)AL_GetAllocSizeSrc_UV(arg1[9], alloc_pitch_y,
                                                        ((alloc_dim.iHeight + 7) >> 3) << 3,
                                                        alloc_chroma_mode);
        } else {
            plane_need = AL_GetAllocSizeSrc_Y(alloc_dim.iWidth, alloc_dim.iHeight, alloc_pitch_y);
        }

        chunk_need[chunk_idx] += plane_need;
    }

    for (chunk_idx = 0; chunk_idx != 3; ++chunk_idx) {
        if (chunk_need[chunk_idx] != 0 &&
            AL_Buffer_GetSizeChunk(arg2, chunk_idx) < (uint32_t)chunk_need[chunk_idx]) {
            srcchk_dump_layout(arg2, fourcc);
            srcchk_kmsg("reject chunk buf=%p idx=%d need=%d have=%u fourcc=0x%x pitchY=%d pitchUV=%d dim=%dx%d exp=%dx%d",
                        arg2, chunk_idx, chunk_need[chunk_idx], AL_Buffer_GetSizeChunk(arg2, chunk_idx),
                        fourcc, pitch_y, pitch_uv, actual_dim.iWidth, actual_dim.iHeight, arg1[0], arg1[1]);
            return 0;
        }
    }

    return 1;
}

static int32_t fillScalingList(uint8_t *arg1, uint8_t *arg2, int32_t arg3, int32_t arg4, int32_t arg5, uint8_t *arg6)
{
    int32_t v0_1 = arg3 * 6;
    uint8_t *a0_1 = arg2 + (((v0_1 + arg4) << 6) + 0x13b);

    if ((uint32_t)arg1[v0_1 + arg4 + 0x728] != 0U) {
        int32_t v0_5 = 0x40;

        *arg6 = 1;
        if (arg3 == 0)
            v0_5 = 0x10;
        return (int32_t)(intptr_t)Rtos_Memcpy(a0_1, arg1 + (((v0_1 + arg4) << 6) + 0x128), (size_t)v0_5);
    }

    *arg6 = 0;
    if (arg3 != 0)
        return (int32_t)(intptr_t)Rtos_Memcpy(a0_1, &AL_AVC_DefaultScalingLists8x8[arg5 << 6], 0x40);
    if (arg4 != 0 && arg4 != 3) {
        uint8_t *v0_8 = arg2 + arg4 + 0x16;

        arg4 -= 1;
        if (v0_8 != NULL) {
            *arg6 = 1;
            return (int32_t)(intptr_t)Rtos_Memcpy(a0_1, arg2 + ((arg4 << 6) + 0x13b), 0x10);
        }
    }
    return (int32_t)(intptr_t)Rtos_Memcpy(a0_1, &AL_AVC_DefaultScalingLists4x4[arg5 << 4], 0x10);
}

uint8_t *AL_AVC_SelectScalingList(uint8_t *arg1, uint8_t *arg2)
{
    uint8_t i_2 = arg2[0x10c];

    if (i_2 == 3) {
        int32_t a0_13 = __assert("eScalingList != AL_SCL_MAX_ENUM",
                                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/AVC_EncUtils.c",
                                 0x2e, "AL_AVC_SelectScalingList", &_gp);
        return (uint8_t *)(intptr_t)AL_AVC_PreprocessScalingList(a0_13, NULL);
    }
    if (i_2 != 0) {
        arg1[0x16] = 1;
        if (i_2 == 2) {
            uint8_t *s5 = arg1 + 0x17;
            int32_t s3 = 0;
            int32_t i = 0;
            int32_t v0_5 = 0;

            while (i != 2) {
                int32_t i_3 = i;
                int32_t a3_5;

                fillScalingList(arg2, arg1, 1, s3, i, arg1 + i + 0x1d);
                fillScalingList(arg2, arg1, 0, s3, i, s5);
                fillScalingList(arg2, arg1, 0, s3 + 1, i, &s5[1]);
                a3_5 = s3 + 2;
                i += 1;
                s3 += 3;
                v0_5 = fillScalingList(arg2, arg1, 0, a3_5, i_3, &s5[2]);
                s5 = &s5[3];
            }
            return (uint8_t *)(intptr_t)v0_5;
        }
        if (i_2 == 1) {
            uint8_t *i_1 = arg1 + 0x17;

            do {
                *i_1 = 0;
                i_1 = &i_1[1];
            } while (arg1 + 0x1f != i_1);
            Rtos_Memcpy(arg1 + 0x2bb, &AL_AVC_DefaultScalingLists8x8[0], 0x40);
            Rtos_Memcpy(arg1 + 0x13b, &AL_AVC_DefaultScalingLists4x4[0], 0x10);
            Rtos_Memcpy(arg1 + 0x17b, &AL_AVC_DefaultScalingLists4x4[0], 0x10);
            Rtos_Memcpy(arg1 + 0x1bb, &AL_AVC_DefaultScalingLists4x4[0], 0x10);
            Rtos_Memcpy(arg1 + 0x37b, &AL_AVC_DefaultScalingLists8x8[0x40], 0x40);
            Rtos_Memcpy(arg1 + 0x1fb, &AL_AVC_DefaultScalingLists4x4[0x10], 0x10);
            Rtos_Memcpy(arg1 + 0x23b, &AL_AVC_DefaultScalingLists4x4[0x10], 0x10);
            return (uint8_t *)Rtos_Memcpy(arg1 + 0x27b, &AL_AVC_DefaultScalingLists4x4[0x10], 0x10);
        }
    } else {
        uint8_t *i_3 = arg1 + 0x17;

        arg1[0x16] = 0;
        do {
            *i_3 = 0;
            i_3 = &i_3[1];
        } while (arg1 + 0x1f != i_3);
        return i_3;
    }

    return NULL;
}

int32_t AL_AVC_PreprocessScalingList(int32_t arg1, int32_t *arg2)
{
    void *var_5dd8 = &_gp;
    uint8_t var_5dd0[0x5dd0];
    int32_t result;

    (void)var_5dd8;
    AL_AVC_GenerateHwScalingList((void *)(intptr_t)arg1, (int32_t)(intptr_t)&var_5dd0[0]);
    AL_AVC_WriteEncHwScalingList((void *)(intptr_t)arg1, &var_5dd0[0], (uint32_t *)(uintptr_t)(*arg2 + 0x100));
    result = arg2[5] | 0x10;
    arg2[5] = result;
    return result;
}

char AL_AVC_GenerateSPS(uint8_t *arg1, uint8_t *arg2, int32_t arg3, int32_t arg4)
{
    int32_t v1 = *(int32_t *)(arg2 + 0x1c);
    int32_t v0 = v1 & 0xffff00;
    int32_t s2_1 = (*(int32_t *)(arg2 + 0x10) >> 8) & 0xf;
    uint32_t v1_2;
    uint8_t v0_8;
    uint32_t a2_1;
    int32_t v0_22;
    int32_t v1_6;
    char result;

    arg1[4] = (uint8_t)((v0 >> 8) & 1);
    arg1[5] = (uint8_t)((v0 >> 9) & 1);
    arg1[6] = (uint8_t)((v0 >> 0x0a) & 1);
    arg1[7] = (uint8_t)((v0 >> 0x0b) & 1);
    arg1[8] = (uint8_t)((v0 >> 0x0c) & 1);
    arg1[9] = (uint8_t)((v0 >> 0x0d) & 1);
    arg1[0x10] = (uint8_t)s2_1;
    arg1[0x13] = (uint8_t)((arg2[0x10] & 0xf) - 8);
    arg1[0x14] = (uint8_t)(((arg2[0x10] >> 4) & 0xf) - 8);
    arg1[0] = (uint8_t)(v1 & 0xff);
    arg1[0x15] = 0;
    AL_AVC_SelectScalingList(arg1, arg2);
    v1_2 = (uint32_t)*(uint16_t *)(arg2 + 0x20);
    arg1[0x0c] = 0;
    arg1[0x750] = 0;
    arg1[0xb64] = 0;
    v0_8 = arg2[0x24];
    arg1[0x1d4] = 0;
    *(int32_t *)(arg1 + 0xb60) = arg3;
    arg1[0x751] = (uint8_t)((v0_8 & 0xf) - 3);
    *(int32_t *)(arg1 + 0x0c) = v1_2;
    if ((*(int32_t *)(arg2 + 0xa8) & 4) != 0 && (uint32_t)*(uint16_t *)(arg2 + 0xae) == 0xf) {
        arg1[0x74f] = 1;
    } else if (AL_IsGdrEnabled(arg2) != 0) {
        arg1[0x74f] = 6;
    }
    arg1[0xb93] = (uint8_t)(((s2_1 ^ 1) < 1) ? 1 : 0);
    arg1[0xb6a] = 1;
    arg1[0xb6c] = 1;
    arg1[0xb80] = 1;
    arg1[0xb6b] = 0;
    arg1[0xb85] = 0;
    arg1[0xb95] = 0;
    AL_AVC_GenerateSPS_Resolution(arg1, (uint32_t)*(uint16_t *)(arg2 + 4), (uint32_t)*(uint16_t *)(arg2 + 6),
                                  (uint32_t)*(uint16_t *)(arg2 + 0x4e), *(int32_t *)(arg2 + 0x10),
                                  *(int32_t *)(arg2 + 0xfc));
    arg1[0xb8c] = 1;
    arg1[0xb8d] = 5;
    arg1[0xb8f] = 1;
    arg1[0xb8a] = 0;
    arg1[0xb8e] = 0;
    arg1[0xb90] = AL_H273_ColourDescToColourPrimaries(*(int32_t *)(arg2 + 0x100));
    arg1[0xb91] = arg2[0x104];
    arg1[0xbac] = 1;
    arg1[0xb92] = arg2[0x108];
    a2_1 = (uint32_t)*(uint16_t *)(arg2 + 0x74);
    *(int32_t *)(arg1 + 0xbb0) = (uint32_t)*(uint16_t *)(arg2 + 0x76);
    *(int32_t *)(arg1 + 0xbb4) = a2_1 * 0x7d0;
    AL_Reduction((int32_t *)(void *)(arg1 + 0xbb4), (int32_t *)(void *)(arg1 + 0xbb0));
    arg1[0x106d] = 0;
    arg1[0xbc4] = 0;
    arg1[0xbc5] = 1;
    v0_22 = *(int32_t *)(arg2 + 0x7c);
    v1_6 = *(int32_t *)(arg2 + 0x120);
    *(int32_t *)(arg1 + 0xe4c) = 0;
    if (v1_6 == 0)
        __builtin_trap();
    *(int32_t *)(arg1 + 0xe4c) = (int32_t)((uint32_t)v0_22 / (uint32_t)v1_6) >> 6;
    AL_Decomposition((int32_t *)(void *)(arg1 + 0xe4c), arg1 + 0xbcb);
    if (*(uint32_t *)(arg1 + 0xe4c) == 0xffffffffU) {
        int32_t a0_6 = __assert("pSubHrdParam->bit_rate_value_minus1[0] <= ((4294967295U) - 1)",
                                "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/AVC_EncUtils.c",
                                0x67, "AL_AVC_UpdateHrdParameters", &_gp);
        return (char)AL_AVC_GeneratePPS((uint8_t *)(intptr_t)a0_6, NULL, 0, 0);
    }
    *(int32_t *)(arg1 + 0xecc) = arg4 >> 4;
    AL_Decomposition((int32_t *)(void *)(arg1 + 0xecc), arg1 + 0xbcd);
    if (*(uint32_t *)(arg1 + 0xecc) == 0xffffffffU) {
        int32_t a0_6 = __assert("pSubHrdParam->cpb_size_value_minus1[0] <= ((4294967295U) - 1)",
                                "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/AVC_EncUtils.c",
                                0x6c, "AL_AVC_UpdateHrdParameters", &_gp);
        return (char)AL_AVC_GeneratePPS((uint8_t *)(intptr_t)a0_6, NULL, 0, 0);
    }
    *(int32_t *)(arg1 + 0xbce) = 0x1f;
    v0_22 = *(int32_t *)(arg2 + 0x68);
    result = (char)(((v0_22 ^ 1) < 1) ? 1 : 0);
    arg1[0x106c] = (uint8_t)result;
    *(int32_t *)(arg1 + 0xbcf) = 0x1f;
    arg1[0xbd0] = 0x1f;
    *(int32_t *)(arg1 + 0xbd4) = 0;
    arg1[0xc04] = 0;
    arg1[0x106f] = 1;
    arg1[0x1078] = 0;
    return result;
}

int32_t AL_AVC_GeneratePPS(uint8_t *arg1, uint8_t *arg2, char arg3, uint8_t *arg4)
{
    int32_t v0_7;
    int32_t v0_8;
    uint32_t v0_11;
    char result;

    arg1[0] = 0;
    arg1[1] = 0;
    arg1[0x7fc0] = (uint8_t)(arg3 - 1);
    arg1[0x7fc1] = (uint8_t)(arg3 - 1);
    arg1[2] = (uint8_t)(((arg2[0x54] ^ 1) < 1) ? 1 : 0);
    arg1[3] = 0;
    *(int32_t *)(arg1 + 4) = 0;
    arg1[0x7fc2] = (uint8_t)(((*(int32_t *)(arg2 + 0x58) ^ 1) < 1) ? 1 : 0);
    arg1[0x7fc3] = arg2[0x58];
    arg1[0x7fc4] = 0;
    arg1[0x7fc5] = 0;
    v0_7 = (int32_t)*(int8_t *)(arg2 + 0x38);
    if (v0_7 >= 0x0d)
        v0_7 = 0x0c;
    if (v0_7 < -0x0c)
        v0_7 = -0x0c;
    arg1[0x7fc6] = (uint8_t)v0_7;
    v0_8 = (int32_t)*(int8_t *)(arg2 + 0x39);
    arg1[0x7fc8] = 1;
    if (v0_8 >= 0x0d)
        v0_8 = 0x0c;
    if (v0_8 < -0x0c)
        v0_8 = -0x0c;
    arg1[0x7fc7] = (uint8_t)v0_8;
    arg1[0x7fca] = 0;
    arg1[0x7fc9] = (uint8_t)((*(uint32_t *)(arg2 + 0x30) >> 6) & 1);
    v0_11 = (uint32_t)*(uint16_t *)(arg2 + 0x50);
    arg1[0x7fcc] = 0;
    result = (char)(((v0_11 < 3U) ? 1U : 0U) ^ 1U);
    arg1[0x7fcb] = (uint8_t)result;
    *(uint32_t *)(arg1 + 0x80c0) = (uint32_t)(uintptr_t)arg4;
    return result;
}

int32_t AL_AVC_UpdateSPS(uint8_t *arg1, uint8_t *arg2, void *arg3, uint8_t *arg4)
{
    uint32_t v0 = (uint32_t)*arg4;

    if (v0 == 0)
        return (int32_t)v0;

    {
        PixMapDimension var_18;
        AL_PixMapBuffer_GetDimension(&var_18, AL_GetSrcBufferFromStatus(arg3));
        AL_AVC_GenerateSPS_Resolution(arg1, (uint32_t)(uint16_t)var_18.iWidth, (uint32_t)(uint16_t)var_18.iHeight,
                                      (uint32_t)*(uint16_t *)(arg2 + 0x4e), *(int32_t *)(arg2 + 0x10),
                                      *(int32_t *)(arg2 + 0xfc));
    }

    arg1[0x10] = arg4[1];
    return arg4[1];
}

int32_t AL_AVC_UpdatePPS(uint8_t *arg1, uint8_t *arg2, uint8_t *arg3)
{
    int32_t result = 0;

    arg1[0x7fc4] = (uint8_t)(arg2[0xac] - 0x1a);
    if ((uint32_t)*arg3 != 0U)
        result = 1;
    arg1[0] = arg3[1];
    arg1[1] = arg3[1];
    return result;
}

static int32_t shouldReleaseSource(void)
{
    return 1;
}

static int32_t updateHlsAndWriteSections(uint8_t *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    int32_t s0_3 = arg5 * 0xe0c4;
    int32_t a1_3 = 0;
    int32_t result;

    AL_AVC_UpdateSPS(arg1 + s0_3 + 0x1c, *(uint8_t **)(arg1 + 0x14), arg2, (uint8_t *)&arg3);
    AVC_GenerateSections(arg1, arg4, (int32_t)(intptr_t)arg2, AL_AVC_UpdatePPS(arg1 + s0_3 + 0x5a28, (uint8_t *)arg2,
                                                                                (uint8_t *)&arg3));
    if (*(int32_t *)((uint8_t *)arg2 + 0xa0) != 2)
        a1_3 = *(int32_t *)(arg1 + 0xed58);
    result = (int32_t)PicStructToFieldNumber[*(uint8_t *)(uintptr_t)*(uint32_t *)((uint8_t *)arg2 + 0xa4)] + a1_3;
    *(int32_t *)(arg1 + 0xed58) = result;
    return result;
}

static int32_t generateNals(uint8_t *arg1, int32_t arg2)
{
    uint8_t *s3 = *(uint8_t **)(arg1 + 0x14);
    uint8_t *a1_2 = s3 + arg2 * 0xf0;
    uint64_t product = (uint64_t)*(uint32_t *)(a1_2 + 0x70) * (uint64_t)*(uint32_t *)(a1_2 + 0x7c);
    uint32_t v0_2 = __udivdi3((uint32_t)product, (uint32_t)(product >> 32), 0x7d0, 0);

    AL_AVC_GenerateSPS(arg1 + 0x1c, s3, *(int32_t *)(arg1 + 0xed80), (int32_t)v0_2);
    return AL_AVC_GeneratePPS(arg1 + 0x5a28, *(uint8_t **)(arg1 + 0x14), *(int32_t *)(arg1 + 0xed80),
                              arg1 + 0x1c);
}

static int32_t ConfigureChannel(int32_t arg1, uint8_t *arg2, uint8_t *arg3)
{
    void *var_18 = &_gp;
    { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[128]; int n = snprintf(b, sizeof(b), "libimp/ENC: AVC.ConfigureChannel ENTRY a2=%p a3=%p\n", (void*)arg2, (void*)arg3); if (n>0) write(kfd, b, n); close(kfd); } }
    int32_t v0_1 = *(int32_t *)(arg2 + 0x90) & 8;
    int32_t a1 = 0x0a;
    int32_t v0_3;
    int32_t v0_4;

    (void)arg1;
    (void)var_18;
    *(int32_t *)(arg2 + 0x24) = 0x100000;
    if (v0_1 != 0)
        a1 = 0x10;
    if ((intptr_t)arg2 == -36) {
        int32_t a0_7 = __assert("pHlsParam",
                                "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/include/lib_common_enc/EncChanParam.h",
                                0x9e, "AL_SET_SPS_LOG2_MAX_POC", &_gp);
        return preprocessEp1((uint8_t *)(intptr_t)a0_7);
    }
    v0_3 = (a1 - 1) | 0x100000;
    a1 = 0xa0;
    if ((*(int32_t *)(arg2 + 0xa8) & 4) != 0 && (uint32_t)*(uint16_t *)(arg2 + 0xae) == 0x0f) {
        v0_4 = 0x50 | v0_3;
    } else {
        int32_t v1_4 = 0x40;

        if ((*(int32_t *)(arg2 + 0xbc) & 2) != 0)
            v1_4 = 0xa0;
        v0_4 = v1_4 | v0_3;
    }
    *(int32_t *)(arg2 + 0x24) = v0_4;
    *(int32_t *)(arg2 + 0x28) = 4;
    { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: CC-pre-SetHlsParam\n"; write(kfd, m, strlen(m)); close(kfd); } }
    AL_Common_Encoder_SetHlsParam(arg2);
    { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: CC-pre-SetME\n"; write(kfd, m, strlen(m)); close(kfd); } }
    AL_Common_Encoder_SetME(0x10, 0x10, 8, 8, (int8_t *)arg2);
    { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: CC-pre-ComputeRCParam\n"; write(kfd, m, strlen(m)); close(kfd); } }
    AL_Common_Encoder_ComputeRCParam((int32_t)*(int8_t *)(arg2 + 0x38), (int32_t)*(int8_t *)(arg2 + 0x39), 0, 0x0c,
                                     (int16_t *)arg2);
    { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: CC-post-ComputeRCParam\n"; write(kfd, m, strlen(m)); close(kfd); } }
    if (*(int32_t *)(arg3 + 0x10c) != 0) {
        int32_t result = *(int32_t *)(arg2 + 0x30) | 0x20;

        *(int32_t *)(arg2 + 0x30) = result;
        return result;
    }
    return *(int32_t *)(arg3 + 0x10c);
}

static int32_t preprocessEp1(uint8_t *arg1)
{
    int32_t result = *(int32_t *)(*(uint8_t **)(arg1 + 0x14) + 0x10c);

    if (result != 0)
        return AL_AVC_PreprocessScalingList((int32_t)(intptr_t)(arg1 + 0x11b), (int32_t *)(void *)(arg1 + 0x14));
    return result;
}

int32_t (*AL_CreateAvcEncoder(int32_t (**arg1)(void)))(void)
{
    { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[192]; int n = snprintf(b, sizeof(b), "libimp/ENC: AL_CreateAvcEncoder arg1=%p fns[0..4]=%p,%p,%p,%p,%p\n", (void*)arg1, (void*)shouldReleaseSource, (void*)preprocessEp1, (void*)ConfigureChannel, (void*)generateNals, (void*)updateHlsAndWriteSections); if (n>0) write(kfd, b, n); close(kfd); } }
    *arg1 = shouldReleaseSource;
    arg1[1] = (int32_t (*)(void))preprocessEp1;
    arg1[2] = (int32_t (*)(void))ConfigureChannel;
    arg1[3] = (int32_t (*)(void))generateNals;
    arg1[4] = (int32_t (*)(void))updateHlsAndWriteSections;
    return (int32_t (*)(void))updateHlsAndWriteSections;
}

static uint8_t *GetNalHeaderAvc(uint8_t *arg1, char arg2, int32_t arg3)
{
    arg1[0] = (uint8_t)(((arg3 & 3) << 5) | (arg2 & 0x1f));
    arg1[4] = 1;
    return arg1;
}

static uint32_t *CreateAvcNuts(uint32_t *arg1)
{
    arg1[5] = 6;
    arg1[0] = (uint32_t)(uintptr_t)GetNalHeaderAvc;
    arg1[1] = 7;
    arg1[2] = 8;
    arg1[3] = 9;
    arg1[4] = 0x0c;
    arg1[6] = 6;
    return arg1;
}

static int32_t AVC_GenerateSections(uint8_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    uint32_t var_38[7];
    uint32_t var_58[8];
    uint8_t *s1_1;

    CreateAvcNuts(&var_38[0]);
    AL_ExtractNalsData(&var_58[0], arg1, 0);
    s1_1 = *(uint8_t **)(arg1 + 0x14);
    return GenerateSections(AL_GetAvcRbspWriter(), (int32_t)var_38[0], (int32_t)var_38[1], (int32_t)var_38[2],
                            var_38[3], (int32_t)var_38[4], (int32_t)var_38[5], (int32_t)var_38[6], &var_58[0], arg2,
                            (void *)(intptr_t)arg3, arg4, (char)*(uint32_t *)(s1_1 + 0x40),
                            (char)*(uint32_t *)(s1_1 + 0xc4));
}
