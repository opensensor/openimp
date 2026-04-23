#ifndef OPENIMP_ALCODEC_AL_SETTINGS_H
#define OPENIMP_ALCODEC_AL_SETTINGS_H

#include <stdint.h>
#include "al_types.h"

int32_t AL_AVC_GetLevelFromBitrate(void);
int32_t AL_HEVC_GetLevelFromBitrate(void);
int32_t AL_AVC_GetMaxNumberOfSlices(void);
int32_t AL_DPBConstraint_GetMaxDPBSize(void);
int32_t AL_GetRequiredLevel(void);
int32_t AL_Dump_TEncSettings(AL_TEncSettings *arg1);
int32_t AL_Dump_TRCParam(AL_TRCParam *arg1);
int32_t AL_Dump_TGopParam(AL_TGopParam *arg1);
int32_t AL_Dump_TEncChanParam(AL_TEncChanParam *arg1);
const char *AL_Encoder_ErrorToString(int32_t arg1);

#endif
