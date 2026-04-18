#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <unistd.h>

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

        (*(void (**)(int32_t *, int32_t))(intptr_t)READ_S32(*(void **)a0_6, 8))(a0_6, READ_S32(arg1, 0x18));
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
    if (v0 != 0) {
        AL_StreamMetaData_ClearAllSections((void *)(intptr_t)v0);
        if (s2 != NULL) {
            s1 = arg3 << 4;
            s6 = arg3 << 8;
            if (READ_U8((uint8_t *)READ_PTR(s2, 0x14) + s6 - s1, 0x1f) == 4) {
                void *s5_2 = READ_PTR(s2, 0xf27c);

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
                    (*(void (**)(int32_t *, int32_t, AL_TBuffer *))(intptr_t)READ_S32(*(void **)a0_10, 0x10))(
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
                if (READ_U8((uint8_t *)READ_PTR(s2, 0x14) + s6 - s1, 0x1f) == 4)
                    s1_3 = 0;
                if (AL_Buffer_GetSize(arg2) < 0x200U) {
                    Rtos_ReleaseMutex(READ_PTR(s2, 0xf254));
                    return 0;
                }
                a0_5 = (int32_t *)READ_PTR(s2, 0xf25c);
                var_3c = s7 >> 0x1f;
                var_38 = s1_3;
                var_40 = s7;
                (*(void (**)(int32_t *, int32_t, AL_TBuffer *))(intptr_t)READ_S32(*(void **)a0_5, 0x10))(
                    a0_5, READ_S32(s3_2, 0x18), arg2);
                Rtos_ReleaseMutex(READ_PTR(s2, 0xf254));
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

    (*(void (**)(void))(intptr_t)READ_S32(*(void **)READ_PTR(v1, 0xf25c), 0x14))();
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

        (*(void (**)(int32_t *, int32_t, int32_t *))(intptr_t)READ_S32(*(void **)a0_1, 0x18))(
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

int32_t AL_Common_Encoder_Create(int32_t **result, int32_t arg1)
{
    (void)_gp;
    *result = Rtos_Malloc(4);
    if (*result == NULL)
        return 0;

    Rtos_Memset(*result, 0, 4);
    *result = Rtos_Malloc(0xf280);
    if (*result != NULL) {
        Rtos_Memset(*result, 0, 0xf280);
        if (MemDesc_Alloc((uint8_t *)*result + 0xf268, arg1, 0x754) != 0) {
            void *v1_1 = *result;
            int32_t a0_5 = READ_S32(v1_1, 0xf268);
            int32_t t0_1 = READ_S32(v1_1, 0xf274);
            int32_t a3_1 = READ_S32(v1_1, 0xf26c);
            int32_t a2_1 = READ_S32(v1_1, 0xf270);

            WRITE_S32(v1_1, 0x14, a0_5);
            WRITE_S32(v1_1, 0xe0c4, t0_1);
            WRITE_S32(v1_1, 0xe0b8, a0_5);
            WRITE_S32(v1_1, 0xe0bc, a3_1);
            WRITE_S32(v1_1, 0xe0c0, 0x754);
            Rtos_Memset((void *)(intptr_t)a0_5, 0, (size_t)a2_1);
            return (int32_t)(intptr_t)result;
        }

        AL_Common_Encoder_Destroy(result);
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
