#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "imp/imp_ivs.h"
#include "imp/imp_ivs_move.h"

int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
int32_t c_clip3(int32_t arg1, int32_t arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t is_has_simd128(void); /* forward decl, ported by T<N> later */
void *Init(int32_t arg1, int32_t arg2, int32_t arg3, int32_t *arg4); /* forward decl, ported by T<N> later */
void freeFilterEngine(void *arg1); /* forward decl, ported by T<N> later */
int32_t move_detect(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7,
    int32_t arg8, void *arg9, int32_t *arg10, int32_t arg11); /* forward decl, ported by T<N> later */

extern char _gp; /* MIPS GOT base symbol */

typedef struct IMPIVSMoveInterface {
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
    uint32_t reserved2;
    uint8_t param_storage[0x450];
} IMPIVSMoveInterface;

typedef struct MoveContext {
    int32_t width;
    int32_t height;
    int32_t frame_depth;
    int32_t *frames;
    int32_t *sense_map;
    int32_t frame_count;
    int32_t use_simd;
    int32_t skip_frame_cnt;
    int32_t cur_idx;
    int32_t old_idx;
    int32_t *ret_for_roi;
    uint8_t pad2c[0x14];
    void *filter_engine;
    void *resize_frame;
    uint8_t pad48[8];
} MoveContext;

typedef struct MoveResultRing {
    int32_t read_idx;
    int32_t write_idx;
    int32_t results;
} MoveResultRing;

typedef struct MoveHandle {
    int32_t state;
    MoveContext *ctx;
    uint8_t param_storage[0x450];
    MoveResultRing *result_ring;
    int32_t (*release_data)(void *vaddr);
} MoveHandle;

int32_t *CreateImage(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4, char arg5); /* forward decl, ported by T<N> later */
int32_t sub_da8dc(void); /* forward decl */
int32_t *sub_da760(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7,
    int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11, int32_t arg12, int32_t *arg13, int32_t arg14); /* forward decl */
MoveHandle *imp_alloc_move(uint32_t *arg1, int32_t arg2); /* forward decl */
void imp_free_move(void *arg1); /* forward decl */
int32_t imp_move_preprocess(void *arg1, void *arg2); /* forward decl */
int32_t imp_move_process(void *arg1, void *arg2, int32_t arg3); /* forward decl */
int32_t imp_get_move_param(void *arg1, uint32_t *arg2); /* forward decl */
int32_t imp_set_move_param(void *arg1, uint32_t *arg2); /* forward decl */
int32_t imp_flush_frame(void *arg1); /* forward decl */
static IMPIVSInterface *sub_da058(uint32_t *arg1, int32_t arg2, int32_t arg3);
static void sub_da1c4(IMPIVSInterface *arg1);

static void copy_u32_block(uint32_t *dst, const uint32_t *src, uint32_t words)
{
    const uint32_t *i = src;
    uint32_t *out = dst;

    do {
        out[0] = i[0];
        out[1] = i[1];
        out[2] = i[2];
        out[3] = i[3];
        i = &i[4];
        out = &out[4];
    } while (i != &src[words]);
}

int32_t MoveReleaseResult(void)
{
    return 0;
}

int32_t sub_d9630(void)
{
    return 0;
}

int32_t sub_d9670(IMPIVSMoveInterface *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    MoveHandle *s1_3 = *(MoveHandle **)((char *)arg1 + 0x30);
    int32_t result;

    (void)arg3;
    (void)arg4;
    (void)arg5;

    if (s1_3 == NULL) {
        result = -1;
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x8d,
            "MoveProcessAsync", "ivsMove is null\n", &_gp);
    } else {
        MoveResultRing *v0 = *(MoveResultRing **)((char *)s1_3 + 0x458);
        int32_t result_1 = imp_move_process(s1_3, arg2, v0->results + v0->write_idx * 0xd0);

        if (result_1 == 0) {
            MoveResultRing *a1 = *(MoveResultRing **)((char *)s1_3 + 0x458);

            a1->write_idx = (a1->write_idx + 1) % 6;
            return 0;
        }

        result = result_1;
        if (result_1 < 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x99,
                "MoveProcessAsync", "imp_move_process() error !\n", &_gp);
        }
    }

    return result;
}

int32_t MoveProcessAsync(IMPIVSMoveInterface *arg1, void *arg2)
{
    void *var_18 = &_gp;

    (void)var_18;
    return sub_d9670(arg1, arg2, (int32_t)(intptr_t)&_gp, 0, 0);
}

