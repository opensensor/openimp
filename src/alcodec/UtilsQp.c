#include <stdint.h>

#include "alcodec/al_rtos.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line,
                        const char *function, void *caller);

/* Placement:
 * - AL_ApplyGopCommands / AL_ApplyPictCommands / AL_ApplyNewGOPAndRCParams /
 *   AL_ApplyGmvCommands / AL_UpdateAutoQpCtrl /
 *   GetSliceEnc2CmdOffset / GetCoreFirstEnc2CmdOffset @ UtilsQp.c
 * - AL_GetLambda @ ChooseLda.c
 * - AL_CoreConstraintEnc_* @ CoreConstraintEnc.c
 * - AL_GmvMngr_* @ GmvMngr.c
 */

/* forward decl, ported by T<N> later */
int32_t PreprocessHwRateCtrl(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4,
                             void *arg5);
/* forward decl, ported by T<N> later */
int32_t UpdateHwRateCtrlParam(void *arg1, void *arg2, int32_t *arg3);

uint32_t AL_CoreConstraint_Init(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4,
                                int32_t arg5, int32_t arg6);
uint32_t AL_CoreConstraint_GetExpectedNumberOfCores(void *arg1, int32_t arg2, int32_t arg3,
                                                    int32_t arg4, int32_t arg5);
uint32_t AL_GetResources(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4);

uint32_t AL_GmvMngr_AddGMV(void *arg1, int32_t arg2, int16_t arg3, int16_t arg4);
int32_t AL_GetLambda(int32_t arg1, void *arg2, int32_t arg3, char arg4, void *arg12,
                     void *arg13, char arg14);
uint32_t AL_GmvMngr_GetGMVIdx(int32_t *arg1, int32_t arg2, char arg3, int32_t *arg4);
int32_t AL_GmvMngr_SumGMV(int32_t *arg1, int32_t arg2, int32_t arg3, int16_t *arg4,
                          int16_t *arg5, char arg6);

static const uint8_t DELTA0_QP_CTRL_TABLE[0x14] = {
    0xc4, 0x09, 0xe8, 0x03, 0x00, 0x01, 0x88, 0x13, 0x58, 0x1b,
    0x28, 0x23, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x33,
};

static const uint8_t DEFAULT_QP_CTRL_TABLE_VP9_2[0x14] = {
    0xc4, 0x09, 0xe8, 0x03, 0x00, 0x01, 0x88, 0x13, 0x58, 0x1b,
    0x28, 0x23, 0x05, 0x0f, 0x32, 0x05, 0x0a, 0x0f, 0x00, 0xff,
};

static const uint8_t DEFAULT_QP_CTRL_TABLE[0x14] = {
    0xc4, 0x09, 0xe8, 0x03, 0x00, 0x01, 0x88, 0x13, 0x58, 0x1b,
    0x28, 0x23, 0x01, 0x03, 0x0a, 0x01, 0x02, 0x03, 0x06, 0x33,
};

static const uint8_t AL_LAMBDAs_AUTO_QP[0x34] = {
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x03, 0x04,
    0x04, 0x04, 0x05, 0x05, 0x06, 0x07, 0x09, 0x0c, 0x10, 0x15,
    0x1b, 0x22,
};

static const char data_e59a0[] = "pEP";

static const uint8_t HEVC_NEW_DEFAULT_LDA_TABLE[0xd0] = {
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x02, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03,
    0x02, 0x03, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x03, 0x03, 0x03, 0x04,
    0x03, 0x03, 0x03, 0x04, 0x03, 0x04, 0x03, 0x05, 0x04, 0x04, 0x04, 0x06,
    0x04, 0x05, 0x04, 0x07, 0x05, 0x05, 0x05, 0x07, 0x05, 0x06, 0x05, 0x09,
    0x06, 0x06, 0x06, 0x0a, 0x06, 0x07, 0x06, 0x0b, 0x07, 0x08, 0x07, 0x0d,
    0x08, 0x09, 0x08, 0x0f, 0x09, 0x0a, 0x09, 0x11, 0x0a, 0x0b, 0x0a, 0x13,
    0x0b, 0x0c, 0x0b, 0x15, 0x0c, 0x0e, 0x0c, 0x18, 0x0e, 0x0f, 0x0e, 0x1a,
    0x0f, 0x11, 0x0f, 0x1d, 0x11, 0x13, 0x11, 0x21, 0x13, 0x15, 0x13, 0x25,
    0x15, 0x18, 0x15, 0x2a, 0x18, 0x1b, 0x18, 0x2f, 0x1b, 0x1e, 0x1b, 0x34,
    0x1e, 0x22, 0x1e, 0x3a, 0x21, 0x26, 0x21, 0x42, 0x25, 0x2a, 0x25, 0x4a,
    0x2a, 0x30, 0x2a, 0x53, 0x2f, 0x35, 0x2f, 0x5d, 0x35, 0x3c, 0x35, 0x68,
    0x3b, 0x43, 0x3b, 0x00,
};

