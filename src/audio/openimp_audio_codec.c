/* Built-in AENC/ADEC speech codecs.  The algorithms are the classic public
 * reference implementations the OEM libimp is built from (its g711codec.c,
 * g726codec.c and adpcm.c objects): the CCITT G.711/G.721 code released by
 * Sun Microsystems for unrestricted use, and the IMA ADPCM coder by Jack
 * Jansen.  Bit packing and state handling follow the OEM objects. */

#include "audio/openimp_audio_codec.h"

#include <string.h>

/* ---- G.711 ------------------------------------------------------------ */

static const int16_t seg_aend[8] = {
    0x1F, 0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF
};
static const int16_t seg_uend[8] = {
    0x3F, 0x7F, 0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF
};

static int seg_search(int val, const int16_t *table, int size)
{
    int i;

    for (i = 0; i < size; i++) {
        if (val <= table[i])
            return i;
    }
    return size;
}

static uint8_t linear2alaw(int16_t pcm)
{
    int val = pcm >> 3;
    int mask, seg;
    uint8_t aval;

    if (val >= 0) {
        mask = 0xD5;
    } else {
        mask = 0x55;
        val = -val - 1;
    }
    seg = seg_search(val, seg_aend, 8);
    if (seg >= 8)
        return (uint8_t)(0x7F ^ mask);
    aval = (uint8_t)(seg << 4);
    aval |= (uint8_t)((seg < 2 ? val >> 1 : val >> seg) & 0x0F);
    return (uint8_t)(aval ^ mask);
}

static int16_t alaw2linear(uint8_t a)
{
    int t, seg;

    a ^= 0x55;
    t = (a & 0x0F) << 4;
    seg = (a & 0x70) >> 4;
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
    return (int16_t)((a & 0x80) ? t : -t);
}

#define ULAW_BIAS 0x84
#define ULAW_CLIP 8159

static uint8_t linear2ulaw(int16_t pcm)
{
    int val = pcm >> 2;
    int mask, seg;

    if (val < 0) {
        val = -val;
        mask = 0x7F;
    } else {
        mask = 0xFF;
    }
    if (val > ULAW_CLIP)
        val = ULAW_CLIP;
    val += ULAW_BIAS >> 2;
    seg = seg_search(val, seg_uend, 8);
    if (seg >= 8)
        return (uint8_t)(0x7F ^ mask);
    return (uint8_t)(((seg << 4) | ((val >> (seg + 1)) & 0x0F)) ^ mask);
}

static int16_t ulaw2linear(uint8_t u)
{
    int t;

    u = (uint8_t)~u;
    t = ((u & 0x0F) << 3) + ULAW_BIAS;
    t <<= (u & 0x70) >> 4;
    return (int16_t)((u & 0x80) ? ULAW_BIAS - t : t - ULAW_BIAS);
}

int openimp_g711a_encode(uint8_t *out, const int16_t *in, int samples)
{
    for (int i = 0; i < samples; i++)
        out[i] = linear2alaw(in[i]);
    return samples > 0 ? samples : 0;
}

int openimp_g711u_encode(uint8_t *out, const int16_t *in, int samples)
{
    for (int i = 0; i < samples; i++)
        out[i] = linear2ulaw(in[i]);
    return samples > 0 ? samples : 0;
}

int openimp_g711a_decode(int16_t *out, const uint8_t *in, int bytes)
{
    for (int i = 0; i < bytes; i++)
        out[i] = alaw2linear(in[i]);
    return bytes > 0 ? bytes * 2 : 0;
}

int openimp_g711u_decode(int16_t *out, const uint8_t *in, int bytes)
{
    for (int i = 0; i < bytes; i++)
        out[i] = ulaw2linear(in[i]);
    return bytes > 0 ? bytes * 2 : 0;
}

/* ---- G.726 32 kbit/s (G.721) ------------------------------------------ */

static const int16_t power2[15] = {
    1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000,
    0x2000, 0x4000
};

static const int16_t qtab_721[7] = { -124, 80, 178, 246, 300, 349, 400 };
static const int16_t dqlntab[16] = {
    -2048, 4, 135, 213, 273, 323, 373, 425, 425, 373, 323, 273, 213, 135, 4,
    -2048
};
static const int16_t witab[16] = {
    -12, 18, 41, 64, 112, 198, 355, 1122, 1122, 355, 198, 112, 64, 41, 18, -12
};
static const int16_t fitab[16] = {
    0, 0, 0, 0x200, 0x200, 0x200, 0x600, 0xE00, 0xE00, 0x600, 0x200, 0x200,
    0x200, 0, 0, 0
};

