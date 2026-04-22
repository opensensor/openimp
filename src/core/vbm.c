#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/imp_alloc.h"

extern char _gp;
typedef struct VbmInstance VbmInstance;
extern VbmInstance *g_pVbmInstance;

int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
int32_t IMP_Alloc(IMP_Alloc_Info *arg1, int32_t arg2, char *arg3); /* forward decl, ported by T<N> later */
int32_t IMP_Free(IMP_Alloc_Info *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t IMP_PoolAlloc(int32_t arg1, IMP_Alloc_Info *arg2, int32_t arg3, char *arg4); /* forward decl, ported by T<N> later */
int32_t IMP_PoolFree(int32_t arg1, IMP_Alloc_Info *arg2, int32_t arg3); /* forward decl, ported by T<N> later */
int32_t IMP_FrameSource_GetPool(int32_t chn); /* forward decl, ported by T<N> later */
int32_t IMP_ISP_Tuning_GetAeZone(void *zone); /* forward decl, ported by T<N> later */
void _setLeftPart32(uint32_t leftpart); /* forward decl, ported by T<N> later */
uint32_t _setRightPart32(uint32_t rightpart); /* forward decl, ported by T<N> later */

typedef struct VBMFrame {
    int32_t index;          /* 0x000 */
    int32_t pool_idx;       /* 0x004 */
    int32_t width;          /* 0x008 */
    int32_t height;         /* 0x00c */
    int32_t pixfmt;         /* 0x010 */
    int32_t size;           /* 0x014 */
    uint32_t phys_addr;     /* 0x018 */
    uint32_t virt_addr;     /* 0x01c */
    uint8_t pad20[0x3c4];   /* 0x020 */
    uint8_t type;           /* 0x3e4 */
    uint8_t pad3e5;         /* 0x3e5 */
    uint16_t field_3e6;     /* 0x3e6 */
    int32_t field_3e8;      /* 0x3e8 */
    int32_t field_3ec;      /* 0x3ec */
    int32_t field_3f0;      /* 0x3f0 */
    uint8_t pad3f4[0x34];   /* 0x3f4 */
} VBMFrame;

typedef struct VBMFrameVolume {
    VBMFrame *frame;            /* 0x00 */
    uint32_t virt_addr;         /* 0x04 */
    uint32_t phys_addr;         /* 0x08 */
    int32_t ref_count;          /* 0x0c */
    uint8_t mutex_storage[0x18];/* 0x10 */
} VBMFrameVolume;

static void *vbm_instance[6];
static VBMFrameVolume g_framevolumes[0x1e];
static VBMFrame frame_type2;
static uint8_t print_cnt_vtv = 1;
static const char data_edab0[] = "vbm";

static inline pthread_mutex_t *vbm_volume_mutex(VBMFrameVolume *volume)
{
    return (pthread_mutex_t *)(void *)volume->mutex_storage;
}

static void vbm_assert_trap(void)
{
    volatile int *ptr = (volatile int *)(uintptr_t)0xc;

    IMP_Log_Get_Option();
    (void)*ptr;
    __builtin_trap();
}

void *VBMGetInstance(void)
{
    if (g_pVbmInstance == 0) {
        g_pVbmInstance = (VbmInstance *)vbm_instance;
    }

    return &vbm_instance;
}

int32_t VBMLockFrame(VBMFrame *arg1)
{
    int32_t i = 0;
    VBMFrame **v1 = &g_framevolumes[0].frame;

    do {
        VBMFrame *a1_1 = *v1;

        v1 = (VBMFrame **)((char *)v1 + 0x28);
        if (arg1 == a1_1) {
            int32_t s2_1 = i * 0x28;
            pthread_mutex_t *s1_1 = (pthread_mutex_t *)(void *)((char *)g_framevolumes + s2_1 + 0x10);
            VBMFrameVolume *s0 = (VBMFrameVolume *)((char *)g_framevolumes + s2_1);

            pthread_mutex_lock(s1_1);
            s0->ref_count += 1;
            pthread_mutex_unlock(s1_1);
            return 0;
        }

        i += 1;
    } while (i != 0x1e);

    return -1;
}

int32_t VBMUnLockFrame(VBMFrame *arg1)
{
    uint32_t t0 = (uint32_t)arg1->type;
    int32_t v1 = 0;
    VBMFrame **v0 = &g_framevolumes[0].frame;
    int32_t result;

    while (1) {
        VBMFrame *a1_1 = *v0;

        v0 = (VBMFrame **)((char *)v0 + 0x28);
        if (arg1 == a1_1) {
            result = 0;
            if (t0 == 2) {
                break;
            }

            {
                int32_t v1_2 = v1 * 0x28;
                pthread_mutex_t *s1_2 = (pthread_mutex_t *)(void *)((char *)g_framevolumes + v1_2 + 0x10);
                VBMFrameVolume *s0_1 = (VBMFrameVolume *)((char *)g_framevolumes + v1_2);

                pthread_mutex_lock(s1_2);
                if (s0_1->ref_count == 0) {
                    int32_t v0_10 = IMP_Log_Get_Option();
                    VBMFrame *v1_7 = s0_1->frame;

                    imp_log_fun(6, v0_10, 2, data_edab0,
                        "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0xa1, "vbm_unlock_vol",
                        "%s(%d):fvol->ref=%d, fvol->frame->pool_idx=%d, fvol->frame->index=%d\n",
                        "vbm_unlock_vol", 0xa1, s0_1->ref_count, v1_7->pool_idx, v1_7->index);
                    pthread_mutex_unlock(s1_2);
                    return -1;
                }

                s0_1->ref_count -= 1;
                if (s0_1->ref_count == 0) {
                    void **v0_6 = (void **)((char *)VBMGetInstance() + ((s0_1->frame->pool_idx) << 2));
                    VBMFrame *a0_2 = s0_1->frame;
                    void *pool = *v0_6;

                    if (((int32_t (*)(VBMFrame *, void *))*(void **)((char *)pool + 0x178))(a0_2, *(void **)((char *)pool + 4)) < 0) {
                        int32_t v0_8 = IMP_Log_Get_Option();
                        VBMFrame *v1_5 = s0_1->frame;

                        imp_log_fun(6, v0_8, 2, data_edab0,
                            "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0xb1, "vbm_unlock_vol",
                            "%s:releaseFrame failed:frame->pool_idx=%d, frame->index=%d\n",
                            "vbm_unlock_vol", v1_5->pool_idx, v1_5->index);
                    }
                }

                pthread_mutex_unlock(s1_2);
                return 0;
            }
        }

        v1 += 1;
        if (v1 == 0x1e) {
            result = 0;
            if (t0 == 2) {
                break;
            }

            imp_log_fun(6, IMP_Log_Get_Option(), 2, data_edab0,
                "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0xcb, "VBMUnLockFrame",
                "fvol is NULL, frame->pool_id=%d, frame->index=%d\n", arg1->pool_idx, arg1->index);
            return -1;
        }
    }

    return result;
}

int32_t VBMLockFrameByVaddr(uint32_t arg1)
{
    int32_t i = 0;
    uint32_t *v1 = &g_framevolumes[0].virt_addr;

    do {
        uint32_t a1_1 = *v1;

        v1 = (uint32_t *)((char *)v1 + 0x28);
        if (arg1 == a1_1) {
            int32_t s2_1 = i * 0x28;
            pthread_mutex_t *s1_1 = (pthread_mutex_t *)(void *)((char *)g_framevolumes + s2_1 + 0x10);
            VBMFrameVolume *s0_1 = (VBMFrameVolume *)((char *)g_framevolumes + s2_1);

            pthread_mutex_lock(s1_1);
            s0_1->ref_count += 1;
            pthread_mutex_unlock(s1_1);
            return 0;
        }

        i += 1;
    } while (i != 0x1e);

    {
        uint32_t print_cnt_vtv_1 = (uint32_t)print_cnt_vtv;

        if (print_cnt_vtv_1 != 0) {
            VBMFrameVolume *fp_1 = g_framevolumes;
            int32_t i_1 = 0;

            print_cnt_vtv = (uint8_t)(print_cnt_vtv_1 - 1);
            while (i_1 != 0x1e) {
                fp_1 = (VBMFrameVolume *)((char *)fp_1 + 0x28);
                imp_log_fun(4, IMP_Log_Get_Option(), 2, data_edab0,
                    "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x70, "vaddr_to_vol",
                    "fval[%d].frame=%p\n", i_1, *(VBMFrame **)((char *)fp_1 - 0x28), &_gp);
                imp_log_fun(4, IMP_Log_Get_Option(), 2, data_edab0,
                    "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x71, "vaddr_to_vol",
                    "fval[%d].vaddr=0x%x\n", i_1, *(uint32_t *)((char *)fp_1 - 0x24));
                imp_log_fun(4, IMP_Log_Get_Option(), 2, data_edab0,
                    "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x72, "vaddr_to_vol",
                    "fval[%d].paddr=0x%x\n", i_1, *(uint32_t *)((char *)fp_1 - 0x20));
                imp_log_fun(4, IMP_Log_Get_Option(), 2, data_edab0,
                    "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x73, "vaddr_to_vol",
                    "fval[%d].ref=%d\n", i_1, *(int32_t *)((char *)fp_1 - 0x1c));
                i_1 += 1;
            }
        }
    }

    return -1;
}

int32_t VBMUnlockFrameByVaddr(uint32_t arg1)
{
    int32_t v0 = 0;
    uint32_t *v1 = &g_framevolumes[0].virt_addr;

    while (1) {
        uint32_t a0 = *v1;

        v1 = (uint32_t *)((char *)v1 + 0x28);
        if (arg1 == a0) {
            break;
        }

        v0 += 1;
        if (v0 == 0x1e) {
            uint32_t print_cnt_vtv_1 = (uint32_t)print_cnt_vtv;
            int32_t var_4c = 0;

            if (print_cnt_vtv_1 == 0) {
                imp_log_fun(2, IMP_Log_Get_Option(), 2, data_edab0,
                    "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0xde, "VBMUnlockFrameByVaddr",
                    "vaddr=%x failed to get fvol\n", arg1, var_4c);
                return -1;
            }

            {
                VBMFrameVolume *s5_1 = g_framevolumes;
                int32_t i = 0;

                print_cnt_vtv = (uint8_t)(print_cnt_vtv_1 - 1);
                while (i != 0x1e) {
                    s5_1 = (VBMFrameVolume *)((char *)s5_1 + 0x28);
                    imp_log_fun(4, IMP_Log_Get_Option(), 2, data_edab0,
                        "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x70, "vaddr_to_vol",
                        "fval[%d].frame=%p\n", i, *(VBMFrame **)((char *)s5_1 - 0x28));
                    imp_log_fun(4, IMP_Log_Get_Option(), 2, data_edab0,
                        "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x71, "vaddr_to_vol",
                        "fval[%d].vaddr=0x%x\n", i, *(uint32_t *)((char *)s5_1 - 0x24));
                    imp_log_fun(4, IMP_Log_Get_Option(), 2, data_edab0,
                        "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x72, "vaddr_to_vol",
                        "fval[%d].paddr=0x%x\n", i, *(uint32_t *)((char *)s5_1 - 0x20));
                    var_4c = *(int32_t *)((char *)s5_1 - 0x1c);
                    imp_log_fun(4, IMP_Log_Get_Option(), 2, data_edab0,
                        "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x73, "vaddr_to_vol",
                        "fval[%d].ref=%d\n", i, var_4c);
                    i += 1;
                }
            }

            imp_log_fun(2, IMP_Log_Get_Option(), 2, data_edab0,
                "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0xde, "VBMUnlockFrameByVaddr",
                "vaddr=%x failed to get fvol\n", arg1, var_4c);
            return -1;
        }
    }

    {
        int32_t v0_2 = v0 * 0x28;
        pthread_mutex_t *s1_1 = (pthread_mutex_t *)(void *)((char *)g_framevolumes + v0_2 + 0x10);
        VBMFrameVolume *s0_1 = (VBMFrameVolume *)((char *)g_framevolumes + v0_2);

        pthread_mutex_lock(s1_1);
        if (s0_1->ref_count == 0) {
            int32_t v0_18 = IMP_Log_Get_Option();
            VBMFrame *v1_5 = s0_1->frame;

            imp_log_fun(6, v0_18, 2, data_edab0,
                "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0xa1, "vbm_unlock_vol",
                "%s(%d):fvol->ref=%d, fvol->frame->pool_idx=%d, fvol->frame->index=%d\n",
                "vbm_unlock_vol", 0xa1, s0_1->ref_count, v1_5->pool_idx, v1_5->index);
            pthread_mutex_unlock(s1_1);
            return -1;
        }

        s0_1->ref_count -= 1;
        if (s0_1->ref_count == 0) {
            void *a0_3 = s0_1->frame;
            void *v0_8 = *(void **)((char *)VBMGetInstance() + ((*(int32_t *)((char *)a0_3 + 4)) << 2));

            if (((int32_t (*)(void *, void *))*(void **)((char *)v0_8 + 0x178))(a0_3, *(void **)((char *)v0_8 + 4)) < 0) {
                int32_t v0_10 = IMP_Log_Get_Option();
                VBMFrame *v1_4 = s0_1->frame;

                imp_log_fun(6, v0_10, 2, data_edab0,
                    "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0xb1, "vbm_unlock_vol",
                    "%s:releaseFrame failed:frame->pool_idx=%d, frame->index=%d\n",
                    "vbm_unlock_vol", v1_4->pool_idx, v1_4->index);
            }
        }

        pthread_mutex_unlock(s1_1);
        return 0;
    }
}

int32_t VBMLockFrameByPaddr(uint32_t arg1)
{
    int32_t i = 0;
    uint32_t *v1 = &g_framevolumes[0].phys_addr;

    do {
        uint32_t a1_1 = *v1;

        v1 = (uint32_t *)((char *)v1 + 0x28);
        if (arg1 == a1_1) {
            int32_t s2_1 = i * 0x28;
            pthread_mutex_t *s1_1 = (pthread_mutex_t *)(void *)((char *)g_framevolumes + s2_1 + 0x10);
            VBMFrameVolume *s0 = (VBMFrameVolume *)((char *)g_framevolumes + s2_1);

            pthread_mutex_lock(s1_1);
            s0->ref_count += 1;
            pthread_mutex_unlock(s1_1);
            return 0;
        }

        i += 1;
    } while (i != 0x1e);

    return -1;
}

int32_t VBMUnlockFrameByPaddr(uint32_t arg1)
{
    int32_t i = 0;
    uint32_t *v1 = &g_framevolumes[0].phys_addr;

    do {
        uint32_t a1_1 = *v1;

        v1 = (uint32_t *)((char *)v1 + 0x28);
        if (arg1 == a1_1) {
            int32_t v0_1 = i * 0x28;
            pthread_mutex_t *s1_2 = (pthread_mutex_t *)(void *)((char *)g_framevolumes + v0_1 + 0x10);
            VBMFrameVolume *s0_1 = (VBMFrameVolume *)((char *)g_framevolumes + v0_1);

            pthread_mutex_lock(s1_2);
            if (s0_1->ref_count == 0) {
                int32_t v0_10 = IMP_Log_Get_Option();
                VBMFrame *v1_5 = s0_1->frame;

                imp_log_fun(6, v0_10, 2, data_edab0,
                    "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0xa1, "vbm_unlock_vol",
                    "%s(%d):fvol->ref=%d, fvol->frame->pool_idx=%d, fvol->frame->index=%d\n",
                    "vbm_unlock_vol", 0xa1, s0_1->ref_count, v1_5->pool_idx, v1_5->index);
                pthread_mutex_unlock(s1_2);
                break;
            }

            s0_1->ref_count -= 1;
            if (s0_1->ref_count == 0) {
                void *a0_2 = s0_1->frame;
                void *v0_7 = *(void **)((char *)VBMGetInstance() + ((*(int32_t *)((char *)a0_2 + 4)) << 2));

                if (((int32_t (*)(void *, void *))*(void **)((char *)v0_7 + 0x178))(a0_2, *(void **)((char *)v0_7 + 4)) < 0) {
                    int32_t v0_9 = IMP_Log_Get_Option();
                    VBMFrame *v1_4 = s0_1->frame;

                    imp_log_fun(6, v0_9, 2, data_edab0,
                        "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0xb1, "vbm_unlock_vol",
                        "%s:releaseFrame failed:frame->pool_idx=%d, frame->index=%d\n",
                        "vbm_unlock_vol", v1_4->pool_idx, v1_4->index);
                }
            }

            pthread_mutex_unlock(s1_2);
            return 0;
        }

        i += 1;
    } while (i != 0x1e);

    return -1;
}

int32_t VBMCreatePool(int32_t arg1, int32_t *arg2, int32_t *arg3, int32_t arg4)
{
    int32_t s0_1;
    int32_t s7_6;
    int32_t s6_2;
    int32_t v1_8;
    int32_t v0_16;
    char *str;

    if (arg1 >= 6) {
        return -1;
    }

    VBMGetInstance();
    str = (char *)malloc((size_t)(arg2[0x33] * 0x428 + 0x180));
    if (str == 0) {
        return -1;
    }

    if (arg3 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, data_edab0,
            "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x129, "VBMCreatePool",
            "%s(): Invalid interface\n", "VBMCreatePool");
    }

    memset(str, 0, (size_t)(arg2[0x33] * 0x428 + 0x180));
    *(int32_t *)(str + 4) = arg4;
    *(int32_t *)str = arg1;

    {
        int32_t *v1_1 = (int32_t *)(void *)(str + 8);
        int32_t *i = arg2;

        do {
            int32_t t0_1 = i[0];
            int32_t a3 = i[1];
            int32_t a2_1 = i[2];
            int32_t a1 = i[3];

            i = &i[4];
            v1_1[0] = t0_1;
            v1_1[1] = a3;
            v1_1[2] = a2_1;
            v1_1[3] = a1;
            v1_1 = &v1_1[4];
        } while (i != &arg2[0x34]);
    }

    *(int32_t *)(str + 0x178) = arg3[1];
    *(int32_t *)(str + 0x174) = arg3[0];
    *(int32_t *)(str + 0x17c) = -1;
    sprintf(str + 0xd8, "VBMPool%d", arg1);

    {
        int32_t a1_1 = *(int32_t *)(str + 0x14);

        if (a1_1 == 0x23 || a1_1 == 0xf) {
            s0_1 = (((*(int32_t *)(str + 0x10) + 0xf) & 0xfffffff0) * (*(int32_t *)(str + 0xc) << 2));
            s7_6 = s0_1;
        } else {
            int32_t v0_6 = *(int32_t *)(str + 0xc);
            int32_t a2_3 = *(int32_t *)(str + 0x10);

            if (a1_1 == 0x3231564e) {
                s7_6 = (((((v0_6 + 0xf) & 0xfffffff0) * 0xc) >> 3) * ((a2_3 + 0xf) & 0xfffffff0));
                s0_1 = s7_6;
            } else if ((uint32_t)a1_1 < 0x3231564fU) {
                if (a1_1 == 0x32314742) {
                    s7_6 = (v0_6 * a2_3) << 4 >> 3;
                    s0_1 = s7_6;
                } else if ((uint32_t)a1_1 < 0x32314743U) {
                    if (a1_1 == 0x32314142 || a1_1 == 0x32314247) {
                        s7_6 = (v0_6 * a2_3) << 4 >> 3;
                        s0_1 = s7_6;
                    } else {
                        s0_1 = -1;
                        s7_6 = -1;
                    }
                } else if (a1_1 == 0x32314752) {
                    s7_6 = (v0_6 * a2_3) << 4 >> 3;
                    s0_1 = s7_6;
                } else if (a1_1 == 0x32315559) {
                    s7_6 = (((((v0_6 + 0xf) & 0xfffffff0) * 0xc) >> 3) * ((a2_3 + 0xf) & 0xfffffff0));
                    s0_1 = s7_6;
                } else {
                    s0_1 = -1;
                    s7_6 = -1;
                }
            } else if (a1_1 == 0x50424752) {
                s7_6 = (v0_6 * a2_3) << 4 >> 3;
                s0_1 = s7_6;
            } else if ((uint32_t)a1_1 >= 0x50424753U) {
                if (a1_1 == 0x56595559 || a1_1 == 0x59565955) {
                    s7_6 = (v0_6 * a2_3) << 4 >> 3;
                    s0_1 = s7_6;
                } else {
                    s0_1 = -1;
                    s7_6 = -1;
                }
            } else if (a1_1 == 0x33524742) {
                s7_6 = (v0_6 * a2_3 * 0x18) >> 3;
                s0_1 = s7_6;
            } else if (a1_1 == 0x34524742) {
                s7_6 = (v0_6 * a2_3) << 5 >> 3;
                s0_1 = s7_6;
            } else {
                s0_1 = -1;
                s7_6 = -1;
            }
        }
    }

    imp_log_fun(4, IMP_Log_Get_Option(), 2, data_edab0,
        "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x142, "VBMCreatePool",
        "%s()-%d: w=%d h=%d f=%d nrVBs=%d\n", "VBMCreatePool", arg1,
        *(int32_t *)(str + 0xc), *(int32_t *)(str + 0x10), *(int32_t *)(str + 0x14),
        *(int32_t *)(str + 0xd4));
    imp_log_fun(4, IMP_Log_Get_Option(), 2, data_edab0,
        "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x143, "VBMCreatePool",
        "%s()-%d: pool->config.fmt.fmt.pix.sizeimage=%d sizeimage=%d\n",
        "VBMCreatePool", arg1, *(int32_t *)(str + 0x20), s7_6);

    if ((uint32_t)*(int32_t *)(str + 0x20) >= (uint32_t)s0_1) {
        s0_1 = *(int32_t *)(str + 0x20);
    }

    *(int32_t *)(str + 0x17c) = IMP_FrameSource_GetPool(arg1);
    if (*(int32_t *)(str + 0x17c) < 0) {
        s6_2 = IMP_Alloc((IMP_Alloc_Info *)(void *)(str + 0xd8), s0_1 * arg2[0x33], str + 0xd8);
    } else {
        s6_2 = IMP_PoolAlloc(*(int32_t *)(str + 0x17c), (IMP_Alloc_Info *)(void *)(str + 0xd8), s0_1 * arg2[0x33], str + 0xd8);
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, data_edab0,
        "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x14d, "VBMCreatePool",
        "%s()-%d: sizeimage=%d\n", "VBMCreatePool", arg1, s0_1);
    if (s6_2 < 0) {
        printf("%s[%d] poolid = %d IMP_ALLOC failed size = %d \n", "VBMCreatePool", 0x14f, arg1, s0_1 * arg2[0x33]);
        return -1;
    }

    v1_8 = *(int32_t *)(str + 0x158);
    v0_16 = *(int32_t *)(str + 0x15c);
    *(int32_t *)(str + 0x16c) = v1_8;
    *(int32_t *)(str + 0x170) = v0_16;

    if (*(int32_t *)(str + 0xd4) > 0) {
        VBMFrame *s6_3 = (VBMFrame *)(void *)(str + 0x180);
        int32_t s7_7 = 0;
        int32_t s4_2 = 0;

        while (1) {
            int32_t i_1 = 0;
            VBMFrame **v1_11 = &g_framevolumes[0].frame;

            s6_3->index = s4_2;
            s6_3->pool_idx = *(int32_t *)str;
            s6_3->width = *(int32_t *)(str + 0xc);
            s6_3->height = *(int32_t *)(str + 0x10);
            s6_3->phys_addr = (uint32_t)(s7_7 + v0_16);
            s6_3->size = s0_1;
            s6_3->pixfmt = *(int32_t *)(str + 0x14);
            s6_3->virt_addr = (uint32_t)(s7_7 + v1_8);

            imp_log_fun(4, IMP_Log_Get_Option(), 2, data_edab0,
                "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x170, "VBMCreatePool",
                "PoolId:%d, frame=%p, frame->priv=%p, frame[%d].virAddr=%x, frame[%d].phyAddr=%x\n",
                s6_3->pool_idx, s6_3, &((int32_t *)s6_3)[0xb], s4_2, s6_3->virt_addr, s4_2, s6_3->phys_addr);

            do {
                VBMFrame *a0_14 = *v1_11;

                v1_11 = (VBMFrame **)((char *)v1_11 + 0x28);
                if (a0_14 == 0) {
                    int32_t v0_20 = i_1 * 0x28;
                    VBMFrameVolume *v1_12 = (VBMFrameVolume *)((char *)g_framevolumes + v0_20);

                    v1_12->phys_addr = s6_3->phys_addr;
                    v1_12->frame = s6_3;
                    v1_12->virt_addr = s6_3->virt_addr;
                    v1_12->ref_count = 0;
                    pthread_mutex_init((pthread_mutex_t *)(void *)((char *)g_framevolumes + v0_20 + 0x10), 0);
                    break;
                }

                i_1 += 1;
            } while (i_1 != 0x1e);

            s4_2 += 1;
            s6_3 = (VBMFrame *)((char *)s6_3 + 0x428);
            s7_7 += s0_1;
            if (s4_2 >= *(int32_t *)(str + 0xd4)) {
                break;
            }

            v0_16 = *(int32_t *)(str + 0x170);
            v1_8 = *(int32_t *)(str + 0x16c);
        }
    }

    *(void **)((char *)VBMGetInstance() + (arg1 << 2)) = str;
    return 0;
}

