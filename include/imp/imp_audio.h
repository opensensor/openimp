/**
 * IMP Audio Module
 * 
 * Audio input, output, encoding, and decoding
 */

#ifndef __IMP_AUDIO_H__
#define __IMP_AUDIO_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "imp_common.h"

/**
 * Audio sample rate
 */
typedef enum {
    AUDIO_SAMPLE_RATE_8000 = 8000,      /**< 8KHz */
    AUDIO_SAMPLE_RATE_16000 = 16000,    /**< 16KHz */
    AUDIO_SAMPLE_RATE_24000 = 24000,    /**< 24KHz */
    AUDIO_SAMPLE_RATE_44100 = 44100,    /**< 44.1KHz */
    AUDIO_SAMPLE_RATE_48000 = 48000     /**< 48KHz */
} IMPAudioSampleRate;

/**
 * Audio bit width
 */
typedef enum {
    AUDIO_BIT_WIDTH_16 = 16             /**< 16-bit */
} IMPAudioBitWidth;

/**
 * Audio sound mode
 */
typedef enum {
    AUDIO_SOUND_MODE_MONO = 1,          /**< Mono */
    AUDIO_SOUND_MODE_STEREO = 2         /**< Stereo */
} IMPAudioSoundMode;

/**
 * Audio payload type
 */
typedef enum {
    PT_PCM = 0,                         /**< PCM */
    PT_G711A = 1,                       /**< G.711 A-law */
    PT_G711U = 2,                       /**< G.711 μ-law */
    PT_G726 = 3,                        /**< G.726 */
    PT_AEC = 4,                         /**< AEC */
    PT_ADPCM = 5,                       /**< ADPCM */
    PT_OPUS = 6,                        /**< Opus */
    PT_AAC = 7                          /**< AAC */
} IMPAudioPalyloadType;

/**
 * Audio I/O attributes
 */
typedef struct {
    IMPAudioSampleRate samplerate;      /**< Sample rate */
    IMPAudioBitWidth bitwidth;          /**< Bit width */
    IMPAudioSoundMode soundmode;        /**< Sound mode */
    int frmNum;                         /**< Frame number */
    int numPerFrm;                      /**< Samples per frame */
    int chnCnt;                         /**< Channel count */
} IMPAudioIOAttr;

/**
 * Audio input channel parameters
 */
typedef struct {
    int usrFrmDepth;                    /**< User frame depth */
    int Rev;                            /**< Reserved */
} IMPAudioIChnParam;

/**
 * Audio output channel parameters
 */
typedef struct {
    int mode;                           /**< Mode */
} IMPAudioOChnParam;

/**
 * Audio frame
 */
typedef struct {
    IMPAudioBitWidth bitwidth;          /**< Bit width */
    IMPAudioSoundMode soundmode;        /**< Sound mode */
    uint32_t *virAddr;                  /**< Virtual address */
    uint32_t phyAddr;                   /**< Physical address */
    int64_t timeStamp;                  /**< Timestamp */
    int seq;                            /**< Sequence number */
    int len;                            /**< Length in bytes */
} IMPAudioFrame;

/**
 * Audio stream
 */
typedef struct {
    uint32_t *stream;                   /**< Stream data */
    uint32_t phyAddr;                   /**< Physical address */
    int len;                            /**< Length */
    int64_t timeStamp;                  /**< Timestamp */
    int seq;                            /**< Sequence number */
} IMPAudioStream;

/**
 * Audio encoder channel attributes
 */
typedef struct {
    IMPAudioPalyloadType type;          /**< Payload type */
    int bufSize;                        /**< Buffer size */
    uint32_t *value;                    /**< Value pointer */
} IMPAudioEncChnAttr;

/**
 * Audio decoder channel attributes
 */
typedef struct {
    IMPAudioPalyloadType type;          /**< Payload type */
    int bufSize;                        /**< Buffer size */
    IMPAudioSoundMode mode;             /**< Sound mode */
    uint32_t *value;                    /**< Value pointer */
} IMPAudioDecChnAttr;

/**
 * Audio encoder callbacks
 */
typedef struct {
    IMPAudioPalyloadType type;          /**< Payload type */
    int maxFrmLen;                      /**< Maximum frame length */
    char name[32];                      /**< Encoder name */
    int (*openEncoder)(void *attr, void *enc);      /**< Open callback */
    int (*encoderFrm)(void *enc, IMPAudioFrame *data, unsigned char *outbuf, int *outLen);  /**< Encode callback */
    int (*closeEncoder)(void *enc);     /**< Close callback */
} IMPAudioEncEncoder;

/**
 * Audio decoder callbacks
 */
typedef struct {
    IMPAudioPalyloadType type;          /**< Payload type */
    int maxFrmLen;                      /**< Maximum frame length */
    char name[32];                      /**< Decoder name */
    int (*openDecoder)(void *attr, void *dec);      /**< Open callback */
    int (*decodeFrm)(void *dec, unsigned char *inbuf, int inLen, unsigned short *outbuf, int *outLen, int *chns);  /**< Decode callback */
    int (*getFrmInfo)(void *dec, void *info);       /**< Get frame info callback */
    int (*closeDecoder)(void *dec);     /**< Close callback */
} IMPAudioDecDecoder;

