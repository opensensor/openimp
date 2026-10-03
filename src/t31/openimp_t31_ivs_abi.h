/* IVS types in the layout of the vendor headers streamers are compiled
 * against: T31 1.1.6, T23 1.3.0 with PLATFORM_T23, or the legacy T20 3.12.0
 * / T21 1.0.33 / T30 1.0.5 SDKs with PLATFORM_T21 or PLATFORM_T30. OpenIMP's generic
 * include/imp/imp_ivs*.h use a two-int IMPFrameInfo and an x/y/w/h IMPRect
 * and must not be used for these ABIs (IMP_IVS_MoveParam would be 0x420
 * instead of 0x450 on T31 and 0x458 on T23).
 *
 * T23 differs from T31 only in IMPFrameInfo: direct_phyAddr follows
 * virAddr, the 64-bit timestamps move to 0x28 and a second (IVDC dequeue)
 * timestamp replaces rotate_osdflag, which makes the record 0x38 bytes and
 * shifts everything after a frameInfo member (move: roiRect, roiRectCnt;
 * base move output: timeStamp is public on T23).
 *
 * T20/T21/T30 have the T31 IMPFrameInfo without rotate_osdflag (0x28
 * bytes), which moves roiRect/roiRectCnt 8 bytes down (OEM T20 libimp
 * 3.12.0: CreateMoveInterface allocates 0x34 + 0x448, imp_set_move_param
 * reads roiRect at 0x100 and roiRectCnt at 0x440; CreateBaseMoveInterface
 * allocates 0x34 + 0x38). T21 and T30 publish the base move timestamp as
 * T23 does, T20 does not. */
#ifndef OPENIMP_T31_IVS_ABI_H
#define OPENIMP_T31_IVS_ABI_H

#include <stddef.h>
#include <stdint.h>

#define IMP_IVS_MOVE_MAX_ROI_CNT 52
#define IMP_IVS_DEFAULT_TIMEOUTMS (-1)

/* Vendor IMPPixelFormat value of NV12. */
#define T31_IVS_PIX_NV12 10

#if defined(PLATFORM_T23)
/* Vendor IMPFrameInfo (imp_common.h, T23 1.3.0). */
typedef struct {
    int index;
    int pool_idx;
    uint32_t width;
    uint32_t height;
    uint32_t pixfmt;
    uint32_t size;
    uint32_t phyAddr;
    uint32_t virAddr;
    uint32_t direct_phyAddr;
    int64_t timeStamp;
    int64_t timeStamp_ivdc;
} T31IVSFrameInfo;

/* Bytes of a capture frame record (kernel_interface.c VBMFrame) that carry
 * the public IMPFrameInfo. */
#define T31_IVS_FRAME_RECORD_BYTES sizeof(T31IVSFrameInfo)
#elif defined(PLATFORM_T41)
/* Vendor IMPFrameInfo (imp_common.h, T41 1.2.0): direct_phyAddr and the
 * frame's pool follow virAddr, the timestamp is at 0x28. */
typedef struct {
    int index;
    int pool_idx;
    uint32_t width;
    uint32_t height;
    uint32_t pixfmt;
    uint32_t size;
    uint32_t phyAddr;
    uint32_t virAddr;
    uint32_t direct_phyAddr;
    uint32_t pool;                  /* void * in the vendor header */
    int64_t timeStamp;
} T31IVSFrameInfo;

/* openimp_p1.c's frame record is the whole public IMPFrameInfo. */
#define T31_IVS_FRAME_RECORD_BYTES sizeof(T31IVSFrameInfo)
#elif defined(PLATFORM_T21) || defined(PLATFORM_T30)
/* Vendor IMPFrameInfo (imp_common.h, T20 3.12.0, T21 1.0.33, T30 1.0.5). */
typedef struct {
    int index;
    int pool_idx;
    uint32_t width;
    uint32_t height;
    uint32_t pixfmt;
    uint32_t size;
    uint32_t phyAddr;
    uint32_t virAddr;
    int64_t timeStamp;
} T31IVSFrameInfo;

/* The capture frame record starts with the whole public IMPFrameInfo (the
 * timestamp is stored at VBMFrame +0x20). */
#define T31_IVS_FRAME_RECORD_BYTES sizeof(T31IVSFrameInfo)
#else
/* Vendor IMPFrameInfo (imp_common.h, T31 1.1.6). */
typedef struct {
    int index;
    int pool_idx;
    uint32_t width;
    uint32_t height;
    uint32_t pixfmt;
    uint32_t size;
    uint32_t phyAddr;
    uint32_t virAddr;
    int64_t timeStamp;
    int rotate_osdflag;
} T31IVSFrameInfo;

