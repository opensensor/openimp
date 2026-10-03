/*
 * T31 Allegro rate controller, ported from the OEM libimp 1.1.6 (addresses in
 * the comments).  Integer arithmetic follows the MIPS code exactly: 32-bit
 * truncation, 64-bit products where the OEM uses multu, the compiler's magic
 * divisions where they are not exact, signed/unsigned compares as in the
 * instructions.  Divisions by zero that would trap in the OEM yield 0 here.
 */
#include "t31_al_rc.h"

#include <math.h>
#ifdef T31_AL_RC_DEBUG
#include <stdio.h>
#endif
#include <stddef.h>
#include <string.h>

#define STATIC_ASSERT(cond, name) typedef char name[(cond) ? 1 : -1]
STATIC_ASSERT(sizeof(T31AlRcState) == 328, t31_al_rc_state_size);
STATIC_ASSERT(offsetof(T31AlRcState, hrd) == 72, t31_al_rc_hrd_off);
STATIC_ASSERT(offsetof(T31AlRcState, init_level) == 136, t31_al_rc_136);
STATIC_ASSERT(offsetof(T31AlRcState, model_size) == 200, t31_al_rc_200);
STATIC_ASSERT(offsetof(T31AlRcState, step_ratio) == 248, t31_al_rc_248);
STATIC_ASSERT(offsetof(T31AlRcState, opt_bit0) == 280, t31_al_rc_280);
STATIC_ASSERT(offsetof(T31AlRcState, step) == 304, t31_al_rc_304);
STATIC_ASSERT(offsetof(T31AlRcState, flag320) == 320, t31_al_rc_320);
STATIC_ASSERT(sizeof(T31AlRcHrd) == 64, t31_al_rc_hrd_size);

/* 2^((i-51)/6) * 32768, IIIi table at 0xe3ee0 */
static const uint32_t t31_qp_scale[103] = {
    91, 102, 114, 128, 144, 161, 181, 203, 228, 256, 287, 323, 362, 406, 456,
    512, 575, 645, 724, 813, 912, 1024, 1149, 1290, 1448, 1625, 1825, 2048,
    2299, 2580, 2896, 3251, 3649, 4096, 4598, 5161, 5793, 6502, 7298, 8192,
    9195, 10321, 11585, 13004, 14596, 16384, 18390, 20643, 23170, 26008, 29193,
    32768, 36781, 41285, 46341, 52016, 58386, 65536, 73562, 82570, 92682,
    104032, 116772, 131072, 147123, 165140, 185364, 208064, 233544, 262144,
    294247, 330281, 370728, 416128, 467088, 524288, 588493, 660561, 741455,
    832255, 934175, 1048576, 1176987, 1321123, 1482910, 1664511, 1868350,
    2097152, 2353974, 2642246, 2965821, 3329021, 3736700, 4194304, 4707947,
    5284492, 5931642, 6658043, 7473400, 8388608, 9415894, 10568984, 11863283
};

static uint64_t udiv64(uint64_t n, uint64_t d) { return d ? n / d : 0u; }
static uint64_t umod64(uint64_t n, uint64_t d) { return d ? n % d : 0u; }
static uint32_t udiv32(uint32_t n, uint32_t d) { return d ? n / d : 0u; }
static uint32_t umod32(uint32_t n, uint32_t d) { return d ? n % d : 0u; }
static int32_t sdiv32(int32_t n, int32_t d)
{
    if (d == 0 || (d == -1 && n == INT32_MIN))
        return 0;
    return n / d;
}
/* (u64)x * 1000 / 10000 as the OEM shift sequences compute them: plain
 * 64-bit products of a zero-extended 32-bit value. */
#define U64(x) ((uint64_t)(uint32_t)(x))
#define LO32(x) ((uint32_t)(x))
/* signed 32-bit division by 90000 via 0x5d34edef (mult/mfhi/sra 15) */
static int32_t sdiv90000(int32_t v)
{
    int64_t p = (int64_t)v * (int64_t)0x5d34edef;
    int32_t hi = (int32_t)(p >> 32);
    return (hi >> 15) - (v >> 31);
}
/* unsigned (v >> 4) * 0x05d34edf >> 39 */
static uint32_t useconds90000(uint32_t v)
{
    uint64_t p = (uint64_t)(v >> 4) * 0x05d34edfu;
    return (uint32_t)(p >> 39);
}
static int32_t sdiv3(int32_t v)
{
    int64_t p = (int64_t)v * (int64_t)0x2aaaaaab;
    return (int32_t)(p >> 32) - (v >> 31);
}
static int32_t sdiv4(int32_t v) { return (v + (v < 0 ? 3 : 0)) >> 2; }
static int16_t s16(int32_t v) { return (int16_t)v; }
static int32_t clamp_qp(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo)
        return lo;
    if (v >= hi)
        return hi;
    return v;
}

/* ------------------------------------------------------------------ HRD */

/* l0io 0x56110 */
static void hrd_init(T31AlRcHrd *h, uint32_t cpb_bits, uint32_t init_delay,
                     uint32_t clk, uint32_t fps_1000, uint32_t max_br,
                     uint8_t is_cbr, uint8_t strict)
{
    h->cpb_bits = cpb_bits;
    h->fps_1000 = fps_1000;
    h->is_cbr = is_cbr;
    h->strict = strict;
    h->init_delay = init_delay;
    h->clk_ratio = clk;
    h->max_bitrate = max_br;
    h->arrival = 0;
    h->arrival_rem = 0;
    h->removal = init_delay;
    h->removal_rem = 0;
    h->seconds = 0;
    h->total_bits_lo = 0;
    h->total_bits_hi = 0;
    h->pictures = 0;
    h->idle_ticks = 0;
}

/* OOlo 0x569f8: buffer level in bits */
static int32_t hrd_level(const T31AlRcHrd *h)
{
    uint32_t s3 = h->arrival, s1 = h->arrival_rem, s2 = h->removal;
    uint32_t s5 = h->fps_1000, s4 = h->max_bitrate;
    uint32_t a0 = h->removal_rem;
    int32_t v0, q;
    int negative = 0;

    if (!h->is_cbr) {
        uint32_t s6 = s2 - h->init_delay;

        if (s3 < s6) {
            s1 = LO32(udiv64(U64(s4) * U64(s2), s5));
            s3 = s6;
        }
    }
    if (s2 < s3) {
        negative = 1;
    } else if (s3 == s2) {
        /* 0x56bcc: equal tick times, compare the fractions */
        if ((uint32_t)(a0 * s4) < (uint32_t)(s1 * s5))
            negative = 1;
    }
    if (!negative) {
        v0 = (int32_t)(LO32(udiv64(U64(a0) * U64(s4), s5)) - s1);
        q = sdiv90000(v0);
        return q + (int32_t)LO32(udiv64(U64(s2 - s3) * U64(s4), 90000u));
    }
    v0 = (int32_t)(s1 - LO32(udiv64(U64(a0) * U64(s4), s5)));
    q = (v0 >> 31) - (int32_t)((((int64_t)v0 * (int64_t)0x5d34edef) >> 32) >> 15);
    return q - (int32_t)LO32(udiv64(U64(s3 - s2) * U64(s4), 90000u));
}

