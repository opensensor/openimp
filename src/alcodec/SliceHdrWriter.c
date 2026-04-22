#include <stdint.h>
#include <string.h>

#include "alcodec/BitStreamLite.h"

/* Channel-context layout is owned by T42 Scheduler and pinned there. This TU
 * uses raw byte offsets until that struct is declared in a shared header. */

extern char _gp;
extern int32_t __assert(const char *expression, const char *file, int32_t line, const char *function,
                        void *caller);

int32_t ceil_log2(int32_t arg1); /* forward decl, ported by T0/T14 earlier */
int32_t AL_HEVC_IsIDR(int32_t arg1); /* forward decl, ported by T<N> later */
int32_t AL_GetAllocSize_WPP(int32_t arg1, int32_t arg2, char arg3); /* forward decl, ported by T20 later */

static const int32_t AL_AVC_SLICE_TYPE[4] = {
    1, 0, 2, 0,
};

static inline uint8_t read_u8(const void *ptr, uint32_t offset)
{
    return *(const uint8_t *)((const uint8_t *)ptr + offset);
}

static inline int8_t read_s8(const void *ptr, uint32_t offset)
{
    return *(const int8_t *)((const uint8_t *)ptr + offset);
}

static inline uint16_t read_u16(const void *ptr, uint32_t offset)
{
    return *(const uint16_t *)((const uint8_t *)ptr + offset);
}

static inline uint32_t read_u32(const void *ptr, uint32_t offset)
{
    return *(const uint32_t *)((const uint8_t *)ptr + offset);
}

static inline int32_t read_s32(const void *ptr, uint32_t offset)
{
    return *(const int32_t *)((const uint8_t *)ptr + offset);
}

static inline void write_u32(void *ptr, uint32_t offset, uint32_t value)
{
    *(uint32_t *)((uint8_t *)ptr + offset) = value;
}

static inline void write_u16(void *ptr, uint32_t offset, uint16_t value)
{
    *(uint16_t *)((uint8_t *)ptr + offset) = value;
}

static inline void write_u8(void *ptr, uint32_t offset, uint8_t value)
{
    *(uint8_t *)((uint8_t *)ptr + offset) = value;
}

static int32_t writeStartCode(AL_BitStreamLite *arg1, int32_t arg2, int32_t arg3)
{
    int32_t a2_1;

    if (arg2 == 0) {
        a2_1 = (uint32_t)(arg3 - 6) < 0xaU ? 1 : 0;
    } else if (arg2 == 1) {
        a2_1 = (uint32_t)(arg3 - 0x20) < 9U ? 1 : 0;
    } else {
        a2_1 = 0;
    }

    if (a2_1 != 0) {
        AL_BitStreamLite_PutBits(arg1, 8, 0);
    }

    AL_BitStreamLite_PutBits(arg1, 8, 0);
    AL_BitStreamLite_PutBits(arg1, 8, 0);
    return AL_BitStreamLite_PutBits(arg1, 8, 1);
}

static int32_t FlushNAL(AL_BitStreamLite *arg1, int32_t arg2, char arg3, char *arg4, char *arg5, int32_t arg6)
{
    char *s0;
    int32_t result;
    uint32_t s2_2;

    s0 = arg5;
    writeStartCode(arg1, arg2, (uint32_t)(uint8_t)arg3);
    result = *(int32_t *)(arg4 + 4);

    if (result > 0) {
        AL_BitStreamLite_PutBits(arg1, 8, (uint32_t)(uint8_t)arg4[0]);
        result = *(int32_t *)(arg4 + 4) < 2 ? 1 : 0;

        if (result == 0) {
            result = AL_BitStreamLite_PutBits(arg1, 8, (uint32_t)(uint8_t)arg4[1]);
        }
    }

    s2_2 = ((uint32_t)arg6 + 7U) >> 3;

    if (s0 != 0) {
        if (s2_2 != 0) {
            uint32_t v0_1;
            int32_t s1;

            result = s2_2 < 3U ? 1 : 0;
            v0_1 = (uint32_t)(uint8_t)*s0;
            s1 = 2;

            if (result == 0) {
                while (1) {
                    AL_BitStreamLite_PutBits(arg1, 8, v0_1);

                    if ((uint8_t)*s0 != 0) {
                        v0_1 = (uint32_t)(uint8_t)s0[1];
                        s0 = &s0[1];
                    } else {
                        v0_1 = (uint32_t)(uint8_t)s0[1];

                        if (v0_1 == 0) {
                            int32_t a3_1 = (int32_t)((uint32_t)(uint8_t)s0[2] & 0xfffffffcU);

                            if (a3_1 == 0) {
                                AL_BitStreamLite_PutBits(arg1, 8, 0);
                                AL_BitStreamLite_PutBits(arg1, 8, 3);
                                s1 += 2;
                                v0_1 = (uint32_t)(uint8_t)s0[2];
                                s0 = &s0[2];

                                if (s1 >= (int32_t)s2_2) {
                                    break;
                                }
                                continue;
                            }
                        }

                        s0 = &s0[1];
                    }

                    s1 += 1;

                    if (s1 >= (int32_t)s2_2) {
                        break;
                    }
                }
            }

            if ((int32_t)s2_2 >= s1) {
                AL_BitStreamLite_PutBits(arg1, 8, v0_1);
                v0_1 = (uint32_t)(uint8_t)s0[1];
            }

            return AL_BitStreamLite_PutBits(arg1, 8, v0_1);
        }
    }

    return result;
}

uint32_t WriteAvcReorderList(AL_BitStreamLite *arg1, int16_t *arg2)
{
    int16_t *s0;
    uint32_t i;

    s0 = arg2;
    AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint16_t)*arg2);
    i = 3;

    if ((uint32_t)(uint16_t)*s0 != 3U) {
        do {
            uint32_t a1_1;

            a1_1 = (uint32_t)(uint16_t)s0[1];
            AL_BitStreamLite_PutUE(arg1, (int32_t)a1_1);
            s0 = &s0[2];
            AL_BitStreamLite_PutUE(arg1, (uint32_t)(uint16_t)*s0);
            i = (uint32_t)(uint16_t)*s0;
        } while (i != 3U);
    }

    return i;
}

