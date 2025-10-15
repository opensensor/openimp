/**
 * IMP Encoder Module
 * 
 * Video encoding (H264/H265/JPEG)
 */

#ifndef __IMP_ENCODER_H__
#define __IMP_ENCODER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "imp_common.h"

/**
 * Encoder type
 */
typedef enum {
    IMP_ENC_TYPE_AVC      = 0,
    IMP_ENC_TYPE_HEVC     = 1,
    IMP_ENC_TYPE_JPEG     = 4,
} IMPEncoderEncType;

/**
 * Profile IDC values (match H.264/H.265 spec)
 */
#define IMP_ENC_AVC_PROFILE_IDC_BASELINE      66
#define IMP_ENC_AVC_PROFILE_IDC_MAIN          77
#define IMP_ENC_AVC_PROFILE_IDC_HIGH          100
#define IMP_ENC_HEVC_PROFILE_IDC_MAIN         1

/**
 * Encoder profile (matches vendor library format)
 */
typedef enum {
    IMP_ENC_PROFILE_AVC_BASELINE  = ((IMP_ENC_TYPE_AVC << 24) | (IMP_ENC_AVC_PROFILE_IDC_BASELINE)),   /**< H.264 Baseline = 0x00000042 = 66 */
    IMP_ENC_PROFILE_AVC_MAIN      = ((IMP_ENC_TYPE_AVC << 24) | (IMP_ENC_AVC_PROFILE_IDC_MAIN)),       /**< H.264 Main = 0x0000004D = 77 */
    IMP_ENC_PROFILE_AVC_HIGH      = ((IMP_ENC_TYPE_AVC << 24) | (IMP_ENC_AVC_PROFILE_IDC_HIGH)),       /**< H.264 High = 0x00000064 = 100 */
    IMP_ENC_PROFILE_HEVC_MAIN     = ((IMP_ENC_TYPE_HEVC << 24) | (IMP_ENC_HEVC_PROFILE_IDC_MAIN)),     /**< H.265 Main = 0x01000001 */
    IMP_ENC_PROFILE_JPEG          = (IMP_ENC_TYPE_JPEG << 24),                                          /**< JPEG = 0x04000000 */
} IMPEncoderProfile;

/**
 * Rate control mode
 */
typedef enum {
    IMP_ENC_RC_MODE_FIXQP = 0,          /**< Fixed QP */
    IMP_ENC_RC_MODE_CBR = 1,            /**< Constant bitrate */
    IMP_ENC_RC_MODE_VBR = 2,            /**< Variable bitrate */
    IMP_ENC_RC_MODE_CAPPED_VBR = 3,     /**< Capped VBR */
    IMP_ENC_RC_MODE_CAPPED_QUALITY = 4  /**< Capped quality */
} IMPEncoderRcMode;

/**
 * GOP mode
 */
typedef enum {
    IMP_ENC_GOP_MODE_NORMALP = 0,       /**< Normal P */
    IMP_ENC_GOP_MODE_SMARTP = 1         /**< Smart P */
} IMPEncoderGopMode;

/**
 * Frame rate control
 */
typedef struct {
    uint32_t frmRateNum;                /**< Frame rate numerator */
    uint32_t frmRateDen;                /**< Frame rate denominator */
} IMPEncoderFrmRate;

/**
 * H264 CBR attributes
 */
typedef struct {
    uint32_t outFrmRate;                /**< Output frame rate */
    uint32_t maxGop;                    /**< Maximum GOP */
    uint32_t maxQp;                     /**< Maximum QP */
    uint32_t minQp;                     /**< Minimum QP */
    uint32_t iBiasLvl;                  /**< I frame bias level */
    uint32_t frmQPStep;                 /**< Frame QP step */
    uint32_t gopQPStep;                 /**< GOP QP step */
    int adaptiveMode;                   /**< Adaptive mode */
    int gopRelation;                    /**< GOP relation */
} IMPEncoderAttrH264Cbr;

/**
 * H264 VBR attributes
 */
typedef struct {
    uint32_t outFrmRate;                /**< Output frame rate */
    uint32_t maxGop;                    /**< Maximum GOP */
    uint32_t maxQp;                     /**< Maximum QP */
    uint32_t minQp;                     /**< Minimum QP */
    int staticTime;                     /**< Static time */
} IMPEncoderAttrH264Vbr;

