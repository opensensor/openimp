/* Built-in AENC/ADEC codecs, see openimp_t31_acodec_core.h.
 *
 * G.711 and G.726 follow the Sun Microsystems reference implementation
 * (released for unrestricted use), IMA ADPCM follows Jack Jansen's
 * public-domain adpcm.c. Arithmetic is kept in the same 16-bit widths the
 * vendor libimp uses (quantize/fmult/update/reconstruct in libimp 1.1.6)
 * so the bitstreams are identical.
 */

#include "t31/openimp_t31_acodec_core.h"

#include <string.h>

/* ---------------------------------------------------------------- G.711 */

static const int16_t seg_aend[8] = {
    0x1f, 0x3f, 0x7f, 0xff, 0x1ff, 0x3ff, 0x7ff, 0xfff
};
static const int16_t seg_uend[8] = {
    0x3f, 0x7f, 0xff, 0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff
};

static int g711_search(int value, const int16_t *table, int size)
{
    int i;

    for (i = 0; i < size; i++)
        if (value <= table[i])
            return i;
    return size;
}

uint8_t openimp_linear2alaw(int16_t pcm)
{
    int value = pcm >> 3;
    int mask;
    int seg;
    int aval;

    if (value >= 0) {
        mask = 0xd5;
    } else {
        mask = 0x55;
        value = -value - 1;
    }
    seg = g711_search(value, seg_aend, 8);
    if (seg >= 8)
        return (uint8_t)(0x7f ^ mask);
    aval = seg << 4;
    if (seg < 2)
        aval |= (value >> 1) & 0x0f;
    else
        aval |= (value >> seg) & 0x0f;
    return (uint8_t)(aval ^ mask);
}

uint8_t openimp_linear2ulaw(int16_t pcm)
{
    int value = pcm >> 2;
    int mask;
    int seg;

    if (value < 0) {
        value = -value;
        mask = 0x7f;
    } else {
        mask = 0xff;
    }
    if (value > 0x1fdf)
        value = 0x1fdf;
    value += 0x21;
    seg = g711_search(value, seg_uend, 8);
    if (seg >= 8)
        return (uint8_t)(0x7f ^ mask);
    return (uint8_t)(((seg << 4) | ((value >> (seg + 1)) & 0x0f)) ^ mask);
}

int16_t openimp_alaw2linear(uint8_t code)
{
    int value = code ^ 0x55;
    int t = (value & 0x0f) << 4;
    int seg = (value & 0x70) >> 4;

    switch (seg) {
    case 0:
        t += 8;
        break;
    case 1:
        t += 0x108;
        break;
    default:
        t += 0x108;
        t <<= seg - 1;
        break;
    }
    return (int16_t)((value & 0x80) ? t : -t);
}

int16_t openimp_ulaw2linear(uint8_t code)
{
    int value = (uint8_t)~code;
    int t = ((value & 0x0f) << 3) + 0x84;

    t <<= (value & 0x70) >> 4;
    return (int16_t)((value & 0x80) ? (0x84 - t) : (t - 0x84));
}

int openimp_g711a_encode(uint8_t *out, const int16_t *in, int samples)
{
    int i;

    for (i = 0; i < samples; i++)
        out[i] = openimp_linear2alaw(in[i]);
    return samples > 0 ? samples : 0;
}

int openimp_g711u_encode(uint8_t *out, const int16_t *in, int samples)
{
    int i;

    for (i = 0; i < samples; i++)
        out[i] = openimp_linear2ulaw(in[i]);
    return samples > 0 ? samples : 0;
}

int openimp_g711a_decode(int16_t *out, const uint8_t *in, int bytes)
{
    int i;

    for (i = 0; i < bytes; i++)
        out[i] = openimp_alaw2linear(in[i]);
    return bytes > 0 ? bytes * 2 : 0;
}

int openimp_g711u_decode(int16_t *out, const uint8_t *in, int bytes)
{
    int i;

    for (i = 0; i < bytes; i++)
        out[i] = openimp_ulaw2linear(in[i]);
    return bytes > 0 ? bytes * 2 : 0;
}

/* ------------------------------------------------------------ IMA ADPCM */

static const int adpcm_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8,
};

static const int adpcm_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

