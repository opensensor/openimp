#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* forward decl, ported by T<N> later */
void _setLeftPart32(uint32_t leftpart);
/* forward decl, ported by T<N> later */
uint32_t _setRightPart32(uint32_t rightpart);

static inline uint8_t *i32_to_u8_ptr(int32_t value)
{
    return (uint8_t *)(uintptr_t)(uint32_t)value;
}

static inline uint32_t *i32_to_u32_ptr(int32_t value)
{
    return (uint32_t *)(uintptr_t)(uint32_t)value;
}

int32_t c_copy_frame_t420_to_t420(void **arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                                  int32_t arg5, int32_t arg6)
{
    int32_t s4 = arg6 + 0xf;
    int32_t result;
    int32_t s4_1;
    int32_t result_1;

    if (arg6 >= 0) {
        s4 = arg6;
    }

    result = *arg2 << 4;
    s4_1 = s4 >> 4;
    result_1 = result;

    if (s4_1 > 0) {
        int32_t s7_1 = arg2[1] << 3;
        int32_t s2_1 = 0;
        int32_t s1_1 = 0;
        int32_t s0_1 = 0;

        do {
            void *a0_1 = (uint8_t *)arg1[0] + s1_1;

            s1_1 += result_1;
            memcpy(a0_1, i32_to_u8_ptr(arg3[0]) + (s0_1 * arg4[0]), (size_t)(arg5 << 4));

            {
                void *v1_2 = (void *)(uintptr_t)((size_t)(s0_1 * arg4[1]));
                void *a0_3 = (uint8_t *)arg1[1] + s2_1;

                s0_1 += 1;
                s2_1 += s7_1;
                result = (int32_t)(uintptr_t)memcpy(a0_3, i32_to_u8_ptr(arg3[1]) + (uintptr_t)v1_2,
                                                    (size_t)(arg5 << 3));
            }
        } while (s0_1 != s4_1);
    }

    return result;
}

