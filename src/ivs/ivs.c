#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

#include "core/globals.h"
#include "imp/imp_ivs.h"

int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
void *alloc_device(const char *arg1, size_t arg2); /* forward decl, ported by T<N> later */
void free_device(void *arg1); /* forward decl, ported by T<N> later */
void *create_group(int32_t arg1, int32_t arg2, const char *arg3,
    int32_t (*arg4)(void *arg1, void *arg2)); /* forward decl, ported by T<N> later */
int32_t destroy_group(int32_t arg1, int32_t arg2); /* forward decl, ported by T<N> later */

extern int VBMUnlockFrameByVaddr_export(uint32_t vaddr) __asm__("VBMUnlockFrameByVaddr");
int VBMLockFrameByVaddr(uint32_t vaddr); /* forward decl, ported by T<N> later */

typedef struct IMPIVSInterfaceLayout {
    void *param;
    uint32_t param_size;
    void *reserved;
    int32_t (*init)(struct IMPIVSInterfaceLayout *handler);
    void (*exit)(struct IMPIVSInterfaceLayout *handler);
    int32_t (*preProcessSync)(struct IMPIVSInterfaceLayout *handler, void *frame);
    int32_t (*processAsync)(struct IMPIVSInterfaceLayout *handler, void *frame);
    int32_t (*getResult)(struct IMPIVSInterfaceLayout *handler);
    int32_t (*releaseResult)(struct IMPIVSInterfaceLayout *handler);
    int32_t (*getParam)(struct IMPIVSInterfaceLayout *handler);
    void (*updateParam)(struct IMPIVSInterfaceLayout *handler, void *param);
    void (*stop)(struct IMPIVSInterfaceLayout *handler);
} IMPIVSInterfaceLayout;

static char *ivs_channel_ptr(int32_t chn_num)
{
    return (char *)gIVS + chn_num * 0x48;
}

static int32_t *ivs_group_used_ptr(int32_t grp_num)
{
    return (int32_t *)((char *)gIVS + 0x1204 + grp_num * 4);
}

static void **ivs_device_group_slot(void *device_base, int32_t grp_num)
{
    return (void **)((char *)device_base + 0x28 + grp_num * 4);
}

static int VBMUnlockFrameByVaddr(uint32_t vaddr)
{
    return VBMUnlockFrameByVaddr_export(vaddr);
}

static void ivs_processing_cleanup_push_handle(void *arg1)
{
    int32_t result = *(int32_t *)((char *)arg1 + 0x40);

    if (result == 0) {
        return;
    }

    imp_log_fun(3, IMP_Log_Get_Option(), 2, "IVS",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x68,
        "ivs_processing_cleanup_push_handle",
        "%s(%d)\n", "ivs_processing_cleanup_push_handle", 0x68);
    VBMUnlockFrameByVaddr(*(uint32_t *)(*(char **)((char *)arg1 + 0x40) + 0x1c));
}

