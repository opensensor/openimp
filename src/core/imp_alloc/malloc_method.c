#include <stdint.h>
#include <stdlib.h>

#include "core/imp_alloc.h"

int IMP_Log_Get_Option(void);
void imp_log_fun(int level, int option, int type, ...);
extern char _gp;

int32_t alloc_malloc_init(void *arg1)
{
    int32_t v0;

    if (arg1 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Malloc Method",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/malloc_method.c", 0x1b,
            "alloc_malloc_init", "%s function param is NULL\n", "alloc_malloc_init");
        return -1;
    }

    v0 = (int32_t)(intptr_t)valloc(0x2000000);
    *(int32_t *)((char *)arg1 + 0x5c) = v0; /* offset 0x5c */
    if (v0 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Malloc Method",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/malloc_method.c", 0x23,
            "alloc_malloc_init", "pmem mmap failed\n");
        return -1;
    }

    *(int32_t *)((char *)arg1 + 0x64) = 0x2000000; /* offset 0x64 */
    *(int32_t *)((char *)arg1 + 0x60) = 0; /* offset 0x60 */
    imp_log_fun(3, IMP_Log_Get_Option(), 2, "Malloc Method",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/malloc_method.c", 0x30,
        "alloc_malloc_init",
        "alloc->mem_alloc.method = %s\n \t\t\talloc->mem_alloc.vaddr = 0x%08x\n \t\t\talloc->mem_alloc.paddr = 0x%08x\n \t\t\talloc->mem_alloc.length = %d\n",
        (char *)arg1 + 0x3c, *(int32_t *)((char *)arg1 + 0x5c),
        *(int32_t *)((char *)arg1 + 0x60), *(int32_t *)((char *)arg1 + 0x64), &_gp);
    return 0;
}

int32_t alloc_malloc_get_paddr(void)
{
    imp_log_fun(6, IMP_Log_Get_Option(), 2, "Malloc Method",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/malloc_method.c", 0x37,
        "alloc_malloc_get_paddr", "alloc_malloc_get_paddr is not support\n", &_gp);
    return 0;
}
