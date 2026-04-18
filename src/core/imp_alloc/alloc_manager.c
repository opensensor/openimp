#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/imp_alloc.h"

int IMP_Log_Get_Option(void);
int32_t imp_log_fun(int level, int option, int type, ...);
extern char _gp;

int32_t alloc_malloc_init(void *arg1);
int32_t alloc_malloc_get_paddr(int32_t arg1);
int32_t alloc_pmem_init(void *arg1);
int32_t alloc_pmem_get_paddr(int32_t arg1);
int32_t alloc_pmem_get_vaddr(int32_t arg1);
int32_t alloc_kmem_init(void *arg1);
int32_t alloc_kmem_get_paddr(int32_t arg1);
int32_t alloc_kmem_get_vaddr(int32_t arg1);
int32_t alloc_kmem_flush_cache(int32_t arg1, int32_t arg2, int32_t arg3);
int32_t buddy_init(void *arg1, int32_t arg2);
int32_t buddy_alloc(int32_t arg1);
int32_t buddy_free(int32_t arg1);
int32_t buddy_dump(void);

/* forward decl, ported by T<N> later */
int32_t continuous_init(void *arg1, int32_t arg2);
/* forward decl, ported by T<N> later */
int32_t continuous_alloc(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t continuous_free(int32_t arg1);
/* forward decl, ported by T<N> later */
int32_t continuous_dump(void);
/* forward decl, ported by T<N> later */
int32_t continuous_dump_to_file(void);

typedef struct AllocInfoNode {
    uint32_t next;
    char owner[0x20];
    uint32_t vaddr;
    uint32_t paddr;
    int32_t length;
    int32_t ref_cnt;
    int32_t mem_attr;
} AllocInfoNode;

#define ALLOC_INIT_FLAG_OFFSET 0x38
#define ALLOC_METHOD_NAME_OFFSET 0x3c
#define ALLOC_BASE_VADDR_OFFSET 0x5c
#define ALLOC_BASE_PADDR_OFFSET 0x60
#define ALLOC_BASE_LENGTH_OFFSET 0x64
#define ALLOC_METHOD_INIT_OFFSET 0x68
#define ALLOC_METHOD_GET_PADDR_OFFSET 0x6c
#define ALLOC_METHOD_GET_VADDR_OFFSET 0x70
#define ALLOC_METHOD_FLUSH_CACHE_OFFSET 0x74
#define ALLOC_MANAGER_NAME_OFFSET 0x78
#define ALLOC_MANAGER_INIT_OFFSET 0x98
#define ALLOC_MANAGER_ALLOC_OFFSET 0x9c
#define ALLOC_MANAGER_FREE_OFFSET 0xa0
#define ALLOC_MANAGER_DUMP_OFFSET 0xa4
#define ALLOC_MANAGER_DUMP_TO_FILE_OFFSET 0xa8
#define ALLOC_API_ALLOC_OFFSET 0xac
#define ALLOC_API_SPALLOC_OFFSET 0xb0
#define ALLOC_API_FREE_OFFSET 0xb4
#define ALLOC_API_GET_INFO_OFFSET 0xb8
#define ALLOC_API_GET_ATTR_OFFSET 0xbc
#define ALLOC_API_SET_ATTR_OFFSET 0xc0
#define ALLOC_API_DUMP_STATUS_OFFSET 0xc4
#define ALLOC_API_DUMP_TO_FILE_OFFSET 0xc8

typedef int32_t (*AllocManagerAllocFn)(int32_t arg1);
typedef int32_t (*AllocManagerFreeFn)(int32_t arg1);
typedef int32_t (*AllocMethodInitFn)(void *arg1);
typedef int32_t (*AllocMethodGetPaddrFn)(int32_t arg1);
typedef int32_t (*AllocMethodGetVaddrFn)(int32_t arg1);
typedef int32_t (*AllocMethodFlushCacheFn)(int32_t arg1, int32_t arg2, int32_t arg3);
typedef int32_t (*MemManagerInitFn)(void *arg1, int32_t arg2);
typedef int32_t (*MemManagerDumpFn)(void);
typedef int32_t (*MemManagerDumpToFileFn)(void);

static inline void **alloc_ptr_field(void *base, uint32_t offset)
{
    return (void **)((char *)base + offset);
}

static inline AllocInfoNode *alloc_node_next(const AllocInfoNode *node)
{
    return (AllocInfoNode *)(uintptr_t)node->next;
}

AllocInfoNode *alloc_get_info(AllocInfoNode *arg1, int32_t arg2)
{
    AllocInfoNode *result = arg1;

    if (arg1 == NULL) {
        return result;
    }

    if (arg2 == (int32_t)arg1->vaddr) {
        return result;
    }

    while (1) {
        result = alloc_node_next(result);
        if (result == NULL) {
            return result;
        }

        if (arg2 == (int32_t)result->vaddr) {
            return result;
        }
    }
}

int32_t allocMem(AllocInfoNode **arg1, int32_t arg2, char *arg3)
{
    const char *var_2c_1;
    int32_t v0_3;
    int32_t v1_2;
    AllocManagerAllocFn t9_1;
    int32_t result;
    AllocInfoNode *v0;
    AllocMethodGetPaddrFn t9_2;
    uint32_t *a0_3;
    uint32_t v1_1;

    if (arg1 == NULL) {
        v0_3 = IMP_Log_Get_Option();
        var_2c_1 = "%s function param is NULL\n";
        v1_2 = 0xdd;
        imp_log_fun(6, v0_3, 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", v1_2,
            "allocMem", var_2c_1, "allocMem");
        return 0;
    }

    t9_1 = *(AllocManagerAllocFn *)alloc_ptr_field(arg1, ALLOC_MANAGER_ALLOC_OFFSET);
    if (t9_1 == NULL) {
        v0_3 = IMP_Log_Get_Option();
        var_2c_1 = "%s mem_manager->alloc is NULL\n";
        v1_2 = 0xe8;
        imp_log_fun(6, v0_3, 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", v1_2,
            "allocMem", var_2c_1, "allocMem");
        return 0;
    }

    result = t9_1(arg2);
    if (result == 0) {
        v0_3 = IMP_Log_Get_Option();
        var_2c_1 = "%s mem_manager->alloc is failed\n";
        v1_2 = 0xe4;
        imp_log_fun(6, v0_3, 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", v1_2,
            "allocMem", var_2c_1, "allocMem");
        return 0;
    }

    v0 = (AllocInfoNode *)calloc(1, 0x38);
    if (v0 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", 0xee,
            "allocMem", "Alloc Info struct malloc failed\n");
        return 0;
    }

    v0->next = 0;
    strncpy((char *)((char *)v0 + 4), arg3, 0x1f);
    t9_2 = *(AllocMethodGetPaddrFn *)alloc_ptr_field(arg1, ALLOC_METHOD_GET_PADDR_OFFSET);
    v0->vaddr = (uint32_t)result;
    v0->length = arg2;
    v0->ref_cnt = 1;
    if (t9_2 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", 0xfb,
            "allocMem", "%s mem_alloc->get_paddr is NULL\n", "allocMem");
        free(v0);
        return 0;
    }

    v0->paddr = (uint32_t)t9_2(result);
    a0_3 = (uint32_t *)arg1;
    while (1) {
        v1_1 = *a0_3;
        if (v1_1 == 0) {
            break;
        }

        a0_3 = &((AllocInfoNode *)(uintptr_t)v1_1)->next;
    }

    *a0_3 = (uint32_t)(uintptr_t)v0;
    return result;
}

