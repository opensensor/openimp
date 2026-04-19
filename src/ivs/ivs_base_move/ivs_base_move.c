#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "imp/imp_ivs.h"
#include "imp/imp_ivs_base_move.h"

int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
int32_t IMP_IVS_ReleaseData(void *vaddr); /* forward decl, ported by T<N> later */
void *imp_alloc_base_move(uint32_t *arg1, int32_t arg2);
int32_t imp_free_base_move(void *arg1);
int32_t imp_base_move_preprocess(void *arg1, void *arg2);
int32_t imp_base_move_process(void *arg1, void *arg2, int32_t *arg3, int32_t arg4);
int32_t imp_get_base_move_param(void *arg1, uint32_t *arg2);
int32_t imp_set_base_move_param(void *arg1, void *arg2);
int32_t imp_base_flush_frame(void *arg1);
int32_t sad(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, void *arg6,
    int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10, void *arg11, int32_t arg12,
    int32_t arg13, int32_t arg14, int32_t arg15, void *arg16, int32_t arg17, int32_t arg18,
    int32_t arg19, int32_t arg20, int32_t arg21, int32_t arg22, int32_t arg23, int32_t arg24);
void MergeBaseMove(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11,
    int32_t arg12, int32_t arg13, int32_t arg14, int32_t arg15, int32_t arg16,
    int32_t arg17); /* forward decl, ported by T<N> later */
int32_t is_has_simd128(int32_t arg1); /* forward decl, ported by T<N> later */

typedef struct IMPIVSInterfaceLayout {
    void *param;
    uint32_t param_size;
    void *reserved;
    void *init;
    void *exit;
    void *preProcessSync;
    void *processAsync;
    void *getResult;
    void *releaseResult;
    void *getParam;
    void *updateParam;
    void *stop;
    void *priv;
    uint32_t param_storage[16];
} IMPIVSInterfaceLayout;

uint32_t dump_base_move_ivs = 0;

static int32_t sub_de4b0(void)
{
    return 0;
}

int32_t BaseMoveReleaseResult(void)
{
    return sub_de4b0();
}

static int32_t sub_de500(void *arg1, int32_t **arg2)
{
    void *s1_1 = *(void **)((char *)arg1 + 0x30);

    if (s1_1 == NULL) {
        imp_log_fun(6, sub_de500(arg1, arg2), 2, "BASEIMPIVSMOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0xc4,
            "BaseMoveGetResult", "BaseMoveGetResult");
        return -1;
    }

    {
        int32_t *v0 = *(int32_t **)((char *)s1_1 + 0x48);
        int32_t s0_1 = v0[0];
        int32_t v0_2 = s0_1 * 0x18;
        int32_t *a2_1;

        *arg2 = (int32_t *)((char *)((intptr_t)v0[2]) + v0_2);

        if ((dump_base_move_ivs & 1U) != 0) {
            int32_t *s5_3 = (int32_t *)((char *)((intptr_t)(*(int32_t **)((char *)s1_1 + 0x48))[2]) + v0_2);
            int32_t s0_3;
            char *v0_5;
            char *a0_4;

            imp_log_fun(6, IMP_Log_Get_Option(), 2, "BASEIMPIVSMOVE",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0xd3,
                "BaseMoveGetResult", "result: read id = %d, ret = %d, datalen = %d\n",
                s0_1, s5_3[0], s5_3[2]);

            a0_4 = (char *)(intptr_t)s5_3[2];
            v0_5 = (char *)(intptr_t)s5_3[1];

            if ((intptr_t)a0_4 <= 0) {
                s0_3 = 0;
            } else {
                a0_4 += (intptr_t)v0_5;
                s0_3 = 0;
                do {
                    uint32_t v1_12 = (uint8_t)*v0_5;
                    v0_5 = &v0_5[1];
                    s0_3 += (0U < v1_12) ? 1 : 0;
                } while (a0_4 != v0_5);
            }

            imp_log_fun(6, IMP_Log_Get_Option(), 2, "BASEIMPIVSMOVE",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0xda,
                "BaseMoveGetResult", "result: read cnt = %d\n", s0_3);
            a2_1 = *(int32_t **)((char *)s1_1 + 0x48);
        } else {
            a2_1 = *(int32_t **)((char *)s1_1 + 0x48);
        }

        a2_1[0] = (a2_1[0] + 1) % 6;
    }

    return 0;
}

int32_t BaseMoveGetResult(void *arg1, int32_t **arg2)
{
    return sub_de500(arg1, arg2);
}

static int32_t sub_de734(void *arg1, void *arg2)
{
    void *s3_3 = *(void **)((char *)arg1 + 0x30);
    int32_t result;

    if (s3_3 == NULL) {
        result = -1;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "BASEIMPIVSMOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0x97,
            "BaseMoveProcessAsync", "BaseMoveProcessAsync");
    } else {
        int32_t *v0 = *(int32_t **)((char *)s3_3 + 0x48);
        int32_t s2_1 = v0[1];
        int32_t s0_2 = s2_1 * 0x18;

        result = imp_base_move_process(s3_3, arg2, (int32_t *)((char *)(intptr_t)v0[2] + s0_2), 0);

        if ((dump_base_move_ivs & 1U) != 0) {
            int32_t *s0_3 = (int32_t *)((char *)((intptr_t)(*(int32_t **)((char *)s3_3 + 0x48))[2]) + s0_2);

            if (s2_1 == 0) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "BASEIMPIVSMOVE",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0xa7,
                    "BaseMoveProcessAsync", "result: write id = %d, ret = %d, datalen = %d ret = %d\n",
                    0, s0_3[0], s0_3[2], result);
            }

            {
                int32_t a1 = s0_3[2];
                char *i = (char *)(intptr_t)s0_3[1];

                if (a1 > 0) {
                    int32_t s0_4 = 0;
                    char *end = i + a1;
                    do {
                        uint32_t a0_1 = (uint8_t)*i;
                        i = &i[1];
                        s0_4 += (0U < a0_1) ? 1 : 0;
                    } while (i != end);

                    if (s0_4 != 0) {
                        imp_log_fun(6, IMP_Log_Get_Option(), 2, "BASEIMPIVSMOVE",
                            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0xb0,
                            "BaseMoveProcessAsync", "result: write cnt = %d\n", s0_4);
                    }
                }
            }
        }

        if (result == 0) {
            int32_t *a1_2 = *(int32_t **)((char *)s3_3 + 0x48);
            a1_2[1] = (a1_2[1] + 1) % 6;
            return result;
        }

        if (result < 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "BASEIMPIVSMOVE",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0xb7,
                "BaseMoveProcessAsync", "result process failed\n");
        }
    }

    return result;
}