static const uint8_t AVC_DEFAULT_LDA_TABLE[0xd0] = {
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x02, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x02, 0x02, 0x02, 0x03, 0x02, 0x03,
    0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x03, 0x03, 0x03, 0x04, 0x03, 0x03,
    0x03, 0x04, 0x03, 0x04, 0x03, 0x05, 0x04, 0x04, 0x04, 0x06, 0x04, 0x05,
    0x04, 0x06, 0x05, 0x05, 0x05, 0x07, 0x05, 0x06, 0x05, 0x08, 0x06, 0x06,
    0x06, 0x09, 0x06, 0x07, 0x06, 0x0a, 0x07, 0x08, 0x07, 0x0b, 0x08, 0x09,
    0x08, 0x0d, 0x09, 0x0a, 0x09, 0x0e, 0x0a, 0x0b, 0x0a, 0x10, 0x0b, 0x0c,
    0x0b, 0x12, 0x0d, 0x0e, 0x0d, 0x14, 0x0e, 0x0f, 0x0e, 0x17, 0x0f, 0x11,
    0x0f, 0x19, 0x11, 0x13, 0x11, 0x1d, 0x13, 0x15, 0x13, 0x20, 0x15, 0x18,
    0x15, 0x24, 0x18, 0x1b, 0x18, 0x28, 0x1b, 0x1e, 0x1b, 0x2d, 0x1e, 0x22,
    0x1e, 0x33, 0x21, 0x26, 0x21, 0x39, 0x25, 0x2a, 0x25, 0x40, 0x2a, 0x30,
    0x2a, 0x48, 0x2f, 0x35, 0x2f, 0x51, 0x35, 0x3c, 0x35, 0x5b, 0x3b, 0x43,
    0x3b, 0x00,
};

uint32_t AL_ApplyGopCommands(void *arg1, int32_t *arg2, void *arg3)
{
    int32_t v0 = *arg2;
    int32_t result;

    if ((v0 & 1) != 0) {
        int32_t s3_2 = (int32_t)(intptr_t)arg3 + arg2[1];
        void (*fn)(void *, int32_t) = *(void (**)(void *, int32_t))((char *)arg1 + 0x2c);

        Rtos_GetMutex(*(void **)((char *)arg1 + 0x48));
        fn(arg1, s3_2);
        Rtos_ReleaseMutex(*(void **)((char *)arg1 + 0x48));
        v0 = *arg2;
    }

    result = v0 & 8;
    if (result == 0) {
        return (uint32_t)result;
    }

    Rtos_GetMutex(*(void **)((char *)arg1 + 0x48));
    {
        void (*fn)(void *, void *) = *(void (**)(void *, void *))((char *)arg1 + 0x28);

        fn(arg1, arg3);
    }
    return Rtos_ReleaseMutex(*(void **)((char *)arg1 + 0x48));
}

uint32_t AL_ApplyPictCommands(void *arg1, int32_t *arg2, void *arg3)
{
    int32_t v0 = *arg2;
    uint8_t *s0 = (uint8_t *)arg1;
    uint8_t *s3 = s0 + 0x128;

    if ((v0 & 2) != 0) {
        void (*fn)(void *) = *(void (**)(void *))(s0 + 0x158);

        Rtos_GetMutex(*(void **)(s0 + 0x170));
        fn(s3);
        Rtos_ReleaseMutex(*(void **)(s0 + 0x170));
        v0 = *arg2;
    }

    if ((v0 & 4) != 0) {
        void (*fn)(void *) = *(void (**)(void *))(s0 + 0x15c);

        Rtos_GetMutex(*(void **)(s0 + 0x170));
        fn(s3);
        Rtos_ReleaseMutex(*(void **)(s0 + 0x170));
        v0 = *arg2;
    }

    if ((v0 & 0x80) != 0) {
        void (*fn)(void *) = *(void (**)(void *))(s0 + 0x160);

        Rtos_GetMutex(*(void **)(s0 + 0x170));
        fn(s3);
        Rtos_ReleaseMutex(*(void **)(s0 + 0x170));
        v0 = *arg2;
    }

    if ((v0 & 0x100) != 0) {
        uint8_t v0_1 = *((uint8_t *)arg2 + 0x64);

        *((uint8_t *)arg3 + 0x2c) = 1;
        *((uint8_t *)arg3 + 0x2d) = v0_1;
        v0 = *arg2;
    }

    if ((v0 & 0x400) != 0) {
        /* +0x12fd0 */
        *(int8_t *)(s0 + 0x12fd0) = *(int8_t *)((char *)arg2 + 0x66);
        /* +0x12fd1 */
        *(int8_t *)(s0 + 0x12fd1) = *(int8_t *)((char *)arg2 + 0x67);
    }

    return (uint32_t)(int32_t)*(int8_t *)(s0 + 0x12fd1);
}