int32_t sub_d9830(IMPIVSMoveInterface *arg1, int32_t arg2, int32_t arg3, int32_t arg4)
{
    MoveHandle *s0_3 = *(MoveHandle **)((char *)arg1 + 0x30);
    int32_t gp_4;

    (void)arg3;
    (void)arg4;

    if (s0_3 == NULL) {
        return imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x4b,
            "MoveExit", "ivsMove is null\n", &_gp);
    }

    {
        MoveResultRing *v0 = *(MoveResultRing **)((char *)s0_3 + 0x458);

        if (v0 == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x52,
                "MoveExit", "resultRing is null\n", &_gp);
            gp_4 = arg2;
        } else {
            void *a0 = *(void **)((char *)v0 + 8);

            if (a0 != NULL) {
                free(a0);
            } else {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x57,
                    "MoveExit", "resultRing->results is null\n", &_gp);
            }

            free(*(void **)((char *)s0_3 + 0x458));
            {
                imp_free_move(s0_3);
                *(MoveHandle **)((char *)arg1 + 0x30) = NULL;
                return 0;
            }
        }
    }

    {
        imp_free_move(s0_3);
        *(MoveHandle **)((char *)arg1 + 0x30) = NULL;
        (void)gp_4;
        return 0;
    }
}

void MoveExit(IMPIVSMoveInterface *arg1)
{
    void *var_18 = &_gp;

    (void)var_18;
    sub_d9830(arg1, (int32_t)(intptr_t)&_gp, 0, 0);
}

int32_t sub_d9a28(IMPIVSMoveInterface *arg1, IMPIVSMoveInterface *arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int32_t arg6)
{
    uint32_t *a0_4 = *(uint32_t **)arg1;

    (void)arg4;
    (void)arg5;
    (void)arg6;

    if (a0_4 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x1e,
            "MoveInit", "param is null\n", &_gp);
        return -1;
    }

    {
        MoveHandle *v0 = imp_alloc_move(a0_4, (int32_t)(intptr_t)IMP_IVS_ReleaseData);

        if (v0 == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x24,
                "MoveInit", "imp_alloc_move() error !\n", &_gp);
            *(MoveHandle **)((char *)arg2 + 0x30) = NULL;
            return -1;
        }

        {
            MoveResultRing *v0_1 = calloc(1, 0xc);

            *(MoveResultRing **)((char *)v0 + 0x458) = v0_1;
            if (v0_1 == NULL) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x2a,
                    "MoveInit", "malloc resultRing failed\n", &_gp);
                free(*(void **)((char *)v0 + 0x458));
                imp_free_move(v0);
                *(MoveHandle **)((char *)arg2 + 0x30) = NULL;
                return -1;
            }

            {
                int32_t v0_2 = (int32_t)(intptr_t)calloc(6, 0xd0);

                v0_1->results = v0_2;
                if (v0_2 != 0) {
                    v0_1->write_idx = 0;
                    v0_1->read_idx = 0;
                    *(MoveHandle **)((char *)arg2 + 0x30) = v0;
                    (void)arg3;
                    return 0;
                }

                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x30,
                    "MoveInit", "malloc resultRing->results failed\n", &_gp);
                free(*(void **)((char *)v0 + 0x458));
                imp_free_move(v0);
                *(MoveHandle **)((char *)arg2 + 0x30) = NULL;
            }
        }
    }

    return -1;
}

int32_t MoveInit(IMPIVSMoveInterface *arg1)
{
    void *var_18 = &_gp;

    (void)var_18;
    return sub_d9a28(arg1, arg1, (int32_t)(intptr_t)&_gp, 0, 0, 0);
}

int32_t sub_d9c8c(IMPIVSMoveInterface *arg1, int32_t *arg2, int32_t arg3)
{
    MoveHandle *a2_1 = *(MoveHandle **)((char *)arg1 + 0x30);

    if (a2_1 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0xa6,
            "MoveGetResult", "ivsMove is null\n", &_gp);
        return -1;
    }

    {
        MoveResultRing *v0 = *(MoveResultRing **)((char *)a2_1 + 0x458);
        MoveResultRing *a2 = *(MoveResultRing **)((char *)a2_1 + 0x458);

        *arg2 = v0->results + v0->read_idx * 0xd0;
        a2->read_idx = (a2->read_idx + 1) % 6;
        return 0;
    }
}

int32_t MoveGetResult(IMPIVSMoveInterface *arg1, int32_t *arg2)
{
    void *var_10 = &_gp;

    (void)var_10;
    return sub_d9c8c(arg1, arg2, (int32_t)(intptr_t)&_gp);
}

int32_t sub_d9d98(IMPIVSMoveInterface *arg1, int32_t arg2, int32_t arg3)
{
    MoveHandle *a0_3 = *(MoveHandle **)((char *)arg1 + 0x30);

    if (a0_3 != NULL) {
        return imp_flush_frame(a0_3);
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0xd5,
        "MoveFlushFrame", "ivsMove is null\n", &_gp);
    return -1;
}

int32_t MoveFlushFrame(IMPIVSMoveInterface *arg1)
{
    void *var_10 = &_gp;

    (void)var_10;
    return sub_d9d98(arg1, (int32_t)(intptr_t)&_gp, 0);
}