static void *ivs_processing(void *arg1)
{
    int32_t chn_num = (int32_t)(intptr_t)arg1;
    int32_t var_40 = -1;
    int32_t i_1 = 0;
    char var_98[0x89];
    char *chn = ivs_channel_ptr(chn_num);

    sprintf(var_98, "IVS(%d)-%s", chn_num, "ivs_processing");
    prctl(PR_SET_NAME, (unsigned long)var_98, 0UL, 0UL, 0UL);

    if (gIVS == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x7e,
            "ivs_processing", "ivs_create_group error !\n");
        return NULL;
    }

    while (1) {
        int32_t i;

        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        if (*(int32_t *)(chn + 0x44) == 0) {
            pthread_cleanup_push(ivs_processing_cleanup_push_handle, chn + 0x4);
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            sem_wait((sem_t *)(chn + 0x4));
            pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
            pthread_cleanup_pop(0);
        }

        while (1) {
            int32_t i_3;
            int32_t enable_before = *(int32_t *)(chn + 0x34);

            i_1 = 0;
            i = i_1;

            if (enable_before == 1) {
                do {
                    struct timespec var_58;
                    uint64_t deadline_ns;

                    clock_gettime(CLOCK_REALTIME, &var_58);
                    deadline_ns = (uint64_t)var_58.tv_nsec + 0x77359400ULL;
                    var_58.tv_sec += (time_t)(deadline_ns / 1000000000ULL);
                    var_58.tv_nsec = (long)(deadline_ns % 1000000000ULL);
                    i_3 = sem_timedwait((sem_t *)(chn + 0x4), &var_58);
                    i_1 = i_3;

                    if (i_3 < 0) {
                        int32_t err = errno;

                        var_40 = err;
                        imp_log_fun(5, IMP_Log_Get_Option(), 2, "IVS",
                            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x94,
                            "ivs_processing",
                            ":warn: sem_timedwait %d ret = %d, errnum = %d, enable = %d\n",
                            0x94, i_1, err, *(int32_t *)(chn + 0x34));
                        if (i_3 >= 0 || err == 0x91) {
                            i = i_1;
                        } else {
                            i = i_1;
                            if (err != 4 || *(int32_t *)(chn + 0x34) != enable_before) {
                                break;
                            }
                        }
                    } else {
                        i = i_1;
                    }

                    if (i_1 < 0) {
                        break;
                    }

                    if (*(uint32_t *)(*(char **)(chn + 0x44) + 0x1c) != 0) {
                        imp_log_fun(4, IMP_Log_Get_Option(), 2, "IVS",
                            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x9d,
                            "ivs_processing",
                            "%s(%d):unlock has post frame(pool_idx=%d,idx=%d),ret=%d\n",
                            "ivs_processing", 0x9d,
                            ((int32_t *)(*(void **)(chn + 0x44)))[1],
                            *(int32_t *)(*(void **)(chn + 0x44)),
                            i_1);
                        VBMUnlockFrameByVaddr(*(uint32_t *)(*(char **)(chn + 0x44) + 0x1c));
                    }
                } while (i != 0);
            }

            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

            if (*(int32_t *)(chn + 0x34) != 1) {
                imp_log_fun(5, IMP_Log_Get_Option(), 2, "IVS",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0xa4,
                    "ivs_processing",
                    "warn: %d ivs->chn[%d].enable = %d\n",
                    0xa4, chn_num, *(int32_t *)(chn + 0x34));
                *(void **)(chn + 0x44) = NULL;
                pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
                break;
            }

            {
                IMPIVSInterfaceLayout *handler = *(IMPIVSInterfaceLayout **)(chn + 0x38);

                if (handler->processAsync == NULL) {
                    VBMUnlockFrameByVaddr(*(uint32_t *)(*(char **)(chn + 0x44) + 0x1c));
                    i_1 = i_3;
                    break;
                }

                i_3 = handler->processAsync(handler, *(void **)(chn + 0x44));
                i_1 = i_3;

                if (i_3 < 0) {
                    break;
                }

                *(void **)(chn + 0x44) = NULL;
                pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

                if (i_1 == 0) {
                    sem_post((sem_t *)(chn + 0x24));
                }

                sem_post((sem_t *)(chn + 0x14));
                pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            }
        }

        if (*(int32_t *)(chn + 0x34) == 1) {
            if (i_1 < 0 && var_40 != 0x91) {
                if (var_40 != 4) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0xc2,
                        "ivs_processing",
                        "%s(%d): chnNum=%d ivs process failed\n",
                        "ivs_processing", 0xc2, chn_num);
                    return NULL;
                }
            } else {
                continue;
            }
        }

        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0xa8,
            "ivs_processing",
            "%s(%d):ivs chn[%d].enable=%d is stoped, ret=%d, errnum=%d\n",
            "ivs_processing", 0xa8, chn_num,
            *(int32_t *)(chn + 0x34), i_1, var_40);
        return NULL;
    }
}

