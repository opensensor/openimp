/* T31 IVS motion algorithms ("move" and "base move").
 *
 * Clean-room implementation after a behavioural description of the T31
 * 1.1.6 libimp scalar path; results are bit-identical to an independent
 * reference model of that path, except where noted ("deviation").
 *
 * move (per ROI yes/no)
 *   - The luma is decimated 2:1 (top-left pixel of every 2x2 block) into a
 *     ring of four half-resolution images.
 *   - Input frame t is always compared with input frame t-3.
 *   - D = |A - B| > 20, then a 3x3 erosion (all nine neighbours set; image
 *     borders replicate), then per ROI count > T[sense].
 *   - The first three frames publish all-zero results; afterwards a result
 *     comes every skipFrameCnt + 1 frames, the first at frame skip + 3.
 *   - When the ROI bounding box does not start at row 0, the vendor feeds
 *     the erosion one row late: output row y is centred on image row y + 1.
 *     This is reproduced. If the box then also touches the bottom row, the
 *     vendor reads one row past its buffer; here that row is replicated
 *     from the last image row instead (deviation, affects only that case).
 *
 * base move (one byte per 8x8 block)
 *   - Frame n is compared with frame n - referenceNum every skipFrameCnt + 1
 *     frames, in full resolution.
 *   - T = |A - B| if >= thr[sense] else 0, then a 3x3 minimum (borders
 *     replicate), then the 8x8 block sum truncated to 8 bit.
 *
 * Performance notes: move keeps the binary maps as bit rows (32 pixels per
 * word); the difference test runs four pixels per 32-bit word in byte
 * lanes, erosion and counting are word operations. Only frames that take
 * part in a comparison are decimated. Eroded rows are computed on demand:
 * one empty row clears three output rows, so a still scene differences
 * only every third row. When the new frame is not needed again as a
 * reference (skipFrameCnt 1 or >= 3), the detection runs in feed() on the
 * input frame directly and decimates just the rows it reads; run() then
 * only applies the thresholds. All of this leaves the results unchanged
 * (tests/t23/ivs_move_bench.c compares them with the previous code).
 * base move uses a lookup table for the thresholded difference and a
 * separable, branch-free minimum. */

#include <stdlib.h>
#include <string.h>

#include "openimp_t31_ivs_move.h"

typedef uint32_t __attribute__((may_alias)) t31_ivs_u32a;

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static inline uint32_t popcount32(uint32_t x)
{
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0f0f0f0fu;
    return (x * 0x01010101u) >> 24;
}

/* ================================ move ================================ */

#define MV_RING 4
#define MV_MAX_ROI IMP_IVS_MOVE_MAX_ROI_CNT

/* MV_JOB_COUNTED: the detection already ran in feed(), straight from the
 * input frame; run() only applies the thresholds. */
enum { MV_JOB_NONE, MV_JOB_WARMUP, MV_JOB_DETECT, MV_JOB_COUNTED };
enum { MV_ROW_UNKNOWN, MV_ROW_ZERO, MV_ROW_SET };

static const int move_threshold[9] = { 1365, 455, 151, 50, 16, 8, 4, 2, 1 };

struct T31IvsMove {
    IMP_IVS_MoveParam param;        /* internal copy, what GetParam returns */
    int width, height;
    int w2, h2, pitch;              /* half-resolution image, row pitch */
    int cnt, cur, ref, skip;        /* frame state machine */
    uint8_t *buf[MV_RING];
    int nroi;
    int rx0[MV_MAX_ROI], ry0[MV_MAX_ROI], rx1[MV_MAX_ROI], ry1[MV_MAX_ROI];
    int thr[MV_MAX_ROI];
    int bx0, by0, bx1, by1;         /* ROI bounding box, inclusive */
    int job, job_a, job_b;
    int wpr;                        /* words per bit row incl. two guards */
    uint32_t *hbits;                /* horizontally eroded rows, h2 * wpr */
    uint32_t *dwork;                /* difference bits of one row */
    uint32_t *erow;                 /* fully eroded output row */
    uint8_t *hstate;                /* MV_ROW_* per hbits row */
    int xs, xe, ws, nw;             /* detection columns, set per detection */
    const uint8_t *src;             /* new image straight from the frame */
    uint32_t sstride;
    uint8_t *arow;                  /* one decimated row of src, pitch bytes */
    int counts[MV_MAX_ROI];
};

