#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
int32_t imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
int32_t IMP_MemPool_GetById(int32_t arg1); /* forward decl, ported by T<N> later */

static void *g_encoder_pools;
static void *g_framesource_pools;

int32_t IMP_Encoder_ClearPoolId(void)
{
    void *pools_1 = g_encoder_pools;

    if (pools_1 != 0) {
        free(pools_1);
    }

    g_encoder_pools = 0;
    return 0;
}

int32_t IMP_Encoder_SetPool(int32_t arg1, int32_t arg2)
{
    int32_t var_24_1;
    int32_t v0_4;
    char const *a0_2;
    int32_t a1;

    if ((uint32_t)arg1 >= 0x21) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "MemPool",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_mempool.c", 0x1e,
            "IMP_Encoder_SetPool", "%s(%d):chnNum(%d) error\n",
            "IMP_Encoder_SetPool", 0x1e, arg1);
        return -1;
    }

    if (IMP_MemPool_GetById(arg2) == 0) {
        v0_4 = IMP_Log_Get_Option();
        a1 = 0x23;
        var_24_1 = 0x23;
        a0_2 = "%s(%d):POOL is not init\n";
    } else {
        void *pools_1 = g_encoder_pools;

        if (pools_1 == 0) {
            void *str = malloc(0x80);

            g_encoder_pools = str;
            if (str == 0) {
                v0_4 = IMP_Log_Get_Option();
                a1 = 0x2a;
                var_24_1 = 0x2a;
                a0_2 = "%s(%d):malloc pool error\n";
                goto label_1;
            }

            pools_1 = memset(str, 0xffffffff, 0x80);
        }

        if (*(int32_t *)((char *)pools_1 + (arg1 << 2)) < 0) {
            *(int32_t *)((char *)pools_1 + (arg1 << 2)) = arg2;
            return 0;
        }

        v0_4 = IMP_Log_Get_Option();
        a1 = 0x31;
        var_24_1 = 0x31;
        a0_2 = "%s(%d):pools already set\n";
    }

label_1:
    imp_log_fun(6, v0_4, 2, "MemPool",
        "/home/user/git/proj/sdk-lv3/src/imp/video/imp_mempool.c", a1,
        "IMP_Encoder_SetPool", a0_2, "IMP_Encoder_SetPool", var_24_1);
    return -1;
}

int32_t IMP_Encoder_GetPool(int32_t arg1)
{
    if ((uint32_t)arg1 >= 0x21) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "MemPool",
            "/home/user/git/proj/sdk-lv3/src/imp/video/imp_mempool.c", 0x3b,
            "IMP_Encoder_GetPool", "%s(%d):chnNum(%d) error\n",
            "IMP_Encoder_GetPool", 0x3b, arg1);
        return -1;
    }

    if (g_encoder_pools != 0) {
        int32_t result = *(int32_t *)((char *)g_encoder_pools + (arg1 << 2));

        if (result != -1) {
            return result;
        }
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "MemPool",
        "/home/user/git/proj/sdk-lv3/src/imp/video/imp_mempool.c", 0x40,
        "IMP_Encoder_GetPool", "%s(%d):chnNum: %d not bind pool\n",
        "IMP_Encoder_GetPool", 0x40, arg1);
    return -1;
}

int32_t IMP_FrameSource_ClearPoolId(void)
{
    void *pools_1 = g_framesource_pools;

    if (pools_1 != 0) {
        free(pools_1);
    }

    g_framesource_pools = 0;
    return 0;
}

int32_t IMP_FrameSource_SetPool(int32_t arg1, int32_t arg2)
{
    int32_t var_24_1;
    int32_t v0_4;
    char const *a0_2;
    int32_t a1;

    if ((uint32_t)arg1 >= 0x21) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c", 0xcbe,
            "IMP_FrameSource_SetPool", "%s(%d):chnNum(%d) error\n",
            "IMP_FrameSource_SetPool", 0xcbe, arg1);
        return -1;
    }

    if (IMP_MemPool_GetById(arg2) == 0) {
        v0_4 = IMP_Log_Get_Option();
        a1 = 0xcc2;
        var_24_1 = 0xcc2;
        a0_2 = "%s(%d):POOL is not init\n";
    } else {
        void *pools_1 = g_framesource_pools;

        if (pools_1 == 0) {
            void *str = malloc(0x80);

            g_framesource_pools = str;
            if (str == 0) {
                v0_4 = IMP_Log_Get_Option();
                a1 = 0xcc8;
                var_24_1 = 0xcc8;
                a0_2 = "%s(%d):malloc pool error\n";
                goto label_2;
            }

            pools_1 = memset(str, 0xffffffff, 0x80);
        }

        if (*(int32_t *)((char *)pools_1 + (arg1 << 2)) < 0) {
            *(int32_t *)((char *)pools_1 + (arg1 << 2)) = arg2;
            return 0;
        }

        v0_4 = IMP_Log_Get_Option();
        a1 = 0xccf;
        var_24_1 = 0xccf;
        a0_2 = "%s(%d):pools already set\n";
    }

label_2:
    imp_log_fun(6, v0_4, 2, "Framesource",
        "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c", a1,
        "IMP_FrameSource_SetPool", a0_2, "IMP_FrameSource_SetPool", var_24_1);
    return -1;
}

int32_t IMP_FrameSource_GetPool(int32_t arg1)
{
    if ((uint32_t)arg1 >= 0x21) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
            "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c", 0xcd9,
            "IMP_FrameSource_GetPool", "%s(%d):chnNum(%d) error\n",
            "IMP_FrameSource_GetPool", 0xcd9, arg1);
        return -1;
    }

    if (g_framesource_pools != 0) {
        int32_t result = *(int32_t *)((char *)g_framesource_pools + (arg1 << 2));

        if (result != -1) {
            return result;
        }
    }

    imp_log_fun(6, IMP_Log_Get_Option(), 2, "Framesource",
        "/home/user/git/proj/sdk-lv3/src/imp/framesource/framesource_tseries.c", 0xcde,
        "IMP_FrameSource_GetPool", "%s(%d):chnNum: %d not bind pool\n",
        "IMP_FrameSource_GetPool", 0xcde, arg1);
    return -1;
}
