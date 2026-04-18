#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static uint32_t pmem_length = 0;
static uint32_t pmem_paddr = 0;
static uint32_t pmem_vaddr = 0;

int IMP_Log_Get_Option(void);
void imp_log_fun(int level, int option, int type, ...);

int32_t memparse(char *arg1, void **arg2)
{
    char *var_10;
    int32_t result;

    result = (int32_t)strtoull(arg1, &var_10, 0);
    switch ((unsigned char)(*var_10 - 0x47)) {
    case 0:
    case 0x20:
        result <<= 0xa;
        /* label_27588 */
        result <<= 0x14;
        var_10 = &var_10[1];
        break;
    case 4:
    case 0x24:
        result <<= 0xa;
        var_10 = &var_10[1];
        break;
    case 6:
    case 0x26:
        result <<= 0x14;
        var_10 = &var_10[1];
        break;
    default:
        break;
    }

    if (arg2 != 0) {
        *arg2 = var_10;
    }

    return result;
}

int32_t get_pmem_size(void)
{
    int32_t var_14 = 0;
    int32_t result = 0;
    char haystack[0x200];
    FILE *stream;
    const char *var_22c_1;
    int32_t v0_6;
    int32_t v1_2;

    memset(haystack, 0, 0x1fc);
    stream = fopen("/proc/cmdline", "r");
    if (stream == 0) {
        v0_6 = IMP_Log_Get_Option();
        var_22c_1 = "%s open file (%s) error\n";
        v1_2 = 0x28;
    } else if ((int32_t)fread(haystack, 1, 0x200, stream) <= 0) {
        v0_6 = IMP_Log_Get_Option();
        var_22c_1 = "%s fread (%s) error\n";
        v1_2 = 0x2e;
    } else {
        fclose(stream);
        char *v0_1 = strstr(haystack, "pmem");
        if (v0_1 != 0) {
            sscanf(v0_1, "pmem=%dM@%x", &result, &var_14);
            result <<= 0x14;
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "PMEM Method",
                        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/pmem_method.c",
                        0x3c, "get_pmem_size",
                        "CMD Line Pmem Size:%d, Addr:0x%08x\n", result, var_14);
            return result;
        }
        v0_6 = IMP_Log_Get_Option();
        var_22c_1 = "%s fread (%s) error\n";
        v1_2 = 0x36;
    }

    imp_log_fun(6, v0_6, 2, "PMEM Method",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/pmem_method.c",
                v1_2, "get_pmem_size", var_22c_1, "get_pmem_size",
                "/proc/cmdline");
    return -1;
}

int32_t alloc_pmem_init(void *arg1)
{
    const char *var_34_2;
    intptr_t var_30_2;
    int32_t v0_8;
    int32_t v1_4;

    if (arg1 == 0) {
        v0_8 = IMP_Log_Get_Option();
        var_30_2 = (intptr_t)"alloc_pmem_init";
        var_34_2 = "%s function param is NULL\n";
        v1_4 = 0x48;
    } else {
        int32_t v0 = get_pmem_size();
        if (v0 <= 0) {
            v0_8 = IMP_Log_Get_Option();
            var_30_2 = v0;
            var_34_2 = "get pmem size %d failed\n";
            v1_4 = 0x4e;
        } else {
            int32_t v0_1 = open("/dev/pmem", 2);
            if (v0_1 > 0) {
                int32_t v0_2 =
                    (int32_t)(intptr_t)mmap(0, (size_t)v0, 3, 1, v0_1, 0);
                *(int32_t *)((char *)arg1 + 0x5c) = v0_2; /* offset 0x5c */
                if (v0_2 == 0) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "PMEM Method",
                                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/pmem_method.c",
                                0x5d, "alloc_pmem_init", "pmem mmap failed\n");
                    return -1;
                }

                int32_t result_2 = ioctl(v0_1, 0x80047003,
                                         (char *)arg1 + 0x64);
                int32_t result = result_2;
                const char *var_34_1;
                int32_t result_1;
                int32_t v0_5;
                int32_t v1_3;

                if (result_2 < 0) {
                    v0_5 = IMP_Log_Get_Option();
                    result_1 = result;
                    var_34_1 = "pmem get size error (ret = %d)\n";
                    v1_3 = 0x63;
                } else {
                    int32_t result_3 = ioctl(v0_1, 0x80047001,
                                             (char *)arg1 + 0x60);
                    result = result_3;
                    if (result_3 >= 0) {
                        close(v0_1);
                        uint32_t a0_3 =
                            *(uint32_t *)((char *)arg1 + 0x60); /* offset 0x60 */
                        uint32_t v1_1 =
                            *(uint32_t *)((char *)arg1 + 0x64); /* offset 0x64 */
                        pmem_vaddr =
                            *(uint32_t *)((char *)arg1 + 0x5c); /* offset 0x5c */
                        pmem_paddr = a0_3;
                        pmem_length = v1_1;
                        imp_log_fun(3, IMP_Log_Get_Option(), 2, "PMEM Method",
                                    "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/pmem_method.c",
                                    0x7a, "alloc_pmem_init",
                                    "alloc->mem_alloc.method = %s\n \t\t\talloc->mem_alloc.vaddr = 0x%08x\n \t\t\talloc->mem_alloc.paddr = 0x%08x\n \t\t\talloc->mem_alloc.length = %d\n",
                                    (char *)arg1 + 0x3c, /* offset 0x3c */
                                    *(uint32_t *)((char *)arg1 + 0x5c),
                                    *(uint32_t *)((char *)arg1 + 0x60),
                                    *(uint32_t *)((char *)arg1 + 0x64));
                        return 0;
                    }

                    v0_5 = IMP_Log_Get_Option();
                    result_1 = result;
                    var_34_1 = "pmem get phys address error (ret=%d)\n";
                    v1_3 = 0x69;
                }

                imp_log_fun(6, v0_5, 2, "PMEM Method",
                            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/pmem_method.c",
                            v1_3, "alloc_pmem_init", var_34_1, result_1);
                return result;
            }

            v0_8 = IMP_Log_Get_Option();
            var_30_2 = (intptr_t)"/dev/pmem";
            var_34_2 = "open %s failed\n";
            v1_4 = 0x54;
        }
    }

    imp_log_fun(6, v0_8, 2, "PMEM Method",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/pmem_method.c",
                v1_4, "alloc_pmem_init", var_34_2, var_30_2);
    return -1;
}

int32_t alloc_pmem_get_paddr(int32_t arg1)
{
    uint32_t pmem_vaddr_1 = pmem_vaddr;

    if ((uint32_t)arg1 >= pmem_vaddr_1 &&
        pmem_vaddr_1 + pmem_length >= (uint32_t)arg1) {
        return (int32_t)(pmem_paddr - pmem_vaddr_1 + (uint32_t)arg1);
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "PMEM Method",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/pmem_method.c",
                0x82, "alloc_pmem_get_paddr", "vaddr input error\n");
    return 0;
}

int32_t alloc_pmem_get_vaddr(int32_t arg1)
{
    uint32_t pmem_paddr_1 = pmem_paddr;

    if ((uint32_t)arg1 >= pmem_paddr_1 &&
        pmem_paddr_1 + pmem_length >= (uint32_t)arg1) {
        return (int32_t)(pmem_vaddr - pmem_paddr_1 + (uint32_t)arg1);
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "PMEM Method",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/pmem_method.c",
                0x8c, "alloc_pmem_get_vaddr", "paddr input error\n");
    return 0;
}