int32_t BaseMoveProcessAsync(void *arg1, void *arg2)
{
    return sub_de734(arg1, arg2);
}

static int32_t sub_dea30(void *arg1)
{
    void *s1_3 = *(void **)((char *)arg1 + 0x30);

    if (s1_3 == NULL) {
        return imp_log_fun(6, sub_dea30(arg1), 2, "BASEIMPIVSMOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0x50,
            "BaseMoveExit", "BaseMoveExit");
    }

    {
        void *v0 = *(void **)((char *)s1_3 + 0x48);
        int32_t gp_4;

        if (v0 == NULL) {
            imp_log_fun(6, sub_dea30(arg1), 2, "BASEIMPIVSMOVE",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0x57,
                "BaseMoveExit", "calloc baseMoveInterface is NULL!\n");
            gp_4 = 0;
        } else {
            void *v0_1 = *(void **)((char *)v0 + 8);
            int32_t s0_1 = 0;

            if (v0_1 != NULL) {
                void *v0_3;
                do {
                    free(*(void **)((char *)v0_1 + s0_1 + 4));
                    s0_1 += 0x18;
                    v0_3 = *(void **)((char *)s1_3 + 0x48);
                    if (s0_1 == 0x90) {
                        break;
                    }
                    v0_1 = *(void **)((char *)v0_3 + 8);
                } while (1);

                free(*(void **)((char *)v0_3 + 8));
                free(*(void **)((char *)s1_3 + 0x48));
                gp_4 = imp_free_base_move(s1_3);
                *(void **)((char *)arg1 + 0x30) = NULL;
                return gp_4;
            }

            imp_log_fun(6, sub_dea30(arg1), 2, "BASEIMPIVSMOVE",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0x5c,
                "BaseMoveExit", "calloc baseMoveInterface is NULL!\n");
            free(*(void **)((char *)s1_3 + 0x48));
            gp_4 = 0;
        }

        {
            int32_t v0_8 = imp_free_base_move(s1_3);
            *(void **)((char *)arg1 + 0x30) = NULL;
            return v0_8;
        }
    }
}

int32_t BaseMoveExit(void *arg1)
{
    return sub_dea30(arg1);
}

static int32_t sub_dec78(uint32_t *arg1, void *arg2)
{
    int32_t a0_5 = (int32_t)*arg1;

    if (a0_5 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "BASEIMPIVSMOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0x1e,
            "BaseMoveInit", "BaseMoveInit");
        return -1;
    }

    {
        int32_t v0 = (int32_t)(intptr_t)imp_alloc_base_move((uint32_t *)(uintptr_t)a0_5,
            (int32_t)(intptr_t)IMP_IVS_ReleaseData);

        if (v0 == 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "BASEIMPIVSMOVE",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0x23,
                "BaseMoveInit", "calloc baseMoveInterface is NULL!\n");
            *(void **)((char *)arg2 + 0x30) = NULL;
            return -1;
        }

        {
            int32_t *v0_1 = calloc(1, 0xc);
            int32_t gp_5;

            *(int32_t **)(v0 + 0x48) = v0_1;

            if (v0_1 == NULL) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "BASEIMPIVSMOVE",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0x28,
                    "BaseMoveInit", "calloc baseMoveInterface is NULL!\n");
                gp_5 = 0;
            } else {
                int32_t v0_2 = (int32_t)(intptr_t)calloc(6, 0x18);
                int32_t gp_2 = 0;

                v0_1[2] = v0_2;

                if (v0_2 != 0) {
                    int32_t i = v0_2 + 4;
                    int32_t v1_1 = *(int32_t *)(v0 + 0x20) * *(int32_t *)(v0 + 0x24);
                    int32_t s2_2 = v1_1 + 0x3f;

                    if (v1_1 >= 0) {
                        s2_2 = v1_1;
                    }

                    do {
                        i += 0x18;
                        *(int32_t *)(i - 0x18) = (int32_t)(intptr_t)calloc((size_t)(s2_2 >> 6), 1);
                    } while (i != v0_2 + 0x94);

                    v0_1[1] = 0;
                    v0_1[0] = 0;
                    *(int32_t *)((char *)arg2 + 0x30) = v0;
                    return 0;
                }

                imp_log_fun(6, IMP_Log_Get_Option(), 2, "BASEIMPIVSMOVE",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0x2e,
                    "BaseMoveInit", "calloc baseMoveInterface is NULL!\n");
                free(*(void **)(v0 + 0x48));
                gp_5 = 0;
            }

            imp_free_base_move((void *)(intptr_t)v0);
            *(void **)((char *)arg2 + 0x30) = NULL;
            return -1;
        }
    }
}

int32_t BaseMoveInit(uint32_t *arg1, void *arg2)
{
    return sub_dec78(arg1, arg2);
}

int32_t imp_base_flush_frame(void *arg1)
{
    (void)arg1;
    return 0;
}

static int32_t sub_def28(void *arg1)
{
    void *a0_3 = *(void **)((char *)arg1 + 0x30);

    if (a0_3 != NULL) {
        return imp_base_flush_frame(a0_3);
    }

    imp_log_fun(6, sub_def28(a0_3), 2, "BASEIMPIVSMOVE",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0x102,
        "BaseMoveFlushFrame", "BaseMoveFlushFrame");
    return -1;
}

int32_t BaseMoveFlushFrame(void *arg1)
{
    return sub_def28(arg1);
}