int32_t sub_d9e48(IMPIVSMoveInterface *arg1, int32_t arg2, int32_t arg3)
{
    MoveHandle *a0_3 = *(MoveHandle **)((char *)arg1 + 0x30);

    if (a0_3 != NULL) {
        return imp_set_move_param(a0_3, (uint32_t *)arg1->param);
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0xca,
        "MoveSetParam", "ivsMove is null\n", &_gp);
    return -1;
}

int32_t MoveSetParam(IMPIVSMoveInterface *arg1, void *arg2)
{
    void *var_10 = &_gp;

    (void)arg2;
    (void)var_10;
    return sub_d9e48(arg1, (int32_t)(intptr_t)&_gp, 0);
}

int32_t sub_d9ef8(IMPIVSMoveInterface *arg1, int32_t arg2, int32_t arg3)
{
    MoveHandle *a0_3 = *(MoveHandle **)((char *)arg1 + 0x30);

    if (a0_3 != NULL) {
        return imp_get_move_param(a0_3, (uint32_t *)arg1->param);
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0xbf,
        "MoveGetParam", "ivsMove is null\n", &_gp);
    return -1;
}

int32_t MoveGetParam(IMPIVSMoveInterface *arg1)
{
    void *var_10 = &_gp;

    (void)var_10;
    return sub_d9ef8(arg1, (int32_t)(intptr_t)&_gp, 0);
}

int32_t sub_d9fa8(IMPIVSMoveInterface *arg1, int32_t arg2, int32_t arg3)
{
    MoveHandle *a0_3 = *(MoveHandle **)((char *)arg1 + 0x30);

    if (a0_3 != NULL) {
        return imp_move_preprocess(a0_3, *(void **)(intptr_t)arg2);
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x70,
        "MovePreprocessSync", "ivsMove is null\n", &_gp);
    return -1;
}

int32_t MovePreprocessSync(IMPIVSMoveInterface *arg1, void *arg2)
{
    void *var_10 = &_gp;

    (void)var_10;
    return sub_d9fa8(arg1, (int32_t)(intptr_t)&arg2, 0);
}

static IMPIVSInterface *sub_da058(uint32_t *arg1, int32_t arg2, int32_t arg3)
{
    void *result_raw = calloc(1, 0x484);
    IMPIVSMoveInterface *result = (IMPIVSMoveInterface *)result_raw;

    if (result == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0xe1,
            "IMP_IVS_CreateMoveInterface", "malloc interface failed\n", &_gp);
        return NULL;
    }

    result->param = result->param_storage;
    copy_u32_block((uint32_t *)result->param_storage, arg1, 0x114);
    result->param_size = 0x450;
    result->reserved = (void *)0xa;
    result->init = MoveInit;
    result->exit = MoveExit;
    result->preProcessSync = MovePreprocessSync;
    result->processAsync = MoveProcessAsync;
    result->getResult = MoveGetResult;
    result->releaseResult = MoveReleaseResult;
    result->getParam = MoveGetParam;
    result->updateParam = MoveSetParam;
    result->stop = MoveFlushFrame;
    return (IMPIVSInterface *)result;
}

IMPIVSInterface *IMP_IVS_CreateMoveInterface(IMP_IVS_MoveParam *param)
{
    void *var_10 = &_gp;

    (void)var_10;
    return sub_da058((uint32_t *)param, (int32_t)(intptr_t)&_gp, 0);
}

static void sub_da1c4(IMPIVSInterface *arg1)
{
    if (arg1 == NULL) {
        return;
    }

    free(arg1);
}

void IMP_IVS_DestroyMoveInterface(IMPIVSInterface *moveInterface)
{
    sub_da1c4(moveInterface);
}