void *freeMem(AllocInfoNode **arg1, int32_t arg2)
{
    const char *var_2c_1;
    int32_t v0;
    int32_t v1;
    AllocInfoNode *i = (AllocInfoNode *)arg1;
    int32_t j;
    int32_t v1_2;
    AllocManagerFreeFn t9_1;
    AllocInfoNode *a0_1;

    if (arg1 == NULL) {
        v0 = IMP_Log_Get_Option();
        var_2c_1 = "%s function param is NULL\n";
        v1 = 0x122;
        return (void *)(intptr_t)imp_log_fun(6, v0, 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", v1,
            "freeMem", var_2c_1, "freeMem");
    }

    j = (int32_t)i->vaddr;
    if (arg2 == j) {
        v1_2 = i->ref_cnt - 1;
        i->ref_cnt = v1_2;
        if (v1_2 == 0) {
            t9_1 = *(AllocManagerFreeFn *)alloc_ptr_field(arg1, ALLOC_MANAGER_FREE_OFFSET);
            if (t9_1 == NULL) {
                return (void *)(intptr_t)imp_log_fun(6, IMP_Log_Get_Option(), 2, "Alloc Manager",
                    "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", 0x131,
                    "freeMem", "alloc->mem_manager->free is NULL\n");
            }

            t9_1(j);
            i = (AllocInfoNode *)(uintptr_t)*(uint32_t *)arg1;
            if (i == NULL) {
                i = (AllocInfoNode *)arg1;
                if (arg2 == (int32_t)((AllocInfoNode *)arg1)->vaddr) {
                    *(uint32_t *)arg1 = 0;
                    free(arg1);
                    return NULL;
                }
            } else {
                if ((int32_t)i->vaddr == j) {
                    *(uint32_t *)arg1 = i->next;
                    free(i);
                    return NULL;
                }

                a0_1 = alloc_node_next(i);
                if (a0_1 != NULL) {
                    while (j != (int32_t)a0_1->vaddr) {
                        a0_1 = alloc_node_next(a0_1);
                        if (a0_1 == NULL) {
                            return i;
                        }

                        i = alloc_node_next(i);
                    }

                    i->next = a0_1->next;
                    free(a0_1);
                    return NULL;
                }
            }
        }

        return i;
    }

    do {
        i = alloc_node_next(i);
    } while (i != NULL);

    v0 = IMP_Log_Get_Option();
    var_2c_1 = "%s alloc get info failed\n";
    v1 = 0x128;
    return (void *)(intptr_t)imp_log_fun(6, v0, 2, "Alloc Manager",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", v1,
        "freeMem", var_2c_1, "freeMem");
}