static int32_t sub_defd8(void *arg1, void *arg2)
{
    void *a0_3 = *(void **)((char *)arg1 + 0x30);

    if (a0_3 != NULL) {
        return imp_set_base_move_param(a0_3, arg2);
    }

    imp_log_fun(6, sub_defd8(a0_3, arg2), 2, "BASEIMPIVSMOVE",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0xf7,
        "BaseMoveSetParam", "BaseMoveSetParam");
    return -1;
}

int32_t BaseMoveSetParam(void *arg1, void *arg2)
{
    return sub_defd8(arg1, arg2);
}

static int32_t sub_df088(void *arg1, void *arg2)
{
    void *a0_3 = *(void **)((char *)arg1 + 0x30);

    if (a0_3 != NULL) {
        return imp_get_base_move_param(a0_3, (uint32_t *)arg2);
    }

    imp_log_fun(6, sub_df088(a0_3, arg2), 2, "BASEIMPIVSMOVE",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0xec,
        "BaseMoveGetParam", "BaseMoveGetParam");
    return -1;
}

int32_t BaseMoveGetParam(void *arg1, void *arg2)
{
    return sub_df088(arg1, arg2);
}

static int32_t sub_df138(void *arg1, void *arg2)
{
    void *a0_3 = *(void **)((char *)arg1 + 0x30);

    if (a0_3 != NULL) {
        return imp_base_move_preprocess(a0_3, arg2);
    }

    imp_log_fun(6, sub_df138(a0_3, arg2), 2, "BASEIMPIVSMOVE",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0x7a,
        "BaseMovePreprocessSync", "BaseMovePreprocessSync");
    return -1;
}

int32_t BaseMovePreprocessSync(void *arg1, void *arg2)
{
    return sub_df138(arg1, arg2);
}

static void *sub_df1e8(uint32_t *arg1)
{
    IMPIVSInterfaceLayout *result = calloc(1, 0x74);

    if (result == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "BASEIMPIVSMOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/base_move_ivs.c", 0x110,
            "IMP_IVS_CreateBaseMoveInterface", "calloc baseMoveInterface is NULL!\n");
        return NULL;
    }

    {
        uint32_t *v1 = result->param_storage;
        uint32_t *i = arg1;

        result->param = v1;

        do {
            v1[0] = i[0];
            v1[1] = i[1];
            v1[2] = i[2];
            v1[3] = i[3];
            i = &i[4];
            v1 = &v1[4];
        } while (i != &arg1[0x10]);
    }

    result->param_size = 0x40;
    result->reserved = (void *)0xa;
    result->init = (void *)BaseMoveInit;
    result->exit = (void *)BaseMoveExit;
    result->preProcessSync = (void *)BaseMovePreprocessSync;
    result->processAsync = (void *)BaseMoveProcessAsync;
    result->getResult = (void *)BaseMoveGetResult;
    result->releaseResult = (void *)BaseMoveReleaseResult;
    result->getParam = (void *)BaseMoveGetParam;
    result->updateParam = (void *)BaseMoveSetParam;
    result->stop = (void *)BaseMoveFlushFrame;
    return result;
}

IMPIVSInterface *IMP_IVS_CreateBaseMoveInterface(IMP_IVS_BaseMoveParam *arg1)
{
    return (IMPIVSInterface *)sub_df1e8((uint32_t *)arg1);
}

static void sub_df354(void *arg1)
{
    if (arg1 == NULL) {
        return;
    }

    free(arg1);
}

void IMP_IVS_DestroyBaseMoveInterface(IMPIVSInterface *arg1)
{
    sub_df354(arg1);
}

void *sub_df520(void *arg1, int32_t arg2, void *arg3, int32_t *arg4)
{
    int32_t s4_4 = arg4[0];
    int32_t s5 = arg4[1];
    int32_t s2_1;
    int32_t s4;
    int32_t gp_1;
    size_t nitems;
    void *(*t9_1)(size_t, size_t);

    if (arg2 <= 0) {
        nitems = (size_t)(s5 * s4_4);
        t9_1 = calloc;
label_df70c:
        arg4[0xf] = (int32_t)(intptr_t)t9_1(nitems, 1);

        if (arg4[0xf] != 0) {
            int32_t v0_10 = arg4[0xd];

            arg4[0x14] = s4_4;
            arg4[0x15] = s5;
            arg4[0x13] = s4_4;
            arg4[0x19] = s4_4;
            arg4[0x1a] = s5;
            arg4[0x18] = s4_4;
            arg4[0x1e] = s4_4;
            arg4[0x1f] = s5;
            arg4[0x1d] = s4_4;

            if (v0_10 == 0) {
                int32_t s4_3 = s4_4 >> 3;
                arg4[0x23] = s4_3;
                arg4[0x24] = s5 >> 3;
                arg4[0x22] = s4_3;
            }

            if (arg4[0xd] == 1) {
                arg4[0xc] = 0x14;
                *(int32_t *)((char *)arg3 + 4) = (int32_t)(intptr_t)arg4;
                return arg3;
            }

            if (arg4[0xd] == 2) {
                arg4[0xc] = 0xf;
                *(int32_t *)((char *)arg3 + 4) = (int32_t)(intptr_t)arg4;
                return arg3;
            }

            if (arg4[0xd] == 3) {
                arg4[0xc] = 0xa;
                *(int32_t *)((char *)arg3 + 4) = (int32_t)(intptr_t)arg4;
                return arg3;
            }

            if (arg4[0xd] == 0) {
                arg4[0xc] = 0x1e;
                *(int32_t *)((char *)arg3 + 4) = (int32_t)(intptr_t)arg4;
                return arg3;
            }

            arg4[0xc] = 0;
            *(int32_t *)((char *)arg3 + 4) = (int32_t)(intptr_t)arg4;
            return arg3;
        }

        s4 = 0;
        s2_1 = 0;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/src/ivs_base_move.c", 0x73,
            "IVSMove_init", "malloc ivsMove failed\n");
        gp_1 = 0;
    } else {
        void *s7_1 = arg1;
        int32_t s6_1 = 0;

        nitems = (size_t)(s4_4 * s5);

        while (1) {
            int32_t s0_1 = s6_1 << 2;

            *(int32_t *)((char *)s7_1 + s0_1) = (int32_t)(intptr_t)calloc(nitems, 1);
            s7_1 = (void *)(intptr_t)arg4[2];
            s6_1 += 1;

            if (*(int32_t *)((char *)s7_1 + s0_1) == 0) {
                s4 = 0;
                s2_1 = 0;
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/src/ivs_base_move.c", 0x6c,
                    "IVSMove_init", "malloc ivsMove failed\n", s6_1 - 1);
                gp_1 = 0;
                break;
            }

            t9_1 = calloc;
            if (arg2 == s6_1) {
                goto label_df70c;
            }
        }
    }

    {
        int32_t i = arg4[7] - 1;
        void *v1_6 = (void *)(intptr_t)arg4[2];

        if (i >= 0) {
            int32_t s0_2 = i << 2;
            do {
                int32_t v0_4 = *(int32_t *)((char *)v1_6 + s0_2);
                i -= 1;
                if (v0_4 != 0) {
                    free((void *)(intptr_t)v0_4);
                    gp_1 = 0;
                    *(int32_t *)((char *)(intptr_t)arg4[2] + s0_2) = 0;
                    v1_6 = (void *)(intptr_t)arg4[2];
                }
                s0_2 -= 4;
            } while (i != -1);
        }

        free(v1_6);
        free(arg4);
        *(int32_t *)((char *)arg3 + 4) = 0;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/src/ivs_base_move.c", 0x13b,
            "imp_alloc_base_move", "IVSMove_init failed\n");
        free(arg3);
    }

    return NULL;
}