static void move_set_rois(T31IvsMove *m, const T31IVSRect *rect, int count)
{
    int i;

    m->nroi = count;
    m->bx0 = m->by0 = 1 << 30;
    m->bx1 = m->by1 = -1;
    for (i = 0; i < count; i++) {
        /* Clamp to the frame, halve; inclusive corners. The extra clamp to
         * the half-resolution image only matters for odd frame sizes. */
        m->rx0[i] = clampi(clampi(rect[i].p0.x, 0, m->width - 1) >> 1, 0, m->w2 - 1);
        m->ry0[i] = clampi(clampi(rect[i].p0.y, 0, m->height - 1) >> 1, 0, m->h2 - 1);
        m->rx1[i] = clampi(clampi(rect[i].p1.x, 0, m->width - 1) >> 1, 0, m->w2 - 1);
        m->ry1[i] = clampi(clampi(rect[i].p1.y, 0, m->height - 1) >> 1, 0, m->h2 - 1);
        if (m->rx0[i] < m->bx0)
            m->bx0 = m->rx0[i];
        if (m->ry0[i] < m->by0)
            m->by0 = m->ry0[i];
        if (m->rx1[i] > m->bx1)
            m->bx1 = m->rx1[i];
        if (m->ry1[i] > m->by1)
            m->by1 = m->ry1[i];
    }
}

T31IvsMove *t31_ivs_move_create(const IMP_IVS_MoveParam *param)
{
    T31IvsMove *m;
    int i;

    if (!param || param->frameInfo.width < 2 || param->frameInfo.height < 2 ||
        param->frameInfo.width > 8192 || param->frameInfo.height > 8192)
        return NULL;
    m = calloc(1, sizeof(*m));
    if (!m)
        return NULL;
    m->param = *param;
    m->width = (int)param->frameInfo.width;
    m->height = (int)param->frameInfo.height;
    m->w2 = m->width >> 1;
    m->h2 = m->height >> 1;
    m->pitch = (m->w2 + 31) & ~31;
    m->skip = param->skipFrameCnt;
    m->wpr = m->pitch / 32 + 2;
    for (i = 0; i < MV_RING; i++) {
        m->buf[i] = calloc((size_t)m->pitch * (size_t)m->h2, 1);
        if (!m->buf[i])
            goto fail;
    }
    m->hbits = calloc((size_t)m->wpr * (size_t)m->h2, sizeof(uint32_t));
    m->dwork = calloc((size_t)m->wpr, sizeof(uint32_t));
    m->erow = calloc((size_t)m->wpr, sizeof(uint32_t));
    m->hstate = calloc((size_t)m->h2, 1);
    m->arow = calloc((size_t)m->pitch, 1);  /* padding stays zero */
    if (!m->hbits || !m->dwork || !m->erow || !m->hstate || !m->arow)
        goto fail;
    move_set_rois(m, param->roiRect, clampi(param->roiRectCnt, 0, MV_MAX_ROI));
    for (i = 0; i < m->nroi; i++)
        m->thr[i] = move_threshold[clampi(param->sense[i], 0, 4)];
    return m;

fail:
    t31_ivs_move_destroy(m);
    return NULL;
}

void t31_ivs_move_destroy(T31IvsMove *m)
{
    int i;

    if (!m)
        return;
    for (i = 0; i < MV_RING; i++)
        free(m->buf[i]);
    free(m->hbits);
    free(m->dwork);
    free(m->erow);
    free(m->hstate);
    free(m->arow);
    free(m);
}

void t31_ivs_move_frame_size(const T31IvsMove *m, uint32_t *width,
                             uint32_t *height)
{
    *width = (uint32_t)m->width;
    *height = (uint32_t)m->height;
}

int t31_ivs_move_set_param(T31IvsMove *m, const IMP_IVS_MoveParam *param)
{
    int count = clampi(param->roiRectCnt, 0, MV_MAX_ROI);
    int i;

    /* The vendor accepts sense 0..8 here (0..4 at creation). It fails half
     * way through on a bad value and leaves a freed filter behind; this
     * checks first and keeps the old state instead (deviation). */
    for (i = 0; i < count; i++)
        if (param->sense[i] < 0 || param->sense[i] > 8)
            return -1;
    memcpy(m->param.sense, param->sense, sizeof(m->param.sense));
    if (count >= 1) {
        memcpy(m->param.roiRect, param->roiRect, sizeof(m->param.roiRect));
        m->param.roiRectCnt = param->roiRectCnt;
        move_set_rois(m, param->roiRect, count);
    } else {
        m->nroi = 0;
    }
    for (i = 0; i < m->nroi; i++)
        m->thr[i] = move_threshold[param->sense[i]];
    return 0;
}