static int32_t reset_param_isra_0(int32_t *arg1, void **arg2, uint32_t *arg3, int32_t *arg4, int32_t arg5, int32_t arg6,
    int32_t arg7, int32_t *arg8)
{
    uint32_t *i_2 = arg3;
    int32_t *i = arg4;
    int32_t v0 = c_clip3(arg5, 0, 0x34);
    int32_t a2 = v0;
    uint32_t *i_3;

    *arg1 = v0;
    if (i == NULL) {
        i_3 = i_2;
    } else {
        i_3 = i_2;
        if ((uint32_t)(arg5 - 1) < 0x34U) {
            int32_t a0_1 = *(int32_t *)arg2;

            if (a0_1 != 0) {
                freeFilterEngine((void *)(intptr_t)a0_1);
                a2 = *arg1;
            }

            {
                int32_t var_378[0xd0 / 4];

                if (a2 > 0) {
                    int32_t *i_4 = i;
                    int32_t *s0_3 = var_378;
                    int32_t fp_1 = 0;

                    do {
                        int32_t v0_4 = c_clip3(*i_4, 0, arg6 - 1);
                        int32_t a0_3 = i_4[1];
                        int32_t v0_6;
                        int32_t a0_4;
                        int32_t v0_8;
                        int32_t a0_5;

                        *s0_3 = v0_4 >> 1;
                        v0_6 = c_clip3(a0_3, 0, arg7 - 1);
                        a0_4 = i_4[2];
                        s0_3[1] = v0_6 >> 1;
                        v0_8 = c_clip3(a0_4, 0, arg6 - 1);
                        a0_5 = i_4[3];
                        s0_3[2] = v0_8 >> 1;
                        s0_3[3] = c_clip3(a0_5, 0, arg7 - 1) >> 1;
                        fp_1 += 1;
                        i_4 = &i_4[4];
                        s0_3 = &s0_3[4];
                    } while (fp_1 < a2);
                }

                {
                    int32_t v0_12 = (int32_t)(intptr_t)Init(arg6 >> 1, arg7 >> 1, a2, var_378);

                    *arg2 = (void *)(intptr_t)v0_12;
                    if (v0_12 == 0) {
                        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x3b,
                            "reset_param", "%s(%d): Init ivsMove->filterEngine failed\n",
                            "reset_param", 0x3b, &_gp);
                        return -1;
                    }
                }

                {
                    int32_t *v0_14 = &arg8[0x42];

                    arg8[0x112] = *arg1;
                    do {
                        v0_14[0] = i[0];
                        v0_14[1] = i[1];
                        v0_14[2] = i[2];
                        v0_14[3] = i[3];
                        i = &i[4];
                        v0_14 = &v0_14[4];
                    } while (i != &i[0xd0]);
                    a2 = *arg1;
                }
            }
        }
    }

    {
        int32_t i_1 = 0;

        if (a2 > 0) {
            do {
                int32_t v0_15 = *(int32_t *)i_3;
                int32_t a2_7 = i_1 << 2;

                if ((uint32_t)v0_15 >= 9) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x62,
                        "reset_param", "have none of this sense, the support range is 0-4\n");
                    {
                        int32_t a0_13 = *(int32_t *)arg2;

                        if (a0_13 == 0) {
                            return -1;
                        }

                        freeFilterEngine((void *)(intptr_t)a0_13);
                        return -1;
                    }
                }

                switch (v0_15) {
                case 0:
                    *(int32_t *)((char *)*(void **)arg2 + a2_7) = 0x555;
                    break;
                case 1:
                    *(int32_t *)((char *)*(void **)arg2 + a2_7) = 0x1c7;
                    break;
                case 2:
                    *(int32_t *)((char *)*(void **)arg2 + a2_7) = 0x97;
                    break;
                case 3:
                    *(int32_t *)((char *)*(void **)arg2 + a2_7) = 0x32;
                    break;
                case 4:
                    *(int32_t *)((char *)*(void **)arg2 + a2_7) = 0x10;
                    break;
                case 5:
                    *(int32_t *)((char *)*(void **)arg2 + a2_7) = 8;
                    break;
                case 6:
                    *(int32_t *)((char *)*(void **)arg2 + a2_7) = 4;
                    break;
                case 7:
                    *(int32_t *)((char *)*(void **)arg2 + a2_7) = 2;
                    break;
                case 8:
                    *(int32_t *)((char *)*(void **)arg2 + a2_7) = 1;
                    break;
                }

                i_1 += 1;
                i_3 = &i_3[1];
            } while (i_1 < *arg1);
        }
    }

    {
        int32_t *v0_24 = arg8;

        do {
            v0_24[0] = i_2[0];
            v0_24[1] = i_2[1];
            v0_24[2] = i_2[2];
            v0_24[3] = i_2[3];
            i_2 = &i_2[4];
            v0_24 = &v0_24[4];
        } while (i_2 != &i_2[0x34]);
    }

    return 0;
}

MoveContext *IVSMove_init(int32_t arg1, int32_t arg2, int32_t *arg3, int32_t arg4, int32_t arg5)
{
    int32_t arg_8 = (int32_t)(intptr_t)arg3;
    void *v0_raw = calloc(1, 0x50);
    MoveContext *v0 = (MoveContext *)v0_raw;

    (void)arg_8;

    if (v0 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x8c,
            "IVSMove_init", "malloc ivsMove failed\n");
    } else {
        v0->width = arg1;
        v0->height = arg2;
        v0->frame_depth = 4;
        {
            int32_t v0_1 = (int32_t)(intptr_t)calloc(0x34, 4);

            v0->ret_for_roi = (int32_t *)(intptr_t)v0_1;
            if (v0_1 != 0) {
                v0->skip_frame_cnt = arg4;
                v0->frame_count = 0;
                v0->use_simd = is_has_simd128();
                v0->sense_map = NULL;
                v0->old_idx = 0;
                v0->cur_idx = 0;
                if (v0->use_simd != 0) {
                    sub_da8dc();
                }
                return (MoveContext *)(intptr_t)sub_da760((int32_t *)v0, arg5, (int32_t)(intptr_t)&_gp, 0, 0, 0, 0, 0,
                    0, 0, 0, 0, arg3, 0);
            }

            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x96,
                "IVSMove_init", "retForRoi malloc retForRoi error\n");
            free(v0);
        }
    }

    return NULL;
}