int32_t alloc_dump_to_file(AllocInfoNode *arg1)
{
    AllocInfoNode *i = arg1;
    FILE *stream;

    stream = fopen("/tmp/alloc_manager_info", "w");
    if (stream == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", 0x6c,
            "alloc_dump_to_file", "Open /tmp/alloc_manager_info failed\n", &_gp);
        return -1;
    }

    if (i != NULL) {
        do {
            union {
                struct {
                    int32_t var_238;
                    char str[0x1fc];
                } s;
                char buf[0x200];
            } stack_buf;
            int32_t v0;
            int32_t s2_2;
            int32_t s2_3;
            int32_t s2_4;
            int32_t s2_5;
            int32_t s2_6;
            int32_t v0_8;

            memset(stack_buf.s.str, 0, 0x1fc);
            stack_buf.s.var_238 = 0;
            v0 = sprintf(stack_buf.buf, "\ninfo->n_info = %p\n",
                (void *)(uintptr_t)i->next);
            s2_2 = v0 + sprintf(stack_buf.buf + v0, "info->owner = %s\n", i->owner);
            s2_3 = s2_2 + sprintf(stack_buf.buf + s2_2, "info->vaddr = 0x%08x\n", i->vaddr);
            s2_4 = s2_3 + sprintf(stack_buf.buf + s2_3, "info->paddr = 0x%08x\n", i->paddr);
            s2_5 = s2_4 + sprintf(stack_buf.buf + s2_4, "info->length = %d\n", i->length);
            s2_6 = s2_5 + sprintf(stack_buf.buf + s2_5, "info->ref_cnt = %d\n", i->ref_cnt);
            v0_8 = sprintf(stack_buf.buf + s2_6, "info->mem_attr = %d\n", i->mem_attr);
            i = alloc_node_next(i);
            fwrite(stack_buf.buf, (size_t)(s2_6 + v0_8), 1, stream);
        } while (i != NULL);
    }

    fclose(stream);
    return 0;
}

