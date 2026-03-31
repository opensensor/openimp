/**
 * IMP FrameSource Module
 * 
 * Video frame source channel management
 */

#ifndef __IMP_FRAMESOURCE_H__
#define __IMP_FRAMESOURCE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "imp_common.h"

/**
 * Frame source channel type
 */
typedef enum {
    FS_PHY_CHANNEL = 0,     /**< Physical channel */
    FS_EXT_CHANNEL = 1      /**< Extended channel */
} IMPFSChnType;

/**
 * Crop configuration
 */
typedef struct {
    int enable;             /**< Enable crop */
    int top;                /**< Top offset */
    int left;               /**< Left offset */
    int width;              /**< Crop width */
    int height;             /**< Crop height */
} IMPFSChnCrop;

/**
 * Scaler configuration
 */
typedef struct {
    int enable;             /**< Enable scaler */
    int outwidth;           /**< Output width */
    int outheight;          /**< Output height */
} IMPFSChnScaler;

/**
 * Frame source channel attributes
 */
typedef struct {
    int picWidth;           /**< Picture width */
    int picHeight;          /**< Picture height */
    IMPPixelFormat pixFmt;  /**< Pixel format */
    IMPFSChnCrop crop;      /**< Crop configuration */
    IMPFSChnScaler scaler;  /**< Scaler configuration */
    int outFrmRateNum;      /**< Output frame rate numerator */
    int outFrmRateDen;      /**< Output frame rate denominator */
    int nrVBs;              /**< Number of video buffers */
    IMPFSChnType type;      /**< Channel type */
#if defined(PLATFORM_T31) || defined(PLATFORM_C100) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
    IMPFSChnCrop fcrop;     /**< Frame crop (newer platforms) */
#endif
} IMPFSChnAttr;

/**
 * FIFO attributes
 */
typedef struct {
    int maxdepth;           /**< Maximum FIFO depth */
    int depth;              /**< Current depth */
} IMPFSChnFifoAttr;

/**
 * Create frame source channel
 * 
 * @param chnNum Channel number
 * @param chn_attr Channel attributes
 * @return 0 on success, negative on error
 */
int IMP_FrameSource_CreateChn(int chnNum, IMPFSChnAttr *chn_attr);

/**
 * Destroy frame source channel
 * 
 * @param chnNum Channel number
 * @return 0 on success, negative on error
 */
int IMP_FrameSource_DestroyChn(int chnNum);

/**
 * Enable frame source channel
 * 
 * @param chnNum Channel number
 * @return 0 on success, negative on error
 */
int IMP_FrameSource_EnableChn(int chnNum);

/**
 * Disable frame source channel
 * 
 * @param chnNum Channel number
 * @return 0 on success, negative on error
 */
int IMP_FrameSource_DisableChn(int chnNum);

/**
 * Set channel attributes
 * 
 * @param chnNum Channel number
 * @param chn_attr Channel attributes
 * @return 0 on success, negative on error
 */
int IMP_FrameSource_SetChnAttr(int chnNum, IMPFSChnAttr *chn_attr);

/**
 * Get channel attributes
 * 
 * @param chnNum Channel number
 * @param chn_attr Pointer to channel attributes
 * @return 0 on success, negative on error
 */
int IMP_FrameSource_GetChnAttr(int chnNum, IMPFSChnAttr *chn_attr);

/**
 * Set channel FIFO attributes
 * 
 * @param chnNum Channel number
 * @param attr FIFO attributes
 * @return 0 on success, negative on error
 */
int IMP_FrameSource_SetChnFifoAttr(int chnNum, IMPFSChnFifoAttr *attr);

/**
 * Get channel FIFO attributes
 * 
 * @param chnNum Channel number
 * @param attr Pointer to FIFO attributes
 * @return 0 on success, negative on error
 */
int IMP_FrameSource_GetChnFifoAttr(int chnNum, IMPFSChnFifoAttr *attr);

/**
 * Set frame depth
 * 
 * @param chnNum Channel number
 * @param depth Frame depth
 * @return 0 on success, negative on error
 */
int IMP_FrameSource_SetFrameDepth(int chnNum, int depth);

/* Additional APIs used by some apps (vendor parity) */
int IMP_FrameSource_GetFrame(int chnNum, void **frame);
int IMP_FrameSource_ReleaseFrame(int chnNum, void *frame);
int IMP_FrameSource_SnapFrame(int chnNum, IMPPixelFormat fmt, int width, int height,
                              void *out_buffer, IMPFrameInfo *info);

/**
 * Set channel rotation (T31 only)
 * 
 * @param chnNum Channel number
 * @param rotation Rotation angle (0, 90, 180, 270)
 * @param height Height after rotation
 * @param width Width after rotation
 * @return 0 on success, negative on error
 */
int IMP_FrameSource_SetChnRotate(int chnNum, int rotation, int height, int width);

/* Additional framesource functions (raptor-hal parity) */
int IMP_FrameSource_GetFrameDepth(int chnNum, int *depth);
int IMP_FrameSource_SetDelay(int chnNum, int delay_ms);
int IMP_FrameSource_GetDelay(int chnNum, int *delay_ms);
int IMP_FrameSource_SetMaxDelay(int chnNum, int max_delay_ms);
int IMP_FrameSource_GetMaxDelay(int chnNum, int *max_delay_ms);
int IMP_FrameSource_SetPool(int chnNum, int poolId);
int IMP_FrameSource_ChnStatQuery(int chnNum, void *stat);
int IMP_FrameSource_EnableChnUndistort(int chnNum);
int IMP_FrameSource_DisableChnUndistort(int chnNum);
int IMP_FrameSource_GetTimedFrame(int chnNum, void *framets, int block,
                                   void *framedata, void *frame);
int IMP_FrameSource_SetFrameOffset(int chnNum, int offset);

#ifdef __cplusplus
}
#endif

#endif /* __IMP_FRAMESOURCE_H__ */

