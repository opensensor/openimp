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
    int left;               /**< Left offset */
    int top;                /**< Top offset */
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
 * I2D (rotate/flip/mirror) attributes.  T40 1.3.1 and T41 1.2.6 place this
 * structure at the very start of IMPFSChnAttr; T31 1.1.6 and older do not
 * have it at all (their picWidth is the first member).
 */
#if defined(PLATFORM_T40) || defined(PLATFORM_T41)
typedef struct {
    int i2d_enable;         /**< Output rotate enable */
    int flip_enable;        /**< Output flip enable */
    int mirr_enable;        /**< Output mirror enable */
    int rotate_enable;      /**< Output rotate enable */
    int rotate_angle;       /**< Output rotate angle */
} IMPFSI2DAttr;
#endif

/**
 * Frame source channel attributes
 */
typedef struct {
#if defined(PLATFORM_T40) || defined(PLATFORM_T41)
    IMPFSI2DAttr i2dattr;   /**< T40/T41: I2D block comes first */
#endif
    int picWidth;           /**< Picture width */
    int picHeight;          /**< Picture height */
    IMPPixelFormat pixFmt;  /**< Pixel format */
    IMPFSChnCrop crop;      /**< Crop configuration */
    IMPFSChnScaler scaler;  /**< Scaler configuration */
    int outFrmRateNum;      /**< Output frame rate numerator */
    int outFrmRateDen;      /**< Output frame rate denominator */
    int nrVBs;              /**< Number of video buffers */
    IMPFSChnType type;      /**< Channel type */
#if defined(PLATFORM_T23)
    int mirr_enable;        /**< T23 1.3.0 mirror flag */
    IMPFSChnCrop fcrop;     /**< T23 frame crop */
#elif defined(PLATFORM_T31) || defined(PLATFORM_C100) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
    IMPFSChnCrop fcrop;     /**< Frame crop (newer platforms) */
#endif
#if defined(PLATFORM_T41)
    int mirr_enable;        /**< T41 1.2.6 mirror flag, after fcrop */
#endif
} IMPFSChnAttr;

/*
 * T40 1.3.1 / T41 1.2.6 put IMPFSI2DAttr in front of picWidth (picWidth at
 * 0x14, 100 and 104 bytes total).  The OpenIMP T40/T41 backend reads exactly
 * that layout (src/t40/openimp_p1.c declares its own vendor-exact copy and
 * asserts the same offsets), so the public type has to match -- otherwise a
 * caller built against this header writes picWidth to offset 0 and
 * IMP_FrameSource_EnableChn fails on picWidth <= 0.
 */
#if defined(PLATFORM_T40)
_Static_assert(offsetof(IMPFSChnAttr, picWidth) == 0x14,
               "T40 IMPFSChnAttr.picWidth ABI mismatch");
_Static_assert(sizeof(IMPFSChnAttr) == 0x64,
               "T40 IMPFSChnAttr ABI mismatch");
#elif defined(PLATFORM_T41)
_Static_assert(offsetof(IMPFSChnAttr, picWidth) == 0x14,
               "T41 IMPFSChnAttr.picWidth ABI mismatch");
_Static_assert(offsetof(IMPFSChnAttr, mirr_enable) == 0x64,
               "T41 IMPFSChnAttr.mirr_enable ABI mismatch");
_Static_assert(sizeof(IMPFSChnAttr) == 0x68,
               "T41 IMPFSChnAttr ABI mismatch");
#endif

#if defined(PLATFORM_T23)
_Static_assert(offsetof(IMPFSChnAttr, mirr_enable) == 0x3c,
               "T23 IMPFSChnAttr.mirr_enable ABI mismatch");
_Static_assert(offsetof(IMPFSChnAttr, fcrop) == 0x40,
               "T23 IMPFSChnAttr.fcrop ABI mismatch");
_Static_assert(sizeof(IMPFSChnAttr) == 0x54,
               "T23 IMPFSChnAttr ABI mismatch");
#elif defined(PLATFORM_T21) || defined(PLATFORM_T30)
_Static_assert(sizeof(IMPFSChnAttr) == 0x3c,
               "legacy IMPFSChnAttr ABI mismatch");
#endif

/**
 * Channel FIFO type (IMP_FrameSource_SetChnFifoAttr)
 */
typedef enum {
    FIFO_CACHE_PRIORITY = 0,    /**< FIFO fills first, then outputs */
    FIFO_DATA_PRIORITY,         /**< outputs first, then caches */
} IMPFSChnFifoType;

/**
 * FIFO attributes. The SDK names the second word type; depth is the name
 * older OpenIMP headers used for it.
 */
