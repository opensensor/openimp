#include <stdint.h>
#include <string.h>

typedef struct BuddyInfo {
    int32_t left;
    int32_t right;
    int32_t level;
    int32_t level_num;
    int32_t token;
    int32_t addr;
    int32_t size;
    int32_t used;
} BuddyInfo;

typedef struct BuddyState {
    int32_t d;
    int32_t size;
} BuddyState;

extern char _gp;

int32_t IMP_Log_Get_Option(void);
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...);

int32_t buddy_check_child_block_is_used(BuddyInfo *arg1);
BuddyInfo *buddy_request_block(BuddyInfo *arg1, int32_t arg2);
BuddyInfo *buddy_release_block(BuddyInfo *arg1, int32_t arg2);
int32_t buddy_add_list(BuddyInfo *arg1);
int32_t buddy_dump_info(BuddyInfo *arg1);
int32_t buddy_alloc(int32_t arg1);
int32_t buddy_init(void *arg1, int32_t arg2);
int32_t buddy_free(int32_t arg1);
int32_t buddy_dump(void);

static BuddyInfo *head;
static uint32_t g_block_info_addr;
static BuddyState g_buddy;

int32_t buddy_check_child_block_is_used(BuddyInfo *arg1)
{
    int32_t result = arg1->used;

    if (result != 1) {
        int32_t a1_1 = arg1->left;

        if (a1_1 != 0) {
            int32_t i = arg1->right;

            if (i != 0) {
                int32_t buddy_1 = g_buddy.d;

                do {
                    result = buddy_check_child_block_is_used((BuddyInfo *)(intptr_t)(a1_1 + buddy_1));

                    if (result != 0) {
                        break;
                    }

                    {
                        BuddyInfo *v1_1 = (BuddyInfo *)(intptr_t)(i + buddy_1);

                        result = v1_1->used;
                        if (result == 1) {
                            break;
                        }

                        a1_1 = v1_1->left;
                        if (a1_1 == 0) {
                            break;
                        }

                        i = v1_1->right;
                    }
                } while (i != 0);
            }
        }
    }

    return result;
}

BuddyInfo *buddy_request_block(BuddyInfo *arg1, int32_t arg2)
{
    int32_t v1 = arg1->size;

    if (v1 < (arg2 << 1)) {
        if (v1 >= arg2 && arg1->used == 0 && buddy_check_child_block_is_used(arg1) == 0) {
            arg1->used = 1;
            return arg1;
        }
    } else if (arg1->used == 0) {
        int32_t a0 = arg1->left;

        if (a0 == 0 || arg1->right == 0) {
            arg1->used = 1;
            return arg1;
        }

        {
            int32_t buddy_1 = g_buddy.d;
            BuddyInfo *v0_4 = buddy_request_block((BuddyInfo *)(intptr_t)(a0 + buddy_1), arg2);

            if (v0_4 == 0) {
                v0_4 = buddy_request_block((BuddyInfo *)(intptr_t)(arg1->right + buddy_1), arg2);
                if (v0_4 == 0) {
                    return 0;
                }
            }

            {
                int32_t v1_1 = arg1->left;

                v0_4->used = 1;

                {
                    int32_t v1_3 = *(int32_t *)(intptr_t)(v1_1 + buddy_1 + 0x1c);

                    if (v1_3 == 1) {
                        int32_t a0_3 = *(int32_t *)(intptr_t)(arg1->right + buddy_1 + 0x1c);

                        if (a0_3 == v1_3) {
                            arg1->used = a0_3;
                        }
                    }
                }
            }

            return v0_4;
        }
    }

    return 0;
}

BuddyInfo *buddy_release_block(BuddyInfo *arg1, int32_t arg2)
{
    if (arg2 == arg1->addr && arg1->used == 1) {
        arg1->used = 0;
        return arg1;
    }

    {
        int32_t v0_1 = arg1->left;

        if (v0_1 == 0 || arg1->right == 0) {
            return 0;
        }

        {
            int32_t buddy_1 = g_buddy.d;
            BuddyInfo *v0_2 = buddy_release_block((BuddyInfo *)(intptr_t)(v0_1 + buddy_1), arg2);
            BuddyInfo *v1_2 = v0_2;

            if (v0_2 == 0) {
                BuddyInfo *v0_10 = buddy_release_block((BuddyInfo *)(intptr_t)(arg1->right + buddy_1), arg2);

                v1_2 = v0_10;
                if (v0_10 == 0) {
                    return 0;
                }
            }

            {
                int32_t v0_3 = arg1->left;

                v1_2->used = 0;
                if (*(int32_t *)(intptr_t)(v0_3 + buddy_1 + 0x1c) != 0 &&
                    *(int32_t *)(intptr_t)(arg1->right + buddy_1 + 0x1c) != 0) {
                    return v1_2;
                }

                arg1->used = 0;
                return v1_2;
            }
        }
    }
}