void *imp_alloc_base_move(uint32_t *arg1, int32_t arg2)
{
    int32_t *v0 = calloc(1, 0x50);

    if (v0 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/src/ivs_base_move.c", 0x12b,
            "imp_alloc_base_move", "calloc ivs move failed\n");
        return NULL;
    }

    {
        uint32_t *i = arg1;
        uint32_t *v0_1 = (uint32_t *)&v0[2];

        do {
            v0_1[0] = i[0];
            v0_1[1] = i[1];
            v0_1[2] = i[2];
            v0_1[3] = i[3];
            i = &i[4];
            v0_1 = &v0_1[4];
        } while (i != &arg1[0x10]);
    }

    {
        int32_t v0_2 = v0[5];
        int32_t s0_1 = v0[2];
        int32_t s5_1;
        int32_t s4_1;
        int32_t s1_1;
        int32_t s2_1;

        v0[0x13] = arg2;
        v0[0] = 0;
        s5_1 = v0[8];
        s4_1 = v0[9];
        s1_1 = v0[3];
        s2_1 = v0[4];

        if (s0_1 < 0 || s1_1 <= 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/src/ivs_base_move.c", 0x34,
                "IVSMove_init", "skipFrameCnt or referenceNum out of range\n");
        } else {
            int32_t *v0_3 = calloc(1, 0x94);

            if (v0_3 == NULL) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/src/ivs_base_move.c", 0x39,
                    "IVSMove_init", "malloc ivsMove failed\n");
            } else {
                int32_t i_2;
                int32_t v1_3;
                int32_t i_1;
                int32_t a2_3;
                int32_t a3_3;

                v0_3[0] = s5_1;
                v0_3[1] = s4_1;
                v0_3[3] = s1_1;
                v0_3[4] = s0_1 + 1;
                memset(&v0_3[5], 0, 0x1c);

                if (s0_1 + 1 < s1_1) {
                    if (s0_1 == -1) {
                        __builtin_trap();
                    }
                    v0_3[7] = ((0 < (s1_1 % (s0_1 + 1))) ? 1 : 0) + s1_1 / (s0_1 + 1) + 1;
                } else {
                    v0_3[7] = 2;
                }

                i_2 = s0_1 + 1 - s1_1;
                v1_3 = 1;
                do {
                    i_1 = i_2;
                    a3_3 = v1_3;
                    a2_3 = s1_1 + i_2;
                    v1_3 += 1;
                    i_2 += s0_1 + 1;
                } while (i_1 < 0);

                v0_3[6] = a3_3;
                v0_3[0xa] = i_1;
                v0_3[0xb] = a2_3;
                v0_3[0xd] = s2_1;
                v0_3[0xe] = 0;

                if (s2_1 == 0) {
                    int32_t v0_5 = is_has_simd128(i_1);
                    size_t nitems;
                    void *v0_6;

                    v0_3[0x10] = v0_5;
                    nitems = (size_t)v0_3[7];
                    v0_6 = calloc(nitems, 4);
                    v0_3[2] = (int32_t)(intptr_t)v0_6;

                    if (v0_6 == NULL) {
                        return sub_df520(v0_6, 0, v0, v0_3);
                    }

                    return sub_df520(v0_6, (int32_t)nitems, v0, v0_3);
                }

                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/src/ivs_base_move.c", 0x59,
                    "IVSMove_init", "sad mode error\n");
                free(v0_3);
            }
        }

        v0[1] = 0;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/src/ivs_base_move.c", 0x13b,
            "imp_alloc_base_move", "IVSMove_init failed\n");
        free(v0);
    }

    return NULL;
}

int32_t imp_free_base_move(void *arg1)
{
    if (arg1 == NULL) {
        return 0;
    }

    {
        void *s2 = *(void **)((char *)arg1 + 4);
        void *a0 = *(void **)((char *)s2 + 0x3c);

        if (a0 != NULL) {
            free(a0);
            *(void **)((char *)s2 + 0x3c) = NULL;
        }

        {
            int32_t a1 = *(int32_t *)((char *)s2 + 0x1c);
            void *v1 = *(void **)((char *)s2 + 8);

            if (a1 > 0) {
                int32_t s0_1 = 0;
                int32_t s1_1 = 0;
                do {
                    void *v0_2 = *(void **)((char *)v1 + s1_1);
                    s0_1 += 1;
                    if (v0_2 != NULL) {
                        free(v0_2);
                        a1 = *(int32_t *)((char *)s2 + 0x1c);
                        *(void **)((char *)(*(void **)((char *)s2 + 8)) + s1_1) = NULL;
                        v1 = *(void **)((char *)s2 + 8);
                    }
                    s1_1 = s0_1 << 2;
                } while (s0_1 < a1);
            }

            if (v1 != NULL) {
                free(v1);
            }
        }

        free(s2);
    }

    free(arg1);
    return 0;
}