int32_t c_copy_frame_nv21_to_t420(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                                  int32_t arg5, int32_t arg6)
{
    int32_t t5 = arg6 + 0xf;
    int32_t result = arg6 < 0 ? 1 : 0;
    int32_t t5_1;
    uint32_t *s2;
    uint32_t *i_2;

    if (result == 0) {
        t5 = arg6;
    }

    t5_1 = t5 >> 4;
    s2 = i32_to_u32_ptr(arg3[0]);
    i_2 = i32_to_u32_ptr(arg1[0]);

    if (t5_1 > 0) {
        int32_t t4_1 = arg5 + 0xf;
        int32_t t6_1;
        int32_t t4_2;
        int32_t t9_1;
        uint8_t *i_5;

        if (arg5 >= 0) {
            t4_1 = arg5;
        }

        (void)arg2[0];
        t6_1 = arg2[1] << 3;
        t4_2 = t4_1 >> 4;
        t9_1 = 0;

        while (1) {
            uint32_t *t8_1 = s2;

            if (t4_2 <= 0) {
                t9_1 += 1;
                i_2 = &i_2[arg2[0] * 4];
                s2 = &s2[arg4[0] * 4];
                if (t5_1 == t9_1) {
                    i_5 = i32_to_u8_ptr(arg3[1]);
                    break;
                }
            } else {
                uint32_t *i = i_2;
                int32_t t7_1 = 0;
                int32_t a1_2 = 0;

                do {
                    uint32_t *v1_2 = t8_1;

                    do {
                        _setLeftPart32(v1_2[0]);
                        _setLeftPart32(v1_2[1]);
                        _setLeftPart32(v1_2[2]);
                        _setLeftPart32(v1_2[3]);

                        {
                            uint32_t t2_2 = _setRightPart32(v1_2[0]);
                            uint32_t t1_2 = _setRightPart32(v1_2[1]);
                            uint32_t a1_1 = _setRightPart32(v1_2[3]);
                            uint32_t t0_2 = _setRightPart32(v1_2[2]);

                            i[0] = t2_2;
                            i[1] = t1_2;
                            i[2] = t0_2;
                            i[3] = a1_1;
                            a1_2 = arg4[0];
                            i = &i[4];
                            v1_2 += a1_2;
                        }
                    } while (i != &i[0x40]);

                    t7_1 += 1;
                    t8_1 = &t8_1[4];
                } while (t4_2 != t7_1);

                t9_1 += 1;
                i_2 = &i_2[arg2[0] * 4];
                s2 = &s2[a1_2 * 4];
                if (t5_1 == t9_1) {
                    i_5 = i32_to_u8_ptr(arg3[1]);
                    break;
                }
            }
        }

        {
            uint8_t *t8_2 = i32_to_u8_ptr(arg1[1]);
            int32_t t7_2 = 0;

            while (1) {
                uint8_t *a2 = t8_2;

                if (t4_2 <= 0) {
                    t7_2 += 1;
                    t8_2 += t6_1;
                    result = arg4[1] << 3;
                    i_5 = &i_5[result];
                    if (t5_1 == t7_2) {
                        break;
                    }
                } else {
                    uint8_t *i_4 = i_5;
                    int32_t t2_3 = 0;
                    int32_t v0_1 = 0;

                    do {
                        uint8_t *t1_3 = a2 + 0x80;
                        uint8_t *i_3 = i_4;

                        do {
                            uint8_t *v1_3 = a2;
                            uint8_t *i_1 = i_3;

                            do {
                                uint8_t a0_1 = *i_1;

                                v1_3 += 1;
                                i_1 = &i_1[2];
                                *(v1_3 + 7) = a0_1;
                                *(v1_3 - 1) = *(i_1 - 1);
                            } while (i_1 != &i_3[0x10]);

                            v0_1 = arg4[1];
                            a2 += 0x10;
                            i_3 = &i_3[v0_1];
                        } while (a2 != t1_3);

                        t2_3 += 1;
                        i_4 = &i_4[0x10];
                    } while (t4_2 != t2_3);

                    result = v0_1 << 3;
                    t7_2 += 1;
                    i_5 = &i_5[result];
                    t8_2 += t6_1;
                    if (t5_1 == t7_2) {
                        break;
                    }
                }
            }
        }
    }

    return result;
}

