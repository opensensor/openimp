#include <stdint.h>

typedef struct {
    int32_t bitstream;
    int32_t residue;
} BitstreamState;

typedef struct {
    int16_t valprev;
    int8_t index;
    int8_t pad;
} AdpcmState;

typedef struct G726State G726State;

struct G726State {
    int32_t rate;
    int32_t code_size;
    int32_t yl;
    int16_t yu;
    int16_t dms;
    int16_t dml;
    int16_t ap;
    int16_t a[2];
    int16_t b[6];
    int16_t pk[2];
    int16_t dq[6];
    int16_t sr[2];
    int8_t td;
    uint8_t pad_39[3];
    BitstreamState bs;
    int32_t (*encoder)(G726State *state, int32_t sample);
    int32_t (*decoder)(G726State *state, char code);
};

_Static_assert(__builtin_offsetof(G726State, yl) == 0x08, "G726State.yl");
_Static_assert(__builtin_offsetof(G726State, yu) == 0x0c, "G726State.yu");
_Static_assert(__builtin_offsetof(G726State, ap) == 0x12, "G726State.ap");
_Static_assert(__builtin_offsetof(G726State, a) == 0x14, "G726State.a");
_Static_assert(__builtin_offsetof(G726State, b) == 0x18, "G726State.b");
_Static_assert(__builtin_offsetof(G726State, pk) == 0x24, "G726State.pk");
_Static_assert(__builtin_offsetof(G726State, dq) == 0x28, "G726State.dq");
_Static_assert(__builtin_offsetof(G726State, sr) == 0x34, "G726State.sr");
_Static_assert(__builtin_offsetof(G726State, td) == 0x38, "G726State.td");
_Static_assert(__builtin_offsetof(G726State, bs) == 0x3c, "G726State.bs");
#if UINTPTR_MAX == 0xffffffffu
_Static_assert(__builtin_offsetof(G726State, encoder) == 0x44, "G726State.encoder");
_Static_assert(__builtin_offsetof(G726State, decoder) == 0x48, "G726State.decoder");
_Static_assert(sizeof(G726State) == 0x4c, "G726State size");
#endif

static const int16_t seg_uend[8] = {0x003f, 0x007f, 0x00ff, 0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff};
static const int16_t seg_aend[8] = {0x001f, 0x003f, 0x007f, 0x00ff, 0x01ff, 0x03ff, 0x07ff, 0x0fff};

static const int32_t qtab_726_40[15] = {
    -122, -16, 68, 139, 198, 250, 298, 339, 378, 413, 445, 475, 502, 528, 553,
};
static const int32_t qtab_726_32[7] = {-124, 80, 178, 246, 300, 349, 400};
static const int32_t qtab_726_24[3] = {8, 218, 331};
static const int32_t qtab_726_16[1] = {261};

static const int32_t g726_40_witab[32] = {
    0, 0, 0, 0, 0x200, 0x200, 0x200, 0x200,
    0x600, 0xe00, 0xe00, 0x600, 0x200, 0x200, 0x200, 0x200,
    0x200, 0x200, 0x200, 0x200, 0x600, 0xe00, 0xe00, 0x600,
    0x200, 0x200, 0x200, 0x200, 0, 0, 0, 0,
};
static const int32_t g726_40_fitab[32] = {
    -704, 960, 1696, 2304, 2898, 3454, 4008, 4551,
    5142, 5661, 6208, 6765, 7281, 7865, 8471, 9005,
    9507, 10039, 10569, 11192, 11773, 12269, 12794, 13321,
    13836, 14319, 14880, 15426, 15840, 16319, 16895, 17471,
};
static const int32_t g726_40_dqlntab[32] = {
    -2048, -66, 28, 104, 169, 224, 274, 318,
    358, 395, 429, 459, 488, 514, 539, 566,
    566, 539, 514, 488, 459, 429, 395, 358,
    318, 274, 224, 169, 104, 28, -66, -2048,
};

static const int32_t g726_32_witab[16] = {
    -12, 18, 41, 64, 112, 198, 355, 1122,
    1122, 355, 198, 112, 64, 41, 18, -12,
};
static const int32_t g726_32_fitab[16] = {
    0, 0x200, 0x200, 0x200, 0x600, 0xe00, 0xe00, 0x600,
    0x200, 0x200, 0x200, 0x200, 0x200, 0x200, 0x200, 0,
};
static const int32_t g726_32_dqlntab[16] = {
    -2048, 4, 135, 213, 273, 323, 373, 425,
    425, 373, 323, 273, 213, 135, 4, -2048,
};

static const int32_t g726_24_witab[8] = {-128, 960, 4384, 4384, 960, -128, -128, -128};
static const int32_t g726_24_fitab[8] = {0, 0x200, 0x400, 0xe00, 0xe00, 0x400, 0x200, 0};
static const int32_t g726_24_dqlntab[8] = {-2048, 135, 273, 373, 373, 273, 135, -2048};

static const int32_t g726_16_witab[4] = {-704, 14048, 14048, -704};
static const int32_t g726_16_fitab[4] = {0, 0xe00, 0xe00, 0};
static const int32_t g726_16_dqlntab[4] = {116, 365, 365, 116};

static const int32_t stepsizeTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
    724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
static const int32_t indexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

uint32_t linear2alaw(int32_t arg1)
{
    int32_t a0 = arg1 >> 3;
    char v0 = (char)-0x2b;
    int32_t a2 = 0;
    const int16_t *a1 = seg_aend;

    if (a0 < 0) {
        a0 = ~a0;
        v0 = 0x55;
    }

    while (1) {
        a1 += 1;
        if (*((a1 - 1)) >= a0) {
            break;
        }
        a2 += 1;
        if (a2 == 8) {
            return (uint32_t)((uint8_t)v0 ^ 0x7f);
        }
    }

    if (a2 < 2) {
        return (uint32_t)((uint8_t)v0 ^ ((((uint8_t)(a0 >> 1)) & 0x0f) | ((uint8_t)a2 << 4)));
    }
    return (uint32_t)((uint8_t)v0 ^ ((((uint8_t)(a0 >> a2)) & 0x0f) | ((uint8_t)a2 << 4)));
}

uint32_t linear2ulaw(int32_t arg1)
{
    int32_t a0 = arg1 >> 2;
    char v0 = (char)-1;
    int32_t i = 0;
    const int16_t *a1 = seg_uend;

    if (a0 < 0) {
        a0 = -a0;
        v0 = 0x7f;
    }

    if (a0 >= 0x1fe0) {
        a0 = 0x1fdf;
    }

    do {
        a1 += 1;
        if (*((a1 - 1)) >= (a0 + 0x21)) {
            return (uint32_t)((uint8_t)v0 ^
                              ((((uint8_t)((a0 + 0x21) >> (i + 1))) & 0x0f) | ((uint8_t)i << 4)));
        }
        i += 1;
    } while (i != 8);

    return (uint32_t)((uint8_t)v0 ^ 0x7f);
}