static int quan(int val, const int16_t *table, int size)
{
    int i;

    for (i = 0; i < size; i++) {
        if (val < table[i])
            break;
    }
    return i;
}

static int fmult(int an, int srn)
{
    int anmag = an > 0 ? an : ((-an) & 0x1FFF);
    int anexp = quan(anmag, power2, 15) - 6;
    int anmant = anmag == 0 ? 32
               : anexp >= 0 ? anmag >> anexp : anmag << -anexp;
    int wanexp = anexp + ((srn >> 6) & 0xF) - 13;
    int wanmant = (anmant * (srn & 077) + 0x30) >> 4;
    int retval = wanexp >= 0 ? ((wanmant << wanexp) & 0x7FFF)
                             : (wanmant >> -wanexp);

    return (an ^ srn) < 0 ? -retval : retval;
}

void openimp_g726_init(OpenIMPG726State *s)
{
    memset(s, 0, sizeof(*s));
    s->yl = 34816;
    s->yu = 544;
    for (int i = 0; i < 2; i++)
        s->sr[i] = 32;
    for (int i = 0; i < 6; i++)
        s->dq[i] = 32;
}

static int predictor_zero(const OpenIMPG726State *s)
{
    int sezi = fmult(s->b[0] >> 2, s->dq[0]);

    for (int i = 1; i < 6; i++)
        sezi += fmult(s->b[i] >> 2, s->dq[i]);
    return sezi;
}

static int predictor_pole(const OpenIMPG726State *s)
{
    return fmult(s->a[1] >> 2, s->sr[1]) + fmult(s->a[0] >> 2, s->sr[0]);
}

static int step_size(const OpenIMPG726State *s)
{
    int y, dif, al;

    if (s->ap >= 256)
        return s->yu;
    y = s->yl >> 6;
    dif = s->yu - y;
    al = s->ap >> 2;
    if (dif > 0)
        y += (dif * al) >> 6;
    else if (dif < 0)
        y += (dif * al + 0x3F) >> 6;
    return y;
}

static int quantize(int d, int y, const int16_t *table, int size)
{
    int dqm = d < 0 ? -d : d;
    int exp = quan(dqm >> 1, power2, 15);
    int mant = ((dqm << 7) >> exp) & 0x7F;
    int dl = (exp << 7) + mant;
    int dln = dl - (y >> 2);
    int i = quan(dln, table, size);

    if (d < 0)
        return (size << 1) + 1 - i;
    if (i == 0)
        return (size << 1) + 1;
    return i;
}

static int reconstruct(int sign, int dqln, int y)
{
    int dql = dqln + (y >> 2);
    int dex, dqt, dq;

    if (dql < 0)
        return sign ? -0x8000 : 0;
    dex = (dql >> 7) & 15;
    dqt = 128 + (dql & 127);
    dq = (dqt << 7) >> (14 - dex);
    return sign ? dq - 0x8000 : dq;
}

static int16_t log_float(int v)
{
    int exp, mag;

    if (v == 0)
        return 0x20;
    if (v > 0) {
        exp = quan(v, power2, 15);
        return (int16_t)((exp << 6) + ((v << 6) >> exp));
    }
    if (v > -32768) {
        mag = -v;
        exp = quan(mag, power2, 15);
        return (int16_t)((exp << 6) + ((mag << 6) >> exp) - 0x400);
    }
    return (int16_t)0xFC20;
}

