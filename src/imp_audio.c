/**
 * IMP Audio Module Implementation (Stub)
 */

#include <stdio.h>
#include <string.h>
#include <imp/imp_audio.h>

#define LOG_AUD(fmt, ...) fprintf(stderr, "[IMP_Audio] " fmt "\n", ##__VA_ARGS__)

/* Audio Input (AI) Functions */

int IMP_AI_Enable(int audioDevId) {
    LOG_AUD("AI_Enable: dev=%d", audioDevId);
    return 0;
}

int IMP_AI_Disable(int audioDevId) {
    LOG_AUD("AI_Disable: dev=%d", audioDevId);
    return 0;
}

int IMP_AI_SetPubAttr(int audioDevId, IMPAudioIOAttr *attr) {
    if (attr == NULL) return -1;
    LOG_AUD("AI_SetPubAttr: dev=%d, rate=%d, bits=%d", 
            audioDevId, attr->samplerate, attr->bitwidth);
    return 0;
}

int IMP_AI_GetPubAttr(int audioDevId, IMPAudioIOAttr *attr) {
    if (attr == NULL) return -1;
    LOG_AUD("AI_GetPubAttr: dev=%d", audioDevId);
    memset(attr, 0, sizeof(*attr));
    return 0;
}

int IMP_AI_EnableChn(int audioDevId, int aiChn) {
    LOG_AUD("AI_EnableChn: dev=%d, chn=%d", audioDevId, aiChn);
    return 0;
}

int IMP_AI_DisableChn(int audioDevId, int aiChn) {
    LOG_AUD("AI_DisableChn: dev=%d, chn=%d", audioDevId, aiChn);
    return 0;
}

int IMP_AI_SetChnParam(int audioDevId, int aiChn, IMPAudioIChnParam *attr) {
    if (attr == NULL) return -1;
    LOG_AUD("AI_SetChnParam: dev=%d, chn=%d", audioDevId, aiChn);
    return 0;
}

int IMP_AI_GetChnParam(int audioDevId, int aiChn, IMPAudioIChnParam *attr) {
    if (attr == NULL) return -1;
    LOG_AUD("AI_GetChnParam: dev=%d, chn=%d", audioDevId, aiChn);
    memset(attr, 0, sizeof(*attr));
    return 0;
}

int IMP_AI_SetVol(int audioDevId, int aiChn, int vol) {
    LOG_AUD("AI_SetVol: dev=%d, chn=%d, vol=%d", audioDevId, aiChn, vol);
    return 0;
}

int IMP_AI_GetVol(int audioDevId, int aiChn, int *vol) {
    if (vol == NULL) return -1;
    *vol = 60;
    return 0;
}

int IMP_AI_SetGain(int audioDevId, int aiChn, int gain) {
    LOG_AUD("AI_SetGain: dev=%d, chn=%d, gain=%d", audioDevId, aiChn, gain);
    return 0;
}

int IMP_AI_GetGain(int audioDevId, int aiChn, int *gain) {
    if (gain == NULL) return -1;
    *gain = 28;
    return 0;
}

int IMP_AI_SetAlcGain(int audioDevId, int aiChn, int gain) {
    LOG_AUD("AI_SetAlcGain: dev=%d, chn=%d, gain=%d", audioDevId, aiChn, gain);
    return 0;
}

int IMP_AI_PollingFrame(int audioDevId, int aiChn, uint32_t timeoutMs) {
    /* Return timeout */
    return -1;
}

int IMP_AI_GetFrame(int audioDevId, int aiChn, IMPAudioFrame *frame, IMPBlock block) {
    if (frame == NULL) return -1;
    /* No frame available */
    return -1;
}

int IMP_AI_ReleaseFrame(int audioDevId, int aiChn, IMPAudioFrame *frame) {
    if (frame == NULL) return -1;
    return 0;
}

int IMP_AI_EnableNs(IMPAudioIOAttr *attr, int level) {
    if (attr == NULL) return -1;
    LOG_AUD("AI_EnableNs: level=%d", level);
    return 0;
}

