/* Baseline JPEG decoder for IMP_Decoder_* on T23 (see openimp_t23_jpegdec.h).
 *
 * The vendor library decodes on the Helix VPU; the stack without the OEM
 * worker has no decoder at all, so this is a plain software decoder: marker
 * parser, Huffman decoder, dequantisation, an integer IDCT
 * and the rearrangement into 4:2:0.  No dependencies beyond libc. */

#include <stdlib.h>
#include <string.h>

#include "t23/openimp_t23_jpegdec.h"

#define MAX_COMP 3

static const uint8_t zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63
};

typedef struct {
    int present;
    uint8_t val[256];
    int mincode[17], maxcode[18], valptr[17];
} Huff;

typedef struct {
    int id, h, v, tq;
    int td, ta;             /* huffman tables of the current scan */
    uint32_t bw, bh;        /* plane size in blocks (MCU padded) */
    uint8_t *plane;
    int pred;
} Comp;

typedef struct {
    const uint8_t *p, *end;
    uint32_t acc;
    int nb;
    int marker;             /* a marker was reached: only zero bits follow */
    int zeros;              /* zero bytes fed after the marker */
} Bits;

static int getbits(Bits *b, int n)
{
    while (b->nb < n) {
        int c = 0;

        if (!b->marker && b->p < b->end) {
            c = *b->p++;
            if (c == 0xff) {
                int c2 = b->p < b->end ? *b->p : 0xd9;

                if (c2 == 0) {
                    b->p++;
                } else {
                    b->p--;     /* leave the marker for the parser */
                    b->marker = 1;
                    b->zeros++;
                    c = 0;
                }
            }
        } else {
            b->marker = 1;
            b->zeros++;
        }
        b->acc = (b->acc << 8) | (uint32_t)c;
        b->nb += 8;
    }
    b->nb -= n;
    return (int)((b->acc >> b->nb) & ((1u << n) - 1u));
}

static int decode_huff(Bits *b, const Huff *h)
{
    int code = 0;

    for (int l = 1; l <= 16; l++) {
        code = (code << 1) | getbits(b, 1);
        if (h->maxcode[l] >= 0 && code <= h->maxcode[l] && code >= h->mincode[l])
            return h->val[h->valptr[l] + code - h->mincode[l]];
    }
    return -1;
}

static int extend(int v, int t)
{
    return t && v < (1 << (t - 1)) ? v - (1 << t) + 1 : v;
}

/* Integer inverse DCT (Loeffler / Ligtenberg / Moschytz, 13 bit constants, two passes): no floating point (the XBurst
 * cores of T10/T20/T21/T23 emulate it in the kernel) and no libm; results equal the "slow integer" IDCT of libjpeg. */
#define IDCT_BITS  13
#define IDCT_PASS1 2
#define F_0_298 2446
#define F_0_390 3196
#define F_0_541 4433
#define F_0_765 6270
#define F_0_899 7373
#define F_1_175 9633
#define F_1_501 12299
#define F_1_847 15137
#define F_1_961 16069
#define F_2_053 16819
#define F_2_562 20995
#define F_3_072 25172
#define DESCALE(x, n) (((x) + (1 << ((n) - 1))) >> (n))

/* one 8 point transform: in[k * st], k = 0..7 -> o[0..7] scaled up by IDCT_BITS */
static void idct_1d(const int *in, int st, int32_t o[8])
{
    int32_t z1, z2, z3, z4, z5, t0, t1, t2, t3, t10, t11, t12, t13;

    z2 = in[2 * st]; z3 = in[6 * st];
    z1 = (z2 + z3) * F_0_541;
    t2 = z1 - z3 * F_1_847;
    t3 = z1 + z2 * F_0_765;
    z2 = in[0]; z3 = in[4 * st];
    t0 = (z2 + z3) * (1 << IDCT_BITS);
    t1 = (z2 - z3) * (1 << IDCT_BITS);
    t10 = t0 + t3; t13 = t0 - t3; t11 = t1 + t2; t12 = t1 - t2;
    t0 = in[7 * st]; t1 = in[5 * st]; t2 = in[3 * st]; t3 = in[st];
    z1 = t0 + t3; z2 = t1 + t2; z3 = t0 + t2; z4 = t1 + t3;
    z5 = (z3 + z4) * F_1_175;
    t0 *= F_0_298; t1 *= F_2_053; t2 *= F_3_072; t3 *= F_1_501;
    z1 *= -F_0_899; z2 *= -F_2_562; z3 *= -F_1_961; z4 *= -F_0_390;
    z3 += z5; z4 += z5;
    t0 += z1 + z3; t1 += z2 + z4; t2 += z2 + z3; t3 += z1 + z4;
    o[0] = t10 + t3; o[7] = t10 - t3;
    o[1] = t11 + t2; o[6] = t11 - t2;
    o[2] = t12 + t1; o[5] = t12 - t1;
    o[3] = t13 + t0; o[4] = t13 - t0;
}