int32_t g711a_decode(int16_t *arg1, uint8_t *arg2, int32_t arg3)
{
    uint8_t *t2 = &arg2[arg3];

    if (arg3 <= 0) {
        return 0;
    }

    while (1) {
        int32_t v1_3 = (int32_t)(*arg2) ^ 0x55;
        int32_t a3_3 = ((uint32_t)v1_3 >> 4) & 7;
        uint32_t t0_3 = (uint32_t)((uint8_t)(v1_3 << 4));

        if (a3_3 != 0) {
            int32_t t0_1 = (int32_t)t0_3 + 0x108;
            if (a3_3 != 1) {
                t0_1 <<= (a3_3 - 1) & 0x1f;
            }

            {
                int16_t v1_1 = (int16_t)t0_1;
                if (((uint32_t)v1_3 >> 7) == 0) {
                    v1_1 = (int16_t)(-t0_1);
                }
                arg2 = &arg2[1];
                *arg1 = v1_1;
                arg1 = &arg1[1];
                if (arg2 == t2) {
                    break;
                }
            }
        } else {
            int16_t v1_4 = (int16_t)(t0_3 + 8);
            if (((uint32_t)v1_3 >> 7) == 0) {
                v1_4 = (int16_t)(-(int32_t)(t0_3 + 8));
            }
            arg2 = &arg2[1];
            *arg1 = v1_4;
            arg1 = &arg1[1];
            if (arg2 == t2) {
                break;
            }
        }
    }

    return arg3 << 1;
}

int32_t g711u_decode(int16_t *arg1, uint8_t *arg2, int32_t arg3)
{
    uint8_t *end;

    if (arg3 <= 0) {
        return 0;
    }

    end = &arg2[arg3];
    do {
        uint32_t v1_1 = (uint32_t)(*arg2);
        uint32_t v1_3;
        int16_t t0_4;
        int16_t v1_4;

        arg1 += 1;
        arg2 = &arg2[1];
        v1_3 = (uint32_t)(uint8_t)(~v1_1);
        t0_4 = (int16_t)(((((int32_t)(v1_3 & 0x0f) << 3) + 0x84) << (((uint32_t)v1_3 >> 4) & 7)));
        v1_4 = (int16_t)(0x84 - t0_4);
        if ((int8_t)v1_3 >= 0) {
            v1_4 = (int16_t)(t0_4 - 0x84);
        }
        *(arg1 - 1) = v1_4;
    } while (arg2 != end);

    return arg3 << 1;
}

int32_t g711a_encode(uint8_t *arg1, int16_t *arg2, int32_t arg3)
{
    if (arg3 > 0) {
        uint8_t *i = arg1;
        int16_t *s1_1 = arg2;

        do {
            int32_t a0 = (int32_t)(*s1_1);
            i += 1;
            s1_1 = &s1_1[1];
            *(i - 1) = (uint8_t)linear2alaw(a0);
        } while (i != arg1 + arg3);
    }

    return arg3;
}

int32_t g711u_encode(uint8_t *arg1, int16_t *arg2, int32_t arg3)
{
    if (arg3 > 0) {
        uint8_t *i = arg1;
        int16_t *s1_1 = arg2;

        do {
            int32_t a0 = (int32_t)(*s1_1);
            i += 1;
            s1_1 = &s1_1[1];
            *(i - 1) = (uint8_t)linear2ulaw(a0);
        } while (i != arg1 + arg3);
    }

    return arg3;
}

int32_t *bitstream_init(int32_t *arg1)
{
    if (arg1 != 0) {
        *arg1 = 0;
        arg1[1] = 0;
    }
    return arg1;
}

int32_t quantize(int32_t arg1, int32_t arg2, const int32_t *arg3, int32_t arg4)
{
    int16_t v1_9 = (int16_t)(arg1 >> 0x1f);
    int32_t v1_1 = (int32_t)(int16_t)(((uint16_t)v1_9 ^ (uint16_t)arg1) - (uint16_t)v1_9);
    int32_t v0_1 = v1_1 >> 1;
    uint16_t v0_4;
    uint32_t t1_2;

    if (v0_1 == 0) {
        v0_4 = 0;
        t1_2 = 0;
    } else {
        int32_t t0_1 = v0_1 & 0xffff0000;
        int32_t t1 = 0x10;
        int32_t v0_2;

        if (t0_1 == 0) {
            t0_1 = v0_1;
            v0_2 = t0_1 & 0xff00ff00;
            t1 = 0;
            if (v0_2 != 0) {
                t1 = 8;
            }
        } else {
            v0_2 = t0_1 & 0xff00ff00;
            if (v0_2 != 0) {
                t1 = 0x18;
            }
        }

        t0_1 = v0_2;

        {
            int32_t t2_1 = t0_1 & 0xf0f0f0f0;
            if (t2_1 != 0) {
                t1 += 4;
                t0_1 = t2_1;
            }

            {
                int32_t t2_2 = t0_1 & 0xcccccccc;
                if (t2_2 != 0) {
                    t1 += 2;
                    if ((t2_2 & 0xaaaaaaaa) != 0) {
                        t1_2 = (uint32_t)(uint16_t)(t1 + 2);
                        v0_4 = (uint16_t)(t1_2 << 7);
                    } else {
                        t1_2 = (uint32_t)(uint16_t)(t1 + 1);
                        v0_4 = (uint16_t)(t1_2 << 7);
                    }
                } else if ((t0_1 & 0xaaaaaaaa) == 0) {
                    t1_2 = (uint32_t)(uint16_t)(t1 + 1);
                    v0_4 = (uint16_t)(t1_2 << 7);
                } else {
                    t1_2 = (uint32_t)(uint16_t)(t1 + 2);
                    v0_4 = (uint16_t)(t1_2 << 7);
                }
            }
        }
    }

    {
        int32_t t0_4 = (arg4 - 1) >> 1;
        int32_t i = (int32_t)(int16_t)((((int16_t)((v1_1 << 7) >> (t1_2 & 0x1f)) & 0x7f) + v0_4) -
                                       ((int16_t)(arg2 >> 2)));

        if (t0_4 > 0) {
            const int32_t *a2 = &arg3[1];

            if (i >= arg3[0]) {
                int32_t v0_7 = 0;

                do {
                    v0_7 += 1;
                    a2 += 1;
                    if (t0_4 == v0_7) {
                        v0_7 = t0_4;
                        break;
                    }
                } while (i >= *(a2 - 1));

                if (arg1 >= 0) {
                    return (int32_t)(int16_t)v0_7;
                }
                return (int32_t)(int16_t)(((t0_4 << 1) + 1) - (uint16_t)v0_7);
            }
        }

        if (arg1 >= 0) {
            if ((arg4 & 1) != 0) {
                return (int32_t)(int16_t)arg4;
            }
        }
    }

    return 0;
}

