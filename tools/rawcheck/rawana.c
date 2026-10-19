/* rawana - host check of the NV12 memory layout of raw FrameSource frames (stream from rawcheck).
 *
 *   rawana [-s] frames.raw
 *
 * -s : the frames hold the synthetic pattern (OPENIMP_TEST_PATTERN=1): the chroma start is found
 *      by an exact comparison with openimp_testpattern.h.  Default: sensor test pattern (vertically
 *      constant, left to right black | colour gradient | white, curved edges from the lens shading).
 * -n : natural scene (or a sensor pattern without colour, e.g. T31 sc4336p: chroma all 128): the chroma
 *      start is the line S where the gradient magnitude of the chroma correlates best with that of the
 *      luma; needs a scene with coloured edges.
 *
 * Per frame:
 *   pitch   - found from the luma plane (the pattern is vertically smooth only with the right pitch)
 *   uv      - chroma start in buffer lines (units of the pitch).  Each candidate start S is judged by
 *             how well the horizontal edge position of the chroma saturation, row by row, follows the
 *             edge position of the luma (sensor pattern) or by the exact pattern (-s).  The vendor
 *             rule (docs/T31_ROTATE.md, docs/T40_STATUS.md) is S = ALIGN16(h) lines, i.e. byte
 *             offset pitch*ALIGN16(h); a UV plane shifted by n chroma lines shows as S = ALIGN16(h)+n.
 *   pad     - the pitch-w padding bytes per luma row and the ALIGN16(h)-h padding luma rows
 * Exit status 1 if a frame deviates from the expected layout (pitch ALIGN16(w) / UV at ALIGN16(h),
 * snap frames: packed, pitch w, UV at h).                                                      */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/openimp_testpattern.h"

static int al16(int v) { return (v + 15) & ~15; }

/* luma edge column per luma row: first x where the 4-px mean passes half of the row's min..max */
static double luma_edge(const unsigned char *y, int p, int w, int row)
{
    int mn = 255, mx = 0, x;
    const unsigned char *r = y + (size_t)row * p;
    for (x = 0; x + 4 <= w; x += 4) {
        int m = (r[x] + r[x+1] + r[x+2] + r[x+3]) / 4;
        if (m < mn) mn = m;
        if (m > mx) mx = m;
    }
    if (mx - mn < 24) return -1;
    for (x = 0; x + 4 <= w; x++) {
        int m = (r[x] + r[x+1] + r[x+2] + r[x+3]) / 4;
        if (m >= mn + (mx - mn) / 2) return x + 2;
    }
    return -1;
}

static double chroma_edge(const unsigned char *uv, int p, int w, int row, double thr)
{
    const unsigned char *r = uv + (size_t)row * p;
    int c;
    for (c = 0; c < w / 2; c++) {
        double s = abs(r[2*c] - 128) + abs(r[2*c+1] - 128);
        if (s >= thr) return 2.0 * c;
    }
    return -1;
}

static double chroma_peak(const unsigned char *uv, int p, int w, int h)
{
    double pk = 0;
    for (int r = h / 8; r < h / 2 - h / 8; r += 2)
        for (int c = 0; c < w / 2; c += 2) {
            double s = abs(uv[(size_t)r * p + 2*c] - 128) + abs(uv[(size_t)r * p + 2*c + 1] - 128);
            if (s > pk) pk = s;
        }
    return pk;
}

/* residual of the edge shapes for chroma start line S; -1 = not measurable */
static double edge_resid(const unsigned char *buf, int p, int w, int h, int S, const double *ey, double thr)
{
    const unsigned char *uv = buf + (size_t)S * p;
    double a[4096], b[4096], ma = 0, mb = 0, ss = 0; int n = 0;
    for (int r = h / 8; r < h / 2 - h / 8 && n < 4096; r++) {
        double c = chroma_edge(uv, p, w, r, thr), l0 = ey[2*r], l1 = ey[2*r+1];
        if (c < 0 || l0 < 0 || l1 < 0) continue;
        a[n] = c; b[n] = (l0 + l1) / 2; ma += a[n]; mb += b[n]; n++;
    }
    if (n < h / 8) return -1;
    ma /= n; mb /= n;
    for (int i = 0; i < n; i++) { double d = (a[i] - ma) - (b[i] - mb); ss += d * d; }
    return ss / n;
}