/* i0Io 0x565b4: account one picture */
static void hrd_add_picture(T31AlRcHrd *h, uint32_t bits)
{
    uint32_t t2 = h->arrival, t0 = h->removal, s8 = h->init_delay;
    uint32_t s7 = h->max_bitrate, s4 = h->clk_ratio, s3 = h->fps_1000;
    uint64_t p;
    uint32_t rem, sum, s1, s2, v0, arr_rem, rem_rem, a0 = 0, a3 = 0;
    uint32_t s6, carry;

    if (!h->is_cbr) {
        uint32_t v = t0 - s8;

        if (t2 < v) {
            h->idle_ticks += v - t2;
            t2 = v;
        }
    }
    p = U64(bits) * 90000u;
    rem = LO32(umod64(p, s7));
    sum = h->arrival_rem + rem;
    arr_rem = umod32(sum, s7);
    s1 = udiv32(sum, s7);
    s2 = s1 + LO32(udiv64(p, s7)) + t2;         /* arrival of this picture */
    h->arrival_rem = arr_rem;

    p = U64(s4) * 90000u;
    rem = LO32(umod64(p, s3));
    sum = h->removal_rem + rem;
    rem_rem = umod32(sum, s3);
    s1 = udiv32(sum, s3);
    v0 = s1 + LO32(udiv64(p, s3)) + t0;         /* next removal */
    h->removal_rem = rem_rem;

    if (s8 < v0 && s8 < s2) {
        uint32_t v1 = (v0 < s2) ? v0 : s2;

        a0 = useconds90000(v1 - s8);
        a3 = a0 * 90000u;
    }
    s6 = h->total_bits_lo + bits;
    carry = s6 < h->total_bits_lo;
    h->seconds += a0;
    h->arrival = s2 - a3;
    h->removal = v0 - a3;
    h->pictures += 1;
    h->total_bits_lo = s6;
    h->total_bits_hi += carry;
    if (h->strict)
        return;
    v0 -= a3;
    if (v0 < s2 - a3) {
        h->removal = s2;
        h->removal_rem = arr_rem;
    } else if (v0 == s2 - a3) {
        if ((uint32_t)(s7 * rem_rem) < (uint32_t)(arr_rem * s3)) {
            h->removal = s2;
            h->removal_rem = arr_rem;
        }
    }
}

/* IIIo 0x56380: filler bytes (CBR only), -1 on buffer underflow */
static int32_t hrd_filler(const T31AlRcHrd *h, uint32_t bits)
{
    uint32_t s5 = h->arrival, s4 = h->removal, s3 = h->max_bitrate;
    uint64_t p;
    uint32_t sum, s0, s2, v0;

    if (!h->is_cbr) {
        uint32_t v = s4 - h->init_delay;

        if (s5 < v)
            s5 = v;
    }
    p = U64(bits) * 90000u;
    sum = h->arrival_rem + LO32(umod64(p, s3));
    s0 = udiv32(sum, s3);
    s2 = umod32(sum, s3);
    v0 = s0 + LO32(udiv64(p, s3)) + s5;
    if (s4 < v0)
        return -1;
    if (v0 == s4) {
        if ((uint32_t)(s3 * h->removal_rem) < (uint32_t)(s2 * h->fps_1000))
            return -1;
    }
    if (!h->is_cbr)
        return 0;
    /* 0x5646c: bytes that must be stuffed so the CPB does not overflow */
    {
        uint32_t s6 = h->removal_rem, s5 = h->fps_1000, t0 = h->clk_ratio;
        uint32_t rem, sum2, s4q, s6n, q, s0b, s4t, frac, v, s2s;

        p = U64(t0) * 90000u;
        rem = LO32(umod64(p, s5));
        sum2 = s6 + rem;
        s4q = udiv32(sum2, s5);
        s6n = umod32(sum2, s5);
        q = LO32(udiv64(p, s5));
        s0b = s4 - v0;
        s4t = s0b + q + s4q;
        frac = LO32(udiv64(U64(s6n) * U64(s3), s5));
        v = frac - s2;
        s2s = useconds90000(v);
        v = s2s + LO32(udiv64(U64(s3) * U64(s4t), 90000u));
        if (h->cpb_bits < v)
            return (int32_t)((v - h->cpb_bits + 7u) >> 3);
    }
    return 0;
}

/* iiIo 0x5616c: bitrate / frame rate change */
static void hrd_set_rate(T31AlRcHrd *h, uint32_t clk, uint32_t fps_1000,
                         uint32_t max_br)
{
    uint32_t s5 = h->removal, s0 = h->max_bitrate, s1 = max_br, s2 = fps_1000;
    uint64_t p;
    uint32_t sp24, s4, s7, t0, s3, s6, v0, v1;

    p = U64(s5) * U64(s0);
    sp24 = LO32(umod64(p, s1));
    s4 = LO32(udiv64(p, s1));
    p = U64(h->arrival) * U64(s0);
    s7 = LO32(umod64(p, s1));
    t0 = LO32(udiv64(p, s1));
    if (s5 + t0 < s4) {
        s5 = s4 - t0;
        h->removal = s5;
    }
    s3 = h->removal_rem;
    s6 = h->fps_1000;
    v0 = LO32(udiv64(U64(s0) * U64(s3), s6));
    v0 = h->arrival_rem - v0 + s1;
    s0 = v0 + s7;
    v0 = LO32(udiv64(U64(s1) * U64(s3), s6));
    v0 = s0 + v0 - sp24;
    t0 -= 1;
    s4 = t0 - s4;
    v1 = umod32(v0, s1);
    v0 = s4 + udiv32(v0, s1);
    h->arrival = v0 + s5;
    h->arrival_rem = v1;
    if (s6 != s2) {
        v0 = LO32(udiv64(U64(s3) * U64(s2), s6));
        h->removal = udiv32(v0, s2) + s5;
        h->removal_rem = umod32(v0, s2);
    }
    h->fps_1000 = s2;
    h->max_bitrate = s1;
    h->clk_ratio = clk;
}

/* ------------------------------------------------------------- helpers */

/* loli.isra.4 0x50de8: QP offset of a picture type */
static int32_t type_qp_delta(const T31AlRcState *st, uint32_t type)
{
    switch (type) {
    case 0: return st->pb_delta;
    case 1: case 8: return 0;
    case 2: return -st->ip_delta;
    case 3: return -st->f172;
    default: return 0; /* OEM: assert */
    }
}

/* IIIi 0x5192c: predicted size at another QP */
static uint32_t predict_size(uint32_t size, uint32_t model_qp, uint32_t qp,
                             uint32_t ratio, int32_t max_steps)
{
    uint32_t v0 = size, n;

    model_qp &= 0xffffu;
    qp &= 0xffffu;
    if (ratio == 0) {
        uint32_t idx = (model_qp + 51u - qp) & 0xffffu;
        uint64_t p;

        if (idx >= 103u)
            return 0; /* OEM reads past the table */
        p = U64(t31_qp_scale[idx]) * U64(v0);
        return LO32(p >> 15);
    }
    if (model_qp < qp) {
        n = qp - model_qp;
        if ((int32_t)n > max_steps)
            n = (uint32_t)max_steps;
        while (n--)
            v0 = LO32(udiv64(U64(v0) * 10000u, ratio));
        return v0;
    }
    n = model_qp - qp;
    if ((int32_t)n > max_steps)
        n = (uint32_t)max_steps;
    while (n--)
        v0 = LO32(udiv64(U64(v0) * U64(ratio), 10000u));
    return v0;
}

/* illi 0x51ae8 */
static int32_t predict_size_limited(uint32_t size, uint32_t model_qp,
                                    uint32_t qp, uint32_t ratio,
                                    int32_t max_steps, int32_t limit)
{
    int32_t v = (int32_t)predict_size(size, model_qp, qp, ratio, max_steps);

    return limit < v ? limit : v;
}

