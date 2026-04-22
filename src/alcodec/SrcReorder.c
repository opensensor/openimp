#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "alcodec/al_rtos.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line,
                        const char *function, void *caller);

/* forward decl, ported by T<N> later */
int32_t SourceVector_Init(void *arg1);
/* forward decl, ported by T<N> later */
int32_t SourceVector_Add(void *arg1, int32_t arg2, void *arg3);
/* forward decl, ported by T<N> later */
int32_t SourceVector_Size(void *arg1);
/* forward decl, ported by T<N> later */
int32_t SourceVector_Remove(void *arg1, int32_t arg2);
/* forward decl, ported by T<N> later */
void *SourceVector_Get(void *arg1, int32_t arg2);
/* forward decl, ported by T<N> later */
uint32_t SourceVector_IsIn(void *arg1, int32_t arg2);
/* forward decl, ported by T<N> later */
int32_t AL_sRefMngr_IncrementBufPtr(int32_t arg1, char arg2);

int32_t AL_SrcReorder_AddSrcBuffer(void *arg1, int32_t *arg2);
int32_t AL_SrcReorder_GetWaitingSrcBufferCount(void *arg1);
int32_t AL_SrcReorder_Cancel(void *arg1, int32_t arg2);
void *AL_SrcReorder_GetReadyCommand(void *arg1, int32_t arg2);
int32_t AL_SrcReorder_EndSrcBuffer(void *arg1, int32_t arg2);

static void *findContainer(void *arg1, int32_t arg2)
{
    uint8_t *s0 = (uint8_t *)arg1 + 0x1db0;

    if (SourceVector_IsIn(s0, arg2) == 0) {
        s0 = (uint8_t *)arg1 + 0x1ee0;
        if (SourceVector_IsIn(s0, arg2) == 0) {
            uint8_t *s0_1 = (uint8_t *)arg1 + 0x2010;

            if (SourceVector_IsIn(s0_1, arg2) == 0) {
                return 0;
            }
            return s0_1;
        }
    }
    return s0;
}

int32_t AL_SrcReorder_Init(void *arg1)
{
    int32_t result;

    SourceVector_Init((uint8_t *)arg1 + 0x1db0);
    SourceVector_Init((uint8_t *)arg1 + 0x1ee0);
    SourceVector_Init((uint8_t *)arg1 + 0x2010);
    *(int32_t *)((uint8_t *)arg1 + 0x2140) = 0;
    result = (int32_t)(intptr_t)Rtos_CreateMutex();
    *(int32_t *)((uint8_t *)arg1 + 0x2144) = 0;
    *(void **)((uint8_t *)arg1 + 0x2148) = (void *)(intptr_t)result;
    return result;
}

int32_t AL_SrcReorder_Deinit(void *arg1)
{
    if (arg1 != 0) {
        Rtos_DeleteMutex(*(void **)((uint8_t *)arg1 + 0x2148));
        return 0;
    }

    return AL_SrcReorder_AddSrcBuffer(
        (void *)(intptr_t)__assert("pCtx",
                                   "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/SrcReorder.c",
                                   0x14, "AL_SrcReorder_Deinit", &_gp),
        (int32_t *)(intptr_t)0);
}

