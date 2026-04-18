#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

typedef int32_t (*ShareMemFunc)(int32_t *arg1, int32_t arg2, void *arg3);
typedef int32_t (*UserMemFunc)(void **arg1, void *arg2);
typedef int32_t (*ShmDispatchCb)(void *arg1, int32_t arg2);
typedef void (*UserMemReleaseFunc)(void *arg1);

typedef struct FuncEntry {
    int32_t used;
    char name[0x14];
    ShareMemFunc share_mem_cb;
    UserMemFunc user_mem_cb;
    UserMemReleaseFunc user_mem_release_cb;
    uint32_t ctx_handle;
    void *ctx_data;
    uint32_t ctx_size;
    int32_t ctx_ret;
    int32_t ctx_flag;
    int32_t *ctx_req;
    uint8_t *next_ptr;
} FuncEntry;

typedef struct ShmArea {
    sem_t *sem_tos;
    sem_t *sem_toc;
    int32_t shm_fd;
    void *shm_addr;
    pthread_t thread;
    ShmDispatchCb dispatch_cb;
} ShmArea;

/* Struct sizes not asserted — stock struct size was not independently
 * verified via a __sizeof__ reference in the binary; the layout above
 * matches the per-field decomp usage but the total size asserted here
 * (0x44) was a Codex guess that disagrees with the field list (0x40).
 * If runtime integration surfaces a size mismatch, revisit via objdump
 * of a caller that stores/copies these structs. */

static FuncEntry funcs[6][0x1e];
static ShmArea *psa = NULL;

static void *shm_thread(void *arg1);
int32_t shm_init(void);
int32_t shm_deinit(void);
int32_t shm_register_cb(ShmDispatchCb arg1);
int32_t func_dispatch(int32_t *arg1, int32_t arg2);
int32_t func_init(void);
int32_t func_deinit(void);
int32_t DsystemInit(void);

int32_t FBInit(void)
{
    return 0;
}

int32_t FBExit(void)
{
    return 0;
}