/**
 * AGC configuration
 */
typedef struct {
    int TargetLevelDbfs;                /**< Target level in dBFS */
    int CompressionGaindB;              /**< Compression gain in dB */
} IMPAudioAgcConfig;

/* Audio Input (AI) Functions */

/**
 * Enable audio input device
 * 
 * @param audioDevId Audio device ID
 * @return 0 on success, negative on error
 */
int IMP_AI_Enable(int audioDevId);

/**
 * Disable audio input device
 * 
 * @param audioDevId Audio device ID
 * @return 0 on success, negative on error
 */
int IMP_AI_Disable(int audioDevId);

/**
 * Set audio input public attributes
 * 
 * @param audioDevId Audio device ID
 * @param attr I/O attributes
 * @return 0 on success, negative on error
 */
int IMP_AI_SetPubAttr(int audioDevId, IMPAudioIOAttr *attr);

/**
 * Get audio input public attributes
 * 
 * @param audioDevId Audio device ID
 * @param attr Pointer to I/O attributes
 * @return 0 on success, negative on error
 */
int IMP_AI_GetPubAttr(int audioDevId, IMPAudioIOAttr *attr);

/**
 * Enable audio input channel
 * 
 * @param audioDevId Audio device ID
 * @param aiChn Audio input channel
 * @return 0 on success, negative on error
 */
int IMP_AI_EnableChn(int audioDevId, int aiChn);

/**
 * Disable audio input channel
 * 
 * @param audioDevId Audio device ID
 * @param aiChn Audio input channel
 * @return 0 on success, negative on error
 */
int IMP_AI_DisableChn(int audioDevId, int aiChn);

/**
 * Set audio input channel parameters
 * 
 * @param audioDevId Audio device ID
 * @param aiChn Audio input channel
 * @param attr Channel parameters
 * @return 0 on success, negative on error
 */
int IMP_AI_SetChnParam(int audioDevId, int aiChn, IMPAudioIChnParam *attr);

/**
 * Get audio input channel parameters
 * 
 * @param audioDevId Audio device ID
 * @param aiChn Audio input channel
 * @param attr Pointer to channel parameters
 * @return 0 on success, negative on error
 */
int IMP_AI_GetChnParam(int audioDevId, int aiChn, IMPAudioIChnParam *attr);

/**
 * Set audio input volume
 * 
 * @param audioDevId Audio device ID
 * @param aiChn Audio input channel
 * @param vol Volume level
 * @return 0 on success, negative on error
 */
int IMP_AI_SetVol(int audioDevId, int aiChn, int vol);

/**
 * Get audio input volume
 * 
 * @param audioDevId Audio device ID
 * @param aiChn Audio input channel
 * @param vol Pointer to volume level
 * @return 0 on success, negative on error
 */
int IMP_AI_GetVol(int audioDevId, int aiChn, int *vol);

/**
 * Set audio input gain
 * 
 * @param audioDevId Audio device ID
 * @param aiChn Audio input channel
 * @param gain Gain level
 * @return 0 on success, negative on error
 */
int IMP_AI_SetGain(int audioDevId, int aiChn, int gain);

/**
 * Get audio input gain
 * 
 * @param audioDevId Audio device ID
 * @param aiChn Audio input channel
 * @param gain Pointer to gain level
 * @return 0 on success, negative on error
 */
int IMP_AI_GetGain(int audioDevId, int aiChn, int *gain);

/**
 * Set ALC gain (T21/T31/C100)
 * 
 * @param audioDevId Audio device ID
 * @param aiChn Audio input channel
 * @param gain ALC gain level
 * @return 0 on success, negative on error
 */
int IMP_AI_SetAlcGain(int audioDevId, int aiChn, int gain);

/**
 * Poll for audio frame
 * 
 * @param audioDevId Audio device ID
 * @param aiChn Audio input channel
 * @param timeoutMs Timeout in milliseconds
 * @return 0 on success, negative on error
 */
int IMP_AI_PollingFrame(int audioDevId, int aiChn, uint32_t timeoutMs);

/**
 * Get audio frame
 * 
 * @param audioDevId Audio device ID
 * @param aiChn Audio input channel
 * @param frame Pointer to audio frame
 * @param block Blocking mode
 * @return 0 on success, negative on error
 */
int IMP_AI_GetFrame(int audioDevId, int aiChn, IMPAudioFrame *frame, IMPBlock block);

/**
 * Release audio frame
 * 
 * @param audioDevId Audio device ID
 * @param aiChn Audio input channel
 * @param frame Pointer to audio frame
 * @return 0 on success, negative on error
 */
int IMP_AI_ReleaseFrame(int audioDevId, int aiChn, IMPAudioFrame *frame);