int IMP_AI_DisableNs(void) {
    LOG_AUD("AI_DisableNs");
    return 0;
}

int IMP_AI_EnableHpf(void) {
    LOG_AUD("AI_EnableHpf");
    return 0;
}

int IMP_AI_DisableHpf(void) {
    LOG_AUD("AI_DisableHpf");
    return 0;
}

int IMP_AI_EnableAgc(IMPAudioIOAttr *attr, IMPAudioAgcConfig config) {
    if (attr == NULL) return -1;
    LOG_AUD("AI_EnableAgc: target=%d, gain=%d", 
            config.TargetLevelDbfs, config.CompressionGaindB);
    return 0;
}

int IMP_AI_DisableAgc(void) {
    LOG_AUD("AI_DisableAgc");
    return 0;
}

/* Audio Encoder (AENC) Functions */

int IMP_AENC_RegisterEncoder(int *handle, IMPAudioEncEncoder *encoder) {
    if (handle == NULL || encoder == NULL) return -1;
    LOG_AUD("AENC_RegisterEncoder: %s", encoder->name);
    *handle = 100;  /* Dummy handle */
    return 0;
}

int IMP_AENC_UnRegisterEncoder(int *handle) {
    if (handle == NULL) return -1;
    LOG_AUD("AENC_UnRegisterEncoder");
    return 0;
}

int IMP_AENC_CreateChn(int aeChn, IMPAudioEncChnAttr *attr) {
    if (attr == NULL) return -1;
    LOG_AUD("AENC_CreateChn: chn=%d, type=%d", aeChn, attr->type);
    return 0;
}

int IMP_AENC_DestroyChn(int aeChn) {
    LOG_AUD("AENC_DestroyChn: chn=%d", aeChn);
    return 0;
}

int IMP_AENC_SendFrame(int aeChn, IMPAudioFrame *frame) {
    if (frame == NULL) return -1;
    return 0;
}

int IMP_AENC_PollingStream(int aeChn, uint32_t timeoutMs) {
    /* Return timeout */
    return -1;
}

int IMP_AENC_GetStream(int aeChn, IMPAudioStream *stream, IMPBlock block) {
    if (stream == NULL) return -1;
    /* No stream available */
    return -1;
}

int IMP_AENC_ReleaseStream(int aeChn, IMPAudioStream *stream) {
    if (stream == NULL) return -1;
    return 0;
}

/* Audio Decoder (ADEC) Functions */

int IMP_ADEC_RegisterDecoder(int *handle, IMPAudioDecDecoder *decoder) {
    if (handle == NULL || decoder == NULL) return -1;
    LOG_AUD("ADEC_RegisterDecoder: %s", decoder->name);
    *handle = 200;  /* Dummy handle */
    return 0;
}

int IMP_ADEC_UnRegisterDecoder(int *handle) {
    if (handle == NULL) return -1;
    LOG_AUD("ADEC_UnRegisterDecoder");
    return 0;
}

int IMP_ADEC_CreateChn(int adChn, IMPAudioDecChnAttr *attr) {
    if (attr == NULL) return -1;
    LOG_AUD("ADEC_CreateChn: chn=%d, type=%d", adChn, attr->type);
    return 0;
}

int IMP_ADEC_DestroyChn(int adChn) {
    LOG_AUD("ADEC_DestroyChn: chn=%d", adChn);
    return 0;
}

int IMP_ADEC_SendStream(int adChn, IMPAudioStream *stream, IMPBlock block) {
    if (stream == NULL) return -1;
    return 0;
}

int IMP_ADEC_GetStream(int adChn, IMPAudioStream *stream, IMPBlock block) {
    if (stream == NULL) return -1;
    /* No stream available */
    return -1;
}

int IMP_ADEC_ReleaseStream(int adChn, IMPAudioStream *stream) {
    if (stream == NULL) return -1;
    return 0;
}

