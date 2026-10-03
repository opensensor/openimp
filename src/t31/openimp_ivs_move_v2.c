/* Motion v2 (beyond vendor, opt-in), see openimp_ivs_move_v2.h and
 * include/imp/openimp_ivs_move_ex.h.
 *
 * Per analysed frame:
 *   1. feed (capture thread): the luma is reduced to a grid of cells
 *      (8x8 pixels at 640x360, coarser for big frames so the grid stays at
 *      most 80x60); each cell value is the sum of 4x4 samples (0..4080).
 *   2. run (IVS thread):
 *      - global brightness ratio g of the frame against the background:
 *        median of the per-cell ratios (histogram, cells that moved last
 *        frame left out), refined by the mean ratio around the median.
 *        It compensates AE / IR brightness changes and detects jumps;
 *      - suppression: ISP running mode change, total gain jump or |g - 1|
 *        above jump_pct start a hold-off of suppress_ms, during which
 *        nothing is reported and the background re-learns fast;
 *      - per cell: d = value - g * background; moving when
 *        |d| > max(min_delta, k * noise), noise = running mean of |d|;
 *        the background follows static cells at 1/2^learn_shift and
 *        moving cells 16 times slower (without the BACKGROUND feature the
 *        background is just the previous analysed frame);
 *      - objects: 8-connected moving cells, at least min_cells, tracked by
 *        box overlap, reported after min_frames consecutive frames
 *        (without BLOBS: one box around all moving cells, no persistence).
 * Integer arithmetic only. */
#include <stdlib.h>
#include <string.h>

#include "openimp_ivs_move_v2.h"

#define V2_MAX_GRID_W 80
#define V2_MAX_GRID_H 60
#define V2_WARMUP     4         /* analysed frames before anything is reported */
#define V2_MAX_TRACKS OPENIMP_IVS_MOVE_EX_MAX_OBJ
#define V2_MAX_COMPS  64
#define V2_HIST_BINS  128       /* ratio bins of 1/32, 0..4 */
#define V2_ACT_LIMIT  32        /* moving share above 1/8 (EMA over ~64 frames) */
#define V2_TEX_FLOOR  (14 << 2) /* minimum texture change (2 per difference) */

struct v2_comp {
    int x0, y0, x1, y1;         /* grid cells, inclusive */
    int cells, exc, label;
    int used;
};

struct v2_track {
    int x0, y0, x1, y1;
    int age, miss, label, reported;
    unsigned int cells, strength;
    uint16_t id;
};

struct IvsMoveV2 {
    OpenIMP_IVS_MoveConfigEx cfg;
    uint32_t w, h, cs, step, off, gw, gh, ncell;
    uint32_t interval, phase;
    uint16_t *cur;              /* fed cell sums */
    uint16_t *tex;              /* fed cell texture: sum |s[i] - s[i+1]| */
    uint16_t *tbg;              /* background texture << 2 */
    uint16_t *tdv;              /* running mean texture deviation << 2 */
    int fed;
    int64_t ts, last_ts;
    uint16_t *mu;               /* background, cell sum << 4 (< 2^16) */
    uint16_t *dev;              /* running mean |d|, cell sum << 4 */
    uint8_t *mov;               /* moving cells of the last run */
    uint8_t *exc;               /* |d| / threshold, Q4, capped at 64 */
    uint8_t *act;               /* long-term moving share, Q8 (flicker) */
    uint16_t *lab;              /* component label per cell, 0 = none */
    uint16_t *parent;           /* union-find over labels */
    uint8_t *rep;               /* label belongs to a reported object */
    uint32_t warm;
    int64_t supp_until;
    uint32_t supp_frames;       /* analysed frames still suppressed (minimum) */
    uint32_t supp_reason;
    int last_mode;
    uint32_t last_gain;
    struct v2_comp comp[V2_MAX_COMPS];
    int ncomp;
    struct v2_track tr[V2_MAX_TRACKS];
    int ntr;
    int nmov;                   /* moving cells of the last run */
    int lab_dirty;              /* lab[] holds labels */
    int min_cells;              /* effective: config, vendor-sense area */
    uint16_t next_id;
    uint32_t seq;
};

/* ---------------- configuration ---------------- */

static const OpenIMP_IVS_MoveConfigEx v2_defaults = {
    sizeof(OpenIMP_IVS_MoveConfigEx), OPENIMP_IVS_MOVE_EX_VERSION,
    0,          /* features */
    4,          /* learn_shift */
    64,         /* thresh_k (4.0) */
    10,         /* min_delta */
    4000,       /* suppress_ms */
    25,         /* jump_pct */
    3,          /* min_cells */
    2,          /* min_frames */
    { 0, 0, 0, 0, 0, 0 }
};