/* exact pattern comparison for chroma start S (-s) */
static double pat_resid(const unsigned char *buf, int p, int w, int h, int S, const unsigned char *ref)
{
    double se = 0; long n = 0;
    for (int r = 0; r < h / 2; r += 3)
        for (int c = 0; c < w / 2; c += 3) {
            const unsigned char *d = buf + (size_t)(S + r) * p + 2 * c, *e = ref + (size_t)r * w + 2 * c;
            se += abs(d[0] - e[0]) + abs(d[1] - e[1]); n++;
        }
    return n ? se / n : -1;
}

/* natural scene: Pearson correlation of the gradient magnitude of the (2x2 mean) luma with that of
 * the chroma planes for the chroma start line S; -2 = not measurable */
static double grad_corr(const unsigned char *buf, int p, int w, int h, int S)
{
    const unsigned char *uv = buf + (size_t)S * p;
    int cw = w / 2, ch = h / 2;
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0; long n = 0;
    for (int r = 2; r < ch - 2; r += 2)
        for (int c = 2; c < cw - 2; c += 2) {
#define YD(rr, cc) ((buf[(size_t)(2*(rr)) * p + 2*(cc)] + buf[(size_t)(2*(rr)) * p + 2*(cc) + 1] + \
                     buf[(size_t)(2*(rr)+1) * p + 2*(cc)] + buf[(size_t)(2*(rr)+1) * p + 2*(cc) + 1]) / 4.0)
#define CV(rr, cc, k) ((double)uv[(size_t)(rr) * p + 2*(cc) + (k)])
            double gy = fabs(YD(r, c + 1) - YD(r, c - 1)) + fabs(YD(r + 1, c) - YD(r - 1, c));
            double gc = 0;
            for (int k = 0; k < 2; k++) gc += fabs(CV(r, c + 1, k) - CV(r, c - 1, k)) + fabs(CV(r + 1, c, k) - CV(r - 1, c, k));
            sx += gy; sy += gc; sxx += gy * gy; syy += gc * gc; sxy += gy * gc; n++;
        }
    if (n < 100) return -2;
    double vx = sxx / n - (sx / n) * (sx / n), vy = syy / n - (sy / n) * (sy / n);
    if (vx < 1e-6 || vy < 1e-6) return -2;
    return (sxy / n - (sx / n) * (sy / n)) / sqrt(vx * vy);
}

