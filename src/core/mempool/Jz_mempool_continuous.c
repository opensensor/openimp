#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#endif

typedef struct ContinuousInfo {
    uint32_t prev;
    uint32_t next;
    int32_t addr;
    int32_t size;
    int32_t used;
    char name[0x40];
} ContinuousInfo;

typedef struct MempoolContinuousInfo {
    ContinuousInfo info;
    char pool_name[0x40];
    int32_t alloc_count;
} MempoolContinuousInfo;

#if UINTPTR_MAX == 0xffffffffu
_Static_assert(sizeof(ContinuousInfo) == 0x54, "ContinuousInfo size");
_Static_assert(sizeof(MempoolContinuousInfo) == 0x98, "MempoolContinuousInfo size");
#endif

extern char _gp;

int32_t IMP_Log_Get_Option(void);
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...);

static int32_t continuous_dump_info(void *arg1);
int32_t mempool_continuous_dump_info_to_file(void *arg1, char *arg2);
void *mempool_continuous_init(void *arg1, size_t arg2, char *arg3);
int32_t mempool_continuous_alloc(void *arg1, int32_t arg2, char *arg3);
int32_t mempool_continuous_free(void *arg1, int32_t arg2);
int32_t mempool_continuous_dump(void *arg1);
int32_t mempool_continuous_dump_to_file(void *arg1);
int32_t mempool_continuous_deinit(void *arg1);

static int32_t continuous_dump_info(void *arg1)
{
    ContinuousInfo *s5 = (ContinuousInfo *)arg1;
    int32_t fp = 0;
    int32_t s4 = 0;
    int32_t s3 = 0;
    int32_t var_34 = 0;
    int32_t a0;
    int32_t a1_1;
    int32_t s5_1;
    int32_t s2_1;
    uint64_t used_size;
    uint64_t total_size;
    uint64_t usage_rate;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0x85,
        "continuous_dump_info", "\n================ continuous dump info ============\n");

    while (1) {
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0x87,
            "continuous_dump_info", "\ninfo->addr = 0x%08x ", s5->addr);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0x88,
            "continuous_dump_info", "info->size = %d ", s5->size);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0x89,
            "continuous_dump_info", "info->used = %d ", s5->used);
        if (s5->used != 1) {
            int32_t v0_1 = s5->size;

            s5 = (ContinuousInfo *)(uintptr_t)s5->next;
            a0 = fp + v0_1;
            a1_1 = ((uint32_t)a0 < (uint32_t)fp) ? 1 : 0;
            s4 = a1_1 + s4 + (v0_1 >> 0x1f);
            fp = a0;
            if ((ContinuousInfo *)arg1 == s5) {
                break;
            }
        } else {
            int32_t v0_8 = s5->size;

            s5 = (ContinuousInfo *)(uintptr_t)s5->next;
            a0 = s3 + v0_8;
            a1_1 = ((uint32_t)a0 < (uint32_t)s3) ? 1 : 0;
            var_34 = a1_1 + var_34 + (v0_8 >> 0x1f);
            s3 = a0;
            if ((ContinuousInfo *)arg1 == s5) {
                break;
            }
        }
    }

    s5_1 = s3 + fp;
    s2_1 = (((uint32_t)s5_1 < (uint32_t)s3) ? 1 : 0) + var_34 + s4;
    imp_log_fun(4, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0x95,
        "continuous_dump_info", "\nMem Total Size: %d\n", s5_1, s2_1, &_gp);
    imp_log_fun(4, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0x96,
        "continuous_dump_info", "Mem Used  Size: %d\n", s3, var_34);
    imp_log_fun(4, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0x97,
        "continuous_dump_info", "Mem Free  Size: %d\n", fp, s4);

    used_size = (uint64_t)(uint32_t)s3 | ((uint64_t)(uint32_t)var_34 << 32);
    total_size = (uint64_t)(uint32_t)s5_1 | ((uint64_t)(uint32_t)s2_1 << 32);
    usage_rate = (used_size * 100ULL) / total_size;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0x98,
        "continuous_dump_info", "Mem Usage Rate: %lld%%\n", (long long)usage_rate);
    return imp_log_fun(4, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0x99,
        "continuous_dump_info", "\n=============== continuous dump info End ============\n");
}