void t31_ivs_move_get_param(const T31IvsMove *m, IMP_IVS_MoveParam *param)
{
    *param = m->param;
}

/* The frame state machine, on the counter alone. */
static int move_step(int skip, int *cnt)
{
    int c = *cnt;

    if (c < MV_RING - 1) {
        *cnt = c + 1;
        return MV_JOB_WARMUP;
    }
    if (c >= skip && c >= skip + MV_RING - 1) {
        *cnt = MV_RING - 1;
        return MV_JOB_DETECT;
    }
    *cnt = c + 1;
    return MV_JOB_NONE;
}

/* Frame t lands in ring slot t % 4 and is only ever read at t (as the new
 * image) or at t + 3 (as the reference). Everything else the vendor
 * decimates is overwritten unread, so it is skipped here. */
int t31_ivs_move_needs_luma(const T31IvsMove *m)
{
    int c = m->cnt;
    int i;

    if (move_step(m->skip, &c) == MV_JOB_DETECT)
        return 1;
    if (m->cnt >= MV_RING - 1 && m->cnt < m->skip)
        return 0;                   /* the vendor does not store this frame */
    for (i = 0; i < 2; i++)
        move_step(m->skip, &c);
    return move_step(m->skip, &c) == MV_JOB_DETECT;
}

static void decimate_row(uint8_t *dst, const uint8_t *src, int w2)
{
    int x = 0;

    if (!((uintptr_t)src & 3u)) {
        const t31_ivs_u32a *s = (const t31_ivs_u32a *)(const void *)src;
        t31_ivs_u32a *d = (t31_ivs_u32a *)(void *)dst;

        for (; x + 8 <= w2; x += 8, s += 4, d += 2) {
            uint32_t a = s[0], b = s[1], c = s[2], e = s[3];

            d[0] = (a & 0xffu) | ((a >> 8) & 0xff00u) |
                   ((b & 0xffu) << 16) | ((b << 8) & 0xff000000u);
            d[1] = (c & 0xffu) | ((c >> 8) & 0xff00u) |
                   ((e & 0xffu) << 16) | ((e << 8) & 0xff000000u);
        }
    }
    for (; x < w2; x++)
        dst[x] = src[2 * x];
}

static void move_detect(T31IvsMove *m, const uint8_t *A, const uint8_t *B);

/* Is the frame being fed now read again, as the reference three frames
 * later? Called with the state after this frame. */
static int move_ref_later(const T31IvsMove *m)
{
    int c = m->cnt;

    move_step(m->skip, &c);
    move_step(m->skip, &c);
    return move_step(m->skip, &c) == MV_JOB_DETECT;
}

void t31_ivs_move_feed(T31IvsMove *m, const uint8_t *luma, uint32_t stride)
{
    int y;

    if (m->cnt < MV_RING - 1) {
        if (luma)
            for (y = 0; y < m->h2; y++)
                decimate_row(m->buf[m->cnt] + (size_t)y * m->pitch,
                             luma + (size_t)(2 * y) * stride, m->w2);
        m->cnt++;
        m->cur = m->cnt;
        m->job = MV_JOB_WARMUP;
        return;
    }
    m->cur %= MV_RING;
    m->ref %= MV_RING;
    m->job = MV_JOB_NONE;
    if (m->cnt < m->skip) {
        m->cnt++;
    } else if (m->cnt >= m->skip + MV_RING - 1) {
        m->job = MV_JOB_DETECT;
        m->job_a = m->cur;
        m->job_b = m->ref;
        m->skip = m->param.skipFrameCnt;
        m->cnt = MV_RING - 1;
        if (luma && !move_ref_later(m)) {
            /* Nobody reads this frame again: difference it against the
             * reference right here instead of decimating all of it into
             * the ring first. Only the rows the detection asks for are
             * decimated (a third of them in a still scene). */
            m->src = luma;
            m->sstride = stride;
            move_detect(m, NULL, m->buf[m->job_b]);
            m->src = NULL;
            m->job = MV_JOB_COUNTED;
        } else if (luma) {
            for (y = 0; y < m->h2; y++)
                decimate_row(m->buf[m->cur] + (size_t)y * m->pitch,
                             luma + (size_t)(2 * y) * stride, m->w2);
        }
    } else {
        if (luma)
            for (y = 0; y < m->h2; y++)
                decimate_row(m->buf[m->cur] + (size_t)y * m->pitch,
                             luma + (size_t)(2 * y) * stride, m->w2);
        m->cnt++;
    }
    m->ref++;
    m->cur++;
}