int32_t fmult(int16_t arg1, int16_t arg2)
{
    int32_t a0_2 = (int32_t)arg1;
    int32_t a1 = (int32_t)arg2;
    int16_t v0_3;
    int32_t v0_7;
    int32_t v1;
    int32_t v1_5;
    int32_t a2_2;
    int32_t t0;

    if (a0_2 <= 0) {
        t0 = (-a0_2) & 0x1fff;
        v1 = t0;
        if (t0 == 0) {
            a2_2 = 0x20;
            v0_3 = -6;
            goto label_b4138;
        }
    } else {
        v1 = a0_2;
        t0 = a0_2;
    }

    {
        int32_t v0 = v1 & 0xff00ff00;
        int32_t a2;
        int32_t a3_1;
        int32_t v0_1;
        int32_t a3_2;
        int32_t a2_1;

        if (v0 != 0) {
            v1 = v0;
        }
        a2 = 8;
        if (v0 == 0) {
            a2 = 0;
        }
        a3_1 = v1 & 0xf0f0f0f0;
        v0_1 = a2;
        if (a3_1 != 0) {
            v0_1 = a2 + 4;
            v1 = a3_1;
        }
        a3_2 = v1 & 0xcccccccc;
        if (a3_2 != 0) {
            v0_1 += 2;
            if ((a3_2 & 0xaaaaaaaa) != 0) {
                v0_3 = (int16_t)(v0_1 - 4);
                a2_1 = (int32_t)v0_3;
                if (a2_1 >= 0) {
                    a2_2 = t0 >> (a2_1 & 0x1f);
                    goto label_b4138;
                }
                v1_5 = (int32_t)((int16_t)(v0_3 + (((uint32_t)a1 >> 6) & 0xf) - 0xd));
                v0_7 = (int32_t)(int16_t)(((((a1 & 0x3f) * (int32_t)(int16_t)(t0 << ((-a2_1) & 0x1f))) + 0x30) >> 4));
                if (v1_5 >= 0) {
                    goto label_b4158;
                }
                goto label_b41cc;
            }
            v0_3 = (int16_t)(v0_1 - 5);
            a2_1 = (int32_t)v0_3;
            if (a2_1 >= 0) {
                a2_2 = t0 >> (a2_1 & 0x1f);
                goto label_b4138;
            }
            v1_5 = (int32_t)((int16_t)(v0_3 + (((uint32_t)a1 >> 6) & 0xf) - 0xd));
            v0_7 = (int32_t)(int16_t)(((((a1 & 0x3f) * (int32_t)(int16_t)(t0 << ((-a2_1) & 0x1f))) + 0x30) >> 4));
            if (v1_5 >= 0) {
                goto label_b4158;
            }
            goto label_b41cc;
        }

        if ((v1 & 0xaaaaaaaa) == 0) {
            v0_3 = (int16_t)(v0_1 - 5);
            a2_1 = (int32_t)v0_3;
            if (a2_1 >= 0) {
                a2_2 = t0 >> (a2_1 & 0x1f);
                goto label_b4138;
            }
            v1_5 = (int32_t)((int16_t)(v0_3 + (((uint32_t)a1 >> 6) & 0xf) - 0xd));
            v0_7 = (int32_t)(int16_t)(((((a1 & 0x3f) * (int32_t)(int16_t)(t0 << ((-a2_1) & 0x1f))) + 0x30) >> 4));
            if (v1_5 >= 0) {
                goto label_b4158;
            }
            goto label_b41cc;
        }

        v0_3 = (int16_t)(v0_1 - 4);
        a2_1 = (int32_t)v0_3;
        if (a2_1 >= 0) {
            a2_2 = t0 >> (a2_1 & 0x1f);
            goto label_b4138;
        }
        v1_5 = (int32_t)((int16_t)(v0_3 + (((uint32_t)a1 >> 6) & 0xf) - 0xd));
        v0_7 = (int32_t)(int16_t)(((((a1 & 0x3f) * (int32_t)(int16_t)(t0 << ((-a2_1) & 0x1f))) + 0x30) >> 4));
        if (v1_5 >= 0) {
            goto label_b4158;
        }
        goto label_b41cc;
    }

label_b4138:
    v1_5 = (int32_t)((int16_t)(v0_3 + (((uint32_t)a1 >> 6) & 0xf) - 0xd));
    v0_7 = (int32_t)(int16_t)(((((a1 & 0x3f) * a2_2) + 0x30) >> 4));
    if (v1_5 < 0) {
        goto label_b41cc;
    }

label_b4158:
    {
        int32_t result = (v0_7 << (v1_5 & 0x1f)) & 0x7fff;
        if ((a0_2 ^ a1) < 0) {
            return (int32_t)(int16_t)(-result);
        }
        return result;
    }

label_b41cc:
    {
        int32_t result = v0_7 >> ((-v1_5) & 0x1f);
        if ((a0_2 ^ a1) < 0) {
            return (int32_t)(int16_t)(-result);
        }
        return (int32_t)(int16_t)result;
    }
}

int32_t step_size(G726State *arg1)
{
    int32_t a1_1 = (int32_t)arg1->ap;

    if (a1_1 >= 0x100) {
        return (int32_t)arg1->yu;
    }

    {
        int32_t result = arg1->yl >> 6;
        int32_t v1_1 = (int32_t)arg1->yu - result;
        int32_t a1 = a1_1 >> 2;

        if (v1_1 > 0) {
            return ((v1_1 * a1) >> 6) + result;
        }
        if (v1_1 != 0) {
            return ((v1_1 * a1 + 0x3f) >> 6) + result;
        }
        return result;
    }
}

int32_t reconstruct(int32_t arg1, int32_t arg2, int32_t arg3)
{
    uint32_t a1_1 = (uint32_t)((int16_t)(arg3 >> 2) + arg2);
    int32_t v1 = (int32_t)(int16_t)a1_1;

    if (v1 < 0) {
        if (arg1 == 0) {
            return 0;
        }
        return (int32_t)0xffff8000u;
    }

    {
        int16_t a1_5 = (int16_t)(((((a1_1 & 0x7f) + 0x80) << 7) >> ((0xe - (((uint32_t)v1 >> 7) & 0xf)) & 0x1f)));
        if (arg1 == 0) {
            return (int32_t)a1_5;
        }
        return (int32_t)(int16_t)(a1_5 - 0x8000);
    }
}

