#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "alcodec/al_buffer.h"
#include "alcodec/al_rtos.h"
#include "alcodec/al_settings.h"
#include "core/globals.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function, ...);
extern int32_t access(const char *pathname, int mode);

#define READ_U8(base, off) (*(uint8_t *)((uint8_t *)(base) + (off)))
#define READ_S8(base, off) (*(int8_t *)((uint8_t *)(base) + (off)))
#define READ_U16(base, off) (*(uint16_t *)((uint8_t *)(base) + (off)))
#define READ_S16(base, off) (*(int16_t *)((uint8_t *)(base) + (off)))
#define READ_U32(base, off) (*(uint32_t *)((uint8_t *)(base) + (off)))
#define READ_S32(base, off) (*(int32_t *)((uint8_t *)(base) + (off)))
#define READ_PTR(base, off) (*(void **)((uint8_t *)(base) + (off)))
#define WRITE_U8(base, off, val) (*(uint8_t *)((uint8_t *)(base) + (off)) = (uint8_t)(val))
#define WRITE_S8(base, off, val) (*(int8_t *)((uint8_t *)(base) + (off)) = (int8_t)(val))
#define WRITE_U16(base, off, val) (*(uint16_t *)((uint8_t *)(base) + (off)) = (uint16_t)(val))
#define WRITE_S16(base, off, val) (*(int16_t *)((uint8_t *)(base) + (off)) = (int16_t)(val))
#define WRITE_U32(base, off, val) (*(uint32_t *)((uint8_t *)(base) + (off)) = (uint32_t)(val))
#define WRITE_S32(base, off, val) (*(int32_t *)((uint8_t *)(base) + (off)) = (int32_t)(val))
#define WRITE_PTR(base, off, val) (*(void **)((uint8_t *)(base) + (off)) = (void *)(val))

#define CENC_KMSG(fmt, ...) do { \
    int _kfd = open("/dev/kmsg", O_WRONLY); \
    if (_kfd >= 0) { \
        char _b[256]; \
        int _n = snprintf(_b, sizeof(_b), "libimp/CENC: " fmt "\n", ##__VA_ARGS__); \
        if (_n > 0) write(_kfd, _b, _n > (int)sizeof(_b) ? (int)sizeof(_b) : _n); \
        close(_kfd); \
    } \
} while (0)

int32_t AL_EncRecBuffer_FillPlaneDesc(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
                                      int32_t arg6, void *arg7); /* forward decl, ported by T<N> later */