void FillRefPOCList(int32_t arg1, int32_t arg2, int32_t arg3, int32_t *arg4, int32_t *arg5, int32_t *arg6,
                    int32_t *arg7, char *arg8, char *arg9, char arg10)
{
    if (arg3 != 0) {
        return;
    }

    if (arg2 < arg1) {
        int32_t a2 = *arg4;
        char *v0_2 = arg8;

        if (a2 > 0) {
            int32_t *v1_1 = &arg6[1];
            int32_t *v0;

            if (arg2 != *arg6) {
                v0 = 0;

                while (1) {
                    v0 += 1;
                    v1_1 += 1;

                    if (a2 == (int32_t)(intptr_t)v0) {
                        break;
                    }

                    if (arg2 == *(v1_1 - 1)) {
                        return;
                    }
                }
            }
        }

        v0_2[a2] = (char)((uint8_t)arg10 ^ 1U);
        *arg4 = a2 + 1;
        arg6[a2] = arg2;
        return;
    }

    if (arg1 < arg2) {
        int32_t a2_2 = *arg5;
        char *v0_4 = arg9;

        if (a2_2 > 0) {
            int32_t *v1_3 = &arg7[1];
            int32_t *v0;

            if (arg2 != *arg7) {
                v0 = 0;

                while (1) {
                    v0 += 1;
                    v1_3 += 1;

                    if (a2_2 == (int32_t)(intptr_t)v0) {
                        break;
                    }

                    if (arg2 == *(v1_3 - 1)) {
                        return;
                    }
                }
            }
        }

        v0_4[a2_2] = (char)((uint8_t)arg10 ^ 1U);
        *arg5 = a2_2 + 1;
        arg7[a2_2] = arg2;
    }
}

