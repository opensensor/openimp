#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "alcodec/al_buffer.h"
#include "alcodec/al_metadata.h"
#include "alcodec/al_rtos.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-truncation"

extern void __assert(const char *assertion, const char *filename, int line, const char *function);

int AL_Codec_Encode_SetDefaultParam(void *param);
int AL_Codec_Encode_Create(void **codec, void *params);
int AL_Codec_Encode_Destroy(void *codec);
int AL_Codec_Encode_Process(void *codec, void *frame, void *user_data);
int AL_Codec_Encode_GetStream(void *codec, void **stream, void **user_data);
int AL_Codec_Encode_ReleaseStream(void *codec, void *stream, void *user_data);

/* +0x00/+0x01: signed clamp range bytes */
#define ROI_MNGR_MIN_QP(ctx) (*(int8_t *)((char *)(ctx) + 0x00))
#define ROI_MNGR_MAX_QP(ctx) (*(int8_t *)((char *)(ctx) + 0x01))
/* +0x04/+0x08: original pixel width/height */
#define ROI_MNGR_WIDTH(ctx) (*(int32_t *)((char *)(ctx) + 0x04))
#define ROI_MNGR_HEIGHT(ctx) (*(int32_t *)((char *)(ctx) + 0x08))
/* +0x0c/+0x10/+0x14: block-grid width/height/count */
#define ROI_MNGR_BLK_W(ctx) (*(int32_t *)((char *)(ctx) + 0x0c))
#define ROI_MNGR_BLK_H(ctx) (*(int32_t *)((char *)(ctx) + 0x10))
#define ROI_MNGR_BLK_COUNT(ctx) (*(int32_t *)((char *)(ctx) + 0x14))
/* +0x18: block-shift amount, +0x1c: default fill value, +0x20: insertion mode */
#define ROI_MNGR_SHIFT(ctx) (*(int32_t *)((char *)(ctx) + 0x18))
#define ROI_MNGR_DEFAULT_QP(ctx) (*(int32_t *)((char *)(ctx) + 0x1c))
#define ROI_MNGR_ORDER_MODE(ctx) (*(int32_t *)((char *)(ctx) + 0x20))
/* +0x24/+0x28: first/last ROI node */
#define ROI_MNGR_FIRST(ctx) (*(void **)((char *)(ctx) + 0x24))
#define ROI_MNGR_LAST(ctx) (*(void **)((char *)(ctx) + 0x28))

/* +0x00/+0x04: next/prev list links */
#define ROI_NODE_NEXT(node) (*(void **)((char *)(node) + 0x00))
#define ROI_NODE_PREV(node) (*(void **)((char *)(node) + 0x04))
/* +0x08/+0x0c/+0x10/+0x14: x/y/w/h in block units */
#define ROI_NODE_X(node) (*(int32_t *)((char *)(node) + 0x08))
#define ROI_NODE_Y(node) (*(int32_t *)((char *)(node) + 0x0c))
#define ROI_NODE_W(node) (*(int32_t *)((char *)(node) + 0x10))
#define ROI_NODE_H(node) (*(int32_t *)((char *)(node) + 0x14))
/* +0x18: signed ROI QP byte */
#define ROI_NODE_QP(node) (*(int8_t *)((char *)(node) + 0x18))

/* +0x10/+0x14 on stream metadata: section array/count */
#define STREAM_META_SECTIONS(meta) (*(uint8_t **)((char *)(meta) + 0x10))
#define STREAM_META_SECTION_COUNT(meta) (*(int32_t *)((char *)(meta) + 0x14))
/* each section is 0x14 bytes; +0x00 offset, +0x04 length */
#define STREAM_SECTION_OFFSET(sec) (*(int32_t *)((char *)(sec) + 0x00))
#define STREAM_SECTION_LENGTH(sec) (*(int32_t *)((char *)(sec) + 0x04))

static char *UpdateTransitionHorz(void *arg1, char *arg2, char *arg3, int32_t arg4, int32_t arg5,
                                  int32_t arg6, int32_t arg7, int32_t arg8, char arg9);
static char *UpdateTransitionVert(void *arg1, char *arg2, char *arg3, int32_t arg4, int32_t arg5,
                                  int32_t arg6, int32_t arg7, char arg8);
static void *PushBack(void **arg1, void **arg2, void *arg3);

