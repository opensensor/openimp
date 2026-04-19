#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "core/imp_alloc.h"

extern char _gp;

int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
int32_t IMP_Alloc(IMP_Alloc_Info *arg1, int32_t arg2, char *arg3); /* forward decl, ported by T<N> later */
int32_t IMP_Free(IMP_Alloc_Info *arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t IMP_Virt_to_Phys(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t IMP_Phys_to_Virt(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t c_align(int32_t arg1, int32_t arg2); /* forward decl, ported by T<N> later */
uint32_t _getLeftPart32(uint32_t value); /* forward decl, ported by T<N> later */
uint32_t _getRightPart32(uint32_t value); /* forward decl, ported by T<N> later */

typedef struct VideoVbmInfo {
    IMP_Alloc_Info alloc;     /* 0x00 */
    int32_t in_use;           /* 0x94 */
    int32_t aligned_vaddr;    /* 0x98 */
} VideoVbmInfo;

typedef struct ImpVideoListElem {
    struct ImpVideoListElem *next;  /* 0x00 */
    struct ImpVideoListElem *prev;  /* 0x04 */
    void *data;                     /* 0x08 */
    int32_t size;                   /* 0x0c */
    void (*release)(void);          /* 0x10 */
} ImpVideoListElem;

typedef struct ImpVideoContainer {
    pthread_mutex_t mutex0;         /* 0x00 */
    ImpVideoListElem queue0;        /* 0x18 */
    pthread_mutex_t mutex1;         /* 0x20 */
    ImpVideoListElem queue1;        /* 0x38 */
    pthread_mutex_t mutex2;         /* 0x40 */
    ImpVideoListElem queue2;        /* 0x58 */
    ImpVideoListElem *pelem;        /* 0x60 */
    int32_t elem_count;             /* 0x64 */
} ImpVideoContainer;

static VideoVbmInfo *g_video_vbm_info;

void *video_vbm_malloc(int32_t arg1, int32_t arg2)
{
    VideoVbmInfo *video_vbm_info_1 = g_video_vbm_info;
    void *video_vbm_info_2;

    if (video_vbm_info_1 == NULL) {
        video_vbm_info_1 = calloc(0x10, 0x9c);
        video_vbm_info_2 = video_vbm_info_1;
        g_video_vbm_info = video_vbm_info_1;
        if (video_vbm_info_1 != NULL) {
            goto label_8d500;
        }

        imp_log_fun(6, IMP_Log_Get_Option(), 2, "VIDEOCOMMON",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_video_common.c", 0x26,
            "video_vbm_malloc", "init video vbm info failed:%s\n",
            strerror(errno));
    } else {
label_8d500:
        {
            int32_t *v0 = (int32_t *)((char *)video_vbm_info_1 + 0x94);
            int32_t s0 = 0;

            while (1) {
                int32_t v1_1 = *v0;

                v0 += 0x27;
                if (v1_1 == 0) {
                    IMP_Alloc_Info var_b0;

                    if (IMP_Alloc(&var_b0, arg1 + arg2, "video") < 0) {
                        video_vbm_info_2 = NULL;
                        imp_log_fun(6, IMP_Log_Get_Option(), 2, "VIDEOCOMMON",
                            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_video_common.c", 0x37,
                            "video_vbm_malloc", "video_vbm_malloc(%d, %d, video) failed\n",
                            arg1, arg2, &_gp);
                        break;
                    }

                    {
                        int32_t v0_4 = c_align(*(int32_t *)((char *)&var_b0 + 0x88), arg2);
                        int32_t *i = (int32_t *)&var_b0;
                        int32_t *s0_1 = (int32_t *)((char *)g_video_vbm_info + s0);
                        int32_t *v1_3 = s0_1;
                        int32_t *limit = (int32_t *)((char *)&var_b0 + 0x90);

                        do {
                            int32_t a3_1 = i[1];
                            int32_t a2_1 = i[2];
                            uint32_t a1_3 = (uint32_t)i[3];

                            *v1_3 = *i;
                            v1_3[1] = a3_1;
                            v1_3[2] = a2_1;
                            v1_3[3] = (int32_t)_getLeftPart32(a1_3);
                            i += 4;
                            v1_3[3] = (int32_t)_getRightPart32(a1_3);
                            v1_3 += 4;
                        } while (i != limit);

                        *v1_3 = *i;
                        s0_1[0x26] = v0_4;
                        s0_1[0x25] = 1;
                        return (void *)(intptr_t)v0_4;
                    }
                }

                s0 += 0x9c;
                if (s0 == 0x9c0) {
                    video_vbm_info_2 = NULL;
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "VIDEOCOMMON",
                        "/home/user/git/proj/sdk-lv3/src/imp/video/imp_video_common.c", 0x32,
                        "video_vbm_malloc", "can't find unused alloc info elem\n");
                    break;
                }
            }
        }
    }

    return video_vbm_info_2;
}

int32_t video_vbm_free(int32_t arg1)
{
    int32_t video_vbm_info_1 = (int32_t)(intptr_t)g_video_vbm_info;

    if (video_vbm_info_1 == 0) {
        video_vbm_info_1 = imp_log_fun(5, IMP_Log_Get_Option(), 2, "VIDEOCOMMON",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_video_common.c", 0x53,
            "video_vbm_free", "video vbm info han't been init\n");
        if (arg1 == 0) {
            return video_vbm_info_1;
        }
    } else if (arg1 == 0) {
        return video_vbm_info_1;
    }

    {
        uint32_t video_vbm_info_2 = (uint32_t)(uintptr_t)g_video_vbm_info;
        int32_t v1 = 0;
        uint32_t video_vbm_info_3;
        int32_t s1_1;

        while (1) {
            s1_1 = v1;
            video_vbm_info_3 = video_vbm_info_2;
            v1 += 0x9c;
            if (arg1 == *(int32_t *)(uintptr_t)(video_vbm_info_2 + 0x98) &&
                    *(int32_t *)(uintptr_t)(video_vbm_info_2 + 0x94) == 1) {
                break;
            }

            video_vbm_info_2 += 0x9c;
            if (v1 == 0x9c0) {
                s1_1 = 0x9c0;
                imp_log_fun(5, IMP_Log_Get_Option(), 2, "VIDEOCOMMON",
                    "/home/user/git/proj/sdk-lv3/src/imp/video/imp_video_common.c", 0x60,
                    "video_vbm_free", "%s:can't free vaddr=%x\n", "video_vbm_free");
                video_vbm_info_3 = (uint32_t)(uintptr_t)g_video_vbm_info + 0x9c0;
                break;
            }
        }

        IMP_Free((IMP_Alloc_Info *)(uintptr_t)video_vbm_info_3,
            *(int32_t *)(uintptr_t)(video_vbm_info_3 + 0x80));
        return (int32_t)(intptr_t)memset((char *)g_video_vbm_info + s1_1, 0, 0x9c);
    }
}

int32_t video_vbm_virt_to_phys(int32_t arg1)
{
    return IMP_Virt_to_Phys(arg1);
}

int32_t video_vbm_phys_to_virt(int32_t arg1)
{
    return IMP_Phys_to_Virt(arg1);
}

int32_t video_sem_timedwait(sem_t *arg1, int32_t *arg2)
{
    struct timespec var_18;

    if (clock_gettime(0, &var_18) < 0) {
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "VIDEOCOMMON",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_video_common.c", 0x7d,
            "video_sem_timedwait", "clock_gettime CLOCK_REALTIME failed\n", &_gp);
        return -1;
    }

    {
        int32_t a2_2 = (int32_t)var_18.tv_nsec + arg2[1];

        var_18.tv_sec = var_18.tv_sec + arg2[0] + a2_2 / 0x3b9aca00;
        {
            int32_t a2_3 = a2_2 % 0x3b9aca00;

            var_18.tv_nsec = a2_3;
            return sem_timedwait(arg1, &var_18);
        }
    }
}

int32_t imp_video_push_queue_head(pthread_mutex_t *arg1, ImpVideoListElem *arg2, ImpVideoListElem *arg3)
{
    pthread_mutex_lock(arg1);
    arg3->prev = arg2->prev;
    {
        ImpVideoListElem *v0_1 = arg2->prev;

        arg3->next = arg2;
        v0_1->next = arg3;
        arg2->prev = arg3;
    }
    return pthread_mutex_unlock(arg1);
}

int32_t imp_video_push_queue_tail(pthread_mutex_t *arg1, ImpVideoListElem *arg2, ImpVideoListElem *arg3)
{
    pthread_mutex_lock(arg1);
    arg3->next = arg2->next;
    {
        ImpVideoListElem *v0_1 = arg2->next;

        arg3->prev = arg2;
        v0_1->prev = arg3;
        arg2->next = arg3;
    }
    return pthread_mutex_unlock(arg1);
}

int32_t imp_video_pop_queue(pthread_mutex_t *arg1, ImpVideoListElem *arg2, ImpVideoListElem **arg3)
{
    pthread_mutex_lock(arg1);
    {
        ImpVideoListElem *v0 = arg2->prev;

        if (arg2 == v0) {
            *arg3 = NULL;
            return pthread_mutex_unlock(arg1);
        }

        *arg3 = v0;
        {
            ImpVideoListElem *v0_2 = arg2->prev->prev;

            arg2->prev = v0_2;
            v0_2->next = arg2;
        }
        {
            ImpVideoListElem *v0_3 = *arg3;

            v0_3->next = v0_3;
            v0_3->prev = v0_3;
        }
        return pthread_mutex_unlock(arg1);
    }
}

void *imp_video_container_init(size_t arg1, size_t arg2)
{
    void *result = calloc(1, 0x68);

    if (result == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "VIDEOCOMMON",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_video_common.c", 0xb5,
            "imp_video_container_init", "calloc imp_video_container_t failed\n");
    } else {
        ImpVideoListElem *v0 = calloc(arg1, 0x14);

        *(ImpVideoListElem **)((char *)result + 0x60) = v0;
        if (v0 == NULL) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "VIDEOCOMMON",
                "/home/user/git/proj/sdk-lv3/src/imp/video/imp_video_common.c", 0xbb,
                "imp_video_container_init", "calloc imp_video_list_elem_t failed\n");
        } else {
            ImpVideoListElem *s0_1 = v0;
            int32_t s1_1 = 0;

            if (arg1 == 0) {
                goto label_8dd94;
            }

            while (1) {
                void *v0_1 = calloc(1, arg2);

                s0_1->data = v0_1;
                if (v0_1 == NULL) {
                    int32_t i = s1_1 - 1;

                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "VIDEOCOMMON",
                        "/home/user/git/proj/sdk-lv3/src/imp/video/imp_video_common.c", 0xc2,
                        "imp_video_container_init",
                        "calloc pcontainer->pelem[%d].data failed\n", s1_1);
                    {
                        void *a0_1 = *(void **)((char *)result + 0x60);

                        if (s1_1 != 0) {
                            int32_t s0_3 = i * 0x14;

                            do {
                                i -= 1;
                                free(*(void **)((char *)a0_1 + s0_3 + 8));
                                a0_1 = *(void **)((char *)result + 0x60);
                                *(void **)((char *)a0_1 + s0_3 + 8) = NULL;
                                s0_3 -= 0x14;
                            } while (i != -1);
                        }

                        free(a0_1);
                    }
                    break;
                }

                s1_1 += 1;
                s0_1->size = (int32_t)arg2;
                s0_1->release = NULL;
                s0_1->prev = s0_1;
                s0_1->next = s0_1;
                s0_1 = (ImpVideoListElem *)((char *)s0_1 + 0x14);
                if ((size_t)s1_1 == arg1) {
label_8dd94:
                    *(int32_t *)((char *)result + 0x64) = (int32_t)arg1;
                    pthread_mutex_init((pthread_mutex_t *)result, NULL);
                    *(ImpVideoListElem **)((char *)result + 0x1c) = (ImpVideoListElem *)((char *)result + 0x18);
                    *(ImpVideoListElem **)((char *)result + 0x18) = (ImpVideoListElem *)((char *)result + 0x18);
                    pthread_mutex_init((pthread_mutex_t *)((char *)result + 0x20), NULL);
                    *(ImpVideoListElem **)((char *)result + 0x3c) = (ImpVideoListElem *)((char *)result + 0x38);
                    *(ImpVideoListElem **)((char *)result + 0x38) = (ImpVideoListElem *)((char *)result + 0x38);
                    pthread_mutex_init((pthread_mutex_t *)((char *)result + 0x40), NULL);
                    *(ImpVideoListElem **)((char *)result + 0x5c) = (ImpVideoListElem *)((char *)result + 0x58);
                    *(ImpVideoListElem **)((char *)result + 0x58) = (ImpVideoListElem *)((char *)result + 0x58);
                    {
                        int32_t s1_2 = 0;
                        int32_t s0_4 = 0;

                        if (arg1 != 0) {
                            do {
                                s0_4 += 1;
                                imp_video_push_queue_tail((pthread_mutex_t *)result,
                                    (ImpVideoListElem *)((char *)result + 0x18),
                                    (ImpVideoListElem *)((char *)(*(void **)((char *)result + 0x60)) + s1_2));
                                s1_2 += 0x14;
                            } while ((size_t)s0_4 != arg1);
                        }
                    }
                    return result;
                }
            }
        }

        free(result);
    }

    return NULL;
}