int32_t WriteHevcSliceSegmentHdr(AL_BitStreamLite *arg1, uint8_t *arg2, uint8_t *arg3, uint8_t *arg4)
{
    uint32_t v0;
    int16_t v0_2;
    int32_t v0_3;
    int32_t s4;
    int32_t v0_19;
    int32_t v0_29;
    int32_t a1_20;
    int32_t a2_10;
    int32_t a1_34;
    int32_t a0_34;
    char var_3c[4];
    char var_40[4];
    int32_t var_44;
    int32_t var_48;
    int32_t var_58[4];
    int32_t var_68[4];

    v0 = (uint32_t)read_u8(arg3, 0x4e);
    v0_2 = (int16_t)((((int32_t)read_u16(arg3, 0x04) + (1 << (v0 & 0x1f)) - 1) >> (v0 & 0x1f)) *
                      (((int32_t)read_u16(arg3, 0x18) + (1 << (v0 & 0x1f)) - 1) >> (v0 & 0x1f)));
    v0_3 = ceil_log2((int32_t)(uint16_t)v0_2);
    s4 = read_s32(arg3, 0x00);

    AL_BitStreamLite_PutBit(arg1, (uint32_t)arg2[3]);

    if ((uint32_t)(arg2[0] - 0x10U) < 8U) {
        AL_BitStreamLite_PutBit(arg1, (uint32_t)arg2[4]);
    }

    AL_BitStreamLite_PutUE(arg1, (int32_t)arg2[5]);

    if (arg2[3] == 0) {
        if ((read_u8(arg3, 0x28) >> 3 & 1U) != 0) {
            AL_BitStreamLite_PutBit(arg1, (uint32_t)arg2[0x0c]);
        }

        AL_BitStreamLite_PutU(arg1, (uint8_t)v0_3, read_u32(arg2, 0x08));

        if (arg2[0x0c] == 0) {
            AL_BitStreamLite_PutUE(arg1, (int32_t)arg2[0x0d]);
            v0_19 = AL_HEVC_IsIDR((int32_t)arg2[0]);

            if (s4 > 0) {
                AL_BitStreamLite_PutU(arg1, (read_u32(arg3, 0x24) & 0xf) + 1, read_u32(arg2, 0x10));
            } else if (v0_19 == 0) {
                AL_BitStreamLite_PutU(arg1, (read_u32(arg3, 0x24) & 0xf) + 1, read_u32(arg2, 0x10));
            }

            if (v0_19 != 0 && s4 > 0) {
                AL_BitStreamLite_PutU(arg1, 1, (uint32_t)arg2[0x17e]);
            }

            if (arg2[0x0d] != 2) {
                AL_BitStreamLite_PutBit(arg1, (uint32_t)arg2[0x17b]);

                if (arg2[0x17b] != 0) {
                    uint32_t a0_15;
                    int32_t v1_4;
                    uint32_t v0_65;

                    AL_BitStreamLite_PutUE(arg1, (int32_t)arg2[0x17c]);

                    if (arg2[0x0d] == 0) {
                        AL_BitStreamLite_PutUE(arg1, (int32_t)arg2[0x17d]);
                    }

                    if ((read_u8(arg3, 0x28) & 1U) != 0) {
                        a0_15 = read_u32(arg4, 0x164);

                        if (a0_15 >= 2U) {
                            uint8_t v0_24;
                            int32_t i;

                            v0_24 = (uint8_t)ceil_log2((int32_t)a0_15);
                            AL_BitStreamLite_PutBit(arg1, (uint32_t)arg2[0x17f]);

                            if (arg2[0x17f] != 0) {
                                for (i = 0; i <= arg2[0x17c]; ++i) {
                                    AL_BitStreamLite_PutBits(arg1, v0_24, (uint32_t)arg2[0x180 + i]);
                                }
                            }

                            if (arg2[0x0d] == 0) {
                                AL_BitStreamLite_PutBit(arg1, (uint32_t)arg2[0x190]);

                                if (arg2[0x190] != 0) {
                                    for (i = 0; i <= arg2[0x17d]; ++i) {
                                        AL_BitStreamLite_PutBits(arg1, v0_24, (uint32_t)arg2[0x191 + i]);
                                    }
                                }
                            }

                            v1_4 = read_u32(arg3, 0x28);
                        } else {
                            if (arg2[0x0d] == 0) {
                                AL_BitStreamLite_PutBit(arg1, (uint32_t)arg2[0x1a1]);
                            }

                            v1_4 = read_u32(arg3, 0x28);
                        }
                    } else {
                        v1_4 = read_u32(arg3, 0x28);
                    }

                    if ((v1_4 & 2) != 0) {
                        AL_BitStreamLite_PutBit(arg1, (uint32_t)arg2[0x1a2]);
                    }
                }
            }

            if ((read_u32(arg3, 0x24) >> 20 & 1U) != 0) {
                AL_BitStreamLite_PutBit(arg1, (uint32_t)arg2[0x17a]);

                if (s4 > 0) {
                    AL_BitStreamLite_PutU(arg1, 1, (uint32_t)arg2[0x17e]);
                }
            } else if (s4 > 0) {
                AL_BitStreamLite_PutU(arg1, 1, (uint32_t)arg2[0x17e]);
            }

            if (arg2[0x17a] != 0) {
                if (arg2[0x0d] == 0) {
                    AL_BitStreamLite_PutBit(arg1, (uint32_t)arg2[0x1a3]);
                }

                if (arg2[0x1a3] == 0) {
                    if (arg2[0x17c] != 0) {
                        AL_BitStreamLite_PutUE(arg1, (int32_t)arg2[0x1a4]);
                    }

                    if (arg2[0x17d] != 0) {
                        AL_BitStreamLite_PutUE(arg1, (int32_t)arg2[0x1a4]);
                    }
                }
            }

            AL_BitStreamLite_PutUE(arg1, read_s32(arg2, 0x308));
            AL_BitStreamLite_PutSE(arg1, (int32_t)read_s8(arg2, 0x309));

            if ((read_u32(arg2, 0x308) & 0xffff0000U) != 0) {
                AL_BitStreamLite_PutSE(arg1, (int32_t)read_s8(arg2, 0x30a));
                AL_BitStreamLite_PutSE(arg1, (int32_t)read_s8(arg2, 0x30b));
            }

            if ((read_u32(arg3, 0x28) >> 12 & 1U) != 0) {
                AL_BitStreamLite_PutBit(arg1, (uint32_t)arg2[0x30d]);

                if (arg2[0x30d] != 0) {
                    AL_BitStreamLite_PutBit(arg1, (uint32_t)arg2[0x30e]);

                    if (arg2[0x30e] == 0) {
                        AL_BitStreamLite_PutSE(arg1, (int32_t)read_s8(arg2, 0x30f));
                        AL_BitStreamLite_PutSE(arg1, (int32_t)read_s8(arg2, 0x310));
                    }
                }

                if ((read_u32(arg3, 0x30) & 8U) != 0 &&
                    (read_u32(arg2, 0x178) != 0 || arg2[0x30e] == 0)) {
                    AL_BitStreamLite_PutBit(arg1, (uint32_t)arg2[0x311]);
                }
            }
        }
    }

    if ((read_u32(arg3, 0x30) & 3U) != 0) {
        AL_BitStreamLite_PutUE(arg1, read_s32(arg2, 0x312));

        if (read_u32(arg2, 0x312) != 0) {
            int32_t *s1_1;
            int32_t i_2;

            AL_BitStreamLite_PutUE(arg1, (int32_t)arg2[0x314]);

            if (read_u32(arg2, 0x312) != 0) {
                s1_1 = (int32_t *)(arg2 + 0x318);
                i_2 = 0;

                do {
                    AL_BitStreamLite_PutU(arg1, (uint8_t)(arg2[0x314] + 1), (uint32_t)*s1_1);
                    i_2 += 1;
                    s1_1 = &s1_1[1];
                } while (i_2 < read_s32(arg2, 0x312));
            }
        }
    } else {
        AL_BitStreamLite_PutBit(arg1, (uint32_t)arg2[0x14]);
        v0_29 = read_u32(arg3, 0x24);
        a1_20 = (v0_29 >> 8) & 0x3f;

        if (arg2[0x14] == 0) {
            if (a1_20 != 0) {
                AL_BitStreamLite_PutBit(arg1, 0);
            } else {
                a2_10 = read_s32(arg4, 0x58);
                a1_34 = read_s32(arg4, 0x5c);
                a0_34 = read_s32(arg4, 0x28);
                memset(var_40, 0, sizeof(var_40));
                memset(var_3c, 0, sizeof(var_3c));
                var_44 = 0;
                var_48 = 0;
                FillRefPOCList(a0_34, a1_34, a2_10, &var_44, &var_48, var_58, var_68, var_3c, var_40, 0);
                FillRefPOCList(read_s32(arg4, 0x28), read_s32(arg4, 0x70), read_s32(arg4, 0x6c),
                               &var_44, &var_48, var_58, var_68, var_3c, var_40, 0);
                FillRefPOCList(read_s32(arg4, 0x28), read_s32(arg4, 0x98), read_s32(arg4, 0x94),
                               &var_44, &var_48, var_58, var_68, var_3c, var_40, 1);
                FillRefPOCList(read_s32(arg4, 0x28), read_s32(arg4, 0xac), read_s32(arg4, 0xa8),
                               &var_44, &var_48, var_58, var_68, var_3c, var_40, 1);
                FillRefPOCList(read_s32(arg4, 0x28), read_s32(arg4, 0xc0), read_s32(arg4, 0xbc),
                               &var_44, &var_48, var_58, var_68, var_3c, var_40, 1);

                if (var_44 >= 2) {
                    int32_t t4_1;
                    int32_t t5_1;
                    int32_t t1_1;
                    int32_t t2_1;

                    t4_1 = var_44;
                    t5_1 = 0;
                    t1_1 = 0;
                    t2_1 = 1;

                    do {
                        int32_t v1_10;
                        int32_t a2_15;

                        if (t2_1 < var_44) {
                            int32_t *a3_6 = &var_58[t1_1];
                            int32_t v0_57 = t2_1;

                            a2_15 = t1_1;

                            do {
                                if (var_68[a2_15 + 4] < *(a3_6 + 1)) {
                                    a2_15 = v0_57;
                                }

                                v0_57 += 1;
                                a3_6 += 1;
                            } while (v0_57 != var_44);
                        } else {
                            a2_15 = t1_1;
                        }

                        if (t1_1 != 0) {
                            v1_10 = t1_1 + 0x3fffffff;

                            if (var_68[v1_10 + 4] == var_58[a2_15]) {
                                t4_1 = var_44 - 1;
                                {
                                    int32_t *v0_59 = &var_58[a2_15];
                                    char *a2_17 = &var_3c[a2_15];
                                    int32_t *a0_48 = &var_58[t4_1];
                                    char *a3_8 = &var_3c[t4_1];
                                    char t0_3 = *a2_17;
                                    char t2_2 = *a3_8;

                                    *v0_59 = *a0_48;
                                    var_44 = t4_1;
                                    *a2_17 = t2_2;
                                    *a0_48 = var_68[v1_10 + 4];
                                    *a3_8 = t0_3;
                                    t5_1 = 1;
                                }
                            }
                        }

                        if (t1_1 != a2_15) {
                            int32_t *v1_15 = &var_58[t1_1];
                            int32_t *v0_61 = &var_58[a2_15];
                            char *a0_42 = &var_3c[t1_1];
                            char *a2_16 = &var_3c[a2_15];
                            int32_t t0_2 = *v1_15;
                            char a3_7 = *a0_42;
                            char t6_1 = *a2_16;

                            *v1_15 = *v0_61;
                            *a0_42 = t6_1;
                            *v0_61 = t0_2;
                            *a2_16 = a3_7;
                        }

                        t1_1 = t2_1;
                        t2_1 = t1_1 + 1;
                    } while (t1_1 < var_44);

                    if (t5_1 != 0) {
                        var_44 = t4_1;
                    }
                }

                AL_BitStreamLite_PutUE(arg1, 0);
                AL_BitStreamLite_PutUE(arg1, var_48);

                if (var_44 > 0) {
                    int32_t *s1_3 = &var_58[0];
                    char *fp_1 = &var_3c[0];
                    int32_t s7_1 = 0;
                    int32_t v1_16 = read_s32(arg4, 0x28);

                    while (1) {
                        AL_BitStreamLite_PutUE(arg1, v1_16 - *s1_3 - 1);
                        AL_BitStreamLite_PutBit(arg1, (uint32_t)(uint8_t)*fp_1);
                        s7_1 += 1;

                        if (s7_1 >= var_44) {
                            break;
                        }

                        v1_16 = *s1_3;
                        s1_3 = &s1_3[1];
                        fp_1 = &fp_1[1];
                    }
                }

                if (var_48 > 0) {
                    int32_t *s6_3 = &var_68[0];
                    char *s1_4 = &var_40[0];
                    int32_t s7_2 = 0;
                    int32_t v1_16 = read_s32(arg4, 0x28);

                    while (1) {
                        AL_BitStreamLite_PutUE(arg1, *s6_3 - v1_16 - 1);
                        AL_BitStreamLite_PutBit(arg1, (uint32_t)(uint8_t)*s1_4);
                        s7_2 += 1;

                        if (s7_2 >= var_48) {
                            break;
                        }

                        v1_16 = *s6_3;
                        s6_3 = &s6_3[1];
                        s1_4 = &s1_4[1];
                    }
                }
            }
        }

        if (a1_20 != 0) {
            AL_BitStreamLite_PutU(arg1, (uint8_t)a1_20, (uint32_t)arg2[0x15]);
        }

        v0_29 = read_u32(arg3, 0x24);

        if (((v0_29 >> 14) & 0x3f) != 0) {
            AL_BitStreamLite_PutUE(arg1, (int32_t)arg2[0x17]);

            if (arg2[0x17] != 0) {
                AL_BitStreamLite_PutU(arg1, (read_u32(arg3, 0x24) & 0xf) + 1, read_u32(arg2, 0x38));
            }
        }

        AL_BitStreamLite_PutBit(arg1, (uint32_t)arg2[0xb8]);
        AL_BitStreamLite_PutBit(arg1, 0);
    }

    AL_BitStreamLite_PutU(arg1, 1, 1);
    return AL_BitStreamLite_AlignWithBits(arg1, 0);
}