static int32_t update(G726State *arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7)
{
    int32_t t5_11 = arg1->yl;
    int32_t t7 = (int32_t)(int16_t)(t5_11 >> 0xf);
    int32_t t3 = arg5 & 0x7fff;
    uint32_t t4 = (uint32_t)arg7 >> 0x1f;
    int32_t t0 = (int32_t)(int16_t)t3;
    int32_t v1_1 = 0x5d00;
    int32_t v0_7;
    int32_t v1_5;
    int16_t *v0_10;
    int16_t *t3_1;
    int32_t t5;
    int32_t pk0;
    int32_t v1_10;
    int32_t t0_1;
    int32_t v1_12;
    int32_t v0_19;

    if (t7 < 0xa) {
        int32_t v1_3 = (int32_t)(int16_t)(((((t5_11 >> 0xa) & 0x1f) + 0x20) << (t7 & 0x1f)));
        v1_1 = ((v1_3 >> 1) + v1_3) >> 1;
    }

    v0_7 = (int32_t)(int16_t)(((arg3 - arg2) >> 5) + (int16_t)arg2);
    v1_5 = (v1_1 < t0) ? 1 : 0;
    if (arg1->td == 0) {
        v1_5 = 0;
    }

    if (v0_7 < 0x220) {
        arg1->yu = 0x220;
        v0_7 = 0x220;
    } else if (v0_7 < 0x1401) {
        arg1->yu = (int16_t)v0_7;
    } else {
        arg1->yu = 0x1400;
        v0_7 = 0x1400;
    }

    arg1->yl = ((-t5_11) >> 6) + v0_7 + t5_11;

    if (v1_5 == 0) {
        int32_t v0_25 = (int32_t)arg1->a[1];
        int32_t t5_2 = (v0_25 - (v0_25 >> 7)) & 0xff80;
        int32_t v0_28;
        int32_t v0_29;
        int32_t v1_21;
        int32_t t6_2;
        int32_t t9_1;

        if (arg7 != 0) {
            int32_t t7_6 = (int32_t)(int16_t)((uint16_t)t4 ^ (uint16_t)arg1->pk[0]);
            int32_t v0_47;
            int32_t v1_31;
            int32_t t8_2;
            int32_t v1_29;
            int32_t t6_9;

            if (t7_6 != 0) {
                v1_29 = (int32_t)arg1->a[0];
                v0_47 = v1_29 & 0xffff;
                t8_2 = v1_29;
                if (v1_29 < -0x1fff) {
                    v1_31 = (int32_t)(int16_t)(t5_2 - 0x100);
                } else {
                    t6_9 = (v1_29 < 0x2000) ? 1 : 0;
label_a4800:
                    if (t6_9 != 0) {
                        v1_31 = (int32_t)(int16_t)(t5_2 + (v1_29 >> 5));
                    } else {
                        v1_31 = (int32_t)(int16_t)(t5_2 + 0xff);
                    }
                }
            } else {
                t8_2 = (int32_t)arg1->a[0];
                v0_47 = t8_2 & 0xffff;
                v1_29 = (int32_t)(int16_t)(-v0_47);
                t6_9 = (v1_29 < 0x2000) ? 1 : 0;
                if (v1_29 < -0x1fff) {
                    v1_31 = (int32_t)(int16_t)(t5_2 - 0x100);
                } else {
                    goto label_a4800;
                }
            }

            if ((int32_t)arg1->pk[1] != (int32_t)(int16_t)t4) {
                if (v1_31 < -0x2f7f) {
                    t5 = -0x3000;
                    v0_28 = (int32_t)(int16_t)(((v0_47 & 0xffff) - (t8_2 >> 8)));
                    t6_2 = -0x6c00;
                    t9_1 = 0x6c00;
                    v1_21 = 0x6c00;
                    arg1->a[1] = (int16_t)0xd000;
                    arg1->a[0] = (int16_t)v0_28;
                    if (t7_6 != 0) {
                        v0_28 = (int32_t)(int16_t)(v0_28 - 0xc0);
                        arg1->a[0] = (int16_t)v0_28;
                    } else {
                        int32_t v0_51 = (int32_t)(int16_t)(v0_28 + 0xc0);
                        arg1->a[0] = (int16_t)v0_51;
                        if (v0_51 < t6_2) {
                            arg1->a[0] = (int16_t)(-v1_21);
                        } else {
                            v0_29 = (t9_1 < v0_51) ? 1 : 0;
                            if (v0_29 != 0) {
                                arg1->a[0] = (int16_t)t9_1;
                            }
                        }
                    }
                    goto label_after_a_update;
                }
                if (v1_31 >= 0x3080) {
                    t6_2 = -0xc00;
                    t9_1 = 0xc00;
                    v1_21 = 0xc00;
                    t5 = 0x3000;
                } else {
                    int32_t t5_9 = (v1_31 - 0x80) & 0xffff;
                    v1_21 = (0x3c00 - t5_9) & 0xffff;
                    t5 = (int32_t)(int16_t)t5_9;
                    t9_1 = (int32_t)(int16_t)v1_21;
                    t6_2 = -v1_21;
                }
                v0_28 = (int32_t)(int16_t)(((v0_47 & 0xffff) - (t8_2 >> 8)));
                arg1->a[1] = (int16_t)t5;
                arg1->a[0] = (int16_t)v0_28;
                if (t7_6 != 0) {
                    v0_28 = (int32_t)(int16_t)(v0_28 - 0xc0);
                    arg1->a[0] = (int16_t)v0_28;
                } else {
                    int32_t v0_51 = (int32_t)(int16_t)(v0_28 + 0xc0);
                    arg1->a[0] = (int16_t)v0_51;
                    if (v0_51 < t6_2) {
                        arg1->a[0] = (int16_t)(-v1_21);
                    } else {
                        v0_29 = (t9_1 < v0_51) ? 1 : 0;
                        if (v0_29 != 0) {
                            arg1->a[0] = (int16_t)t9_1;
                        }
                    }
                }
            } else {
                int32_t t5_9;

                if (v1_31 >= -0x307f) {
                    if (v1_31 >= 0x2f80) {
                        t6_2 = -0xc00;
                        t9_1 = 0xc00;
                        v1_21 = 0xc00;
                        t5 = 0x3000;
                    } else {
                        t5_9 = (v1_31 + 0x80) & 0xffff;
                        v1_21 = (0x3c00 - t5_9) & 0xffff;
                        t5 = (int32_t)(int16_t)t5_9;
                        t9_1 = (int32_t)(int16_t)v1_21;
                        t6_2 = -v1_21;
                    }
                } else {
                    t5 = -0x3000;
                    t6_2 = -0xc00;
                    t9_1 = 0xc00;
                    v1_21 = 0xc00;
                }

                v0_28 = (int32_t)(int16_t)(((v0_47 & 0xffff) - (t8_2 >> 8)));
                arg1->a[1] = (int16_t)t5;
                arg1->a[0] = (int16_t)v0_28;
                if (t7_6 != 0) {
                    v0_28 = (int32_t)(int16_t)(v0_28 - 0xc0);
                    arg1->a[0] = (int16_t)v0_28;
                } else {
                    int32_t v0_51 = (int32_t)(int16_t)(v0_28 + 0xc0);
                    arg1->a[0] = (int16_t)v0_51;
                    if (v0_51 < t6_2) {
                        arg1->a[0] = (int16_t)(-v1_21);
                    } else {
                        v0_29 = (t9_1 < v0_51) ? 1 : 0;
                        if (v0_29 != 0) {
                            arg1->a[0] = (int16_t)t9_1;
                        }
                    }
                }
            }
        } else {
            int32_t v0_26 = (int32_t)arg1->a[0];

            v1_21 = (0x3c00 - t5_2) & 0xffff;
            t5 = (int32_t)(int16_t)t5_2;
            v0_28 = (int32_t)(int16_t)(v0_26 - (v0_26 >> 8));
            t9_1 = (int32_t)(int16_t)v1_21;
            arg1->a[1] = (int16_t)t5;
            arg1->a[0] = (int16_t)v0_28;
            t6_2 = -t9_1;
            v0_29 = (t9_1 < v0_28) ? 1 : 0;
            if (v0_28 < t6_2) {
                arg1->a[0] = (int16_t)(-v1_21);
            } else if (v0_29 != 0) {
                arg1->a[0] = (int16_t)t9_1;
            }
        }

label_after_a_update:
        {
            int32_t t7_3 = 9;
            int16_t *i;

            if (arg1->code_size != 5) {
                t7_3 = 8;
            }
            i = &arg1->b[0];
            do {
                int32_t v0_31 = (int32_t)(*i);
                int16_t v0_33 = (int16_t)(v0_31 - (v0_31 >> (t7_3 & 0x1f)));
                *i = v0_33;
                if (t3 != 0) {
                    if ((((int32_t)i[8]) ^ arg5) < 0) {
                        *i = (int16_t)(v0_33 - 0x80);
                    } else {
                        *i = (int16_t)(v0_33 + 0x80);
                    }
                }
                i = &i[1];
                v0_10 = &arg1->dq[4];
            } while (&arg1->pk[0] != i);
            t3_1 = &arg1->pk[1];
        }
    } else {
        arg1->a[0] = 0;
        arg1->a[1] = 0;
        arg1->b[0] = 0;
        arg1->b[1] = 0;
        arg1->b[2] = 0;
        arg1->b[3] = 0;
        arg1->b[4] = 0;
        arg1->b[5] = 0;
        t5 = 0;
        v0_10 = &arg1->dq[4];
        t3_1 = &arg1->pk[1];
    }

    do {
        int16_t v1_8 = *v0_10;
        v0_10 = &v0_10[-1];
        v0_10[2] = v1_8;
    } while (t3_1 != v0_10);

    {
        int32_t v1_9 = t0 & 0x7f00;
        if (t0 != 0) {
            int32_t v0_40 = 8;

            if (v1_9 == 0) {
                v1_9 = t0;
                v0_40 = 0;
            }

            {
                int32_t t3_2 = v1_9 & 0xf0f0f0f0;
                if (t3_2 != 0) {
                    v0_40 += 4;
                    v1_9 = t3_2;
                }
            }

            {
                int32_t t3_3 = v1_9 & 0xcccccccc;
                if (t3_3 != 0) {
                    v0_40 += 2;
                    v1_9 = t3_3;
                }
            }

            {
                int32_t v0_43 = (v0_40 + (((uint32_t)(v1_9 & 0xaaaaaaaa) != 0) ? 1 : 0) + 1) & 0xffff;
                int32_t t0_5 = t0 << 6;
                if (arg5 < 0) {
                    arg1->dq[0] = (int16_t)(((t0_5 >> (v0_43 & 0x1f)) + (v0_43 << 6)) - 0x400);
                } else {
                    arg1->dq[0] = (int16_t)((t0_5 >> (v0_43 & 0x1f)) + (v0_43 << 6));
                }
            }
        } else {
            int16_t t1_1 = (int16_t)-0x3e0;
            if (arg5 >= 0) {
                t1_1 = 0x20;
            }
            arg1->dq[0] = t1_1;
        }
    }

    arg1->sr[1] = arg1->sr[0];
    if (arg6 == 0) {
        arg1->sr[0] = 0x20;
    } else if (arg6 > 0) {
        int32_t v1_24 = arg6 & 0xffff0000;
        int32_t v0_34 = 0x10;
        int32_t t0_2;

        if (v1_24 == 0) {
            v1_24 = arg6;
            t0_2 = v1_24 & 0xff00ff00;
            v0_34 = 0;
            if (t0_2 != 0) {
                v0_34 = 8;
            }
        } else {
            t0_2 = v1_24 & 0xff00ff00;
            if (t0_2 != 0) {
                v0_34 = 0x18;
            }
        }

        v1_24 = t0_2;
        {
            int32_t t0_3 = v1_24 & 0xf0f0f0f0;
            if (t0_3 != 0) {
                v0_34 += 4;
                v1_24 = t0_3;
            }
        }
        {
            int32_t t0_4 = v1_24 & 0xcccccccc;
            if (t0_4 != 0) {
                v0_34 += 2;
                v1_24 = t0_4;
            }
        }
        {
            int32_t v0_37 = (v0_34 + (((uint32_t)(v1_24 & 0xaaaaaaaa) != 0) ? 1 : 0) + 1) & 0xffff;
            arg1->sr[0] = (int16_t)((arg6 << 6 >> (v0_37 & 0x1f)) + (v0_37 << 6));
        }
    } else if (arg6 < -0x7fff) {
        arg1->sr[0] = (int16_t)0xfc20;
    } else {
        int32_t v1_34 = (-arg6) & 0xffff;
        int32_t v0_57 = v1_34 & 0xff00;
        int32_t t0_7 = v0_57;
        int32_t t2_3 = 8;
        int32_t t1_2;
        int32_t v0_58;
        int32_t t1_3;
        int32_t v0_61;

        if (v0_57 == 0) {
            t0_7 = v1_34;
            t2_3 = 0;
        }
        t1_2 = t0_7 & 0xf0f0f0f0;
        v0_58 = t2_3;
        if (t1_2 != 0) {
            v0_58 = t2_3 + 4;
            t0_7 = t1_2;
        }
        t1_3 = t0_7 & 0xcccccccc;
        if (t1_3 != 0) {
            v0_58 += 2;
            t0_7 = t1_3;
        }
        v0_61 = (v0_58 + (((uint32_t)(t0_7 & 0xaaaaaaaa) != 0) ? 1 : 0) + 1) & 0xffff;
        arg1->sr[0] = (int16_t)(((v1_34 << 6 >> (v0_61 & 0x1f)) + (v0_61 << 6)) - 0x400);
    }

    pk0 = (int32_t)(int16_t)t4;
    t5 = 0;
    {
        int16_t v0_13 = arg1->pk[0];
        arg1->pk[0] = (int16_t)pk0;
        arg1->pk[1] = v0_13;
    }

    if (v1_5 != 0 || t5 < -0x2e00) {
        arg1->td = 0;
    } else {
        arg1->td = 1;
    }

    v1_10 = (int32_t)arg1->dms;
    t0_1 = (int32_t)arg1->dml;
    v1_12 = (int32_t)(int16_t)(((int16_t)arg4 - v1_10) >> 5) + v1_10;
    v0_19 = (int32_t)(int16_t)((((int16_t)(arg4 << 2) - t0_1) >> 7) + t0_1);
    arg1->dms = (int16_t)v1_12;
    arg1->dml = (int16_t)v0_19;

    if (v1_5 != 0) {
        arg1->ap = 0x100;
        return 0x100;
    }

    if (arg2 >= 0x600 && arg1->td == 0) {
        int32_t v1_14 = (v1_12 << 2) - v0_19;
        int32_t a1_2 = v1_14 >> 0x1f;
        if ((((a1_2 ^ v1_14) - a1_2) < (v0_19 >> 3))) {
            int32_t v1_17 = (int32_t)arg1->ap;
            int16_t v0_24 = (int16_t)(((-v1_17) >> 4) + v1_17);
            arg1->ap = v0_24;
            return v0_24;
        }
    }

    {
        int32_t v1_32 = (int32_t)arg1->ap;
        int16_t v0_54 = (int16_t)(((0x200 - v1_32) >> 4) + v1_32);
        arg1->ap = v0_54;
        return v0_54;
    }
}