/* llli 0x51fa4: refine a one-QP size ratio from two measurements */
static uint32_t refine_ratio(uint32_t model_size, uint32_t model_qp,
                             uint32_t size, uint32_t qp, uint32_t ratio,
                             int32_t rmin, int32_t rmax)
{
    uint32_t t0 = model_qp & 0xffffu, s2 = qp & 0xffffu;
    uint32_t s1 = ratio, v0, s8;
    uint64_t num;
    int s6;

#ifdef T31_AL_RC_DEBUG
    fprintf(stderr, "llli(%u,%u,%u,%u,%u,%d,%d)\n", model_size, model_qp, size, qp, ratio, rmin, rmax);
#endif
    if (t0 == s2)
        return s1;
    if ((int32_t)size < (int32_t)model_size) {
        if (!(t0 < s2))
            return (uint32_t)((int32_t)(s1 + (uint32_t)rmin) / 2);
        v0 = LO32(udiv64(U64(model_size) * 10000u, size));
        s2 = s2 - t0;
    } else {
        if (!(s2 < t0))
            return (uint32_t)((int32_t)(s1 + (uint32_t)rmin) / 2);
        if (!((int32_t)model_size < (int32_t)size))
            return (uint32_t)((int32_t)(s1 + (uint32_t)rmin) / 2);
        v0 = LO32(udiv64(U64(size) * 10000u, model_size));
        s2 = t0 - s2;
    }
    num = U64(v0) * 10000u;
    s6 = (int32_t)s2 < 2;
    s8 = s2 - 1u;
    for (;;) {
        uint32_t a2, s0, v1, q;

        if (!s6) {
            uint32_t a0 = 10000u, i;

            for (i = 1; i != s2; ++i)
                a0 = LO32(udiv64(U64(a0) * U64(s1), 10000u));
            a2 = a0;
            if (a2 == 0) {
                /* 0x52138: clamp(rmax) */
                int32_t a = rmax;

                if (a < rmin)
                    return (uint32_t)rmin;
                return (uint32_t)a;
            }
        } else {
            a2 = 10000u;
        }
        s0 = s8 * s1;
        q = LO32(udiv64(num, a2));
        v0 = s0 + q;
        v1 = udiv32(v0, s2);
        if ((uint32_t)(v1 - s1 + 1u) < 3u) {
            int32_t a = (int32_t)v1;

            if (a < rmin)
                return (uint32_t)rmin;
            if (!(a < rmax))
                a = rmax;
            return (uint32_t)a;
        }
        if (!((int32_t)v1 < rmax) || !(rmin < (int32_t)v1)) {
            int32_t a = (int32_t)v1;

            if (a < rmin)
                return (uint32_t)rmin;
            if (!(a < rmax))
                a = rmax;
            return (uint32_t)a;
        }
        s1 = v1;
    }
}

/* ooIi 0x51b38: QP that brings size to the target */
static int16_t qp_for_target(uint32_t size, int16_t qp, int32_t target,
                             uint32_t ratio, int16_t min_qp, int16_t max_qp)
{
    uint32_t v0 = size;
    int32_t s3 = qp;

    if (target < (int32_t)size) {
        if (!(s3 < max_qp))
            return s16(s3);
        for (;;) {
            v0 = LO32(udiv64(U64(v0) * 10000u, ratio));
            s3 = s16(s3 + 1);
            if (!(s3 < max_qp))
                return s16(s3);
            if (!(target < (int32_t)v0))
                return s16(s3);
        }
    }
    if (!(min_qp < s3))
        return s16(s3);
    if (!((int32_t)size < target))
        return s16(s3);
    for (;;) {
        v0 = LO32(udiv64(U64(v0) * U64(ratio), 10000u));
        s3 = s16(s3 - 1);
        if (!(min_qp < s3))
            return s16(s3);
        if (!((int32_t)v0 < target))
            return s16(s3);
    }
}

/* lo0i 0x50af0 */
static void reset_models(T31AlRcState *st)
{
    uint32_t v;

    st->model_qp[3] = st->model_qp[0] = st->model_qp[1] = st->model_qp[2] =
        (uint32_t)(int32_t)st->p_qp;
    st->model_size[2] = 0;
    st->model_size[1] = st->model_size[0] = st->model_size[3] = st->target_frame;
    st->type_count[2] = st->type_count[1] = st->type_count[0] = 0;
    st->p20_ref = 60;
    st->last_size = st->target_frame;
    st->f236 = st->target_frame;
    v = LO32(udiv64(U64(st->init_level) * 1000u, st->target_frame));
    if (!((int32_t)v < 10001))
        v = 10000u;
    st->ratio_i = v;
    st->ratio_b = 333u;
    st->ratio_3 = st->flag320 ? (uint32_t)st->f172 * 1000u : 0u;
    v = st->flag300 ? 10233u : 11225u;
    st->step_ratio[3] = st->step_ratio[2] = st->step_ratio[1] =
        st->step_ratio[0] = v;
}

/* lI0i 0x50c18 */
static void reset_run(T31AlRcState *st)
{
    st->f152 = 0;
    st->gop_pictures = 0;
    st->f144 = st->target_frame;
    st->prev_type = -1;
    st->last_type = -1;
    st->f284 = st->f292;
    st->qp_sync = st->p_qp;
    st->qp = st->p_qp;
    if (!(st->gop_length < 2u) && st->auto_ip)
        st->ip_delta = st->flag300 ? 15 : 4;
    reset_models(st);
}

/* step st->ratio_i / ratio_b by n QP steps (lI1i/O0ii loops) */
static uint32_t ratio_steps(uint32_t ratio, int32_t n, uint32_t step_ratio)
{
    while (n > 0) {
        ratio = LO32(udiv64(U64(ratio) * 10000u, step_ratio));
        --n;
    }
    while (n < 0) {
        ratio = LO32(udiv64(U64(ratio) * U64(step_ratio), 10000u));
        ++n;
    }
    return ratio;
}

/* ----------------------------------------------------------------- init */

/* lI1i 0x50e80, first call (state[0] != 0) */
static void init_full(T31AlRcState *st, const T31AlRcParam *rcp,
                      const T31AlGopParam *gop)
{
    uint32_t mode = rcp->mode;
    uint8_t is_cbr = mode == 1u;
    uint8_t strict = mode != 9u;
    uint32_t s3 = rcp->target_bitrate & ~63u;
    uint32_t s2 = rcp->max_bitrate & ~63u;
    uint32_t fps_1000 = (uint32_t)rcp->frame_rate * 1000u;
    uint32_t clk = rcp->clk_ratio;
    uint32_t gop_len;
    int32_t ipd = rcp->ip_delta, pbd = rcp->pb_delta;
    int gop_short;

    st->target_bitrate = s3;
    st->max_bitrate = s2;
    memcpy(&st->gop_mode, gop, 28);
    gop_len = st->gop_length ? st->gop_length : 1u;
    st->gop_length = (uint16_t)gop_len;
    hrd_init(&st->hrd,
             LO32(udiv64(U64(rcp->cpb_size) * U64(rcp->max_bitrate), 90000u)),
             rcp->initial_rem_delay, clk, fps_1000, s2, is_cbr, strict);
    st->init_level = (uint32_t)hrd_level(&st->hrd);
    st->min_qp = (int16_t)(uint16_t)rcp->min_qp;
    st->max_qp = (int16_t)(uint16_t)rcp->max_qp;
    st->fps_1000 = fps_1000;
    st->clk_ratio = clk;
    st->p_qp = rcp->initial_qp;
    st->opt_flag = (rcp->options & 5u) != 1u;
    st->f281 = 0;
    st->f188 = 0;
    st->target_frame = LO32(udiv64(U64(clk) * U64(s3), (uint64_t)(int64_t)(int32_t)fps_1000));
    st->max_frame = LO32(udiv64(U64(clk) * U64(s2), (uint64_t)(int64_t)(int32_t)fps_1000));
    st->flag320 = rcp->flag34;
    if (rcp->flag34) {
        int32_t v = rcp->field36;

        st->flag321 = (uint8_t)rcp->field38;
        if (v < 0)
            v = st->flag300 ? 10 : 2;
        st->f172 = v;
    } else {
        st->flag321 = 0;
    }
    gop_short = st->gop_length < 2u;
    st->auto_ip = ipd < 0;
    /* 0x513a0 .. 0x51428 */
    if (gop_short) {
        st->ip_delta = 0;
        if (ipd < 0) {
            /* 0x51804 */
            if (st->gop_mode == 9u) {
                st->pb_delta = 0;
                st->i_qp_ref = rcp->initial_qp;
                st->opt_bit0 = (uint8_t)(rcp->options & 1u);
                if (!st->flag300) {
                    st->step = 1;
                    st->f308 = 20000;
                    st->f312 = 20000;
                    st->f316 = 11180;
                } else {
                    st->f308 = st->f312 = 14000;
                    st->step = 5;
                    st->f316 = 10190;
                }
                goto f288;
            }
            if (st->gop_mode & 4u)
                st->pb_delta = 0;
            else
                st->pb_delta = st->flag300 ? 10 : 2;
            st->i_qp_ref = rcp->initial_qp;
            goto opts;
        }
        if (pbd < 0) {
            if (st->gop_mode == 9u || (st->gop_mode & 4u))
                st->pb_delta = 0;
            else
                st->pb_delta = st->flag300 ? 10 : 2;
        } else {
            st->pb_delta = pbd;
        }
        st->i_qp_ref = rcp->initial_qp;
        goto opts;
    }
    if (ipd >= 0) {
        st->ip_delta = ipd;
        if (pbd < 0) {
            if (st->gop_mode == 9u || (st->gop_mode & 4u))
                st->pb_delta = 0;
            else
                st->pb_delta = st->flag300 ? 10 : 2;
        } else {
            st->pb_delta = pbd;
        }
    } else {
        ipd = st->flag300 ? 15 : 4;
        st->ip_delta = ipd;
        if (st->gop_mode == 9u || (st->gop_mode & 4u))
            st->pb_delta = 0;
        else
            st->pb_delta = st->flag300 ? 10 : 2;
    }
    st->i_qp_ref = rcp->initial_qp;
    /* 0x51400: P QP = initial + IP delta, clamped */
    {
        int32_t v = rcp->initial_qp + ipd;

        if (v < st->min_qp)
            st->p_qp = st->min_qp;
        else if (!(v < st->max_qp))
            st->p_qp = st->max_qp;
        else
            st->p_qp = s16(v);
    }
opts:
    st->opt_bit0 = (uint8_t)(rcp->options & 1u);
    if (!st->flag300) {
        st->step = 1;
        st->f308 = 20000;
        if (st->gop_mode == 9u)
            st->f312 = 20000;
        else
            st->f312 = 16000;
        st->f316 = 11180;
    } else {
        st->f308 = st->f312 = 14000;
        st->step = 5;
        st->f316 = 10190;
    }
f288:
    if (st->gop_mode & 8u) {
        uint32_t fps = udiv32(st->fps_1000, st->clk_ratio);

        st->f296 = 2;
        st->f288 = (int32_t)fps;
        st->f292 = (int32_t)(fps * 3u);
    } else {
        st->f292 = st->f296 = st->f288 = 0;
    }
    st->max_pel = rcp->max_pel;
    st->max_psnr = rcp->max_psnr_x100;
    st->num_pixels = rcp->num_pixels;
    st->opt_bit1 = (uint8_t)((rcp->options >> 1) & 1u);
    st->opt_bit4 = (uint8_t)((rcp->options >> 4) & 1u);
    reset_run(st);
    st->needs_init = 0;
}