int32_t imp_base_move_preprocess(void *arg1, void *arg2)
{
    memcpy(*(void **)((char *)(*(void **)((char *)arg1 + 4)) + 0x3c),
        *(void **)((char *)arg2 + 0x1c),
        (size_t)(*(int32_t *)((char *)arg2 + 8) * *(int32_t *)((char *)arg2 + 0xc)));
    return 1;
}

static int32_t sub_dfbd0(void *arg1, int32_t *arg2, void *arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10)
{
    char arg_100[0x24];
    int32_t *i;
    char *v1 = arg_100;

    i = (int32_t *)"/tmp/mountdir/ivsbasemovesnap.nv12";
    while ((uintptr_t)i != 0x1049a8U) {
        int32_t t0_1 = i[0];
        int32_t a3_1 = i[1];
        int32_t a2_1 = i[2];
        int32_t a1_1 = i[3];
        i = &i[4];
        *(int32_t *)&v1[0] = t0_1;
        *(int32_t *)&v1[4] = a3_1;
        *(int32_t *)&v1[8] = a2_1;
        *(int32_t *)&v1[12] = a1_1;
        v1 = &v1[16];
    }
    {
        char v0 = *((char *)i + 2);
        *(int32_t *)&v1[0] = *i;
        v1[4] = v0;
    }

    if (access(arg_100, 6) == 0) {
        if (arg5 < (((*(int32_t *)((char *)arg3 + 8) * *(int32_t *)((char *)arg3 + 0xc) * 3) & 0xfffffffe))) {
            FILE *v0_13 = fopen(arg_100, "w+");
            if (v0_13 != NULL) {
                fwrite(*(void **)((char *)arg3 + 0x1c), 1,
                    (size_t)(*(int32_t *)((char *)arg3 + 8) * *(int32_t *)((char *)arg3 + 0xc)), v0_13);
                {
                    int32_t a2_5 = *(int32_t *)((char *)arg3 + 0xc);
                    int32_t v1_12 = *(int32_t *)((char *)arg3 + 8);
                    int32_t a3_3 = ((a2_5 + 0xf) & 0xfffffff0) * v1_12;

                    fwrite((char *)(*(intptr_t *)((char *)arg3 + 0x1c) + a3_3), 1,
                        (size_t)(((uint32_t)(v1_12 * a2_5)) >> 1), v0_13);
                }
                fclose(v0_13);
            }
        }
    }

    {
        int32_t *s0 = *(int32_t **)((char *)arg1 + 4);
        int32_t v0_2 = s0[5];
        int32_t a0_3;

        if (v0_2 < s0[0xa]) {
            s0[5] = v0_2 + 1;
            a0_3 = s0[0xe];
        } else {
            int32_t a0_2 = s0[4];
            int32_t s4_1 = s0[0xf];
            int32_t v1_4;
            int32_t hi_2;

            if (a0_2 == 0) {
                __builtin_trap();
            }

            v1_4 = v0_2 + s0[3];
            s0[0xe] = 0;
            hi_2 = v1_4 % a0_2;

            if (a0_2 == 0) {
                __builtin_trap();
            }

            if ((v0_2 % a0_2) != 0 || v0_2 < s0[0xb]) {
                a0_3 = 0;

                if (hi_2 == 0) {
                    memcpy((void *)(intptr_t)(*(int32_t *)((char *)(intptr_t)s0[2] + (s0[8] << 2))),
                        (void *)(intptr_t)s4_1, (size_t)(s0[0] * s0[1]));

                    {
                        int32_t v1_14 = s0[7];
                        int32_t a0_11 = s0[9];
                        int32_t hi_3 = (s0[8] + 1) % v1_14;

                        if (v1_14 == 0) {
                            __builtin_trap();
                        }

                        s0[8] = hi_3;
                        if (hi_3 == a0_11) {
                            if (v1_14 == 0) {
                                __builtin_trap();
                            }
                            a0_3 = s0[0xe];
                            v0_2 = s0[5];
                            s0[9] = (hi_3 + 1) % v1_14;
                        } else {
                            v0_2 = s0[5];
                            a0_3 = s0[0xe];
                        }
                    }
                }
            } else {
                int32_t v0_29;

                memcpy((void *)(intptr_t)(*(int32_t *)((char *)(intptr_t)s0[2] + (s0[8] << 2))),
                    (void *)(intptr_t)s4_1, (size_t)(s0[0] * s0[1]));

                {
                    void *v1_16 = (void *)(intptr_t)s0[2];
                    int32_t a1_9 = *(int32_t *)((char *)v1_16 + (s0[8] << 2));
                    int32_t v1_18 = *(int32_t *)((char *)v1_16 + (s0[9] << 2));
                    v0_29 = s0[0x10];
                    s0[0x20] = arg2[1];
                    s0[0x1b] = s4_1;
                    s0[0x11] = a1_9;
                    s0[0x16] = v1_18;
                }

                if (sad((void *)(intptr_t)s0[0x11], s0[0x12], s0[0x13], s0[0x14], s0[0x15],
                        (void *)(intptr_t)s0[0x16], s0[0x17], s0[0x18], s0[0x19], s0[0x1a],
                        (void *)(intptr_t)s0[0x1b], s0[0x1c], s0[0x1d], s0[0x1e], s0[0x1f],
                        (void *)(intptr_t)s0[0x20], s0[0x21], s0[0x22], s0[0x23], s0[0x24],
                        v0_29, s0[0xc], s0[0xd], 0) == 0) {
                    s0[0xe] = 1;
                }

                if (hi_2 == 0) {
                    int32_t v1_22 = s0[7];
                    int32_t a0_20 = s0[9];
                    int32_t hi_4 = (s0[8] + 1) % v1_22;

                    if (v1_22 == 0) {
                        __builtin_trap();
                    }

                    s0[8] = hi_4;
                    if (hi_4 == a0_20) {
                        if (v1_22 == 0) {
                            __builtin_trap();
                        }
                        s0[9] = (hi_4 + 1) % v1_22;
                    }
                }

                v0_2 = s0[5];
                a0_3 = s0[0xe];
                s0[6] += 1;
            }

            s0[5] = v0_2 + 1;
        }

        {
            int32_t a2_2 = *(int32_t *)((char *)arg3 + 0x20);
            int32_t a3_2 = *(int32_t *)((char *)arg3 + 0x24);
            int32_t v1_8 = s0[0x23] * s0[0x24];

            arg2[0] = a0_3;
            arg2[4] = a2_2;
            arg2[5] = a3_2;
            arg2[2] = v1_8;
        }
    }

    return 0;
}