int32_t g726_16_decoder(G726State *arg1, char arg2)
{
    uint32_t s4 = (uint32_t)(uint8_t)arg2;
    int32_t s1 = fmult((int16_t)((int32_t)arg1->b[0] >> 2), arg1->dq[0]);
    int16_t *i = &arg1->b[1];

    do {
        int32_t a1_1 = (int32_t)i[8];
        int32_t a0_3 = (int32_t)(*i) >> 2;
        i = &i[1];
        s1 += fmult((int16_t)a0_3, (int16_t)a1_1);
    } while (&arg1->pk[0] != i);

    {
        int32_t s5_1 = ((int32_t)(s4 & 3)) << 2;
        int16_t v0_2 = (int16_t)fmult((int16_t)((int32_t)arg1->a[1] >> 2), arg1->sr[1]);
        int16_t v0_3 = (int16_t)fmult((int16_t)((int32_t)arg1->a[0] >> 2), arg1->sr[0]);
        int32_t v0_4 = step_size(arg1);
        int32_t v0_6 = reconstruct((int32_t)(s4 & 2), g726_16_dqlntab[s5_1 >> 2], v0_4);
        int16_t v1_3 = (int16_t)((v0_2 + v0_3 + (int16_t)s1) >> 1);
        int32_t s0_2;

        if (v0_6 < 0) {
            s0_2 = (int32_t)(int16_t)(v1_3 - (v0_6 & 0x3fff));
        } else {
            s0_2 = (int32_t)(int16_t)(v1_3 + v0_6);
        }

        update(arg1, v0_4, g726_16_witab[s5_1 >> 2], g726_16_fitab[s5_1 >> 2], v0_6, s0_2,
               (int32_t)(int16_t)(((int16_t)(s1 >> 1)) + s0_2 - v1_3));
        return (int32_t)(int16_t)(s0_2 << 2);
    }
}