int32_t WriteAvcSliceSegmentHdr(AL_BitStreamLite *arg1, uint8_t *arg2, uint8_t *arg3, uint8_t *arg4)
{
    uint32_t v0;
    uint32_t s4;
    int32_t a1_4;
    uint32_t v0_5;
    uint32_t v0_7;
    uint32_t v0_9;
    uint32_t v1_2;
    int32_t result;

    AL_BitStreamLite_PutUE(arg1, read_s32(arg2, 0x04));
    v0 = read_u32(arg2, 0x08);

    if (v0 >= 3U) {
        int32_t a0_34;
        void *a1_23;
        void *a2_11;
        void *a3_1;

        a0_34 = __assert("pSH->slice_type < 3",
                         "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/SliceHdrWriter.c",
                         0x2e8, "WriteAvcSliceSegmentHdr", &_gp);
        (void)a1_23;
        (void)a2_11;
        (void)a3_1;
        return a0_34;
    }

    AL_BitStreamLite_PutUE(arg1, AL_AVC_SLICE_TYPE[v0]);
    AL_BitStreamLite_PutUE(arg1, (int32_t)arg2[9]);
    s4 = read_u32(arg2, 0x4f5);
    a1_4 = (read_u32(arg3, 0x24) >> 4) & 0xf;

    if (s4 == 5U) {
        AL_BitStreamLite_PutU(arg1, (uint8_t)a1_4, 0);
        AL_BitStreamLite_PutUE(arg1, read_s32(arg2, 0x14));
    } else {
        AL_BitStreamLite_PutU(arg1, (uint8_t)a1_4, ((1U << (a1_4 & 0x1f)) - 1U) & read_u32(arg2, 0x0c));
    }

    AL_BitStreamLite_PutU(arg1, (read_u32(arg3, 0x24) & 0xf) + 1, read_u32(arg2, 0x18));
    v0_5 = read_u32(arg2, 0x08);

    if (v0_5 == 0) {
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)arg2[0x2a]);
        v0_5 = read_u32(arg2, 0x08);
    }

    if (v0_5 != 2) {
        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)arg2[0x2b]);

        if (arg2[0x2b] != 0) {
            AL_BitStreamLite_PutUE(arg1, read_s32(arg2, 0x2c));
            v0_7 = read_u32(arg2, 0x08);

            if (v0_7 == 0) {
                AL_BitStreamLite_PutUE(arg1, read_s32(arg2, 0x30));
                v0_7 = read_u32(arg2, 0x08);
            }
        } else {
            v0_7 = read_u32(arg2, 0x08);
        }

        if (v0_7 != 2) {
            AL_BitStreamLite_PutU(arg1, 1, (uint32_t)arg2[0x168]);

            if (arg2[0x168] != 0) {
                WriteAvcReorderList(arg1, (int16_t *)(arg4 + 0xe0));
                v0_9 = read_u32(arg2, 0x08);
            } else {
                v0_9 = read_u32(arg2, 0x08);
            }

            if (v0_9 == 0) {
                AL_BitStreamLite_PutU(arg1, 1, (uint32_t)arg2[0x169]);

                if (arg2[0x169] != 0) {
                    WriteAvcReorderList(arg1, (int16_t *)(arg4 + 0x122));
                }
            }
        }
    }

    if (arg2[0x29] != 0) {
        if (s4 == 5U) {
            AL_BitStreamLite_PutU(arg1, 1, (uint32_t)arg2[0x4ec]);
            AL_BitStreamLite_PutU(arg1, 1, (uint32_t)arg2[0x4ed]);

            if (read_u32(arg3, 0x54) == 0) {
                goto label_71ea8;
            }

            v1_2 = read_u32(arg2, 0x08);
            goto label_71ef4;
        }

        AL_BitStreamLite_PutU(arg1, 1, (uint32_t)arg2[0x4ee]);

        if (arg2[0x4ee] != 0) {
            if (read_u32(arg4, 0xd4) != 0xffffffffU) {
                AL_BitStreamLite_PutUE(arg1, 1);
                AL_BitStreamLite_PutUE(arg1, read_s32(arg4, 0x2c) - read_s32(arg4, 0xd4) - 1);
            }

            if (arg2[0x4ed] != 0 || arg4[0xd8] != 0) {
                AL_BitStreamLite_PutUE(arg1, 4);
                AL_BitStreamLite_PutUE(arg1, 1);

                if (arg4[0xd8] != 0) {
                    AL_BitStreamLite_PutUE(arg1, 2);
                    AL_BitStreamLite_PutUE(arg1, 0);
                }

                if (arg2[0x4ed] != 0) {
                    AL_BitStreamLite_PutUE(arg1, 6);
                    AL_BitStreamLite_PutUE(arg1, 0);
                }
            }

            AL_BitStreamLite_PutUE(arg1, 0);
        }
    }

    if (read_u32(arg3, 0x54) != 0) {
        v1_2 = read_u32(arg2, 0x08);

label_71ef4:
        if (v1_2 != 2) {
            AL_BitStreamLite_PutUE(arg1, (int32_t)arg2[0x4ef]);
            AL_BitStreamLite_PutSE(arg1, read_s32(arg2, 0x4f0));

            if ((read_u32(arg3, 0x28) >> 2 & 1U) != 0) {
                AL_BitStreamLite_PutUE(arg1, (int32_t)arg2[0x4f4]);

                if (arg2[0x4f4] != 1) {
                    AL_BitStreamLite_PutSE(arg1, (int32_t)read_s8(arg2, 0x4f6));
                    AL_BitStreamLite_PutSE(arg1, (int32_t)read_s8(arg2, 0x4f7));
                }
            }
        }
    } else {
label_71ea8:
        AL_BitStreamLite_PutSE(arg1, read_s32(arg2, 0x4f0));

        if ((read_u32(arg3, 0x28) >> 2 & 1U) != 0) {
            AL_BitStreamLite_PutUE(arg1, (int32_t)arg2[0x4f4]);

            if (arg2[0x4f4] != 1) {
                AL_BitStreamLite_PutSE(arg1, (int32_t)read_s8(arg2, 0x4f6));
                AL_BitStreamLite_PutSE(arg1, (int32_t)read_s8(arg2, 0x4f7));
            }
        }
    }

    result = read_s32(arg3, 0x54);

    if (result == 0) {
        return result;
    }

    return AL_BitStreamLite_AlignWithBits(arg1, 1);
}