static int32_t ivs_update(void *arg1, void *arg2)
{
    int32_t s0_1 = -1;
    int32_t s5_1 = 0;
    char *s4_1;

    if (gIVS == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0xea,
            "ivs_update", "ivs_create_group error !\n");
        return -1;
    }

    s4_1 = (char *)gIVS + 0x14;

    while (1) {
        int32_t var_48 = 0;

        if (*(int32_t *)(s4_1 + 0x2c) == *(int32_t *)((char *)arg1 + 0x8) &&
            *(int32_t *)(s4_1 + 0x20) == 1) {
            int32_t var_64;
            int32_t var_60;

            if (sem_getvalue((sem_t *)s4_1, &var_48) < 0) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0xfb,
                    "ivs_update",
                    "sem_getvalue(&ivs->chn[%d].sem_process_end) failed\n",
                    s5_1, var_64, var_60);
                return -1;
            }

            if (var_48 > 0) {
                int32_t s7_1 = *(int32_t *)(s4_1 + 0x20);

                while (1) {
                    const char *var_3c_1;
                    const char *s2_3;

                    if (s7_1 != 1) {
                        var_3c_1 = "AI_DisableAec";
                        s2_3 = "ivs_update";
                        goto label_log_warn;
                    }

                    {
                        struct timespec var_50;
                        uint64_t deadline_ns;
                        int32_t v0_9;

                        clock_gettime(CLOCK_REALTIME, &var_50);
                        deadline_ns = (uint64_t)var_50.tv_nsec + 0x77359400ULL;
                        var_50.tv_sec += (time_t)(deadline_ns / 1000000000ULL);
                        var_50.tv_nsec = (long)(deadline_ns % 1000000000ULL);
                        v0_9 = sem_timedwait((sem_t *)s4_1, &var_50);
                        if (v0_9 < 0) {
                            s2_3 = "ivs_update";
                            var_3c_1 = "AI_DisableAec";
                            s0_1 = errno;
                            var_60 = s0_1;
                            var_64 = v0_9;
                            imp_log_fun(5, IMP_Log_Get_Option(), 2, "IVS",
                                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x10a,
                                "ivs_update",
                                "warn: sem_timedwait %d ret = %d errnum = %d\n",
                                0x10a, var_64, var_60);
                            if (s0_1 != 0x91 && s0_1 != 4) {
                                goto label_log_warn;
                            }
                        } else if (v0_9 == 0) {
                            break;
                        }

                        if (*(int32_t *)(s4_1 + 0x20) == s7_1) {
                            s2_3 = "ivs_update";
                            var_3c_1 = "AI_DisableAec";
                            goto label_log_warn;
                        }
                    }
                }

                if (*(uint32_t *)(s4_1 + 0x28) != 0) {
                    IMPIVSInterfaceLayout *handler = *(IMPIVSInterfaceLayout **)(s4_1 + 0x24);

                    *(int32_t *)(s4_1 + 0x28) = 0;
                    if (handler->updateParam != NULL) {
                        handler->updateParam(handler, handler->param);
                        handler = *(IMPIVSInterfaceLayout **)(s4_1 + 0x24);
                    }

                    if (handler->preProcessSync != NULL &&
                        handler->preProcessSync(handler, arg2) < 0) {
                        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x121,
                            "ivs_update", "ivs chn[%d] PreprocessSync failed\n",
                            s5_1, var_64, var_60);
                    } else {
                        VBMLockFrameByVaddr(*(uint32_t *)((char *)arg2 + 0x1c));
                        *(void **)(s4_1 + 0x30) = arg2;
                        sem_post((sem_t *)(s4_1 - 0x10));
                    }
                } else {
                    const char *var_3c_1 = "AI_DisableAec";
                    const char *s2_3 = "ivs_update";

label_log_warn:
                    var_64 = s0_1;
                    imp_log_fun(5, IMP_Log_Get_Option(), 2, "IVS",
                        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x112,
                        "ivs_update", "warn: %d ivs->chn[%d].enable = %d\n",
                        s5_1, var_64, var_60);
                    (void)var_3c_1;
                    (void)s2_3;
                }
            }
        }

        s5_1 += 1;
        s4_1 += 0x48;

        if (s5_1 == 0x40) {
            break;
        }
    }

    {
        int32_t v0_14 = *(int32_t *)((char *)arg1 + 0xc);

        if (v0_14 > 0) {
            *(void **)((char *)arg1 + 0x10) = arg2;
            if (v0_14 != 1) {
                *(void **)((char *)arg1 + 0x14) = arg2;
                if (v0_14 != 2) {
                    *(void **)((char *)arg1 + 0x18) = arg2;
                }
            }
        }
    }

    return 0;
}

int32_t IVSInit(void)
{
    char *v0 = alloc_device("IVS", 0x1208);

    if (v0 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x48,
            "ivs_create_device", "alloc_device() error\n");
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x174,
            "IVSInit", "ivs_create_dev failed\n");
        return -1;
    }

    *(int32_t *)(v0 + 0x20) = 3;
    *(int32_t *)(v0 + 0x24) = 1;
    *(void **)(v0 + 0x40) = v0;
    *(int32_t *)(v0 + 0x1244) = 0;

    {
        int32_t *i = (int32_t *)(v0 + 0x74);

        do {
            *i = 0;
            i[1] = 0;
            i[3] = -1;
            i = &i[0x12];
        } while ((char *)i != v0 + 0x1274);
    }

    gIVS = (IVSState *)(v0 + 0x40);
    return 0;
}

int32_t IVSExit(void)
{
    if (gIVS != 0) {
        free_device(*(void **)gIVS);
        gIVS = 0;
    }

    return 0;
}

int IMP_IVS_CreateGroup(int grp_num)
{
    const char *var_3c_1;
    int32_t v0_6;
    int32_t v1_1;

    if ((uint32_t)grp_num >= 2) {
        v0_6 = IMP_Log_Get_Option();
        var_3c_1 = "GrpNum is error !\n";
        v1_1 = 0x1a2;
        imp_log_fun(6, v0_6, 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_1,
            "IMP_IVS_CreateGroup", var_3c_1);
        return -1;
    }

    if (gIVS != 0) {
        int32_t *s1_2 = ivs_group_used_ptr(grp_num);

        if (*s1_2 == 1) {
            imp_log_fun(2, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x1ad,
                "IMP_IVS_CreateGroup",
                "IMP_IVS_CreateGroup(%d) had been used !\n", grp_num);
        } else {
            char var_28[0x40];
            void *s2_1 = *(void **)gIVS;

            if (grp_num >= *(int32_t *)((char *)s2_1 + 0x24)) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x13f,
                    "ivs_create_group", "Invalid group num%d\n", grp_num);
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x1b1,
                    "IMP_IVS_CreateGroup", "ivs_create_group(%d) error !",
                    grp_num);
                return -1;
            }

            sprintf(var_28, "%s-%d", (char *)s2_1, grp_num);

            {
                void **v0_3 = create_group(*(int32_t *)((char *)s2_1 + 0x20),
                    grp_num, var_28, ivs_update);

                *v0_3 = s2_1;
                *ivs_device_group_slot(s2_1, grp_num) = v0_3;
                v0_3[3] = (void *)1;
            }
        }

        *s1_2 = 1;
        return 0;
    }

    v0_6 = IMP_Log_Get_Option();
    var_3c_1 = "IVS may not init\n";
    v1_1 = 0x1a8;
    imp_log_fun(6, v0_6, 2, "IVS",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_1,
        "IMP_IVS_CreateGroup", var_3c_1);
    return -1;
}

