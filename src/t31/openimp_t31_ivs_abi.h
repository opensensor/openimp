/* IVS types in the layout of the T31 1.1.6 vendor headers, which streamers
 * are compiled against. OpenIMP's generic include/imp/imp_ivs*.h use a
 * two-int IMPFrameInfo and an x/y/w/h IMPRect on T31 and must not be used
 * for the T31 ABI (IMP_IVS_MoveParam would be 0x420 instead of 0x450). */
#ifndef OPENIMP_T31_IVS_ABI_H
#define OPENIMP_T31_IVS_ABI_H

#include <stddef.h>
#include <stdint.h>

#define IMP_IVS_MOVE_MAX_ROI_CNT 52
#define IMP_IVS_DEFAULT_TIMEOUTMS (-1)

/* Vendor IMPPixelFormat value of NV12. */
#define T31_IVS_PIX_NV12 10

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

typedef struct {
    int ret;
    uint8_t *data;
    int datalen;
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

_Static_assert(sizeof(IMPIVSInterface) == 0x34 || sizeof(void *) != 4,
               "vendor IMPIVSInterface is 0x34 bytes");
_Static_assert(offsetof(IMPIVSInterface, preProcessSync) == 0x14 || sizeof(void *) != 4,
               "IMPIVSInterface.preProcessSync at 0x14");
_Static_assert(offsetof(IMPIVSInterface, flushFrame) == 0x2c || sizeof(void *) != 4,
               "IMPIVSInterface.flushFrame at 0x2c");
_Static_assert(offsetof(IMPIVSInterface, priv) == 0x30 || sizeof(void *) != 4,
               "IMPIVSInterface.priv at 0x30");

#endif