void imp_video_container_deinit(void *arg1)
{
    if (arg1 == NULL) {
        return;
    }

    {
        ImpVideoListElem *j_1 = NULL;
        int32_t i = 0;

        while (i != 3) {
            char *s0_2 = (char *)arg1;

            if (i != 0) {
                char *v0_1 = (char *)arg1 + 0x20;

                if (i != 1) {
                    v0_1 = (char *)arg1 + 0x40;
                }
                s0_2 = v0_1;
            }

label_8df84:
            imp_video_pop_queue((pthread_mutex_t *)s0_2, (ImpVideoListElem *)(s0_2 + 0x18), &j_1);
            {
                ImpVideoListElem *j = j_1;

                while (j != NULL) {
                    if (*(void **)((char *)j + 8) == NULL) {
                        goto label_8df84;
                    }
                    if ((void (*)(void))((char *)j + 0x10) == NULL) {
                        goto label_8df84;
                    }
                    ((void (*)(void))((char *)j + 0x10))();
                    {
                        ImpVideoListElem *j_2 = j_1;

                        j_2->prev = j_2;
                        j_2->next = j_2;
                    }
                    imp_video_pop_queue((pthread_mutex_t *)s0_2, (ImpVideoListElem *)(s0_2 + 0x18), &j_1);
                    j = j_1;
                }
            }

            i += 1;
            pthread_mutex_destroy((pthread_mutex_t *)s0_2);
            *(ImpVideoListElem **)(s0_2 + 0x1c) = (ImpVideoListElem *)(s0_2 + 0x18);
            *(ImpVideoListElem **)(s0_2 + 0x18) = (ImpVideoListElem *)(s0_2 + 0x18);
        }

        {
            int32_t s0_3 = *(int32_t *)((char *)arg1 + 0x64);
            int32_t i_1 = s0_3 - 1;
            void *a0_3;

            if (i_1 < 0) {
                a0_3 = *(void **)((char *)arg1 + 0x60);
            } else {
                a0_3 = *(void **)((char *)arg1 + 0x60);
                {
                    int32_t s0_6 = s0_3 * 0x14 - 0x14;

                    do {
                        i_1 -= 1;
                        free(*(void **)((char *)a0_3 + s0_6 + 8));
                        a0_3 = *(void **)((char *)arg1 + 0x60);
                        {
                            char *v0_3 = (char *)a0_3 + s0_6;

                            *(void **)(v0_3 + 8) = NULL;
                            *(int32_t *)(v0_3 + 0xc) = 0;
                        }
                        s0_6 -= 0x14;
                    } while (i_1 != -1);
                }
            }

            free(a0_3);
            free(arg1);
        }
    }
}