/* Four pixels per word: bit k of the result is |a_k - b_k| > 20, with
 * byte lanes and no carry between them. With y = 255 - b, the floor
 * average (a + y) >> 1 is >= 138 exactly when a - b >= 21 and the ceiling
 * average (a + y + 1) >> 1 is <= 117 exactly when a - b <= -21. Both
 * averages and both constant compares stay inside their byte; the
 * multiply gathers the four lane flags (bits 0, 8, 16, 24) into bits
 * 24..27 without overlapping partial products. */
static inline uint32_t move_diff4(uint32_t a, uint32_t b)
{
    const uint32_t lo7 = 0x7f7f7f7fu;
    uint32_t y = ~b;
    uint32_t s = ((a ^ y) >> 1) & lo7;
    uint32_t fl = (a & y) + s;
    uint32_t ce = (a | y) - s;
    uint32_t ge = fl & ((fl & lo7) + 0x76767676u);
    uint32_t lt = ~(ce | ((ce & lo7) + 0x0a0a0a0au));
    uint32_t f = ((ge | lt) >> 7) & 0x01010101u;

    return (f * 0x01020408u) >> 24;
}

static int count_range(const uint32_t *w, int x0, int x1)
{
    int i0 = x0 >> 5, i1 = x1 >> 5, i;
    uint32_t m0 = ~0u << (x0 & 31);
    uint32_t m1 = ~0u >> (31 - (x1 & 31));
    int c;

    if (i0 == i1)
        return (int)popcount32(w[i0] & m0 & m1);
    c = (int)popcount32(w[i0] & m0);
    for (i = i0 + 1; i < i1; i++)
        c += (int)popcount32(w[i]);
    return c + (int)popcount32(w[i1] & m1);
}

/* Difference bits of image row r inside the bounding-box words, then the
 * horizontal 3-erosion into hbits row r. Returns non-zero when any bit of
 * the eroded row is set. */
static uint32_t move_hrow(T31IvsMove *m, const uint8_t *A, const uint8_t *B,
                          int r)
{
    const int w2 = m->w2, ws = m->ws, nw = m->nw, base = ws << 5;
    const t31_ivs_u32a *a;
    const t31_ivs_u32a *b =
        (const t31_ivs_u32a *)(const void *)(B + (size_t)r * m->pitch + base);
    uint32_t *d = m->dwork;
    uint32_t *h = m->hbits + (size_t)r * m->wpr;
    uint32_t any = 0, dor = 0;
    int i;

    if (m->src) {
        int n = nw << 5;

        if (n > w2 - base)
            n = w2 - base;
        decimate_row(m->arow + base, m->src + (size_t)(2 * r) * m->sstride +
                     2 * (size_t)base, n);
        a = (const t31_ivs_u32a *)(const void *)(m->arow + base);
    } else {
        a = (const t31_ivs_u32a *)(const void *)(A + (size_t)r * m->pitch + base);
    }
    for (i = 1; i <= nw; i++, a += 8, b += 8) {
        d[i] = move_diff4(a[0], b[0]) |
               (move_diff4(a[1], b[1]) << 4) |
               (move_diff4(a[2], b[2]) << 8) |
               (move_diff4(a[3], b[3]) << 12) |
               (move_diff4(a[4], b[4]) << 16) |
               (move_diff4(a[5], b[5]) << 20) |
               (move_diff4(a[6], b[6]) << 24) |
               (move_diff4(a[7], b[7]) << 28);
        dor |= d[i];
    }
    if (!dor) {
        /* No difference at all: the eroded row is zero. Callers only read
         * hbits rows whose state says non-zero. */
        m->hstate[r] = MV_ROW_ZERO;
        return 0;
    }
    /* Guard bits: replicate at the image borders; elsewhere the value
     * only reaches pixels outside the bounding box. */
    d[0] = ws == 0 ? d[1] << 31 : 0;
    d[nw + 1] = 0;
    if (m->xe == w2 - 1) {
        int rel = w2 - base, last = rel - 1;
        uint32_t bit = (d[1 + (last >> 5)] >> (last & 31)) & 1u;
        uint32_t *dw = &d[1 + (rel >> 5)];

        *dw = (*dw & ~(1u << (rel & 31))) | (bit << (rel & 31));
    }
    for (i = 1; i <= nw; i++) {
        h[i] = d[i] & ((d[i] << 1) | (d[i - 1] >> 31)) &
               ((d[i] >> 1) | (d[i + 1] << 31));
        any |= h[i];
    }
    m->hstate[r] = any ? MV_ROW_SET : MV_ROW_ZERO;
    return any;
}