void openimp_adpcm_init(OpenIMPAdpcmState *state)
{
    state->valprev = 0;
    state->index = 0;
}

static int adpcm_clamp_index(int index)
{
    if (index < 0)
        return 0;
    if (index > 88)
        return 88;
    return index;
}

int openimp_adpcm_encode(OpenIMPAdpcmState *state, uint8_t *out,
                         const int16_t *in, int samples)
{
    int valpred = state->valprev;
    int index = state->index;
    int step = adpcm_step_table[adpcm_clamp_index(index)];
    int bufferstep = 1;
    int outputbuffer = 0;
    int written = 0;
    int i;

    for (i = 0; i < samples; i++) {
        int diff = in[i] - valpred;
        int sign = 0;
        int delta = 0;
        int vpdiff;
        int half;

        if (diff < 0) {
            sign = 8;
            diff = -diff;
        }
        vpdiff = step >> 3;
        if (diff >= step) {
            delta = 4;
            diff -= step;
            vpdiff += step;
        }
        half = step >> 1;
        if (diff >= half) {
            delta |= 2;
            diff -= half;
            vpdiff += half;
        }
        half >>= 1;
        if (diff >= half) {
            delta |= 1;
            vpdiff += half;
        }
        valpred = sign ? valpred - vpdiff : valpred + vpdiff;
        if (valpred < -32768)
            valpred = -32768;
        else if (valpred > 32767)
            valpred = 32767;
        delta |= sign;
        index += adpcm_index_table[delta];
        if (index < 0) {
            index = 0;
            step = 7;
        } else if (index < 89) {
            step = adpcm_step_table[index];
        } else {
            index = 88;
            step = 32767;
        }
        if (bufferstep) {
            outputbuffer = (delta << 4) & 0xf0;
        } else {
            out[written++] = (uint8_t)(delta | outputbuffer);
        }
        bufferstep ^= 1;
    }
    if (!bufferstep)
        out[written++] = (uint8_t)outputbuffer;
    state->valprev = (int16_t)valpred;
    state->index = (int8_t)index;
    return written;
}

int openimp_adpcm_decode(OpenIMPAdpcmState *state, int16_t *out,
                         const uint8_t *in, int bytes)
{
    int valpred = state->valprev;
    int index = state->index;
    int step = adpcm_step_table[adpcm_clamp_index(index)];
    int samples = bytes > 0 ? bytes * 2 : 0;
    int inputbuffer = 0;
    int bufferstep = 0;
    int i;

    for (i = 0; i < samples; i++) {
        int delta;
        int next_step;
        int vpdiff;

        if (!bufferstep) {
            inputbuffer = *in++;
            delta = (inputbuffer >> 4) & 0x0f;
        } else {
            delta = inputbuffer & 0x0f;
        }
        bufferstep ^= 1;
        index += adpcm_index_table[delta];
        if (index < 0) {
            index = 0;
            next_step = 7;
        } else if (index < 89) {
            next_step = adpcm_step_table[index];
        } else {
            index = 88;
            next_step = 32767;
        }
        vpdiff = step >> 3;
        if (delta & 4)
            vpdiff += step;
        if (delta & 2)
            vpdiff += step >> 1;
        if (delta & 1)
            vpdiff += step >> 2;
        valpred = (delta & 8) ? valpred - vpdiff : valpred + vpdiff;
        if (valpred < -32768)
            valpred = -32768;
        else if (valpred > 32767)
            valpred = 32767;
        out[i] = (int16_t)valpred;
        step = next_step;
    }
    state->valprev = (int16_t)valpred;
    state->index = (int8_t)index;
    return samples * 2;
}

/* ---------------------------------------------------------- G.726 16k */

#define G726_16_BITS 2

static const int16_t g726_power2[15] = {
    1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80,
    0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000
};
static const int16_t g726_16_qtab[1] = { 261 };
static const int16_t g726_16_dqlntab[4] = { 116, 365, 365, 116 };
static const int16_t g726_16_witab[4] = { -704, 14048, 14048, -704 };
static const int16_t g726_16_fitab[4] = { 0, 0xe00, 0xe00, 0 };