#define T31_IVS_FRAME_RECORD_BYTES offsetof(T31IVSFrameInfo, rotate_osdflag)
#endif

typedef struct {
    int x;
    int y;
} T31IVSPoint;

/* Inclusive corners, unlike OpenIMP's IMPRect {x, y, width, height}. */
typedef struct {
    T31IVSPoint p0;
    T31IVSPoint p1;
} T31IVSRect;

typedef struct {
    int sense[IMP_IVS_MOVE_MAX_ROI_CNT];
    int skipFrameCnt;
    T31IVSFrameInfo frameInfo;                  /* width/height only */
    T31IVSRect roiRect[IMP_IVS_MOVE_MAX_ROI_CNT];
    int roiRectCnt;
} IMP_IVS_MoveParam;

typedef struct {
    int retRoi[IMP_IVS_MOVE_MAX_ROI_CNT];
} IMP_IVS_MoveOutput;

typedef struct {
    int skipFrameCnt;
    int referenceNum;
    int sadMode;
    int sense;
    T31IVSFrameInfo frameInfo;
} IMP_IVS_BaseMoveParam;

/* IMP_IVS_BaseMoveOutput carries the frame timestamp (T23, T21, T30). */
#if defined(PLATFORM_T23) || defined(PLATFORM_T41) || \
    ((defined(PLATFORM_T21) || defined(PLATFORM_T30)) && !defined(PLATFORM_T20))
#define OPENIMP_IVS_BASE_MOVE_TIMESTAMP 1
#else
#define OPENIMP_IVS_BASE_MOVE_TIMESTAMP 0
#endif

typedef struct {
    int ret;
    uint8_t *data;
    int datalen;
#if OPENIMP_IVS_BASE_MOVE_TIMESTAMP
    int64_t timeStamp;
#endif
} IMP_IVS_BaseMoveOutput;

typedef struct IMPIVSInterface IMPIVSInterface;

struct IMPIVSInterface {
    void *param;
    int paramSize;
    int pixfmt;                                 /* vendor IMPPixelFormat */
    int (*init)(IMPIVSInterface *inf);
    void (*exit)(IMPIVSInterface *inf);
    int (*preProcessSync)(IMPIVSInterface *inf, T31IVSFrameInfo *frame);
    int (*processAsync)(IMPIVSInterface *inf, T31IVSFrameInfo *frame);
    int (*getResult)(IMPIVSInterface *inf, void **result);
    int (*releaseResult)(IMPIVSInterface *inf, void *result);
    int (*getParam)(IMPIVSInterface *inf, void *param);
    int (*setParam)(IMPIVSInterface *inf, void *param);
    int (*flushFrame)(IMPIVSInterface *inf);
    void *priv;
};

#if defined(PLATFORM_T23)
_Static_assert(sizeof(T31IVSFrameInfo) == 0x38, "vendor IMPFrameInfo is 0x38 bytes");
_Static_assert(offsetof(T31IVSFrameInfo, virAddr) == 0x1c, "IMPFrameInfo.virAddr at 0x1c");
_Static_assert(offsetof(T31IVSFrameInfo, direct_phyAddr) == 0x20, "IMPFrameInfo.direct_phyAddr at 0x20");
_Static_assert(offsetof(T31IVSFrameInfo, timeStamp) == 0x28, "IMPFrameInfo.timeStamp at 0x28");
_Static_assert(offsetof(T31IVSFrameInfo, timeStamp_ivdc) == 0x30, "IMPFrameInfo.timeStamp_ivdc at 0x30");
_Static_assert(sizeof(T31IVSRect) == 0x10, "vendor IMPRect is 0x10 bytes");

/* T23 1.3.0 libimp: IMP_IVS_CreateMoveInterface allocates 0x34 + 0x458,
 * imp_set_move_param reads roiRect at 0x110 and roiRectCnt at 0x450. */