int32_t c_copy_frame_i420_to_t420(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                                  int32_t arg5, int32_t arg6)
{
    int32_t t4 = arg6 + 0xf;
    int32_t result = arg6 < 0 ? 1 : 0;
    int32_t t4_1;
    uint32_t *s2;
    uint32_t *i_3;

    if (result == 0) {
        t4 = arg6;
    }

    t4_1 = t4 >> 4;
    s2 = i32_to_u32_ptr(arg3[0]);
    i_3 = i32_to_u32_ptr(arg1[0]);

    if (t4_1 > 0) {
        int32_t t3_1 = arg5 + 0xf;
        int32_t t5_1;
        int32_t t3_2;
        int32_t t9_1;
        uint8_t *i_5;

        if (arg5 >= 0) {
            t3_1 = arg5;
        }

        (void)arg2[0];
        t5_1 = arg2[1] << 3;
        t3_2 = t3_1 >> 4;
        t9_1 = 0;

        while (1) {
            uint32_t *t8_1 = s2;

            if (t3_2 <= 0) {
                t9_1 += 1;
                i_3 = &i_3[arg2[0] * 4];
                s2 = &s2[arg4[0] * 4];
                if (t4_1 == t9_1) {
                    i_5 = i32_to_u8_ptr(arg3[1]);
                    break;
                }
            } else {
                uint32_t *i = i_3;
                int32_t t7_1 = 0;
                int32_t a1_2 = 0;

                do {
                    uint32_t *v1_2 = t8_1;

                    do {
                        _setLeftPart32(v1_2[0]);
                        _setLeftPart32(v1_2[1]);
                        _setLeftPart32(v1_2[2]);
                        _setLeftPart32(v1_2[3]);

                        {
                            uint32_t t2_2 = _setRightPart32(v1_2[0]);
                            uint32_t t1_2 = _setRightPart32(v1_2[1]);
                            uint32_t a1_1 = _setRightPart32(v1_2[3]);
                            uint32_t t0_2 = _setRightPart32(v1_2[2]);

                            i[0] = t2_2;
                            i[1] = t1_2;
                            i[2] = t0_2;
                            i[3] = a1_1;
                            a1_2 = arg4[0];
                            i = &i[4];
                            v1_2 += a1_2;
                        }
                    } while (i != &i[0x40]);

                    t7_1 += 1;
                    t8_1 = &t8_1[4];
                } while (t7_1 != t3_2);

                t9_1 += 1;
                i_3 = &i_3[arg2[0] * 4];
                s2 = &s2[a1_2 * 4];
                if (t4_1 == t9_1) {
                    i_5 = i32_to_u8_ptr(arg3[1]);
                    break;
                }
            }
        }

        {
            uint8_t *s2_1 = i32_to_u8_ptr(arg3[2]);
            uint8_t *s1 = i32_to_u8_ptr(arg1[1]);
            int32_t s0_2 = 0;

            while (1) {
                uint8_t *t0_3 = s1;

                if (t3_2 <= 0) {
                    s0_2 += 1;
                    result = arg4[2] << 3;
                    i_5 += arg4[1] << 3;
                    s2_1 += result;
                    s1 += t5_1;
                    if (t4_1 == s0_2) {
                        break;
                    }
                } else {
                    uint8_t *t9_2 = s2_1;
                    uint8_t *i_4 = i_5;
                    int32_t t7_2 = 0;
                    int32_t v0_1 = 0;
                    int32_t v1_4 = 0;

                    do {
                        uint8_t *t6_2 = t0_3 + 0x80;
                        uint8_t *t2_3 = t9_2;
                        uint8_t *i_2 = i_4;

                        do {
                            uint8_t *v1_3 = t0_3;
                            uint8_t *a0 = t2_3;
                            uint8_t *i_1 = i_2;

                            do {
                                i_1 += 1;
                                v1_3 += 1;
                                a0 += 1;
                                *(v1_3 - 1) = *(i_1 - 1);
                                *(v1_3 + 7) = *(a0 - 1);
                            } while (i_1 != i_2 + 8);

                            v1_4 = arg4[1];
                            v0_1 = arg4[2];
                            t0_3 += 0x10;
                            i_2 += v1_4;
                            t2_3 += v0_1;
                        } while (t6_2 != t0_3);

                        t7_2 += 1;
                        i_4 += 8;
                        t9_2 += 8;
                    } while (t3_2 != t7_2);

                    result = v0_1 << 3;
                    s0_2 += 1;
                    i_5 += v1_4 << 3;
                    s2_1 += result;
                    s1 += t5_1;
                    if (t4_1 == s0_2) {
                        break;
                    }
                }
            }
        }
    }

    return result;
}