/**
 * H264 FixQP attributes
 */
typedef struct {
    uint32_t outFrmRate;                /**< Output frame rate */
    uint32_t maxGop;                    /**< Maximum GOP */
    uint32_t qp;                        /**< QP value */
} IMPEncoderAttrH264FixQp;

/**
 * H264 rate control attributes
 */
typedef struct {
    IMPEncoderRcMode rcMode;            /**< RC mode */
    union {
        IMPEncoderAttrH264Cbr attrH264Cbr;
        IMPEncoderAttrH264Vbr attrH264Vbr;
        IMPEncoderAttrH264FixQp attrH264FixQp;
    };
} IMPEncoderAttrRcMode;

/**
 * H264 GOP attributes
 */
typedef struct {
    uint32_t gopLength;                 /**< GOP length */
    uint32_t ipQpDelta;                 /**< IP QP delta */
    IMPEncoderGopMode gopMode;          /**< GOP mode */
} IMPEncoderGopAttr;

/**
 * H264 attributes
 */
typedef struct {
    uint32_t maxPicWidth;               /**< Maximum picture width */
    uint32_t maxPicHeight;              /**< Maximum picture height */
    uint32_t bufSize;                   /**< Buffer size */
    uint32_t profile;                   /**< Profile */
} IMPEncoderAttrH264;

/**
 * H265 attributes (similar to H264)
 */
typedef IMPEncoderAttrH264 IMPEncoderAttrH265;

/**
 * JPEG attributes
 */
typedef struct {
    uint32_t maxPicWidth;               /**< Maximum picture width */
    uint32_t maxPicHeight;              /**< Maximum picture height */
    uint32_t bufSize;                   /**< Buffer size */
} IMPEncoderAttrJpeg;

/**
 * Encoder attributes
 */
typedef struct {
    IMPEncoderProfile profile;          /**< Encoder profile */
    union {
        IMPEncoderAttrH264 attrH264;
        IMPEncoderAttrH265 attrH265;
        IMPEncoderAttrJpeg attrJpeg;
    };
} IMPEncoderAttr;

/**
 * Rate control attributes
 */
typedef struct {
    IMPEncoderAttrRcMode attrRcMode;    /**< RC mode attributes */
    IMPEncoderGopAttr attrGop;          /**< GOP attributes */
    IMPEncoderFrmRate outFrmRate;       /**< Output frame rate */
} IMPEncoderRcAttr;

/**
 * Encoder channel attributes (T20/T21/T23)
 */
typedef struct {
    IMPEncoderAttr encAttr;             /**< Encoder attributes */
    IMPEncoderRcAttr rcAttr;            /**< RC attributes */
} IMPEncoderCHNAttr;

/**
 * Encoder channel attributes (T31/C100/T40/T41)
 */
typedef struct {
    IMPEncoderAttr encAttr;             /**< Encoder attributes */
    IMPEncoderRcAttr rcAttr;            /**< RC attributes */
} IMPEncoderChnAttr;

/* T31-compatible NAL type structure (minimal) */
typedef struct {
    uint8_t h264NalType;                /**< H.264 NAL type */
    uint8_t h265NalType;                /**< H.265 NAL type */
} IMPEncoderNalType;

/* T31-compatible slice type (keep as int for ABI leniency) */
typedef int IMPEncoderSliceType;

/**
 * Stream pack (T31 layout)
 */
typedef struct {
    uint32_t offset;                    /**< Stream packet offset */
    uint32_t length;                    /**< Stream packet length */
    int64_t  timestamp;                 /**< Timestamp in us */
    int      frameEnd;                  /**< End of frame flag (bool) */
    IMPEncoderNalType   nalType;        /**< NAL type (H.264/H.265) */
    IMPEncoderSliceType sliceType;      /**< Slice type */
} IMPEncoderPack;

/**
 * Encoder stream (T31 layout)
 */