int32_t *sub_da760(int32_t *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7,
    int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11, int32_t arg12, int32_t *arg13, int32_t arg14)
{
    size_t nitems = (size_t)arg1[2];
    void *v0 = calloc(nitems, 4);
    int32_t gp = arg3;
    int32_t gp_3;

    (void)arg4;
    (void)arg5;
    (void)arg6;
    (void)arg7;
    (void)arg8;
    (void)arg9;
    (void)arg10;
    (void)arg11;
    (void)arg12;
    (void)arg14;

    arg1[3] = (int32_t)(intptr_t)v0;
    if (v0 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0xad,
            "IVSMove_init", "malloc resize_frame failed:%s\n", "IVSMove_init", 0xad, &_gp);
        gp_3 = arg3;
    } else {
        int32_t s7_1 = arg1[0];
        int32_t s6_1 = arg1[1];
        int32_t gp_2;
        int32_t fp_2;

        if ((int32_t)nitems <= 0) {
            goto label_da92c;
        }

        {
            void *s4_1 = v0;
            int32_t fp_1 = 0;

            while (1) {
                int32_t s0_1 = fp_1 << 2;
                int32_t v0_1 = (int32_t)(intptr_t)calloc((size_t)(s7_1 * s6_1), 1);

                gp = arg3;
                *(int32_t *)((char *)s4_1 + s0_1) = v0_1;
                s4_1 = (void *)(intptr_t)arg1[3];
                fp_1 += 1;
                if (*(int32_t *)((char *)s4_1 + s0_1) == 0) {
                    int32_t i;
                    int32_t s0_2;

                    fp_2 = fp_1 - 1;
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0xb4,
                        "IVSMove_init", "malloc frame[%d] failed\n", fp_2, &_gp);
                    gp_2 = arg3;
                    i = fp_2 - 1;
                    s0_2 = i << 2;
                    if (i >= 0) {
                        do {
                            i -= 1;
                            free((void *)(intptr_t)(*(int32_t *)((char *)(intptr_t)arg1[3] + s0_2)));
                            s0_2 -= 4;
                            gp_2 = arg3;
                        } while (i != -1);
                    }

                    free((void *)(intptr_t)arg1[3]);
                    gp_3 = arg3;
                    goto label_cleanup;
                }

                if ((int32_t)nitems == fp_1) {
                    break;
                }
            }
        }

label_da92c:
        {
            int32_t v0_7 = (int32_t)(intptr_t)Init(s7_1, s6_1, arg2, arg13);

            arg1[0x10] = v0_7;
            if (v0_7 == 0) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0xbc,
                    "IVSMove_init", "Init failed\n", &_gp);
                gp_2 = arg3;
                fp_2 = arg1[2];
            } else {
                int32_t v0_10 = (int32_t)(intptr_t)calloc((size_t)((arg1[0] << 1) * (arg1[1] << 1)), 1);
                int32_t gp_9;

                arg1[0x11] = v0_10;
                if (v0_10 == 0) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0xc2,
                        "IVSMove_init", "malloc resize_tmp_buf failed:%s\n", "IVSMove_init", 0xc2, &_gp);
                    gp_9 = arg3;
                } else {
                    if (arg2 <= 0) {
                        return arg1;
                    }

                    {
                        int32_t *a2_4 = arg13;

                        while (1) {
                            int32_t v0_11 = *a2_4;

                            if ((uint32_t)v0_11 < 9) {
                                switch (v0_11) {
                                case 0:
                                    *(int32_t *)((char *)(intptr_t)arg1[0x10] + ((a2_4 - arg13) << 2)) = 0x555;
                                    break;
                                case 1:
                                    *(int32_t *)((char *)(intptr_t)arg1[0x10] + ((a2_4 - arg13) << 2)) = 0x1c7;
                                    break;
                                case 2:
                                    *(int32_t *)((char *)(intptr_t)arg1[0x10] + ((a2_4 - arg13) << 2)) = 0x97;
                                    break;
                                case 3:
                                    *(int32_t *)((char *)(intptr_t)arg1[0x10] + ((a2_4 - arg13) << 2)) = 0x32;
                                    break;
                                case 4:
                                    *(int32_t *)((char *)(intptr_t)arg1[0x10] + ((a2_4 - arg13) << 2)) = 0x10;
                                    break;
                                case 5:
                                    *(int32_t *)((char *)(intptr_t)arg1[0x10] + ((a2_4 - arg13) << 2)) = 8;
                                    break;
                                case 6:
                                    *(int32_t *)((char *)(intptr_t)arg1[0x10] + ((a2_4 - arg13) << 2)) = 4;
                                    break;
                                case 7:
                                    *(int32_t *)((char *)(intptr_t)arg1[0x10] + ((a2_4 - arg13) << 2)) = 2;
                                    break;
                                case 8:
                                    *(int32_t *)((char *)(intptr_t)arg1[0x10] + ((a2_4 - arg13) << 2)) = 1;
                                    break;
                                }
                                a2_4 += 1;
                                if (a2_4 != &arg13[arg2]) {
                                    continue;
                                }
                            } else {
                                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0xe6,
                                    "IVSMove_init",
                                    "have none of this sense, the support range is 0-4\n");
                                free((void *)(intptr_t)arg1[0x11]);
                                gp_9 = arg3;
                            }

                            return arg1;
                        }
                    }
                }

                free((void *)(intptr_t)arg1[0x10]);
                gp_2 = arg3;
                fp_2 = arg1[2];
            }

            {
                int32_t i = fp_2 - 1;
                int32_t s0_2 = i << 2;

                if (i >= 0) {
                    do {
                        i -= 1;
                        free((void *)(intptr_t)(*(int32_t *)((char *)(intptr_t)arg1[3] + s0_2)));
                        s0_2 -= 4;
                        gp_2 = arg3;
                    } while (i != -1);
                }

                free((void *)(intptr_t)arg1[3]);
                gp_3 = arg3;
            }
        }
    }

