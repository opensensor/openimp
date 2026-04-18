#include <stdint.h>

int32_t nv12_scaler_16(
    void *arg1,
    void *arg2,
    void *arg3,
    void *arg4,
    int16_t arg5,
    int16_t arg6,
    int16_t arg7,
    char arg8,
    char arg9,
    char arg10)
{
    uint32_t a1 = (uint16_t)arg9;
    uint32_t v0 = (uint16_t)arg7;
    uint32_t s0 = (uint16_t)arg6;
    uint32_t fp = (uint16_t)arg5;
    uint32_t t8 = (uint8_t)arg8;
    char *src_y = (char *)arg1;
    char *dst_y = (char *)arg2;
    char *src_uv = (char *)arg3;
    char *dst_uv = (char *)arg4;

    if (v0 != 0) {
        int32_t s6_1 = 0;
        int32_t var_40_1 = 0;
        int32_t s7_1 = 0;
        char *arg_8 = src_uv;

        while (1) {
            int32_t t7_3 = ((s6_1 >> 4) + 1) * (int32_t)fp;

            if (s0 != 0) {
                int32_t t2_1 = 0;
                int32_t t1_1 = 0;
                char *t3_1 = dst_y + var_40_1;

                while (1) {
                    if ((((int32_t)(v0 - 0xf)) & -16) >= s7_1) {
                        char *a2_1 = src_y + t7_3 - (int32_t)fp + (t2_1 >> 4) + 1;
                        char *a1_3 = src_y + t7_3 + (t2_1 >> 4) + 1;
                        char *t4_2 = src_y + t7_3 + (int32_t)fp + (t2_1 >> 4) + 1;
                        char *v0_8 = src_y + (t2_1 >> 4) + 1 + ((int32_t)fp << 1) + t7_3;

                        if ((((int32_t)(s0 - 0xf)) & -16) >= t1_1) {
                            uint32_t v1_8 = (uint8_t)*a2_1 + (uint8_t)*a1_3 + (uint8_t)*t4_2 +
                                (uint8_t)*v0_8 + (uint8_t)*(a2_1 - 1) + (uint8_t)a2_1[1] +
                                (uint8_t)a2_1[2] + (uint8_t)*(a1_3 - 1) + (uint8_t)a1_3[1] +
                                (uint8_t)a1_3[2] + (uint8_t)*(t4_2 - 1) + (uint8_t)t4_2[1] +
                                (uint8_t)t4_2[2];
                            t1_1 += 1;
                            *t3_1 = (char)(((v1_8 + (uint8_t)*(v0_8 - 1) + (uint8_t)v0_8[1] +
                                (uint8_t)v0_8[2]) >> 4) & 0xff);
                            t2_1 += (int32_t)t8;
                            t3_1 = &t3_1[1];
                            if ((uint32_t)t1_1 == s0) {
                                break;
                            }
                            continue;
                        }
                    }

                    {
                        char v0_14 = *(src_y + t7_3 + (t2_1 >> 4) + 1);
                        t1_1 += 1;
                        t2_1 += (int32_t)t8;
                        *t3_1 = v0_14;
                        t3_1 = &t3_1[1];
                        if ((uint32_t)t1_1 == s0) {
                            break;
                        }
                    }
                }

                var_40_1 += (int32_t)s0;
            }

            s7_1 += 1;
            s6_1 += (int32_t)a1;
            src_uv = arg_8;
            if ((uint32_t)s7_1 == v0) {
                break;
            }
        }
    }

    {
        uint32_t t3_2 = v0 >> 1;

        if (t3_2 != 0) {
            uint32_t t4_3 = s0 >> 1;
            uint32_t t6_2 = t4_3 << 1;
            int32_t t2_2 = 0;
            int32_t t0_6 = 0;
            int32_t t1_2 = 0;

            do {
                if (t4_3 != 0) {
                    char *i = dst_uv + t0_6;
                    int32_t a0 = 0;

                    do {
                        char *v0_24 = src_uv + (((a0 >> 4) + 1) << 1) + (((t2_2 >> 4) + 1) * (int32_t)fp);
                        i += 2;
                        a0 += (int32_t)t8;
                        *(i - 2) = *v0_24;
                        *(i - 1) = v0_24[1];
                    } while (i != dst_uv + t6_2 + t0_6);

                    t0_6 += (int32_t)t6_2;
                }

                t1_2 += 1;
                t2_2 += (int32_t)a1;
            } while ((uint32_t)t1_2 != t3_2);
        }

        if ((uint8_t)arg10 != 1) {
            return 0;
        }

        {
            int32_t i_1 = 8;
            int32_t t0_8 = ((int32_t)v0 - 1) * (int32_t)s0;
            char *a2_6 = dst_y + (s0 * v0);

            do {
                char *j = dst_y + t0_8;
                char *v1_13 = a2_6;

                if (s0 != 0) {
                    do {
                        char a0_2 = *j;
                        v1_13 += 1;
                        j = &j[1];
                        *(v1_13 - 1) = a0_2;
                    } while (dst_y + s0 + t0_8 != j);
                }

                i_1 -= 1;
                a2_6 += (int32_t)s0;
            } while (i_1 != 0);
        }

        {
            int32_t a3_10 = ((int32_t)t3_2 - 1) * (int32_t)s0;
            int32_t i_2 = 4;
            char *t3_3 = dst_uv + (t3_2 * s0);

            do {
                char *j_1 = dst_uv + a3_10;
                char *v1_15 = t3_3;

                if (s0 != 0) {
                    do {
                        char a0_3 = *j_1;
                        v1_15 += 1;
                        j_1 = &j_1[1];
                        *(v1_15 - 1) = a0_3;
                    } while (dst_uv + s0 + a3_10 != j_1);
                }

                i_2 -= 1;
                t3_3 += (int32_t)s0;
            } while (i_2 != 0);
        }
    }

    return 0;
}