int32_t AL_SrcReorder_AddSrcBuffer(void *arg1, int32_t *arg2)
{
    int32_t a1;
    int32_t *s2_1;

    Rtos_GetMutex(*(void **)((uint8_t *)arg1 + 0x2148));
    a1 = *(int32_t *)((uint8_t *)arg1 + 0x2140);
    s2_1 = (int32_t *)((uint8_t *)arg1 + (a1 % 0x26) * 0xc8);
    if (arg2 == 0) {
        Rtos_Memset(s2_1, 0, 0xc8);
        a1 = *(int32_t *)((uint8_t *)arg1 + 0x2140);
    } else {
        int32_t *i = arg2;
        int32_t *a0_2 = s2_1;

        do {
            int32_t t1_1 = *i;
            int32_t t0_1 = i[1];
            int32_t a3_1 = i[2];
            int32_t a2_1 = i[3];

            i = &i[4];
            *a0_2 = t1_1;
            a0_2[1] = t0_1;
            a0_2[2] = a3_1;
            a0_2[3] = a2_1;
            a0_2 = &a0_2[4];
        } while (i != &arg2[0x30]);

        {
            int32_t v0_10 = i[1];

            *a0_2 = *i;
            a0_2[1] = v0_10;
        }
    }

    if (SourceVector_IsIn((uint8_t *)arg1 + 0x1db0, a1) != 0) {
        __assert("!SourceVector_IsIn(&pCtx->aheadSources, pCtx->aheadCount)",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/SrcReorder.c",
                 0x23, "AL_SrcReorder_AddSrcBuffer", &_gp);
    } else if (SourceVector_IsIn((uint8_t *)arg1 + 0x1ee0,
                                 *(int32_t *)((uint8_t *)arg1 + 0x2140)) == 0) {
        if (SourceVector_IsIn((uint8_t *)arg1 + 0x2010,
                              *(int32_t *)((uint8_t *)arg1 + 0x2140)) == 0) {
            int32_t a0_7;

            SourceVector_Add((uint8_t *)arg1 + 0x1db0,
                             *(int32_t *)((uint8_t *)arg1 + 0x2140), s2_1);
            a0_7 = (int32_t)(intptr_t)*(void **)((uint8_t *)arg1 + 0x2148);
            *(int32_t *)((uint8_t *)arg1 + 0x2140) += 1;
            return (int32_t)Rtos_ReleaseMutex((void *)(intptr_t)a0_7);
        }
        __assert("!SourceVector_IsIn(&pCtx->usedSources, pCtx->aheadCount)",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/SrcReorder.c",
                 0x25, "AL_SrcReorder_AddSrcBuffer", &_gp);
    }

    return AL_SrcReorder_GetWaitingSrcBufferCount(
        (void *)(intptr_t)__assert("!SourceVector_IsIn(&pCtx->readySources, pCtx->aheadCount)",
                                   "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/SrcReorder.c",
                                   0x24, "AL_SrcReorder_AddSrcBuffer", &_gp));
}

int32_t AL_SrcReorder_GetWaitingSrcBufferCount(void *arg1)
{
    int32_t result;

    Rtos_GetMutex(*(void **)((uint8_t *)arg1 + 0x2148));
    result = SourceVector_Size((uint8_t *)arg1 + 0x1ee0);
    Rtos_ReleaseMutex(*(void **)((uint8_t *)arg1 + 0x2148));
    return result;
}

int32_t AL_SrcReorder_IsEosNext(void *arg1)
{
    int32_t v0;

    Rtos_GetMutex(*(void **)((uint8_t *)arg1 + 0x2148));
    v0 = (int32_t)SourceVector_IsIn((uint8_t *)arg1 + 0x1db0,
                                    *(int32_t *)((uint8_t *)arg1 + 0x2144));
    if (v0 == 0) {
        Rtos_ReleaseMutex(*(void **)((uint8_t *)arg1 + 0x2148));
        return v0;
    }

    {
        int32_t s0_1 =
            *(int32_t *)SourceVector_Get((uint8_t *)arg1 + 0x1db0,
                                         *(int32_t *)((uint8_t *)arg1 + 0x2144)) < 1
                ? 1
                : 0;

        Rtos_ReleaseMutex(*(void **)((uint8_t *)arg1 + 0x2148));
        return s0_1;
    }
}