/* lI1i 0x50e80, later calls (state[0] == 0): parameter update */
static void init_update(T31AlRcState *st, const T31AlRcParam *rcp,
                        const T31AlGopParam *gop)
{
    uint32_t s2 = (uint32_t)rcp->frame_rate * 1000u;
    uint32_t a0 = rcp->target_bitrate & ~63u;
    uint32_t s3 = rcp->max_bitrate & ~63u;
    uint32_t s5 = rcp->clk_ratio;
    uint32_t s6 = st->max_bitrate;
    int s4;   /* frame rate changed */
    int32_t ipd, pbd, v;
    int qp_changed;

    if (st->fps_1000 == s2)
        s4 = st->clk_ratio != s5;
    else
        s4 = 1;
    if (a0 != st->target_bitrate) {
        st->target_bitrate = a0;
        if (s3 != s6)
            st->max_bitrate = s3;
        st->target_frame = LO32(udiv64(U64(s5) * U64(a0), (uint64_t)(int64_t)(int32_t)s2));
        if (s3 != s6 || s4) {
            st->max_frame = LO32(udiv64(U64(s5) * U64(s3), (uint64_t)(int64_t)(int32_t)s2));
            hrd_set_rate(&st->hrd, s5, s2, s3);
        }
        if (s4) {
            st->fps_1000 = s2;
            st->clk_ratio = s5;
        }
    } else {
        if (s3 != s6)
            st->max_bitrate = s3;
        if (s4) {
            st->target_frame = LO32(udiv64(U64(s5) * U64(a0), (uint64_t)(int64_t)(int32_t)s2));
            st->max_frame = LO32(udiv64(U64(s5) * U64(s3), (uint64_t)(int64_t)(int32_t)s2));
            hrd_set_rate(&st->hrd, s5, s2, s3);
            st->fps_1000 = s2;
            st->clk_ratio = s5;
        } else if (s3 != s6) {
            st->max_frame = LO32(udiv64(U64(s5) * U64(s3), (uint64_t)(int64_t)(int32_t)s2));
            hrd_set_rate(&st->hrd, s5, s2, s3);
        }
    }
    /* 0x50fb8: QP bounds */
    {
        int32_t mn = rcp->min_qp, mx = rcp->max_qp;
        int32_t p = (int16_t)(uint16_t)st->p_qp;
        int32_t i = (int16_t)(uint16_t)st->i_qp_ref;
        int32_t q = st->qp, c;

        st->min_qp = (int16_t)mn;
        st->max_qp = (int16_t)mx;
        if (mx < p)
            p = mx;
        if (p < mn)
            p = mn;
        st->p_qp = s16(p);
        if (mx < i)
            i = mx;
        if (i < mn)
            i = mn;
        st->i_qp_ref = s16(i);
        c = q;
        if (q < mx)
            c = q;
        else
            c = mx;
        if (c < mn)
            c = mn;
        qp_changed = s16(c) != q;
        if (qp_changed) {
            st->qp = s16(c);
            reset_models(st);
        }
    }
    s4 = !qp_changed;   /* 0x5107c: s4 = 1 when the QP stayed */
    /* IP delta */
    ipd = rcp->ip_delta;
    if (ipd < 0 && st->auto_ip) {
        st->auto_ip = 1;
        if (st->gop_mode == 9u || (st->gop_mode & 4u))
            pbd = 0;
        else
            pbd = st->flag300 ? 10 : 2;
        goto pb;
    }
    if (st->gop_length < 2u) {
        v = 0;
        if (!s4) {
            st->ip_delta = 0;
        } else {
            int32_t n = st->ip_delta - 0;

            st->ratio_i = ratio_steps(st->ratio_i, n, st->step_ratio[2]);
            st->ip_delta = 0;
        }
        if (ipd < 0) {
            st->auto_ip = 1;
            if (st->gop_mode == 9u || (st->gop_mode & 4u))
                pbd = 0;
            else
                pbd = st->flag300 ? 10 : 2;
            goto pb;
        }
        st->auto_ip = 0;
    } else if (ipd < 0) {
        v = st->flag300 ? 15 : 4;
        if (!s4) {
            st->ip_delta = v;
            st->auto_ip = 1;
            if (st->gop_mode == 9u || (st->gop_mode & 4u))
                pbd = 0;
            else
                pbd = st->flag300 ? 10 : 2;
            goto pb;
        }
        st->ratio_i = ratio_steps(st->ratio_i, st->ip_delta - v, st->step_ratio[2]);
        st->ip_delta = v;
        st->auto_ip = 1;
        if (st->gop_mode == 9u || (st->gop_mode & 4u))
            pbd = 0;
        else
            pbd = st->flag300 ? 10 : 2;
        goto pb;
    } else {
        if (!s4) {
            st->ip_delta = ipd;
            st->auto_ip = 0;
        } else {
            st->ratio_i = ratio_steps(st->ratio_i, st->ip_delta - ipd, st->step_ratio[2]);
            st->ip_delta = ipd;
            st->auto_ip = 0;
        }
    }
    /* 0x510b0: PB delta from the parameter */
    pbd = rcp->pb_delta;
    if (pbd < 0) {
        if (st->gop_mode == 9u || (st->gop_mode & 4u))
            pbd = 0;
        else
            pbd = st->flag300 ? 10 : 2;
    }
pb:
    if (s4)
        st->ratio_b = ratio_steps(st->ratio_b, pbd - st->pb_delta, st->step_ratio[0]);
    st->pb_delta = pbd;
    st->num_b = gop->num_b;
    st->gop_length = gop->length ? gop->length : 1u;
}