int32_t nv12_scaler_8(
    void *arg1,
    void *arg2,
    void *arg3,
    void *arg4,
    int16_t arg5,
    int16_t arg6,
    int16_t arg7,
    char arg8,
    char arg9,
    char arg10)
{
    uint32_t a1 = (uint16_t)arg9;
    uint32_t v0 = (uint16_t)arg7;
    uint32_t t8 = (uint16_t)arg6;
    uint32_t s6 = (uint16_t)arg5;
    uint32_t t7 = (uint8_t)arg8;
    char *src_y = (char *)arg1;
    char *dst_y = (char *)arg2;
    char *src_uv = (char *)arg3;
    char *dst_uv = (char *)arg4;

    if (v0 != 0) {
        int32_t s7_1 = 0;
        int32_t fp_1 = 0;
        int32_t s5_1 = 0;

        while (1) {
            int32_t t5_3 = ((s7_1 >> 4) + 1) * (int32_t)s6;

            if (t8 != 0) {
                int32_t t2_1 = 0;
                int32_t t1_1 = 0;
                char *t3_1 = dst_y + fp_1;

                while (1) {
                    if ((((int32_t)(v0 - 0xf)) & -16) >= s5_1) {
                        char *a3_1 = src_y + t5_3 - (int32_t)s6 + (t2_1 >> 4) + 1;
                        char *a2_1 = src_y + t5_3 + (int32_t)s6 + (t2_1 >> 4) + 1;
                        char *v0_6 = src_y + t5_3 + (t2_1 >> 4) + 1;

                        if ((((int32_t)(t8 - 0xf)) & -16) >= t1_1) {
                            uint32_t v0_8 = (uint8_t)*a3_1 + (uint8_t)*a2_1 + (uint8_t)*(a3_1 - 1) +
                                (uint8_t)a3_1[1] + (uint8_t)*(v0_6 - 1) + (uint8_t)*(v0_6 + 1) +
                                (uint8_t)*(a2_1 - 1);
                            t1_1 += 1;
                            *t3_1 = (char)(((v0_8 + (uint8_t)a2_1[1]) >> 3) & 0xff);
                            t2_1 += (int32_t)t7;
                            t3_1 = &t3_1[1];
                            if ((uint32_t)t1_1 == t8) {
                                break;
                            }
                            continue;
                        }
                    }

                    {
                        char v0_13 = *(src_y + t5_3 + (t2_1 >> 4) + 1);
                        t1_1 += 1;
                        t2_1 += (int32_t)t7;
                        *t3_1 = v0_13;
                        t3_1 = &t3_1[1];
                        if ((uint32_t)t1_1 == t8) {
                            break;
                        }
                    }
                }

                fp_1 += (int32_t)t8;
            }

            s5_1 += 1;
            s7_1 += (int32_t)a1;
            if ((uint32_t)s5_1 == v0) {
                break;
            }
        }
    }

    {
        uint32_t t4_3 = v0 >> 1;

        if (t4_3 != 0) {
            uint32_t t5_4 = t8 >> 1;
            uint32_t t9_2 = t5_4 << 1;
            int32_t t3_2 = 0;
            int32_t t1_2 = 0;
            int32_t t2_2 = 0;

            do {
                if (t5_4 != 0) {
                    char *i = dst_uv + t1_2;
                    int32_t a0 = 0;

                    do {
                        char *v0_19 = src_uv + (((a0 >> 4) + 1) << 1) + (((t3_2 >> 4) + 1) * (int32_t)s6);
                        i += 2;
                        a0 += (int32_t)t7;
                        *(i - 2) = *v0_19;
                        *(i - 1) = v0_19[1];
                    } while (i != dst_uv + t1_2 + t9_2);

                    t1_2 += (int32_t)t9_2;
                }

                t2_2 += 1;
                t3_2 += (int32_t)a1;
            } while ((uint32_t)t2_2 != t4_3);
        }

        if ((uint8_t)arg10 != 1) {
            return 0;
        }

        {
            int32_t i_1 = 8;
            int32_t t1_4 = ((int32_t)v0 - 1) * (int32_t)t8;
            char *a2_3 = dst_y + (t8 * v0);

            do {
                char *j = dst_y + t1_4;
                char *v1_8 = a2_3;

                if (t8 != 0) {
                    do {
                        char a0_2 = *j;
                        v1_8 += 1;
                        j = &j[1];
                        *(v1_8 - 1) = a0_2;
                    } while (dst_y + t8 + t1_4 != j);
                }

                i_1 -= 1;
                a2_3 += (int32_t)t8;
            } while (i_1 != 0);
        }

        {
            int32_t t0_7 = ((int32_t)t4_3 - 1) * (int32_t)t8;
            int32_t i_2 = 4;
            char *t4_4 = dst_uv + (t4_3 * t8);

            do {
                char *j_1 = dst_uv + t0_7;
                char *v1_9 = t4_4;

                if (t8 != 0) {
                    do {
                        char a0_3 = *j_1;
                        v1_9 += 1;
                        j_1 = &j_1[1];
                        *(v1_9 - 1) = a0_3;
                    } while (dst_uv + t8 + t0_7 != j_1);
                }

                i_2 -= 1;
                t4_4 += (int32_t)t8;
            } while (i_2 != 0);
        }
    }

    return 0;
}
