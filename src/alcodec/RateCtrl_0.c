#include <stdint.h>

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

/* Placement:
 * - AL_GopMngr_Init @ RateCtrl_0.c
 * - AL_GopMngr_Deinit @ RateCtrl_0.c
 * - O/i/Il0 @ RateCtrl_0.c
 */

/* forward decl, ported by T<N> later */
int32_t AL_GetGopMngrType(int32_t arg1, uint32_t arg2);
int32_t AL_GopMngr_Deinit(void *arg1);

/* stock name: O */
static int32_t rc_O(void *arg1, void *arg2);
/* stock name: i */
static int32_t rc_i(void *arg1, void *arg2);
/* stock name: Il0 */
int32_t rc_Il0(void *arg1);

/* forward decl, ported by T<N> later */
static void *rc_i10(void *arg1, int32_t arg2, int32_t arg3, int16_t arg4, int16_t arg5);
/* forward decl, ported by T<N> later */
int32_t rc_I10(void *arg1, int32_t *arg2);
/* forward decl, ported by T<N> later */
int32_t rc_OO1(void *arg1);
/* forward decl, ported by T<N> later */
int32_t rc_ol1(void *arg1, int32_t *arg2);
/* forward decl, ported by T<N> later */
int32_t rc_O11(void *arg1, void *arg2);
/* forward decl, ported by T<N> later */
int32_t rc_iOOo(void *arg1, void *arg2, int32_t *arg3, int32_t *arg4, int32_t *arg5);
/* forward decl, ported by T<N> later */
static int32_t rc_oO1(void *arg1, int32_t *arg2);
/* forward decl, ported by T<N> later */
int32_t rc_IO1(void *arg1, int32_t *arg2);
/* forward decl, ported by T<N> later */
static int32_t *rc_Oo1(void *arg1, int32_t arg2, void *arg3, void *arg4);
/* forward decl, ported by T<N> later */
int32_t rc_oI1(void *arg1, int32_t arg2);
/* forward decl, ported by T<N> later */
static int32_t rc_iI1(void *arg1);
/* forward decl, ported by T<N> later */
static int32_t rc_Ol1(void *arg1);
/* forward decl, ported by T<N> later */
static void *rc_lI1(void *arg1);
/* forward decl, ported by T<N> later */
void *rc_OI1(void *arg1, int32_t arg2);
/* forward decl, ported by T<N> later */
static int32_t rc_ioOo(void *arg1);
/* forward decl, ported by T<N> later */
int32_t rc_o0i(void);
/* forward decl, ported by T<N> later */
int32_t rc_I0I(void);
/* forward decl, ported by T<N> later */
int32_t *rc_ioI(void *arg1, int32_t *arg2, int32_t arg3);
/* forward decl, ported by T<N> later */
int32_t rc_OiI(void *arg1);
/* forward decl, ported by T<N> later */
int32_t rc_l0I(void *arg1, int32_t arg2);
/* forward decl, ported by T<N> later */
extern int32_t rc_o1I(void *arg1, int32_t *arg2);
/* forward decl, ported by T<N> later */
int32_t rc_I0l(void *arg1);
/* forward decl, ported by T<N> later */
int32_t rc_Iol(void);
/* forward decl, ported by T<N> later */
extern int32_t rc_Ill(void);
/* forward decl, ported by T<N> later */
int32_t rc_oiI(void);
/* forward decl, ported by T<N> later */
extern int32_t rc_olI(void);
/* forward decl, ported by T<N> later */
int32_t rc_llI(void);
/* forward decl, ported by T<N> later */
int32_t rc_o0I(void);

int32_t AL_GopMngr_Init(void *arg1, void *arg2, int32_t arg3, char arg4)
{
    int32_t result;
    int32_t (*t9)(void *, void *);

    if (arg1 == NULL) {
        __assert("o",
                 "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_0.c",
                 0x76, "AL_GopMngr_Init", &_gp);
    }
    if (arg2 == NULL) {
        return AL_GopMngr_Deinit((void *)(intptr_t)__assert(
            "pAllocator",
            "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_0.c",
            0x7b, "AL_GopMngr_Init", &_gp));
    }

    *(int32_t *)((char *)arg1 + 0x44) = 0;
    if (AL_GetGopMngrType(arg3, (uint32_t)(uint8_t)arg4) == 0) {
        t9 = rc_O;
    } else {
        t9 = rc_i;
    }

    result = t9(arg1, arg2);
    if (result != 0) {
        void *v0_1 = Rtos_CreateMutex();

        *(void **)((char *)arg1 + 0x48) = v0_1;
        if (v0_1 != NULL) {
            return result;
        }
        {
            int32_t (*t9_1)(void *) = *(int32_t (**)(void *))((char *)arg1 + 0x3c);

            if (t9_1 != NULL) {
                t9_1(arg1);
            }
        }
    }
    return 0;
}

