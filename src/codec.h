/**
 * AL_Codec Interface
 * Based on reverse engineering of libimp.so v1.1.6
 */

#ifndef CODEC_H
#define CODEC_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Set default encoding parameters
 * @param param Pointer to parameter structure (0x794 bytes)
 * @return 0 on success, -1 on failure
 */
int AL_Codec_Encode_SetDefaultParam(void *param);

/**
 * Create a codec encoder instance
 * @param codec Output pointer to codec instance
 * @param params Codec parameters (0x794 bytes)
 * @return 0 on success, -1 on failure
 */
int AL_Codec_Encode_Create(void **codec, void *params);

/**
 * Destroy a codec encoder instance
 * @param codec Codec instance to destroy
 * @return 0 on success, -1 on failure
 */
int AL_Codec_Encode_Destroy(void *codec);

/**
 * Get source frame buffer count and size
 * @param codec Codec instance
 * @param cnt Output frame buffer count
 * @param size Output frame buffer size
 * @return 0 on success, -1 on failure
 */
int AL_Codec_Encode_GetSrcFrameCntAndSize(void *codec, int *cnt, int *size);

/**
 * Get source stream buffer count and size
 * @param codec Codec instance
 * @param cnt Output stream buffer count
 * @param size Output stream buffer size
 * @return 0 on success, -1 on failure
 */
int AL_Codec_Encode_GetSrcStreamCntAndSize(void *codec, int *cnt, int *size);

int AL_Codec_Encode_SetStreamBufferCount(void *codec, int count);
int AL_Codec_Encode_SetStreamBufferSize(void *codec, int size);

/**
 * Process a frame for encoding
 * @param codec Codec instance
 * @param frame Frame buffer to encode
 * @param user_data User data pointer
 * @return 0 on success, -1 on failure
 */
int AL_Codec_Encode_Process(void *codec, void *frame, void *user_data);

/**
 * Get an encoded stream
 * @param codec Codec instance
 * @param stream Output stream buffer
 * @return 0 on success, -1 on failure
 */
int AL_Codec_Encode_GetStream(void *codec, void **stream, void **user_data);

/**
 * Release an encoded stream
 * @param codec Codec instance
 * @param stream Stream buffer to release
 * @param user_data User data pointer
 * @return 0 on success, -1 on failure
 */
int AL_Codec_Encode_ReleaseStream(void *codec, void *stream, void *user_data);

/**
 * Set QP (Quantization Parameter) for encoder
 * @param codec Codec instance
 * @param qp QP structure pointer
 * @return 0 on success, -1 on failure
 */
int AL_Codec_Encode_SetQp(void *codec, void *qp);

/**
 * Set entropy mode for encoder
 * @param codec Codec instance
 * @param mode 0=CAVLC, 1=CABAC
 * @return 0 on success, -1 on failure
 */
int AL_Codec_Encode_SetEntropyMode(void *codec, int mode);

/**
 * Set QP bounds (min/max) for encoder
 * @param codec Codec instance
 * @param minQp Minimum QP value
 * @param maxQp Maximum QP value
 * @return 0 on success, -1 on failure
 */
int AL_Codec_Encode_SetQpBounds(void *codec, int minQp, int maxQp);

/**
 * Set bitrate for encoder
 * @param codec Codec instance
 * @param targetBitrate Target bitrate in kbps
 * @param maxBitrate Maximum bitrate in kbps
 * @return 0 on success, -1 on failure
 */
int AL_Codec_Encode_SetBitRate(void *codec, int targetBitrate, int maxBitrate);

int AL_Codec_Encode_GetRcParam(void *codec, void *rcAttr);
int AL_Codec_Encode_SetRcParam(void *codec, void *rcAttr);
#if defined(PLATFORM_T21) || defined(PLATFORM_T23) || defined(PLATFORM_T30)
/* Helix rate-control extras (staticTime, changePos, qualityLvl, QP steps,
 * iBiasLvl, SMART, ...) of an IMPEncoderAttrRcMode, for channel creation */