/**
 * Enable noise suppression
 * 
 * @param attr I/O attributes
 * @param level NS level
 * @return 0 on success, negative on error
 */
int IMP_AI_EnableNs(IMPAudioIOAttr *attr, int level);

/**
 * Disable noise suppression
 * 
 * @return 0 on success, negative on error
 */
int IMP_AI_DisableNs(void);

/**
 * Enable high-pass filter
 * 
 * @return 0 on success, negative on error
 */
int IMP_AI_EnableHpf(void);

/**
 * Disable high-pass filter
 * 
 * @return 0 on success, negative on error
 */
int IMP_AI_DisableHpf(void);

/**
 * Enable automatic gain control
 * 
 * @param attr I/O attributes
 * @param config AGC configuration
 * @return 0 on success, negative on error
 */
int IMP_AI_EnableAgc(IMPAudioIOAttr *attr, IMPAudioAgcConfig config);

/**
 * Disable automatic gain control
 * 
 * @return 0 on success, negative on error
 */
int IMP_AI_DisableAgc(void);

/* Audio Encoder (AENC) Functions */

/**
 * Register audio encoder
 * 
 * @param handle Pointer to encoder handle
 * @param encoder Encoder callbacks
 * @return 0 on success, negative on error
 */
int IMP_AENC_RegisterEncoder(int *handle, IMPAudioEncEncoder *encoder);

/**
 * Unregister audio encoder
 * 
 * @param handle Pointer to encoder handle
 * @return 0 on success, negative on error
 */
int IMP_AENC_UnRegisterEncoder(int *handle);

/**
 * Create audio encoder channel
 * 
 * @param aeChn Audio encoder channel
 * @param attr Channel attributes
 * @return 0 on success, negative on error
 */
int IMP_AENC_CreateChn(int aeChn, IMPAudioEncChnAttr *attr);

/**
 * Destroy audio encoder channel
 * 
 * @param aeChn Audio encoder channel
 * @return 0 on success, negative on error
 */
int IMP_AENC_DestroyChn(int aeChn);

/**
 * Send frame to encoder
 * 
 * @param aeChn Audio encoder channel
 * @param frame Audio frame
 * @return 0 on success, negative on error
 */
int IMP_AENC_SendFrame(int aeChn, IMPAudioFrame *frame);

/**
 * Poll for encoded stream
 * 
 * @param aeChn Audio encoder channel
 * @param timeoutMs Timeout in milliseconds
 * @return 0 on success, negative on error
 */
int IMP_AENC_PollingStream(int aeChn, uint32_t timeoutMs);

/**
 * Get encoded stream
 * 
 * @param aeChn Audio encoder channel
 * @param stream Pointer to audio stream
 * @param block Blocking mode
 * @return 0 on success, negative on error
 */
int IMP_AENC_GetStream(int aeChn, IMPAudioStream *stream, IMPBlock block);

/**
 * Release encoded stream
 * 
 * @param aeChn Audio encoder channel
 * @param stream Pointer to audio stream
 * @return 0 on success, negative on error
 */
int IMP_AENC_ReleaseStream(int aeChn, IMPAudioStream *stream);

/* Audio Decoder (ADEC) Functions */

/**
 * Register audio decoder
 * 
 * @param handle Pointer to decoder handle
 * @param decoder Decoder callbacks
 * @return 0 on success, negative on error
 */
int IMP_ADEC_RegisterDecoder(int *handle, IMPAudioDecDecoder *decoder);

/**
 * Unregister audio decoder
 * 
 * @param handle Pointer to decoder handle
 * @return 0 on success, negative on error
 */
int IMP_ADEC_UnRegisterDecoder(int *handle);

/**
 * Create audio decoder channel
 * 
 * @param adChn Audio decoder channel
 * @param attr Channel attributes
 * @return 0 on success, negative on error
 */
int IMP_ADEC_CreateChn(int adChn, IMPAudioDecChnAttr *attr);

/**
 * Destroy audio decoder channel
 * 
 * @param adChn Audio decoder channel
 * @return 0 on success, negative on error
 */
int IMP_ADEC_DestroyChn(int adChn);

/**
 * Send stream to decoder
 * 
 * @param adChn Audio decoder channel
 * @param stream Audio stream
 * @param block Blocking mode
 * @return 0 on success, negative on error
 */
int IMP_ADEC_SendStream(int adChn, IMPAudioStream *stream, IMPBlock block);

/**
 * Get decoded stream
 * 
 * @param adChn Audio decoder channel
 * @param stream Pointer to audio stream
 * @param block Blocking mode
 * @return 0 on success, negative on error
 */
int IMP_ADEC_GetStream(int adChn, IMPAudioStream *stream, IMPBlock block);

/**
 * Release decoded stream
 * 
 * @param adChn Audio decoder channel
 * @param stream Pointer to audio stream
 * @return 0 on success, negative on error
 */
int IMP_ADEC_ReleaseStream(int adChn, IMPAudioStream *stream);

#ifdef __cplusplus
}
#endif

#endif /* __IMP_AUDIO_H__ */

