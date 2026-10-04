/*
 * ivs_move_v2_test - host test of the opt-in motion v2 extension
 * (src/t31/openimp_ivs_move_v2.c, OpenIMP_IVS_Move*Ex) through the real IVS
 * framework (src/t31/openimp_t31_ivs.c).
 *
 *   1. off-identity: with v2 off (environment unset, SetConfigEx with
 *      features 0) and in shadow mode (all features but OVERRIDE), every
 *      IMP_IVS_MoveOutput of a long scene (noise, brightness jumps, moving
 *      boxes, live SetParam) is bit-identical to the bare vendor algorithm,
 *      and GetResultEx.legacy_roi mirrors retRoi;
 *   2. behaviour with OPENIMP_MOTION_V2=1 (environment): noise and a global
 *      brightness jump give no object (the jump is flagged as suppressed),
 *      a single flickering cell gives no object, a moving 48x96 box gives
 *      one object whose box covers it, in the right ROI only.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "imp/imp_common.h"
#include "imp/openimp_ivs_move_ex.h"
#include "openimp_t31_ivs.h"
#include "openimp_t31_ivs_abi.h"
#include "openimp_t31_ivs_move.h"

IMPIVSInterface *IMP_IVS_CreateMoveInterface(IMP_IVS_MoveParam *param);
void IMP_IVS_DestroyMoveInterface(IMPIVSInterface *moveInterface);
int IMP_IVS_CreateGroup(int group);
int IMP_IVS_DestroyGroup(int group);
int IMP_IVS_CreateChn(int channel, IMPIVSInterface *handler);
int IMP_IVS_DestroyChn(int channel);
int IMP_IVS_RegisterChn(int group, int channel);
int IMP_IVS_UnRegisterChn(int channel);
int IMP_IVS_StartRecvPic(int channel);
int IMP_IVS_StopRecvPic(int channel);
int IMP_IVS_PollingResult(int channel, int timeout_ms);
int IMP_IVS_GetResult(int channel, void **result);
int IMP_IVS_ReleaseResult(int channel, void *result);
int IMP_IVS_GetParam(int channel, void *param);
int IMP_IVS_SetParam(int channel, void *param);

#define FS_CHN 1
#define W 640
#define H 360
#define GRID 5

static int bound;
static int failures;

int IMP_System_GetBindbyDest(IMPCell *destination, IMPCell *source)
{
    if (!bound || destination->deviceID != DEV_ID_IVS ||
        destination->groupID != 0)
        return -1;
    source->deviceID = DEV_ID_FS;
    source->groupID = FS_CHN;
    source->outputID = 0;
    return 0;
}

int DMA_RmemFlushCache(void *virt_addr, uint32_t size, int dir)
{
    (void)virt_addr;
    (void)size;
    (void)dir;
    return 0;
}

#define CHECK(cond, ...) do {                                         \
        if (!(cond)) {                                                \
            failures++;                                               \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);      \
            fprintf(stderr, __VA_ARGS__);                             \
            fputc('\n', stderr);                                      \
        }                                                             \
    } while (0)

static void *map_low(size_t size)
{
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);

    if (p == MAP_FAILED || (uintptr_t)p + size > 0xffffffffu) {
        fprintf(stderr, "cannot map a frame buffer below 4 GiB\n");
        exit(2);
    }
    return p;
}

static void put32(uint8_t *rec, size_t off, uint32_t v)
{
    memcpy(rec + off, &v, sizeof(v));
}

static void put64(uint8_t *rec, size_t off, int64_t v)
{
    memcpy(rec + off, &v, sizeof(v));
}

/* the T23 1.3.0 capture record (see ivs_framework_test.c) */
static void make_record(uint8_t *rec, const uint8_t *frame, int index,
                        int64_t ts)
{
    uint32_t size = (uint32_t)W * ((H + 15u) & ~15u) * 3u / 2u;

    memset(rec, 0xa5, 0x428);
    put32(rec, 0x00, (uint32_t)index);
    put32(rec, 0x04, FS_CHN);
    put32(rec, 0x08, W);
    put32(rec, 0x0c, H);
    put32(rec, 0x10, 0x3231564eu);
    put32(rec, 0x14, size);
    put32(rec, 0x18, 0x02000000u + (uint32_t)index * size);
    put32(rec, 0x1c, (uint32_t)(uintptr_t)frame);
    put32(rec, 0x20, 0x02000000u + (uint32_t)index * size);
    put32(rec, 0x24, 0);
    put64(rec, 0x28, ts);
    put64(rec, 0x30, ts + 7);
}

