#include <stdint.h>
#include <string.h>

#include "core/imp_alloc.h"

int IMP_Log_Get_Option();  /* K&R: variadic-adjacent — some call sites
                             pass extra arg, Binja HLIL artifact */
int32_t imp_log_fun(int level, int option, int type, ...);
extern char _gp;

/* forward decl, ported by T<N> later */
int32_t allocInit(void *arg1);

typedef int32_t (*AllocApiAllocFn)(void *arg1, int32_t arg2, char *arg3);
typedef int32_t (*AllocApiSpAllocFn)(void *arg1, int32_t arg2);
typedef void *(*AllocApiFreeFn)(void *arg1, int32_t arg2);
typedef void *(*AllocApiGetInfoFn)(void *arg1, int32_t arg2);
typedef int32_t (*AllocApiGetAttrFn)(void *arg1, int32_t arg2);
typedef int32_t (*AllocApiSetAttrFn)(void *arg1, int32_t arg2, int32_t arg3);
typedef int32_t (*AllocApiDumpFn)(void *arg1);
typedef int32_t (*AllocMethodVirtToPhysFn)(int32_t arg1);
typedef int32_t (*AllocMethodPhysToVirtFn)(int32_t arg1);
typedef int32_t (*AllocMethodFlushCacheFn)(int32_t arg1, int32_t arg2, int32_t arg3);

typedef struct AllocApiState {
    void *head;
    uint8_t pad04[0x34];
    int32_t init_flag;
    char alloc_method_name[0x20];
    uint32_t base_vaddr;
    uint32_t base_paddr;
    uint32_t base_length;
    void *alloc_method_init;
    AllocMethodVirtToPhysFn virt_to_phys;
    AllocMethodPhysToVirtFn phys_to_virt;
    AllocMethodFlushCacheFn flush_cache;
    char manager_name[0x20];
    void *manager_init;
    void *manager_alloc;
    void *manager_free;
    void *manager_dump;
    void *manager_dump_to_file;
    AllocApiAllocFn alloc_mem;
    AllocApiSpAllocFn sp_alloc_mem;
    AllocApiFreeFn free_mem;
    AllocApiGetInfoFn get_info;
    AllocApiGetAttrFn get_attr;
    AllocApiSetAttrFn set_attr;
    AllocApiDumpFn dump_status;
    AllocApiDumpFn dump_to_file;
} AllocApiState;

static AllocApiState g_alloc;

static int32_t IMP_init_part_0(void)
{
    int32_t result;

    strcpy(g_alloc.alloc_method_name, "kmalloc");
    strncpy(g_alloc.manager_name, "continuous", 0xb);
    result = allocInit(&g_alloc);
    if (result != 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP Alloc APIs",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", 0x20,
            "IMP_init", "alloc init failed\n", &_gp);
    }

    return result;
}

int32_t IMP_Get_Info(IMP_Alloc_Info *arg1, int32_t arg2)
{
    void *v0_2;
    int32_t v0_6;
    const char *var_2c_1;
    int32_t v1_3;

    if (arg1 == NULL || arg2 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP Alloc APIs",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", 0x28,
            "IMP_Get_Info", "%s IMPAlloc *alloc == NULL or ptr == NULL\n", "IMP_Get_Info");
        return -1;
    }

    if (g_alloc.init_flag != 1) {
        int32_t v0_1 = IMP_init_part_0();

        if (g_alloc.init_flag != 1 && v0_1 != 0) {
            v0_6 = IMP_Log_Get_Option();
            var_2c_1 = "imp init failed\n";
            v1_3 = 0x2d;
            imp_log_fun(6, v0_6, 2, "IMP Alloc APIs",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_3,
                "IMP_Get_Info", var_2c_1);
            return -1;
        }
    }

    if (g_alloc.get_info == NULL) {
        return 0;
    }

    v0_2 = g_alloc.get_info(&g_alloc, arg2);
    if (v0_2 != NULL) {
        strncpy(arg1->name, (char *)v0_2 + 4, 0x1f);
        arg1->method = *(int32_t *)((char *)v0_2 + 0x24);
        arg1->phys_addr = *(uint32_t *)((char *)v0_2 + 0x28);
        arg1->virt_addr = *(uint32_t *)((char *)v0_2 + 0x2c);
        arg1->length = *(uint32_t *)((char *)v0_2 + 0x30);
        arg1->attr = *(int32_t *)((char *)v0_2 + 0x34);
        return 0;
    }

    v0_6 = IMP_Log_Get_Option();
    var_2c_1 = "get alloc info error\n";
    v1_3 = 0x34;
    imp_log_fun(6, v0_6, 2, "IMP Alloc APIs",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_3,
        "IMP_Get_Info", var_2c_1);
    return -1;
}