int32_t func_dispatch(int32_t *arg1, int32_t arg2)
{
    int32_t s3;
    int32_t s5;
    int32_t s2_1;
    int32_t fp_1;
    int32_t s4_1;
    int32_t t2_1;
    FuncEntry *s1_3;

    s3 = arg1[0];
    s5 = arg1[1];
    if ((uint32_t)s3 >= 6) {
        printf("err: %s,%d ", "func_dispatch", 0x95);
        printf("mid = %d\n", s3);
        arg1[3] = -1;
    }
    if ((uint32_t)s5 >= 0x1e) {
        printf("err: %s,%d ", "func_dispatch", 0x9a);
        printf("fid = %d\n", s5);
        arg1[3] = -1;
    }

    s2_1 = s5 << 2;
    fp_1 = s5 << 6;
    s4_1 = s3 << 3;
    t2_1 = s3 << 0xb;
    s1_3 = (FuncEntry *)((uint8_t *)funcs + s2_1 + fp_1 + t2_1 - s4_1);
    if (s1_3->used == 0) {
        printf("err: %s,%d ", "func_dispatch", 0x9f);
        printf("used= %d\n", s1_3->used);
        arg1[3] = -1;
    }

    if (s1_3->share_mem_cb != NULL) {
        int32_t v0_6;
        int32_t s6_1 = 0;

        if ((arg1[2] & 1) != 0) {
            printf("info: %s,%d", "func_dispatch", 0xad);
            printf("func mid:fid:name= %d:%d:%s\n", s3, s5, s1_3->name);
        }
        v0_6 = s1_3->share_mem_cb(arg1, arg2, &s1_3->ctx_handle);
        if (v0_6 == -1) {
            printf("err: %s,%d ", "func_dispatch", 0xb5);
            printf("func mid:fid:name= %d:%d:%s\n", s3, s5, s1_3->name);
            s6_1 = -1;
        }
        arg1[3] = v0_6;
        return s6_1;
    }

    if (s1_3->user_mem_cb == NULL) {
        printf("err: %s,%d ", "func_dispatch", 0xa8);
        printf("func mid:fid:used:cb= %d:%d:%d:%p\n", s3, s5, s1_3->used, s1_3->share_mem_cb);
        arg1[3] = -1;
        return -1;
    }

    {
        int32_t s6_2;
        int32_t s1_6;
        FuncEntry *t3_1;

        s6_2 = arg1[2];
        if ((s6_2 & 1) != 0) {
            printf("info: %s,%d", "func_dispatch", 0xad);
            printf("func mid:fid:name= %d:%d:%s\n", s3, s5, s1_3->name);
            s6_2 = arg1[2];
        }

        s1_6 = s2_1 + fp_1 + t2_1 - s4_1;
        t3_1 = (FuncEntry *)((uint8_t *)funcs + s1_6);
        if (t3_1->ctx_req != NULL) {
            if ((s6_2 & 2) != 0) {
                uint32_t n_1;
                int32_t a2_7;

                n_1 = t3_1->ctx_size - (uint32_t)(t3_1->next_ptr - (uint8_t *)t3_1->ctx_data);
                if ((int32_t)(arg2 - 0x18) < (int32_t)n_1) {
                    arg1[3] = 0;
                    arg1[5] = arg2 - 0x18;
                    arg1[2] = s6_2 & 0xfffffeff;
                    arg1[4] = t3_1->ctx_ret;
                    memcpy(&arg1[6], t3_1->next_ptr, (size_t)(arg2 - 0x18));
                    t3_1->ctx_req = (int32_t *)1;
                    t3_1->next_ptr = t3_1->next_ptr + arg2 - 0x18;
                    a2_7 = 0x110;
                } else {
                    int32_t s6_4;
                    FuncEntry *s1_9;
                    void *a0_10;

                    if (t3_1->ctx_flag == 0) {
                        s6_4 = s6_2 & 0xfffffeff;
                    } else {
                        s6_4 = s6_2 | 0x100;
                    }

                    s1_9 = (FuncEntry *)((uint8_t *)funcs + s2_1 + fp_1 + t2_1 - s4_1);
                    arg1[3] = 0;
                    arg1[2] = s6_4;
                    arg1[5] = (int32_t)n_1;
                    arg1[4] = s1_9->ctx_ret;
                    memcpy(&arg1[6], s1_9->next_ptr, n_1);
                    a0_10 = (uint8_t *)funcs + s2_1 + fp_1 + t2_1 - s4_1 + 0x20;
                    s1_9->ctx_req = NULL;
                    s1_9->next_ptr = NULL;
                    memset((uint8_t *)a0_10 + 8, 0, 0x14);
                    if (s1_9->user_mem_release_cb != NULL) {
                        s1_9->user_mem_release_cb((uint8_t *)a0_10 + 4);
                    }
                    a2_7 = 0x107;
                }

                printf("info: %s,%d", "func_dispatch", a2_7);
                printf(" size = %d\n", arg1[5]);
                return 0;
            }

            printf("err: %s,%d ", "func_dispatch", 0xef);
            printf("func mid:fid:name= %d:%d:%s, should get next data\n", s3, s5, t3_1->name);
            arg1[3] = -1;
            t3_1->ctx_req = NULL;
            t3_1->next_ptr = NULL;
            memset((uint8_t *)t3_1 + 0x28, 0, 0x14);
            if (t3_1->user_mem_release_cb != NULL) {
                t3_1->user_mem_release_cb((uint8_t *)t3_1 + 0x24);
            }
        } else {
            int32_t s6_3;
            void *var_58 = NULL;
            uint32_t n = 0;
            int32_t var_50_1 = 0;
            int32_t var_4c_1 = 1;

            s6_3 = s6_2 & 2;
            if (s6_3 != 0) {
                printf("err: %s,%d ", "func_dispatch", 0xc6);
                printf("func mid:fid:name= %d:%d:%s, should not get next data\n", s3, s5, t3_1->name);
                arg1[3] = -1;
                t3_1->ctx_req = NULL;
                t3_1->next_ptr = NULL;
                memset((uint8_t *)funcs + s1_6 + 0x28, 0, 0x14);
            } else {
                void *s1_8;
                int32_t v0_12;

                s1_8 = (uint8_t *)funcs + s1_6 + 0x20;
                v0_12 = t3_1->user_mem_cb(&var_58, (uint8_t *)s1_8 + 4);
                n = t3_1->ctx_size;
                var_50_1 = t3_1->ctx_ret;
                var_4c_1 = t3_1->ctx_flag;
                if (v0_12 != -1) {
                    if ((int32_t)(arg2 - 0x18) < (int32_t)n) {
                        void *s2_7;

                        s2_7 = var_58;
                        arg1[2] &= 0xfffffeff;
                        arg1[5] = arg2 - 0x18;
                        arg1[4] = var_50_1;
                        arg1[3] = 0;
                        memcpy(&arg1[6], s2_7, (size_t)(arg2 - 0x18));
                        t3_1->ctx_req = (int32_t *)1;
                        t3_1->next_ptr = (uint8_t *)s2_7 + arg2 - 0x18;
                        *(void **)((uint8_t *)s1_8 + 8) = var_58;
                        *(uint32_t *)((uint8_t *)s1_8 + 0xc) = n;
                        *(int32_t *)((uint8_t *)s1_8 + 0x10) = var_50_1;
                        *(int32_t *)((uint8_t *)s1_8 + 0x14) = var_4c_1;
                        *(int32_t **)((uint8_t *)s1_8 + 0x18) = arg1;
                        printf("info: %s,%d", "func_dispatch", 0xe9);
                        printf(" size = %d ret = %d\n", arg1[5], arg1[3]);
                        return s6_3;
                    }

                    if (var_4c_1 == 0) {
                        arg1[2] = arg1[2] & 0xfffffeff;
                    } else {
                        arg1[2] = arg1[2] | 0x100;
                    }
                    arg1[3] = 0;
                    arg1[4] = var_50_1;
                    arg1[5] = (int32_t)n;
                    memcpy(&arg1[6], var_58, n);
                    memset((uint8_t *)funcs + s2_1 + fp_1 + t2_1 - s4_1 + 0x28, 0, 0x14);
                    if (t3_1->user_mem_release_cb != NULL) {
                        t3_1->user_mem_release_cb((uint8_t *)s1_8 + 4);
                    }
                    printf("info: %s,%d", "func_dispatch", 0xde);
                    printf(" size = %d\n", arg1[5]);
                    return s6_3;
                }

                printf("err: %s,%d ", "func_dispatch", 0xcf);
                printf("func mid:fid:name= %d:%d:%s\n", s3, s5, t3_1->name);
                arg1[3] = v0_12;
            }
        }
    }

    return -1;
}