int32_t g726_16_encoder(G726State *arg1, int32_t arg2)
{
    int16_t *i = &arg1->b[1];
    int32_t s1 = fmult((int16_t)((int32_t)arg1->b[0] >> 2), arg1->dq[0]);

    do {
        int32_t a1_1 = (int32_t)i[8];
        int32_t a0_3 = (int32_t)(*i) >> 2;
        i = &i[1];
        s1 += fmult((int16_t)a0_3, (int16_t)a1_1);
    } while (&arg1->pk[0] != i);

    {
        int32_t s1_2 = (int32_t)(int16_t)(fmult((int16_t)((int32_t)arg1->a[1] >> 2), arg1->sr[1]) +
                                           fmult((int16_t)((int32_t)arg1->a[0] >> 2), arg1->sr[0]) +
                                           (int16_t)s1);
        int32_t v0_5 = step_size(arg1);
        int16_t s1_3 = (int16_t)(((uint32_t)s1_2 >> 1) & 0xffff);
        int32_t v0_6 = quantize((int32_t)(int16_t)(arg2 - s1_3), v0_5, qtab_726_16, 4);
        int32_t s5 = v0_6 << 2;
        int32_t v0_8 = reconstruct(v0_6 & 2, g726_16_dqlntab[s5 >> 2], v0_5);
        int32_t a1_7;

        if (v0_8 < 0) {
            a1_7 = (int32_t)(int16_t)(s1_3 - (v0_8 & 0x3fff));
        } else {
            a1_7 = (int32_t)(int16_t)(s1_3 + v0_8);
        }

        update(arg1, v0_5, g726_16_witab[s5 >> 2], g726_16_fitab[s5 >> 2], v0_8, a1_7,
               (int32_t)(int16_t)(((int16_t)(s1 >> 1)) - s1_3 + a1_7));
        return v0_6 & 0xff;
    }
}

int32_t g726_24_decoder(G726State *arg1, char arg2)
{
    uint32_t s4 = (uint32_t)(uint8_t)arg2;
    int32_t s1 = fmult((int16_t)((int32_t)arg1->b[0] >> 2), arg1->dq[0]);
    int16_t *i = &arg1->b[1];

    do {
        int32_t a1_1 = (int32_t)i[8];
        int32_t a0_3 = (int32_t)(*i) >> 2;
        i = &i[1];
        s1 += fmult((int16_t)a0_3, (int16_t)a1_1);
    } while (&arg1->pk[0] != i);

    {
        int32_t s5_1 = ((int32_t)(s4 & 7)) << 2;
        int16_t v0_2 = (int16_t)fmult((int16_t)((int32_t)arg1->a[1] >> 2), arg1->sr[1]);
        int16_t v0_3 = (int16_t)fmult((int16_t)((int32_t)arg1->a[0] >> 2), arg1->sr[0]);
        int32_t v0_4 = step_size(arg1);
        int32_t v0_6 = reconstruct((int32_t)(s4 & 4), g726_24_dqlntab[s5_1 >> 2], v0_4);
        int16_t v1_3 = (int16_t)((v0_2 + v0_3 + (int16_t)s1) >> 1);
        int32_t s0_2;

        if (v0_6 < 0) {
            s0_2 = (int32_t)(int16_t)(v1_3 - (v0_6 & 0x3fff));
        } else {
            s0_2 = (int32_t)(int16_t)(v1_3 + v0_6);
        }

        update(arg1, v0_4, g726_24_witab[s5_1 >> 2], g726_24_fitab[s5_1 >> 2], v0_6, s0_2,
               (int32_t)(int16_t)(((int16_t)(s1 >> 1)) + s0_2 - v1_3));
        return (int32_t)(int16_t)(s0_2 << 2);
    }
}

int32_t g726_24_encoder(G726State *arg1, int32_t arg2)
{
    int16_t *i = &arg1->b[1];
    int32_t s1 = fmult((int16_t)((int32_t)arg1->b[0] >> 2), arg1->dq[0]);

    do {
        int32_t a1_1 = (int32_t)i[8];
        int32_t a0_3 = (int32_t)(*i) >> 2;
        i = &i[1];
        s1 += fmult((int16_t)a0_3, (int16_t)a1_1);
    } while (&arg1->pk[0] != i);

    {
        int32_t s1_2 = (int32_t)(int16_t)(fmult((int16_t)((int32_t)arg1->a[1] >> 2), arg1->sr[1]) +
                                           fmult((int16_t)((int32_t)arg1->a[0] >> 2), arg1->sr[0]) +
                                           (int16_t)s1);
        int32_t v0_5 = step_size(arg1);
        int16_t s1_3 = (int16_t)(((uint32_t)s1_2 >> 1) & 0xffff);
        int32_t v0_6 = quantize((int32_t)(int16_t)(arg2 - s1_3), v0_5, qtab_726_24, 7);
        int32_t s5 = v0_6 << 2;
        int32_t v0_8 = reconstruct(v0_6 & 4, g726_24_dqlntab[s5 >> 2], v0_5);
        int32_t a1_7;

        if (v0_8 < 0) {
            a1_7 = (int32_t)(int16_t)(s1_3 - (v0_8 & 0x3fff));
        } else {
            a1_7 = (int32_t)(int16_t)(s1_3 + v0_8);
        }

        update(arg1, v0_5, g726_24_witab[s5 >> 2], g726_24_fitab[s5 >> 2], v0_8, a1_7,
               (int32_t)(int16_t)(((int16_t)(s1 >> 1)) - s1_3 + a1_7));
        return v0_6 & 0xff;
    }
}

int32_t g726_32_decoder(G726State *arg1, char arg2)
{
    uint32_t s4 = (uint32_t)(uint8_t)arg2;
    int32_t s1 = fmult((int16_t)((int32_t)arg1->b[0] >> 2), arg1->dq[0]);
    int16_t *i = &arg1->b[1];

    do {
        int32_t a1_1 = (int32_t)i[8];
        int32_t a0_3 = (int32_t)(*i) >> 2;
        i = &i[1];
        s1 += fmult((int16_t)a0_3, (int16_t)a1_1);
    } while (&arg1->pk[0] != i);

    {
        int32_t s5_1 = ((int32_t)(s4 & 0x0f)) << 2;
        int16_t v0_2 = (int16_t)fmult((int16_t)((int32_t)arg1->a[1] >> 2), arg1->sr[1]);
        int16_t v0_3 = (int16_t)fmult((int16_t)((int32_t)arg1->a[0] >> 2), arg1->sr[0]);
        int32_t v0_4 = step_size(arg1);
        int32_t v0_6 = reconstruct((int32_t)(s4 & 8), g726_32_dqlntab[s5_1 >> 2], v0_4);
        int16_t v1_3 = (int16_t)((v0_2 + v0_3 + (int16_t)s1) >> 1);
        int32_t s0_2;

        if (v0_6 < 0) {
            s0_2 = (int32_t)(int16_t)(v1_3 - (v0_6 & 0x3fff));
        } else {
            s0_2 = (int32_t)(int16_t)(v1_3 + v0_6);
        }

        update(arg1, v0_4, g726_32_witab[s5_1 >> 2], g726_32_fitab[s5_1 >> 2], v0_6, s0_2,
               (int32_t)(int16_t)(((int16_t)(s1 >> 1)) + s0_2 - v1_3));
        return (int32_t)(int16_t)(s0_2 << 2);
    }
}