static char *UpdateTransitionHorz(void *arg1, char *arg2, char *arg3, int32_t arg4, int32_t arg5,
                                  int32_t arg6, int32_t arg7, int32_t arg8, char arg9)
{
    int32_t s1 = (int32_t)arg9;

    if (arg7 >= 2) {
        int32_t t0_1 = -arg5;
        int32_t v1_2 = ((int32_t)arg3[t0_1 * 2] + (s1 << 26 >> 26));
        char *t0_2 = arg2 + t0_1;
        int32_t v0_6 = (int32_t)ROI_MNGR_MIN_QP(arg1);
        int32_t t1_2 = ((uint32_t)v1_2 >> 31) + v1_2;
        int32_t t2_2;

        t1_2 >>= 1;
        t2_2 = (int32_t)ROI_MNGR_MAX_QP(arg1);

        if (t1_2 >= v0_6) {
            if (t1_2 >= t2_2)
                t1_2 = t2_2;
            v0_6 = t1_2;
        }

        {
            char v1_5 = (char)((v0_6 & 0x3f) | (*t0_2 & 0xc0));
            *t0_2 = v1_5;

            if (arg4 >= 2) {
                char *v0_8 = &t0_2[1];

                while (1) {
                    *v0_8 = v1_5;
                    v0_8 = &v0_8[1];
                    if (v0_8 == t0_2 + arg4)
                        break;
                    v1_5 = *t0_2;
                }
            }
        }
    }

    if (arg7 == 1) {
        int32_t t0_7 = -arg5;
        int32_t v1_25 = ((int32_t)arg3[t0_7] + (s1 << 26 >> 26));
        char *t0_8 = arg2 + t0_7;
        int32_t v0_26 = (int32_t)ROI_MNGR_MIN_QP(arg1);
        int32_t t1_13 = ((uint32_t)v1_25 >> 31) + v1_25;
        int32_t t2_5;

        t1_13 >>= 1;
        t2_5 = (int32_t)ROI_MNGR_MAX_QP(arg1);

        if (t1_13 >= v0_26) {
            if (t1_13 >= t2_5)
                t1_13 = t2_5;
            v0_26 = t1_13;
        }

        {
            char v1_28 = (char)((v0_26 & 0x3f) | (*t0_8 & 0xc0));
            *t0_8 = v1_28;

            if (arg4 >= 2) {
                char *v0_29 = &t0_8[1];

                while (1) {
                    *v0_29 = v1_28;
                    v0_29 = &v0_29[1];
                    if (v0_29 == t0_8 + arg4)
                        break;
                    v1_28 = *t0_8;
                }
            }
        }
    }

    if (arg8 > 0) {
        char *t3_2 = arg3;
        char *t0_3 = arg2 + 1;
        int32_t t2_3 = 0;

        do {
            int32_t v0_9 = (int32_t)ROI_MNGR_MIN_QP(arg1);
            int32_t t1_7 = (int32_t)(*t3_2) + (s1 << 26 >> 26);
            int32_t t1_9 = (((uint32_t)t1_7 >> 31) + t1_7) >> 1;
            int32_t s3_2 = (int32_t)ROI_MNGR_MAX_QP(arg1);

            if (t1_9 >= v0_9) {
                if (t1_9 >= s3_2)
                    t1_9 = s3_2;
                v0_9 = t1_9;
            }

            {
                char v1_8 = (char)((v0_9 & 0x3f) | (0xc0 & *(t0_3 - 1)));
                *(t0_3 - 1) = v1_8;

                if (arg4 >= 2) {
                    char *v0_12 = t0_3;

                    while (1) {
                        *v0_12 = v1_8;
                        v0_12 = &v0_12[1];
                        if (v0_12 == t0_3 + arg4 - 1)
                            break;
                        v1_8 = *(t0_3 - 1);
                    }
                }
            }

            t2_3 += 1;
            t3_2 = &t3_2[arg5];
            t0_3 = &t0_3[arg5];
        } while (arg8 != t2_3);
    }

    {
        int32_t s0_1 = arg7 + arg8;
        char *v0_14 = (s0_1 + 2 < arg6) ? (char *)1 : (char *)0;

        if (v0_14 == 0) {
            int32_t t4_2 = arg5 * arg8;

            if (s0_1 + 1 < arg6) {
                int32_t v0_18 = (int32_t)ROI_MNGR_MIN_QP(arg1);
                int32_t t0_6 = (int32_t)ROI_MNGR_MAX_QP(arg1);
                char *t4_3 = arg2 + t4_2;
                int32_t v1_19 = (int32_t)arg3[t4_2] + (s1 << 26 >> 26);
                int32_t a0_6 = (((uint32_t)v1_19 >> 31) + v1_19) >> 1;

                if (a0_6 >= v0_18) {
                    if (a0_6 >= t0_6)
                        a0_6 = t0_6;
                    v0_18 = a0_6;
                }

                {
                    char v1_22 = (char)((v0_18 & 0x3f) | (*t4_3 & 0xc0));
                    v0_14 = (arg4 < 2) ? (char *)1 : (char *)0;
                    *t4_3 = v1_22;

                    if (v0_14 == 0) {
                        char *v0_20 = &t4_3[1];

                        while (1) {
                            *v0_20 = v1_22;
                            v0_20 = &v0_20[1];
                            if (v0_20 == t4_3 + arg4)
                                break;
                            v1_22 = *t4_3;
                        }

                        return v0_20;
                    }
                }
            }

            return v0_14;
        } else {
            int32_t t5_1 = arg5 * arg8;
            int32_t v0_15 = (int32_t)ROI_MNGR_MIN_QP(arg1);
            int32_t t1_11 = (int32_t)ROI_MNGR_MAX_QP(arg1);
            char *a1 = arg2 + t5_1;
            int32_t v1_13 = (int32_t)arg3[t5_1 + arg5] + (s1 << 26 >> 26);
            int32_t a0_1 = (((uint32_t)v1_13 >> 31) + v1_13) >> 1;

            if (a0_1 >= v0_15) {
                if (a0_1 >= t1_11)
                    a0_1 = t1_11;
                v0_15 = a0_1;
            }

            {
                char v1_16 = (char)((v0_15 & 0x3f) | (*a1 & 0xc0));
                v0_14 = (arg4 < 2) ? (char *)1 : (char *)0;
                *a1 = v1_16;

                if (v0_14 == 0) {
                    v0_14 = &a1[1];

                    while (1) {
                        *v0_14 = v1_16;
                        v0_14 = &v0_14[1];
                        if (a1 + arg4 == v0_14)
                            break;
                        v1_16 = *a1;
                    }
                }
            }

            return v0_14;
        }
    }
}