int32_t VBMDestroyPool(int32_t arg1)
{
    if (arg1 >= 6) {
        return -1;
    }

    {
        void **s3 = (void **)((char *)VBMGetInstance() + (arg1 << 2));
        char *s6 = (char *)*s3;

        if (s6 == 0) {
            return -1;
        }

        if (*(int32_t *)(s6 + 0xd4) > 0) {
            VBMFrame *s0_1 = (VBMFrame *)(void *)(s6 + 0x180);
            int32_t i = 0;

            do {
                void *a0_1 = *(void **)((char *)s0_1 + 0x54);

                if (a0_1 != 0) {
                    free(a0_1);
                }

                {
                    VBMFrame **v1_1 = &g_framevolumes[0].frame;
                    int32_t j = 0;

                    while (j != 0x1e) {
                        VBMFrame *a0_2 = *v1_1;

                        v1_1 = (VBMFrame **)((char *)v1_1 + 0x28);
                        if (a0_2 == s0_1) {
                            int32_t s4_2 = j * 0x28;

                            pthread_mutex_destroy((pthread_mutex_t *)(void *)((char *)g_framevolumes + s4_2 + 0x10));
                            memset((char *)g_framevolumes + s4_2, 0, 0x28);
                            break;
                        }

                        j += 1;
                    }
                }

                i += 1;
                s0_1 = (VBMFrame *)((char *)s0_1 + 0x428);
            } while (i < *(int32_t *)(s6 + 0xd4));
        }

        if (*(int32_t *)(s6 + 0x17c) < 0) {
            IMP_Free((IMP_Alloc_Info *)(void *)(s6 + 0xd8), *(int32_t *)(s6 + 0x158));
        } else {
            IMP_PoolFree(*(int32_t *)(s6 + 0x17c), (IMP_Alloc_Info *)(void *)(s6 + 0xd8), *(int32_t *)(s6 + 0x158));
        }

        free(s6);
        *s3 = 0;
        return 0;
    }
}