int32_t spAllocMem(AllocInfoNode *arg1, int32_t arg2)
{
    const char *var_1c;
    int32_t v0;
    int32_t v1_2;

    if (arg1 == NULL) {
        v0 = IMP_Log_Get_Option();
        var_1c = "%s function param is NULL\n";
        v1_2 = 0x10e;
    } else {
        do {
            if (arg2 == (int32_t)arg1->vaddr) {
                arg1->ref_cnt += 1;
                return arg2;
            }

            arg1 = alloc_node_next(arg1);
        } while (arg1 != NULL);

        v0 = IMP_Log_Get_Option();
        var_1c = "%s alloc get info failed\n";
        v1_2 = 0x114;
    }

    imp_log_fun(6, v0, 2, "Alloc Manager",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", v1_2,
        "spAllocMem", var_1c, "spAllocMem");
    return 0;
}

int32_t getMemAttr(AllocInfoNode *arg1, int32_t arg2)
{
    const char *var_1c;
    int32_t v0_2;
    int32_t v1;

    if (arg1 == NULL) {
        v0_2 = IMP_Log_Get_Option();
        var_1c = "%s function param is NULL\n";
        v1 = 0x142;
    } else {
        do {
            if (arg2 == (int32_t)arg1->vaddr) {
                return arg1->mem_attr;
            }

            arg1 = alloc_node_next(arg1);
        } while (arg1 != NULL);

        v0_2 = IMP_Log_Get_Option();
        var_1c = "%s alloc get info failed\n";
        v1 = 0x148;
    }

    imp_log_fun(6, v0_2, 2, "Alloc Manager",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", v1,
        "getMemAttr", var_1c, "getMemAttr");
    return -1;
}

int32_t setMemAttr(AllocInfoNode *arg1, int32_t arg2, int32_t arg3)
{
    const char *var_1c;
    int32_t v0_2;
    int32_t v1;

    if (arg1 == NULL) {
        v0_2 = IMP_Log_Get_Option();
        var_1c = "%s function param is NULL\n";
        v1 = 0x154;
    } else {
        do {
            if (arg2 == (int32_t)arg1->vaddr) {
                arg1->mem_attr = arg3;
                return 0;
            }

            arg1 = alloc_node_next(arg1);
        } while (arg1 != NULL);

        v0_2 = IMP_Log_Get_Option();
        var_1c = "%s alloc get info failed\n";
        v1 = 0x15a;
    }

    imp_log_fun(6, v0_2, 2, "Alloc Manager",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", v1,
        "setMemAttr", var_1c, "setMemAttr");
    return -1;
}

int32_t dumpMemStatus(AllocInfoNode **arg1)
{
    MemManagerDumpFn t9_1;
    AllocInfoNode *i;

    if (arg1 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", 0x166,
            "dumpMemStatus", "%s function param is NULL\n", "dumpMemStatus");
        return -1;
    }

    t9_1 = *(MemManagerDumpFn *)alloc_ptr_field(arg1, ALLOC_MANAGER_DUMP_OFFSET);
    if (t9_1 != NULL) {
        t9_1();
    }

    i = (AllocInfoNode *)arg1;
    do {
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", 0x5a,
            "alloc_dump_info", "\ninfo->n_info = %p\n", (void *)(uintptr_t)i->next);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", 0x5b,
            "alloc_dump_info", "info->owner = %s\n", i->owner);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", 0x5c,
            "alloc_dump_info", "info->vaddr = 0x%08x\n", i->vaddr);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", 0x5d,
            "alloc_dump_info", "info->paddr = 0x%08x\n", i->paddr);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", 0x5e,
            "alloc_dump_info", "info->length = %d\n", i->length);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", 0x5f,
            "alloc_dump_info", "info->ref_cnt = %d\n", i->ref_cnt);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", 0x60,
            "alloc_dump_info", "info->mem_attr = %d\n", i->mem_attr);
        i = alloc_node_next(i);
    } while (i != NULL);

    return 0;
}

