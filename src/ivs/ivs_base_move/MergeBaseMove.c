#include <assert.h>
#include <stdint.h>

int32_t sub_e0e68(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    void *arg6, void *arg7, int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11,
    int32_t arg12, int32_t arg13);
int32_t sub_e17dc(int32_t arg1, int32_t arg2, void *arg3, int32_t arg4, void *arg5, int32_t arg6,
    int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11, int32_t arg12,
    int32_t arg13, int32_t arg14, int32_t arg15, int32_t arg16, int32_t arg17, int32_t arg18,
    void *arg19, int32_t arg20, int32_t arg21, int32_t arg22, int32_t arg23, int32_t arg24,
    int32_t arg25, int32_t arg26, int32_t arg27, int32_t arg28, int32_t arg29, int32_t arg30,
    int32_t arg31, int32_t arg32, int32_t arg33, int32_t arg34, int32_t arg35, int32_t arg36,
    int32_t arg37, int32_t arg38, int32_t arg39, int32_t arg40, int32_t arg41);
int32_t sub_e232c(int32_t arg1, int32_t arg2);
int32_t sub_e26b4(int32_t arg1, int32_t arg2, void *arg3, void *arg4, int32_t arg5, int32_t arg6,
    int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11);
int32_t sub_e2818(int32_t arg1, int32_t arg2);

static inline uint8_t sub_e0ca0(void)
{
    return 0;
}

static inline uint8_t sub_e_absdiff_u8(const uint8_t *a, const uint8_t *b, int32_t thresh)
{
    int32_t diff = (int32_t)*a - (int32_t)*b;
    if (diff < 0) {
        diff = -diff;
    }
    if (diff < thresh) {
        return 0;
    }
    return (uint8_t)diff;
}

static void sub_e_generic_merge(const uint8_t *src0, int32_t src0_stride, const uint8_t *src1,
    int32_t src1_stride, uint8_t *dst, int32_t dst_stride, int32_t width, int32_t height,
    int32_t thresh)
{
    int32_t y = 0;

    while (y < height) {
        int32_t x = 0;
        uint8_t *dst_row = dst + y * dst_stride;

        while (x < width) {
            int32_t yy = y - 1;
            uint8_t best = 0xff;

            while (yy <= y + 1) {
                int32_t cy = yy;
                int32_t xx = x - 1;

                if (cy < 0) {
                    cy = 0;
                } else if (cy >= height) {
                    cy = height - 1;
                }

                while (xx <= x + 1) {
                    int32_t cx = xx;
                    uint8_t v;

                    if (cx < 0) {
                        cx = 0;
                    } else if (cx >= width) {
                        cx = width - 1;
                    }

                    v = sub_e_absdiff_u8(src1 + cy * src1_stride + cx, src0 + cy * src0_stride + cx,
                        thresh);
                    if (best < v) {
                        v = best;
                    }
                    best = v;
                    xx += 1;
                }

                yy += 1;
            }

            dst_row[x] = best;
            x += 1;
        }

        y += 1;
    }
}

int32_t sub_e0b9c(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, void *arg7, void *arg8, void *arg9, int32_t arg10, int32_t arg11,
    int32_t arg12, int32_t arg13, int32_t arg14)
{
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg14;
    sub_e_generic_merge((const uint8_t *)arg8, arg11, (const uint8_t *)arg9, arg5,
        (uint8_t *)arg7, arg12, arg10, arg13, arg6);
    return sub_e0ca0();
}

int32_t sub_e0e5c(int32_t arg1, int32_t arg2, int32_t arg3)
{
    return (arg2 == arg3) ? 0 : sub_e0e68(arg1, arg3, (arg2 < arg3) ? 1 : 0, 0, 0, 0, 0, arg2,
               0, 0, 0, 0, 0);
}