_Static_assert(sizeof(IMP_IVS_MoveParam) == 0x458, "vendor IMP_IVS_MoveParam is 0x458 bytes");
_Static_assert(offsetof(IMP_IVS_MoveParam, skipFrameCnt) == 0xd0, "MoveParam.skipFrameCnt at 0xd0");
_Static_assert(offsetof(IMP_IVS_MoveParam, frameInfo) == 0xd8, "MoveParam.frameInfo at 0xd8");
_Static_assert(offsetof(IMP_IVS_MoveParam, roiRect) == 0x110, "MoveParam.roiRect at 0x110");
_Static_assert(offsetof(IMP_IVS_MoveParam, roiRectCnt) == 0x450, "MoveParam.roiRectCnt at 0x450");
_Static_assert(sizeof(IMP_IVS_MoveOutput) == 0xd0, "vendor IMP_IVS_MoveOutput is 0xd0 bytes");

/* IMP_IVS_CreateBaseMoveInterface allocates 0x34 + 0x48. */
_Static_assert(sizeof(IMP_IVS_BaseMoveParam) == 0x48, "vendor IMP_IVS_BaseMoveParam is 0x48 bytes");
_Static_assert(offsetof(IMP_IVS_BaseMoveParam, sense) == 0x0c, "BaseMoveParam.sense at 0x0c");
_Static_assert(offsetof(IMP_IVS_BaseMoveParam, frameInfo) == 0x10, "BaseMoveParam.frameInfo at 0x10");
_Static_assert(sizeof(IMP_IVS_BaseMoveOutput) == 0x18 || sizeof(void *) != 4,
               "vendor IMP_IVS_BaseMoveOutput is 0x18 bytes");
_Static_assert(offsetof(IMP_IVS_BaseMoveOutput, timeStamp) == 0x10 || sizeof(void *) != 4,
               "BaseMoveOutput.timeStamp at 0x10");
#elif defined(PLATFORM_T41)
_Static_assert(sizeof(T31IVSFrameInfo) == 0x30, "vendor IMPFrameInfo is 0x30 bytes");
_Static_assert(offsetof(T31IVSFrameInfo, direct_phyAddr) == 0x20, "IMPFrameInfo.direct_phyAddr at 0x20");
_Static_assert(offsetof(T31IVSFrameInfo, timeStamp) == 0x28, "IMPFrameInfo.timeStamp at 0x28");
_Static_assert(sizeof(T31IVSRect) == 0x10, "vendor IMPRect is 0x10 bytes");
_Static_assert(offsetof(IMP_IVS_MoveParam, frameInfo) == 0xd8, "MoveParam.frameInfo at 0xd8");
_Static_assert(offsetof(IMP_IVS_MoveParam, roiRect) == 0x108, "MoveParam.roiRect at 0x108");
_Static_assert(offsetof(IMP_IVS_MoveParam, roiRectCnt) == 0x448, "MoveParam.roiRectCnt at 0x448");
_Static_assert(sizeof(IMP_IVS_MoveParam) == 0x450, "vendor IMP_IVS_MoveParam is 0x450 bytes");
_Static_assert(sizeof(IMP_IVS_MoveOutput) == 0xd0, "vendor IMP_IVS_MoveOutput is 0xd0 bytes");
_Static_assert(sizeof(IMP_IVS_BaseMoveParam) == 0x40, "vendor IMP_IVS_BaseMoveParam is 0x40 bytes");
_Static_assert(sizeof(IMP_IVS_BaseMoveOutput) == 0x18 || sizeof(void *) != 4,
               "vendor IMP_IVS_BaseMoveOutput is 0x18 bytes");
#elif defined(PLATFORM_T21) || defined(PLATFORM_T30)
_Static_assert(sizeof(T31IVSFrameInfo) == 0x28, "vendor IMPFrameInfo is 0x28 bytes");
_Static_assert(offsetof(T31IVSFrameInfo, virAddr) == 0x1c, "IMPFrameInfo.virAddr at 0x1c");
_Static_assert(offsetof(T31IVSFrameInfo, timeStamp) == 0x20, "IMPFrameInfo.timeStamp at 0x20");
_Static_assert(sizeof(T31IVSRect) == 0x10, "vendor IMPRect is 0x10 bytes");

_Static_assert(sizeof(IMP_IVS_MoveParam) == 0x448, "vendor IMP_IVS_MoveParam is 0x448 bytes");
_Static_assert(offsetof(IMP_IVS_MoveParam, skipFrameCnt) == 0xd0, "MoveParam.skipFrameCnt at 0xd0");
_Static_assert(offsetof(IMP_IVS_MoveParam, frameInfo) == 0xd8, "MoveParam.frameInfo at 0xd8");
_Static_assert(offsetof(IMP_IVS_MoveParam, roiRect) == 0x100, "MoveParam.roiRect at 0x100");
_Static_assert(offsetof(IMP_IVS_MoveParam, roiRectCnt) == 0x440, "MoveParam.roiRectCnt at 0x440");
_Static_assert(sizeof(IMP_IVS_MoveOutput) == 0xd0, "vendor IMP_IVS_MoveOutput is 0xd0 bytes");