int32_t VBMFlushFrame(int32_t arg1)
{
    if (arg1 >= 6) {
        return -1;
    }

    {
        char *fp_1 = *(char **)((char *)VBMGetInstance() + (arg1 << 2));

        if (fp_1 != 0) {
            if (*(int32_t *)(fp_1 + 0xd4) <= 0) {
                return 0;
            }

            {
                VBMFrame *s4_1 = (VBMFrame *)(void *)(fp_1 + 0x180);
                int32_t s1_1 = 0;
                int32_t s6_1 = 0;

                while (1) {
                    VBMFrame **v0_4 = &g_framevolumes[0].frame;
                    int32_t s0_2 = 0;
                    pthread_mutex_t *s2_2;
                    int32_t s3_1;

                    while (1) {
                        s0_2 += 1;
                        if (*v0_4 == s4_1) {
                            int32_t v0_5 = (s0_2 - 1) << 3;
                            int32_t v1_2 = (s0_2 - 1) << 5;

                            s3_1 = v0_5 + v1_2;
                            s2_2 = (pthread_mutex_t *)(void *)((char *)g_framevolumes + s3_1 + 0x10);
                            break;
                        }

                        v0_4 = (VBMFrame **)((char *)v0_4 + 0x28);
                        if (s0_2 == 0x1e) {
                            vbm_assert_trap();
                        }
                    }

                    while (1) {
                        int32_t s0_4;

                        s1_1 += 1;
                        if ((s1_1 - 1) >= 0x3e8) {
                            break;
                        }

                        do {
                        } while (usleep(0x2710) == 0);

                        if (s1_1 >= 0x3e8) {
                            break;
                        }

                        pthread_mutex_lock(s2_2);
                        s0_4 = *(int32_t *)((char *)g_framevolumes + s3_1 + 0xc);
                        pthread_mutex_unlock(s2_2);
                        if (s0_4 <= 0) {
                            goto label_2f924;
                        }
                    }

                    imp_log_fun(5, IMP_Log_Get_Option(), 2, data_edab0,
                        "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x1ca, "VBMFlushFrame",
                        "frame:pool_idx=%d, index=%d, ref=%d, virAddr=%x maybe deadlocked\n",
                        s4_1->pool_idx, s4_1->index,
                        *(int32_t *)((char *)g_framevolumes + (((s0_2 - 1) << 3) + ((s0_2 - 1) << 5) + 0xc)),
                        s4_1->virt_addr, &_gp);
                    return -1;

label_2f924:
                    s6_1 += 1;
                    s4_1 = (VBMFrame *)((char *)s4_1 + 0x428);
                    if (s6_1 >= *(int32_t *)(fp_1 + 0xd4)) {
                        return 0;
                    }
                }
            }
        }
    }

    return 0;
}