int32_t sub_e0ef0(int32_t arg1, int32_t arg2, char *arg3, char *arg4, int32_t arg5, char *arg6,
    char *arg7, int32_t arg8, void *arg9, void *arg10, int32_t arg11, int32_t arg12,
    int32_t arg13, int32_t arg14, int32_t arg15, int32_t arg16, int32_t arg17, int32_t arg18,
    int32_t arg19)
{
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    (void)arg6;
    (void)arg7;
    (void)arg9;
    (void)arg10;
    sub_e_generic_merge((const uint8_t *)arg4 - (arg14 * arg17 + arg2),
        arg17, (const uint8_t *)arg6 - (arg14 * arg16 + arg2),
        arg16, (uint8_t *)(intptr_t)(arg8 - arg2), arg13, arg11, arg19, arg12);
    return 0;
}

int32_t sub_e0e68(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    void *arg6, void *arg7, int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11,
    int32_t arg12, int32_t arg13)
{
    (void)arg1;
    if (arg3 == 0) {
        uint8_t a = sub_e_absdiff_u8((const uint8_t *)arg7 + arg2 - 1, (const uint8_t *)arg6 + arg2 - 1,
            arg4);
        uint8_t b = sub_e_absdiff_u8((const uint8_t *)arg7 + arg2 - 2, (const uint8_t *)arg6 + arg2 - 2,
            arg4);
        if (a < b) {
            a = b;
        }
        return sub_e0ef0((a < b) ? 1 : 0, arg2, (char *)arg6 + arg10 + arg2 - 2,
            (char *)arg6 + arg2, a, (char *)arg7 + arg2, (char *)arg7 + arg9 + arg2 - 2,
            arg5 + arg2, 0, 0, arg8 + 1, arg4, arg5, arg8, arg9, arg10, arg11, arg12, arg13);
    }

    if (arg8 == arg2) {
        return 0;
    }

    return sub_e0ef0(0, arg2, (char *)arg6 + arg10 + arg2 - 2, (char *)arg6 + arg2, 0,
        (char *)arg7 + arg2, (char *)arg7 + arg9 + arg2 - 2, arg5 + arg2, 0, 0, arg8 + 1, arg4,
        arg5, arg8, arg9, arg10, arg11, arg12, arg13);
}

int32_t sub_e17b0(int32_t arg1, int32_t arg2, int32_t arg3)
{
    int32_t v1_1 = (((arg3 - arg1) >> 4) << 4) + arg1 + 0x10;
    return (arg2 == v1_1) ? 0 : sub_e17dc((arg2 < v1_1) ? 1 : 0, v1_1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
               0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
               0, 0, 0);
}