_Static_assert(sizeof(IMP_IVS_BaseMoveParam) == 0x38, "vendor IMP_IVS_BaseMoveParam is 0x38 bytes");
_Static_assert(offsetof(IMP_IVS_BaseMoveParam, sense) == 0x0c, "BaseMoveParam.sense at 0x0c");
_Static_assert(offsetof(IMP_IVS_BaseMoveParam, frameInfo) == 0x10, "BaseMoveParam.frameInfo at 0x10");
#if OPENIMP_IVS_BASE_MOVE_TIMESTAMP
_Static_assert(sizeof(IMP_IVS_BaseMoveOutput) == 0x18 || sizeof(void *) != 4,
               "vendor IMP_IVS_BaseMoveOutput is 0x18 bytes");
_Static_assert(offsetof(IMP_IVS_BaseMoveOutput, timeStamp) == 0x10 || sizeof(void *) != 4,
               "BaseMoveOutput.timeStamp at 0x10");
#else
_Static_assert(sizeof(IMP_IVS_BaseMoveOutput) == 0x0c || sizeof(void *) != 4,
               "vendor IMP_IVS_BaseMoveOutput is 0x0c bytes");
#endif
#else
_Static_assert(sizeof(T31IVSFrameInfo) == 0x30, "vendor IMPFrameInfo is 0x30 bytes");
_Static_assert(offsetof(T31IVSFrameInfo, virAddr) == 0x1c, "IMPFrameInfo.virAddr at 0x1c");
_Static_assert(offsetof(T31IVSFrameInfo, timeStamp) == 0x20, "IMPFrameInfo.timeStamp at 0x20");
_Static_assert(offsetof(T31IVSFrameInfo, rotate_osdflag) == 0x28, "IMPFrameInfo.rotate_osdflag at 0x28");
_Static_assert(sizeof(T31IVSRect) == 0x10, "vendor IMPRect is 0x10 bytes");

_Static_assert(sizeof(IMP_IVS_MoveParam) == 0x450, "vendor IMP_IVS_MoveParam is 0x450 bytes");
_Static_assert(offsetof(IMP_IVS_MoveParam, skipFrameCnt) == 0xd0, "MoveParam.skipFrameCnt at 0xd0");
_Static_assert(offsetof(IMP_IVS_MoveParam, frameInfo) == 0xd8, "MoveParam.frameInfo at 0xd8");
_Static_assert(offsetof(IMP_IVS_MoveParam, roiRect) == 0x108, "MoveParam.roiRect at 0x108");
_Static_assert(offsetof(IMP_IVS_MoveParam, roiRectCnt) == 0x448, "MoveParam.roiRectCnt at 0x448");
_Static_assert(sizeof(IMP_IVS_MoveOutput) == 0xd0, "vendor IMP_IVS_MoveOutput is 0xd0 bytes");

_Static_assert(sizeof(IMP_IVS_BaseMoveParam) == 0x40, "vendor IMP_IVS_BaseMoveParam is 0x40 bytes");
_Static_assert(offsetof(IMP_IVS_BaseMoveParam, sense) == 0x0c, "BaseMoveParam.sense at 0x0c");
_Static_assert(offsetof(IMP_IVS_BaseMoveParam, frameInfo) == 0x10, "BaseMoveParam.frameInfo at 0x10");
_Static_assert(sizeof(IMP_IVS_BaseMoveOutput) == 0x0c || sizeof(void *) != 4,
               "vendor IMP_IVS_BaseMoveOutput is 0x0c bytes");
#endif

_Static_assert(sizeof(IMPIVSInterface) == 0x34 || sizeof(void *) != 4,
               "vendor IMPIVSInterface is 0x34 bytes");
_Static_assert(offsetof(IMPIVSInterface, preProcessSync) == 0x14 || sizeof(void *) != 4,
               "IMPIVSInterface.preProcessSync at 0x14");
_Static_assert(offsetof(IMPIVSInterface, flushFrame) == 0x2c || sizeof(void *) != 4,
               "IMPIVSInterface.flushFrame at 0x2c");
_Static_assert(offsetof(IMPIVSInterface, priv) == 0x30 || sizeof(void *) != 4,
               "IMPIVSInterface.priv at 0x30");

#endif