int32_t IMP_Alloc_Get_Attr(IMP_Alloc_Attr *arg1)
{
    int32_t v0_2;
    const char *var_1c;
    int32_t v1_1;

    if (arg1 == NULL) {
        v0_2 = IMP_Log_Get_Option();
        var_1c = "IMPAlloc *alloc == NULL\n";
        v1_1 = 0x9f;
        imp_log_fun(6, v0_2, 2, "IMP Alloc APIs",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_1,
            "IMP_Alloc_Get_Attr", var_1c, &_gp);
        return -1;
    }

    if (g_alloc.init_flag != 1) {
        int32_t v0_1 = IMP_init_part_0();

        if (g_alloc.init_flag != 1 && v0_1 != 0) {
            v0_2 = IMP_Log_Get_Option();
            var_1c = "imp init failed\n";
            v1_1 = 0xa4;
            imp_log_fun(6, v0_2, 2, "IMP Alloc APIs",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_1,
                "IMP_Alloc_Get_Attr", var_1c, &_gp);
            return -1;
        }
    }

    if (g_alloc.get_attr == NULL) {
        return arg1->attr;
    }

    arg1->attr = g_alloc.get_attr(&g_alloc, arg1->method);
    return arg1->attr;
}

int32_t IMP_Alloc_Set_Attr(IMP_Alloc_Attr *arg1)
{
    int32_t v0_4;
    const char *var_1c_1;
    int32_t v1_2;

    if (arg1 == NULL) {
        v0_4 = IMP_Log_Get_Option();
        var_1c_1 = "IMPAlloc *alloc == NULL\n";
        v1_2 = 0xb3;
        imp_log_fun(6, v0_4, 2, "IMP Alloc APIs",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_2,
            "IMP_Alloc_Set_Attr", var_1c_1, &_gp);
        return -1;
    }

    if (g_alloc.init_flag != 1) {
        int32_t v0_1 = IMP_init_part_0();

        if (g_alloc.init_flag != 1 && v0_1 != 0) {
            v0_4 = IMP_Log_Get_Option();
            var_1c_1 = "imp init failed\n";
            v1_2 = 0xb8;
            imp_log_fun(6, v0_4, 2, "IMP Alloc APIs",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_2,
                "IMP_Alloc_Set_Attr", var_1c_1, &_gp);
            return -1;
        }
    }

    if (g_alloc.set_attr == NULL) {
        return 0;
    }

    return g_alloc.set_attr(&g_alloc, arg1->method, arg1->attr);
}

int32_t IMP_Alloc_Dump(void)
{
    int32_t result = 1;

    if (g_alloc.init_flag != 1) {
        result = IMP_init_part_0();
    }

    if (result != 0) {
        return imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP Alloc APIs",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", 0xc7,
            "IMP_Alloc_Dump", "imp init failed\n", &_gp);
    }

    if (g_alloc.dump_status == NULL) {
        return result;
    }

    return g_alloc.dump_status(&g_alloc);
}

int32_t IMP_Alloc_Dump_To_File(void)
{
    int32_t result = 1;

    if (g_alloc.init_flag != 1) {
        result = IMP_init_part_0();
    }

    if (result != 0) {
        return imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP Alloc APIs",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", 0xd2,
            "IMP_Alloc_Dump_To_File", "imp init failed\n", &_gp);
    }

    if (g_alloc.dump_to_file == NULL) {
        return result;
    }

    return g_alloc.dump_to_file(&g_alloc);
}