int32_t GetSliceSizeOffset(uint8_t *arg1, uint8_t *arg2, int32_t arg3, uint8_t arg4)
{
    uint32_t v0_10;
    uint32_t a3;
    uint32_t t0;
    int32_t v1;
    uint32_t t1;
    uint32_t a1;
    int32_t lo;
    int32_t v0_8;

    v0_10 = read_u16(arg2, 0x10a);
    a3 = (uint32_t)arg4;
    t0 = read_u16(arg1, 0x40);
    v1 = (int32_t)(v0_10 * a3);
    t1 = read_u16(arg2, 0x108);
    a1 = read_u16(arg1, 0x3c);
    lo = v1 / (int32_t)t0;

    if (t0 == 0) {
        __builtin_trap();
    }

    if (t0 == 0) {
        __builtin_trap();
    }

    if (a1 == 0) {
        __builtin_trap();
    }

    v0_8 = (((((((((int32_t)(v0_10 + v1) / (int32_t)t0) - lo) * (int32_t)t1) << 2) / (int32_t)a1) + 0x80) *
              arg3) +
             (((int32_t)a3 << 7) * (int32_t)a1) + ((lo * (int32_t)t1) << 2) + 0x7f) >>
            7;
    return v0_8 << 7;
}

int32_t GetWPPSize(uint8_t *arg1, uint8_t *arg2)
{
    uint32_t v0_5;
    uint32_t a0;

    v0_5 = read_u16(arg1, 0x40);
    a0 = read_u16(arg1, 0x3c);

    if (v0_5 == 0) {
        __builtin_trap();
    }

    if (a0 == 0) {
        __builtin_trap();
    }

    return ((((int32_t)read_u16(arg2, 0x10a) + (int32_t)v0_5 - 1) / (int32_t)v0_5 +
             (((int32_t)a0 - 1) << 1)) /
            (int32_t)a0)
           << 2;
}

