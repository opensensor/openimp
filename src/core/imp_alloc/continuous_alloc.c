#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ContinuousInfo {
    uint32_t prev;
    uint32_t next;
    uint32_t addr;
    int32_t size;
    int32_t used;
} ContinuousInfo;

extern char _gp;

int32_t IMP_Log_Get_Option(void);
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...);

/* forward decl, ported by T<N> later */
int32_t buddy_init(void *arg1, int32_t arg2);
/* forward decl, ported by T<N> later */
int32_t buddy_alloc(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t buddy_free(int32_t arg1);

static int32_t continuous_dump_info(void *arg1);
int32_t continuous_dump_info_to_file(void *arg1);
int32_t continuous_init(void *arg1, int32_t arg2);
int32_t continuous_alloc(int32_t arg1);
int32_t continuous_free(int32_t arg1);
int32_t continuous_dump(void);
int32_t continuous_dump_to_file(void);

static ContinuousInfo *head;

static int32_t continuous_dump_info(void *arg1)
{
    ContinuousInfo *s5 = (ContinuousInfo *)arg1;
    int32_t fp = 0;
    int32_t s4 = 0;
    int32_t s3 = 0;
    int32_t var_34 = 0;
    int32_t a0;
    int32_t a1;
    int32_t s5_1;
    int32_t s2_1;
    uint64_t used_size;
    uint64_t total_size;
    uint64_t usage_rate;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, "Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0x84,
        "continuous_dump_info", "\n================ continuous dump info ============\n");

    while (1) {
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Continuous Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0x86,
            "continuous_dump_info", "\ninfo->addr = 0x%08x\n", s5->addr);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Continuous Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0x87,
            "continuous_dump_info", "info->size = %d\n", s5->size);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Continuous Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0x88,
            "continuous_dump_info", "info->used = %d\n", s5->used);
        if (s5->used != 1) {
            int32_t v0_1 = s5->size;

            s5 = (ContinuousInfo *)(uintptr_t)s5->next;
            a0 = fp + v0_1;
            a1 = ((uint32_t)a0 < (uint32_t)fp) ? 1 : 0;
            s4 = a1 + s4 + (v0_1 >> 0x1f);
            fp = a0;
            if ((ContinuousInfo *)arg1 == s5) {
                break;
            }
        } else {
            int32_t v0_8 = s5->size;

            s5 = (ContinuousInfo *)(uintptr_t)s5->next;
            a0 = s3 + v0_8;
            a1 = ((uint32_t)a0 < (uint32_t)s3) ? 1 : 0;
            var_34 = a1 + var_34 + (v0_8 >> 0x1f);
            s3 = a0;
            if ((ContinuousInfo *)arg1 == s5) {
                break;
            }
        }
    }

    s5_1 = s3 + fp;
    s2_1 = (((uint32_t)s5_1 < (uint32_t)s3) ? 1 : 0) + var_34 + s4;
    imp_log_fun(4, IMP_Log_Get_Option(), 2, "Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0x94,
        "continuous_dump_info", "\nMem Total Size: %d\n", s5_1, s2_1, &_gp);
    imp_log_fun(4, IMP_Log_Get_Option(), 2, "Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0x95,
        "continuous_dump_info", "Mem Used  Size: %d\n", s3, var_34);
    imp_log_fun(4, IMP_Log_Get_Option(), 2, "Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0x96,
        "continuous_dump_info", "Mem Free  Size: %d\n", fp, s4);

    used_size = (uint64_t)(uint32_t)s3 | ((uint64_t)(uint32_t)var_34 << 32);
    total_size = (uint64_t)(uint32_t)s5_1 | ((uint64_t)(uint32_t)s2_1 << 32);
    usage_rate = (used_size * 100ULL) / total_size;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, "Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0x97,
        "continuous_dump_info", "Mem Usage Rate: %lld%%\n",
        (long long)usage_rate);
    return imp_log_fun(4, IMP_Log_Get_Option(), 2, "Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0x98,
        "continuous_dump_info",
        "\n=============== continuous dump info End ============\n");
}