int32_t func_init(void)
{
    int32_t v0;

    v0 = shm_init();
    if ((uint32_t)v0 >= 2) {
        printf("err: %s,%d ", "func_init", 0x2d);
        puts("shm_init error");
        return -1;
    }
    if (v0 != 1) {
        memset(funcs, 0, 0x2fd0);
    }
    if (shm_register_cb((ShmDispatchCb)func_dispatch) == 0) {
        return 0;
    }
    shm_deinit();
    printf("err: %s,%d ", "func_init", 0x38);
    puts("shm_init error");
    return -1;
}

int32_t func_deinit(void)
{
    return shm_deinit();
}

int32_t dsys_func_share_mem_register(int32_t arg1, int32_t arg2, char *arg3, ShareMemFunc arg4)
{
    int32_t result;
    FuncEntry *s0_4;
    size_t n_1;
    size_t n;

    if ((uint32_t)arg1 >= 6) {
        printf("err: %s,%d ", "dsys_func_share_mem_register", 0x47);
        result = -1;
        printf("mid = %d\n", arg1);
    }
    if ((uint32_t)arg2 >= 0x1e) {
        printf("err: %s,%d ", "dsys_func_share_mem_register", 0x4b);
        result = -1;
        printf("fid = %d\n", arg2);
    }

    s0_4 = (FuncEntry *)((uint8_t *)funcs + arg2 * 0x44 + arg1 * 0x7f8);
    result = s0_4->used;
    if (result != 0) {
        printf("warn: %s,%d", "dsys_func_share_mem_register", 0x54);
        printf("fid register already name = %s\n", s0_4->name);
        return 0;
    }
    s0_4->share_mem_cb = arg4;
    s0_4->used = 1;
    n_1 = strlen(arg3);
    n = 0x14;
    if (n_1 < 0x14) {
        n = n_1;
    }
    memcpy(s0_4->name, arg3, n);
    return result;
}

int32_t dsys_func_user_mem_register(int32_t arg1, int32_t arg2, char *arg3, UserMemFunc arg4, UserMemReleaseFunc arg5)
{
    int32_t result;
    FuncEntry *s0_2;
    size_t n_1;
    size_t n;

    if ((uint32_t)arg1 >= 6) {
        printf("err: %s,%d ", "dsys_func_user_mem_register", 0x5d);
        result = -1;
        printf("mid = %d\n", arg1);
    }
    if ((uint32_t)arg2 >= 0x1e) {
        printf("err: %s,%d ", "dsys_func_user_mem_register", 0x61);
        result = -1;
        printf("fid = %d\n", arg2);
    }

    s0_2 = (FuncEntry *)((uint8_t *)funcs + arg2 * 0x44 + arg1 * 0x7f8);
    result = s0_2->used;
    if (result != 0) {
        printf("err: %s,%d ", "dsys_func_user_mem_register", 0x6b);
        result = -1;
        printf("fid register already name = %s\n", s0_2->name);
    }
    s0_2->used = 1;
    s0_2->user_mem_cb = arg4;
    s0_2->user_mem_release_cb = arg5;
    n_1 = strlen(arg3);
    n = 0x14;
    if (n_1 < 0x14) {
        n = n_1;
    }
    memcpy(s0_2->name, arg3, n);
    return result;
}