int32_t imp_base_move_process(void *arg1, void *arg2, int32_t *arg3, int32_t arg4)
{
    const char *var_134;
    int32_t v0_3;
    int32_t v1_1;
    void *s0 = arg1;

    if (arg1 == NULL) {
        v0_3 = IMP_Log_Get_Option();
        var_134 = "move is NULL !\n";
        v1_1 = 0x173;
    } else {
        int32_t t9_1 = *(int32_t *)((char *)arg1 + 0x4c);
        void *s3_1 = arg2;
        int32_t *s1_1 = arg3;

        if (t9_1 != 0) {
            arg4 = t9_1;
        }

        if (s1_1 != NULL) {
            return sub_dfbd0(s0, s1_1, s3_1, arg4, 0, 0, 0, 0, 0, 0);
        }

        v0_3 = IMP_Log_Get_Option();
        var_134 = "result is NULL !\n";
        v1_1 = 0x17c;
    }

    imp_log_fun(6, v0_3, 2, "IVS_MOVE",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/src/ivs_base_move.c", v1_1,
        "imp_base_move_process", var_134);
    return -1;
}

int32_t imp_get_base_move_param(void *arg1, uint32_t *arg2)
{
    uint32_t *i = (uint32_t *)((char *)arg1 + 8);

    if (arg1 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/src/ivs_base_move.c", 0x1a1,
            "imp_get_base_move_param", "move is NULL\n");
        return -1;
    }

    do {
        arg2[0] = i[0];
        arg2[1] = i[1];
        arg2[2] = i[2];
        arg2[3] = i[3];
        i = &i[4];
        arg2 = &arg2[4];
    } while ((char *)i != (char *)arg1 + 0x48);

    return 0;
}

int32_t imp_set_base_move_param(void *arg1, void *arg2)
{
    int32_t result;

    if (arg1 == NULL || arg2 == NULL) {
        result = -1;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/src/ivs_base_move.c", 0x1ac,
            "imp_set_base_move_param", "move=%p, param=%p is NULL\n", arg1, arg2);
    } else {
        int32_t s0_1 = *(int32_t *)((char *)arg2 + 0xc);
        int32_t *v0 = *(int32_t **)((char *)arg1 + 4);

        result = *(int32_t *)((char *)arg2 + 8);
        v0[0xd] = result;

        if ((uint32_t)s0_1 >= 4U) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/src/ivs_base_move.c", 0xb9,
                "reset_param", "sense value error, %d\n", s0_1);
            return -1;
        }

        if (s0_1 == 2) {
            v0[0xc] = 0xf;
        } else if (s0_1 == 3) {
            v0[0xc] = 0xa;
        } else if (s0_1 == 1) {
            v0[0xc] = 0x14;
        } else {
            v0[0xc] = 0x1e;
        }

        if (result != 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_base_move/src/ivs_base_move.c", 0xcd,
                "reset_param", "sad mode error %d\n", result);
            return -1;
        }

        {
            int32_t v1_3 = v0[0] >> 3;
            int32_t a0_1 = v0[1] >> 3;

            v0[0x23] = v1_3;
            v0[0x24] = a0_1;
            v0[0x22] = v1_3;
        }
    }

    return result;
}

