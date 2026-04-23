#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "alcodec/al_rtos.h"

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line,
                        const char *function, void *caller);

typedef struct AL_TAllocatorVtableCompat {
    void *reserved0;
    void *(*Alloc)(void *allocator, int32_t size);
    void (*Free)(void *allocator, void *buffer);
    void *(*GetVirtualAddr)(void *allocator, void *buffer);
} AL_TAllocatorVtableCompat;

#define RC_KMSG(fmt, ...)                                                                         \
    do {                                                                                          \
        int _kfd = open("/dev/kmsg", O_WRONLY);                                                   \
        if (_kfd >= 0) {                                                                          \
            char _buf[256];                                                                       \
            int _n = snprintf(_buf, sizeof(_buf), "libimp/RC: " fmt "\n", ##__VA_ARGS__);        \
            if (_n > 0) {                                                                         \
                write(_kfd, _buf, (size_t)_n);                                                    \
            }                                                                                     \
            close(_kfd);                                                                          \
        }                                                                                         \
    } while (0)

/* Placement:
 * - AL_RateCtrl_Init @ RateCtrl_11.c
 * - AL_RateCtrl_Deinit @ RateCtrl_11.c
 * - IOOi/OoOi/ioOi/loOi/OiOi/ooOi @ RateCtrl_11.c
 */

/* stock name: IOOi */
static int32_t rc_IOOi(void *arg1, uint32_t arg2);
/* stock name: OoOi */
static int32_t rc_OoOi(void *arg1, uint32_t arg2);
/* stock name: ioOi */
static int32_t rc_ioOi(int32_t *arg1, int32_t arg2);
/* stock name: loOi */
static int32_t rc_loOi(int32_t *arg1);
/* stock name: OiOi */
static int32_t rc_OiOi(int32_t *arg1);
/* stock name: ooOi */
static int32_t rc_ooOi(void *arg1, uint32_t arg2, uint32_t arg3);

/* forward decl, ported by T<N> later */
int32_t rc_oiii(void *arg1, uint32_t arg2, uint32_t arg3);
/* forward decl, ported by T<N> later */
int32_t rc_IIii(void *arg1, void *arg2, void *arg3, int32_t arg4, int32_t *arg5);
/* forward decl, ported by T<N> later */
int32_t rc_OOoI(void *arg1, void *arg2, void *arg3, int32_t arg4, int32_t *arg5);
/* forward decl, ported by T<N> later */
int32_t rc_IlOI(void);
/* forward decl, ported by T<N> later */
int32_t rc_llOI(void);
/* forward decl, ported by T<N> later */
int32_t rc_i0OI(void);
/* forward decl, ported by T<N> later */
int32_t rc_l0OI(void);
/* forward decl, ported by T<N> later */
int32_t rc_O1OI(void);
/* forward decl, ported by T<N> later */
int32_t rc_o1OI(void);
/* forward decl, ported by T<N> later */
int32_t rc_i1OI(void);
/* forward decl, ported by T<N> later */
int32_t rc_I1OI(void);
/* forward decl, ported by T<N> later */
int32_t rc_I0OI(void);
/* forward decl, ported by T<N> later */
int32_t rc_OiOI(void);
/* forward decl, ported by T<N> later */
int32_t rc_oiOI(void);
/* forward decl, ported by T<N> later */
int32_t rc_iiOI(void);
/* forward decl, ported by T<N> later */
int32_t rc_IiOI(void);
/* forward decl, ported by T<N> later */
int32_t rc_OIOI(void);
/* forward decl, ported by T<N> later */
int32_t rc_oIOI(void);
/* forward decl, ported by T<N> later */
int32_t rc_iIOI(void);
/* forward decl, ported by T<N> later */
int32_t rc_IIOI(void);
/* forward decl, ported by T<N> later */
int32_t rc_oIoi(void);
/* forward decl, ported by T<N> later */
int32_t rc_iIoi(int32_t arg1, void *arg2);
/* forward decl, ported by T<N> later */
int32_t rc_lIoi(void);
/* forward decl, ported by T<N> later */
int32_t rc_Iloi(void);
/* forward decl, ported by T<N> later */
int16_t rc_O0oi(int32_t arg1, void *arg2, uint16_t *arg3);
/* forward decl, ported by T<N> later */
int32_t rc_i0oi(void);
/* forward decl, ported by T<N> later */
int32_t rc_l0oi(void);
/* forward decl, ported by T<N> later */
int32_t rc_o1oi(void);
/* forward decl, ported by T<N> later */
uint32_t AL_RateCtrl_Deinit(void *arg1);
int16_t AL_RateCtrl_ExtractStatistics(void *arg1, int32_t *arg2);
/* forward decl, ported by T<N> later */
int32_t rc_Ooii(void);

static int32_t rc_OIoi;

int32_t AL_RateCtrl_Init(void *arg1, void *arg2, uint32_t arg3, char arg4)
{
    uint32_t a3;
    int32_t a2_2;
    int32_t result;
    void *vtable = arg2 ? *(void **)arg2 : NULL;
    void *alloc_fn = vtable ? *(void **)((char *)vtable + 4) : NULL;
    void *getvirt_fn = vtable ? *(void **)((char *)vtable + 0xc) : NULL;

    RC_KMSG("Init entry rc=%p alloc=%p vtbl=%p alloc_fn=%p getvirt=%p mode=%u arg4=%u",
            arg1, arg2, vtable, alloc_fn, getvirt_fn, arg3, (unsigned)(uint8_t)arg4);

    if (arg1 == NULL) {
        __assert("pRCCtx",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_11.c",
                 0x5c, "AL_RateCtrl_Init", &_gp);
    }
    if (arg2 == NULL) {
        RC_KMSG("Init null allocator rc=%p", arg1);
        return AL_RateCtrl_Deinit((void *)(intptr_t)__assert(
            "pAllocator",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_11.c",
            0x61, "AL_RateCtrl_Init", &_gp));
    }

    *(void **)((char *)arg1 + 0x24) = arg2;
    *(int32_t *)((char *)arg1 + 0x28) = 0;
    *(int32_t *)((char *)arg1 + 0x20) = 0;
    if (arg3 >= 0xa) {
        RC_KMSG("Init unsupported mode=%u rc=%p", arg3, arg1);
        return 0;
    }

    a3 = (uint32_t)(uint8_t)arg4;
    switch (arg3) {
    case 0:
        result = rc_IOOi(arg1, a3);
        break;
    case 1:
        result = rc_OoOi(arg1, a3);
        break;
    case 2:
        result = rc_ioOi((int32_t *)arg1, 1);
        break;
    case 3:
        result = rc_ioOi((int32_t *)arg1, 2);
        break;
    case 4:
        result = rc_loOi((int32_t *)arg1);
        break;
    case 5:
        result = rc_OiOi((int32_t *)arg1);
        break;
    case 6:
        result = rc_ioOi((int32_t *)arg1, 3);
        break;
    case 8:
        a2_2 = 1;
        result = rc_ooOi(arg1, a3, (uint32_t)a2_2);
        break;
    case 9:
        a2_2 = 0;
        result = rc_ooOi(arg1, a3, (uint32_t)a2_2);
        break;
    default:
        return 0;
    }

    if (result != 0) {
        *(void **)((char *)arg1 + 0x2c) = Rtos_CreateMutex();
    }
    RC_KMSG("Init exit rc=%p result=%d fn=%p alloc=%p state=%p mutex=%p",
            arg1, result, *(void **)((char *)arg1 + 0x20), *(void **)((char *)arg1 + 0x24),
            *(void **)((char *)arg1 + 0x28), *(void **)((char *)arg1 + 0x2c));
    return result;
}

uint32_t AL_RateCtrl_Deinit(void *arg1)
{
    if (arg1 == NULL) {
        void *a0_3;
        int32_t *a1_2;

        a0_3 = (void *)(intptr_t)__assert(
            "pRCCtx", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_11.c",
            0x121, "AL_RateCtrl_Deinit", &_gp);
        a1_2 = NULL;
        return (uint32_t)AL_RateCtrl_ExtractStatistics(a0_3, a1_2);
    }
    RC_KMSG("Deinit entry rc=%p fn=%p alloc=%p state=%p mutex=%p",
            arg1, *(void **)((char *)arg1 + 0x20), *(void **)((char *)arg1 + 0x24),
            *(void **)((char *)arg1 + 0x28), *(void **)((char *)arg1 + 0x2c));

    {
        int32_t (*t9)(void) = *(int32_t (**)(void))((char *)arg1 + 0x20);

        if (t9 != NULL) {
            RC_KMSG("Deinit callback rc=%p cb=%p", arg1, t9);
            t9();
        }
    }
    Rtos_DeleteMutex(*(void **)((char *)arg1 + 0x2c));
    {
        void *a0_1 = *(void **)((char *)arg1 + 0x24);
        void *state = *(void **)((char *)arg1 + 0x28);

        if (a0_1 == NULL) {
            RC_KMSG("Deinit skip null allocator rc=%p state=%p", arg1, state);
            return 0;
        }

        {
            AL_TAllocatorVtableCompat *vtable = *(AL_TAllocatorVtableCompat **)a0_1;

            if (vtable == NULL || vtable->Free == NULL) {
                RC_KMSG("Deinit skip bad vtable rc=%p alloc=%p vtbl=%p state=%p",
                        arg1, a0_1, vtable, state);
                return 0;
            }

            RC_KMSG("Deinit free rc=%p alloc=%p state=%p", arg1, a0_1, state);
            vtable->Free(a0_1, state);
        }
        return 0;
    }
}

int16_t AL_RateCtrl_ExtractStatistics(void *arg1, int32_t *arg2)
{
    int32_t t5 = *(int32_t *)((char *)arg1 + 8);
    int32_t t4 = *(int32_t *)((char *)arg1 + 0xc);
    int32_t t3 = *(int32_t *)((char *)arg1 + 0x1c);
    int32_t t2 = *(int32_t *)((char *)arg1 + 0x20);
    int32_t t1 = *(int32_t *)((char *)arg1 + 0x24);
    int32_t t0 = *(int32_t *)((char *)arg1 + 0x28);
    int32_t a3 = *(int32_t *)((char *)arg1 + 0x2c);
    int32_t a2 = *(int32_t *)((char *)arg1 + 0x38);
    int16_t v1 = *(int16_t *)((char *)arg1 + 0x3e);
    int16_t result = *(int16_t *)((char *)arg1 + 0x40);

    *arg2 = *(int32_t *)((char *)arg1 + 4);
    arg2[1] = t5;
    arg2[2] = t4;
    arg2[3] = t3;
    arg2[4] = t2;
    arg2[5] = t1;
    arg2[6] = t0;
    arg2[7] = a3;
    arg2[8] = a2;
    *(int16_t *)((char *)arg2 + 0x24) = v1;
    *(int16_t *)((char *)arg2 + 0x26) = result;
    return result;
}

/* stock name: IOOi */
static int32_t rc_IOOi(void *arg1, uint32_t arg2)
{
    int32_t result = rc_oiii(arg1, arg2, 1);

    if (result != 0) {
        *(intptr_t *)((char *)arg1 + 0x0c) = (intptr_t)rc_IIii;
    }
    return result;
}

/* stock name: OoOi */
static int32_t rc_OoOi(void *arg1, uint32_t arg2)
{
    int32_t result = rc_oiii(arg1, arg2, 1);

    if (result != 0) {
        *(intptr_t *)((char *)arg1 + 0x0c) = (intptr_t)rc_OOoI;
    }
    return result;
}

/* stock name: ioOi */
static int32_t rc_ioOi(int32_t *arg1, int32_t arg2)
{
    int32_t *v0 = (int32_t *)(intptr_t)arg1[9];
    int32_t (*t9)(void *, int32_t) = *(int32_t (**)(void *, int32_t))(*(intptr_t *)v0 + 4);

    if (t9 == NULL) {
        arg1[10] = 0;
        return 0;
    }

    {
        void *v0_1 = (void *)(intptr_t)t9(v0, 0x24);

        arg1[10] = (int32_t)(intptr_t)v0_1;
        if (v0_1 == NULL) {
            return 0;
        }
        {
            AL_TAllocatorVtableCompat *vtable =
                *(AL_TAllocatorVtableCompat **)(intptr_t)arg1[9];
            char *v0_4 = (char *)vtable->GetVirtualAddr((void *)(intptr_t)arg1[9], v0_1);

            arg1[12] = (int32_t)(intptr_t)v0_4;
            arg1[0] = (int32_t)(intptr_t)rc_IlOI;
            arg1[1] = (int32_t)(intptr_t)rc_llOI;
            arg1[2] = (int32_t)(intptr_t)rc_i0OI;
            arg1[3] = (int32_t)(intptr_t)rc_l0OI;
            arg1[4] = (int32_t)(intptr_t)rc_O1OI;
            arg1[5] = (int32_t)(intptr_t)rc_o1OI;
            arg1[6] = (int32_t)(intptr_t)rc_i1OI;
            arg1[7] = (int32_t)(intptr_t)rc_I1OI;
            if (arg2 == 3) {
                arg1[2] = (int32_t)(intptr_t)rc_I0OI;
            }
            *(int32_t *)(v0_4 + 0x14) = 0x33;
            *(int32_t *)(v0_4 + 4) = 0x1a;
            *(int32_t *)(v0_4 + 0x0c) = -1;
            *(int32_t *)(v0_4 + 8) = ((uint32_t)(arg2 ^ 1) < 1) ? 1 : 0;
            *(int32_t *)(v0_4 + 0x10) = 0;
            *(int32_t *)(v0_4 + 0x00) = 1;
            return 1;
        }
    }
}

/* stock name: loOi */
static int32_t rc_loOi(int32_t *arg1)
{
    int32_t *v0 = (int32_t *)(intptr_t)arg1[9];
    int32_t (*t9)(void *, int32_t) = *(int32_t (**)(void *, int32_t))(*(intptr_t *)v0 + 4);

    if (t9 == NULL) {
        arg1[10] = 0;
        return 0;
    }

    {
        void *v0_1 = (void *)(intptr_t)t9(v0, 0x88);

        arg1[10] = (int32_t)(intptr_t)v0_1;
        if (v0_1 == NULL) {
            return 0;
        }
        {
            AL_TAllocatorVtableCompat *vtable =
                *(AL_TAllocatorVtableCompat **)(intptr_t)arg1[9];
            char *v0_4 = (char *)vtable->GetVirtualAddr((void *)(intptr_t)arg1[9], v0_1);

            arg1[12] = (int32_t)(intptr_t)v0_4;
            arg1[0] = (int32_t)(intptr_t)rc_OiOI;
            arg1[1] = (int32_t)(intptr_t)rc_oiOI;
            arg1[2] = (int32_t)(intptr_t)rc_iiOI;
            arg1[3] = (int32_t)(intptr_t)rc_IiOI;
            arg1[4] = (int32_t)(intptr_t)rc_OIOI;
            arg1[5] = (int32_t)(intptr_t)rc_oIOI;
            arg1[6] = (int32_t)(intptr_t)rc_iIOI;
            arg1[7] = (int32_t)(intptr_t)rc_IIOI;
            *(int32_t *)(v0_4 + 0x84) = 0x33;
            *(int16_t *)(v0_4 + 0x82) = 0;
            *(int32_t *)(v0_4 + 0x00) = 1;
            return 1;
        }
    }
}

/* stock name: OiOi */
static int32_t rc_OiOi(int32_t *arg1)
{
    arg1[0] = (int32_t)(intptr_t)rc_oIoi;
    arg1[1] = (int32_t)(intptr_t)rc_iIoi;
    arg1[2] = (int32_t)(intptr_t)rc_lIoi;
    arg1[3] = (int32_t)(intptr_t)rc_Iloi;
    arg1[4] = (int32_t)(intptr_t)rc_O0oi;
    arg1[5] = (int32_t)(intptr_t)rc_i0oi;
    arg1[6] = (int32_t)(intptr_t)rc_l0oi;
    arg1[7] = (int32_t)(intptr_t)rc_o1oi;
    return 1;
}

/* stock name: ooOi */
static int32_t rc_ooOi(void *arg1, uint32_t arg2, uint32_t arg3)
{
    int32_t result = rc_oiii(arg1, arg2, arg3);

    if (result != 0) {
        *(intptr_t *)((char *)arg1 + 0x0c) = (intptr_t)rc_Ooii;
    }
    return result;
}

/* stock name: oIoi */
int32_t rc_oIoi(void)
{
    return 0;
}

/* stock name: iIoi */
int32_t rc_iIoi(int32_t arg1, void *arg2)
{
    (void)arg1;
    rc_OIoi = *(int32_t *)((char *)arg2 + 0x18);
    return 0x120000;
}

/* stock name: lIoi */
int32_t rc_lIoi(void)
{
    return 0;
}

/* stock name: Iloi */
int32_t rc_Iloi(void)
{
    return 0;
}

/* stock name: O0oi */
int16_t rc_O0oi(int32_t arg1, void *arg2, uint16_t *arg3)
{
    uint16_t result;

    (void)arg1;
    if (*(uint8_t *)((char *)arg2 + 0x2c) != 0) {
        int16_t signed_value = *(int16_t *)((char *)arg2 + 0x2d);

        *arg3 = (uint16_t)signed_value;
        return signed_value;
    }

    result = (uint16_t)rc_OIoi;
    *arg3 = result;
    return (int16_t)result;
}

/* stock name: o1oi */
int32_t rc_o1oi(void)
{
    return 0;
}

/* stock name: l0oi */
int32_t rc_l0oi(void)
{
    return 0;
}

/* stock name: i0oi */
int32_t rc_i0oi(void)
{
    return 0;
}