int32_t continuous_dump_info_to_file(void *arg1)
{
    char str[0x200];
    FILE *stream;
    ContinuousInfo *s6;
    int32_t fp;
    int32_t s2;
    const char *var_38;
    int32_t s1;
    int32_t var_30;
    const char *var_34;
    int32_t s7;
    int32_t s6_2;
    int32_t v0_10;
    int32_t s5_2;
    int32_t s1_4;
    uint64_t used_size;
    uint64_t total_size;
    uint64_t usage_rate;
    int32_t v0_15;

    memset(str, 0, 0x1fc);
    stream = fopen("/tmp/continuous_mem_info", "w");
    if (stream == NULL) {
        return imp_log_fun(6, IMP_Log_Get_Option(), 2, "Continuous Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0xa5,
            "continuous_dump_info_to_file", "Open /tmp/continuous_mem_info failed\n", &_gp);
    }

    memcpy(str, "\n================ continuous dump info ============\n", 0x34);
    fwrite(str, 0x34, 1, stream);

    s6 = (ContinuousInfo *)arg1;
    fp = 0;
    s2 = 0;
    var_38 = "\ninfo->addr = 0x%08x\n";
    s1 = 0;
    var_30 = 0;
    var_34 = "info->used = %d\n";

    while (1) {
        int32_t s3_2;

        memset(str, 0, 0x200);
        s3_2 = sprintf(str, var_38, s6->addr);
        s3_2 += sprintf(&str[s3_2], "info->size = %d\n", s6->size);
        fwrite(str, s3_2 + sprintf(&str[s3_2], var_34, s6->used), 1, stream);

        if (s6->used != 1) {
            int32_t v0_1 = s6->size;
            int32_t a0_3;
            int32_t a1_2;

            s6 = (ContinuousInfo *)(uintptr_t)s6->next;
            a0_3 = fp + v0_1;
            a1_2 = ((uint32_t)a0_3 < (uint32_t)fp) ? 1 : 0;
            fp = a0_3;
            s2 = a1_2 + s2 + (v0_1 >> 0x1f);
            if ((ContinuousInfo *)arg1 == s6) {
                break;
            }
        } else {
            int32_t v0_6 = s6->size;
            int32_t a0_10;
            int32_t v0_8;

            s6 = (ContinuousInfo *)(uintptr_t)s6->next;
            a0_10 = s1 + v0_6;
            v0_8 = ((uint32_t)a0_10 < (uint32_t)s1) ? 1 : 0;
            s1 = a0_10;
            var_30 = v0_8 + var_30 + (v0_6 >> 0x1f);
            if ((ContinuousInfo *)arg1 == s6) {
                break;
            }
        }
    }

    memset(str, 0, 0x200);
    s7 = s1 + fp;
    s6_2 = (((uint32_t)s7 < (uint32_t)s1) ? 1 : 0) + var_30 + s2;
    v0_10 = sprintf(str, "\nMem Total Size: %d\n", s7);
    s5_2 = v0_10 + sprintf(&str[v0_10], "Mem Used  Size: %d\n", s1);
    s1_4 = s5_2 + sprintf(&str[s5_2], "Mem Free  Size: %d\n", fp);

    used_size = (uint64_t)(uint32_t)s1 | ((uint64_t)(uint32_t)var_30 << 32);
    total_size = (uint64_t)(uint32_t)s7 | ((uint64_t)(uint32_t)s6_2 << 32);
    usage_rate = (used_size * 100ULL) / total_size;
    v0_15 = s1_4 + sprintf(&str[s1_4], "Mem Usage Rate: %lld%%\n",
        (long long)usage_rate);
    memcpy(&str[v0_15], "\n=============== continuous dump info End ============\n", 0x37);
    fwrite(str, v0_15 + 0x37, 1, stream);
    return fclose(stream);
}

int32_t continuous_init(void *arg1, int32_t arg2)
{
    int32_t var_2c;
    const char *var_24;
    const char *var_20 = NULL;
    int32_t v0_6;
    const char *var_30_1;

    if (arg1 == NULL) {
        v0_6 = IMP_Log_Get_Option();
        var_24 = "Mem Manager mode init error, base addr is NULL\n";
        var_2c = 0xef;
        var_30_1 = "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c";
        goto label_2605c;
    }

    if (arg2 < 0x100) {
        v0_6 = IMP_Log_Get_Option();
        var_24 = "Mem Manager mode init error, continuous size is too small\n";
        var_2c = 0xf4;
        var_30_1 = "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c";
        goto label_2605c;
    }

    memset(arg1, 0, (size_t)arg2);
    if (head != NULL) {
        return 0;
    }

    {
        ContinuousInfo *v0_3 = (ContinuousInfo *)malloc(0x14);

        head = v0_3;
        if (v0_3 != NULL) {
            v0_3->prev = (uint32_t)(uintptr_t)v0_3;
            v0_3->next = (uint32_t)(uintptr_t)v0_3;
            v0_3->addr = (uint32_t)(uintptr_t)arg1;
            v0_3->size = arg2;
            v0_3->used = 0;
            return 0;
        }
    }

    var_20 = "continuous_init_list";
    imp_log_fun(6, IMP_Log_Get_Option(), 2, "Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0xce,
        "continuous_init_list", "%s malloc error\n", "continuous_init_list");
    imp_log_fun(6, IMP_Log_Get_Option(), 2, "Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0xe1,
        "continuous_base_init", "continuous init list error\n");
    v0_6 = IMP_Log_Get_Option();
    var_24 = "continuous base init error\n";
    var_2c = 0xfa;
    var_30_1 = "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c";

label_2605c:
    imp_log_fun(6, v0_6, 2, "Continuous Alloc", var_30_1, var_2c,
        "continuous_init", var_24, var_20);
    return -1;
}