int32_t sad(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, void *arg6,
    int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10, void *arg11, int32_t arg12,
    int32_t arg13, int32_t arg14, int32_t arg15, void *arg16, int32_t arg17, int32_t arg18,
    int32_t arg19, int32_t arg20, int32_t arg21, int32_t arg22, int32_t arg23, int32_t arg24)
{
    if (arg1 == NULL || arg6 == NULL || arg11 == NULL || arg16 == NULL) {
        printf("%s: %d: invalid parameter error: input data pointer is NULL: %p ,%p, %p\n",
            "sad", 0x76, arg1, arg6, arg16);
        return -1;
    }

    if (arg9 != arg4 || arg10 != arg5) {
        printf("%s: %d: invalid parameter error: Size of src1 is different from Size of src2\n",
            "sad", 0x7b);
        return -1;
    }

    if (arg18 != 0) {
        printf("%s: %d: invalid parameter error:Mode out of range\n", "sad", 0x96);
        return 0;
    }

    {
        uint32_t a0_2 = ((uint32_t)(arg9 >> 0x1f)) >> 0x1d;

        if (((arg9 + (int32_t)a0_2) & 7) == (int32_t)a0_2) {
            uint32_t a0_4 = ((uint32_t)(arg10 >> 0x1f)) >> 0x1d;
            int32_t result = ((arg10 + (int32_t)a0_4) & 7) - (int32_t)a0_4;

            if (result == 0) {
                if (arg20 != 0) {
                    MergeBaseMove((int32_t)(intptr_t)arg1, arg2, arg3, arg4, arg5,
                        (int32_t)(intptr_t)arg6, arg7, arg8, arg9, arg10,
                        (int32_t)(intptr_t)arg11, arg12, arg13, arg14, arg15, arg19, arg20);
                    if (arg15 - 8 >= 0) {
                        int32_t t7_1 = 0;
                        int32_t t4_1 = 0;
                        int32_t i = 0;

                        do {
                            if (arg14 - 8 >= 0) {
                                int32_t j = 0;
                                char *t2_1 = (char *)arg16 + t7_1;

                                do {
                                    char *a2_3 = (char *)arg11 + j + t4_1;
                                    int32_t k = 8;
                                    int32_t v1_1 = 0;

                                    do {
                                        char *v0_14 = a2_3;
                                        do {
                                            uint32_t a0_7 = (uint8_t)*v0_14;
                                            v0_14 = &v0_14[1];
                                            v1_1 += (int32_t)a0_7;
                                        } while (&a2_3[8] != v0_14);
                                        k -= 1;
                                        a2_3 = &a2_3[arg13];
                                    } while (k != 0);

                                    j += 8;
                                    *t2_1 = (char)v1_1;
                                    t2_1 = &t2_1[1];
                                } while (arg14 - 8 >= j);
                            }

                            i += 8;
                            t4_1 += arg13 << 3;
                            t7_1 += arg17;
                        } while (arg15 - 8 >= i);
                    }

                    return result;
                }

                if (arg10 - 8 < 0) {
                    printf("total_count:  %d\n", 0);
                } else {
                    int32_t v0_19 = arg3 << 3;
                    int32_t var_38_1 = 0;
                    int32_t var_48_1 = 0;
                    int32_t var_4c_1 = 0;
                    int32_t var_54_1 = 0;

                    while (1) {
                        if (arg9 - 8 < 0) {
                            int32_t v0_82 = var_4c_1 + 8;
                            var_4c_1 = v0_82;
                            var_48_1 += v0_19;
                            var_38_1 += arg17;
                            if (arg10 - 8 < v0_82) {
                                break;
                            }
                        } else {
                            int32_t j_3 = 0;
                            char *var_50_1 = (char *)arg16 + var_38_1;
                            int32_t v0_24 = var_4c_1 + 8;

                            do {
                                int32_t s2_1 = var_4c_1;
                                int32_t t9_1 = var_48_1;
                                int32_t ra_1 = 0;

                                do {
                                    int32_t t8_2 = (0 < s2_1) ? 1 : 0;
                                    int32_t t7_2 = (s2_1 < arg10 - 1) ? 1 : 0;
                                    int32_t v0_25;
                                    int32_t t7_3;
                                    int32_t v0_26;
                                    int32_t a3_1;
                                    int32_t t3_3;
                                    char *t6_1;
                                    char *t5_3;
                                    int32_t j_1;
                                    char *a3_2;
                                    char *t3_4;
                                    int32_t t2_2;
                                    char *t4_2;
                                    char *t2_3;

                                    s2_1 += 1;
                                    v0_25 = s2_1;
                                    if (t7_2 == 0) {
                                        v0_25 = arg10 - 1;
                                    }
                                    t7_3 = v0_25 * arg3;
                                    v0_26 = t9_1 - arg3;
                                    if (t8_2 == 0) {
                                        v0_26 = 0;
                                    }
                                    a3_1 = t9_1 + j_3;
                                    t3_3 = v0_26 + j_3;
                                    t6_1 = (char *)arg1 + a3_1;
                                    t5_3 = (char *)arg1 + t3_3;
                                    j_1 = j_3;
                                    a3_2 = (char *)arg6 + a3_1;
                                    t3_4 = (char *)arg6 + t3_3;
                                    t2_2 = t7_3 + j_3;
                                    t4_2 = (char *)arg1 + t2_2;
                                    t2_3 = (char *)arg6 + t2_2;

                                    do {
                                        int32_t a0_9 = j_1 - 1;
                                        int32_t j_2;
                                        int32_t v0_28;
                                        uint32_t v0_30;
                                        int32_t fp_2;
                                        uint32_t s6_3;
                                        int32_t s7_3;
                                        int32_t v0_32;
                                        int32_t s6_5;
                                        int32_t v0_33;
                                        uint32_t fp_4;
                                        int32_t v0_34;
                                        uint32_t v0_36;
                                        int32_t s6_9;
                                        int32_t v0_38;
                                        uint32_t s7_8;
                                        int32_t v0_40;
                                        uint32_t v0_42;
                                        int32_t fp_7;
                                        int32_t v0_44;
                                        uint32_t s6_14;
                                        uint32_t v0_47;
                                        int32_t fp_10;
                                        int32_t v0_49;
                                        uint32_t s7_9;
                                        int32_t v0_51;
                                        uint32_t v0_53;
                                        int32_t fp_14;
                                        int32_t v0_55;
                                        uint32_t s6_18;
                                        int32_t v1_6;
                                        uint32_t v0_59;
                                        int32_t v1_9;
                                        int32_t v0_61;
                                        uint32_t s7_12;
                                        uint32_t v0_64;
                                        int32_t s6_22;
                                        int32_t v0_66;
                                        uint32_t v1_13;
                                        int32_t a0_10;
                                        uint32_t a0_12;
                                        int32_t s6_25;
                                        int32_t v0_71;
                                        uint32_t s7_13;

                                        if (0 >= j_1) {
                                            a0_9 = 0;
                                        }
                                        if (j_1 >= arg9 - 1) {
                                            j_2 = arg9 - 1;
                                            j_1 += 1;
                                        } else {
                                            j_1 += 1;
                                            j_2 = j_1;
                                        }

                                        v0_28 = v0_26 + a0_9;
                                        v0_30 = (uint8_t)*((char *)arg1 + v0_28) - (uint8_t)*((char *)arg6 + v0_28);
                                        fp_2 = (int32_t)v0_30 >> 0x1f;
                                        s6_3 = (uint8_t)*t5_3 - (uint8_t)*t3_4;
                                        s7_3 = (int32_t)s6_3 >> 0x1f;
                                        v0_32 = (fp_2 ^ (int32_t)v0_30) - fp_2;
                                        s6_5 = (s7_3 ^ (int32_t)s6_3) - s7_3;
                                        v0_33 = v0_32 & 0xff;
                                        if (v0_32 < arg19) {
                                            v0_33 = 0;
                                        }
                                        fp_4 = 0;
                                        if (s6_5 >= arg19) {
                                            fp_4 = (uint32_t)(s6_5 & 0xff);
                                            if ((uint32_t)v0_33 < (uint32_t)(s6_5 & 0xff)) {
                                                fp_4 = (uint32_t)(uint8_t)v0_33;
                                            }
                                        }

                                        v0_34 = j_2 + v0_26;
                                        v0_36 = (uint8_t)*((char *)arg1 + v0_34) - (uint8_t)*((char *)arg6 + v0_34);
                                        s6_9 = (int32_t)v0_36 >> 0x1f;
                                        v0_38 = (s6_9 ^ (int32_t)v0_36) - s6_9;
                                        s7_8 = 0;
                                        if (v0_38 >= arg19) {
                                            s7_8 = (uint32_t)(v0_38 & 0xff);
                                            if (fp_4 < (uint32_t)(v0_38 & 0xff)) {
                                                s7_8 = (uint32_t)(uint8_t)fp_4;
                                            }
                                        }

                                        v0_40 = t9_1 + a0_9;
                                        v0_42 = (uint8_t)*((char *)arg1 + v0_40) - (uint8_t)*((char *)arg6 + v0_40);
                                        fp_7 = (int32_t)v0_42 >> 0x1f;
                                        v0_44 = (fp_7 ^ (int32_t)v0_42) - fp_7;
                                        s6_14 = 0;
                                        if (v0_44 >= arg19) {
                                            s6_14 = (uint32_t)(v0_44 & 0xff);
                                            if (s7_8 < (uint32_t)(v0_44 & 0xff)) {
                                                s6_14 = (uint32_t)(uint8_t)s7_8;
                                            }
                                        }

                                        v0_47 = (uint8_t)*t6_1 - (uint8_t)*a3_2;
                                        fp_10 = (int32_t)v0_47 >> 0x1f;
                                        v0_49 = (fp_10 ^ (int32_t)v0_47) - fp_10;
                                        s7_9 = 0;
                                        if (v0_49 >= arg19) {
                                            s7_9 = (uint32_t)(v0_49 & 0xff);
                                            if (s6_14 < (uint32_t)(v0_49 & 0xff)) {
                                                s7_9 = (uint32_t)(uint8_t)s6_14;
                                            }
                                        }

                                        v0_51 = j_2 + t9_1;
                                        v0_53 = (uint8_t)*((char *)arg1 + v0_51) - (uint8_t)*((char *)arg6 + v0_51);
                                        fp_14 = (int32_t)v0_53 >> 0x1f;
                                        v0_55 = (fp_14 ^ (int32_t)v0_53) - fp_14;
                                        s6_18 = 0;
                                        if (v0_55 >= arg19) {
                                            s6_18 = (uint32_t)(v0_55 & 0xff);
                                            if (s7_9 < (uint32_t)(v0_55 & 0xff)) {
                                                s6_18 = (uint32_t)(uint8_t)s7_9;
                                            }
                                        }

                                        v1_6 = t7_3 + a0_9;
                                        v0_59 = (uint8_t)*((char *)arg1 + v1_6) - (uint8_t)*((char *)arg6 + v1_6);
                                        v1_9 = (int32_t)v0_59 >> 0x1f;
                                        v0_61 = (v1_9 ^ (int32_t)v0_59) - v1_9;
                                        s7_12 = 0;
                                        if (v0_61 >= arg19) {
                                            s7_12 = (uint32_t)(v0_61 & 0xff);
                                            if (s6_18 < (uint32_t)(v0_61 & 0xff)) {
                                                s7_12 = (uint32_t)(uint8_t)s6_18;
                                            }
                                        }

                                        v0_64 = (uint8_t)*t4_2 - (uint8_t)*t2_3;
                                        s6_22 = (int32_t)v0_64 >> 0x1f;
                                        v0_66 = (s6_22 ^ (int32_t)v0_64) - s6_22;
                                        v1_13 = 0;
                                        if (v0_66 >= arg19) {
                                            v1_13 = (uint32_t)(v0_66 & 0xff);
                                            if (s7_12 < (uint32_t)(v0_66 & 0xff)) {
                                                v1_13 = (uint32_t)(uint8_t)s7_12;
                                            }
                                        }

                                        a0_10 = j_2 + t7_3;
                                        a0_12 = (uint8_t)*((char *)arg1 + a0_10) - (uint8_t)*((char *)arg6 + a0_10);
                                        s6_25 = (int32_t)a0_12 >> 0x1f;
                                        v0_71 = (s6_25 ^ (int32_t)a0_12) - s6_25;
                                        s7_13 = 0;
                                        if (v0_71 >= arg19) {
                                            s7_13 = (uint32_t)(v0_71 & 0xff);
                                            if (v1_13 < (uint32_t)(v0_71 & 0xff)) {
                                                s7_13 = (uint32_t)(uint8_t)v1_13;
                                            }
                                        }

                                        ra_1 += (int32_t)s7_13;
                                        t4_2 = &t4_2[1];
                                        t2_3 = &t2_3[1];
                                        t6_1 = &t6_1[1];
                                        a3_2 = &a3_2[1];
                                        t5_3 = &t5_3[1];
                                        t3_4 = &t3_4[1];
                                    } while (j_3 + 8 != j_1);

                                    t9_1 += arg3;
                                } while (v0_24 != s2_1);

                                var_54_1 += (0U < (uint32_t)(ra_1 & 0xff)) ? 1 : 0;
                                {
                                    int32_t i_1 = (arg9 - 8 < j_3 + 8) ? 1 : 0;
                                    j_3 += 8;
                                    *var_50_1 = (char)ra_1;
                                    var_50_1 = &var_50_1[1];
                                    if (i_1 != 0) {
                                        break;
                                    }
                                }
                            } while (1);

                            var_4c_1 = v0_24;
                            var_48_1 += v0_19;
                            var_38_1 += arg17;
                            if (arg10 - 8 < v0_24) {
                                break;
                            }
                        }
                    }

                    printf("total_count:  %d\n", var_54_1);
                }

                return result;
            }
        }
    }

    printf("%s: %d: invalid parameter error: src.width MOD %d or src.height MOD %d is not equal to 0\n",
        "sad", 0x8a, 8, 8);
    return -1;
}