int32_t IMP_Alloc(IMP_Alloc_Info *arg1, int32_t arg2, char *arg3)
{
    int32_t v0_5;
    const char *var_24_1;
    int32_t v1_1;

    if (arg1 == NULL) {
        v0_5 = IMP_Log_Get_Option();
        var_24_1 = "IMPAlloc *alloc = NULL\n";
        v1_1 = 0x47;
        imp_log_fun(6, v0_5, 2, "IMP Alloc APIs",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_1,
            "IMP_Alloc", var_24_1, &_gp);
        return -1;
    }

    if (g_alloc.init_flag != 1) {
        int32_t v0_1 = IMP_init_part_0();

        if (g_alloc.init_flag != 1 && v0_1 != 0) {
            v0_5 = IMP_Log_Get_Option();
            var_24_1 = "imp init failed\n";
            v1_1 = 0x4c;
            imp_log_fun(6, v0_5, 2, "IMP Alloc APIs",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_1,
                "IMP_Alloc", var_24_1, &_gp);
            return -1;
        }
    }

    if (g_alloc.alloc_mem != NULL) {
        int32_t v0_2 = g_alloc.alloc_mem(&g_alloc, arg2, arg3);

        if (v0_2 == 0) {
            v0_5 = IMP_Log_Get_Option();
            var_24_1 = "g_alloc.alloc_mem failed\n";
            v1_1 = 0x53;
            imp_log_fun(6, v0_5, 2, "IMP Alloc APIs",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_1,
                "IMP_Alloc", var_24_1, &_gp);
            IMP_Alloc_Dump();
            return -1;
        }

        if (IMP_Get_Info(arg1, v0_2) != 0) {
            v0_5 = IMP_Log_Get_Option();
            var_24_1 = "IMP_Get_info failed\n";
            v1_1 = 0x58;
            imp_log_fun(6, v0_5, 2, "IMP Alloc APIs",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_1,
                "IMP_Alloc", var_24_1, &_gp);
            IMP_Alloc_Dump();
            return -1;
        }

        IMP_Alloc_Dump_To_File();
        return 0;
    }

    IMP_Alloc_Dump_To_File();
    return 0;
}

int32_t IMP_Sp_Alloc(IMP_Alloc_Info *arg1, int32_t arg2)
{
    int32_t v0_5;
    const char *var_24_1;
    int32_t v1_1;

    if (arg1 == NULL || arg2 == 0) {
        v0_5 = IMP_Log_Get_Option();
        var_24_1 = "IMPAlloc *alloc == NULL or ptr == NULL\n";
        v1_1 = 0x67;
        imp_log_fun(6, v0_5, 2, "IMP Alloc APIs",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_1,
            "IMP_Sp_Alloc", var_24_1, &_gp);
        return -1;
    }

    if (g_alloc.init_flag != 1) {
        int32_t v0_1 = IMP_init_part_0();

        if (g_alloc.init_flag != 1 && v0_1 != 0) {
            v0_5 = IMP_Log_Get_Option();
            var_24_1 = "imp init failed\n";
            v1_1 = 0x6c;
            imp_log_fun(6, v0_5, 2, "IMP Alloc APIs",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_1,
                "IMP_Sp_Alloc", var_24_1, &_gp);
            return -1;
        }
    }

    if (g_alloc.sp_alloc_mem == NULL) {
        IMP_Alloc_Dump_To_File();
        return 0;
    }

    {
        int32_t v0_2 = g_alloc.sp_alloc_mem(&g_alloc, arg2);

        if (v0_2 == 0) {
            v0_5 = IMP_Log_Get_Option();
            var_24_1 = "g_alloc.alloc_mem failed\n";
            v1_1 = 0x73;
            imp_log_fun(6, v0_5, 2, "IMP Alloc APIs",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_1,
                "IMP_Sp_Alloc", var_24_1, &_gp);
            return -1;
        }

        if (IMP_Get_Info(arg1, v0_2) == 0) {
            IMP_Alloc_Dump_To_File();
            return 0;
        }

        v0_5 = IMP_Log_Get_Option();
        var_24_1 = "IMP_Get_info failed\n";
        v1_1 = 0x77;
        imp_log_fun(6, v0_5, 2, "IMP Alloc APIs",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_1,
            "IMP_Sp_Alloc", var_24_1, &_gp);
        return -1;
    }
}