static char *UpdateTransitionVert(void *arg1, char *arg2, char *arg3, int32_t arg4, int32_t arg5,
                                  int32_t arg6, int32_t arg7, char arg8)
{
    if (arg7 <= 0)
        return NULL;

    {
        int32_t t2_2 = arg6 * arg5;
        char *a1 = arg2 + 1;
        int32_t t1_1 = 0;

        do {
            int32_t v0_2 = (int32_t)ROI_MNGR_MIN_QP(arg1);
            int32_t a3_3 = (int32_t)(*arg3) + ((int32_t)arg8 << 26 >> 26);
            int32_t a3_5 = (((uint32_t)a3_3 >> 31) + a3_3) >> 1;
            int32_t t0_2 = (int32_t)ROI_MNGR_MAX_QP(arg1);

            if (a3_5 >= v0_2) {
                if (a3_5 >= t0_2)
                    a3_5 = t0_2;
                v0_2 = a3_5;
            }

            {
                char v1_3 = (char)((v0_2 & 0x3f) | (0xc0 & *(a1 - 1)));
                *(a1 - 1) = v1_3;

                if (arg4 >= 2) {
                    char *v0 = a1;

                    while (1) {
                        *v0 = v1_3;
                        v0 = &v0[1];
                        if (a1 + arg4 - 1 == v0)
                            break;
                        v1_3 = *(a1 - 1);
                    }
                }
            }

            t1_1 += 1;
            arg3 = &arg3[t2_2];
            a1 = &a1[t2_2];
        } while (arg7 != t1_1);
    }

    return NULL;
}

static void *PushBack(void **arg1, void **arg2, void *arg3)
{
    if (*arg1 == NULL) {
        int32_t v0_2 = (int32_t)(intptr_t)(*arg2);

        if (v0_2 == 0) {
            ROI_NODE_NEXT(arg3) = NULL;
            ROI_NODE_PREV(arg3) = NULL;
            *arg2 = arg3;
            *arg1 = arg3;
            return (void *)(intptr_t)v0_2;
        }

        __assert("pCtx->pLastNode",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/ROIMngr.c",
                 0x35, "PushBack");
        return NULL;
    }

    {
        void *v0_1 = *arg2;

        if (v0_1 != NULL) {
            ROI_NODE_NEXT(arg3) = v0_1;
            ROI_NODE_PREV(arg3) = NULL;
            *(void **)((char *)v0_1 + 0x04) = arg3;
            *arg2 = arg3;
            return v0_1;
        }
    }

    __assert("!pCtx->pLastNode",
             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/ROIMngr.c",
             0x3d, "PushBack");
    return NULL;
}