uint32_t AL_ApplyNewGOPAndRCParams(void *arg1, int32_t *arg2)
{
    int32_t v0_1 = *arg2 & 0x10;

    if (v0_1 == 0) {
        return (uint32_t)v0_1;
    }

    Rtos_GetMutex(*(void **)((char *)arg1 + 0x120));
    {
        void (*fn)(void *, int32_t *, int32_t *) =
            *(void (**)(void *, int32_t *, int32_t *))((char *)arg1 + 0xf8);

        fn((char *)arg1 + 0xf4, &arg2[2], &arg2[0x12]);
    }
    Rtos_ReleaseMutex(*(void **)((char *)arg1 + 0x120));
    {
        void (*fn)(void *, int32_t *, uint32_t) =
            *(void (**)(void *, int32_t *, uint32_t))((char *)arg1 + 0x12c);

        fn((char *)arg1 + 0x128, &arg2[0x12], (uint32_t)*(uint8_t *)((char *)arg1 + 0x1f));
    }

    if (*(int32_t *)((char *)arg1 + 0x68) == 3 || *(int32_t *)((char *)arg1 + 0xa4) != 0 ||
        *(int32_t *)((char *)arg1 + 0xa0) != 0 || *(int32_t *)((char *)arg1 + 0x9c) != 0) {
        void **s2 = (void **)((char *)arg1 + 0x35d0);
        int32_t i = 0;

        while (i != 3) {
            int32_t i_3 = i;

            i += 1;
            PreprocessHwRateCtrl(&arg2[2], (int32_t)(intptr_t)&arg2[0x12],
                                 (int32_t)(uint32_t)*(uint8_t *)((char *)arg1 + 0x3c),
                                 i_3, s2[0]);
            s2 = &s2[1];
        }
    }

    if ((uint32_t)*(uint8_t *)((char *)arg1 + 0x3c) != 0) {
        uint8_t *s2_1 = (uint8_t *)arg1 + 0x35e4;
        int32_t i_1 = 0;

        do {
            UpdateHwRateCtrlParam(s2_1, &arg2[2], &arg2[0x12]);
            i_1 += 1;
            s2_1 += 0x78;
        } while (i_1 < (int32_t)(uint32_t)*(uint8_t *)((char *)arg1 + 0x3c));
    }

    *(int32_t *)((char *)arg1 + 0xa8) = arg2[0x12];
    *(int32_t *)((char *)arg1 + 0xac) = arg2[0x13];
    *(int32_t *)((char *)arg1 + 0xb0) = arg2[0x14];
    *(int32_t *)((char *)arg1 + 0xb4) = arg2[0x15];
    *(int32_t *)((char *)arg1 + 0xb8) = arg2[0x16];
    *(int32_t *)((char *)arg1 + 0xbc) = arg2[0x17];
    *(int32_t *)((char *)arg1 + 0xc0) = arg2[0x18];

    {
        int32_t *i_2 = &arg2[2];
        int32_t *s0_1 = (int32_t *)((char *)arg1 + 0x68);
        int32_t v0_8 = 0;

        do {
            int32_t a2_5 = i_2[0];
            int32_t a0_7 = i_2[1];
            int32_t v1_2 = i_2[2];

            v0_8 = i_2[3];
            i_2 = &i_2[4];
            s0_1[0] = a2_5;
            s0_1[1] = a0_7;
            s0_1[2] = v1_2;
            s0_1[3] = v0_8;
            s0_1 = &s0_1[4];
            if (i_2 == &arg2[0x12]) {
                return (uint32_t)v0_8;
            }
        } while (1);
    }
}

uint32_t AL_ApplyGmvCommands(void *arg1, int32_t *arg2)
{
    int32_t result = *arg2 & 0x20;

    if (result != 0) {
        return AL_GmvMngr_AddGMV(arg1, arg2[0x1d], *(int16_t *)&arg2[0x1e],
                                 *(int16_t *)((char *)arg2 + 0x7a));
    }

    return (uint32_t)result;
}

int32_t AL_UpdateAutoQpCtrl(int16_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4, char arg5,
                            char arg6, char arg7, char arg8, char arg9)
{
    uint32_t v0 = (uint32_t)(uint8_t)arg8;

    if ((uint32_t)(uint8_t)arg7 != 0) {
        const void *a1;

        if ((uint32_t)(uint8_t)arg9 != 0) {
            a1 = DELTA0_QP_CTRL_TABLE;
        } else if (v0 != 0) {
            a1 = DEFAULT_QP_CTRL_TABLE_VP9_2;
        } else {
            a1 = DEFAULT_QP_CTRL_TABLE;
        }

        Rtos_Memcpy(arg1, a1, 0x14);
        *((uint8_t *)arg1 + 0x13) = (uint8_t)arg6;
        ((uint8_t *)arg1)[9] = (uint8_t)arg5;
        return (uint8_t)arg5;
    }

    if (v0 != 0) {
        arg4 /= 5;
    }

    if (!(arg4 < 0x34)) {
        return AL_GetLambda((int32_t)(intptr_t)__assert(
                                "iSliceQP < 52",
                                "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/UtilsQp.c",
                                0x31, "AL_UpdateAutoQpCtrl", &_gp),
                            0, 0, 0, arg1, 0, 0);
    }

    {
        int32_t lo_2 = arg2 / arg3;
        uint32_t a1_1;
        int32_t t6;
        int32_t t5;
        int32_t a0_1;
        int32_t t4;
        int32_t v1_5;
        int32_t t7;
        int32_t a2_3;
        int32_t a0_5;
        int32_t t0_7;
        int32_t a0_6;
        int32_t v1_8;
        int32_t a1_2;
        int32_t v0_5;

        if (arg3 == 0) {
            __builtin_trap();
        }
        *((uint8_t *)arg1 + 0xd) = 2;
        ((uint8_t *)arg1)[7] = 3;
        *((uint8_t *)arg1 + 0x11) = 3;
        a1_1 = AL_LAMBDAs_AUTO_QP[arg4];
        ((uint8_t *)arg1)[8] = 2;
        ((uint8_t *)arg1)[9] = (uint8_t)arg5;
        ((uint8_t *)arg1)[6] = 1;
        *((uint8_t *)arg1 + 0xf) = 1;
        *((uint8_t *)arg1 + 0x13) = (uint8_t)arg6;
        t6 = lo_2 >> 5;
        t5 = lo_2 >> 1;
        a0_1 = lo_2 >> 4;
        t4 = lo_2 << 1;
        v1_5 = lo_2 >> 6;
        t7 = lo_2 >> 7;
        a2_3 = (t5 - lo_2) * (int32_t)a1_1 + lo_2;
        a0_5 = ((lo_2 >> 2) + a0_1 + t6 + t7 - lo_2) * (int32_t)a1_1 + lo_2;
        if (a2_3 >= 0x10000) {
            a2_3 = 0xffff;
        }
        if (a2_3 < 0) {
            a2_3 = 0;
        }
        arg1[0] = (int16_t)a2_3;
        t0_7 = (a0_1 + t6 + v1_5 + t7 - lo_2) * (int32_t)a1_1 + lo_2;
        if (a0_5 >= 0x10000) {
            a0_5 = 0xffff;
        }
        if (a0_5 < 0) {
            a0_5 = 0;
        }
        arg1[1] = (int16_t)a0_5;
        a0_6 = (lo_2 + t5 + (lo_2 >> 3) + t6 - lo_2) * (int32_t)a1_1 + lo_2;
        if (t0_7 >= 0x10000) {
            t0_7 = 0xffff;
        }
        if (t0_7 < 0) {
            t0_7 = 0;
        }
        arg1[2] = (int16_t)t0_7;
        v1_8 = (a0_1 + t4 + t6 + v1_5 - lo_2) * (int32_t)a1_1 + lo_2;
        if (a0_6 >= 0x10000) {
            a0_6 = 0xffff;
        }
        if (a0_6 < 0) {
            a0_6 = 0;
        }
        arg1[3] = (int16_t)a0_6;
        a1_2 = (t5 + t4 - lo_2) * (int32_t)a1_1 + lo_2;
        if (v1_8 >= 0x10000) {
            v1_8 = 0xffff;
        }
        if (a1_2 >= 0x10000) {
            a1_2 = 0xffff;
        }
        if (v1_8 < 0) {
            v1_8 = 0;
        }
        v0_5 = a1_2 < 0 ? 1 : 0;
        if (v0_5 != 0) {
            a1_2 = 0;
        }
        arg1[4] = (int16_t)v1_8;
        arg1[5] = (int16_t)a1_2;
        return v0_5;
    }
}