label_cleanup:
    free((void *)(intptr_t)arg1[0xa]);
    free(arg1);
    (void)gp;
    (void)gp_3;
    return NULL;
}

int32_t sub_da8dc(void)
{
    char arg_30[0x20];
    char *v1 = &arg_30[0];
    uint32_t i = 0;

    while (i != 0x20) {
        char a0_1 = (char)i + 1;

        *v1 = (char)i;
        i = (uint32_t)(uint8_t)((char)i + 4);
        v1[8] = a0_1;
        v1 = &v1[1];
    }

    return 0;
}

int32_t sub_dad24(int32_t arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7)
{
    (void)arg1;

    while (1) {
        int32_t t2_1 = arg4 + arg5;
        char *v1_1 = (char *)arg2 + (arg5 << 2);
        int32_t i = arg3 + arg5;

        do {
            char a0 = *v1_1;

            i += 1;
            v1_1 = &v1_1[2];
            *(char *)(intptr_t)(i - 1) = a0;
        } while (i != arg3 + t2_1);

        do {
            arg6 += 1;
            arg5 = t2_1;
            if (arg7 == arg6) {
                return arg5;
            }

            t2_1 = arg5 + arg4;
        } while (arg4 <= 0);
    }
}

void sub_dadbc(int32_t arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7,
    int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11)
{
    (void)arg1;
    (void)arg3;
    (void)arg11;

    while (1) {
        if (arg10 < arg4) {
            char *v1_2 = (char *)arg2 + (arg10 << 1);

            arg5 += arg10;
            do {
                char a2 = *v1_2;

                arg5 += 1;
                v1_2 = &v1_2[2];
                *(char *)(intptr_t)(arg5 - 1) = a2;
            } while (arg5 != arg6);
        }

        if (arg9 == arg8 + 1) {
            return;
        }

        if (arg7 <= 0) {
            return;
        }

        arg8 += 1;
        arg9 += 1;
        arg10 = 0;
    }
}

int32_t resize(void *arg1, void *arg2, int32_t arg3, int32_t arg4, int32_t arg5)
{
    int32_t result = *(int32_t *)((char *)arg1 + 0x18);

    if (result != 0) {
        if (arg5 > 0) {
            if (arg4 - 0x10 <= 0) {
                sub_dadbc((int32_t)(intptr_t)arg1, arg2, arg3, arg4, arg3, arg3 + arg4, arg5, 0, 1, 0, 0);
                return result;
            }

            sub_dadbc((int32_t)(intptr_t)arg1, arg2, arg3, arg4, arg3, arg3 + arg4, arg5, 0, 1, 0, 0);
        }
    } else if (arg5 > 0) {
        if (arg4 <= 0) {
            return 0;
        }

        return sub_dad24((int32_t)(intptr_t)arg1, arg2, arg3, arg4, result, 0, arg5);
    }

    return result;
}