int IMP_IVS_DestroyGroup(int grp_num)
{
    const char *var_24_1;
    int32_t v0_4;
    int32_t v1_2;

    if ((uint32_t)grp_num >= 2) {
        v0_4 = IMP_Log_Get_Option();
        var_24_1 = "GrpNum is error !\n";
        v1_2 = 0x1c1;
        imp_log_fun(6, v0_4, 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_2,
            "IMP_IVS_DestroyGroup", var_24_1);
        return -1;
    }

    if (gIVS != 0) {
        int32_t *s2_1 = ivs_group_used_ptr(grp_num);
        int32_t a0 = *s2_1;

        if (a0 == 0) {
            imp_log_fun(2, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x1cc,
                "IMP_IVS_DestroyGroup",
                "IMP_IVS_CreateGroup(%d) had been not used !\n", grp_num);
            *s2_1 = 0;
        } else {
            void *a1 = *(void **)gIVS;

            if (grp_num >= *(int32_t *)((char *)a1 + 0x24)) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x159,
                    "ivs_destroy_group", "Invalid group num%d\n\n", grp_num);
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x1d0,
                    "IMP_IVS_DestroyGroup", "ivs_destroy_group(%d) error !",
                    grp_num);
                return -1;
            }

            {
                void **s1_1 = ivs_device_group_slot(a1, grp_num);
                int32_t a0_1 = (int32_t)(intptr_t)*s1_1;

                if (a0_1 == 0) {
                    imp_log_fun(5, IMP_Log_Get_Option(), 2, "IVS",
                        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x15f,
                        "ivs_destroy_group", "group-%d has not been created\n",
                        grp_num);
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x1d0,
                        "IMP_IVS_DestroyGroup", "ivs_destroy_group(%d) error !",
                        grp_num);
                    return -1;
                }

                destroy_group(a0_1, *(int32_t *)((char *)a1 + 0x20));
                *s1_1 = 0;
                *s2_1 = 0;
            }
        }

        return 0;
    }

    v0_4 = IMP_Log_Get_Option();
    var_24_1 = "IVS may not init\n";
    v1_2 = 0x1c7;
    imp_log_fun(6, v0_4, 2, "IVS",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_2,
        "IMP_IVS_DestroyGroup", var_24_1);
    return -1;
}