int32_t GetWPPOffset(uint8_t *arg1, uint8_t *arg2, int32_t arg3, uint8_t arg4)
{
    int32_t v0;
    int32_t v0_1;
    uint32_t a3;
    int64_t acc;

    v0 = AL_GetAllocSize_WPP((int32_t)read_u16(arg2, 0x10a), (int32_t)read_u16(arg1, 0x40),
                             (char)read_u8(arg1, 0x3c));
    v0_1 = GetWPPSize(arg1, arg2);
    a3 = read_u16(arg1, 0x3c);

    if (a3 == 0) {
        __builtin_trap();
    }

    acc = (int64_t)(v0 / (int32_t)a3) * (int64_t)arg3;
    acc += (int64_t)(uint32_t)arg4 * (int64_t)((((v0_1 + 0x7f) >> 7) << 7));
    return (int32_t)((((int32_t)acc + 0x7f) >> 7) << 7);
}

int32_t GetWPPOrSliceSizeOffset(uint8_t *arg1, uint8_t *arg2, int32_t arg3, uint8_t arg4)
{
    if (read_u32(arg2, 0xec) != 0) {
        return GetSliceSizeOffset(arg1, arg2, arg3, arg4);
    }

    if (read_u32(arg2, 0x2c) != 0) {
        return GetWPPOffset(arg1, arg2, arg3, arg4);
    }

    return 0;
}