int32_t VBMFillPool(int32_t arg1)
{
    void **s0_2 = (void **)((char *)VBMGetInstance() + (arg1 << 2));
    char *s6 = (char *)*s0_2;

    if (s6 == 0) {
        return -1;
    }

    if (*(int32_t *)(s6 + 0xd4) > 0) {
        int32_t s3_1 = 0;
        int32_t i = 0;

        do {
            VBMFrame **v1_1 = &g_framevolumes[0].frame;
            VBMFrame *s6_2 = (VBMFrame *)(void *)((char *)s6 + 0x180 + s3_1);
            VBMFrameVolume *s7_1 = 0;
            int32_t j = 0;

            while (j != 0x1e) {
                VBMFrame *a0 = *v1_1;

                v1_1 = (VBMFrame **)((char *)v1_1 + 0x28);
                if (s6_2 == a0) {
                    s7_1 = (VBMFrameVolume *)((char *)g_framevolumes + j * 0x28);
                    break;
                }

                j += 1;
            }

            pthread_mutex_lock(vbm_volume_mutex(s7_1));
            if (((int32_t (*)(VBMFrame *, void *))*(void **)((char *)*s0_2 + 0x178))(s6_2, *(void **)((char *)*s0_2 + 4)) < 0) {
                pthread_mutex_unlock(vbm_volume_mutex(s7_1));
                imp_log_fun(6, IMP_Log_Get_Option(), 2, data_edab0,
                    "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x1e4, "VBMFillPool",
                    "%s:releaseFrame failed:frame->pool_idx=%d, frame->index=%d\n",
                    "VBMFillPool", s6_2->pool_idx, s6_2->index);
            } else {
                pthread_mutex_unlock(vbm_volume_mutex(s7_1));
            }

            s6 = (char *)*s0_2;
            i += 1;
            s3_1 += 0x428;
        } while (i < *(int32_t *)(s6 + 0xd4));
    }

    return 0;
}