typedef struct {
    uint32_t        phyAddr;            /**< Physical base address of frame */
    uint32_t        virAddr;            /**< Virtual base address of frame */
    uint32_t        streamSize;         /**< Size of the allocated virtual address */
    IMPEncoderPack *pack;               /**< Frame stream packets */
    uint32_t        packCount;          /**< Number of packets in the frame */
    uint32_t        seq;                /**< Sequence number of coded frame */
    int             isVI;               /**< Source is VI flag (bool) */
    union {
        struct {                         /* Minimal JPEG info placeholder */
            int32_t iNumBytes;
            int16_t iQPfactor;
        } jpegInfo;
        struct {                         /* Minimal stream info placeholder */
            int32_t iNumBytes;
            int16_t iSliceQP;
            int16_t iMinQP;
            int16_t iMaxQP;
        } streamInfo;
    };
} IMPEncoderStream;

/**
 * Encoder channel statistics (T20/T21/T23)
 */
typedef struct {
    uint32_t leftPics;                  /**< Left pictures */
    uint32_t leftBytes;                 /**< Left bytes */
    uint32_t leftFrames;                /**< Left frames */
    uint32_t curPacks;                  /**< Current packs */
    uint32_t work_done;                 /**< Work done flag */
} IMPEncoderCHNStat;

/**
 * Encoder channel statistics (T31/C100/T40/T41)
 */
typedef struct {
    uint32_t leftPics;                  /**< Left pictures */
    uint32_t leftBytes;                 /**< Left bytes */
    uint32_t leftFrames;                /**< Left frames */
    uint32_t curPacks;                  /**< Current packs */
    uint32_t work_done;                 /**< Work done flag */
} IMPEncoderChnStat;

/**
 * JPEG quality level
 */
typedef struct {
    uint32_t qmaxI;                     /**< Maximum I frame quality */
    uint32_t qminI;                     /**< Minimum I frame quality */
    uint32_t qmaxP;                     /**< Maximum P frame quality */
    uint32_t qminP;                     /**< Minimum P frame quality */
} IMPEncoderJpegeQl;

/**
 * Quantization Parameter (QP) structure
 */
typedef struct {
    uint32_t qp_i;                      /**< I frame QP */
    uint32_t qp_p;                      /**< P frame QP */
    uint32_t qp_b;                      /**< B frame QP */
} IMPEncoderQp;

/**
 * Create encoder group
 * 
 * @param encGroup Encoder group number
 * @return 0 on success, negative on error
 */
int IMP_Encoder_CreateGroup(int encGroup);

/**
 * Destroy encoder group
 * 
 * @param encGroup Encoder group number
 * @return 0 on success, negative on error
 */
int IMP_Encoder_DestroyGroup(int encGroup);

/**
 * Create encoder channel (T20/T21/T23)
 * 
 * @param encChn Encoder channel number
 * @param attr Channel attributes
 * @return 0 on success, negative on error
 */
int IMP_Encoder_CreateChn(int encChn, IMPEncoderCHNAttr *attr);

/**
 * Destroy encoder channel
 * 
 * @param encChn Encoder channel number
 * @return 0 on success, negative on error
 */
int IMP_Encoder_DestroyChn(int encChn);

/**
 * Register channel to group
 * 
 * @param encGroup Encoder group number
 * @param encChn Encoder channel number
 * @return 0 on success, negative on error
 */
int IMP_Encoder_RegisterChn(int encGroup, int encChn);

/**
 * Unregister channel from group
 * 
 * @param encChn Encoder channel number
 * @return 0 on success, negative on error
 */
int IMP_Encoder_UnRegisterChn(int encChn);

/**
 * Start receiving pictures
 * 
 * @param encChn Encoder channel number
 * @return 0 on success, negative on error
 */
int IMP_Encoder_StartRecvPic(int encChn);

/**
 * Stop receiving pictures
 * 
 * @param encChn Encoder channel number
 * @return 0 on success, negative on error
 */
int IMP_Encoder_StopRecvPic(int encChn);

/**
 * Get encoded stream
 * 
 * @param encChn Encoder channel number
 * @param stream Pointer to stream structure
 * @param block Blocking mode
 * @return 0 on success, negative on error
 */
int IMP_Encoder_GetStream(int encChn, IMPEncoderStream *stream, int block);

/**
 * Release encoded stream
 * 
 * @param encChn Encoder channel number
 * @param stream Pointer to stream structure
 * @return 0 on success, negative on error
 */
int IMP_Encoder_ReleaseStream(int encChn, IMPEncoderStream *stream);