int32_t g726_32_encoder(G726State *arg1, int32_t arg2)
{
    int16_t *i = &arg1->b[1];
    int32_t s1 = fmult((int16_t)((int32_t)arg1->b[0] >> 2), arg1->dq[0]);

    do {
        int32_t a1_1 = (int32_t)i[8];
        int32_t a0_3 = (int32_t)(*i) >> 2;
        i = &i[1];
        s1 += fmult((int16_t)a0_3, (int16_t)a1_1);
    } while (&arg1->pk[0] != i);

    {
        int32_t s1_2 = (int32_t)(int16_t)(fmult((int16_t)((int32_t)arg1->a[1] >> 2), arg1->sr[1]) +
                                           fmult((int16_t)((int32_t)arg1->a[0] >> 2), arg1->sr[0]) +
                                           (int16_t)s1);
        int32_t v0_5 = step_size(arg1);
        int16_t s1_3 = (int16_t)(((uint32_t)s1_2 >> 1) & 0xffff);
        int32_t v0_6 = quantize((int32_t)(int16_t)(arg2 - s1_3), v0_5, qtab_726_32, 0x0f);
        int32_t s5 = v0_6 << 2;
        int32_t v0_8 = reconstruct(v0_6 & 8, g726_32_dqlntab[s5 >> 2], v0_5);
        int32_t a1_7;

        if (v0_8 < 0) {
            a1_7 = (int32_t)(int16_t)(s1_3 - (v0_8 & 0x3fff));
        } else {
            a1_7 = (int32_t)(int16_t)(s1_3 + v0_8);
        }

        update(arg1, v0_5, g726_32_witab[s5 >> 2], g726_32_fitab[s5 >> 2], v0_8, a1_7,
               (int32_t)(int16_t)(((int16_t)(s1 >> 1)) - s1_3 + a1_7));
        return v0_6 & 0xff;
    }
}

int32_t g726_40_decoder(G726State *arg1, char arg2)
{
    uint32_t s4 = (uint32_t)(uint8_t)arg2;
    int32_t s1 = fmult((int16_t)((int32_t)arg1->b[0] >> 2), arg1->dq[0]);
    int16_t *i = &arg1->b[1];

    do {
        int32_t a1_1 = (int32_t)i[8];
        int32_t a0_3 = (int32_t)(*i) >> 2;
        i = &i[1];
        s1 += fmult((int16_t)a0_3, (int16_t)a1_1);
    } while (&arg1->pk[0] != i);

    {
        int32_t s5_1 = ((int32_t)(s4 & 0x1f)) << 2;
        int16_t v0_2 = (int16_t)fmult((int16_t)((int32_t)arg1->a[1] >> 2), arg1->sr[1]);
        int16_t v0_3 = (int16_t)fmult((int16_t)((int32_t)arg1->a[0] >> 2), arg1->sr[0]);
        int32_t v0_4 = step_size(arg1);
        int32_t v0_6 = reconstruct((int32_t)(s4 & 0x10), g726_40_dqlntab[s5_1 >> 2], v0_4);
        int16_t v1_3 = (int16_t)((v0_2 + v0_3 + (int16_t)s1) >> 1);
        int32_t s0_2;

        if (v0_6 < 0) {
            s0_2 = (int32_t)(int16_t)(v1_3 - (v0_6 & 0x7fff));
        } else {
            s0_2 = (int32_t)(int16_t)(v1_3 + v0_6);
        }

        update(arg1, v0_4, g726_40_witab[s5_1 >> 2], g726_40_fitab[s5_1 >> 2], v0_6, s0_2,
               (int32_t)(int16_t)(((int16_t)(s1 >> 1)) + s0_2 - v1_3));
        return (int32_t)(int16_t)(s0_2 << 2);
    }
}

int32_t g726_40_encoder(G726State *arg1, int32_t arg2)
{
    int16_t *i = &arg1->b[1];
    int32_t s1 = fmult((int16_t)((int32_t)arg1->b[0] >> 2), arg1->dq[0]);

    do {
        int32_t a1_1 = (int32_t)i[8];
        int32_t a0_3 = (int32_t)(*i) >> 2;
        i = &i[1];
        s1 += fmult((int16_t)a0_3, (int16_t)a1_1);
    } while (&arg1->pk[0] != i);

    {
        int32_t s1_2 = (int32_t)(int16_t)(fmult((int16_t)((int32_t)arg1->a[1] >> 2), arg1->sr[1]) +
                                           fmult((int16_t)((int32_t)arg1->a[0] >> 2), arg1->sr[0]) +
                                           (int16_t)s1);
        int32_t v0_5 = step_size(arg1);
        int16_t s1_3 = (int16_t)(((uint32_t)s1_2 >> 1) & 0xffff);
        int32_t v0_6 = quantize((int32_t)(int16_t)(arg2 - s1_3), v0_5, qtab_726_40, 0x1f);
        int32_t s5 = v0_6 << 2;
        int32_t v0_8 = reconstruct(v0_6 & 0x10, g726_40_dqlntab[s5 >> 2], v0_5);
        int32_t a1_7;

        if (v0_8 < 0) {
            a1_7 = (int32_t)(int16_t)(s1_3 - (v0_8 & 0x7fff));
        } else {
            a1_7 = (int32_t)(int16_t)(s1_3 + v0_8);
        }

        update(arg1, v0_5, g726_40_witab[s5 >> 2], g726_40_fitab[s5 >> 2], v0_8, a1_7,
               (int32_t)(int16_t)(((int16_t)(s1 >> 1)) - s1_3 + a1_7));
        return v0_6 & 0xff;
    }
}

G726State *g726_init(G726State *arg1, int32_t arg2)
{
    if (arg2 != 0x3e80 && arg2 != 0x5dc0 && arg2 != 0x7d00 && arg2 != 0x9c40) {
        return 0;
    }

    arg1->yl = 0x8800;
    arg1->yu = 0x220;
    arg1->td = 0;
    arg1->dms = 0;
    arg1->ap = 0;
    arg1->rate = arg2;
    arg1->dml = 0;
    arg1->pk[1] = 0;
    arg1->dq[5] = 0x20;
    arg1->sr[1] = 0;
    arg1->a[0] = 0;
    arg1->a[1] = 0;

    {
        int16_t *i = &arg1->b[0];
        do {
            *i = 0;
            i[8] = 0x20;
            i = &i[1];
        } while (&arg1->pk[0] != i);
    }

    arg1->encoder = 0;

    if (arg2 == 0x5dc0) {
        arg1->encoder = g726_24_encoder;
        arg1->decoder = g726_24_decoder;
        arg1->code_size = 3;
    } else if (arg2 == 0x9c40) {
        arg1->encoder = g726_40_encoder;
        arg1->decoder = g726_40_decoder;
        arg1->code_size = 5;
    } else if (arg2 == 0x3e80) {
        arg1->encoder = g726_16_encoder;
        arg1->decoder = g726_16_decoder;
        arg1->code_size = 2;
    } else {
        arg1->encoder = g726_32_encoder;
        arg1->decoder = g726_32_decoder;
        arg1->code_size = 4;
    }

    bitstream_init(&arg1->bs.bitstream);
    return arg1;
}