int32_t mempool_continuous_dump_info_to_file(void *arg1, char *arg2)
{
    char filename[0x40];
    char str[0x200];
    FILE *stream;
    ContinuousInfo *s5;
    int32_t s7;
    const char *var_38;
    const char *var_34;
    int32_t s2;
    int32_t s1;
    int32_t var_2c;
    const char *var_30;
    int32_t s6_4;
    int32_t s5_2;
    int32_t v0_12;
    int32_t s4_2;
    int32_t s1_4;
    int32_t v0_17;
    uint64_t used_size;
    uint64_t total_size;
    uint64_t usage_rate;

    strcpy(filename, "/tmp/mempool-");
    memset(filename + 0xe, 0, 0x32);
    strcpy(filename + 0x14, arg2);

    memset(str, 0, 0x1fc);
    stream = fopen(filename, "w");
    if (stream == NULL) {
        return imp_log_fun(6, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0xb0,
            "mempool_continuous_dump_info_to_file", "Open %s failed\n", filename);
    }

    memcpy(str, "\n================ continuous dump info ============\n", 0x34);
    fwrite(str, 0x34, 1, stream);

    s5 = (ContinuousInfo *)arg1;
    s7 = 0;
    var_38 = "begin addr = 0x%08x ";
    var_34 = "size = %d ";
    s2 = 0;
    s1 = 0;
    var_2c = 0;
    var_30 = "used = %d";

    while (1) {
        int32_t v0_3;
        int32_t s6_2;
        int32_t s6_3;

        memset(str, 0, 0x200);
        v0_3 = sprintf(str, "\nname = [%s] ", s5->name);
        s6_2 = v0_3 + sprintf(str + v0_3, var_38, s5->addr);
        s6_3 = s6_2 + sprintf(str + s6_2, var_34, s5->size);
        fwrite(str, s6_3 + sprintf(str + s6_3, var_30, s5->used), 1, stream);

        if (s5->used != 1) {
            int32_t v0_1 = s5->size;
            int32_t a0_6;
            int32_t a1_2;

            s5 = (ContinuousInfo *)(uintptr_t)s5->next;
            a0_6 = s7 + v0_1;
            a1_2 = ((uint32_t)a0_6 < (uint32_t)s7) ? 1 : 0;
            s7 = a0_6;
            s2 = a1_2 + s2 + (v0_1 >> 0x1f);
            if ((ContinuousInfo *)arg1 == s5) {
                break;
            }
        } else {
            int32_t v0_7 = s5->size;
            int32_t a0_14;
            int32_t v0_10;

            s5 = (ContinuousInfo *)(uintptr_t)s5->next;
            a0_14 = s1 + v0_7;
            v0_10 = ((uint32_t)a0_14 < (uint32_t)s1) ? 1 : 0;
            s1 = a0_14;
            var_2c = v0_10 + var_2c + (v0_7 >> 0x1f);
            if ((ContinuousInfo *)arg1 == s5) {
                break;
            }
        }
    }

    memset(str, 0, 0x200);
    s6_4 = s1 + s7;
    s5_2 = (((uint32_t)s6_4 < (uint32_t)s1) ? 1 : 0) + var_2c + s2;
    v0_12 = sprintf(str, "\nMem Total Size: %d\n", s6_4);
    s4_2 = v0_12 + sprintf(str + v0_12, "Mem Used  Size: %d\n", s1);
    s1_4 = s4_2 + sprintf(str + s4_2, "Mem Free  Size: %d\n", s7);

    used_size = (uint64_t)(uint32_t)s1 | ((uint64_t)(uint32_t)var_2c << 32);
    total_size = (uint64_t)(uint32_t)s6_4 | ((uint64_t)(uint32_t)s5_2 << 32);
    usage_rate = (used_size * 100ULL) / total_size;
    v0_17 = s1_4 + sprintf(str + s1_4, "Mem Usage Rate: %lld%%\n", (long long)usage_rate);
    memcpy(str + v0_17, "\n=============== continuous dump info End ============\n", 0x37);
    fwrite(str, v0_17 + 0x37, 1, stream);
    return fclose(stream);
}