int32_t update_mhi(int32_t *arg1, void *arg2, int32_t arg3, void *arg4)
{
    int32_t v0 = arg1[2];
    int32_t t0 = arg1[4];

    if (t0 < v0 - 1) {
        resize(arg1, (void *)(intptr_t)arg3, *(int32_t *)((char *)(intptr_t)arg1[3] + (t0 << 2)), *arg1, arg1[1]);
        {
            int32_t a2_6 = arg1[5];
            int32_t v0_11 = arg1[4] + 1;

            arg1[4] = v0_11;
            arg1[8] = v0_11;
            memset(arg4, 0, (size_t)(a2_6 << 2));
            return 0;
        }
    }

    {
        int32_t v1_2 = arg1[8];

        if (v0 == 0) {
            __builtin_trap();
        }

        {
            int32_t a3_1 = t0 < arg1[7] ? 1 : 0;
            int32_t a2_1 = arg1[9] % v0;

            arg1[9] = a2_1;
            if (v0 == 0) {
                __builtin_trap();
            }

            {
                int32_t v1_3 = v1_2 % v0;
                int32_t result;

                arg1[8] = v1_3;
                if (a3_1 == 0) {
                    resize(arg1, (void *)(intptr_t)arg3, *(int32_t *)((char *)(intptr_t)arg1[3] + (v1_3 << 2)),
                        *arg1, arg1[1]);
                    {
                        int32_t v1_5 = arg1[4];

                        if (v1_5 >= arg1[7] + arg1[2] - 1) {
                            void *v1_7 = (void *)(intptr_t)arg1[3];
                            int32_t s3_1 = *(int32_t *)((char *)v1_7 + (arg1[9] << 2));
                            int32_t var_30[8];
                            int32_t var_48[8];

                            CreateImage(var_30, *(int32_t *)((char *)v1_7 + (arg1[8] << 2)), arg1[1], *arg1, 0);
                            CreateImage(var_48, s3_1, arg1[1], *arg1, 0);
                            move_detect(var_30[0], var_30[1], var_30[2], var_30[3], var_30[4], var_30[5], var_48[0],
                                var_48[1], arg4, (int32_t *)(intptr_t)arg1[0x10], arg1[6]);
                            arg1[7] = *(int32_t *)((char *)arg2 + 0xd8);
                            arg1[4] = arg1[2] - 1;
                            memcpy(arg4, (void *)(intptr_t)arg1[0xa], (size_t)(arg1[5] << 2));
                            a2_1 = arg1[9];
                            v1_3 = arg1[8];
                            result = 0;
                        } else {
                            arg1[4] = v1_5 + 1;
                            a2_1 = arg1[9];
                            v1_3 = arg1[8];
                            result = 1;
                        }
                    }
                } else {
                    arg1[4] = t0 + 1;
                    result = 1;
                }

                arg1[9] = a2_1 + 1;
                arg1[8] = v1_3 + 1;
                return result;
            }
        }
    }
}

MoveHandle *imp_alloc_move(uint32_t *arg1, int32_t arg2)
{
    int32_t v0 = c_clip3((int32_t)arg1[0x112], 0, 0x34);
    void *result_raw = calloc(1, 0x460);
    MoveHandle *result_1 = (MoveHandle *)result_raw;

    if (result_1 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x192,
            "imp_alloc_move", "calloc ivs move failed\n");
        return NULL;
    }

    {
        uint32_t *s2_1 = (uint32_t *)&result_1->param_storage[0];

        copy_u32_block(s2_1, arg1, 0x114);
        {
            int32_t var_450[0xd0 / 4];
            int32_t var_110[0x34];
            int32_t *var_34_1;
            int32_t *var_30_1;

            if (v0 <= 0) {
                var_34_1 = var_110;
                var_30_1 = var_450;
            } else {
                int32_t *s0_1 = var_450;
                int32_t *s1_1 = (int32_t *)&result_1->param_storage[0x108];
                int32_t *s3_1 = var_110;
                int32_t fp_1 = 0;

                var_30_1 = var_450;
                var_34_1 = var_110;
                do {
                    int32_t v0_3 = c_clip3(*s1_1, 0, (int32_t)arg1[0x38] - 1);
                    int32_t a2_5 = (int32_t)arg1[0x39];
                    int32_t a0_4 = s1_1[1];
                    int32_t v0_5;
                    int32_t a2_7;
                    int32_t a0_5;
                    int32_t v0_7;
                    int32_t a2_9;
                    int32_t a0_6;
                    int32_t a0_7;

                    *s0_1 = v0_3 >> 1;
                    v0_5 = c_clip3(a0_4, 0, a2_5 - 1);
                    a2_7 = (int32_t)arg1[0x38];
                    a0_5 = s1_1[2];
                    s0_1[1] = v0_5 >> 1;
                    v0_7 = c_clip3(a0_5, 0, a2_7 - 1);
                    a2_9 = (int32_t)arg1[0x39];
                    a0_6 = s1_1[3];
                    s0_1[2] = v0_7 >> 1;
                    a0_7 = *s2_1;
                    s0_1[3] = c_clip3(a0_6, 0, a2_9 - 1) >> 1;
                    *s3_1 = c_clip3(a0_7, 0, 4);
                    s1_1 = &s1_1[4];
                    s0_1 = &s0_1[4];
                    s2_1 = &s2_1[1];
                    imp_log_fun(3, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x19f,
                        "imp_alloc_move", "move->param.sense[%d]=%d\n", fp_1, s2_1[-1], &_gp);
                    fp_1 += 1;
                    s3_1 = &s3_1[1];
                } while (v0 != fp_1);
            }

            {
                MoveHandle *result = result_1;
                int32_t v1_1 = *(int32_t *)&result->param_storage[0xd8];
                int32_t a1_3 = *(int32_t *)&result->param_storage[0xec];
                int32_t a0_9 = *(int32_t *)&result->param_storage[0xe8];
                int32_t v0_16;

                result->state = 0;
                result->release_data = (int32_t (*)(void *))(intptr_t)arg2;
                v0_16 = (int32_t)(intptr_t)IVSMove_init((uint32_t)a0_9 >> 1, (uint32_t)a1_3 >> 1, var_34_1, v1_1, v0);
                result->ctx = (MoveContext *)(intptr_t)v0_16;
                if (v0_16 != 0) {
                    return result;
                }

                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x1ae,
                    "imp_alloc_move", "IVSMove_init failed\n");
                free(result_1);
            }
        }
    }

    return NULL;
}