int32_t AL_GopMngr_Deinit(void *arg1)
{
    if (arg1 == NULL) {
        return rc_Il0((void *)(intptr_t)__assert(
            "o", "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_rate_ctrl/RateCtrl_0.c",
            0xc4, "AL_GopMngr_Deinit", &_gp));
    }
    {
        int32_t (*t9)(void) = *(int32_t (**)(void))((char *)arg1 + 0x3c);

        if (t9 != NULL) {
            t9();
        }
    }
    Rtos_DeleteMutex(*(void **)((char *)arg1 + 0x48));
    return 0;
}

/* stock name: O */
static int32_t rc_O(void *arg1, void *arg2)
{
    void *v0 = *(void **)arg2;
    int32_t (*t9)(void *, int32_t) = *(int32_t (**)(void *, int32_t))((char *)v0 + 4);

    *(void **)((char *)arg1 + 0x40) = arg2;
    if (t9 == NULL) {
        *(void **)((char *)arg1 + 0x44) = NULL;
        return 0;
    }

    {
        void *v0_1 = (void *)(intptr_t)t9(arg2, 0x70);

        *(void **)((char *)arg1 + 0x44) = v0_1;
        if (v0_1 != NULL) {
            AL_TAllocatorVtableCompat *vtable =
                *(AL_TAllocatorVtableCompat **)*(void **)((char *)arg1 + 0x40);
            void *v0_3 = vtable->GetVirtualAddr(*(void **)((char *)arg1 + 0x40), v0_1);

            *(void **)((char *)arg1 + 0x4c) = v0_3;
            *(int32_t *)((char *)v0_3 + 0x1c) = 1;
            *(intptr_t *)((char *)arg1 + 0x00) = (intptr_t)rc_i10;
            *(intptr_t *)((char *)arg1 + 0x04) = (intptr_t)rc_I10;
            *(intptr_t *)((char *)arg1 + 0x08) = (intptr_t)rc_OO1;
            *(intptr_t *)((char *)arg1 + 0x0c) = (intptr_t)rc_ol1;
            *(intptr_t *)((char *)arg1 + 0x10) = (intptr_t)rc_O11;
            *(intptr_t *)((char *)arg1 + 0x14) = (intptr_t)rc_iOOo;
            *(intptr_t *)((char *)arg1 + 0x18) = (intptr_t)rc_oO1;
            *(intptr_t *)((char *)arg1 + 0x1c) = (intptr_t)rc_IO1;
            *(intptr_t *)((char *)arg1 + 0x20) = 0;
            *(intptr_t *)((char *)arg1 + 0x24) = (intptr_t)rc_Oo1;
            *(intptr_t *)((char *)arg1 + 0x28) = (intptr_t)rc_OI1;
            *(intptr_t *)((char *)arg1 + 0x2c) = (intptr_t)rc_oI1;
            *(intptr_t *)((char *)arg1 + 0x30) = (intptr_t)rc_iI1;
            *(intptr_t *)((char *)arg1 + 0x34) = (intptr_t)rc_Ol1;
            *(intptr_t *)((char *)arg1 + 0x38) = (intptr_t)rc_lI1;
            *(intptr_t *)((char *)arg1 + 0x3c) = (intptr_t)rc_ioOo;
            return 1;
        }
    }
    return 0;
}