static uint32_t rng = 1;

static int noise(int amp)
{
    rng = rng * 1103515245u + 12345u;
    return (int)((rng >> 16) % (unsigned)(2 * amp + 1)) - amp;
}

static uint8_t clamp8(int v)
{
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

/* textured background, gain in percent, noise amplitude, optional box */
static void scene(uint8_t *f, int gain, int amp, int bx, int by, int bw,
                  int bh, int bval)
{
    int x, y;

    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            int v = 50 + ((x * 7 + y * 3) % 61) + ((x / 40 + y / 30) & 1) * 30;

            if (bw && x >= bx && x < bx + bw && y >= by && y < by + bh)
                v = bval;
            f[y * W + x] = clamp8(v * gain / 100 + (amp ? noise(amp) : 0));
        }
}

static void grid_param(IMP_IVS_MoveParam *p, int skip, int sense)
{
    int r, c, n = 0;

    memset(p, 0, sizeof(*p));
    p->skipFrameCnt = skip;
    p->frameInfo.width = W;
    p->frameInfo.height = H;
    for (r = 0; r < GRID; r++)
        for (c = 0; c < GRID; c++, n++) {
            p->sense[n] = sense;
            p->roiRect[n].p0.x = c * W / GRID;
            p->roiRect[n].p0.y = r * H / GRID;
            p->roiRect[n].p1.x = (c + 1) * W / GRID - 1;
            p->roiRect[n].p1.y = (r + 1) * H / GRID - 1;
        }
    p->roiRectCnt = n;
}

struct chan {
    IMPIVSInterface *inf;
    uint8_t *frame;
    size_t fsize;
    int index;
};

static void chan_open(struct chan *ch, IMP_IVS_MoveParam *p)
{
    ch->fsize = (size_t)W * ((H + 15) & ~15) * 3 / 2;
    ch->frame = map_low(ch->fsize);
    memset(ch->frame, 128, ch->fsize);
    ch->index = 0;
    CHECK(IMP_IVS_CreateGroup(0) == 0, "CreateGroup");
    ch->inf = IMP_IVS_CreateMoveInterface(p);
    CHECK(ch->inf != NULL, "CreateMoveInterface");
    CHECK(IMP_IVS_CreateChn(0, ch->inf) == 0, "CreateChn");
    CHECK(IMP_IVS_RegisterChn(0, 0) == 0, "RegisterChn");
    bound = 1;
    CHECK(IMP_IVS_StartRecvPic(0) == 0, "StartRecvPic");
}

static void chan_close(struct chan *ch)
{
    CHECK(IMP_IVS_StopRecvPic(0) == 0, "StopRecvPic");
    bound = 0;
    CHECK(IMP_IVS_UnRegisterChn(0) == 0, "UnRegisterChn");
    CHECK(IMP_IVS_DestroyChn(0) == 0, "DestroyChn");
    IMP_IVS_DestroyMoveInterface(ch->inf);
    CHECK(IMP_IVS_DestroyGroup(0) == 0, "DestroyGroup");
    munmap(ch->frame, ch->fsize);
}