int IMP_IVS_CreateChn(int chn_num, IMPIVSInterface *handler)
{
    const char *var_3c_3;
    int32_t v0_16;
    int32_t v1_6;

    if ((uint32_t)chn_num >= 0x41) {
        v0_16 = IMP_Log_Get_Option();
        var_3c_3 = "ChnNum is error !\n";
        v1_6 = 0x1e0;
        imp_log_fun(6, v0_16, 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_6,
            "IMP_IVS_CreateChn", var_3c_3);
        return -1;
    }

    if (handler == NULL) {
        v0_16 = IMP_Log_Get_Option();
        var_3c_3 = "IMPIVSInterface is error, handler is null!\n";
        v1_6 = 0x1e5;
        imp_log_fun(6, v0_16, 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_6,
            "IMP_IVS_CreateChn", var_3c_3);
        return -1;
    }

    {
        IMPIVSInterfaceLayout *arg2 = (IMPIVSInterfaceLayout *)handler;

        if (arg2->param == NULL || arg2->param_size == 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x1ea,
                "IMP_IVS_CreateChn",
                "IMPIVSInterface is error, param(%p) not match paramSize(%d)\n",
                arg2->param, arg2->param_size);
            return -1;
        }

        if (arg2->preProcessSync == NULL && arg2->processAsync == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x1f0,
                "IMP_IVS_CreateChn",
                "IMPIVSInterface is error, preProcessSync(%p) and processAsync(%p) must be used at lest one\n",
                arg2->preProcessSync, arg2->processAsync);
            return -1;
        }

        if (gIVS == 0) {
            v0_16 = IMP_Log_Get_Option();
            var_3c_3 = "ivs_create_group error !\n";
            v1_6 = 0x1f6;
            imp_log_fun(6, v0_16, 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_6,
                "IMP_IVS_CreateChn", var_3c_3);
            return -1;
        }

        {
            char *fp_1 = ivs_channel_ptr(chn_num);

            if (*(void **)(fp_1 + 0x38) != NULL) {
                imp_log_fun(2, IMP_Log_Get_Option(), 2, "IVS",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x1fb,
                    "IMP_IVS_CreateChn",
                    "IMP_IVS_CreateChn(%d) has been used !\n", chn_num);
                return 0;
            }

            if (sem_init((sem_t *)(fp_1 + 0x4), 0, 0) < 0) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x200,
                    "IMP_IVS_CreateChn",
                    "sem_init ivs->chn[%d].sem_process_start failed\n", chn_num);
                return -1;
            }

            if (sem_init((sem_t *)(fp_1 + 0x14), 0, 1) < 0) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x205,
                    "IMP_IVS_CreateChn",
                    "sem_init ivs->chn[%d].sem_process_end failed\n", chn_num);
                sem_destroy((sem_t *)(fp_1 + 0x4));
                return -1;
            }

            if (sem_init((sem_t *)(fp_1 + 0x24), 0, 0) < 0) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x20a,
                    "IMP_IVS_CreateChn",
                    "sem_init ivs->chn[%d].sem_process_end failed\n", chn_num);
                sem_destroy((sem_t *)(fp_1 + 0x14));
                sem_destroy((sem_t *)(fp_1 + 0x4));
                return -1;
            }

            *(int32_t *)(fp_1 + 0x34) = 0;
            *(void **)(fp_1 + 0x38) = arg2;
            *(int32_t *)(fp_1 + 0x3c) = 0;

            if (arg2->init != NULL && arg2->init(arg2) < 0) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x217,
                    "IMP_IVS_CreateChn",
                    "ivs->chn[%d].handler->init failed\n", chn_num);
                *(int32_t *)(fp_1 + 0x34) = 0;
                *(void **)(fp_1 + 0x38) = NULL;
                *(int32_t *)(fp_1 + 0x3c) = 0;
                sem_destroy((sem_t *)(fp_1 + 0x24));
                sem_destroy((sem_t *)(fp_1 + 0x14));
                sem_destroy((sem_t *)(fp_1 + 0x4));
                return -1;
            }

            if (pthread_create((pthread_t *)(fp_1 + 0x48), NULL, ivs_processing,
                    (void *)(intptr_t)chn_num) >= 0) {
                return 0;
            }

            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x21d,
                "IMP_IVS_CreateChn", "pthread_create(%d) error !", chn_num);
            if (arg2->exit != NULL) {
                arg2->exit(arg2);
            }
            *(int32_t *)(fp_1 + 0x34) = 0;
            *(void **)(fp_1 + 0x38) = NULL;
            *(int32_t *)(fp_1 + 0x3c) = 0;
            sem_destroy((sem_t *)(fp_1 + 0x24));
            sem_destroy((sem_t *)(fp_1 + 0x14));
            sem_destroy((sem_t *)(fp_1 + 0x4));
            return -1;
        }
    }
}

int IMP_IVS_DestroyChn(int chn_num)
{
    const char *var_34_1;
    int32_t v0_6;
    int32_t v1_1;

    if ((uint32_t)chn_num >= 0x41) {
        v0_6 = IMP_Log_Get_Option();
        var_34_1 = "ChnNum is error !\n";
        v1_1 = 0x239;
        imp_log_fun(6, v0_6, 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_1,
            "IMP_IVS_DestroyChn", var_34_1);
        return -1;
    }

    if (gIVS == 0) {
        v0_6 = IMP_Log_Get_Option();
        var_34_1 = "ivs_create_group error !\n";
        v1_1 = 0x23f;
        imp_log_fun(6, v0_6, 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_1,
            "IMP_IVS_DestroyChn", var_34_1);
        return -1;
    }

    {
        char *s2_2 = ivs_channel_ptr(chn_num);

        if (*(void **)(s2_2 + 0x38) == NULL) {
            imp_log_fun(2, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x244,
                "IMP_IVS_DestroyChn",
                "IMP_IVS_DestroyChn(%d) has been not used !\n", chn_num);
            return 0;
        }

        pthread_cancel(*(pthread_t *)(s2_2 + 0x48));
        pthread_join(*(pthread_t *)(s2_2 + 0x48), NULL);

        {
            IMPIVSInterfaceLayout *a0_2 = *(IMPIVSInterfaceLayout **)(s2_2 + 0x38);

            if (a0_2->exit != NULL) {
                a0_2->exit(a0_2);
            }
        }

        *(int32_t *)(s2_2 + 0x48) = -1;
        *(int32_t *)(s2_2 + 0x34) = 0;
        *(void **)(s2_2 + 0x38) = NULL;
        *(void **)(s2_2 + 0x44) = NULL;
        sem_destroy((sem_t *)(s2_2 + 0x24));
        sem_destroy((sem_t *)(s2_2 + 0x14));
        sem_destroy((sem_t *)(s2_2 + 0x4));
        return 0;
    }
}

