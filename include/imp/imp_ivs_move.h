/**
 * IMP IVS Move Detection Module
 * 
 * Motion detection interface for IVS
 */

#ifndef __IMP_IVS_MOVE_H__
#define __IMP_IVS_MOVE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "imp_ivs.h"

#define IMP_IVS_MOVE_MAX_ROI_CNT 52

/* Motion detection parameter structure (T31 1.1.5.2 parity) */
typedef struct {
    int             sense[IMP_IVS_MOVE_MAX_ROI_CNT];
    int             skipFrameCnt;
    IMPFrameInfo    frameInfo;                     /* width/height only */
    IMPRect         roiRect[IMP_IVS_MOVE_MAX_ROI_CNT];
    int             roiRectCnt;
} IMP_IVS_MoveParam;

/* Motion detection output structure */
typedef struct {
    int retRoi[IMP_IVS_MOVE_MAX_ROI_CNT];
} IMP_IVS_MoveOutput;

/* Create/Destroy interface (vendor signatures) */
IMPIVSInterface *IMP_IVS_CreateMoveInterface(IMP_IVS_MoveParam *param);
void IMP_IVS_DestroyMoveInterface(IMPIVSInterface *moveInterface);

#ifdef __cplusplus
}
#endif

#endif /* __IMP_IVS_MOVE_H__ */