int32_t sub_e19f8(void *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, int32_t arg7, void *arg8, int32_t arg9, void *arg10, int32_t arg11,
    int32_t arg12, int32_t arg13, int32_t arg14, int32_t arg15, int32_t arg16, char *arg17,
    char *arg18, char *arg19, char *arg20, char *arg21, char *arg22, char *arg23, char *arg24,
    char *arg25, char *arg26, char *arg27, char *arg28, int32_t arg29, int32_t arg30,
    int32_t arg31, int32_t arg32, int32_t arg33, int32_t arg34, int32_t arg35, int32_t arg36,
    int32_t arg37, int32_t arg38, int32_t arg39, void *arg40, void *arg41, void *arg42,
    void *arg43, void *arg44, void *arg45, void *arg46, void *arg47, void *arg48, void *arg49,
    void *arg50, void *arg51, void *arg52, void *arg53, void *arg54, void *arg55, int32_t arg56,
    int32_t arg57, int32_t arg58, int32_t arg59, int32_t arg60, int32_t arg61, int32_t arg62,
    int32_t arg63, int32_t arg64, int32_t arg65, int32_t arg66, int32_t arg67, int32_t arg68,
    int32_t arg69, int32_t arg70, int32_t arg71, int32_t arg72, int32_t arg73, int32_t arg74,
    int32_t arg75, int32_t arg76, int32_t arg77, int32_t arg78, int32_t arg79)
{
    (void)arg3; (void)arg4; (void)arg5; (void)arg6; (void)arg7; (void)arg11; (void)arg12;
    (void)arg13; (void)arg14; (void)arg15; (void)arg16; (void)arg17; (void)arg18; (void)arg19;
    (void)arg20; (void)arg21; (void)arg22; (void)arg23; (void)arg24; (void)arg25; (void)arg26;
    (void)arg27; (void)arg28; (void)arg29; (void)arg30; (void)arg31; (void)arg32; (void)arg33;
    (void)arg34; (void)arg35; (void)arg36; (void)arg37; (void)arg38; (void)arg39; (void)arg40;
    (void)arg42; (void)arg43; (void)arg44; (void)arg45; (void)arg46; (void)arg47; (void)arg48;
    (void)arg49; (void)arg50; (void)arg51; (void)arg52; (void)arg53; (void)arg54; (void)arg55;
    (void)arg57; (void)arg58; (void)arg59; (void)arg60; (void)arg61; (void)arg62; (void)arg63;
    (void)arg64; (void)arg65; (void)arg66; (void)arg67; (void)arg68; (void)arg69; (void)arg70;
    (void)arg71; (void)arg72; (void)arg73; (void)arg74; (void)arg75; (void)arg76; (void)arg77;
    (void)arg78; (void)arg79;
    sub_e_generic_merge((const uint8_t *)arg8, arg76, (const uint8_t *)arg10, arg77,
        (uint8_t *)arg41, arg75, arg56 - arg2, 1, arg9);
    return 0;
}

int32_t sub_e17dc(int32_t arg1, int32_t arg2, void *arg3, int32_t arg4, void *arg5, int32_t arg6,
    int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11, int32_t arg12,
    int32_t arg13, int32_t arg14, int32_t arg15, int32_t arg16, int32_t arg17, int32_t arg18,
    void *arg19, int32_t arg20, int32_t arg21, int32_t arg22, int32_t arg23, int32_t arg24,
    int32_t arg25, int32_t arg26, int32_t arg27, int32_t arg28, int32_t arg29, int32_t arg30,
    int32_t arg31, int32_t arg32, int32_t arg33, int32_t arg34, int32_t arg35, int32_t arg36,
    int32_t arg37, int32_t arg38, int32_t arg39, int32_t arg40, int32_t arg41)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6; (void)arg7; (void)arg8;
    (void)arg9; (void)arg10; (void)arg11; (void)arg12; (void)arg13; (void)arg14; (void)arg15;
    (void)arg16; (void)arg17; (void)arg18; (void)arg19; (void)arg20; (void)arg21; (void)arg22;
    (void)arg23; (void)arg24; (void)arg25; (void)arg26; (void)arg27; (void)arg28; (void)arg29;
    (void)arg30; (void)arg31; (void)arg32; (void)arg33; (void)arg34; (void)arg35; (void)arg36;
    (void)arg37; (void)arg38; (void)arg39; (void)arg40; (void)arg41;
    return 0;
}

int32_t sub_e1618(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11,
    int32_t arg12, int32_t arg13, int32_t arg14, int32_t arg15, int32_t arg16, int32_t arg17,
    int32_t arg18)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6; (void)arg7;
    (void)arg8; (void)arg9; (void)arg10; (void)arg11;
    if (arg18 < arg12) {
        return 0;
    }
    if (arg13 == arg18) {
        return 0;
    }
    return sub_e17dc((arg13 < arg18) ? 1 : 0, arg18, (void *)(intptr_t)arg14, arg15,
        (void *)(intptr_t)arg16, arg17, 0, 0, 0, 0, 0, 0, arg12, arg13, arg14, arg15, arg16, arg18,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

int32_t sub_e131c(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5;
    if (arg10 != 0) {
        return sub_e1618(arg1, arg2, arg3, arg4, arg5, arg6, arg7, arg8, arg9, arg10, 0, 0, 0, 0, 0,
            0, 0, 0);
    }
    return 0;
}

int32_t sub_e2290(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, int32_t arg7)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6;
    if (arg7 != 0) {
        return sub_e232c(arg1, arg2);
    }
    return 0;
}