void *mempool_continuous_init(void *arg1, size_t arg2, char *arg3)
{
    int32_t var_34;
    const char *var_2c;
    const char *var_28 = NULL;
    int32_t v0_2;
    const char *var_38;

    if (arg1 == NULL) {
        v0_2 = IMP_Log_Get_Option();
        var_2c = "Mem Manager mode init error, base addr is NULL\n";
        var_34 = 0xfb;
        var_38 = "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c";
        goto label_1a198;
    }

    if ((int32_t)arg2 < 0x100) {
        v0_2 = IMP_Log_Get_Option();
        var_2c = "Mem Manager mode init error, continuous size is too small\n";
        var_34 = 0x100;
        var_38 = "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c";
        goto label_1a198;
    }

    memset(arg1, 0, arg2);
    {
        MempoolContinuousInfo *result = (MempoolContinuousInfo *)malloc(0x98);

        if (result != NULL) {
            size_t n;

            memset(result->pool_name, 0, 0x40);
            result->info.addr = (int32_t)(intptr_t)arg1;
            result->info.size = (int32_t)arg2;
            result->info.prev = (uint32_t)(uintptr_t)&result->info;
            result->info.next = (uint32_t)(uintptr_t)&result->info;
            result->info.used = 0;
            n = strlen(arg3);
            strncpy(result->pool_name, arg3, n);
            memset(result->info.name, 0, 0x40);
            result->alloc_count = 0;
            strncpy(result->info.name, arg3, n);
            return result;
        }
    }

    var_28 = "continuous_init_list";
    imp_log_fun(6, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0xda,
        "continuous_init_list", "%s malloc error\n", "continuous_init_list");
    v0_2 = IMP_Log_Get_Option();
    var_2c = "continuous base init error\n";
    var_34 = 0x107;
    var_38 = "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c";
    (void)var_38;

label_1a198:
    imp_log_fun(6, v0_2, 2, "MEMPOOL Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", var_34,
        "mempool_continuous_init", var_2c, var_28);
    return 0;
}

int32_t mempool_continuous_alloc(void *arg1, int32_t arg2, char *arg3)
{
    int32_t s2_1;
    ContinuousInfo *s0_1;
    int32_t var_34 = 0;
    int32_t var_30 = 0;

    if (arg1 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0x116,
            "mempool_continuous_alloc", "continuous memery manager no inited\n");
        return 0;
    }

    if (arg2 <= 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0x11b,
            "mempool_continuous_alloc", "continuous alloc size(%d) error\n", arg2);
        return 0;
    }

    s2_1 = (arg2 + 0xff) & 0xffffff00;
    s0_1 = (ContinuousInfo *)arg1;

    while (1) {
        if (s0_1->used == 0) {
            int32_t v0_2 = s0_1->size;

            if (v0_2 >= s2_1) {
                if (s2_1 != v0_2) {
                    ContinuousInfo *v0_6 = (ContinuousInfo *)malloc(0x54);

                    if (v0_6 == NULL) {
                        imp_log_fun(6, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
                            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c",
                            0x3f, "continuous_request_block", "%s malloc info error\n",
                            "continuous_request_block");
                        break;
                    }

                    memset(v0_6->name, 0, 0x40);
                    v0_6->next = s0_1->next;
                    v0_6->addr = s0_1->addr + s2_1;
                    v0_6->prev = (uint32_t)(uintptr_t)s0_1;
                    v0_6->size = v0_2 - s2_1;
                    v0_6->used = 0;
                    strncpy(s0_1->name, arg3, strlen(arg3));
                    ((ContinuousInfo *)(uintptr_t)v0_6->next)->prev = (uint32_t)(uintptr_t)v0_6;
                    s0_1->next = (uint32_t)(uintptr_t)v0_6;
                    s0_1->size = s2_1;
                }

                ((MempoolContinuousInfo *)arg1)->alloc_count += 1; /* 0x94 */
                s0_1->used = 1;
                return s0_1->addr;
            }
        }

        s0_1 = (ContinuousInfo *)(uintptr_t)s0_1->next;
        if ((ContinuousInfo *)arg1 == s0_1) {
            int32_t v0_3 = IMP_Log_Get_Option();

            var_30 = ((ContinuousInfo *)arg1)->size;
            var_34 = ((ContinuousInfo *)arg1)->addr;
            imp_log_fun(6, v0_3, 2, "MEMPOOL Continuous Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0x59,
                "continuous_request_block", "space isn't enough, t_info:used=%d,addr=0x%x,size=%d\n",
                ((ContinuousInfo *)arg1)->used, var_34, var_30);
            break;
        }
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0x121,
        "mempool_continuous_alloc", "Can't alloc memery: %d\n", arg2, var_34, var_30);
    continuous_dump_info(arg1);
    return 0;
}