/* O0ii 0x52f84: adapt the IP delta from the intra percentage */
static void adapt_ip_delta(T31AlRcState *st, int32_t pct)
{
    int32_t v0, v1, s2, s3;

    if (pct < 81) {
        if (pct < 61) {
            if (pct < 41)
                v0 = pct < 21 ? 4 : 3;
            else
                v0 = 2;
        } else {
            v0 = 1;
        }
    } else {
        v0 = 0;
    }
    v0 *= st->step;
    s2 = st->qp - st->min_qp;
    v1 = st->max_qp - st->qp;
    if (!(v1 < s2))
        v1 = s2;
    if (!(v0 < v1))
        v0 = v1;
    s3 = st->ip_delta - v0;
    st->ratio_i = ratio_steps(st->ratio_i, s3, st->step_ratio[2]);
    st->ip_delta = v0;
}

/* Ilii 0x5310c: macroblock statistics percentages */
static void mb_stats(const T31AlRcStatus *s, uint32_t size, uint32_t *tot,
                     uint32_t *p32, uint32_t *p28, uint32_t *p20, uint32_t *p24)
{
    uint32_t t = s->stat40 * 4u + s->stat44 * 16u + s->stat36 + s->stat48 * 64u;

    *tot = t;
    *p32 = udiv32(s->stat32 * 100u, t);
    *p28 = udiv32(s->stat28 * 100u, t);
    *p20 = udiv32(s->stat20 * 100u, size);
    *p24 = udiv32(s->stat24 * 25u, t);
}

/* i0ii 0x52e2c */
static int32_t picture_account(T31AlRcState *st, const T31AlRcPicture *pic,
                               uint32_t *size, uint32_t extra, int32_t p32,
                               int32_t p20)
{
    int32_t v1;

    hrd_add_picture(&st->hrd, *size + extra);
    if (st->last_type == 1 && pic->type == 1u) {
        int32_t r = st->p20_ref;

        if (r + 60 < p32 || p32 < r - 60)
            *size = st->model_size[1];
    }
    v1 = pic->type == 2u ? 1 : st->gop_pictures + 1;
    st->p20_ref = p32;
    st->gop_pictures = v1;
    if (pic->flags & 4u) {
        st->model_qp[3] = st->model_qp[0] = st->model_qp[1] = (uint32_t)(int32_t)st->p_qp;
        st->model_size[3] = st->model_size[0] = st->model_size[1] = st->target_frame;
        if (st->qp < st->p_qp)
            st->qp = st->p_qp;
    }
    if (!st->opt_bit4)
        return 0;
    if (!(p32 < 96))
        return st->opt_bit4;
    if (p32 < 86)
        return 0;
    return p20 < 70;
}

/* Ioli 0x53304 */
static int32_t static_scene_delta(const T31AlRcState *st, int32_t delta)
{
    if (st->i_qp_ref < st->qp)
        return delta < 0 ? -st->step : delta;
    if (st->qp < st->i_qp_ref)
        return st->step;
    return 0;
}

/* l0ii 0x52df4: QP step after a re-encoded (overflowed) picture */
static int32_t overflow_delta(const T31AlRcState *st, int32_t static_flag)
{
    int32_t v = 3;

    if (static_flag && st->opt_bit0 && !(st->qp < st->p_qp))
        v = 0;
    return v * st->step;
}