int32_t c_copy_frame_nv12_to_t420(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                                  int32_t arg5, int32_t arg6)
{
    int32_t t5 = arg6 + 0xf;
    int32_t result = arg6 < 0 ? 1 : 0;
    int32_t t5_1;
    uint32_t *s2;
    uint32_t *i_2;

    if (result == 0) {
        t5 = arg6;
    }

    t5_1 = t5 >> 4;
    s2 = i32_to_u32_ptr(arg3[0]);
    i_2 = i32_to_u32_ptr(arg1[0]);

    if (t5_1 > 0) {
        int32_t t4_1 = arg5 + 0xf;
        int32_t t6_1;
        int32_t t4_2;
        int32_t t9_1;
        uint8_t *i_5;

        if (arg5 >= 0) {
            t4_1 = arg5;
        }

        (void)arg2[0];
        t6_1 = arg2[1] << 3;
        t4_2 = t4_1 >> 4;
        t9_1 = 0;

        while (1) {
            uint32_t *t8_1 = s2;

            if (t4_2 <= 0) {
                t9_1 += 1;
                i_2 = &i_2[arg2[0] * 4];
                s2 = &s2[arg4[0] * 4];
                if (t5_1 == t9_1) {
                    i_5 = i32_to_u8_ptr(arg3[1]);
                    break;
                }
            } else {
                uint32_t *i = i_2;
                int32_t t7_1 = 0;
                int32_t a1_2 = 0;

                do {
                    uint32_t *v1_2 = t8_1;

                    do {
                        _setLeftPart32(v1_2[0]);
                        _setLeftPart32(v1_2[1]);
                        _setLeftPart32(v1_2[2]);
                        _setLeftPart32(v1_2[3]);

                        {
                            uint32_t t2_2 = _setRightPart32(v1_2[0]);
                            uint32_t t1_2 = _setRightPart32(v1_2[1]);
                            uint32_t a1_1 = _setRightPart32(v1_2[3]);
                            uint32_t t0_2 = _setRightPart32(v1_2[2]);

                            i[0] = t2_2;
                            i[1] = t1_2;
                            i[2] = t0_2;
                            i[3] = a1_1;
                            a1_2 = arg4[0];
                            i = &i[4];
                            v1_2 += a1_2;
                        }
                    } while (i != &i[0x40]);

                    t7_1 += 1;
                    t8_1 = &t8_1[4];
                } while (t4_2 != t7_1);

                t9_1 += 1;
                i_2 = &i_2[arg2[0] * 4];
                s2 = &s2[a1_2 * 4];
                if (t5_1 == t9_1) {
                    i_5 = i32_to_u8_ptr(arg3[1]);
                    break;
                }
            }
        }

        {
            uint8_t *t8_2 = i32_to_u8_ptr(arg1[1]);
            int32_t t7_2 = 0;

            while (1) {
                uint8_t *a2 = t8_2;

                if (t4_2 <= 0) {
                    t7_2 += 1;
                    t8_2 += t6_1;
                    result = arg4[1] << 3;
                    i_5 = &i_5[result];
                    if (t5_1 == t7_2) {
                        break;
                    }
                } else {
                    uint8_t *i_4 = i_5;
                    int32_t t2_3 = 0;
                    int32_t v0_1 = 0;

                    do {
                        uint8_t *t1_3 = a2 + 0x80;
                        uint8_t *i_3 = i_4;

                        do {
                            uint8_t *v1_3 = a2;
                            uint8_t *i_1 = i_3;

                            do {
                                uint8_t a0_1 = *i_1;

                                v1_3 += 1;
                                i_1 = &i_1[2];
                                *(v1_3 - 1) = a0_1;
                                *(v1_3 + 7) = *(i_1 - 1);
                            } while (i_1 != &i_3[0x10]);

                            v0_1 = arg4[1];
                            a2 += 0x10;
                            i_3 = &i_3[v0_1];
                        } while (a2 != t1_3);

                        t2_3 += 1;
                        i_4 = &i_4[0x10];
                    } while (t4_2 != t2_3);

                    result = v0_1 << 3;
                    t7_2 += 1;
                    i_5 = &i_5[result];
                    t8_2 += t6_1;
                    if (t5_1 == t7_2) {
                        break;
                    }
                }
            }
        }
    }

    return result;
}