int32_t mempool_continuous_free(void *arg1, int32_t arg2)
{
    ContinuousInfo *s0;

    if (arg1 == NULL) {
        return imp_log_fun(6, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0x131,
            "mempool_continuous_free", "continuous memery manager no inited\n", &_gp);
    }

    s0 = (ContinuousInfo *)arg1;
    do {
        if (s0->used == 1 && arg2 == s0->addr) {
            ContinuousInfo *a0 = (ContinuousInfo *)(uintptr_t)s0->next;

            s0->used = 0;
            if (a0->used == 0 && (ContinuousInfo *)arg1 != a0) {
                int32_t a1 = a0->size;
                int32_t v0_4 = s0->size;

                s0->next = a0->next;
                s0->size = v0_4 + a1;
                ((ContinuousInfo *)(uintptr_t)a0->next)->prev = (uint32_t)(uintptr_t)s0;
                free(a0);
            }

            {
                ContinuousInfo *v0_6 = (ContinuousInfo *)(uintptr_t)s0->prev;

                if (v0_6->used == 0 && (ContinuousInfo *)arg1 != s0) {
                    int32_t a0_1 = s0->size;
                    int32_t v1_4 = v0_6->size;
                    ContinuousInfo *a1_2 = (ContinuousInfo *)(uintptr_t)s0->next;

                    v0_6->next = s0->next;
                    v0_6->size = v1_4 + a0_1;
                    a1_2->prev = (uint32_t)(uintptr_t)v0_6;
                    free(s0);
                }

                ((MempoolContinuousInfo *)arg1)->alloc_count -= 1; /* 0x94 */
                return ((MempoolContinuousInfo *)arg1)->alloc_count;
            }
        }

        s0 = (ContinuousInfo *)(uintptr_t)s0->next;
    } while ((ContinuousInfo *)arg1 != s0);

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "MEMPOOL Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/mempool/Jz_mempool_continuous.c", 0x137,
        "mempool_continuous_free", "Can't free memery\n", &_gp);
    return continuous_dump_info(arg1);
}

int32_t mempool_continuous_dump(void *arg1)
{
    if (arg1 == NULL) {
        return 0;
    }

    return continuous_dump_info(arg1);
}

int32_t mempool_continuous_dump_to_file(void *arg1)
{
    if (arg1 == NULL) {
        return 0;
    }

    return mempool_continuous_dump_info_to_file(arg1, (char *)arg1 + 0x54);
}

int32_t mempool_continuous_deinit(void *arg1)
{
    if (arg1 == NULL || ((MempoolContinuousInfo *)arg1)->alloc_count != 0) {
        return -1;
    }

    free(arg1);
    return 0;
}