static int g726_quan(int value, const int16_t *table, int size)
{
    int i;

    for (i = 0; i < size; i++)
        if (value < table[i])
            break;
    return i;
}

static int16_t g726_fmult(int an, int srn)
{
    int16_t anmag = (int16_t)(an > 0 ? an : ((-an) & 0x1fff));
    int16_t anexp = (int16_t)(g726_quan(anmag, g726_power2, 15) - 6);
    int16_t anmant;
    int16_t wanexp;
    int16_t wanmant;
    int16_t retval;

    if (anmag == 0)
        anmant = 32;
    else if (anexp >= 0)
        anmant = (int16_t)(anmag >> anexp);
    else
        anmant = (int16_t)(anmag << -anexp);
    wanexp = (int16_t)(anexp + ((srn >> 6) & 0x0f) - 13);
    wanmant = (int16_t)((anmant * (srn & 077) + 0x30) >> 4);
    retval = (int16_t)(wanexp >= 0 ? ((wanmant << wanexp) & 0x7fff)
                                   : (wanmant >> -wanexp));
    return (int16_t)(((an ^ srn) < 0) ? -retval : retval);
}

static int g726_predictor_zero(const OpenIMPG726State *st)
{
    int sezi = g726_fmult(st->b[0] >> 2, st->dq[0]);
    int i;

    for (i = 1; i < 6; i++)
        sezi += g726_fmult(st->b[i] >> 2, st->dq[i]);
    return sezi;
}

static int g726_predictor_pole(const OpenIMPG726State *st)
{
    return g726_fmult(st->a[1] >> 2, st->sr[1]) +
           g726_fmult(st->a[0] >> 2, st->sr[0]);
}

static int g726_step_size(const OpenIMPG726State *st)
{
    int y;
    int dif;
    int al;

    if (st->ap >= 256)
        return st->yu;
    y = st->yl >> 6;
    dif = st->yu - y;
    al = st->ap >> 2;
    if (dif > 0)
        y += (dif * al) >> 6;
    else if (dif < 0)
        y += (dif * al + 0x3f) >> 6;
    return y;
}

/* libimp's quantize(): "size" is the number of codes; a non-negative
 * difference below the first threshold yields code 0 for an even size. */
static int g726_quantize(int d, int y, const int16_t *table, int size)
{
    int16_t dqm = (int16_t)(d < 0 ? -d : d);
    int16_t exp = (int16_t)g726_quan(dqm >> 1, g726_power2, 15);
    int16_t mant = (int16_t)(((dqm * 128) >> exp) & 0x7f);
    int16_t dl = (int16_t)((exp << 7) + mant);
    int16_t dln = (int16_t)(dl - (y >> 2));
    int entries = (size - 1) >> 1;
    int i = g726_quan(dln, table, entries);

    if (d < 0)
        return (entries << 1) + 1 - i;
    if (i == 0)
        return (size & 1) ? size : 0;
    return i;
}

static int g726_reconstruct(int sign, int dqln, int y)
{
    int16_t dql = (int16_t)(dqln + (y >> 2));
    int16_t dex;
    int16_t dqt;
    int16_t dq;

    if (dql < 0)
        return sign ? -0x8000 : 0;
    dex = (int16_t)((dql >> 7) & 15);
    dqt = (int16_t)(128 + (dql & 127));
    dq = (int16_t)((dqt << 7) >> (14 - dex));
    return sign ? (int16_t)(dq - 0x8000) : dq;
}