int AL_Codec_Encode_SetRcExtras(void *codec, const void *rcMode);
/* rcAttr.attrHSkip.hSkipAttr.maxSameSceneCnt as the OEM i264e uses it: the
 * IDR period in GOPs (skip types N1X, H1M only; else 0) */
int AL_Codec_Encode_SetSameSceneGops(void *codec, uint32_t gops);
/* IMP_Encoder_InsertUserData (T20/T21/T23/T30 native Helix): SEI payload for
 * the next picture; -1 when the queue is full. */
int AL_Codec_Encode_InsertUserData(void *codec, const void *data,
                                   uint32_t size, uint32_t max_cnt,
                                   uint32_t max_size);
/* IMP_Encoder_SetMbRC: the eprc macroblock rate control of the Helix
 * encoder (T21, T23; applied from the next picture) */
int AL_Codec_Encode_SetMbRC(void *codec, int enable);
/* IMP_Encoder_SetChnColor2Grey on the native Helix/NVPU encoder (T20,
 * T21, T10): grey chroma from the next IDR on */
int AL_Codec_Encode_SetColor2Grey(void *codec, int enable);
/* IMP_Encoder_SetChnROI on the native Helix/NVPU encoder (T20, T10, T21): the i264e ROI table entry (t30/helix_roi.h)
 * of region index (0..7), from the next picture on */
int AL_Codec_Encode_SetRoi(void *codec, uint32_t index,
                           const uint8_t entry[7]);
/* IMP_Encoder_SetChnRoiAttr on the AVPU encoder (T41, T31 beyond vendor):
 * the windows (IMPEncoderRoiAttr, up to 10) go into the QP table of the
 * next picture.  Returns -1 for an invalid window, mode or codec, or a
 * delta outside -26..25 (H.264 mb_qp_delta; the effective QPs are clamped
 * further in src/avpu_roi.h so the stream stays valid). */
int AL_Codec_Encode_SetRoiAttr(void *codec, const void *roi_attr);
/* IMP_Encoder_SetH264TransCfg on the native Helix encoder (T20, T21):
 * chroma_qp_index_offset (-12..12) from the next IDR on */
int AL_Codec_Encode_SetChromaQpOffset(void *codec, int offset);
/* IMP_Encoder_SetSuperFrameCfg for the OEM T20/T10 controller: mode
 * HW_SUPERFRM_NONE or HW_SUPERFRM_REENCODE, thresholds in bits */
int AL_Codec_Encode_SetSuperFrame(void *codec, uint32_t mode,
                                  uint32_t i_bits, uint32_t p_bits);
#endif
#if defined(PLATFORM_T31) || defined(PLATFORM_T40) || defined(PLATFORM_T41)
/* CappedVBR/CappedQuality: the PSNR cap (uMaxPSNR, dB) of the OEM capped
 * rate-control modes.  rcMode is the IMP mode; any other mode clears it. */
int AL_Codec_Encode_SetRcQualityCap(void *codec, int rcMode,
                                    unsigned int maxPsnr);
#endif
int AL_Codec_Encode_GetFrameRate(void *codec, void *fps);
int AL_Codec_Encode_SetFrameRate(void *codec, void *fps);
int AL_Codec_Encode_SetQpIPDelta(void *codec, int delta);
int AL_Codec_Encode_RestartGop(void *codec);
int AL_Codec_Encode_GetGopParam(void *codec, void *gopAttr);
int AL_Codec_Encode_SetGopParam(void *codec, void *gopAttr);
int AL_Codec_Encode_SetGopLength(void *codec, int gopLength);
int AL_Codec_Encode_SetInputResolution(void *codec, int width, int height);
int AL_Codec_Encode_SetLoopFilterBetaOffset(void *codec, int offset);
int AL_Codec_Encode_SetLoopFilterTcOffset(void *codec, int offset);
int AL_Codec_Encode_GetLastError(void *codec);
int AL_Codec_Encode_RequestIDR(void *codec);

/**
 * Request an IDR frame on the next encode for this codec instance
 * @param codec Codec instance
 * @return 0 on success, -1 on failure
 */
#ifdef __cplusplus
}
#endif

#endif /* CODEC_H */