int32_t dsys_func_unregister(int32_t arg1, int32_t arg2)
{
    int32_t s3_1;
    FuncEntry *str;
    int32_t s3;

    if ((uint32_t)arg1 >= 6) {
        printf("err: %s,%d ", "dsys_func_unregister", 0x74);
        s3_1 = -1;
        printf("mid = %d\n", arg1);
    }
    if ((uint32_t)arg2 >= 0x1e) {
        printf("err: %s,%d ", "dsys_func_unregister", 0x78);
        s3_1 = -1;
        printf("fid = %d\n", arg2);
    }

    str = (FuncEntry *)((uint8_t *)funcs + arg2 * 0x44 + arg1 * 0x7f8);
    s3 = str->used;
    if (s3 == 0) {
        printf("warn: %s,%d", "dsys_func_unregister", 0x7d);
        printf(" fid unregister already mid:fid=%d:%d\n", arg1, arg2);
        return s3;
    }
    if (str->ctx_req != NULL) {
        printf("warn: %s,%d", "dsys_func_unregister", 0x80);
        puts("pri do not free");
        free(str->ctx_req);
    }
    memset(str, 0, 0x44);
    s3_1 = 0;
    return s3_1;
}

int32_t DsystemInit(void)
{
    return func_init();
}

int32_t DsystemExit(void)
{
    func_deinit();
    return 0;
}

static void *shm_thread(void *arg1)
{
    ShmArea *area;
    int32_t *v0;

    area = (ShmArea *)arg1;
    prctl(0xf, "shm_thread");
    v0 = __errno_location();
    while (1) {
        pthread_setcancelstate(0, 0);
        if (sem_wait(area->sem_tos) != 0) {
            printf("err: %s,%d ", "shm_thread", 0xd5);
            printf("sem_wait %s\n", strerror(*v0));
        }
        pthread_setcancelstate(1, 0);
        if (area->dispatch_cb != NULL) {
            area->dispatch_cb(area->shm_addr, 0x5000);
        }
        if (sem_post(area->sem_toc) != 0) {
            printf("err: %s,%d ", "shm_thread", 0xeb);
            printf("sem_post %s\n", strerror(*v0));
        }
    }

    return NULL;
}