static void g726_update(int code_size, int y, int wi, int fi, int dq, int sr,
                        int dqsez, OpenIMPG726State *st)
{
    int cnt;
    int16_t mag;
    int16_t exp;
    int16_t a2p = 0;
    int16_t a1ul;
    int16_t pks1;
    int16_t fa1;
    int tr;
    int16_t ylint;
    int16_t thr1;
    int16_t thr2;
    int16_t dqthr;
    int16_t ylfrac;
    int16_t pk0;

    pk0 = (int16_t)(dqsez < 0 ? 1 : 0);
    mag = (int16_t)(dq & 0x7fff);
    ylint = (int16_t)(st->yl >> 15);
    ylfrac = (int16_t)((st->yl >> 10) & 0x1f);
    thr1 = (int16_t)((32 + ylfrac) << ylint);
    thr2 = (int16_t)(ylint > 9 ? 31 << 10 : thr1);
    dqthr = (int16_t)((thr2 + (thr2 >> 1)) >> 1);
    if (st->td == 0)
        tr = 0;
    else if (mag <= dqthr)
        tr = 0;
    else
        tr = 1;

    st->yu = (int16_t)(y + ((wi - y) >> 5));
    if (st->yu < 544)
        st->yu = 544;
    else if (st->yu > 5120)
        st->yu = 5120;
    st->yl += st->yu + ((-st->yl) >> 6);

    if (tr == 1) {
        st->a[0] = 0;
        st->a[1] = 0;
        memset(st->b, 0, sizeof(st->b));
    } else {
        pks1 = (int16_t)(pk0 ^ st->pk[0]);
        a2p = (int16_t)(st->a[1] - (st->a[1] >> 7));
        if (dqsez != 0) {
            fa1 = (int16_t)(pks1 ? st->a[0] : -st->a[0]);
            if (fa1 < -8191)
                a2p = (int16_t)(a2p - 0x100);
            else if (fa1 > 8191)
                a2p = (int16_t)(a2p + 0xff);
            else
                a2p = (int16_t)(a2p + (fa1 >> 5));
            if (pk0 ^ st->pk[1]) {
                if (a2p <= -12160)
                    a2p = -12288;
                else if (a2p >= 12416)
                    a2p = 12288;
                else
                    a2p = (int16_t)(a2p - 0x80);
            } else if (a2p <= -12416) {
                a2p = -12288;
            } else if (a2p >= 12160) {
                a2p = 12288;
            } else {
                a2p = (int16_t)(a2p + 0x80);
            }
        }
        st->a[1] = a2p;
        st->a[0] = (int16_t)(st->a[0] - (st->a[0] >> 8));
        if (dqsez != 0)
            st->a[0] = (int16_t)(pks1 == 0 ? st->a[0] + 192
                                           : st->a[0] - 192);
        a1ul = (int16_t)(15360 - a2p);
        if (st->a[0] < -a1ul)
            st->a[0] = (int16_t)-a1ul;
        else if (st->a[0] > a1ul)
            st->a[0] = a1ul;
        for (cnt = 0; cnt < 6; cnt++) {
            if (code_size == 5)
                st->b[cnt] = (int16_t)(st->b[cnt] - (st->b[cnt] >> 9));
            else
                st->b[cnt] = (int16_t)(st->b[cnt] - (st->b[cnt] >> 8));
            if (dq & 0x7fff) {
                if ((dq ^ st->dq[cnt]) >= 0)
                    st->b[cnt] = (int16_t)(st->b[cnt] + 128);
                else
                    st->b[cnt] = (int16_t)(st->b[cnt] - 128);
            }
        }
    }

    for (cnt = 5; cnt > 0; cnt--)
        st->dq[cnt] = st->dq[cnt - 1];
    if (mag == 0) {
        st->dq[0] = (int16_t)(dq >= 0 ? 0x20 : 0xfc20);
    } else {
        exp = (int16_t)g726_quan(mag, g726_power2, 15);
        st->dq[0] = (int16_t)(dq >= 0
                                  ? (exp << 6) + ((mag << 6) >> exp)
                                  : (exp << 6) + ((mag << 6) >> exp) - 0x400);
    }

    st->sr[1] = st->sr[0];
    if (sr == 0) {
        st->sr[0] = 0x20;
    } else if (sr > 0) {
        exp = (int16_t)g726_quan(sr, g726_power2, 15);
        st->sr[0] = (int16_t)((exp << 6) + ((sr << 6) >> exp));
    } else if (sr > -32768) {
        mag = (int16_t)-sr;
        exp = (int16_t)g726_quan(mag, g726_power2, 15);
        st->sr[0] = (int16_t)((exp << 6) + ((mag << 6) >> exp) - 0x400);
    } else {
        st->sr[0] = (int16_t)0xfc20;
    }

    st->pk[1] = st->pk[0];
    st->pk[0] = pk0;

    if (tr == 1)
        st->td = 0;
    else if (a2p < -11776)
        st->td = 1;
    else
        st->td = 0;

    st->dms = (int16_t)(st->dms + ((fi - st->dms) >> 5));
    st->dml = (int16_t)(st->dml + (((int16_t)(fi << 2) - st->dml) >> 7));

    if (tr == 1) {
        st->ap = 256;
    } else if (y < 1536) {
        st->ap = (int16_t)(st->ap + ((0x200 - st->ap) >> 4));
    } else if (st->td == 1) {
        st->ap = (int16_t)(st->ap + ((0x200 - st->ap) >> 4));
    } else {
        int diff = st->dms * 4 - st->dml;

        if (diff < 0)
            diff = -diff;
        if (diff >= (st->dml >> 3))
            st->ap = (int16_t)(st->ap + ((0x200 - st->ap) >> 4));
        else
            st->ap = (int16_t)(st->ap + ((-st->ap) >> 4));
    }
}