int32_t AL_GetLambda(int32_t arg1, void *arg2, int32_t arg3, char arg4, void *arg12,
                     void *arg13, char arg14)
{
    uint8_t *t1 = (uint8_t *)arg13;
    uint32_t t7 = (uint32_t)(uint8_t)arg14;
    int32_t t5_1;
    const uint8_t *t8_1;
    int32_t t9_1;

    if (t1 == 0) {
        __assert(data_e59a0,
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChooseLda.c",
                 0x76, "AL_GetLambda", &_gp);
    } else {
        int32_t v0_1 = 1;

        if (arg3 != 1) {
            int32_t v1_1 = 2;

            if (arg3 == 0) {
                uint32_t a3_1 = (uint32_t)(uint8_t)arg4;

                v0_1 = 2;
                if (a3_1 != 0) {
                    v0_1 = (int32_t)a3_1 + 1;
                }
            } else {
                if (arg3 == 2) {
                    v1_1 = 0;
                }
                v0_1 = v1_1;
            }
        }

        t5_1 = *(int32_t *)((char *)arg12 + ((v0_1 + 0x30) << 2) + 0xc);
        if (arg1 == 2) {
            t8_1 = HEVC_NEW_DEFAULT_LDA_TABLE;
            t9_1 = 0x2aaaaaab;
            goto label_70498;
        }

        if (arg1 != 0x80) {
            if (arg1 != 0) {
                return 0;
            }

            {
                uint32_t v0_7 = (uint32_t)*(uint8_t *)((char *)arg12 + 0x1f);

                if (v0_7 == 0) {
                    Rtos_Memcpy(t1, AVC_DEFAULT_LDA_TABLE, 0xd0);
                    return 1;
                }
                if (v0_7 != 1) {
                    return 1;
                }
                Rtos_Memcpy(t1, HEVC_NEW_DEFAULT_LDA_TABLE, 0xd0);
                return 1;
            }
        }

        if (arg2 != 0) {
            uint8_t *t1_1 = t1 + arg3;
            uint8_t *a2 = (uint8_t *)arg2 + arg3;
            uint8_t *a1_1 = &t1_1[0xd0];

            while (1) {
                int32_t v0_12 = (uint32_t)(*a2) * t5_1;
                int32_t v0_9;

                if (v0_12 < 0) {
                    v0_12 += 0xff;
                }
                v0_9 = v0_12 >> 8;
                if (v0_9 > 0) {
                    if (v0_9 >= 0x100) {
                        v0_9 = 0xff;
                    }
                    *t1_1 = (uint8_t)(v0_9 & 0xff);
                    t1_1 = &t1_1[4];
                    a2 = &a2[4];
                    if (t1_1 == a1_1) {
                        break;
                    }
                } else {
                    *t1_1 = 1;
                    t1_1 = &t1_1[4];
                    a2 = &a2[4];
                    if (t1_1 == a1_1) {
                        break;
                    }
                }
            }

            return 1;
        }
    }

    {
        int32_t v0_23 = 0xccc;
        int32_t t6_1;
        int32_t arg5 = 0;
        int32_t arg6 = 0x400a;
        int32_t arg7 = 3;
        int32_t arg8 = 4;
        int32_t arg9 = t5_1 << 1;
        int32_t arg10 = 0xffff0000;

        __assert("pLoadEP1",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/ChooseLda.c",
                 0x8c, "AL_GetLambda", &_gp);
        if ((*(uint32_t *)((char *)arg12 + 0xa8) & 8) != 0) {
            goto label_70658;
        }
        v0_23 = -(int32_t)(uint32_t)*(uint16_t *)((char *)arg12 + 0xae) * 0xcd + 0x1000;
label_70658:
        {
            int32_t v0_29 = t5_1 * v0_23;

            if (v0_29 < 0) {
                v0_29 += 0xfff;
            }
            t6_1 = v0_29 >> 0xc;
        }

        while (1) {
            int32_t *t2_1;
            uint8_t *t3_1;
            int32_t t4_1;
            int32_t t0_1;

label_704c4:
            t2_1 = (int32_t *)((char *)HEVC_NEW_DEFAULT_LDA_TABLE + 0xffffffffffffff34LL);
            t3_1 = t1 + arg5;
            t4_1 = arg10 + 0x4000;
            t0_1 = 0x10;
            while (1) {
                int32_t v0_18 = t6_1;

                if (t7 != 0) {
                    int64_t product = (int64_t)t4_1 * (int64_t)t9_1;
                    int32_t v1_5 = (int32_t)(product >> 32) - (t4_1 >> 0x1f);
                    int32_t v0_15 = 0x2000;
                    int32_t v0_17;
                    uint32_t a0_5 = 1;
                    uint32_t a1_3 = 0xff;
                    int32_t a3 = 2;

                    if (v1_5 >= 0x2000) {
                        if (v1_5 >= 0x400b) {
                            v1_5 = arg6;
                        }
                        v0_15 = v1_5;
                    }
                    v0_17 = t6_1 * v0_15;
                    if (v0_17 < 0) {
                        v0_17 += 0xfff;
                    }
                    v0_18 = v0_17 >> 0xc;
                    while (a3 != (int32_t)a1_3) {
                        while (1) {
                            uint32_t v0_20 = (a0_5 + a1_3) >> 1;

                            if (((uint32_t)(v0_18 * t0_1) >> 0x10) < v0_20 * v0_20) {
                                a1_3 = v0_20;
                                break;
                            }
                            a3 = (int32_t)v0_20 + 1;
                            a0_5 = v0_20;
                            if (a3 == (int32_t)a1_3) {
                                break;
                            }
                        }
                    }
                    *t3_1 = (uint8_t)a0_5;
                } else {
                    *t3_1 = 1;
                }
                t4_1 += 0x1000;
                t3_1 = &t3_1[4];
                if (t8_1 == (const uint8_t *)t2_1) {
                    break;
                }
                t0_1 = *t2_1;
                t2_1 = &t2_1[1];
            }

            arg5 += 1;
            if (arg5 == arg8) {
                return 1;
            }
            if (arg3 == arg5) {
                if (arg3 == arg7) {
                    t6_1 = 0x5a;
                    goto label_704c4;
                }
                if (arg3 == 2) {
                    goto label_70630;
                }
                if (arg3 != 0) {
                    t6_1 = t5_1;
                    goto label_704c4;
                }
                if ((*(uint32_t *)((char *)arg12 + 0xa8) & 2) != 0) {
                    t6_1 = arg9;
                } else {
                    t6_1 = t5_1;
                }
                goto label_704c4;
            }
            if (arg5 == arg7) {
                if (arg3 != 2) {
                    return 1;
                }
                t6_1 = 0x5a;
                goto label_704c4;
            }
        }
    }

label_70498:
    if (arg3 == 2) {
label_70630:
        {
            int32_t v0_23 = 0xccc;
            int32_t t6_1;
            int32_t arg5 = 0;
            int32_t arg6 = 0x400a;
            int32_t arg7 = 3;
            int32_t arg8 = 4;
            int32_t arg9 = t5_1 << 1;
            int32_t arg10 = 0xffff0000;

            if ((*(uint32_t *)((char *)arg12 + 0xa8) & 8) == 0) {
                v0_23 = -(int32_t)(uint32_t)*(uint16_t *)((char *)arg12 + 0xae) * 0xcd +
                        0x1000;
            }
            {
                int32_t v0_29 = t5_1 * v0_23;

                if (v0_29 < 0) {
                    v0_29 += 0xfff;
                }
                t6_1 = v0_29 >> 0xc;
            }

            while (1) {
                int32_t *t2_1 = (int32_t *)((char *)HEVC_NEW_DEFAULT_LDA_TABLE +
                                            0xffffffffffffff34LL);
                uint8_t *t3_1 = t1 + arg5;
                int32_t t4_1 = arg10 + 0x4000;
                int32_t t0_1 = 0x10;

                while (1) {
                    int32_t v0_18 = t6_1;

                    if (t7 != 0) {
                        int64_t product = (int64_t)t4_1 * (int64_t)t9_1;
                        int32_t v1_5 = (int32_t)(product >> 32) - (t4_1 >> 0x1f);
                        int32_t v0_15 = 0x2000;
                        int32_t v0_17;
                        uint32_t a0_5 = 1;
                        uint32_t a1_3 = 0xff;
                        int32_t a3 = 2;

                        if (v1_5 >= 0x2000) {
                            if (v1_5 >= 0x400b) {
                                v1_5 = arg6;
                            }
                            v0_15 = v1_5;
                        }
                        v0_17 = t6_1 * v0_15;
                        if (v0_17 < 0) {
                            v0_17 += 0xfff;
                        }
                        v0_18 = v0_17 >> 0xc;
                        while (a3 != (int32_t)a1_3) {
                            while (1) {
                                uint32_t v0_20 = (a0_5 + a1_3) >> 1;

                                if (((uint32_t)(v0_18 * t0_1) >> 0x10) <
                                    v0_20 * v0_20) {
                                    a1_3 = v0_20;
                                    break;
                                }
                                a3 = (int32_t)v0_20 + 1;
                                a0_5 = v0_20;
                                if (a3 == (int32_t)a1_3) {
                                    break;
                                }
                            }
                        }
                        *t3_1 = (uint8_t)a0_5;
                    } else {
                        *t3_1 = 1;
                    }
                    t4_1 += 0x1000;
                    t3_1 = &t3_1[4];
                    if (t8_1 == (const uint8_t *)t2_1) {
                        break;
                    }
                    t0_1 = *t2_1;
                    t2_1 = &t2_1[1];
                }

                arg5 += 1;
                if (arg5 == arg8) {
                    return 1;
                }
                if (arg3 == arg5) {
                    if (arg3 == arg7) {
                        t6_1 = 0x5a;
                    } else if (arg3 == 2) {
                        continue;
                    } else if (arg3 != 0) {
                        t6_1 = t5_1;
                    } else if ((*(uint32_t *)((char *)arg12 + 0xa8) & 2) != 0) {
                        t6_1 = arg9;
                    } else {
                        t6_1 = t5_1;
                    }
                } else {
                    if (arg5 == arg7) {
                        if (arg3 != 2) {
                            return 1;
                        }
                        t6_1 = 0x5a;
                    } else {
                        continue;
                    }
                }

                {
                    int32_t *t2_1 = (int32_t *)((char *)HEVC_NEW_DEFAULT_LDA_TABLE +
                                                0xffffffffffffff34LL);
                    uint8_t *t3_1 = t1 + arg5;
                    int32_t t4_1 = arg10 + 0x4000;
                    int32_t t0_1 = 0x10;

                    while (1) {
                        int32_t v0_18 = t6_1;

                        if (t7 != 0) {
                            int64_t product = (int64_t)t4_1 * (int64_t)t9_1;
                            int32_t v1_5 = (int32_t)(product >> 32) - (t4_1 >> 0x1f);
                            int32_t v0_15 = 0x2000;
                            int32_t v0_17;
                            uint32_t a0_5 = 1;
                            uint32_t a1_3 = 0xff;
                            int32_t a3 = 2;

                            if (v1_5 >= 0x2000) {
                                if (v1_5 >= 0x400b) {
                                    v1_5 = arg6;
                                }
                                v0_15 = v1_5;
                            }
                            v0_17 = t6_1 * v0_15;
                            if (v0_17 < 0) {
                                v0_17 += 0xfff;
                            }
                            v0_18 = v0_17 >> 0xc;
                            while (a3 != (int32_t)a1_3) {
                                while (1) {
                                    uint32_t v0_20 = (a0_5 + a1_3) >> 1;

                                    if (((uint32_t)(v0_18 * t0_1) >> 0x10) <
                                        v0_20 * v0_20) {
                                        a1_3 = v0_20;
                                        break;
                                    }
                                    a3 = (int32_t)v0_20 + 1;
                                    a0_5 = v0_20;
                                    if (a3 == (int32_t)a1_3) {
                                        break;
                                    }
                                }
                            }
                            *t3_1 = (uint8_t)a0_5;
                        } else {
                            *t3_1 = 1;
                        }
                        t4_1 += 0x1000;
                        t3_1 = &t3_1[4];
                        if (t8_1 == (const uint8_t *)t2_1) {
                            break;
                        }
                        t0_1 = *t2_1;
                        t2_1 = &t2_1[1];
                    }
                }
            }
        }
    }

    {
        int32_t v0_23 = 0xccc;
        int32_t t6_1;
        int32_t arg5 = 0;
        int32_t arg6 = 0x400a;
        int32_t arg7 = 3;
        int32_t arg8 = 4;
        int32_t arg9 = t5_1 << 1;
        int32_t arg10 = 0xffff0000;

        if ((*(uint32_t *)((char *)arg12 + 0xa8) & 8) == 0) {
            v0_23 = -(int32_t)(uint32_t)*(uint16_t *)((char *)arg12 + 0xae) * 0xcd + 0x1000;
        }
        {
            int32_t v0_29 = t5_1 * v0_23;

            if (v0_29 < 0) {
                v0_29 += 0xfff;
            }
            t6_1 = v0_29 >> 0xc;
        }

        while (1) {
            int32_t *t2_1 = (int32_t *)((char *)HEVC_NEW_DEFAULT_LDA_TABLE +
                                        0xffffffffffffff34LL);
            uint8_t *t3_1 = t1 + arg5;
            int32_t t4_1 = arg10 + 0x4000;
            int32_t t0_1 = 0x10;

            while (1) {
                int32_t v0_18 = t6_1;

                if (t7 != 0) {
                    int64_t product = (int64_t)t4_1 * (int64_t)t9_1;
                    int32_t v1_5 = (int32_t)(product >> 32) - (t4_1 >> 0x1f);
                    int32_t v0_15 = 0x2000;
                    int32_t v0_17;
                    uint32_t a0_5 = 1;
                    uint32_t a1_3 = 0xff;
                    int32_t a3 = 2;

                    if (v1_5 >= 0x2000) {
                        if (v1_5 >= 0x400b) {
                            v1_5 = arg6;
                        }
                        v0_15 = v1_5;
                    }
                    v0_17 = t6_1 * v0_15;
                    if (v0_17 < 0) {
                        v0_17 += 0xfff;
                    }
                    v0_18 = v0_17 >> 0xc;
                    while (a3 != (int32_t)a1_3) {
                        while (1) {
                            uint32_t v0_20 = (a0_5 + a1_3) >> 1;

                            if (((uint32_t)(v0_18 * t0_1) >> 0x10) < v0_20 * v0_20) {
                                a1_3 = v0_20;
                                break;
                            }
                            a3 = (int32_t)v0_20 + 1;
                            a0_5 = v0_20;
                            if (a3 == (int32_t)a1_3) {
                                break;
                            }
                        }
                    }
                    *t3_1 = (uint8_t)a0_5;
                } else {
                    *t3_1 = 1;
                }
                t4_1 += 0x1000;
                t3_1 = &t3_1[4];
                if (t8_1 == (const uint8_t *)t2_1) {
                    break;
                }
                t0_1 = *t2_1;
                t2_1 = &t2_1[1];
            }

            arg5 += 1;
            if (arg5 == arg8) {
                return 1;
            }
            if (arg3 == arg5) {
                if (arg3 == arg7) {
                    t6_1 = 0x5a;
                } else if (arg3 == 2) {
                    continue;
                } else if (arg3 != 0) {
                    t6_1 = t5_1;
                } else if ((*(uint32_t *)((char *)arg12 + 0xa8) & 2) != 0) {
                    t6_1 = arg9;
                } else {
                    t6_1 = t5_1;
                }
            } else {
                if (arg5 == arg7) {
                    if (arg3 != 2) {
                        return 1;
                    }
                    t6_1 = 0x5a;
                } else {
                    continue;
                }
            }

            {
                int32_t *t2_1 = (int32_t *)((char *)HEVC_NEW_DEFAULT_LDA_TABLE +
                                            0xffffffffffffff34LL);
                uint8_t *t3_1 = t1 + arg5;
                int32_t t4_1 = arg10 + 0x4000;
                int32_t t0_1 = 0x10;

                while (1) {
                    int32_t v0_18 = t6_1;

                    if (t7 != 0) {
                        int64_t product = (int64_t)t4_1 * (int64_t)t9_1;
                        int32_t v1_5 = (int32_t)(product >> 32) - (t4_1 >> 0x1f);
                        int32_t v0_15 = 0x2000;
                        int32_t v0_17;
                        uint32_t a0_5 = 1;
                        uint32_t a1_3 = 0xff;
                        int32_t a3 = 2;

                        if (v1_5 >= 0x2000) {
                            if (v1_5 >= 0x400b) {
                                v1_5 = arg6;
                            }
                            v0_15 = v1_5;
                        }
                        v0_17 = t6_1 * v0_15;
                        if (v0_17 < 0) {
                            v0_17 += 0xfff;
                        }
                        v0_18 = v0_17 >> 0xc;
                        while (a3 != (int32_t)a1_3) {
                            while (1) {
                                uint32_t v0_20 = (a0_5 + a1_3) >> 1;

                                if (((uint32_t)(v0_18 * t0_1) >> 0x10) < v0_20 * v0_20) {
                                    a1_3 = v0_20;
                                    break;
                                }
                                a3 = (int32_t)v0_20 + 1;
                                a0_5 = v0_20;
                                if (a3 == (int32_t)a1_3) {
                                    break;
                                }
                            }
                        }
                        *t3_1 = (uint8_t)a0_5;
                    } else {
                        *t3_1 = 1;
                    }
                    t4_1 += 0x1000;
                    t3_1 = &t3_1[4];
                    if (t8_1 == (const uint8_t *)t2_1) {
                        break;
                    }
                    t0_1 = *t2_1;
                    t2_1 = &t2_1[1];
                }
            }
        }
    }
}