int32_t GenerateHevcSliceHeader(uint8_t *chCtx, uint8_t *sliceParam, uint8_t *picCtx, uint8_t *schedCtx,
                                uint8_t *coreOffsets, int32_t dstOffset, int16_t tileGroup)
{
    uint8_t sh[0xd00];
    uint32_t mask;
    uint32_t numCore;
    uint32_t nalType;
    uint32_t listCount0;
    uint32_t listCount1;
    AL_BitStreamLite *bs;
    uint32_t bitsCount;
    uint32_t count;
    uint32_t maxVal;
    uint32_t i;
    uint8_t nalHdr[8];

    memset(sh, 0, sizeof(sh));

    nalType = read_u32(sliceParam, 0x24);
    if ((nalType & 1U) != 0) {
        nalType = 0x13;
    } else if (read_u8(sliceParam, 0x3c) == 0) {
        nalType = ((nalType & 2U) != 0) ? 1U : 0U;
    } else {
        nalType = ((nalType & 2U) != 0) ? 3U : 2U;
    }

    write_u32(picCtx, 0x11c, nalType);
    numCore = read_u8(chCtx, 0x3c);
    if (numCore == 0) {
        return __assert("pCtx->ChanParam.uNumCore != 0",
                        "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/SliceHdrWriter.c",
                        0x81, "GenerateHevcSliceHeader", &_gp);
    }

    mask = (1U << (((read_u32(chCtx, 0x24) & 0xfU) + 1U) & 0x1fU)) - 1U;

    sh[0x000] = (uint8_t)nalType;
    sh[0x002] = (uint8_t)(read_u8(sliceParam, 0x3c) + 1U);
    sh[0x003] = (uint8_t)(read_u32(picCtx, 0x3c) < 1U);
    write_u32(sh, 0x008, read_u32(picCtx, 0x3c));
    sh[0x00c] = read_u32(picCtx, 0x3c) != 0 ? ((read_u8(sliceParam, 0x00) >> 4) & 1U) : 0;
    /* raw channel-context offset 0x24 (Binary Ninja alias "group_update+0x24") */
    sh[0x00d] = *(uint8_t *)((char *)chCtx + 0x24);
    sh[0x010] = 0;
    write_u32(sh, 0x010, read_u32(sliceParam, 0x28) & mask);
    sh[0x014] = (uint8_t)(read_u8(sliceParam, 0x44) != 0xff);
    sh[0x015] = read_u8(sliceParam, 0x44);

    listCount0 = read_u8(sliceParam, 0x120);
    sh[0x17f] = (uint8_t)listCount0;
    for (i = 0; i < listCount0; ++i) {
        sh[0x180 + i] = read_u8(sliceParam, 0xe0 + (i * 2));
    }

    listCount1 = read_u8(sliceParam, 0x488);
    sh[0x190] = (uint8_t)listCount1;
    for (i = 0; i < listCount1; ++i) {
        sh[0x191 + i] = read_u8(sliceParam, 0x488 + (i * 2));
    }

    if (read_s32(sliceParam, 0x58) == 1) {
        sh[0x0b8] = 1;
        sh[0x017] = 1;
        write_u32(sh, 0x038, read_u32(sliceParam, 0x5c) & mask);
    } else if (read_s32(sliceParam, 0x6c) == 1) {
        sh[0x0b8] = 1;
        sh[0x017] = 1;
        write_u32(sh, 0x038, read_u32(sliceParam, 0x70) & mask);
    } else if (read_s32(sliceParam, 0x94) == 1 || read_s32(sliceParam, 0xa8) == 1 ||
               read_s32(sliceParam, 0xbc) == 1) {
        sh[0x017] = 1;
        if (read_s32(sliceParam, 0x94) == 1) {
            write_u32(sh, 0x038, read_u32(sliceParam, 0x98) & mask);
        } else if (read_s32(sliceParam, 0xa8) == 1) {
            write_u32(sh, 0x038, read_u32(sliceParam, 0xac) & mask);
        } else {
            write_u32(sh, 0x038, read_u32(sliceParam, 0xc0) & mask);
        }
    }

    sh[0x086] = read_u8(picCtx, 0x23) != 0 ? (uint8_t)(read_u8(sliceParam, 0x30) != 6) : 0;
    sh[0x07d] = 1;
    if (read_u8(picCtx, 0x41) == ((read_u32(chCtx, 0x28) >> 4) & 0xfU) - ((((read_u32(chCtx, 0x28) >> 4) & 0xfU) != 0U) ? 1U : 0U)) {
        uint32_t l1 = (read_u32(chCtx, 0x28) >> 8) & 0xfU;
        sh[0x07d] = (uint8_t)(read_u8(picCtx, 0x40) != (l1 - ((l1 != 0U) ? 1U : 0U)) || read_u32(picCtx, 0x30) != 0);
    }

    if (read_u8(picCtx, 0x10) < 2U) {
        sh[0x190 - 0x139] = read_u8(picCtx, 0x10);
        sh[0x15d] = read_u32(picCtx, 0x30) == 1 ? 1U : read_u8(picCtx, 0x20);
        sh[0x0f0] = (uint8_t)(5 - read_u8(picCtx, 0x0f));
        sh[0x113] = read_u8(picCtx, 0x27);
        sh[0x154] = read_u8(picCtx, 0x9c);
        sh[0x111] = (uint8_t)(read_u8(picCtx, 0x28) - read_u8(sliceParam, 0x04));
        sh[0x112] = read_u8(picCtx, 0x26);
        sh[0x110] = read_u8(picCtx, 0x36);
        sh[0x10f] = read_u8(picCtx, 0x37);
        sh[0x115] = read_u8(picCtx, 0x38);
        sh[0x10b] = (uint8_t)((((read_u32(chCtx, 0x28) >> 13) & 1U) ^ read_u8(picCtx, 0x38)) |
                              ((read_s8(chCtx, 0x34) != read_s8(picCtx, 0x37)) ||
                               (read_s8(chCtx, 0x35) != read_s8(picCtx, 0x36))));
        sh[0x111 - 0x2] = (read_u32(chCtx, 0x30) >> 3) & 1U;
    }

    if (read_u8(sliceParam, 0x30) != 7) {
        if (read_u32(picCtx, 0xec) != 0) {
            if ((read_u32(chCtx, 0x30) & 1U) == 0) {
                if ((read_u32(chCtx, 0x30) & 2U) == 0) {
                    write_u32(sh, 0x312, 0);
                } else {
                    count = numCore - 1U;
                    write_u32(sh, 0x312, count);
                    for (i = 0; i < count; ++i) {
                        write_u32(sh, 0x318 + (i * 4), read_u32(coreOffsets, 0x20 + (i * 0x78)) - 1U);
                    }
                }
            } else {
                /* raw channel-context offsets 0x3d84/0x3d88 owned by T42 Scheduler */
                count = *(uint32_t *)((char *)chCtx + 0x3d84);
                if (count != 0) {
                    count -= 1U;
                }
                write_u32(sh, 0x312, count);
                for (i = 0; i < count; ++i) {
                    write_u32(sh, 0x318 + (i * 4), *(uint32_t *)((char *)chCtx + 0x3d88 + (i * 4)) - 1U);
                }
            }
        } else if (read_u32(picCtx, 0x2c) != 0) {
            uint32_t rowCount;
            uint32_t colWidth;
            uint32_t *sliceSizeTable;

            if ((read_u32(chCtx, 0x30) & 2U) != 0) {
                return __assert("!(pCtx->ChanParam.eEncTools & AL_OPT_WPP)",
                                "/home/user/git/proj/sdk-lv3/src/imp/video/alcodec/lib_scheduler_enc/SliceHdrWriter.c",
                                0xca, "GenerateHevcSliceHeader", &_gp);
            }

            sliceSizeTable = (uint32_t *)schedCtx;
            for (i = 0; i < numCore; ++i) {
                sliceSizeTable[i] = (uint32_t)(GetWPPOrSliceSizeOffset(chCtx, picCtx, (int32_t)i, (uint8_t)tileGroup) >> 2);
            }

            colWidth = read_u16(picCtx, 0x108);
            if (colWidth == 0) {
                __builtin_trap();
            }
            rowCount = (read_u32(picCtx, 0x44) / colWidth) - (read_u32(picCtx, 0x3c) / colWidth);
            write_u32(sh, 0x312, rowCount);
            for (i = 0; i < rowCount; ++i) {
                uint32_t coreIdx = i / numCore;
                uint32_t laneIdx = i % numCore;
                write_u32(sh, 0x318 + (i * 4), ((uint32_t *)read_u32(schedCtx, 0x88))[coreIdx + sliceSizeTable[laneIdx]] - 1U);
            }
        } else {
            write_u32(sh, 0x312, 0);
        }

        count = read_u32(sh, 0x312);
        maxVal = 0;
        for (i = 0; i < count; ++i) {
            uint32_t value = read_u32(sh, 0x318 + (i * 4));
            if (maxVal < value) {
                maxVal = value;
            }
        }

        i = 0;
        while (maxVal != 0U) {
            maxVal >>= 1;
            ++i;
        }
        write_u32(sh, 0x314, (i != 0U) ? (i - 1U) : 0U);
    } else {
        write_u32(sh, 0x312, 0);
    }

    bs = *(AL_BitStreamLite **)(chCtx + 0x20);
    AL_BitStreamLite_Reset(bs);
    WriteHevcSliceSegmentHdr(bs, sh, chCtx, sliceParam);
    bitsCount = (uint32_t)AL_BitStreamLite_GetBitsCount(bs);

    nalHdr[0] = read_u8(sliceParam, 0x3c);
    nalHdr[1] = read_u8(chCtx, 0x00);
    memset(&nalHdr[2], 0, sizeof(nalHdr) - 2);
    write_u32(nalHdr, 4, 2);

    return FlushNAL((AL_BitStreamLite *)(*(uintptr_t *)(*(uintptr_t *)(schedCtx + 0x80) + 8U) + (uintptr_t)dstOffset), 1,
                    (char)nalType, (char *)nalHdr, (char *)AL_BitStreamLite_GetData(bs), (int32_t)bitsCount);
}