int32_t dumpMemToFile(AllocInfoNode **arg1)
{
    MemManagerDumpToFileFn t9_1;

    if (arg1 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", 0x175,
            "dumpMemToFile", "%s function param is NULL\n", "dumpMemToFile");
        return -1;
    }

    t9_1 = *(MemManagerDumpToFileFn *)alloc_ptr_field(arg1, ALLOC_MANAGER_DUMP_TO_FILE_OFFSET);
    if (t9_1 != NULL) {
        t9_1();
    }

    alloc_dump_to_file((AllocInfoNode *)arg1);
    return 0;
}

int32_t allocInit(void *arg1)
{
    int32_t var_3c_1;
    const char *var_34_1;
    int32_t v0_10;
    AllocMethodInitFn t9_1;
    int32_t result;
    int32_t v1_2;
    int32_t v0_4;
    int32_t v0_5 = 0;
    MemManagerInitFn t9_2;

    if (arg1 == NULL) {
        v0_10 = IMP_Log_Get_Option();
        var_3c_1 = 0x89;
        var_34_1 = "%s function param is NULL\n";
        imp_log_fun(6, v0_10, 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", var_3c_1,
            "allocInit", var_34_1, "allocInit");
        return -1;
    }

    if (*(int32_t *)((char *)arg1 + ALLOC_INIT_FLAG_OFFSET) == 1) {
        return 0;
    }

    *(void (**)(void))((char *)arg1 + ALLOC_API_ALLOC_OFFSET) = (void (*)(void))allocMem;
    *(void (**)(void))((char *)arg1 + ALLOC_API_SPALLOC_OFFSET) = (void (*)(void))spAllocMem;
    *(void (**)(void))((char *)arg1 + ALLOC_API_FREE_OFFSET) = (void (*)(void))freeMem;
    *(void (**)(void))((char *)arg1 + ALLOC_API_GET_INFO_OFFSET) = (void (*)(void))alloc_get_info;
    *(void (**)(void))((char *)arg1 + ALLOC_API_GET_ATTR_OFFSET) = (void (*)(void))getMemAttr;
    *(void (**)(void))((char *)arg1 + ALLOC_API_SET_ATTR_OFFSET) = (void (*)(void))setMemAttr;
    *(void (**)(void))((char *)arg1 + ALLOC_API_DUMP_STATUS_OFFSET) = (void (*)(void))dumpMemStatus;
    *(void (**)(void))((char *)arg1 + ALLOC_API_DUMP_TO_FILE_OFFSET) = (void (*)(void))dumpMemToFile;

    imp_log_fun(4, IMP_Log_Get_Option(), 2, "Alloc Manager",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", 0x9b,
        "allocInit", "MEM Alloc Method is %s\n", (char *)arg1 + ALLOC_METHOD_NAME_OFFSET);

    if (strcmp((char *)arg1 + ALLOC_METHOD_NAME_OFFSET, "pmem") != 0) {
        if (strcmp((char *)arg1 + ALLOC_METHOD_NAME_OFFSET, "kmalloc") == 0) {
            t9_1 = alloc_kmem_init;
            *(AllocMethodGetPaddrFn *)alloc_ptr_field(arg1, ALLOC_METHOD_GET_PADDR_OFFSET) = alloc_kmem_get_paddr;
            *(AllocMethodInitFn *)alloc_ptr_field(arg1, ALLOC_METHOD_INIT_OFFSET) = alloc_kmem_init;
            *(AllocMethodGetVaddrFn *)alloc_ptr_field(arg1, ALLOC_METHOD_GET_VADDR_OFFSET) = alloc_kmem_get_vaddr;
            *(AllocMethodFlushCacheFn *)alloc_ptr_field(arg1, ALLOC_METHOD_FLUSH_CACHE_OFFSET) = alloc_kmem_flush_cache;
        } else {
            if (strcmp((char *)arg1 + ALLOC_METHOD_NAME_OFFSET, "malloc") != 0) {
                goto label_14188;
            }

            t9_1 = alloc_malloc_init;
            *(AllocMethodInitFn *)alloc_ptr_field(arg1, ALLOC_METHOD_INIT_OFFSET) = alloc_malloc_init;
            *(AllocMethodGetPaddrFn *)alloc_ptr_field(arg1, ALLOC_METHOD_GET_PADDR_OFFSET) = (AllocMethodGetPaddrFn)alloc_malloc_get_paddr;
        }
    } else {
label_14188:
        t9_1 = alloc_pmem_init;
        *(AllocMethodGetPaddrFn *)alloc_ptr_field(arg1, ALLOC_METHOD_GET_PADDR_OFFSET) = alloc_pmem_get_paddr;
        *(AllocMethodInitFn *)alloc_ptr_field(arg1, ALLOC_METHOD_INIT_OFFSET) = alloc_pmem_init;
        *(AllocMethodGetVaddrFn *)alloc_ptr_field(arg1, ALLOC_METHOD_GET_VADDR_OFFSET) = alloc_pmem_get_vaddr;
    }

    result = t9_1(arg1);
    if (result != 0) {
        v0_10 = IMP_Log_Get_Option();
        var_34_1 = "%s function Mem Alloc init error\n";
        v1_2 = 0xb0;
        var_3c_1 = v1_2;
        imp_log_fun(6, v0_10, 2, "Alloc Manager",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", var_3c_1,
            "allocInit", var_34_1, "allocInit");
        return -1;
    }

    imp_log_fun(4, IMP_Log_Get_Option(), 2, "Alloc Manager",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", 0xb6,
        "allocInit", "MEM Manager Method is %s\n", (char *)arg1 + ALLOC_MANAGER_NAME_OFFSET);
    v0_4 = strcmp((char *)arg1 + ALLOC_MANAGER_NAME_OFFSET, "buddy");
    if (v0_4 != 0) {
        v0_5 = strcmp((char *)arg1 + ALLOC_MANAGER_NAME_OFFSET, "continuous");
    }

    if (v0_4 != 0 && v0_5 == 0) {
        t9_2 = continuous_init;
        *(AllocManagerAllocFn *)alloc_ptr_field(arg1, ALLOC_MANAGER_ALLOC_OFFSET) = continuous_alloc;
        *(MemManagerInitFn *)alloc_ptr_field(arg1, ALLOC_MANAGER_INIT_OFFSET) = continuous_init;
        *(AllocManagerFreeFn *)alloc_ptr_field(arg1, ALLOC_MANAGER_FREE_OFFSET) = continuous_free;
        *(MemManagerDumpFn *)alloc_ptr_field(arg1, ALLOC_MANAGER_DUMP_OFFSET) = continuous_dump;
        *(MemManagerDumpToFileFn *)alloc_ptr_field(arg1, ALLOC_MANAGER_DUMP_TO_FILE_OFFSET) = continuous_dump_to_file;
    } else {
        t9_2 = buddy_init;
        *(AllocManagerAllocFn *)alloc_ptr_field(arg1, ALLOC_MANAGER_ALLOC_OFFSET) = buddy_alloc;
        *(MemManagerInitFn *)alloc_ptr_field(arg1, ALLOC_MANAGER_INIT_OFFSET) = buddy_init;
        *(AllocManagerFreeFn *)alloc_ptr_field(arg1, ALLOC_MANAGER_FREE_OFFSET) = buddy_free;
        *(MemManagerDumpFn *)alloc_ptr_field(arg1, ALLOC_MANAGER_DUMP_OFFSET) = buddy_dump;
    }

    if (t9_2(*(void **)((char *)arg1 + ALLOC_BASE_VADDR_OFFSET),
            *(int32_t *)((char *)arg1 + ALLOC_BASE_LENGTH_OFFSET)) == 0) {
        *(int32_t *)((char *)arg1 + ALLOC_INIT_FLAG_OFFSET) = 1;
        return result;
    }

    v0_10 = IMP_Log_Get_Option();
    var_34_1 = "%s function Mem Manager init error\n";
    v1_2 = 0xcd;
    var_3c_1 = v1_2;
    imp_log_fun(6, v0_10, 2, "Alloc Manager",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/alloc_manager.c", var_3c_1,
        "allocInit", var_34_1, "allocInit");
    return -1;
}