int main(int argc, char **argv)
{
    int synth = 0, nat = 0, bad = 0, a = 1;
    FILE *f;
    char line[256];

    if (argc > 1 && !strcmp(argv[1], "-s")) { synth = 1; a = 2; }
    if (argc > 1 && !strcmp(argv[1], "-n")) { nat = 1; a = 2; }
    if (a >= argc || !(f = fopen(argv[a], "rb"))) { fprintf(stderr, "usage: rawana [-s] frames.raw\n"); return 2; }
    while (fgets(line, sizeof line, f)) {
        int ch, k, w, h; unsigned pixfmt; size_t size; char kind[8];
        if (sscanf(line, "RAW ch=%d kind=%7s k=%d w=%d h=%d pixfmt=%u size=%zu", &ch, kind, &k, &w, &h, &pixfmt, &size) != 7)
            continue;
        unsigned char *buf = calloc(1, size + 65536);
        if (fread(buf, 1, size, f) != size) { fprintf(stderr, "short read\n"); return 1; }
        int snap = !strcmp(kind, "snap");
        int pmax = snap ? w : (int)(size / ((size_t)h * 3 / 2)); if (pmax > w + 1024) pmax = w + 1024;
        if (pmax < w) pmax = w;
        /* pitch: vertical smoothness of the luma plane */
        double best = 1e30, second = 1e30; int pit = w;
        for (int p = w; p <= pmax; p++) {
            if (nat && p != w && p != al16(w)) continue;   /* natural scene: not smooth enough to search */
            double s = 0; long n = 0;
            for (int y = h / 4; y < h / 4 + 48 && y + 1 < h; y++)
                for (int x = 0; x < w; x += 3) { { int d_ = buf[(size_t)y * p + x] - buf[(size_t)(y + 1) * p + x]; s += d_ * d_; n++; } }
            s /= n ? n : 1;
            if (s < best) { second = best; best = s; pit = p; } else if (s < second) second = s;
        }
        int exp_pit = snap ? w : al16(w), exp_s = snap ? h : al16(h);
        /* chroma start */
        unsigned char *ref = NULL;
        double ey[8192]; double thr = 0, ebest = 1e30, emed[512]; int S = -1, ns = 0, flat = 0;
        if (synth) {
            unsigned char *nv = calloc(1, (size_t)w * h * 3 / 2); ref = nv;
            openimp_testpattern_nv12(nv, nv + (size_t)w * h, w, h, w);
            ref = nv + (size_t)w * h;
        } else {
            double mn = 1e9, mx = -1e9;
            for (int y = 0; y < h && y < 8192; y++) {
                ey[y] = luma_edge(buf, pit, w, y);
                if (y >= h / 4 && y < 3 * h / 4 && ey[y] >= 0) { if (ey[y] < mn) mn = ey[y]; if (ey[y] > mx) mx = ey[y]; }
            }
            flat = (mx - mn) < 1.5;
        }
        int smax = (int)(size / pit) - h / 2; if (smax > h + 96) smax = h + 96;
        for (int s = h - 8; s <= smax; s++) {
            double r;
            if (synth) r = pat_resid(buf, pit, w, h, s, ref);
            else { double pk = chroma_peak(buf + (size_t)s * pit, pit, w, h); thr = pk / 2; r = pk < 10 ? -1 : edge_resid(buf, pit, w, h, s, ey, thr); }
            if (r < 0) continue;
            if (ns < 512) emed[ns++] = r;
            if (r < ebest) { ebest = r; S = s; }
        }
        double corr = -9; int natmode = 0;
        if (!synth && (nat || S < 0)) {
            double bc = -9, c2 = -9; int bs = -1;
            for (int s2 = h - 16; s2 <= smax && s2 >= 0; s2++) {
                double c = grad_corr(buf, pit, w, h, s2);
                if (c > bc) { c2 = bc; bc = c; bs = s2; } else if (c > c2) c2 = c;
            }
            if (bs >= 0 && bc > 0.05 && bc > c2 + 0.02) { S = bs; corr = bc; natmode = 1; flat = 0; ebest = c2; }
        }
        double med = 0;
        if (ns) { for (int i = 0; i < ns; i++) for (int j = i + 1; j < ns; j++) if (emed[j] < emed[i]) { double t = emed[i]; emed[i] = emed[j]; emed[j] = t; } med = emed[ns / 2]; }
        /* padding */
        long pz = 0, pn = 0; for (int y = 0; y < h; y += 7) for (int x = w; x < pit; x++) { pn++; if (!buf[(size_t)y * pit + x]) pz++; }
        double prow = 0; long prn = 0; for (int y = h; y < al16(h) && !snap; y++) for (int x = 0; x < w; x += 5) { prow += buf[(size_t)y * pit + x]; prn++; }
        int sb = -1, usedb = 0;
        {   /* vertically constant pattern: rows of one plane are alike, so rows where the content
             * changes are plane boundaries (Y end, padding, UV end); UV start = end boundary - h/2 */
            int nb = 0, rows = (int)(size / pit), last = -1, prev = -2;
            printf("   row changes (diff>6):");
            for (int r = 1; r < rows; r++) {
                long d = 0, m = 0;
                for (int x = 0; x < w; x += 4) { d += abs(buf[(size_t)r * pit + x] - buf[(size_t)(r - 1) * pit + x]); m++; }
                if (d / (m ? m : 1) > 6) { if (nb < 8) printf(" %d", r); nb++; if (prev != r - 1) last = r; prev = r; }
            }
            printf("%s", nb > 8 ? " ..." : "");
            if (last > h + h / 4 && last - h / 2 >= h - 8) sb = last - h / 2;
            printf(" | end boundary %d -> UV start %d\n", last, sb);
        }
        if (!synth && !natmode && (flat || S < 0) && sb >= 0) { S = sb; usedb = 1; }
        int ok = pit == exp_pit && S == exp_s && !(!synth && flat && !natmode && !usedb);
printf("ch%d %-4s %d %dx%d size %zu: pitch %d (expect %d, smooth %.3g vs next %.3g) | uv start line %d (expect %d, shift %+d chroma = %+d luma lines)%s "
               "| %s %.3f vs %.3f | pad cols %ld/%ld zero, pad rows meanY %.0f -> %s\n",
               ch, kind, k, w, h, size, pit, exp_pit, best, second, S, exp_s, S - exp_s, 2 * (S - exp_s),
               usedb ? " [from the UV end boundary]" : (!synth && flat) ? " [luma edges flat: shift not measurable]" : (S < 0 ? " [not measurable]" : ""),
               natmode ? "grad-corr best vs 2nd" : "edge fit vs median", natmode ? corr : ebest, natmode ? ebest : med, pz, pn, prn ? prow / prn : 0.0, ok ? "OK" : ((!synth && flat) || S < 0) ? "UNDETERMINED" : "DEVIATION");
        if (!ok && S >= 0 && !(!synth && flat)) bad = 1;
        free(buf);
    }
    return bad;
}
