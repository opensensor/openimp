/*
 * ivs_framework_test - the T23 IVS path end to end on the host.
 *
 * Builds src/t31/openimp_t31_ivs.c and openimp_t31_ivs_move.c with
 * PLATFORM_T23 and drives them the way timps does: CreateGroup,
 * CreateMoveInterface, CreateChn, RegisterChn, FS -> IVS bind,
 * StartRecvPic, then PollingResult / GetResult / ReleaseResult per frame.
 * Frames enter through openimp_t31_ivs_capture() as T23 capture records
 * (kernel_interface.c VBMFrame), filled here by byte offset from the T23
 * 1.3.0 IMPFrameInfo layout rather than through the ABI header, so a wrong
 * header layout fails the test instead of agreeing with itself.
 *
 * Checks:
 *   - every move result equals a direct run of the algorithm on the same
 *     frames (the framework hands the right luma to the right instance);
 *   - a static scene gives no motion, a moving box only in its own ROI;
 *   - Get/SetParam use the T23 IMP_IVS_MoveParam layout;
 *   - base move results carry the frame timestamp in the public T23
 *     IMP_IVS_BaseMoveOutput.timeStamp (offset 0x10 on the target).
 *
 * The frame buffers are mapped below 4 GiB because the vendor frame record
 * carries 32-bit virtual addresses.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "imp/imp_common.h"
#include "openimp_t31_ivs.h"
#include "openimp_t31_ivs_abi.h"
#include "openimp_t31_ivs_move.h"

IMPIVSInterface *IMP_IVS_CreateMoveInterface(IMP_IVS_MoveParam *param);
void IMP_IVS_DestroyMoveInterface(IMPIVSInterface *moveInterface);
IMPIVSInterface *IMP_IVS_CreateBaseMoveInterface(IMP_IVS_BaseMoveParam *param);
void IMP_IVS_DestroyBaseMoveInterface(IMPIVSInterface *moveInterface);
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
#define W 320
#define H 240
#define BW 64
#define BH 48

/* ---- stubs for what libimp provides around the IVS files ---- */

static int bound;

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

/* ---- helpers ---- */

static int failures;

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

/* T23 1.3.0 IMPFrameInfo at the start of the 0x428-byte capture record. */
static void make_record(uint8_t *rec, const uint8_t *frame, int w, int h,
                        int index, int64_t ts)
{
    uint32_t size = (uint32_t)w * (((uint32_t)h + 15u) & ~15u) * 3u / 2u;

    memset(rec, 0xa5, 0x428);
    put32(rec, 0x00, (uint32_t)index);
    put32(rec, 0x04, FS_CHN);
    put32(rec, 0x08, (uint32_t)w);
    put32(rec, 0x0c, (uint32_t)h);
    put32(rec, 0x10, 0x3231564eu);          /* V4L2 NV12, as captured */
    put32(rec, 0x14, size);
    put32(rec, 0x18, 0x02000000u + (uint32_t)index * size);
    put32(rec, 0x1c, (uint32_t)(uintptr_t)frame);
    put32(rec, 0x20, 0x02000000u + (uint32_t)index * size);
    put32(rec, 0x24, 0);
    put64(rec, 0x28, ts);
    put64(rec, 0x30, ts + 7);
}

/* Flat background, a bright 60x60 box in the bottom-right quadrant while
 * 10 <= t < 20, moving 6 px per frame. */
static void scene(uint8_t *f, int w, int h, int t)
{
    int x, y;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            f[y * w + x] = (uint8_t)(60 + ((x + y) & 7));
    if (t >= 10 && t < 20) {
        int bx = w / 2 + 10 + (t - 10) * 6, by = h / 2 + 20;

        for (y = by; y < by + 60 && y < h; y++)
            for (x = bx; x < bx + 60 && x < w; x++)
                f[y * w + x] = 200;
    }
}