static int32_t clampi(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void ivs_move_v2_config_sanitize(OpenIMP_IVS_MoveConfigEx *dst,
                                 const OpenIMP_IVS_MoveConfigEx *src)
{
    size_t n = sizeof(*dst);

    *dst = v2_defaults;
    if (src) {
        if (src->size < n)
            n = src->size;
        if (n > 8)
            memcpy((uint8_t *)dst + 8, (const uint8_t *)src + 8, n - 8);
    }
    dst->size = sizeof(*dst);
    dst->version = OPENIMP_IVS_MOVE_EX_VERSION;
    dst->features &= OPENIMP_MOVE_F_ALL;
    /* 0 = default, so a caller may set only the features */
#define V2_DEF0(f) do { if (!dst->f) dst->f = v2_defaults.f; } while (0)
    V2_DEF0(learn_shift);
    V2_DEF0(thresh_k);
    V2_DEF0(min_delta);
    V2_DEF0(suppress_ms);
    V2_DEF0(jump_pct);
    V2_DEF0(min_cells);
    V2_DEF0(min_frames);
#undef V2_DEF0
    dst->learn_shift = clampi(dst->learn_shift, 1, 10);
    dst->thresh_k = clampi(dst->thresh_k, 16, 255);
    dst->min_delta = clampi(dst->min_delta, 1, 64);
    dst->suppress_ms = clampi(dst->suppress_ms, 1, 60000);
    dst->jump_pct = clampi(dst->jump_pct, 5, 90);
    dst->min_cells = clampi(dst->min_cells, 1, 64);
    dst->min_frames = clampi(dst->min_frames, 1, 30);
    memset(dst->reserved, 0, sizeof(dst->reserved));
}

static int env_int(const char *name, int32_t *out)
{
    const char *s = getenv(name);
    char *end;
    long v;

    if (!s || !*s)
        return 0;
    v = strtol(s, &end, 0);
    if (*end)
        return 0;
    *out = (int32_t)v;
    return 1;
}

static void env_flag(const char *name, uint32_t bit, uint32_t *features)
{
    int32_t v;

    if (env_int(name, &v))
        *features = v ? (*features | bit) : (*features & ~bit);
}

void ivs_move_v2_config_env(OpenIMP_IVS_MoveConfigEx *cfg)
{
    const char *s = getenv("OPENIMP_MOTION_V2");
    OpenIMP_IVS_MoveConfigEx c = v2_defaults;
    int32_t v;

    if (s && *s) {
        if (!strcmp(s, "1") || !strcmp(s, "on") || !strcmp(s, "all"))
            c.features = OPENIMP_MOVE_F_ALL;
        else if (!strcmp(s, "shadow"))
            c.features = OPENIMP_MOVE_F_ALL & ~OPENIMP_MOVE_F_OVERRIDE;
        else if (env_int("OPENIMP_MOTION_V2", &v))
            c.features = (uint32_t)v;
    }
    env_flag("OPENIMP_MOTION_V2_BG", OPENIMP_MOVE_F_BACKGROUND, &c.features);
    env_flag("OPENIMP_MOTION_V2_SUPPRESS", OPENIMP_MOVE_F_SUPPRESS, &c.features);
    env_flag("OPENIMP_MOTION_V2_BLOBS", OPENIMP_MOVE_F_BLOBS, &c.features);
    env_flag("OPENIMP_MOTION_V2_OVERRIDE", OPENIMP_MOVE_F_OVERRIDE, &c.features);
    env_int("OPENIMP_MOTION_V2_LEARN", &c.learn_shift);
    env_int("OPENIMP_MOTION_V2_K", &c.thresh_k);
    env_int("OPENIMP_MOTION_V2_MIN_DELTA", &c.min_delta);
    env_int("OPENIMP_MOTION_V2_SUPPRESS_MS", &c.suppress_ms);
    env_int("OPENIMP_MOTION_V2_JUMP_PCT", &c.jump_pct);
    env_int("OPENIMP_MOTION_V2_MIN_CELLS", &c.min_cells);
    env_int("OPENIMP_MOTION_V2_MIN_FRAMES", &c.min_frames);
    ivs_move_v2_config_sanitize(cfg, &c);
}

/* ---------------- life cycle ---------------- */

static void v2_restart(IvsMoveV2 *v2)
{
    v2->warm = 0;
    v2->supp_until = 0;
    v2->supp_frames = 0;
    v2->supp_reason = 0;
    v2->ntr = 0;
    v2->ncomp = 0;
    memset(v2->mov, 0, v2->ncell);
    memset(v2->act, 0, v2->ncell);
    memset(v2->lab, 0, v2->ncell * sizeof(*v2->lab));
    memset(v2->rep, 0, v2->ncell + 2);
}

IvsMoveV2 *ivs_move_v2_create(uint32_t width, uint32_t height,
                              uint32_t interval,
                              const OpenIMP_IVS_MoveConfigEx *cfg)
{
    IvsMoveV2 *v2;
    uint32_t cs = 8;

    if (width < 32 || height < 32 || width > 8192 || height > 8192)
        return NULL;
    while (width / cs > V2_MAX_GRID_W || height / cs > V2_MAX_GRID_H)
        cs *= 2;
    v2 = calloc(1, sizeof(*v2));
    if (!v2)
        return NULL;
    v2->w = width;
    v2->h = height;
    v2->cs = cs;
    v2->step = cs / 4;
    v2->off = v2->step / 2;
    v2->gw = width / cs;
    v2->gh = height / cs;
    v2->ncell = v2->gw * v2->gh;
    v2->interval = interval ? interval : 1;
    v2->last_mode = -1;
    v2->next_id = 1;
    v2->cur = calloc(v2->ncell, sizeof(*v2->cur));
    v2->tex = calloc(v2->ncell, sizeof(*v2->tex));
    v2->tbg = calloc(v2->ncell, sizeof(*v2->tbg));
    v2->tdv = calloc(v2->ncell, sizeof(*v2->tdv));
    v2->mu = calloc(v2->ncell, sizeof(*v2->mu));
    v2->dev = calloc(v2->ncell, sizeof(*v2->dev));
    v2->mov = calloc(v2->ncell, 1);
    v2->exc = calloc(v2->ncell, 1);
    v2->act = calloc(v2->ncell, 1);
    v2->lab = calloc(v2->ncell, sizeof(*v2->lab));
    v2->parent = calloc(v2->ncell + 2, sizeof(*v2->parent));
    v2->rep = calloc(v2->ncell + 2, 1);
    if (!v2->cur || !v2->tex || !v2->tbg || !v2->tdv || !v2->mu || !v2->dev || !v2->mov || !v2->exc || !v2->act ||
        !v2->lab || !v2->parent || !v2->rep) {
        ivs_move_v2_destroy(v2);
        return NULL;
    }
    ivs_move_v2_config_sanitize(&v2->cfg, cfg);
    return v2;
}

void ivs_move_v2_destroy(IvsMoveV2 *v2)
{
    if (!v2)
        return;
    free(v2->cur);
    free(v2->tex);
    free(v2->tbg);
    free(v2->tdv);
    free(v2->mu);
    free(v2->dev);
    free(v2->mov);
    free(v2->exc);
    free(v2->act);
    free(v2->lab);
    free(v2->parent);
    free(v2->rep);
    free(v2);
}

void ivs_move_v2_set_config(IvsMoveV2 *v2, const OpenIMP_IVS_MoveConfigEx *cfg)
{
    OpenIMP_IVS_MoveConfigEx c;

    ivs_move_v2_config_sanitize(&c, cfg);
    if (c.features != v2->cfg.features)
        v2_restart(v2);
    v2->cfg = c;
}

/* ---------------- capture thread ---------------- */

int ivs_move_v2_next_frame(IvsMoveV2 *v2)
{
    int take = v2->phase == 0;

    if (++v2->phase >= v2->interval)
        v2->phase = 0;
    return take;
}

/* Cell value: 2 sample rows (at cs/4 and 3cs/4) x 8 samples (every cs/8
 * pixels) = 16 samples; cell texture: sum of |sample - next sample| along
 * the first row (7 differences). At 8x8 cells the second row is summed
 * as two words when the buffer allows it. */
#define V2_ABS(a) ((a) < 0 ? -(a) : (a))
static inline void v2_row(const uint8_t *b, uint32_t *sum, uint32_t *tex)
{
    int d0 = b[0] - b[1], d1 = b[1] - b[2], d2 = b[2] - b[3], d3 = b[3] - b[4];
    int d4 = b[4] - b[5], d5 = b[5] - b[6], d6 = b[6] - b[7];

    *sum += (uint32_t)(b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + b[7]);
    *tex += (uint32_t)(V2_ABS(d0) + V2_ABS(d1) + V2_ABS(d2) + V2_ABS(d3) +
                       V2_ABS(d4) + V2_ABS(d5) + V2_ABS(d6));
}

void ivs_move_v2_feed(IvsMoveV2 *v2, const uint8_t *luma, uint32_t stride,
                      int64_t timestamp)
{
    const uint32_t cs = v2->cs, gw = v2->gw, st = cs / 8;
    const int words = st == 1 && !((uintptr_t)luma & 3) && !(stride & 3);
    uint32_t gx, gy, i;

    for (gy = 0; gy < v2->gh; gy++) {
        const uint8_t *p0 = luma + (size_t)(gy * cs + cs / 4) * stride;
        const uint8_t *p1 = luma + (size_t)(gy * cs + 3 * cs / 4) * stride;
        uint16_t *acc = v2->cur + gy * gw;
        uint16_t *tx = v2->tex + gy * gw;

        for (gx = 0; gx < gw; gx++, p0 += cs, p1 += cs) {
            uint32_t sum = 0, tex = 0;

            if (st == 1) {
                v2_row(p0, &sum, &tex);
            } else {
                uint8_t b[8];

                for (i = 0; i < 8; i++)
                    b[i] = p0[i * st];
                v2_row(b, &sum, &tex);
            }
            if (words) {
                const uint32_t *w = (const uint32_t *)(const void *)p1;
                uint32_t a = (w[0] & 0x00ff00ffu) + ((w[0] >> 8) & 0x00ff00ffu) +
                             (w[1] & 0x00ff00ffu) + ((w[1] >> 8) & 0x00ff00ffu);

                sum += (a & 0xffffu) + (a >> 16);
            } else {
                for (i = 0; i < 8; i++)
                    sum += p1[i * st];
            }
            acc[gx] = (uint16_t)sum;
            tx[gx] = (uint16_t)tex;
        }
    }
    v2->ts = timestamp;
    v2->fed = 1;
}

/* ---------------- IVS thread ---------------- */

static uint16_t uf_find(uint16_t *parent, uint16_t a)
{
    while (parent[a] != a) {
        parent[a] = parent[parent[a]];
        a = parent[a];
    }
    return a;
}

static void uf_union(uint16_t *parent, uint16_t a, uint16_t b)
{
    a = uf_find(parent, a);
    b = uf_find(parent, b);
    if (a < b)
        parent[b] = a;
    else if (b < a)
        parent[a] = b;
}

/* Global brightness ratio frame / background, Q12: median of the per-cell
 * ratios of every fifth cell (Q5 histogram; cells that moved last frame
 * left out unless that is most of them), refined by the mean ratio of the
 * cells around the median. The ratios are parked in exc[] (the cell loop
 * rewrites it afterwards). */
#define V2_RATIO_STEP 5
static int32_t v2_gain_ratio(IvsMoveV2 *v2)
{
    uint32_t hist[V2_HIST_BINS];
    uint32_t c, n = 0, all = 0, half, acc, bin, sv = 0, sm = 0;
    uint8_t *ratio = v2->exc;
    int skip_moving;

    memset(hist, 0, sizeof(hist));
    for (c = 0; c < v2->ncell; c += V2_RATIO_STEP) {
        uint32_t mu = (uint32_t)(v2->mu[c] > 64 ? v2->mu[c] : 64);
        uint32_t r = ((uint32_t)v2->cur[c] << 9) / mu;   /* (cur << 4) / mu, Q5 */

        ratio[c] = (uint8_t)(r < V2_HIST_BINS ? r : V2_HIST_BINS - 1);
        all++;
        if (!v2->mov[c]) {
            hist[ratio[c]]++;
            n++;
        }
    }
    skip_moving = n * 2 >= all;
    if (!skip_moving) {
        memset(hist, 0, sizeof(hist));
        for (c = 0; c < v2->ncell; c += V2_RATIO_STEP)
            hist[ratio[c]]++;
        n = all;
    }
    if (!n)
        return 4096;
    half = (n + 1) / 2;
    for (bin = 0, acc = 0; bin < V2_HIST_BINS; bin++) {
        acc += hist[bin];
        if (acc >= half)
            break;
    }
    if (bin >= V2_HIST_BINS)
        bin = V2_HIST_BINS - 1;
    /* sums stay below 2^32: at most 80x60/3 cells of at most 65280 */
    for (c = 0; c < v2->ncell; c += V2_RATIO_STEP) {
        uint32_t r = ratio[c];

        if (skip_moving && v2->mov[c])
            continue;
        if (r + 1 >= bin && r <= bin + 1) {
            sv += (uint32_t)v2->cur[c] << 4;
            sm += (uint32_t)(v2->mu[c] > 64 ? v2->mu[c] : 64);
        }
    }
    if (!sm)
        return 4096;
    return (int32_t)(((uint64_t)sv << 12) / sm);
}

static void v2_report_tracks(IvsMoveV2 *v2)
{
    int i;

    memset(v2->rep, 0, v2->ncell + 2);
    for (i = 0; i < v2->ntr; i++) {
        struct v2_track *t = &v2->tr[i];

        t->reported = t->miss == 0 &&
                      t->age >= v2->cfg.min_frames;
        if (t->reported && t->label > 0)
            v2->rep[t->label] = 1;
    }
}

static unsigned int v2_strength(const IvsMoveV2 *v2, int exc)
{
    uint32_t norm = v2->ncell / 16 > 16 ? v2->ncell / 16 : 16;
    uint32_t s = (uint32_t)exc * 1000u / (64u * norm);

    return s > 1000 ? 1000 : s;
}

static void v2_objects(IvsMoveV2 *v2)
{
    const uint32_t gw = v2->gw, gh = v2->gh;
    uint32_t x, y, c;
    uint16_t next = 1;
    int i, j;

    v2->ncomp = 0;
    if (!(v2->cfg.features & OPENIMP_MOVE_F_BLOBS)) {
        /* one box around every moving cell, reported at once */
        struct v2_track *t = &v2->tr[0];
        int any = 0, exc = 0;

        v2->lab_dirty = 1;
        t->x0 = (int)gw; t->y0 = (int)gh; t->x1 = -1; t->y1 = -1;
        t->cells = 0;
        for (c = 0; c < v2->ncell; c++) {
            v2->lab[c] = v2->mov[c] ? 1 : 0;
            if (!v2->mov[c])
                continue;
            x = c % gw;
            y = c / gw;
            if ((int)x < t->x0) t->x0 = (int)x;
            if ((int)x > t->x1) t->x1 = (int)x;
            if ((int)y < t->y0) t->y0 = (int)y;
            if ((int)y > t->y1) t->y1 = (int)y;
            t->cells++;
            exc += v2->exc[c];
            any = 1;
        }
        if (any) {
            if (v2->ntr == 0) {
                t->id = v2->next_id++;
                if (!v2->next_id)
                    v2->next_id = 1;
                t->age = 0;
            }
            t->age++;
            t->miss = 0;
            t->label = 1;
            t->strength = v2_strength(v2, exc);
            v2->ntr = 1;
        } else {
            v2->ntr = 0;
        }
        memset(v2->rep, 0, v2->ncell + 2);
        if (any) {
            t->reported = 1;
            v2->rep[1] = 1;
        }
        return;
    }

    /* nothing moves: no labelling, the tracks just miss a frame */
    if (!v2->nmov) {
        if (v2->lab_dirty)
            memset(v2->lab, 0, v2->ncell * sizeof(*v2->lab));
        v2->lab_dirty = 0;
        goto track;
    }
    v2->lab_dirty = 1;
    /* 8-connected components, two passes with union-find */
    for (y = 0; y < gh; y++) {
        for (x = 0; x < gw; x++) {
            uint16_t l = 0;

            c = y * gw + x;
            if (!v2->mov[c]) {
                v2->lab[c] = 0;
                continue;
            }
            if (x > 0 && v2->lab[c - 1])
                l = v2->lab[c - 1];
            if (y > 0) {
                const uint16_t *up = v2->lab + c - gw;
                int k;

                for (k = -1; k <= 1; k++) {
                    if ((int)x + k < 0 || x + (uint32_t)(k + 1) > gw)
                        continue;
                    if (up[k]) {
                        if (!l)
                            l = up[k];
                        else if (up[k] != l)
                            uf_union(v2->parent, l, up[k]);
                    }
                }
            }
            if (!l) {
                l = next++;
                v2->parent[l] = l;
            }
            v2->lab[c] = l;
        }
    }
    /* resolve labels and gather per-root statistics in comp[] */
    {
        uint16_t *slot = v2->parent;    /* reuse: root -> comp index + 1 */
        uint16_t root;

        for (i = 1; i < next; i++)
            v2->parent[i] = uf_find(v2->parent, (uint16_t)i);
        /* parent[] now maps every label to its root; map roots to slots
         * through rep[] as scratch (cleared again in v2_report_tracks) */
        memset(v2->rep, 0, v2->ncell + 2);
        for (c = 0; c < v2->ncell; c++) {
            struct v2_comp *k;
            int idx;

            if (!v2->lab[c])
                continue;
            root = slot[v2->lab[c]];
            v2->lab[c] = root;
            idx = v2->rep[root];
            if (!idx) {
                if (v2->ncomp >= V2_MAX_COMPS) {
                    v2->lab[c] = 0;
                    continue;
                }
                idx = ++v2->ncomp;
                v2->rep[root] = (uint8_t)idx;
                k = &v2->comp[idx - 1];
                k->x0 = k->x1 = (int)(c % gw);
                k->y0 = k->y1 = (int)(c / gw);
                k->cells = 0;
                k->exc = 0;
                k->label = root;
                k->used = 0;
            }
            k = &v2->comp[idx - 1];
            x = c % gw;
            y = c / gw;
            if ((int)x < k->x0) k->x0 = (int)x;
            if ((int)x > k->x1) k->x1 = (int)x;
            if ((int)y < k->y0) k->y0 = (int)y;
            if ((int)y > k->y1) k->y1 = (int)y;
            k->cells++;
            k->exc += v2->exc[c];
        }
    }
track:
    /* track: match each existing track to the largest overlapping component */
    for (i = 0; i < v2->ntr; i++) {
        struct v2_track *t = &v2->tr[i];
        int best = -1;

        for (j = 0; j < v2->ncomp; j++) {
            struct v2_comp *k = &v2->comp[j];

            if (k->used || k->cells < v2->min_cells)
                continue;
            if (k->x1 < t->x0 - 1 || k->x0 > t->x1 + 1 ||
                k->y1 < t->y0 - 1 || k->y0 > t->y1 + 1)
                continue;
            if (best < 0 || k->cells > v2->comp[best].cells)
                best = j;
        }
        if (best >= 0) {
            struct v2_comp *k = &v2->comp[best];

            k->used = 1;
            t->x0 = k->x0; t->y0 = k->y0; t->x1 = k->x1; t->y1 = k->y1;
            t->cells = (unsigned int)k->cells;
            t->strength = v2_strength(v2, k->exc);
            t->label = k->label;
            t->age++;
            t->miss = 0;
        } else {
            t->miss++;
            t->label = 0;
        }
    }
    /* drop tracks missed twice in a row */
    for (i = 0, j = 0; i < v2->ntr; i++)
        if (v2->tr[i].miss <= 1)
            v2->tr[j++] = v2->tr[i];
    v2->ntr = j;
    /* new tracks for the remaining components that are large enough */
    for (j = 0; j < v2->ncomp && v2->ntr < V2_MAX_TRACKS; j++) {
        struct v2_comp *k = &v2->comp[j];
        struct v2_track *t;

        if (k->used || k->cells < v2->min_cells)
            continue;
        t = &v2->tr[v2->ntr++];
        memset(t, 0, sizeof(*t));
        t->x0 = k->x0; t->y0 = k->y0; t->x1 = k->x1; t->y1 = k->y1;
        t->cells = (unsigned int)k->cells;
        t->strength = v2_strength(v2, k->exc);
        t->label = k->label;
        t->age = 1;
        t->id = v2->next_id++;
        if (!v2->next_id)
            v2->next_id = 1;
    }
    v2_report_tracks(v2);
}

/* Threshold scale (Q4) by the vendor sense of the most sensitive ROI:
 * sense 2 (timps default) = the configured thresholds, 0 = x2, 4 = x0.5. */
static const uint8_t v2_sense_scale[9] = { 32, 24, 16, 12, 8, 7, 6, 5, 4 };
/* Vendor move: an ROI fires when more than T[sense] pixels of the 2:1
 * decimated, eroded difference remain. An object needs at least that area
 * (x4 for full resolution) in v2 too, so a sensitivity setting means the
 * same object size in both. */
static const uint16_t v2_sense_area[9] = { 1365, 455, 151, 50, 16, 8, 4, 2, 1 };

void ivs_move_v2_run(IvsMoveV2 *v2, int isp_mode, uint32_t total_gain,
                     int64_t now_ms, int sense)
{
    const OpenIMP_IVS_MoveConfigEx *cfg = &v2->cfg;
    const int bg = (cfg->features & OPENIMP_MOVE_F_BACKGROUND) != 0;
    const int ls = cfg->learn_shift;
    const int32_t sc = v2_sense_scale[sense < 0 || sense > 8 ? 2 : sense];
    const int32_t mind = (cfg->min_delta << 8) * sc >> 4;
    const int32_t kk = cfg->thresh_k * sc >> 4;
    const uint32_t area = (uint32_t)v2_sense_area[sense < 0 || sense > 8 ? 2 : sense] * 4u;
    const uint32_t cell_px = v2->cs * v2->cs;
    uint32_t reason = 0, c, phase;
    int nmov;
    int32_t g;
    int suppressed, learning;

    if (!v2->fed)
        return;
    v2->fed = 0;
    v2->last_ts = v2->ts;
    v2->seq++;
    v2->min_cells = (int)((area + cell_px / 2) / cell_px);
    if (v2->min_cells < cfg->min_cells)
        v2->min_cells = cfg->min_cells;

    if (isp_mode >= 0) {
        if (v2->last_mode >= 0 && isp_mode != v2->last_mode)
            reason |= OPENIMP_MOVE_SUPP_DAYNIGHT;
        v2->last_mode = isp_mode;
    }
    if (total_gain) {
        if (v2->last_gain) {
            uint32_t hi = total_gain > v2->last_gain ? total_gain : v2->last_gain;
            uint32_t lo = total_gain > v2->last_gain ? v2->last_gain : total_gain;

            if ((uint64_t)(hi - lo) * 100u > (uint64_t)cfg->jump_pct * hi)
                reason |= OPENIMP_MOVE_SUPP_GAIN;
        }
        v2->last_gain = total_gain;
    }

    if (v2->warm == 0) {
        int32_t dev0 = (mind << 4) / cfg->thresh_k;

        for (c = 0; c < v2->ncell; c++) {
            v2->mu[c] = (uint16_t)(v2->cur[c] << 4);
            v2->tbg[c] = (uint16_t)(v2->tex[c] << 2);
            v2->tdv[c] = V2_TEX_FLOOR;
            v2->dev[c] = (uint16_t)(dev0 > 65535 ? 65535 : dev0);
        }
        memset(v2->mov, 0, v2->ncell);
        v2->warm = 1;
        v2->ntr = 0;
        memset(v2->rep, 0, v2->ncell + 2);
        return;
    }

    g = v2_gain_ratio(v2);      /* mu < 2^17, g clamped: product < 2^32 */
    if (g > 32767)
        g = 32767;
    if ((int64_t)(g > 4096 ? g - 4096 : 4096 - g) * 100 >
        (int64_t)cfg->jump_pct * 4096)
        reason |= OPENIMP_MOVE_SUPP_LUMA;
    if ((cfg->features & OPENIMP_MOVE_F_SUPPRESS) && reason) {
        if (now_ms >= v2->supp_until && !v2->supp_frames)
            v2->supp_reason = 0;
        v2->supp_until = now_ms + cfg->suppress_ms;
        v2->supp_frames = 3;    /* at least this frame and two more */
        v2->supp_reason |= reason;
    }
    suppressed = (cfg->features & OPENIMP_MOVE_F_SUPPRESS) &&
                 (now_ms < v2->supp_until || v2->supp_frames);
    if (v2->supp_frames)
        v2->supp_frames--;
    if (!suppressed)
        v2->supp_reason = 0;
    learning = v2->warm < V2_WARMUP;

    /* The noise statistics (dev, tdv) and the flicker share (act) of a
     * quarter of the cells are updated per frame, at four times the rate:
     * the same time constants for a quarter of the work. */
    phase = v2->seq & 3;
    nmov = 0;
    for (c = 0; c < v2->ncell; c++) {
        int32_t v4 = (int32_t)v2->cur[c] << 4;
        int32_t mu = v2->mu[c];
        int32_t e = (int32_t)(((uint32_t)mu * (uint32_t)g) >> 12);
        int32_t d = v4 - e;
        int32_t ad = d < 0 ? -d : d;
        int32_t thr = (kk * (int32_t)v2->dev[c]) >> 4;
        int32_t t4 = (int32_t)v2->tex[c] << 2;
        int32_t tb = v2->tbg[c];
        int m = 0;

        if (thr < mind)
            thr = mind;
        /* flicker / foliage: a cell that keeps moving for a long time
         * (lamps, leaves, water) needs three times the threshold */
        if (bg && v2->act[c] > V2_ACT_LIMIT)
            thr *= 3;
        if (ad > thr && !suppressed && !learning) {
            m = 1;
            /* texture: a change of the cell mean that keeps the texture of
             * the background, scaled by the cell's own brightness ratio, is
             * light (shadow, lamp, reflection, headlights), not an object;
             * very large mean changes count anyway */
            if (bg && ad <= 3 * thr) {
                int32_t tthr = (kk * (int32_t)v2->tdv[c]) >> 4;
                int32_t r8 = (v4 << 8) / (mu > 64 ? mu : 64);
                int32_t et = (tb * (r8 > 1024 ? 1024 : r8)) >> 8;

                if (tthr < V2_TEX_FLOOR)
                    tthr = V2_TEX_FLOOR;
                m = V2_ABS(t4 - et) > tthr;
            }
        }
        v2->mov[c] = (uint8_t)m;
        nmov += m;
        if (m) {
            int32_t x = (ad << 4) / thr;

            v2->exc[c] = (uint8_t)(x > 64 ? 64 : x);
        } else {
            v2->exc[c] = 0;
        }
        if (suppressed || learning) {
            mu += (v4 - mu) >> 1;
            tb += (t4 - tb) >> 1;
        } else if (!bg) {
            mu = v4;
            tb = t4;
        } else if (!m) {
            mu += (v4 - mu) >> ls;
            tb += (t4 - tb) >> ls;
        } else {
            mu += (v4 - mu) >> (ls + 4);
            tb += (t4 - tb) >> (ls + 4);
        }
        v2->mu[c] = (uint16_t)mu;   /* between old and new value: fits */
        v2->tbg[c] = (uint16_t)tb;
        if ((c & 3) != phase || suppressed)
            continue;
        if (bg && !learning)
            v2->act[c] = (uint8_t)(v2->act[c] +
                                   (((m ? 255 : 0) - (int)v2->act[c]) >> 4));
        {
            int32_t cl = ad < 2 * thr ? ad : 2 * thr;
            int32_t dv = (int32_t)v2->dev[c];
            int32_t tthr = (kk * (int32_t)v2->tdv[c]) >> 4;
            int32_t eg = (int32_t)(((uint32_t)tb * (uint32_t)g) >> 12);
            int32_t dt = V2_ABS(t4 - eg);
            int32_t tv = (int32_t)v2->tdv[c];

            dv += (cl - dv) >> (m ? 5 : 2);
            v2->dev[c] = (uint16_t)(dv < 1 ? 1 : dv > 65535 ? 65535 : dv);
            if (tthr < V2_TEX_FLOOR)
                tthr = V2_TEX_FLOOR;
            if (dt > 2 * tthr)
                dt = 2 * tthr;
            tv += (dt - tv) >> (m ? 5 : 2);
            v2->tdv[c] = (uint16_t)(tv < 1 ? 1 : tv > 65535 ? 65535 : tv);
        }
    }
    v2->nmov = nmov;
    if (v2->warm < 0xffff)
        v2->warm++;
    if (suppressed || learning) {
        v2->ntr = 0;
        v2->ncomp = 0;
        memset(v2->lab, 0, v2->ncell * sizeof(*v2->lab));
        memset(v2->rep, 0, v2->ncell + 2);
        return;
    }
    v2_objects(v2);
}

void ivs_move_v2_result(const IvsMoveV2 *v2, OpenIMP_IVS_MoveOutputEx *ex,
                        int roi_cnt, const int *roi_x0, const int *roi_y0,
                        const int *roi_x1, const int *roi_y1, int *ret_roi)
{
    const int cs = (int)v2->cs;
    uint32_t c;
    int i, n = 0;

    if (ret_roi)
        for (i = 0; i < roi_cnt; i++)
            ret_roi[i] = 0;
    ex->timestamp = v2->last_ts;
    ex->flags |= OPENIMP_MOVE_EX_ACTIVE;
    if (v2->warm < V2_WARMUP)
        ex->flags |= OPENIMP_MOVE_EX_WARMUP;
    if (v2->supp_reason)
        ex->flags |= OPENIMP_MOVE_EX_SUPPRESSED;
    ex->suppress = v2->supp_reason;
    ex->grid_w = v2->gw;
    ex->grid_h = v2->gh;
    ex->v2_roi[0] = ex->v2_roi[1] = 0;
    for (i = 0; i < v2->ntr && n < OPENIMP_IVS_MOVE_EX_MAX_OBJ; i++) {
        const struct v2_track *t = &v2->tr[i];
        OpenIMP_IVS_MoveObject *o;

        if (!t->reported)
            continue;
        o = &ex->obj[n++];
        o->x0 = (int16_t)(t->x0 * cs);
        o->y0 = (int16_t)(t->y0 * cs);
        o->x1 = (int16_t)(t->x1 * cs + cs - 1);
        o->y1 = (int16_t)(t->y1 * cs + cs - 1);
        o->strength = (uint16_t)t->strength;
        o->cells = (uint16_t)(t->cells > 65535 ? 65535 : t->cells);
        o->age = (uint16_t)(t->age > 65535 ? 65535 : t->age);
        o->id = t->id;
    }
    ex->obj_cnt = (uint32_t)n;
    if (!n || roi_cnt <= 0)
        return;
    for (c = 0; c < v2->ncell; c++) {
        int x0, y0;

        if (!v2->lab[c] || !v2->rep[v2->lab[c]])
            continue;
        x0 = (int)(c % v2->gw) * cs;
        y0 = (int)(c / v2->gw) * cs;
        for (i = 0; i < roi_cnt && i < 64; i++) {
            if (x0 + cs - 1 < roi_x0[i] || x0 > roi_x1[i] ||
                y0 + cs - 1 < roi_y0[i] || y0 > roi_y1[i])
                continue;
            ex->v2_roi[i >> 5] |= 1u << (i & 31);
            if (ret_roi)
                ret_roi[i] = 1;
        }
    }
}