void *AL_SrcReorder_GetCommandAndMoveNext(void *arg1)
{
    Rtos_GetMutex(*(void **)((uint8_t *)arg1 + 0x2148));
    if (SourceVector_IsIn((uint8_t *)arg1 + 0x1db0,
                          *(int32_t *)((uint8_t *)arg1 + 0x2144)) != 0) {
        uint8_t *v0_1 = (uint8_t *)SourceVector_Get((uint8_t *)arg1 + 0x1db0,
                                                    *(int32_t *)((uint8_t *)arg1 + 0x2144));

        if (*(int32_t *)v0_1 != 0) {
            int32_t a0_5;

            SourceVector_Remove((uint8_t *)arg1 + 0x1db0,
                                *(int32_t *)((uint8_t *)arg1 + 0x2144));
            SourceVector_Add((uint8_t *)arg1 + 0x1ee0,
                             *(int32_t *)((uint8_t *)arg1 + 0x2144), v0_1);
            a0_5 = (int32_t)(intptr_t)*(void **)((uint8_t *)arg1 + 0x2148);
            *(int32_t *)((uint8_t *)arg1 + 0x2144) += 1;
            Rtos_ReleaseMutex((void *)(intptr_t)a0_5);
            return v0_1 + 0x48;
        }
    }

    Rtos_ReleaseMutex(*(void **)((uint8_t *)arg1 + 0x2148));
    return 0;
}

int32_t AL_SrcReorder_IsAvailable(void *arg1, int32_t arg2)
{
    int32_t result;

    Rtos_GetMutex(*(void **)((uint8_t *)arg1 + 0x2148));
    result = (int32_t)SourceVector_IsIn((uint8_t *)arg1 + 0x1ee0, arg2);
    Rtos_ReleaseMutex(*(void **)((uint8_t *)arg1 + 0x2148));
    return result;
}

void *AL_SrcReorder_GetSrcBuffer(void *arg1, int32_t arg2)
{
    void *v0;

    {
        int kfd = open("/dev/kmsg", O_WRONLY);
        if (kfd >= 0) {
            char b[160];
            int n = snprintf(b, sizeof(b),
                             "libimp/SRCRO: GetSrcBuffer entry ctx=%p order=%d mutex=%p\n",
                             arg1, arg2, *(void **)((uint8_t *)arg1 + 0x2148));
            if (n > 0)
                write(kfd, b, n);
            close(kfd);
        }
    }
    Rtos_GetMutex(*(void **)((uint8_t *)arg1 + 0x2148));
    {
        int kfd = open("/dev/kmsg", O_WRONLY);
        if (kfd >= 0) {
            char b[160];
            int n = snprintf(b, sizeof(b),
                             "libimp/SRCRO: GetSrcBuffer locked ctx=%p order=%d\n",
                             arg1, arg2);
            if (n > 0)
                write(kfd, b, n);
            close(kfd);
        }
    }
    v0 = findContainer(arg1, arg2);
    {
        int kfd = open("/dev/kmsg", O_WRONLY);
        if (kfd >= 0) {
            char b[160];
            int n = snprintf(b, sizeof(b),
                             "libimp/SRCRO: GetSrcBuffer container ctx=%p order=%d container=%p\n",
                             arg1, arg2, v0);
            if (n > 0)
                write(kfd, b, n);
            close(kfd);
        }
    }
    if (v0 == 0) {
        Rtos_ReleaseMutex(*(void **)((uint8_t *)arg1 + 0x2148));
        {
            int kfd = open("/dev/kmsg", O_WRONLY);
            if (kfd >= 0) {
                char b[160];
                int n = snprintf(b, sizeof(b),
                                 "libimp/SRCRO: GetSrcBuffer miss ctx=%p order=%d\n",
                                 arg1, arg2);
                if (n > 0)
                    write(kfd, b, n);
                close(kfd);
            }
        }
        return 0;
    }

    {
        void *result = SourceVector_Get(v0, arg2);
        {
            int kfd = open("/dev/kmsg", O_WRONLY);
            if (kfd >= 0) {
                char b[160];
                int n = snprintf(b, sizeof(b),
                                 "libimp/SRCRO: GetSrcBuffer result ctx=%p order=%d result=%p\n",
                                 arg1, arg2, result);
                if (n > 0)
                    write(kfd, b, n);
                close(kfd);
            }
        }

        Rtos_ReleaseMutex(*(void **)((uint8_t *)arg1 + 0x2148));
        return result;
    }
}