int IMP_IVS_RegisterChn(int grp_num, int chn_num)
{
    const char *var_34_1;
    int32_t v0_6;
    int32_t v1_2;

    if ((uint32_t)grp_num >= 2) {
        v0_6 = IMP_Log_Get_Option();
        var_34_1 = "GrpNum is error !\n";
        v1_2 = 0x25e;
    } else if ((uint32_t)chn_num >= 0x41) {
        v0_6 = IMP_Log_Get_Option();
        var_34_1 = "ChnNum is error !\n";
        v1_2 = 0x263;
    } else if (gIVS == 0) {
        v0_6 = IMP_Log_Get_Option();
        var_34_1 = "ivs_create_group error !\n";
        v1_2 = 0x269;
    } else {
        char *v0_3 = ivs_channel_ptr(chn_num);

        if (*(void **)(v0_3 + 0x38) == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x26e,
                "IMP_IVS_RegisterChn", "%s:not init IMP_IVS_CreateChn!\n",
                "IMP_IVS_RegisterChn");
            return -1;
        }

        if (*(int32_t *)(v0_3 + 0x40) != -1) {
            imp_log_fun(2, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x273,
                "IMP_IVS_RegisterChn",
                "IMP_IVS_RegisterChn(%d,%d) has been used !\n",
                grp_num, chn_num);
        }

        *(int32_t *)(v0_3 + 0x40) = grp_num;
        return 0;
    }

    imp_log_fun(6, v0_6, 2, "IVS",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_2,
        "IMP_IVS_RegisterChn", var_34_1);
    return -1;
}

int IMP_IVS_UnRegisterChn(int chn_num)
{
    const char *var_2c_1;
    int32_t v0_5;
    int32_t v1_2;

    if ((uint32_t)chn_num >= 0x41) {
        v0_5 = IMP_Log_Get_Option();
        var_2c_1 = "ChnNum is error !\n";
        v1_2 = 0x27f;
    } else if (gIVS == 0) {
        v0_5 = IMP_Log_Get_Option();
        var_2c_1 = "ivs_create_group error !\n";
        v1_2 = 0x285;
    } else {
        char *v0_2 = ivs_channel_ptr(chn_num);

        if (*(void **)(v0_2 + 0x38) == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x28a,
                "IMP_IVS_UnRegisterChn", "%s:not init IMP_IVS_CreateChn!\n",
                "IMP_IVS_UnRegisterChn");
            return -1;
        }

        if (*(int32_t *)(v0_2 + 0x40) == -1) {
            imp_log_fun(2, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x28f,
                "IMP_IVS_UnRegisterChn",
                "IMP_IVS_UnRegisterChn(%d) has been not used !\n", chn_num);
        }

        *(int32_t *)(v0_2 + 0x40) = -1;
        return 0;
    }

    imp_log_fun(6, v0_5, 2, "IVS",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_2,
        "IMP_IVS_UnRegisterChn", var_2c_1);
    return -1;
}

int IMP_IVS_StartRecvPic(int chn_num)
{
    const char *var_2c_1;
    int32_t v0_5;
    int32_t v1_2;

    if ((uint32_t)chn_num >= 0x41) {
        v0_5 = IMP_Log_Get_Option();
        var_2c_1 = "ChnNum is error !\n";
        v1_2 = 0x29b;
    } else if (gIVS == 0) {
        v0_5 = IMP_Log_Get_Option();
        var_2c_1 = "ivs_create_group error !\n";
        v1_2 = 0x2a1;
    } else {
        char *v0_2 = ivs_channel_ptr(chn_num);

        if (*(void **)(v0_2 + 0x38) == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x2a6,
                "IMP_IVS_StartRecvPic", "%s:not init IMP_IVS_CreateChn!\n",
                "IMP_IVS_StartRecvPic");
            return -1;
        }

        if (*(int32_t *)(v0_2 + 0x34) == 1) {
            imp_log_fun(2, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x2ab,
                "IMP_IVS_StartRecvPic",
                "IMP_IVS_StartRecvPic(%d) has been used !\n", chn_num);
        }

        *(int32_t *)(v0_2 + 0x34) = 1;
        return 0;
    }

    imp_log_fun(6, v0_5, 2, "IVS",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_2,
        "IMP_IVS_StartRecvPic", var_2c_1);
    return -1;
}