/**
 * Poll for stream availability
 * 
 * @param encChn Encoder channel number
 * @param timeoutMsec Timeout in milliseconds
 * @return 0 on success, negative on error
 */
int IMP_Encoder_PollingStream(int encChn, uint32_t timeoutMsec);

/**
 * Query channel status (T20/T21/T23)
 * 
 * @param encChn Encoder channel number
 * @param stat Pointer to status structure
 * @return 0 on success, negative on error
 */
int IMP_Encoder_Query(int encChn, IMPEncoderCHNStat *stat);

/**
 * Request IDR frame
 * 
 * @param encChn Encoder channel number
 * @return 0 on success, negative on error
 */
int IMP_Encoder_RequestIDR(int encChn);

/**
 * Flush stream
 * 
 * @param encChn Encoder channel number
 * @return 0 on success, negative on error
 */
int IMP_Encoder_FlushStream(int encChn);

/**
 * Set default parameters
 * 
 * @param attr Channel attributes
 * @param profile Encoder profile
 * @param rcMode Rate control mode
 * @param width Picture width
 * @param height Picture height
 * @param fpsNum FPS numerator
 * @param fpsDen FPS denominator
 * @param gopLen GOP length
 * @param gopMode GOP mode (2 for normal)
 * @param quality Quality (-1 for default, or JPEG quality)
 * @param bitrate Target bitrate
 * @return 0 on success, negative on error
 */
int IMP_Encoder_SetDefaultParam(IMPEncoderChnAttr *attr, IMPEncoderProfile profile,
                                 IMPEncoderRcMode rcMode, int width, int height,
                                 int fpsNum, int fpsDen, int gopLen, int gopMode,
                                 int quality, int bitrate);

/**
 * Get channel attributes
 * 
 * @param encChn Encoder channel number
 * @param attr Pointer to channel attributes
 * @return 0 on success, negative on error
 */
int IMP_Encoder_GetChnAttr(int encChn, IMPEncoderChnAttr *attr);

/**
 * Set JPEG quality
 * 
 * @param encChn Encoder channel number
 * @param attr JPEG quality attributes
 * @return 0 on success, negative on error
 */
int IMP_Encoder_SetJpegeQl(int encChn, IMPEncoderJpegeQl *attr);

/**
 * Set buffer sharing between channels (T31/C100/T40/T41)
 * 
 * @param srcChn Source channel
 * @param dstChn Destination channel
 * @return 0 on success, negative on error
 */
int IMP_Encoder_SetbufshareChn(int srcChn, int dstChn);

/**
 * Enable/disable fisheye (T31)
 * 
 * @param encChn Encoder channel number
 * @param enable Enable flag
 * @return 0 on success, negative on error
 */
int IMP_Encoder_SetFisheyeEnableStatus(int encChn, int enable);

/**
 * Get encoder file descriptor
 *
 * @param encChn Encoder channel number
 * @return File descriptor, or negative on error
 */
int IMP_Encoder_GetFd(int encChn);

/**
 * Set channel QP (Quantization Parameter)
 *
 * @param encChn Encoder channel number
 * @param qp QP structure
 * @return 0 on success, negative on error
 */
int IMP_Encoder_SetChnQp(int encChn, IMPEncoderQp *qp);

/**
 * Set channel GOP length
 *
 * @param encChn Encoder channel number
 * @param gopLength GOP length
 * @return 0 on success, negative on error
 */
int IMP_Encoder_SetChnGopLength(int encChn, int gopLength);

/**
 * Set channel entropy mode
 *
 * @param encChn Encoder channel number
 * @param mode Entropy mode (0=CAVLC, 1=CABAC)
 * @return 0 on success, negative on error
 */
int IMP_Encoder_SetChnEntropyMode(int encChn, int mode);

/**
 * Set maximum stream count
 *
 * @param encChn Encoder channel number
 * @param cnt Maximum stream count
 * @return 0 on success, negative on error
 */
int IMP_Encoder_SetMaxStreamCnt(int encChn, int cnt);

/**
 * Set stream buffer size
 *
 * @param encChn Encoder channel number
 * @param size Stream buffer size
 * @return 0 on success, negative on error
 */
int IMP_Encoder_SetStreamBufSize(int encChn, int size);

#ifdef __cplusplus
}
#endif

#endif /* __IMP_ENCODER_H__ */