/* Is eroded row r known to be zero, computing it if needed? */
static int move_row_zero(T31IvsMove *m, const uint8_t *A, const uint8_t *B,
                         int r)
{
    if (m->hstate[r] == MV_ROW_UNKNOWN)
        (void)move_hrow(m, A, B, r);
    return m->hstate[r] == MV_ROW_ZERO;
}

/* Output row y is the AND of eroded rows y+sh-1, y+sh, y+sh+1, so one
 * all-zero eroded row clears three output rows. Eroded rows are computed
 * on demand, the one furthest down first: in a still scene only every
 * third row is ever differenced, and the result is the same as computing
 * them all. */
static void move_detect(T31IvsMove *m, const uint8_t *A, const uint8_t *B)
{
    const int h2 = m->h2;
    int sh, ws, base, y, i;

    memset(m->counts, 0, sizeof(m->counts));
    if (m->nroi <= 0 || m->bx1 < m->bx0 || m->by1 < m->by0)
        return;
    sh = m->by0 > 0 ? 1 : 0;        /* vendor row offset, see top */
    m->xs = m->bx0 > 0 ? m->bx0 - 1 : 0;
    m->xe = m->bx1 + 1 < m->w2 ? m->bx1 + 1 : m->w2 - 1;
    ws = m->ws = m->xs >> 5;
    m->nw = (m->xe >> 5) - ws + 1;
    base = ws << 5;
    memset(m->hstate, MV_ROW_UNKNOWN, (size_t)h2);

    for (y = m->by0; y <= m->by1; y++) {
        int ra = clampi(y + sh - 1, 0, h2 - 1);
        int rb = clampi(y + sh, 0, h2 - 1);
        int rc = clampi(y + sh + 1, 0, h2 - 1);
        const uint32_t *ha, *hb, *hc;
        uint32_t *e = m->erow;
        uint32_t any = 0;

        if (m->hstate[ra] == MV_ROW_ZERO || m->hstate[rb] == MV_ROW_ZERO ||
            m->hstate[rc] == MV_ROW_ZERO)
            continue;
        if (move_row_zero(m, A, B, rc) || move_row_zero(m, A, B, rb) ||
            move_row_zero(m, A, B, ra))
            continue;
        ha = m->hbits + (size_t)ra * m->wpr;
        hb = m->hbits + (size_t)rb * m->wpr;
        hc = m->hbits + (size_t)rc * m->wpr;
        for (i = 1; i <= m->nw; i++) {
            e[i] = ha[i] & hb[i] & hc[i];
            any |= e[i];
        }
        if (!any)
            continue;
        for (i = 0; i < m->nroi; i++)
            if (y >= m->ry0[i] && y <= m->ry1[i] && m->rx0[i] <= m->rx1[i])
                m->counts[i] += count_range(e + 1, m->rx0[i] - base,
                                            m->rx1[i] - base);
    }
}

int t31_ivs_move_run(T31IvsMove *m, int *retRoi)
{
    int job = m->job;
    int i;

    m->job = MV_JOB_NONE;
    if (job == MV_JOB_WARMUP) {
        memset(retRoi, 0, sizeof(int) * (size_t)m->nroi);
        return 0;
    }
    if (job == MV_JOB_DETECT)
        move_detect(m, m->buf[m->job_a], m->buf[m->job_b]);
    else if (job != MV_JOB_COUNTED)
        return 1;
    for (i = 0; i < m->nroi; i++)
        retRoi[i] = m->counts[i] > m->thr[i];
    return 0;
}

/* ============================== base move ============================== */

static const int base_threshold[4] = { 30, 20, 15, 10 };