uint32_t GetSliceEnc2CmdOffset(char arg1, char arg2, char arg3)
{
    uint32_t a1 = (uint32_t)(uint8_t)arg2;
    uint32_t a0 = (uint32_t)(uint8_t)arg1;

    if (a0 == 0) {
        __builtin_trap();
    }
    return a1 - (a1 - (uint32_t)(uint8_t)arg3 - 1) / a0 - 1;
}

uint32_t GetCoreFirstEnc2CmdOffset(char arg1, char arg2, char arg3)
{
    uint32_t a0 = (uint32_t)(uint8_t)arg1;
    uint32_t a1 = (uint32_t)(uint8_t)arg2;

    if (a0 == 0) {
        __builtin_trap();
    }
    return a1 - (a1 + a0 - (uint32_t)(uint8_t)arg3 - 1) / a0;
}

uint32_t AL_CoreConstraintEnc_Init(int32_t *arg1, int32_t arg2, int32_t arg3)
{
    int32_t v1 = 0x100;
    int32_t v0;
    int32_t a2_1;
    int32_t a3;

    if (arg3 == 4) {
        v0 = 0x4000;
        a3 = 0x600;
        a2_1 = 0;
    } else {
        if (arg3 != 1) {
            v1 = 0;
        }
        v0 = 0xa20;
        a3 = 0xd48;
        a2_1 = v1;
    }

    return AL_CoreConstraint_Init(arg1, arg2, 0xa, a3, a2_1, v0);
}