void *AL_RoiMngr_Create(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    void *result = Rtos_Malloc(0x2c);
    uint32_t a2 = (uint32_t)arg3 >> 24;

    if (result == NULL)
        return NULL;

    if (a2 == 0) {
        ROI_MNGR_MIN_QP(result) = (int8_t)0xe0;
        ROI_MNGR_MAX_QP(result) = 0x1f;
        ROI_MNGR_WIDTH(result) = arg1;
        ROI_MNGR_HEIGHT(result) = arg2;
        {
            int32_t a0 = -0x10;
            int32_t a1 = 0x10;
            int32_t v1 = 4;
            char a2_1 = 4;
            int32_t s1_4 = ((arg1 + a1 - 1) & a0) >> (v1 & 0x1f);
            int32_t s0_2 = ((arg2 + a1 - 1) & a0) >> (v1 & 0x1f);

            ROI_MNGR_SHIFT(result) = a2_1;
            ROI_MNGR_DEFAULT_QP(result) = arg4;
            ROI_MNGR_ORDER_MODE(result) = arg5;
            ROI_MNGR_FIRST(result) = NULL;
            ROI_MNGR_LAST(result) = NULL;
            ROI_MNGR_BLK_W(result) = s1_4;
            ROI_MNGR_BLK_H(result) = s0_2;
            ROI_MNGR_BLK_COUNT(result) = s1_4 * s0_2;
        }

        return result;
    }

    if (a2 == 1) {
        ROI_MNGR_MIN_QP(result) = (int8_t)0xe0;
        ROI_MNGR_MAX_QP(result) = 0x1f;
        ROI_MNGR_WIDTH(result) = arg1;
        ROI_MNGR_HEIGHT(result) = arg2;
        {
            int32_t a0 = -0x20;
            int32_t a1 = 0x20;
            int32_t v1 = 5;
            char a2_1 = 5;
            int32_t s1_4 = ((arg1 + a1 - 1) & a0) >> (v1 & 0x1f);
            int32_t s0_2 = ((arg2 + a1 - 1) & a0) >> (v1 & 0x1f);

            ROI_MNGR_SHIFT(result) = a2_1;
            ROI_MNGR_DEFAULT_QP(result) = arg4;
            ROI_MNGR_ORDER_MODE(result) = arg5;
            ROI_MNGR_FIRST(result) = NULL;
            ROI_MNGR_LAST(result) = NULL;
            ROI_MNGR_BLK_W(result) = s1_4;
            ROI_MNGR_BLK_H(result) = s0_2;
            ROI_MNGR_BLK_COUNT(result) = s1_4 * s0_2;
        }

        return result;
    }

    ROI_MNGR_MIN_QP(result) = (int8_t)0x80;
    ROI_MNGR_MAX_QP(result) = 0x7f;
    ROI_MNGR_WIDTH(result) = arg1;
    ROI_MNGR_HEIGHT(result) = arg2;
    {
        int32_t a0 = -0x40;
        int32_t a1 = 0x40;
        int32_t v1 = 6;
        char a2_1 = 6;
        int32_t s1_4 = ((arg1 + a1 - 1) & a0) >> (v1 & 0x1f);
        int32_t s0_2 = ((arg2 + a1 - 1) & a0) >> (v1 & 0x1f);

        ROI_MNGR_SHIFT(result) = a2_1;
        ROI_MNGR_DEFAULT_QP(result) = arg4;
        ROI_MNGR_ORDER_MODE(result) = arg5;
        ROI_MNGR_FIRST(result) = NULL;
        ROI_MNGR_LAST(result) = NULL;
        ROI_MNGR_BLK_W(result) = s1_4;
        ROI_MNGR_BLK_H(result) = s0_2;
        ROI_MNGR_BLK_COUNT(result) = s1_4 * s0_2;
    }

    return result;
}

void AL_RoiMngr_Clear(void *arg1)
{
    void *i_1 = ROI_MNGR_FIRST(arg1);

    if (i_1 != NULL) {
        void *i;

        do {
            i = ROI_NODE_PREV(i_1);
            Rtos_Free(i_1);
            i_1 = i;
        } while (i != NULL);
    }

    ROI_MNGR_FIRST(arg1) = NULL;
    ROI_MNGR_LAST(arg1) = NULL;
}

int32_t AL_RoiMngr_Destroy(void *arg1)
{
    AL_RoiMngr_Clear(arg1);
    Rtos_Free(arg1);
    return 0;
}