/* stock name: i */
static int32_t rc_i(void *arg1, void *arg2)
{
    void *v0 = *(void **)arg2;
    int32_t (*t9)(void *, int32_t) = *(int32_t (**)(void *, int32_t))((char *)v0 + 4);

    *(void **)((char *)arg1 + 0x40) = arg2;
    if (t9 == NULL) {
        *(void **)((char *)arg1 + 0x44) = NULL;
        return 0;
    }

    {
        void *v0_1 = (void *)(intptr_t)t9(arg2, 0xac);

        *(void **)((char *)arg1 + 0x44) = v0_1;
        if (v0_1 != NULL) {
            AL_TAllocatorVtableCompat *vtable =
                *(AL_TAllocatorVtableCompat **)*(void **)((char *)arg1 + 0x40);
            void *v0_3 = vtable->GetVirtualAddr(*(void **)((char *)arg1 + 0x40), v0_1);

            *(void **)((char *)arg1 + 0x4c) = v0_3;
            *(int32_t *)((char *)v0_3 + 0x1c) = 1;
            *(intptr_t *)((char *)arg1 + 0x00) = (intptr_t)rc_o0i;
            *(intptr_t *)((char *)arg1 + 0x04) = (intptr_t)rc_ioI;
            *(intptr_t *)((char *)arg1 + 0x08) = (intptr_t)rc_OiI;
            *(intptr_t *)((char *)arg1 + 0x0c) = (intptr_t)rc_o1I;
            *(intptr_t *)((char *)arg1 + 0x10) = (intptr_t)rc_Iol;
            *(intptr_t *)((char *)arg1 + 0x14) = (intptr_t)rc_Ill;
            *(intptr_t *)((char *)arg1 + 0x18) = (intptr_t)rc_oiI;
            *(intptr_t *)((char *)arg1 + 0x1c) = (intptr_t)rc_olI;
            *(intptr_t *)((char *)arg1 + 0x20) = 0;
            *(intptr_t *)((char *)arg1 + 0x24) = (intptr_t)rc_llI;
            *(intptr_t *)((char *)arg1 + 0x28) = (intptr_t)rc_l0I;
            *(intptr_t *)((char *)arg1 + 0x2c) = (intptr_t)rc_o0I;
            *(intptr_t *)((char *)arg1 + 0x30) = (intptr_t)rc_I0I;
            *(intptr_t *)((char *)arg1 + 0x34) = (intptr_t)rc_I0I;
            *(intptr_t *)((char *)arg1 + 0x3c) = (intptr_t)rc_I0l;
            return 1;
        }
    }
    return 0;
}

/* stock name: Il0 */
int32_t rc_Il0(void *arg1)
{
    int32_t a2_1 = *(int32_t *)((char *)arg1 + 0x48);

    *(int32_t *)((char *)arg1 + 0x28) = 0x7fffffff;
    if (a2_1 != 0) {
        *(int32_t *)((char *)arg1 + 0x28) = 0;
        if ((a2_1 & 1) == 0) {
            int32_t v1_1 = 1;
            int32_t a1_1;
            int32_t i;

            do {
                i = (1 << (v1_1 & 0x1f)) & a2_1;
                a1_1 = v1_1;
                v1_1 += 1;
            } while (i == 0);
            *(int32_t *)((char *)arg1 + 0x28) = a1_1;
        }
    }

    {
        int32_t a1_2 = *(int32_t *)((char *)arg1 + 8);
        int32_t v0_3 = (uint32_t)a1_2 < 0x7fffffffU ? 1 : 0;

        if (v0_3 == 0) {
            return v0_3;
        }
        {
            int32_t a3 = *(int32_t *)((char *)arg1 + 0x20);
            int32_t a2 = *(int32_t *)((char *)arg1 + 0x28);
            int32_t v0_5 =
                *(int32_t *)((char *)arg1 + 0x34) - *(int32_t *)((char *)arg1 + 0x30) + a1_2;

            if (a3 >= v0_5) {
                v0_5 = a3;
            }
            if (a2 < v0_5) {
                v0_5 = a2;
            }
            *(int32_t *)((char *)arg1 + 0x28) = v0_5;
            return v0_5;
        }
    }
}

/* stock name: i10 */
static void *rc_i10(void *arg1, int32_t arg2, int32_t arg3, int16_t arg4, int16_t arg5)
{
    char *result = *(char **)((char *)arg1 + 0x4c);

    *(int32_t *)(result + 0x40) = arg3;
    *(int32_t *)(result + 0x64) = arg2;
    *(int16_t *)(result + 0x5a) = arg4;
    *(int16_t *)(result + 0x5c) = arg5;
    return result;
}