void imp_free_move(void *arg1)
{
    if (arg1 == NULL) {
        return;
    }

    {
        MoveContext *s1 = *(MoveContext **)((char *)arg1 + 4);
        void *a0 = *(void **)((char *)s1 + 0x44);

        if (a0 != NULL) {
            free(a0);
            *(void **)((char *)s1 + 0x44) = NULL;
        }

        {
            int32_t a0_1 = *(int32_t *)((char *)s1 + 0x40);

            if (a0_1 != 0) {
                freeFilterEngine((void *)(intptr_t)a0_1);
            }
        }

        {
            int32_t a1 = *(int32_t *)((char *)s1 + 8);
            void *v1 = *(void **)((char *)s1 + 0xc);

            if (a1 > 0) {
                int32_t s0_1 = 0;
                int32_t s2_1 = 0;

                do {
                    void *v0_2 = *(void **)((char *)v1 + s2_1);

                    s0_1 += 1;
                    if (v0_2 != NULL) {
                        free(v0_2);
                        a1 = *(int32_t *)((char *)s1 + 8);
                        *(int32_t *)((char *)*(void **)((char *)s1 + 0xc) + s2_1) = 0;
                        v1 = *(void **)((char *)s1 + 0xc);
                    }

                    s2_1 = s0_1 << 2;
                } while (s0_1 < a1);
            }

            if (v1 != NULL) {
                free(v1);
                *(void **)((char *)s1 + 0xc) = NULL;
            }
        }

        {
            void *a0_4 = *(void **)((char *)s1 + 0x28);

            if (a0_4 != NULL) {
                free(a0_4);
            }
        }

        free(s1);
    }

    free(arg1);
}

int32_t imp_move_preprocess(void *arg1, void *arg2)
{
    void *var_10 = &_gp;

    (void)var_10;
    memcpy(*(void **)((char *)*(void **)((char *)arg1 + 4) + 0x44), *(void **)((char *)arg2 + 0x1c),
        (size_t)(*(int32_t *)((char *)arg2 + 8) * *(int32_t *)((char *)arg2 + 0xc)));
    return 1;
}

int32_t imp_move_process(void *arg1, void *arg2, int32_t arg3)
{
    const char *var_1c;
    int32_t v0_2;
    int32_t v1_1;

    if (arg1 == NULL) {
        v0_2 = IMP_Log_Get_Option();
        var_1c = "move is NULL !\n";
        v1_1 = 0x1f8;
    } else {
        int32_t (*t9_1)(void *) = *(int32_t (**)(void *))((char *)arg1 + 0x45c);

        if (t9_1 != NULL) {
            t9_1(*(void **)((char *)arg2 + 0x1c));
        }

        if (arg3 != 0) {
            int32_t *a0_1 = *(int32_t **)((char *)arg1 + 4);

            if (*(int32_t *)((char *)a0_1 + 0x18) == 0) {
                return update_mhi(a0_1, arg1, *(int32_t *)((char *)a0_1 + 0x44), (void *)(intptr_t)arg3);
            }
        }

        v0_2 = IMP_Log_Get_Option();
        var_1c = "result is NULL !\n";
        v1_1 = 0x201;
    }

    imp_log_fun(6, v0_2, 2, "IVS_MOVE",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", v1_1,
        "imp_move_process", var_1c, &_gp);
    return -1;
}

int32_t imp_get_move_param(void *arg1, uint32_t *arg2)
{
    uint32_t *i = (uint32_t *)((char *)arg1 + 8);

    if (arg1 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x215,
            "imp_get_move_param", "move is NULL\n", &_gp);
        return -1;
    }

    copy_u32_block(arg2, i, 0x114);
    return 0;
}

int32_t imp_set_move_param(void *arg1, uint32_t *arg2)
{
    if (arg1 != NULL && arg2 != NULL) {
        void *v0 = *(void **)((char *)arg1 + 4);

        return reset_param_isra_0((int32_t *)((char *)v0 + 0x14), (void **)((char *)v0 + 0x40), arg2,
            (int32_t *)&arg2[0x42], (int32_t)arg2[0x112], *(int32_t *)((char *)arg1 + 0xe8),
            *(int32_t *)((char *)arg1 + 0xec), (int32_t *)((char *)arg1 + 8));
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS_MOVE",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/ivs_move.c", 0x220,
        "imp_set_move_param", "move=%p, param=%p is NULL\n", &_gp);
    return -1;
}

int32_t imp_flush_frame(void *arg1)
{
    (void)arg1;
    return 0;
}
