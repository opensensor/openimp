#include <stdint.h>

#include "alcodec/al_rtos.h"
#include "alcodec/al_types.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function,
                        void *caller);

int32_t AL_StreamMngr_AddBuffer(AL_TStreamMngrCtx *arg1, int32_t *arg2);
int32_t AL_StreamMngr_AddBufferBack(AL_TStreamMngrCtx *arg1, int32_t *arg2);
int32_t AL_sDPB_UpdateRefPtr(int32_t arg1); /* forward decl, ported by T<N> later */

static inline int32_t stream_mngr_read_s32(const void *ptr, uint32_t offset)
{
    return *(const int32_t *)((const uint8_t *)ptr + offset);
}

static inline void *stream_mngr_read_ptr(const void *ptr, uint32_t offset)
{
    return *(void *const *)((const uint8_t *)ptr + offset);
}

static inline void stream_mngr_write_s32(void *ptr, uint32_t offset, int32_t value)
{
    *(int32_t *)((uint8_t *)ptr + offset) = value;
}

static inline void stream_mngr_write_ptr(void *ptr, uint32_t offset, const void *value)
{
    *(void **)((uint8_t *)ptr + offset) = (void *)value;
}

int32_t AL_StreamMngr_Init(AL_TStreamMngrCtx **arg1)
{
    void *var_18 = &_gp;
    AL_TStreamMngrCtx *v0 = (AL_TStreamMngrCtx *)Rtos_Malloc(0x2810);

    (void)var_18;
    *arg1 = v0;
    stream_mngr_write_s32(v0, 0x2800, 0);
    stream_mngr_write_s32(v0, 0x2804, 0);
    stream_mngr_write_s32(v0, 0x2808, 0x140);
    stream_mngr_write_ptr(v0, 0x280c, Rtos_CreateMutex());
    return 1;
}

int32_t AL_StreamMngr_Deinit(AL_TStreamMngrCtx *arg1)
{
    if (arg1 == 0) {
        void *a0_3;
        int32_t *a1_1;
        int32_t assert_result =
            __assert("pCtx", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/StreamManager.c",
                     0x14, "AL_StreamMngr_Deinit", &_gp);

        a0_3 = (void *)(intptr_t)assert_result;
        a1_1 = (int32_t *)(intptr_t)assert_result;
        return AL_StreamMngr_AddBuffer((AL_TStreamMngrCtx *)a0_3, a1_1);
    }

    Rtos_DeleteMutex(stream_mngr_read_ptr(arg1, 0x280c));
    Rtos_Free(arg1);
    return 0;
}

int32_t AL_StreamMngr_AddBuffer(AL_TStreamMngrCtx *arg1, int32_t *arg2)
{
    if (arg1 == 0) {
        void *a0_3;
        int32_t *a1_2;
        int32_t assert_result =
            __assert("pCtx", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/StreamManager.c",
                     0x1d, "AL_StreamMngr_AddBuffer", &_gp);

        a0_3 = (void *)(intptr_t)assert_result;
        a1_2 = (int32_t *)(intptr_t)assert_result;
        return AL_StreamMngr_AddBufferBack((AL_TStreamMngrCtx *)a0_3, a1_2);
    }

    Rtos_GetMutex(stream_mngr_read_ptr(arg1, 0x280c));

    {
        int32_t a1 = stream_mngr_read_s32(arg1, 0x2804);
        int32_t v0 = stream_mngr_read_s32(arg1, 0x2808);

        if (a1 < v0) {
            int32_t a2_1 = *arg2;

            if (a2_1 != 0) {
                int32_t a3_1 = arg2[1];

                if (a3_1 != 0) {
                    int32_t t5 = arg2[2];
                    int32_t t4 = arg2[3];
                    int32_t t3;
                    int32_t t2;
                    int32_t t1;
                    int32_t t0;
                    void *a0_2;
                    int32_t *v0_3;

                    if (v0 == 0) {
                        __builtin_trap();
                    }

                    t3 = arg2[4];
                    t2 = arg2[5];
                    t1 = arg2[6];
                    t0 = arg2[7];
                    a0_2 = stream_mngr_read_ptr(arg1, 0x280c);
                    v0_3 = (int32_t *)((uint8_t *)arg1 + ((((a1 + stream_mngr_read_s32(arg1, 0x2800)) % v0)) << 5));
                    *v0_3 = a2_1;
                    v0_3[1] = a3_1;
                    v0_3[2] = t5;
                    v0_3[3] = t4;
                    v0_3[4] = t3;
                    v0_3[5] = t2;
                    v0_3[6] = t1;
                    v0_3[7] = t0;
                    stream_mngr_write_s32(arg1, 0x2804, a1 + 1);
                    Rtos_ReleaseMutex(a0_2);
                    return 1;
                }
            }
        }
    }

    Rtos_ReleaseMutex(stream_mngr_read_ptr(arg1, 0x280c));
    return 0;
}