int32_t sub_e232c(int32_t arg1, int32_t arg2)
{
    return (arg1 == arg2) ? 0 : 0;
}

int32_t sub_e2344(int32_t arg1, int32_t arg2, int32_t arg3, void *arg4, void *arg5, int32_t arg6,
    void *arg7, int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11, int32_t arg12,
    int32_t arg13, int32_t arg14, int32_t arg15, int32_t arg16, int32_t arg17, int32_t arg18,
    int32_t arg19, int32_t arg20)
{
    int32_t width = arg8 - arg2;

    sub_e_generic_merge((const uint8_t *)arg4 + arg11, arg18, (const uint8_t *)arg5 + arg10, arg17,
        (uint8_t *)arg7 + arg6, arg19, width, 1 + (arg20 - arg15), arg3);
    return (arg15 != arg20) ? sub_e26b4(arg20, arg3, arg4, arg5, arg6, (int32_t)(intptr_t)arg7, arg8, arg9, arg10,
               arg11, 0) :
           0;
}

int32_t sub_e26b4(int32_t arg1, int32_t arg2, void *arg3, void *arg4, int32_t arg5, int32_t arg6,
    int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11)
{
    (void)arg2; (void)arg3; (void)arg4; (void)arg5; (void)arg6; (void)arg7; (void)arg8; (void)arg10;
    (void)arg11;
    if (arg9 == arg1) {
        assert(!"j==height-1");
    }
    return 0;
}

int32_t sub_e2750(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, int32_t arg7, int32_t arg8)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg6; (void)arg7; (void)arg8;
    if (arg5 != 0) {
        return sub_e2818(arg1, arg2);
    }
    return 0;
}

int32_t sub_e2818(int32_t arg1, int32_t arg2)
{
    return (arg2 + 0x30 >= arg1) ? 0 : 0;
}

int32_t sub_e2894(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, int32_t arg7)
{
    (void)arg1; (void)arg2; (void)arg3; (void)arg4; (void)arg6;
    if (arg5 != 0) {
        return 0;
    }
    return (arg7 < (arg2 + arg6 - 0x10)) ? 1 : 0;
}

int32_t sub_e28c8(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, void *arg7, void *arg8, int32_t arg9, void *arg10, void *arg11, void *arg12,
    void *arg13, int32_t arg14, int32_t arg15)
{
    (void)arg1; (void)arg3; (void)arg5; (void)arg7; (void)arg8; (void)arg13; (void)arg15;
    sub_e_generic_merge((const uint8_t *)arg11 + arg1, arg14, (const uint8_t *)arg12 + arg2, arg14,
        (uint8_t *)arg10 + arg5, arg14, (arg14 + 1) - arg6, 1, arg9);
    return sub_e0ca0();
}

void MergeBaseMove(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5,
    int32_t arg6, int32_t arg7, int32_t arg8, int32_t arg9, int32_t arg10, int32_t arg11,
    int32_t arg12, int32_t arg13, int32_t arg14, int32_t arg15, int32_t arg16, int32_t arg17)
{
    if (arg17 != 0) {
        sub_e131c(arg1, arg6, arg11, arg16, arg7, arg12, arg8, arg13, arg9, arg17);
        return;
    }

    sub_e0b9c(arg1, arg2, arg3, arg4, arg5, arg16, (void *)(intptr_t)arg11, (void *)(intptr_t)arg1,
        (void *)(intptr_t)arg6, arg9, arg2, arg12, arg10, 0);

    (void)arg14;
    (void)arg15;
}