int32_t AL_RoiMngr_AddROI(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6)
{
    if (arg2 >= ROI_MNGR_WIDTH(arg1) || arg3 >= ROI_MNGR_HEIGHT(arg1))
        return 0;

    {
        uint32_t s4 = (uint32_t)ROI_MNGR_SHIFT(arg1);
        int32_t v1 = 1 << (s4 & 0x1f);
        int32_t v1_1 = -v1;
        int32_t s1 = arg2 >> (s4 & 0x1f);
        int32_t s2 = arg3 >> (s4 & 0x1f);
        int32_t s0_3 = ((v1 + arg4 - 1) & v1_1) >> (s4 & 0x1f);
        int32_t s4_1 = ((v1 + arg5 - 1) & v1_1) >> (s4 & 0x1f);
        void *v0_9 = Rtos_Malloc(0x1c);

        if (v0_9 == NULL)
            return 0;

        {
            int32_t a2 = ROI_MNGR_BLK_W(arg1);
            int32_t a1 = ROI_MNGR_BLK_H(arg1);

            if (a2 < s1 + s0_3)
                s0_3 = a2 - s1;
            if (a1 < s2 + s4_1)
                s4_1 = a1 - s2;

            ROI_NODE_X(v0_9) = s1;
            ROI_NODE_Y(v0_9) = s2;
            ROI_NODE_W(v0_9) = s0_3;
            ROI_NODE_H(v0_9) = s4_1;
        }

        {
            int32_t v1_4;
            char a0_3;

            if (arg6 == 0x80) {
                a0_3 = (char)0x80;
                v1_4 = 0x80;
            } else if (arg6 == 0x40) {
                a0_3 = 0x40;
                v1_4 = 0x40;
            } else {
                v1_4 = arg6 & 0x3f;
                a0_3 = (char)v1_4;
            }

            ROI_NODE_QP(v0_9) = a0_3;
            ROI_NODE_PREV(v0_9) = NULL;
            ROI_NODE_NEXT(v0_9) = NULL;

            if (ROI_MNGR_ORDER_MODE(arg1) != 1) {
                PushBack((void **)((char *)arg1 + 0x24), (void **)((char *)arg1 + 0x28), v0_9);
                return 1;
            }

            {
                void *i_1 = ROI_MNGR_FIRST(arg1);

                if (i_1 != NULL) {
                    void *i = i_1;

                    do {
                        if ((v1_4 & 0x40) == 0 &&
                            (v1_4 << 26 >> 26) >= ((int32_t)ROI_NODE_QP(i) << 26 >> 26))
                        {
                            i = ROI_NODE_PREV(i);
                        } else {
                            void *v1_7 = ROI_NODE_NEXT(i);
                            ROI_NODE_PREV(v0_9) = i;
                            ROI_NODE_NEXT(v0_9) = v1_7;
                            ROI_NODE_NEXT(i) = v0_9;

                            if (i_1 != i)
                                return 1;

                            ROI_MNGR_FIRST(arg1) = v0_9;
                            return 1;
                        }
                    } while (i != NULL);

                    PushBack((void **)((char *)arg1 + 0x24), (void **)((char *)arg1 + 0x28), v0_9);
                    return 1;
                }
            }

            ROI_MNGR_FIRST(arg1) = v0_9;
            return 1;
        }
    }
}

