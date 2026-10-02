/*
 * ivs_move_bench - equivalence check and host benchmark for the IVS move
 * algorithm (src/t31/openimp_t31_ivs_move.c).
 *
 *   ivs_move_bench equiv   run the current implementation and the frozen
 *                          reference copy (ivs_move_ref.c, symbols ref_*)
 *                          side by side over many sizes, skips, ROI
 *                          layouts, live SetParam changes and scenes;
 *                          every result must be bit-identical. Prints a
 *                          digest of all results (equal for both).
 *   ivs_move_bench bench   ns per input frame of feed + run (the whole
 *                          move path: decimation in the capture thread,
 *                          detection in the IVS thread) at 1920x1080 and
 *                          640x360 NV12, reference vs current.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "openimp_t31_ivs_move.h"

T31IvsMove *ref_t31_ivs_move_create(const IMP_IVS_MoveParam *param);
void ref_t31_ivs_move_destroy(T31IvsMove *move);
int ref_t31_ivs_move_set_param(T31IvsMove *move, const IMP_IVS_MoveParam *param);
void ref_t31_ivs_move_get_param(const T31IvsMove *move, IMP_IVS_MoveParam *param);
int ref_t31_ivs_move_needs_luma(const T31IvsMove *move);
void ref_t31_ivs_move_feed(T31IvsMove *move, const uint8_t *luma, uint32_t stride);
int ref_t31_ivs_move_run(T31IvsMove *move, int *retRoi);
T31IvsBaseMove *ref_t31_ivs_base_move_create(const IMP_IVS_BaseMoveParam *param);
void ref_t31_ivs_base_move_destroy(T31IvsBaseMove *base);
int ref_t31_ivs_base_move_set_param(T31IvsBaseMove *base,
                                    const IMP_IVS_BaseMoveParam *param);
int ref_t31_ivs_base_move_needs_luma(const T31IvsBaseMove *base);
void ref_t31_ivs_base_move_feed(T31IvsBaseMove *base, const uint8_t *luma,
                                uint32_t stride);
void ref_t31_ivs_base_move_run(T31IvsBaseMove *base, uint8_t *data, int *ret);

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

static uint64_t digest = 1469598103934665603ull;

static void mix(const void *data, size_t size)
{
    const uint8_t *p = data;

    while (size--) {
        digest ^= *p++;
        digest *= 1099511628211ull;
    }
}

/* Scenes: 0 static texture + sensor noise (+-3), 1 night noise (+-24),
 * 2 static + moving box, 3 global lighting steps, 4 random bytes. */