int32_t g726_decode(G726State *arg1, int16_t *arg2, uint8_t *arg3, int32_t arg4)
{
    int16_t *s5 = arg2;
    int32_t result = 0;
    int32_t s4 = 0;

    while (1) {
        int32_t v1_1 = arg1->bs.residue;
        int32_t v0_4 = arg1->code_size;
        int32_t a1_1;

        if (v1_1 < v0_4) {
            v1_1 += 8;
            if (s4 >= arg4) {
                return result;
            }
            a1_1 = (arg1->bs.bitstream << 8) | arg3[s4];
            arg1->bs.bitstream = a1_1;
            s4 += 1;
        } else {
            a1_1 = arg1->bs.bitstream;
        }

        {
            int32_t v1_2 = v1_1 - v0_4;
            int32_t (*t9_1)(G726State *, char) = arg1->decoder;
            arg1->bs.residue = v1_2;
            *s5 = (int16_t)t9_1(arg1, (char)(((1U << (v0_4 & 0x1f)) - 1) & ((uint32_t)a1_1 >> (v1_2 & 0x1f))));
            result += 1;
            s5 = &s5[1];
        }
    }
}

int32_t g726_encode(G726State *arg1, uint8_t *arg2, int16_t *arg3, int32_t arg4)
{
    if (arg4 <= 0) {
        return 0;
    }

    {
        int16_t *i = arg3;
        int32_t result = 0;

        do {
            int32_t v0_1 = arg1->encoder(arg1, ((int32_t)(*i)) >> 2);
            int32_t a2 = arg1->code_size;
            int32_t a0_2 = a2 + arg1->bs.residue;
            int32_t v0_2 = (arg1->bs.bitstream << (a2 & 0x1f)) | v0_1;

            i = &i[1];
            arg1->bs.residue = a0_2;
            arg1->bs.bitstream = v0_2;
            if (a0_2 >= 8) {
                arg2[result] = (uint8_t)((uint32_t)v0_2 >> ((a0_2 - 8) & 0x1f));
                result += 1;
                arg1->bs.residue -= 8;
            }
            if (i == &arg3[arg4]) {
                return result;
            }
        } while (1);
    }
}

int32_t adpcm_coder(int16_t *arg1, char *arg2, int32_t arg3, AdpcmState *arg4)
{
    int32_t t4 = (int32_t)arg4->index;
    int32_t v1 = stepsizeTable[t4];
    int32_t s2 = (int32_t)arg4->valprev;
    int32_t result;
    char arg5 = 0;

    if (arg3 <= 0) {
        result = 0;
    } else {
        int16_t *a2_1 = arg1 + arg3;
        int32_t t5_1 = 1;

        result = 0;
        do {
            int32_t t0_2;
            int32_t s3_1;
            int32_t t2_1;
            int32_t t3_1;
            int32_t t1_2;
            int32_t t1_3;
            int32_t t3_2;
            int32_t v1_2;

            arg1 += 1;
            t0_2 = (int32_t)*(arg1 - 1) - s2;
            if (t0_2 < 0) {
                t0_2 = -t0_2;
                s3_1 = 8;
            } else {
                s3_1 = 0;
            }

            t2_1 = v1 >> 3;
            t3_1 = 0;
            if (t0_2 >= v1) {
                t0_2 -= v1;
                t2_1 += v1;
                t3_1 = 4;
            }

            t1_2 = v1 >> 1;
            if (t0_2 >= t1_2) {
                t3_1 |= 2;
                t0_2 -= t1_2;
                t2_1 += t1_2;
            }

            t1_3 = t1_2 >> 1;
            if (t0_2 >= t1_3) {
                t3_1 |= 1;
                t2_1 += t1_3;
            }

            t3_2 = t3_1 | s3_1;
            v1_2 = s2 - t2_1;
            if (s3_1 == 0) {
                v1_2 = t2_1 + s2;
            }
            if (v1_2 < -0x8000) {
                v1_2 = -0x8000;
            }
            if (v1_2 >= 0x8000) {
                v1_2 = 0x7fff;
            }

            t4 += indexTable[t3_2];
            s2 = v1_2;
            if (t4 < 0) {
                v1 = 7;
                t4 = 0;
            } else if (t4 < 0x59) {
                v1 = stepsizeTable[t4];
            } else {
                v1 = 0x7fff;
                t4 = 0x58;
            }

            if (t5_1 == 0) {
                *arg2 = (char)((uint8_t)t3_2 | (uint8_t)arg5);
                result += 1;
                arg2 = &arg2[1];
            } else {
                arg5 = (char)(t3_2 << 4);
            }
            t5_1 ^= 1;
        } while (arg1 != a2_1);

        if (t5_1 == 0) {
            *arg2 = arg5;
            result += 1;
        }
    }

    arg4->valprev = (int16_t)s2;
    arg4->index = (int8_t)t4;
    return result;
}

int32_t adpcm_decoder(char *arg1, int16_t *arg2, int32_t arg3, AdpcmState *arg4)
{
    int32_t t2 = (int32_t)arg4->index;
    int32_t v1 = (int32_t)arg4->valprev;
    int32_t t3 = stepsizeTable[t2];
    int32_t result = 0;
    int32_t arg5 = 0;

    if (arg3 > 0) {
        int32_t t1_1 = v1;
        int16_t *s0_2 = arg2 + arg3;
        int32_t t6_1 = 0;

        result = arg3;
        while (1) {
            int32_t t0_6 = arg5 & 0x0f;
            int32_t t7_1;
            int32_t v1_2;
            int32_t t0_3;
            int32_t v1_3;
            int32_t v1_4;

            if (t6_1 == 0) {
                arg5 = (int32_t)(*arg1);
                arg1 = &arg1[1];
                t0_6 = ((uint32_t)arg5 >> 4) & 0x0f;
            }

            t2 += indexTable[t0_6];
            t6_1 ^= 1;
            if (t2 < 0) {
                t7_1 = 7;
                t2 = 0;
            } else if (t2 < 0x59) {
                t7_1 = stepsizeTable[t2];
            } else {
                t7_1 = 0x7fff;
                t2 = 0x58;
            }

            v1_2 = t3 >> 3;
            if ((t0_6 & 4) != 0) {
                v1_2 += t3;
            }
            if ((t0_6 & 2) != 0) {
                v1_2 += t3 >> 1;
            }

            t0_3 = t1_1 - v1_2;
            if ((t0_6 & 1) != 0) {
                v1_2 += t3 >> 2;
                t0_3 = t1_1 - v1_2;
            }

            v1_3 = v1_2 + t1_1;
            if ((t0_6 & 8) != 0) {
                v1_3 = t0_3;
            }
            if (v1_3 < -0x8000) {
                v1_3 = -0x8000;
            }

            v1_4 = 0x7fff;
            if (v1_3 < 0x8000) {
                v1_4 = v1_3;
            }

            t1_1 = v1_4;
            arg2 += 1;
            v1 = (int32_t)(int16_t)v1_4;
            *(arg2 - 1) = (int16_t)v1;
            if (arg2 == s0_2) {
                break;
            }
            t3 = t7_1;
        }
    }

    arg4->valprev = (int16_t)v1;
    arg4->index = (int8_t)t2;
    return result;
}