int32_t AL_RoiMngr_FillBuff(void *arg1, int32_t arg2, int32_t arg3, void *arg4)
{
    int32_t result;

    if (arg4 == NULL) {
        __assert("pBuf",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_codec/ROIMngr.c",
                 0x152, "AL_RoiMngr_FillBuff");
        return 0;
    }

    result = ROI_MNGR_BLK_COUNT(arg1);

    {
        void *a0 = (char *)arg4 + arg2;
        int32_t t1 = 0;

        if (result > 0) {
            if (arg2 > 0) {
                char *v1_1 = (char *)a0 - arg2;

                do {
                    int32_t v0_2 = ROI_MNGR_DEFAULT_QP(arg1);

                    if (v0_2 != 0x80) {
                        char a1 = 0x40;

                        if (v0_2 != 0x40)
                            a1 = (char)(v0_2 & 0x3f);

                        *v1_1 = a1;
                        v1_1 = &v1_1[1];
                        if (v1_1 == a0)
                            break;
                    }

                    *v1_1 = (char)0x80;
                    v1_1 = &v1_1[1];
                    if (v1_1 == a0)
                        break;
                } while (1);
            }

            do {
                result = ROI_MNGR_BLK_COUNT(arg1);
                t1 += 1;
                a0 = (char *)a0 + arg3;
            } while (t1 < result);
        }
    }

    {
        void *s2 = ROI_MNGR_FIRST(arg1);

        if (s2 != NULL) {
            while (1) {
                int32_t v1_3 = ROI_NODE_H(s2);
                char *t1_3 = (char *)arg4 + ((ROI_NODE_Y(s2) * ROI_MNGR_BLK_W(arg1) + ROI_NODE_X(s2)) * arg3);

                if (v1_3 > 0) {
                    int32_t v0_5 = ROI_NODE_W(s2);
                    int32_t t2_1 = 0;

                    while (1) {
                        int32_t a3 = 0;
                        int32_t t0_1 = 0;

                        if (v0_5 > 0) {
                            while (1) {
                                if (arg2 > 0) {
                                    char *v0_6 = t1_3 + a3;
                                    char *a2_2 = t1_3 + arg2 + a3;

                                    while (1) {
                                        int32_t a0_2 = (int32_t)ROI_NODE_QP(s2);
                                        int32_t v1_7 = a0_2 & 0xff;

                                        if ((v1_7 & 0x40) != 0) {
                                            *v0_6 = (char)((*v0_6 & 0x3f) | 0x40);
                                        } else if (((uint8_t)(*v0_6) & 0x40) != 0 && a0_2 >= 0) {
                                            *v0_6 = (char)(v1_7 | 0x40);
                                        } else {
                                            *v0_6 = (char)v1_7;
                                        }

                                        v0_6 = &v0_6[1];
                                        if (a2_2 == v0_6)
                                            break;
                                    }
                                }

                                v0_5 = ROI_NODE_W(s2);
                                t0_1 += 1;
                                a3 += arg3;
                                if (t0_1 >= v0_5)
                                    break;
                            }
                        }

                        v1_3 = ROI_NODE_H(s2);
                        t2_1 += 1;
                        t1_3 += arg3 * ROI_MNGR_BLK_W(arg1);
                        if (t2_1 >= v1_3)
                            break;
                    }
                }

                {
                    int32_t a3_1 = (int32_t)ROI_NODE_QP(s2);
                    result = a3_1 & 0xc0;

                    if (result != 0) {
                        int32_t v0_7 = ROI_NODE_Y(s2);

                        if (v0_7 != 0) {
                            int32_t a0_4 = ROI_MNGR_BLK_W(arg1);
                            int32_t t0_2 = ROI_NODE_X(s2);
                            int32_t v1_11 = (v0_7 - 1) * a0_4;
                            char *a1_7 = (char *)arg4 + ((t0_2 + v1_11) * arg3);
                            char *a2_7;

                            if (v0_7 < 2)
                                a2_7 = a1_7;
                            else
                                a2_7 = (char *)arg4 + ((v1_11 - a0_4 + t0_2) * arg3);

                            UpdateTransitionHorz(arg1, a1_7, a2_7, arg2, arg3, a0_4, t0_2,
                                                 ROI_NODE_W(s2), (char)a3_1);
                        }

                        {
                            int32_t v0_11 = ROI_NODE_Y(s2) + ROI_NODE_H(s2);
                            int32_t a0_6 = ROI_MNGR_BLK_H(arg1);

                            if (v0_11 + 1 < a0_6) {
                                int32_t t0_3 = ROI_MNGR_BLK_W(arg1);
                                int32_t t1_4 = ROI_NODE_X(s2);
                                char *a1_11 = (char *)arg4 + ((v0_11 * t0_3 + t1_4) * arg3);
                                char *a2_9 = a1_11;

                                if (v0_11 + 2 < a0_6)
                                    a2_9 = (char *)arg4 + (((v0_11 + 1) * t0_3 + t1_4) * arg3);

                                UpdateTransitionHorz(arg1, a1_11, a2_9, arg2, arg3, t0_3, t1_4,
                                                     ROI_NODE_W(s2), (char)ROI_NODE_QP(s2));
                            }
                        }

                        {
                            int32_t v0_21 = ROI_NODE_X(s2);

                            if (v0_21 != 0) {
                                int32_t a0_9 = ROI_MNGR_BLK_W(arg1);
                                int32_t a2_11 = a0_9 * ROI_NODE_Y(s2);
                                char *a1_14 = (char *)arg4 + ((v0_21 - 1 + a2_11) * arg3);
                                char *a2_13;

                                if (v0_21 < 2)
                                    a2_13 = a1_14;
                                else
                                    a2_13 = (char *)arg4 + ((v0_21 - 2 + a2_11) * arg3);

                                UpdateTransitionVert(arg1, a1_14, a2_13, arg2, arg3, a0_9,
                                                     ROI_NODE_H(s2), (char)ROI_NODE_QP(s2));
                            }
                        }

                        {
                            int32_t v0_21 = ROI_NODE_X(s2);
                            int32_t v1_15 = ROI_MNGR_BLK_W(arg1);

                            result = v0_21 + ROI_NODE_W(s2);

                            if (result + 1 < v1_15) {
                                int32_t a3_7 = v1_15 * ROI_NODE_Y(s2);
                                char *a1_16 = (char *)arg4 + ((result + a3_7) * arg3);
                                char *a2_15 = a1_16;

                                if (result + 2 < v1_15)
                                    a2_15 = (char *)arg4 + ((result + 1 + a3_7) * arg3);

                                result = (int32_t)(intptr_t)UpdateTransitionVert(arg1, a1_16, a2_15,
                                                                                 arg2, arg3, v1_15,
                                                                                 ROI_NODE_H(s2),
                                                                                 (char)ROI_NODE_QP(s2));
                            }
                        }
                    }
                }

                s2 = ROI_NODE_PREV(s2);
                if (s2 == NULL)
                    break;
            }
        }
    }

    return result;
}