typedef struct {
    int maxdepth;           /**< Maximum FIFO depth (frames) */
    union {
        IMPFSChnFifoType type;  /**< Channel FIFO type */
        int depth;              /**< same word, older OpenIMP name */
    };
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
 * Set channel rotation (vendor API on T31; OpenIMP extra on T10/T20/T21/T30)
 *
 * Software rotation of the channel's NV12 frames, as in the vendor libimp
 * (docs/T31_ROTATE.md). Call it before CreateChn with the landscape size; the
 * FrameSource attributes stay landscape, set the encoder channel to the
 * rotated size. On SoCs without the software path it returns -1 for a
 * rotation. Probe OpenIMP_Cap_FsRotate / OpenIMP_FS_RotateLimits below.
 *
 * @param chnNum Channel number
 * @param rotTo90 0: off, 1: 90 degrees counterclockwise, 2: 90 degrees
 *                clockwise (vendor values); 3: 180 degrees (OpenIMP only)
 * @param width Image width before rotation
 * @param height Image height before rotation
 * @return 0 on success, negative on error
 */
int IMP_FrameSource_SetChnRotate(int chnNum, int rotTo90, int width, int height);

/* OpenIMP extras (bind weakly; absent in vendor libraries and in OpenIMP
 * builds where SetChnRotate does not rotate): OpenIMP_Cap_FsRotate is 1 when
 * SetChnRotate rotates in software on this SoC.  OpenIMP_FS_RotateLimits
 * returns 0 and the envelope SetChnRotate accepts for chnNum (pre-rotation
 * size at most *max_w x *max_h pixels, both sides multiples of *align,
 * *max_fps 0 = no frame-rate cap), -1 for an invalid channel. */
extern const int OpenIMP_Cap_FsRotate;
int OpenIMP_FS_RotateLimits(int chn, int *max_w, int *max_h, int *max_fps,
                            int *align);

/* Additional framesource functions (raptor-hal parity) */
int IMP_FrameSource_GetFrameDepth(int chnNum, int *depth);
int IMP_FrameSource_SetDelay(int chnNum, int delay_ms);
int IMP_FrameSource_GetDelay(int chnNum, int *delay_ms);
int IMP_FrameSource_SetMaxDelay(int chnNum, int max_delay_ms);
int IMP_FrameSource_GetMaxDelay(int chnNum, int *max_delay_ms);
int IMP_FrameSource_SetPool(int chnNum, int poolId);
int IMP_FrameSource_GetPool(int chnNum);
int IMP_FrameSource_ChnStatQuery(int chnNum, void *stat);
int IMP_FrameSource_EnableChnUndistort(int chnNum, int param);   /* T23/T31: 960x960 channels only */
int IMP_FrameSource_DisableChnUndistort(int chnNum);
int IMP_FrameSource_GetTimedFrame(int chnNum, void *framets, int block,
                                   void *framedata, void *frame);
int IMP_FrameSource_SetFrameOffset(int chnNum, int x, int y);
#if defined(PLATFORM_T31)
/* T31 1.1.5-zrt: cb(chn, frame) for every captured frame before delivery; NULL removes it */
int IMP_FrameSource_SetChnCallback(int chnNum, int (*cb)(int, void *));
#endif

#if defined(PLATFORM_T40) || defined(PLATFORM_T41)
/* T40 1.3.1 / T41 1.2.6: frames without the depth queue, external frame
 * injection (an NV12 channel the application fills), YUV alignment. */
typedef enum {
    FRAME_ALIGN_8BYTE,
    FRAME_ALIGN_16BYTE,
    FRAME_ALIGN_32BYTE,
} FSChannelYuvAlign;

struct yuvaliparm {
    FSChannelYuvAlign w;
    FSChannelYuvAlign h;
};

typedef struct {
    int32_t enable;
    struct yuvaliparm param;
} IMPFrameAlign;

int IMP_FrameSource_GetFrameEx(int chnNum, IMPFrameInfo **frame);
int IMP_FrameSource_ReleaseFrameEx(int chnNum, IMPFrameInfo *pframe);
int IMP_FrameSource_SetYuvAlign(int chnNum, IMPFrameAlign *param);
int IMP_FrameSource_ExternInject_CreateChn(int chnNum, IMPFSChnAttr *chnAttr);
int IMP_FrameSource_ExternInject_DestroyChn(int chnNum);
int IMP_FrameSource_ExternInject_EnableChn(int chnNum);
int IMP_FrameSource_ExternInject_DisableChn(int chnNum);
int IMP_FrameSource_DequeueBuffer(int chnNum, IMPFrameInfo **frame);
int IMP_FrameSource_QueueBuffer(int chnNum, const IMPFrameInfo *frame);
int IMP_EmuFrameSource_GetChnAttr(uint32_t chn_num, IMPFSChnAttr *chn_attr);
int IMP_EmuFrameSource_EnableChn(uint32_t chn_num);
int IMP_EmuFrameSource_DisableChn(uint32_t chn_num);
int IMP_EmuFrameSource_CreateChn(uint32_t chn_num, IMPFSChnAttr *chn_attr);
int IMP_EmuFrameSource_DestroyChn(uint32_t chn_num);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __IMP_FRAMESOURCE_H__ */
