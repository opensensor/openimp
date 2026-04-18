#ifndef OPENIMP_ALCODEC_AL_SETTINGS_H
#define OPENIMP_ALCODEC_AL_SETTINGS_H

#include <stdint.h>
#include "al_types.h"

int32_t AL_AVC_GetLevelFromBitrate(void);
int32_t AL_HEVC_GetLevelFromBitrate(void);
int32_t AL_AVC_GetMaxNumberOfSlices(void);
int32_t AL_DPBConstraint_GetMaxDPBSize(void);
int32_t AL_GetRequiredLevel(void);
int32_t AL_GetBitDepth(void);
int32_t AL_Dump_TEncSettings(void);
int32_t AL_Dump_TRCParam(void);

#endif