struct T31IvsBaseMove {
    IMP_IVS_BaseMoveParam param;
    int w, h;
    int period, refnum, nbuf, start, det0;
    long long n;
    int wr, ref, flag;
    int thr;
    uint8_t **buf;
    uint8_t lut[511];               /* |d| >= thr ? |d| : 0, index d + 255 */
    uint8_t *hmin[3];               /* horizontal minima of three rows */
    uint32_t *acc;                  /* block sums of the current block row */
    int job_sad, job_a, job_b, job_ret;
};

static void base_build_lut(T31IvsBaseMove *b)
{
    int d;

    for (d = -255; d <= 255; d++) {
        int a = d < 0 ? -d : d;

        b->lut[d + 255] = (uint8_t)(a >= b->thr ? a : 0);
    }
}

static int base_sense_threshold(int sense)
{
    return sense >= 0 && sense < 4 ? base_threshold[sense] : 0;
}

T31IvsBaseMove *t31_ivs_base_move_create(const IMP_IVS_BaseMoveParam *param)
{
    T31IvsBaseMove *b;
    int k, i;

    if (!param || param->skipFrameCnt < 0 || param->referenceNum <= 0 ||
        param->sadMode != 0 || param->frameInfo.width < 1 ||
        param->frameInfo.height < 1 || param->frameInfo.width > 8192 ||
        param->frameInfo.height > 8192 || param->skipFrameCnt > 0x3fffffff)
        return NULL;
    b = calloc(1, sizeof(*b));
    if (!b)
        return NULL;
    b->param = *param;
    b->w = (int)param->frameInfo.width;
    b->h = (int)param->frameInfo.height;
    b->period = param->skipFrameCnt + 1;
    b->refnum = param->referenceNum;
    b->nbuf = b->period < b->refnum
        ? (b->refnum + b->period - 1) / b->period + 1 : 2;
    if (b->nbuf > 64)
        goto fail;                  /* one full frame per buffer */
    for (k = 1; k * b->period < b->refnum; k++)
        ;
    b->det0 = k * b->period;
    b->start = b->det0 - b->refnum;
    b->thr = base_sense_threshold(param->sense);
    base_build_lut(b);
    b->buf = calloc((size_t)b->nbuf, sizeof(*b->buf));
    if (!b->buf)
        goto fail;
    for (i = 0; i < b->nbuf; i++) {
        b->buf[i] = calloc((size_t)b->w * (size_t)b->h, 1);
        if (!b->buf[i])
            goto fail;
    }
    for (i = 0; i < 3; i++) {
        b->hmin[i] = malloc((size_t)b->w);
        if (!b->hmin[i])
            goto fail;
    }
    b->acc = calloc((size_t)b->w / 8 + 1, sizeof(uint32_t));
    if (!b->acc)
        goto fail;
    return b;

fail:
    t31_ivs_base_move_destroy(b);
    return NULL;
}

void t31_ivs_base_move_destroy(T31IvsBaseMove *b)
{
    int i;

    if (!b)
        return;
    if (b->buf)
        for (i = 0; i < b->nbuf; i++)
            free(b->buf[i]);
    free(b->buf);
    for (i = 0; i < 3; i++)
        free(b->hmin[i]);
    free(b->acc);
    free(b);
}

void t31_ivs_base_move_frame_size(const T31IvsBaseMove *b, uint32_t *width,
                                  uint32_t *height)
{
    *width = (uint32_t)b->w;
    *height = (uint32_t)b->h;
}

int t31_ivs_base_move_datalen(const T31IvsBaseMove *b)
{
    return (b->w >> 3) * (b->h >> 3);
}

int t31_ivs_base_move_set_param(T31IvsBaseMove *b,
                                const IMP_IVS_BaseMoveParam *param)
{
    b->param.sense = param->sense;
    b->thr = base_sense_threshold(param->sense);
    base_build_lut(b);
    return 0;
}

void t31_ivs_base_move_get_param(const T31IvsBaseMove *b,
                                 IMP_IVS_BaseMoveParam *param)
{
    *param = b->param;
}

int t31_ivs_base_move_needs_luma(const T31IvsBaseMove *b)
{
    if (b->n < b->start)
        return 0;
    return (b->n % b->period == 0 && b->n >= b->det0) ||
           (b->n + b->refnum) % b->period == 0;
}

static void base_copy(T31IvsBaseMove *b, uint8_t *dst, const uint8_t *luma,
                      uint32_t stride)
{
    int y;

    if (!luma)
        return;
    if (stride == (uint32_t)b->w) {
        memcpy(dst, luma, (size_t)b->w * (size_t)b->h);
        return;
    }
    for (y = 0; y < b->h; y++)
        memcpy(dst + (size_t)y * b->w, luma + (size_t)y * stride, (size_t)b->w);
}