int32_t buddy_add_list(BuddyInfo *arg1)
{
    int32_t a2 = arg1->size;
    int32_t a3 = arg1->addr;
    int32_t a2_2 = (int32_t)(((uint32_t)a2 >> 0x1f) + a2) >> 1;

    if (a2_2 == 0) {
        __builtin_trap();
    }

    {
        uint32_t block_info_addr_1 = g_block_info_addr;
        int32_t t1 = arg1->level;

        arg1->left = (int32_t)(block_info_addr_1 + 0x20);
        arg1->right = (int32_t)(block_info_addr_1 + 0x40);
        *(int32_t *)(intptr_t)(block_info_addr_1 + 0x38) = a2_2;

        {
            int32_t t3 = arg1->size;
            int32_t t0 = arg1->addr;
            int32_t v0_3 = (int32_t)(((uint32_t)t3 >> 0x1f) + t3) >> 1;
            int32_t a3_2 = arg1->level;

            *(int32_t *)(intptr_t)(block_info_addr_1 + 0x34) = a3;
            *(int32_t *)(intptr_t)(block_info_addr_1 + 0x28) = t1 + 1;
            *(int32_t *)(intptr_t)(block_info_addr_1 + 0x30) = 1;
            *(int32_t *)(intptr_t)(block_info_addr_1 + 0x3c) = 0;
            g_block_info_addr = block_info_addr_1 + 0x40;
            *(int32_t *)(intptr_t)(block_info_addr_1 + 0x2c) = a3 / a2_2;
            *(int32_t *)(intptr_t)(block_info_addr_1 + 0x58) = v0_3;

            if (v0_3 == 0) {
                __builtin_trap();
            }

            *(int32_t *)(intptr_t)(block_info_addr_1 + 0x48) = a3_2 + 1;
            *(int32_t *)(intptr_t)(block_info_addr_1 + 0x50) = 2;
            *(int32_t *)(intptr_t)(block_info_addr_1 + 0x54) = t0 + v0_3;
            *(int32_t *)(intptr_t)(block_info_addr_1 + 0x5c) = 0;
            *(int32_t *)(intptr_t)(block_info_addr_1 + 0x4c) = t0 / v0_3 + 1;

            if (t3 < 0x4002) {
                int32_t buddy_2 = g_buddy.d;

                arg1->left = (int32_t)(block_info_addr_1 + 0x20 - (uint32_t)buddy_2);
                arg1->right = (int32_t)(block_info_addr_1 + 0x40 - (uint32_t)buddy_2);
                return 0;
            }

            {
                int32_t result = buddy_add_list((BuddyInfo *)(intptr_t)(block_info_addr_1 + 0x20));

                if (result != 0) {
                    imp_log_fun(6, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x6d,
                        "buddy_add_list", "buddy_add_list error\n", &_gp);
                    return result;
                }

                result = buddy_add_list((BuddyInfo *)(intptr_t)arg1->right);
                if (result == 0) {
                    int32_t buddy_1 = g_buddy.d;
                    int32_t v1_3 = arg1->right - buddy_1;

                    arg1->left -= buddy_1;
                    arg1->right = v1_3;
                    return 0;
                }

                imp_log_fun(6, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                    "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x73,
                    "buddy_add_list", "buddy_add_list error\n", &_gp);
                return result;
            }
        }
    }
}

int32_t buddy_dump_info(BuddyInfo *arg1)
{
    BuddyInfo *s4 = arg1;

    while (1) {
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Buddy Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0xd2,
            "buddy_dump_info", "\ninfo->level = %d\n", s4->level);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Buddy Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0xd3,
            "buddy_dump_info", "info->level_num = %d\n", s4->level_num);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Buddy Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0xd4,
            "buddy_dump_info", "info->token = %d\n", s4->token);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Buddy Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0xd5,
            "buddy_dump_info", "info->addr  = 0x%08x\n", s4->addr + g_buddy.d);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Buddy Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0xd6,
            "buddy_dump_info", "info->size  = %d\n", s4->size);
        imp_log_fun(4, IMP_Log_Get_Option(), 2, "Buddy Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0xd7,
            "buddy_dump_info", "info->used  = %d\n", s4->used);

        {
            int32_t result = s4->left;

            if (result == 0 || s4->right == 0) {
                return result;
            }

            buddy_dump_info((BuddyInfo *)(intptr_t)(result + g_buddy.d));
            s4 = (BuddyInfo *)(intptr_t)(s4->right + g_buddy.d);
        }
    }
}