int32_t AL_SrcReorder_MarkSrcBufferAsUsed(void *arg1, int32_t arg2)
{
    Rtos_GetMutex(*(void **)((uint8_t *)arg1 + 0x2148));
    if (SourceVector_IsIn((uint8_t *)arg1 + 0x1ee0, arg2) == 0) {
        return AL_SrcReorder_Cancel(
            (void *)(intptr_t)__assert("SourceVector_IsIn(&pCtx->readySources, uSrcOrder)",
                                       "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/SrcReorder.c",
                                       0x94, "AL_SrcReorder_MarkSrcBufferAsUsed", &_gp),
            arg2);
    }

    {
        void *v0_1 = SourceVector_Get((uint8_t *)arg1 + 0x1ee0, arg2);

        SourceVector_Remove((uint8_t *)arg1 + 0x1ee0, arg2);
        SourceVector_Add((uint8_t *)arg1 + 0x2010, arg2, v0_1);
        return (int32_t)Rtos_ReleaseMutex(*(void **)((uint8_t *)arg1 + 0x2148));
    }
}

int32_t AL_SrcReorder_Cancel(void *arg1, int32_t arg2)
{
    Rtos_GetMutex(*(void **)((uint8_t *)arg1 + 0x2148));
    if (SourceVector_IsIn((uint8_t *)arg1 + 0x2010, arg2) == 0) {
        (void)AL_SrcReorder_GetReadyCommand(
            (void *)(intptr_t)__assert("SourceVector_IsIn(&pCtx->usedSources, uSrcOrder)",
                                       "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/SrcReorder.c",
                                       0xa2, "AL_SrcReorder_Cancel", &_gp),
            arg2);
        return 0;
    }

    {
        void *v0_1 = SourceVector_Get((uint8_t *)arg1 + 0x2010, arg2);

        SourceVector_Remove((uint8_t *)arg1 + 0x2010, arg2);
        SourceVector_Add((uint8_t *)arg1 + 0x1ee0, arg2, v0_1);
        return (int32_t)Rtos_ReleaseMutex(*(void **)((uint8_t *)arg1 + 0x2148));
    }
}

void *AL_SrcReorder_GetReadyCommand(void *arg1, int32_t arg2)
{
    Rtos_GetMutex(*(void **)((uint8_t *)arg1 + 0x2148));
    if (SourceVector_IsIn((uint8_t *)arg1 + 0x1ee0, arg2) == 0) {
        (void)AL_SrcReorder_EndSrcBuffer(
            (void *)(intptr_t)__assert("SourceVector_IsIn(&pCtx->readySources, uSrcOrder)",
                                       "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/SrcReorder.c",
                                       0xb0, "AL_SrcReorder_GetReadyCommand", &_gp),
            arg2);
        return 0;
    }

    {
        uint8_t *v0_1 = (uint8_t *)SourceVector_Get((uint8_t *)arg1 + 0x1ee0, arg2);

        Rtos_ReleaseMutex(*(void **)((uint8_t *)arg1 + 0x2148));
        return v0_1 + 0x48;
    }
}

int32_t AL_SrcReorder_EndSrcBuffer(void *arg1, int32_t arg2)
{
    void *v0;

    Rtos_GetMutex(*(void **)((uint8_t *)arg1 + 0x2148));
    v0 = findContainer(arg1, arg2);
    if (v0 == 0) {
        return AL_sRefMngr_IncrementBufPtr(
            __assert("pVector",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/SrcReorder.c",
                     0xbc, "AL_SrcReorder_EndSrcBuffer", &_gp),
            (char)arg2);
    }

    SourceVector_Remove(v0, arg2);
    return (int32_t)Rtos_ReleaseMutex(*(void **)((uint8_t *)arg1 + 0x2148));
}