static void make_frame(uint8_t *f, int w, int h, int t, int scene)
{
    int bx = (t * 37) % w, by = (t * 23) % h, x, y;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int v = ((x * 7) ^ (y * 5)) + ((x / 16 + y / 16) & 1) * 40;

            v &= 0xff;
            switch (scene) {
            case 0: v += (int)(rnd() % 7) - 3; break;
            case 1: v += (int)(rnd() % 49) - 24; break;
            case 2:
                v += (int)(rnd() % 7) - 3;
                if (x >= bx && x < bx + w / 7 && y >= by && y < by + h / 5)
                    v = 255 - v;
                break;
            case 3: v = v / 2 + ((t / 4) % 3) * 50; break;
            default: v = (int)(rnd() & 0xff); break;
            }
            f[(size_t)y * w + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
}

static int grid_rois(IMP_IVS_MoveParam *p, int w, int h, int rows, int cols)
{
    int n = 0, r, c;

    for (r = 0; r < rows; r++)
        for (c = 0; c < cols && n < IMP_IVS_MOVE_MAX_ROI_CNT; c++, n++) {
            p->sense[n] = (r * cols + c) % 5;
            p->roiRect[n].p0.x = c * w / cols;
            p->roiRect[n].p0.y = r * h / rows;
            p->roiRect[n].p1.x = (c + 1) * w / cols - 1;
            p->roiRect[n].p1.y = (r + 1) * h / rows - 1;
        }
    return n;
}

static int random_rois(IMP_IVS_MoveParam *p, int w, int h, int maxsense)
{
    int n = 1 + (int)(rnd() % 8), i;

    for (i = 0; i < n; i++) {
        p->sense[i] = (int)(rnd() % (unsigned)(maxsense + 1));
        p->roiRect[i].p0.x = (int)(rnd() % (unsigned)(w + 8)) - 4;
        p->roiRect[i].p0.y = (int)(rnd() % (unsigned)(h + 8)) - 4;
        p->roiRect[i].p1.x = (int)(rnd() % (unsigned)(w + 8)) - 4;
        p->roiRect[i].p1.y = (int)(rnd() % (unsigned)(h + 8)) - 4;
        if (rnd() % 4 == 0) {       /* full width / to the bottom */
            p->roiRect[i].p1.x = w - 1;
            p->roiRect[i].p1.y = h - 1;
        }
    }
    return n;
}

static long checked, ones;

static int equiv_move(int w, int h, int skip, int layout, int scene, int frames)
{
    IMP_IVS_MoveParam p, q, pa, pb;
    T31IvsMove *a, *b;
    uint8_t *f = malloc((size_t)w * h);
    int t, i;

    memset(&p, 0, sizeof(p));
    p.skipFrameCnt = skip;
    p.frameInfo.width = (uint32_t)w;
    p.frameInfo.height = (uint32_t)h;
    p.roiRectCnt = layout == 0 ? grid_rois(&p, w, h, 4, 4)
                 : layout == 1 ? grid_rois(&p, w, h, 4, 13)
                               : random_rois(&p, w, h, 4);
    a = ref_t31_ivs_move_create(&p);
    b = t31_ivs_move_create(&p);
    if (!a || !b || !f) {
        fprintf(stderr, "move %dx%d: create failed\n", w, h);
        return -1;
    }
    for (t = 0; t < frames; t++) {
        int ra[IMP_IVS_MOVE_MAX_ROI_CNT], rb[IMP_IVS_MOVE_MAX_ROI_CNT];
        int na = ref_t31_ivs_move_needs_luma(a), nb = t31_ivs_move_needs_luma(b);
        int xa, xb;

        make_frame(f, w, h, t, scene);
        memset(ra, 0x5a, sizeof(ra));
        memset(rb, 0x5a, sizeof(rb));
        ref_t31_ivs_move_feed(a, na ? f : NULL, (uint32_t)w);
        t31_ivs_move_feed(b, nb ? f : NULL, (uint32_t)w);
        xa = ref_t31_ivs_move_run(a, ra);
        xb = t31_ivs_move_run(b, rb);
        ref_t31_ivs_move_get_param(a, &pa);
        t31_ivs_move_get_param(b, &pb);
        if (na != nb || xa != xb || memcmp(ra, rb, sizeof(ra)) ||
            memcmp(&pa, &pb, sizeof(pa))) {
            fprintf(stderr, "MISMATCH move %dx%d skip %d layout %d scene %d "
                    "frame %d: needs %d/%d ret %d/%d\n", w, h, skip, layout,
                    scene, t, na, nb, xa, xb);
            return -1;
        }
        mix(&xa, sizeof(xa));
        mix(ra, sizeof(ra));
        checked++;
        if (xa == 0)
            for (i = 0; i < pa.roiRectCnt && i < IMP_IVS_MOVE_MAX_ROI_CNT; i++)
                ones += ra[i] == 1;
        if (t % 11 == 10) {
            /* live change as IMP_IVS_SetParam: sense 0..8, sometimes ROIs */
            q = pa;
            for (i = 0; i < IMP_IVS_MOVE_MAX_ROI_CNT; i++)
                q.sense[i] = (int)(rnd() % 9);
            if (rnd() % 2)
                q.roiRectCnt = random_rois(&q, w, h, 8);
            if (rnd() % 8 == 0)
                q.roiRectCnt = 0;
            xa = ref_t31_ivs_move_set_param(a, &q);
            xb = t31_ivs_move_set_param(b, &q);
            if (xa != xb) {
                fprintf(stderr, "MISMATCH set_param %d/%d\n", xa, xb);
                return -1;
            }
        }
    }
    ref_t31_ivs_move_destroy(a);
    t31_ivs_move_destroy(b);
    free(f);
    return 0;
}

static int equiv_base(int w, int h, int skip, int refnum, int sense, int scene)
{
    IMP_IVS_BaseMoveParam p;
    T31IvsBaseMove *a, *b;
    int len = (w / 8) * (h / 8), t;
    uint8_t *f = malloc((size_t)w * h);
    uint8_t *da = calloc((size_t)len + 1, 1), *db = calloc((size_t)len + 1, 1);

    memset(&p, 0, sizeof(p));
    p.skipFrameCnt = skip;
    p.referenceNum = refnum;
    p.sense = sense;
    p.frameInfo.width = (uint32_t)w;
    p.frameInfo.height = (uint32_t)h;
    a = ref_t31_ivs_base_move_create(&p);
    b = t31_ivs_base_move_create(&p);
    if (!a || !b || !f || !da || !db)
        return -1;
    for (t = 0; t < 24; t++) {
        int na = ref_t31_ivs_base_move_needs_luma(a);
        int nb = t31_ivs_base_move_needs_luma(b);
        int xa = 0, xb = 0;

        make_frame(f, w, h, t, scene);
        ref_t31_ivs_base_move_feed(a, na ? f : NULL, (uint32_t)w);
        t31_ivs_base_move_feed(b, nb ? f : NULL, (uint32_t)w);
        ref_t31_ivs_base_move_run(a, da, &xa);
        t31_ivs_base_move_run(b, db, &xb);
        if (na != nb || xa != xb || memcmp(da, db, (size_t)len)) {
            fprintf(stderr, "MISMATCH base %dx%d frame %d\n", w, h, t);
            return -1;
        }
        mix(&xa, sizeof(xa));
        if (xa)
            mix(da, (size_t)len);
        if (t == 12) {
            p.sense = (sense + 1) % 4;
            ref_t31_ivs_base_move_set_param(a, &p);
            t31_ivs_base_move_set_param(b, &p);
        }
    }
    ref_t31_ivs_base_move_destroy(a);
    t31_ivs_base_move_destroy(b);
    free(f);
    free(da);
    free(db);
    return 0;
}

static int equiv(void)
{
    static const int size[][2] = {
        { 640, 360 }, { 320, 240 }, { 64, 48 }, { 66, 50 }, { 98, 34 },
        { 34, 18 }, { 130, 7 }, { 5, 130 }, { 2, 2 }, { 3, 3 },
    };
    static const int skips[] = { 0, 1, 2, 3, 5, 9 };
    int s, k, l, e;

    for (s = 0; s < (int)(sizeof(size) / sizeof(size[0])); s++)
        for (k = 0; k < 6; k++)
            for (l = 0; l < 4; l++)
                for (e = 0; e < 5; e++)
                    if (equiv_move(size[s][0], size[s][1], skips[k], l > 2 ? 2 : l,
                                   e, 48) != 0)
                        return 1;
    for (k = 0; k < 3; k++)
        for (e = 0; e < 3; e++)
            if (equiv_move(1920, 1080, skips[k * 2], e == 2 ? 2 : 0, e, 16) != 0)
                return 1;
    for (s = 0; s < 4; s++)
        for (k = 0; k < 3; k++)
            for (e = 0; e < 5; e++)
                if (equiv_base(size[s][0], size[s][1], skips[k], 1 + k * 2,
                               e % 4, e) != 0)
                    return 1;
    if (!ones) {
        fprintf(stderr, "degenerate run: no detections\n");
        return 1;
    }
    printf("ivs move equivalence: %ld results identical to the reference "
           "(%ld ROI hits), digest %016llx\n", checked, ones,
           (unsigned long long)digest);
    return 0;
}

/* ------------------------------ bench ------------------------------ */

#define BENCH_FRAMES 16

static uint64_t now_ns(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

typedef struct {
    T31IvsMove *(*create)(const IMP_IVS_MoveParam *);
    void (*destroy)(T31IvsMove *);
    int (*needs)(const T31IvsMove *);
    void (*feed)(T31IvsMove *, const uint8_t *, uint32_t);
    int (*run)(T31IvsMove *, int *);
} MoveImpl;

static const MoveImpl impl_ref = {
    ref_t31_ivs_move_create, ref_t31_ivs_move_destroy,
    ref_t31_ivs_move_needs_luma, ref_t31_ivs_move_feed, ref_t31_ivs_move_run,
};
static const MoveImpl impl_new = {
    t31_ivs_move_create, t31_ivs_move_destroy,
    t31_ivs_move_needs_luma, t31_ivs_move_feed, t31_ivs_move_run,
};

/* Best of three runs of n frames; returns ns per input frame and the
 * feed (capture thread) share. */
static double bench_one(const MoveImpl *im, uint8_t **frames, int w, int h,
                        int skip, int n, double *feed_ns)
{
    IMP_IVS_MoveParam p;
    double best = 1e30, bestfeed = 0;
    int rep, t;

    memset(&p, 0, sizeof(p));
    p.skipFrameCnt = skip;
    p.frameInfo.width = (uint32_t)w;
    p.frameInfo.height = (uint32_t)h;
    p.roiRectCnt = grid_rois(&p, w, h, 4, 4);
    for (rep = 0; rep < 3; rep++) {
        T31IvsMove *m = im->create(&p);
        uint64_t tf = 0, t0, t1, tt;
        int ret[IMP_IVS_MOVE_MAX_ROI_CNT];

        tt = now_ns();
        for (t = 0; t < n; t++) {
            const uint8_t *f = frames[t % BENCH_FRAMES];

            t0 = now_ns();
            im->feed(m, im->needs(m) ? f : NULL, (uint32_t)w);
            t1 = now_ns();
            tf += t1 - t0;
            (void)im->run(m, ret);
        }
        tt = now_ns() - tt;
        im->destroy(m);
        if ((double)tt / n < best) {
            best = (double)tt / n;
            bestfeed = (double)tf / n;
        }
    }
    *feed_ns = bestfeed;
    return best;
}

static int bench(void)
{
    static const int size[][2] = { { 1920, 1080 }, { 640, 360 } };
    static const char *scene_name[] = { "static", "night", "motion" };
    static const int skips[] = { 5, 0 };
    int s, e, k, i;

    for (s = 0; s < 2; s++) {
        int w = size[s][0], h = size[s][1];
        int n = w > 1000 ? 240 : 1200;
        uint8_t *frames[BENCH_FRAMES];

        for (e = 0; e < 3; e++) {
            for (i = 0; i < BENCH_FRAMES; i++) {
                /* NV12, luma first, as the capture buffer */
                frames[i] = malloc((size_t)w * h * 3 / 2);
                make_frame(frames[i], w, h, i, e == 2 ? 2 : e);
            }
            for (k = 0; k < 2; k++) {
                double fr, fn, r = bench_one(&impl_ref, frames, w, h, skips[k], n, &fr);
                double c = bench_one(&impl_new, frames, w, h, skips[k], n, &fn);

                printf("%4dx%-4d %-6s skip %d: ref %8.0f ns/frame (feed %7.0f)"
                       "  new %8.0f ns/frame (feed %7.0f)  x%.2f\n", w, h,
                       scene_name[e], skips[k], r, fr, c, fn, r / c);
            }
            for (i = 0; i < BENCH_FRAMES; i++)
                free(frames[i]);
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "bench"))
        return bench();
    return equiv();
}
