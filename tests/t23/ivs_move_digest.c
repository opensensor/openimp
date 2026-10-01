/*
 * ivs_move_digest - run OpenIMP's IVS move / base-move algorithms over a
 * fixed set of synthetic frame sequences and print a digest of every result.
 *
 * Built twice, with the T31 1.1.6 parameter layout and with PLATFORM_T23
 * (T23 1.3.0 layout, IMPFrameInfo 0x38 bytes, roiRect at 0x110): the T23
 * libimp runs the same move / base-move code as the T31 scalar path, so
 * both builds must print the same digest. A mismatch means the T23 ABI
 * header feeds the algorithm different parameters than the T31 one.
 *
 * The parameters are filled through the vendor field names only, so the
 * test also catches a T23 layout that compiles but places roiRect, sense
 * or frameInfo where the algorithm does not expect them.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openimp_t31_ivs_move.h"

static uint32_t rng;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }
static int sat8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static uint64_t digest = 1469598103934665603ull;
static long move_results, move_ones, base_results, base_nonzero;

static void mix(const void *data, size_t size)
{
    const uint8_t *p = data;

    while (size--) {
        digest ^= *p++;
        digest *= 1099511628211ull;
    }
}

/* Gradient with a moving inverted box; noise / random / lighting modes. */
static void make_frame(uint8_t *f, int w, int h, int t, int mode)
{
    int x, y;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int v = (x * 3 + y * 2) & 0xff;
            int bx = (t * 13) % w, by = (t * 7) % h;

            if (mode == 1)
                v = (v + (int)(rnd() % 7)) & 0xff;
            else if (mode == 2)
                v = rnd() & 0xff;
            else if (mode == 3)
                v = sat8(v / 2 + ((t / 5) % 3) * 60);
            if (mode != 2 && x >= bx && x < bx + w / 6 && y >= by && y < by + h / 5)
                v = 255 - v;
            f[y * w + x] = (uint8_t)v;
        }
}

static int run_move(int w, int h, int skip, int layout, int mode)
{
    IMP_IVS_MoveParam p;
    T31IvsMove *m;
    uint8_t *f = malloc((size_t)w * h);
    int rows = 3, cols = 4, n = 0, r, c, t, i;

    memset(&p, 0, sizeof(p));
    p.skipFrameCnt = skip;
    p.frameInfo.width = (uint32_t)w;
    p.frameInfo.height = (uint32_t)h;
    for (r = 0; r < rows; r++)
        for (c = 0; c < cols; c++) {
            if (layout == 1 && r == 0)
                continue;
            if (layout == 2 && (r == 0 || r == rows - 1))
                continue;
            p.sense[n] = (r * cols + c) % 5;
            p.roiRect[n].p0.x = c * w / cols;
            p.roiRect[n].p0.y = r * h / rows;
            p.roiRect[n].p1.x = (c + 1) * w / cols - 1;
            p.roiRect[n].p1.y = (r + 1) * h / rows - 1;
            n++;
        }
    p.roiRectCnt = n;
    m = t31_ivs_move_create(&p);
    if (!m || !f) {
        free(f);
        t31_ivs_move_destroy(m);
        return -1;
    }
    for (t = 0; t < 40; t++) {
        IMP_IVS_MoveOutput o;
        int ret;

        make_frame(f, w, h, t, mode);
        memset(&o, 0, sizeof(o));
        t31_ivs_move_feed(m, t31_ivs_move_needs_luma(m) ? f : NULL,
                          (uint32_t)w);
        ret = t31_ivs_move_run(m, o.retRoi);
        mix(&ret, sizeof(ret));
        if (ret == 0) {
            move_results++;
            for (i = 0; i < n; i++)
                move_ones += o.retRoi[i] != 0;
            mix(o.retRoi, sizeof(int) * (size_t)n);
        }
        if (t == 20) {
            /* live sensitivity change, as IMP_IVS_SetParam does it */
            IMP_IVS_MoveParam q;

            t31_ivs_move_get_param(m, &q);
            for (i = 0; i < q.roiRectCnt; i++)
                q.sense[i] = 4 - q.sense[i] % 5;
            if (t31_ivs_move_set_param(m, &q) != 0)
                return -1;
        }
    }
    t31_ivs_move_destroy(m);
    free(f);
    return 0;
}

static int run_base(int w, int h, int skip, int refnum, int sense, int mode)
{
    IMP_IVS_BaseMoveParam p;
    T31IvsBaseMove *b;
    int len = (w / 8) * (h / 8), t, i;
    uint8_t *f = malloc((size_t)w * h);
    uint8_t *d = calloc((size_t)len + 1, 1);

    memset(&p, 0, sizeof(p));
    p.skipFrameCnt = skip;
    p.referenceNum = refnum;
    p.sense = sense;
    p.frameInfo.width = (uint32_t)w;
    p.frameInfo.height = (uint32_t)h;
    b = t31_ivs_base_move_create(&p);
    if (!b || !f || !d || t31_ivs_base_move_datalen(b) != len) {
        t31_ivs_base_move_destroy(b);
        free(f);
        free(d);
        return -1;
    }
    for (t = 0; t < 30; t++) {
        int ret = 0;

        make_frame(f, w, h, t, mode);
        t31_ivs_base_move_feed(b, t31_ivs_base_move_needs_luma(b) ? f : NULL,
                               (uint32_t)w);
        t31_ivs_base_move_run(b, d, &ret);
        mix(&ret, sizeof(ret));
        if (ret) {
            base_results++;
            for (i = 0; i < len; i++)
                base_nonzero += d[i] != 0;
            mix(d, (size_t)len);
        }
    }
    t31_ivs_base_move_destroy(b);
    free(f);
    free(d);
    return 0;
}

int main(void)
{
    static const int msize[][2] = { { 640, 360 }, { 320, 240 }, { 64, 48 } };
    static const int bsize[][2] = { { 320, 240 }, { 64, 48 } };
    static const int skips[] = { 0, 1, 2, 3, 5, 9 };
    int s, k, l, m, r, e;

    rng = 12345;
    for (s = 0; s < 3; s++)
        for (k = 0; k < 6; k++)
            for (l = 0; l < 3; l++)
                for (m = 0; m < 4; m++)
                    if (run_move(msize[s][0], msize[s][1], skips[k], l, m) != 0) {
                        fprintf(stderr, "move %dx%d: setup failed\n",
                                msize[s][0], msize[s][1]);
                        return 1;
                    }
    for (s = 0; s < 2; s++)
        for (k = 0; k < 4; k++)
            for (r = 1; r <= 7; r += 3)
                for (e = 0; e < 4; e++)
                    for (m = 0; m < 4; m++)
                        if (run_base(bsize[s][0], bsize[s][1], skips[k], r, e, m) != 0) {
                            fprintf(stderr, "base %dx%d: setup failed\n",
                                    bsize[s][0], bsize[s][1]);
                            return 1;
                        }
    /* Both kinds must actually detect something, or the digest proves
     * nothing. */
    if (!move_ones || !base_nonzero || move_ones == move_results * 12) {
        fprintf(stderr, "degenerate run: move results %ld (ones %ld), "
                "base results %ld (nonzero %ld)\n", move_results, move_ones,
                base_results, base_nonzero);
        return 1;
    }
    printf("move results %ld ones %ld base results %ld nonzero %ld "
           "digest %016llx\n", move_results, move_ones, base_results,
           base_nonzero, (unsigned long long)digest);
    return 0;
}