static void test_move(void)
{
    const size_t fsize = (size_t)W * ((H + 15) & ~15) * 3 / 2;
    uint8_t *frame = map_low(fsize);
    uint8_t rec[0x428];
    IMP_IVS_MoveParam p, q;
    IMPIVSInterface *inf;
    T31IvsMove *ref;
    int i, t, ones[4] = { 0, 0, 0, 0 };

    memset(frame, 128, fsize);
    memset(&p, 0, sizeof(p));
    p.skipFrameCnt = 0;
    p.frameInfo.width = W;
    p.frameInfo.height = H;
    for (i = 0; i < 4; i++) {
        p.sense[i] = 4;
        p.roiRect[i].p0.x = (i % 2) * W / 2;
        p.roiRect[i].p0.y = (i / 2) * H / 2;
        p.roiRect[i].p1.x = (i % 2 + 1) * W / 2 - 1;
        p.roiRect[i].p1.y = (i / 2 + 1) * H / 2 - 1;
    }
    p.roiRectCnt = 4;
    /* the layout the vendor library reads, by offset */
    CHECK(*(int *)((uint8_t *)&p + 0x450) == 4, "roiRectCnt not at 0x450");
    CHECK(*(int *)((uint8_t *)&p + 0x110 + 3 * 16 + 8) == W - 1,
          "roiRect[3].p1.x not at 0x138");

    ref = t31_ivs_move_create(&p);
    CHECK(ref != NULL, "reference instance");
    CHECK(IMP_IVS_CreateGroup(0) == 0, "CreateGroup");
    inf = IMP_IVS_CreateMoveInterface(&p);
    CHECK(inf && inf->paramSize == (int)sizeof(IMP_IVS_MoveParam) &&
          inf->paramSize == 0x458, "move interface paramSize %d",
          inf ? inf->paramSize : -1);
    CHECK(inf && inf->pixfmt == 10, "move interface pixfmt NV12 (10)");
    CHECK(IMP_IVS_CreateChn(0, inf) == 0, "CreateChn");
    CHECK(IMP_IVS_RegisterChn(0, 0) == 0, "RegisterChn");
    bound = 1;
    CHECK(IMP_IVS_StartRecvPic(0) == 0, "StartRecvPic");

    for (t = 0; t < 30; t++) {
        IMP_IVS_MoveOutput *out = NULL;
        int expect[IMP_IVS_MOVE_MAX_ROI_CNT];
        int r;

        scene(frame, W, H, t);
        make_record(rec, frame, W, H, t % 4, 1000000 + t * 40000);
        openimp_t31_ivs_capture(FS_CHN, rec);
        /* not bound to this FS channel: must be ignored */
        openimp_t31_ivs_capture(FS_CHN + 1, rec);
        CHECK(IMP_IVS_PollingResult(0, 2000) == 0, "PollingResult t=%d", t);
        CHECK(IMP_IVS_GetResult(0, (void **)&out) == 0 && out,
              "GetResult t=%d", t);
        t31_ivs_move_feed(ref, t31_ivs_move_needs_luma(ref) ? frame : NULL, W);
        r = t31_ivs_move_run(ref, expect);
        CHECK(r == 0, "reference result t=%d", t);
        if (!out)
            continue;
        for (i = 0; i < 4; i++) {
            CHECK(out->retRoi[i] == expect[i],
                  "t=%d roi %d: framework %d, algorithm %d", t, i,
                  out->retRoi[i], expect[i]);
            ones[i] += out->retRoi[i] != 0;
            if (t < 10 || t >= 23)  /* static, reference 3 frames back too */
                CHECK(out->retRoi[i] == 0, "t=%d roi %d: motion in a static "
                      "scene", t, i);
        }
        CHECK(IMP_IVS_ReleaseResult(0, out) == 0, "ReleaseResult");
        if (t == 15) {
            memset(&q, 0, sizeof(q));
            CHECK(IMP_IVS_GetParam(0, &q) == 0, "GetParam");
            CHECK(q.roiRectCnt == 4 && q.roiRect[3].p1.y == H - 1 &&
                  q.frameInfo.width == W, "GetParam layout");
            q.sense[0] = 0;          /* applied with the next frame */
            CHECK(IMP_IVS_SetParam(0, &q) == 0, "SetParam");
        }
    }
    CHECK(ones[3] > 0, "moving box not detected in its ROI");
    CHECK(ones[0] == 0 && ones[1] == 0 && ones[2] == 0,
          "motion reported outside the box's ROI (%d %d %d)",
          ones[0], ones[1], ones[2]);
    CHECK(IMP_IVS_GetParam(0, &q) == 0 && q.sense[0] == 0 && q.sense[1] == 4,
          "SetParam not applied");

    /* no new frame: polling times out */
    CHECK(IMP_IVS_PollingResult(0, 50) < 0 && errno == ETIMEDOUT,
          "PollingResult without a frame");
    CHECK(IMP_IVS_StopRecvPic(0) == 0, "StopRecvPic");
    bound = 0;
    CHECK(IMP_IVS_UnRegisterChn(0) == 0, "UnRegisterChn");
    CHECK(IMP_IVS_DestroyChn(0) == 0, "DestroyChn");
    IMP_IVS_DestroyMoveInterface(inf);
    CHECK(IMP_IVS_DestroyGroup(0) == 0, "DestroyGroup");
    t31_ivs_move_destroy(ref);
    munmap(frame, fsize);
    printf("move: roi hits %d %d %d %d\n", ones[0], ones[1], ones[2], ones[3]);
}