/* Push the current frame; returns 1 and the result when one was produced. */
static int chan_push(struct chan *ch, IMP_IVS_MoveOutput *out,
                     OpenIMP_IVS_MoveOutputEx *ex, int expect)
{
    uint8_t rec[0x428];
    IMP_IVS_MoveOutput *r = NULL;

    make_record(rec, ch->frame, ch->index % 4, 1000000 + ch->index * 40000);
    ch->index++;
    openimp_t31_ivs_capture(FS_CHN, rec);
    /* frames without a result: give the channel thread time to finish,
     * a busy channel would drop the next frame */
    if (IMP_IVS_PollingResult(0, expect ? 2000 : 60) != 0)
        return 0;
    /* the channel thread posts the result before it marks itself idle */
    usleep(3000);
    CHECK(IMP_IVS_GetResult(0, (void **)&r) == 0 && r, "GetResult");
    if (!r)
        return 0;
    *out = *r;
    memset(ex, 0, sizeof(*ex));
    ex->size = sizeof(*ex);
    CHECK(OpenIMP_IVS_MoveGetResultEx(0, ex) == 0, "GetResultEx");
    CHECK(IMP_IVS_ReleaseResult(0, r) == 0, "ReleaseResult");
    return 1;
}

/* One frame of the long identity scene. */
static void identity_frame(uint8_t *f, int t)
{
    int gain = 100, amp = 3;

    if (t >= 60 && t < 70)
        gain = 160;                         /* AE jump */
    if (t >= 120)
        amp = 12;                           /* night noise */
    if (t >= 30 && t < 55)
        scene(f, gain, amp, 40 + t * 9, 150, 48, 96, 230);
    else if (t >= 140 && t < 170)
        scene(f, gain, amp, 500 - (t - 140) * 12, 60, 30, 30, 20);
    else
        scene(f, gain, amp, 0, 0, 0, 0, 0);
}

/* mode: 0 env unset, 1 SetConfigEx(features 0), 2 shadow (no override) */
static void test_identity(int mode, int skip, int sense)
{
    IMP_IVS_MoveParam p;
    struct chan ch;
    T31IvsMove *ref;
    int t, i, results = 0, hits = 0, objs = 0;

    grid_param(&p, skip, sense);
    ref = t31_ivs_move_create(&p);
    chan_open(&ch, &p);
    if (mode) {
        OpenIMP_IVS_MoveConfigEx c, g;

        memset(&c, 0, sizeof(c));
        c.size = sizeof(c);
        c.version = OPENIMP_IVS_MOVE_EX_VERSION;
        c.features = mode == 2 ? (OPENIMP_MOVE_F_ALL & ~OPENIMP_MOVE_F_OVERRIDE)
                               : 0;
        c.learn_shift = 4;
        c.thresh_k = 64;
        c.min_delta = 6;
        c.suppress_ms = 0;
        c.jump_pct = 25;
        c.min_cells = 3;
        c.min_frames = 2;
        CHECK(OpenIMP_IVS_MoveSetConfigEx(0, &c) == 0, "SetConfigEx");
        memset(&g, 0, sizeof(g));
        g.size = sizeof(g);
        CHECK(OpenIMP_IVS_MoveGetConfigEx(0, &g) == 0 &&
              g.features == c.features, "GetConfigEx");
    }
    for (t = 0; t < 200; t++) {
        IMP_IVS_MoveOutput out;
        OpenIMP_IVS_MoveOutputEx ex;
        int expect[IMP_IVS_MOVE_MAX_ROI_CNT];
        int r, got;

        identity_frame(ch.frame, t);
        if (t == 100) {             /* live SetParam: one ROI less sensitive */
            IMP_IVS_MoveParam q;

            CHECK(IMP_IVS_GetParam(0, &q) == 0, "GetParam");
            q.sense[12] = 0;
            CHECK(IMP_IVS_SetParam(0, &q) == 0, "SetParam");
            CHECK(t31_ivs_move_set_param(ref, &q) == 0, "ref SetParam");
        }
        t31_ivs_move_feed(ref, t31_ivs_move_needs_luma(ref) ? ch.frame : NULL,
                          W);
        r = t31_ivs_move_run(ref, expect);
        got = chan_push(&ch, &out, &ex, r == 0);
        CHECK(got == (r == 0), "mode %d t=%d: result %d, reference %d",
              mode, t, got, r);
        if (!got || r != 0)
            continue;
        results++;
        for (i = 0; i < p.roiRectCnt; i++) {
            int bit = (ex.legacy_roi[i >> 5] >> (i & 31)) & 1;

            CHECK(out.retRoi[i] == expect[i],
                  "mode %d skip %d t=%d roi %d: framework %d, vendor algorithm %d",
                  mode, skip, t, i, out.retRoi[i], expect[i]);
            CHECK(bit == (expect[i] != 0), "mode %d t=%d roi %d legacy bit",
                  mode, t, i);
            hits += expect[i] != 0;
        }
        CHECK(ex.version == OPENIMP_IVS_MOVE_EX_VERSION && ex.frame_w == W &&
              ex.frame_h == H, "ex header");
        /* a configuration is applied after the result of the frame it
         * arrived with: the first result still has v2 off */
        CHECK(results == 1 || !!(ex.flags & OPENIMP_MOVE_EX_ACTIVE) == (mode == 2),
              "mode %d: ex flags 0x%x", mode, ex.flags);
        CHECK(!(ex.flags & OPENIMP_MOVE_EX_OVERRIDE), "override flagged");
        objs += (int)ex.obj_cnt;
    }
    CHECK(results > 20 && hits > 0, "mode %d: %d results, %d hits", mode,
          results, hits);
    chan_close(&ch);
    t31_ivs_move_destroy(ref);
    printf("identity mode %d skip %d sense %d: %d results, %d roi hits, "
           "%d v2 objects, all equal to the vendor algorithm\n",
           mode, skip, sense, results, hits, objs);
}