static void update(int y, int wi, int fi, int dq, int sr, int dqsez,
                   OpenIMPG726State *s)
{
    int pk0 = dqsez < 0 ? 1 : 0;
    int mag = dq & 0x7FFF;
    int ylint = s->yl >> 15;
    int ylfrac = (s->yl >> 10) & 0x1F;
    int thr1 = (32 + ylfrac) << ylint;
    int thr2 = ylint > 9 ? 31 << 10 : thr1;
    int dqthr = (thr2 + (thr2 >> 1)) >> 1;
    int tr = s->td != 0 && mag > dqthr;
    int a2p = 0;
    int i;

    s->yu = (int16_t)(y + ((wi - y) >> 5));
    if (s->yu < 544)
        s->yu = 544;
    else if (s->yu > 5120)
        s->yu = 5120;
    s->yl += s->yu + ((-s->yl) >> 6);

    if (tr) {
        s->a[0] = s->a[1] = 0;
        for (i = 0; i < 6; i++)
            s->b[i] = 0;
    } else {
        int pks1 = pk0 ^ s->pk[0];
        int a1ul;

        a2p = s->a[1] - (s->a[1] >> 7);
        if (dqsez != 0) {
            int fa1 = pks1 ? s->a[0] : -s->a[0];

            if (fa1 < -8191)
                a2p -= 0x100;
            else if (fa1 > 8191)
                a2p += 0xFF;
            else
                a2p += fa1 >> 5;
            if (pk0 ^ s->pk[1]) {
                if (a2p <= -12160)
                    a2p = -12288;
                else if (a2p >= 12416)
                    a2p = 12288;
                else
                    a2p -= 0x80;
            } else if (a2p <= -12416) {
                a2p = -12288;
            } else if (a2p >= 12160) {
                a2p = 12288;
            } else {
                a2p += 0x80;
            }
        }
        s->a[1] = (int16_t)a2p;
        s->a[0] -= s->a[0] >> 8;
        if (dqsez != 0)
            s->a[0] += pks1 == 0 ? 192 : -192;
        a1ul = 15360 - a2p;
        if (s->a[0] < -a1ul)
            s->a[0] = (int16_t)-a1ul;
        else if (s->a[0] > a1ul)
            s->a[0] = (int16_t)a1ul;
        for (i = 0; i < 6; i++) {
            s->b[i] -= s->b[i] >> 8;
            if (dq & 0x7FFF)
                s->b[i] += (dq ^ s->dq[i]) >= 0 ? 128 : -128;
        }
    }

    for (i = 5; i > 0; i--)
        s->dq[i] = s->dq[i - 1];
    if (mag == 0) {
        s->dq[0] = dq >= 0 ? 0x20 : (int16_t)0xFC20;
    } else {
        int exp = quan(mag, power2, 15);

        s->dq[0] = (int16_t)(dq >= 0 ? (exp << 6) + ((mag << 6) >> exp)
                                     : (exp << 6) + ((mag << 6) >> exp) - 0x400);
    }
    s->sr[1] = s->sr[0];
    s->sr[0] = log_float(sr);
    s->pk[1] = s->pk[0];
    s->pk[0] = (int16_t)pk0;

    if (tr)
        s->td = 0;
    else
        s->td = a2p < -11776 ? 1 : 0;

    s->dms += (fi - s->dms) >> 5;
    s->dml += ((fi << 2) - s->dml) >> 7;

    if (tr) {
        s->ap = 256;
    } else if (y < 1536 || s->td == 1) {
        s->ap += (0x200 - s->ap) >> 4;
    } else {
        int diff = (s->dms << 2) - s->dml;

        if ((diff < 0 ? -diff : diff) >= (s->dml >> 3))
            s->ap += (0x200 - s->ap) >> 4;
        else
            s->ap += (-s->ap) >> 4;
    }
}

static int g721_encoder(int sl, OpenIMPG726State *s)
{
    int sezi = predictor_zero(s);
    int sez = (int16_t)sezi >> 1;
    int se = (int16_t)(sezi + predictor_pole(s)) >> 1;
    int d = sl - se;
    int y = step_size(s);
    int i = quantize(d, y, qtab_721, 7);
    int dq = reconstruct(i & 8, dqlntab[i], y);
    int sr = dq < 0 ? se - (dq & 0x3FFF) : se + dq;
    int dqsez = sr + sez - se;

    update(y, witab[i] << 5, fitab[i], dq, sr, dqsez, s);
    return i;
}

static int g721_decoder(int i, OpenIMPG726State *s)
{
    int sezi, sez, se, y, dq, sr, dqsez;

    i &= 0x0F;
    sezi = predictor_zero(s);
    sez = (int16_t)sezi >> 1;
    se = (int16_t)(sezi + predictor_pole(s)) >> 1;
    y = step_size(s);
    dq = reconstruct(i & 0x08, dqlntab[i], y);
    sr = dq < 0 ? se - (dq & 0x3FFF) : se + dq;
    dqsez = sr - se + sez;
    update(y, witab[i] << 5, fitab[i], dq, sr, dqsez, s);
    return (int16_t)(sr << 2);
}