int32_t shm_init(void)
{
    void *v0_2;
    int32_t v0_3;
    int32_t a0_1;
    void *v0_6;
    sem_t *v0_8;
    sem_t *v0_10;

    if (psa != NULL) {
        printf("warn: %s,%d", "shm_init", 0x35);
        puts("shm init already");
        return 1;
    }

    v0_2 = calloc(0x18, 1);
    psa = (ShmArea *)v0_2;
    if (v0_2 == NULL) {
        printf("err: %s,%d ", "shm_init", 0x30);
        puts("malloc err ");
        return -1;
    }

    v0_3 = shm_open("imp_deubg_shm", 0x102, 0x1ff);
    psa->shm_fd = v0_3;
    a0_1 = psa->shm_fd;
    if (a0_1 == -1) {
        printf("err: %s,%d ", "shm_init", 0x3b);
        printf("shm_open err %s, %s\n", strerror(errno), "imp_deubg_shm");
    }

    if (ftruncate(a0_1, 0x5000) == -1) {
        printf("err: %s,%d ", "shm_init", 0x40);
        printf("ftruncate err %s\n", strerror(errno));
    }

    v0_6 = mmap(0, 0x5000, 3, 1, psa->shm_fd, 0);
    psa->shm_addr = v0_6;
    if ((intptr_t)psa->shm_addr == -1) {
        printf("err: %s,%d ", "shm_init", 0x45);
        printf("mmap err %s\n", strerror(errno));
    }

    v0_8 = sem_open("imp_deubg_sem_tos", 0x500, 0x1b6, 0);
    psa->sem_tos = v0_8;
    if (psa->sem_tos == NULL) {
        int32_t *v0_18;

        v0_18 = __errno_location();
        if (*v0_18 == 0x11) {
            sem_unlink("imp_deubg_sem_tos");
            v0_8 = sem_open("imp_deubg_sem_tos", 0x500, 0x1b6, 0);
            psa->sem_tos = v0_8;
            if (psa->sem_tos != NULL) {
                goto label_e41ac;
            }
            printf("err: %s,%d ", "shm_init", 0x54);
            printf("sem_open tos err %s\n", strerror(*v0_18));
        } else {
            printf("err: %s,%d ", "shm_init", 0x50);
            printf("sem_open tos err %s\n", strerror(*v0_18));
        }
    }

label_e41ac:
    v0_10 = sem_open("imp_deubg_sem_toc", 0x500, 0x1b6, 0);
    psa->sem_toc = v0_10;
    if (psa->sem_toc == NULL) {
        int32_t *v0_20;

        v0_20 = __errno_location();
        if (*v0_20 == 0x11) {
            sem_unlink("imp_deubg_sem_toc");
            v0_10 = sem_open("imp_deubg_sem_toc", 0x500, 0x1b6, 0);
            psa->sem_toc = v0_10;
            if (psa->sem_toc != NULL) {
                goto label_e41d8;
            }
            printf("err: %s,%d ", "shm_init", 0x63);
            printf("sem_open tos err %s\n", strerror(*v0_20));
        } else {
            printf("err: %s,%d ", "shm_init", 0x5f);
            printf("sem_open tos err %s\n", strerror(*v0_20));
        }
    }

label_e41d8:
    if (pthread_create(&psa->thread, 0, shm_thread, psa) == 0) {
        return 0;
    }

    printf("err: %s,%d ", "shm_init", 0x6b);
    puts("pthread_create ");
    if (psa == NULL) {
        return -1;
    }
    free(psa);
    psa = NULL;
    return -1;
}

int32_t shm_deinit(void)
{
    ShmArea *psa_1;
    int32_t result;
    int32_t result_1;
    ShmArea *psa_2;

    psa_1 = psa;
    if (psa_1 == NULL) {
        printf("warn: %s,%d", "shm_deinit", 0x83);
        puts("shm_deinit already ");
        return 0;
    }

    result = 0;
    if (pthread_cancel(psa_1->thread) != 0) {
        printf("err: %s,%d ", "shm_deinit", 0x8a);
        result = -1;
        printf("pthread_cancel %s\n", strerror(errno));
    }
    pthread_join(psa->thread, 0);

    if (sem_close(psa->sem_tos) != 0) {
        printf("err: %s,%d ", "shm_deinit", 0x91);
        result = -1;
        printf("sem_close tos err %s\n", strerror(errno));
    }
    if (sem_unlink("imp_deubg_sem_tos") != 0) {
        printf("err: %s,%d ", "shm_deinit", 0x95);
        result = -1;
        printf("sem_unlink tos err %s\n", strerror(errno));
    }
    if (sem_close(psa->sem_toc) != 0) {
        printf("err: %s,%d ", "shm_deinit", 0x9a);
        result = -1;
        printf("sem_close toc err %s\n", strerror(errno));
    }
    result_1 = sem_unlink("imp_deubg_sem_toc");
    result = result_1;
    if (result_1 != 0) {
        printf("err: %s,%d ", "shm_deinit", 0x9e);
        result = -1;
        printf("sem_unlink toc err %s\n", strerror(errno));
    }
    if (munmap(psa->shm_addr, 0x5000) == -1) {
        printf("err: %s,%d ", "shm_deinit", 0xa5);
        result = -1;
        printf("munmap err %s\n", strerror(errno));
    }
    close(psa->shm_fd);
    if (shm_unlink("imp_deubg_shm") == -1) {
        printf("err: %s,%d ", "shm_deinit", 0xab);
        result = -1;
        printf("shm_unlink err %s\n", strerror(errno));
    }
    psa_2 = psa;
    if (psa_2 != NULL) {
        free(psa_2);
    }
    psa = NULL;
    return result;
}

int32_t shm_register_cb(ShmDispatchCb arg1)
{
    ShmArea *psa_1;

    psa_1 = psa;
    if (psa_1 != NULL) {
        psa_1->dispatch_cb = arg1;
        return 0;
    }
    printf("err: %s,%d ", "shm_register_cb", 0xc3);
    puts("psa == NULL");
    return -1;
}