VBMFrame *VBMGetFrameInstance(int32_t arg1, int32_t arg2)
{
    char *a0 = *(char **)((char *)VBMGetInstance() + (arg1 << 2));

    if (a0 == 0) {
        return 0;
    }

    return (VBMFrame *)(void *)(a0 + 0x180 + arg2 * 0x428);
}

VBMFrame *vbm_get_frame(int32_t arg1, int32_t arg2)
{
    return VBMGetFrameInstance(arg1, arg2);
}

int32_t VBMFlushPool(int32_t arg1)
{
    void **s1_1 = (void **)((char *)VBMGetInstance() + (arg1 << 2));
    void *v0_1 = *s1_1;
    int32_t i = 0;

    if (v0_1 == 0) {
        return -1;
    }

    if (*(int32_t *)((char *)v0_1 + 0xd4) > 0) {
        do {
            uint8_t var_48[8];

            *(int32_t *)(void *)(var_48 + 4) = arg1;
            ((void (*)(void *, void *))*(void **)((char *)v0_1 + 0x174))(var_48, *(void **)((char *)v0_1 + 4));
            v0_1 = *s1_1;
            i += 1;
        } while (i < *(int32_t *)((char *)v0_1 + 0xd4));
    }

    return 0;
}

int32_t VBMGetFrame(int32_t arg1, VBMFrame **arg2)
{
    void **s2_1 = (void **)((char *)VBMGetInstance() + (arg1 << 2));
    void *v0_1 = *s2_1;

    if (v0_1 == 0) {
        return -1;
    }

    {
        uint8_t stack_buf[0x438];
        uint32_t a2_1;
        VBMFrame *i;
        int32_t v0_8;

        memset(stack_buf, 0, sizeof(stack_buf));
        *(int32_t *)(void *)(stack_buf + 4) = arg1;
        ((void (*)(void *, void *))*(void **)((char *)v0_1 + 0x174))(stack_buf, *(void **)((char *)v0_1 + 4));
        a2_1 = *(uint8_t *)(void *)(stack_buf + 0x3e8);
        i = (VBMFrame *)((char *)*s2_1 + 0x180 + (*(int32_t *)(void *)stack_buf) * 0x428);
        *arg2 = i;

        if (a2_1 == 2) {
            uint32_t *v1_6 = (uint32_t *)(void *)&frame_type2;
            uint32_t *src = (uint32_t *)(void *)i;

            do {
                _setLeftPart32(src[0]);
                _setLeftPart32(src[1]);
                _setLeftPart32(src[2]);
                _setLeftPart32(src[3]);
                v1_6[0] = _setRightPart32(src[0]);
                v1_6[1] = _setRightPart32(src[1]);
                v1_6[2] = _setRightPart32(src[2]);
                v1_6[3] = _setRightPart32(src[3]);
                src += 4;
                v1_6 += 4;
            } while (src != (uint32_t *)(void *)((char *)i + 0x420));

            _setLeftPart32(src[0]);
            _setLeftPart32(src[1]);
            v1_6[0] = _setRightPart32(src[0]);
            v1_6[1] = _setRightPart32(src[1]);
            v0_8 = 0;
            *(int32_t *)((char *)&frame_type2 + 0x3e8) = 2;
            *(uint16_t *)((char *)&frame_type2 + 0x3ea) = *(uint16_t *)(void *)(stack_buf + 0x3ea);
            *(int32_t *)((char *)&frame_type2 + 0x3ec) = *(int32_t *)(void *)(stack_buf + 0x3ec);
            *arg2 = &frame_type2;
        } else {
            i->type = (uint8_t)a2_1;
            (*(VBMFrame **)arg2)->field_3ec = *(int32_t *)(void *)(stack_buf + 0x3f0);
            (*(VBMFrame **)arg2)->field_3f0 = *(int32_t *)(void *)(stack_buf + 0x3f4);
            (*(VBMFrame **)arg2)->field_3e6 = *(uint16_t *)(void *)(stack_buf + 0x3ea);
            *(int32_t *)((char *)(*arg2) + 0x20) = *(int32_t *)(void *)(stack_buf + 0x20);
            (*(VBMFrame **)arg2)->field_3e8 = *(int32_t *)(void *)(stack_buf + 0x3ec);
            *(int32_t *)((char *)(*arg2) + 0x24) = *(int32_t *)(void *)(stack_buf + 0x24);
            v0_8 = 0;
            if (arg1 == 0) {
                int32_t v0_9 = IMP_ISP_Tuning_GetAeZone((char *)(*arg2) + 0x58);

                if (v0_9 == 0) {
                    *(int32_t *)((char *)(*arg2) + 0x38) = 1;
                    return v0_9;
                }

                *(int32_t *)((char *)(*arg2) + 0x38) = 0;
                imp_log_fun(3, IMP_Log_Get_Option(), 2, data_edab0,
                    "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x260, "VBMGetFrame",
                    "%s(%d):Get Ae_Zone failed!\n", "VBMGetFrame", 0x260, &_gp);
                return 0;
            }
        }

        return v0_8;
    }
}