int32_t buddy_alloc(int32_t arg1)
{
    BuddyInfo *head_1 = head;

    if (head_1 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Buddy Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x148,
            "buddy_alloc", "buddy memery manager no inited\n");
        return 0;
    }

    {
        BuddyInfo *v0 = buddy_request_block(head_1, arg1);

        if (v0 != 0) {
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x152,
                "buddy_alloc", "======= [%s] =========\n", "buddy_alloc");
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x153,
                "buddy_alloc", "info->level = %d\n", v0->level);
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x154,
                "buddy_alloc", "info->level_num = %d\n", v0->level_num);
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x155,
                "buddy_alloc", "info->token = %d\n", v0->token);
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x156,
                "buddy_alloc", "info->addr  = 0x%08x\n", v0->addr + g_buddy.d);
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x157,
                "buddy_alloc", "info->size  = %d\n", v0->size);
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x158,
                "buddy_alloc", "info->used  = %d\n", v0->used);
            return v0->addr + g_buddy.d;
        }
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "Buddy Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x14e,
        "buddy_alloc", "Can't alloc memery\n");
    return 0;
}

int32_t buddy_init(void *arg1, int32_t arg2)
{
    if (arg1 == 0) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Buddy Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x11a,
            "buddy_init", "Mem Manager mode init error, base addr is NULL\n", &_gp);
        return -1;
    }

    if (arg2 < 0x2000) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Buddy Alloc",
            "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x11f,
            "buddy_init", "Mem Manager mode init error, buddy size is too small\n", &_gp);
        return -1;
    }

    g_buddy.d = (int32_t)(intptr_t)arg1;
    g_buddy.size = arg2;
    memset(arg1, 0, (uint32_t)arg2);

    {
        BuddyInfo *head_1 = head;

        if (head_1 == 0) {
            head_1 = (BuddyInfo *)((char *)arg1 + 4);
            head = head_1;
            *(int32_t *)((char *)arg1 + 0xc) = 0;
            *(int32_t *)((char *)arg1 + 0x10) = 0;
            *(int32_t *)((char *)arg1 + 0x14) = 0;
            *(int32_t *)((char *)arg1 + 0x18) = 0;
            *(int32_t *)((char *)arg1 + 0x1c) = arg2;
            *(int32_t *)((char *)arg1 + 0x20) = 0;
            g_block_info_addr = (uint32_t)(intptr_t)head_1;
        }

        {
            int32_t result = buddy_add_list(head_1);

            if (result != 0) {
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                    "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0xef,
                    "buddy_init_list", "buddy_add_list error\n", &_gp);
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                    "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x109,
                    "buddy_base_init", "buddy init list error\n");
                imp_log_fun(6, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                    "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x132,
                    "buddy_init", "buddy base init error\n");
                return result;
            }

            if (buddy_alloc((int32_t)(g_block_info_addr - (uint32_t)g_buddy.d)) != 0) {
                __builtin_strncpy((char *)arg1, "ZZZZ", 4);
                return 0;
            }
        }
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "Buddy Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x137,
        "buddy_init", "buddy alloc block info mem error\n", &_gp);
    return -1;
}

int32_t buddy_free(int32_t arg1)
{
    BuddyInfo *head_1 = head;
    const char *var_2c;
    int32_t v1_8;

    if (head_1 == 0) {
        var_2c = "buddy memery manager no inited\n";
        v1_8 = 0x162;
    } else {
        BuddyInfo *v0 = buddy_release_block(head_1, arg1 - g_buddy.d);

        if (v0 != 0) {
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x16c,
                "buddy_free", "======= [%s] =========\n", "buddy_free");
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x16d,
                "buddy_free", "info->level = %d\n", v0->level);
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x16e,
                "buddy_free", "info->level_num = %d\n", v0->level_num);
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x16f,
                "buddy_free", "info->token = %d\n", v0->token);
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x170,
                "buddy_free", "info->addr  = 0x%08x\n", v0->addr + g_buddy.d);
            imp_log_fun(3, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x171,
                "buddy_free", "info->size  = %d\n", v0->size);
            return imp_log_fun(3, IMP_Log_Get_Option(), 2, "Buddy Alloc",
                "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", 0x172,
                "buddy_free", "info->used  = %d\n", v0->used);
        }

        var_2c = "Can't free memery\n";
        v1_8 = 0x168;
    }

    return imp_log_fun(6, IMP_Log_Get_Option(), 2, "Buddy Alloc",
        "/home/user/git/proj/sdk-lv3/src/imp/core/imp_alloc/buddy_alloc.c", v1_8,
        "buddy_free", var_2c);
}

int32_t buddy_dump(void)
{
    BuddyInfo *head_1 = head;

    if (head_1 == 0) {
        return 0x120000;
    }

    return buddy_dump_info(head_1);
}