/* Ioii 0x54820: picture analysis, yields the QP delta */
static void analyse(T31AlRcState *st, const T31AlRcPicture *pic,
                    const T31AlRcStatus *status, uint32_t size_in,
                    uint8_t fl, uint32_t n8, int32_t *delta,
                    uint32_t *out_t, uint32_t *out_idle5)
{
    T31AlRcHrd *h = &st->hrd;
    uint32_t tot, p32, p28, p20, p24;
    uint32_t size = size_in;
    uint64_t p, d;
    uint32_t v0, a1, s0, s3;
    int32_t level_before, level, static_flag, remaining;
    int32_t pred_b, pred_p, pred_3;
    uint32_t cpb;

    mb_stats(status, size_in, &tot, &p32, &p28, &p20, &p24);
    if (pic->type == 1u && st->auto_ip)
        adapt_ip_delta(st, (int32_t)p28);
    /* 0x548d0: idle budget for a too-large picture */
    v0 = LO32(udiv64(U64(st->target_bitrate) * 104u, 100u));
    a1 = h->max_bitrate;
    if (a1 < v0)
        v0 = a1;
    v0 = a1 - v0;
    p = U64(v0) * 90000u;
    p = p * U64(h->pictures);
    p = p * U64(h->clk_ratio);
    d = U64(h->max_bitrate) * U64(h->fps_1000);
    *out_t = LO32(udiv64(p, d));
    /* 0x549a8: (uMaxBitRate - 0.95 * uTargetBitRate) / uMaxBitRate of the
     * elapsed stream time (5 % when max == target) */
    v0 = h->max_bitrate - LO32(udiv64(U64(st->target_bitrate) * 95u, 100u));
    p = U64(v0) * 90000u;
    p = p * U64(h->pictures);
    p = p * U64(h->clk_ratio);
    *out_idle5 = LO32(udiv64(p, d));
    level_before = hrd_level(h);
    static_flag = picture_account(st, pic, &size, n8, (int32_t)p32, (int32_t)p20);
    if (st->opt_flag)
        static_flag = 0;
    if (fl) {
        *delta = overflow_delta(st, static_flag);
        return;
    }
    if (!(pic->flags & 2u) && !(st->gop_length < 2u))
        return;
    /* 0x54af8 */
    s0 = size;
    level = hrd_level(h);
    if (pic->type != 2u) {
        int32_t a2 = sdiv4(level * 3);

        if (a2 < (int32_t)s0)
            st->qp = qp_for_target(s0, st->qp, a2, st->step_ratio[2],
                                   st->min_qp, st->max_qp);
    }
    /* 0x54b44: per-type target */
    if (st->flag320) {
        if (st->gop_mode & 8u) {
            /* 0x55198 */
            uint32_t a2 = (uint32_t)st->flag321 * 1000u;

            v0 = LO32(udiv64(U64(a2) * U64(st->target_frame), st->ratio_3 + a2));
            if (pic->type == 2u) {
                s0 = LO32(udiv64(U64(v0) * U64(st->ratio_i), 1000u));
                if (s0 == 0) s0 = 1u;
            } else if (pic->flags & 0x80u) {
                s0 = LO32(udiv64(U64(v0) * U64(st->ratio_3), 1000u));
                if (s0 == 0) s0 = 1u;
            } else {
                s0 = (int32_t)v0 > 0 ? v0 : 1u;
            }
        } else if (st->gop_length < 2u) {
            s0 = st->target_frame;
        } else {
            uint32_t gl = st->gop_length, nb = st->num_b;
            uint32_t np = udiv32(gl, nb + 1u);
            uint32_t v1 = gl - np * nb - 1u;
            uint32_t a2 = udiv32(v1, st->gop7);
            uint32_t sum;

            v1 -= a2;
            sum = a2 * st->ratio_3 + st->ratio_b * (np * nb) + st->ratio_i + v1 * 1000u;
            v0 = LO32(udiv64(U64(gl * 1000u) * U64(st->target_frame), sum));
            goto per_type;
        }
    } else if (st->gop_length < 2u || (st->gop_mode & 8u)) {
        s0 = st->target_frame;
    } else {
        uint32_t gl = st->gop_length, nb = st->num_b;
        uint32_t np = udiv32(gl, nb + 1u);
        uint32_t v1 = gl - np * nb - 1u;
        uint32_t sum;

        sum = st->ratio_b * (np * nb) + st->ratio_i + v1 * 1000u;
        v0 = LO32(udiv64(U64(gl * 1000u) * U64(st->target_frame), sum));
per_type:
        if (pic->type == 1u) {
            if (pic->flags & 0x80u) {
                s0 = LO32(udiv64(U64(v0) * U64(st->ratio_3), 1000u));
                if (s0 == 0) s0 = 1u;
            } else {
                s0 = (int32_t)v0 > 0 ? v0 : 1u;
            }
        } else if (pic->type == 0u) {
            s0 = LO32(udiv64(U64(v0) * U64(st->ratio_b), 1000u));
            if (s0 == 0) s0 = 1u;
        } else if (pic->type == 2u) {
            s0 = LO32(udiv64(U64(v0) * U64(st->ratio_i), 1000u));
            if (s0 == 0) s0 = 1u;
        } else {
            s0 = 1u; /* OEM: assert(678) */
        }
    }
    /* 0x54c98 */
    s3 = size;
#ifdef T31_AL_RC_DEBUG
    fprintf(stderr, "ioii: p32=%u p28=%u p20=%u out_t=%u idle5=%u lvl0=%d lvl=%d static=%d target=%u size=%u delta=%d idle=%u\n",
            p32, p28, p20, *out_t, *out_idle5, level_before, level, static_flag, s0, s3, *delta, h->idle_ticks);
#endif
    if ((int32_t)s3 < (int32_t)s0) {
        if (h->idle_ticks < *out_idle5 && st->capped_vbr)
            goto remaining_pictures;
        {
            uint32_t type = pic->type;
            uint64_t tgt = U64(s0) * 10000u;
            uint32_t ratio;
            int32_t d0 = *delta - 1, s2;

            if (st->flag320 && (pic->flags & 0x80u))
                type = type == 2u ? 2u : 3u;
            ratio = st->step_ratio[type & 3u];
            for (;;) {
                s3 = LO32(udiv64(U64(s3) * U64(ratio), 10000u));
                s2 = d0;
                *delta = d0;
                v0 = LO32(udiv64(tgt, s3));
                if (!(ratio < v0))
                    break;
                if (!(-(int32_t)st->max_delta < s2))
                    break;
                d0 -= 1;
            }
        }
    } else if ((int32_t)s0 < (int32_t)s3) {
        if (*out_t < h->idle_ticks)
            goto remaining_pictures;
        {
            uint32_t type = pic->type;
            uint32_t ratio;
            int32_t s1 = *delta + 1, s2;

            if (st->flag320 && (pic->flags & 0x80u))
                type = type == 2u ? 2u : 3u;
            ratio = st->step_ratio[type & 3u];
            for (;;) {
                uint32_t nsz = LO32(udiv64(U64(s3) * 10000u, ratio));

                *delta = s1;
                v0 = LO32(udiv64(U64(nsz) * 10000u, s0));
                s3 = nsz;
                s2 = s1;
                if (!(ratio < v0))
                    break;
                if (!(s2 < (int32_t)st->max_delta))
                    break;
                s1 += 1;
            }
        }
    }
remaining_pictures:
#ifdef T31_AL_RC_DEBUG
    fprintf(stderr, "ioii: after search delta=%d\n", *delta);
#endif
    /* 0x54dd8 */
    {
        int32_t s1 = st->gop_pictures;
        int32_t gl = st->gop_length;

        remaining = s1 < gl ? gl - s1 : 1;
        if (st->gop_mode & 8u) {
            int32_t cur = st->f284;
            int32_t v = cur - st->f296;

            remaining = cur;
            if (v < st->f288)
                st->f284 = st->f288;
            else
                st->f284 = v < st->f292 ? v : st->f292;
        }
    }
    cpb = h->cpb_bits;
    {
        int32_t qp = st->qp;
        int32_t base = qp + *delta;
        int32_t q0 = base + st->pb_delta;

        if (q0 < 0) q0 = 0;
        pred_b = predict_size_limited(st->model_size[0], st->model_qp[0] & 0xffffu,
                                      (uint32_t)q0 & 0xffffu, st->step_ratio[0],
                                      st->max_delta, (int32_t)cpb);
        if (base < 0) base = 0;
        pred_p = predict_size_limited(st->model_size[1], st->model_qp[1] & 0xffffu,
                                      (uint32_t)base & 0xffffu, st->step_ratio[1],
                                      st->max_delta, (int32_t)cpb);
        pred_3 = predict_size_limited(st->model_size[3], st->model_qp[3] & 0xffffu,
                                      (uint32_t)base & 0xffffu, st->step_ratio[3],
                                      st->max_delta, (int32_t)cpb);
    }
#ifdef T31_AL_RC_DEBUG
    fprintf(stderr, "ioii: remaining=%d cpb=%u preds b=%d p=%d 3=%d\n", remaining, cpb, pred_b, pred_p, pred_3);
#endif
    if (!(st->gop_mode & 8u)) {
        if (st->gop_length < 2u || pic->type == 2u) {
            /* 0x552f0 */
            int32_t now = hrd_level(h);
            int32_t third = sdiv3((int32_t)cpb);

            if (now < third && now < level_before) {
                *delta += st->step;
                goto clamp;
            }
            if (static_flag)
                *delta = static_scene_delta(st, *delta);
            goto clamp_only;
        } else {
            int32_t q = st->qp + *delta + st->ip_delta;
            int32_t pred_i, need, avail, v;
            uint32_t a2;

            if (q < 0) q = 0;
            pred_i = (int32_t)predict_size(st->model_size[2], st->model_qp[2] & 0xffffu,
                                           (uint32_t)q & 0xffffu, st->step_ratio[2],
                                           st->max_delta);
            a2 = st->num_b;
            if (a2 == 0 && st->flag320) {
                uint32_t k = st->flag321;

                need = (int32_t)LO32(udiv64(U64(remaining) * U64(k * (uint32_t)pred_p + (uint32_t)pred_3),
                                            (uint64_t)(int64_t)(int32_t)(k + 1u)));
            } else {
                need = (int32_t)LO32(udiv64(U64(remaining) * U64(a2 * (uint32_t)pred_b + (uint32_t)pred_p),
                                            (uint64_t)(int64_t)(int32_t)(a2 + 1u)));
            }
            avail = (int32_t)((uint32_t)remaining * st->max_frame + (uint32_t)level);
            v = avail - need;
            if (v < sdiv4((int32_t)cpb) + pred_i) {
                if (*delta > 0)
                    goto clamp;
                *delta += st->step;
            }
            goto clamp;
        }
    } else {
        /* pyramidal / adaptive GOP (0x54e80) */
        int32_t v;

        if (st->gop_mode == 9u) {
            v = (int32_t)((uint32_t)remaining * st->max_frame + (uint32_t)level -
                          (uint32_t)remaining * (uint32_t)pred_b);
        } else {
            uint32_t a2 = st->num_b, a0, a1v;
            int32_t need;

            if (a2 == 0 && st->flag320) {
                a2 = st->flag321;
                a0 = (uint32_t)pred_3;
                a1v = a2 * (uint32_t)pred_p;
            } else {
                a0 = (uint32_t)pred_p;
                a1v = a2 * (uint32_t)pred_b;
            }
            need = (int32_t)LO32(udiv64(U64(remaining) * U64(a1v + a0),
                                        (uint64_t)(int64_t)(int32_t)(a2 + 1u)));
            v = (int32_t)((uint32_t)remaining * st->max_frame + (uint32_t)level) - need;
        }
        if (v < sdiv3((int32_t)cpb)) {
            if (*delta <= 0)
                *delta += st->step;
            goto clamp;
        }
        if (static_flag)
            *delta = static_scene_delta(st, *delta);
        goto clamp_only;
    }
clamp:
    if (static_flag)
        *delta = static_scene_delta(st, *delta);
clamp_only:
#ifdef T31_AL_RC_DEBUG
    fprintf(stderr, "ioii: before clamp delta=%d\n", *delta);
#endif
    if (st->max_delta < *delta)
        *delta = st->max_delta;
    else if (*delta < -(int32_t)st->max_delta)
        *delta = -(int32_t)st->max_delta;
}

/* loii 0x55514: clamp the QP */
static void clamp_state_qp(T31AlRcState *st)
{
    int32_t q = st->qp;

    if (q < st->min_qp)
        st->qp = st->min_qp;
    else if (!(q < st->max_qp))
        st->qp = st->max_qp;
}

/* ------------------------------------------------------------ public */