int32_t c_copy_frame_t420_to_nv12(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                                  int32_t arg5, int32_t arg6)
{
    int32_t t5 = arg6 + 0xf;
    int32_t result = arg6 < 0 ? 1 : 0;
    int32_t t5_1;
    uint32_t *i_3;
    uint32_t *s0;

    if (result == 0) {
        t5 = arg6;
    }

    t5_1 = t5 >> 4;
    i_3 = i32_to_u32_ptr(arg3[0]);
    s0 = i32_to_u32_ptr(arg1[0]);

    if (t5_1 > 0) {
        int32_t t6_1 = arg5 + 0xf;
        int32_t t6_2;
        int32_t t9_1;
        uint8_t *i_4;

        if (arg5 >= 0) {
            t6_1 = arg5;
        }

        t6_2 = t6_1 >> 4;
        t9_1 = 0;

        while (1) {
            uint32_t *t8_1 = s0;

            if (t6_2 <= 0) {
                t9_1 += 1;
                i_3 = &i_3[arg4[0] * 4];
                s0 = &s0[arg2[0] * 4];
                if (t5_1 == t9_1) {
                    i_4 = i32_to_u8_ptr(arg3[1]);
                    break;
                }
            } else {
                uint32_t *i = i_3;
                int32_t t7_1 = 0;
                int32_t t0_3 = 0;

                do {
                    uint32_t *v0_1 = t8_1;

                    do {
                        _setLeftPart32(i[0]);
                        _setLeftPart32(i[1]);
                        _setLeftPart32(i[2]);
                        _setLeftPart32(i[3]);

                        {
                            uint32_t t3_2 = _setRightPart32(i[0]);
                            uint32_t t2_2 = _setRightPart32(i[1]);
                            uint32_t t0_2 = _setRightPart32(i[3]);
                            uint32_t t1_2 = _setRightPart32(i[2]);

                            v0_1[0] = t3_2;
                            v0_1[1] = t2_2;
                            v0_1[2] = t1_2;
                            v0_1[3] = t0_2;
                            t0_3 = arg2[0];
                            i = &i[4];
                            v0_1 += t0_3;
                        }
                    } while (i != &i[0x40]);

                    t7_1 += 1;
                    t8_1 = &t8_1[4];
                } while (t6_2 != t7_1);

                t9_1 += 1;
                i_3 = &i_3[arg4[0] * 4];
                s0 = &s0[t0_3 * 4];
                if (t5_1 == t9_1) {
                    i_4 = i32_to_u8_ptr(arg3[1]);
                    break;
                }
            }
        }

        {
            uint8_t *t8_2 = i32_to_u8_ptr(arg1[1]);
            int32_t t7_2 = 0;

            while (1) {
                uint8_t *t4_2 = t8_2;

                if (t6_2 <= 0) {
                    t7_2 += 1;
                    result = arg2[1] << 3;
                    i_4 += arg4[1] << 3;
                    t8_2 += result;
                    if (t5_1 == t7_2) {
                        break;
                    }
                } else {
                    uint8_t *i_2 = i_4;
                    int32_t t3_3 = 0;
                    int32_t v0_4 = 0;

                    do {
                        uint8_t *t2_3 = i_2 + 0x80;
                        uint8_t *t1_3 = t4_2;

                        do {
                            uint8_t *v1_2 = t1_3;
                            uint8_t *i_1 = i_2;

                            do {
                                i_1 += 1;
                                v1_2 += 2;
                                *(v1_2 - 2) = *(i_1 - 1);
                                *(v1_2 - 1) = *(i_1 + 7);
                            } while (i_1 != i_2 + 8);

                            v0_4 = arg2[1];
                            i_2 += 0x10;
                            t1_3 += v0_4;
                        } while (i_2 != t2_3);

                        t3_3 += 1;
                        t4_2 += 0x10;
                    } while (t6_2 != t3_3);

                    result = v0_4 << 3;
                    t7_2 += 1;
                    i_4 += arg4[1] << 3;
                    t8_2 += result;
                    if (t5_1 == t7_2) {
                        break;
                    }
                }
            }
        }
    }

    return result;
}