int32_t alcodec_encyuv_init(void **arg1, int32_t arg2, int32_t arg3, int32_t *arg4)
{
    if ((arg2 & 0xf) != 0 || (arg3 & 7) != 0) {
        printf("width=%d must be multiples of 16 and height=%d must be multiples of 8\n", arg2, arg3);
        return -1;
    }

    {
        uint8_t *v0_2 = (uint8_t *)calloc(0x7f8, 1);

        if (v0_2 == NULL) {
            puts("malloc handle failed");
            return -1;
        }

        AL_Codec_Encode_SetDefaultParam(v0_2);

        {
            uint32_t v1_1 = *(uint16_t *)((char *)arg4 + 0x0a);
            int32_t a2 = arg4[0];
            int16_t a1 = (int16_t)(arg2 & 0xffff);
            int16_t a0_1 = (int16_t)(arg3 & 0xffff);
            int32_t a3;
            int32_t t1_1;

            strncpy((char *)(v0_2 + 0x79c), "NV12", 4);
            *(int32_t *)(v0_2 + 0x7a0) = 0x19;
            *(int32_t *)(v0_2 + 0x794) = arg2;
            *(int32_t *)(v0_2 + 0x798) = arg3;
            *(int32_t *)(v0_2 + 0x7a4) = 1;
            *(int32_t *)(v0_2 + 0x7a8) = 0;
            *(int32_t *)(v0_2 + 0x7ac) = 0;
            *(int32_t *)(v0_2 + 0x7b0) = 0;
            *(int32_t *)(v0_2 + 0x7b4) = 0;
            *(int32_t *)(v0_2 + 0x7f0) = 0;
            *(int32_t *)(v0_2 + 0x7f4) = 0;
            *(int32_t *)(v0_2 + 0x7c8) = 0;
            *(int32_t *)(v0_2 + 0x76c) = 8;
            *(int16_t *)(v0_2 + 0x08) = a1;
            *(int16_t *)(v0_2 + 0x0a) = a0_1;
            *(int16_t *)(v0_2 + 0x0c) = a1;
            *(int16_t *)(v0_2 + 0x0e) = a0_1;
            *(uint16_t *)(v0_2 + 0xb0) = (uint16_t)v1_1;
            *(int32_t *)(v0_2 + 0xb4) = (int32_t)v1_1;

            if (a2 == 0) {
                *(int32_t *)(v0_2 + 0x20) = 0x4d;
                *(uint8_t *)(v0_2 + 0x25) = 0;
                t1_1 = arg4[3];
                a3 = arg4[4];
                *(int32_t *)(v0_2 + 0x6c) = arg4[1];
            } else if (a2 == 1) {
                *(int32_t *)(v0_2 + 0x20) = 0x1000001;
                *(uint8_t *)(v0_2 + 0x25) = 0;
                t1_1 = arg4[3];
                a3 = arg4[4];
                *(int32_t *)(v0_2 + 0x6c) = arg4[1];
            } else {
                *(int32_t *)(v0_2 + 0x20) = 0x4000000;
                *(uint8_t *)(v0_2 + 0x24) = 0;
                *(uint8_t *)(v0_2 + 0x25) = 0;

                if (a2 == 4) {
                    a3 = arg4[3];
                    *(int32_t *)(v0_2 + 0x6c) = 0;
                    arg4[4] = a3;
                    t1_1 = a3;
                } else {
                    t1_1 = arg4[3];
                    a3 = arg4[4];
                    *(int32_t *)(v0_2 + 0x6c) = arg4[1];
                }
            }

            {
                int16_t t4_1 = *(int16_t *)((char *)arg4 + 0x14);
                int16_t t3_1 = *(int16_t *)((char *)arg4 + 0x08);
                int16_t t2_2 = *(int16_t *)((char *)arg4 + 0x16);
                int16_t a2_2 = *(int16_t *)((char *)arg4 + 0x18);

                *(int32_t *)(v0_2 + 0xa8) = arg4[7] * 0x3e8;
                *(int32_t *)(v0_2 + 0x7c) = t1_1 * 0x3e8;
                *(int16_t *)(v0_2 + 0x84) = t4_1;
                *(int16_t *)(v0_2 + 0x78) = t3_1;
                *(int32_t *)(v0_2 + 0x80) = a3 * 0x3e8;
                *(int16_t *)(v0_2 + 0x86) = t2_2;
                *(int16_t *)(v0_2 + 0x88) = a2_2;
                *(int32_t *)(v0_2 + 0x24) = 0x3c;
                *(int32_t *)(v0_2 + 0x110) = 0;
            }
        }

        if (AL_Codec_Encode_Create((void **)(v0_2 + 0x7c8), v0_2) >= 0 && *(void **)(v0_2 + 0x7c8) != NULL) {
            *arg1 = v0_2;
            return 0;
        }

        puts("AL_Codec_Encode_Create failed");
        return -1;
    }
}