int t31_al_rc_init(T31AlRc *rc, uint32_t mode, const T31AlRcParam *rcp,
                   const T31AlGopParam *gop)
{
    T31AlRcState *st;

    if (!rc || !rcp || !gop)
        return -1;
    if (mode != 1u && mode != 8u && mode != 9u)
        return -1;
    memset(rc, 0, sizeof(*rc));
    st = &rc->st;
    rc->mode = mode;
    /* oiii 0x52ce4 */
    st->needs_init = 1;
    st->min_qp = 0;
    st->max_qp = 51;
    st->flag300 = 0;
    st->max_delta = 4;
    st->auto_ip = 1;
    st->capped_vbr = mode != 9u;
    init_full(st, rcp, gop);
    rc->valid = 1;
    return 0;
}

void t31_al_rc_set_params(T31AlRc *rc, const T31AlRcParam *rcp,
                          const T31AlGopParam *gop)
{
    if (!rc || !rc->valid || !rcp || !gop)
        return;
    if (rc->st.needs_init)
        init_full(&rc->st, rcp, gop);
    else
        init_update(&rc->st, rcp, gop);
}

void t31_al_rc_reset(T31AlRc *rc)
{
    if (!rc || !rc->valid || rc->st.needs_init)
        return;
    reset_run(&rc->st);
}

/* Il1i 0x51cc4 */
int16_t t31_al_rc_picture_qp(T31AlRc *rc, const T31AlRcPicture *pic)
{
    T31AlRcState *st;
    uint32_t type;
    int32_t qp, v;

    if (!rc || !rc->valid || !pic)
        return 0;
    st = &rc->st;
    if (pic->fixed_qp) {
        v = pic->forced_qp;
        if (v < st->min_qp)
            v = st->min_qp;
        else if (!(v < st->max_qp))
            v = st->max_qp;
        return s16(v);
    }
    type = pic->type;
    if (st->flag320 && (pic->flags & 0x80u) && pic->type != 2u)
        type = 3u;
    if (pic->flags & 4u) {
        /* 0x51e94 */
        qp = st->qp < st->p_qp ? st->p_qp : st->qp;
    } else {
        qp = st->qp;
    }
    if (st->opt_flag && st->model_size[type & 3u] != 0 && type == 2u &&
        !(pic->flags & 4u)) {
        /* 0x51ea8..: I picture QP from the average P QP of the GOP */
        if (!(st->gop_length < 2u)) {
            uint32_t gl = st->gop_length, nb = st->num_b;
            uint32_t np = udiv32(gl, nb + 1u);
            uint32_t v1 = gl - np * nb - 1u;
            uint32_t v0 = 0;
            int32_t cnt, avg;

            if (st->flag320)
                v0 = udiv32(v1, st->flag321);
            v0 = v1 - v0;
            cnt = (int32_t)v0 > 0 ? (int32_t)v0 : 1;
            avg = sdiv32((cnt >> 1) + st->f232, cnt);
            if ((int32_t)st->f236 < (int32_t)st->model_size[1])
                avg += sdiv32((int32_t)st->model_size[1], (int32_t)st->f236);
            qp = s16(avg);
        }
    }
    /* 0x51d9c */
    {
        int32_t s4 = (int32_t)((uint32_t)(type_qp_delta(st, type) + qp) & 0xffffu);
        int32_t level = hrd_level(&st->hrd);
        int32_t s5 = s16(s4);
        uint32_t msz = st->model_size[type & 3u];

        if (msz != 0) {
            int32_t s6 = sdiv4(level * 3);

            if (s6 > 0 && s5 < st->max_qp && !(pic->flags & 4u)) {
                /* 0x51f4c */
                int32_t pred = (int32_t)predict_size(msz, st->model_qp[type & 3u] & 0xffffu,
                                                     (uint32_t)s4 & 0xffffu,
                                                     st->step_ratio[type & 3u],
                                                     st->max_qp - (int32_t)st->model_qp[type & 3u]);
                if (s6 < pred)
                    s5 = qp_for_target(msz, s16(st->model_qp[type & 3u]), s6,
                                       st->step_ratio[type & 3u], st->min_qp,
                                       st->max_qp);
            }
        }
        v = s16(s5 + pic->qp_offset);
        if (!(v < st->min_qp)) {
            if (!(v < st->max_qp))
                v = st->max_qp;
        } else {
            v = st->min_qp;
        }
        return s16(v);
    }
}

/* l01i 0x522e8: update the per-type size ratios after a picture */
static void update_ratios(T31AlRcState *st, uint32_t type, uint32_t size,
                          uint32_t qp)
{
    int32_t sz = (int32_t)size;

    switch (type) {
    case 1: {
        if (st->model_size[1] != 0)
            st->step_ratio[1] = refine_ratio(st->model_size[1], st->model_qp[1] & 0xffffu,
                                             size, qp, st->step_ratio[1], st->f316, st->f308);
        if (st->model_size[2] != 0) {
            int32_t a2 = clamp_qp(st->qp - st->ip_delta, st->min_qp, st->max_qp);
            uint32_t v0 = predict_size(st->model_size[2], st->model_qp[2] & 0xffffu,
                                       (uint32_t)a2 & 0xffffu, st->step_ratio[2], st->max_delta);

            if (st->prev_type == 2) {
                if (sdiv32((int32_t)v0, sz) < 501)
                    st->ratio_i = LO32(udiv64(U64(v0) * 1000u, size));
                else
                    st->ratio_i = 500000u;
                goto type1_p;
            }
        }
        if (st->model_size[3] != 0 && st->prev_type == 3) {
            int32_t a2 = clamp_qp(st->qp - st->f172, st->min_qp, st->max_qp);
            uint32_t v0 = predict_size(st->model_size[3], st->model_qp[3] & 0xffffu,
                                       (uint32_t)a2 & 0xffffu, st->step_ratio[3], st->max_delta);

            if (sdiv32((int32_t)v0, sz) < 501) {
                uint32_t r = LO32(udiv64(U64(v0) * 1000u, size));

                st->ratio_3 = r < 1001u ? 1000u : r;
            } else {
                st->ratio_3 = 500000u;
            }
        }
type1_p:
        if (st->model_size[0] != 0) {
            int32_t a2 = clamp_qp(st->qp + st->pb_delta, st->min_qp, st->max_qp);
            uint32_t v0 = predict_size(st->model_size[0], st->model_qp[0] & 0xffffu,
                                       (uint32_t)a2 & 0xffffu, st->step_ratio[0], st->max_delta);

            if (v0 == 0)
                st->ratio_b = 2u;
            else if (sdiv32(sz, (int32_t)v0) < 501)
                st->ratio_b = LO32(udiv64(U64(v0) * 1000u, size));
            else
                st->ratio_b = 2u;
        }
        break;
    }
    case 0: {
        int32_t a2;
        uint32_t v0;

        if (st->model_size[1] == 0)
            return;
        a2 = clamp_qp(st->qp, st->min_qp, st->max_qp);
        v0 = predict_size(st->model_size[1], st->model_qp[1] & 0xffffu,
                          (uint32_t)a2 & 0xffffu, st->step_ratio[1], st->max_delta);
        if (v0 == 0)
            st->ratio_b = 2u;
        else if (sdiv32((int32_t)v0, sz) < 501)
            st->ratio_b = LO32(udiv64(U64(size) * 1000u, v0));
        else
            st->ratio_b = 2u;
        st->step_ratio[0] = refine_ratio(st->model_size[0], st->model_qp[0] & 0xffffu,
                                         size, qp, st->step_ratio[0], st->f316, st->f312);
        break;
    }
    case 2: {
        if (st->model_size[1] != 0) {
            int32_t a2 = clamp_qp(st->qp, st->min_qp, st->max_qp);
            uint32_t v0 = predict_size(st->model_size[1], st->model_qp[1] & 0xffffu,
                                       (uint32_t)a2 & 0xffffu, st->step_ratio[1], st->max_delta);

            if (v0 == 0)
                st->ratio_i = 500000u;
            else if (sdiv32(sz, (int32_t)v0) < 501)
                st->ratio_i = LO32(udiv64(U64(size) * 1000u, v0));
            else
                st->ratio_i = 500000u;
        }
        if (!st->opt_flag && !(st->gop_length < 2u))
            return;
        if (st->model_size[2] == 0)
            return;
        st->step_ratio[2] = refine_ratio(st->model_size[2], st->model_qp[2] & 0xffffu,
                                         size, qp, st->step_ratio[2], st->f316, st->f308);
        break;
    }
    case 3: {
        if (st->model_size[1] != 0) {
            int32_t a2 = clamp_qp(st->qp, st->min_qp, st->max_qp);
            uint32_t v0 = predict_size(st->model_size[1], st->model_qp[1] & 0xffffu,
                                       (uint32_t)a2 & 0xffffu, st->step_ratio[1], st->max_delta);

            if (v0 == 0) {
                st->ratio_3 = 500000u;
            } else if (sdiv32(sz, (int32_t)v0) < 501) {
                uint32_t r = LO32(udiv64(U64(size) * 1000u, v0));

                st->ratio_3 = r < 1001u ? 1000u : r;
            } else {
                st->ratio_3 = 500000u;
            }
        }
        if (st->model_size[3] == 0)
            return;
        st->step_ratio[3] = refine_ratio(st->model_size[3], st->model_qp[3] & 0xffffu,
                                         size, qp, st->step_ratio[3], st->f316, st->f308);
        break;
    }
    default:
        break; /* OEM: assert(4215) */
    }
}