static void test_base_move(void)
{
    const size_t fsize = (size_t)BW * ((BH + 15) & ~15) * 3 / 2;
    uint8_t *frame = map_low(fsize);
    uint8_t rec[0x428];
    IMP_IVS_BaseMoveParam p;
    IMPIVSInterface *inf;
    int t, detections = 0;

    memset(frame, 128, fsize);
    memset(&p, 0, sizeof(p));
    p.skipFrameCnt = 0;
    p.referenceNum = 1;
    p.sense = 3;
    p.frameInfo.width = BW;
    p.frameInfo.height = BH;
    CHECK(IMP_IVS_CreateGroup(0) == 0, "CreateGroup");
    inf = IMP_IVS_CreateBaseMoveInterface(&p);
    CHECK(inf && inf->paramSize == 0x48, "base move paramSize");
    CHECK(IMP_IVS_CreateChn(1, inf) == 0, "CreateChn");
    CHECK(IMP_IVS_RegisterChn(0, 1) == 0, "RegisterChn");
    bound = 1;
    CHECK(IMP_IVS_StartRecvPic(1) == 0, "StartRecvPic");
    for (t = 0; t < 12; t++) {
        IMP_IVS_BaseMoveOutput *out = NULL;
        int64_t ts = 5000000 + t * 33333;
        int64_t got;

        memset(frame, (t & 1) ? 67 : 100, (size_t)BW * BH);
        make_record(rec, frame, BW, BH, t % 4, ts);
        openimp_t31_ivs_capture(FS_CHN, rec);
        CHECK(IMP_IVS_PollingResult(1, 2000) == 0, "base PollingResult t=%d", t);
        CHECK(IMP_IVS_GetResult(1, (void **)&out) == 0 && out,
              "base GetResult t=%d", t);
        if (!out)
            continue;
        /* 0x10 on the 32-bit target (the ABI header asserts it there);
         * the host has a 64-bit data pointer in front of it */
        memcpy(&got, (uint8_t *)out + (sizeof(void *) == 4 ? 0x10 : 0x18),
               sizeof(got));
        CHECK(got == ts && out->timeStamp == ts,
              "base move timeStamp %lld, frame %lld", (long long)got,
              (long long)ts);
        CHECK(out->datalen == (BW / 8) * (BH / 8), "base move datalen");
        if (out->ret) {
            detections++;
            /* full-frame step of 33: 64 * 33 per block, truncated to 8 bit */
            CHECK(out->data && out->data[0] == (uint8_t)(64 * 33),
                  "base move block sum %d", out->data ? out->data[0] : -1);
        }
        IMP_IVS_ReleaseResult(1, out);
    }
    CHECK(detections >= 10, "base move detections %d", detections);
    CHECK(IMP_IVS_StopRecvPic(1) == 0, "StopRecvPic");
    bound = 0;
    CHECK(IMP_IVS_UnRegisterChn(1) == 0, "UnRegisterChn");
    CHECK(IMP_IVS_DestroyChn(1) == 0, "DestroyChn");
    IMP_IVS_DestroyBaseMoveInterface(inf);
    CHECK(IMP_IVS_DestroyGroup(0) == 0, "DestroyGroup");
    munmap(frame, fsize);
    printf("base move: %d detections\n", detections);
}

int main(void)
{
    test_move();
    test_base_move();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("T23 IVS framework: all checks passed\n");
    return 0;
}