void c_copy_frame_nv21_to_nv12(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                               size_t arg5, int32_t arg6)
{
    uint8_t *fp = i32_to_u8_ptr(arg3[0]);
    uint8_t *t0 = i32_to_u8_ptr(arg1[0]);
    uint8_t *i_1 = i32_to_u8_ptr(arg3[1]);
    uint8_t *s1 = i32_to_u8_ptr(arg1[1]);

    if (arg6 <= 0) {
        return;
    }

    {
        int32_t v0_3 = (int32_t)(((arg5 >> 0x1f) + arg5) >> 1);
        int32_t s7_1 = 0;

        do {
            uint8_t *v0_5 = memcpy(t0, fp, arg5);

            if (s7_1 < (arg6 >> 1)) {
                if (v0_3 > 0) {
                    uint8_t *v1_1 = s1;
                    uint8_t *i = i_1;

                    do {
                        uint8_t a0_1 = *(i + 1);

                        v1_1 += 2;
                        i += 2;
                        *(v1_1 - 2) = a0_1;
                        *(v1_1 - 1) = *(i - 2);
                    } while (i != i_1 + (v0_3 << 1));
                }

                i_1 += arg4[1];
                s1 += arg2[1];
            }

            s7_1 += 1;
            fp += arg4[0];
            t0 = v0_5 + arg2[0];
        } while (arg6 != s7_1);
    }
}

int32_t c_copy_frame_i420_to_nv12(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                                  size_t arg5, int32_t arg6)
{
    int32_t result = arg6;
    uint8_t *fp = i32_to_u8_ptr(arg3[0]);
    uint8_t *t1 = i32_to_u8_ptr(arg1[0]);
    uint8_t *i_1 = i32_to_u8_ptr(arg3[1]);
    uint8_t *s1 = i32_to_u8_ptr(arg3[2]);
    uint8_t *s2 = i32_to_u8_ptr(arg1[1]);

    if (result > 0) {
        int32_t s3_3 = (int32_t)(((arg5 >> 0x1f) + arg5) >> 1);
        int32_t s7_1 = 0;

        do {
            uint8_t *v0_2 = memcpy(t1, fp, arg5);

            if (s7_1 < (arg6 >> 1)) {
                uint8_t *v1_1 = s2;

                if (s3_3 > 0) {
                    uint8_t *a0_1 = s1;
                    uint8_t *i = i_1;

                    do {
                        i += 1;
                        v1_1 += 2;
                        a0_1 += 1;
                        *(v1_1 - 2) = *(i - 1);
                        *(v1_1 - 1) = *(a0_1 - 1);
                    } while (i != i_1 + s3_3);
                }

                i_1 += arg4[1];
                s1 += arg4[2];
                s2 += arg2[1];
            }

            s7_1 += 1;
            t1 = v0_2 + arg2[0];
            result = arg6;
            fp += arg4[0];
        } while (result != s7_1);
    }

    return result;
}

void c_copy_frame_nv12_to_nv12(int32_t *arg1, int32_t *arg2, int32_t *arg3, int32_t *arg4,
                               size_t arg5, int32_t arg6)
{
    if (arg6 <= 0) {
        return;
    }

    {
        int32_t s0_1 = 0;

        while (1) {
            memcpy(i32_to_u8_ptr(arg1[0]) + (s0_1 * arg2[0]), i32_to_u8_ptr(arg3[0]) + (s0_1 * arg4[0]),
                   arg5);

            if ((s0_1 & 1) != 0) {
                s0_1 += 1;
                if (arg6 == s0_1) {
                    break;
                }
            } else {
                int32_t v0_4 = (int32_t)(((uint32_t)s0_1 + ((uint32_t)s0_1 >> 0x1f)) >> 1);

                s0_1 += 1;
                memcpy(i32_to_u8_ptr(arg1[1]) + (v0_4 * arg2[1]),
                       i32_to_u8_ptr(arg3[1]) + (v0_4 * arg4[1]), arg5);
                if (arg6 == s0_1) {
                    break;
                }
            }
        }
    }
}