int32_t IMP_Free(IMP_Alloc_Info *arg1, int32_t arg2)
{
    int32_t v0_7;
    const char *var_1c;
    int32_t v1_2;

    if (arg1 == NULL || arg2 == 0) {
        v0_7 = IMP_Log_Get_Option();
        var_1c = "IMPAlloc *alloc == NULL or ptr == NULL\n";
        v1_2 = 0x83;
        return imp_log_fun(6, v0_7, 2, "IMP Alloc APIs",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_2,
            "IMP_Free", var_1c, &_gp);
    }

    if (g_alloc.init_flag != 1) {
        int32_t v0_1 = IMP_init_part_0();

        if (g_alloc.init_flag != 1 && v0_1 != 0) {
            v0_7 = IMP_Log_Get_Option();
            var_1c = "imp init failed\n";
            v1_2 = 0x88;
            return imp_log_fun(6, v0_7, 2, "IMP Alloc APIs",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_2,
                "IMP_Free", var_1c, &_gp);
        }
    }

    if (g_alloc.free_mem == NULL) {
        return IMP_Alloc_Dump_To_File();
    }

    if (IMP_Get_Info(arg1, arg2) == 0) {
        g_alloc.free_mem(&g_alloc, arg2);
        arg1->length -= 1;
        if (arg1->length == 0) {
            arg1->method = 0;
            arg1->phys_addr = 0;
        }

        return IMP_Alloc_Dump_To_File();
    }

    v0_7 = IMP_Log_Get_Option();
    var_1c = "IMP_Get_info failed\n";
    v1_2 = 0x8e;
    return imp_log_fun(6, v0_7, 2, "IMP Alloc APIs",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", v1_2,
        "IMP_Free", var_1c, &_gp);
}

int32_t IMP_Virt_to_Phys(int32_t arg1)
{
    if (g_alloc.init_flag != 1) {
        int32_t v0_1 = IMP_init_part_0();

        if (g_alloc.init_flag != 1 && v0_1 != 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP Alloc APIs",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", 0xdd,
                "IMP_Virt_to_Phys", "imp init failed\n", &_gp);
        }
    }

    if (g_alloc.virt_to_phys != NULL) {
        return g_alloc.virt_to_phys(arg1);
    }

    return 0;
}

int32_t IMP_Phys_to_Virt(int32_t arg1)
{
    if (g_alloc.init_flag != 1) {
        int32_t v0_1 = IMP_init_part_0();

        if (g_alloc.init_flag != 1 && v0_1 != 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP Alloc APIs",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", 0xe9,
                "IMP_Phys_to_Virt", "imp init failed\n", &_gp);
        }
    }

    if (g_alloc.phys_to_virt != NULL) {
        return g_alloc.phys_to_virt(arg1);
    }

    return 0;
}

int32_t IMP_FlushCache(int32_t arg1, int32_t arg2, int32_t arg3)
{
    if (g_alloc.init_flag != 1) {
        int32_t v0_1 = IMP_init_part_0();

        if (g_alloc.init_flag != 1 && v0_1 != 0) {
            imp_log_fun(6, IMP_Log_Get_Option(), 2, "IMP Alloc APIs",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/imp_alloc_api.c", 0xf5,
                "IMP_FlushCache", "imp init failed\n", &_gp);
        }
    }

    if (g_alloc.flush_cache != NULL) {
        return g_alloc.flush_cache(arg1, arg2, arg3);
    }

    return -1;
}