void openimp_g726_16_init(OpenIMPG726State *state)
{
    int i;

    memset(state, 0, sizeof(*state));
    state->yl = 34816;
    state->yu = 544;
    for (i = 0; i < 2; i++)
        state->sr[i] = 32;
    for (i = 0; i < 6; i++)
        state->dq[i] = 32;
}

static int g726_16_encode_sample(OpenIMPG726State *st, int sl)
{
    int sezi = g726_predictor_zero(st);
    int16_t sez = (int16_t)((int16_t)sezi >> 1);
    int16_t se = (int16_t)((int16_t)(sezi + g726_predictor_pole(st)) >> 1);
    int16_t d = (int16_t)(sl - se);
    int y = g726_step_size(st);
    int i = g726_quantize(d, y, g726_16_qtab, 4);
    int16_t dq = (int16_t)g726_reconstruct(i & 2, g726_16_dqlntab[i], y);
    int16_t sr = (int16_t)(dq < 0 ? se - (dq & 0x3fff) : se + dq);
    int16_t dqsez = (int16_t)(sr + sez - se);

    g726_update(G726_16_BITS, y, g726_16_witab[i], g726_16_fitab[i], dq, sr,
                dqsez, st);
    return i;
}

static int16_t g726_16_decode_sample(OpenIMPG726State *st, int code)
{
    int sezi = g726_predictor_zero(st);
    int16_t sez = (int16_t)((int16_t)sezi >> 1);
    int16_t se = (int16_t)((int16_t)(sezi + g726_predictor_pole(st)) >> 1);
    int y = g726_step_size(st);
    int16_t dq;
    int16_t sr;
    int16_t dqsez;

    code &= 3;
    dq = (int16_t)g726_reconstruct(code & 2, g726_16_dqlntab[code], y);
    sr = (int16_t)(dq < 0 ? se - (dq & 0x3fff) : se + dq);
    dqsez = (int16_t)(sr - se + sez);
    g726_update(G726_16_BITS, y, g726_16_witab[code], g726_16_fitab[code], dq,
                sr, dqsez, st);
    return (int16_t)(sr * 4);
}

int openimp_g726_16_encode(OpenIMPG726State *state, uint8_t *out,
                           const int16_t *in, int samples)
{
    int written = 0;
    int i;

    for (i = 0; i < samples; i++) {
        int code = g726_16_encode_sample(state, in[i] >> 2);

        state->bit_buffer = (state->bit_buffer << G726_16_BITS) |
                            (uint32_t)code;
        state->bit_count += G726_16_BITS;
        if (state->bit_count >= 8) {
            state->bit_count -= 8;
            out[written++] =
                (uint8_t)(state->bit_buffer >> state->bit_count);
        }
    }
    return written;
}

int openimp_g726_16_decode(OpenIMPG726State *state, int16_t *out,
                           const uint8_t *in, int bytes)
{
    int consumed = 0;
    int written = 0;

    for (;;) {
        if (state->bit_count < G726_16_BITS) {
            if (consumed >= bytes)
                break;
            state->bit_buffer = (state->bit_buffer << 8) | in[consumed++];
            state->bit_count += 8;
        }
        state->bit_count -= G726_16_BITS;
        out[written++] = g726_16_decode_sample(
            state, (int)((state->bit_buffer >> state->bit_count) &
                         ((1u << G726_16_BITS) - 1u)));
    }
    return written;
}
