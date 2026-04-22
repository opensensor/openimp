#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern char _gp;
extern void *__assert(const char *expression, const char *file, int32_t line, const char *function, ...);

#define DPB_KMSG(fmt, ...)                                                                          \
    do {                                                                                            \
        int _kfd = open("/dev/kmsg", O_WRONLY);                                                     \
        if (_kfd >= 0) {                                                                            \
            char _buf[192];                                                                         \
            int _n = snprintf(_buf, sizeof(_buf), "libimp/DPB: " fmt "\n", ##__VA_ARGS__);        \
            if (_n > 0) {                                                                           \
                write(_kfd, _buf, (size_t)_n);                                                      \
            }                                                                                       \
            close(_kfd);                                                                            \
        }                                                                                           \
    } while (0)

int32_t AL_sDPB_HEVC_Reordering(void *arg1, int32_t *arg2, int32_t arg3,
                                char *arg4, int16_t *arg5,
                                char *arg6); /* forward decl, ported by T<N> later */
uint32_t AL_sDPB_Bumping(void *arg1, int32_t arg2, uint32_t arg3,
                          char *arg4, int32_t arg5, void *arg6); /* forward decl, ported by T<N> later */
int32_t AL_DPB_HEVC_GetRefInfo(char *arg1, void *arg2, void *arg3, int32_t *arg4,
                               uint8_t arg5); /* forward decl, ported in this file below */

static uint8_t *dpb_list_node(void *arg1, uint32_t arg2)
{
    return (uint8_t *)arg1 + arg2 * 5U;
}

static uint8_t *dpb_pic(void *arg1, uint32_t arg2)
{
    return (uint8_t *)arg1 + arg2 * 0x18U + 0x6cU;
}

static int32_t *dpb_validate_list_ptr(int32_t *ptr, const char *tag)
{
    uintptr_t addr = (uintptr_t)ptr;

    if (ptr == NULL) {
        return NULL;
    }
    if ((addr & 3U) != 0 || addr < 0x70000000U || addr >= 0x80000000U) {
        DPB_KMSG("AVC_GetRefInfo invalid %s list ptr=%p", tag, ptr);
        return NULL;
    }
    return ptr;
}

uintptr_t AL_sDPB_UpdateRefPtr(void *arg1)
{
    uint8_t *ctx = (uint8_t *)arg1;
    uint32_t v1_11 = (uint32_t)ctx[0x402];
    uint32_t v1_4;
    uintptr_t result;

    if (v1_11 != 0xffU) {
        uint8_t *v0_2 = dpb_list_node(ctx, v1_11);

        while (1) {
            uint32_t a1_1 = (uint32_t)v0_2[2];

            if (a1_1 != 0xffU) {
                uint8_t *v1_2 = (uint8_t *)arg1 + a1_1 * 0x18U;

                if (v1_2[0x6f] != 0) {
                    goto label_6d1c8;
                }

                if (v1_2[0x71] != 0) {
                    goto label_6d1c8;
                }

                if (v1_2[0x72] != 0) {
                    goto label_6d1c8;
                }
            }

            {
                uint32_t v1_10 = (uint32_t)v0_2[1];

                ctx[0x402] = (uint8_t)v1_10;

                if (v1_10 != 0xffU) {
                    v0_2 = dpb_list_node(ctx, v1_10);
                    continue;
                }

                v1_4 = (uint32_t)ctx[0x403];
                result = (uintptr_t)(v1_4 << 2);

                if (v1_4 == 0xffU) {
                    return result;
                }

                break;
            }
        }
    }

label_6d1c8:
    v1_4 = (uint32_t)ctx[0x403];
    result = (uintptr_t)(v1_4 << 2);

    if (v1_4 == 0xffU) {
        return result;
    }

    {
        uint8_t *result_ptr = ctx + result + v1_4;

        while (1) {
            uint32_t a1_4 = (uint32_t)result_ptr[2];

            if (a1_4 != 0xffU) {
                uint8_t *v1_7 = (uint8_t *)arg1 + a1_4 * 0x18U;

                if (v1_7[0x6f] != 0) {
                    return (uintptr_t)result_ptr;
                }

                if (v1_7[0x71] != 0) {
                    return (uintptr_t)result_ptr;
                }

                if (v1_7[0x72] != 0) {
                    return (uintptr_t)result_ptr;
                }
            }

            {
                uint32_t v1_9 = (uint32_t)result_ptr[0];

                ctx[0x403] = (uint8_t)v1_9;

                if (v1_9 == 0xffU) {
                    return 0;
                }

                result_ptr = dpb_list_node(ctx, v1_9);
            }
        }
    }
}

uintptr_t AL_sDPB_FindRefWithPOC(void *arg1, int32_t arg2)
{
    uint8_t *ctx = (uint8_t *)arg1;
    uint32_t result = (uint32_t)ctx[0x405];

    if (result != 0xffU) {
        uint32_t v1_2 = result * 0x18U;
        uint8_t *v1_4 = ctx + v1_2 + 0x6cU;

        if (arg2 < *(int32_t *)(ctx + v1_2 + 0x80U)) {
            uint8_t *a2_7;

            do {
                result = (uint32_t)*v1_4;

                if (result == 0xffU) {
                    return result;
                }

                a2_7 = ctx + result * 0x18U;
                v1_4 = a2_7 + 0x6cU;
            } while (arg2 < *(int32_t *)(a2_7 + 0x80U));
        }

        if (arg2 != *(int32_t *)(v1_4 + 0x14U)) {
            if ((uint32_t)v1_4[3] == 0U) {
                return 0xffU;
            }
        } else {
            return result;
        }
    }

    return result;
}

uintptr_t MarkPicsAsUnusedForRef(void *arg1, void *arg2)
{
    uint32_t v0_11 = (uint32_t)*((uint8_t *)arg2 + 2);

    if (v0_11 == 0xffU) {
        __builtin_trap();
    }

    {
        uint8_t *v0_2 = (uint8_t *)arg1 + v0_11 * 0x18U;
        uint32_t a2_1 = (uint32_t)v0_2[0x71];
        uint32_t v0_9;
        uint32_t v1;

        v0_2[0x6f] = 0;

        if (a2_1 != 0U || (uint32_t)v0_2[0x72] != 0U) {
            v0_9 = (uint32_t)*((uint8_t *)arg2 + 3);
            v1 = v0_9 << 3;

            if (v0_9 != 0xffU) {
                uint8_t *v0_6 = (uint8_t *)arg1 + (v0_9 << 5) - v1;

                v0_6[0x6f] = 0;
                return (uintptr_t)v0_6;
            }

            return v0_9;
        }

        *(int32_t *)((uint8_t *)arg1 + 0x3e8U) -= 1;
        v0_9 = (uint32_t)*((uint8_t *)arg2 + 3);
        v1 = v0_9 << 3;

        if (v0_9 != 0xffU) {
            uint8_t *v0_6 = (uint8_t *)arg1 + (v0_9 << 5) - v1;

            v0_6[0x6f] = 0;
            return (uintptr_t)v0_6;
        }

        return v0_9;
    }
}

uint32_t AL_sDPB_OutputPic(void *arg1, int32_t arg2)
{
    uint32_t result = 0xffU;
    uint8_t *s0 = 0;

    if (arg2 != 0xff) {
        s0 = (uint8_t *)arg1 + (uint32_t)arg2 * 0x18U + 0x6cU;
    }

    {
        uintptr_t t9 = (uintptr_t) * (int32_t *)((uint8_t *)arg1 + 0x414U);

        if (t9 != 0U) {
            result = (uint32_t)s0[2];

            if (result != 0xffU && (uint32_t)s0[4] != 0U) {
                result = (uint32_t)((uint32_t(*)(int32_t, uint32_t, int32_t, int32_t))t9)(
                    *(int32_t *)((uint8_t *)arg1 + 0x408U),
                    (uint32_t)*((uint8_t *)arg1 + result * 5U + 4U),
                    *(int32_t *)(s0 + 8),
                    *(int32_t *)(s0 + 0x14U));
            }

            s0[4] = 0;
        }
    }

    return result;
}

void *AL_sDPB_GetOldestStRef(void *arg1)
{
    uint8_t *ctx = (uint8_t *)arg1;
    uint32_t a3 = (uint32_t)ctx[0x402];
    void *result = 0;
    uint32_t t0;

    if (a3 == 0xffU) {
        t0 = (uint32_t)ctx[0x403];
    } else {
        t0 = (uint32_t)ctx[0x403];
        result = dpb_list_node(ctx, a3);

        while (a3 != t0) {
            while (1) {
                uint32_t v1_1 = (uint32_t)*((uint8_t *)result + 2);

                if (v1_1 == 0xffU) {
                    __builtin_trap();
                }

                {
                    uint8_t *v1_4 = (uint8_t *)arg1 + v1_1 * 0x18U;

                    if (v1_4[0x71] == 0 && v1_4[0x6f] != 0) {
                        return result;
                    }
                }

                {
                    uint32_t v1_6 = (uint32_t)*((uint8_t *)result + 1);

                    if (v1_6 == 0xffU) {
                        result = 0;
                        break;
                    }

                    result = dpb_list_node(ctx, v1_6);

                    if (a3 == t0) {
                        return result;
                    }
                }
            }
        }
    }

    return result;
}

uintptr_t AL_sDPB_AddPicToList(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, void *arg5,
                               void **arg6)
{
    uintptr_t result;

    DPB_KMSG("AddPicToList entry cur=%d pair=%d list=%d count=%ld", arg2, arg3, arg4,
             (long)(arg6 ? (intptr_t)*arg6 : -1));
    result = (arg2 == 0xff) ? (uintptr_t)0 : (uintptr_t)((uint8_t *)arg1 + (uint32_t)arg2 * 0x18U + 0x6cU);

    if (arg3 == 0xff) {
        void *v1_4 = 0;

        if (result == 0U) {
            __assert("p1stPic != ((void *)0)",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                     0x2c8, "AL_sDPB_AddPicToList", &_gp);
            __builtin_unreachable();
        }

        if ((uint32_t)*((uint8_t *)result + 3) != 0U) {
            if ((uint32_t)*((uint8_t *)result + 6) != 0U) {
                goto label_6d52c;
            }

            goto label_6d5f8;
        }

        goto label_6d564;
    } else {
        int32_t t0_2 = arg3 * 0x18;
        void *v1_4 = (uint8_t *)arg1 + (uint32_t)t0_2 + 0x6cU;
        int32_t a0_1;

        if (result == 0U) {
            if (v1_4 == 0) {
                __assert("p1stPic != ((void *)0)",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                         0x2c8, "AL_sDPB_AddPicToList", &_gp);
                __builtin_unreachable();
            }

            a0_1 = 0;

            if ((uint32_t)*((uint8_t *)arg1 + (uint32_t)t0_2 + 0x6fU) == 0U) {
                __assert("p1stPic != ((void *)0)",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                         0x2c8, "AL_sDPB_AddPicToList", &_gp);
                __builtin_unreachable();
            }

            goto label_6d58c;
        } else if ((uint32_t)*((uint8_t *)result + 3) != 0U && (uint32_t)*((uint8_t *)result + 6) == 0U) {
            uint32_t t0_8 = (uint32_t)*((uint8_t *)result + 5);

            a0_1 = ((t0_8 ^ (uint32_t)arg4) < 1U) ? 1 : 0;

            if (v1_4 != 0) {
                goto label_6d534;
            }

            if (t0_8 == (uint32_t)arg4) {
                goto label_6d550;
            }

            goto label_6d564;
        } else {
label_6d52c:
            a0_1 = 0;

            if (v1_4 != 0) {
                goto label_6d534;
            }

            goto label_6d564;
        }

label_6d534:
        if ((uint32_t)*((uint8_t *)v1_4 + 3) != 0U) {
            goto label_6d58c;
        }

        if (a0_1 != 0) {
            goto label_6d550;
        }

label_6d564:
        DPB_KMSG("AddPicToList keep-existing cur=%d pair=%d list=%d count=%ld", arg2, arg3, arg4,
                 (long)(arg6 ? (intptr_t)*arg6 : -1));
        return result;

label_6d58c:
        {
            int32_t t0_5 = 0;

            if ((uint32_t)*((uint8_t *)v1_4 + 6) == 0U) {
                t0_5 = ((((uint32_t)*((uint8_t *)v1_4 + 5) ^ (uint32_t)arg4) < 1U) ? 1 : 0);
            }

            if (result == 0U) {
                __assert("p1stPic != ((void *)0)",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                         0x2c8, "AL_sDPB_AddPicToList", &_gp);
                __builtin_unreachable();
            }

            if (a0_1 != 0) {
                if (t0_5 == 0 || *(int32_t *)((uint8_t *)v1_4 + 0x14U) >= *(int32_t *)((uint8_t *)result + 0x14U)) {
                    goto label_6d550;
                }

                *((uint8_t *)arg5 + (uintptr_t)*arg6) = (uint8_t)arg3;
                result = (uintptr_t)(*arg6 + 1);
                *arg6 = (void *)result;
                DPB_KMSG("AddPicToList push-pair cur=%d pair=%d list=%d new_count=%ld", arg2, arg3, arg4,
                         (long)result);
                return result;
            }

            goto label_6d564;
        }

label_6d550:
        *((uint8_t *)arg5 + (uintptr_t)*arg6) = (uint8_t)arg2;
        result = (uintptr_t)(*arg6 + 1);
        *arg6 = (void *)result;
        DPB_KMSG("AddPicToList push-cur cur=%d pair=%d list=%d new_count=%ld", arg2, arg3, arg4,
                 (long)result);
        return result;
    }

label_6d5f8:
    {
        uint32_t t0_8 = (uint32_t)*((uint8_t *)result + 5);
        int32_t a0_1 = ((t0_8 ^ (uint32_t)arg4) < 1U) ? 1 : 0;

        if ((uint8_t *)0 != 0) {
            __builtin_unreachable();
        }

        if (t0_8 == (uint32_t)arg4) {
            goto label_6d550_result_only;
        }
    }

    goto label_6d564_result_only;

label_6d550_result_only:
    *((uint8_t *)arg5 + (uintptr_t)*arg6) = (uint8_t)arg2;
    result = (uintptr_t)(*arg6 + 1);
    *arg6 = (void *)result;
    return result;

label_6d564_result_only:
    return result;
}

uintptr_t AL_sDPB_RemoveRef(void *arg1, void *arg2)
{
    if (arg2 == 0) {
        __assert("pRef != ((void *)0)",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                 0x19a, "AL_sDPB_RemoveRef", &_gp);
        __builtin_unreachable();
    }

    {
        uint32_t v0_1 = (uint32_t)*((uint8_t *)arg2 + 2);

        if (v0_1 == 0xffU) {
            __builtin_trap();
        }

        {
            uint8_t *v0_4 = (uint8_t *)arg1 + v0_1 * 0x18U;

            v0_4[0x72] = 0;
            v0_4[0x71] = 0;
        }
    }

    return AL_sDPB_UpdateRefPtr((void *)MarkPicsAsUnusedForRef(arg1, arg2));
}

static void *RemovePicPtr_part_5(void *arg1, int32_t arg2)
{
    int32_t t0 = arg2 << 3;

    if (arg2 == 0xff) {
        __assert("((pPicPtr) != 0xff)",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                 0x101, "AL_sDPB_ErasePic", &_gp);
        __builtin_unreachable();
    }

    {
        int32_t v0_1 = arg2 << 5;
        uint8_t *t1_1 = (uint8_t *)arg1 + (uint32_t)v0_1 - (uint32_t)t0;
        uint32_t v1_1 = (uint32_t)t1_1[0x6c];
        uint32_t v1_5;
        uint8_t *a3_3;

        if (v1_1 == 0xffU) {
            if ((uint32_t)*((uint8_t *)arg1 + 0x404U) != (uint32_t)arg2) {
                __assert("pPicPtr == pCtx->pFirstPicPtr",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                         0x10b, "AL_sDPB_ErasePic", &_gp);
                __builtin_unreachable();
            }

            a3_3 = (uint8_t *)arg1 + (uint32_t)v0_1 - (uint32_t)t0;
            *((uint8_t *)arg1 + 0x404U) = t1_1[0x6d];
            v1_5 = (uint32_t)a3_3[0x6d];

            if (v1_5 == 0xffU) {
                goto label_6d9f0;
            }
        } else {
            *((uint8_t *)arg1 + v1_1 * 0x18U + 0x6dU) = t1_1[0x6d];
            a3_3 = (uint8_t *)arg1 + (uint32_t)v0_1 - (uint32_t)t0;
            v1_5 = (uint32_t)a3_3[0x6d];

            if (v1_5 == 0xffU) {
label_6d9f0:
                if ((uint32_t)*((uint8_t *)arg1 + 0x405U) != (uint32_t)arg2) {
                    __assert("pPicPtr == pCtx->pLastPicPtr",
                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                             0x115, "AL_sDPB_ErasePic", &_gp);
                    __builtin_unreachable();
                }

                *((uint8_t *)arg1 + 0x405U) = a3_3[0x6c];
            } else {
                *((uint8_t *)arg1 + v1_5 * 0x18U + 0x6cU) = a3_3[0x6c];
            }
        }

        {
            uint8_t *v0_3 = (uint8_t *)arg1 + (uint32_t)v0_1 - (uint32_t)t0;
            int32_t v0_4 = *(int32_t *)((uint8_t *)arg1 + 0x3d8U);
            uint8_t *result;

            v0_3[0x6c] = 0xff;
            v0_3[0x6d] = 0xff;

            if (t1_1 != (uint8_t *)0xffffff94) {
                v0_3[0x6e] = 0xff;
                v0_3[0x6f] = 0;
                v0_3[0x70] = 0;
                v0_3[0x71] = 0;
                v0_3[0x72] = 0;
                *(int32_t *)(v0_3 + 0x74U) = 0;
                *(int32_t *)(v0_3 + 0x80U) = 0x7fffffff;
                *(int32_t *)(v0_3 + 0x7cU) = 0;
            }

            result = (uint8_t *)arg1 + (uint32_t)v0_4;
            *(int32_t *)((uint8_t *)arg1 + 0x3d8U) = v0_4 + 1;
            result[0x3b4] = (uint8_t)arg2;
            return result;
        }
    }
}

void *AL_sDPB_RemoveBuf(void *arg1, int32_t arg2, char *arg3, int32_t arg4, void *arg5)
{
    (void)arg3;
    (void)arg4;
    (void)arg5;

    if (arg2 == 0xff) {
        __builtin_trap();
    }

    {
        uint8_t *buf = (uint8_t *)arg1 + (uint32_t)arg2 * 5U;
        uint32_t a1 = (uint32_t)buf[2];
        uint32_t a1_1;
        uint32_t v1_1;
        uint32_t v1_2;
        uintptr_t t9_4;
        int32_t a0_4;
        int32_t v0_9;
        uint8_t *result;

        if (a1 != 0xffU) {
            RemovePicPtr_part_5(arg1, (int32_t)a1);
        }

        a1_1 = (uint32_t)buf[3];

        if (a1_1 != 0xffU) {
            RemovePicPtr_part_5(arg1, (int32_t)a1_1);
        }

        AL_sDPB_UpdateRefPtr(arg1);
        v1_1 = (uint32_t)buf[0];

        if (v1_1 != 0xffU) {
            *((uint8_t *)arg1 + v1_1 * 5U + 1U) = buf[1];
            v1_2 = (uint32_t)buf[1];

            if (v1_2 == 0xffU) {
                goto label_6dbd8;
            }

            goto label_6db40;
        }

        if ((uint32_t)*((uint8_t *)arg1 + 0x400U) != (uint32_t)arg2) {
            __assert("pBuf == pCtx->uHeadBuf",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                     0x149, "AL_sDPB_EraseBuf", &_gp);
            __builtin_unreachable();
        }

        *((uint8_t *)arg1 + 0x400U) = buf[1];
        v1_2 = (uint32_t)buf[1];

        if (v1_2 != 0xffU) {
label_6db40:
            *((uint8_t *)arg1 + v1_2 * 5U) = buf[0];
        }

label_6db48:
        buf[0] = 0xff;
        buf[1] = 0xff;
        t9_4 = (uintptr_t) * (int32_t *)((uint8_t *)arg1 + 0x410U);
        a0_4 = *(int32_t *)((uint8_t *)arg1 + 0x408U);
        *(int32_t *)((uint8_t *)arg1 + 0x3e0U) -= 1;
        ((void (*)(int32_t, uint32_t))t9_4)(a0_4, (uint32_t)buf[4]);
        buf[2] = 0xff;
        buf[3] = 0xff;
        buf[4] = 0xff;
        v0_9 = *(int32_t *)((uint8_t *)arg1 + 0x68U);
        result = (uint8_t *)arg1 + (uint32_t)v0_9;
        *(int32_t *)((uint8_t *)arg1 + 0x68U) = v0_9 + 1;
        result[0x55] = (uint8_t)arg2;
        return result;

label_6dbd8:
        if ((uint32_t)*((uint8_t *)arg1 + 0x401U) == (uint32_t)arg2) {
            *((uint8_t *)arg1 + 0x401U) = buf[0];
            goto label_6db48;
        }

        __assert("pBuf == pCtx->uTailBuf",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                 0x151, "AL_sDPB_EraseBuf", &_gp);
        __builtin_unreachable();
    }
}

uint32_t AL_sDPB_RemovePic(void *arg1, char *arg2, uint32_t arg3)
{
    uint32_t a3 = (uint32_t)*((uint8_t *)arg1 + 0x400U);
    uint32_t i_1 = 0xffU;
    int32_t s1;

    if (a3 != 0xffU) {
        uint32_t v0 = a3 << 2;
        uint8_t *v0_2 = (uint8_t *)arg1 + v0 + a3;
        uint32_t a1 = (uint32_t)v0_2[2];
        uint32_t s4_1 = (uint32_t)*((uint8_t *)arg1 + v0 + a3 + 1U);

        s1 = 0x60000;

        if (a1 != 0xffU) {
label_6debc:
            {
                uint8_t *i;
                uint8_t *v0_6;

                arg3 = (uint32_t)v0_2[3];
                i = dpb_pic(arg1, a1);

                if (arg3 != 0xffU) {
                    goto label_6dee4;
                }

                while (i != 0) {
                    if ((uint32_t)i[4] != 0U) {
                        goto label_6dfb0;
                    }

label_6def8:
                    if ((uint32_t)i[3] != 0U) {
                        goto label_6dfb0;
                    }

                    if ((uint32_t)i[5] != 0U) {
                        goto label_6dfb0;
                    }

                    if ((uint32_t)i[6] != 0U) {
                        goto label_6dfb0;
                    }

                    if ((uint8_t *)0 == 0) {
                        break;
                    }

                    __builtin_unreachable();
                }

                while (1) {
                    i_1 = (uint32_t)(uintptr_t)AL_sDPB_RemoveBuf(arg1, (int32_t)a3, arg2, 0x60000, arg1);

label_6df64:
                    if (s4_1 == 0xffU) {
                        goto label_6e054;
                    }

label_6df6c:
                    {
                        uint32_t v0_8 = s4_1 << 2;

                        a3 = s4_1;
                        v0_2 = (uint8_t *)arg1 + v0_8 + a3;
                        a1 = (uint32_t)v0_2[2];
                        s4_1 = (uint32_t)*((uint8_t *)arg1 + v0_8 + s4_1 + 1U);

                        if (a1 != 0xffU) {
                            goto label_6debc;
                        }
                    }

label_6df90:
                    arg3 = (uint32_t)v0_2[3];
                    i = 0;

                    if (arg3 == 0xffU) {
                        break;
                    }

label_6dee4:
                    v0_6 = dpb_pic(arg1, arg3);

                    if (i != 0) {
                        if ((uint32_t)i[4] != 0U) {
                            goto label_6dfb0;
                        }

                        goto label_6def8;
                    }

                    v0_6 = 0;
                }

label_6dfb0:
                i_1 = (uint32_t)*((uint8_t *)arg1 + 0x402U);

                if (i_1 != a3) {
                    goto label_6df64;
                }

                {
                    uint8_t *v1_15;

                    if (a1 == 0xffU) {
                        i_1 = 0;

                        if (arg3 != 0xffU) {
label_6dfdc:
                            {
                                uint32_t v1_12 = arg3 << 3;

                                arg3 <<= 5;
                                v1_15 = (uint8_t *)arg1 + arg3 - v1_12 + 0x6cU;

                                if (i_1 != 0U) {
                                    goto label_6dff8;
                                }

label_6e020:
                                i_1 = (uint32_t)v1_15[3];

                                if (i_1 != 0U) {
                                    goto label_6df64;
                                }

                                i_1 = (uint32_t)v1_15[5];

                                if (i_1 != 0U) {
                                    goto label_6df64;
                                }

                                i_1 = (uint32_t)v1_15[6];

                                if (i_1 != 0U) {
                                    goto label_6df64;
                                }
                            }
                        } else {
                            v1_15 = 0;
                        }
                    } else {
                        i_1 = (uint32_t)(uintptr_t)dpb_pic(arg1, a1);

                        if (arg3 != 0xffU) {
                            goto label_6dfdc;
                        }

                        v1_15 = 0;
                    }

label_6dff8:
                    if ((uint32_t)*((uint8_t *)(uintptr_t)i_1 + 3U) != 0U) {
                        goto label_6df64;
                    }

                    if ((uint32_t)*((uint8_t *)(uintptr_t)i_1 + 5U) != 0U) {
                        goto label_6df64;
                    }

                    i_1 = (uint32_t)*((uint8_t *)(uintptr_t)i_1 + 6U);

                    if (i_1 != 0U) {
                        goto label_6df64;
                    }

                    if (v1_15 != 0) {
                        goto label_6e020;
                    }
                }

                i_1 = (uint32_t)AL_sDPB_UpdateRefPtr(arg1);

                if (s4_1 != 0xffU) {
                    goto label_6df6c;
                }
            }
        } else {
            s1 = 0x60000;
            arg3 = (uint32_t)v0_2[3];

            if (arg3 != 0xffU) {
                goto label_6dee4;
            }
        }
    } else {
        s1 = 0x60000;
    }

label_6e054:
    if (arg2 != 0) {
        for (i_1 = (uint32_t)*(int32_t *)((uint8_t *)arg1 + 0x3e0U); i_1 != 0U;
             i_1 = (uint32_t)*(int32_t *)((uint8_t *)arg1 + 0x3e0U)) {
            arg3 = (uint32_t)AL_sDPB_Bumping(arg1, 1, arg3, (char *)AL_sDPB_Bumping, s1, arg1);
        }
    }

    return i_1;
}

int32_t AL_sDPB_HEVC_Reordering(void *arg1, int32_t *arg2, int32_t arg3,
                                char *arg4, int16_t *arg5, char *arg6)
{
    int32_t t2 = arg3;
    char *t1 = arg4;
    int32_t *t6_1 = arg2;
    int32_t *s2_1 = arg2;
    int16_t *s3_1 = arg5;
    void *t3_1 = arg1;
    int32_t s1_1 = 0;
    uint32_t t7_1 = 0xffU;
    uint8_t t8_1 = 1;
    char *t5_1 = 0;
    int32_t result;

    if (arg3 <= 0) {
        goto label_6d7f0;
    }

    do {
        int32_t t4_1 = *s2_1;
        uint32_t v0_1 = (uint32_t)AL_sDPB_FindRefWithPOC(t3_1, t4_1);
        int32_t v1_3 = 0;

        if (v0_1 == t7_1) {
            __assert("((pPicPtr) != 0xff)",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                     0x396, "AL_sDPB_HEVC_Reordering", &_gp);
            __builtin_unreachable();
        }

        {
            uint8_t *v0_4 = (uint8_t *)t3_1 + v0_1 * 0x18U;

            t5_1 = t1;

            if (v0_4[0x6f] == 0 && v0_4[0x71] == 0) {
                __assert("(((pPicPtr) != 0xff) ? &(pCtx)->PicPool[(pPicPtr)] : ((void *)0))->bUsedForReference || (((pPicPtr) != 0xff) ? &(pCtx)->PicPool[(pPicPtr)] : ((void *)0))->bLongTerm",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                         0x397, "AL_sDPB_HEVC_Reordering", &_gp);
                __builtin_unreachable();
            }
        }

        {
            char *a0_1 = t1;

            do {
                uint32_t v0_9 = (uint32_t)(uint8_t)*a0_1;

                if (v0_9 == t7_1) {
                    __builtin_trap();
                }

                if (t4_1 == *(int32_t *)((uint8_t *)t3_1 + v0_9 * 0x18U + 0x80U)) {
                    break;
                }

                v1_3 += 1;
                a0_1 = &a0_1[1];
            } while (t2 != v1_3);
        }

        *s3_1 = (int16_t)v1_3;

        if (v1_3 != s1_1) {
            *arg6 = (char)t8_1;
        }

        s1_1 += 1;
        s2_1 = &s2_1[1];
        s3_1 = &s3_1[2];
    } while (t2 != s1_1);

    {
        int32_t t4_2 = 0;

        do {
            uint32_t v0_11 = (uint32_t)AL_sDPB_FindRefWithPOC(t3_1, *t6_1);
            char *v1_4 = &t1[t2 - 1];
            int32_t a0_3 = t2 - 1;

            if (t4_2 < t2 - 1) {
                while (1) {
                    uint32_t t0_3 = (uint32_t)(uint8_t)t1[a0_3];

                    if (v0_11 == t0_3) {
                        char t0_4 = t1[a0_3 - 1];

                        v1_4 = &v1_4[-1];
                        a0_3 -= 2;
                        v1_4[1] = t0_4;

                        if (t5_1 == v1_4) {
                            break;
                        }

                        t4_2 += 1;
                    } else {
                        *v1_4 = (char)t0_3;
                        v1_4 = &v1_4[-1];
                        a0_3 -= 1;

                        if (t5_1 == v1_4) {
                            break;
                        }
                    }

                    t4_2 += 1;
                }
            }

            *t5_1 = (char)v0_11;
            t6_1 = &t6_1[1];
            t5_1 = &t5_1[1];
            t4_2 += 1;
        } while (t2 != t4_2);
    }

    result = (t2 < 0x20) ? 1 : 0;

    if (result != 0) {
label_6d7f0:
        char *i = &t1[t2];

        result = -1;
        do {
            *i = (char)0xff;
            i = &i[1];
        } while (i != &t1[0x20]);
    }

    return result;
}

uint32_t AL_sDPB_Bumping(void *arg1, int32_t arg2, uint32_t arg3,
                         char *arg4, int32_t arg5, void *arg6)
{
    (void)arg6;

    {
        uint32_t a3 = (uint32_t)*(uint8_t *)((uint8_t *)arg1 + 0x404U);
        uint32_t result = 0xffU;

        if (a3 != 0xffU) {
            uint32_t v0_1 = a3 * 0x18U;
            uint8_t *v0_3 = (uint8_t *)arg1 + v0_1 + 0x6cU;

            if (*((uint8_t *)arg1 + v0_1 + 0x70U) == 0) {
                while (1) {
                    arg3 = (uint32_t)v0_3[1];

                    if (arg3 == 0xffU) {
                        break;
                    }

                    {
                        uint32_t v0_5 = arg3 * 0x18U;

                        v0_3 = (uint8_t *)arg1 + v0_5 + 0x6cU;

                        if (*((uint8_t *)arg1 + v0_5 + 0x70U) != 0) {
                            a3 = arg3;
                            break;
                        }
                    }
                }
            }

            {
                uint32_t s4_1 = (uint32_t)*((uint8_t *)arg1 + a3 * 0x18U + 0x6eU);

                if (s4_1 == 0xffU) {
                    __builtin_trap();
                }

                {
                    uint8_t *v0_10 = dpb_list_node(arg1, s4_1);
                    uint32_t s0 = (uint32_t)v0_10[2];
                    uint32_t s5_1 = (uint32_t)v0_10[3];
                    uint32_t s7_1;
                    uint32_t v0_17;

                    if (s0 != 0xffU) {
                        AL_sDPB_OutputPic(arg1, (int32_t)s0);
                        result = (uint32_t)(uintptr_t)((uint8_t *)arg1 + s0 * 0x18U);
                        s7_1 = (uint32_t)*((uint8_t *)(uintptr_t)result + 0x6fU);

                        if (s5_1 != 0xffU) {
                            if (s5_1 == s0) {
                                return (uint32_t)(uintptr_t)AL_sDPB_RemoveBuf(arg1, (int32_t)s4_1, arg4, arg5, arg6);
                            }

                            AL_sDPB_OutputPic(arg1, (int32_t)s5_1);
                            result = (uint32_t)*((uint8_t *)arg1 + s5_1 * 0x18U + 0x6fU);

                            if (result == 0U) {
                                s0 = s5_1;
                                goto label_6dda4;
                            }

                            s0 = s5_1;
                            if (s7_1 == 0U) {
label_6dda4:
                                v0_17 = s0 << 3;
                                goto label_6ddb4;
                            }

                            if (result == 0U) {
                                goto label_6de34;
                            }
                        } else if (s7_1 == 0U) {
                            goto label_6dda4;
                        }

                        return result;
                    }

                    if (s5_1 != 0xffU) {
                        AL_sDPB_OutputPic(arg1, (int32_t)s5_1);
                        result = (uint32_t)*((uint8_t *)arg1 + s5_1 * 0x18U + 0x6fU);
                        s0 = s5_1;

                        if (result == 0U) {
                            goto label_6de34;
                        }

                        return result;
                    }

                    return result;

label_6de34:
                    if (s7_1 == 0U) {
                        v0_17 = s0 << 3;
label_6ddb4:
                        result = (uint32_t)*((uint8_t *)arg1 + (s0 << 5) - v0_17 + 0x71U);

                        if (result == 0U || arg2 != 0) {
                            return (uint32_t)(uintptr_t)AL_sDPB_RemoveBuf(arg1, (int32_t)s4_1, arg4, arg5, arg6);
                        }
                    }
                }
            }
        }

        return result;
    }
}

int32_t AL_sDPB_AVC_Reordering_isra_8(void *arg1, int32_t *arg2, int32_t arg3,
                                      int32_t *arg4, int32_t arg5, uint8_t *arg6,
                                      int32_t *arg7, int16_t *arg8, uint8_t *arg9)
{
    int32_t t6_1 = arg3;
    int32_t *s4_1 = arg4;
    int16_t *s5_1 = &arg8[1];
    uint8_t *t1_1 = arg6;
    int32_t t5_1 = 0;

    if (arg5 <= 0) {
        *arg9 = 0;
        *arg8 = 3;
        *arg7 = arg5;
    } else {
        int16_t s3_1 = (int16_t)(*(int32_t *)((uint8_t *)arg1 + 0x3f8U) - 1);
        uint32_t fp_1;

        do {
            int32_t s2_1 = *s4_1;
            uint32_t v0_1 = (uint32_t)AL_sDPB_FindRefWithPOC(arg1, s2_1);

            if (v0_1 == 0xffU) {
                __assert("((pPicPtr) != 0xff)",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                         0x3cc, "AL_sDPB_AVC_Reordering", &_gp);
                __builtin_unreachable();
            }

            {
                uint32_t s6_1 = v0_1 << 3;
                uint32_t s7_1 = v0_1 << 5;
                uint32_t fp_1_local = (uint32_t)*((uint8_t *)arg1 + s7_1 - s6_1 + 0x6fU);

                if (fp_1_local == 0U) {
                    __assert("(((pPicPtr) != 0xff) ? &(pCtx)->PicPool[(pPicPtr)] : ((void *)0))->bUsedForReference",
                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                             0x3cd, "AL_sDPB_AVC_Reordering", &_gp);
                    __builtin_unreachable();
                }

                if (s2_1 == *arg2) {
                    __assert("(*pReoList)[i].iPOC != pPict->iPOC",
                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                             0x3cf, "AL_sDPB_AVC_Reordering", &_gp);
                    __builtin_unreachable();
                }

                {
                    int32_t *a1_1 = s4_1;
                    uint8_t *a0_4 = t1_1;
                    int32_t a2 = t5_1;
                    uint32_t s6_2;

                    while (1) {
                        uint32_t v1_6 = (uint32_t)*a0_4;

                        if (v1_6 == 0xffU) {
                            __builtin_trap();
                        }

                        a2 += 1;

                        if (*(int32_t *)((uint8_t *)arg1 + v1_6 * 0x18U + 0x80U) == *a1_1) {
                            break;
                        }

                        a0_4 = &a0_4[1];
                        a1_1 = &a1_1[1];

                        if (a2 >= arg5) {
                            fp_1 = (t5_1 > 0) ? 1U : 0U;
                            goto label_6e724;
                        }
                    }

                    {
                        int32_t a0_5 = arg5 - 1;
                        uint8_t *a1_2 = &arg6[a0_5];
                        uint8_t *v1_7 = a1_2;

                        if (t5_1 < a0_5) {
                            while (1) {
                                uint32_t a1_3 = (uint32_t)*a1_2;

                                if (v0_1 != a1_3) {
                                    *v1_7 = (uint8_t)a1_3;
                                    v1_7 = &v1_7[-1];
                                    a0_5 -= 1;

                                    if (v1_7 == t1_1) {
                                        break;
                                    }
                                } else {
                                    uint8_t a1_5 = arg6[a0_5 - 1];

                                    v1_7 = &v1_7[-1];
                                    a0_5 -= 2;
                                    v1_7[1] = a1_5;

                                    if (v1_7 == t1_1) {
                                        break;
                                    }
                                }

                                a1_2 = &arg6[a0_5];
                            }

                            s6_2 = s7_1 - s6_1;
                        } else {
                            s6_2 = s7_1 - s6_1;
                        }

                        *t1_1 = (uint8_t)v0_1;

                        {
                            uint8_t *s6_3 = (uint8_t *)arg1 + s6_2;

                            if (s6_3[0x71] == 0) {
                                int32_t v0_4 = *(int32_t *)(s6_3 + 0x7cU);
                                int32_t t6_2 = v0_4 - t6_1;

                                if (t6_2 <= 0) {
                                    int32_t t6_4 = ~t6_2;

                                    if (t6_4 == -1) {
                                        *s5_1 = s3_1;
                                        *(s5_1 - 2) = 0;
                                        t6_1 = v0_4;
                                    } else {
                                        *s5_1 = (int16_t)t6_4;
                                        *(s5_1 - 2) = 0;
                                        t6_1 = v0_4;
                                    }
                                } else {
                                    *s5_1 = (int16_t)(t6_2 - 1);
                                    *(s5_1 - 2) = 1;
                                    t6_1 = v0_4;
                                }
                            } else {
                                *(s5_1 - 2) = 2;
                                *s5_1 = 0;
                            }
                        }
                    }
                }
            }

            t5_1 += 1;
            s4_1 = &s4_1[1];
            s5_1 = &s5_1[2];
            t1_1 = &t1_1[1];
        } while (arg5 != t5_1);

        t5_1 = arg5;
        fp_1 = 0U;

label_6e724:
        *arg9 = (uint8_t)fp_1;
        arg8[t5_1 * 2] = 3;

        {
            int32_t result = (arg5 < 0x20) ? 1 : 0;

            *arg7 = arg5;

            if (result != 0) {
                uint8_t *i = &arg6[arg5];

                do {
                    *i = 0xffU;
                    i = &i[1];
                } while (&arg6[0x20] != i);

                return -1;
            }

            return result;
        }
    }

    return -1;
}

uint32_t AL_sDPB_BuildRefList_isra_7(char *arg1, int32_t arg2, int32_t *arg3,
                                     uint8_t *arg4, int32_t *arg5, uint8_t *arg6,
                                     int32_t *arg7)
{
    uint32_t i_5 = (uint32_t)(uint8_t)arg1[0x405];
    char *fp = arg1;
    int32_t v1 = 2;
    uint32_t i_2 = i_5;
    uint32_t i_4;

    if (*arg3 != 0) {
        v1 = 1;
    }

    DPB_KMSG("BuildRefList entry poc=%d slice_type=%d head0=%u", arg2, *arg3, i_5);

    if (i_5 != 0xffU) {
        uint32_t a0 = i_5 << 3;
        uint32_t v0 = i_5 << 5;
        uint32_t i_3 = i_5;

        if (arg2 < *(int32_t *)(fp + v0 - a0 + 0x80)) {
            uint32_t i = (uint32_t)(uint8_t)fp[v0 - a0 + 0x6c];

            while (i != 0xffU) {
                uint32_t a0_1 = i << 3;
                uint32_t v0_3 = i << 5;

                if (arg2 >= *(int32_t *)(fp + v0_3 - a0_1 + 0x80)) {
                    break;
                }

                i_3 = i;
                i = (uint32_t)(uint8_t)fp[v0_3 - a0_1 + 0x6c];
            }

            i_2 = i;
            i_4 = i_3;
        } else {
            i_4 = 0xffU;
        }
    } else {
        i_4 = 0xffU;
    }

    if (arg5 == 0 || arg7 == 0) {
        __assert("pNumRefL0 && pNumRefL1",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                 0x322, "AL_sDPB_BuildRefList");
        __builtin_unreachable();
    }

    {
        uint8_t *s2 = arg4;

        *arg5 = 0;
        *arg7 = 0;

        {
            int32_t var_48 = 0;

            while (1) {
                int32_t v0_12 = ((i_2 ^ 0xffU) < 1U) ? 1 : 0;
                void *var_50 = 0;
                int32_t s0_1 = 0;
                uint8_t i_7;
                char *s6_1 = fp;

                if (var_48 != 0) {
                    v0_12 = (0U < (i_4 ^ 0xffU)) ? 1 : 0;
                }

                i_7 = (uint8_t)i_4;

                if (v0_12 == 0) {
                    i_7 = (uint8_t)i_2;
                }

                DPB_KMSG("BuildRefList pass=%d start=%u alt=%u mode=%d", var_48,
                         (uint32_t)i_7, (uint32_t)i_4, v0_12);

                while (1) {
                    uint32_t i_6 = (uint32_t)i_7;
                    int32_t s1_1 = (0U < (uint32_t)v0_12) ? 1 : 0;
                    uint32_t v1_9;
                    uint8_t *fp_1;
                    uint32_t s5_1;
                    void **var_5c_1 = &var_50;
                    int32_t v0_23;

                    if (i_6 == 0xffU) {
                        goto label_6e2ec;
                    }

label_6e240:
                    v1_9 = i_6 << 3;

label_6e250:
                    {
                        uint32_t v1_10 = (uint32_t)(uint8_t)s6_1[(i_6 << 5) - v1_9 + 0x6e];

                        if (v1_10 == 0xffU) {
                            __builtin_trap();
                        }

                        {
                            uint8_t *v0_19 = (uint8_t *)s6_1 + v1_10 * 5U;
                            uint32_t a1 = (uint32_t)v0_19[2];

                            if (a1 != 0xffU) {
                                fp_1 = (uint8_t *)s6_1 + a1 * 0x18U + 0x6cU;
                            } else {
                                fp_1 = 0;
                            }

                            s5_1 = (uint32_t)v0_19[3];
                            DPB_KMSG("BuildRefList cand pass=%d pic=%u pair=%u list=%d next=%u", var_48,
                                     a1, s5_1, s0_1, v1_10);

                            if (s5_1 == 0xffU) {
                                AL_sDPB_AddPicToList(s6_1, (int32_t)a1, 0xff, s0_1, s2, var_5c_1);

                                if (s1_1 == 0) {
                                    i_6 = (uint32_t)fp_1[0];
                                    v1_9 = i_6 << 3;

                                    if (i_6 != 0xffU) {
                                        goto label_6e250;
                                    }

                                    v0_23 = var_48;
                                    goto label_6e2e4_check;
                                }

                                if (fp_1 != 0) {
                                    i_6 = (uint32_t)fp_1[1];
                                    v1_9 = i_6 << 3;

                                    if (i_6 != 0xffU) {
                                        goto label_6e250;
                                    }

                                    v0_23 = var_48;
                                    goto label_6e2e4_check;
                                }

                                v0_23 = var_48;
                                __assert("p1stPic",
                                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                                         0x344, "AL_sDPB_BuildRefList", s2, var_5c_1, &_gp);
                                __builtin_unreachable();
                            }

                            AL_sDPB_AddPicToList(s6_1, (int32_t)a1, (int32_t)s5_1, s0_1, s2, var_5c_1);

                            if (s1_1 == 0) {
                                if (fp_1 != 0) {
                                    uint8_t *s5_4 = (uint8_t *)s6_1 + s5_1 * 0x18U;

                                    if (*(int32_t *)(fp_1 + 0x14U) < *(int32_t *)(s5_4 + 0x80U)) {
                                        i_6 = (uint32_t)fp_1[0];
                                        v1_9 = i_6 << 3;

                                        if (i_6 != 0xffU) {
                                            goto label_6e250;
                                        }

                                        v0_23 = var_48;
                                        goto label_6e2e4_check;
                                    }

                                    i_6 = (uint32_t)s5_4[0x6c];
                                    v1_9 = i_6 << 3;

                                    if (i_6 != 0xffU) {
                                        goto label_6e250;
                                    }

                                    v0_23 = var_48;
                                } else {
                                    v0_23 = var_48;
                                }
                            } else {
                                if (fp_1 == 0) {
                                    v0_23 = var_48;
                                    __assert("p1stPic",
                                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                                             0x344, "AL_sDPB_BuildRefList", s2, var_5c_1, &_gp);
                                    __builtin_unreachable();
                                }

                                {
                                    uint8_t *s5_7 = (uint8_t *)s6_1 + s5_1 * 0x18U;

                                    if (*(int32_t *)(s5_7 + 0x80U) < *(int32_t *)(fp_1 + 0x14U)) {
                                        i_6 = (uint32_t)fp_1[1];
                                        v1_9 = i_6 << 3;

                                        if (i_6 != 0xffU) {
                                            goto label_6e250;
                                        }

                                        v0_23 = var_48;
                                        goto label_6e2e4_check;
                                    }

                                    i_6 = (uint32_t)s5_7[0x6d];
                                    v1_9 = i_6 << 3;

                                    if (i_6 != 0xffU) {
                                        goto label_6e250;
                                    }

                                    v0_23 = var_48;
                                }
                            }

label_6e2e4_check:
                            if (v0_23 != s1_1) {
                                goto label_6e2ec;
                            }

                            if (v0_23 == 1) {
                                i_6 = i_4;
                                s1_1 = 1;

                                if (i_6 != 0xffU) {
                                    goto label_6e240;
                                }

                                s0_1 += 1;
                                break;
                            }

                            i_6 = i_2;
                            s1_1 = 0;

                            if (i_6 != 0xffU) {
                                goto label_6e240;
                            }

                            s0_1 += 1;
                            break;
                        }
                    }

label_6e2ec:
                    s0_1 += 1;

                    if (s0_1 == 2) {
                        break;
                    }
                }

                fp = s6_1;

                if (var_48 == 0) {
                    *arg7 = (int32_t)(uintptr_t)var_50;
                } else {
                    *arg5 = (int32_t)(uintptr_t)var_50;
                }

                DPB_KMSG("BuildRefList pass-done pass=%d count=%ld first=%u", var_48,
                         (long)(intptr_t)var_50, (uint32_t)*s2);

                if ((int32_t)(uintptr_t)var_50 < 0x20) {
                    char *i_1 = s2 + (int32_t)(uintptr_t)var_50;

                    do {
                        *i_1 = (char)0xff;
                        i_1 = &i_1[1];
                    } while (i_1 != &s2[0x20]);
                }

                {
                    int32_t v0_27 = var_48 + 1;

                    var_48 = v0_27;

                    if (v1 == v0_27) {
                        break;
                    }
                }

                s2 = arg6;
            }
        }

        {
            int32_t *v0_33 = (int32_t *)(uintptr_t)*arg3;

            if (v0_33 == 0) {
                v0_33 = (int32_t *)1;
            }

            if (*(int32_t *)(fp + 0x3ecU) != 1) {
                v0_33 = arg5;
            }

            {
                int32_t a2_2 = *v0_33;

                if (a2_2 > 0) {
                    uint32_t v0_34 = (uint32_t)(uint8_t)*arg4;
                    uint32_t t0_1 = (uint32_t)(uint8_t)*arg6;

                    if (v0_34 == t0_1) {
                        uint8_t *v0_35 = (uint8_t *)&arg6[1];
                        char *v1_21 = &arg4[1];

                        while (&arg6[a2_2] != (char *)v0_35) {
                            uint32_t a1_1 = (uint32_t)*v0_35;
                            uint32_t a0_9 = (uint32_t)(uint8_t)*v1_21;

                            v0_35 = &v0_35[1];
                            v1_21 = &v1_21[1];

                            if (a1_1 != a0_9) {
                                return (uint32_t)(uintptr_t)v0_35;
                            }
                        }

                        if (a2_2 >= 2) {
                            char v0_37 = arg6[1];

                            arg6[1] = (char)t0_1;
                            *arg6 = v0_37;
                            return (uint32_t)(uint8_t)v0_37;
                        }
                    }
                }
            }
        }
    }

    DPB_KMSG("BuildRefList exit l0=%d l1=%d first0=%u first1=%u", *arg5, *arg7,
             (uint32_t)arg4[0], (uint32_t)arg6[0]);
    return 0;
}

/* Binja preserves the dotted symbol name; export the translated helper under
 * the same alias so internal callers resolve to this OEM-shaped body. */
__asm__(".globl AL_sDPB_BuildRefList.isra.7");
__asm__("AL_sDPB_BuildRefList.isra.7 = AL_sDPB_BuildRefList_isra_7");

int32_t AL_DPB_Init(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4,
                    int32_t arg5, int32_t arg6, int32_t arg7, int32_t arg8,
                    int32_t arg9)
{
    if (arg1 == 0) {
        __assert("pCtx",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                 0x409, "AL_DPB_Init", &_gp);
        __builtin_unreachable();
    }

    *(int32_t *)((uint8_t *)arg1 + 0x3ecU) = arg2;
    *(int32_t *)((uint8_t *)arg1 + 0x3fcU) = arg5;
    *(int32_t *)((uint8_t *)arg1 + 0x3f4U) = -1;
    *(int32_t *)((uint8_t *)arg1 + 0x3e4U) = arg4;
    *(int32_t *)((uint8_t *)arg1 + 0x408U) = arg6;
    *((uint8_t *)arg1 + 0x400U) = 0xff;
    *((uint8_t *)arg1 + 0x401U) = 0xff;
    *(int32_t *)((uint8_t *)arg1 + 0x40cU) = arg7;
    *((uint8_t *)arg1 + 0x402U) = 0xff;
    *((uint8_t *)arg1 + 0x403U) = 0xff;
    *(int32_t *)((uint8_t *)arg1 + 0x410U) = arg8;
    *((uint8_t *)arg1 + 0x404U) = 0xff;
    *((uint8_t *)arg1 + 0x405U) = 0xff;
    *(int32_t *)((uint8_t *)arg1 + 0x414U) = arg9;
    *(int32_t *)((uint8_t *)arg1 + 0x3f0U) = 0;
    *(int32_t *)((uint8_t *)arg1 + 0x3dcU) = arg3;
    *(int32_t *)((uint8_t *)arg1 + 0x3e0U) = 0;
    *(int32_t *)((uint8_t *)arg1 + 0x3e8U) = 0;
    *(int32_t *)((uint8_t *)arg1 + 0x3f8U) = 0;

    {
        uint8_t *a3 = (uint8_t *)arg1 + 0x55U;
        uint8_t *v0 = (uint8_t *)arg1 + 0x51U;
        int32_t i = 0x10;

        while (i != -1) {
            *a3 = (uint8_t)i;
            i -= 1;
            *v0 = 0xff;
            *(v0 - 1) = 0xff;
            v0[1] = 0xff;
            v0[2] = 0xff;
            v0[3] = 0xff;
            a3 = &a3[1];
            v0 = &v0[-5];
        }
    }

    *(int32_t *)((uint8_t *)arg1 + 0x68U) = arg3 + 1;

    {
        uint8_t *v0_1 = (uint8_t *)arg1 + 0x385U;
        uint8_t *a2_1 = (uint8_t *)arg1 + 0x3b4U;
        int32_t i_1 = 0x21;

        while (i_1 != -1) {
            *a2_1 = (uint8_t)i_1;
            i_1 -= 1;
            *v0_1 = 0xff;
            *(v0_1 - 1) = 0xff;
            v0_1[1] = 0xff;
            v0_1[2] = 0;
            v0_1[3] = 0;
            v0_1[4] = 0;
            v0_1[5] = 0;
            *(v0_1 + 7) = 0;
            *(int32_t *)(v0_1 + 0x13U) = 0x7fffffff;
            *(int32_t *)(v0_1 + 0x0fU) = 0;
            a2_1 = &a2_1[1];
            v0_1 = &v0_1[-0x18];
        }
    }

    *(int32_t *)((uint8_t *)arg1 + 0x3d8U) = 0x22;
    return 1;
}

int32_t AL_DPB_Deinit(void *arg1)
{
    uint32_t i_1 = (uint32_t)*((uint8_t *)arg1 + 0x400U);

    if (i_1 != 0xffU) {
        uint32_t i;

        do {
            uint8_t *s0_3 = dpb_list_node(arg1, i_1);

            i = (uint32_t)s0_3[1];
            ((void (*)(int32_t, uint32_t))*(int32_t *)((uint8_t *)arg1 + 0x410U))(
                *(int32_t *)((uint8_t *)arg1 + 0x408U), (uint32_t)s0_3[4]);
            s0_3[2] = 0xff;
            s0_3[3] = 0xff;
            s0_3[4] = 0xff;

            {
                int32_t v0_1 = *(int32_t *)((uint8_t *)arg1 + 0x68U);

                *(int32_t *)((uint8_t *)arg1 + 0x68U) = v0_1 + 1;
                *((uint8_t *)arg1 + v0_1 + 0x55U) = (uint8_t)i_1;
            }

            i_1 = i;
        } while (i != 0xffU);
    }

    return 1;
}

int32_t AL_DPB_PushPicture(void *arg1, char *arg2, char arg3,
                           int32_t (*arg4)(void *arg1, void *arg2))
{
    int32_t v0 = *(int32_t *)((uint8_t *)arg1 + 0x3d8U);

    if (v0 == 0) {
        return 0;
    }

    {
        int32_t (*var_c)(void *arg1, void *arg2) = arg4;

        (void)var_c;
        *(int32_t *)((uint8_t *)arg1 + 0x3d8U) = v0 - 1;

        {
            uint32_t s2 = (uint32_t)*((uint8_t *)arg1 + v0 - 1 + 0x3b4U);
            uint32_t a3 = (uint32_t)(uint8_t)arg3;
            uint32_t s1 = s2 << 3;

            if (s2 == 0xffU) {
                return 0;
            }

            {
                uint32_t s5_1 = s2 << 5;
                uint8_t *v0_4 = (uint8_t *)arg1 + s5_1 - s1;
                int32_t v0_50;

                v0_4[0x6f] = (uint8_t)(((*(int32_t *)(arg2 + 4) >> 1) & 1));
                v0_4[0x70] = 1;
                *(int32_t *)(v0_4 + 0x80U) = *(int32_t *)(arg2 + 8);
                *(int32_t *)(v0_4 + 0x7cU) = *(int32_t *)(arg2 + 0xc);
                *(int32_t *)(v0_4 + 0x74U) = *(int32_t *)(arg2 + 0x14);
                *(int32_t *)(v0_4 + 0x78U) = *(int32_t *)(arg2 + 0x10);

                {
                    int32_t a0 = *(int32_t *)(arg2 + 0x18);

                    v0_4[0x71] = (uint8_t)(((a0 ^ 1) < 1) ? 1 : 0);

                    if (a0 == 1) {
                        uint32_t v0_43 = (uint32_t)*((uint8_t *)arg1 + 0x404U);

                        if (v0_43 != 0xffU) {
                            uint32_t t1_1 = v0_43 << 3;
                            uint32_t t0_1 = v0_43 << 5;
                            uint8_t *v1_40 = (uint8_t *)arg1 + t0_1 - t1_1;
                            uint32_t a1_17 = (uint32_t)v1_40[0x71];
                            uint8_t *v0_45 = v1_40 + 0x6cU;

                            if (a1_17 == 0U) {
                                while (1) {
                                    uint32_t v0_49 = (uint32_t)v0_45[1];

                                    if (v0_49 == 0xffU) {
                                        v0_50 = *(int32_t *)((uint8_t *)arg1 + 0x68U);
                                        goto label_6eb34;
                                    }

                                    {
                                        uint32_t v0_47 = v0_49 * 0x18U;

                                        v0_45 = (uint8_t *)arg1 + v0_47 + 0x6cU;

                                        if (*((uint8_t *)arg1 + v0_47 + 0x71U) != 0) {
                                            break;
                                        }
                                    }
                                }
                            }

                            {
                                int32_t t3_1 = 0xff;

                                while (1) {
                                    uint32_t v0_58 = t0_1 - t1_1;

                                    if (a1_17 != 0U) {
                                        uint8_t *v0_56 = (uint8_t *)arg1 + v0_58;
                                        uint32_t a1_22 = (uint32_t)v0_56[0x6e];
                                        void *a1_24;
                                        int32_t t0_2;
                                        int32_t t1_2;
                                        int32_t t2_1;

                                        v0_56[0x72] = 0;
                                        v0_56[0x71] = 0;
                                        a1_24 = (a1_22 == (uint32_t)t3_1) ? 0 : (void *)((uint8_t *)arg1 + a1_22 * 5U);
                                        a3 = (uint32_t)MarkPicsAsUnusedForRef(arg1, a1_24);
                                        t0_2 = (int32_t)a3;
                                        t1_2 = 0;
                                        t2_1 = 0;
                                        v0_58 = (uint32_t)(t0_2 - t1_2);

                                        {
                                            uint32_t v0_60 = (uint32_t)*((uint8_t *)arg1 + v0_58 + 0x6dU);

                                            t1_1 = v0_60 << 3;

                                            if (v0_60 == (uint32_t)t3_1) {
                                                break;
                                            }

                                            t0_1 = v0_60 << 5;
                                            a1_17 = (uint32_t)*((uint8_t *)arg1 + t0_1 - t1_1 + 0x71U);
                                        }
                                    }
                                }

                                if ((uint32_t)*((uint8_t *)arg1 + 0x402U) == (uint32_t)t3_1) {
                                    *((uint8_t *)arg1 + 0x403U) = 0xff;
                                    *(int32_t *)((uint8_t *)arg1 + 0x3f0U) = 1;
                                } else {
                                    *(int32_t *)((uint8_t *)arg1 + 0x3f0U) = 1;
                                }
                            }
                        }
                    }

                    v0_50 = *(int32_t *)((uint8_t *)arg1 + 0x68U);
                }

label_6eb34:
                if (v0_50 > 0) {
                    uint32_t s4_1;
                    uint8_t *s3_3;
                    int32_t v0_10;
                    uint32_t v0_15;
                    int32_t v1_16;
                    int32_t a1_1;
                    uint32_t a2;
                    const char *a3_3;

                    *(int32_t *)((uint8_t *)arg1 + 0x68U) = v0_50 - 1;
                    s4_1 = (uint32_t)*((uint8_t *)arg1 + v0_50 - 1 + 0x55U);
                    arg4 = (int32_t (*)(void *, void *))(uintptr_t)0xff;

                    if (s4_1 == 0xffU) {
                        return 0;
                    }

                    s3_3 = dpb_list_node(arg1, s4_1);
                    s3_3[4] = (uint8_t)a3;
                    s3_3[2] = (uint8_t)s2;
                    *((uint8_t *)arg1 + s5_1 - s1 + 0x6eU) = (uint8_t)s4_1;
                    a2 = (uint32_t)((uint32_t (*)(int32_t, uint32_t))*(int32_t *)((uint8_t *)arg1 + 0x40cU))(
                        *(int32_t *)((uint8_t *)arg1 + 0x408U), a3);
                    a1_1 = *(int32_t *)(arg2 + 4);

                    if ((a1_1 & 2) != 0) {
                        v0_10 = *(int32_t *)((uint8_t *)arg1 + 0x3ecU);

                        if (v0_10 != 1) {
                            if ((a1_1 & 1) != 0) {
                                if (*(int32_t *)((uint8_t *)arg1 + 0x3e8U) != 0) {
                                    do {
                                        AL_sDPB_RemoveRef(arg1, AL_sDPB_GetOldestStRef(arg1));
                                    } while (*(int32_t *)((uint8_t *)arg1 + 0x3e8U) != 0);

                                    v0_10 = *(int32_t *)((uint8_t *)arg1 + 0x3ecU);
                                }

                                *(int32_t *)((uint8_t *)arg1 + 0x3f8U) = 0;
                                {
                                    int32_t a0_2 = 0;
                                    int32_t a2_1 = 0;

                                    goto label_6ebc0;
                                }
                            }

                            {
                                int32_t a0_2 = *(int32_t *)((uint8_t *)arg1 + 0x3e8U);
                                int32_t a2_1;

                                if (a0_2 >= *(int32_t *)((uint8_t *)arg1 + 0x3e4U)) {
                                    int32_t a2_4 = *(int32_t *)((uint8_t *)arg1 + 0x3f4U);
                                    void *v0_69;

                                    if (a2_4 == -1) {
                                        v0_69 = AL_sDPB_GetOldestStRef(arg1);
                                    } else {
                                        uint32_t i = (uint32_t)*((uint8_t *)arg1 + 0x402U);

                                        if (i != (uint32_t)(uintptr_t)arg4) {
                                            do {
                                                uint8_t *v1_49 = dpb_list_node(arg1, i);
                                                uint32_t v0_71 = (uint32_t)v1_49[2];

                                                if (v0_71 == 0xffU) {
                                                    __builtin_trap();
                                                }

                                                v0_69 = v1_49;

                                                if (a2_4 == *(int32_t *)((uint8_t *)arg1 + v0_71 * 0x18U + 0x7cU)) {
                                                    goto label_6f254;
                                                }

                                                i = (uint32_t)v1_49[1];
                                            } while (i != 0xffU);
                                        }

                                        a3_3 = (const char *)__assert("0",
                                                                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                                                                     0x192, "AL_sDPB_GetMMCORemovePic", &_gp);
                                        (void)a3_3;
                                        __builtin_unreachable();
                                    }

label_6f254:
                                    AL_sDPB_RemoveRef(arg1, v0_69);
                                    v0_10 = *(int32_t *)((uint8_t *)arg1 + 0x3ecU);
                                    a2_1 = *(int32_t *)((uint8_t *)arg1 + 0x3f8U);
                                    a0_2 = *(int32_t *)((uint8_t *)arg1 + 0x3e8U);
                                } else {
                                    a2_1 = *(int32_t *)((uint8_t *)arg1 + 0x3f8U);
                                }

label_6ebc0:
                                {
                                    uint32_t v1_11 = (uint32_t)*((uint8_t *)arg1 + 0x401U);

                                    s3_3[1] = 0xff;
                                    *s3_3 = (uint8_t)v1_11;

                                    if ((uint32_t)*((uint8_t *)arg1 + 0x400U) == 0xffU) {
                                        *((uint8_t *)arg1 + 0x400U) = (uint8_t)s4_1;
                                    }

                                    if (v1_11 != 0xffU) {
                                        *((uint8_t *)arg1 + v1_11 * 5U + 1U) = (uint8_t)s4_1;
                                    }

                                    {
                                        uint32_t a1_4 = (uint32_t)*((uint8_t *)arg1 + 0x402U);

                                        *((uint8_t *)arg1 + 0x401U) = (uint8_t)s4_1;

                                        if (a1_4 == 0xffU) {
                                            *((uint8_t *)arg1 + 0x402U) = (uint8_t)s4_1;
                                        }

                                        *((uint8_t *)arg1 + 0x403U) = (uint8_t)s4_1;

                                        {
                                            int32_t v1_14 = *(int32_t *)(arg2 + 0xc);

                                            a2 = (a2_1 < v1_14) ? 1U : 0U;
                                            *(int32_t *)((uint8_t *)arg1 + 0x3e8U) = a0_2 + 1;

                                            if (a2 != 0U) {
                                                *(int32_t *)((uint8_t *)arg1 + 0x3f8U) = v1_14;
                                            }

                                            if (v0_10 != 1) {
                                                a1_1 = *(int32_t *)(arg2 + 4);
                                            }
                                        }
                                    }
                                }
                            }
                        } else {
                            goto label_6ecf4;
                        }
                    } else {
label_6ecf4:
                        v0_10 = *(int32_t *)((uint8_t *)arg1 + 0x3ecU);

                        if (v0_10 != 1) {
                            a2 = AL_sDPB_RemovePic(arg1, (char *)(uintptr_t)(a1_1 & 1), a2);
                            v0_10 = *(int32_t *)((uint8_t *)arg1 + 0x3ecU);
                        }
                    }

                    {
                        int32_t a1_6 = *(int32_t *)(arg2 + 4);

                        if ((a1_6 & 2) != 0) {
                            int32_t v0_14;

                            if (v0_10 == 1) {
label_6ecb4:
                                v1_16 = *(int32_t *)((uint8_t *)arg1 + 0x3e0U);
                                v0_14 = ((v1_16 < *(int32_t *)((uint8_t *)arg1 + 0x3dcU)) ? 1 : 0) ^ 1;
                            } else {
                                while (1) {
                                    v1_16 = *(int32_t *)((uint8_t *)arg1 + 0x3e0U);
                                    v0_14 = ((v1_16 < *(int32_t *)((uint8_t *)arg1 + 0x3dcU) - 1) ? 1 : 0) ^ 1;

                                    if (v0_14 == 0) {
                                        break;
                                    }

                                    {
                                        int32_t a1_7 = a1_6 & 1;

                                        if (a1_7 == 0) {
                                            break;
                                        }

                                        a1_7 = 1;

                                        if (*(int32_t *)((uint8_t *)arg1 + 0x3e0U) == 0) {
                                            v0_15 = s5_1 - s1;
                                            goto label_6eea4;
                                        }

                                        a2 = AL_sDPB_Bumping(arg1, a1_7, a2, arg2, (int32_t)s1, (void *)1);
                                        a1_6 = *(int32_t *)(arg2 + 4);

                                        if (*(int32_t *)((uint8_t *)arg1 + 0x3ecU) == 1) {
                                            goto label_6ecb4;
                                        }
                                    }
                                }
                            }

                            if (v0_14 != 0) {
                                goto label_6ed30;
                            }
                        }

                        if (v0_10 != 1) {
                            while (1) {
                                v1_16 = *(int32_t *)((uint8_t *)arg1 + 0x3e0U);
                                {
                                    int32_t result_1 = ((v1_16 < *(int32_t *)((uint8_t *)arg1 + 0x3dcU) - 1) ? 1 : 0) ^ 1;

                                    if (result_1 == 0) {
                                        break;
                                    }

label_6ed30:
                                    {
                                        uint32_t v1_17 = (uint32_t)*((uint8_t *)arg1 + 0x404U);

                                        if (v1_17 != 0xffU) {
                                            uint32_t a0_6 = v1_17 << 3;

                                            while (1) {
                                                uint32_t a1_8 = v1_17 << 5;
                                                uint32_t v1_18 = a1_8 - a0_6;
                                                uint32_t a2_3 = (uint32_t)*((uint8_t *)arg1 + v1_18 + 0x70U);

                                                if (a2_3 == 0U) {
                                                    v1_17 = (uint32_t)*((uint8_t *)arg1 + v1_18 + 0x6dU);
                                                    a0_6 = v1_17 << 3;

                                                    if (v1_17 != 0xffU) {
                                                        continue;
                                                    }
                                                } else if (*(int32_t *)(arg2 + 8) >= *(int32_t *)((uint8_t *)arg1 + a1_8 - a0_6 + 0x80U)) {
                                                    AL_sDPB_Bumping(arg1, 0, a2_3, arg2, (int32_t)s1, (void *)1);

                                                    if (*(int32_t *)((uint8_t *)arg1 + 0x3ecU) == 1) {
                                                        break;
                                                    }

                                                    continue;
                                                }

                                                break;
                                            }
                                        }
                                    }
                                }

                                {
                                    uint8_t *s0_3 = (uint8_t *)arg1 + s5_1 - s1;
                                    uint32_t s3_4 = (uint32_t)s0_3[0x6e];
                                    int32_t result_1 = ((v1_16 < *(int32_t *)((uint8_t *)arg1 + 0x3dcU) - 1) ? 1 : 0) ^ 1;
                                    int32_t result = result_1;

                                    AL_sDPB_OutputPic(arg1, (int32_t)s2);
                                    s0_3[0x6e] = 0xff;
                                    s0_3[0x6d] = 0xff;
                                    s0_3[0x6c] = 0xff;
                                    s0_3[0x6f] = 0;
                                    s0_3[0x70] = 0;
                                    s0_3[0x71] = 0;
                                    s0_3[0x72] = 0;
                                    *(int32_t *)(s0_3 + 0x74U) = 0;
                                    *(int32_t *)(s0_3 + 0x80U) = 0x7fffffff;
                                    *(int32_t *)(s0_3 + 0x7cU) = 0;

                                    {
                                        int32_t v1_21 = *(int32_t *)((uint8_t *)arg1 + 0x3d8U);

                                        *(int32_t *)((uint8_t *)arg1 + 0x3d8U) = v1_21 + 1;
                                        *((uint8_t *)arg1 + v1_21 + 0x3b4U) = (uint8_t)s2;

                                        if (s3_4 != 0xffU) {
                                            uint8_t *s0_6 = dpb_list_node(arg1, s3_4);

                                            ((void (*)(int32_t, uint32_t))*(int32_t *)((uint8_t *)arg1 + 0x410U))(
                                                *(int32_t *)((uint8_t *)arg1 + 0x408U), (uint32_t)s0_6[4]);
                                            result = result_1;
                                            s0_6[2] = 0xff;
                                            s0_6[3] = 0xff;
                                            s0_6[4] = 0xff;
                                        }

                                        {
                                            int32_t v1_23 = *(int32_t *)((uint8_t *)arg1 + 0x68U);

                                            *(int32_t *)((uint8_t *)arg1 + 0x68U) = v1_23 + 1;
                                            *((uint8_t *)arg1 + v1_23 + 0x55U) = (uint8_t)s3_4;
                                            return result;
                                        }
                                    }
                                }
                            }
                        }

label_6ee84:
                        {
                            int32_t v1_16_local = *(int32_t *)((uint8_t *)arg1 + 0x3e0U);
                            int32_t result_1 = ((v1_16_local < *(int32_t *)((uint8_t *)arg1 + 0x3dcU)) ? 1 : 0) ^ 1;

                            if (result_1 != 0) {
                                goto label_6ed30;
                            }
                        }
                    }

                    v0_15 = s5_1 - s1;
label_6eea4:
                    {
                        uint32_t a0_12 = (uint32_t)*((uint8_t *)arg1 + 0x401U);
                        uint32_t v0_28 = (uint32_t)*((uint8_t *)arg1 + v0_15 + 0x6eU);

                        if (a0_12 != v0_28) {
                            if (v0_28 == 0xffU) {
                                __assert("!((pFirst->pPrevPtr) != 0xff)",
                                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                                         0xf7,
                                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/FbcMapSize.c",
                                         &_gp);
                                __builtin_unreachable();
                            }

                            if (a0_12 == 0xffU) {
                                if ((uint32_t)*((uint8_t *)arg1 + 0x400U) == a0_12) {
                                    *((uint8_t *)arg1 + 0x401U) = (uint8_t)v0_28;
                                    *((uint8_t *)arg1 + 0x400U) = (uint8_t)v0_28;
                                } else {
                                    __assert("!((pFirst->pPrevPtr) != 0xff)",
                                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                                             0xf7,
                                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_common/FbcMapSize.c",
                                             &_gp);
                                    __builtin_unreachable();
                                }
                            } else {
                                *((uint8_t *)arg1 + a0_12 * 5U + 1U) = (uint8_t)v0_28;
                                *((uint8_t *)arg1 + v0_28 * 5U) = (uint8_t)a0_12;
                                *((uint8_t *)arg1 + 0x401U) = (uint8_t)v0_28;
                            }
                        }

                        {
                            uint32_t a0_13 = (uint32_t)*((uint8_t *)arg1 + 0x405U);

                            *(int32_t *)((uint8_t *)arg1 + 0x3e0U) += 1;

                            if (a0_13 != 0xffU) {
                                uint8_t *v1_36 = (uint8_t *)arg1 + a0_13 * 0x18U + 0x6cU;
                                int32_t a1_15 = *(int32_t *)((uint8_t *)arg1 + s5_1 - s1 + 0x80U);

                                if (a1_15 >= *(int32_t *)(v1_36 + 0x14U)) {
label_6ef58:
                                    {
                                        uint32_t v0_37 = (uint32_t)v1_36[1];

                                        if (v0_37 == 0xffU) {
                                            *((uint8_t *)arg1 + 0x405U) = (uint8_t)s2;
                                        } else {
                                            *((uint8_t *)arg1 + v0_37 * 0x18U + 0x6cU) = (uint8_t)s2;
                                        }

                                        {
                                            uint8_t *s6_2 = (uint8_t *)arg1 + s5_1 - s1;

                                            s6_2[0x6d] = v1_36[1];
                                            s6_2[0x6c] = (uint8_t)a0_13;
                                            v1_36[1] = (uint8_t)s2;
                                            return 1;
                                        }
                                    }
                                }

                                while (1) {
                                    a0_13 = (uint32_t)*v1_36;

                                    if (a0_13 == 0xffU) {
                                        break;
                                    }

                                    v1_36 = (uint8_t *)arg1 + a0_13 * 0x18U + 0x6cU;

                                    if (a1_15 >= *(int32_t *)(v1_36 + 0x14U)) {
                                        goto label_6ef58;
                                    }
                                }

                                {
                                    uint32_t v0_53 = (uint32_t)*((uint8_t *)arg1 + 0x404U);

                                    if (v0_53 == a0_13) {
                                        __builtin_trap();
                                    }

                                    {
                                        uint8_t *v1_47 = (uint8_t *)arg1 + v0_53 * 0x18U;

                                        if ((uint32_t)v1_47[0x6c] == a0_13) {
                                            *((uint8_t *)arg1 + s5_1 - s1 + 0x6dU) = (uint8_t)v0_53;
                                            v1_47[0x6c] = (uint8_t)s2;
                                            *((uint8_t *)arg1 + 0x404U) = (uint8_t)s2;
                                            return 1;
                                        }

                                        if ((uint32_t)*((uint8_t *)arg1 + 0x404U) == a0_13) {
                                            *((uint8_t *)arg1 + 0x405U) = (uint8_t)s2;
                                            *((uint8_t *)arg1 + 0x404U) = (uint8_t)s2;
                                            return 1;
                                        }
                                    }
                                }
                            }

                            *((uint8_t *)arg1 + 0x405U) = (uint8_t)s2;
                            return 1;
                        }
                    }
                }
            }
        }
    }

    return 0;
}

int32_t AL_DPB_AVC_CheckMMCORequired(void *arg1, void *arg2, int32_t *arg3)
{
    int32_t v0 = *(int32_t *)((uint8_t *)arg1 + 0x3ecU);

    *(int32_t *)((uint8_t *)arg1 + 0x3f4U) = -1;

    if (v0 != 0) {
        return -1;
    }

    {
        int32_t result = -1;

        if ((*(int32_t *)((uint8_t *)arg2 + 4U) & 3) != 2 ||
            *(int32_t *)((uint8_t *)arg2 + 0x18U) == 1 ||
            *(int32_t *)((uint8_t *)arg1 + 0x3e8U) < *(int32_t *)((uint8_t *)arg1 + 0x3e4U)) {
            return -1;
        }

        {
            void *v0_7 = AL_sDPB_GetOldestStRef(arg1);
            uint32_t t3 = (uint32_t)*((uint8_t *)arg1 + 0x402U);

            if (t3 != 0xffU) {
                uint32_t t5_1 = t3 << 2;

                while (1) {
                    uint8_t *a3_2 = (uint8_t *)arg1 + t5_1 + t3;
                    uint32_t v1_3 = (uint32_t)a3_2[2];
                    uint32_t t6_1;
                    uint32_t t2_1;

                    if (v1_3 == 0xffU) {
                        __builtin_trap();
                    }

                    t6_1 = v1_3 << 3;
                    t2_1 = v1_3 << 5;

                    {
                        uint8_t *v1_5 = (uint8_t *)arg1 + t2_1 - t6_1;

                        if (v1_5[0x71] == 0 && v1_5[0x6f] != 0) {
                            int32_t a0_2 = arg3[0x11];
                            int32_t t1 = *(int32_t *)(v1_5 + 0x80U);

                            if (a0_2 > 0) {
                                int32_t *a1 = &arg3[0x13];

                                if (t1 == arg3[0x12]) {
                                    goto label_6f4a4;
                                }

                                {
                                    int32_t v1_7 = 0;

                                    while (1) {
                                        if (t1 == *(a1 - 1)) {
                                            goto label_6f4a4;
                                        }

                                        v1_7 += 1;
                                        a1 = &a1[1];

                                        if (a0_2 == v1_7) {
                                            break;
                                        }
                                    }
                                }
                            }

                            {
                                int32_t t0_2 = *arg3;

                                if (t0_2 > 0) {
                                    int32_t *a1_2 = &arg3[2];

                                    if (t1 == arg3[1]) {
                                        goto label_6f4a4;
                                    }

                                    {
                                        int32_t v1_9 = 0;

                                        while (1) {
                                            if (t1 == *(a1_2 - 1)) {
                                                goto label_6f4a4;
                                            }

                                            v1_9 += 1;
                                            a1_2 = &a1_2[1];

                                            if (t0_2 == v1_9) {
                                                break;
                                            }
                                        }
                                    }
                                }
                            }

                            if (v0_7 != a3_2) {
                                result = *(int32_t *)((uint8_t *)arg1 + t2_1 - t6_1 + 0x7cU);
                                *(int32_t *)((uint8_t *)arg1 + 0x3f4U) = result;
                                return result;
                            }

                            break;
                        }
                    }

label_6f4a4:
                    t3 = (uint32_t)a3_2[1];
                    t5_1 = t3 << 2;

                    if (t3 == 0xffU) {
                        break;
                    }
                }
            }
        }

        return -1;
    }
}

int32_t AL_DPB_Update(void *arg1, int32_t *arg2, uint32_t arg3)
{
    (void)arg3;

    {
        uint32_t i = (uint32_t)*((uint8_t *)arg1 + 0x404U);

        if (i != 0xffU) {
            uint32_t s0_1 = i << 3;
            uint32_t s1_1 = i << 5;
            int32_t t0_1 = arg2[0x11];
            int32_t j = *(int32_t *)((uint8_t *)arg1 + s1_1 - s0_1 + 0x80U);
            int32_t t2_1;
            int32_t t1_2;

            if (t0_1 <= 0) {
label_6f56c:
                t2_1 = 0;
            } else {
                int32_t v1_2 = 0;

                if (j == arg2[0x12]) {
                    t2_1 = 1;
                } else {
                    int32_t *a0 = &arg2[0x13];

                    do {
                        t2_1 = 1;

                        if (j == *(a0 - 1)) {
                            goto label_6f718;
                        }

                        v1_2 += 1;
                        a0 = &a0[1];
                    } while (t0_1 != v1_2);

                    goto label_6f56c;
                }
            }

label_6f718:
            *((uint8_t *)arg1 + s1_1 - s0_1 + 0x72U) = (uint8_t)t2_1;

            {
                int32_t t1_1 = *arg2;

                if (t1_1 <= 0) {
label_6f5b8:
                    t1_2 = arg2[0x22];

                    if (t1_2 > 0) {
                        if (j == arg2[0x23]) {
                            goto label_6f694;
                        }

                        goto label_6f5d4;
                    }
                } else if (j == arg2[1]) {
                    t2_1 = 1;
                    goto label_6f694;
                } else {
                    int32_t v1_6 = 0;
                    int32_t *a0_2 = &arg2[2];

                    do {
                        if (j == *(a0_2 - 1)) {
                            goto label_6f694;
                        }

                        v1_6 += 1;
                        a0_2 = &a0_2[1];
                    } while (t1_1 != v1_6);

                    goto label_6f5b8;
                }

label_6f5d4:
                {
                    int32_t *a0_4 = &arg2[0x24];
                    int32_t v1_10 = 0;

                    do {
                        if (j == *(a0_4 - 1)) {
                            goto label_6f694;
                        }

                        v1_10 += 1;
                        a0_4 = &a0_4[1];
                    } while (v1_10 < t1_2);
                }

                if (t2_1 == 0) {
                    uint32_t v0_5 = (uint32_t)*((uint8_t *)arg1 + s1_1 - s0_1 + 0x6eU);
                    void *a1_4 = 0;

                    if (v0_5 != 0xffU) {
                        a1_4 = (uint8_t *)arg1 + v0_5 * 5U;
                    }

                    arg3 = (uint32_t)MarkPicsAsUnusedForRef(arg1, a1_4);

                    {
                        uint32_t a1_7 = (uint32_t)*((uint8_t *)arg1 + 0x404U);

                        if (a1_7 == 0xffU) {
                            __builtin_trap();
                        }

                        {
                            uint32_t v1_11 = a1_7 << 5;

                            while (1) {
                                uint8_t *s5_1 = (uint8_t *)arg1 + v1_11 - (a1_7 << 3);

                                if (*(int32_t *)(s5_1 + 0x80U) < *(int32_t *)((uint8_t *)arg1 + s1_1 - s0_1 + 0x80U)) {
                                    arg3 = AL_sDPB_OutputPic(arg1, (int32_t)a1_7);
                                    a1_7 = (uint32_t)s5_1[0x6d];
                                    v1_11 = a1_7 << 5;

                                    if (a1_7 == 0xffU) {
                                        __builtin_trap();
                                    }

                                    continue;
                                }

                                arg3 = AL_sDPB_OutputPic(arg1, (int32_t)i);
                                break;
                            }
                        }
                    }
                }
            }

label_6f694:
            if (t0_1 == 0 || j != arg2[0x12]) {
                *((uint8_t *)arg1 + s1_1 - s0_1 + 0x6fU) = 1;
            } else {
                uint32_t v0_13 = (uint32_t)*((uint8_t *)arg1 + s1_1 - s0_1 + 0x6eU);
                void *a1_9 = 0;

                if (v0_13 != 0xffU) {
                    a1_9 = (uint8_t *)arg1 + v0_13 * 5U;
                }

                arg3 = (uint32_t)MarkPicsAsUnusedForRef(arg1, a1_9);
            }

            do {
                i = (uint32_t)*((uint8_t *)arg1 + s1_1 - s0_1 + 0x6dU);
                s0_1 = i << 3;
            } while (i != 0xffU);

            AL_sDPB_RemovePic(arg1, 0, arg3);
        }
    }

    return 1;
}

uint32_t AL_DPB_RmLT(void *arg1)
{
    return (uint32_t)*(int32_t *)((uint8_t *)arg1 + 0x3f0U);
}

uint32_t AL_DPB_GetLTPOC(void *arg1)
{
    uint8_t *ctx = (uint8_t *)arg1;
    uint32_t v1_5 = (uint32_t)ctx[0x404];

    if (v1_5 != 0xffU) {
        uint32_t v0_2 = v1_5 * 0x18U;
        uint8_t *v0_4 = ctx + v0_2 + 0x6cU;

        if ((uint32_t)ctx[v0_2 + 0x71U] != 0U) {
            return (uint32_t)*(int32_t *)(v0_4 + 0x14U);
        }

        while (1) {
            uint32_t v1_4 = (uint32_t)v0_4[1];
            uint8_t *a1_2 = ctx + v1_4 * 0x18U;

            if (v1_4 == 0xffU) {
                break;
            }

            v0_4 = a1_2 + 0x6cU;

            if ((uint32_t)a1_2[0x71] != 0U) {
                return (uint32_t)*(int32_t *)(v0_4 + 0x14U);
            }
        }
    }

    return 0xffffffffU;
}

void AL_DPB_ResetLTFlags(void *arg1)
{
    *(int32_t *)((uint8_t *)arg1 + 0x3f0U) = 0;
}

uint32_t AL_DPB_GetLTPictNum(void *arg1, int32_t arg2)
{
    uint8_t *ctx = (uint8_t *)arg1;
    uint32_t i = (uint32_t)ctx[0x404];
    uint8_t *v1_2 = 0;

    while (i != 0xffU) {
        v1_2 = ctx + i * 0x18U;

        if (*(int32_t *)(v1_2 + 0x80U) == arg2 && (uint32_t)v1_2[0x71] != 0U) {
            return (uint32_t)*(int32_t *)(v1_2 + 0x7cU);
        }

        i = (uint32_t)v1_2[0x6d];
    }

    return 0xffffffffU;
}

uint32_t AL_DPB_GetRefMode(void *arg1, int32_t arg2)
{
    uint8_t *ctx = (uint8_t *)arg1;
    uint32_t v1_5 = (uint32_t)ctx[0x404];

    if (v1_5 != 0xffU) {
        uint32_t v0_2 = v1_5 * 0x18U;
        uint8_t *v0_4 = ctx + v0_2 + 0x6cU;

        if (arg2 == *(int32_t *)(ctx + v0_2 + 0x80U)) {
            return (uint32_t)v0_4[5];
        }

        while (1) {
            uint32_t v1_4 = (uint32_t)v0_4[1];
            uint8_t *a2_2 = ctx + v1_4 * 0x18U;

            if (v1_4 == 0xffU) {
                break;
            }

            v0_4 = a2_2 + 0x6cU;

            if (*(int32_t *)(a2_2 + 0x80U) == arg2) {
                return (uint32_t)v0_4[5];
            }
        }
    }

    return 2;
}

int32_t AL_DPB_Flush(void *arg1, int32_t arg2)
{
    uint32_t a0 = (uint32_t)*((uint8_t *)arg1 + 0x404U);

    if (a0 != 0xffU) {
        int32_t fp_1 = arg2;
        uint8_t *s1_3 = (uint8_t *)arg1 + a0 * 0x18U + 0x6cU;

        if (arg2 != 0) {
            int32_t result = 0;

            while (1) {
                uint32_t v1_1 = (uint32_t)s1_3[2];

                if (v1_1 == 0xffU) {
                    break;
                }

                uint8_t *v1_3 = (uint8_t *)arg1 + v1_1 * 5U;
                uint32_t v0_3 = (uint32_t)v1_3[2];
                uint8_t *s0_1 = 0;

                if (v0_3 != 0xffU) {
                    s0_1 = (uint8_t *)arg1 + v0_3 * 0x18U + 0x6cU;
                }

                {
                    uint32_t v0_5 = (uint32_t)v1_3[3];
                    uint8_t *s5_1 = 0;

                    if (v0_5 != 0xffU) {
                        s5_1 = (uint8_t *)arg1 + v0_5 * 0x18U + 0x6cU;
                    }

                    if ((uint32_t)s1_3[4] != 0U) {
                        AL_sDPB_OutputPic(arg1, (int32_t)a0);
                        fp_1 -= 1;
                        result = 1;
                    }

                    if ((s0_1 == 0 || (uint32_t)s0_1[4] == 0U) &&
                        (s5_1 == 0 || (uint32_t)s5_1[4] == 0U)) {
                        AL_sDPB_RemoveBuf(arg1, (int32_t)s1_3[2], (char *)s0_1, (int32_t)(uintptr_t)s1_3,
                                          (uint8_t *)AL_DPB_HEVC_GetRefInfo + 0x248);
                        a0 = (uint32_t)*((uint8_t *)arg1 + 0x404U);
                    } else {
                        a0 = (uint32_t)s1_3[1];
                    }

                    if (a0 != 0xffU) {
                        s1_3 = (uint8_t *)arg1 + a0 * 0x18U + 0x6cU;

                        if (fp_1 != 0) {
                            continue;
                        }
                    }

                    return result;
                }
            }

            __builtin_trap();
        }
    }

    return 0;
}

uint32_t AL_DPB_GetRefFromPOC(uint8_t *arg1, int32_t arg2)
{
    uint32_t v1_8 = (uint32_t)arg1[0x405];

    if (v1_8 == 0xffU) {
        return 0xffU;
    }

    {
        uint32_t v0_2 = v1_8 * 0x18U;
        uint8_t *v0_4 = &arg1[v0_2 + 0x6cU];
        uint32_t v1_3;

        if (arg2 != *(int32_t *)(arg1 + v0_2 + 0x80U)) {
            uint32_t v0_8;
            uint32_t v0_6;

            do {
                v0_8 = (uint32_t)v0_4[0];

                if (v0_8 == 0xffU) {
                    return 0xffU;
                }

                v0_6 = v0_8 * 0x18U;
                v0_4 = &arg1[v0_6 + 0x6cU];
            } while (*(int32_t *)(arg1 + v0_6 + 0x80U) != arg2);

            v1_3 = (uint32_t)v0_4[2];
        } else {
            v1_3 = (uint32_t)v0_4[2];
        }

        if (v1_3 != 0xffU) {
            return (uint32_t)arg1[v1_3 * 5U + 4U];
        }

        __builtin_trap();
    }
}

int32_t AL_DPB_GetAvailRef(void *arg1, void *arg2, int32_t *arg3)
{
    if (arg3 == 0) {
        char *a0;
        void *a1_8;

        a0 = (char *)__assert("pAvailRef",
                              "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                              0x56a, "AL_DPB_GetAvailRef", &_gp);
        return (int32_t)AL_DPB_GetRefFromPOC((uint8_t *)a0, (int32_t)(uintptr_t)a1_8);
    }

    {
        int32_t v1 = *(int32_t *)((uint8_t *)arg2 + 0x10U);

        arg3[0] = 0;
        arg3[1] = 0;

        if (v1 == 2 && *(int32_t *)((uint8_t *)arg1 + 0x3ecU) != 1) {
            return 1;
        }

        {
            uint8_t *ctx = (uint8_t *)arg1;
            uint32_t v0 = (uint32_t)ctx[0x405];

            if (v0 != 0xffU) {
                while (1) {
                    uint32_t t6_1 = v0 << 3;
                    uint32_t t5_2 = v0 << 5;
                    uint32_t a1_5 = (uint32_t)ctx[t5_2 - t6_1 + 0x6eU];
                    uint32_t t3_1 = v0;
                    uint32_t a1_6;
                    uint32_t v0_3;

                    if (a1_5 == 0xffU) {
                        __builtin_trap();
                    }

                    {
                        uint8_t *a1_1 = ctx + a1_5 * 5U;
                        uint32_t arg5 = (uint32_t)a1_1[2];
                        uint32_t arg4;
                        uint8_t *v1_5;
                        uint8_t *a1_5_ptr;

                        if (arg5 == 0xffU) {
                            arg4 = (uint32_t)a1_1[3];
                            v1_5 = 0;

                            if (arg4 != 0xffU) {
                                a1_5_ptr = ctx + arg4 * 0x18U + 0x6cU;
                            } else {
                                a1_5_ptr = 0;
                            }
                        } else {
                            arg4 = (uint32_t)a1_1[3];
                            v1_5 = ctx + arg5 * 0x18U + 0x6cU;

                            if (arg4 != 0xffU) {
                                a1_5_ptr = ctx + arg4 * 0x18U + 0x6cU;

                                if (v1_5 != 0) {
                                    goto label_6fb14;
                                }
                            } else {
                                a1_5_ptr = 0;
                            }
                        }

                        if (a1_5_ptr != 0 && (uint32_t)a1_5_ptr[3] != 0U) {
                            v1_5 = 0;
                            goto label_6fb50;
                        }

label_6fb14:
                        if (v1_5 != 0 &&
                            ((uint32_t)v1_5[3] != 0U || (uint32_t)v1_5[5] != 0U || (uint32_t)v1_5[6] != 0U)) {
                            if (a1_5_ptr != 0) {
                                if ((uint32_t)a1_5_ptr[3] == 0U) {
                                    goto label_6fb50;
                                }

label_6fc0c:
                                if (*(int32_t *)(a1_5_ptr + 0x14U) >= *(int32_t *)(v1_5 + 0x14U)) {
                                    arg4 = arg5;
                                }
                            }

                            if (arg4 == 0xffU) {
                                __builtin_trap();
                            }

                            t3_1 = arg4;

                            if ((uint32_t)ctx[arg4 * 0x18U + 0x71U] != 0U) {
                                int32_t t0 = arg3[1];

                                a1_6 = t3_1 << 3;
                                v0_3 = t3_1 << 5;

                                {
                                    uint8_t *v1_10 = (uint8_t *)&arg3[t0 + arg3[0]];
                                    uint8_t *a3_9 = ctx + v0_3 - a1_6;

                                    *(int32_t *)(v1_10 + 0x88U) = 1;
                                    *(int32_t *)(v1_10 + 8U) = *(int32_t *)(a3_9 + 0x80U);
                                    *(int32_t *)(v1_10 + 0x108U) = *(int32_t *)(a3_9 + 0x78U);
                                    arg3[1] = t0 + 1;
                                }

                                if (a1_5_ptr != 0 && (uint32_t)a1_5_ptr[3] == 0U) {
                                    v1_5 = 0;
                                    goto label_6fb50;
                                }

                                a1_6 = v0 << 3;
                                v0_3 = v0 << 5;
                            } else {
label_6fc40:
                                {
                                    int32_t a3_12 = arg3[0];

                                    a1_6 = t3_1 << 3;
                                    v0_3 = t3_1 << 5;

                                    {
                                        uint8_t *v1_16 = (uint8_t *)&arg3[a3_12 + arg3[1]];
                                        uint8_t *t0_2 = ctx + v0_3 - a1_6;

                                        *(int32_t *)(v1_16 + 0x88U) = 0;
                                        *(int32_t *)(v1_16 + 8U) = *(int32_t *)(t0_2 + 0x80U);
                                        *(int32_t *)(v1_16 + 0x108U) = *(int32_t *)(t0_2 + 0x78U);
                                        arg3[0] = a3_12 + 1;
                                    }
                                }
                            }
                        } else {
                            if (a1_5_ptr != 0 && (uint32_t)a1_5_ptr[3] != 0U) {
                                a1_6 = v0 << 3;
                                v0_3 = v0 << 5;
                            } else {
label_6fb50:
                                if (a1_5_ptr != 0 &&
                                    ((uint32_t)a1_5_ptr[5] != 0U || (uint32_t)a1_5_ptr[6] != 0U)) {
                                    if (v1_5 != 0) {
                                        goto label_6fc0c;
                                    }

                                    a1_6 = v0 << 3;
                                    v0_3 = v0 << 5;
                                } else {
                                    a1_6 = v0 << 3;
                                    v0_3 = v0 << 5;

                                    if (v1_5 != 0) {
label_6fb78:
                                        if ((uint32_t)ctx[t5_2 - t6_1 + 0x71U] == 0U) {
                                            goto label_6fc40;
                                        }

                                        int32_t t0 = arg3[1];

                                        a1_6 = t3_1 << 3;
                                        v0_3 = t3_1 << 5;

                                        {
                                            uint8_t *v1_10 = (uint8_t *)&arg3[t0 + arg3[0]];
                                            uint8_t *a3_9 = ctx + v0_3 - a1_6;

                                            *(int32_t *)(v1_10 + 0x88U) = 1;
                                            *(int32_t *)(v1_10 + 8U) = *(int32_t *)(a3_9 + 0x80U);
                                            *(int32_t *)(v1_10 + 0x108U) = *(int32_t *)(a3_9 + 0x78U);
                                            arg3[1] = t0 + 1;
                                        }
                                    }
                                }
                            }
                        }

                        v0 = (uint32_t)ctx[v0_3 - a1_6 + 0x6cU];

                        if (v0 == 0xffU) {
                            break;
                        }
                    }
                }
            }

            return 1;
        }
    }
}

extern uint32_t AL_sDPB_BuildRefList_isra_7(char *arg1, int32_t arg2, int32_t *arg3, uint8_t *arg4,
                                            int32_t *arg5, uint8_t *arg6, int32_t *arg7)
    __asm__("AL_sDPB_BuildRefList.isra.7"); /* forward decl, ported by T<N> later */
extern int32_t AL_sDPB_AVC_Reordering_isra_8(void *arg1, int32_t *arg2, int32_t arg3, int32_t *arg4,
                                             int32_t arg5, uint8_t *arg6, int32_t *arg7, int16_t *arg8,
                                             uint8_t *arg9)
    __asm__("AL_sDPB_AVC_Reordering.isra.8"); /* forward decl, ported by T<N> later */

int32_t AL_DPB_HEVC_GetRefInfo(char *arg1, void *arg2, void *arg3, int32_t *arg4, uint8_t arg5)
{
    int32_t var_3c = 0;
    int32_t var_40 = 0;
    int32_t var_100[0x20];
    uint8_t var_80 = 0;
    uint8_t var_60 = 0;
    uint8_t var_5f = 0;
    uint8_t *var_114 = &var_80;
    int32_t *var_118 = &var_3c;
    uint32_t i_2 = AL_sDPB_BuildRefList_isra_7(arg1, *(int32_t *)((uint8_t *)arg2 + 8U),
                                               (int32_t *)((uint8_t *)arg2 + 0x10U), &var_60,
                                               var_118, var_114, &var_40);
    int32_t a2_1 = var_3c;
    int32_t *a3_1 = var_118;
    uint32_t i_5;
    int32_t t0_1;
    int32_t t0_9;
    int32_t v0_29;
    void *t3_2;
    int32_t a2_2;

    (void)i_2;
    (void)a3_1;

    arg4[0x15] = 0xff;
    arg4[0x25] = 0xff;
    arg4[0x35] = 0xff;
    *((uint8_t *)arg3 + 0x85U) = 0;

    if (arg5 != 0U) {
        t0_1 = *(int32_t *)((uint8_t *)arg2 + 0x10U);

        if (t0_1 == 1) {
            goto label_600bc;
        }

        i_5 = (uint32_t)var_60;

        if (t0_1 != 7) {
            if (t0_1 != 0) {
                goto label_5ff40;
            }

            {
                int32_t t0_2 = 1;
                int32_t s6_1 = 0;

                while (1) {
                    uint8_t *v1_1;
                    int32_t t0_10;

                    a2_1 = var_40;

                    if (s6_1 == 0) {
                        if (a2_1 > 0) {
                            v1_1 = &var_60;
                            var_100[0] = *(int32_t *)((uint8_t *)arg3 + 8U);
                            goto label_5feb8;
                        }

                        goto label_604f4;
                    }

                    a2_1 = var_3c;
                    var_100[0] = *(int32_t *)((uint8_t *)arg3 + 0x1cU);

                    if (a2_1 > 0) {
                        v1_1 = &var_80;

label_5feb8:
                        {
                            int32_t a3_2 = 1;
                            uint32_t version_1 = 0;

                            do {
                                uint32_t v0_12 = (uint32_t)*v1_1;

                                if (v0_12 == 0xffU) {
                                    __builtin_trap();
                                }

                                {
                                    int32_t a1_1 = *(int32_t *)(arg1 + v0_12 * 0x18U + 0x80U);

                                    if (a1_1 != var_100[0]) {
                                        var_100[a3_2] = a1_1;
                                        a3_2 += 1;
                                    }
                                }

                                version_1 += 1;
                                v1_1 = &v1_1[1];
                            } while ((int32_t)version_1 < a2_1);
                        }
                    }

                    if (s6_1 == 0) {
label_604f4:
                        ((uintptr_t(*)(void *, int32_t *, int32_t, uint8_t *, void *, uint8_t *))
                             AL_sDPB_HEVC_Reordering)(arg1, var_100, a2_1, &var_60,
                                                      *(void **)*(int32_t **)arg4,
                                                      (uint8_t *)arg3 + 0x8aU);
                        t0_10 = t0_2;
                    } else {
                        var_114 = (uint8_t *)arg3 + 0xccU;
                        var_118 = *(int32_t **)((uint8_t *)arg4[1]);
                        ((uintptr_t(*)(void *, int32_t *, int32_t, uint8_t *, void *, uint8_t *))
                             AL_sDPB_HEVC_Reordering)(arg1, var_100, a2_1, &var_80, var_118, var_114);
                        t0_10 = t0_2;

                        if (t0_10 == 2) {
                            break;
                        }
                    }

                    s6_1 += 1;
                    t0_2 = t0_10 + 1;
                }

                goto label_60234;
            }
        }
    } else {
label_600bc:
        i_5 = (uint32_t)var_60;
        a2_1 = var_3c;

        {
            uint8_t *v1_24;

            if (i_5 == 0xffU) {
                v1_24 = 0;
            } else {
                v1_24 = (uint8_t *)arg1 + i_5 * 0x18U + 0x6cU;
            }

            if (*(int32_t *)((uint8_t *)arg3 + 4U) != 2) {
                uint32_t version_1 = (uint32_t)*(int32_t *)(v1_24 + 0x14U);

                if (version_1 == (uint32_t)*(int32_t *)((uint8_t *)arg3 + 8U)) {
                    var_100[0] = (int32_t)version_1;

                    if (a2_1 > 0) {
                        uint8_t *v1_46 = &var_5f;
                        int32_t a1_17 = 1;

                        while (1) {
                            if (i_5 == 0xffU) {
                                __builtin_trap();
                            }

                            version_1 = (uint32_t)*(int32_t *)(arg1 + i_5 * 0x18U + 0x80U);

                            if (version_1 != (uint32_t)var_100[0]) {
                                var_100[a1_17] = (int32_t)version_1;
                                a1_17 += 1;
                            }

                            if (&(&var_60)[a2_1] == v1_46) {
                                break;
                            }

                            i_5 = (uint32_t)*v1_46;
                            v1_46 = &v1_46[1];
                        }
                    }
                } else if (*(int32_t *)((uint8_t *)arg3 + 0x18U) != 2) {
                    uint32_t version = (uint32_t)*(int32_t *)(v1_24 + 0x14U);

                    if (version == (uint32_t)*(int32_t *)((uint8_t *)arg3 + 0x1cU)) {
                        var_100[0] = (int32_t)version;

                        if (a2_1 > 0) {
                            uint8_t *v1_47 = &var_5f;
                            int32_t a1_18 = 1;

                            while (1) {
                                if (i_5 == 0xffU) {
                                    __builtin_trap();
                                }

                                version_1 = (uint32_t)*(int32_t *)(arg1 + i_5 * 0x18U + 0x80U);

                                if (version_1 != (uint32_t)var_100[0]) {
                                    var_100[a1_18] = (int32_t)version_1;
                                    a1_18 += 1;
                                }

                                if (&(&var_60)[a2_1] == v1_47) {
                                    break;
                                }

                                i_5 = (uint32_t)*v1_47;
                                v1_47 = &v1_47[1];
                            }
                        }
                    }
                }
            }
        }

        var_114 = (uint8_t *)arg3 + 0x8aU;
        var_118 = *(int32_t **)*(int32_t **)arg4;
        ((uintptr_t(*)(void *, int32_t *, int32_t, uint8_t *, void *, uint8_t *))
             AL_sDPB_HEVC_Reordering)(arg1, var_100, a2_1, &var_60, var_118, var_114);
        t0_1 = *(int32_t *)((uint8_t *)arg2 + 0x10U);
        **(int32_t **)arg4[1] = 3;
        i_5 = (uint32_t)var_60;

        if (t0_1 == 2) {
            goto label_60170;
        }
    }

label_5ff40:
    i_5 = (uint32_t)var_60;

    if (t0_1 == 2) {
label_60170:
        a3_1 = (int32_t *)arg4[0];
        *(int32_t *)((uint8_t *)arg3 + 0x7cU) = 0;
        *((uint8_t *)arg3 + 0x7dU) = 0;
        a2_1 = 1;

label_60180:
        t3_2 = (void *)arg4[1];

        {
            int32_t *i = *(int32_t **)((uint8_t *)t3_2 + 4U);

            if (i != 0) {
                int32_t *end = &i[0x10];

                do {
                    *i = -1;
                    i = &i[1];
                } while (i != end);
            }
        }

        t0_9 = var_40;
        v0_29 = var_3c;
        goto label_601b4;
    }

    if (i_5 == 0xffU) {
        __builtin_trap();
    }

    *(int32_t *)((uint8_t *)arg3 + 0x7cU) = *(int32_t *)(arg1 + i_5 * 0x18U + 0x71U);

    {
        int32_t t4_1;

        if (t0_1 != 0) {
            *((uint8_t *)arg3 + 0x7dU) = 0;

            if (t0_1 != 1) {
                t4_1 = 1;
            } else {
                t4_1 = (((*(int32_t *)((uint8_t *)arg3 + 0x18U) ^ 2) < 1) ? 1 : 0);
            }
        } else {
            uint32_t v1_7 = (uint32_t)var_80;

            if (v1_7 == 0xffU) {
                __builtin_trap();
            }

            if ((uint32_t)*(uint8_t *)(arg1 + v1_7 * 0x18U + 0x71U) == 0U) {
                *((uint8_t *)arg3 + 0x7dU) = 0;
                t4_1 = 1;
            } else {
                *((uint8_t *)arg3 + 0x7dU) = 1;
                t4_1 = 1;
            }
        }

        {
            int32_t t2_1 = var_3c;
            int32_t a1_10;
            int32_t a2_2_local = 1;

            if (t2_1 > 0) {
                uint8_t *t1_1;
                uint8_t *v0_20;
                int32_t v1_13;
                int32_t a1_3;
                int32_t t5_1;
                int32_t t3_1;
                uint32_t version_1;

                if (i_5 == 0xffU) {
                    goto label_60060;
                }

                v0_20 = (uint8_t *)arg1 + i_5 * 0x18U + 0x6cU;
                t1_1 = &var_5f;

                if (v0_20 == 0) {
label_60060:
                    a2_2 = 0x615;
                    __assert("pPic",
                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                             a2_2, "AL_DPB_HEVC_GetRefInfo", var_118, var_114);
                }

                a3_1 = (int32_t *)arg4[0];
                t5_1 = 0;
                version_1 = 0;
                t3_1 = 0xff;
                a1_3 = *(int32_t *)(v0_20 + 0x14U);
                v1_13 = a1_3;

                if (t5_1 == 0 && a1_3 == *(int32_t *)((uint8_t *)arg3 + 8U)) {
                    arg4[2] = a1_3;
                    *((uint8_t *)&arg4[5]) = (uint8_t)version_1;

                    {
                        uint32_t t0_8 = (uint32_t)v0_20[2];

                        if (t0_8 != (uint32_t)t3_1) {
                            uint8_t *v1_19 = (uint8_t *)arg1 + t0_8 * 5U;

                            if (v1_19 != 0) {
                                arg4[0x15] = (int32_t)v1_19[4];
                            }
                        }
                    }

                    v1_13 = *(int32_t *)(v0_20 + 0x14U);
                    t5_1 = 1;
                    arg4[4] = 0;
                }

                while (1) {
                    if (t4_1 == 0 && a1_3 == *(int32_t *)((uint8_t *)arg3 + 0x1cU)) {
                        arg4[6] = v1_13;
                        *((uint8_t *)&arg4[9]) = (uint8_t)version_1;

                        {
                            uint32_t a1_9 = (uint32_t)v0_20[2];

                            if (a1_9 != (uint32_t)t3_1) {
                                uint8_t *v1_31 = (uint8_t *)arg1 + a1_9 * 5U;

                                if (v1_31 != 0) {
                                    arg4[0x25] = (int32_t)v1_31[4];
                                }
                            }
                        }

                        v1_13 = *(int32_t *)(v0_20 + 0x14U);
                        t4_1 = 1;
                        arg4[8] = 0;
                    }

                    a1_10 = a3_1[1];

                    if (a1_10 != 0) {
                        *(int32_t *)(uintptr_t)(a1_10 + (int32_t)(version_1 << 2)) = v1_13;
                        v1_13 = *(int32_t *)(v0_20 + 0x14U);
                    }

                    version_1 += 1;

                    if (*(int32_t *)((uint8_t *)arg2 + 8U) < v1_13) {
                        a2_2_local = 0;
                    }

                    if ((int32_t)version_1 >= t2_1) {
                        t0_1 = *(int32_t *)((uint8_t *)arg2 + 0x10U);

                        if (a1_10 == 0) {
                            goto label_602b8;
                        }

                        if ((int32_t)version_1 >= 0x10) {
                            goto label_602b8;
                        }

                        {
                            int32_t *a1_11 = (int32_t *)(uintptr_t)(a1_10 + (int32_t)(version_1 << 2));

                            do {
                                version_1 += 1;
                                *a1_11 = -1;
                                a1_11 = &a1_11[1];
                            } while (version_1 != 0x10U);
                        }

                        goto label_602b8;
                    }

                    {
                        uint32_t v1_15 = (uint32_t)*t1_1;

                        if (v1_15 != (uint32_t)t3_1) {
                            v0_20 = (uint8_t *)arg1 + v1_15 * 0x18U + 0x6cU;
                            t1_1 = &t1_1[1];

                            if (v0_20 != 0) {
                                a1_3 = *(int32_t *)(v0_20 + 0x14U);
                                v1_13 = a1_3;
                                continue;
                            }
                        }
                    }

                    a2_2 = 0x615;
                    __assert("pPic",
                             "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                             a2_2, "AL_DPB_HEVC_GetRefInfo", var_118, var_114);
                }
            }

            a3_1 = (int32_t *)arg4[0];
            a1_10 = a3_1[1];
            a2_1 = a2_2_local;

            if (a1_10 != 0) {
                uint32_t version_1 = 0;
                int32_t *a1_11 = (int32_t *)(uintptr_t)(a1_10 + (int32_t)(version_1 << 2));

                do {
                    version_1 += 1;
                    *a1_11 = -1;
                    a1_11 = &a1_11[1];
                } while (version_1 != 0x10U);
            }
        }
    }

label_602b8:
    {
        uint32_t v0_38;
        uint32_t v1_34;

        if (t0_1 == 1) {
            *((uint8_t *)arg3 + 0x85U) = (uint8_t)t0_1;
            goto label_60494;
        }

        if (*(int32_t *)((uint8_t *)arg3 + 0x14U) == 2) {
            *((uint8_t *)arg3 + 0x85U) = 1;
            v0_38 = (uint32_t)var_60;
            v1_34 = v0_38 << 3;
        } else {
            int32_t v0_37 = (*(int32_t *)((uint8_t *)arg3 + 0x1cU) <
                             *(int32_t *)((uint8_t *)arg2 + 8U)) ? 1 : 0;
            *((uint8_t *)arg3 + 0x85U) = (uint8_t)v0_37;

            if (v0_37 != 0) {
label_60494:
                v0_38 = (uint32_t)var_60;
                v1_34 = v0_38 << 3;
            } else {
                v0_38 = (uint32_t)var_80;
                v1_34 = v0_38 << 3;
            }
        }

        if (v0_38 == 0xffU || (uint8_t *)arg1 + (v0_38 << 5) - v1_34 == (uint8_t *)0xffffff94) {
            a2_2 = 0x63e;
            __assert("pPic",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                     a2_2, "AL_DPB_HEVC_GetRefInfo", var_118, var_114);
        }

        arg4[0x0d] = 0;
        arg4[0x0a] = *(int32_t *)(arg1 + (v0_38 << 5) - v1_34 + 0x80U);

        {
            uint32_t v1_36 = (uint32_t)*(uint8_t *)(arg1 + (v0_38 << 5) - v1_34 + 0x6eU);

            if (v1_36 != 0xffU) {
                uint8_t *v0_45 = (uint8_t *)arg1 + v1_36 * 5U;

                if (v0_45 != 0) {
                    arg4[0x35] = (int32_t)v0_45[4];
                }
            }
        }

        {
            int32_t v0_47 = *(int32_t *)((uint8_t *)arg2 + 0x10U);

            arg4[0x0c] = 0;

            if (v0_47 == 0) {
                int32_t t2_2 = var_40;

                t0_9 = t2_2;

                if (t2_2 > 0) {
                    uint32_t v0_48 = (uint32_t)var_80;
                    uint8_t *v0_52;
                    uint8_t *t1_2;
                    int32_t i_1;
                    int32_t t4_2;
                    int32_t *a1_14;

                    if (v0_48 != 0xffU) {
                        v0_52 = (uint8_t *)arg1 + v0_48 * 0x18U + 0x6cU;
                        t1_2 = &((uint8_t){0});
                    } else {
                        v0_52 = 0;
                        t1_2 = 0;
                    }

                    if (v0_48 == 0xffU || v0_52 == 0) {
                        a2_2 = 0x64d;
                        __assert("pPic",
                                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                                 a2_2, "AL_DPB_HEVC_GetRefInfo", var_118, var_114);
                    }

                    t3_2 = (void *)arg4[1];
                    i_1 = 0;
                    t4_2 = 0;

                    while (1) {
                        int32_t a1_12 = *(int32_t *)(v0_52 + 0x14U);
                        int32_t v1_38 = a1_12;

                        if (t4_2 == 0 && a1_12 == *(int32_t *)((uint8_t *)arg3 + 0x1cU)) {
                            arg4[6] = a1_12;
                            *((uint8_t *)&arg4[9]) = (uint8_t)i_1;

                            {
                                uint32_t a1_13 = (uint32_t)v0_52[2];

                                if (a1_13 != 0xffU) {
                                    uint8_t *v1_44 = (uint8_t *)arg1 + a1_13 * 5U;

                                    if (v1_44 != 0) {
                                        arg4[0x25] = (int32_t)v1_44[4];
                                    }
                                }
                            }

                            v1_38 = *(int32_t *)(v0_52 + 0x14U);
                            t4_2 = 1;
                            arg4[8] = 0;
                        }

                        a1_14 = *(int32_t **)((uint8_t *)t3_2 + 4U);

                        if (a1_14 != 0) {
                            a1_14[i_1] = v1_38;
                            v1_38 = *(int32_t *)(v0_52 + 0x14U);
                            t2_2 = var_40;
                        }

                        i_1 += 1;
                        t0_9 = t2_2;

                        if (*(int32_t *)((uint8_t *)arg2 + 8U) < v1_38) {
                            a2_1 = 0;
                        }

                        if (i_1 >= t2_2) {
                            if (a1_14 != 0 && i_1 < 0x10) {
                                do {
                                    *a1_14 = -1;
                                    a1_14 = &a1_14[1];
                                    i_1 += 1;
                                } while (i_1 != 0x10);
                            }

                            v0_29 = var_3c;
                            goto label_601b4;
                        }

                        {
                            uint32_t v1_40 = (uint32_t)*t1_2;

                            if (v1_40 == 0xffU) {
                                a2_2 = 0x64d;
                                __assert("pPic",
                                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                                         a2_2, "AL_DPB_HEVC_GetRefInfo", var_118, var_114);
                            }

                            v0_52 = (uint8_t *)arg1 + v1_40 * 0x18U + 0x6cU;
                            t1_2 = &t1_2[1];

                            if (v0_52 == 0) {
                                a2_2 = 0x64d;
                                __assert("pPic",
                                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                                         a2_2, "AL_DPB_HEVC_GetRefInfo", var_118, var_114);
                            }
                        }
                    }
                }

                t3_2 = (void *)arg4[1];

                {
                    int32_t *a1_14 = *(int32_t **)((uint8_t *)t3_2 + 4U);
                    int32_t i_1 = 0;

                    if (a1_14 != 0) {
                        do {
                            i_1 += 1;
                            *a1_14 = -1;
                            a1_14 = &a1_14[1];
                        } while (i_1 != 0x10);
                    }
                }

                t0_9 = var_40;
            } else {
                goto label_60180;
            }

            v0_29 = var_3c;
        }
    }

label_601b4:
    a3_1[2] = v0_29;
    *(int32_t *)((uint8_t *)t3_2 + 8U) = t0_9;
    *((uint8_t *)arg3 + 0x86U) = (uint8_t)a2_1;
    return 1;

label_60234:
    t0_1 = *(int32_t *)((uint8_t *)arg2 + 0x10U);
    goto label_5ff40;
}

int32_t AL_DPB_AVC_GetRefInfo(char *arg1, void *arg2, void *arg3, int32_t *arg4, int32_t (*arg5)[0x20],
                              int32_t arg6)
{
    int32_t *out = (int32_t *)arg3;
    int32_t var_24 = arg6;
    int32_t var_34 = 0;
    int32_t var_38 = 0;
    int32_t *var_110 = 0;
    int32_t *var_118 = 0;
    uint8_t *var_114 = 0;
    int32_t var_f8[0x20];
    uint8_t var_78 = 0;
    uint8_t var_58 = 0;
    int32_t *a1_1;
    int16_t *var_10c;
    uint8_t *var_108;
    void *s0 = arg2;
    int32_t v0_1;
    int32_t a3_1;

    (void)var_24;

    out[0x15] = 0xff;
    out[0x25] = 0xff;
    out[0x35] = 0xff;
    v0_1 = *(int32_t *)((uint8_t *)arg2 + 0x10U);
    *((uint8_t *)arg3 + 0x85U) = 0;
    DPB_KMSG("AVC_GetRefInfo entry mode=%d poc=%d poc_l1=%d", v0_1,
             *(int32_t *)((uint8_t *)arg3 + 8U), *(int32_t *)((uint8_t *)arg3 + 0x1cU));

    if (v0_1 == 1) {
        int32_t var_2c_2 = 0x70000;
        int32_t v1_30;

        var_f8[0] = 0;
        arg6 = 0;

        do {
            uint32_t j;

            for (j = (uint32_t)arg1[0x403]; j != 0xffU; j = (uint32_t)*(uint8_t *)(arg1 + j * 5U)) {
                uint8_t *s3_2 = (uint8_t *)arg1 + j * 5U;

                var_114 = (uint8_t *)var_f8;
                var_118 = (int32_t *)&var_58;
                ((uintptr_t(*)(void *, uint32_t, uint32_t, int32_t, int32_t *, uint8_t *, int32_t *))
                     ((uintptr_t)var_2c_2 - 0x2b3cU))(arg1, (uint32_t)s3_2[2], (uint32_t)s3_2[3], arg6,
                                                       var_118, var_114, var_110);
            }

            arg6 += 1;
            v1_30 = var_f8[0];
        } while (arg6 != 2);

        var_34 = v1_30;

        if (v1_30 < 0x20) {
            uint8_t *i = &var_58 + v1_30;

            do {
                *i = 0xff;
                i = &i[1];
            } while ((void *)&var_38 != (void *)i);
        }

        a1_1 = (int32_t *)arg4[0];
    } else {
        var_110 = &var_38;
        var_114 = &var_78;
        var_118 = &var_34;
        AL_sDPB_BuildRefList_isra_7(arg1, *(int32_t *)((uint8_t *)arg2 + 8U), (int32_t *)((uint8_t *)s0 + 0x10U),
                                    &var_58, var_118, var_114, var_110);
        a1_1 = (int32_t *)arg4[0];
    }

    DPB_KMSG("AVC_GetRefInfo post-build l0=%d l1=%d first0=%u first1=%u desc0=%p desc1=%p",
             var_34, var_38, (uint32_t)var_58, (uint32_t)var_78,
             (void *)arg4[0], (void *)arg4[1]);

    {
        int16_t *t4_1 = *(int16_t **)a1_1;

        DPB_KMSG("AVC_GetRefInfo pre-kind-write a1_1=%p kind0=%p", (void *)a1_1, (void *)t4_1);
        if (t4_1 != 0) {
            int32_t *v1_1 = (int32_t *)arg4[1];
            int32_t *kind1 = (v1_1 != 0) ? *(int32_t **)v1_1 : 0;

            DPB_KMSG("AVC_GetRefInfo pre-kind-write kind1_desc=%p kind1=%p",
                     (void *)v1_1, (void *)kind1);
            *t4_1 = 3;
            DPB_KMSG("AVC_GetRefInfo post-kind0-write kind0=%p", (void *)t4_1);
            if (kind1 != 0) {
                *kind1 = 3;
            }
            DPB_KMSG("AVC_GetRefInfo post-kind1-write kind1=%p", (void *)kind1);
        }

        v0_1 = *(int32_t *)((uint8_t *)s0 + 0x10U);
        a3_1 = var_34;

        if (v0_1 == 1) {
            var_f8[0] = *(int32_t *)((uint8_t *)arg3 + 8U);

            if (a3_1 > 0) {
                uint8_t *v1_29 = &var_58;
                int32_t a1_4 = 1;

                while (1) {
                    uint32_t v0_68 = (uint32_t)*v1_29;

                    if (v0_68 == 0xffU) {
                        __builtin_trap();
                    }

                    {
                        int32_t a0_13 = *(int32_t *)(arg1 + v0_68 * 0x18U + 0x80U);

                        if (a0_13 != var_f8[0]) {
                            var_f8[a1_4] = a0_13;
                            a1_4 += 1;
                        }
                    }

                    v1_29 = &v1_29[1];

                    if (v1_29 == &var_58 + a3_1) {
                        break;
                    }
                }

                if (a1_4 != 1) {
                    var_108 = (uint8_t *)arg3 + 0x8aU;
                    var_110 = &var_34;
                    var_118 = &a3_1;
                    var_10c = t4_1;
                    var_114 = &var_58;
                    AL_sDPB_AVC_Reordering_isra_8(arg1, (int32_t *)((uint8_t *)s0 + 8U),
                                                 *(int32_t *)((uint8_t *)s0 + 0xcU), var_f8,
                                                 *var_118, var_114, var_110, var_10c,
                                                 var_108);
                    v0_1 = *(int32_t *)((uint8_t *)s0 + 0x10U);
                    **(int32_t **)arg4[1] = 3;

                    if (v0_1 == 2) {
                        a1_1 = (int32_t *)arg4[0];
                        goto label_70b10;
                    }
                }
            }

            a3_1 = var_34;
            v0_1 = 1;
            arg5 = ((*(int32_t *)((uint8_t *)arg3 + 0x18U) ^ 2) < 1) ? (int32_t(*)[0x20])1 : 0;
        } else if (v0_1 == 0) {
            int32_t s4_1 = 1;
            int32_t s3_1 = 0;
            int32_t *s2_1 = (int32_t *)((uint8_t *)s0 + 8U);
            int32_t *var_2c_1 = &var_38;

            while (1) {
                uint8_t *v1_4;
                uint8_t *v0_14_base;

                a3_1 = var_34;

                if (s3_1 == 0) {
                    v0_14_base = (uint8_t *)arg3;
                    v1_4 = &var_58;
                } else {
                    a3_1 = var_38;
                    v0_14_base = (uint8_t *)arg3 + 0x14U;
                    v1_4 = &var_78;
                }

                var_f8[0] = *(int32_t *)(v0_14_base + 8U);

                if (a3_1 > 0) {
                    uint8_t *a2_1 = v1_4 + a3_1;
                    int32_t a1_2 = 1;

                    do {
                        uint32_t v0_22 = (uint32_t)*v1_4;

                        if (v0_22 == 0xffU) {
                            __builtin_trap();
                        }

                        {
                            int32_t a0_1 = *(int32_t *)(arg1 + v0_22 * 0x18U + 0x80U);

                            if (a0_1 != var_f8[0]) {
                                var_f8[a1_2] = a0_1;
                                a1_2 += 1;
                            }
                        }

                        v1_4 = &v1_4[1];
                    } while (a2_1 != v1_4);

                    if (a1_2 != 1) {
                        if (s3_1 == 0) {
                            var_108 = (uint8_t *)arg3 + 0x8aU;
                            var_10c = *(int16_t **) *(int32_t **)arg4;
                            var_110 = &var_34;
                            var_118 = &a3_1;
                            var_114 = &var_58;
                            AL_sDPB_AVC_Reordering_isra_8(arg1, s2_1,
                                                         *(int32_t *)((uint8_t *)s0 + 0xcU), var_f8,
                                                         *var_118, var_114, var_110,
                                                         var_10c, var_108);
                        } else {
                            var_108 = (uint8_t *)arg3 + 0xccU;
                            var_10c = *(int16_t **)(uintptr_t)*(int32_t *)((uint8_t *)arg4[1]);
                            var_118 = &a3_1;
                            var_110 = var_2c_1;
                            var_114 = &var_78;
                            AL_sDPB_AVC_Reordering_isra_8(arg1, s2_1,
                                                         *(int32_t *)((uint8_t *)s0 + 0xcU), var_f8,
                                                         *var_118, var_114, var_110,
                                                         var_10c, var_108);
                        }
                    }
                }

                if (s4_1 == 2) {
                    break;
                }

                s3_1 += 1;
                s4_1 += 1;
            }

            v0_1 = *(int32_t *)((uint8_t *)s0 + 0x10U);

            if (v0_1 == 2) {
                a1_1 = (int32_t *)arg4[0];
                goto label_70b10;
            }

            if (v0_1 == 1) {
                a3_1 = var_34;
                v0_1 = 1;
                arg5 = ((*(int32_t *)((uint8_t *)arg3 + 0x18U) ^ 2) < 1) ? (int32_t(*)[0x20])1 : 0;
            } else {
                arg5 = (int32_t(*)[0x20])1;
                a3_1 = var_34;
            }
        } else if (v0_1 != 2) {
            arg5 = (int32_t(*)[0x20])1;
        }
    }

    if (a3_1 > 0) {
        uint32_t v0_2 = (uint32_t)var_58;
        DPB_KMSG("AVC_GetRefInfo pre-l0-walk count=%d first=%u", a3_1, v0_2);

        if (v0_2 == 0xffU) {
            __assert("pPic",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                     0x6b8, "AL_DPB_AVC_GetRefInfo", var_118, var_114, var_110, var_10c, var_108);
        }

        {
            uint8_t *v0_6 = (uint8_t *)arg1 + v0_2 * 0x18U + 0x6cU;
            uint8_t *a2_1 = &(&var_58)[1];
            int32_t t4_2 = 0;
            uint32_t a0 = 0;

            if (v0_6 == 0) {
                __assert("pPic",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                         0x6b8, "AL_DPB_AVC_GetRefInfo", var_118, var_114, var_110, var_10c, var_108);
            }

            a1_1 = (int32_t *)arg4[0];

            while (1) {
                int32_t v1_4 = *(int32_t *)(v0_6 + 0x14U);

                if (t4_2 == 0 && v1_4 == *(int32_t *)((uint8_t *)arg3 + 8U)) {
                    out[2] = v1_4;
                    *((uint8_t *)&out[5]) = (uint8_t)a0;

                    {
                        uint32_t t5_3 = (uint32_t)v0_6[2];

                        if (t5_3 != 0xffU) {
                            uint8_t *t4_5 = (uint8_t *)arg1 + t5_3 * 5U;

                            if (t4_5 != 0) {
                                out[0x15] = (int32_t)t4_5[4];
                            }
                        }
                    }

                    out[4] = 0;
                    t4_2 = 1;
                }

                if (arg5 == 0 && v1_4 == *(int32_t *)((uint8_t *)arg3 + 0x1cU)) {
                    *((uint8_t *)&out[9]) = (uint8_t)a0;
                    out[6] = *(int32_t *)(v0_6 + 0x14U);

                    {
                        uint32_t t3 = (uint32_t)v0_6[2];

                        if (t3 != 0xffU) {
                            uint8_t *v1_10 = (uint8_t *)arg1 + t3 * 5U;

                            if (v1_10 != 0) {
                                out[0x25] = (int32_t)v1_10[4];
                            }
                        }
                    }

                    out[8] = 0;
                    arg5 = (int32_t(*)[0x20])1;
                }

                {
                    int32_t *v1_14 = dpb_validate_list_ptr(*(int32_t **)&a1_1[1], "l0");

                    if (v1_14 != 0) {
                        v1_14[a0] = *(int32_t *)(v0_6 + 0x14U);
                        a3_1 = var_34;
                    }

                    a0 += 1;

                    if ((int32_t)a0 >= a3_1) {
                        if (v1_14 != 0 && (int32_t)a0 < 0x20) {
                            int32_t *v1_15 = &v1_14[a0];

                            while (1) {
                                a0 += 1;
                                *v1_15 = -1;
                                v1_15 = &v1_15[1];

                                if (a0 == 0x20U) {
                                    break;
                                }
                            }
                        }

                        goto label_709e0;
                    }
                }

                {
                    uint32_t v1_5 = (uint32_t)*a2_1;

                    if (v1_5 != 0xffU) {
                        v0_6 = (uint8_t *)arg1 + v1_5 * 0x18U + 0x6cU;
                        a2_1 = &a2_1[1];

                        if (v0_6 != 0) {
                            continue;
                        }
                    }
                }

                __assert("pPic",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                         0x6b8, "AL_DPB_AVC_GetRefInfo", var_118, var_114, var_110, var_10c, var_108);
            }
        }
        DPB_KMSG("AVC_GetRefInfo post-l0-walk count=%d", a3_1);
    } else {
        {
            int32_t *v1_14 = dpb_validate_list_ptr(*(int32_t **)&a1_1[1], "l0-empty");
            uint32_t a0 = 0;

            if (v1_14 != 0) {
                do {
                    a0 += 1;
                    *v1_14 = -1;
                    v1_14 = &v1_14[1];
                } while (a0 != 0x20U);
            }
        }
    }

label_709e0:
    if (v0_1 == 1) {
        uint32_t v0_38 = (uint32_t)var_58;

label_70abc:
        if (v0_38 != 0xffU) {
            uint32_t v0_40 = v0_38 * 0x18U;
            uint8_t *v0_41 = (uint8_t *)arg1 + v0_40;

            if (v0_41 != (uint8_t *)0xffffff94) {
                out[0x0d] = 0;
                out[0x0a] = *(int32_t *)(v0_41 + 0x80U);

                {
                    uint32_t v1_22 = (uint32_t)v0_41[0x6e];

                    if (v1_22 != 0xffU) {
                        out[0x35] = (int32_t)*(uint8_t *)(arg1 + v1_22 * 5U + 4U);
                    }
                }

                {
                    int32_t v0_46 = *(int32_t *)((uint8_t *)s0 + 0x10U);

                    out[0x0c] = 0;

                    if (v0_46 != 0) {
                        goto label_70b10;
                    }
                }

                goto label_709f0;
            }
        }

        __assert("pPic",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                 0x6d4, "AL_DPB_AVC_GetRefInfo", var_118, var_114, var_110, var_10c, var_108);
    }

    if (v0_1 != 0) {
        goto label_70b10;
    }

label_709f0:
    {
        int32_t a3_4 = var_38;
        uint32_t v0_26 = (uint32_t)var_78;
        uint32_t v0_51;
        int32_t v1_17;
        int32_t *a0_10;
        DPB_KMSG("AVC_GetRefInfo pre-l1 stage count=%d first=%u", a3_4, v0_26);

        if (a3_4 <= 0) {
            void *t2_1 = (void *)arg4[1];

                    a0_10 = dpb_validate_list_ptr(*(int32_t **)((uint8_t *)t2_1 + 4U), "l1");
            v1_17 = 0;

            if (a0_10 != 0) {
                do {
                    a0_10[v1_17] = -1;
                    v1_17 += 1;
                } while (v1_17 != 0x20);
            }

            v0_51 = (uint32_t)var_78;
        } else if (v0_26 == 0xffU) {
            __assert("pPic",
                     "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                     0x6e4, "AL_DPB_AVC_GetRefInfo", var_118, var_114, var_110, var_10c, var_108);
            v0_51 = (uint32_t)var_78;
        } else {
            uint8_t *v0_30 = (uint8_t *)arg1 + v0_26 * 0x18U + 0x6cU;
            void *t2_1 = (void *)arg4[1];
            uint8_t *a2_5 = &(&var_78)[1];
            int32_t t3_1 = 0;

            v1_17 = 0;

            if (v0_30 == 0) {
                __assert("pPic",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                         0x6e4, "AL_DPB_AVC_GetRefInfo", var_118, var_114, var_110, var_10c, var_108);
            }

            while (1) {
                if (t3_1 == 0) {
                    int32_t a0_3 = *(int32_t *)(v0_30 + 0x14U);

                    if (a0_3 == *(int32_t *)((uint8_t *)arg3 + 0x1cU)) {
                        out[6] = a0_3;
                        *((uint8_t *)&out[9]) = (uint8_t)v1_17;

                        {
                            uint32_t t3_2 = (uint32_t)v0_30[2];

                            if (t3_2 != 0xffU) {
                                uint8_t *a0_8 = (uint8_t *)arg1 + t3_2 * 5U;

                                if (a0_8 != 0) {
                                    out[0x25] = (int32_t)a0_8[4];
                                }
                            }
                        }

                        out[8] = 0;
                        t3_1 = 1;
                    }
                }

            a0_10 = dpb_validate_list_ptr(*(int32_t **)((uint8_t *)t2_1 + 4U), "l1-empty");

                if (a0_10 != 0) {
                    a0_10[v1_17] = *(int32_t *)(v0_30 + 0x14U);
                    a3_4 = var_38;
                }

                v1_17 += 1;

                if (v1_17 >= a3_4) {
                    if (a0_10 != 0 && v1_17 < 0x20) {
                        do {
                            a0_10[v1_17] = -1;
                            v1_17 += 1;
                        } while (v1_17 != 0x20);
                    }

                    v0_51 = (uint32_t)var_78;
                    break;
                }

                {
                    uint32_t v0_34 = (uint32_t)*a2_5;

                    if (v0_34 != 0xffU) {
                        v0_30 = (uint8_t *)arg1 + v0_34 * 0x18U + 0x6cU;
                        a2_5 = &a2_5[1];

                        if (v0_30 != 0) {
                            continue;
                        }
                    }
                }

                __assert("pPic",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                         0x6e4, "AL_DPB_AVC_GetRefInfo", var_118, var_114, var_110, var_10c, var_108);
                v0_51 = (uint32_t)var_78;
                break;
            }
        }

        DPB_KMSG("AVC_GetRefInfo post-l1 stage count=%d first=%u", a3_4, v0_51);

label_70bec:
        if (v0_51 != 0xffU) {
            uint32_t v0_54 = v0_51 * 0x18U;
            uint8_t *v0_55 = (uint8_t *)arg1 + v0_54;

            if (v0_55 != (uint8_t *)0xffffff94) {
                out[0x0d] = 0;
                out[0x0a] = *(int32_t *)(v0_55 + 0x80U);

                {
                    uint32_t v1_27 = (uint32_t)v0_55[0x6e];

                    if (v1_27 != 0xffU) {
                        out[0x35] = (int32_t)*(uint8_t *)(arg1 + v1_27 * 5U + 4U);
                    }
                }

                out[0x0c] = 0;
            } else {
                __assert("pPic",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_buf_mngt/DPB.c",
                         0x6f9, "AL_DPB_AVC_GetRefInfo", var_118, var_114, var_110, var_10c, var_108);
            }
        }
    }

label_70b10:
    {
        void *t2_1 = (void *)arg4[1];
        int32_t *i_1 = *(int32_t **)((uint8_t *)t2_1 + 4U);

        if (i_1 != 0) {
            int32_t *end = &i_1[0x20];

            do {
                *i_1 = -1;
                i_1 = &i_1[1];
            } while (i_1 != end);
        }

        a1_1[2] = var_34;
        *(int32_t *)((uint8_t *)t2_1 + 8U) = var_38;
        DPB_KMSG("AVC_GetRefInfo exit l0=%d l1=%d", var_34, var_38);
        return 1;
    }
}