uint32_t AL_CoreConstraintEnc_GetExpectedNumberOfCores(int32_t *arg1, void *arg2)
{
    int32_t v0_1 =
        (int32_t)AL_CoreConstraint_GetExpectedNumberOfCores(arg1,
                                                            (uint32_t)*(uint16_t *)((char *)arg2 + 4),
                                                            (uint32_t)*(uint16_t *)((char *)arg2 + 6),
                                                            (uint32_t)*(uint16_t *)((char *)arg2 + 0x74) *
                                                                0x3e8,
                                                            (uint32_t)*(uint16_t *)((char *)arg2 + 0x76));
    int32_t a0 = *arg1;
    int32_t v1_2;

    if (a0 > 0) {
        v1_2 = (uint32_t)*(uint16_t *)((char *)arg2 + 4) / a0;
        if (0 >= v1_2) {
            v1_2 = 1;
        }
        if (v1_2 < v0_1) {
            return (uint32_t)v1_2;
        }
        return (uint32_t)v0_1;
    }

    if (a0 == 0) {
        __builtin_trap();
    }
    return (uint32_t)v0_1;
}

uint32_t AL_CoreConstraintEnc_GetResources(int32_t arg3, int32_t arg4, char arg5, char arg6)
{
    return AL_GetResources(arg3, arg4, (uint32_t)(uint8_t)arg5 * 0x3e8,
                           (uint32_t)(uint8_t)arg6);
}