int IMP_IVS_StopRecvPic(int chn_num)
{
    int32_t var_30 = 0;
    const char *var_44_1;
    int32_t v0_15;
    int32_t v1_1;

    if ((uint32_t)chn_num >= 0x41) {
        v0_15 = IMP_Log_Get_Option();
        var_44_1 = "ChnNum is error !\n";
        v1_1 = 0x2b8;
    } else if (gIVS == 0) {
        v0_15 = IMP_Log_Get_Option();
        var_44_1 = "ivs_create_group error !\n";
        v1_1 = 0x2be;
    } else {
        char *s7_1 = ivs_channel_ptr(chn_num);

        if (*(void **)(s7_1 + 0x38) == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x2c3,
                "IMP_IVS_StopRecvPic", "%s:not init IMP_IVS_CreateChn!\n",
                "IMP_IVS_StopRecvPic");
            return -1;
        }

        if (*(int32_t *)(s7_1 + 0x34) == 0) {
            imp_log_fun(2, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x2c8,
                "IMP_IVS_StopRecvPic",
                "IMP_IVS_StopRecvPic(%d) has been not used !\n", chn_num);
            return 0;
        }

        *(int32_t *)(s7_1 + 0x34) = 0;

        {
            int32_t s0_1 = 0;

            while (1) {
                sem_getvalue((sem_t *)(s7_1 + 0x14), &var_30);
                if (var_30 <= 0) {
                    IMPIVSInterfaceLayout *a0_1 = *(IMPIVSInterfaceLayout **)(s7_1 + 0x38);

                    s0_1 += 1;
                    if (a0_1->stop != NULL) {
                        a0_1->stop(a0_1);
                        return 0;
                    }

                    if (s0_1 != 0x64) {
                        return 0;
                    }

                    break;
                }

                s0_1 += 1;
                if ((*(void (**)(IMPIVSInterfaceLayout *))( *(char **)(s7_1 + 0x38) + 0x2c) != NULL) &&
                    usleep(0x2710) == 0) {
                    if (s0_1 != 0x64) {
                        continue;
                    }
                }

                break;
            }
        }

        imp_log_fun(5, IMP_Log_Get_Option(), 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x2d6,
            "IMP_IVS_StopRecvPic",
            "IMP_IVS_StopRecvPic(%d) hasn't sync the sem_process_end signal, force stoped\n",
            chn_num);
        return 0;
    }

    imp_log_fun(6, v0_15, 2, "IVS",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_1,
        "IMP_IVS_StopRecvPic", var_44_1);
    return -1;
}

int IMP_IVS_PollingResult(int chn_num, int timeout_ms)
{
    const char *var_44_1;
    int32_t v0_16;
    int32_t v1_3;

    if ((uint32_t)chn_num >= 0x41) {
        v0_16 = IMP_Log_Get_Option();
        var_44_1 = "ChnNum is error !\n";
        v1_3 = 0x2e3;
        imp_log_fun(6, v0_16, 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_3,
            "IMP_IVS_PollingResult", var_44_1);
        return -1;
    }

    if (gIVS == 0) {
        v0_16 = IMP_Log_Get_Option();
        var_44_1 = "ivs_create_group error !\n";
        v1_3 = 0x2e9;
        imp_log_fun(6, v0_16, 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_3,
            "IMP_IVS_PollingResult", var_44_1);
        return -1;
    }

    {
        char *chn = ivs_channel_ptr(chn_num);
        int32_t result;

        if (*(void **)(chn + 0x38) == NULL) {
            result = -1;
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x2ee,
                "IMP_IVS_PollingResult", "%s:not init IMP_IVS_CreateChn!\n",
                "IMP_IVS_PollingResult");
            return result;
        }

        if (timeout_ms == 0) {
            return sem_trywait((sem_t *)(chn + 0x24));
        }

        if (timeout_ms < 0) {
            struct timespec var_28;

            var_28.tv_sec = time(NULL) + 0xa;
            var_28.tv_nsec = 0;
            return sem_timedwait((sem_t *)(chn + 0x24), &var_28);
        }

        {
            struct timespec var_28;
            uint64_t timeout_ns;
            int32_t result_1;

            clock_gettime(CLOCK_REALTIME, &var_28);
            timeout_ns = (uint64_t)var_28.tv_nsec + (uint64_t)timeout_ms * 1000000ULL;
            var_28.tv_sec += (time_t)(timeout_ns / 1000000000ULL);
            var_28.tv_nsec = (long)(timeout_ns % 1000000000ULL);
            result_1 = sem_timedwait((sem_t *)(chn + 0x24), &var_28);
            result = result_1;
            if (result_1 < 0) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                    "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x301,
                    "IMP_IVS_PollingResult",
                    "%s(%d):sem_timedwait() error [%s], ms=%d!\n",
                    "IMP_IVS_PollingResult", 0x301, strerror(errno), timeout_ms);
            }
            return result;
        }
    }
}

