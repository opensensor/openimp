/*
 * BlackEnhance: a P slice in which every macroblock is skipped (CABAC
 * mb_skip_flag 1, end_of_slice_flag 1 on the last macroblock).  The OEM
 * i264e_icodec_black (libimp 3.12.0, 0x3dfe8) writes such a slice in
 * software for the drop-able pictures of an HSkip structure instead of
 * running the encoder; the decoder copies the reference picture.  This is
 * the same arithmetic code written with an ordinary CABAC encoder
 * (H.264 9.3.4.2); tests/hskip/black_vectors.txt holds the OEM bytes.
 */
#include <stdint.h>
#include <string.h>

#include "hskip/hskip.h"

static const uint8_t lps_range[64][4] = {
    {128, 176, 208, 240}, {128, 167, 197, 227}, {128, 158, 187, 216}, {123, 150, 178, 205},
    {116, 142, 169, 195}, {111, 135, 160, 185}, {105, 128, 152, 175}, {100, 122, 144, 166},
    {95, 116, 137, 158}, {90, 110, 130, 150}, {85, 104, 123, 142}, {81, 99, 117, 135},
    {77, 94, 111, 128}, {73, 89, 105, 122}, {69, 85, 100, 116}, {66, 80, 95, 110},
    {62, 76, 90, 104}, {59, 72, 86, 99}, {56, 69, 81, 94}, {53, 65, 77, 89},
    {51, 62, 73, 85}, {48, 59, 69, 80}, {46, 56, 66, 76}, {43, 53, 63, 72},
    {41, 50, 59, 69}, {39, 48, 56, 65}, {37, 45, 54, 62}, {35, 43, 51, 59},
    {33, 41, 48, 56}, {32, 39, 46, 53}, {30, 37, 43, 50}, {29, 35, 41, 48},
    {27, 33, 39, 45}, {26, 31, 37, 43}, {24, 30, 35, 41}, {23, 28, 33, 39},
    {22, 27, 32, 37}, {21, 26, 30, 35}, {20, 24, 29, 33}, {19, 23, 27, 31},
    {18, 22, 26, 30}, {17, 21, 25, 28}, {16, 20, 23, 27}, {15, 19, 22, 25},
    {14, 18, 21, 24}, {14, 17, 20, 23}, {13, 16, 19, 22}, {12, 15, 18, 21},
    {12, 14, 17, 20}, {11, 14, 16, 19}, {11, 13, 15, 18}, {10, 12, 15, 17},
    {10, 12, 14, 16}, {9, 11, 13, 15}, {9, 11, 12, 14}, {8, 10, 12, 14},
    {8, 9, 11, 13}, {7, 9, 11, 12}, {7, 9, 10, 12}, {7, 8, 10, 11},
    {6, 8, 9, 11}, {6, 7, 9, 10}, {6, 7, 8, 9}, {2, 2, 2, 2},
};

static const uint8_t trans_lps[64] = {
    0, 0, 1, 2, 2, 4, 4, 5, 6, 7, 8, 9, 9, 11, 11, 12,
    13, 13, 15, 15, 16, 16, 18, 18, 19, 19, 21, 21, 22, 22, 23, 24,
    24, 25, 26, 26, 27, 27, 28, 29, 29, 30, 30, 30, 31, 32, 32, 33,
    33, 33, 34, 34, 35, 35, 35, 36, 36, 36, 37, 37, 37, 38, 38, 63,
};

typedef struct {
    uint32_t low, range;
    int first, outstanding;
    uint8_t *out;
    size_t cap, n;
    uint32_t acc;               /* bit accumulator */
    int nbits;
    int overflow;
} Enc;

static void put_raw(Enc *e, int bit)
{
    e->acc = (e->acc << 1) | (uint32_t)bit;
    if (++e->nbits == 8) {
        if (e->n < e->cap)
            e->out[e->n++] = (uint8_t)e->acc;
        else
            e->overflow = 1;
        e->acc = 0;
        e->nbits = 0;
    }
}

static void put_bit(Enc *e, int b)
{
    if (e->first)
        e->first = 0;
    else
        put_raw(e, b);
    while (e->outstanding > 0) {
        put_raw(e, 1 - b);
        e->outstanding--;
    }
}

static void renorm(Enc *e)
{
    while (e->range < 256) {
        if (e->low < 256) {
            put_bit(e, 0);
        } else if (e->low >= 512) {
            e->low -= 512;
            put_bit(e, 1);
        } else {
            e->low -= 256;
            e->outstanding++;
        }
        e->range <<= 1;
        e->low <<= 1;
    }
}

/* Returns the number of bytes of the slice data of `mbs` skipped macroblocks
 * (0 on overflow).  `ctx_state` is the initial preCtxState (1..126) of the
 * P slice context 11 (mb_skip_flag, neighbours skipped: ctxIdxInc 0). */
size_t hskip_black_slice_data(uint8_t *out, size_t cap, uint32_t mbs, uint8_t ctx_state)
{
    Enc e;
    uint32_t i;
    int pstate, mps;

    if (!mbs)
        return 0;
    memset(&e, 0, sizeof(e));
    e.low = 0;
    e.range = 510;
    e.first = 1;
    e.out = out;
    e.cap = cap;
    if (ctx_state <= 63) {
        pstate = 63 - ctx_state;
        mps = 0;
    } else {
        pstate = ctx_state - 64;
        mps = 1;
    }
    for (i = 0; i < mbs; i++) {
        uint32_t rlps = lps_range[pstate][(e.range >> 6) & 3];

        /* mb_skip_flag = 1 */
        e.range -= rlps;
        if (mps != 1) {
            e.low += e.range;
            e.range = rlps;
            if (pstate == 0)
                mps = 1 - mps;
            pstate = trans_lps[pstate];
        } else if (pstate < 62) {
            pstate++;
        }
        renorm(&e);
        /* end_of_slice_flag */
        e.range -= 2;
        if (i + 1 == mbs) {
            e.low += e.range;
            e.range = 2;
            renorm(&e);
            put_bit(&e, (int)((e.low >> 9) & 1));
            put_raw(&e, (int)((e.low >> 8) & 1));
            put_raw(&e, 1);
        } else {
            renorm(&e);
        }
    }
    while (e.nbits)             /* rbsp_stop_one_bit is the last bit written; zero padding */
        put_raw(&e, 0);
    /* the OEM also sets the last bit of the last byte (identical bytes for every
     * slice of 135 or more macroblocks; below that the OEM construction is not a
     * valid CABAC string and OpenIMP still codes the correct all-skip slice) */
    if (e.n && !e.overflow)
        out[e.n - 1] |= 1;
    return e.overflow ? 0 : e.n;
}
