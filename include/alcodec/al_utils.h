#ifndef OPENIMP_ALCODEC_AL_UTILS_H
#define OPENIMP_ALCODEC_AL_UTILS_H

#include <stdint.h>
#include "al_types.h"

int32_t AL_Fifo_Init(int32_t *arg1, int32_t arg2);
void AL_Fifo_Deinit(int32_t *arg1);
int32_t AL_Fifo_Queue(int32_t *arg1, void *arg2, int32_t arg3);
void *AL_Fifo_Dequeue(int32_t *arg1, int32_t arg2);

int32_t AL_GetRequiredLevel(uint32_t arg1, const uint32_t *arg2, int32_t arg3);
int32_t GetBlk64x64(int32_t arg1, int32_t arg2);
int32_t GetBlk32x32(int32_t arg1, int32_t arg2);
int32_t GetBlk16x16(int32_t arg1, int32_t arg2);
int32_t GetPcmVclNalSize(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4);
int32_t Hevc_GetMaxVclNalSize(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4);
int32_t AL_GetMaxNalSize(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4,
                         int32_t arg5, int32_t arg6, int32_t arg7);
int32_t AL_GetMitigatedMaxNalSize(int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4);
AL_THardwareDriver *AL_GetHardwareDriver(void);
void AL_HDRSEIs_Reset(uint8_t *arg1);

#endif