int32_t continuous_alloc(int32_t arg1)
{
    ContinuousInfo *head_1 = head;
    ContinuousInfo *head_2;
    int32_t s3_1;
    int32_t var_34 = 0;
    int32_t var_30 = 0;

    if (head_1 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Continuous Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0x106,
            "continuous_alloc", "continuous memery manager no inited\n");
        return 0;
    }

    if (arg1 <= 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Continuous Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0x10b,
            "continuous_alloc", "continuous alloc size(%d) error\n", arg1);
        return 0;
    }

    s3_1 = (arg1 + 0xff) & 0xffffff00;
    head_2 = head_1;

    while (1) {
        if (head_2->used == 0) {
            int32_t s2_1 = head_2->size;

            if (s2_1 >= s3_1) {
                if (s3_1 != s2_1) {
                    ContinuousInfo *v0_6 = (ContinuousInfo *)malloc(0x14);

                    if (v0_6 == NULL) {
                        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Continuous Alloc",
                            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c",
                            0x40, "continuous_request_block", "%s malloc info error\n",
                            "continuous_request_block");
                        break;
                    }

                    v0_6->next = head_2->next;
                    v0_6->prev = (uint32_t)(uintptr_t)head_2;
                    v0_6->addr = head_2->addr + (uint32_t)s3_1;
                    v0_6->size = s2_1 - s3_1;
                    v0_6->used = 0;
                    ((ContinuousInfo *)(uintptr_t)v0_6->next)->prev = (uint32_t)(uintptr_t)v0_6;
                    head_2->next = (uint32_t)(uintptr_t)v0_6;
                    head_2->size = s3_1;
                }

                head_2->used = 1;
                return (int32_t)head_2->addr;
            }
        }

        head_2 = (ContinuousInfo *)(uintptr_t)head_2->next;
        if (head_1 == head_2) {
            int32_t v0_3 = IMP_Log_Get_Option();

            var_30 = head_1->size;
            var_34 = (int32_t)head_1->addr;
            imp_log_fun(6, v0_3, 2, "Continuous Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0x58,
                "continuous_request_block", "space isn't enough, t_info:used=%d,addr=0x%x,size=%d\n",
                head_1->used, var_34, var_30);
            break;
        }
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0x111,
        "continuous_alloc", "Can't alloc memery: %d\n", arg1, var_34, var_30);
    continuous_dump_info(head);
    return 0;
}

int32_t continuous_free(int32_t arg1)
{
    ContinuousInfo *head_1 = head;
    ContinuousInfo *head_2 = head_1;

    if (head_1 == NULL) {
        return imp_log_fun(6, IMP_Log_Get_Option(), 2, "Continuous Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0x11e,
            "continuous_free", "continuous memery manager no inited\n", &_gp);
    }

    do {
        if (head_2->used == 1 && arg1 == (int32_t)head_2->addr) {
            ContinuousInfo *a0 = (ContinuousInfo *)(uintptr_t)head_2->next;

            head_2->used = 0;
            if (a0->used == 0 && head_1 != a0) {
                int32_t a1_1 = a0->size;
                int32_t v0_4 = head_2->size;

                head_2->next = a0->next;
                ((ContinuousInfo *)(uintptr_t)a0->next)->prev = (uint32_t)(uintptr_t)head_2;
                head_2->size = v0_4 + a1_1;
                free(a0);
            }

            {
                ContinuousInfo *result = (ContinuousInfo *)(uintptr_t)head_2->prev;

                if (result->used != 0 || head_1 == head_2) {
                    return (int32_t)(intptr_t)result;
                }

                {
                    int32_t a0_1 = head_2->size;
                    int32_t v1_4 = result->size;
                    ContinuousInfo *a1_3 = (ContinuousInfo *)(uintptr_t)head_2->next;

                    result->next = head_2->next;
                    result->size = v1_4 + a0_1;
                    a1_3->prev = (uint32_t)(uintptr_t)result;
                    free(head_2);
                    return (int32_t)(intptr_t)result;
                }
            }
        }

        head_2 = (ContinuousInfo *)(uintptr_t)head_2->next;
    } while (head_1 != head_2);

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "Continuous Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/continuous_alloc.c", 0x124,
        "continuous_free", "Can't free memery\n", &_gp);
    return continuous_dump_info(head);
}

int32_t continuous_dump(void)
{
    ContinuousInfo *head_1 = head;

    if (head_1 == NULL) {
        return 0x120000;
    }

    return continuous_dump_info(head_1);
}

int32_t continuous_dump_to_file(void)
{
    ContinuousInfo *head_1 = head;

    if (head_1 == NULL) {
        return 0x120000;
    }

    return continuous_dump_info_to_file(head_1);
}
