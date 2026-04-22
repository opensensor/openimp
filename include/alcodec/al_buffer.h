#ifndef OPENIMP_ALCODEC_AL_BUFFER_H
#define OPENIMP_ALCODEC_AL_BUFFER_H

#include <stdint.h>
#include "al_types.h"

void *AL_Buffer_GetData(AL_TBuffer *buffer);
void *AL_Buffer_GetMetaData(AL_TBuffer *buffer, int32_t meta_type);
uint32_t AL_Buffer_GetSize(AL_TBuffer *buffer);
uint32_t AL_Buffer_GetSizeChunk(AL_TBuffer *buffer, int32_t chunk_idx);
uint32_t AL_Buffer_GetPhysicalAddressChunk(AL_TBuffer *buffer, int32_t chunk_idx);
void *AL_Buffer_GetUserData(AL_TBuffer *buffer);
void AL_Buffer_SetUserData(AL_TBuffer *buffer, void *user_data);
void AL_Buffer_Destroy(AL_TBuffer *buffer);

#endif
