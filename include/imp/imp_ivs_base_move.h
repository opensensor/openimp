/**
 * IMP IVS base motion detection module
 */

#ifndef __IMP_IVS_BASE_MOVE_H__
#define __IMP_IVS_BASE_MOVE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "imp_ivs.h"

#define IMP_IVS_MOVE_MAX_ROI_CNT 52

typedef struct {
    int          skipFrameCnt;
    int          referenceNum;
    int          sadMode;
    int          sense;
    IMPFrameInfo frameInfo;
} IMP_IVS_BaseMoveParam;

typedef struct {
    int      ret;
    uint8_t *data;
    int      datalen;
} IMP_IVS_BaseMoveOutput;

IMPIVSInterface *IMP_IVS_CreateBaseMoveInterface(IMP_IVS_BaseMoveParam *param);
void IMP_IVS_DestroyBaseMoveInterface(IMPIVSInterface *moveInterface);

#ifdef __cplusplus
}
#endif

#endif /* __IMP_IVS_BASE_MOVE_H__ */