int32_t VBMDumpPool(int32_t arg1)
{
    int32_t i = (int32_t)(intptr_t)VBMGetInstance();
    void **s2 = (void **)(intptr_t)(i + (arg1 << 2));
    char *s7 = (char *)*s2;

    if (s7 != 0) {
        i = (int32_t)(intptr_t)g_framevolumes;
        if (*(int32_t *)(s7 + 0xd4) > 0) {
            int32_t s6_1 = 0;
            int32_t fp_1 = 0;

            do {
                VBMFrame **v0_1 = &g_framevolumes[0].frame;
                VBMFrame *s7_2 = (VBMFrame *)(void *)(s7 + 0x180 + s6_1);
                int32_t s0_2 = 0;

                while (1) {
                    s0_2 += 1;
                    if (s7_2 == *v0_1) {
                        break;
                    }

                    v0_1 = (VBMFrame **)((char *)v0_1 + 0x28);
                    if (s0_2 == 0x1e) {
                        vbm_assert_trap();
                    }
                }

                fp_1 += 1;
                s6_1 += 0x428;
                imp_log_fun(3, IMP_Log_Get_Option(), 2, data_edab0,
                    "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x2a3, "VBMDumpPool",
                    "%s(%d):frame->pool_idx=%d, frame->index=%d, frameVol->ref=%d\n",
                    "VBMDumpPool", 0x2a3, s7_2->pool_idx, s7_2->index,
                    *(int32_t *)((char *)g_framevolumes + (s0_2 - 1) * 0x28 + 0xc));
                s7 = (char *)*s2;
                i = fp_1 < *(int32_t *)(s7 + 0xd4) ? 1 : 0;
            } while (i != 0);
        }
    }

    return i;
}

