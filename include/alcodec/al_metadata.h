#ifndef OPENIMP_ALCODEC_AL_METADATA_H
#define OPENIMP_ALCODEC_AL_METADATA_H

#include <stdint.h>
#include "al_types.h"

uint32_t AL_StreamMetaData_AddSection(void *metadata, int32_t offset, int32_t length, int32_t handle, int32_t user_data, int32_t flags);
int32_t AL_StreamMetaData_ChangeSection(void *metadata, int32_t section_id, int32_t offset, int32_t length, int32_t handle, int32_t user_data);
void *AL_StreamMetaData_SetSectionFlags(void *metadata, int32_t section_id, int32_t flags);
void AL_StreamMetaData_ClearAllSections(void *metadata);
void *AL_StreamMetaData_Clone(void);
void *AL_StreamMetaData_Create(void);
int32_t AL_StreamMetaData_AddSeiSection(void);
int32_t AL_StreamMetaData_GetUnusedStreamPart(void);

#endif