int32_t alcodec_encyuv_encode(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t *arg5)
{
    int32_t var_24 = 0;
    int32_t var_28 = 0;
    uint8_t str[0x458];

    if (arg1 == NULL) {
        puts("encode handle is null");
        return -1;
    }

    {
        void *s1_1 = *(void **)((char *)arg1 + 0x7c8);
        int32_t a2_1 = *(int32_t *)((char *)s1_1 + 0x8dc);

        if (arg2 != a2_1) {
            printf("frame.size(%d) != initsize(%d)\n", arg2, a2_1);
            return -1;
        }

        memset(str, 0, 0x458);
        *(int32_t *)(str + 0x00) = *(int32_t *)((char *)arg1 + 0x794);
        *(int32_t *)(str + 0x04) = *(int32_t *)((char *)arg1 + 0x798);
        strncpy((char *)(str + 0x08), "NV12", 4);
        *(int32_t *)(str + 0x0c) = arg2;
        *(int32_t *)(str + 0x10) = arg3;
        *(int32_t *)(str + 0x14) = arg4;

        if (AL_Codec_Encode_Process(s1_1, str, NULL) < 0) {
            puts("AL_Codec_Encode_Process failed");
            return -1;
        }

        if (AL_Codec_Encode_GetStream(*(void **)((char *)arg1 + 0x7c8), (void **)&var_24, (void **)&var_28) < 0) {
            puts("AL_Codec_Encode_GetStream failed");
            return -1;
        }

        {
            void *v0_5 = AL_Buffer_GetMetaData((AL_TBuffer *)(intptr_t)var_24, 1);
            uint8_t *v0_6 = (uint8_t *)AL_Buffer_GetData((AL_TBuffer *)(intptr_t)var_24);
            uint32_t v1_2 = (uint32_t)STREAM_META_SECTION_COUNT(v0_5);

            arg5[1] = 0;

            if (v1_2 != 0) {
                uint8_t *v0_7 = STREAM_META_SECTIONS(v0_5);
                int32_t s0_1 = 0;
                int32_t v1_3 = 0;
                int32_t s4_1 = 0;
                int32_t i;

                do {
                    uint8_t *v0_8 = v0_7 + s0_1;
                    memcpy((uint8_t *)(intptr_t)(*arg5) + v1_3,
                           v0_6 + STREAM_SECTION_OFFSET(v0_8),
                           (size_t)STREAM_SECTION_LENGTH(v0_8));
                    v0_7 = STREAM_META_SECTIONS(v0_5);
                    s4_1 += 1;
                    i = (s4_1 < STREAM_META_SECTION_COUNT(v0_5)) ? 1 : 0;
                    v1_3 = STREAM_SECTION_LENGTH(v0_7 + s0_1) + arg5[1];
                    arg5[1] = v1_3;
                    s0_1 += 0x14;
                } while (i != 0);
            }
        }

        AL_Codec_Encode_ReleaseStream(*(void **)((char *)arg1 + 0x7c8),
                                      (void *)(intptr_t)var_24,
                                      (void *)(intptr_t)var_28);
        return 0;
    }
}

int32_t alcodec_encyuv_deinit(void *arg1)
{
    if (arg1 == NULL) {
        puts("deinit handle is null");
        return -1;
    }

    AL_Codec_Encode_Destroy(*(void **)((char *)arg1 + 0x7c8));
    free(arg1);
    return 0;
}

#pragma GCC diagnostic pop