int32_t AL_StreamMngr_AddBufferBack(AL_TStreamMngrCtx *arg1, int32_t *arg2)
{
    void *var_18 = &_gp;

    (void)var_18;
    Rtos_GetMutex(stream_mngr_read_ptr(arg1, 0x280c));

    {
        int32_t a1 = stream_mngr_read_s32(arg1, 0x2804);
        int32_t v1 = stream_mngr_read_s32(arg1, 0x2808);

        if (a1 >= v1) {
            Rtos_ReleaseMutex(stream_mngr_read_ptr(arg1, 0x280c));
            return 0;
        }

        {
            int32_t v0_1 = stream_mngr_read_s32(arg1, 0x2800);
            int32_t v1_1 = v1 - 1;
            int32_t t5 = *arg2;
            int32_t t4 = arg2[1];
            int32_t t3 = arg2[2];
            int32_t t2 = arg2[3];
            int32_t t1 = arg2[4];
            int32_t t0 = arg2[5];
            int32_t a3 = arg2[6];
            int32_t a2 = arg2[7];
            void *a0_2;
            int32_t *v0_3;

            if (v0_1 != 0) {
                v1_1 = v0_1 - 1;
            }

            a0_2 = stream_mngr_read_ptr(arg1, 0x280c);
            v0_3 = (int32_t *)((uint8_t *)arg1 + (v1_1 << 5));
            stream_mngr_write_s32(arg1, 0x2800, v1_1);
            *v0_3 = t5;
            v0_3[1] = t4;
            v0_3[2] = t3;
            v0_3[3] = t2;
            v0_3[4] = t1;
            v0_3[5] = t0;
            v0_3[6] = a3;
            v0_3[7] = a2;
            stream_mngr_write_s32(arg1, 0x2804, a1 + 1);
            Rtos_ReleaseMutex(a0_2);
            return 1;
        }
    }
}

int32_t AL_StreamMngr_GetBuffer(AL_TStreamMngrCtx *arg1, int32_t *arg2)
{
    if (arg1 == 0) {
        return AL_sDPB_UpdateRefPtr(
            __assert("pCtx", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/StreamManager.c",
                     0x4c, "AL_StreamMngr_GetBuffer", &_gp));
    }

    Rtos_GetMutex(stream_mngr_read_ptr(arg1, 0x280c));

    {
        int32_t result = 0;
        int32_t v1 = stream_mngr_read_s32(arg1, 0x2804);

        if (v1 != 0) {
            int32_t v0_1 = stream_mngr_read_s32(arg1, 0x2800);
            int32_t a0_1 = stream_mngr_read_s32(arg1, 0x2808);
            int32_t *v0_3;
            int32_t t2_1;
            int32_t t1_1;
            int32_t t0_1;
            int32_t a3_1;
            int32_t a2_1;
            int32_t a0_2;
            int32_t v0_4;

            if (a0_1 == 0) {
                __builtin_trap();
            }

            v0_3 = (int32_t *)((uint8_t *)arg1 + (v0_1 << 5));
            t2_1 = v0_3[1];
            t1_1 = v0_3[2];
            t0_1 = v0_3[3];
            a3_1 = v0_3[4];
            a2_1 = v0_3[5];
            a0_2 = v0_3[6];
            v0_4 = v0_3[7];
            result = 1;
            *arg2 = *v0_3;
            arg2[1] = t2_1;
            arg2[2] = t1_1;
            arg2[3] = t0_1;
            arg2[4] = a3_1;
            arg2[5] = a2_1;
            arg2[6] = a0_2;
            arg2[7] = v0_4;
            stream_mngr_write_s32(arg1, 0x2804, v1 - 1);
            stream_mngr_write_s32(arg1, 0x2800, (v0_1 + 1) % a0_1);
        }

        Rtos_ReleaseMutex(stream_mngr_read_ptr(arg1, 0x280c));
        return result;
    }
}
