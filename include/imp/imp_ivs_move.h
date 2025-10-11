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

/**
 * Motion detection parameters
 */
typedef struct {
    int sense;                          /**< Sensitivity */
    int skipFrameCnt;                   /**< Skip frame count */
    int frameInfo;                      /**< Frame info */
    int roiRectCnt;                     /**< ROI rectangle count */
    IMPRect *roiRect;                   /**< ROI rectangles */
} IMP_IVS_MoveParam;

/**
 * Create motion detection interface
 * 
 * @param interface Pointer to interface pointer
 * @param param Motion detection parameters
 * @return 0 on success, negative on error
 */
int IMP_IVS_CreateMoveInterface(IMPIVSInterface **interface, IMP_IVS_MoveParam *param);

/**
 * Destroy motion detection interface
 * 
 * @param interface Interface pointer
 * @return 0 on success, negative on error
 */
int IMP_IVS_DestroyMoveInterface(IMPIVSInterface *interface);

#ifdef __cplusplus
}
#endif

#endif /* __IMP_IVS_MOVE_H__ */