int32_t AL_GetEncoderFbcMapSize(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4); /* forward decl */
int32_t AL_Buffer_Unref(AL_TBuffer *buffer); /* forward decl, ported by T<N> later */
int32_t AL_Buffer_Ref(AL_TBuffer *buffer, int32_t arg2); /* forward decl, ported by T<N> later */
uint32_t AL_Buffer_GetPhysicalAddress(AL_TBuffer *buffer); /* forward decl, ported by T<N> later */
void AL_StreamMetaData_ClearAllSections(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_ParamConstraints_CheckLFTcOffset(int32_t arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_ParamConstraints_CheckLFBetaOffset(int32_t arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t AL_ParamConstraints_CheckResolution(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4,
                                            void *arg5); /* forward decl, ported by T<N> later */
int32_t AL_SrcBuffersChecker_UpdateResolution(void *arg1, int32_t arg2, int32_t arg3); /* forward decl */
void watermark_deinit(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t MemDesc_Alloc(void *arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t MemDesc_Free(void *arg1); /* forward decl, ported by T<N> later */
int32_t AL_Fifo_Deinit(void *arg1); /* forward decl, ported by T<N> later */

static int32_t setNewParams(int32_t *arg1, int32_t arg2);
static int32_t destroyChannels_part_6(int32_t *arg1);

int32_t FillRec(int32_t *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    int32_t v0 = arg5;

    if (v0 != 0) {
        int32_t s6_1 = READ_S32(arg2, 0x10);
        uint32_t t0_1 = READ_U8(arg2, 0x1f);
        int32_t a0 = (int32_t)((uint32_t)s6_1 >> 4) & 0xf;
        int32_t v0_1 = s6_1 & 0xf;
        int32_t s6_2;
        int32_t s7_1 = 0x10;
        int32_t s5_1;
        int32_t t0_2 = 2;
        int32_t var_38 = 0;
        int32_t var_34_1 = 0;
        int32_t var_30_1 = 0;
        int32_t var_2c_1 = 0;
        int32_t s0_2;
        int32_t s0_3;

        if (a0 >= v0_1)
            v0_1 = a0;
        s6_2 = (int32_t)((uint32_t)s6_1 >> 8) & 0xf;
        s5_1 = t0_1 < 1 ? 1 : 0;
        if (t0_1 != 0)
            s7_1 = 8;
        if (s6_2 != 1)
            t0_2 = 1;

        AL_EncRecBuffer_FillPlaneDesc(&var_38, arg3, arg4, s6_2, v0_1, s5_1, &_gp);
        if (arg4 < 0) {
            if (s7_1 == 0)
                __builtin_trap();
            s0_2 = arg4 / s7_1 * s7_1;
        } else {
            if (s7_1 == 0)
                __builtin_trap();
            s0_2 = (s7_1 + arg4 - 1) / s7_1 * s7_1;
        }

        s0_3 = s0_2 >> 2;
        arg1[0] = arg5 + var_34_1;
        arg1[1] = s0_3 * var_30_1;
        if (s6_2 != 0) {
            var_38 = 1;
            AL_EncRecBuffer_FillPlaneDesc(&var_38, arg3, arg4, s6_2, v0_1, s5_1, NULL);
            v0 = READ_S32(arg2, 0x2c) & 0x20;
            arg1[2] = arg5 + var_34_1;
            if (t0_2 == 0)
                __builtin_trap();
            arg1[3] = (int32_t)((uint32_t)(s0_3 * var_30_1) / (uint32_t)t0_2);
            if (v0 != 0) {
                int32_t v0_13;

                var_38 = 2;
                AL_EncRecBuffer_FillPlaneDesc(&var_38, arg3, arg4, s6_2, v0_1, s5_1, NULL);
                v0_13 = AL_GetEncoderFbcMapSize(0, arg3, arg4, s7_1);
                arg1[4] = arg5 + var_34_1;
                arg1[5] = v0_13;
                var_38 = 3;
                AL_EncRecBuffer_FillPlaneDesc(&var_38, arg3, arg4, s6_2, v0_1, s5_1, NULL);
                if (t0_2 == 0)
                    __builtin_trap();
                arg1[6] = arg5 + var_34_1;
                arg1[7] = (int32_t)((uint32_t)v0_13 / (uint32_t)t0_2);
            }

            return var_34_1;
        }

        v0 = READ_S32(arg2, 0x2c) & 0x20;
        if (v0 != 0) {
            var_38 = 2;
            AL_EncRecBuffer_FillPlaneDesc(&var_38, arg3, arg4, 0, v0_1, s5_1, NULL);
            arg1[5] = AL_GetEncoderFbcMapSize(0, arg3, arg4, s7_1);
            arg1[4] = arg5 + var_34_1;
            return var_34_1;
        }
    }

    return v0;
}

int32_t releaseSource(int32_t *arg1, int32_t arg2, void *arg3)
{
    int32_t *v1;
    int32_t v0_1;
    int32_t result;

    Rtos_GetMutex(READ_PTR(arg1, 0xf254));
    v1 = (int32_t *)((uint8_t *)arg1 + 0xf114);
    v0_1 = 0;

    while (1) {
        v0_1 += 1;
        if (arg2 == *v1)
            break;
        v1 = &v1[2];
        if (v0_1 == 0x26) {
            int32_t a0_6;
            int32_t a1_1;

            a0_6 = __assert("0", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c", 0x13c,
                            "RemoveSourceSent", &_gp);
            a1_1 = 0;
            return setNewParams((int32_t *)(intptr_t)a0_6, a1_1);
        }
    }

    WRITE_S32(arg1, ((v0_1 - 1) << 3) + 0xf114, 0);
    WRITE_S32(arg1, ((v0_1 - 1) << 3) + 0xf110, 0);
    Rtos_ReleaseMutex(READ_PTR(arg1, 0xf254));
    result = AL_Buffer_Unref((AL_TBuffer *)(intptr_t)arg2);
    if (arg3 != NULL) {
        int32_t a0_4 = READ_S32(arg3, 0x20);

        if (a0_4 != 0)
            return AL_Buffer_Unref((AL_TBuffer *)(intptr_t)a0_4);
    }

    return result;
}

static int32_t setNewParams(int32_t *arg1, int32_t arg2)
{
    int32_t s1 = arg2 << 6;
    int32_t s0 = arg2 << 0xa;
    int32_t v1 = s0 - s1 - arg2;
    int32_t s3;
    int32_t s2;
    int32_t t9;
    void (*cb)(int32_t *, int32_t, int32_t);
    void *a1_1;
    uint8_t *a0_2;
    int32_t *i;
    int32_t *v0_12;
    int32_t s5_1;
    uint8_t *s2_2;

    (void)_gp;
    WRITE_S32(arg1, v1 * 0x3c + 0xdb1c, READ_S32(arg1, v1 * 0x3c + 0xdb1c) | 0x10);

    if (access("/tmp/NewEncParam", 0) == 0) {
        s3 = arg2 << 4;
        s2 = arg2 << 8;
        {
            int32_t s6_1 = s2 - s3;
            uint8_t *v0_14 = (uint8_t *)READ_PTR(arg1, 0x14) + s6_1;

            fprintf(stderr, "############AL_Dump_TRCGopParam start wxh=%dx%d, profile=0x%x############\n",
                    READ_U8(v0_14, 4), READ_U8(v0_14, 6), READ_S32(v0_14, 0x1c));
            AL_Dump_TRCParam((AL_TRCParam *)(v0_14 + 0x68));
            AL_Dump_TGopParam((AL_TGopParam *)(v0_14 + 0xa8));
            fprintf(stderr, "############AL_Dump_TRCGopParam end wxh=%dx%d, profile=0x%x############\n",
                    READ_U8(v0_14, 4), READ_U8(v0_14, 6), READ_S32(v0_14, 0x1c));
        }
        t9 = READ_S32(arg1, 0xc);
    } else {
        s3 = arg2 << 4;
        s2 = arg2 << 8;
        t9 = READ_S32(arg1, 0xc);
    }

    cb = (void (*)(int32_t *, int32_t, int32_t))(intptr_t)t9;
    cb(arg1, arg2, 1);
    a1_1 = READ_PTR(arg1, 0x14);
    a0_2 = (uint8_t *)a1_1 + s2 - s3;
    i = (int32_t *)(a0_2 + 0x68);
    v0_12 = (int32_t *)((uint8_t *)arg1 + (s0 - s1 - arg2) * 0x3c + 0xdb24);

    do {
        int32_t t1_1 = i[0];
        int32_t t0_1 = i[1];
        int32_t a3_1 = i[2];
        int32_t a2_1 = i[3];

        i = &i[4];
        v0_12[0] = t1_1;
        v0_12[1] = t0_1;
        v0_12[2] = a3_1;
        v0_12[3] = a2_1;
        v0_12 = &v0_12[4];
    } while ((uint8_t *)i != a0_2 + 0xa8);

    s5_1 = s0 - s1 - arg2;
    s2_2 = (uint8_t *)a1_1 + s2 - s3;
    WRITE_S32(arg1, s5_1 * 0x3c + 0xdb64, READ_S32(s2_2, 0xa8));
    WRITE_S32(arg1, s5_1 * 0x3c + 0xdb68, READ_S32(s2_2, 0xac));
    WRITE_S32(arg1, s5_1 * 0x3c + 0xdb6c, READ_S32(s2_2, 0xb0));
    WRITE_S32(arg1, s5_1 * 0x3c + 0xdb70, READ_S32(s2_2, 0xb4));
    WRITE_S32(arg1, s5_1 * 0x3c + 0xdb74, READ_S32(s2_2, 0xb8));
    WRITE_S32(arg1, s5_1 * 0x3c + 0xdb78, READ_S32(s2_2, 0xbc));
    WRITE_S32(arg1, s5_1 * 0x3c + 0xdb7c, READ_S32(s2_2, 0xc0));
    return 1;
}

static int32_t destroyChannels_part_6(int32_t *arg1)
{
    if (READ_S32(READ_PTR(arg1, 0x14), 0x124) > 0) {
        int32_t *a0_6 = (int32_t *)READ_PTR(arg1, 0xf25c);

        ((void (*)(int32_t *, int32_t))(intptr_t)READ_S32(*(void **)a0_6, 8))(a0_6, READ_S32(arg1, 0x18));
    }

    if (READ_U8(READ_PTR(arg1, 0x14), 0x1f) != 4 || READ_PTR(arg1, 0xf27c) == NULL) {
        int32_t i = READ_S32(arg1, 0xdbb4);

        while (i != READ_S32(arg1, 0xdbb0)) {
            int32_t s4_1 = READ_S32(arg1, ((i + 0x36ec) << 2) + 8);

            ((void (*)(void *, int32_t, int32_t, int32_t, void *))(intptr_t)READ_S32(arg1, 0xe0d4))(
                READ_PTR(arg1, 0xe0d8), s4_1, 0, 0, &_gp);
            AL_Buffer_Unref((AL_TBuffer *)(intptr_t)s4_1);
            i = (i + 1) % 0x140;
        }

        {
            int32_t *i_1 = (int32_t *)((uint8_t *)arg1 + 0xf114);

            do {
                int32_t s1_1 = *i_1;

                if (s1_1 != 0) {
                    ((void (*)(void *, int32_t, int32_t, int32_t, void *))(intptr_t)READ_S32(arg1, 0xe0d4))(
                        READ_PTR(arg1, 0xe0d8), 0, s1_1, 0, &_gp);
                    releaseSource(arg1, s1_1, (void *)(intptr_t)*(i_1 - 4));
                }
                i_1 = &i_1[2];
            } while (i_1 != (int32_t *)((uint8_t *)arg1 + 0xf244));
        }
    }

    Rtos_DeleteMutex(READ_PTR(arg1, 0xf254));
    Rtos_DeleteSemaphore(READ_PTR(arg1, 0xf258));
    if (READ_S32(READ_PTR(arg1, 0x14), 0x124) > 0) {
        uint8_t *s0_2 = (uint8_t *)arg1 + 0xdb98;
        int32_t i_2 = 0;

        do {
            i_2 += 1;
            MemDesc_Free(s0_2);
            s0_2 += 0xe0c4;
        } while (i_2 < READ_S32(READ_PTR(arg1, 0x14), 0x124));
    }

    AL_Fifo_Deinit((uint8_t *)arg1 + 0xf0f0);
    return (int32_t)(intptr_t)Rtos_Memset(READ_PTR(arg1, 0xf268), 0, READ_U32(arg1, 0xf270));
}

int32_t AL_Common_Encoder_WaitReadiness(int32_t *arg1)
{
    return Rtos_GetSemaphore(READ_PTR(arg1, 0xf258), -1);
}

void *AL_Common_Encoder_NotifySceneChange(int32_t *arg1, int32_t arg2)
{
    void *result = (void *)(intptr_t)*arg1;
    int32_t v1 = READ_S32(result, 0xdb1c);

    WRITE_S32(result, 0xdb20, arg2);
    WRITE_S32(result, 0xdb1c, v1 | 1);
    return result;
}

void *AL_Common_Encoder_NotifyIsLongTerm(int32_t *arg1)
{
    void *result = (void *)(intptr_t)*arg1;

    if (READ_U8(READ_PTR(READ_PTR(result, 0x14), 0xb4), 0) != 0)
        WRITE_S32(result, 0xdb1c, READ_S32(result, 0xdb1c) | 2);
    return result;
}

void *AL_Common_Encoder_NotifyUseLongTerm(int32_t *arg1)
{
    void *result = (void *)(intptr_t)*arg1;

    if (READ_U8(READ_PTR(READ_PTR(result, 0x14), 0xb4), 0) != 0)
        WRITE_S32(result, 0xdb1c, READ_S32(result, 0xdb1c) | 4);
    return result;
}

void *AL_Common_Encoder_NotifyIsSkip(int32_t *arg1)
{
    void *result = (void *)(intptr_t)*arg1;

    WRITE_S32(result, 0xdb1c, READ_S32(result, 0xdb1c) | 0x80);
    return result;
}

void *AL_Common_Encoder_NotifyGMV(int32_t *arg1, int32_t arg2, int16_t arg3, int16_t arg4)
{
    void *result = (void *)(intptr_t)*arg1;
    int32_t v1 = READ_S32(result, 0xdb1c);

    WRITE_S32(result, 0xdb90, arg2);
    WRITE_S16(result, 0xdb94, arg3);
    WRITE_S32(result, 0xdb1c, v1 | 0x20);
    WRITE_S16(result, 0xdb96, arg4);
    return result;
}

int32_t AL_Common_Encoder_PutStreamBuffer(int32_t *arg1, AL_TBuffer *arg2, int32_t arg3)
{
    void *s2;
    int32_t v0;
    int32_t s1;
    int32_t s6;

    (void)_gp;
    s2 = (void *)(intptr_t)*arg1;
    v0 = (int32_t)(intptr_t)AL_Buffer_GetMetaData(arg2, 1);
    CENC_KMSG("PutStreamBuffer arg1=%p ctx=%p buf=%p meta=%p layer=%d", arg1, s2, arg2, (void *)(intptr_t)v0, arg3);
    if (v0 != 0) {
        AL_StreamMetaData_ClearAllSections((void *)(intptr_t)v0);
        if (s2 != NULL) {
            void *mode_base = READ_PTR(s2, 0x14);
            void *stream_mutex = READ_PTR(s2, 0xf254);
            void *stream_vtbl_obj = READ_PTR(s2, 0xf25c);
            s1 = arg3 << 4;
            s6 = arg3 << 8;
            CENC_KMSG("PutStreamBuffer ctx=%p mode_base=%p stream_mutex=%p stream_vtbl_obj=%p slot_off=0x%x",
                      s2, mode_base, stream_mutex, stream_vtbl_obj, s6 - s1);
            if (READ_U8((uint8_t *)mode_base + s6 - s1, 0x1f) == 4) {
                void *s5_2 = READ_PTR(s2, 0xf27c);
                CENC_KMSG("PutStreamBuffer special alt_ctx=%p", s5_2);

                if (s5_2 != NULL) {
                    int32_t s3_4;
                    uint8_t *s6_2;
                    int32_t s2_1;
                    int32_t a1_4;
                    int32_t *a0_10;
                    int32_t var_3c_1;
                    int32_t var_38_1;
                    int32_t var_40_1;

                    Rtos_GetMutex(READ_PTR(s5_2, 0xf254));
                    s3_4 = arg3 * 0x3831;
                    s6_2 = (uint8_t *)s5_2 + (s3_4 << 2);
                    s2_1 = READ_S32(s6_2, 0xdbb0);
                    WRITE_PTR(s5_2, ((s3_4 + s2_1 + 0x36ec) << 2) + 8, arg2);
                    a1_4 = (s2_1 + 1) % 0x140;
                    WRITE_S32(s6_2, 0xdbb0, a1_4);
                    AL_Buffer_Ref(arg2, a1_4);
                    a0_10 = (int32_t *)READ_PTR(s5_2, 0xf25c);
                    var_3c_1 = s2_1 >> 0x1f;
                    var_38_1 = 0x200;
                    var_40_1 = s2_1;
                    CENC_KMSG("PutStreamBuffer special dispatch obj=%p vtbl=%p chn=%d",
                              a0_10, a0_10 ? *(void **)a0_10 : NULL, READ_S32(s6_2, 0x18));
                    ((void (*)(int32_t *, int32_t, AL_TBuffer *))(intptr_t)READ_S32(*(void **)a0_10, 0x10))(
                        a0_10, READ_S32(s6_2, 0x18), arg2);
                    Rtos_ReleaseMutex(READ_PTR(s5_2, 0xf254));
                    return 1;
                }
            } else {
                int32_t a2_3;
                uint8_t *s3_2;
                int32_t s7;
                int32_t a1_1;
                int32_t s1_3 = 0x200;
                int32_t *a0_5;
                int32_t var_3c;
                int32_t var_38;
                int32_t var_40;

                Rtos_GetMutex(READ_PTR(s2, 0xf254));
                a2_3 = arg3 * 0x3831;
                s3_2 = (uint8_t *)s2 + (a2_3 << 2);
                s7 = READ_S32(s3_2, 0xdbb0);
                WRITE_PTR(s2, ((a2_3 + s7 + 0x36ec) << 2) + 8, arg2);
                a1_1 = (s7 + 1) % 0x140;
                WRITE_S32(s3_2, 0xdbb0, a1_1);
                AL_Buffer_Ref(arg2, a1_1);
                if (READ_U8((uint8_t *)mode_base + s6 - s1, 0x1f) == 4)
                    s1_3 = 0;
                if (AL_Buffer_GetSize(arg2) < 0x200U) {
                    Rtos_ReleaseMutex(READ_PTR(s2, 0xf254));
                    return 0;
                }
                a0_5 = (int32_t *)READ_PTR(s2, 0xf25c);
                CENC_KMSG("PutStreamBuffer normal dispatch obj=%p vtbl=%p chn=%d size=%u",
                          a0_5, a0_5 ? *(void **)a0_5 : NULL, READ_S32(s3_2, 0x18), (unsigned)AL_Buffer_GetSize(arg2));
                var_3c = s7 >> 0x1f;
                var_38 = s1_3;
                var_40 = s7;
                CENC_KMSG("PutStreamBuffer pre-vtbl-call obj=%p fn=%p slot=%d next=%d",
                          a0_5, a0_5 ? (void *)(intptr_t)READ_S32(*(void **)a0_5, 0x10) : NULL, s7, a1_1);
                ((void (*)(int32_t *, int32_t, AL_TBuffer *))(intptr_t)READ_S32(*(void **)a0_5, 0x10))(
                    a0_5, READ_S32(s3_2, 0x18), arg2);
                CENC_KMSG("PutStreamBuffer post-vtbl-call obj=%p chn=%d",
                          a0_5, READ_S32(s3_2, 0x18));
                Rtos_ReleaseMutex(READ_PTR(s2, 0xf254));
                CENC_KMSG("PutStreamBuffer post-release ctx=%p", s2);
                return 1;
            }
        }

        __assert("pCtx", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c", 0x1b6,
                 "AL_Common_Encoder_PutStreamBuffer");
    }

    __assert("pMetaData", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c", 0x1b5,
             "AL_Common_Encoder_PutStreamBuffer");
    return 0;
}

int32_t AL_Common_Encoder_GetRecPicture(int32_t *arg1, int32_t *arg2, int32_t arg3)
{
    void *v1 = (void *)(intptr_t)*arg1;

    if (v1 == NULL) {
        __assert("pCtx", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c", 0x1e7,
                 "AL_Common_Encoder_GetRecPicture", &_gp);
        return 0;
    }

    ((void (*)(void))(intptr_t)READ_S32(*(void **)READ_PTR(v1, 0xf25c), 0x14))();
    (void)READ_S32(v1, arg3 * 0xe0c4 + 0x18);
    (void)arg2;
    return 0;
}

int32_t AL_Common_Encoder_ReleaseRecPicture(int32_t *arg1, int32_t *arg2, int32_t arg3)
{
    void *v1 = (void *)(intptr_t)*arg1;

    if (v1 == NULL) {
        __assert("pCtx", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c", 0x1f0,
                 "AL_Common_Encoder_ReleaseRecPicture", &_gp);
        return 0;
    }

    {
        int32_t *a0_1 = (int32_t *)READ_PTR(v1, 0xf25c);
        void *a0_2;
        int32_t v0_4;

        ((void (*)(int32_t *, int32_t, int32_t *))(intptr_t)READ_S32(*(void **)a0_1, 0x18))(
            a0_1, READ_S32(v1, arg3 * 0xe0c4 + 0x18), arg2);
        a0_2 = (void *)(intptr_t)*arg2;
        v0_4 = READ_S32(a0_2, 4);
        if (v0_4 > 0) {
            if (v0_4 != 1) {
                if (v0_4 != 2)
                    goto destroy;
                WRITE_S32(a0_2, 0x1c, 0);
                goto destroy;
            }
            WRITE_S32(a0_2, 0x18, 0);
            goto destroy;
        }
        WRITE_S32(a0_2, 0x14, 0);
    }

destroy:
    AL_Buffer_Destroy((AL_TBuffer *)READ_PTR(arg2, 0));
    return 0;
}

void *AL_Common_Encoder_SetTraceFolder(int32_t *arg1, int32_t arg2, int32_t arg3)
{
    int32_t v0 = *arg1;

    if (v0 == 0 || arg2 == 0) {
        __assert("pCtx && sPath", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c", 0x202,
                 "AL_Common_Encoder_SetTraceFolder", &_gp);
        return NULL;
    }

    WRITE_S32((void *)(intptr_t)v0, 0xf24c, arg2);
    WRITE_S32((void *)(intptr_t)v0, 0xf250, arg3);
    return (void *)(intptr_t)v0;
}

uint32_t *AL_Common_Encoder_SetTraceMode(int32_t *arg1, int32_t arg2, int32_t arg3, char arg4)
{
    uint32_t *const result = (uint32_t *)(intptr_t)*arg1;

    WRITE_S32(result, 0xf240, arg2);
    WRITE_S32(result, 0xf244, arg3);
    WRITE_U8(result, 0xf248, arg4);
    if (arg2 != 0)
        AL_CLEAN_BUFFERS = 1;
    return result;
}

uint32_t AL_Common_Encoder_SetEncodingOptions(void *arg1, uint32_t *arg2)
{
    void *v0_1 = READ_PTR(arg1, 0x14);
    uint32_t result;

    if (READ_U8(v0_1, 0x112) != 0)
        *arg2 |= 2;
    if (READ_U8(v0_1, 0x111) != 0)
        *arg2 |= 8;
    result = READ_U8(v0_1, 0x110);
    if (result != 0) {
        result = *arg2 | 0x10;
        *arg2 = result;
    }
    return result;
}

int32_t AL_Common_Encoder_GetLastError(int32_t *arg1)
{
    void *s0_1 = (uint8_t *)(intptr_t)*arg1;
    int32_t result;

    Rtos_GetMutex(READ_PTR(s0_1, 0xf254));
    result = READ_S32(s0_1, 0xed8c);
    Rtos_ReleaseMutex(READ_PTR(s0_1, 0xf254));
    return result;
}

uint32_t AL_Common_Encoder_SetHlsParam(void *arg1)
{
    uint32_t result = READ_U8(arg1, 0x3b);

    if (result != 0) {
        result = READ_U32(arg1, 0x28) | 2;
        WRITE_U32(arg1, 0x28, result);
    }
    return result;
}

int32_t AL_Common_SetError(int32_t *arg1, int32_t arg2)
{
    void *var_18 = &_gp;

    (void)var_18;
    Rtos_GetMutex(READ_PTR(arg1, 0xf254));
    WRITE_S32(arg1, 0xed8c, arg2);
    return Rtos_ReleaseMutex(READ_PTR(arg1, 0xf254));
}

int32_t AL_Common_Encoder_SetLoopFilterOffset(int32_t *arg1, int32_t arg2, int32_t arg3)
{
    void *s2 = (void *)(intptr_t)*arg1;
    void *s0 = READ_PTR(s2, 0x14);
    uint8_t *s0_1;
    void *a0_1;

    (void)_gp;
    if (READ_S32(s0, 0x124) > 0) {
        if (arg2 == 0) {
            s0_1 = (uint8_t *)s0 + 0x35;
            if (AL_ParamConstraints_CheckLFTcOffset(READ_S32(s0, 0x1c), arg3) == 0) {
                AL_Common_SetError((int32_t *)s2, 0x92);
                return 0;
            }
        } else {
            s0_1 = (uint8_t *)s0 + 0x34;
            if (AL_ParamConstraints_CheckLFBetaOffset(READ_S32(s0, 0x1c), arg3) == 0) {
                AL_Common_SetError((int32_t *)s2, 0x92);
                return 0;
            }
        }

        *s0_1 = (uint8_t)arg3;
        a0_1 = READ_PTR(s2, 0x14);
        WRITE_S32(s2, 0xdb1c, READ_S32(s2, 0xdb1c) | 0x400);
        WRITE_U8(s2, 0xdb82, READ_U8(a0_1, 0x34));
        WRITE_U8(s2, 0xdb83, READ_U8(READ_PTR(s2, 0x14), 0x35));
    }

    return 1;
}

int32_t AL_Common_Encoder_IsInitialQpProvided(int32_t *arg1)
{
    if (READ_S32(arg1, 0x68) == 0)
        return ((~READ_U8(arg1, 0x80)) >> 0xf) & 1;
    return 1;
}

void *AL_Common_Encoder_SetME(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int8_t *arg5)
{
    if ((int32_t)arg5[0x4a] < 0)
        arg5[0x4a] = (int8_t)arg1;
    if ((int32_t)arg5[0x4c] < 0)
        arg5[0x4c] = (int8_t)arg2;
    if ((int32_t)arg5[0x46] < 0)
        arg5[0x46] = (int8_t)arg3;
    if ((int32_t)arg5[0x48] < 0)
        arg5[0x48] = (int8_t)arg4;
    return arg5;
}

int32_t AL_Common_Encoder_ComputeRCParam(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int16_t *arg5)
{
    static const char table[] = "!(%')&-%3$8#>\"F!L S";
    int32_t s2 = arg1;
    int32_t s1_1;

    (void)_gp;
    if (AL_Common_Encoder_IsInitialQpProvided((int32_t *)arg5) == 0) {
        uint32_t t0_2 = (uint8_t)arg5[0x74 / 2];
        int32_t t1_1;
        int16_t v0_12;
        int32_t a0_6 = 0;
        int32_t a2_4;
        const uint16_t *v1_9;

        if (t0_2 == 0)
            __builtin_trap();
        t1_1 = arg3 << 5;
        v1_9 = (const uint16_t *)(table + t1_1 + (arg3 << 8));
        v0_12 = (int16_t)((const int8_t *)v1_9)[0x47];
        a2_4 = (uint8_t)arg5[2] * (uint8_t)arg5[3];
        if (a2_4 == 0)
            __builtin_trap();

        while (1) {
            int32_t v0_11 = (uint16_t)v1_9[0] < ((uint32_t)*(uint32_t *)((uint8_t *)arg5 + 0x78) / t0_2) * 0x3e8U / (uint32_t)a2_4;

            v1_9 += 1;
            if (v0_11 != 0) {
                a0_6 += 1;
                if (a0_6 != 0x23)
                    continue;
                if ((uint8_t)*(uint32_t *)((uint8_t *)arg5 + 0xac) >= 2)
                    v0_12 = (int16_t)(int8_t)table[1 + ((arg3 << 2) + t1_1 + a0_6) * 2];
                s1_1 = (int8_t)(arg4 + (int8_t)v0_12);
                *(int16_t *)((uint8_t *)arg5 + 0x80) = (int16_t)s1_1;
                break;
            }

            if ((*(uint32_t *)((uint8_t *)arg5 + 0xa8) & 8) != 0) {
                v0_12 = (int16_t)((int8_t)v0_12 - 6);
                s1_1 = (int8_t)v0_12;
                *(int16_t *)((uint8_t *)arg5 + 0x80) = (int16_t)s1_1;
                break;
            }

            if ((uint8_t)*(uint32_t *)((uint8_t *)arg5 + 0xac) >= 2)
                v0_12 = (int16_t)(int8_t)table[1 + ((arg3 << 2) + t1_1 + a0_6) * 2];
            s1_1 = (int8_t)(arg4 + (int8_t)v0_12);
            *(int16_t *)((uint8_t *)arg5 + 0x80) = (int16_t)s1_1;
            break;
        }
    } else {
        int32_t v1_1 = *(int32_t *)((uint8_t *)arg5 + 0x68);
        int32_t v0_1 = *(int16_t *)((uint8_t *)arg5 + 0x82);
        int32_t t0_1;
        int32_t a3;
        int32_t a1;
        int32_t a0_3;
        int32_t a0_4;
        int32_t v0_3;
        int32_t v0_4;
        int16_t a1_2;
        int16_t result = -5;

        s1_1 = *(int16_t *)((uint8_t *)arg5 + 0x80);
        if (v1_1 != 0)
            t0_1 = 0xa;
        else
            t0_1 = v0_1;
        if (v1_1 != 0 && v0_1 < 0xa) {
            *(int16_t *)((uint8_t *)arg5 + 0x82) = 0xa;
            v0_1 = 0xa;
            t0_1 = 0xa;
        }

        a3 = *(int16_t *)((uint8_t *)arg5 + 0x84);
        a1 = arg2;
        if (arg2 >= s2)
            a1 = s2;
        a0_3 = -a1;
        if (a3 < v0_1)
            a3 = t0_1;
        v0_3 = a3;
        if (s2 < arg2)
            s2 = arg2;
        if (a0_3 < t0_1)
            a0_3 = t0_1;
        a0_4 = (int16_t)a0_3;
        if (v0_3 >= 0x33 - s2)
            v0_3 = 0x33 - s2;
        v0_4 = (int16_t)v0_3;
        a1_2 = (int16_t)a0_4;
        *(int16_t *)((uint8_t *)arg5 + 0x82) = (int16_t)a0_4;
        *(int16_t *)((uint8_t *)arg5 + 0x84) = (int16_t)v0_4;
        if (s1_1 >= a0_4) {
            if (s1_1 >= v0_4)
                s1_1 = v0_4;
            a1_2 = (int16_t)s1_1;
        }
        *(int16_t *)((uint8_t *)arg5 + 0x80) = a1_2;
        if ((((v1_1 - 4) & 0xfffffffb) == 0)) {
            int32_t v0_5 = *(int32_t *)((uint8_t *)arg5 + 0x10);
            int32_t a1_5 = (uint8_t)arg5[2] * (uint8_t)arg5[3];
            int32_t v1_5 = (int32_t)((uint32_t)v0_5 >> 4) & 0xf;
            int32_t v0_6 = v0_5 & 0xf;

            if (v1_5 >= v0_6)
                v0_6 = v1_5;
            result = 0xff;
            if (v0_6 != 8)
                result = 0x3ff;
            *(int16_t *)((uint8_t *)arg5 + 0x9a) = result;
            *(int32_t *)((uint8_t *)arg5 + 0x94) = a1_5;
        }
        return result;
    }

    return -5;
}

int32_t AL_Common_Encoder_Destroy(int32_t **arg1)
{
    void *s0 = *arg1;

    (void)_gp;
    if (s0 != NULL) {
        void *v0_1 = READ_PTR(s0, 0x14);
        int32_t a0 = READ_S32(v0_1, 0xec);

        if (a0 != 0) {
            watermark_deinit(a0);
            v0_1 = READ_PTR(s0, 0x14);
            WRITE_S32(v0_1, 0xe8, 0);
            WRITE_S32(v0_1, 0xec, 0);
        }
        if (READ_S32(v0_1, 0x124) != 0)
            destroyChannels_part_6((int32_t *)s0);
        MemDesc_Free((uint8_t *)s0 + 0xf268);
        Rtos_Free(s0);
    }

    Rtos_Free(arg1);
    return 0;
}

/* Stock signature: int32_t AL_Common_Encoder_Create(int32_t arg1)
 * Returns an int32_t (pointer-to-wrapper). The wrapper is a 4-byte heap
 * block whose first word points at the 0xf280-byte encoder state.
 * Callers read (*result) as the state pointer. */
int32_t AL_Common_Encoder_Create(int32_t arg1)
{
    (void)_gp;

    { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[96]; int n = snprintf(b, sizeof(b), "libimp/ENC: Com_Encoder_Create ENTRY alloc=0x%x\n", arg1); if (n>0) write(kfd, b, n); close(kfd); } }
    int32_t **wrapper = (int32_t **)Rtos_Malloc(4);
    if (wrapper == NULL)
        return 0;
    Rtos_Memset(wrapper, 0, 4);

    int32_t *v0 = (int32_t *)Rtos_Malloc(0xf280);
    *wrapper = v0;
    if (v0 != NULL) {
        Rtos_Memset(v0, 0, 0xf280);
        { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[96]; int n = snprintf(b, sizeof(b), "libimp/ENC: pre-MemDesc_Alloc v0=%p\n", (void*)v0); if (n>0) write(kfd, b, n); close(kfd); } }
        if (MemDesc_Alloc((uint8_t *)v0 + 0xf268, arg1, 0x754) != 0) {
            { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: MemDesc_Alloc OK\n"; write(kfd, m, strlen(m)); close(kfd); } }
            int32_t a0_5 = READ_S32(v0, 0xf268);
            int32_t t0_1 = READ_S32(v0, 0xf274);
            int32_t a3_1 = READ_S32(v0, 0xf26c);
            int32_t a2_1 = READ_S32(v0, 0xf270);

            WRITE_S32(v0, 0x14,   a0_5);
            WRITE_S32(v0, 0xe0c4, t0_1);
            WRITE_S32(v0, 0xe0b8, a0_5);
            WRITE_S32(v0, 0xe0bc, a3_1);
            WRITE_S32(v0, 0xe0c0, 0x754);
            Rtos_Memset((void *)(intptr_t)a0_5, 0, (size_t)a2_1);
            { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[96]; int n = snprintf(b, sizeof(b), "libimp/ENC: Com_Encoder_Create OK wrap=%p\n", (void*)wrapper); if (n>0) write(kfd, b, n); close(kfd); } }
            return (int32_t)(intptr_t)wrapper;
        }
        { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: MemDesc_Alloc FAIL\n"; write(kfd, m, strlen(m)); close(kfd); } }
        AL_Common_Encoder_Destroy((int32_t **)wrapper);
    }

    return 0;
}

int32_t AL_Common_Encoder_RestartGop(int32_t *arg1)
{
    void *a0 = (void *)(intptr_t)*arg1;
    void *a1 = READ_PTR(a0, 0x14);

    (void)_gp;
    if (READ_S32(a1, 0x124) <= 0)
        return 1;
    if (((READ_S32(a1, 0xa8) - 0x10) & 0xffffffef) != 0) {
        WRITE_S32(a0, 0xdb1c, READ_S32(a0, 0xdb1c) | 8);
        if (READ_S32(a1, 0x124) < 2)
            return 1;
    }
    AL_Common_SetError((int32_t *)a0, 0x91);
    return 0;
}

int32_t AL_Common_Encoder_SetGopLength(int32_t *arg1, int32_t arg2)
{
    void *s0 = (void *)(intptr_t)*arg1;
    void *v0 = READ_PTR(s0, 0x14);

    (void)_gp;
    if (READ_S32(v0, 0x124) > 0) {
        if ((READ_S32(v0, 0xa8) & 2) != 0) {
            WRITE_S32(v0, 0xac, arg2);
            setNewParams((int32_t *)s0, 0);
            if ((READ_S32(v0, 0xa8) & 2) == 0 || READ_S32(READ_PTR(s0, 0x14), 0x124) >= 2) {
                AL_Common_SetError((int32_t *)s0, 0x91);
                return 0;
            }
        }
    }

    return 1;
}

int32_t AL_Common_Encoder_SetGopNumB(int32_t *arg1, int32_t arg2)
{
    void *s0 = (void *)(intptr_t)*arg1;
    void *v0 = READ_PTR(s0, 0x14);

    (void)_gp;
    if (READ_S32(v0, 0x124) > 0) {
        if ((READ_S32(v0, 0xa8) & 2) == 0) {
            AL_Common_SetError((int32_t *)s0, 0x91);
            return 0;
        }
        if (READ_S32(s0, 0xf260) < arg2) {
            AL_Common_SetError((int32_t *)s0, 0x92);
            return 0;
        }
        WRITE_U8(v0, 0xae, arg2);
        setNewParams((int32_t *)s0, 0);
        v0 = READ_PTR(s0, 0x14);
        if (READ_S32(v0, 0x124) >= 2) {
            if ((READ_S32(v0, 0x198) & 2) == 0) {
                AL_Common_SetError((int32_t *)s0, 0x91);
                return 0;
            }
        }
    }

    return 1;
}

int32_t AL_Common_Encoder_SetFreqIDR(int32_t *arg1, int32_t arg2)
{
    void *s0 = (void *)(intptr_t)*arg1;
    void *v0 = READ_PTR(s0, 0x14);

    (void)_gp;
    if (READ_S32(v0, 0x124) > 0) {
        if ((READ_S32(v0, 0xa8) & 2) == 0) {
            AL_Common_SetError((int32_t *)s0, 0x91);
            return 0;
        }
        if (arg2 < -1) {
            AL_Common_SetError((int32_t *)s0, 0x92);
            return 0;
        }
        WRITE_S32(v0, 0xb0, arg2);
        setNewParams((int32_t *)s0, 0);
        if (READ_S32(READ_PTR(s0, 0x14), 0x124) >= 2) {
            AL_Common_SetError((int32_t *)s0, 0x91);
            return 0;
        }
    }

    return 1;
}

int32_t AL_Common_Encoder_SetBitRate(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    void *a0 = (void *)(intptr_t)*arg1;
    int32_t t2 = arg4 << 4;
    int32_t t0 = arg4 << 8;
    void *v1 = READ_PTR(a0, 0x14);
    uint8_t *v0_1 = (uint8_t *)v1 + t0 - t2;
    int32_t t1 = READ_S32(v0_1, 0x68);

    (void)_gp;
    WRITE_S32(v0_1, 0x78, arg2);
    if (t1 == 1)
        WRITE_S32(v0_1, 0x7c, arg2);
    if (((t1 - 2) & 0xfffffffd) == 0)
        WRITE_S32((uint8_t *)v1 + t0 - t2, 0x7c, arg3);
    if (t1 == 8)
        WRITE_S32((uint8_t *)v1 + t0 - t2, 0x7c, arg3);
    setNewParams((int32_t *)a0, arg4);
    return 1;
}

int32_t AL_Common_Encoder_GetFrameRate(int32_t *arg1, int16_t *arg2, int16_t *arg3)
{
    void *v1 = READ_PTR((void *)(intptr_t)*arg1, 0x14);

    *arg2 = READ_U16(v1, 0x74);
    *arg3 = READ_U16(v1, 0x76);
    return 1;
}

int32_t AL_Common_Encoder_SetFrameRate(int32_t *arg1, uint8_t arg2, int16_t arg3)
{
    void *a0 = (void *)(intptr_t)*arg1;
    uint32_t a1 = arg2;

    (void)_gp;
    if (READ_U32(a0, 0xf264) < a1) {
        AL_Common_SetError((int32_t *)a0, 0x92);
        return 0;
    }

    {
        void *v0_3 = READ_PTR(a0, 0x14);

        if (READ_S32(v0_3, 0x124) > 0)
            return 1;
        WRITE_U16(v0_3, 0x74, a1);
        WRITE_S16(v0_3, 0x76, arg3);
        setNewParams((int32_t *)a0, 0);
        return 1;
    }
}

int32_t AL_Common_Encoder_SetQP(int32_t *arg1, int16_t arg2)
{
    void *a0 = (void *)(intptr_t)*arg1;
    void *v1 = READ_PTR(a0, 0x14);
    int32_t v0_1;
    int32_t a1;

    (void)_gp;
    if (READ_S32(v1, 0x124) > 0) {
        v0_1 = READ_S32(v1, 0x68);
        a1 = arg2;
        if (v0_1 != 0x3f && (uint32_t)(v0_1 - 1) >= 2U) {
            AL_Common_SetError((int32_t *)a0, 0x91);
            return 0;
        }
        if (a1 < READ_S16(v1, 0x82) || READ_S16(v1, 0x84) < a1) {
            AL_Common_SetError((int32_t *)a0, 0x92);
            return 0;
        }
        WRITE_S16(a0, 0xdb80, a1);
        WRITE_S32(a0, 0xdb1c, READ_S32(a0, 0xdb1c) | 0x100);
    }

    return 1;
}

int32_t AL_Common_Encoder_SetQPBounds(int32_t *arg1, int16_t arg2, int16_t arg3)
{
    void *a0 = (void *)(intptr_t)*arg1;
    void *v0 = READ_PTR(a0, 0x14);

    if (READ_S32(v0, 0x124) <= 0)
        return 1;
    (void)_gp;
    WRITE_S16(v0, 0x82, arg2);
    WRITE_S16(v0, 0x84, arg3);
    setNewParams((int32_t *)a0, 0);
    return 1;
}

int32_t AL_Common_Encoder_SetQPIPDelta(int32_t *arg1, int16_t arg2)
{
    void *a0 = (void *)(intptr_t)*arg1;
    void *v0 = READ_PTR(a0, 0x14);

    if (READ_S32(v0, 0x124) <= 0)
        return 1;
    (void)_gp;
    WRITE_S16(v0, 0x86, arg2);
    setNewParams((int32_t *)a0, 0);
    return 1;
}

int32_t AL_Common_Encoder_SetQPPBDelta(int32_t *arg1, int16_t arg2)
{
    void *a0 = (void *)(intptr_t)*arg1;
    void *v0 = READ_PTR(a0, 0x14);

    if (READ_S32(v0, 0x124) <= 0)
        return 1;
    (void)_gp;
    WRITE_S16(v0, 0x88, arg2);
    setNewParams((int32_t *)a0, 0);
    return 1;
}

int32_t AL_Common_Encoder_SetInputResolution(int32_t *arg1, int32_t arg2, int32_t arg3)
{
    void *s1 = (void *)(intptr_t)*arg1;
    void *s5 = READ_PTR(s1, 0x14);
    int32_t s0_1;
    int32_t s2_1;

    if (READ_S32(s5, 0x124) > 0) {
        if (((READ_S32(s5, 0xa8) - 0x10) & 0xffffffef) == 0) {
            AL_Common_SetError((int32_t *)s1, 0x91);
            return 0;
        }
        s0_1 = arg2 & 0xffff;
        s2_1 = arg3 & 0xffff;
        if (AL_ParamConstraints_CheckResolution(READ_S32(s5, 0x1c), (READ_S32(s5, 0x10) >> 8) & 0xf, s0_1, s2_1,
                                                &_gp) != 0) {
            AL_Common_SetError((int32_t *)s1, 0x91);
            return 0;
        }
        if (AL_SrcBuffersChecker_UpdateResolution((uint8_t *)s1 + 0xdaf0, arg2, arg3) == 0) {
            AL_Common_SetError((int32_t *)s1, 0x91);
            return 0;
        }

        WRITE_S16(s5, 4, arg2);
        WRITE_S16(s5, 6, arg3);
        WRITE_S32(s1, 0xdb84, arg2);
        WRITE_S32(s1, 0xdb88, arg3);
        WRITE_S32(s1, 0xdb1c, READ_S32(s1, 0xdb1c) | 0x208);

        {
            int32_t *v1_3 = (int32_t *)((uint8_t *)s1 + 0xe0dc);
            int32_t v0_9 = 0;
            uint8_t v1_4;

            while (1) {
                int32_t a0_3 = *v1_3;

                if (a0_3 == 0) {
                    v1_4 = 0;
                    v0_9 = 0;
                    break;
                }
                if (a0_3 != s0_1) {
                    v0_9 += 1;
                } else {
                    v0_9 += 1;
                    if (v1_3[1] == s2_1) {
                        v1_4 = (uint8_t)(v0_9 - 1);
                        break;
                    }
                }
                v1_3 = &v1_3[2];
                if (v0_9 == 0xf) {
                    v1_4 = 0;
                    v0_9 = 0;
                    break;
                }
            }

            if (v0_9 == 0) {
                void *v0_18 = (uint8_t *)s1 + ((v0_9 + 0x1c1a) << 3);

                WRITE_S32(v0_18, 0xc, s0_1);
                WRITE_S32(v0_18, 0x10, s2_1);
            }
            WRITE_U8(s1, 0xdb8c, v1_4);
        }

        if (READ_S32(READ_PTR(s1, 0x14), 0x124) >= 2) {
            AL_Common_SetError((int32_t *)s1, 0x91);
            return 0;
        }
    }

    return 1;
}

int32_t AL_Common_Encoder_SetLoopFilterBetaOffset(int32_t *arg1, int8_t arg2)
{
    return AL_Common_Encoder_SetLoopFilterOffset(arg1, 1, arg2);
}

int32_t AL_Common_Encoder_SetLoopFilterTcOffset(int32_t *arg1, int8_t arg2)
{
    return AL_Common_Encoder_SetLoopFilterOffset(arg1, 0, arg2);
}

int32_t AL_Common_Encoder_SetHDRSEIs(int32_t *arg1, int32_t *arg2, int32_t arg3)
{
    void *a0 = (void *)(intptr_t)*arg1;
    int32_t v1_2;

    (void)_gp;
    if (READ_U32(a0, 0xed84) == 0) {
        AL_Common_SetError((int32_t *)a0, 0x91);
        return 0;
    }

    v1_2 = READ_S32(READ_PTR(a0, 0x14), 0xf8);
    arg3 = v1_2 & 8;
    if (arg3 != 0) {
        WRITE_S32(a0, 0xed5c, arg2[0]);
        WRITE_S32(a0, 0xed60, arg2[1]);
        WRITE_S32(a0, 0xed64, arg2[2]);
        WRITE_S32(a0, 0xed68, arg2[3]);
        WRITE_S32(a0, 0xed6c, arg2[4]);
        WRITE_S32(a0, 0xed70, arg2[5]);
        WRITE_S32(a0, 0xed74, arg2[6]);
        if ((v1_2 & 0x10) == 0)
            return 1;
        goto label_55664;
    }

    if ((v1_2 & 0x10) != 0) {
label_55664:
        WRITE_U8(a0, 0xed78, ((uint8_t *)arg2)[0x1c]);
        WRITE_S16(a0, 0xed7a, *(int16_t *)((uint8_t *)arg2 + 0x1e));
        WRITE_S32(a0, 0xed7c, *(int32_t *)((uint8_t *)arg2 + 0x20));
    }

    return 1;
}

int32_t AL_Common_Encoder_GetRcParam(int32_t *arg1, void *arg2)
{
    (void)_gp;
    Rtos_Memcpy(arg2, (uint8_t *)READ_PTR((void *)(intptr_t)*arg1, 0x14) + 0x68, 0x40);
    return 1;
}

int32_t AL_Common_Encoder_SetRcParam(int32_t *arg1, void *arg2)
{
    void *s1 = (void *)(intptr_t)*arg1;
    int32_t i = 0;
    void *v0_2;

    (void)_gp;
    if (READ_U32(s1, 0xf264) < READ_U32(arg2, 0xc)) {
        AL_Common_SetError((int32_t *)s1, 0x92);
        return 0;
    }

    v0_2 = READ_PTR(s1, 0x14);
    if (READ_S32(v0_2, 0x124) > 0) {
        do {
            Rtos_Memcpy((uint8_t *)v0_2 + i * 0xf0 + 0x68, arg2, 0x40);
            setNewParams((int32_t *)s1, i);
            v0_2 = READ_PTR(s1, 0x14);
            i += 1;
        } while (i < READ_S32(v0_2, 0x124));
    }

    return 1;
}

int32_t AL_Common_Encoder_GetGopParam(int32_t *arg1, void *arg2)
{
    (void)_gp;
    Rtos_Memcpy(arg2, (uint8_t *)READ_PTR((void *)(intptr_t)*arg1, 0x14) + 0xa8, 0x1c);
    return 1;
}

int32_t AL_Common_Encoder_SetGopParam(int32_t *arg1, void *arg2)
{
    void *s1 = (void *)(intptr_t)*arg1;
    void *v0 = READ_PTR(s1, 0x14);
    int32_t s2_1;

    (void)_gp;
    if (READ_S32(v0, 0x124) > 0) {
        s2_1 = READ_S32(v0, 0xa8);
        if (s2_1 != 2) {
            AL_Common_SetError((int32_t *)s1, 0x91);
            return 0;
        }
        if (READ_S32(s1, 0xf260) < READ_U8(arg2, 6)) {
            AL_Common_SetError((int32_t *)s1, 0x92);
            return 0;
        }
        WRITE_U8(arg2, 7, READ_U8(v0, 0xaf));
        Rtos_Memcpy((uint8_t *)arg2 + 0x18, (uint8_t *)READ_PTR(s1, 0x14) + 0xc0, 4);
        WRITE_S32(arg2, 0x14, READ_S32(READ_PTR(s1, 0x14), 0xbc));
        Rtos_Memcpy((uint8_t *)READ_PTR(s1, 0x14) + 0xa8, arg2, 0x1c);
        setNewParams((int32_t *)s1, 0);
        {
            void *v1_3 = READ_PTR(s1, 0x14);

            if (READ_S32(v1_3, 0x124) >= 2) {
                if (READ_S32(v1_3, 0x198) != s2_1) {
                    AL_Common_SetError((int32_t *)s1, 0x91);
                    return 0;
                }
            }
        }
    }

    return 1;
}

int32_t AL_GetAllocSize_MV(int32_t arg1, int32_t arg2, char arg3, int32_t arg4); /* forward decl */
int32_t AL_GetAllocSize_CompData(int32_t arg1, int32_t arg2, char arg3, char arg4, int32_t arg5,
                                 int32_t arg6); /* forward decl */
int32_t AL_GetAllocSize_EncCompMap(int32_t arg1, int32_t arg2, char arg3, char arg4, char arg5); /* forward decl */
int32_t AL_GetAllocSize_WPP(int32_t arg1, int32_t arg2, char arg3); /* forward decl */
int32_t AL_GetAllocSize_SliceSize(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4); /* forward decl */
int32_t AL_GetAllocSizeEP1(void); /* forward decl */
int32_t AL_GetAllocSizeEP2(int32_t arg1, int32_t arg2, int32_t arg3); /* forward decl */
int32_t AL_GetAllocSizeEP3PerCore(void); /* forward decl */
int32_t AL_GetAllocSizeSRD(int32_t arg1, int16_t arg2, char arg3); /* forward decl */
int32_t *GetPicDimFromCmdRegsEnc1(int32_t *arg1, void *arg2); /* forward decl */
uint32_t AL_PixMapBuffer_GetPlanePhysicalAddress(AL_TBuffer *arg1, int32_t arg2); /* forward decl */
int32_t AL_PixMapBuffer_GetPlanePitch(AL_TBuffer *arg1, int32_t arg2); /* forward decl */
uint32_t AL_PixMapBuffer_GetFourCC(AL_TBuffer *arg1); /* forward decl */
uint32_t AL_IsCompressed(uint32_t tFourCC); /* forward decl */
int32_t AL_Fifo_Init(int32_t *arg1, int32_t arg2); /* forward decl */
int32_t AL_Fifo_Dequeue(int32_t *arg1, int32_t arg2); /* forward decl */
int32_t AL_Fifo_Queue(int32_t *arg1, void *arg2, int32_t arg3); /* forward decl */
void *Rtos_CreateSemaphore(int32_t initial_count); /* forward decl */
int32_t Rtos_ReleaseSemaphore(void *sem); /* forward decl */
void *Rtos_CreateMutex(void); /* forward decl */
uint32_t AL_CleanupMemory(int32_t arg1, int32_t arg2); /* forward decl */
int32_t LoadLambdaFromFile(char *arg1, int32_t *arg2); /* forward decl */
int32_t LoadCustomLda(int32_t *arg1); /* forward decl */
void *watermark_init(void); /* forward decl */
int32_t MemDesc_AllocNamed(void *arg1, void *arg2, int32_t arg3, char *arg4); /* forward decl */
int32_t AL_SrcBuffersChecker_CanBeUsed(int32_t *arg1, AL_TBuffer *arg2); /* forward decl */
void AL_SrcBuffersChecker_Init(void *arg1, void *arg2); /* forward decl */
void AL_HDRSEIs_Reset(uint8_t *arg1); /* forward decl */
int32_t IMP_Log_Get_Option(void); /* forward decl */
void imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl */
void *memset(void *s, int c, size_t n); /* forward decl */
int32_t AL_EncTrace_TraceInputsFifo(char *arg1, char *arg2, int32_t arg3, char arg4, char arg5, char arg6, int32_t arg7,
                                    int32_t arg8, int32_t arg9, void *arg10); /* forward decl */
uint32_t AL_EncTrace_TraceOutputsFifo(char *arg1, char *arg2, int32_t arg3, char arg4, char arg5, char arg6, char arg7,
                                      int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11, void *arg12); /* forward decl */
int32_t AL_EncTrace_TraceStatus(char *arg1, char *arg2, int32_t arg3, char arg4, char arg5, char arg6, char arg7,
                                int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11, void *arg12); /* forward decl */
int32_t AL_EncTrace_TraceJpeg(char *arg1, char *arg2, void *arg3, int32_t *arg4); /* forward decl */
int32_t AL_EncTrace_TraceJpegStatus(char *arg1, char *arg2, int32_t arg3, void *arg4); /* forward decl */

static int32_t TracedBufferToSliceBuffer(int32_t *arg1, void *arg2, int32_t *arg3, int32_t arg4)
{
    int32_t var_40 = 0;
    int32_t var_3c = 0;
    int32_t var_58;
    int32_t var_54;
    int32_t var_50;
    int32_t var_4c;
    int32_t *(*fill_rec)(int32_t *, void *, int32_t, int32_t, int32_t);
    void *s3;
    int32_t v0_3;
    int32_t s5;
    int32_t s4_1;
    void *s0_1;
    int32_t s7_1;
    int32_t s6_1;
    int32_t v1_2;
    int32_t t0;
    int32_t v0_6;
    int32_t a3_5;
    int32_t t0_2;
    int32_t v1_4;
    int32_t v0_8;
    int32_t t1;
    int32_t a3_7;
    uint32_t t0_6;
    int32_t var_38_3;
    int32_t v0_10;
    int32_t a0_9;
    int32_t t1_1;
    int32_t t2_1;
    int32_t v0_13;
    int32_t a0_12;
    int32_t s0_2;
    int32_t s6_2;
    int32_t v0_14;
    int32_t v1_9;
    int32_t v0_15;
    void *s0_4;
    int32_t v0_16;
    int32_t a1_12;
    int32_t s4_2;
    void *s3_1;
    int32_t s3_2;
    int32_t s0_8;
    int32_t v0_27;
    int32_t a0_16;
    int32_t v1_15;
    int32_t a0_17;
    int32_t result;
    int32_t v1_16;
    int32_t a2_10;
    int32_t a1_14;
    uint32_t i;

    Rtos_Memset(arg3, 0, 0x108);
    s3 = READ_PTR(arg1[0x1e], 0x14);
    GetPicDimFromCmdRegsEnc1(&var_40, *(void **)((uint8_t *)READ_PTR(arg2, 8) + (READ_U8(arg2, 0x11) << 2)));
    v0_3 = READ_S32(arg2, 0xd0);
    s5 = arg4 << 4;
    s4_1 = arg4 << 8;
    s0_1 = (uint8_t *)s3 + s4_1 - s5;
    s7_1 = var_40 << 3;
    s6_1 = var_3c << 3;
    arg3[0] = READ_S32(arg2, 0xc0);
    fill_rec = (int32_t *(*)(int32_t *, void *, int32_t, int32_t, int32_t))(intptr_t)FillRec;
    var_40 = s7_1;
    FillRec(&arg3[0x11], s0_1, s7_1, s6_1, v0_3);
    fill_rec(&arg3[1], s0_1, var_40, s6_1, READ_S32(arg2, 0xc8));
    var_58 = s7_1;
    var_54 = s6_1;
    fill_rec(&arg3[9], s0_1, s7_1, s6_1, READ_S32(arg2, 0xcc));
    v1_2 = READ_S32(arg2, 0xd4);
    t0 = READ_S32(arg2, 0x78);
    var_50 = s7_1;
    var_4c = s6_1;
    v0_6 = AL_GetAllocSize_MV(s7_1, s6_1, READ_U8(s0_1, 0x4e), READ_U32(s0_1, 0x1c) >> 0x18);
    a3_5 = READ_S32(s0_1, 0x1c);
    arg3[0x1a] = t0;
    arg3[0x19] = v1_2;
    t0_2 = READ_S32(arg2, 0x7c);
    v1_4 = READ_S32(arg2, 0xd8);
    arg3[0x1b] = v0_6;
    arg3[0x20] = AL_GetAllocSize_MV(s7_1, s6_1, READ_U8(s0_1, 0x4e), (uint32_t)a3_5 >> 0x18);
    arg3[0x1e] = v1_4;
    arg3[0x1f] = t0_2;
    v0_8 = READ_S32(s0_1, 0x10);
    t1 = v0_8 & 0xf;
    a3_7 = ((uint32_t)v0_8 >> 4) & 0xf;
    t0_6 = (uint32_t)(uint8_t)(1 << (READ_U8(s0_1, 0x4e) & 0x1f));
    if (a3_7 < t1)
        a3_7 = t1;
    var_38_3 = 1;
    v0_10 = AL_GetAllocSize_CompData(var_40, s6_1, (char)t0_6, (char)a3_7, (v0_8 >> 8) & 0xf, 1);
    a0_9 = READ_S32(arg2, 0x90);
    t1_1 = READ_S32(arg2, 0xdc);
    arg3[0x23] = READ_S32(arg2, 0xe0);
    arg3[0x24] = a0_9;
    t2_1 = READ_S32(arg2, 0x8c);
    arg3[0x25] = v0_10;
    arg3[0x2a] = AL_GetAllocSize_EncCompMap(var_40, s6_1, (char)t0_6, READ_U8(arg2, 0x13), (char)var_38_3);
    arg3[0x29] = t2_1;
    arg3[0x28] = t1_1;
    if (READ_U8(s0_1, 0x3e) == 0) {
        uint32_t a0_18 = READ_U8(s0_1, 0x4e);

        v0_13 = AL_GetAllocSize_WPP(((var_38_3 << (a0_18 & 0x1f)) + s6_1 - 1) >> (a0_18 & 0x1f),
                                    READ_U8(arg2, 0x14), READ_U8(arg2, 0x13));
    } else {
        v0_13 = AL_GetAllocSize_SliceSize(s7_1, s6_1, READ_U8(arg2, 0x14), READ_U8(s0_1, 0x4e));
    }
    a0_12 = READ_S32(arg2, 0xa8);
    arg1[0xd] = READ_S32(arg2, 0x80);
    arg1[0xc] = a0_12;
    arg1[0xe] = v0_13;
    arg3[0x2d] = (int32_t)(intptr_t)&arg1[0xc];
    s0_2 = READ_S32(arg2, 0xb0);
    s6_2 = READ_S32(arg2, 0x84);
    v0_14 = AL_GetAllocSizeEP1();
    v1_9 = READ_S32(s3, 0x10c);
    arg1[1] = s6_2;
    arg1[0] = s0_2;
    arg1[2] = v0_14;
    arg3[0x2e] = (int32_t)(intptr_t)arg1;
    arg1[5] = v1_9 != 0 ? 0x11 : 1;
    v0_15 = READ_S32(arg2, 0x3c);
    arg1[6] = READ_S32(arg2, 0x40);
    s0_4 = (uint8_t *)s3 + s4_1 - s5;
    arg1[7] = v0_15;
    v0_16 = AL_GetAllocSizeEP2(var_40, s6_1, READ_U8(s0_4, 0x1f));
    a1_12 = READ_S32(s0_4, 0x68);
    arg1[8] = v0_16;
    arg3[0x2f] = (int32_t)(intptr_t)&arg1[6];

    if (a1_12 == 3 || READ_S32(s0_4, 0xa4) != 0 || READ_S32(s0_4, 0xa0) != 0 || READ_S32(s0_4, 0x9c) != 0) {
        i = 0;
        if (READ_U8(arg2, 0x13) != 0) {
            do {
                int32_t v0_20 = (int32_t)i * AL_GetAllocSizeEP3PerCore();
                uint8_t *s0_7 = (uint8_t *)&arg3[i * 6];

                i = (uint8_t)i + 1;
                WRITE_S32(s0_7, 0xc0, READ_S32(arg2, 0xac) + v0_20);
                WRITE_S32(s0_7, 0xc4, READ_S32(arg2, 0x88) + v0_20);
                WRITE_S32(s0_7, 0xc8, AL_GetAllocSizeEP3PerCore());
                WRITE_S32(s0_7, 0xd4, 3);
            } while (i < READ_U8(arg2, 0x13));
        }
        s4_2 = s4_1 - s5;
    } else {
        s4_2 = s4_1 - s5;
    }

    s3_1 = (uint8_t *)s3 + s4_2;
    if ((READ_S32(s3_1, 0x2c) & 0x8000) != 0) {
        s3_2 = READ_S32(arg2, 0xb8);
        s0_8 = READ_S32(arg2, 0xb4);
        v0_27 = AL_GetAllocSizeSRD(var_40, (int16_t)s6_1, READ_U8(s3_1, 0x4e));
        a0_16 = arg1[0x22];
        v1_15 = arg1[0x23];
        arg1[0x20] = s0_8;
        arg1[0x1f] = s3_2;
        arg1[0x21] = v0_27;
        arg3[0x36] = s3_2;
        arg3[0x37] = s0_8;
        arg3[0x38] = v0_27;
        arg3[0x39] = a0_16;
        arg3[0x3a] = v1_15;
    }

    a0_17 = READ_S32(arg2, 0xf0);
    result = READ_S32(arg2, 0xf8);
    v1_16 = READ_S32(arg2, 0xf4);
    a2_10 = arg1[0x1a];
    a1_14 = arg1[0x1b];
    arg1[0x18] = v1_16;
    arg1[0x17] = a0_17;
    arg1[0x19] = result;
    arg1[0x1d] = result;
    arg1[0x1c] = 0;
    arg3[0x3b] = a0_17;
    arg3[0x3c] = v1_16;
    arg3[0x3d] = result;
    arg3[0x3e] = a2_10;
    arg3[0x3f] = a1_14;
    arg3[0x40] = 0;
    arg3[0x41] = result;
    return result;
}

static uint32_t trace(int32_t *arg1, int32_t arg2)
{
    int32_t s1 = arg1[0];
    int32_t v1 = READ_S32((void *)(intptr_t)s1, 0xf240);
    int32_t a0 = arg1[7];
    int32_t s2_1 = arg1[6] & 8;
    int32_t s3_1;
    int32_t var_1b0[0x42];
    int32_t var_a8[0x0f];
    uint32_t result;

    if (v1 == 3) {
        s3_1 = a0 | 0x10000000;
    } else {
        s3_1 = a0;
        if (v1 == 4) {
            if (a0 == READ_S32((void *)(intptr_t)s1, 0xf244))
                s3_1 = a0 | 0x10000000;
        } else if (v1 == 5) {
            s3_1 = a0 | 0x20000000;
        }
    }

    TracedBufferToSliceBuffer(var_a8, arg1, var_1b0, arg2);
    if (arg1[6] != 0) {
        if (s2_1 == 0) {
            int32_t v1_1 = s3_1 & 0xf0000000;

            if (v1_1 == 0x10000000) {
                int32_t var_1c0 = arg1[2];
                int32_t var_1c4 = (uint32_t)arg1[6] >> 2 & 1;

                AL_EncTrace_TraceOutputsFifo((char *)(intptr_t)READ_S32((void *)(intptr_t)s1, 0xf24c),
                                             (char *)(intptr_t)READ_S32((void *)(intptr_t)s1, 0xf250),
                                             s3_1 & 0x0fffffff, (char)arg1[4], (char)arg1[0x11], (char)arg1[0x13],
                                             (char)arg1[0x12], (uint16_t)arg1[5], ((uint32_t)arg1[6] >> 1) & 1, var_1c4,
                                             var_1c0, var_1b0);
                if ((arg1[6] & 4) != 0) {
                    AL_EncTrace_TraceStatus((char *)(intptr_t)READ_S32((void *)(intptr_t)s1, 0xf24c),
                                            (char *)(intptr_t)READ_S32((void *)(intptr_t)s1, 0xf250),
                                            s3_1 & 0x0fffffff, (char)arg1[4], (char)arg1[0x11], (char)arg1[0x13],
                                            (char)arg1[0x12], (uint16_t)arg1[5], arg1[2], var_1c4, var_1c0, var_1b0);
                }
            } else if (v1_1 == 0x20000000) {
                int32_t var_1c0 = arg1[2];
                int32_t var_1c4 = (uint32_t)arg1[6] >> 2 & 1;

                if ((arg1[6] & 4) != 0) {
                    AL_EncTrace_TraceStatus((char *)(intptr_t)READ_S32((void *)(intptr_t)s1, 0xf24c),
                                            (char *)(intptr_t)READ_S32((void *)(intptr_t)s1, 0xf250),
                                            s3_1 & 0x0fffffff, (char)arg1[4], (char)arg1[0x11], (char)arg1[0x13],
                                            (char)arg1[0x12], (uint16_t)arg1[5], arg1[2], var_1c4, var_1c0, var_1b0);
                }
            } else if (arg1[6] == 9) {
                int32_t v0_7 = s3_1 & 0xf0000000;

                if (v0_7 == 0x10000000) {
                    AL_EncTrace_TraceJpeg((char *)(intptr_t)READ_S32((void *)(intptr_t)s1, 0xf24c),
                                          (char *)(intptr_t)READ_S32((void *)(intptr_t)s1, 0xf250),
                                          (void *)(intptr_t)*((int32_t *)(intptr_t)arg1[2]), var_1b0);
                } else if (v0_7 == 0x20000000) {
                    AL_EncTrace_TraceJpegStatus((char *)(intptr_t)READ_S32((void *)(intptr_t)s1, 0xf24c),
                                                (char *)(intptr_t)READ_S32((void *)(intptr_t)s1, 0xf250),
                                                s3_1 & 0x0fffffff, (void *)(intptr_t)*((int32_t *)(intptr_t)arg1[2]));
                } else {
                    result = READ_U8((void *)(intptr_t)s1, 0xf248);
                    if (result != 0)
                        return (uint32_t)puts("end_frame,");
                }
            }
        } else if ((s3_1 & 0xf0000000) == 0x10000000) {
            AL_EncTrace_TraceInputsFifo((char *)(intptr_t)READ_S32((void *)(intptr_t)s1, 0xf24c),
                                        (char *)(intptr_t)READ_S32((void *)(intptr_t)s1, 0xf250),
                                        s3_1 & 0x0fffffff, (char)arg1[4], (char)arg1[0x11], (char)arg1[0x13],
                                        (uint16_t)arg1[5], arg1[2], arg1[3], var_1b0);
        }

        result = READ_U8((void *)(intptr_t)s1, 0xf248);
        if (result != 0) {
            int32_t v0_4 = arg1[6];

            if ((v0_4 & 1) == 0)
                goto label_start;
            if (s2_1 != 0)
                return (uint32_t)puts("end_frame,");
            if ((v0_4 & 4) != 0) {
                result = (uint16_t)arg1[5] - 1;
                if ((uint32_t)arg1[0x12] == result)
                    return (uint32_t)puts("end_frame,");
            }
        }
    } else {
        result = READ_U8((void *)(intptr_t)s1, 0xf248);
        if (result != 0) {
label_start:
            if (s2_1 == 0)
                result = (uint32_t)arg1[0x11];
            if (s2_1 != 0 || result == 0)
                return (uint32_t)puts("start_frame,");
        }
    }

    return result;
}

static int32_t EndEncoding(int32_t *arg1, int32_t *arg2, int32_t arg3)
{
    int32_t *s1 = (int32_t *)(intptr_t)arg1[0];
    int32_t s2 = arg1[1];
    char const *var_68 = NULL;
    int32_t var_64 = 0;
    char const *var_60 = NULL;
    char const *var_5c = NULL;
    char const *var_58 = NULL;
    int32_t var_54 = 0;
    int32_t var_50 = 0;
    int32_t var_4c = 0;
    uint32_t var_48 = 0;
    uint32_t var_44 = 0;
    int32_t s7_1;
    int32_t s5_1;
    void *a3_8 = NULL;
    int32_t s3_8;
    int32_t s4_1;
    int32_t s6_1;
    int32_t fp_1;
    void *v0_11;
    void *v0_12;
    void *v0_13;
    int32_t s5_4;

    if (arg2 == NULL) {
        intptr_t v1_20 = (intptr_t)(s2 * 0x3bf);
        return ((int32_t (*)(int32_t, int32_t *, int32_t, int32_t))(intptr_t)READ_S32(s1, v1_20 * 0x3c + 0xe0d4))(
            READ_S32(s1, v1_20 * 0x3c + 0xe0d8), arg2, 0, s2);
    }

    if ((uint32_t)arg3 >= 0x140) {
        __assert("streamId >= 0 && streamId < (32 * 10)",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c", 0x5fb, "EndEncoding");
    }

    s7_1 = s2 << 4;
    s5_1 = s2 << 8;
    if (READ_U8(READ_PTR(s1, 0x14), s5_1 - s7_1 + 0x1f) == 4)
        a3_8 = READ_PTR(s1, 0xf27c);

    if (READ_U8(READ_PTR(s1, 0x14), s5_1 - s7_1 + 0x1f) != 4 || a3_8 == NULL) {
        s6_1 = s2 << 6;
        s4_1 = s2 << 0xa;
        Rtos_GetMutex(READ_PTR(s1, 0xf254));
        {
            intptr_t s3_3 = (intptr_t)((s4_1 - s6_1 - s2) * 0xf);
            int32_t a1 = arg2[0x27];

            AL_Common_SetError(s1, a1);
            WRITE_S32(s1, s3_3 * 4 + 0xdbb4, (READ_S32(s1, s3_3 * 4 + 0xdbb4) + 1) % 0x140);
            s3_8 = READ_S32(s1, (s3_3 + arg3 + 0x36ee) * 4);
        }
        Rtos_ReleaseMutex(READ_PTR(s1, 0xf254));
    } else {
        s6_1 = s2 << 6;
        s4_1 = s2 << 0xa;
        Rtos_GetMutex(READ_PTR(a3_8, 0xf254));
        {
            intptr_t s3_11 = (intptr_t)((s4_1 - s6_1 - s2) * 0xf);
            int32_t a1_6 = arg2[0x27];
            int32_t a2_14 = (READ_S32(a3_8, (s3_11 << 2) + 0xdbb4) + 1) % 0x140;

            WRITE_S32(a3_8, (s3_11 << 2) + 0xdbb4, a2_14);
            AL_Common_SetError((int32_t *)a3_8, a1_6);
            s3_8 = READ_S32(a3_8, ((s3_11 + arg3 + 0x36ec) << 2) + 8);
        }
        Rtos_ReleaseMutex(READ_PTR(a3_8, 0xf254));
    }

    fp_1 = arg2[0];
    if ((uint32_t)arg2[0x27] < 0x80) {
        var_68 = (char const *)(intptr_t)s2;
        ((void (*)(int32_t *, int32_t *, void *, int32_t, char const *))(intptr_t)s1[4])(s1, arg2,
                                                                                           (uint8_t *)s1 + fp_1 * 0x30 + 0xedb4,
                                                                                           s3_8, var_68);
    } else {
        int32_t v0_10 = IMP_Log_Get_Option();
        void *s5_3 = (uint8_t *)READ_PTR(s1, 0x14) + s5_1 - s7_1;

        var_44 = READ_U8(s5_3, 6);
        var_48 = READ_U8(s5_3, 4);
        var_50 = arg2[0x27];
        var_4c = READ_S32(s5_3, 0x1c);
        var_5c = "%s(%d):errorCode = %x, eProfile = %x, uEncWidth = %d, uEncHeight = %d\n";
        var_54 = 0x61d;
        var_60 = "EndEncoding";
        var_64 = 0x61d;
        var_58 = "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c";
        var_68 = "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c";
        imp_log_fun(6, v0_10, 2, "Encoder",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c",
                    0x61d, "EndEncoding",
                    "%s(%d):errorCode = %x, eProfile = %x, uEncWidth = %d, uEncHeight = %d\n",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c", 0x61d,
                    var_50, var_4c, var_48, var_44, &_gp);
    }

    v0_11 = AL_Buffer_GetMetaData((AL_TBuffer *)(intptr_t)s3_8, 3);
    if (v0_11 != NULL)
        WRITE_S32(v0_11, 0xc, arg2[0x28]);

    v0_12 = AL_Buffer_GetMetaData((AL_TBuffer *)(intptr_t)s3_8, 1);
    if (v0_12 != NULL)
        WRITE_U8(v0_12, 0xc, READ_U8(arg2, 0xb4));

    s5_4 = arg2[2];
    v0_13 = AL_Buffer_GetMetaData((AL_TBuffer *)(intptr_t)s3_8, 7);
    if (v0_13 == NULL) {
        return AL_Common_Encoder_IsInitialQpProvided(
            (int32_t *)(intptr_t)__assert("pStreamMeta",
                                          "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c",
                                          0x626, "EndEncoding", var_68, var_64, var_60, var_5c, var_58, var_54,
                                          var_50, var_4c, var_48, var_44, &_gp));
    }

    WRITE_U8(v0_13, 0xc, READ_U8(arg2, 0xaa));
    if (READ_U8(arg2, 0xaa) != 0) {
        int32_t *i = &arg2[0x2e];
        int32_t *v0_14 = (int32_t *)((uint8_t *)v0_13 + 0x10);

        do {
            int32_t t0_3 = i[0];
            int32_t a3_4 = i[1];
            int32_t a2_4 = i[2];
            int32_t a1_2 = i[3];

            i += 4;
            v0_14[0] = t0_3;
            v0_14[1] = a3_4;
            v0_14[2] = a2_4;
            v0_14[3] = a1_2;
            v0_14 += 4;
        } while (i != &arg2[0x36]);

        v0_14[0] = i[0];
        v0_14[1] = i[1];
    }

    {
        intptr_t v0_16 = (intptr_t)(s4_1 - s6_1 - s2);

        ((void (*)(int32_t, int32_t, int32_t, int32_t))(intptr_t)READ_S32(s1, v0_16 * 0x3c + 0xe0d4))(
            READ_S32(s1, v0_16 * 0x3c + 0xe0d8), s3_8, s5_4, s2);
    }

    if (READ_U8(arg2, 0xaa) != 0) {
        int32_t (*done_cb)(int32_t *) = (int32_t (*)(int32_t *))(intptr_t)READ_S32(s1, 0);

        if (done_cb(arg2) != 0)
            releaseSource(s1, s5_4, (uint8_t *)s1 + fp_1 * 0x30 + 0xed90);

        Rtos_GetMutex(READ_PTR(s1, 0xf254));
        WRITE_S32(s1, 0xed88, READ_S32(s1, 0xed88) + 1);
        if (done_cb(arg2) != 0)
            AL_Fifo_Queue((int32_t *)((uint8_t *)s1 + 0xf0f0), (void *)(intptr_t)(fp_1 + 1), 0);
        Rtos_ReleaseMutex(READ_PTR(s1, 0xf254));
        Rtos_ReleaseSemaphore(READ_PTR(s1, 0xf258));
        return 0;
    }

    Rtos_GetMutex(READ_PTR(s1, 0xf254));
    AL_Buffer_Unref((AL_TBuffer *)(intptr_t)s3_8);
    return Rtos_ReleaseMutex(READ_PTR(s1, 0xf254));
}

int32_t AL_Common_Encoder_Process(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    int32_t *s7 = (int32_t *)(intptr_t)*arg1;
    struct {
        int32_t src_y_phys;
        int32_t src_uv_phys;
        int32_t src_pitch_y;
        int32_t src_flags50;
        int32_t src_flags58;
        int32_t src_map_y_phys;
        int32_t src_map_uv_phys;
        int32_t stream_phys;
        int32_t stream_virt;
        int32_t stream_off;
    } io = {0};
    int32_t var_38_1 = 0;
    int32_t *s4_2 = NULL;
    int32_t s6_1 = 0;
    int32_t s2_1;
    int32_t s3_1;
    int32_t can_use_src;
    int32_t result;

    (void)_gp;
    {
        int kfd = open("/dev/kmsg", O_WRONLY);
        if (kfd >= 0) {
            char b[160];
            int n = snprintf(b, sizeof(b),
                             "libimp/CENC: Process entry wrap=%p enc=%p frame=%p stream=%p layer=%d active=%u\n",
                             arg1, s7, (void *)(intptr_t)arg2, (void *)(intptr_t)arg3, arg4,
                             s7 ? (unsigned)READ_U8(s7, arg4 + 0xed4c) : 0U);
            if (n > 0) write(kfd, b, n);
            close(kfd);
        }
    }
    if (READ_U8(s7, arg4 + 0xed4c) == 0) {
        if (arg2 == 0) {
            if (arg4 < 0 || READ_U8(s7, 0xed4c) != 0)
                return 1;

            {
                int32_t *a0_36 = (int32_t *)READ_PTR(s7, 0xf25c);
                int32_t *vtbl = a0_36 ? (int32_t *)(intptr_t)READ_S32(a0_36, 0) : NULL;
                int32_t encode_one_frame_fn = vtbl ? READ_S32(vtbl, 0xc) : 0;

                WRITE_U8(s7, 0xed4c, 1);
                {
                    int kfd = open("/dev/kmsg", O_WRONLY);
                    if (kfd >= 0) {
                        char b[160];
                        int n = snprintf(b, sizeof(b),
                                         "libimp/CENC: Activate dispatch sched=%p vtbl=%p fn=%p chn=%d\n",
                                         a0_36, vtbl, (void *)(intptr_t)encode_one_frame_fn, READ_S32(s7, 0x18));
                        if (n > 0) write(kfd, b, n);
                        close(kfd);
                    }
                }
                if (encode_one_frame_fn == 0) {
                    __assert("pScheduler && pScheduler->vtable && pScheduler->vtable->EncodeOneFrame",
                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c",
                             0x1c5, "AL_Common_Encoder_Process");
                }
                return ((int32_t (*)(int32_t *, int32_t, int32_t, int32_t, int32_t))(intptr_t)encode_one_frame_fn)(
                    a0_36, READ_S32(s7, 0x18), 0, 0, 0);
            }
        }

        s2_1 = arg4 << 6;
        s3_1 = arg4 << 0xa;
        can_use_src =
            AL_SrcBuffersChecker_CanBeUsed((int32_t *)((uint8_t *)s7 + (s3_1 - s2_1 - arg4) * 0x3c + 0xdaf0),
                                           (AL_TBuffer *)(intptr_t)arg2);
        {
            int kfd = open("/dev/kmsg", O_WRONLY);
            if (kfd >= 0) {
                char b[192];
                int n = snprintf(b, sizeof(b),
                                 "libimp/CENC: Process source-check can_use=%d frame=%p layer=%d checker=%p active=%u stream=%p\n",
                                 can_use_src, (void *)(intptr_t)arg2, arg4,
                                 (uint8_t *)s7 + (s3_1 - s2_1 - arg4) * 0x3c + 0xdaf0,
                                 (unsigned)READ_U8(s7, arg4 + 0xed4c), (void *)(intptr_t)arg3);
                if (n > 0) write(kfd, b, n);
                close(kfd);
            }
        }
        if (can_use_src != 0) {
            if ((uint32_t)(READ_S32(READ_PTR(s7, 0x14), 0x11c) - 1) >= 2) {
                CENC_KMSG("Process wait-readiness multi-slot frame=%p layer=%d", (void *)(intptr_t)arg2, arg4);
                AL_Common_Encoder_WaitReadiness(s7);
                {
                    int32_t a3 = AL_Fifo_Dequeue((int32_t *)((uint8_t *)s7 + 0xf0f0), 0) - 1;
                    int32_t v0_11 = a3 << 6;
                    int32_t v0_12;

                    CENC_KMSG("Process got-slot multi-slot idx=%d layer=%d stream=%p", a3, arg4,
                              (void *)(intptr_t)arg3);
                    s6_1 = a3 << 4;
                    var_38_1 = v0_11;
                    v0_12 = v0_11 - s6_1;
                    WRITE_S32(s7, 0xf10c, a3);
                    s4_2 = (int32_t *)((uint8_t *)s7 + v0_12 + 0xed90);
                    WRITE_S32(s7, v0_12 + 0xeda4, a3 >> 0x1f);
                    WRITE_S32(s7, v0_12 + 0xeda0, a3);
                    WRITE_S32(s7, v0_12 + 0xed94, 0x1a);
                    __builtin_memset(&io, 0, sizeof(io));
                    AL_Common_Encoder_SetEncodingOptions(s7, (uint32_t *)s4_2);
                    WRITE_S32(s7, v0_12 + 0xedb0, arg3);
                    if (arg3 == 0) {
                        io.stream_virt = 0;
                        io.stream_off = 0;
                        io.stream_phys = 0;
                        CENC_KMSG("Process null-stream path layer=%d slot=%d", arg4, a3);
                        CENC_KMSG("Process using null-stream metadata layer=%d slot=%d", arg4, a3);
                        goto label_53d78;
                    }
                }
            } else if (arg3 != 0) {
                CENC_KMSG("Process wait-readiness single-slot frame=%p layer=%d", (void *)(intptr_t)arg2, arg4);
                AL_Common_Encoder_WaitReadiness(s7);
                {
                    int32_t a3_2 = AL_Fifo_Dequeue((int32_t *)((uint8_t *)s7 + 0xf0f0), 0) - 1;
                    int32_t t0_1 = a3_2 * 0x30;

                    CENC_KMSG("Process got-slot single-slot idx=%d layer=%d stream=%p", a3_2, arg4,
                              (void *)(intptr_t)arg3);
                    WRITE_S32(s7, 0xf10c, a3_2);
                    var_38_1 = a3_2 << 6;
                    s6_1 = a3_2 << 4;
                    memset(&io, 0, sizeof(io));
                    WRITE_S32(s7, t0_1 + 0xeda4, a3_2 >> 0x1f);
                    s4_2 = (int32_t *)((uint8_t *)s7 + t0_1 + 0xed90);
                    WRITE_S32(s7, t0_1 + 0xeda0, a3_2);
                    WRITE_S32(s7, t0_1 + 0xed94, 0x1a);
                    AL_Common_Encoder_SetEncodingOptions(s7, (uint32_t *)s4_2);
                    WRITE_S32(s7, t0_1 + 0xedb0, arg3);
                }
            } else {
                CENC_KMSG("Process no-stream-buffer available layer=%d frame=%p -> return 0",
                          arg4, (void *)(intptr_t)arg2);
                return 0;
            }

            ((int32_t (*)(AL_TBuffer *))(intptr_t)AL_Buffer_Ref)((AL_TBuffer *)(intptr_t)arg3);
            io.stream_phys = (int32_t)AL_Buffer_GetPhysicalAddress((AL_TBuffer *)(intptr_t)arg3);
            io.stream_virt = (int32_t)(intptr_t)AL_Buffer_GetData((AL_TBuffer *)(intptr_t)arg3);
            io.stream_off = 0;
            WRITE_S32(s7, var_38_1 - s6_1 + 0xed90, READ_S32(s7, var_38_1 - s6_1 + 0xed90) | 1);

label_53d78:
            io.src_y_phys = (int32_t)AL_PixMapBuffer_GetPlanePhysicalAddress((AL_TBuffer *)(intptr_t)arg2, 0);
            io.src_uv_phys = (int32_t)AL_PixMapBuffer_GetPlanePhysicalAddress((AL_TBuffer *)(intptr_t)arg2, 1);
            io.src_pitch_y = AL_PixMapBuffer_GetPlanePitch((AL_TBuffer *)(intptr_t)arg2, 0);
            {
                void *fp_2 = READ_PTR(s7, 0x14);
                int32_t v0_21 = (int32_t)AL_PixMapBuffer_GetFourCC((AL_TBuffer *)(intptr_t)arg2);

                if (AL_IsCompressed((uint32_t)v0_21) != 0) {
                    io.src_map_y_phys = (int32_t)AL_PixMapBuffer_GetPlanePhysicalAddress((AL_TBuffer *)(intptr_t)arg2, 2);
                    io.src_map_uv_phys = (int32_t)AL_PixMapBuffer_GetPlanePhysicalAddress((AL_TBuffer *)(intptr_t)arg2, 3);
                    io.src_flags50 = AL_PixMapBuffer_GetPlanePitch((AL_TBuffer *)(intptr_t)arg2, 2) & 0xff;
                }

                io.src_flags58 = (((uint32_t (*)(uint32_t))(intptr_t)AL_GetBitDepth)((uint32_t)v0_21) < 9U) ^ 1U;
                io.src_flags50 = (io.src_flags50 & ~0xff) | ((READ_U32(fp_2, arg4 * 0xf0 + 0x14) >> 1) & 7);
            }

            ((int32_t (*)(AL_TBuffer *))(intptr_t)AL_Buffer_Ref)((AL_TBuffer *)(intptr_t)arg2);
            WRITE_S32(s7, var_38_1 - s6_1 + 0xeda8, arg2);
            WRITE_S32(s7, var_38_1 - s6_1 + 0xedac, 0);
            CENC_KMSG("Process source planes frame=%p y=0x%x uv=0x%x pitch=%d mapY=0x%x mapUV=0x%x flags50=0x%x flags58=0x%x",
                      (void *)(intptr_t)arg2, io.src_y_phys, io.src_uv_phys, io.src_pitch_y,
                      io.src_map_y_phys, io.src_map_uv_phys, io.src_flags50, io.src_flags58);
            CENC_KMSG("Process source-sent mutex enter mutex=%p frame=%p", READ_PTR(s7, 0xf254),
                      (void *)(intptr_t)arg2);
            Rtos_GetMutex(READ_PTR(s7, 0xf254));
            CENC_KMSG("Process source-sent mutex acquired mutex=%p frame=%p", READ_PTR(s7, 0xf254),
                      (void *)(intptr_t)arg2);
            {
                int32_t *v1_9 = (int32_t *)((uint8_t *)s7 + 0xf114);
                int32_t v0_30 = 0;

                while (1) {
                    v0_30 += 1;
                    if (*v1_9 == 0) {
                        int32_t v0_39;
                        int32_t a1_1;

                        CENC_KMSG("Process source-sent slot=%d frame=%p opts=%p", v0_30 - 1,
                                  (void *)(intptr_t)arg2, s4_2);
                        WRITE_S32(s7, ((v0_30 - 1) << 3) + 0xf114, arg2);
                        WRITE_PTR(s7, ((v0_30 - 1) << 3) + 0xf110, s4_2);
                        Rtos_ReleaseMutex(READ_PTR(s7, 0xf254));
                        CENC_KMSG("Process source-sent mutex released mutex=%p slot=%d", READ_PTR(s7, 0xf254),
                                  v0_30 - 1);
                        v0_39 = (s3_1 - s2_1 - arg4) * 0x3c;
                        WRITE_S32(s7, var_38_1 - s6_1 + 0xedb4, 0);
                        WRITE_S32(s7, var_38_1 - s6_1 + 0xedb8, 0);
                        a1_1 = READ_S32(s7, v0_39 + 0xdb1c);
                        if ((a1_1 & 0x200) != 0) {
                            WRITE_U8(s7, var_38_1 - s6_1 + 0xedb4, 1);
                            WRITE_U8(s7, var_38_1 - s6_1 + 0xedb5, READ_U8(s7, v0_39 + 0xdb8c));
                        }
                        if ((a1_1 & 0x400) != 0) {
                            int32_t a0_30 = s3_1 - s2_1 - arg4;

                            WRITE_U8(s7, var_38_1 - s6_1 + 0xedb6, 1);
                            WRITE_U8(s7, var_38_1 - s6_1 + 0xedb7, READ_U8(s7, a0_30 * 0x3c + 0xdb82));
                            WRITE_U8(s7, var_38_1 - s6_1 + 0xedb8, READ_U8(s7, a0_30 * 0x3c + 0xdb83));
                        }
                        if (READ_U8(s7, 0xed84) == 0)
                            WRITE_U8(s7, 0xed84, 1);

                        {
                            int32_t *a0_32 = (int32_t *)READ_PTR(s7, 0xf25c);
                            int32_t *vtbl_1 = a0_32 ? (int32_t *)(intptr_t)READ_S32(a0_32, 0) : NULL;
                            int32_t encode_one_frame_fn = vtbl_1 ? READ_S32(vtbl_1, 0xc) : 0;

                            {
                                int kfd = open("/dev/kmsg", O_WRONLY);
                                if (kfd >= 0) {
                                    char b[224];
                                    int n = snprintf(b, sizeof(b),
                                                     "libimp/CENC: Process dispatch sched=%p vtbl=%p fn=%p chn=%d src=%p opts=%p stream_meta=%p\n",
                                                     a0_32, vtbl_1, (void *)(intptr_t)encode_one_frame_fn,
                                                     READ_S32(s7, (s3_1 - s2_1 - arg4) * 0x3c + 0x18),
                                                     s4_2, (uint8_t *)s7 + v0_39 + 0xdb1c, &io);
                                    if (n > 0) write(kfd, b, n);
                                    close(kfd);
                                }
                            }

                            if (encode_one_frame_fn == 0) {
                                __assert("pScheduler && pScheduler->vtable && pScheduler->vtable->EncodeOneFrame",
                                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c",
                                         0x242, "AddSourceSent");
                            }

                            result =
                                ((int32_t (*)(int32_t *, int32_t, int32_t *, void *, void *))(intptr_t)encode_one_frame_fn)(
                                    a0_32, READ_S32(s7, (s3_1 - s2_1 - arg4) * 0x3c + 0x18), s4_2,
                                    (uint8_t *)s7 + v0_39 + 0xdb1c, &io);
                        }
                        {
                            int kfd = open("/dev/kmsg", O_WRONLY);
                            if (kfd >= 0) {
                                char b[96];
                                int n = snprintf(b, sizeof(b), "libimp/CENC: Process dispatch result=%d\n", result);
                                if (n > 0) write(kfd, b, n);
                                close(kfd);
                            }
                        }
                        if (result == 0)
                            releaseSource(s7, arg2, s4_2);
                        Rtos_Memset((uint8_t *)s7 + v0_39 + 0xdb1c, 0, 0x7c);
                        Rtos_Memset(s4_2, 0, 0x20);
                        break;
                    }
                    v1_9 = &v1_9[2];
                    if (v0_30 == 0x26) {
                        __assert("0", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c",
                                 0x24b, "AddSourceSent");
                        io.stream_virt = 0;
                        io.stream_off = 0;
                        io.stream_phys = 0;
                        goto label_53d78;
                    }
                }
            }

            return result;
        }

        CENC_KMSG("Process source-check rejected frame=%p layer=%d checker=%p",
                  (void *)(intptr_t)arg2, arg4,
                  (uint8_t *)s7 + (s3_1 - s2_1 - arg4) * 0x3c + 0xdaf0);
    }

    return 1;
}

int32_t AL_Common_Encoder_CreateChannel(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t *arg4)
{
    int32_t *s2_1;
    int32_t *i;
    int32_t result;

    (void)_gp;
    { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[128]; int n = snprintf(b, sizeof(b), "libimp/ENC: CreateChannel ENTRY wrap=%p NumLayer=%d\n", (void*)arg1, arg4[0x49]); if (n>0) write(kfd, b, n); close(kfd); } }
    if (arg4[0x49] <= 0) {
        __assert("pSettings->NumLayer > 0",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_encode/Com_Encoder.c", 0x64e,
                 "AL_Common_Encoder_CreateChannel");
    }

    s2_1 = (int32_t *)(intptr_t)*arg1;
    i = arg4;
    if (READ_PTR(s2_1, 8) == NULL) {
label_55d0c:
        { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: CreateChannel FAIL s2+8 is NULL\n"; write(kfd, m, strlen(m)); close(kfd); } }
        result = 0x80;
        goto label_55c1c;
    }

    { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[96]; int n = snprintf(b, sizeof(b), "libimp/ENC: CC-s2+14 ptr=%p\n", READ_PTR(s2_1, 0x14)); if (n>0) write(kfd, b, n); close(kfd); } }
    {
        int32_t *v1_1 = (int32_t *)READ_PTR(s2_1, 0x14);

        do {
            int32_t t0_1 = i[0];
            int32_t a3 = i[1];
            int32_t a2 = i[2];
            int32_t a1 = i[3];

            i += 4;
            v1_1[0] = t0_1;
            v1_1[1] = a3;
            v1_1[2] = a2;
            v1_1[3] = a1;
            v1_1 += 4;
        } while (i != &arg4[0x1d4]);
        *v1_1 = *i;
    }
    { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: CC-post-settings-copy\n"; write(kfd, m, strlen(m)); close(kfd); } }

    {
        void *s1_1 = READ_PTR(s2_1, 0x14);
        int32_t v0_4;
        int32_t a0_2;
        int32_t v1_6;
        int32_t a0_4 = 2;
        int32_t v1_11 = 0;

        if (READ_S32(s1_1, 0xe8) != 0) {
            WRITE_PTR(s1_1, 0xec, watermark_init());
            s1_1 = READ_PTR(s2_1, 0x14);
            if (READ_PTR(s1_1, 0xec) == NULL) {
                fwrite("watermark_init failed\n", 1, 0x16, stderr);
                s1_1 = READ_PTR(s2_1, 0x14);
                WRITE_S32(s1_1, 0xe8, 0);
            }
        }

        v0_4 = READ_S32(s1_1, 0x2c);
        if (((uint32_t)v0_4 >> 0x11 & 1) == 0) {
            a0_2 = arg4[0x46];
            WRITE_S16(s1_1, 0x44, 0);
            WRITE_S16(s1_1, 0x42, 0);
            if ((uint32_t)(a0_2 - 1) < 2) {
                if (a0_2 == 2)
                    v0_4 |= 0x18;
                else
                    v0_4 |= 8;
                WRITE_S32(s1_1, 0x2c, v0_4);
            }
        } else {
            WRITE_S16(s1_1, 0x42, (int16_t)arg4[0x45]);
            a0_2 = arg4[0x46];
            WRITE_S16(s1_1, 0x44, *(int16_t *)((uint8_t *)arg4 + 0x116));
            if ((uint32_t)(a0_2 - 1) < 2) {
                if (a0_2 == 2)
                    v0_4 |= 0x18;
                else
                    v0_4 |= 8;
                WRITE_S32(s1_1, 0x2c, v0_4);
            }
        }

        if (arg4[0x47] == 1) {
            v0_4 |= 1;
            WRITE_S32(s1_1, 0x2c, v0_4);
        }

        v1_6 = READ_S32(s1_1, 0xc8);
        if (v1_6 == 3) {
            if (READ_S32(s1_1, 0x68) == 0) {
                v1_11 = READ_S32(s1_1, 0xa8);
                a0_4 = v1_11 & 2;
                if ((a0_4 != 0 && READ_U8(s1_1, 0xac) >= 2 && !(READ_U8(s1_1, 0xae) != 0 && v1_11 != 8)) ||
                    (a0_4 == 0 && v1_11 == 8)) {
                    WRITE_S32(s1_1, 0xc8, 2);
                    WRITE_S32(s1_1, 0x2c, v0_4 | 4);
                    WRITE_U8(s1_1, 0xaf, 0);
                    goto label_55b9c;
                }
            }
            WRITE_S32(s1_1, 0xc8, 0);
            WRITE_S32(s1_1, 0x2c, v0_4 | 4);
            WRITE_U8(s1_1, 0xaf, 0);
            if (READ_U8(s1_1, 0x8a) != 0)
                goto label_55b9c;
        } else {
            WRITE_S32(s1_1, 0x2c, v0_4 | 4);
            WRITE_U8(s1_1, 0xaf, 0);
            if (v1_6 == 2 || READ_U8(s1_1, 0x8a) != 0) {
label_55b9c:
                int32_t v0_8;

                v1_11 = READ_S32(s1_1, 0xa8);
                a0_4 = v1_11 & 2;
                if (a0_4 == 0) {
                    if (v1_11 == 8) {
                        v0_8 = READ_S8(s1_1, 0x8e);
                        if (v0_8 >= -1)
                            WRITE_U8(s1_1, 0xaf, (uint8_t)v0_8);
                        else
                            WRITE_U8(s1_1, 0xaf, 5);
                    }
                } else if (READ_U8(s1_1, 0xae) == 0) {
                    v0_8 = READ_S8(s1_1, 0x8e);
                    if (v0_8 < -1)
                        WRITE_U8(s1_1, 0xaf, 4);
                    else
                        WRITE_U8(s1_1, 0xaf, (uint8_t)v0_8);
                }
            }
        }

        WRITE_S32(s2_1, 0xf25c, arg2);
        { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[96]; int n = snprintf(b, sizeof(b), "libimp/ENC: CC-pre-MemDesc_AllocNamed size=%d\n", AL_GetAllocSizeEP1()); if (n>0) write(kfd, b, n); close(kfd); } }
        if (MemDesc_AllocNamed((uint8_t *)s2_1 + 0xdb98, (void *)(intptr_t)arg3, AL_GetAllocSizeEP1(),
                               "CompData") == 0) {
            { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: CC FAIL MemDesc_AllocNamed\n"; write(kfd, m, strlen(m)); close(kfd); } }
            result = 0x87;
            goto label_55c1c;
        }
        { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: CC-post-MemDesc_AllocNamed\n"; write(kfd, m, strlen(m)); close(kfd); } }

        WRITE_S32(s2_1, 0xdbac, 0);
        WRITE_S32(s2_1, 0xf10c, 0);
        AL_SrcBuffersChecker_Init((uint8_t *)s2_1 + 0xdaf0, s1_1);
        WRITE_S32(s2_1, 0xf244, -1);
        WRITE_S32(s2_1, 0xed54, READ_S32(s1_1, 0x6c));
        WRITE_S32(s2_1, 0xf24c, 0);
        WRITE_S32(s2_1, 0xf250, 0);
        WRITE_S32(s2_1, 0xf240, 0);
        WRITE_S32(s2_1, 0xed50, 0);
        WRITE_S32(s2_1, 0xed4c, 0);
        WRITE_S32(s2_1, 0xed58, 0);
        AL_HDRSEIs_Reset((uint8_t *)s2_1 + 0xed5c);
        WRITE_S32(s2_1, 0xdbb0, 0);
        WRITE_S32(s2_1, 0xdbb4, 0);
        WRITE_S32(s2_1, 0xed88, 0);
        WRITE_S32(s2_1, 0xed8c, 0);
        Rtos_Memset((uint8_t *)s2_1 + 0xed90, 0, 0x360);
        Rtos_Memset((uint8_t *)s2_1 + 0xf110, 0, 0x130);
        WRITE_PTR(s2_1, 0xf254, Rtos_CreateMutex());
        { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[96]; int n = snprintf(b, sizeof(b), "libimp/ENC: CC mutex=%p\n", READ_PTR(s2_1, 0xf254)); if (n>0) write(kfd, b, n); close(kfd); } }
        if (READ_PTR(s2_1, 0xf254) != NULL) {
            int32_t i_1 = 0;
            int32_t *v1_14;
            int32_t v0_23;
            uint32_t a1_9;
            uint32_t a3_2;
            int32_t (*var_28)(int32_t *, int32_t *, int32_t);

            if (AL_Fifo_Init((int32_t *)((uint8_t *)s2_1 + 0xf0f0), 0x12) == 0) {
                result = 0x87;
                goto label_55c1c;
            }
            do {
                i_1 += 1;
                AL_Fifo_Queue((int32_t *)((uint8_t *)s2_1 + 0xf0f0), (void *)(intptr_t)i_1, 0);
            } while (i_1 != 0x12);

            {
                int kfd = open("/dev/kmsg", O_WRONLY);
                if (kfd >= 0) {
                    char b[160];
                    int32_t fn8 = READ_S32(s2_1, 8);
                    uint32_t first = fn8 ? *(uint32_t *)(intptr_t)fn8 : 0;
                    uint32_t second = fn8 ? *(uint32_t *)(intptr_t)(fn8+4) : 0;
                    int n = snprintf(b, sizeof(b), "libimp/ENC: CC pre-vfn[8] fn=0x%x first-instr=0x%08x next=0x%08x\n", fn8, first, second);
                    if (n>0) write(kfd, b, n); close(kfd);
                }
            }
            ((void (*)(int32_t *, void *, int32_t *))(intptr_t)READ_S32(s2_1, 8))(s2_1, s1_1, arg4);
            { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: CC post-vfn[8] (ConfigureChannel)\n"; write(kfd, m, strlen(m)); close(kfd); } }
            WRITE_PTR(s2_1, 0xe0cc, s2_1);
            WRITE_S32(s2_1, 0xe0d0, 0);
            var_28 = EndEncoding;
            a1_9 = READ_U8(s1_1, 4);
            a3_2 = READ_U8(s1_1, 6);
            v1_14 = (int32_t *)((uint8_t *)s2_1 + 0xe0dc);
            v0_23 = 0;
            while (1) {
                int32_t a0_14 = *v1_14;

                if (a0_14 != 0) {
                    if ((uint32_t)a1_9 != (uint32_t)a0_14) {
                        v0_23 += 1;
                    } else {
                        v0_23 += 1;
                        if ((uint32_t)a3_2 == (uint32_t)v1_14[1])
                            break;
                    }
                    v1_14 += 2;
                    if (v0_23 == 0xf)
                        v0_23 = 0;
                    else
                        continue;
                }

                {
                    void *v0_34 = (uint8_t *)s2_1 + ((v0_23 + 0x1c1a) << 3);

                    WRITE_S32(v0_34, 0xc, a1_9);
                    WRITE_S32(v0_34, 0x10, a3_2);
                }
                break;
            }

            { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[96]; int n = snprintf(b, sizeof(b), "libimp/ENC: CC pre-vfn[c]#1 fn=0x%x\n", READ_S32(s2_1, 0xc)); if (n>0) write(kfd, b, n); close(kfd); } }
            ((void (*)(int32_t *, int32_t, int32_t))(intptr_t)READ_S32(s2_1, 0xc))(s2_1, 0, 1);
            { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: CC post-vfn[c]#1\n"; write(kfd, m, strlen(m)); close(kfd); } }
            {
                void *s3_2 = READ_PTR(s2_1, 0x14);
                int32_t a1_10 = READ_S32(s2_1, 0xdba0);
                int32_t a0_17 = READ_S32(s2_1, 0xdb98);
                int32_t v0_25;
                int32_t *a1_12;

                WRITE_S32(s2_1, 0xdbac, 0);
                { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[96]; int n = snprintf(b, sizeof(b), "libimp/ENC: CC pre-CleanupMemory a0=%d a1=%d\n", a0_17, a1_10); if (n>0) write(kfd, b, n); close(kfd); } }
                AL_CleanupMemory(a0_17, a1_10);
                { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[64]; int n = snprintf(b, sizeof(b), "libimp/ENC: CC post-Cleanup v0_25=0x%x\n", READ_S32(s3_2, 0xc8)); if (n>0) write(kfd, b, n); close(kfd); } }
                v0_25 = READ_S32(s3_2, 0xc8);
                if (v0_25 == 0x80) {
                    if (LoadLambdaFromFile("./Lambdas.hex", (int32_t *)((uint8_t *)s2_1 + 0xdb98)) == 0)
                        goto label_55d0c;
                } else if (v0_25 == 1) {
                    LoadCustomLda((int32_t *)((uint8_t *)s2_1 + 0xdb98));
                    WRITE_S32(s3_2, 0xc8, 0x80);
                }
                { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[96]; int n = snprintf(b, sizeof(b), "libimp/ENC: CC pre-vfn[4] fn=0x%x\n", READ_S32(s2_1, 4)); if (n>0) write(kfd, b, n); close(kfd); } }
                ((void (*)(int32_t *, void *))(intptr_t)READ_S32(s2_1, 4))(s2_1, (uint8_t *)s2_1 + 0xdb98);
                { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: CC post-vfn[4] (preprocessEp1)\n"; write(kfd, m, strlen(m)); close(kfd); } }
                a1_12 = (int32_t *)READ_PTR(s2_1, 0xf25c);
                /* HLIL: (*(*a1_12 + 4))(...) — a1_12 points to a struct whose
                 * first field is the scheduler vtable; +4 is the 2nd vtable slot. */
                {
                    int32_t *vtbl = (int32_t *)(intptr_t)READ_S32(a1_12, 0);
                    { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[160]; int n = snprintf(b, sizeof(b), "libimp/ENC: CC a1_12=%p vtbl=%p pre-sched[4] fn=0x%x\n", (void*)a1_12, (void*)vtbl, vtbl ? READ_S32(vtbl, 4) : 0); if (n>0) write(kfd, b, n); close(kfd); } }
                    result = ((int32_t (*)(void *, int32_t *, void *, void *, void *))(intptr_t)READ_S32(vtbl, 4))(
                        (uint8_t *)s2_1 + 0x18, a1_12, (uint8_t *)s2_1 + 0xe0b8, (uint8_t *)s2_1 + 0xdb98, &var_28);
                }
                { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[96]; int n = snprintf(b, sizeof(b), "libimp/ENC: CC post-sched[4] result=0x%x\n", result); if (n>0) write(kfd, b, n); close(kfd); } }
                if ((uint32_t)result >= 0x80)
                    goto label_55c1c;
                {
                    int32_t *vtbl = (int32_t *)(intptr_t)READ_S32(a1_12, 0);
                    if (((int32_t (*)(int32_t *, int32_t, uint32_t (*)(int32_t *, int32_t), int32_t *))(intptr_t)READ_S32(vtbl, 0x1c))(
                            a1_12, READ_S32(s2_1, 0x18), trace, s2_1) == 0)
                        goto label_55c1c;
                }
                { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: CC post-sched[1c]\n"; write(kfd, m, strlen(m)); close(kfd); } }

                WRITE_PTR(s2_1, 0xf258, Rtos_CreateSemaphore(0x11));
                WRITE_S32(s2_1, 0xed80, (READ_U32(s1_1, 0x28) >> 4) & 0xf);
                ((void (*)(int32_t *, int32_t, int32_t))(intptr_t)READ_S32(s2_1, 0xc))(s2_1, 0, 1);
                WRITE_S32(s2_1, 0xf260, READ_U8(s1_1, 0xae));
                WRITE_S32(s2_1, 0xf264, READ_S16(s1_1, 0x74));
                { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { const char *m = "libimp/ENC: CC SUCCESS return\n"; write(kfd, m, strlen(m)); close(kfd); } }
                return result;
            }
        }
    }

label_55c1c:
    {
        void *v0_11 = READ_PTR(s2_1, 0x14);

        if (v0_11 != NULL) {
            if (READ_S32(v0_11, 0x124) == 0) {
                { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[96]; int n = snprintf(b, sizeof(b), "libimp/ENC: CreateChannel EXIT result=0x%x\n", result); if (n>0) write(kfd, b, n); close(kfd); } }
                return result;
            }
            destroyChannels_part_6(s2_1);
        }
    }
    { int kfd = open("/dev/kmsg", O_WRONLY); if (kfd >= 0) { char b[96]; int n = snprintf(b, sizeof(b), "libimp/ENC: CreateChannel EXIT2 result=0x%x\n", result); if (n>0) write(kfd, b, n); close(kfd); } }
    return result;
}