void *AL_GmvMngr_Init(uint8_t *arg1)
{
    int16_t *i;

    *(int32_t *)(arg1 + 0xcc) = 0;
    i = (int16_t *)(arg1 + 8);
    do {
        i[0] = 0xff;
        i[1] = 0xff;
        i[-8] = 0xff;
        i[-4] = 0xff;
        i = &i[6];
    } while ((uint8_t *)i != arg1 + 0xd4);

    return i;
}

uint32_t AL_GmvMngr_UpdateGMVPoc(int32_t *arg1, int32_t arg2, int32_t arg3)
{
    int32_t *v1 = arg1;
    int32_t i = 0;

    while (i != 0x11) {
        int32_t a3_1 = *v1;

        v1 = &v1[3];
        if (a3_1 == arg2) {
            arg1[i * 3 + 1] = arg3;
            return 1;
        }
        i += 1;
    }

    return 0;
}

uint32_t AL_GmvMngr_AddGMV(void *arg1, int32_t arg2, int16_t arg3, int16_t arg4)
{
    int32_t v1_3 = *(int32_t *)((char *)arg1 + 0xcc);
    uint8_t *v0_2 = (uint8_t *)arg1 + v1_3 * 0xc;

    *(int32_t *)v0_2 = arg2;
    *(int16_t *)(v0_2 + 8) = arg3;
    *(int16_t *)(v0_2 + 0xa) = arg4;
    *(int32_t *)((char *)arg1 + 0xcc) = (v1_3 + 1) % 0x11;
    return 1;
}