static void test_behaviour(void)
{
    IMP_IVS_MoveParam p;
    struct chan ch;
    OpenIMP_IVS_MoveConfigEx g;
    int t, i, legacy_noise = 0, v2_noise = 0, legacy_jump = 0, v2_jump = 0;
    int supp = 0, flicker = 0, box_frames = 0, box_hit = 0, box_ok = 1;
    int wrong_roi = 0, lamp_legacy = 0, lamp = 0;

    setenv("OPENIMP_MOTION_V2", "1", 1);
    setenv("OPENIMP_MOTION_V2_SUPPRESS_MS", "1", 1);    /* 3-frame minimum */
    grid_param(&p, 0, 2);       /* timps default sensitivity */
    chan_open(&ch, &p);
    setenv("OPENIMP_MOTION_V2", "0", 1);  /* v2 is on by default: force the vendor algorithm */
    unsetenv("OPENIMP_MOTION_V2_SUPPRESS_MS");
    memset(&g, 0, sizeof(g));
    g.size = sizeof(g);
    CHECK(OpenIMP_IVS_MoveGetConfigEx(0, &g) == 0 &&
          g.features == OPENIMP_MOVE_F_ALL && g.min_cells == 3,
          "environment config: features 0x%x", g.features);

    for (t = 0; t < 160; t++) {
        IMP_IVS_MoveOutput out;
        OpenIMP_IVS_MoveOutputEx ex;
        int legacy, any = 0;

        if (t >= 40 && t < 44)          /* global jump */
            scene(ch.frame, 150, 8, 0, 0, 0, 0, 0);
        else
            scene(ch.frame, 100, 8, 0, 0, 0, 0, 0);
        if (t >= 60 && t < 90 && (t & 1)) {     /* flicker of one cell */
            int x, y;

            for (y = 200; y < 208; y++)
                for (x = 300; x < 308; x++)
                    ch.frame[y * W + x] = 250;
        }
        if (t >= 100 && t < 130)        /* box walks right in grid row 2 */
            scene(ch.frame, 100, 8, 20 + (t - 100) * 12, 150, 48, 60, 235);
        if (t >= 140)                   /* a lamp switches on and stays */
            scene(ch.frame, 100, 8, 400, 40, 64, 40, 250);
        if (!chan_push(&ch, &out, &ex, 1))
            continue;
        legacy = (ex.legacy_roi[0] | ex.legacy_roi[1]) != 0;
        for (i = 0; i < p.roiRectCnt; i++)
            any |= out.retRoi[i] != 0;
        CHECK(t == 0 || (ex.flags & OPENIMP_MOVE_EX_OVERRIDE),
              "t=%d: not overridden", t);
        CHECK(any == (ex.obj_cnt != 0), "t=%d: retRoi %d but %u objects", t,
              any, ex.obj_cnt);
        if (t >= 10 && t < 40) {
            legacy_noise += legacy;
            v2_noise += any;
        } else if (t >= 40 && t < 50) {
            legacy_jump += legacy;
            v2_jump += any;
            supp += !!(ex.flags & OPENIMP_MOVE_EX_SUPPRESSED);
        } else if (t >= 60 && t < 90) {
            flicker += any;
        } else if (t >= 140) {
            lamp_legacy += legacy;
            lamp += any;
        } else if (t >= 102 && t < 130) {
            box_frames++;
            if (ex.obj_cnt) {
                int bx = 20 + (t - 100) * 12;
                const OpenIMP_IVS_MoveObject *o = &ex.obj[0];

                box_hit++;
                if (o->x1 < bx || o->x0 > bx + 48 || o->y1 < 150 ||
                    o->y0 > 210 || o->strength == 0 || o->id == 0)
                    box_ok = 0;
                for (i = 0; i < p.roiRectCnt; i++)
                    if (out.retRoi[i] && (i / GRID < 2 || i / GRID > 3))
                        wrong_roi++;
            }
        }
    }
    {   /* a caller that sets only size + features gets the defaults */
        OpenIMP_IVS_MoveConfigEx c, d;

        memset(&c, 0, sizeof(c));
        c.size = 12;
        c.features = OPENIMP_MOVE_F_BLOBS;
        CHECK(OpenIMP_IVS_MoveSetConfigEx(0, &c) == 0, "short SetConfigEx");
        memset(&d, 0, sizeof(d));
        d.size = sizeof(d);
        CHECK(OpenIMP_IVS_MoveGetConfigEx(0, &d) == 0 &&
              d.features == OPENIMP_MOVE_F_BLOBS && d.learn_shift == 4 &&
              d.thresh_k == 64 && d.min_delta == 10 && d.suppress_ms == 4000 &&
              d.jump_pct == 25 && d.min_cells == 3 && d.min_frames == 2 &&
              d.min_move == 3 &&
              d.version == OPENIMP_IVS_MOVE_EX_VERSION, "defaults");
        c.size = 8;
        CHECK(OpenIMP_IVS_MoveSetConfigEx(0, &c) < 0 && errno == EINVAL,
              "size 8 accepted");
        CHECK(OpenIMP_IVS_MoveGetResultEx(5, &(OpenIMP_IVS_MoveOutputEx){
              .size = sizeof(OpenIMP_IVS_MoveOutputEx) }) < 0 &&
              errno == ENOENT, "GetResultEx on an empty channel");
    }
    chan_close(&ch);
    printf("behaviour: noise legacy %d v2 %d | jump legacy %d v2 %d (suppressed "
           "%d) | flicker v2 %d | box %d/%d frames, roi outside %d | lamp legacy %d "
           "v2 %d\n",
           legacy_noise, v2_noise, legacy_jump, v2_jump, supp, flicker,
           box_hit, box_frames, wrong_roi, lamp_legacy, lamp);
    CHECK(v2_noise == 0, "v2 reported noise");
    CHECK(legacy_jump > 0, "the scene does not trigger the vendor algorithm");
    CHECK(v2_jump == 0 && supp > 0, "v2 reported the brightness jump");
    CHECK(flicker == 0, "v2 reported a one-cell flicker");
    CHECK(box_hit >= box_frames - 3 && box_ok, "box not tracked");
    CHECK(wrong_roi == 0, "box reported in a wrong ROI");
    CHECK(lamp_legacy > 0 && lamp == 0, "a lamp switching on (no movement) "
          "reported by v2");
}

int main(void)
{
    setenv("OPENIMP_MOTION_V2", "0", 1);  /* v2 is on by default: force the vendor algorithm */
    test_identity(0, 0, 4);
    test_identity(0, 5, 2);
    test_identity(1, 0, 4);
    test_identity(1, 2, 3);
    test_identity(2, 0, 4);
    test_identity(2, 5, 2);
    test_behaviour();
    if (failures) {
        fprintf(stderr, "IVS move v2: %d failures\n", failures);
        return 1;
    }
    printf("IVS move v2: all checks passed\n");
    return 0;
}