int openimp_g726_encode(OpenIMPG726State *s, uint8_t *out, const int16_t *in,
                        int samples)
{
    int bytes = 0;

    for (int n = 0; n < samples; n++) {
        int code = g721_encoder(in[n] >> 2, s);

        s->bitbuf = (s->bitbuf << 4) | (uint32_t)code;
        s->nbits += 4;
        if (s->nbits >= 8) {
            out[bytes++] = (uint8_t)(s->bitbuf >> (s->nbits - 8));
            s->nbits -= 8;
        }
    }
    return bytes;
}

int openimp_g726_decode(OpenIMPG726State *s, int16_t *out, const uint8_t *in,
                        int bytes)
{
    int samples = 0;
    int used = 0;

    for (;;) {
        if (s->nbits < 4) {
            if (used >= bytes)
                break;
            s->bitbuf = (s->bitbuf << 8) | in[used++];
            s->nbits += 8;
        }
        s->nbits -= 4;
        out[samples++] = (int16_t)g721_decoder(
            (int)(s->bitbuf >> s->nbits) & 0x0F, s);
    }
    return samples;
}

/* ---- IMA ADPCM -------------------------------------------------------- */

static const int8_t index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8
};

static const int16_t stepsize_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
    209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
    2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
    8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767
};

static int clamp_index(int index)
{
    return index < 0 ? 0 : index > 88 ? 88 : index;
}

static int clamp_sample(int v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

int openimp_adpcm_encode(OpenIMPAdpcmState *state, uint8_t *out,
                         const int16_t *in, int samples)
{
    int valpred = state->valprev;
    int index = clamp_index(state->index);
    int step = stepsize_table[index];
    int high = 1;
    int outbuf = 0;
    int bytes = 0;

    for (int n = 0; n < samples; n++) {
        int diff = in[n] - valpred;
        int sign = diff < 0 ? 8 : 0;
        int delta = 0;
        int vpdiff = step >> 3;

        if (sign)
            diff = -diff;
        if (diff >= step) {
            delta = 4;
            diff -= step;
            vpdiff += step;
        }
        step >>= 1;
        if (diff >= step) {
            delta |= 2;
            diff -= step;
            vpdiff += step;
        }
        step >>= 1;
        if (diff >= step) {
            delta |= 1;
            vpdiff += step;
        }
        valpred = clamp_sample(sign ? valpred - vpdiff : valpred + vpdiff);
        delta |= sign;
        index = clamp_index(index + index_table[delta]);
        step = stepsize_table[index];
        if (high) {
            outbuf = (delta << 4) & 0xF0;
        } else {
            out[bytes++] = (uint8_t)((delta & 0x0F) | outbuf);
        }
        high = !high;
    }
    if (!high)
        out[bytes++] = (uint8_t)outbuf;
    state->valprev = (int16_t)valpred;
    state->index = (int8_t)index;
    return bytes;
}

int openimp_adpcm_decode(OpenIMPAdpcmState *state, int16_t *out,
                         const uint8_t *in, int nibbles)
{
    int valpred = state->valprev;
    int index = clamp_index(state->index);
    int step = stepsize_table[index];
    int high = 1;
    int inbuf = 0;

    for (int n = 0; n < nibbles; n++) {
        int delta, sign, vpdiff;

        if (high) {
            inbuf = *in++;
            delta = (inbuf >> 4) & 0x0F;
        } else {
            delta = inbuf & 0x0F;
        }
        high = !high;
        index = clamp_index(index + index_table[delta]);
        sign = delta & 8;
        delta &= 7;
        vpdiff = step >> 3;
        if (delta & 4)
            vpdiff += step;
        if (delta & 2)
            vpdiff += step >> 1;
        if (delta & 1)
            vpdiff += step >> 2;
        valpred = clamp_sample(sign ? valpred - vpdiff : valpred + vpdiff);
        step = stepsize_table[index];
        out[n] = (int16_t)valpred;
    }
    state->valprev = (int16_t)valpred;
    state->index = (int8_t)index;
    return nibbles > 0 ? nibbles : 0;
}