int32_t AL_GmvMngr_SumGMV(int32_t *arg1, int32_t arg2, int32_t arg3, int16_t *arg4,
                          int16_t *arg5, char arg6)
{
    int32_t v0_1;

    if (arg3 != arg2) {
        while (1) {
            uint8_t *t0_4 = (uint8_t *)arg1 + arg3 * 0xc;
            int16_t v0_8 = *arg4;
            int32_t v1_2;

            if ((uint32_t)(uint8_t)arg6 != 0) {
                *arg4 = (int16_t)(v0_8 - *(int16_t *)(t0_4 + 8));
                v1_2 = arg3 - 1;
                if (arg3 == 0) {
                    v1_2 = 0x10;
                }
                arg3 = v1_2;
                *arg5 = (int16_t)(*arg5 + -(int16_t)*(uint16_t *)(t0_4 + 0xa));
                if (v1_2 == arg2) {
                    break;
                }
            } else {
                *arg4 = (int16_t)(v0_8 + *(int16_t *)(t0_4 + 8));
                v1_2 = arg3 - 1;
                if (arg3 == 0) {
                    v1_2 = 0x10;
                }
                arg3 = v1_2;
                *arg5 = (int16_t)(*arg5 + *(int16_t *)(t0_4 + 0xa));
                if (v1_2 == arg2) {
                    break;
                }
            }
        }
    }

    v0_1 = *arg4;
    if (v0_1 >= 0x401) {
        v0_1 = 0x400;
    }
    if (v0_1 < -0x400) {
        v0_1 = -0x400;
    }
    *arg4 = (int16_t)v0_1;
    v0_1 = *arg5;
    if (v0_1 >= 0x401) {
        v0_1 = 0x400;
    }
    if (v0_1 < -0x400) {
        v0_1 = -0x400;
    }
    *arg5 = (int16_t)v0_1;
    return v0_1;
}

uint32_t AL_GmvMngr_GetGMVIdx(int32_t *arg1, int32_t arg2, char arg3, int32_t *arg4)
{
    int32_t *a0 = arg1 + 4;
    int32_t i = 0;

    *arg4 = 0;
    while (i != 0x11) {
        if ((uint32_t)(uint8_t)arg3 == 0) {
            if (*(a0 - 4) == arg2) {
                break;
            }
            i += 1;
            *arg4 = i;
            a0 = &a0[3];
            return i < 0x11 ? 1 : 0;
        }
        i += 1;
        if (arg2 == *a0) {
            return i - 1 < 0x11 ? 1 : 0;
        }
    }
    return 0;
}

uint32_t AL_GmvMngr_GetGMV(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4,
                           int32_t arg5, int16_t *arg6, char arg7)
{
    int32_t var_28 = 0;
    int32_t var_2c = 0;

    arg6[3] = 0;
    arg6[2] = 0;
    arg6[1] = 0;
    arg6[0] = 0;

    if (arg2 != 2) {
        if (AL_GmvMngr_GetGMVIdx(arg1, arg3, 1, &var_28) == 0) {
            return 0;
        }
        if (AL_GmvMngr_GetGMVIdx(arg1, arg4, 0, &var_2c) == 0) {
            return 0;
        }
        AL_GmvMngr_SumGMV(arg1, var_2c, var_28, arg6, &arg6[1], 0);
        if ((uint32_t)(uint8_t)arg7 != 0) {
            int32_t var_30 = 0;
            int32_t v0_8 = (int32_t)AL_GmvMngr_GetGMVIdx(arg1, arg5, 0, &var_30);

            if (v0_8 == 0) {
                return 0;
            }
            AL_GmvMngr_SumGMV(arg1, var_30, var_28, &arg6[2], &arg6[3], 0);
            return (uint32_t)v0_8;
        }
        if (arg2 == 0) {
            int32_t var_30 = 0;
            int32_t v0_6 = (int32_t)AL_GmvMngr_GetGMVIdx(arg1, arg5, 0, &var_30);

            if (v0_6 == 0) {
                return 0;
            }
            AL_GmvMngr_SumGMV(arg1, var_28, var_30, &arg6[2], &arg6[3], 1);
            return (uint32_t)v0_6;
        }
        return 1;
    }

    if (AL_GmvMngr_GetGMVIdx(arg1, arg3, 1, &var_28) == 0) {
        return 0;
    }
    return 0;
}