/* a damaged file must not overflow the IDCT arithmetic: valid 8 bit data stays far below this */
static int clampc(long long v)
{
    return v < -16384 ? -16384 : v > 16383 ? 16383 : (int)v;
}

static void idct_block(const int *coef, uint8_t *dst, int stride)
{
    int ws[64];
    int32_t o[8];

    for (int x = 0; x < 8; x++) {                   /* columns */
        idct_1d(coef + x, 8, o);
        for (int y = 0; y < 8; y++)
            ws[y * 8 + x] = DESCALE(o[y], IDCT_BITS - IDCT_PASS1);
    }
    for (int y = 0; y < 8; y++) {                   /* rows */
        idct_1d(ws + y * 8, 1, o);
        for (int x = 0; x < 8; x++) {
            int v = DESCALE(o[x], IDCT_BITS + IDCT_PASS1 + 3) + 128;

            dst[y * stride + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
    }
}

static int build_huff(Huff *h, const uint8_t *counts, const uint8_t *vals, int nvals)
{
    int code = 0, k = 0;

    memset(h, 0, sizeof(*h));
    if (nvals > 256)
        return -1;
    memcpy(h->val, vals, (size_t)nvals);
    for (int l = 1; l <= 16; l++) {
        h->valptr[l] = k;
        h->mincode[l] = code;
        code += counts[l - 1];
        k += counts[l - 1];
        h->maxcode[l] = counts[l - 1] ? code - 1 : -1;
        if (code > (1 << l))
            return -1;
        code <<= 1;
    }
    h->present = 1;
    return 0;
}

static int decode_scan(Bits *b, Comp *comp, int ncomp, const int *scan, int ns,
                       uint32_t w, uint32_t h, int hmax, int vmax, int restart,
                       const Huff dc[4], const Huff ac[4], const uint16_t q[4][64])
{
    uint32_t mcux, mcuy, n, total;
    int coef[64];
    int since = 0;

    (void)ncomp;
    if (ns == 1) {      /* non-interleaved: raster order over the component's own blocks */
        Comp *c = &comp[scan[0]];
        uint32_t cw = (w * (uint32_t)c->h + (uint32_t)hmax - 1) / (uint32_t)hmax;
        uint32_t ch = (h * (uint32_t)c->v + (uint32_t)vmax - 1) / (uint32_t)vmax;

        mcux = (cw + 7) / 8;
        mcuy = (ch + 7) / 8;
    } else {
        mcux = (w + 8u * (uint32_t)hmax - 1) / (8u * (uint32_t)hmax);
        mcuy = (h + 8u * (uint32_t)vmax - 1) / (8u * (uint32_t)vmax);
    }
    total = mcux * mcuy;
    for (int i = 0; i < ns; i++)
        comp[scan[i]].pred = 0;
    for (n = 0; n < total; n++) {
        uint32_t mx = n % mcux, my = n / mcux;

        if (restart && since == restart) {
            int m;

            b->nb = 0;                  /* byte align, drop pad bits */
            b->acc = 0;
            if (b->marker || (b->p + 1 < b->end && b->p[0] == 0xff)) {
                m = b->p + 1 < b->end ? b->p[1] : 0;
                if (m >= 0xd0 && m <= 0xd7) {
                    b->p += 2;
                    b->marker = 0;
                } else {
                    return -1;
                }
            } else {
                return -1;
            }
            for (int i = 0; i < ns; i++)
                comp[scan[i]].pred = 0;
            since = 0;
        }
        since++;
        for (int i = 0; i < ns; i++) {
            Comp *c = &comp[scan[i]];
            int bh = ns == 1 ? 1 : c->h, bv = ns == 1 ? 1 : c->v;

            for (int vy = 0; vy < bv; vy++)
                for (int vx = 0; vx < bh; vx++) {
                    uint32_t bx = ns == 1 ? mx : mx * (uint32_t)c->h + (uint32_t)vx;
                    uint32_t by = ns == 1 ? my : my * (uint32_t)c->v + (uint32_t)vy;
                    int t, k, diff;

                    memset(coef, 0, sizeof(coef));
                    t = decode_huff(b, &dc[c->td]);
                    if (t < 0 || t > 15)
                        return -1;
                    diff = t ? extend(getbits(b, t), t) : 0;
                    c->pred += diff;
                    coef[0] = clampc((long long)c->pred * (long long)q[c->tq][0]);
                    for (k = 1; k < 64;) {
                        int rs = decode_huff(b, &ac[c->ta]), r, s;

                        if (rs < 0)
                            return -1;
                        r = rs >> 4;
                        s = rs & 15;
                        if (!s) {
                            if (r != 15)
                                break;
                            k += 16;
                            continue;
                        }
                        k += r;
                        if (k > 63)
                            return -1;
                        coef[zigzag[k]] = clampc((long long)extend(getbits(b, s), s) * (long long)q[c->tq][k]);
                        k++;
                    }
                    if (bx < c->bw && by < c->bh)
                        idct_block(coef, c->plane + (size_t)by * 8 * c->bw * 8 + (size_t)bx * 8,
                                   (int)(c->bw * 8));
                }
        }
        if (b->zeros > 2)
            return -1;                  /* data ended inside the scan */
    }
    return 0;
}

size_t t23_jpeg_size(uint32_t w, uint32_t h)
{
    return (size_t)w * h + 2 * (size_t)((w + 1) / 2) * ((h + 1) / 2);
}

int t23_jpeg_decode(const uint8_t *jpeg, size_t len, int pixfmt,
                    uint32_t max_w, uint32_t max_h,
                    uint8_t **out, uint32_t *ow, uint32_t *oh)
{
    Huff dc[4], ac[4];
    uint16_t q[4][64];
    Comp comp[MAX_COMP];
    int ncomp = 0, hmax = 1, vmax = 1, restart = 0, have_frame = 0, scans = 0;
    uint32_t w = 0, h = 0;
    const uint8_t *p = jpeg, *end = jpeg + len;
    uint8_t *pic = NULL;
    int ret = -1;

    if (!jpeg || len < 4 || !out || !ow || !oh ||
        (pixfmt != T23_JPEGDEC_YUV420P && pixfmt != T23_JPEGDEC_NV12 && pixfmt != T23_JPEGDEC_NV21))
        return -1;
    memset(dc, 0, sizeof(dc));
    memset(ac, 0, sizeof(ac));
    memset(q, 0, sizeof(q));
    memset(comp, 0, sizeof(comp));
    if (p[0] != 0xff || p[1] != 0xd8)
        return -1;
    p += 2;
    while (p + 4 <= end) {
        int m, seg;
        const uint8_t *s;

        if (*p != 0xff) {
            p++;
            continue;
        }
        m = p[1];
        if (m == 0xff) {
            p++;
            continue;
        }
        if (m == 0 || (m >= 0xd0 && m <= 0xd8) || m == 0x01) {
            p += 2;
            continue;
        }
        if (m == 0xd9)
            break;
        seg = (p[2] << 8) | p[3];
        if (seg < 2 || p + 2 + seg > end)
            goto out;
        s = p + 4;
        seg -= 2;
        p += 4 + seg;

        if (m == 0xdb) {
            while (seg > 0) {
                int pq = s[0] >> 4, tq = s[0] & 15, need = 1 + 64 * (pq ? 2 : 1);

                if (tq > 3 || pq > 1 || seg < need)
                    goto out;
                for (int i = 0; i < 64; i++)
                    q[tq][i] = pq ? (uint16_t)((s[1 + 2 * i] << 8) | s[2 + 2 * i]) : s[1 + i];
                s += need;
                seg -= need;
            }
        } else if (m == 0xc4) {
            while (seg >= 17) {
                int tc = s[0] >> 4, th = s[0] & 15, n = 0;

                if (tc > 1 || th > 3)
                    goto out;
                for (int i = 0; i < 16; i++)
                    n += s[1 + i];
                if (seg < 17 + n || build_huff(tc ? &ac[th] : &dc[th], s + 1, s + 17, n) != 0)
                    goto out;
                s += 17 + n;
                seg -= 17 + n;
            }
        } else if (m == 0xdd) {
            if (seg < 2)
                goto out;
            restart = (s[0] << 8) | s[1];
        } else if (m == 0xc0 || m == 0xc1) {
            if (have_frame || seg < 6 || s[0] != 8)
                goto out;
            h = (uint32_t)((s[1] << 8) | s[2]);
            w = (uint32_t)((s[3] << 8) | s[4]);
            ncomp = s[5];
            if (!w || !h || (ncomp != 1 && ncomp != 3) || seg < 6 + 3 * ncomp ||
                (max_w && w > max_w) || (max_h && h > max_h) || (uint64_t)w * h > (64u << 20))
                goto out;
            for (int i = 0; i < ncomp; i++) {
                comp[i].id = s[6 + 3 * i];
                comp[i].h = s[7 + 3 * i] >> 4;
                comp[i].v = s[7 + 3 * i] & 15;
                comp[i].tq = s[8 + 3 * i];
                if (comp[i].h < 1 || comp[i].h > 2 || comp[i].v < 1 || comp[i].v > 2 || comp[i].tq > 3)
                    goto out;
                if (comp[i].h > hmax)
                    hmax = comp[i].h;
                if (comp[i].v > vmax)
                    vmax = comp[i].v;
            }
            for (int i = 0; i < ncomp; i++) {
                uint32_t mcux = (w + 8u * (uint32_t)hmax - 1) / (8u * (uint32_t)hmax);
                uint32_t mcuy = (h + 8u * (uint32_t)vmax - 1) / (8u * (uint32_t)vmax);

                if (hmax % comp[i].h || vmax % comp[i].v)
                    goto out;
                comp[i].bw = mcux * (uint32_t)comp[i].h;
                comp[i].bh = mcuy * (uint32_t)comp[i].v;
                comp[i].plane = calloc((size_t)comp[i].bw * 8, (size_t)comp[i].bh * 8);
                if (!comp[i].plane)
                    goto out;
            }
            have_frame = 1;
        } else if (m == 0xda) {
            int ns, scan[MAX_COMP];
            Bits b;

            if (!have_frame || seg < 1)
                goto out;
            ns = s[0];
            if (ns < 1 || ns > ncomp || seg < 1 + 2 * ns + 3)
                goto out;
            for (int i = 0; i < ns; i++) {
                int j;

                for (j = 0; j < ncomp && comp[j].id != s[1 + 2 * i]; j++)
                    ;
                if (j == ncomp || (s[2 + 2 * i] >> 4) > 3 || (s[2 + 2 * i] & 15) > 3)
                    goto out;
                scan[i] = j;
                comp[j].td = s[2 + 2 * i] >> 4;
                comp[j].ta = s[2 + 2 * i] & 15;
                if (!dc[comp[j].td].present || !ac[comp[j].ta].present)
                    goto out;
            }
            /* sequential only: Ss 0, Se 63, Ah/Al 0 */
            if (s[1 + 2 * ns] != 0 || s[2 + 2 * ns] != 63 || s[3 + 2 * ns] != 0)
                goto out;
            memset(&b, 0, sizeof(b));
            b.p = p;
            b.end = end;
            if (decode_scan(&b, comp, ncomp, scan, ns, w, h, hmax, vmax, restart, dc, ac, q) != 0)
                goto out;
            p = b.p;
            scans++;
        } else if (m >= 0xc2 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc) {
            goto out;       /* progressive, lossless, arithmetic, hierarchical */
        }
    }
    if (!have_frame || !scans)
        goto out;

    pic = malloc(t23_jpeg_size(w, h));
    if (!pic)
        goto out;
    for (uint32_t y = 0; y < h; y++)
        memcpy(pic + (size_t)y * w, comp[0].plane + (size_t)y * comp[0].bw * 8, w);
    {
        uint32_t cw = (w + 1) / 2, ch = (h + 1) / 2;
        uint8_t *cb = pic + (size_t)w * h;

        for (uint32_t cy = 0; cy < ch; cy++)
            for (uint32_t cx = 0; cx < cw; cx++) {
                int val[2] = { 128, 128 };

                for (int k = 0; k < 2 && ncomp == 3; k++) {
                    const Comp *c = &comp[1 + k];
                    int sum = 0;

                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++) {
                            uint32_t lx = 2 * cx + (uint32_t)dx, ly = 2 * cy + (uint32_t)dy;
                            uint32_t px, py;

                            if (lx >= w)
                                lx = w - 1;
                            if (ly >= h)
                                ly = h - 1;
                            px = lx * (uint32_t)c->h / (uint32_t)hmax;
                            py = ly * (uint32_t)c->v / (uint32_t)vmax;
                            sum += c->plane[(size_t)py * c->bw * 8 + px];
                        }
                    val[k] = (sum + 2) / 4;
                }
                if (pixfmt == T23_JPEGDEC_YUV420P) {
                    cb[(size_t)cy * cw + cx] = (uint8_t)val[0];
                    cb[(size_t)cw * ch + (size_t)cy * cw + cx] = (uint8_t)val[1];
                } else {
                    int u = pixfmt == T23_JPEGDEC_NV12 ? val[0] : val[1];
                    int v = pixfmt == T23_JPEGDEC_NV12 ? val[1] : val[0];

                    cb[((size_t)cy * cw + cx) * 2] = (uint8_t)u;
                    cb[((size_t)cy * cw + cx) * 2 + 1] = (uint8_t)v;
                }
            }
    }
    *out = pic;
    *ow = w;
    *oh = h;
    pic = NULL;
    ret = 0;
out:
    free(pic);
    for (int i = 0; i < MAX_COMP; i++)
        free(comp[i].plane);
    return ret;
}