int32_t VBMDumpPoolInfo(void)
{
    void **s0 = (void **)VBMGetInstance();
    int32_t i_1 = 0;
    int32_t i;

    do {
        if (*s0 != 0) {
            int32_t var_4c = 0;
            int32_t var_48 = 0;

            imp_log_fun(3, IMP_Log_Get_Option(), 2, data_edab0,
                "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x2ae, "VBMDumpPoolInfo",
                "GROUP:%d\n", i_1, var_4c, var_48);

            {
                void *v1_2 = *s0;

                if (*(int32_t *)((char *)v1_2 + 0xd4) > 0) {
                    int32_t s7_1 = 0;
                    int32_t j = 0;

                    do {
                        VBMFrame *s6_1 = (VBMFrame *)((char *)v1_2 + 0x180 + s7_1);
                        int32_t v0_4 = IMP_Log_Get_Option();

                        var_48 = *(int32_t *)((char *)s6_1 + 0x3e0);
                        var_4c = *(int32_t *)((char *)s6_1 + 0x3dc);
                        imp_log_fun(3, v0_4, 2, data_edab0,
                            "/home/user/git/proj/sdk-lv3/src/imp/core/vbm.c", 0x2b2, "VBMDumpPoolInfo",
                            "FRAME:%d  qcount:%d dqcount:%d\n", s6_1->index, var_4c, var_48);
                        v1_2 = *s0;
                        j += 1;
                        s7_1 += 0x428;
                    } while (j < *(int32_t *)((char *)v1_2 + 0xd4));
                }
            }
        }

        i = i_1 + 1;
        i_1 = i;
        s0 = (void **)((char *)s0 + 4);
    } while (i != 5);

    return 5;
}
