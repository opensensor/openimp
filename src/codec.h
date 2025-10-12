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

#ifdef __cplusplus
}
#endif

#endif /* CODEC_H */