int IMP_IVS_GetResult(int chn_num, void **result)
{
    const char *var_1c_2;
    int32_t v0_5;
    int32_t v1_3;

    (void)result;

    if ((uint32_t)chn_num >= 0x41) {
        v0_5 = IMP_Log_Get_Option();
        var_1c_2 = "ChnNum is error !\n";
        v1_3 = 0x30c;
    } else if (gIVS == 0) {
        v0_5 = IMP_Log_Get_Option();
        var_1c_2 = "ivs_create_group error !\n";
        v1_3 = 0x312;
    } else {
        IMPIVSInterfaceLayout *s0_1 = *(IMPIVSInterfaceLayout **)(ivs_channel_ptr(chn_num) + 0x38);

        if (s0_1 == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x317,
                "IMP_IVS_GetResult", "%s:not init IMP_IVS_CreateChn!\n",
                "IMP_IVS_GetResult");
            return -1;
        }

        if (s0_1->getResult != NULL && s0_1->getResult(s0_1) >= 0) {
            return 0;
        }

        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x31e,
            "IMP_IVS_GetResult", "handler->getResult(%p) failed!\n",
            s0_1->getResult);
        return -1;
    }

    imp_log_fun(6, v0_5, 2, "IVS",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_3,
        "IMP_IVS_GetResult", var_1c_2);
    return -1;
}

int IMP_IVS_ReleaseResult(int chn_num, void *result)
{
    const char *var_1c_2;
    int32_t v0_5;
    int32_t v1_3;

    (void)result;

    if ((uint32_t)chn_num >= 0x41) {
        v0_5 = IMP_Log_Get_Option();
        var_1c_2 = "ChnNum is error !\n";
        v1_3 = 0x32a;
    } else if (gIVS == 0) {
        v0_5 = IMP_Log_Get_Option();
        var_1c_2 = "ivs_create_group error !\n";
        v1_3 = 0x330;
    } else {
        IMPIVSInterfaceLayout *s0_1 = *(IMPIVSInterfaceLayout **)(ivs_channel_ptr(chn_num) + 0x38);

        if (s0_1 == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x335,
                "IMP_IVS_ReleaseResult", "%s:not init IMP_IVS_CreateChn!\n",
                "IMP_IVS_ReleaseResult");
            return -1;
        }

        if (s0_1->releaseResult != NULL && s0_1->releaseResult(s0_1) >= 0) {
            return 0;
        }

        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x33c,
            "IMP_IVS_ReleaseResult", "handler->releaseResult(%p) failed!\n",
            s0_1->releaseResult);
        return -1;
    }

    imp_log_fun(6, v0_5, 2, "IVS",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_3,
        "IMP_IVS_ReleaseResult", var_1c_2);
    return -1;
}

int IMP_IVS_GetParam(int chn_num, void *param)
{
    if (param == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x34d,
            "IMP_IVS_GetParam", "param is NULL!\n");
    } else if ((uint32_t)chn_num >= 0x41) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x352,
            "IMP_IVS_GetParam", "ChnNum is error !\n");
    } else if (gIVS == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x358,
            "IMP_IVS_GetParam", "ivs_create_group error !\n");
    } else {
        IMPIVSInterfaceLayout *a0_2 = *(IMPIVSInterfaceLayout **)(ivs_channel_ptr(chn_num) + 0x38);

        if (a0_2 == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x35d,
                "IMP_IVS_GetParam", "%s:not init IMP_IVS_CreateChn!\n",
                "IMP_IVS_GetParam");
        } else if (a0_2->getParam != NULL) {
            return a0_2->getParam(a0_2);
        }
    }

    return -1;
}

int IMP_IVS_SetParam(int chn_num, void *param)
{
    const char *var_1c_1;
    int32_t v0_2;
    int32_t v1_1;

    if (param == NULL) {
        v0_2 = IMP_Log_Get_Option();
        var_1c_1 = "param is NULL!\n";
        v1_1 = 0x36b;
    } else if ((uint32_t)chn_num >= 0x41) {
        v0_2 = IMP_Log_Get_Option();
        var_1c_1 = "ChnNum is error !\n";
        v1_1 = 0x370;
    } else if (gIVS != 0) {
        char *s0_2 = ivs_channel_ptr(chn_num);
        IMPIVSInterfaceLayout *v0_1 = *(IMPIVSInterfaceLayout **)(s0_2 + 0x38);

        if (v0_1 == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVS",
                "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", 0x37b,
                "IMP_IVS_SetParam", "%s:not init IMP_IVS_CreateChn!\n",
                "IMP_IVS_SetParam");
            return -1;
        }

        if (v0_1->param == NULL) {
            return 0;
        }

        {
            size_t n = v0_1->param_size;

            if (n == 0) {
                return (int)n;
            }

            memcpy(v0_1->param, param, n);
            *(int32_t *)(s0_2 + 0x3c) = 1;
            return 0;
        }
    } else {
        v0_2 = IMP_Log_Get_Option();
        var_1c_1 = "ivs_create_group error !\n";
        v1_1 = 0x376;
    }

    imp_log_fun(6, v0_2, 2, "IVS",
        "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs.c", v1_1,
        "IMP_IVS_SetParam", var_1c_1);
    return -1;
}
