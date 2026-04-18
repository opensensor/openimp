#include <stdint.h>
#include <string.h>

#include "imp/imp_ivs_move.h"

int32_t IMP_Log_Get_Option(void); /* forward decl, ported by T<N> later */
void imp_log_fun(int32_t level, int32_t option, int32_t type, ...); /* forward decl, ported by T<N> later */
int32_t MinsOp(int32_t arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t MaxsOp(int32_t arg1, int32_t arg2); /* forward decl, ported by T<N> later */
int32_t Proceed(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, void *arg5, int32_t arg6, void *arg7,
    int32_t arg8); /* forward decl, ported by T<N> later */
extern char _gp; /* MIPS GOT base symbol */

static int32_t sub_de374(int32_t arg1, void *arg2, int32_t *arg3, int32_t arg4, int32_t arg5, int32_t arg6,
    int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11, int32_t arg12)
{
    int32_t a0 = arg1 << 2;
    int32_t *a2 = (int32_t *)((uint8_t *)(intptr_t)arg3[8] + a0);
    int32_t *a1_1 = (int32_t *)((uint8_t *)(intptr_t)*arg3 + a0);
    int32_t *a0_1 = (int32_t *)((uint8_t *)arg2 + a0);

    (void)arg4;
    (void)arg5;
    (void)arg6;
    (void)arg7;
    (void)arg8;
    (void)arg9;
    (void)arg10;
    (void)arg11;
    (void)arg12;

    do {
        int32_t v1 = *a2;

        arg1 += 1;
        a2 = &a2[1];
        *a0_1 = *a1_1 < v1 ? 1 : 0;
        a1_1 = &a1_1[1];
        a0_1 = &a0_1[1];
    } while (arg1 < arg3[6]);

    return 0;
}

int32_t move_detect(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7,
    int32_t arg8, void *arg9, int32_t *arg10, int32_t arg11)
{
    int32_t arg_4 = arg2;
    int32_t arg_8 = arg3;
    int32_t a3 = 0;
    int32_t arg_c = a3;
    int32_t s2;
    int32_t s5;
    int32_t s7;
    int32_t v0_2;
    void *str;
    int32_t n;
    int32_t a0_6;
    int32_t a0_7;

    (void)arg_4;
    (void)arg_8;
    (void)arg_c;

    if (arg10 == NULL) {
        imp_log_fun(6, IMP_Log_Get_Option(), 2, "IVSMOVEFILTER",
            "/home/user/git/proj/sdk-lv3/src/imp/ivs/ivs_move/src/move_detect.c", 0x121,
            "move_detect", "filterEngine is NULL\n", &_gp);
        return -1;
    }

    s2 = arg10[0x12];
    arg10[0x1c] = 0;
    arg10[0x1d] = 0;
    s5 = arg10[0x11];
    s7 = arg10[0x14];
    arg10[0x19] = MaxsOp(s2 - 1, 0);
    v0_2 = MinsOp(s2 + s7 + 1, arg4);
    arg10[0x1a] = MaxsOp(s2 - 1, 0);
    str = (void *)(intptr_t)arg10[8];
    n = arg10[6] << 2;
    arg10[0x1b] = v0_2;
    memset(str, 0, (size_t)n);
    arg10[7] = 0;
    arg10[0x1c] = 0;
    arg10[0x1d] = 0;
    Proceed(arg1 + arg3 * s2 + s5, arg3, arg5 + arg6 * s2 + s5, arg6, (void *)(intptr_t)(arg7 + arg8 * s2 + s5),
        arg8, arg10, arg11);
    a0_6 = arg10[6];

    if (arg11 != 0) {
        if (a0_6 >= 4) {
            arg10[8];
        }
    }

    a0_7 = 0 < a0_6 ? 1 : 0;
    if (a0_7 == 0) {
        return 0;
    }

    return sub_de374(0, arg9, arg10, a0_7, 0, 0, 0, 0, 0, 0, 0, 0);
}