/* stock name: oO1 */
static int32_t rc_oO1(void *arg1, int32_t *arg2)
{
    int32_t *ctx = *(int32_t **)((char *)arg1 + 0x4c);

    if (arg2[4] != 0) {
        return -1;
    }

    arg2[0] = ctx[0x0c];
    arg2[1] = 0x38;
    arg2[2] = (ctx[0x0c] - ctx[0x0d]) << 1;
    arg2[3] = ctx[0x0f];
    arg2[4] = 7;
    arg2[5] = 0;
    *((uint8_t *)arg2 + 0x24) = 0xff;
    return -1;
}

/* stock name: Oo1 */
static int32_t *rc_Oo1(void *arg1, int32_t arg2, void *arg3, void *arg4)
{
    int32_t *result = *(int32_t **)((char *)arg1 + 0x4c);

    (void)arg2;
    if (*result == 0x10 && (*(int32_t *)((char *)arg3 + 4) & 4) == 0 &&
        *(int32_t *)((char *)arg3 + 0x10) == 1) {
        int32_t denom = (*(int32_t *)((char *)arg4 + 0x2c) << 4) +
                        (*(int32_t *)((char *)arg4 + 0x30) << 6) +
                        *(int32_t *)((char *)arg4 + 0x24) +
                        (*(int32_t *)((char *)arg4 + 0x28) << 2);

        if (denom != 0) {
            uint32_t load = (uint32_t)(((uint64_t)(uint32_t)*(int32_t *)((char *)arg4 + 0x1c) *
                                        100ULL) /
                                       (uint32_t)denom);
            uint8_t value = *((uint8_t *)result + 0x51);

            if ((int32_t)load >= 0x24) {
                if (value != 0) {
                    *((uint8_t *)result + 0x51) = (uint8_t)(value - 1);
                }
            } else if ((int32_t)load < 0x0a && value < *((uint8_t *)result + 0x52)) {
                *((uint8_t *)result + 0x51) = (uint8_t)(value + 1);
            }
        }
    }

    return result;
}

/* stock name: OI1 */
void *rc_OI1(void *arg1, int32_t arg2)
{
    char *result = *(char **)((char *)arg1 + 0x4c);
    int32_t qp_count = *(int32_t *)(result + 0x24);
    int32_t mask = *(int32_t *)(result + 0x48);
    uint32_t qp = *((uint8_t *)result + 0x51);

    if (*(int32_t *)(result + 0x28) >= arg2) {
        *(int32_t *)(result + 0x28) = arg2;
    }

    if (arg2 >= qp_count) {
        *(int32_t *)(result + 0x24) = arg2;
        qp_count = arg2;
    } else if (mask == 0 && qp_count + *(int32_t *)(result + 0x44) >= arg2) {
        *(int32_t *)(result + 0x24) = arg2;
        qp_count = arg2;
    }

    if ((int32_t)qp >= qp_count) {
        qp = qp_count <= 0 ? 0U : (uint32_t)(qp_count - 1);
        *((uint8_t *)result + 0x50) = (uint8_t)qp;
    }

    *(int32_t *)(result + 0x48) |= 1 << (arg2 & 0x1f);
    return result;
}

/* stock name: iI1 */
static int32_t rc_iI1(void *arg1)
{
    char *result = *(char **)((char *)arg1 + 0x4c);

    if (*(int32_t *)(result + 0x0c) != 0) {
        *(uint8_t *)(result + 0x54) = 1;
    }
    return (int32_t)(intptr_t)result;
}

/* stock name: lI1 */
static void *rc_lI1(void *arg1)
{
    char *result = *(char **)((char *)arg1 + 0x4c);

    *(uint8_t *)(result + 0x57) = 1;
    return result;
}

/* stock name: Ol1 */
static int32_t rc_Ol1(void *arg1)
{
    char *result = *(char **)((char *)arg1 + 0x4c);

    if (*(int32_t *)(result + 0x0c) != 0) {
        *(uint8_t *)(result + 0x55) = 1;
    }
    return (int32_t)(intptr_t)result;
}

/* stock name: ioOo */
static int32_t rc_ioOo(void *arg1)
{
    void *allocator = *(void **)((char *)arg1 + 0x40);
    AL_TAllocatorVtableCompat *vtable = *(AL_TAllocatorVtableCompat **)allocator;

    vtable->Free(allocator, *(void **)((char *)arg1 + 0x44));
    return 0;
}
