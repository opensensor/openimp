#ifndef __IMP_DMIC_H__
#define __IMP_DMIC_H__

#include <stdint.h>
#include "imp_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DMIC_SAMPLE_RATE_8000 = 8000,
    DMIC_SAMPLE_RATE_16000 = 16000,
} IMPDmicSampleRate;

typedef enum {
    DMIC_BIT_WIDTH_16 = 16,
} IMPDmicBitWidth;

typedef enum {
    DMIC_SOUND_MODE_MONO = 1,
    DMIC_SOUND_MODE_STEREO = 2,
} IMPDmicSoundMode;

typedef struct {
    IMPDmicSampleRate samplerate;
    IMPDmicBitWidth bitwidth;
    IMPDmicSoundMode soundmode;
    int frmNum;
    int numPerFrm;
    int chnCnt;
} IMPDmicAttr;

typedef struct {
    IMPDmicBitWidth bitwidth;
    IMPDmicSoundMode soundmode;
    uint32_t *virAddr;
    uint32_t phyAddr;
    int64_t timeStamp;
    int seq;
    int len;
} IMPDmicFrame;

typedef struct {
    IMPDmicFrame rawFrame;
    IMPDmicFrame aecFrame;
} IMPDmicChnFrame;

typedef struct {
    int usrFrmDepth;
    int Rev;
} IMPDmicChnParam;

int IMP_DMIC_SetUserInfo(int dmicDevId, int aecDmicId, int need_aec);
int IMP_DMIC_SetPubAttr(int dmicDevId, IMPDmicAttr *attr);
int IMP_DMIC_GetPubAttr(int dmicDevId, IMPDmicAttr *attr);
int IMP_DMIC_Enable(int dmicDevId);
int IMP_DMIC_Disable(int dmicDevId);
int IMP_DMIC_EnableChn(int dmicDevId, int dmicChnId);
int IMP_DMIC_DisableChn(int dmicDevId, int dmicChnId);
int IMP_DMIC_SetChnParam(int dmicDevId, int dmicChnId, IMPDmicChnParam *chnParam);
int IMP_DMIC_GetChnParam(int dmicDevId, int dmicChnId, IMPDmicChnParam *chnParam);
int IMP_DMIC_GetFrame(int dmicDevId, int dmicChnId, IMPDmicChnFrame *chnFrm, IMPBlock block);
int IMP_DMIC_ReleaseFrame(int dmicDevId, int dmicChnId, IMPDmicChnFrame *chnFrm);
int IMP_DMIC_PollingFrame(int dmicDevId, int dmicChnId, unsigned int timeout_ms);
int IMP_DMIC_SetVol(int dmicDevId, int dmicChnId, int dmicVol);
int IMP_DMIC_GetVol(int dmicDevId, int dmicChnId, int *dmicVol);
int IMP_DMIC_SetGain(int dmicDevId, int dmicChnId, int dmicGain);
int IMP_DMIC_GetGain(int dmicDevId, int dmicChnId, int *dmicGain);
int IMP_DMIC_EnableAec(int dmicDevId, int dmicChnId, int aoDevId, int aoChId);
int IMP_DMIC_DisableAec(int dmicDevId, int dmicChnId);
int IMP_DMIC_EnableAecRefFrame(int dmicDevId, int dmicChnId, int audioAoDevId, int aoChn);
int IMP_DMIC_DisableAecRefFrame(int dmicDevId, int dmicChnId);
int IMP_DMIC_GetFrameAndRef(int dmicDevId, int dmicChnId, IMPDmicChnFrame *chnFrm, IMPDmicFrame *ref, IMPBlock block);

#ifdef __cplusplus
}
#endif

#endif /* __IMP_DMIC_H__ */