static void base_advance(T31IvsBaseMove *b)
{
    b->wr = (b->wr + 1) % b->nbuf;
    if (b->wr == b->ref)
        b->ref = (b->wr + 1) % b->nbuf;
}

void t31_ivs_base_move_feed(T31IvsBaseMove *b, const uint8_t *luma,
                            uint32_t stride)
{
    int capture;

    b->job_sad = 0;
    if (b->n < b->start) {
        b->n++;
        b->job_ret = b->flag;
        return;
    }
    b->flag = 0;
    capture = (b->n + b->refnum) % b->period == 0;
    if (b->n % b->period == 0 && b->n >= b->det0) {
        base_copy(b, b->buf[b->wr], luma, stride);
        if (!(b->w & 7) && !(b->h & 7)) {
            b->job_sad = 1;
            b->job_a = b->wr;
            b->job_b = b->ref;
            b->flag = 1;
        }
        if (capture)
            base_advance(b);
    } else if (capture) {
        base_copy(b, b->buf[b->wr], luma, stride);
        base_advance(b);
    }
    b->n++;
    b->job_ret = b->flag;
}

static inline unsigned umin(unsigned a, unsigned b)
{
    return a < b ? a : b;
}

/* Thresholded difference of one row, then its horizontal 3-minimum. */
static void base_hmin_row(const T31IvsBaseMove *b, const uint8_t *A,
                          const uint8_t *B, uint8_t *dst)
{
    const uint8_t *lut = b->lut + 255;
    const int w = b->w;
    unsigned prev, cur, next;
    int x;

    cur = lut[(int)A[0] - (int)B[0]];
    prev = cur;
    for (x = 0; x < w - 1; x++) {
        next = lut[(int)A[x + 1] - (int)B[x + 1]];
        dst[x] = (uint8_t)umin(umin(prev, cur), next);
        prev = cur;
        cur = next;
    }
    dst[w - 1] = (uint8_t)umin(prev, cur);
}

static void base_sad(T31IvsBaseMove *b, const uint8_t *A, const uint8_t *B,
                     uint8_t *out)
{
    const int w = b->w, h = b->h, bw = w >> 3;
    int y, bx;

    memset(b->acc, 0, sizeof(uint32_t) * (size_t)bw);
    base_hmin_row(b, A, B, b->hmin[0]);
    if (h > 1)
        base_hmin_row(b, A + w, B + w, b->hmin[1]);
    for (y = 0; y < h; y++) {
        const uint8_t *up = b->hmin[(y > 0 ? y - 1 : 0) % 3];
        const uint8_t *mid = b->hmin[y % 3];
        const uint8_t *dn;

        if (y + 1 < h && y + 1 >= 2)
            base_hmin_row(b, A + (size_t)(y + 1) * w, B + (size_t)(y + 1) * w,
                          b->hmin[(y + 1) % 3]);
        dn = b->hmin[(y + 1 < h ? y + 1 : h - 1) % 3];
        for (bx = 0; bx < bw; bx++) {
            const uint8_t *u = up + bx * 8, *m = mid + bx * 8, *d = dn + bx * 8;
            unsigned s;

            s = umin(umin(u[0], m[0]), d[0]);
            s += umin(umin(u[1], m[1]), d[1]);
            s += umin(umin(u[2], m[2]), d[2]);
            s += umin(umin(u[3], m[3]), d[3]);
            s += umin(umin(u[4], m[4]), d[4]);
            s += umin(umin(u[5], m[5]), d[5]);
            s += umin(umin(u[6], m[6]), d[6]);
            s += umin(umin(u[7], m[7]), d[7]);
            b->acc[bx] += s;
        }
        if ((y & 7) == 7) {
            uint8_t *o = out + (size_t)(y >> 3) * bw;

            for (bx = 0; bx < bw; bx++) {
                o[bx] = (uint8_t)b->acc[bx];  /* truncated, as the vendor */
                b->acc[bx] = 0;
            }
        }
    }
}

void t31_ivs_base_move_run(T31IvsBaseMove *b, uint8_t *data, int *ret)
{
    if (b->job_sad && data)
        base_sad(b, b->buf[b->job_a], b->buf[b->job_b], data);
    b->job_sad = 0;
    *ret = b->job_ret;
}