int32_t GenerateAvcSliceHeader(uint8_t *chCtx, uint8_t *sliceParam, uint8_t *picCtx, uint8_t *schedCtx,
                               int32_t dstOffset, char stripPrefix)
{
    uint8_t sh[0x540];
    AL_BitStreamLite *bs;
    uint32_t nalType;
    uint32_t bitsCount;
    uint32_t bytesCount;
    uint32_t prefixBytes;
    uint32_t result;
    uint32_t a1_7;
    uint8_t *dst;

    memset(sh, 0, sizeof(sh));

    nalType = (read_u32(sliceParam, 0x24) & 1U) != 0 ? 5U : 1U;
    write_u32(picCtx, 0x11c, nalType);

    write_u16(sh, 0x000, (uint16_t)((((int32_t)read_u16(picCtx, 0x10a) + 1) >> 1) << 4));
    write_u32(sh, 0x004, read_u32(picCtx, 0x3c));
    write_u32(sh, 0x008, read_u32(picCtx, 0x30));
    sh[0x009] = read_u8(chCtx, 0x24);
    write_u32(sh, 0x00c, read_u32(sliceParam, 0xb0));
    sh[0x00e] = 0;
    sh[0x028] = 0;
    write_u32(sh, 0x014, read_u32(sliceParam, 0x5a0));
    write_u32(sh, 0x018, read_u32(sliceParam, 0xa0) &
                             ((1U << ((((read_u32(chCtx, 0x24) & 0xfU) + 1U) & 0x1fU))) - 1U));
    write_u32(sh, 0x01c, 0);
    sh[0x02a] = 0;
    sh[0x02b] = (uint8_t)((read_u8(picCtx, 0x41) != (((read_u32(chCtx, 0x28) >> 4) & 0xfU) -
                                                      ((((read_u32(chCtx, 0x28) >> 4) & 0xfU) != 0U) ? 1U : 0U))) ||
                          ((read_u8(picCtx, 0x40) != (((read_u32(chCtx, 0x28) >> 8) & 0xfU) -
                                                      ((((read_u32(chCtx, 0x28) >> 8) & 0xfU) != 0U) ? 1U : 0U))) ||
                           read_u32(picCtx, 0x30) != 0));
    write_u32(sh, 0x02c, read_u32(picCtx, 0x41));
    write_u32(sh, 0x030, read_u32(picCtx, 0x40));
    sh[0x029] = read_u8(sliceParam, 0xa4);
    sh[0x168] = read_u8(sliceParam, 0x5a0);
    sh[0x169] = read_u8(sliceParam, 0x5a4);
    sh[0x4ec] = read_u8(sliceParam, 0x1330);
    sh[0x4ed] = read_u8(sliceParam, 0x1334);
    sh[0x4ee] = read_u8(sliceParam, 0x1338);
    sh[0x4ef] = read_u8(sliceParam, 0x133c);
    write_u32(sh, 0x4f0, read_u32(sliceParam, 0x1340));
    sh[0x4f4] = read_u8(sliceParam, 0x1350);
    sh[0x4f5] = (uint8_t)nalType;
    sh[0x4f6] = read_u8(sliceParam, 0x1358);
    sh[0x4f7] = read_u8(sliceParam, 0x135c);

    bs = *(AL_BitStreamLite **)(chCtx + 0x20);
    AL_BitStreamLite_Reset(bs);
    WriteAvcSliceSegmentHdr(bs, sh, chCtx, sliceParam);
    bitsCount = (uint32_t)AL_BitStreamLite_GetBitsCount(bs);

    write_u8(picCtx, 0xf8, 0);
    bytesCount = (bitsCount + 7U) >> 3;
    prefixBytes = 0;
    if (read_u32(chCtx, 0x54) == 0) {
        uint32_t prefixBits = bitsCount & 0xffffU;
        uint32_t prefixValue = (prefixBits >= 0x18U) ? ((bitsCount & 7U) + 0x10U) : (bitsCount & 0xffU);

        write_u8(picCtx, 0xf8, (uint8_t)prefixValue);
        prefixBytes = (prefixValue + 7U) >> 3;
    }

    if ((uint8_t)stripPrefix == 0) {
        bytesCount -= prefixBytes;
        prefixBytes = 0;
    }

    if (read_u32(sliceParam, 0xc0) == 7 && read_u32(chCtx, 0x54) == 0) {
        AL_BitStreamLite_PutUE(bs, read_u8(picCtx, 0x108) * read_u8(picCtx, 0x10a));
        AL_BitStreamLite_PutU(bs, 1, 1);
        AL_BitStreamLite_AlignWithBits(bs, 0);
        bytesCount = ((uint32_t)AL_BitStreamLite_GetBitsCount(bs) + 7U) >> 3;
    }

    dst = *(uint8_t **)(*(uintptr_t *)(schedCtx + 0x80) + 8U) + dstOffset;
    result = (uint8_t)FlushNAL((AL_BitStreamLite *)(dst + prefixBytes), 0, (char)nalType, 0,
                               (char *)AL_BitStreamLite_GetData(bs), (int32_t)bitsCount);
    result -= prefixBytes;

    if (read_u8(picCtx, 0xf8) != 0) {
        a1_7 = bitsCount & 7U;
        write_u32(picCtx, 0x100,
                  ((uint32_t)dst[1] << 8 | (uint32_t)dst[0] << 16 | (uint32_t)dst[2] |
                   (uint32_t)dst[(uint32_t)-1] << 24) >>
                      ((8U - a1_7) & 0x1fU));
    } else {
        write_u32(picCtx, 0x100, 0);
    }

    if ((read_u32(chCtx, 0xa8) & 4U) != 0) {
        uint32_t seiBits;

        AL_BitStreamLite_Reset(bs);
        AL_BitStreamLite_PutU(bs, 1, 1);
        AL_BitStreamLite_PutU(bs, 1, read_u32(sliceParam, 0x24) & 1U);
        AL_BitStreamLite_PutU(bs, 6, 0);
        AL_BitStreamLite_PutU(bs, 1, 1);
        AL_BitStreamLite_PutU(bs, 3, 0);
        AL_BitStreamLite_PutU(bs, 4, 0);
        AL_BitStreamLite_PutU(bs, 3, read_u32(sliceParam, 0xf0));
        AL_BitStreamLite_PutU(bs, 1, 0);
        AL_BitStreamLite_PutU(bs, 1, 0);
        AL_BitStreamLite_PutU(bs, 1, 1);
        AL_BitStreamLite_PutU(bs, 2, 3);
        seiBits = (uint32_t)AL_BitStreamLite_GetBitsCount(bs);
        result += (uint8_t)FlushNAL((AL_BitStreamLite *)(dst - result), 0, 0x0e, 0,
                                    (char *)AL_BitStreamLite_GetData(bs), (int32_t)seiBits);
    }

    return result;
}