/* state[268 + 4*type]: type_count[0..2], type 3 aliases bytes 280..283 */
static int32_t cnt_get(const T31AlRcState *st, uint32_t type)
{
    int32_t v;

    memcpy(&v, ((const uint8_t *)st) + 268u + 4u * (type & 3u), 4);
    return v;
}
static void cnt_set(T31AlRcState *st, uint32_t type, int32_t v)
{
    memcpy(((uint8_t *)st) + 268u + 4u * (type & 3u), &v, 4);
}

/* iili 0x5189c: resynchronise the QP from the encoded picture */
static void sync_qp(T31AlRcState *st, const T31AlRcPicture *pic, int32_t qp)
{
    int32_t s2 = st->qp, s3;

    s3 = st->opt_bit1 ? st->qp_sync : s2;
    st->qp_sync = s16(s2);
    st->qp = s16(qp - type_qp_delta(st, pic->type) - s3 + s2);
}

/* o11i 0x529d8 */
int32_t t31_al_rc_picture_start(T31AlRc *rc, const T31AlRcPicture *pic,
                                const T31AlRcStatus *status, uint32_t size_bits)
{
    T31AlRcState *st;
    int32_t filler;
    uint32_t tot, p28, p32, s3, type;
    int32_t qp, s6;

    if (!rc || !rc->valid || !pic || !status)
        return 0;
    st = &rc->st;
    filler = hrd_filler(&st->hrd, size_bits);
    if (pic->type == 7u)
        return filler;
    tot = status->stat40 * 4u + status->stat44 * 16u + status->stat36 +
          status->stat48 * 64u;
    p28 = udiv32(status->stat28 * 100u, tot);
    p32 = udiv32(status->stat32 * 100u, tot);
    s3 = 2u;
    if ((int32_t)p28 < 81) {
        s3 = pic->type;
        if (st->flag320 && (pic->flags & 0x80u))
            s3 = 3u;
    }
    if (!pic->fixed_qp && filler >= 0)
        sync_qp(st, pic, s16(status->qp - pic->qp_offset));
    qp = st->qp + type_qp_delta(st, s3);
    s6 = qp < st->min_qp ? st->min_qp : (qp < st->max_qp ? qp : st->max_qp);
    if ((int32_t)p32 < 95)
        update_ratios(st, s3, size_bits, (uint32_t)s6);
    type = pic->type;
    if (s3 != type && s3 != 3u)
        cnt_set(st, type, cnt_get(st, type) + 1);
    else
        cnt_set(st, type, 0);           /* 0x52c60 */
    if (st->last_type == 1 && s3 == 1u) {
        int32_t r = st->p20_ref;

        if (r + 60 < (int32_t)p32 || (int32_t)p32 < r - 60) {
            int32_t v1 = (int32_t)(size_bits + st->last_size);

            st->model_size[1] = (uint32_t)((v1 + ((uint32_t)v1 >> 31)) >> 1);
            goto skip_model;
        }
    }
    st->model_size[s3 & 3u] = size_bits;
    st->model_qp[s3 & 3u] = (uint32_t)s6;
skip_model:
    st->last_size = size_bits;      /* 0x52ba0, both paths */
    if (cnt_get(st, type) >= 3) {
        st->model_size[type & 3u] = size_bits;
        st->model_qp[type & 3u] = (uint32_t)s6;
        cnt_set(st, type, 0);
    }
    if (type == 1u)
        st->f232 += s6;
    else if (type == 2u)
        st->f232 = 0;
    if (pic->flags & 2u) {
        st->prev_type = st->last_type;
        st->last_type = (int32_t)type;
    }
    return filler;
}

int32_t t31_al_rc_psnr_x100(uint64_t sse, uint32_t num_pixels, uint32_t max_pel)
{
    uint64_t mse1000 = udiv64(sse * 1000u, (uint64_t)(int64_t)(int32_t)num_pixels);
    uint64_t ratio;
    int32_t pk2_1000 = (int32_t)((max_pel * max_pel) * 1000u);
    double v;

    if (mse1000 == 0u)
        mse1000 = 1u;
    ratio = udiv64((uint64_t)(int64_t)pk2_1000, mse1000);
    v = log10((double)ratio) * 1000.0;
    if (!(v < 2147483648.0) || !(v > -2147483649.0))
        return INT32_MAX;   /* MIPS trunc.w.d of an invalid value */
    return (int32_t)v;
}

void t31_al_rc_update(T31AlRc *rc, const T31AlRcPicture *pic,
                      const T31AlRcStatus *status, uint32_t size_bits,
                      uint8_t overflow, uint32_t extra_bits)
{
    T31AlRcState *st;
    int32_t delta = 0;
    uint32_t out_t = 0, out_idle5 = 0;

    if (!rc || !rc->valid || !pic || !status)
        return;
    st = &rc->st;
    analyse(st, pic, status, size_bits, overflow, extra_bits, &delta, &out_t,
            &out_idle5);
    if (rc->mode != 1u) {
        /* Ooii 0x55540: the quality cap */
        uint64_t sse = ((uint64_t)status->sse_hi << 32) | status->sse_lo;
        int32_t psnr = t31_al_rc_psnr_x100(sse, st->num_pixels, st->max_pel);

        if ((int32_t)st->max_psnr < psnr && delta < 0)
            delta = 0;
    }
    st->qp = (int16_t)(uint16_t)((uint16_t)st->qp + (uint16_t)delta);
    clamp_state_qp(st);
}

void t31_al_rc_status_from_regs(T31AlRcStatus *status, const uint8_t *regs,
                                unsigned int regs_len)
{
    uint32_t v;

    memset(status, 0, sizeof(*status));
    if (!regs || regs_len < 0x160u)
        return;
#define RD(off) (memcpy(&v, regs + (off), 4), v)
    status->stat20 = RD(0x10c);
    status->stat24 = RD(0x110);
    status->stat28 = RD(0x114);
    status->stat32 = RD(0x11c);
    status->stat36 = RD(0x120);
    status->stat40 = RD(0x124);
    status->stat44 = RD(0x128);
    status->stat48 = RD(0x12c) & 0xffffu;
    status->sse_lo = RD(0x15c);
    status->sse_hi = RD(0x158);
#undef RD
}

void t31_al_rc_frame_rate(uint32_t num, uint32_t den, uint16_t *frame_rate,
                          uint16_t *clk_ratio)
{
    /* c_reduce_fraction 0x349e4 */
    if (num != 0u && den != 0u) {
        uint32_t a = num, b = den, r = a % b;

        if (r != 0u) {
            uint32_t x = b, y = r;

            for (;;) {
                uint32_t m = x % y;

                if (m == 0u)
                    break;
                x = y;
                y = m;
            }
            b = y;
        }
        num /= b;
        den /= b;
    }
    *frame_rate = (uint16_t)num;
    *clk_ratio = (uint16_t)(den * 1000u);
}
